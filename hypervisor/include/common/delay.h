/*
 * Copyright (C) 2021-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef COMMON_DELAY_H
#define COMMON_DELAY_H

#include <types.h>

/**
 * @addtogroup hwmgmt_time
 *
 * @{
 */

/**
 * @file
 * @brief Delay functions.
 *
 * This file contains the declaration of functions that implement busy-wait delays by converting time units into
 * CPU ticks.
 */

void udelay(uint32_t us);

#endif /* COMMON_DELAY_H */

/**
 * @}
 */