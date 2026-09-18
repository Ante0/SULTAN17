// SPDX-License-Identifier: GPL-2.0-only
/*
 * Virtual Inference Interface, implements the protocol between AP kernel and TPU firmware.
 *
 * Copyright (C) 2023-2025 Google LLC
 */

#include <linux/kthread.h>
#include <linux/limits.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include <gcip/gcip-fence-array.h>
#include <gcip/gcip-mailbox.h>
#include <gcip/gcip-memory.h>
#include <iif/iif-dma-fence.h>

#include "edgetpu-config.h"
#include "edgetpu-dt-mailbox-adapter.h"
#include "edgetpu-ikv-mailbox-ops.h"
#include "edgetpu-ikv.h"
#include "edgetpu-iremap-pool.h"
#include "edgetpu-kci.h"
#include "edgetpu-mailbox.h"
#include "edgetpu-pm.h"
#include "edgetpu-sw-watchdog.h"
#include "edgetpu-vii-litebuf.h"
#include "edgetpu-vii-packet.h"
#include "edgetpu.h"

static unsigned int user_ikv_timeout;
module_param(user_ikv_timeout, uint, 0660);

static void edgetpu_ikv_handle_irq(struct edgetpu_mailbox *mailbox)
{
	struct edgetpu_ikv *ikv = mailbox->internal.etikv;

	/*
	 * Process responses directly, to avoid the latency from scheduling a worker thread.
	 *
	 * Since the in-kernel VII `acquire_resp_queue_lock` op sets @atomic to true, the response
	 * processing function will be safe to call in an IRQ context.
	 */
	/* TODO(b/312098074) Rename this function to indicate it is not only called by workers */
	gcip_mailbox_consume_responses_work(ikv->mbx_protocol);
}

static int edgetpu_ikv_alloc_queue(struct edgetpu_ikv *etikv, enum gcip_mailbox_queue_type type)
{
	struct edgetpu_dev *etdev = etikv->etdev;
	u32 size;
	struct gcip_memory *mem;
	int ret;

	/* Allocate the queues based on the larger litebuf sizes which can handle both formats. */
	switch (type) {
	case GCIP_MAILBOX_CMD_QUEUE:
		size = etikv->queue_size * VII_CMD_SIZE_BYTES;
		mem = &etikv->cmd_queue_mem;
		break;
	case GCIP_MAILBOX_RESP_QUEUE:
		size = etikv->queue_size * VII_RESP_SIZE_BYTES;
		mem = &etikv->resp_queue_mem;
		break;
	}

	/*
	 * in-kernel VII is kernel-to-firmware communication, so its queues are allocated in the
	 * same context as KCI, despite being a separate protocol.
	 */
	ret = edgetpu_iremap_alloc(etdev, size, mem);
	if (ret)
		return ret;

	ret = edgetpu_mailbox_set_queue(etikv->mbx_hardware, type, mem->dma_addr,
					etikv->queue_size);
	if (ret) {
		etdev_err(etikv->etdev, "failed to set mailbox queue: %d", ret);
		edgetpu_iremap_free(etdev, mem);
		return ret;
	}

	return 0;
}

static void edgetpu_ikv_free_queue(struct edgetpu_ikv *etikv, enum gcip_mailbox_queue_type type)
{
	struct edgetpu_dev *etdev = etikv->etdev;

	switch (type) {
	case GCIP_MAILBOX_CMD_QUEUE:
		edgetpu_iremap_free(etdev, &etikv->cmd_queue_mem);
		break;
	case GCIP_MAILBOX_RESP_QUEUE:
		edgetpu_iremap_free(etdev, &etikv->resp_queue_mem);
		break;
	}
}

int edgetpu_ikv_init(struct edgetpu_dev *etdev, struct edgetpu_ikv *etikv)
{
	struct edgetpu_mailbox *mbx_hardware;
	const unsigned int timeout = user_ikv_timeout ? user_ikv_timeout : IKV_TIMEOUT;
	struct gcip_mailbox_args args = {
		.dev = etdev->dev,
		.mode = GCIP_MAILBOX_MODE_FORWARD | GCIP_MAILBOX_MODE_SEQ_EXTERNAL,
		.queue_wrap_bit = CIRC_QUEUE_WRAP_BIT,
		.tx_elem_size = edgetpu_vii_command_packet_size(),
		.rx_elem_size = edgetpu_vii_response_packet_size(),
		.timeout = timeout,
		.ops = &ikv_mailbox_ops,
		.data = etikv,
	};
	int ret;

	etikv->queue_size = etdev->max_concurrent_clients * EDGETPU_NUM_VII_CREDITS_PER_CLIENT;
	etikv->command_timeout_ms = timeout;
	etikv->etdev = etdev;
	mutex_init(&etikv->enabled_pasids_lock);

	mbx_hardware = edgetpu_mailbox_ikv(etdev);
	if (IS_ERR_OR_NULL(mbx_hardware))
		return !mbx_hardware ? -ENODEV : PTR_ERR(mbx_hardware);
	edgetpu_mailbox_set_irq_handler(mbx_hardware, edgetpu_ikv_handle_irq);
	mbx_hardware->internal.etikv = etikv;
	etikv->mbx_hardware = mbx_hardware;

	etikv->mbx_protocol = devm_kzalloc(etdev->dev, sizeof(*etikv->mbx_protocol), GFP_KERNEL);
	if (!etikv->mbx_protocol) {
		ret = -ENOMEM;
		goto err;
	}

	edgetpu_mailbox_disable_doorbells(mbx_hardware);
	edgetpu_mailbox_clear_doorbells(mbx_hardware);

	ret = edgetpu_ikv_alloc_queue(etikv, GCIP_MAILBOX_CMD_QUEUE);
	if (ret)
		goto err;
	mutex_init(&etikv->cmd_queue_lock);

	ret = edgetpu_ikv_alloc_queue(etikv, GCIP_MAILBOX_RESP_QUEUE);
	if (ret)
		goto err;
	spin_lock_init(&etikv->resp_queue_lock);

	args.tx_queue = etikv->cmd_queue_mem.virt_addr;
	args.rx_queue = etikv->resp_queue_mem.virt_addr;
	ret = gcip_mailbox_init(etikv->mbx_protocol, &args);
	if (ret)
		goto err;

	init_waitqueue_head(&etikv->pending_commands);

	edgetpu_mailbox_init_doorbells(mbx_hardware);
	edgetpu_mailbox_enable(mbx_hardware);

	return 0;

err:
	/* The release handler only cleans up resources that were successfully initialized. */
	edgetpu_ikv_release(etdev, etikv);

	return ret;
}

int edgetpu_ikv_reinit(struct edgetpu_ikv *etikv)
{
	struct edgetpu_mailbox *mbx_hardware = etikv->mbx_hardware;
	struct gcip_memory *cmd_queue_mem = &etikv->cmd_queue_mem;
	struct gcip_memory *resp_queue_mem = &etikv->resp_queue_mem;
	int ret;

	edgetpu_mailbox_disable_doorbells(mbx_hardware);
	edgetpu_mailbox_clear_doorbells(mbx_hardware);

	ret = edgetpu_mailbox_set_queue(mbx_hardware, GCIP_MAILBOX_CMD_QUEUE,
					cmd_queue_mem->dma_addr, etikv->queue_size);
	if (ret)
		return ret;

	ret = edgetpu_mailbox_set_queue(mbx_hardware, GCIP_MAILBOX_RESP_QUEUE,
					resp_queue_mem->dma_addr, etikv->queue_size);
	if (ret)
		return ret;

	/* Restore irq handler */
	edgetpu_mailbox_set_irq_handler(mbx_hardware, edgetpu_ikv_handle_irq);

	edgetpu_mailbox_init_doorbells(mbx_hardware);
	edgetpu_mailbox_enable(mbx_hardware);

	return 0;
}

void edgetpu_ikv_release(struct edgetpu_dev *etdev, struct edgetpu_ikv *etikv)
{
	struct edgetpu_mailbox *mbx_hardware;

	if (!etikv)
		return;

	mbx_hardware = etikv->mbx_hardware;
	etikv->mbx_hardware = NULL;

	/* Before anything else, remove any IRQ handler to stop responding to interrupts. */
	if (mbx_hardware)
		edgetpu_mailbox_set_irq_handler(mbx_hardware, NULL);

	/*
	 * edgetpu_iremap_free() should be a safe no-op if the memory passed in was never
	 * allocated, but double check here to be safe.
	 */
	if (etikv->resp_queue_mem.virt_addr)
		edgetpu_ikv_free_queue(etikv, GCIP_MAILBOX_RESP_QUEUE);
	if (etikv->cmd_queue_mem.virt_addr)
		edgetpu_ikv_free_queue(etikv, GCIP_MAILBOX_CMD_QUEUE);
	if (mbx_hardware)
		edgetpu_mailbox_release(mbx_hardware);
	if (etikv->mbx_protocol)
		gcip_mailbox_release(etikv->mbx_protocol);
}

int edgetpu_ikv_activate_client(struct edgetpu_ikv *etikv, u32 pasid, u32 client_priv, u16 vcid,
				bool first_open)
{
	struct edgetpu_dev *etdev = etikv->etdev;
	u32 mailbox_map = BIT(pasid);
	bool first_party_client;
	int ret;

	/* TODO(b/271938964) ALLOCATE_VMBOX only has a u8 for storing VCID. */
	if (vcid > U8_MAX) {
		etdev_err(etdev, "VCID too large to use (vcid=%#x, vcid_pool=%#0x)\n", vcid,
			  etdev->vcid_pool);
		return -EINVAL;
	}

	/*
	 * While `client_priv` is a u32, it comes from `edgetpu_mailbox_attr` where it is defined
	 * as only being used as 1-bit bitfield, despite being a 32-bit value. As long as it's not
	 * 0, it indicates the client is first-party.
	 */
	first_party_client = client_priv != 0;

	mutex_lock(&etikv->enabled_pasids_lock);
	/* TODO(b/267978887) Finalize `client_id` field format */
	ret = edgetpu_kci_allocate_vmbox(etdev->etkci, pasid, (u8)vcid, first_open,
					 first_party_client);
	if (!ret)
		etikv->enabled_pasids |= mailbox_map;
	mutex_unlock(&etikv->enabled_pasids_lock);
	if (ret == -ETIMEDOUT) {
		edgetpu_watchdog_bite(etdev);
	} else if (ret > 0) {
		etdev_err(etdev, "allocate mailbox for VCID %d failed with firmware error %d", vcid,
			  ret);
		ret = -EBADMSG;
	}

	return ret;
}

void edgetpu_ikv_deactivate_client(struct edgetpu_ikv *etikv, u32 pasid)
{
	struct edgetpu_dev *etdev = etikv->etdev;
	u32 mailbox_map = BIT(pasid);

	mutex_lock(&etikv->enabled_pasids_lock);
	/* TODO(b/267978887) Finalize `client_id` field format */
	if (mailbox_map & etikv->enabled_pasids) {
		edgetpu_kci_release_vmbox(etdev->etkci, pasid);

		/*
		 * Now that firmware has acknowledged the PASID's closure and flushed all in-flight
		 * IKV commands, the IKV response queue must be flushed to ensure no stale packets
		 * meant for this PASID are incorrectly consumed by a future client that recycles
		 * this PASID.
		 */
		edgetpu_ikv_flush_responses(etdev->etikv);
	}

	etikv->enabled_pasids &= ~mailbox_map;
	mutex_unlock(&etikv->enabled_pasids_lock);
}

void edgetpu_ikv_clear_active_clients(struct edgetpu_ikv *etikv)
{
	mutex_lock(&etikv->enabled_pasids_lock);
	etikv->enabled_pasids = 0;
	mutex_unlock(&etikv->enabled_pasids_lock);
}

static void edgetpu_ikv_response_release(struct gcip_mailbox_awaiter *gcip_awaiter)
{
	struct edgetpu_ikv_response *ikv_resp =
		container_of(gcip_awaiter, struct edgetpu_ikv_response, gcip_awaiter);

	/*
	 * If the command was using an iif_dma_fence to proxy a dma in-fence with an IIF,
	 * stop the thread waiting on the dma fence if it is still running.
	 */
	if (ikv_resp->iif_dma_fence)
		iif_dma_fence_stop_and_put_async(ikv_resp->iif_dma_fence);

	edgetpu_ikv_additional_info_free(ikv_resp->etikv->etdev, &ikv_resp->additional_info);
	if (ikv_resp->release_callback)
		ikv_resp->release_callback(ikv_resp->release_data);
	kfree(ikv_resp->resp);
	kfree(ikv_resp);
}

/*
 * Helper function with shared logic needed for notifying awaiters when a command has a response
 * ready or is being flushed.
 */
static void signal_response_waiters(struct edgetpu_ikv_response *ikv_resp, int error,
				    bool notify_group)
{
	/* Refund the credit before notifying any waiters in case they send another command. */
	if (ikv_resp->group_to_notify)
		atomic_inc(&ikv_resp->group_to_notify->available_vii_credits);

	/*
	 * If @error is non-zero, it means that the response was not returned from the firmware and
	 * it is an error response generated by the driver. Therefore, kernel drivers should take
	 * care of the error propagation on behalf of the firmware.
	 */
	if (error && ikv_resp->out_fence_array)
		gcip_fence_array_iif_set_propagate_unblock(ikv_resp->out_fence_array);

	/*
	 * Signal fences before notifying the group of the response.
	 * It is likely the user-space client that owns the group will need any drivers waiting on
	 * the command to finish their work before user-space can make use of the results.
	 */
	gcip_fence_array_signal_async(ikv_resp->out_fence_array, error);

	/*
	 * This function call is meaningful only when the fences are inter-IP fences.
	 * It will decrement the number of outstanding waiters of each fence. Once that becomes
	 * 0 and the fence file is closed, it means that no one is referring to the fence and it
	 * will try to retire the fence.
	 */
	gcip_fence_array_waited_async(ikv_resp->in_fence_array, IIF_IP_TPU);
	gcip_fence_array_put_async(ikv_resp->out_fence_array);
	gcip_fence_array_put_async(ikv_resp->in_fence_array);

	if (ikv_resp->group_to_notify && notify_group)
		edgetpu_group_notify(ikv_resp->group_to_notify, EDGETPU_EVENT_RESPDATA);
}

/*
 * Helper function to prepare a ready response, move it to its destination queue, and signal
 * any waiters for the response.
 *
 * If @resp_code or @resp_retval are provided, the `code` and `retval` fields of @ikv_resp's
 * internal VII response will be overridden with the values they point to before the response
 * is placed in is destination queue.
 *
 * If @force is true, the function will process the response regardless of @ikv_resp->processed.
 */
static void edgetpu_ikv_process_response(struct edgetpu_ikv_response *ikv_resp, u16 *resp_code,
					 u64 *resp_retval, int fence_error, bool force)
{
	unsigned long flags;

	spin_lock_irqsave(ikv_resp->queue_lock, flags);

	/*
	 * Return immediately if either of the following caused the response to be "processed":
	 * - the response timed-out
	 * - the queue waiting for the response is being released
	 */
	if (ikv_resp->processed && !force) {
		spin_unlock_irqrestore(ikv_resp->queue_lock, flags);
		return;
	}
	ikv_resp->processed = true;

	/* If the command resulted in an error, override the error code and retval as provided. */
	if (resp_code)
		edgetpu_vii_response_set_code(ikv_resp->resp, *resp_code);
	if (resp_retval)
		edgetpu_vii_response_set_retval(ikv_resp->resp, *resp_retval);

	/* Set the response sequence number to the value expected by the client. */
	edgetpu_vii_response_set_seq_number(ikv_resp->resp, ikv_resp->client_seq);

	/*
	 * Move the response from the "pending" list to the "ready" list.
	 *
	 * It's necessary to check if the response was actually in the "pending" list, since a
	 * command that was canceled before it was ever enqueued in the mailbox will have a
	 * floating response.
	 */
	if (ikv_resp->list_entry.prev)
		list_del(&ikv_resp->list_entry);
	list_add_tail(&ikv_resp->list_entry, ikv_resp->dest_queue);

	/*
	 * Hold a reference to the awaiter. This prevents the response object from being freed by
	 * the queue consumer before we finish signaling the response waiters.
	 */
	gcip_mailbox_awaiter_get(&ikv_resp->gcip_awaiter);

	spin_unlock_irqrestore(ikv_resp->queue_lock, flags);

	signal_response_waiters(ikv_resp, fence_error, true);
	gcip_mailbox_awaiter_put(&ikv_resp->gcip_awaiter);
}

static void edgetpu_ikv_response_handle_arrived(struct gcip_mailbox_awaiter *gcip_awaiter)
{
	struct edgetpu_ikv_response *ikv_resp =
		container_of(gcip_awaiter, struct edgetpu_ikv_response, gcip_awaiter);
	int fence_error = 0;

	/* Signal any out-fences with an error if the response has any error code. */
	if (edgetpu_vii_response_get_code(ikv_resp->resp))
		fence_error = -EIO;

	edgetpu_ikv_process_response(ikv_resp, NULL, NULL, fence_error, false);
}

static void edgetpu_ikv_response_handle_timedout(struct gcip_mailbox_awaiter *gcip_awaiter)
{
	struct edgetpu_ikv_response *ikv_resp =
		container_of(gcip_awaiter, struct edgetpu_ikv_response, gcip_awaiter);
	struct edgetpu_ikv *ikv = ikv_resp->etikv;
	u16 code = VII_RESPONSE_CODE_KERNEL_CMD_TIMEOUT;
	u64 data = (u64)ikv->command_timeout_ms;

	etdev_warn(ikv->etdev, "IKV seq %llu timed out", ikv_resp->client_seq);
	edgetpu_firmware_log_state(ikv->etdev);
	edgetpu_ikv_process_response(ikv_resp, &code, &data, -ETIMEDOUT, false);
}

static void edgetpu_ikv_response_handle_flushed(struct gcip_mailbox_awaiter *gcip_awaiter)
{
	struct edgetpu_ikv_response *ikv_resp =
		container_of(gcip_awaiter, struct edgetpu_ikv_response, gcip_awaiter);
	/* Store an independent pointer to `queue_lock`, since `resp` may be released. */
	spinlock_t *queue_lock = ikv_resp->queue_lock;
	unsigned long flags;

	spin_lock_irqsave(queue_lock, flags);

	if (ikv_resp->processed) {
		spin_unlock_irqrestore(queue_lock, flags);
		return;
	}
	ikv_resp->processed = true;

	spin_unlock_irqrestore(queue_lock, flags);

	/* Signal any out-fence, but skip the device group since it's being flushed. */
	signal_response_waiters(ikv_resp, -ECANCELED, false);

	gcip_mailbox_awaiter_put(gcip_awaiter);
}

static u32 edgetpu_ikv_response_get_timeout(struct gcip_mailbox_awaiter *gcip_awaiter)
{
#if EDGETPU_USE_LITEBUF_VII
	/* Disable timeout, firmware that implements litebuf VII tracks timeouts in firmware. */
	return GCIP_MAILBOX_AWAITER_TIMEOUT_NONE;
#else
	return user_ikv_timeout ? user_ikv_timeout : IKV_TIMEOUT;
#endif
}

static bool edgetpu_ikv_response_match_response(void *incoming_resp, void *waiter_resp)
{
	u32 incoming_client_id, waiter_client_id;
	u64 incoming_seq_number, waiter_seq_number;

	/* Awaiters only have the local client_id set. Mask away realm and VM ID bits. */
	incoming_client_id = edgetpu_vii_response_get_client_id(incoming_resp) & 0xFFFF;
	waiter_client_id = edgetpu_vii_response_get_client_id(waiter_resp) & 0xFFFF;

	incoming_seq_number = edgetpu_vii_response_get_seq_number(incoming_resp);
	waiter_seq_number = edgetpu_vii_response_get_seq_number(waiter_resp);

	return incoming_seq_number == waiter_seq_number && incoming_client_id == waiter_client_id;
}

const struct gcip_mailbox_awaiter_ops edgetpu_ikv_response_ops = {
	.release = edgetpu_ikv_response_release,
	.handle_arrived = edgetpu_ikv_response_handle_arrived,
	.handle_timedout = edgetpu_ikv_response_handle_timedout,
	.handle_flushed = edgetpu_ikv_response_handle_flushed,
	.get_timeout = edgetpu_ikv_response_get_timeout,
	.match_response = edgetpu_ikv_response_match_response,
};

struct send_cmd_args {
	struct edgetpu_ikv *etikv;
	struct edgetpu_ikv_response *ikv_resp;
	struct dma_fence *fence;
	void *cmd;
};

static int do_send_cmd(struct send_cmd_args *args)
{
	struct edgetpu_ikv *etikv = args->etikv;
	void *cmd = args->cmd;
	struct edgetpu_ikv_response *ikv_resp = args->ikv_resp;

	return gcip_mailbox_send_cmd_async(etikv->mbx_protocol, cmd, &ikv_resp->gcip_awaiter);
}

/* TODO(b/274528886) Finalize timeout value. Set to 10 seconds for now. */
#define VII_IN_FENCE_TIMEOUT_MS 10000

static int send_cmd_thread_fn(void *data)
{
	struct send_cmd_args *args = (struct send_cmd_args *)data;
	/* Save a pointer to the group so it can untrack this task, even if ikv_resp is freed. */
	struct edgetpu_device_group *group_to_notify = args->ikv_resp->group_to_notify;
	int ret, fence_status;
	u16 resp_code;
	u64 resp_data;

	ret = dma_fence_wait_timeout(args->fence, true, msecs_to_jiffies(VII_IN_FENCE_TIMEOUT_MS));
	fence_status = dma_fence_get_status(args->fence);
	dma_fence_put(args->fence);

	/* If the wait was interrupted to kill the thread, then the command is abandoned. */
	if (kthread_should_stop()) {
		/* The command will never be sent at this point so its response must be released. */
		gcip_fence_array_put(args->ikv_resp->out_fence_array);
		gcip_fence_array_put(args->ikv_resp->in_fence_array);
		edgetpu_ikv_additional_info_free(args->ikv_resp->etikv->etdev,
						 &args->ikv_resp->additional_info);
		if (args->ikv_resp->release_callback)
			args->ikv_resp->release_callback(args->ikv_resp->release_data);

		kfree(args->ikv_resp->resp);
		kfree(args->ikv_resp);
		goto out_free_args;
	}

	/* If the wait ended due to a timeout or fence error, enqueue an error response. */
	if (!ret || fence_status < 0) {
		etdev_err(
			args->etikv->etdev,
			"Waiting for client_id=%u's command in-fence failed (ret=%d fence_status=%d)",
			edgetpu_vii_command_get_client_id(args->cmd), ret, fence_status);
		if (!ret) {
			resp_code = VII_RESPONSE_CODE_KERNEL_FENCE_TIMEOUT;
			resp_data = VII_IN_FENCE_TIMEOUT_MS;
			fence_status = -ETIMEDOUT;
		} else {
			resp_code = VII_RESPONSE_CODE_KERNEL_FENCE_ERROR;
			resp_data = (u64)fence_status;
			/* Do not override fence_status, let the error propagate. */
		}
		goto err_send_error_resp;
	}

	ret = do_send_cmd(args);
	if (ret) {
		etdev_err(args->etikv->etdev,
			  "Failed to send command in fence thread for client_id=%u (ret=%d)",
			  edgetpu_vii_command_get_client_id(args->cmd), ret);
		resp_code = VII_RESPONSE_CODE_KERNEL_ENQUEUE_FAILED;
		resp_data = (u64)ret;
		fence_status = -ECANCELED;
		goto err_send_error_resp;
	}

	goto out_untrack;

err_send_error_resp:
	/*
	 * Notify the IIF driver that the signaler of the out_fence_array was "submitted" so that
	 * any IIF out-fences can be signaled when processing the error response.
	 */
	ret = gcip_fence_array_submit_waiter_and_signaler(args->ikv_resp->in_fence_array,
							  args->ikv_resp->out_fence_array, NULL,
							  NULL, IIF_IP_TPU);
	if (ret)
		etdev_err(
			args->etikv->etdev,
			"Failed to submit signaler with errored in-fence for client_id=%u (ret=%d)",
			edgetpu_vii_command_get_client_id(args->cmd), ret);

	edgetpu_ikv_process_response(args->ikv_resp, &resp_code, &resp_data, fence_status, false);

out_untrack:
	edgetpu_device_group_untrack_fence_task(group_to_notify, current);

out_free_args:
	kfree(args->cmd);
	kfree(args);
	/*
	 * This is the return status of the thread, and indicates that the thread is exiting
	 * cleanly, not that there were no errors encountered.
	 *
	 * Any errors have been communicated via a VII error response.
	 */
	return 0;
}

int edgetpu_ikv_send_cmd(struct edgetpu_ikv *etikv, void *cmd, struct list_head *pending_queue,
			 struct list_head *ready_queue, spinlock_t *queue_lock,
			 struct edgetpu_device_group *group_to_notify,
			 struct gcip_fence_array *in_fence_array,
			 struct gcip_fence_array *out_fence_array, struct iif_fence *iif_dma_fence,
			 struct edgetpu_ikv_additional_info *additional_info,
			 void (*release_callback)(void *), void *release_data)
{
	struct edgetpu_ikv_response *ikv_resp;
	struct send_cmd_args *args;
	dma_addr_t additional_info_daddr = 0;
	ssize_t additional_info_size = 0;
	int ret;
	struct task_struct *wait_task;
	struct dma_fence *in_fence;
	int fence_status;
	u16 resp_code;
	u64 resp_data;

	in_fence = gcip_fence_array_merge_ikf(in_fence_array);
	if (IS_ERR(in_fence)) {
		ret = PTR_ERR(in_fence);
		etdev_err(etikv->etdev, "Failed to merge in-kernel fences, ret=%d", ret);
		return ret;
	}

	if (in_fence)
		dma_fence_enable_sw_signaling(in_fence);

	if (in_fence && !group_to_notify) {
		etdev_err(etikv->etdev,
			  "Cannot send a command with an in-fence without an owning device_group");
		ret = -EINVAL;
		goto err_put_in_fence;
	}

	ikv_resp = kzalloc(sizeof(*ikv_resp), GFP_KERNEL);
	if (!ikv_resp) {
		ret = -ENOMEM;
		goto err_put_in_fence;
	}

	ikv_resp->resp = kzalloc(edgetpu_vii_response_packet_size(), GFP_KERNEL);
	if (!ikv_resp->resp) {
		ret = -ENOMEM;
		goto err_free_ikv_resp;
	}

	args = kzalloc(sizeof(*args), GFP_KERNEL);
	if (!args) {
		ret = -ENOMEM;
		goto err_free_ikv_resp_resp;
	}

	args->cmd = kzalloc(edgetpu_vii_command_packet_size(), GFP_KERNEL);
	if (!args->cmd) {
		ret = -ENOMEM;
		goto err_free_args;
	}

	if (additional_info) {
		additional_info_size = edgetpu_ikv_additional_info_alloc_and_copy(
			etikv->etdev, additional_info, &ikv_resp->additional_info);
		if (additional_info_size < 0) {
			ret = additional_info_size;
			goto err_free_args_cmd;
		}
		additional_info_daddr = ikv_resp->additional_info.dma_addr;
	}

	edgetpu_vii_command_set_additional_info(cmd, additional_info_daddr, additional_info_size);

	ret = gcip_mailbox_awaiter_init(&ikv_resp->gcip_awaiter, etikv->mbx_protocol,
					ikv_resp->resp, &edgetpu_ikv_response_ops);
	if (ret)
		goto err_free_additional_info;

	ikv_resp->etikv = etikv;
	ikv_resp->pending_queue = pending_queue;
	ikv_resp->dest_queue = ready_queue;
	ikv_resp->queue_lock = queue_lock;
	ikv_resp->processed = false;
	ikv_resp->client_seq = edgetpu_vii_command_get_seq_number(cmd);
	ikv_resp->group_to_notify = group_to_notify;
	ikv_resp->in_fence_array = gcip_fence_array_get(in_fence_array);
	ikv_resp->out_fence_array = gcip_fence_array_get(out_fence_array);
	ikv_resp->iif_dma_fence = iif_dma_fence;
	ikv_resp->release_callback = release_callback;
	ikv_resp->release_data = release_data;
	edgetpu_vii_response_set_client_id(ikv_resp->resp, edgetpu_vii_command_get_client_id(cmd));

	args->etikv = etikv;
	args->ikv_resp = ikv_resp;
	args->fence = in_fence;
	memcpy(args->cmd, cmd, edgetpu_vii_command_packet_size());

	/* Send the command immediately if there's no fence to wait on. */
	if (!in_fence || dma_fence_get_status(in_fence) == 1) {
		ret = do_send_cmd(args);
		if (ret)
			goto err_put_out_fence_array;
		/* If the command was successfully sent, args is no longer needed. */
		if (in_fence)
			dma_fence_put(in_fence);
		kfree(args->cmd);
		kfree(args);
		return 0;
	}

	fence_status = dma_fence_get_status(in_fence);
	if (fence_status < 0) {
		/*
		 * If the in-fence has an error status, the command must not be sent.
		 * Instead enqueue a VII error response which indicates the command failed to send.
		 *
		 * Since the `ikv_resp` is being used to track this error response, cleanup is tied
		 * to the life of the `ikv_resp` and doesn't have to happen here.
		 */
		resp_code = VII_RESPONSE_CODE_KERNEL_FENCE_ERROR;
		resp_data = fence_status;
		edgetpu_ikv_process_response(ikv_resp, &resp_code, &resp_data, fence_status, false);
		if (in_fence)
			dma_fence_put(in_fence);
		kfree(args->cmd);
		kfree(args);
		return 0;
	}

	wait_task = kthread_create(send_cmd_thread_fn, args,
				   "edgetpu_ikv_send_cmd_client%u_seq%llu",
				   edgetpu_vii_command_get_client_id(cmd),
				   edgetpu_vii_command_get_seq_number(cmd));
	if (IS_ERR(wait_task)) {
		ret = PTR_ERR(wait_task);
		goto err_put_out_fence_array;
	}

	ret = edgetpu_device_group_track_fence_task(args->ikv_resp->group_to_notify, wait_task);
	if (ret)
		goto err_stop_thread;

	wake_up_process(wait_task);

	return 0;

err_stop_thread:
	kthread_stop(wait_task);
err_put_out_fence_array:
	gcip_fence_array_put(out_fence_array);
	gcip_fence_array_put(in_fence_array);
err_free_additional_info:
	edgetpu_ikv_additional_info_free(etikv->etdev, &ikv_resp->additional_info);
err_free_args_cmd:
	kfree(args->cmd);
err_free_args:
	kfree(args);
err_free_ikv_resp_resp:
	kfree(ikv_resp->resp);
err_free_ikv_resp:
	kfree(ikv_resp);
err_put_in_fence:
	if (in_fence)
		dma_fence_put(in_fence);
	return ret;
}

void edgetpu_ikv_flush_responses(struct edgetpu_ikv *etikv)
{
	gcip_mailbox_consume_responses(etikv->mbx_protocol);
}

void edgetpu_ikv_cancel(struct edgetpu_device_group *group, int reason)
{
	struct edgetpu_ikv_response *cur, *nxt;
	unsigned long flags;
	u16 resp_code = VII_RESPONSE_CODE_KERNEL_CANCELED;
	u64 resp_data = reason;
	LIST_HEAD(pending_ikv_resps);

	/*
	 * By setting @cur->processed to true, the responses will be prevented to be processed by
	 * either the arrived or timedout handler even though one of those handlers is fired.
	 * (See `edgetpu_ikv_process_response()`.)
	 */
	spin_lock_irqsave(&group->ikv_resp_lock, flags);

	list_for_each_entry(cur, &group->pending_ikv_resps, list_entry) {
		cur->processed = true;
	}

	list_replace_init(&group->pending_ikv_resps, &pending_ikv_resps);

	spin_unlock_irqrestore(&group->ikv_resp_lock, flags);

	/*
	 * Cancels all pending commands and pushes CANCELED responses for them.
	 *
	 * Note that the arrived or timedout handlers can be still fired while canceling commands
	 * by the race condition, but they will directly return without doing anything because of
	 * the logic above.
	 *
	 * In other words, neither ARRIVED nor TIMEDOUT responses will be pushed to the dest_queue
	 * of @group and one refcount of @cur->awaiter held by the driver won't be released until we
	 * push CANCELED responses and the runtime consumes them. (i.e., there will be no UAF bug.)
	 *
	 * Therefore, we don't need to check the return value of the `gcip_mailbox_cancel_awaiter`
	 * function, it is always safe to push CANCELED responses to the response queue of @group.
	 *
	 * Note that to prevent a potential race condition between ARRIVED and CANCELED, the caller
	 * is expected to call the `edgetpu_ikv_flush_responses()` function first before this
	 * function to ensure consuming all arrived responses from the MCU.
	 *
	 * Another potential race condition that processing TIMEDOUT commands as CANCELED should be
	 * fine.
	 */
	list_for_each_entry_safe(cur, nxt, &pending_ikv_resps, list_entry) {
		gcip_mailbox_cancel_awaiter(&cur->gcip_awaiter);
		edgetpu_ikv_process_response(cur, &resp_code, &resp_data, -ECANCELED, true);
	}
}

void edgetpu_ikv_send_iif_unblock_notification(struct edgetpu_ikv *etikv, int fence_id)
{
#if EDGETPU_USE_LITEBUF_VII
	struct edgetpu_vii_litebuf_command cmd;
#endif
	int ret;

	/*
	 * Theoretically, the meaning of this function call is that MCU is waiting on some fences
	 * to proceed waiter commands so that the block should be already powered on. However,
	 * according to the design of IIF, if signaler IP is crashed, there is a possibility of race
	 * condition that the MCU has processed the commands before this notification which means
	 * the block would be already powered down. To prevent any kernel panic can be caused by it,
	 * acquire the wakelock if the block is powered on. Otherwise, just give up notifying MCU of
	 * the fence unblock.
	 */
	ret = edgetpu_pm_get_if_powered(etikv->etdev, false);
	if (ret) {
		etdev_warn(etikv->etdev,
			   "Unable to send IIF unblock notification due to the block being off");
		return;
	}

#if EDGETPU_USE_LITEBUF_VII
	cmd.signal_fence_command.fence_id = fence_id;
	cmd.type = EDGETPU_VII_LITEBUF_SIGNAL_FENCE_COMMAND;

	ret = gcip_mailbox_send_cmd_no_rsp(etikv->mbx_protocol, &cmd);
	if (ret)
		etdev_warn(etikv->etdev, "Failed to propagate the fence unblock, id=%d, error=%d",
			   fence_id, ret);
#else
	etdev_warn_ratelimited(etikv->etdev,
			       "The firmware doesn't support propagating IIF unblock notification");
#endif

	edgetpu_pm_put(etikv->etdev);
}

void edgetpu_ikv_mappings_show(struct edgetpu_ikv *etikv, struct seq_file *s)
{
	struct gcip_memory *cmd_queue_mem = &etikv->cmd_queue_mem;
	struct gcip_memory *resp_queue_mem = &etikv->resp_queue_mem;

	seq_printf(s, "  %pad %lu ikv cmdq\n", &cmd_queue_mem->dma_addr,
		   DIV_ROUND_UP(cmd_queue_mem->size, PAGE_SIZE));
	seq_printf(s, "  %pad %lu ikv rspq\n", &resp_queue_mem->dma_addr,
		   DIV_ROUND_UP(resp_queue_mem->size, PAGE_SIZE));
}
