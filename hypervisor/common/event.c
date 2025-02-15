/*
 * Copyright (C) 2018-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <asm/cpu.h>
#include <schedule.h>
#include <event.h>
#include <logmsg.h>

/**
 * @addtogroup hwmgmt_schedule
 *
 * @{
 */

/**
 * @file
 *
 * @brief Implementation of schedule event.
 *
 * The schedule event feature provides threads with the ability to wait on a event.
 * Upon wait, the thread is put to sleep until another thread signals the event.
 * Only supports exclusive waiting.
 */

/**
 * @brief Initialize a schedule event.
 *
 * - Initialize spinlock of the event.
 * - Clear the 'set' state of the event.
 * - Clear waiting thread.
 *
 * @param[inout] event Pointer to the schedule event to be initialized.
 *
 * @return None.
 */
void init_event(struct sched_event *event)
{
	spinlock_init(&event->lock);
	event->set = false;
	event->waiting_thread = NULL;
}

/**
 * @brief Reset a schedule event.
 *
 * - Clear the 'set' state of the event.
 * - Clear waiting thread.
 *
 * @param[inout] event Pointer to the schedule event to reset.
 *
 * @return None.
 */
void reset_event(struct sched_event *event)
{
	uint64_t rflag;

	spinlock_irqsave_obtain(&event->lock, &rflag);
	event->set = false;
	event->waiting_thread = NULL;
	spinlock_irqrestore_release(&event->lock, rflag);
}

/**
 * @brief Wait a schedule event.
 *
 * Support exclusive waiting only. The thread calling this function will be put to
 * sleep until another thread signals the event.
 *
 * During wait, the pCPU could be scheduled to run the idle thread when run queue
 * is empty. Signal_event() can happen when schedule() is in process.
 * This signal_event is not going to be lost, for the idle thread will always
 * check need_reschedule() after it is switched to at schedule().
 *
 * @param[inout] event Pointer to the schedule event to wait for.
 *
 * @return None.
 *
 * @pre event != NULL
 */
void wait_event(struct sched_event *event)
{
	uint64_t rflag;

	spinlock_irqsave_obtain(&event->lock, &rflag);
	ASSERT((event->waiting_thread == NULL), "only support exclusive waiting");
	event->waiting_thread = sched_get_current(get_pcpu_id());
	while (!event->set && (event->waiting_thread != NULL)) {
		sleep_thread(event->waiting_thread);
		spinlock_irqrestore_release(&event->lock, rflag);
		schedule();
		spinlock_irqsave_obtain(&event->lock, &rflag);
	}
	event->set = false;
	event->waiting_thread = NULL;
	spinlock_irqrestore_release(&event->lock, rflag);
}

/**
 * @brief Signal a schedule event.
 *
 * Signal a schedule event, so that the thread waiting on the event will be waked up.
 * If there is no thread wait on the event, nothing will happen except event->set is
 * set to true. So that when next time a thread attempts to wait on the event will get
 * the signal and not be put to sleep.
 *
 * @param[inout] event Pointer to the schedule event to be signalled.
 *
 * @return None.
 *
 * @pre event != NULL
 */
void signal_event(struct sched_event *event)
{
	uint64_t rflag;

	spinlock_irqsave_obtain(&event->lock, &rflag);
	event->set = true;
	if (event->waiting_thread != NULL) {
		wake_thread(event->waiting_thread);
	}
	spinlock_irqrestore_release(&event->lock, rflag);
}

/**
 * @}
 */