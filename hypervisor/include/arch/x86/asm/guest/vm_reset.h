/*
 * Copyright (C) 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef VM_RESET_H_
#define VM_RESET_H_

#include <acrn_common.h>

/**
 * @addtogroup vp-base_pm
 *
 * @{
 */

/**
 * @file
 *
 * @brief VM reset header file.
 * This file declares external functions in vm_reset.c that provides VM reset functions.
 */

void register_reset_port_handler(struct acrn_vm *vm);
void shutdown_vm_from_idle(uint16_t pcpu_id);
void triple_fault_shutdown_vm(struct acrn_vcpu *vcpu);

/**
 * @}
 */

#endif /* VM_RESET_H_ */
