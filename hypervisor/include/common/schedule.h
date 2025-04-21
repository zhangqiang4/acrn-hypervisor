/*
 * Copyright (C) 2018-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef SCHEDULE_H
#define SCHEDULE_H
#include <asm/lib/spinlock.h>
#include <lib/list.h>
#include <timer.h>

/**
 * @addtogroup hwmgmt_schedule
 *
 * @{
 */

 /**
 * @file arch/x86/sched.S
 *
 * @brief Assembly functions for scheduling.
 *
 * This file implements the function `arch_switch_to` for context switching.
 * Refer to the documentation of `arch_switch_to` for details.
 */

/**
 * @file
 *
 * @brief Schedule header file.
 *
 * Provides struct, data definition and function declaration for schedule features.
 */

/**
 * @brief Bit index of schedule control flag to indicate a pCPU needs rescheduling.
 */
#define	NEED_RESCHEDULE		(1U)

/**
 * @brief INIT deliver mode.
 *
 * In this mode kick_pcpu() is delivered to target pCPU with a INIT#
 */
#define DEL_MODE_INIT		(1U)

/**
 * @brief IPI deliver mode.
 *
 * In this mode kick_pcpu() is delivered to target pCPU with an IPI interrupt.
 */
#define DEL_MODE_IPI		(2U)

/**
 * @brief PAUSE idle mode.
 *
 * In this mode pCPU idle thread uses PAUSE to relax.
 */
#define IDLE_MODE_PAUSE		(1U)

/**
 * @brief CX idle mode.
 *
 * In this mode pCPU idle thread goes to sleep (Cx) with MWAIT.
 */
#define IDLE_MODE_CX		(2U)

/**
 * @brief Data area size of thread object.
 *
 * Used to store scheduler data.
 */
#define THREAD_DATA_SIZE	(256U)

/**
 * @brief Enum type of thread object states.
 *
 * The value is starting from 1.
 */
enum thread_object_state {
	THREAD_STS_RUNNING = 1, /**< The thread is running, at the head of run queue. */
	THREAD_STS_RUNNABLE, /**< The thread is in run queue waiting to be scheduled. */
	THREAD_STS_BLOCKED /**< The thread is not running and not in run queue. */
};

/**
 * @brief Enum type of thread priority for prio scheduler.
 *
 * Prio scheduler always pick the thread with highest priority in run queue to run.
 */
enum thread_priority {
	PRIO_IDLE = 0, /**< The lowest priority for idle thread. */
	PRIO_LOW, /**< LOW priority. */
	PRIO_HIGH, /**< HIGH priority. */
	PRIO_MAX /**< MAX priority. */
};

/**
 * @brief Struct of schedule parameters for a thread object.
 *
 * For now, we just have several parameters for all the schedulers. So we
 * put them together here for simplicity.
 */
struct sched_params {
	/* TODO
	 * Currently this structure stores BVT param for default. When it grows big enough,
	 * we need to replace it with a union of parameters of different schedulers.
	 */
#ifdef CONFIG_SCHED_PRIO
	uint32_t prio;		/* The priority of a thread */
#endif

	/* per thread parameters for bvt scheduler */
	uint8_t bvt_weight; /**< Weight of a thread. A thread receives a share of CPU time in proportion to weight */
	int32_t bvt_warp_value; /**< The warp value reduce to EVT to boost priority */
	uint32_t bvt_warp_limit; /**< Max time for thread to run warped. Becomes unwarped afterwards */
	uint32_t bvt_unwarp_period; /**< Min unwarp time before next warp */
};

/* Dummy declaration */
struct thread_object;
/**
 * @brief Handler type for a thread's entry.
 */
typedef void (*thread_entry_t)(struct thread_object *obj);
/**
 * @brief Handler type for context switching function.
 */
typedef void (*switch_t)(struct thread_object *obj);

/**
 * @brief Struct to define a thread object.
 */
struct thread_object {
	char name[16]; /**< Name of the thread object */
	uint16_t pcpu_id; /**< ID of the pCPU where the thread is to run on */
	struct sched_control *sched_ctl; /**< Pointer to the schedule control on its pCPU */
	thread_entry_t thread_entry; /**< The thread's entry function */
	volatile enum thread_object_state status; /**< Status of the thread */
	bool be_blocking; /**< Whether the thread has been set to block state while not been blocked yet */

	uint64_t host_sp; /**< Address of SP in the host */
	switch_t switch_out; /**< Handler to switch out the thread */
	switch_t switch_in; /**< Handler to switch in the thread */

	uint8_t data[THREAD_DATA_SIZE]; /**< Thread's private data, used by scheduler */
};

/**
 * @brief Struct to define schedule control.
 *
 * Thread control is a per pCPU object that controls the pCPU's scheduling.
 */
struct sched_control {
	uint16_t pcpu_id; /**< ID of the pCPU where the schedule control is on */
	uint64_t flags; /**< Flag of the thread control status for the pCPU */
	struct thread_object *curr_obj; /**< Pointer to the current running thread object */
	spinlock_t scheduler_lock; /**< To protect sched_control and thread_object */
	struct acrn_scheduler *scheduler; /**< Pointer to the scheduler */
	void *priv; /**< Pointer to the scheduler's private data */
};

/**
 * @brief Struct to define ACRN scheduler.
 *
 * A scheduler is a schedule driver used by the per pCPU schedule control.
 */
struct acrn_scheduler {
	char name[16]; /**< Name of the scheduler */
	int32_t	(*init)(struct sched_control *ctl); /**< Handler to initialize the scheduler */
	/**
	 * @brief Initialize private data of scheduler
	 */
	void	(*init_data)(struct thread_object *obj, struct sched_params *params);
	/**
	 * @brief Handler to pick the next thread object when scheduling
	 */
	struct thread_object* (*pick_next)(struct sched_control *ctl);
	void	(*sleep)(struct thread_object *obj); /**< Handler to put a thread object into sleep */
	void	(*wake)(struct thread_object *obj); /**< Handler to wake up thread object from sleep status */
	void	(*yield)(struct sched_control *ctl); /**< Handler to yield current thread object */
	void	(*prioritize)(struct thread_object *obj); /**< Handler to prioritize the thread object */
	void	(*deinit_data)(struct thread_object *obj); /**< Handler to deinit private data of scheduler */
	void	(*deinit)(struct sched_control *ctl); /**< Handler to deinit scheduler */
	void	(*suspend)(struct sched_control *ctl); /**< Handler to suspend scheduler */
	void	(*resume)(struct sched_control *ctl); /**< Handler to resume scheduler */
};
#ifdef CONFIG_SCHED_NOOP
extern struct acrn_scheduler sched_noop;
struct sched_noop_control {
	struct thread_object *noop_thread_obj;
};
#endif

#ifdef CONFIG_SCHED_IORR
extern struct acrn_scheduler sched_iorr;
struct sched_iorr_control {
	struct list_head runqueue;
	struct hv_timer tick_timer;
};
#endif

#ifdef CONFIG_SCHED_BVT
extern struct acrn_scheduler sched_bvt;

/**
 * @brief Struct of sched_bvt driver's private data.
 */
struct sched_bvt_control {
	struct list_head runqueue; /**< Run queue list head */
	struct hv_timer tick_timer; /**< The schedule tick timer */
	int64_t svt; /**< The minimum AVT of any runnable threads */
};
#endif

#ifdef CONFIG_SCHED_PRIO
extern struct acrn_scheduler sched_prio;
struct sched_prio_control {
	struct list_head prio_queue;
};
#endif

bool is_idle_thread(const struct thread_object *obj);
uint16_t sched_get_pcpuid(const struct thread_object *obj);
struct thread_object *sched_get_current(uint16_t pcpu_id);

void init_sched(uint16_t pcpu_id);
void deinit_sched(uint16_t pcpu_id);
void suspend_sched(void);
void resume_sched(void);
void obtain_schedule_lock(uint16_t pcpu_id, uint64_t *rflag);
void release_schedule_lock(uint16_t pcpu_id, uint64_t rflag);

void init_thread_data(struct thread_object *obj, struct sched_params *params);

void make_reschedule_request(uint16_t pcpu_id);
bool need_reschedule(uint16_t pcpu_id);

void run_thread(struct thread_object *obj);
void sleep_thread(struct thread_object *obj);
void sleep_thread_sync(struct thread_object *obj);
void wake_thread(struct thread_object *obj);
void yield_current(void);
void schedule(void);
/**
 * @brief Arch related context switching function.
 *
 * For X86 it is implemented in shced.s.
 * It will save rflags, rbx, rbp, r12~r15, and rdi in the previous
 * thread_obj's stack, then switch stack pointer(rsp) from previous to next thread_obj (saved
 * in thread_obj->host_sp) and restore above registers from next thread_obj's stack.
 * It make sure the execution context return to the same point of next thread_obj when it got
 * scheduled last time.
 *
 * @param[inout] prev_sp Stack pointer to the previous thread.
 * @param[in] next_sp Stack pointer to the next thread.
 *
 * @return None.
 *
 * @pre prev_sp != NULL
 * @pre next_sp != NULL
 */
void arch_switch_to(void *prev_sp, void *next_sp);
void run_idle_thread(void);
#endif /* SCHEDULE_H */

/**
 * @}
 */