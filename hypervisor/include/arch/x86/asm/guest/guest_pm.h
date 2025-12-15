/*
 * Copyright (C) 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef GUEST_PM_H
#define GUEST_PM_H

/**
 * @defgroup vp-base_pm vp-base.pm
 * @ingroup vp-base
 * @brief Guest power management.
 *
 * This module is implement of guest power management functions, by which the hypervisor can
 * manage VM's ACPI Sx states, reboot and power/performance states.
 *
 * @{
 */

/**
 * @file
 *
 * @brief Guest power management header file.
 * This file declares external functions in guest/pm.c that provides VM power management.
 */
void init_guest_pm(struct acrn_vm *vm);
int s3_resume_vm(struct acrn_vm *vm);
void platform_s3_from_idle(void);
/**
 * @}
 */

#endif /* PM_H */
