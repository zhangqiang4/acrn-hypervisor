/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef VCR_H
#define VCR_H

/**
 * @addtogroup vp-base_vcr
 *
 * @{
*/

/**
 * @file
 * @brief This file declares external APIs to operate on guest CR0, CR2 and CR4 registers.
 */
uint64_t get_cr4_reserved_bits(struct acrn_vm *vm);
void init_cr0_cr4_host_guest_mask(struct acrn_vm *vm);
void fixup_cr0_cr4_guest_mask(struct acrn_vm *vm);
bool is_valid_cr0_cr4(struct acrn_vm *vm, uint64_t cr0, uint64_t cr4);
void init_cr0_cr4_flexible_bits(void );

uint64_t vcpu_get_cr0(struct acrn_vcpu *vcpu);

void vcpu_set_cr0(struct acrn_vcpu *vcpu, uint64_t val);

uint64_t vcpu_get_cr2(const struct acrn_vcpu *vcpu);

void vcpu_set_cr2(struct acrn_vcpu *vcpu, uint64_t val);

uint64_t vcpu_get_cr4(struct acrn_vcpu *vcpu);

void vcpu_set_cr4(struct acrn_vcpu *vcpu, uint64_t val);

int32_t cr_access_vmexit_handler(struct acrn_vcpu *vcpu);

/**
 * @}
 */
#endif /* VCR_H */
