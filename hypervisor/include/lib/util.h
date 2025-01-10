/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef UTIL_H
#define UTIL_H
#include <types.h>

/**
 * @addtogroup lib_util
 *
 * @{
 */

/**
 * @file
 * @brief Implementation of number calculation function.
 *
 * This file implements all the APIs to support memory boundary check and
 * UUID comparation and sum and checksum calculation.
 */

/**
 * @brief Compute the offset of a member within a structure.
 *
 * This macro wraps a compiler-provided intrinsic to compute the offset of a member within a structure.
 *
 * @param[in] st        The type of the structure.
 * @param[in] m         The name of the member within the structure.
 *
 * @return A numerical value indicating the offset
 *
 * @pre N/A
 *
 * @post N/A
 */
#define offsetof(st, m) __builtin_offsetof(st, m)
#define va_start	__builtin_va_start /**< A wrapper for compiler-specific built-in function __builtin_va_start. */
#define va_end		__builtin_va_end /**< A wrapper for compiler-specific built-in function __builtin_va_end. */

/**
 * @brief Divide integer with rounded up result.
 *
 * This macro performs integer division, and ensures quotient is rounded up to the nearest whole number
 * if there is any remainder.
 *
 * @param[in] x         The value to be divided.
 * @param[in] y         The divisor.
 *
 * @return A numerical value indicating rounded up result
 *
 * @pre N/A
 *
 * @post N/A
 */
#define INT_DIV_ROUNDUP(x, y)	((((x)+(y))-1)/(y))

/**
 * @brief Round up number.
 *
 * This macro rounds up a given number x to the next multiple of another number y.
 *
 * @param[in] x         The number to round up.
 * @param[in] y         The alignment factor.
 *
 * @return A numerical value indicating rounded up result
 *
 * @pre N/A
 *
 * @post N/A
 */
#define roundup(x, y)  (((x) + ((y) - 1UL)) & (~((y) - 1UL)))

/**
 * @brief Calculate the minimum of two values.
 *
 * This macro evaluates both arguments and returns the smaller of the two. It uses the ternary operator (? :) for
 * comparison and selection.
 *
 * @param[in] x         The first value to compare.
 * @param[in] y         The second value to compare.
 *
 * @return A numerical value indicating the minimum of input parameters
 *
 * @pre N/A
 *
 * @post N/A
 */
#define min(x, y)	(((x) < (y)) ? (x) : (y))

/**
 * @brief Calculate the maximum of two values.
 *
 * This macro evaluates both arguments and returns the bigger of the two. It uses the ternary operator (? :) for
 * comparison and selection.
 *
 * @param[in] x         The first value to compare.
 * @param[in] y         The second value to compare.
 *
 * @return A numerical value indicating the maximum of input parameters
 *
 * @pre N/A
 *
 * @post N/A
 */
#define max(x, y)	(((x) < (y)) ? (y) : (x))

/**
 * @brief Constrain a value within a specified range.
 *
 * This macro restricts the value v to lie within the range [l, h]. If v is less than l, the result is l.
 * If v is greater than h, the result is h. Otherwise, the result is v.
 *
 * @param[in] v         The value to be clamped.
 * @param[in] l         The lower bound.
 * @param[in] h         The upper bound.
 *
 * @return A numerical value indicating the constrained given number
 *
 * @pre N/A
 *
 * @post N/A
 */
#define clamp(v, l, h)	(max(min((v), (h)), (l)))

/**
 * @brief Convert its argument into a string literal.
 *
 * This macro converts the macro argument into a string literal.
 *
 * @param[in] x         Value to be converted.
 *
 * @return A string corresponding to the input parameter
 *
 * @pre N/A
 *
 * @post N/A
 */
#define STRINGIFY(x) #x

/**
 * @brief Check data memory boundary alignment.
 *
 * This inline function checks if a given 64 bits data is aligned to a given required memory boundary.
 *
 * - It does the bitwise AND operation between value and (req_align-1) to check the lower bit of value,
 * and compare the result with 0, if it is 0, value is aligned to the req_align boundary, if not, value
 * is not aligned to the req_align boundary.
 *
 * @param[in] value       uint64_t type data to check
 * @param[in] req_align   uint64_t type value of alignment requirement
 *
 * @return A bool type value to indicate checking result.
 *
 * @retval true  If aligned.
 * @retval false If not aligned.
 *
 * @pre (req_align & (req_align - 1)) == 0
 *
 * @post N/A
 */
static inline bool mem_aligned_check(uint64_t value, uint64_t req_align)
{
	return ((value & (req_align - 1UL)) == 0UL);
}

/**
 * @brief Calculate the sum of the given number of bytes.
 *
 * This inline function calculate the sum for a given buffer.
 *
 * - It converts the given buffer to constant uint8_t type and accumulate data in the buffer
 * from 0 up to the count of length-1 and store and return the result.
 *
 * @param[in] buf        constant pointer without casting type pointing to buffer to calculate
 * @param[in] length     uint32_t type value of the count of the data in the buffer to calculate
 *
 * @return A uint8_t type value of calculation result.
 *
 * @pre buf != NULL
 *
 * @post N/A
 */
static inline uint8_t calculate_sum8(const void *buf, uint32_t length)
{
	uint32_t i;
	uint8_t sum = 0U;

	for (i = 0U; i < length; i++) {
		sum += *((const uint8_t *)buf + i);
	}

	return sum;
}

/**
 * @brief Calculate the checksum for a given length of buffer.
 *
 * This inline function calls the function calculate_sum8() to get the sum value of the given buffer
 * and do the calculation of 0x100 minus the sum result, and then convert the calculation result to
 * uint8_t type and return the value. For detailed operation, refer to function calculate_sum8().
 *
 * - if difference is zero, the function returns 0.
 * - if difference is not zero, the function returns the converted uint8_t type result.
 *
 * @param[in] buf        constant pointer without casting type pointing to buffer to calculate
 * @param[in] len        uint32_t type value of the count of the data in the buffer to calculate
 *
 * @return A uint8_t type value of checksum result.
 *
 * @pre buf != NULL
 *
 * @post N/A
 */
static inline uint8_t calculate_checksum8(const void *buf, uint32_t len)
{
	return (uint8_t)(0x100U - calculate_sum8(buf, len));
}

/**
 * @brief Check if the two uuids equal.
 *
 * This inline function gets the high 64 bits and the low 64 bits for the both first and the second uuids,
 * and then compare them respectively.
 *
 * - if they are all equal, the function returns true.
 * - if they are not all equal, the function returns false.
 *
 * @param[in] uuid1      constant uint8_t pointer pointing to the first uuid number
 * @param[in] uuid2      constant uint8_t pointer pointing to the second uuid number
 *
 * @return A bool type value to indicate checking result.
 *
 * @retval true  When the first uuid is the same with the second uuid.
 * @retval false When the first uuid is not the same with the second uuid.
 *
 * @pre uuid1 != NULL
 * @pre uuid2 != NULL
 * @pre uuid1+8 != NULL
 * @pre uuid2+8 != NULL
 *
 * @post N/A
 */
static inline bool uuid_is_equal(const uint8_t *uuid1, const uint8_t *uuid2)
{
	uint64_t uuid1_h = *(const uint64_t *)uuid1;
	uint64_t uuid1_l = *(const uint64_t *)(uuid1 + 8);
	uint64_t uuid2_h = *(const uint64_t *)uuid2;
	uint64_t uuid2_l = *(const uint64_t *)(uuid2 + 8);

	return ((uuid1_h == uuid2_h) && (uuid1_l == uuid2_l));
}

/**
 * @}
 */

#endif /* UTIL_H */
