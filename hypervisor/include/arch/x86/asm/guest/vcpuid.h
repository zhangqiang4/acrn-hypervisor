/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef VCPUID_H_
#define VCPUID_H_

/**
 * @addtogroup vp-base_vcpuid
 *
 * @{
 */

/**
 * @file
 * @brief Define virtual CPUID(vCPUID) related data structure and external API prototypes
 *
 * This file includes data structure to form vCPUID entries and function prototypes
 * for external modules.
 */

#define CPUID_CHECK_SUBLEAF		(1U << 0U) /**< Check vCPUID subleaf for the entry */
#define MAX_VM_VCPUID_ENTRIES   	64U /**< Max. guest vCPUID entries */
#define GUEST_CAPS_PRIVILEGE_VM 	(1U << 0U) /**< Guest is a privileged VM */

/**
 * @brief Data structure to hold vCPUID entry data
 *
 * This data structure is used to set, get or store vCPUID entry. It contains vCPUID
 * leaf data in eax/ebx/ecx/edx and its corresponding leaf/subleaf. Also flags to
 * facilitate vCPUID enumeration.
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
struct vcpuid_entry {
	uint32_t eax; /**< vCPUID data in EAX register */
	uint32_t ebx; /**< vCPUID data in EBX register */
	uint32_t ecx; /**< vCPUID data in ECX register */
	uint32_t edx; /**< vCPUID data in EDX register */
	uint32_t leaf; /**< vCPUID leaf */
	uint32_t subleaf; /**< vCPUID sub-leaf */
	uint32_t flags; /**< Check vCPUID sub-leaf if set 1 */
	uint32_t padding; /**< Field to pad structure size */
};

int32_t set_vcpuid_entries(struct acrn_vm *vm);
void guest_cpuid(struct acrn_vcpu *vcpu,
			uint32_t *eax, uint32_t *ebx,
			uint32_t *ecx, uint32_t *edx);

/**
 * @}
 */
#endif /* VCPUID_H_ */
