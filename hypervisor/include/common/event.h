#ifndef EVENT_H
#define EVENT_H
#include <asm/lib/spinlock.h>

/**
 * @addtogroup hwmgmt_schedule
 *
 * @{
 */

/**
 * @file
 *
 * @brief Define struct type and declare functions for schedule event.
 */

/**
 * @brief Struct to define schedule event.
 */
struct sched_event {
	spinlock_t lock; /**< Spinlock to protect schedule event integrity */
	bool set; /**< Whether the event has been signaled. Clear after receive */
	struct thread_object* waiting_thread; /**< Pointer to the thread that is waiting on the event */
};

void init_event(struct sched_event *event);
void reset_event(struct sched_event *event);
void wait_event(struct sched_event *event);
void signal_event(struct sched_event *event);

#endif /* EVENT_H */

/**
 * @}
 */