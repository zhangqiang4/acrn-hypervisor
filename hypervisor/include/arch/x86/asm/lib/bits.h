/*-
 * Copyright (c) 1998 Doug Rabson
 * Copyright (c) 2017-2024 Intel Corporation.
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

#ifndef BITS_H
#define BITS_H
#include <asm/lib/atomic.h>

/**
 * @defgroup lib_bits lib.bits
 * @ingroup lib
 * @brief Bitmap operations.
 *
 * This module provides a set of functions and macros for performing various bit operations on 32-bit and 64-bit
 * bitmaps. These operations include finding the index of the most significant or least significant bit set to 1,
 * counting the number of leading zeros, setting, clearing, testing, and counting the number of set bits in a bitmap.
 * The module also includes functions that ensure atomicity in multi-threaded environments by using the LOCK prefix.
 *
 * @{
 */

/**
 * @file
 * @brief Bitmap operation APIs.
 *
 * This file contains the implementation of various bitmap operation functions and macros. These functions are
 * designed to perform efficient bit manipulations on 32-bit and 64-bit bitmaps, including finding the index of
 * significant bits, counting leading zeros, and performing atomic set, clear, and test operations. The file also
 * includes macros to generate these functions for both 32-bit and 64-bit bitmaps, with and without the LOCK prefix
 * for atomicity.
 */

/**
 * @brief Invalid bit index.
 *
 * This macro means when input parameter is zero, bit operations function can't find bit set and return
 * the invalid bit index directly.
 */
#define INVALID_BIT_INDEX  0xffffU

/**
 * @brief Find the index of the most significant bit that is set to 1 in a 32-bit bitmap.
 *
 * This function is designed to find the index of the most significant bit that is set to 1 in a 32-bit unsigned
 * integer. If the input value is zero, it returns a predefined constant INVALID_BIT_INDEX. This function calls
 * the Bit Scan Reverse (bsrl) instruction to scan the bits of the input value from the most significant bit to the
 * least significant bit and stores the index of the first set bit.
 *
 * Examples:
 * - fls32(0x0) = INVALID_BIT_INDEX
 * - fls32(0x01) = 0
 * - fls32(0x80) = 7
 * - ...
 * - fls32(0x80000001) = 31
 *
 * @param[in] value The 32-bit bitmap which requires finding the most significant bit.
 *
 * @return Index of the most significant bit that is set to 1.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint16_t fls32(uint32_t value)
{
	uint32_t ret;
	asm volatile("bsrl %1,%0\n\t"
			"jnz 1f\n\t"
			"mov %2,%0\n"
			"1:" : "=r" (ret)
			: "rm" (value), "i" (INVALID_BIT_INDEX));
	return (uint16_t)ret;
}

/**
 * @brief Find the index of the most significant bit that is set to 1 in a 64-bit bitmap.
 *
 * This function is designed to find the index of the most significant bit that is set to 1 in a 64-bit unsigned
 * integer. If the input value is zero, it returns a predefined constant INVALID_BIT_INDEX. This function calls
 * the Bit Scan Reverse (bsrq) instruction to scan the bits of the input value from the most significant bit to the
 * least significant bit and stores the index of the first set bit.
 *
 * Examples:
 * - fls64(0x0) = INVALID_BIT_INDEX
 * - fls64(0x01) = 0
 * - fls64(0x80) = 7
 * - ...
 * - fls64(0x80000001) = 31
 * - ...
 * - fls64(0xFF0F000080000001) = 63
 *
 * @param[in] value The 64-bit bitmap which requires finding the most significant bit.
 *
 * @return Index of the most significant bit that is set to 1.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint16_t fls64(uint64_t value)
{
	uint64_t ret = 0UL;
	asm volatile("bsrq %1,%0\n\t"
			"jnz 1f\n\t"
			"mov %2,%0\n"
			"1:" : "=r" (ret)
			: "rm" (value), "i" (INVALID_BIT_INDEX));
	return (uint16_t)ret;
}

/**
 * @brief Find the index of the least significant bit that is set to 1 in a 64-bit bitmap.
 *
 * This function is designed to find the index of the least significant bit that is set to 1 in a 64-bit unsigned
 * integer. If the input value is zero, it returns a predefined constant INVALID_BIT_INDEX. This function calls
 * the Bit Scan Forward (bsfq) instruction to scan the bits of the input value from the least significant bit to the
 * most significant bit and stores the index of the first set bit.
 *
 * Examples:
 * - ffs64(0x0) = INVALID_BIT_INDEX
 * - ffs64(0x01) = 0
 * - ffs64(0xf0) = 4
 * - ffs64(0xf00) = 8
 * - ffs64(0x8000000000000001) = 0
 * - ffs64(0xf000000000000000) = 60
 *
 * @param[in] value The 64-bit bitmap which requires finding the least significant bit.
 *
 * @return Index of the least significant bit that is set to 1.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint16_t ffs64(uint64_t value)
{
	uint64_t ret;
	asm volatile("bsfq %1,%0\n\t"
			"jnz 1f\n\t"
			"mov %2,%0\n"
			"1:" : "=r" (ret)
			: "rm" (value), "i" (INVALID_BIT_INDEX));
	return (uint16_t)ret;
}

/**
 * @brief Find the index of the least significant bit that is set to 0 in a 64-bit bitmap.
 *
 * This function is designed to find the index of the least significant bit that is set to 0 in a 64-bit unsigned
 * integer. It achieves this by inverting the input value and then calling the `ffs64` function to find the index
 * of the least significant bit that is set to 1 in the inverted value. If the input value is all ones
 * (0xFFFFFFFFFFFFFFFF), it returns a predefined constant INVALID_BIT_INDEX.
 *
 * Examples:
 * - ffz64(0xFFFFFFFFFFFFFFFF) = INVALID_BIT_INDEX
 * - ffz64(0xFFFFFFFFFFFFFFFE) = 0
 * - ffz64(0xFFFFFFFFFFFFFF0F) = 4
 * - ffz64(0xFFFFFFFFFFFFF0FF) = 8
 * - ffz64(0x7FFFFFFFFFFFFFFF) = 63
 * - ffz64(0xF000000000000000) = 0
 *
 * @param[in] value The 64-bit bitmap which requires finding the least significant bit set to 0.
 *
 * @return Index of the least significant bit that is set to 0.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint16_t ffz64(uint64_t value)
{
	return ffs64(~value);
}

/**
 * @brief Find the index of the first zero bit in a uint64_t array.
 *
 * This function is designed to find the index of the first zero bit in an array of 64-bit unsigned integers.
 * It iterates through the array, checking each 64-bit value to see if it contains any zero bits. If a value
 * with zero bits is found, it calls the `ffz64` function to find the index of the least significant zero bit
 * within that value and calculates the overall index in the array. If no zero bits are found in the entire array,
 * it returns the total number of bits to search within this array.
 *
 * @param[in] addr The address of the 64-bit bitmap array.
 * @param[in] size The number of bits within the bitmap array.
 *
 * @return Index of the first zero bit in the array.
 *
 * @pre addr != NULL
 * @pre size % 64 == 0
 *
 * @post N/A
 */
static inline uint64_t ffz64_ex(const uint64_t *addr, uint64_t size)
{
	uint64_t ret = size;
	uint64_t idx;

	for (idx = 0UL; (idx << 6U) < size; idx++) {
		if (addr[idx] != ~0UL) {
			ret = (idx << 6U) + ffz64(addr[idx]);
			break;
		}
	}

	return ret;
}

/**
 * @brief Counts the number of leading zeros in a 32-bit value.
 *
 * This function is designed to count the number of leading zeros in a 32-bit unsigned integer. The number of leading
 * zeros is defined as the number of most significant bits which are '0'. If the input value is zero, it returns
 * 32. Otherwise, it uses the `fls32` function to find the index of the most significant bit set to 1 and calculates
 * the number of leading zeros by subtracting this index from 31.
 *
 * Examples:
 * - clz(0x80000000) = 0
 * - clz(0x40000000) = 1
 * - clz(0x00000001) = 31
 * - clz(0x00000000) = 32
 *
 * @param[in] value The 32-bit value to count the number of leading zeros.
 *
 * @return The number of leading zeros in 'value'.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint16_t clz(uint32_t value)
{
	return ((value != 0U) ? (31U - fls32(value)) : 32U);
}

/**
 * @brief Counts the number of leading zeros in a 64-bit value.
 *
 * This function is designed to count the number of leading zeros in a 64-bit unsigned integer. The number of leading
 * zeros is defined as the number of most significant bits which are '0'. If the input value is zero, it returns 64.
 * Otherwise, it uses the `fls64` function to find the index of the most significant bit set to 1 and calculates the
 * number of leading zeros by subtracting this index from 63.
 *
 * Examples:
 * - clz64(0x8000000000000000) = 0
 * - clz64(0x4000000000000000) = 1
 * - clz64(0x0000000000000001) = 63
 * - clz64(0x0000000000000000) = 64
 *
 * @param[in] value The 64-bit value to count the number of leading zeros.
 *
 * @return The number of leading zeros in 'value'.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint16_t clz64(uint64_t value)
{
	return ((value != 0UL) ? (63U - fls64(value)) : 64U);
}

/**
 * @brief Template for defining bitmap set functions.
 *
 * This macro serves as a template to create functions that set a specific bit in a 32-bit or 64-bit bitmap.
 * It generates both a version with the LOCK prefix, ensuring atomicity in multi-threaded environments,
 * and a version without the LOCK prefix.
 *
 * The following functions are included:
 * - build_bitmap_set(bitmap_set_nolock, "q", uint64_t, "")
 * - build_bitmap_set(bitmap_set_lock, "q", uint64_t, BUS_LOCK)
 * - build_bitmap_set(bitmap32_set_nolock, "l", uint32_t, "")
 * - build_bitmap_set(bitmap32_set_lock, "l", uint32_t, BUS_LOCK)
 *
 * These functions operate as: (*addr) |= (1UL<<nr). Inputs in these functions are defined as:
 * - input param 1[in]: nr_arg The bit index to set.
 * - input param 2[inout]: addr The address of the bitmap.
 *
 * @param[in] name The name of the function.
 * @param[in] op_len The operand length for the assembly instruction. "q" for 64-bit, "l" for 32-bit.
 * @param[in] op_type The type of the operand. "uint64_t" for 64-bit, "uint32_t" for 32 bit.
 * @param[in] lock The lock prefix for the assembly instruction, "BUS_LOCK" for lock or none.
 *
 * @return None
 *
 * @pre addr != NULL
 * @pre nr_arg < 64
 *
 * @post N/A
 *
 * @remark If nr_arg is larger than the total number of bits within the bitmap, it will be truncated.
 */
#define build_bitmap_set(name, op_len, op_type, lock)			\
static inline void name(uint16_t nr_arg, volatile op_type *addr)	\
{									\
	uint16_t nr;							\
	nr = nr_arg & ((8U * sizeof(op_type)) - 1U);			\
	asm volatile(lock "or" op_len " %1,%0"				\
			:  "+m" (*addr)					\
			:  "r" ((op_type)(1UL<<nr))			\
			:  "cc", "memory");				\
}

/**
 * @brief Set a bit in 64-bit bitmap.
 *
 * This function sets a specific bit in a 64-bit bitmap.
 *
 * @param[in] nr_arg The bit index to set.
 * @param[inout] addr The address of the bitmap.
 *
 * @return None
 *
 * @pre addr != NULL
 * @pre nr_arg < 64
 *
 * @post N/A
 */
build_bitmap_set(bitmap_set_nolock, "q", uint64_t, "")

/**
 * @brief Set a bit in 64-bit bitmap (LOCK version).
 *
 * This function sets a specific bit in a 64-bit bitmap. This function uses LOCK prefix to ensure atomicity.
 *
 * @param[in] nr_arg The bit index to set.
 * @param[inout] addr The address of the bitmap.
 *
 * @return None
 *
 * @pre addr != NULL
 * @pre nr_arg < 64
 *
 * @post N/A
 */
build_bitmap_set(bitmap_set_lock, "q", uint64_t, BUS_LOCK)

/**
 * @brief Set a bit in 32-bit bitmap.
 *
 * This function sets a specific bit in a 32-bit bitmap.
 *
 * @param[in] nr_arg The bit index to set.
 * @param[inout] addr The address of the bitmap.
 *
 * @return None
 *
 * @pre addr != NULL
 * @pre nr_arg < 32
 *
 * @post N/A
 */
build_bitmap_set(bitmap32_set_nolock, "l", uint32_t, "")

/**
 * @brief Set a bit in 32-bit bitmap (LOCK version).
 *
 * This function sets a specific bit in a 32-bit bitmap. This function uses LOCK prefix to ensure atomicity.
 *
 * @param[in] nr_arg The bit index to set.
 * @param[inout] addr The address of the bitmap.
 *
 * @return None
 *
 * @pre addr != NULL
 * @pre nr_arg < 32
 *
 * @post N/A
 */
build_bitmap_set(bitmap32_set_lock, "l", uint32_t, BUS_LOCK)

/**
 * @brief Template for defining bitmap clear functions.
 *
 * This macro serves as a template to create functions that clear a specific bit in a 32-bit or 64-bit bitmap.
 * It generates both a version with the LOCK prefix, ensuring atomicity in multi-threaded environments,
 * and a version without the LOCK prefix.
 *
 * The following functions are included:
 * - build_bitmap_clear(bitmap_clear_nolock, "q", uint64_t, "")
 * - build_bitmap_clear(bitmap_clear_lock, "q", uint64_t, BUS_LOCK)
 * - build_bitmap_clear(bitmap32_clear_nolock, "l", uint32_t, "")
 * - build_bitmap_clear(bitmap32_clear_lock, "l", uint32_t, BUS_LOCK)
 *
 * These functions operate as: (*addr) &= ~(1UL<<nr). Inputs in these functions are defined as:
 * - input param 1[in]: nr_arg The bit index to set.
 * - input param 2[inout]: addr The address of the bitmap.
 *
 * @param[in] name The name of the function.
 * @param[in] op_len The operand length for the assembly instruction. "q" for 64-bit, "l" for 32-bit.
 * @param[in] op_type The type of the operand. "uint64_t" for 64-bit, "uint32_t" for 32 bit.
 * @param[in] lock The lock prefix for the assembly instruction, "BUS_LOCK" for lock or none.
 *
 * @return None
 *
 * @pre addr != NULL
 * @pre nr_arg < 64
 *
 * @post N/A
 *
 * @remark If nr_arg is larger than the total number of bits within the bitmap, it will be truncated.
 */
#define build_bitmap_clear(name, op_len, op_type, lock)			\
static inline void name(uint16_t nr_arg, volatile op_type *addr)	\
{									\
	uint16_t nr;							\
	nr = nr_arg & ((8U * sizeof(op_type)) - 1U);			\
	asm volatile(lock "and" op_len " %1,%0"				\
			:  "+m" (*addr)					\
			:  "r" ((op_type)(~(1UL<<(nr))))		\
			:  "cc", "memory");				\
}

/**
 * @brief Clear a bit in 64-bit bitmap.
 *
 * This function clears a specific bit in a 64-bit bitmap.
 *
 * @param[in] nr_arg The bit index to clear.
 * @param[inout] addr The address of the bitmap.
 *
 * @return None
 *
 * @pre addr != NULL
 * @pre nr_arg < 64
 *
 * @post N/A
 */
build_bitmap_clear(bitmap_clear_nolock, "q", uint64_t, "")

/**
 * @brief Clear a bit in 64-bit bitmap (LOCK version).
 *
 * This function clears a specific bit in a 64-bit bitmap. This function uses LOCK prefix to ensure atomicity.
 *
 * @param[in] nr_arg The bit index to clear.
 * @param[inout] addr The address of the bitmap.
 *
 * @return None
 *
 * @pre addr != NULL
 * @pre nr_arg < 64
 *
 * @post N/A
 */
build_bitmap_clear(bitmap_clear_lock, "q", uint64_t, BUS_LOCK)

/**
 * @brief Clear a bit in 32-bit bitmap.
 *
 * This function clears a specific bit in a 32-bit bitmap.
 *
 * @param[in] nr_arg The bit index to clear.
 * @param[inout] addr The address of the bitmap.
 *
 * @return None
 *
 * @pre addr != NULL
 * @pre nr_arg < 32
 *
 * @post N/A
 */
build_bitmap_clear(bitmap32_clear_nolock, "l", uint32_t, "")

/**
 * @brief Clear a bit in 32-bit bitmap (LOCK version).
 *
 * This function clears a specific bit in a 32-bit bitmap. This function uses LOCK prefix to ensure atomicity.
 *
 * @param[in] nr_arg The bit index to clear.
 * @param[inout] addr The address of the bitmap.
 *
 * @return None
 *
 * @pre addr != NULL
 * @pre nr_arg < 32
 *
 * @post N/A
 */
build_bitmap_clear(bitmap32_clear_lock, "l", uint32_t, BUS_LOCK)

/**
 * @brief Test a bit in a 64-bit bitmap.
 *
 * This function tests a specific bit in a 64-bit bitmap.
 *
 * This function operates as: return !!((*addr) & (1UL<<nr))
 *
 * @param[in] nr The bit index to test.
 * @param[in] addr The address of the bitmap.
 *
 * @return A boolean value indicating whether this bit is set or not.
 *
 * @retval True This bit is set.
 * @retval False This bit is not set.
 *
 * @pre addr != NULL
 * @pre nr < 64
 *
 * @post N/A
 *
 * @remark If nr is larger than the total number of bits within the bitmap, it will be truncated.
 */
static inline bool bitmap_test(uint16_t nr, const volatile uint64_t *addr)
{
	int32_t ret = 0;
	asm volatile("btq %q2,%1\n\tsbbl %0, %0"
			: "=r" (ret)
			: "m" (*addr), "r" ((uint64_t)(nr & 0x3fU))
			: "cc", "memory");
	return (ret != 0);
}

/**
 * @brief Test a bit in a 32-bit bitmap.
 *
 * This function tests a specific bit in a 32-bit bitmap.
 *
 * This function operates as: return !!((*addr) & (1UL << nr))
 *
 * @param[in] nr The bit index to test.
 * @param[in] addr The address of the bitmap.
 *
 * @return A boolean value indicating whether this bit is set or not.
 *
 * @retval True This bit is set.
 * @retval False This bit is not set.
 *
 * @pre addr != NULL
 * @pre nr < 32
 *
 * @post N/A
 *
 * @remark If nr is larger than the total number of bits within the bitmap, it will be truncated.
 */
static inline bool bitmap32_test(uint16_t nr, const volatile uint32_t *addr)
{
	int32_t ret = 0;
	asm volatile("btl %2,%1\n\tsbbl %0, %0"
			: "=r" (ret)
			: "m" (*addr), "r" ((uint32_t)(nr & 0x1fU))
			: "cc", "memory");
	return (ret != 0);
}

/**
 * @brief Template for defining bitmap test and set functions.
 *
 * This macro serves as a template to create functions that test and set a specific bit in a 32-bit or 64-bit bitmap.
 * It generates both a version with the LOCK prefix, ensuring atomicity in multi-threaded environments,
 * and a version without the LOCK prefix.
 *
 * The following functions are included:
 * - build_bitmap_testandset(bitmap_test_and_set_nolock, "q", uint64_t, "")
 * - build_bitmap_testandset(bitmap_test_and_set_lock, "q", uint64_t, BUS_LOCK)
 * - build_bitmap_testandset(bitmap32_test_and_set_nolock, "l", uint32_t, "")
 * - build_bitmap_testandset(bitmap32_test_and_set_lock, "l", uint32_t, BUS_LOCK)
 *
 * These functions operate as:
 * - bool ret = (*addr) & (1UL<<nr);
 * - (*addr) |= (1UL<<nr);
 * - return ret;
 *
 * Inputs in these functions are defined as:
 * - input param 1[in]: nr_arg The bit index to set.
 * - input param 2[inout]: addr The address of the bitmap.
 *
 * @param[in] name The name of the function.
 * @param[in] op_len The operand length for the assembly instruction. "q" for 64-bit, "l" for 32-bit.
 * @param[in] op_type The type of the operand. "uint64_t" for 64-bit, "uint32_t" for 32 bit.
 * @param[in] lock The lock prefix for the assembly instruction, "BUS_LOCK" for lock or none.
 *
 * @return A boolean value indicating whether this bit was already set or not.
 *
 * @retval True This bit was already set.
 * @retval False This bit was not set, this function sets this bit.
 *
 * @pre addr != NULL
 * @pre nr_arg < 64
 *
 * @post N/A
 *
 * @remark If nr_arg is larger than the total number of bits within the bitmap, it will be truncated.
 */
#define build_bitmap_testandset(name, op_len, op_type, lock)		\
static inline bool name(uint16_t nr_arg, volatile op_type *addr)	\
{									\
	uint16_t nr;							\
	int32_t ret=0;							\
	nr = nr_arg & ((8U * sizeof(op_type)) - 1U);			\
	asm volatile(lock "bts" op_len " %2,%1\n\tsbbl %0,%0"		\
			: "=r" (ret), "=m" (*addr)			\
			: "r" ((op_type)nr)				\
			: "cc", "memory");				\
	return (ret != 0);						\
}

/**
 * @brief Test and set a bit in a 64-bit bitmap.
 *
 * This function tests and sets a specific bit in a 64-bit bitmap.
 *
 * @param[in] nr_arg The bit index to test and set.
 * @param[inout] addr The address of the bitmap.
 *
 * @return A boolean value indicating whether this bit was already set or not.
 *
 * @retval True This bit was already set.
 * @retval False This bit was not set, this function sets this bit.
 *
 * @pre addr != NULL
 * @pre nr_arg < 64
 *
 * @post N/A
 */
build_bitmap_testandset(bitmap_test_and_set_nolock, "q", uint64_t, "")

/**
 * @brief Test and set a bit in a 64-bit bitmap (LOCK version).
 *
 * This function tests and sets a specific bit in a 64-bit bitmap. This function uses LOCK prefix to ensure atomicity.
 *
 * @param[in] nr_arg The bit index to test and set.
 * @param[inout] addr The address of the bitmap.
 *
 * @return A boolean value indicating whether this bit was already set or not.
 *
 * @retval True This bit was already set.
 * @retval False This bit was not set, this function sets this bit.
 *
 * @pre addr != NULL
 * @pre nr_arg < 64
 *
 * @post N/A
 */
build_bitmap_testandset(bitmap_test_and_set_lock, "q", uint64_t, BUS_LOCK)

/**
 * @brief Test and set a bit in a 32-bit bitmap.
 *
 * This function tests and sets a specific bit in a 32-bit bitmap.
 *
 * @param[in] nr_arg The bit index to test and set.
 * @param[inout] addr The address of the bitmap.
 *
 * @return A boolean value indicating whether this bit was already set or not.
 *
 * @retval True This bit was already set.
 * @retval False This bit was not set, this function sets this bit.
 *
 * @pre addr != NULL
 * @pre nr_arg < 32
 *
 * @post N/A
 */
build_bitmap_testandset(bitmap32_test_and_set_nolock, "l", uint32_t, "")

/**
 * @brief Test and set a bit in a 32-bit bitmap (LOCK version).
 *
 * This function tests and sets a specific bit in a 32-bit bitmap. This function uses LOCK prefix to ensure atomicity.
 *
 * @param[in] nr_arg The bit index to test and set.
 * @param[inout] addr The address of the bitmap.
 *
 * @return A boolean value indicating whether this bit was already set or not.
 *
 * @retval True This bit was already set.
 * @retval False This bit was not set, this function sets this bit.
 *
 * @pre addr != NULL
 * @pre nr_arg < 32
 *
 * @post N/A
 */
build_bitmap_testandset(bitmap32_test_and_set_lock, "l", uint32_t, BUS_LOCK)

/**
 * @brief Template for defining bitmap test and clear functions.
 *
 * This macro serves as a template to create functions that test and clear a specific bit in a 32-bit or 64-bit bitmap.
 * It generates both a version with the LOCK prefix, ensuring atomicity in multi-threaded environments,
 * and a version without the LOCK prefix.
 *
 * The following functions are included:
 * - build_bitmap_testandclear(bitmap_test_and_clear_nolock, "q", uint64_t, "")
 * - build_bitmap_testandclear(bitmap_test_and_clear_lock, "q", uint64_t, BUS_LOCK)
 * - build_bitmap_testandclear(bitmap32_test_and_clear_nolock, "l", uint32_t, "")
 * - build_bitmap_testandclear(bitmap32_test_and_clear_lock, "l", uint32_t, BUS_LOCK)
 *
 * These functions operate as:
 * - bool ret = (*addr) & (1UL<<nr);
 * - (*addr) &= ~(1UL<<nr);
 * - return ret;
 *
 * Inputs in these functions are defined as:
 * - input param 1[in]: nr_arg The bit index to set.
 * - input param 2[inout]: addr The address of the bitmap.
 *
 * @param[in] name The name of the function.
 * @param[in] op_len The operand length for the assembly instruction. "q" for 64-bit, "l" for 32-bit.
 * @param[in] op_type The type of the operand. "uint64_t" for 64-bit, "uint32_t" for 32 bit.
 * @param[in] lock The lock prefix for the assembly instruction, "BUS_LOCK" for lock or none.
 *
 * @return A boolean value indicating whether this bit was already set or not.
 *
 * @retval True This bit was already set, this function clears this bit.
 * @retval False This bit was not set.
 *
 * @pre addr != NULL
 * @pre nr_arg < 64
 *
 * @post N/A
 *
 * @remark If nr_arg is larger than the total number of bits within the bitmap, it will be truncated.
 */
#define build_bitmap_testandclear(name, op_len, op_type, lock)		\
static inline bool name(uint16_t nr_arg, volatile op_type *addr)	\
{									\
	uint16_t nr;							\
	int32_t ret=0;							\
	nr = nr_arg & ((8U * sizeof(op_type)) - 1U);			\
	asm volatile(lock "btr" op_len " %2,%1\n\tsbbl %0,%0"		\
			: "=r" (ret), "=m" (*addr)			\
			: "r" ((op_type)nr)				\
			: "cc", "memory");				\
	return (ret != 0);						\
}

/**
 * @brief Test and clear a bit in a 64-bit bitmap.
 *
 * This function tests and clears a specific bit in a 64-bit bitmap.
 *
 * @param[in] nr_arg The bit index to test and clear.
 * @param[inout] addr The address of the bitmap.
 *
 * @return A boolean value indicating whether this bit was already set or not.
 *
 * @retval True This bit was already set, this function clears this bit.
 * @retval False This bit was not set.
 *
 * @pre addr != NULL
 * @pre nr_arg < 64
 *
 * @post N/A
 */
build_bitmap_testandclear(bitmap_test_and_clear_nolock, "q", uint64_t, "")

/**
 * @brief Test and clear a bit in a 64-bit bitmap (LOCK version).
 *
 * This function tests and clears a specific bit in a 64-bit bitmap. This function uses LOCK prefix to ensure atomicity.
 *
 * @param[in] nr_arg The bit index to test and clear.
 * @param[inout] addr The address of the bitmap.
 *
 * @return A boolean value indicating whether this bit was already set or not.
 *
 * @retval True This bit was already set, this function clears this bit.
 * @retval False This bit was not set.
 *
 * @pre addr != NULL
 * @pre nr_arg < 64
 *
 * @post N/A
 */
build_bitmap_testandclear(bitmap_test_and_clear_lock, "q", uint64_t, BUS_LOCK)

/**
 * @brief Test and clear a bit in a 32-bit bitmap.
 *
 * This function tests and clears a specific bit in a 32-bit bitmap.
 *
 * @param[in] nr_arg The bit index to test and clear.
 * @param[inout] addr The address of the bitmap.
 *
 * @return A boolean value indicating whether this bit was already set or not.
 *
 * @retval True This bit was already set, this function clears this bit.
 * @retval False This bit was not set.
 *
 * @pre addr != NULL
 * @pre nr_arg < 32
 *
 * @post N/A
 */
build_bitmap_testandclear(bitmap32_test_and_clear_nolock, "l", uint32_t, "")

/**
 * @brief Test and clear a bit in a 32-bit bitmap (LOCK version).
 *
 * This function tests and clears a specific bit in a 32-bit bitmap. This function uses LOCK prefix to ensure atomicity.
 *
 * @param[in] nr_arg The bit index to test and clear.
 * @param[inout] addr The address of the bitmap.
 *
 * @return A boolean value indicating whether this bit was already set or not.
 *
 * @retval True This bit was already set, this function clears this bit.
 * @retval False This bit was not set.
 *
 * @pre addr != NULL
 * @pre nr_arg < 32
 *
 * @post N/A
 */
build_bitmap_testandclear(bitmap32_test_and_clear_lock, "l", uint32_t, BUS_LOCK)

/**
 * @brief Hamming weight of a bitmap.
 *
 * This function calculates the number of set bits (Hamming weight) in a 64-bit bitmap. It uses the GCC built-in
 * function `__builtin_popcountl()` to efficiently count the number of bits set to 1 in the provided 64-bit integer.
 *
 * Examples:
 * - bitmap_weight(0x0) = 0
 * - bitmap_weight(0x1) = 1
 * - bitmap_weight(0xF) = 4
 * - bitmap_weight(0xFFFFFFFFFFFFFFFF) = 64
 *
 * @param[in] bits The 64-bit bitmap for which the Hamming weight is to be calculated.
 *
 * @return The number of set bits in the 64-bit bitmap.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint16_t bitmap_weight(uint64_t bits)
{
	return (uint16_t)__builtin_popcountl(bits);
}

#endif /* BITS_H */

/**
 * @}
 */