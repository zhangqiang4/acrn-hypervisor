/*
 * Copyright (C) 2021-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <common/ticks.h>
#include <common/delay.h>

/**
 * @addtogroup hwmgmt_time
 *
 * @{
 */

/**
 * @file
 * @brief Delay functions.
 *
 * This file contains functions that implement busy-wait delays by converting time units into CPU ticks and looping
 * until the specified time has elapsed.
 */

/**
 * @brief Delay for a specified number of microseconds.
 *
 * This function converts the specified number of microseconds into CPU ticks and then loops until the current CPU
 * ticks reach the calculated end time. It is used to create a busy-wait delay for precise timing requirements.
 *
 * @param[in] us The number of microseconds to delay.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */

void udelay(uint32_t us)
{
	uint64_t end, delta;

	/* Calculate number of ticks to wait */
	delta = us_to_ticks(us);
	end = cpu_ticks() + delta;

	/* Loop until time expired */
	while (cpu_ticks() < end) {
	}
}

/**
 * @}
 */