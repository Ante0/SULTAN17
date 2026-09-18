/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2022 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#ifndef _WLAN_PTRACKER_DEBUG_H
#define _WLAN_PTRACKER_DEBUG_H

#define PTRACKER_PREFIX "wlan_ptracker"

#define ptracker_err(core, fmt, ...)					\
	do {								\
		dev_err(&core->device, fmt, ##__VA_ARGS__);		\
	} while (0)

#define ptracker_info(core, fmt, ...)					\
	do {								\
		dev_info(&core->device, fmt, ##__VA_ARGS__);		\
	} while (0)

#define ptracker_dbg(core, fmt, ...)					\
	do {								\
		dev_dbg(&core->device, fmt, ##__VA_ARGS__);		\
	} while (0)

#ifdef TP_DEBUG
#define tp_info(tp, fmt, ...)						\
	do {								\
		if ((tp)->debug && (tp)->dev)				\
			dev_info(tp->dev->dev, fmt, ##__VA_ARGS__);	\
	} while (0)
#else
#define tp_info(tp, fmt, ...)
#endif

#endif  /* _WLAN_PTRACKER_DEBUG_H */
