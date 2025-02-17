/*
 * Copyright (C) 2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef HW_THERMAL_H
#define HW_THERMAL_H

#include <types.h>
/**
 * @addtogroup hwmgmt_thermal
 *
 * @{
 */
/**
 * @file
 * @brief Thermal management for x86 architecture in the ACRN hypervisor.
 *
 * This file declares the interface APIs for thermal management in the ACRN hypervisor on the x86 architecture.
 */

void init_hw_thermal(void);

/**
 * @}
 */
#endif /* HW_THERMAL_H */
