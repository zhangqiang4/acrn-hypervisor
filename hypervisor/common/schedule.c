/*
 * Copyright (C) 2018-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <rtl.h>
#include <list.h>
#include <asm/lib/bits.h>
#include <asm/cpu.h>
#include <asm/per_cpu.h>
#include <asm/lapic.h>
#include <schedule.h>
#include <sprintf.h>
#include <asm/irq.h>
#include <trace.h>

/**
 * @defgroup hwmgmt_schedule hwmgmt.schedule
 * @ingroup hwmgmt
 * @brief Implementation of scheduling.
 *
 * This module is implement of scheduling. Concept of scheduling in ACRN includes:
 * - vCPUs are running as threads
 * - A thread is bind to a pCPU
 * - The per pCPU thread control manages scheduling on that pCPU
 * - Thread control use scheduler to implement scheduler strategies, which are:
 *   - BVT (borrowed virtual time)
 *   - NOOP (no op scheduling, future extended feature)
 *   - IORR (IO sensitive Round Robin, future extended feature)
 *   - PRIO (priority based scheduling, future extended feature)
 * @{
 */

/**
 * @file
 *
 * @brief Implementation of CPU scheduling.
 *
 * This file provides functions for hypervisor to schedule threads on CPUs.
 * It supports different types of schedulers, which are implemented in other files.
 */

 /**
 * @brief Whether the given thread object is the idle thread.
 *
 * This function compares the given thread object to the idle thread on the pCPU which
 * runs the given thread. Returns whether they are the same.
 *
 * @param[in] obj Thread object to be compared.
 *
 * @return Bool result of the comparing.
 *
 * @retval true The given thread object is the idle thread on the pCPU it is running.
 * @retval false The given thread object is not the idle thread on the pCPU it is running.
 *
 * @pre obj != NULL
 */
bool is_idle_thread(const struct thread_object *obj)
{
	uint16_t pcpu_id = obj->pcpu_id;
	return (obj == &per_cpu(idle, pcpu_id));
}

 /**
 * @brief Whether the given thread object is in blocked state.
 *
 * This function returns whether the given thread object is in THREAD_STS_BLOCKED stat.
 *
 * @param[in] obj Thread object to be checked.
 *
 * @return Bool result of the comparing.
 *
 * @retval true The given thread object is in THREAD_STS_BLOCKED state.
 * @retval false The given thread object is not in THREAD_STS_BLOCKED state.
 *
 * @pre obj != NULL
 */
static inline bool is_blocked(const struct thread_object *obj)
{
	return obj->status == THREAD_STS_BLOCKED;
}

 /**
 * @brief Whether the given thread object is in running state.
 *
 * This function returns whether the given thread object is in THREAD_STS_RUNNING stat.
 *
 * @param[in] obj Thread object to be checked.
 *
 * @return Bool result of the comparing.
 *
 * @retval true The given thread object is in THREAD_STS_RUNNING state.
 * @retval false The given thread object is not in THREAD_STS_RUNNING state.
 *
 * @pre obj != NULL
 */
static inline bool is_running(const struct thread_object *obj)
{
	return obj->status == THREAD_STS_RUNNING;
}

 /**
 * @brief Set the given thread object's state.
 *
 * This function sets the given thread object's state to the given state.
 *
 * @param[inout] obj Thread object to be set.
 * @param[in] status The state to be set to.
 *
 * @return None.
 *
 * @pre obj != NULL
 */
static inline void set_thread_status(struct thread_object *obj, enum thread_object_state status)
{
	obj->status = status;
}

 /**
 * @brief Obtain the schedule lock.
 *
 * The schedule lock is a per pCPU variable that is used to keep context of scheduling operations intact.
 *
 * @param[in] pcpu_id ID of the pCPU that the schedule lock belongs to.
 * @param[out] rflag Pointer to an integer which is used to store the value of the RFLAGS register.
 *
 * @return None.
 *
 * @pre pcpu_id < MAX_PCPU_NUM
 * @pre rflag != NULL
 */
void obtain_schedule_lock(uint16_t pcpu_id, uint64_t *rflag)
{
	struct sched_control *ctl = &per_cpu(sched_ctl, pcpu_id);
	spinlock_irqsave_obtain(&ctl->scheduler_lock, rflag);
}

 /**
 * @brief Release the schedule lock.
 *
 * The schedule lock is a per pCPU variable that is used to keep context of scheduling operations intact.
 *
 * @param[in] pcpu_id ID of the pCPU that the schedule lock belongs to.
 * @param[in] rflag The integer which is used to restore the value of the RFLAGS register.
 *
 * @return None.
 *
 * @pre pcpu_id < MAX_PCPU_NUM
 */
void release_schedule_lock(uint16_t pcpu_id, uint64_t rflag)
{
	struct sched_control *ctl = &per_cpu(sched_ctl, pcpu_id);
	spinlock_irqrestore_release(&ctl->scheduler_lock, rflag);
}

 /**
 * @brief Get the scheduler.
 *
 * The scheduler is a pointer to per pCPU schedule driver that schedules threads on that pCPU.
 *
 * @param[in] pcpu_id ID of the pCPU to be checked.
 *
 * @return Pointer to the scheduler.
 *
 * @pre pcpu_id < MAX_PCPU_NUM
 */
static struct acrn_scheduler *get_scheduler(uint16_t pcpu_id)
{
	struct sched_control *ctl = &per_cpu(sched_ctl, pcpu_id);
	return ctl->scheduler;
}

 /**
 * @brief Get the pCPU id of the given thread object.
 *
 * This function returns the pCPU id of the given thread object.
 *
 * @param[in] obj Thread object to be checked.
 *
 * @return The pCPU id of the thread object.
 *
 * @pre obj != NULL
 */
uint16_t sched_get_pcpuid(const struct thread_object *obj)
{
	return obj->pcpu_id;
}

 /**
 * @brief Initialize schedule control on one pCPU.
 *
 * This function initializes schedule control on one pCPU. Including:
 *  - Initialize schedule lock.
 *  - Setup scheduler.
 *  - Call scheduler's initializer if it exists.
 *
 * @param[in] pcpu_id ID of the pCPU to be initialized.
 *
 * @return None.
 *
 * @pre pcpu_id < MAX_PCPU_NUM
 */
void init_sched(uint16_t pcpu_id)
{
	struct sched_control *ctl = &per_cpu(sched_ctl, pcpu_id);

	spinlock_init(&ctl->scheduler_lock);
	ctl->flags = 0UL;
	ctl->curr_obj = NULL;
	ctl->pcpu_id = pcpu_id;
#ifdef CONFIG_SCHED_NOOP
	ctl->scheduler = &sched_noop;
#endif
#ifdef CONFIG_SCHED_IORR
	ctl->scheduler = &sched_iorr;
#endif
#ifdef CONFIG_SCHED_BVT
	ctl->scheduler = &sched_bvt;
#endif
#ifdef CONFIG_SCHED_PRIO
	ctl->scheduler = &sched_prio;
#endif
	if (ctl->scheduler->init != NULL) {
		ctl->scheduler->init(ctl);
	}
}

 /**
 * @brief Deinitialize schedule control on one pCPU.
 *
 * Calls the scheduler's deinitializer if it exists.
 *
 * @param[in] pcpu_id ID of the pCPU to be deinitialized.
 *
 * @return None.
 *
 * @pre pcpu_id < MAX_PCPU_NUM
 */
void deinit_sched(uint16_t pcpu_id)
{
	struct sched_control *ctl = &per_cpu(sched_ctl, pcpu_id);

	if (ctl->scheduler->deinit != NULL) {
		ctl->scheduler->deinit(ctl);
	}
}

 /**
 * @brief Suspend the scheduler on bootstrap CPU.
 *
 * Calls the scheduler's suspend() method if it exists.
 *
 * @return None.
 */
void suspend_sched(void)
{
	struct sched_control *ctl = &per_cpu(sched_ctl, BSP_CPU_ID);

	if (ctl->scheduler->suspend != NULL) {
		ctl->scheduler->suspend(ctl);
	}
}


 /**
 * @brief Resume the scheduler on bootstrap CPU.
 *
 * Calls the scheduler's resume() method if it exists.
 *
 * @return None.
 */
void resume_sched(void)
{
	struct sched_control *ctl = &per_cpu(sched_ctl, BSP_CPU_ID);

	if (ctl->scheduler->resume != NULL) {
		ctl->scheduler->resume(ctl);
	}
}

 /**
 * @brief Initialize the thread object data.
 *
 * Initialize the thread object's data by calling its scheduler's init_data() method if it exists,
 * with parameters such as priority and weight. And set its initial state to THREAD_STS_BLOCKED.
 *
 * @param[inout] obj Pointer to the thread object whose private data is to be initialized.
 * @param[in] params Pointer to the parameters of the thread object.
 *
 * @return None.
 *
 * @pre obj != NULL
 * @pre params != NULL
 */
void init_thread_data(struct thread_object *obj, struct sched_params *params)
{
	struct acrn_scheduler *scheduler = get_scheduler(obj->pcpu_id);
	uint64_t rflag;

	obtain_schedule_lock(obj->pcpu_id, &rflag);
	if (scheduler->init_data != NULL) {
		scheduler->init_data(obj, params);
	}
	/* initial as BLOCKED status, so we can wake it up to run */
	set_thread_status(obj, THREAD_STS_BLOCKED);
	release_schedule_lock(obj->pcpu_id, rflag);
}

 /**
 * @brief Get the current schedule object on the specified pCPU.
 *
 * When the pCPU is in running state, the current thread object is the thread that is running.
 *
 * @param[in] pcpu_id ID of the pCPU to be checked.
 *
 * @return Pointer to the current thread object on the pCPU.
 *
 * @pre pcpu_id < MAX_PCPU_NUM
 */
struct thread_object *sched_get_current(uint16_t pcpu_id)
{
	struct sched_control *ctl = &per_cpu(sched_ctl, pcpu_id);
	return ctl->curr_obj;
}


 /**
 * @brief Make reschedule request to one pCPU.
 *
 * Make reschedule request by setting NEED_RESCHEDULE flag, and then kick its pCPU with
 * interrupt or other means (see kick_pcpu()) if the target pCPU is not the currently running one.
 * Scheduler on its pCPU will respond to the reschedule request, and try reschedule its threads.
 *
 * @param[in] pcpu_id ID of the pCPU to make request to.
 *
 * @return None.
 *
 * @pre pcpu_id < MAX_PCPU_NUM
 */
void make_reschedule_request(uint16_t pcpu_id)
{
	struct sched_control *ctl = &per_cpu(sched_ctl, pcpu_id);

	bitmap_set_lock(NEED_RESCHEDULE, &ctl->flags);
	if (get_pcpu_id() != pcpu_id) {
		kick_pcpu(pcpu_id);
	}
}

 /**
 * @brief Whether the target pCPU needs reschedule.
 *
 * Get whether the target pCPU needs reschedule, by checking the NEED_RESCHEDULE flag bit
 * on the pCPU's schedule flag.
 *
 * @param[in] pcpu_id ID of the pCPU to check.
 *
 * @return Bool value of whether the NEED_RESCHEDULE bit is set.
 *
 * @retval true NEED_RESCHEDULE bit is set.
 * @retval false NEED_RESCHEDULE bit is not set.
 *
 * @pre pcpu_id < MAX_PCPU_NUM
 */
bool need_reschedule(uint16_t pcpu_id)
{
	struct sched_control *ctl = &per_cpu(sched_ctl, pcpu_id);

	return bitmap_test(NEED_RESCHEDULE, &ctl->flags);
}

 /**
 * @brief Schedule the current pCPU.
 *
 * This function schedules threads on current pCPU. It will let the scheduler
 * pick the next thread object. If the next is different from the current one,
 * switch to it. If the scheduler does not provide pick_next(), switch to idle.
 * Status of the thread that is switched in is changed to THREAD_STS_RUNNING.
 * The one that is switched out becomes THREAD_STS_RUNNABLE.
 *
 * @return None.
 *
 * @pre per_cpu(sched_ctl, pcpu_id).scheduler != NULL
 */
void schedule(void)
{
	uint16_t pcpu_id = get_pcpu_id();
	struct sched_control *ctl = &per_cpu(sched_ctl, pcpu_id);
	struct thread_object *next = &per_cpu(idle, pcpu_id);
	struct thread_object *prev = ctl->curr_obj;
	uint64_t rflag;
	char name[16];

	obtain_schedule_lock(pcpu_id, &rflag);
	if (ctl->scheduler->pick_next != NULL) {
		next = ctl->scheduler->pick_next(ctl);
	}
	bitmap_clear_lock(NEED_RESCHEDULE, &ctl->flags);

	/* If we picked different sched object, switch context */
	if (prev != next) {
		if (prev != NULL) {
			memcpy_erms(name, prev->name, 4);
			memcpy_erms(name + 4, next->name, 4);
			memset(name + 8, 0, sizeof(name) - 8);
			TRACE_16STR(TRACE_SCHED_NEXT, name);
			if (prev->switch_out != NULL) {
				prev->switch_out(prev);
			}
			set_thread_status(prev, prev->be_blocking ? THREAD_STS_BLOCKED : THREAD_STS_RUNNABLE);
			prev->be_blocking = false;
		}

		if (next->switch_in != NULL) {
			next->switch_in(next);
		}
		set_thread_status(next, THREAD_STS_RUNNING);

		ctl->curr_obj = next;
		release_schedule_lock(pcpu_id, rflag);
		arch_switch_to(&prev->host_sp, &next->host_sp);
	} else {
		release_schedule_lock(pcpu_id, rflag);
	}
}

 /**
 * @brief Sleep the target thread object.
 *
 * This function puts the target thread object to sleep by calling the scheduler's
 * sleep() method if it exists. If the target thread object is running at the moment, make a
 * reschedule request to that pCPU so that the target thread will be switched out afterwards.
 * If the target thread object is not running, directly set its status to THREAD_STS_BLOCKED.
 *
 * @param[inout] obj The target thread object.
 *
 * @return None.
 *
 * @pre obj != NULL
 */
void sleep_thread(struct thread_object *obj)
{
	uint16_t pcpu_id = obj->pcpu_id;
	struct acrn_scheduler *scheduler = get_scheduler(pcpu_id);
	uint64_t rflag;

	obtain_schedule_lock(pcpu_id, &rflag);
	if (scheduler->sleep != NULL) {
		scheduler->sleep(obj);
	}
	if (is_running(obj)) {
		make_reschedule_request(pcpu_id);
		obj->be_blocking = true;
	} else {
		set_thread_status(obj, THREAD_STS_BLOCKED);
	}
	release_schedule_lock(pcpu_id, rflag);
}

/**
 * @brief Sleep the target thread object synchronously.
 *
 * After putting the thread object to sleep with scheduler, sleep_thread() makes a reschedule
 * request to the target pCPU, then the target CPU will switch the thread out afterwards.
 * This function calls sleep_thread(), and then wait for it been switched out by polling its
 * status until it becomes THREAD_STS_BLOCKED.
 *
 * @param[inout] obj The target thread object.
 *
 * @return None.
 *
 * @pre obj != NULL
 */
void sleep_thread_sync(struct thread_object *obj)
{
	sleep_thread(obj);
	while (!is_blocked(obj)) {
		asm_pause();
	}
}

/**
 * @brief Wake the target thread object.
 *
 * This function wakes the target thread object by calling the scheduler's wake() method if it exists.
 * This will only be done for threads that is blocked or be_blocking (put to sleep but
 * not switched out yet).
 * If the thread is already blocked, its status is going to be set to THREAD_STS_RUNNABLE,
 * and a reschedule request is send to its CPU.
 *
 * @param[inout] obj The target thread object.
 *
 * @return None.
 *
 * @pre obj != NULL
 */
void wake_thread(struct thread_object *obj)
{
	uint16_t pcpu_id = obj->pcpu_id;
	struct acrn_scheduler *scheduler;
	uint64_t rflag;

	obtain_schedule_lock(pcpu_id, &rflag);
	if (is_blocked(obj) || obj->be_blocking) {
		scheduler = get_scheduler(pcpu_id);
		if (scheduler->wake != NULL) {
			scheduler->wake(obj);
		}
		if (is_blocked(obj)) {
			set_thread_status(obj, THREAD_STS_RUNNABLE);
			make_reschedule_request(pcpu_id);
		}
		obj->be_blocking = false;
	}
	release_schedule_lock(pcpu_id, rflag);
}

/**
 * @brief Yield the current thread.
 *
 * This function yields the current thread by making a reschedule request to the current CPU.
 * The scheduler will respond the request before next vm entry and try to switch the current
 * thread out.
 *
 * @return None.
 */
void yield_current(void)
{
	make_reschedule_request(get_pcpu_id());
}

/**
 * @brief Run the target thread object.
 *
 * This function sets the thread to THREAD_STS_RUNNING state, and calls the thread object's
 * thread_entry if it exists . This function does not return until the thread exits.
 *
 * @param[inout] obj The target thread object.
 *
 * @return None.
 *
 * @pre obj != NULL
 */
void run_thread(struct thread_object *obj)
{
	uint64_t rflag;

	obtain_schedule_lock(obj->pcpu_id, &rflag);
	get_cpu_var(sched_ctl).curr_obj = obj;
	set_thread_status(obj, THREAD_STS_RUNNING);
	release_schedule_lock(obj->pcpu_id, rflag);

	if (obj->thread_entry != NULL) {
		obj->thread_entry(obj);
	}
}

/**
 * @}
 */