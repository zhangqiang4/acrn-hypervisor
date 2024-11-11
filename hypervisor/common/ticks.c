/*
 * Copyright (C) 2021-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <common/ticks.h>

/**
 * @addtogroup hwmgmt_time
 *
 * @{
 */

/**
 * @file
 * @brief Ticks conversion functions.
 *
 * This file contains functions that convert between different time units and CPU ticks.
 */

/**
 * @brief Convert microseconds into CPU ticks.
 *
 * This function converts the specified number of microseconds into CPU ticks based on the CPU tick rate. It is
 * used to facilitate precise timing operations.
 *
 * @param[in] us The number of microseconds to convert.
 *
 * @return A uint64_t value of the equivalent number of CPU ticks.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark cpu_ticks() and cpu_tickrate() are provided in arch specific modules
 */
uint64_t us_to_ticks(uint32_t us)
{
	return (((uint64_t)us * (uint64_t)cpu_tickrate()) / 1000UL);
}

/**
 * @brief Convert CPU ticks into microseconds.
 *
 * This function converts the specified number of CPU ticks into microseconds based on the CPU tick rate. It is
 * used to facilitate precise timing operations.
 *
 * @param[in] ticks The number of CPU ticks to convert.
 *
 * @return A uint64_t value of the equivalent number of microseconds.
 *
 * @pre N/A
 *
 * @post N/A
 */
uint64_t ticks_to_us(uint64_t ticks)
{
	uint64_t us = 0UL;
	uint64_t khz = cpu_tickrate();

	if (khz != 0U) {
		us = (ticks * 1000UL) / (uint64_t)khz;
	}

	return us;
}

/**
 * @brief Convert CPU ticks into milliseconds.
 *
 * This function converts the specified number of CPU ticks into milliseconds based on the CPU tick rate. It is
 * used to facilitate precise timing operations.
 *
 * @param[in] ticks The number of CPU ticks to convert.
 *
 * @return A uint64_t value of the equivalent number of milliseconds.
 *
 * @pre N/A
 *
 * @post N/A
 */
uint64_t ticks_to_ms(uint64_t ticks)
{
	return ticks / (uint64_t)cpu_tickrate();
}

/**
 * @}
 */