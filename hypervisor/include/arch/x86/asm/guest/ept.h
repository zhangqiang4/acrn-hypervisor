/*
 * Copyright (C) 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef EPT_H
#define EPT_H
#include <types.h>

/**
 * @addtogroup vp-base_guest-mem
 * @brief Guest memory implementation
 *
 * @{
 */

/**
 * @file
 * @brief Declaration of the Extended Page Table (EPT) management functions
 *
 * This file contains the declaration of functions for initializing, modifying, and destroying EPTs, as well as
 * functions for translating addresses between guest and host contexts. Additionally, it provides mechanisms for
 * handling EPT-related VM exits and managing EPT page tables.
 */

typedef void (*pge_handler)(uint64_t *pgentry, uint64_t size);	/**< Callback function type in walk_ept_table */

/**
 * Invalid HPA is defined for error checking,
 * according to SDM vol.3A 4.1.4, the maximum  host physical address width is 52
 */
#define INVALID_HPA	(0x1UL << 52U)	/**< Invalid Host Physical Address */
#define INVALID_GPA	(0x1UL << 52U)	/**< Invalid Guest Physical Address */

struct acrn_vm;

bool ept_is_valid_mr(struct acrn_vm *vm, uint64_t mr_base_gpa, uint64_t size);
void destroy_ept(struct acrn_vm *vm);
uint64_t local_gpa2hpa(struct acrn_vm *vm, uint64_t gpa, uint32_t *size);
uint64_t gpa2hpa(struct acrn_vm *vm, uint64_t gpa);
uint64_t service_vm_hpa2gpa(uint64_t hpa);

void ept_add_mr(struct acrn_vm *vm, uint64_t *pml4_page, uint64_t hpa,
		uint64_t gpa, uint64_t size, uint64_t prot_orig);
void ept_modify_mr(struct acrn_vm *vm, uint64_t *pml4_page, uint64_t gpa,
		uint64_t size, uint64_t prot_set, uint64_t prot_clr);
void ept_del_mr(struct acrn_vm *vm, uint64_t *pml4_page, uint64_t gpa,
		uint64_t size);

void ept_flush_leaf_page(uint64_t *pge, uint64_t size);
void walk_ept_table(struct acrn_vm *vm, pge_handler cb);

int32_t ept_misconfig_vmexit_handler(__unused struct acrn_vcpu *vcpu);

void init_ept_pgtable(struct pgtable *table, uint16_t vm_id);

/**
 * @}
 */

#endif /* EPT_H */
