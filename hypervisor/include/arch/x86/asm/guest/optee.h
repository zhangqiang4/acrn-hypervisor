/*
 * Copyright (C) 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef TEE_H_
#define TEE_H_

#include <asm/guest/vm.h>

/**
 * @addtogroup vp-base_optee
 *
 * @{
 */

/**
 * @file
 * @brief ACRN TEE VM management interface
 *
 * This file defines the API for Trusted Execution Environment (TEE) virtual machine
 * operations, including TEE VM identification, state management (freeze/thaw),
 * and core lifecycle functions for the ACRN hypervisor's TEE implementation.
 */

void set_tee_vm(struct acrn_vm *vm);
struct acrn_vm *get_tee_vm(void);
bool is_tee_vm(struct acrn_vm *vm);
void enter_refrigerator(struct acrn_vcpu *vcpu);
void freeze_vm(struct acrn_vm *vm);
void thaw_vm(struct acrn_vm *vm);

/**
 * @}
 */

#endif /* TEE_H_ */
