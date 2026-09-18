// SPDX-License-Identifier: GPL-2.0-only
/*
 * GCIP Mailbox Interface.
 *
 * Copyright (C) 2022 Google LLC
 */

#include <asm/barrier.h>

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/kref.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h> /* memcpy */
#include <linux/wait.h>

#include <gcip/gcip-mailbox.h>

#if IS_ENABLED(CONFIG_GCIP_TEST)
#include "unittests/helper/gcip-mailbox-controller.h"

#define TEST_TRIGGER_TIMEOUT_RACE(awaiter, lock) \
	gcip_mailbox_controller_trigger_timeout_race(awaiter, lock)
#define TEST_FLUSH_TIMEOUT_RACE(awaiter) gcip_mailbox_controller_flush_timeout_race(awaiter)
#define TEST_WAIT_FIRMWARE_WORK() gcip_mailbox_controller_wait_firmware_work()
#define TEST_NOTIFY_TIMEOUT_HANDLER_START() gcip_mailbox_controller_notify_timeout_handler_start()
#else
#define TEST_TRIGGER_TIMEOUT_RACE(...)
#define TEST_FLUSH_TIMEOUT_RACE(...)
#define TEST_WAIT_FIRMWARE_WORK(...)
#define TEST_NOTIFY_TIMEOUT_HANDLER_START(...)
#endif

#define GET_TX_QUEUE_TAIL() mailbox->ops->get_tx_queue_tail(mailbox)
#define INC_TX_QUEUE_TAIL(inc) mailbox->ops->inc_tx_queue_tail(mailbox, inc)
#define ACQUIRE_TX_QUEUE_LOCK(try, atomic) mailbox->ops->acquire_tx_queue_lock(mailbox, try, atomic)
#define RELEASE_TX_QUEUE_LOCK() mailbox->ops->release_tx_queue_lock(mailbox)

#define GET_CMD_ELEM_SEQ(cmd) mailbox->ops->get_cmd_elem_seq(mailbox, cmd)
#define SET_CMD_ELEM_SEQ(cmd, seq) mailbox->ops->set_cmd_elem_seq(mailbox, cmd, seq)

#define GET_RX_QUEUE_SIZE() mailbox->ops->get_rx_queue_size(mailbox)
#define GET_RX_QUEUE_HEAD() mailbox->ops->get_rx_queue_head(mailbox)
#define INC_RX_QUEUE_HEAD(inc) mailbox->ops->inc_rx_queue_head(mailbox, inc)
#define GET_RX_QUEUE_TAIL() mailbox->ops->get_rx_queue_tail(mailbox)
#define ACQUIRE_RX_QUEUE_LOCK(try, atomic) mailbox->ops->acquire_rx_queue_lock(mailbox, try, atomic)
#define RELEASE_RX_QUEUE_LOCK() mailbox->ops->release_rx_queue_lock(mailbox)

#define GET_RESP_ELEM_SEQ(resp) mailbox->ops->get_resp_elem_seq(mailbox, resp)
#define SET_RESP_ELEM_SEQ(resp, seq) mailbox->ops->set_resp_elem_seq(mailbox, resp, seq)

#define IS_BLOCK_OFF() (mailbox->ops->is_block_off ? mailbox->ops->is_block_off(mailbox) : false)

/**
 * struct gcip_mailbox_waiter_sync - The struct to hold the awaiter of the synchronous command.
 * @awaiter: The embedded awaiter of the synchronous command.
 * @release_ready: A completion struct to indicate that the awaiter is ready to be released.
 */
struct gcip_mailbox_waiter_sync {
	struct gcip_mailbox_awaiter awaiter;
	struct completion release_ready;
};

/**
 * gcip_mailbox_waiter_sync_release() - The callback to release the synchronous awaiter.
 * @awaiter: The pointer to the awaiter of the synchronous command.
 *
 * The sync_rsp will not be freed here because it will be released automatically when the function
 * gcip_mailbox_send_cmd() returns. This function will only mark the @release_ready completed and
 * notify gcip_mailbox_send_cmd() to continue.
 */
static void gcip_mailbox_waiter_sync_release(struct gcip_mailbox_awaiter *awaiter)
{
	struct gcip_mailbox_waiter_sync *waiter_sync =
		container_of(awaiter, struct gcip_mailbox_waiter_sync, awaiter);

	complete_all(&waiter_sync->release_ready);
}

/**
 * struct gcip_mailbox_ops - The awaiter operators for the synchronous command.
 */
static const struct gcip_mailbox_awaiter_ops sync_awaiter_ops = {
	.release = gcip_mailbox_waiter_sync_release
};

static void gcip_mailbox_async_cmd_timeout_work(struct work_struct *work);

/**
 * __gcip_mailbox_awaiter_init() - Initializes the given gcip_mailbox_awaiter.
 * @awaiter: The pointer to the awaiter to be initialized.
 * @mailbox: The pointer to the gcip mailbox to interact with its interfaces.
 * @rsp: The pointer to the response to be filled.
 * @ops: The pointer to the awaiter operators.
 * @on_stack: Whether or not the awaiter is allocated on the stack memory.
 *
 * The awaiter is expected to be released with the gcip_mailbox_awaiter_put().
 *
 * Return: 0 on success, or a negative errno otherwise.
 */
static int __gcip_mailbox_awaiter_init(struct gcip_mailbox_awaiter *awaiter,
				       struct gcip_mailbox *mailbox, void *rsp,
				       const struct gcip_mailbox_awaiter_ops *ops, bool on_stack)
{
	struct delayed_work *timeout_work = &awaiter->timeout_work;

	awaiter->rsp = rsp;
	awaiter->mailbox = mailbox;
	awaiter->ops = ops;
	awaiter->status = GCIP_MAILBOX_AWAITER_STATUS_INIT;

	kref_init(&awaiter->kref);
	init_completion(&awaiter->handled);
	INIT_LIST_HEAD(&awaiter->list);

	if (on_stack)
		INIT_DELAYED_WORK_ONSTACK(timeout_work, gcip_mailbox_async_cmd_timeout_work);
	else
		INIT_DELAYED_WORK(timeout_work, gcip_mailbox_async_cmd_timeout_work);

	return 0;
}

int gcip_mailbox_awaiter_init(struct gcip_mailbox_awaiter *awaiter, struct gcip_mailbox *mailbox,
			      void *rsp, const struct gcip_mailbox_awaiter_ops *ops)
{
	return __gcip_mailbox_awaiter_init(awaiter, mailbox, rsp, ops, false);
}

/**
 * gcip_mailbox_awaiter_init_onstack() - Initializes the awaiter allocated on the stack memory.
 * @awaiter: The pointer to the awaiter to be initialized.
 * @mailbox: The pointer to the gcip mailbox to interact with its interfaces.
 * @rsp: The pointer to the response to be filled.
 * @ops: The pointer to the awaiter operators.
 *
 * The same as gcip_mailbox_awaiter_init(), but the awaiter is allocated on the stack memory.
 *
 * Return: 0 on success, or a negative errno otherwise.
 */
static int gcip_mailbox_awaiter_init_onstack(struct gcip_mailbox_awaiter *awaiter,
					     struct gcip_mailbox *mailbox, void *rsp,
					     const struct gcip_mailbox_awaiter_ops *ops)
{
	return __gcip_mailbox_awaiter_init(awaiter, mailbox, rsp, ops, true);
}

/**
 * gcip_mailbox_awaiter_release() - Destroys the awaiter of the response.
 * @kref: The pointer to the kref of the awaiter to be released.
 *
 * This function will call awaiter->ops->release if provided.
 *
 * This function should only be called by gcip_mailbox_awaiter_put().
 */
static void gcip_mailbox_awaiter_release(struct kref *kref)
{
	struct gcip_mailbox_awaiter *awaiter =
		container_of(kref, struct gcip_mailbox_awaiter, kref);

	if (awaiter->ops->release)
		awaiter->ops->release(awaiter);
}

struct gcip_mailbox_awaiter *gcip_mailbox_awaiter_get(struct gcip_mailbox_awaiter *awaiter)
{
	kref_get(&awaiter->kref);

	return awaiter;
}

void gcip_mailbox_awaiter_put(struct gcip_mailbox_awaiter *awaiter)
{
	kref_put(&awaiter->kref, gcip_mailbox_awaiter_release);
}

/**
 * gcip_mailbox_wait_list_del() - Deletes the awaiter from @mailbox->wait_list.
 * @mailbox: The pointer to the gcip mailbox containing the wait list.
 * @awaiter: The pointer to the target awaiter to be deleted.
 * @status: The status to be set to the deleted awaiter.
 *
 * There are 3 cases that the awaiter can be deleted from the wait list:
 * 1. Response arrived, gcip_mailbox_handle_response() will delete the awaiter form wait list
 * 2. Response timed out, gcip_mailbox_async_cmd_timeout_work() will trigger this function.
 * 3. Response canceled, gcip_mailbox_cancel_awaiter() and gcip_mailbox_cancel_awaiter_all() will
 *    trigger this function.
 *
 * Return: Whether or not the awaiter is deleted from the wait list.
 */
static bool gcip_mailbox_wait_list_del(struct gcip_mailbox *mailbox,
				       struct gcip_mailbox_awaiter *awaiter,
				       enum gcip_mailbox_awaiter_status status)
{
	unsigned long flags;
	bool deleted = false;

	spin_lock_irqsave(&mailbox->wait_list->list_lock, flags);
	if (awaiter->status == GCIP_MAILBOX_AWAITER_STATUS_WAITING) {
		list_del_init(&awaiter->list);
		deleted = true;
		awaiter->status = status;
	}
	spin_unlock_irqrestore(&mailbox->wait_list->list_lock, flags);

	if (deleted)
		gcip_mailbox_awaiter_put(awaiter);

	return deleted;
}

/**
 * gcip_mailbox_wait_list_add() - Adds @awaiter to @mailbox->wait_list.
 * @mailbox: The pointer to the gcip mailbox containing the wait list.
 * @awaiter: The pointer to the awaiter to be added.
 *
 * Context: Depends on @atomic.
 * Return: 0 on success, or a negative errno otherwise.
 */
static int gcip_mailbox_wait_list_add(struct gcip_mailbox *mailbox,
				      struct gcip_mailbox_awaiter *awaiter)
{
	unsigned long flags;
	int ret;

	if (mailbox->ops->before_enqueue_wait_list) {
		/* If the awaiter is a synchronous one, pass NULL to the callback */
		ret = mailbox->ops->before_enqueue_wait_list(
			mailbox, awaiter->rsp,
			gcip_mailbox_awaiter_get_ops(awaiter) == &sync_awaiter_ops ? NULL :
										     awaiter);
		if (ret)
			return ret;
	}

	gcip_mailbox_awaiter_get(awaiter);

	spin_lock_irqsave(&mailbox->wait_list->list_lock, flags);
	list_add_tail(&awaiter->list, &mailbox->wait_list->list);
	awaiter->status = GCIP_MAILBOX_AWAITER_STATUS_WAITING;
	spin_unlock_irqrestore(&mailbox->wait_list->list_lock, flags);

	return 0;
}

/**
 * should_maintain_seq_num() - Checks if the sequence number of the command should be maintained.
 * @mode: The operating mode of the mailbox.
 *
 * Return: Whether or not the sequence number of the command should be maintained.
 */
static inline bool should_maintain_seq_num(u8 mode)
{
	if (mode & GCIP_MAILBOX_MODE_SEQ_EXTERNAL)
		return false;

	return ((mode & GCIP_MAILBOX_MODE_TX_CMD) && (mode & GCIP_MAILBOX_MODE_RX_RSP));
}

/**
 * gcip_mailbox_enqueue_cmd() - Enqueues a command to the command queue of mailbox.
 * @mailbox: The pointer to the gcip mailbox to interact with its interfaces.
 * @cmd: The pointer to the command to be enqueued.
 * @awaiter: The pointer to the awaiter of the async response.
 *
 * Pass NULL to @awaiter if the caller wants to ignore the response, in this case, no awaiter will
 * be added in the wait list and thus the arrived response will find nothing and return.
 *
 * Return: 0 on success, or a negative errno otherwise.
 */
static int gcip_mailbox_enqueue_cmd(struct gcip_mailbox *mailbox, void *cmd,
				    struct gcip_mailbox_awaiter *awaiter)
{
	void *cmd_ptr;
	int idx;
	int ret = 0;
	bool atomic = false;

	ACQUIRE_TX_QUEUE_LOCK(false, &atomic);

	if (should_maintain_seq_num(mailbox->mode))
		SET_CMD_ELEM_SEQ(cmd, atomic64_inc_return(&mailbox->cur_seq));

	/* Wait until the cmd queue has a space for putting cmd. */
	ret = mailbox->ops->wait_for_tx_queue_not_full(mailbox);
	if (ret)
		goto out;

	if (awaiter) {
		/* Adds @resp to the wait_list only if the cmd can be pushed successfully. */
		SET_RESP_ELEM_SEQ(awaiter->rsp, GET_CMD_ELEM_SEQ(cmd));
		ret = gcip_mailbox_wait_list_add(mailbox, awaiter);
		if (ret)
			goto out;
	}

	/* Calculate the address of the command in the command queue. */
	idx = CIRC_QUEUE_REAL_INDEX(GET_TX_QUEUE_TAIL(), mailbox->queue_wrap_bit);
	cmd_ptr = mailbox->tx_queue + mailbox->tx_elem_size * idx;
	memcpy(cmd_ptr, cmd, mailbox->tx_elem_size);

	INC_TX_QUEUE_TAIL(1);

	if (mailbox->ops->after_enqueue_cmd) {
		ret = mailbox->ops->after_enqueue_cmd(mailbox, cmd);
		if (ret) {
			/*
			 * Currently, as both DSP and EdgeTPU never return errors, do nothing
			 * here. We can decide later how to rollback the status such as
			 * `cmd_queue_tail` when the possibility of returning an error is raised.
			 */
			dev_warn(mailbox->dev,
				 "after_enqueue_cmd returned an error, but not handled: ret=%d",
				 ret);

			if (awaiter)
				gcip_mailbox_wait_list_del(mailbox, awaiter,
							   GCIP_MAILBOX_AWAITER_STATUS_CANCELED);

			goto out;
		}
	}

out:
	RELEASE_TX_QUEUE_LOCK();
	if (ret)
		dev_dbg(mailbox->dev, "%s: ret=%d", __func__, ret);

	return ret;
}

/*
 * Handler of a response.
 * Pops the wait_list until the sequence number of @resp is found, and copies @resp to the found
 * entry.
 */
static void gcip_mailbox_handle_response(struct gcip_mailbox *mailbox, void *resp)
{
	struct gcip_mailbox_awaiter *cur;
	struct gcip_mailbox_awaiter *awaiter = NULL;
	unsigned long flags;

	spin_lock_irqsave(&mailbox->wait_list->list_lock, flags);
	list_for_each_entry(cur, &mailbox->wait_list->list, list) {
		bool match;

		if (cur->ops->match_response)
			match = cur->ops->match_response(resp, cur->rsp);
		else
			match = GET_RESP_ELEM_SEQ(resp) == GET_RESP_ELEM_SEQ(cur->rsp);

		if (!match)
			continue;

		list_del_init(&cur->list);
		awaiter = cur;
		awaiter->status = GCIP_MAILBOX_AWAITER_STATUS_ARRIVED;

		/*
		 * The timedout handler will be fired, but pended by waiting for acquiring the
		 * wait_list->list_lock.
		 */
		TEST_TRIGGER_TIMEOUT_RACE(awaiter, &mailbox->wait_list->list_lock);

		break;
	}
	spin_unlock_irqrestore(&mailbox->wait_list->list_lock, flags);

	if (!awaiter) {
		dev_warn_ratelimited(mailbox->dev, "Response seq %llu discarded",
				     GET_RESP_ELEM_SEQ(resp));
		return;
	}

	/* The reference acquired by the timeout work will be released implicitly. */
	gcip_mailbox_cancel_timeout_work(awaiter);

	memcpy(awaiter->rsp, resp, mailbox->rx_elem_size);

	if (awaiter->ops->handle_arrived)
		awaiter->ops->handle_arrived(awaiter);

	complete_all(&awaiter->handled);

	/* Make sure the timedout handler is finished before decreasing the ref count. */
	TEST_FLUSH_TIMEOUT_RACE(awaiter);

	/* Remove the reference acquired by the wait list. */
	gcip_mailbox_awaiter_put(awaiter);
}

/**
 * gcip_mailbox_handle_rx_elem() - Handles the received element according to its type.
 * @mailbox: The pointer to the gcip mailbox to interact with its interfaces.
 * @elem: The received element to be handled.
 *
 * If both GCIP_MAILBOX_MODE_RX_CMD and GCIP_MAILBOX_MODE_RX_RSP are on, the mailboix ops
 * is_rx_elem_reversed must be defined and used here.
 *
 * If only GCIP_MAILBOX_MODE_RX_CMD or GCIP_MAILBOX_MODE_RX_RSP is on, we can assign the handler
 * according to the operating mode.
 *
 * Context: normal and in_interrupt().
 */
static void gcip_mailbox_handle_rx_elem(struct gcip_mailbox *mailbox, void *elem)
{
	bool is_reversed_cmd = mailbox->ops->is_rx_elem_reversed ?
				       mailbox->ops->is_rx_elem_reversed(mailbox, elem) :
				       (mailbox->mode & GCIP_MAILBOX_MODE_RX_CMD);

	if (is_reversed_cmd)
		mailbox->ops->handle_reversed_command(mailbox, elem);
	else
		gcip_mailbox_handle_response(mailbox, elem);
}

/*
 * Fetches elements in the response queue.
 *
 * Returns the pointer of fetched response elements.
 * @total_ptr will be the number of elements fetched.
 *
 * If @trylock is true, the function will return right away if the lock is held by others which
 * means that the response queue is being consumed by other threads. Otherwise, it will use the
 * normal lock to guarantee that all responses have been handled when the function returns.
 *
 * Returns -ENOMEM if failed on memory allocation.
 * Returns NULL if the response queue is empty or there is another worker fetching responses.
 */
static void *gcip_mailbox_fetch_responses(struct gcip_mailbox *mailbox, u32 *total_ptr,
					  bool trylock)
{
	u32 head;
	u32 tail;
	u32 count;
	u32 i;
	u32 j;
	u32 total = 0;
	const u32 wrap_bit = mailbox->queue_wrap_bit;
	const u32 size = GET_RX_QUEUE_SIZE();
	const u32 elem_size = mailbox->rx_elem_size;
	void *ret = NULL; /* Array of responses. */
	void *prev_ptr = NULL; /* Temporary pointer to realloc ret. */
	bool atomic = false;

	/* The block is off or someone is working on consuming - we can leave early. */
	if (IS_BLOCK_OFF() || !ACQUIRE_RX_QUEUE_LOCK(trylock, &atomic))
		goto out;

	head = GET_RX_QUEUE_HEAD();
	/* Loops until our head equals to CSR tail. */
	while (1) {
		tail = GET_RX_QUEUE_TAIL();
		/*
		 * Make sure the CSR is read and reported properly by checking if any bit higher
		 * than wrap_bit is set and if the tail exceeds resp_queue size.
		 */
		if (unlikely(tail & ~CIRC_QUEUE_VALID_MASK(wrap_bit) ||
			     CIRC_QUEUE_REAL_INDEX(tail, wrap_bit) >= size)) {
			dev_err_ratelimited(mailbox->dev, "Invalid response queue tail: %#x", tail);
			break;
		}

		count = gcip_circ_queue_cnt(head, tail, size, wrap_bit);
		if (count == 0)
			break;

		prev_ptr = ret;
		ret = krealloc(prev_ptr, (total + count) * elem_size,
			       atomic ? GFP_ATOMIC : GFP_KERNEL);
		/*
		 * Out-of-memory, we can return the previously fetched responses if any, or ENOMEM
		 * otherwise.
		 */
		if (!ret) {
			if (!prev_ptr)
				ret = ERR_PTR(-ENOMEM);
			else
				ret = prev_ptr;
			break;
		}
		/* Copies responses. */
		j = CIRC_QUEUE_REAL_INDEX(head, wrap_bit);
		for (i = 0; i < count; i++) {
			memcpy(ret + elem_size * total, mailbox->rx_queue + elem_size * j,
			       elem_size);
			j = (j + 1) % size;
			total++;
		}
		head = gcip_circ_queue_inc(head, count, size, wrap_bit);
	}
	INC_RX_QUEUE_HEAD(total);

	RELEASE_RX_QUEUE_LOCK();

	if (mailbox->ops->after_fetch_resps)
		mailbox->ops->after_fetch_resps(mailbox, total);
out:
	*total_ptr = total;
	return ret;
}

/* Fetches one response from the response queue. */
static int gcip_mailbox_fetch_one_response(struct gcip_mailbox *mailbox, void *resp)
{
	u32 head;
	u32 tail;
	bool atomic;

	if (IS_BLOCK_OFF() || !ACQUIRE_RX_QUEUE_LOCK(true, &atomic))
		return 0;

	head = GET_RX_QUEUE_HEAD();
	tail = GET_RX_QUEUE_TAIL();
	/* Queue empty. */
	if (head == tail) {
		RELEASE_RX_QUEUE_LOCK();
		return 0;
	}

	memcpy(resp,
	       mailbox->rx_queue +
		       CIRC_QUEUE_REAL_INDEX(head, mailbox->queue_wrap_bit) * mailbox->rx_elem_size,
	       mailbox->rx_elem_size);
	INC_RX_QUEUE_HEAD(1);

	RELEASE_RX_QUEUE_LOCK();

	if (mailbox->ops->after_fetch_resps)
		mailbox->ops->after_fetch_resps(mailbox, 1);

	return 1;
}

/* Handles the timed out asynchronous commands. */
static void gcip_mailbox_async_cmd_timeout_work(struct work_struct *work)
{
	struct gcip_mailbox_awaiter *awaiter =
		container_of(work, struct gcip_mailbox_awaiter, timeout_work.work);
	struct gcip_mailbox *mailbox = awaiter->mailbox;
	bool deleted;

	TEST_NOTIFY_TIMEOUT_HANDLER_START();

	/*
	 * This function returns true if @awaiter is deleted from the wait list successfully.
	 * It means that it is safe to process @awaiter as timeout. (i.e., there won't be any race
	 * cases that @awaiter has been processed as arrived or canceled at the same time.)
	 */
	deleted =
		gcip_mailbox_wait_list_del(mailbox, awaiter, GCIP_MAILBOX_AWAITER_STATUS_TIMEDOUT);
	if (deleted) {
		if (awaiter->ops->handle_timedout)
			awaiter->ops->handle_timedout(awaiter);

		complete_all(&awaiter->handled);
	}

	/* Remove the reference of the timedout handler. */
	gcip_mailbox_awaiter_put(awaiter);
}

/**
 * gcip_mailbox_cancel_awaiter_all() - Cancels the unhandled awaiters in the wait list.
 * @mailbox: The mailbox to cancel the unhandled awaiters.
 *
 * This function will delete all the awaiters in the wait list and cancel their timeout workers.
 * It is used when the mailbox is about to be released.
 */
static void gcip_mailbox_cancel_awaiter_all(struct gcip_mailbox *mailbox)
{
	struct gcip_mailbox_awaiter *awaiter, *nxt;
	struct list_head cancel_list;
	unsigned long flags;

	/* Tests cases that responses arrived or timedout while flushing awaiters. */
	TEST_WAIT_FIRMWARE_WORK();

	/*
	 * At this point only async responses should be pending.
	 * Remove them all from the `wait_list` at once to prevent them from being handled by
	 * the arrived or timedout handlers.
	 */
	INIT_LIST_HEAD(&cancel_list);
	spin_lock_irqsave(&mailbox->wait_list->list_lock, flags);
	list_for_each_entry(awaiter, &mailbox->wait_list->list, list)
		awaiter->status = GCIP_MAILBOX_AWAITER_STATUS_CANCELED;
	list_splice_init(&mailbox->wait_list->list, &cancel_list);
	spin_unlock_irqrestore(&mailbox->wait_list->list_lock, flags);

	list_for_each_entry_safe(awaiter, nxt, &cancel_list, list) {
		/*  This will implicitly decrease the reference acquired by the timeout work. */
		gcip_mailbox_cancel_timeout_work_sync(awaiter);

		/*
		 * If the operator is defined, @awaiter will be released on the implementation side.
		 * Otherwise, it should be freed from here.
		 */
		if (awaiter->ops->handle_flushed)
			awaiter->ops->handle_flushed(awaiter);
		else
			gcip_mailbox_awaiter_put(awaiter);

		/* Remove the reference of the awaiter acquired by the wait list. */
		gcip_mailbox_awaiter_put(awaiter);
	}
}

/* Verifies the mailbox operators. */
static int gcip_mailbox_ops_verify(const struct gcip_mailbox_ops *ops, u8 mode, struct device *dev)
{
	if (!ops) {
		dev_err(dev, "Mailbox ops should not be NULL.");
		return -EINVAL;
	}

	if ((mode & GCIP_MAILBOX_MODE_TX_CMD) || (mode & GCIP_MAILBOX_MODE_TX_RSP)) {
		if (!ops->get_tx_queue_tail || !ops->inc_tx_queue_tail ||
		    !ops->acquire_tx_queue_lock || !ops->release_tx_queue_lock ||
		    !ops->wait_for_tx_queue_not_full) {
			dev_err(dev, "Incomplete mailbox CMD queue ops.");
			return -EINVAL;
		}
	}

	if ((mode & GCIP_MAILBOX_MODE_RX_RSP) || (mode & GCIP_MAILBOX_MODE_RX_CMD)) {
		if (!ops->get_rx_queue_size || !ops->get_rx_queue_head || !ops->get_rx_queue_tail ||
		    !ops->inc_rx_queue_head || !ops->acquire_rx_queue_lock ||
		    !ops->release_rx_queue_lock) {
			dev_err(dev, "Incomplete mailbox RESP queue ops.");
			return -EINVAL;
		}
	}

	if (should_maintain_seq_num(mode)) {
		if (!ops->get_cmd_elem_seq || !ops->set_cmd_elem_seq || !ops->get_resp_elem_seq ||
		    !ops->set_resp_elem_seq) {
			dev_err(dev, "Incomplete mailbox sequence number ops.");
			return -EINVAL;
		}
	}

	if (mode & GCIP_MAILBOX_MODE_RX_CMD) {
		if (!ops->handle_reversed_command) {
			dev_err(dev, "Incomplete mailbox reversed CMD element ops.");
			return -EINVAL;
		}
	}

	if ((mode & GCIP_MAILBOX_MODE_RX_RSP) && (mode & GCIP_MAILBOX_MODE_RX_CMD)) {
		if (!ops->is_rx_elem_reversed) {
			dev_err(dev, "Incomplete mailbox RX element ops.");
			return -EINVAL;
		}
	}

	return 0;
}

int gcip_mailbox_init(struct gcip_mailbox *mailbox, const struct gcip_mailbox_args *args)
{
	int ret;

	if (!args->mode) {
		dev_err(args->dev, "Mailbox mode cannot be NULL.");
		return -EINVAL;
	}

	ret = gcip_mailbox_ops_verify(args->ops, args->mode, args->dev);
	if (ret)
		return ret;

	mailbox->dev = args->dev;
	mailbox->mode = args->mode;
	mailbox->queue_wrap_bit = args->queue_wrap_bit;
	mailbox->tx_queue = args->tx_queue;
	mailbox->tx_elem_size = args->tx_elem_size;
	mailbox->rx_queue = args->rx_queue;
	mailbox->rx_elem_size = args->rx_elem_size;
	mailbox->timeout = args->timeout;
	mailbox->ops = args->ops;
	mailbox->data = args->data;

	atomic64_set(&mailbox->cur_seq, 0);

	if (args->wait_list_external) {
		mailbox->wait_list = args->wait_list_external;
	} else {
		gcip_mailbox_wait_list_init(&mailbox->wait_list_internal);
		mailbox->wait_list = &mailbox->wait_list_internal;
	}

	return 0;
}

void gcip_mailbox_release(struct gcip_mailbox *mailbox)
{
	gcip_mailbox_cancel_awaiter_all(mailbox);
	mailbox->ops = NULL;
	mailbox->data = NULL;
}

void gcip_mailbox_wait_list_init(struct gcip_mailbox_wait_list *wait_list)
{
	spin_lock_init(&wait_list->list_lock);
	INIT_LIST_HEAD(&wait_list->list);
}

static void gcip_mailbox_do_consume_responses(struct gcip_mailbox *mailbox, bool trylock)
{
	void *responses;
	u32 i;
	u32 count = 0;

	/* Fetches responses and bumps resp_queue head. */
	responses = gcip_mailbox_fetch_responses(mailbox, &count, trylock);
	if (count == 0)
		return;
	if (IS_ERR(responses)) {
		dev_err(mailbox->dev, "GCIP mailbox failed on fetching responses: %ld",
			PTR_ERR(responses));
		return;
	}

	for (i = 0; i < count; i++)
		gcip_mailbox_handle_rx_elem(mailbox, responses + mailbox->rx_elem_size * i);

	kfree(responses);
}

void gcip_mailbox_consume_responses_work(struct gcip_mailbox *mailbox)
{
	gcip_mailbox_do_consume_responses(mailbox, true);
}

void gcip_mailbox_consume_responses(struct gcip_mailbox *mailbox)
{
	gcip_mailbox_do_consume_responses(mailbox, false);
}

int gcip_mailbox_send_cmd_no_rsp(struct gcip_mailbox *mailbox, void *cmd)
{
	int ret;

	ret = gcip_mailbox_enqueue_cmd(mailbox, cmd, NULL);
	if (ret && mailbox->ops->on_error)
		mailbox->ops->on_error(mailbox, cmd, ret);

	return ret;
}

int gcip_mailbox_send_cmd(struct gcip_mailbox *mailbox, void *cmd, void *resp)
{
	struct gcip_mailbox_waiter_sync waiter_sync;
	struct gcip_mailbox_awaiter *awaiter = &waiter_sync.awaiter;
	int ret;

	init_completion(&waiter_sync.release_ready);
	ret = gcip_mailbox_awaiter_init_onstack(awaiter, mailbox, resp, &sync_awaiter_ops);
	if (ret) {
		complete_all(&waiter_sync.release_ready);
		goto out;
	}

	ret = gcip_mailbox_send_cmd_async(mailbox, cmd, awaiter);
	if (ret)
		goto put_awaiter;

	wait_for_completion(&awaiter->handled);

	switch (awaiter->status) {
	case GCIP_MAILBOX_AWAITER_STATUS_ARRIVED:
		ret = 0;
		break;
	case GCIP_MAILBOX_AWAITER_STATUS_TIMEDOUT:
		ret = -ETIMEDOUT;
		break;
	default:
		WARN_ON_ONCE(true);
		ret = -EINVAL;
		break;
	}

put_awaiter:
	gcip_mailbox_awaiter_put(awaiter);
out:
	if (ret && mailbox->ops->on_error)
		mailbox->ops->on_error(mailbox, cmd, ret);

	/* Ensure that all threads are done with the sync_rsp before it is released. */
	wait_for_completion(&waiter_sync.release_ready);

	return ret;
}

int gcip_mailbox_send_cmd_async(struct gcip_mailbox *mailbox, void *cmd,
				struct gcip_mailbox_awaiter *awaiter)
{
	u32 timeout = 0;
	int ret;

	if (awaiter->ops->get_timeout)
		timeout = awaiter->ops->get_timeout(awaiter);

	if (timeout == 0)
		timeout = mailbox->timeout;

	if (timeout != GCIP_MAILBOX_AWAITER_TIMEOUT_NONE) {
		/* The pending timeout worker needs a reference as well. */
		gcip_mailbox_awaiter_get(awaiter);

		schedule_delayed_work(&awaiter->timeout_work, msecs_to_jiffies(timeout));
	}

	ret = gcip_mailbox_enqueue_cmd(mailbox, cmd, awaiter);
	if (ret)
		goto err_free_resp;

	return 0;

err_free_resp:
	gcip_mailbox_cancel_timeout_work_sync(awaiter);

	return ret;
}

bool gcip_mailbox_cancel_awaiter(struct gcip_mailbox_awaiter *awaiter)
{
	bool deleted;

	/* Cancel the timeout work of the awaiter if it is still pending. */
	gcip_mailbox_cancel_timeout_work(awaiter);

	/*
	 * If @deleted is true, it means that the awaiter is now taken by this cancel handler.
	 * The arrived handler will skip the awaiter when they are executed.
	 *
	 * If @deleted is false, it means either the arrived handler or the timeout handler has
	 * already started, wait until they are completed to ensure that no other threads will
	 * access the awaiter. The caller is the only owner that can access the awaiter after this
	 * function returns.
	 */
	deleted = gcip_mailbox_wait_list_del(awaiter->mailbox, awaiter,
					     GCIP_MAILBOX_AWAITER_STATUS_CANCELED);
	if (!deleted)
		wait_for_completion(&awaiter->handled);

	return deleted;
}

void gcip_mailbox_cancel_timeout_work(struct gcip_mailbox_awaiter *awaiter)
{
	/*
	 * If the timeout work is canceled successfully, we have to decrease the reference count
	 * which was acquired by the timeout work.
	 */
	if (cancel_delayed_work(&awaiter->timeout_work))
		gcip_mailbox_awaiter_put(awaiter);
}

void gcip_mailbox_cancel_timeout_work_sync(struct gcip_mailbox_awaiter *awaiter)
{
	/*
	 * If the timeout work is canceled successfully, we have to decrease the reference count
	 * which was acquired by the timeout work.
	 */
	if (cancel_delayed_work_sync(&awaiter->timeout_work))
		gcip_mailbox_awaiter_put(awaiter);
}

void gcip_mailbox_consume_one_response(struct gcip_mailbox *mailbox, void *resp)
{
	int ret;

	/* Fetches (at most) one response. */
	ret = gcip_mailbox_fetch_one_response(mailbox, resp);
	if (!ret)
		return;

	gcip_mailbox_handle_rx_elem(mailbox, resp);
}
