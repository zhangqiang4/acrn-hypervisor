/*
 * Copyright (C) 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef GUEST_H
#define GUEST_H

#ifndef ASSEMBLER

#include <types.h>

/**
 * @addtogroup vp-base_guest-mem
 * @brief Guest memory implementation
 *
 * @{
 */

/**
 * @file
 * @brief Declaration of guest memory access functions
 *
 * This file contains the declaration of functions for translating guest virtual addresses to guest physical addresses,
 * and provides utilities for accessing guest memory from the hypervisor.
 */

struct acrn_vcpu;
struct acrn_vm;

/**
 * @enum vm_paging_mode
 * @brief Enumeration of VM paging modes.
 */
enum vm_paging_mode {
	PAGING_MODE_0_LEVEL = 0U,	/**< Non-paging mode. */
	PAGING_MODE_2_LEVEL = 2U,	/**< 32bit paging, 2-level */
	PAGING_MODE_3_LEVEL = 3U,	/**< PAE paging, 3-level */
	PAGING_MODE_4_LEVEL = 4U,	/**< 64bit paging, 4-level */
	PAGING_MODE_NUM,		/**< Number of paging modes supported */
};

int32_t gva2gpa(struct acrn_vcpu *vcpu, uint64_t gva, uint64_t *gpa, uint32_t *err_code);

/* gpa --> hpa -->hva */
void *gpa2hva(struct acrn_vm *vm, uint64_t x);

int32_t copy_from_gpa(struct acrn_vm *vm, void *h_ptr, uint64_t gpa, uint32_t size);
int32_t copy_to_gpa(struct acrn_vm *vm, void *h_ptr, uint64_t gpa, uint32_t size);
int32_t copy_from_gva(struct acrn_vcpu *vcpu, void *h_ptr, uint64_t gva,
	uint32_t size, uint32_t *err_code, uint64_t *fault_addr);
int32_t copy_to_gva(struct acrn_vcpu *vcpu, void *h_ptr, uint64_t gva,
	uint32_t size, uint32_t *err_code, uint64_t *fault_addr);

bool handle_gva_translation_error(int status, struct acrn_vcpu *vcpu, uint64_t addr, uint32_t err_code);
/**
 * @}
 */
#endif	/* !ASSEMBLER */

#endif /* GUEST_H*/
