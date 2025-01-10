/*
 * Copyright (C) 2020-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef HASH_H
#define HASH_H

#include <types.h>

/**
 * @addtogroup lib_util
 *
 * @{
 */

/**
 * @file
 * @brief Implementation of hash function.
 *
 * This file implements the APIs to support hash operation.
 */

/**
 * @brief Definition of hash factor.
 *
 * Hash factor is selected following below formula:
 *  - factor64 = 2^64 * ((sqrt(5) - 1)/2)
 * here, ((sqrt(5) - 1)/2) is golden ratio.
 */
#define HASH_FACTOR64 0x9E3779B9486E555EUL

/**
 * @brief Hash calculation for a 64 bit data.
 *
 * This inline function implements the hash operation for specific 64 bit key value.
 *
 * - It multiplies key value and HASH_FACTOR64 and then generate the effective high
 * bits width result by right shifting 64 minus bits.
 *
 * @param[in] key         The key to be calculated.
 * @param[in] bits        The effective bit width of the return value
 *
 * @return A uint64_t value of the hash result.
 *
 * @pre bits < 64
 *
 * @post N/A
 */
static inline uint64_t hash64(uint64_t key, uint32_t bits)
{
	return (key * HASH_FACTOR64) >> (64U - bits);
}

/**
 * @}
 */

#endif /* HASH_H */
