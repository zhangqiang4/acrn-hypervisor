/*
 * Copyright (C) 2021-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef COMMON_TICKS_H
#define COMMON_TICKS_H

#include <types.h>

/**
 * @addtogroup hwmgmt_time
 *
 * @{
 */

/**
 * @file
 * @brief Ticks conversion and management functions.
 *
 * This file contains the declarations of functions that convert between different time units and CPU ticks, as
 * well as functions to read the current CPU tick count and get the CPU tick frequency.
 */

/**
 * @brief Get CPU ticks per millisecond.
 *
 * This macro returns the number of CPU ticks that correspond to one millisecond. It leverages the us_to_ticks()
 * function to perform the conversion from microseconds to CPU ticks, ensuring precise timing operations.
 *
 */
#define TICKS_PER_MS	us_to_ticks(1000U)

uint64_t cpu_ticks(void);

uint32_t cpu_tickrate(void);

uint64_t us_to_ticks(uint32_t us);

uint64_t ticks_to_us(uint64_t ticks);

uint64_t ticks_to_ms(uint64_t ticks);

#endif	/* COMMON_TICKS_H */

/**
 * @}
 */