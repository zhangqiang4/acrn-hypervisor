/*
 * Copyright (C) 2020-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <asm/platform_caps.h>

/**
 * @addtogroup hwmgmt_hw-caps
 *
 * @{
 */

/**
 * @file
 * @brief Implementation of platform capability management in the hwmgmt.hw-caps module.
 *
 * This file contains the data structures and functions for the hwmgmt.hw-caps module to manage platform capabilities
 * and provide interfaces to other modules.
 *
 */

/**
 * @brief Global variable storing the platform capabilities.
 *
 * This global variable 'platform_caps' of type 'struct platform_caps_x86' is used to store the platform capabilities.
 * By default, the ".pi" field is set to true to indicate that the posted interrupts are supported by all DRHDs.
 *
 */
struct platform_caps_x86 platform_caps = {.pi = true};

/**
 * @}
 */
