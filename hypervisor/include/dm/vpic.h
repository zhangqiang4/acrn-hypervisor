/*
 * Copyright (C) 2024-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef VPIC_H
#define VPIC_H

/**
 * @file vpic.h
 *
 * @brief public APIs for virtual PIC
 */

struct acrn_vm;
void vpic_hide(struct acrn_vm *vm);

/**
 * @}
 */
/* End of acrn_vpic */

#endif /* VPIC_H */
