/*-
 * Copyright (c) 1998 Doug Rabson
 * Copyright (c) 2018-2024 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef ATOMIC_H
#define ATOMIC_H
#include <types.h>

/**
 * @defgroup lib_lock lib.lock
 * @ingroup lib
 * @brief Provides atomic operations and spinlock mechanisms for synchronization.
 *
 * This module contains the implementation of atomic operations and spinlock mechanisms used in the ACRN hypervisor.
 * Atomic operations ensure data integrity in multi-threaded and multi-processor environments by providing
 * functions for incrementing, decrementing, swapping, comparing, and exchanging values atomically. Spinlocks
 * provide mutual exclusion by allowing only one processor to access a critical section at a time, ensuring
 * synchronization in multiprocessor environments. The module includes architecture-dependent implementations
 * to leverage hardware support for efficient execution.
 *
 * @{
 */

/**
 * @file
 * @brief Atomic operations implementation
 *
 * This file contains the implementation of various atomic operations such as increment, decrement, swap,
 * compare-and-exchange, and fetch-and-add. These operations are crucial for maintaining data integrity in
 * multi-threaded and multi-processor environments. The functions are implemented using inline assembly to
 * ensure atomicity and leverage hardware support for efficient execution. The file provides macros to
 * generate atomic functions for different data sizes (16-bit, 32-bit, and 64-bit), ensuring flexibility
 * and reusability across different use cases.
 */

#define	BUS_LOCK	"lock ; " /**< LOCK prefix */

/**
 * @brief Template for creating atomic increment functions.
 *
 * This macro serves as a template to create functions that add 1 to a 16-bit, 32-bit, or 64-bit memory location
 * atomically.
 *
 * The following functions are included:
 * - build_atomic_inc(atomic_inc16, "w", uint16_t)
 * - build_atomic_inc(atomic_inc32, "l", uint32_t)
 * - build_atomic_inc(atomic_inc64, "q", uint64_t)
 *
 * Inputs in these functions are defined as:
 * - input param [inout]: ptr The pointer to the memory region.
 *
 * @param[in] name The name of the function.
 * @param[in] size The size suffix of the assembly instruction ("w" for 16-bit, "l" for 32-bit, "q" for 64-bit).
 * @param[in] type The C type of the variable (uint16_t, uint32_t, uint64_t).
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
#define build_atomic_inc(name, size, type)		\
static inline void name(type *ptr)			\
{							\
	asm volatile(BUS_LOCK "inc" size " %0"		\
			: "=m" (*ptr)			\
			:  "m" (*ptr));			\
}

/**
 * @brief Adds 1 to a 16-bit memory region.
 *
 * This function adds 1 to the specified 16-bit memory region atomically.
 *
 * @param[inout] ptr The pointer to the 16-bit memory region.
 *
 * @return None
 *
 * @pre ptr != NULL
 *
 * @post N/A
 */
build_atomic_inc(atomic_inc16, "w", uint16_t)

/**
 * @brief Adds 1 to a 32-bit memory region.
 *
 * This function adds 1 to the specified 32-bit memory region atomically.
 *
 * @param[inout] ptr The pointer to the 32-bit memory region.
 *
 * @return None
 *
 * @pre ptr != NULL
 *
 * @post N/A
 */
build_atomic_inc(atomic_inc32, "l", uint32_t)

/**
 * @brief Adds 1 to a 64-bit memory region.
 *
 * This function adds 1 to the specified 64-bit memory region atomically.
 *
 * @param[inout] ptr The pointer to the 64-bit memory region.
 *
 * @return None
 *
 * @pre ptr != NULL
 *
 * @post N/A
 */
build_atomic_inc(atomic_inc64, "q", uint64_t)

/**
 * @brief Template for creating atomic decrement functions.
 *
 * This macro serves as a template to create functions that subtract 1 from a 16-bit, 32-bit, or 64-bit memory
 * location atomically.
 *
 * The following functions are included:
 * - build_atomic_dec(atomic_dec16, "w", uint16_t)
 * - build_atomic_dec(atomic_dec32, "l", uint32_t)
 * - build_atomic_dec(atomic_dec64, "q", uint64_t)
 *
 * Inputs in these functions are defined as:
 * - input param [inout]: ptr The pointer to the memory region.
 *
 * @param[in] name The name of the function.
 * @param[in] size The size suffix of the assembly instruction ("w" for 16-bit, "l" for 32-bit, "q" for 64-bit).
 * @param[in] type The C type of the variable (uint16_t, uint32_t, uint64_t).
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
#define build_atomic_dec(name, size, type)		\
static inline void name(type *ptr)			\
{							\
	asm volatile(BUS_LOCK "dec" size " %0"		\
			: "=m" (*ptr)			\
			:  "m" (*ptr));			\
}

/**
 * @brief Subtracts 1 from a 16-bit memory region.
 *
 * This function subtracts 1 from the specified 16-bit memory region atomically.
 *
 * @param[inout] ptr The pointer to the 16-bit memory region.
 *
 * @return None
 *
 * @pre ptr != NULL
 *
 * @post N/A
 */
build_atomic_dec(atomic_dec16, "w", uint16_t)

/**
 * @brief Subtracts 1 from a 32-bit memory region.
 *
 * This function subtracts 1 from the specified 32-bit memory region atomically.
 *
 * @param[inout] ptr The pointer to the 32-bit memory region.
 *
 * @return None
 *
 * @pre ptr != NULL
 *
 * @post N/A
 */
build_atomic_dec(atomic_dec32, "l", uint32_t)

/**
 * @brief Subtracts 1 from a 64-bit memory region.
 *
 * This function subtracts 1 from the specified 64-bit memory region atomically.
 *
 * @param[inout] ptr The pointer to the 64-bit memory region.
 *
 * @return None
 *
 * @pre ptr != NULL
 *
 * @post N/A
 */
build_atomic_dec(atomic_dec64, "q", uint64_t)

/**
 * @brief Template for creating atomic swap functions.
 *
 * This macro serves as a template to create functions that swap the value of a 32-bit or 64-bit memory location
 * and return the previous value atomically.
 *
 * The following functions are included:
 * - build_atomic_swap(atomic_swap32, "l", uint32_t)
 * - build_atomic_swap(atomic_swap64, "q", uint64_t)
 *
 * Inputs in these functions are defined as:
 * - input param [inout]: ptr The pointer to the memory region.
 * - input param [in]: v The value to be swapped with the memory region.
 *
 * @param[in] name The name of the function.
 * @param[in] size The size suffix of the assembly instruction ("l" for 32-bit, "q" for 64-bit).
 * @param[in] type The C type of the variable (uint32_t, uint64_t).
 *
 * @return The original value of the memory region before the swap.
 *
 * @pre N/A
 *
 * @post N/A
 */
#define build_atomic_swap(name, size, type)		\
static inline type name(type *ptr, type v)		\
{							\
	asm volatile(BUS_LOCK "xchg" size " %1,%0"	\
			:  "+m" (*ptr), "+r" (v)	\
			:				\
			:  "cc", "memory");		\
	return v;					\
}

/**
 * @brief Swaps the value of a 32-bit memory region atomically.
 *
 * This function swaps the value of the specified 32-bit memory region with the provided value and return the original
 * value atomically.
 *
 * @param[inout] ptr The pointer to the 32-bit memory region.
 * @param[in] v The value to be swapped with the memory region.
 *
 * @return The original value of the memory region before the swap.
 *
 * @pre ptr != NULL
 *
 * @post N/A
 */
build_atomic_swap(atomic_swap32, "l", uint32_t)

/**
 * @brief Swaps the value of a 64-bit memory region atomically.
 *
 * This function swaps the value of the specified 64-bit memory region with the provided value and return the original
 * value atomically.
 *
 * @param[inout] ptr The pointer to the 64-bit memory region.
 * @param[in] v The value to be swapped with the memory region.
 *
 * @return The original value of the memory region before the swap.
 *
 * @pre ptr != NULL
 *
 * @post N/A
 */
build_atomic_swap(atomic_swap64, "q", uint64_t)

 /*
 * #define atomic_readandclear32(P) \
 * (return (*(uint32_t *)(P)); *(uint32_t *)(P) = 0U;)
  */
/**
 * @brief Reads and clears a 32-bit integer atomically.
 *
 * This function atomically reads the value of a 32-bit integer and then sets it to 0.
 *
 * @param[inout] p The pointer to the 32-bit integer.
 *
 * @return The original value of the 32-bit integer before it was cleared.
 *
 * @pre p != NULL
 *
 * @post N/A
 */
static inline uint32_t atomic_readandclear32(uint32_t *p)
{
	return atomic_swap32(p, 0U);
}

 /*
 * #define atomic_readandclear64(P) \
 * (return (*(uint64_t *)(P)); *(uint64_t *)(P) = 0UL;)
  */
/**
 * @brief Reads and clears a 64-bit integer atomically.
 *
 * This function atomically reads the value of a 64-bit integer and then sets it to 0.
 *
 * @param[inout] p The pointer to the 64-bit integer.
 *
 * @return The original value of the 64-bit integer before it was cleared.
 *
 * @pre p != NULL
 *
 * @post N/A
 */
static inline uint64_t atomic_readandclear64(uint64_t *p)
{
	return atomic_swap64(p, 0UL);
}

/**
 * @brief Template for creating atomic compare-and-exchange functions.
 *
 * This macro serves as a template to create functions that perform an atomic compare-and-exchange operation on a
 * 32-bit or 64-bit memory location.
 *
 * The following functions are included:
 * - build_atomic_cmpxchg(atomic_cmpxchg32, "l", uint32_t)
 * - build_atomic_cmpxchg(atomic_cmpxchg64, "q", uint64_t)
 *
 * Inputs in these functions are defined as:
 * - input param [inout]: ptr The pointer to the memory region.
 * - input param [in]: old The value to compare against.
 * - input param [in]: new The value to exchange if the value at the specified memory location is equal to the given
 *                         old value.
 *
 * @param[in] name The name of the function.
 * @param[in] size The size suffix of the assembly instruction ("l" for 32-bit, "q" for 64-bit).
 * @param[in] type The C type of the variable (uint32_t, uint64_t).
 *
 * @return The original value of the memory region before the compare-and-exchange operation.
 *
 * @pre N/A
 *
 * @post N/A
 */
#define build_atomic_cmpxchg(name, size, type)			\
static inline type name(volatile type *ptr, type old, type new)	\
{								\
	type ret;						\
	asm volatile(BUS_LOCK "cmpxchg" size " %2,%1"		\
			: "=a" (ret), "+m" (*ptr)		\
			: "r" (new), "0" (old)			\
			: "memory");				\
	return ret;						\
}

/**
 * @brief Performs an atomic compare-and-exchange on a 32-bit memory region.
 *
 * This function performs an atomic compare-and-exchange operation on the specified 32-bit memory region.
 *
 * @param[inout] ptr The pointer to the 32-bit memory region.
 * @param[in] old The value to compare against.
 * @param[in] new The value to exchange if the value at the specified memory location is equal to the given
 *                old value.
 *
 * @return The original value of the memory region before the compare-and-exchange operation.
 *
 * @pre ptr != NULL
 *
 * @post N/A
 */
build_atomic_cmpxchg(atomic_cmpxchg32, "l", uint32_t)

/**
 * @brief Performs an atomic compare-and-exchange on a 64-bit memory region.
 *
 * This function performs an atomic compare-and-exchange operation on the specified 64-bit memory region.
 *
 * @param[inout] ptr The pointer to the 64-bit memory region.
 * @param[in] old The value to compare against.
 * @param[in] new The value to exchange if the value at the specified memory location is equal to the given
 *                old value.
 *
 * @return The original value of the memory region before the compare-and-exchange operation.
 *
 * @pre ptr != NULL
 *
 * @post N/A
 */
build_atomic_cmpxchg(atomic_cmpxchg64, "q", uint64_t)

/**
 * @brief Template for creating atomic fetch-and-add functions.
 *
 * This macro serves as a template to create functions that perform an atomic fetch-and-add operation on a 16-bit,
 * 32-bit, or 64-bit memory location.
 *
 * The following functions are included:
 * - build_atomic_xadd(atomic_xadd16, "w", uint16_t)
 * - build_atomic_xadd(atomic_xadd32, "l", int32_t)
 * - build_atomic_xadd(atomic_xadd64, "q", int64_t)
 *
 * Inputs in these functions are defined as:
 * - input param [inout]: ptr The pointer to the memory region.
 * - input param [in]: v The value to add to the memory region.
 *
 * @param[in] name The name of the function.
 * @param[in] size The size suffix of the assembly instruction ("w" for 16-bit, "l" for 32-bit, "q" for 64-bit).
 * @param[in] type The C type of the variable (uint16_t, int32_t, int64_t).
 *
 * @return The original value of the memory region before the fetch-and-add operation.
 *
 * @pre N/A
 *
 * @post N/A
 */
#define build_atomic_xadd(name, size, type)			\
static inline type name(type *ptr, type v)			\
{								\
	asm volatile(BUS_LOCK "xadd" size " %0,%1"		\
			: "+r" (v), "+m" (*ptr)			\
			:					\
			: "cc", "memory");			\
	return v;						\
 }

/**
 * @brief Performs an atomic fetch-and-add on a 16-bit memory region.
 *
 * This function performs an atomic fetch-and-add operation on the specified 16-bit memory region.
 *
 * @param[inout] ptr The pointer to the 16-bit memory region.
 * @param[in] v The value to add to the memory region.
 *
 * @return The original value of the memory region before the fetch-and-add operation.
 *
 * @pre ptr != NULL
 *
 * @post N/A
 */
build_atomic_xadd(atomic_xadd16, "w", uint16_t)

/**
 * @brief Performs an atomic fetch-and-add on a 32-bit memory region.
 *
 * This function performs an atomic fetch-and-add operation on the specified 32-bit memory region.
 *
 * @param[inout] ptr The pointer to the 32-bit memory region.
 * @param[in] v The value to add to the memory region.
 *
 * @return The original value of the memory region before the fetch-and-add operation.
 *
 * @pre ptr != NULL
 *
 * @post N/A
 */
build_atomic_xadd(atomic_xadd32, "l", int32_t)

/**
 * @brief Performs an atomic fetch-and-add on a 64-bit memory region.
 *
 * This function performs an atomic fetch-and-add operation on the specified 64-bit memory region.
 *
 * @param[inout] ptr The pointer to the 64-bit memory region.
 * @param[in] v The value to add to the memory region.
 *
 * @return The original value of the memory region before the fetch-and-add operation.
 *
 * @pre ptr != NULL
 *
 * @post N/A
 */
build_atomic_xadd(atomic_xadd64, "q", int64_t)

/**
 * @brief Adds a value to a 32-bit integer and returns the result.
 *
 * This function atomically adds a specified value to a 32-bit integer and returns the result.
 *
 * @param[inout] p The pointer to the 32-bit integer.
 * @param[in] v The value to add.
 *
 * @return The result of the addition.
 *
 * @pre p != NULL
 *
 * @post N/A
 */
static inline int32_t atomic_add_return(int32_t *p, int32_t v)
{
	return (atomic_xadd32(p, v) + v);
}

/**
 * @brief Subtracts a value from a 32-bit integer and returns the result.
 *
 * This function atomically subtracts a specified value from a 32-bit integer and returns the result.
 *
 * @param[inout] p The pointer to the 32-bit integer.
 * @param[in] v The value to subtract.
 *
 * @return The result of the subtraction.
 *
 * @pre p != NULL
 *
 * @post N/A
 */
static inline int32_t atomic_sub_return(int32_t *p, int32_t v)
{
	return (atomic_xadd32(p, -v) - v);
}

/**
 * @brief Increments a 32-bit integer by 1 and returns the result.
 *
 * This function atomically increments a 32-bit integer by 1 and returns the result.
 *
 * @param[inout] v The pointer to the 32-bit integer.
 *
 * @return The result of the increment.
 *
 * @pre v != NULL
 *
 * @post N/A
 */
static inline int32_t atomic_inc_return(int32_t *v)
{
	return atomic_add_return(v, 1);
}

/**
 * @brief Decrements a 32-bit integer by 1 and returns the result.
 *
 * This function atomically decrements a 32-bit integer by 1 and returns the result.
 *
 * @param[inout] v The pointer to the 32-bit integer.
 *
 * @return The result of the decrement.
 *
 * @pre v != NULL
 *
 * @post N/A
 */
static inline int32_t atomic_dec_return(int32_t *v)
{
	return atomic_sub_return(v, 1);
}

/**
 * @brief Adds a value to a 64-bit integer and returns the result.
 *
 * This function atomically adds a specified value to a 64-bit integer and returns the result.
 *
 * @param[inout] p The pointer to the 64-bit integer.
 * @param[in] v The value to add.
 *
 * @return The result of the addition.
 *
 * @pre p != NULL
 *
 * @post N/A
 */
static inline int64_t atomic_add64_return(int64_t *p, int64_t v)
{
	return (atomic_xadd64(p, v) + v);
}

/**
 * @brief Subtracts a value from a 64-bit integer and returns the result.
 *
 * This function atomically subtracts a specified value from a 64-bit integer and returns the result.
 *
 * @param[inout] p The pointer to the 64-bit integer.
 * @param[in] v The value to subtract.
 *
 * @return The result of the subtraction.
 *
 * @pre p != NULL
 *
 * @post N/A
 */
static inline int64_t atomic_sub64_return(int64_t *p, int64_t v)
{
	return (atomic_xadd64(p, -v) - v);
}

/**
 * @brief Increments a 64-bit integer by 1 and returns the result.
 *
 * This function atomically increments a 64-bit integer by 1 and returns the result.
 *
 * @param[inout] v The pointer to the 64-bit integer.
 *
 * @return The result of the increment.
 *
 * @pre v != NULL
 *
 * @post N/A
 */
static inline int64_t atomic_inc64_return(int64_t *v)
{
	return atomic_add64_return(v, 1);
}

/**
 * @brief Decrements a 64-bit integer by 1 and returns the result.
 *
 * This function atomically decrements a 64-bit integer by 1 and returns the result.
 *
 * @param[inout] v The pointer to the 64-bit integer.
 *
 * @return The result of the decrement.
 *
 * @pre v != NULL
 *
 * @post N/A
 */
static inline int64_t atomic_dec64_return(int64_t *v)
{
	return atomic_sub64_return(v, 1);
}

#endif /* ATOMIC_H*/

/**
 * @}
 */
