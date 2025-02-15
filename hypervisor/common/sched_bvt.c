/*
 * Copyright (C) 2020-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <list.h>
#include <asm/per_cpu.h>
#include <schedule.h>
#include <ticks.h>

/**
 * @addtogroup hwmgmt_schedule
 *
 * @{
 */

/**
 * @file
 *
 * @brief Implementation of BVT scheduler.
 *
 * This file provides implementation of BVT scheduler.
 * BVT (Borrowed virtual time) scheduler is used to schedule threads on pCPU.
 * It has the concept of virtual time, thread with earliest virtual time is
 * dispatched first.
 *
 * Main concepts:
 *
 *  - tick timer:
 *     a period tick is used to measure the physical time in units of MCU
 *     (minimum charging unit).
 *  - runqueue:
 *     thread in the runqueue is ordered by virtual time.
 *  - weight:
 *     each thread receives a share of the pCPU in proportion to its
 *     weight.
 *  - context switch allowance:
 *     the physical time by which the current thread is allowed to advance
 *     beyond the next runnable thread.
 *  - warp:
 *     a thread with warp enabled will have a change to minus a value (Wi)
 *     from virtual time to achieve higher priority.
 *  - virtual time:
 *     - AVT: actual virtual time, advance in proportional to weight.
 *     - EVT: effective virtual time.
 *          EVT <- AVT - ( warp ? Wi : 0 )
 *     - SVT: scheduler virtual time, the minimum AVT in the runqueue.
 */

/**
 * @brief Defines value of MCU in milliseconds.
 */
#define BVT_MCU_MS		1U
/**
 * @brief Defines value of CSA in MCUs.
 */
#define BVT_CSA_MCU		5U

/**
 * @brief Defines minimal weight we can set to the thread.
 */
#define BVT_WEIGHT_MIN		1U
/**
 * @brief Defines max weight we can set to the thread.
 *
 * Limit the weight range to [1, 128]. It's enough to allocate pCPU resources
 * for different types of vCPUs.
 */
#define BVT_WEIGHT_MAX		128U

/**
 * @brief Defines the minimal VT ratio value.
 *
 * the VT (Virtual Time) ratio is proportional to 1 / weight and making the VT
 * ratio an integer will ease translation between virtual time and physical
 * time.
 *
 * Max (theoretical VT ratio - actual VT ratio) is
 *   1 (< 1 because of integer round down).
 *
 * The minimum total VT ratios of VCPUs (at least two) is
 *   2 * 8 (Min per-vcpu VT ratio)
 *
 * So the max VT ratio share error is about 1/16.
 * To reduce it, we can enlarge the BVT_VT_RATIO_MIN.
 * However increasing VT ratio will reduce the total time needed to overflow
 * AVT. AVT is of type int64_t. The max VT ratio is 1024. MCU is 1 ms.
 * So the time to overflow AVT is about:
 *   2^63  / (1024 * 1000) s, i.e. ~= 9 * 10^12(s) ~= 10^8 day
 *
 * It's so large that we can ignore the AVT overflow case.
 */
#define BVT_VT_RATIO_MIN	8U

/**
 * @brief Defines the max VT ratio value.
 *
 * Max VT ratio is the ratio at max weight.
 */
#define BVT_VT_RATIO_MAX	(BVT_WEIGHT_MAX * BVT_VT_RATIO_MIN / BVT_WEIGHT_MIN)

/**
 * @brief Struct that stores BVT data for each thread object.
 */
struct sched_bvt_data {
	struct list_head list; /**< Link thread objects in queues. Keep it as the first item */
	uint64_t mcu; /**< Minimum charging unit in cycles */
	uint8_t weight;  /**< A thread receives a share of pCPU time in proportion to its weight */
	uint64_t vt_ratio; /**< Virtual time advance variable, proportional to 1 / weight */
	bool warp_on; /**< Whether warp is on for latency-sensitive purpose */
	int32_t warp_value; /**< The value reduced to EVT when warp, boost the thread's priority */
	uint32_t warp_limit; /**< Max time for thread to run warped. Becomes unwarped afterwards */
	uint32_t unwarp_period; /**< Min unwarp time before next warp */
	int64_t avt; /**< Actual virtual time in units of mcu */
	int64_t evt; /**< Effective virtual time in units of mcu */
	uint64_t residual; /**< Remaining VT that a thread has after it has borrowed time */

	uint64_t start_tsc; /**< TSC time stamp when a thread starts to run */
};

/**
 * @brief Check if the thread object has been put into a queue.
 *
 * By checking if its list head is empty (next points to itself).
 *
 * @param[in] obj Pointer to the thread object to be checked.
 *
 * @return Bool value of whether thread object is in any queue.
 *
 * @retval true The thread object is in a queue.
 * @retval false The thread object is not in a queue.
 *
 * @pre obj != NULL
 * @pre obj->data != NULL
 */
static bool is_inqueue(struct thread_object *obj)
{
	struct sched_bvt_data *data = (struct sched_bvt_data *)obj->data;
	return !list_empty(&data->list);
}

/**
 * @brief Update SVT value of the schedule control.
 *
 * Updated SVT with the AVT value of first thread object in its run queue.
 *
 * @param[inout] bvt_ctl Pointer to thread control of one pCPU.
 *
 * @return None.
 *
 * @pre bvt_ctl != NULL
 */
static void update_svt(struct sched_bvt_control *bvt_ctl)
{
	struct sched_bvt_data *obj_data;
	struct thread_object *tmp_obj;

	if (!list_empty(&bvt_ctl->runqueue)) {
		tmp_obj = get_first_item(&bvt_ctl->runqueue, struct thread_object, data);
		obj_data = (struct sched_bvt_data *)tmp_obj->data;
		bvt_ctl->svt = obj_data->avt;
	}
}

/**
 * @brief Add a thread object to the run queue.
 *
 * This function goes through the thread objects in the run queue, and insert the
 * given thread object in a incremental order of the EVT value.
 *
 * @param[inout] obj Pointer to the thread object to be added.
 *
 * @return None.
 *
 * @pre obj != NULL
 * @pre obj->data != NULL
 * @pre obj->sched_ctl != NULL
 * @pre obj->sched_ctl->priv != NULL
 */
static void runqueue_add(struct thread_object *obj)
{
	struct sched_bvt_control *bvt_ctl =
		(struct sched_bvt_control *)obj->sched_ctl->priv;
	struct sched_bvt_data *data = (struct sched_bvt_data *)obj->data;
	struct list_head *pos;
	struct thread_object *iter_obj;
	struct sched_bvt_data *iter_data;

	/*
	 * the earliest evt has highest priority,
	 * the runqueue is ordered by priority.
	 */

	if (list_empty(&bvt_ctl->runqueue)) {
		list_add(&data->list, &bvt_ctl->runqueue);
	} else {
		list_for_each(pos, &bvt_ctl->runqueue) {
			iter_obj = container_of(pos, struct thread_object, data);
			iter_data = (struct sched_bvt_data *)iter_obj->data;
			if (iter_data->evt > data->evt) {
				list_add_node(&data->list, pos->prev, pos);
				break;
			}
		}
		if (!is_inqueue(obj)) {
			list_add_tail(&data->list, &bvt_ctl->runqueue);
		}
	}
}

/**
 * @brief Remove a thread object from the run queue.
 *
 * @param[inout] obj Pointer to the thread object to be removed.
 *
 * @return None.
 *
 * @pre obj != NULL
 * @pre obj->data != NULL
 */
static void runqueue_remove(struct thread_object *obj)
{
	struct sched_bvt_data *data = (struct sched_bvt_data *)obj->data;

	list_del_init(&data->list);
}

/**
 * @brief Get the SVT value from schedule control associated with the given thread object.
 *
 * The SVT value indicates the minimum AVT of any runnable threads.
 *
 * @param[in] obj Pointer to the thread object.
 *
 * @return SVT value.
 *
 * @pre obj != NULL
 * @pre obj->data != NULL
 * @pre obj->sched_ctl != NULL
 * @pre obj->sched_ctl->priv != NULL
 */
static int64_t get_svt(struct thread_object *obj)
{
	struct sched_bvt_control *bvt_ctl = (struct sched_bvt_control *)obj->sched_ctl->priv;

	return bvt_ctl->svt;
}

/**
 * @brief The schedule timer tick handler.
 *
 * This schedule tick handler will make schedule request when non-idle thread is running,
 * or when there is some thread enqueued. The scheduler will decide what to do based
 * on its BVT algorithm at the schedule point before next vm entry.
 *
 * However now ACRN has adopted tickless approach. Even though it is still called tick
 * handler, it only interrupts when the thread runs out its time budget.
 *
 * @param[in] param Pointer to the priv data passed to the handler.
 *
 * @return None.
 *
 * @pre param != NULL
 */
static void sched_tick_handler(void *param)
{
	struct sched_control  *ctl = (struct sched_control *)param;
	struct sched_bvt_control *bvt_ctl = (struct sched_bvt_control *)ctl->priv;
	struct thread_object *current;
	uint16_t pcpu_id = get_pcpu_id();
	uint64_t rflags;

	obtain_schedule_lock(pcpu_id, &rflags);
	current = ctl->curr_obj;

	if (current != NULL ) {
		/* only non-idle thread need to consume run_countdown */
		if (!is_idle_thread(current)) {
			make_reschedule_request(pcpu_id);
		} else {
			if (!list_empty(&bvt_ctl->runqueue)) {
				make_reschedule_request(pcpu_id);
			}
		}
	}
	release_schedule_lock(pcpu_id, rflags);
}

/**
 * @brief Initialize the BVT scheduler on current pCPU.
 *
 * This function initializes the BVT scheduler by:
 *  - Set schedule control's priv data to &per_cpu(sched_bvt_ctl, ctl->pcpu_id).
 *  - Initialize BVT runqueue.
 *  - Setup BVT schedule timer, which will be armed when a thread is given a due time
 *    when it is scheduled to run next (in sched_bvt_pick_next()).
 *    The timer's handler is sched_tick_handler.
 *
 * @param[inout] ctl Pointer to the schedule control struct on current pCPU. BVT scheduler
 * will be initialized and associated to the schedule control.
 *
 * @return Always 0 to indicate success.
 *
 * @pre ctl != NULL
 * @pre ctl->pcpu_id == get_pcpu_id()
 */
static int sched_bvt_init(struct sched_control *ctl)
{
	struct sched_bvt_control *bvt_ctl = &per_cpu(sched_bvt_ctl, ctl->pcpu_id);
	int ret = 0;

	ASSERT(ctl->pcpu_id == get_pcpu_id(), "Init scheduler on wrong CPU!");

	ctl->priv = bvt_ctl;
	INIT_LIST_HEAD(&bvt_ctl->runqueue);

	/* The tick_timer is periodically */
	initialize_timer(&bvt_ctl->tick_timer, sched_tick_handler, ctl, 0, 0);

	return ret;
}

/**
 * @brief Deinitialize the BVT scheduler on current pCPU.
 *
 * This function deinitializes the BVT scheduler by deleting the schedule timer.
 *
 * @param[inout] ctl Pointer to the schedule control on which the BVT scheduler will be deinitialized.
 *
 * @return None.
 *
 * @pre ctl != NULL
 */
static void sched_bvt_deinit(struct sched_control *ctl)
{
	struct sched_bvt_control *bvt_ctl = (struct sched_bvt_control *)ctl->priv;
	del_timer(&bvt_ctl->tick_timer);
}

/**
 * @brief Initialize BVT schedule data for a thread object.
 *
 * BVT schedule data is stored in thread object's data section.
 * Those members are initialized with the given parameters:
 *  - weight
 *  - warp_value
 *  - warp_limit
 *  - unwarp_period
 * Those are initialized with default value:
 *  - mcu = BVT_MCU_MS * TICKS_PER_MS
 *  - warp_on = false
 *  - residual = 0
 * vt_ratio is calculated with BVT_VT_RATIO_MAX / weight
 *
 * @param[inout] obj Pointer to the schedule object whose BVT scheduler data will be initialized.
 * @param[in] params Parameters for the BVT scheduler.
 *
 * @return None.
 *
 * @pre obj != NULL
 * @pre params != NULL
 */
static void sched_bvt_init_data(struct thread_object *obj, struct sched_params * params)
{
	struct sched_bvt_data *data;

	data = (struct sched_bvt_data *)obj->data;
	INIT_LIST_HEAD(&data->list);
	data->mcu = BVT_MCU_MS * TICKS_PER_MS;
	data->weight = clamp(params->bvt_weight, BVT_WEIGHT_MIN, BVT_WEIGHT_MAX);
	data->warp_value = params->bvt_warp_value;
	data->warp_limit = params->bvt_warp_limit;
	data->unwarp_period = params->bvt_unwarp_period;
	data->warp_on = false;	/* warp disabled by default */
	data->vt_ratio = (uint64_t)BVT_VT_RATIO_MAX / data->weight;
	data->residual = 0U;
}

/**
 * @brief Suspend the BVT scheduler.
 *
 * The scheduler is suspended by stopping its timer. In ACRN we just delete the timer.
 * That is what sched_bvt_deinit() does.
 *
 * @param[inout] ctl Pointer to the schedule control on which the BVT scheduler is suspended.
 *
 * @return None.
 *
 * @pre ctl != NULL
 */
static void sched_bvt_suspend(struct sched_control *ctl)
{
	sched_bvt_deinit(ctl);
}


/**
 * @brief Convert virtual time to physical time in unit of MCU.
 *
 * phys_time = virt_time/ratio
 *
 * @param[in] virt_time Virtual time to be converted.
 * @param[in] ratio Convert ratio.
 *
 * @return Physical time in MCU.
 */
static uint64_t v2p(uint64_t virt_time, uint64_t ratio)
{
	return (uint64_t)(virt_time / ratio);
}

/**
 * @brief Convert physical time to virtual time in unit of MCU.
 *
 * virt_time = phys_time * ratio
 *
 * @param[in] phy_time Physical time to be converted.
 * @param[in] ratio Convert ratio.
 *
 * @return Virtual time in MCU.
 */
static uint64_t p2v(uint64_t phy_time, uint64_t ratio)
{
	return (uint64_t)(phy_time * ratio);
}

/**
 * @brief Update VT for a thread object.
 *
 * AVT is incremented with the actual time spent by the thread since last update.
 * EVT is updated with the AVT value as warp is not implemented.
 * Then this function adjusts the thread object's place in run queue with the updated VT value
 * by removing and re-adding it to the run queue.
 *
 * @param[inout] obj Pointer to the thread object whose VT is to be updated.
 *
 * @return None.
 *
 * @pre obj != NULL
 */
static void update_vt(struct thread_object *obj)
{
	struct sched_bvt_data *data;
	uint64_t now_tsc = cpu_ticks();
	uint64_t v_delta, delta_mcu = 0U;

	data = (struct sched_bvt_data *)obj->data;

	/* update current thread's avt and evt */
	if (now_tsc > data->start_tsc) {
		v_delta = p2v(now_tsc - data->start_tsc, data->vt_ratio) + data->residual;
		delta_mcu = (uint64_t)(v_delta / data->mcu);
		data->residual = v_delta % data->mcu;
	}
	data->avt += delta_mcu;
	/* TODO: evt = avt - (warp ? warpback : 0U) */
	data->evt = data->avt;

	if (is_inqueue(obj)) {
		runqueue_remove(obj);
		runqueue_add(obj);
	}
}

/**
 * @brief Pick the next thread object in run queue.
 *
 * This function is called to decide which thread to run next when the pCPU is scheduling its tasks.
 * The first one in run queue is picked as it has the least AVT after update.
 * Additionally, it does update the BVT scheduler by:
 *  - Update VT for current non-idle thread with the time it spent, and the thread object's place in run queue is
 *    updated according to the new VT value.
 *  - Update SVT value with the AVT value of current thread. SVT keeps to be the minimum AVT in the runqueue.
 *  - Decide how much time the next thread can run for.
 *  - Setup a timer to interrupt when the next thread runs out its budget.
 * If run queue is empty, idle thread will be picked as the next thread.
 *
 * @param[inout] ctl Pointer to the thread control of one pCPU.
 *
 * @return Pointer to the next thread object.
 *
 * @pre ctl != NULL
 */
static struct thread_object *sched_bvt_pick_next(struct sched_control *ctl)
{
	struct sched_bvt_control *bvt_ctl = (struct sched_bvt_control *)ctl->priv;
	struct thread_object *first_obj = NULL, *second_obj = NULL;
	struct sched_bvt_data *first_data = NULL, *second_data = NULL;
	struct list_head *first, *sec;
	struct thread_object *next = NULL;
	struct thread_object *current = ctl->curr_obj;
	uint64_t now_tsc = cpu_ticks();
	uint64_t delta_mcu = 0U;
	uint64_t tick_period = BVT_MCU_MS * TICKS_PER_MS;
	uint64_t run_countdown;

	if (!is_idle_thread(current)) {
		update_vt(current);
	}
	/* always align the svt with the avt of the first thread object in runqueue.*/
	update_svt(bvt_ctl);

	del_timer(&bvt_ctl->tick_timer);

	if (!list_empty(&bvt_ctl->runqueue)) {
		first = bvt_ctl->runqueue.next;
		sec = (first->next == &bvt_ctl->runqueue) ? NULL : first->next;

		first_obj = container_of(first, struct thread_object, data);
		first_data = (struct sched_bvt_data *)first_obj->data;

		/* The run_countdown is used to describe how may mcu the next thread
		 * can run for. A one-shot timer is set to expire at
		 * current time + run_countdown. The next thread can run until the
		 * timer interrupts. But when there is only one object
		 * in runqueue, it can run forever. so, no timer is set.
		 */
		if (sec != NULL) {
			second_obj = container_of(sec, struct thread_object, data);
			second_data = (struct sched_bvt_data *)second_obj->data;
			delta_mcu = second_data->evt - first_data->evt;
			run_countdown = v2p(delta_mcu, first_data->vt_ratio) + BVT_CSA_MCU;
		} else {
			run_countdown = UINT64_MAX;
		}
		first_data->start_tsc = now_tsc;
		next = first_obj;
		if (run_countdown != UINT64_MAX) {
			update_timer(&bvt_ctl->tick_timer, cpu_ticks() + run_countdown * tick_period, 0);
			(void)add_timer(&bvt_ctl->tick_timer);
		}
	} else {
		next = &get_cpu_var(idle);
	}

	return next;
}

/**
 * @brief Put a thread object to sleep.
 *
 * Put a thread object to sleep by removing it from run queue.
 * The pCPU then reschedules to switch to the next thread.
 *
 * @param[inout] obj Pointer to the thread object to be put to sleep.
 *
 * @return None.
 *
 * @pre obj != NULL
 */
static void sched_bvt_sleep(struct thread_object *obj)
{
	runqueue_remove(obj);
}


/**
 * @brief Wake a thread object.
 *
 * The thread object is waked by re-adding it to run queue.
 * As its AVT/EVT stops updating when it is sleeping. They are updated
 * to SVT if they are too outdated (< SVT - BVT_CSA_MCU).
 *
 * @param[inout] obj Pointer to the thread object to wake up.
 *
 * @return None.
 *
 * @pre obj != NULL
 */
static void sched_bvt_wake(struct thread_object *obj)
{
	struct sched_bvt_data *data;
	int64_t svt, threshold;

	data = (struct sched_bvt_data *)obj->data;
	svt = get_svt(obj);
	threshold = svt - BVT_CSA_MCU;
	/* adjusting AVT for a thread after a long sleep */
	data->avt = (data->avt > threshold) ? data->avt : svt;
	/* TODO: evt = avt - (warp ? warpback : 0U) */
	data->evt = data->avt;
	/* add to runqueue in order */
	runqueue_add(obj);

}

/**
 * @brief The BVT scheduler.
 *
 * Provides with BVT schedule methods for schedule control.
 */
struct acrn_scheduler sched_bvt = {
	.name		= "sched_bvt",
	.init		= sched_bvt_init,
	.init_data	= sched_bvt_init_data,
	.pick_next	= sched_bvt_pick_next,
	.sleep		= sched_bvt_sleep,
	.wake		= sched_bvt_wake,
	.deinit		= sched_bvt_deinit,
	/* Now suspend is just to do del_timer and add_timer will be delayed to
	 * schedule after resume.
	 * So no need to add .resume now.
	 */
	.suspend	= sched_bvt_suspend,
};

/**
 * @}
 */