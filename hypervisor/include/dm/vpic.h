/*
 * Copyright (C) 2024-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef VPIC_H
#define VPIC_H

/**
 * @addtogroup vp-dm_vperipheral
 *
 * @{
 */

/**
 * @file
 * @brief Definitions for virtual 8259 Programmable Interrupt Controller (PIC)
 *
 * This file defines macros, data structure and functions for virtual 8259 Programmable Interrupt Controller (PIC).
 */

struct acrn_vm;
void vpic_hide(struct acrn_vm *vm);

/**
 * @}
 */

#endif /* VPIC_H */
