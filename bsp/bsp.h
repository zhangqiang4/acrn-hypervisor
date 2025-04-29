/*
 * Copyright (C) 2018 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef BSP_H
#define BSP_H

/**
 * BSP initialization routine
 *
 * Shall be called once on each physical core on hypervisor initialization.
 */
void bsp_init(void);

/**
 * Fatal error handler
 */
void bsp_fatal_error(void);

#endif /* BSP_H */
