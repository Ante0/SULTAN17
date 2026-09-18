// SPDX-License-Identifier: GPL-2.0-only
/* core.c
 *
 * Android Vendor Hook Support
 *
 * Copyright 2026 Google LLC
 */

#include <linux/futex.h>
#include <linux/plist.h>
#include <linux/sched.h>
#include <linux/sched/cputime.h>
#include <kernel/futex/futex.h>
#include <kernel/sched/sched.h>

#include "sched_priv.h"

void vh_alter_futex_plist_add_pixel_mod(void *data, struct plist_node *q_list,
						struct plist_head *hb_chain, bool *already_on_hb)
{
	struct futex_q *this, *next;
	struct plist_node *current_node = q_list;
	struct plist_node *this_node;

	if (!get_adpf(current, true)) {
		*already_on_hb = false;
		return;
	}

	plist_for_each_entry_safe(this, next, hb_chain, list) {
		if ((!this->pi_state || !this->rt_waiter) && !get_adpf(this->task, true)) {
			this_node = &this->list;
			list_add(&current_node->node_list,
				 this_node->node_list.prev);
			*already_on_hb = true;
			return;
		}
	}

	*already_on_hb = false;
}
