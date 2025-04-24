/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef HV_MAIN_H
#define HV_MAIN_H

#include <schedule.h>

/**
 * @addtogroup vp-base_hv-main
 *
 * @{
 */

/**
 * @file
 * @brief hv_main thread relevant operations header
 *
 * This file implements the declaration of vcpu_thread() and run_idle_thread() functions.
 */

void vcpu_thread(struct thread_object *obj);
void run_idle_thread(void);

#endif /* HV_MAIN_H */

/**
 * @}
 */