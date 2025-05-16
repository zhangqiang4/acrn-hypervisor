/*
 * Copyright (C) 2018-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <errno.h>
#include <asm/io.h>
#include <asm/msr.h>
#include <asm/apicreg.h>
#include <asm/cpuid.h>
#include <asm/cpu_caps.h>
#include <softirq.h>
#include <trace.h>
#include <asm/irq.h>
#include <ticks.h>
#include <hw/hw_timer.h>

/**
 * @addtogroup hwmgmt_timer
 *
 * @{
 */

/**
 * @file
 * @brief Timer management functions for hardware management.
 *
 * This file contains the implementation of timer management functions for hardware management, including
 * initialization, addition, deletion, and update for timers.
 */

#define MAX_TIMER_ACTIONS	32U  /**< Maximum number of timer actions that can be scheduled. */
#define MIN_TIMER_PERIOD_US	500U /**< Minimum timer period in microseconds. */

/**
 * @brief Check whether a timer is expired.
 *
 * This function checks the current time against the timer's timeout value. If the timeout value in timer is non-zero
 * and greater than the current time, then the function considers the timer non-expired and stores the time difference
 * in the \p delta.
 *
 * @param[in] timer  The pointer to the timer to be checked.
 * @param[in] now    The current time to compare.
 * @param[out] delta The pointer to the place that store the time delta.
 *
 * @return A boolean value, indicates whether the timer is expired. True if expired, otherwise false.
 *
 * @pre timer != NULL
 *
 * @post N/A
 */
bool timer_expired(const struct hv_timer *timer, uint64_t now, uint64_t *delta)
{
	bool ret = true;
	uint64_t delt = 0UL;

	if  ((timer->timeout != 0UL) && (now < timer->timeout)) {
		ret = false;
		delt = timer->timeout - now;
	}

	if (delta != NULL) {
		*delta = delt;
	}

	return ret;
}

/**
 * @brief Check whether the timer is active.
 *
 * This function checks whether this timer is in the timer list. If this timer is in the list, it returns true;
 * otherwise, it returns false.
 *
 * @param[in] timer The timer which needs to check.
 *
 * @return A boolean value, indicates whether the timer is started.
 *
 * @pre timer != NULL
 *
 * @post N/A
 */
bool timer_is_started(const struct hv_timer *timer)
{
	return (!list_empty(&timer->node));
}

/**
 * @brief Run a timer.
 *
 * This function executes callback function in timer struct to run a timer. When the callback function is registered
 * and the timeout value is non-zero, the function will execute the callback function.
 *
 * @param[in] timer The timer which needs to run.
 *
 * @return None
 *
 * @pre timer != NULL
 *
 * @post N/A
 */
static void run_timer(const struct hv_timer *timer)
{
	/* deadline = 0 means stop timer, we should skip */
	if ((timer->func != NULL) && (timer->timeout != 0UL)) {
		timer->func(timer->priv_data);
	}

	TRACE_2L(TRACE_TIMER_ACTION_PCKUP, timer->timeout, 0UL);
}

/**
 * @brief Update the physical timer.
 *
 * This function updates the physical timer by finding the next event timer and programming the TSC deadline.
 *
 * @param[in] cpu_timer The per-CPU timer structure.
 *
 * @return None
 *
 * @pre cpu_timer != NULL
 *
 * @post N/A
 */
static inline void update_physical_timer(struct per_cpu_timers *cpu_timer)
{
	struct hv_timer *timer = NULL;

	/* find the next event timer */
	if (!list_empty(&cpu_timer->timer_list)) {
		timer = container_of((&cpu_timer->timer_list)->next,
			struct hv_timer, node);

		/* it is okay to program a expired time */
		msr_write(MSR_IA32_TSC_DEADLINE, timer->timeout);
	}
}

/**
 * @brief Add a timer to the CPU timer list.
 *
 * This function adds a timer to the CPU timer list. It inserts the timer into the list in order of the timeout value.
 *
 * @param[inout] cpu_timer The per-CPU timer structure.
 * @param[inout] timer The timer which needs to add.
 *
 * @return A boolean value, indicating whether the timer was added to the head of the list. True if the new timer is
 *         the head of the list, otherwise false.
 *
 * @pre cpu_timer != NULL
 * @pre timer != NULL
 *
 * @post N/A
 */
static bool local_add_timer(struct per_cpu_timers *cpu_timer,
			struct hv_timer *timer)
{
	struct list_head *pos, *prev;
	struct hv_timer *tmp;
	uint64_t tsc = timer->timeout;

	prev = &cpu_timer->timer_list;
	list_for_each(pos, &cpu_timer->timer_list) {
		tmp = container_of(pos, struct hv_timer, node);
		if (tmp->timeout < tsc) {
			prev = &tmp->node;
		} else {
			break;
		}
	}

	list_add(&timer->node, prev);

	return (prev == &cpu_timer->timer_list);
}

/**
 * @brief Add a timer to corresponding physical CPU list.
 *
 * This function checks timer pointer, if the timer is NULL or timer->func is null and timer->timeout == 0, then
 * return -EINVAL for fail.
 *
 * In one-shot mode, the timer is started by programming its initial-count register. The initial count value is then
 * copied into the current-count register and count-down begins. After the timer reaches zero, a timer interrupt is
 * generated and the timer remains at its 0 value until reprogrammed.
 *
 * In periodic mode, the period is adjusted to be no less than the minimum allowed period (MIN_TIMER_PERIOD_US)
 * by converting this minimum period from microseconds to timer ticks and taking the maximum of the current period
 * and this minimum value first. The timer is started by writing to the initial-count register (as in one-shot mode),
 * and the value written is copied into the current-count register, which counts down. The current-count register is
 * automatically reloaded from the initial-count register when the count reaches 0 and a timer interrupt is generated,
 * and the countdown is repeated. If during the count-down process the initial-count register is set, counting will
 * restart, using the new initial-count value.
 *
 * @param[in] timer The timer to add.
 *
 * @return A int32_t value indicating whether the timer is added.
 *
 * @retval 0 Add timer success.
 * @retval -EINVAL Add timer fail.
 *
 * @pre N/A
 *
 * @post N/A
 */
int32_t add_timer(struct hv_timer *timer)
{
	struct per_cpu_timers *cpu_timer;
	uint16_t pcpu_id;
	int32_t ret = 0;
	uint64_t rflags;

	if ((timer == NULL) || (timer->func == NULL) || (timer->timeout == 0UL)) {
		ret = -EINVAL;
	} else {
		ASSERT(list_empty(&timer->node), "add timer again!\n");

		/* limit minimal periodic timer cycle period */
		if (timer->mode == TICK_MODE_PERIODIC) {
			timer->period_in_cycle = max(timer->period_in_cycle, us_to_ticks(MIN_TIMER_PERIOD_US));
		}

		pcpu_id  = get_pcpu_id();
		cpu_timer = &per_cpu(cpu_timers, pcpu_id);

		CPU_INT_ALL_DISABLE(&rflags);
		/* update the physical timer if we're on the timer_list head */
		if (local_add_timer(cpu_timer, timer)) {
			update_physical_timer(cpu_timer);
		}
		CPU_INT_ALL_RESTORE(rflags);

		TRACE_2L(TRACE_TIMER_ACTION_ADDED, timer->timeout, 0UL);
	}

	return ret;

}

/**
 * @brief Initialize a timer structure.
 *
 * When timer is non-NULL, this function initializes a timer structure, setting the header of the timer list with
 * the provided parameters. If the given period_in_cycle is non-zero, the timer is initialized as a periodic timer;
 * otherwise, it is initialized as a one-shot timer.
 *
 * @param[in] timer Pointer to the timer.
 * @param[in] func IRQ callback function to be called when the timer expires.
 * @param[in] priv_data Function private data.
 * @param[in] timeout TSC deadline for the timer interrupt.
 * @param[in] period_in_cycle Period of the periodic timer in unit of TSC cycles.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark Don't initialize a timer twice if it has been added to the timer list after calling add_timer. If want to,
 * delete the timer from the list first.
 */
void initialize_timer(struct hv_timer *timer,
			timer_handle_t func, void *priv_data,
			uint64_t timeout, uint64_t period_in_cycle)
{
	if (timer != NULL) {
		timer->func = func;
		timer->priv_data = priv_data;
		timer->timeout = timeout;
		if (period_in_cycle > 0UL) {
			timer->mode = TICK_MODE_PERIODIC;
			timer->period_in_cycle = period_in_cycle;
		} else {
			timer->mode = TICK_MODE_ONESHOT;
			timer->period_in_cycle = 0UL;
		}
		INIT_LIST_HEAD(&timer->node);
	}
}

/**
 * @brief Update a timer.
 *
 * When timer is non-NULL, this function updates the timeout and period of a timer. If the given period is non-zero,
 * the timer is updated as a periodic timer; otherwise, it is updated as a one-shot timer.
 *
 * @param[in] timer   The timer to update.
 * @param[in] timeout The new timeout value.
 * @param[in] period  The new period value.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark Do not call it in the timer callback function or interrupt content.
 */
void update_timer(struct hv_timer *timer, uint64_t timeout, uint64_t period)
{
	if (timer != NULL) {
		timer->timeout = timeout;
		if (period > 0UL) {
			timer->mode = TICK_MODE_PERIODIC;
			timer->period_in_cycle = period;
		} else {
			timer->mode = TICK_MODE_ONESHOT;
			timer->period_in_cycle = 0UL;
		}
	}
}

/**
 * @brief Delete a timer.
 *
 * This function deletes a timer from the timer list.
 *
 * @param[in] timer The timer to delete.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark Don't call it in the timer callback function or interrupt content.
 */
void del_timer(struct hv_timer *timer)
{
	uint64_t rflags;

	CPU_INT_ALL_DISABLE(&rflags);
	if ((timer != NULL) && !list_empty(&timer->node)) {
		list_del_init(&timer->node);
	}
	CPU_INT_ALL_RESTORE(rflags);
}

/**
 * @brief Initialize per-physical CPU timer for the given physical CPU.
 *
 * This function initialize the timer of per physical CPU, setting the header of timer list.
 *
 * @param[in] pcpu_id Physical CPU ID.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
static void init_percpu_timer(uint16_t pcpu_id)
{
	struct per_cpu_timers *cpu_timer;

	cpu_timer = &per_cpu(cpu_timers, pcpu_id);
	INIT_LIST_HEAD(&cpu_timer->timer_list);
}

/**
 * @brief Handle timer softirq for the given physical CPU.
 *
 * This function handles the timer softirq for a specific physical CPU. It processes the list of timers associated
 * with the given physical CPU, checking each timer to see if it has expired. If a timer has expired, it is removed
 * from the list and its associated function is executed. For periodic timers, the timeout is updated to the next
 * period, and the timer is re-added to the list. The function ensures that no more than a predefined number of
 * timers (MAX_TIMER_ACTIONS) are processed in one invocation to prevent excessive delays. This is crucial for
 * maintaining system responsiveness, especially when dealing with periodic timers that could otherwise cause
 * infinite loops if their execution is delayed. After processing the timers, the function updates the physical timer
 * to the nearest timer in the list to ensure accurate timing for future events.
 *
 * @param[in] pcpu_id Physical CPU ID.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
static void timer_softirq(uint16_t pcpu_id)
{
	struct per_cpu_timers *cpu_timer;
	struct hv_timer *timer;
	struct list_head *pos, *n;
	uint32_t tries = MAX_TIMER_ACTIONS;
	uint64_t current_tsc = cpu_ticks();

	/* handle passed timer */
	cpu_timer = &per_cpu(cpu_timers, pcpu_id);

	/* This is to make sure we are not blocked due to delay inside func()
	 * force to exit irq handler after we serviced >31 timers
	 * caller used to local_add_timer() for periodic timer, if there is a delay
	 * inside func(), it will infinitely loop here, because new added timer
	 * already passed due to previously func()'s delay.
	 */
	list_for_each_safe(pos, n, &cpu_timer->timer_list) {
		timer = container_of(pos, struct hv_timer, node);
		/* timer expried */
		tries--;
		if ((timer->timeout <= current_tsc) && (tries != 0U)) {
			del_timer(timer);

			run_timer(timer);

			if (timer->mode == TICK_MODE_PERIODIC) {
				/* update periodic timer fire tsc */
				timer->timeout += timer->period_in_cycle;
				(void)local_add_timer(cpu_timer, timer);
			} else {
				timer->timeout = 0UL;
			}
		} else {
			break;
		}
	}

	/* update nearest timer */
	update_physical_timer(cpu_timer);
}

/**
 * @brief Initialize timer.
 *
 * This function initializes timer for the current physical CPU. This function initializes the header of timer list of
 * per physical CPU, registers softirq (if current is BSP CPU) and initializes X2APIC timer.
 *
 * This function is invoked at the time of CPU initialization. Since global vector table is being used on all physical
 * CPUs, it's not necessary to request timer IRQ at each CPU initialization. So only BSP registers timer IRQ.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
void timer_init(void)
{
	uint16_t pcpu_id = get_pcpu_id();

	init_percpu_timer(pcpu_id);

	if (pcpu_id == BSP_CPU_ID) {
		register_softirq(SOFTIRQ_TIMER, timer_softirq);
	}

	init_hw_timer();
}

/**
 * @}
 */
