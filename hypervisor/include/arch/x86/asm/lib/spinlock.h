/*
 * Copyright (C) 2018-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef SPINLOCK_H
#define SPINLOCK_H

#ifndef ASSEMBLER

#include <types.h>
#include <rtl.h>

/**
 * @addtogroup lib_lock
 *
 * @{
 */

/**
 * @file
 * @brief Spinlock header file.
 *
 * This file contains the definitions and function prototypes for spinlocks used in the ACRN hypervisor.
 * Spinlocks are used to provide mutual exclusion in multiprocessor environments. This file includes
 * architecture-dependent spinlock types and functions for initializing, obtaining, and releasing spinlocks.
 */

/** The architecture dependent spinlock type. */

/**
 * @brief The spinlock struct definition.
 *
 * This structure contains a head and a tail to manage the queue of processors waiting to acquire a lock. The head
 * will increase 1 when the spinlock is obtained. The tail will increase 1 when the spinlock is released.
 */
typedef struct _spinlock {
	uint32_t head;  /**< Head of the queue for the spinlock */
	uint32_t tail;  /**< Tail of the queue for the spinlock */

} spinlock_t;

/* Function prototypes */

/**
 * @brief Initialize a spinlock.
 *
 * This function initializes a spinlock by setting its head and tail to 0.
 *
 * @param[out] lock Pointer to the spinlock to be initialized.
 *
 * @return None
 *
 * @pre lock != NULL
 *
 * @post lock->head == 0 && lock->tail == 0
 */
static inline void spinlock_init(spinlock_t *lock)
{
	(void)memset(lock, 0U, sizeof(spinlock_t));
}

/**
 * @brief Obtain a spinlock.
 *
 * The function attempts to obtain a spinlock. It atomically increments and exchanges the head counter of the queue.
 * If the old head of the queue is equal to the tail, the spinlock is successfully obtained. Otherwise, it waits
 * until the spinlock is available.
 *
 * @param[inout] lock Pointer to the spinlock to be obtained.
 *
 * @return None
 *
 * @pre lock != NULL
 *
 * @post N/A
 */
static inline void spinlock_obtain(spinlock_t *lock)
{
	asm volatile ("   movl $0x1,%%eax\n"
		      "   lock xaddl %%eax,%[head]\n"
		      "   cmpl %%eax,%[tail]\n"
		      "   jz 1f\n"
		      "2: pause\n"
		      "   cmpl %%eax,%[tail]\n"
		      "   jnz 2b\n"
		      "1:\n"
		      :
		      :
		      [head] "m"(lock->head),
		      [tail] "m"(lock->tail)
		      : "cc", "memory", "eax");
}

/**
 * @brief Release a spinlock.
 *
 * This function releases a spinlock by incrementing the tail of the queue.
 *
 * @param[inout] lock Pointer to the spinlock to be released.
 *
 * @return None
 *
 * @pre lock != NULL
 *
 * @post lock->head == lock->tail
 */
static inline void spinlock_release(spinlock_t *lock)
{
	/* Increment tail of queue */
	asm volatile ("   lock incl %[tail]\n"
				:
				: [tail] "m" (lock->tail)
				: "cc", "memory");
}

#else /* ASSEMBLER */

#define SYNC_SPINLOCK_HEAD_OFFSET       0  /**< The offset of the head element. */

#define SYNC_SPINLOCK_TAIL_OFFSET       4  /**< The offset of the tail element. */

/**
 * @brief Obtain a spinlock.
 *
 * This macro attempts to obtain a spinlock in assembly. It atomically increments and exchanges the head counter of
 * the queue. If the old head of the queue is equal to the tail, the spinlock is successfully obtained. Otherwise, it
 * waits until the spinlock is available.
 */
.macro spinlock_obtain lock
	movl $1, % eax
	lea \lock, % rbx
	lock xaddl % eax, SYNC_SPINLOCK_HEAD_OFFSET(%rbx)
	cmpl % eax, SYNC_SPINLOCK_TAIL_OFFSET(%rbx)
	jz 1f
2 :
	pause
	cmpl % eax, SYNC_SPINLOCK_TAIL_OFFSET(%rbx)
	jnz 2b
1 :
.endm

#define spinlock_obtain(x) spinlock_obtain lock = (x)  /**< obtain a spinlock atomically */

/**
 * @brief Release a spinlock.
 *
 * This macro releases a spinlock in assembly by incrementing the tail of the queue.
 */
.macro spinlock_release lock
	lea \lock, % rbx
	lock incl SYNC_SPINLOCK_TAIL_OFFSET(%rbx)
.endm

#define spinlock_release(x) spinlock_release lock = (x)  /**< release a spinlock atomically */

#endif	/* ASSEMBLER */

/**
 * @brief Disable interrupts and obtain a spinlock.
 *
 * This macro disables interrupts and obtains a spinlock to ensure mutual exclusion.
 *
 * @param[inout] lock The pointer to the spinlock to be obtained.
 * @param[out] p_rflags The pointer to an integer which is used to store the value of the RFLAGS register.
 *
 * @return None
 *
 * @pre lock != NULL
 * @pre p_rflags != NULL
 *
 * @post N/A
 */
#define spinlock_irqsave_obtain(lock, p_rflags)		\
	do {						\
		CPU_INT_ALL_DISABLE(p_rflags);		\
		spinlock_obtain(lock);			\
	} while (0)


/**
 * @brief Release a spinlock and restore interrupts.
 *
 * This macro releases a spinlock and restores the interrupt flags to their previous state.
 *
 * @param[out] lock The pointer to the spinlock to be released.
 * @param[in] rflags The value of the RFLAGS register to be restored.
 *
 * @return None
 *
 * @pre lock != NULL
 *
 * @post lock->tail == lock->head
 */
#define spinlock_irqrestore_release(lock, rflags)	\
	do {						\
		spinlock_release(lock);			\
		CPU_INT_ALL_RESTORE(rflags);		\
	} while (0)
#endif /* SPINLOCK_H */

/**
 * @}
 */