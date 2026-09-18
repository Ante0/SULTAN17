/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * GCIP Mailbox Interface.
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef __GCIP_MAILBOX_H__
#define __GCIP_MAILBOX_H__

#include <linux/atomic.h>
#include <linux/compiler.h>
#include <linux/completion.h>
#include <linux/kref.h>
#include <linux/limits.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#define GCIP_MAILBOX_MODE_TX_CMD BIT(0)
#define GCIP_MAILBOX_MODE_RX_RSP BIT(1)
#define GCIP_MAILBOX_MODE_RX_CMD BIT(2)
#define GCIP_MAILBOX_MODE_TX_RSP BIT(3)
#define GCIP_MAILBOX_MODE_FORWARD (GCIP_MAILBOX_MODE_TX_CMD | GCIP_MAILBOX_MODE_RX_RSP)
#define GCIP_MAILBOX_MODE_BACKWARD (GCIP_MAILBOX_MODE_RX_CMD | GCIP_MAILBOX_MODE_TX_RSP)
#define GCIP_MAILBOX_MODE_BIDIRECTIONAL (GCIP_MAILBOX_MODE_FORWARD | GCIP_MAILBOX_MODE_BACKWARD)
#define GCIP_MAILBOX_MODE_SEQ_EXTERNAL BIT(4)

#define CIRC_QUEUE_WRAPPED(idx, wrap_bit) ((idx) & wrap_bit)
#define CIRC_QUEUE_INDEX_MASK(wrap_bit) (wrap_bit - 1)
#define CIRC_QUEUE_VALID_MASK(wrap_bit) (CIRC_QUEUE_INDEX_MASK(wrap_bit) | wrap_bit)
#define CIRC_QUEUE_REAL_INDEX(idx, wrap_bit) ((idx) & CIRC_QUEUE_INDEX_MASK(wrap_bit))

#define CIRC_QUEUE_MAX_SIZE(wrap_bit) (wrap_bit - 1)

#define GCIP_MAILBOX_AWAITER_TIMEOUT_NONE U32_MAX

/* To specify the operation is toward cmd or resp queue. */
enum gcip_mailbox_queue_type { GCIP_MAILBOX_CMD_QUEUE, GCIP_MAILBOX_RESP_QUEUE };

/* Utilities of circular queue operations */

/*
 * Returns the number of elements in a circular queue given its @head, @tail,
 * and @queue_size.
 */
static inline u32 gcip_circ_queue_cnt(u32 head, u32 tail, u32 queue_size, u32 wrap_bit)
{
	u32 ret;

	if (CIRC_QUEUE_WRAPPED(tail, wrap_bit) != CIRC_QUEUE_WRAPPED(head, wrap_bit))
		ret = queue_size - CIRC_QUEUE_REAL_INDEX(head, wrap_bit) +
		      CIRC_QUEUE_REAL_INDEX(tail, wrap_bit);
	else
		ret = tail - head;

	if (unlikely(ret > queue_size))
		return 0;

	return ret;
}

/* Increases @index of a circular queue by @inc. */
static inline u32 gcip_circ_queue_inc(u32 index, u32 inc, u32 queue_size, u32 wrap_bit)
{
	u32 new_index = CIRC_QUEUE_REAL_INDEX(index, wrap_bit) + inc;

	if (unlikely(new_index >= queue_size))
		return (index + inc - queue_size) ^ wrap_bit;
	else
		return index + inc;
}

/*
 * Checks if @size is a valid circular queue size, which should be a positive
 * number and less than or equal to MAX_QUEUE_SIZE.
 */
static inline bool gcip_valid_circ_queue_size(u32 size, u32 wrap_bit)
{
	if (!size || size > CIRC_QUEUE_MAX_SIZE(wrap_bit))
		return false;
	return true;
}

struct gcip_mailbox;
struct gcip_mailbox_awaiter_ops;

enum gcip_mailbox_awaiter_status {
	GCIP_MAILBOX_AWAITER_STATUS_INIT,
	GCIP_MAILBOX_AWAITER_STATUS_WAITING,
	GCIP_MAILBOX_AWAITER_STATUS_ARRIVED,
	GCIP_MAILBOX_AWAITER_STATUS_TIMEDOUT,
	GCIP_MAILBOX_AWAITER_STATUS_CANCELED,
};

/* Wrapper struct for responses consumed by a thread other than the one which sent the command. */
struct gcip_mailbox_awaiter {
	/* IP-defined response. */
	void *rsp;
	/* The work which will be executed when the timeout occurs. */
	struct delayed_work timeout_work;
	/*
	 * If this response times out, this pointer to the owning mailbox is
	 * needed to delete this response from the list of pending responses.
	 */
	struct gcip_mailbox *mailbox;
	/* Reference count. */
	struct kref kref;
	/* The operations for the awaiter. */
	const struct gcip_mailbox_awaiter_ops *ops;
	/* Completion for the arrival or timeout handler. */
	struct completion handled;
	/* The list node to enqueue to the wait list. */
	struct list_head list;
	/* The status of the awaiter. */
	enum gcip_mailbox_awaiter_status status;
};

/**
 * struct gcip_mailbox_awaiter_ops - Operations for the awaiter.
 *
 * If the awaiter is not canceled, it is guaranteed that only one of the following callbacks will be
 * called:
 * 1. handle_arrived()
 * 2. handle_timedout()
 *
 * TODO(468197241): Use a single callback to handle both scenarios.
 * The awaiter can be canceled for two scenarios:
 * 1. The mailbox is releasing. In this case, handle_flushed() will be called.
 * 2. The command is canceled by the user.
 */
struct gcip_mailbox_awaiter_ops {
	/**
	 * release() - The callback to release the awaiter.
	 * @awaiter: The pointer to the awaiter to be released.
	 *
	 * This operator is mandatory.
	 *
	 * Context: Any context
	 */
	void (*release)(struct gcip_mailbox_awaiter *awaiter);
	/**
	 * handle_arrived() - The callback to handle the arrived awaiter.
	 * @awaiter: The pointer to the awaiter that arrived.
	 *
	 * Context: Any context
	 */
	void (*handle_arrived)(struct gcip_mailbox_awaiter *awaiter);
	/**
	 * handle_timedout() - The callback to handle the timed out awaiter.
	 * @awaiter: The pointer to the awaiter that timed out.
	 *
	 * Context: Any context
	 */
	void (*handle_timedout)(struct gcip_mailbox_awaiter *awaiter);
	/**
	 * handle_flushed() - The callback to handle the flushed awaiter.
	 * @awaiter: The pointer to the awaiter that is flushed.
	 *
	 * This is used when the mailbox is released.
	 * The response has not yet arrived, nor has it timed out.
	 *
	 * Context: Any context
	 */
	void (*handle_flushed)(struct gcip_mailbox_awaiter *awaiter);
	/**
	 * get_timeout() - The operator to get the timeout value in milliseconds.
	 * @awaiter: The pointer to the awaiter that needs to get the timeout value.
	 *
	 * This operator is optional.
	 *
	 * If this operator is not provided, the mailbox timeout value will be used by default.
	 *
	 * Context: Any context.
	 * Return: The timeout value in milliseconds.
	 *
	 * If the returned value is 0, the default value will be used.
	 *
	 * If the returned value is GCIP_MAILBOX_AWAITER_TIMEOUT_NONE, the timeout will be disabled,
	 * in this case, the calling driver must rely on the firmware to timeout this command.
	 */
	u32 (*get_timeout)(struct gcip_mailbox_awaiter *awaiter);
	/**
	 * match_response() - The operator to match the response packet.
	 * @incoming_resp: The pointer to the incoming response packet from the response queue.
	 * @waiter_resp: The pointer to the waiter response packet passed by the user.
	 *
	 * If this operator is not provided, `get_resp_elem_seq` will be used to retrieve the
	 * sequence number of the response packets; a match is confirmed if the sequence numbers are
	 * equal.
	 *
	 * Context: Interrupt context.
	 * Return: whether or not the incoming response matches the waiter response.
	 */
	bool (*match_response)(void *incoming_resp, void *waiter_resp);
};

/*
 * Mailbox operators.
 * For in_interrupt() context, see the implementation of gcip_mailbox_handle_irq for details.
 */
struct gcip_mailbox_ops {
	/* Mandatory if GCIP_MAILBOX_MODE_TX_CMD or GCIP_MAILBOX_MODE_TX_RSP is on. */
	/*
	 * Gets the tail of mailbox transmit queue.
	 *
	 * Context: tx_queue_lock.
	 */
	u32 (*get_tx_queue_tail)(struct gcip_mailbox *mailbox);
	/*
	 * Increases the tail of mailbox transmit queue by @inc.
	 *
	 * Context: tx_queue_lock.
	 */
	void (*inc_tx_queue_tail)(struct gcip_mailbox *mailbox, u32 inc);
	/*
	 * Acquires the lock of tx_queue. If @try is true, "_trylock" functions can be used, but
	 * also it can be ignored. If the lock will make the context atomic, @atomic must be set
	 * to true. Returns 1 if succeed, 0 if failed.
	 *
	 * This callback will be called in the following situations.
	 * - Enqueue a command to the tx_queue.
	 *
	 * The lock can be mutex lock or spin lock and it will be released by calling
	 * `release_tx_queue_lock` callback.
	 *
	 * Context: normal.
	 */
	int (*acquire_tx_queue_lock)(struct gcip_mailbox *mailbox, bool try, bool *atomic);
	/*
	 * Releases the lock of tx_queue which is acquired by calling `acquire_tx_queue_lock`.
	 *
	 * Context: tx_queue_lock.
	 */
	void (*release_tx_queue_lock)(struct gcip_mailbox *mailbox);
	/*
	 * Gets the sequence number of @cmd queue element.
	 *
	 * Context: tx_queue_lock.
	 */
	u64 (*get_cmd_elem_seq)(struct gcip_mailbox *mailbox, void *cmd);
	/*
	 * Sets the sequence number of @cmd queue element.
	 *
	 * Context: tx_queue_lock.
	 */
	void (*set_cmd_elem_seq)(struct gcip_mailbox *mailbox, void *cmd, u64 seq);
	/*
	 * Waits for the tx queue of @mailbox has a available space for putting the command. If
	 * the queue has a space, returns 0. Otherwise, returns error as non-zero value. It depends
	 * on the implementation details, but it is okay to return right away with error when the
	 * queue is full. If this callback returns an error, `gcip_mailbox_send_tx` function or
	 * `gcip_mailbox_put_tx` function will return that error too.
	 *
	 * Context: tx_queue_lock.
	 */
	int (*wait_for_tx_queue_not_full)(struct gcip_mailbox *mailbox);

	/* Mandatory if GCIP_MAILBOX_MODE_RX_RSP or GCIP_MAILBOX_MODE_RX_CMD is on. */
	/*
	 * Gets the size of mailbox receive queue.
	 *
	 * Context: normal.
	 */
	u32 (*get_rx_queue_size)(struct gcip_mailbox *mailbox);
	/*
	 * Gets the head of mailbox receive queue.
	 *
	 * Context: rx_queue_lock.
	 */
	u32 (*get_rx_queue_head)(struct gcip_mailbox *mailbox);
	/*
	 * Gets the tail of mailbox receive queue.
	 *
	 * Context: rx_queue_lock.
	 */
	u32 (*get_rx_queue_tail)(struct gcip_mailbox *mailbox);
	/*
	 * Increases the head of mailbox receive queue by @inc.
	 *
	 * Context: rx_queue_lock.
	 */
	void (*inc_rx_queue_head)(struct gcip_mailbox *mailbox, u32 inc);
	/*
	 * Acquires the lock of rx_queue. If @try is true, "_trylock" functions can be used, but
	 * also it can be ignored. If the lock will make the context atomic, @atomic must be set
	 * to true. Returns 1 if succeed, 0 if failed.
	 *
	 * This callback will be called in the following situations:
	 * - Fetch response(s) from the rx_queue.
	 *
	 * The lock can be a mutex lock or a spin lock. However, if @try is considered and the
	 * "_trylock" is used, it must be a spin lock only.
	 *
	 * The lock will be released by calling `release_rx_queue_lock` callback.
	 *
	 * Context: normal and in_interrupt().
	 */
	int (*acquire_rx_queue_lock)(struct gcip_mailbox *mailbox, bool try, bool *atomic);
	/*
	 * Releases the lock of rx_queue which is acquired by calling `acquire_rx_queue_lock`.
	 *
	 * Context: rx_queue_lock.
	 */
	void (*release_rx_queue_lock)(struct gcip_mailbox *mailbox);

	/* Mandatory if GCIP_MAILBOX_MODE_RX_RSP is on. */
	/*
	 * Gets the sequence number of @resp queue element.
	 *
	 * Context: wait_list_lock.
	 */
	u64 (*get_resp_elem_seq)(struct gcip_mailbox *mailbox, void *resp);
	/*
	 * Sets the sequence number of @resp queue element.
	 *
	 * Context: tx_queue_lock.
	 */
	void (*set_resp_elem_seq)(struct gcip_mailbox *mailbox, void *resp, u64 seq);

	/* Mandatory if GCIP_MAILBOX_MODE_RX_RSP and GCIP_MAILBOX_MODE_RX_CMD are both on. */
	/**
	 * is_rx_elem_reversed() - Distinguishes whether the received element is rsp or rev-cmd.
	 * @mailbox: The pointer to the gcip_mailbox object to interact with mailbox interfaces.
	 * @rx_elem: The received element to be distinguished.
	 *
	 * Context: normal and in_interrupt().
	 * Return: true if the @elem is a reversed command.
	 */
	bool (*is_rx_elem_reversed)(struct gcip_mailbox *mailbox, const void *rx_elem);

	/* Mandatory if GCIP_MAILBOX_MODE_RX_CMD is on. */
	/**
	 * handle_reversed_command() - The handler of the received reversed command.
	 * @mailbox: The pointer to the gcip_mailbox object to interact with mailbox interfaces.
	 * @reversed_cmd: The reversed command to be handled.
	 *
	 * Context: normal and in_interrupt().
	 * Return: 0 on success, or a negative errno otherwise.
	 */
	int (*handle_reversed_command)(struct gcip_mailbox *mailbox, const void *reversed_cmd);

	/* Optional. */
	/*
	 * This callback will be called before putting the @resp into @mailbox->wait_list and
	 * putting @cmd of @resp into the command queue. After this callback returns, the consumer
	 * is able to start processing it and the mailbox is going to wait for it. Therefore, this
	 * callback is the final checkpoint of deciding whether it is good to wait for the response
	 * or not. If you don't want to wait for it, return a non-zero value error.
	 *
	 * If the implement side has its own wait queue, this callback is suitable to put @resp or
	 * @awaiter into that.
	 *
	 * If @resp is synchronous, @awaiter will be NULL.
	 *
	 * Context: cmd_queue_lock.
	 */
	int (*before_enqueue_wait_list)(struct gcip_mailbox *mailbox, void *resp,
					struct gcip_mailbox_awaiter *awaiter);
	/*
	 * This callback will be called after putting the @cmd to the command queue. It can be used
	 * for triggering the doorbell. Returns 0 on success, or returns error code otherwise.
	 *
	 * Context: cmd_queue_lock.
	 */
	int (*after_enqueue_cmd)(struct gcip_mailbox *mailbox, void *cmd);
	/*
	 * This callback will be called after fetching responses. It can be used for triggering
	 * a signal to break up waiting consuming the response queue. This is called without
	 * holding any locks.
	 * - @num_resps: the number of fetched responses.
	 *
	 * Context: normal and in_interrupt().
	 */
	void (*after_fetch_resps)(struct gcip_mailbox *mailbox, u32 num_resps);
	/*
	 * Checks if the block is off.
	 *
	 * Context: in_interrupt()
	 */
	bool (*is_block_off)(struct gcip_mailbox *mailbox);
	/*
	 * Called when command @cmd fails to be sent, or the response times out.
	 *
	 * Context: normal.
	 */
	void (*on_error)(struct gcip_mailbox *mailbox, void *cmd, int err);
};

struct gcip_mailbox_wait_list {
	spinlock_t list_lock;
	struct list_head list;
};

/**
 * struct gcip_mailbox - The core object to provide the GCIP mailbox framework.
 * @dev: The device used for logging and memory allocation.
 * @mode: The operating mode of the mailbox.
 * @queue_wrap_bit: The wrap bit for both Tx and Tx queues.
 * @cur_seq: The sequence number of the last attempted command, the assignment starts from 1.
 * @tx_queue: The pointer to the Tx queue.
 * @tx_elem_size: The size of element of Tx queue.
 * @rx_queue: The pointer to the Rx queue.
 * @rx_elem_size: The size of element of Rx queue.
 * @wait_list: The pointer to the wait list used for asynchronous responses.
 *             This can point to either @wait_list_internal or an externally provided wait list.
 * @wait_list_internal: The internal wait list storage, used if no external wait list is provided.
 * @timeout: The timeout in milliseconds for the mailbox.
 * @ops: The pointer to the mailbox operators.
 * @data: The user-defined data.
 */
struct gcip_mailbox {
	struct device *dev;
	u8 mode;
	u64 queue_wrap_bit;
	atomic64_t cur_seq;
	void *tx_queue;
	u32 tx_elem_size;
	void *rx_queue;
	u32 rx_elem_size;
	struct gcip_mailbox_wait_list *wait_list;
	struct gcip_mailbox_wait_list wait_list_internal;
	u32 timeout;
	const struct gcip_mailbox_ops *ops;
	void *data;
};

/**
 * struct gcip_mailbox_args - Arguments for gcip_mailbox_init().
 * @dev: Same as gcip_mailbox.dev.
 * @mode: Same as gcip_mailbox.mode.
 * @queue_wrap_bit: Same as gcip_mailbox.queue_wrap_bit.
 * @tx_queue: Same as gcip_mailbox.tx_queue.
 * @tx_elem_size: Same as gcip_mailbox.tx_elem_size.
 * @rx_queue: Same as gcip_mailbox.rx_queue.
 * @rx_elem_size: Same as gcip_mailbox.rx_elem_size.
 * @timeout: Same as gcip_mailbox.timeout.
 * @ops: Same as gcip_mailbox.ops.
 * @data: Same as gcip_mailbox.data.
 * @wait_list_external: A external wait list to be used by the mailbox.
 *                      If NULL, a internal wait list will be initialized and used.
 *                      This allows mailboxes to share the same response handling infrastructure.
 */
struct gcip_mailbox_args {
	struct device *dev;
	u8 mode;
	u32 queue_wrap_bit;
	void *tx_queue;
	u32 tx_elem_size;
	void *rx_queue;
	u32 rx_elem_size;
	u32 timeout;
	const struct gcip_mailbox_ops *ops;
	void *data;
	struct gcip_mailbox_wait_list *wait_list_external;
};

/* Initializes a mailbox object. */
int gcip_mailbox_init(struct gcip_mailbox *mailbox, const struct gcip_mailbox_args *args);

/* Releases a mailbox object which is initialized by gcip_mailbox_init */
void gcip_mailbox_release(struct gcip_mailbox *mailbox);

void gcip_mailbox_wait_list_init(struct gcip_mailbox_wait_list *wait_list);

/*
 * Fetches and handles responses, then wakes up threads that are waiting for a response.
 * To consume response queue and get responses, this function should be used as deferred work
 * such as `struct work_struct` or `struct kthread_work`.
 *
 * Note: this worker is scheduled in the IRQ handler, to prevent use-after-free or race-condition
 * bugs, cancel all works before free the mailbox.
 */
void gcip_mailbox_consume_responses_work(struct gcip_mailbox *mailbox);

/*
 * The role of this function is the same with the `gcip_mailbox_consume_responses_work` function
 * above, but expected to be called from the non-deferred work. The function guarantees that all
 * responses in the mailbox at the moment have been processed when the function returns.
 *
 * If the purpose of calling this function is to handle un-processed arrived responses when a client
 * is going to stop using the mailbox, the caller should guarantee that the IP won't return
 * responses for the client anymore first.
 *
 * Note that it is recommended to call this function in the normal context only. Otherwise, please
 * keep in mind that if the `handle_awaiter_arrived`, `handle_reversed_command`,
 * `is_rx_elem_reversed` or `after_fetch_resps` operators can sleep, this function shouldn't be
 * called in the IRQ context.
 */
void gcip_mailbox_consume_responses(struct gcip_mailbox *mailbox);

/**
 * gcip_mailbox_send_cmd_no_rsp() - Pushes an element to cmd queue, does not wait for a response.
 * @mailbox: The pointer to the gcip_mailbox object to interact with mailbox interfaces.
 * @cmd: The pointer to the command to be pushed to the command queue.
 *
 * If a response is sent it will be logged and discarded, no awaiter is pushed to the wait list.
 *
 * Return: 0 on success, or a negative errno otherwise.
 */
int gcip_mailbox_send_cmd_no_rsp(struct gcip_mailbox *mailbox, void *cmd);

/*
 * Pushes an element to cmd queue and waits for the response (synchronous).
 * Returns -ETIMEDOUT if no response is received within mailbox->timeout msecs.
 *
 * Returns the code of response, or a negative errno on error.
 * @resp is updated with the response, as to retrieve returned retval field.
 */
int gcip_mailbox_send_cmd(struct gcip_mailbox *mailbox, void *cmd, void *resp);

/**
 * gcip_mailbox_send_cmd_async() - Sends an asynchronous command.
 * @mailbox: The pointer to the gcip_mailbox object to interact with mailbox interfaces.
 * @cmd: The pointer to the command to be pushed to the command queue.
 * @awaiter: The pointer to the awaiter to be registered to the wait list.
 *
 * This function accepts an instance of `struct gcip_mailbox_awaiter` which handles the arrival and
 * time-out of the response. The implementation side can cancel the asynchronous response by calling
 * the `gcip_mailbox_cancel_awaiter` or `gcip_mailbox_cancel_timeout_work` function with it.
 *
 * Arrived asynchronous response will be handled by `handle_awaiter_arrived` callback and timed out
 * asynchronous response will be handled by `handle_awaiter_timedout` callback. Those callbacks
 * will pass the @awaiter as a parameter. The response can be accessed from `resp` member of it.
 *
 * If the mailbox is released before the response arrives, all the waiting asynchronous responses
 * will be flushed. In this case, the `handle_awaiter_flushed` callback will be called for that
 * response and @awaiter don't have to be released by the implementation side.
 * (i.e, the `gcip_mailbox_awaiter_put` function will be called internally.)
 *
 * Note: if the life cycle of the mailbox is longer than the caller part, you should make sure
 * that the callbacks don't access the variables of caller part after the release of it.
 */
int gcip_mailbox_send_cmd_async(struct gcip_mailbox *mailbox, void *cmd,
				struct gcip_mailbox_awaiter *awaiter);

/**
 * gcip_mailbox_cancel_awaiter() - Cancels awaiting the asynchronous response.
 * @awaiter: The awaiter to be canceled.
 *
 * This function will remove @awaiter from the waiting list to make it not to be handled by the
 * arrived callback. Also, it will cancel the timeout work of @awaiter synchronously.
 *
 * However, by the race condition, the arrived or timedout callback can be executed BEFORE this
 * function returns. (i.e, this function and arrived/timedout callback are called at the same time
 * but the callback acquired the lock earlier.) In this case, the function will wait for the
 * completion of arrived or timedout callbacks.
 *
 * Therefore, AFTER the return of this function, it is guaranteed that arrived or timedout callback
 * will not be called for @awaiter.
 *
 * Note that this function will cancel or wait for the completion of arrived or timedout callbacks
 * synchronously, so make sure that the caller side doesn't hold any locks which can be acquired by
 * the arrived or timedout callbacks.
 *
 * Context: Process context. May sleep if it needs to wait for the completion of other callbacks.
 * Return:
 * * %true - The @awaiter was pending and is cancelled successfully.
 * * %false - The @awaiter was already processed by arrived or timedout handler.
 */
bool gcip_mailbox_cancel_awaiter(struct gcip_mailbox_awaiter *awaiter);

/**
 * gcip_mailbox_cancel_timeout_work() - Cancels the timeout work of the awaiter.
 * @awaiter: The awaiter to cancel the timeout work.
 *
 * The timeout work will be canceled if it is pending.
 *
 * The reference count acquired by the timeout work will be managed carefully according to the
 * result of the work's cancellation.
 */
void gcip_mailbox_cancel_timeout_work(struct gcip_mailbox_awaiter *awaiter);

/**
 * gcip_mailbox_cancel_timeout_work_sync() - Cancels the timeout work or waits for its completion.
 * @awaiter: The awaiter to cancel the timeout work.
 *
 * The synchronous version of the gcip_mailbox_cancel_timeout_work() function. If the timeout work
 * has already started before cancellation, this function will wait until it finishes.
 */
void gcip_mailbox_cancel_timeout_work_sync(struct gcip_mailbox_awaiter *awaiter);

/**
 * gcip_mailbox_awaiter_init() - Initializes the given gcip_mailbox_awaiter.
 * @awaiter: The pointer to the awaiter to be initialized.
 * @mailbox: The pointer to the gcip mailbox to interact with its interfaces.
 * @resp: The pointer to the response to be filled.
 * @ops: The pointer to the awaiter operators.
 *
 * The awaiter is expected to be released with the gcip_mailbox_awaiter_put().
 *
 * Return: 0 on success, or a negative errno otherwise.
 */
int gcip_mailbox_awaiter_init(struct gcip_mailbox_awaiter *awaiter, struct gcip_mailbox *mailbox,
			      void *resp, const struct gcip_mailbox_awaiter_ops *ops);

/**
 * gcip_mailbox_awaiter_get() - Gets the reference count of the awaiter.
 * @awaiter: The awaiter to be got.
 *
 * Return: The pointer to the awaiter.
 */
struct gcip_mailbox_awaiter *gcip_mailbox_awaiter_get(struct gcip_mailbox_awaiter *awaiter);

/**
 * gcip_mailbox_awaiter_put() - Puts the reference count of the awaiter.
 * @awaiter: The awaiter to be put.
 *
 * All the handlers (arrived/timedout/flushed) should call this function to release the reference
 * count acquired by the user.
 *
 * It will call the `release_awaiter` callback when the reference count reaches 0.
 */
void gcip_mailbox_awaiter_put(struct gcip_mailbox_awaiter *awaiter);

/**
 * gcip_mailbox_awaiter_get_ops() - Gets the pointer to the awaiter operators.
 * @awaiter: The awaiter to retrieve the operators from.
 *
 * Return: The pointer to the awaiter operators.
 */
static inline const struct gcip_mailbox_awaiter_ops *
gcip_mailbox_awaiter_get_ops(struct gcip_mailbox_awaiter *awaiter)
{
	if (!awaiter)
		return NULL;
	return awaiter->ops;
}

/*
 * Consume one response and handle it. This can be used for consuming one response quickly and then
 * schedule `gcip_mailbox_consume_responses_work` work in the IRQ handler of mailbox.
 */
void gcip_mailbox_consume_one_response(struct gcip_mailbox *mailbox, void *resp);

/* Getters for member variables of the `struct gcip_mailbox`. */

static inline void *gcip_mailbox_get_tx_queue(struct gcip_mailbox *mailbox)
{
	return mailbox->tx_queue;
}

static inline u32 gcip_mailbox_get_tx_elem_size(struct gcip_mailbox *mailbox)
{
	return mailbox->tx_elem_size;
}

static inline void *gcip_mailbox_get_rx_queue(struct gcip_mailbox *mailbox)
{
	return mailbox->rx_queue;
}

static inline u32 gcip_mailbox_get_rx_elem_size(struct gcip_mailbox *mailbox)
{
	return mailbox->rx_elem_size;
}

static inline u64 gcip_mailbox_get_queue_wrap_bit(struct gcip_mailbox *mailbox)
{
	return mailbox->queue_wrap_bit;
}

static inline struct list_head *gcip_mailbox_get_wait_list(struct gcip_mailbox *mailbox)
{
	return &mailbox->wait_list->list;
}

static inline u32 gcip_mailbox_get_timeout(struct gcip_mailbox *mailbox)
{
	return mailbox->timeout;
}

static inline void *gcip_mailbox_get_data(struct gcip_mailbox *mailbox)
{
	return mailbox->data;
}

#endif /* __GCIP_MAILBOX_H__ */
