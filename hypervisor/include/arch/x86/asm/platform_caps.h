/*
 * Copyright (C) 2020-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PLATFORM_CAPS_H
#define PLATFORM_CAPS_H
/**
 * @addtogroup hwmgmt_hw-caps
 *
 * @{
 */

/**
 * @file
 * @brief Declarations for platform capability management in the hwmgmt.hw-caps module.
 *
 * This file declares the data structures and external global variables provided by the hwmgmt.hw-caps module for
 * platform capability management.
 *
 */

/**
 * @brief Data structure to store platform capabilities.
 *
 * This structure is used for obtaining and storing platform capabilities data.
 */
struct platform_caps_x86 {
	/** @brief Indicates if the posted interrupts are supported by all DRHDs. */
	bool pi;
};

extern struct platform_caps_x86 platform_caps;

struct cpu_state_info *get_cpu_pm_state_info(void);
void load_pcpu_state_data(void);
/**
 * @}
 */
#endif /* PLATFORM_CAPS_H */
