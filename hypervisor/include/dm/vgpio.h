/*
 * Copyright (C) 2020-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef VGPIO_H
#define VGPIO_H

#ifdef P2SB_VGPIO_DM_ENABLED

void register_vgpio_handler(struct acrn_vm *vm, const struct acrn_mmiores *res);

#endif

#endif /* VGPIO_H */
