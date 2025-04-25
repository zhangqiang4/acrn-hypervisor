/*
 * Copyright (C) 2018-2025 Intel Corporation.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <lib/rtl.h>

/**
 * @addtogroup lib_util
 *
 * @{
 */

/**
 * @file
 * @brief Implementation of setting a block of memory to a specific value.
 *
 * This file implements all the APIs to support data value operation in memory.
 */

/**
 * @brief Data value setting for n bytes for specific memory address.
 *
 * This function calls inline assembly instruction "rep stosb" to set value for a block of memory.
 *
 * - It calls the inline assembly instruction to repeat stosb for n times setting each byte in the memory block
 * starting at base to the value v.
 *
 * @param[inout] base  Pointer to memory address
 * @param[in] v        The value need to be set
 * @param[in] n        Number of bytes to set
 *
 * @return None.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline void memset_erms(void *base, uint8_t v, size_t n)
{
	asm volatile("rep ; stosb"
			: "+D"(base)
			: "a" (v), "c"(n));
}

/**
 * @brief Data value setting at n bytes for specific memory address.
 *
 * This function is to set value v for the memory specified by the address base for the length of n bytes.
 *
 * - It does the memory set when runtime-constrain checking passes, that is that input base is not NULL and
 * input n is not zero. For detailed operation, refer to function memset_erms(). Then it returns the input
 * base pointer.
 *
 * @param[inout] base  Pointer without casting type pointing to memory address
 * @param[in] v        The value need to be set
 * @param[in] n        Number of bytes to set
 *
 * @return A void pointer same with input parameter base.
 *
 * @pre N/A
 *
 * @post N/A
 */
void *memset(void *base, uint8_t v, size_t n)
{
	/*
	 * Some CPUs support enhanced REP MOVSB/STOSB feature. It is recommended
	 * to use it when possible.
	 */
	if ((base != NULL) && (n != 0U)) {
		memset_erms(base, v, n);
        }

	return base;
}

/**
 * @brief Memory copies.
 *
 * This function is to use inline assembly instruction enhanced REP MOVSB to do memory copy from source
 * address to destination address by the length of slen.
 *
 * @param[inout] d     Pointer to Destination address
 * @param[in] s        Pointer to Source address
 * @param[in] slen     Number of bytes to copy
 *
 * @return None.
 *
 * @pre d != NULL
 * @pre s != NULL
 *
 * @post N/A
 */
void memcpy_erms(void *d, const void *s, size_t slen)
{
	asm volatile ("rep; movsb"
		: "=&D"(d), "=&S"(s)
		: "c"(slen), "0" (d), "1" (s)
		: "memory");
}

/**
 * @brief Backward memory copies.
 *
 * This function is to use inline assembly instruction enhanced REP MOVSB to do backward memory copy
 * from source address to destination address by the length of slen, starting from the highest memory
 * address and proceeding towards the lowest.
 *
 * @param[inout] d     Pointer pointing to Destination address without casting
 * @param[in] s        Constant pointer pointing to Source address without casting
 * @param[in] slen     Number of bytes to copy
 *
 * @return None.
 *
 * @pre d != NULL
 * @pre s != NULL
 *
 * @post N/A
 */
void memcpy_erms_backwards(void *d, const void *s, size_t slen)
{
	asm volatile ("std; rep; movsb; cld"
		: "=&D"(d), "=&S"(s)
		: "c"(slen), "0" (d), "1" (s)
		: "memory");
}

/**
 * @brief  Copies at most slen bytes from src address to dest address, up to dmax.
 *
 * This function does the memory copy when runtime-constrain checking passes, and the checking
 * includes that both d and s are not NULL, and the destination buffer size dmax is at least as
 * large as the source length slen, and source and destination buffer are either non-overlapping
 * or properly seperated, that is that the address position of destination buffer is more than that
 * of the end of the source buffer with the slen length, or the address position of source buffer is more
 * than that of the end of destination buffer with the dmax length.
 *
 * - If runtime-constrain checking passes and slen is not zero, it calls the function memcpy_erms() to
 * do the memory copy, and return 0. For detailed operation, refer to function memcpy_erms().
 * - If runtime-constrain checking passes and slen is zero, It returns 0.
 * - If runtime-constrain checking fails, it calls the function memset() to set the destination buffer
 * with 0 for length dmax, and returns -1. For detailed operation, refer to function memset().
 *
 * @param[inout] d     Pointer without casting type to destination buffer
 * @param[in] dmax     Maximum  length of destination buffer
 * @param[in] s        Constant pointer without casting type pointing to source buffer
 * @param[in] slen     Maximum number of bytes of src to copy
 *
 * @return A int32_t value to indicate the status of memory copy operation.
 *
 * @retval 0           On success.
 * @retval -1          For runtime-constraint violation.
 *
 * @pre N/A
 *
 * @post N/A
 */
int32_t memcpy_s(void *d, size_t dmax, const void *s, size_t slen)
{
	int32_t ret = -1;

	if ((d != NULL) && (s != NULL) && (dmax >= slen) && ((d > (s + slen)) || (s > (d + dmax)))) {
		if (slen != 0U) {
			memcpy_erms(d, s, slen);
		}
		ret = 0;
	} else {
		(void)memset(d, 0U, dmax);
	}

	return ret;
}

/**
 * @}
 */
