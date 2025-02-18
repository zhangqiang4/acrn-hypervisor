/*
 * Copyright (C) 2021-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef HW_TIMER_H
#define HW_TIMER_H

#include <types.h>

/**
 * @addtogroup hwmgmt_timer
 *
 * @{
 */

/**
 * @file
 * @brief Hardware timer management interface.
 *
 * This file provides the interface for hardware timer management functions, including setting hardware timeouts and
 * initializing hardware timers.
 */

void init_hw_timer(void);

#endif

/**
 * @}
 */