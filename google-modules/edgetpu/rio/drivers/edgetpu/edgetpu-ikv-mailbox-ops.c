// SPDX-License-Identifier: GPL-2.0-only
/*
 * GCIP Mailbox Ops for the in-kernel VII mailbox
 *
 * Copyright (C) 2024-2025 Google LLC
 */

#include <linux/atomic.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>

#include <gcip/gcip-fence-array.h>
#include <gcip/gcip-mailbox.h>
#include <iif/iif-dma-fence.h>
#include <iif/iif-shared.h>

#include "edgetpu-firmware.h"
#include "edgetpu-ikv-mailbox-ops.h"
#include "edgetpu-ikv.h"
#include "edgetpu-iremap-pool.h"
#include "edgetpu-kci.h"
#include "edgetpu-mailbox.h"
#include "edgetpu-vii-packet.h"

static u32 edgetpu_ikv_get_cmd_queue_head(struct gcip_mailbox *mailbox)
{
	struct edgetpu_ikv *ikv = gcip_mailbox_get_data(mailbox);
	struct edgetpu_mailbox *mbx_hw = ikv->mbx_hardware;

	return EDGETPU_MAILBOX_CMD_QUEUE_READ(mbx_hw, head);
}

/* In-Kernel VII gcip_mailbox_ops */

static u32 edgetpu_ikv_get_cmd_queue_tail(struct gcip_mailbox *mailbox)
{
	struct edgetpu_ikv *ikv = gcip_mailbox_get_data(mailbox);
	struct edgetpu_mailbox *mbx_hw = ikv->mbx_hardware;

	return mbx_hw->cmd_queue_tail;
}

static void edgetpu_ikv_inc_cmd_queue_tail(struct gcip_mailbox *mailbox, u32 inc)
{
	struct edgetpu_ikv *ikv = gcip_mailbox_get_data(mailbox);
	struct edgetpu_mailbox *mbx_hw = ikv->mbx_hardware;

	edgetpu_mailbox_inc_cmd_queue_tail(mbx_hw, inc);
}

static int edgetpu_ikv_acquire_cmd_queue_lock(struct gcip_mailbox *mailbox, bool try, bool *atomic)
{
	struct edgetpu_ikv *ikv = gcip_mailbox_get_data(mailbox);

	*atomic = false;
	mutex_lock(&ikv->cmd_queue_lock);
	return 1;
}

static void edgetpu_ikv_release_cmd_queue_lock(struct gcip_mailbox *mailbox)
{
	struct edgetpu_ikv *ikv = gcip_mailbox_get_data(mailbox);

	mutex_unlock(&ikv->cmd_queue_lock);
}

static u64 edgetpu_ikv_get_cmd_elem_seq(struct gcip_mailbox *mailbox, void *cmd)
{
	return edgetpu_vii_command_get_seq_number(cmd);
}

static void edgetpu_ikv_set_cmd_elem_seq(struct gcip_mailbox *mailbox, void *cmd, u64 seq)
{
	edgetpu_vii_command_set_seq_number(cmd, seq);
}

static u32 edgetpu_ikv_get_resp_queue_size(struct gcip_mailbox *mailbox)
{
	struct edgetpu_ikv *ikv = gcip_mailbox_get_data(mailbox);
	struct edgetpu_mailbox *mbx_hw = ikv->mbx_hardware;

	return mbx_hw->resp_queue_size;
}

static u32 edgetpu_ikv_get_resp_queue_head(struct gcip_mailbox *mailbox)
{
	struct edgetpu_ikv *ikv = gcip_mailbox_get_data(mailbox);
	struct edgetpu_mailbox *mbx_hw = ikv->mbx_hardware;

	return mbx_hw->resp_queue_head;
}

static u32 edgetpu_ikv_get_resp_queue_tail(struct gcip_mailbox *mailbox)
{
	struct edgetpu_ikv *ikv = gcip_mailbox_get_data(mailbox);
	struct edgetpu_mailbox *mbx_hw = ikv->mbx_hardware;

	return EDGETPU_MAILBOX_RESP_QUEUE_READ_SYNC(mbx_hw, tail);
}

static void edgetpu_ikv_inc_resp_queue_head(struct gcip_mailbox *mailbox, u32 inc)
{
	struct edgetpu_ikv *ikv = gcip_mailbox_get_data(mailbox);
	struct edgetpu_mailbox *mbx_hw = ikv->mbx_hardware;

	edgetpu_mailbox_inc_resp_queue_head(mbx_hw, inc);
}

static int edgetpu_ikv_acquire_resp_queue_lock(struct gcip_mailbox *mailbox, bool try, bool *atomic)
{
	struct edgetpu_ikv *ikv = gcip_mailbox_get_data(mailbox);
	unsigned long flags;
	int ret;

	*atomic = true;

	if (try) {
		ret = spin_trylock_irqsave(&ikv->resp_queue_lock, flags);
	} else {
		spin_lock_irqsave(&ikv->resp_queue_lock, flags);
		ret = 1;
	}

	if (ret)
		ikv->resp_queue_lock_flags = flags;

	return ret;
}

static void edgetpu_ikv_release_resp_queue_lock(struct gcip_mailbox *mailbox)
{
	struct edgetpu_ikv *ikv = gcip_mailbox_get_data(mailbox);

	spin_unlock_irqrestore(&ikv->resp_queue_lock, ikv->resp_queue_lock_flags);
}

static u64 edgetpu_ikv_get_resp_elem_seq(struct gcip_mailbox *mailbox, void *resp)
{
	return edgetpu_vii_response_get_seq_number(resp);
}

static void edgetpu_ikv_set_resp_elem_seq(struct gcip_mailbox *mailbox, void *resp, u64 seq)
{
	edgetpu_vii_response_set_seq_number(resp, seq);
}

static int edgetpu_ikv_wait_for_cmd_queue_not_full(struct gcip_mailbox *mailbox)
{
	struct edgetpu_ikv *ikv = gcip_mailbox_get_data(mailbox);
	u32 tail = edgetpu_ikv_get_cmd_queue_tail(mailbox);
	int ret;

	if (edgetpu_ikv_get_cmd_queue_head(mailbox) != (tail ^ mailbox->queue_wrap_bit))
		return 0;

	/* Credit enforcement should prevent this from ever happening. Log an error. */
	etdev_warn_ratelimited(ikv->etdev, "kernel VII command queue full\n");

	ret = wait_event_timeout(ikv->pending_commands,
				 edgetpu_ikv_get_cmd_queue_head(mailbox) !=
					 (tail ^ mailbox->queue_wrap_bit),
				 msecs_to_jiffies(mailbox->timeout));
	if (!ret)
		return -ETIMEDOUT;

	return 0;
}

static int edgetpu_ikv_before_enqueue_wait_list(struct gcip_mailbox *mailbox, void *resp,
						struct gcip_mailbox_awaiter *gcip_awaiter)
{
	struct edgetpu_ikv_response *ikv_resp;
	unsigned long flags;
	int ret;

	/*
	 * Since awaiters are only NULL for synchronous commands (which in-kernel VII does not
	 * support), there's no need to check it.
	 */
	ikv_resp = container_of(gcip_awaiter, struct edgetpu_ikv_response, gcip_awaiter);

	/*
	 * This function call is meaningful only for IIFs in the arrays.
	 *
	 * Submitting a waiter means that there is a request sent to the firmware which is waiting
	 * on the fence to be unblocked. Internally, it increments the number of outstanding waiters
	 * of the fence. Once the fence is unblocked and the request is processed, the number will
	 * be decremented back. Its purpose is to track whether it is possible to retire the fence.
	 *
	 * Submitting a signaler means that a request has been sent to the firmware which will
	 * signal the fence once it is processed. To avoid a deadlock, we don't allow submitting
	 * waiter commands earlier than signaler commands. The total number of expected signalers
	 * is decided when the fence is created and this function will decrement the number of
	 * remaining signalers to be submitted. If that number is non-zero, IIF will reject
	 * submitting waiter commands to the fence.
	 *
	 * After this function call, we should signal out-fences in any success or failure cases.
	 * That means if any error happens in the kernel driver side before submitting the command,
	 * the driver should error out-fences out. However, it is hard to do that if they are IIFs
	 * because the firmware must be the only one who can signal the fences (i.e., update IIF
	 * fence table) according to the IIF design. We can't simply set propagate flag to fences
	 * and call `signal()` function to signal fences and we need a special way of requesting the
	 * firmware for signaling out-fences which would be complicated to implement.
	 *
	 * For example, if we assume that there is a special command which can be sent to the
	 * firmware to ask for signaling out-fences when any error happnes in the kernel driver
	 * side, we can imagine a case that even preparing that command fails and we need another
	 * special way of requesting the firmware for signaling out-fences. There would be so many
	 * corner cases that we should consider.
	 *
	 * Therefore, to avoid that kind of situation as much as possible, intentionally call this
	 * function right before submitting the command to the firmware. Note that when this
	 * `enqueue_wait_list()` callback returns 0, it is guaranteed that the command will be
	 * submitted to the firmware and the kernel driver doesn't need to care signaling out-fences
	 * with an error caused in the driver side.
	 */
	ret = gcip_fence_array_submit_waiter_and_signaler(
		ikv_resp->in_fence_array, ikv_resp->out_fence_array, NULL, NULL, IIF_IP_TPU);
	if (ret) {
		dev_err(mailbox->dev, "Failed to submit waiter or signaler to fences, ret=%d", ret);
		return ret;
	}

	spin_lock_irqsave(ikv_resp->queue_lock, flags);
	list_add_tail(&ikv_resp->list_entry, ikv_resp->pending_queue);
	spin_unlock_irqrestore(ikv_resp->queue_lock, flags);

	return 0;
}

static int edgetpu_ikv_after_enqueue_cmd(struct gcip_mailbox *mailbox, void *cmd)
{
	struct edgetpu_ikv *ikv = gcip_mailbox_get_data(mailbox);

	EDGETPU_MAILBOX_CMD_QUEUE_WRITE_SYNC(ikv->mbx_hardware, doorbell_set, 1);

	return 0;
}

static void edgetpu_ikv_after_fetch_resps(struct gcip_mailbox *mailbox, u32 num_resps)
{
	struct edgetpu_ikv *ikv = gcip_mailbox_get_data(mailbox);
	u32 size = edgetpu_ikv_get_resp_queue_size(mailbox);
	/*
	 * We consumed a lot of responses - ring the doorbell of *cmd* queue to notify the firmware,
	 * which might be waiting us to consume the response queue.
	 */
	if (num_resps >= size / 2)
		EDGETPU_MAILBOX_CMD_QUEUE_WRITE(ikv->mbx_hardware, doorbell_set, 1);
}

const struct gcip_mailbox_ops ikv_mailbox_ops = {
	.get_tx_queue_tail = edgetpu_ikv_get_cmd_queue_tail,
	.inc_tx_queue_tail = edgetpu_ikv_inc_cmd_queue_tail,
	.acquire_tx_queue_lock = edgetpu_ikv_acquire_cmd_queue_lock,
	.release_tx_queue_lock = edgetpu_ikv_release_cmd_queue_lock,
	.get_cmd_elem_seq = edgetpu_ikv_get_cmd_elem_seq,
	.set_cmd_elem_seq = edgetpu_ikv_set_cmd_elem_seq,
	.get_rx_queue_size = edgetpu_ikv_get_resp_queue_size,
	.get_rx_queue_head = edgetpu_ikv_get_resp_queue_head,
	.get_rx_queue_tail = edgetpu_ikv_get_resp_queue_tail,
	.inc_rx_queue_head = edgetpu_ikv_inc_resp_queue_head,
	.acquire_rx_queue_lock = edgetpu_ikv_acquire_resp_queue_lock,
	.release_rx_queue_lock = edgetpu_ikv_release_resp_queue_lock,
	.get_resp_elem_seq = edgetpu_ikv_get_resp_elem_seq,
	.set_resp_elem_seq = edgetpu_ikv_set_resp_elem_seq,
	.wait_for_tx_queue_not_full = edgetpu_ikv_wait_for_cmd_queue_not_full,
	.before_enqueue_wait_list = edgetpu_ikv_before_enqueue_wait_list,
	.after_enqueue_cmd = edgetpu_ikv_after_enqueue_cmd,
	.after_fetch_resps = edgetpu_ikv_after_fetch_resps,
	/* .before_handle_resp is not needed */
};
