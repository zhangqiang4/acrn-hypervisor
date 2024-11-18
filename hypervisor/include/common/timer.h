/*
 * Copyright (C) 2018-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef COMMON_TIMER_H
#define COMMON_TIMER_H

#include <list.h>
#include <ticks.h>

/**
 * @defgroup hwmgmt_timer hwmgmt.timer
 * @ingroup hwmgmt
 *
 * @brief Timer
 *
 * This module provides timer management functionalities, including initialization, update, and deletion
 * of timers.
 *
 * @{
 */

/**
 * @file
 * @brief Timer management header file
 *
 * This file contains the declarations for timer management functions and data structures.
 */

 /**
  * @brief Timer handle function pointer type.
  *
  * This typedef defines a function pointer type for timer callback functions. The function should take a single
  * argument, which is a pointer to user-defined data associated with the timer.
  */
typedef void (*timer_handle_t)(void *data);

/**
 * @brief Definition of timer tick mode.
 *
 * The timer can be configured through the timer LVT entry for one-shot or periodic operation.
 */
enum tick_mode {
	TICK_MODE_ONESHOT = 0,	/**< one-shot mode */
	TICK_MODE_PERIODIC,	    /**< periodic mode */
};

/**
 * @brief Definition of timers for per-cpu.
 *
 * This structure holds the list of active timers for each CPU.
 */
struct per_cpu_timers {
	struct list_head timer_list;	/**< it's for runtime active timer list */
};

/**
 * @brief Definition of timer.
 *
 * This structure represents a timer with its configuration and state.
 */
struct hv_timer {
	struct list_head node;		/**< link all timers */
	enum tick_mode mode;		/**< timer mode: one-shot or periodic */
	uint64_t timeout;		/**< tsc deadline to interrupt */
	uint64_t period_in_cycle;	/**< period of the periodic timer in CPU ticks */
	timer_handle_t func;		/**< callback if time reached */
	void *priv_data;		/**< func private data */
};

/* External Interfaces */

void initialize_timer(struct hv_timer *timer,
		      timer_handle_t func, void *priv_data,
		      uint64_t timeout, uint64_t period_in_cycle);

bool timer_expired(const struct hv_timer *timer, uint64_t now, uint64_t *delta);

bool timer_is_started(const struct hv_timer *timer);

void update_timer(struct hv_timer *timer, uint64_t timeout, uint64_t period);

int32_t add_timer(struct hv_timer *timer);

void del_timer(struct hv_timer *timer);

void timer_init(void);

#endif /* COMMON_TIMER_H */

/**
 * @}
 */
