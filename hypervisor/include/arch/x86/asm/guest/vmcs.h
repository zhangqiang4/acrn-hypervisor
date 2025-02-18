/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef VMCS_H_
#define VMCS_H_

/**
 * @defgroup vp-base_hv-main vp-base.hv-main
 * @ingroup vp-base
 * @brief Hypervisor main
 *
 * This module is responsible for the core operations of the ACRN hypervisor, focusing on the management and execution
 * of virtual CPUs (vCPUs) and handling VM exits. It includes functionalities for initializing and managing the Virtual
 * Machine Control Structure (VMCS), handling VM exits, and executing the main hypervisor loop.
 *
 * @{
 */

/**
 * @file
 * @brief VMCS header file.
 *
 * This file contains the declarations and inline functions for managing the VMCS in the ACRN hypervisor.
 * It provides macros for VMX instruction success and failure, and functions for manipulating VMCS fields
 * and handling APIC access qualifications.
 */

/**
 * @brief A pre-defined value to indicate the success of VMX instruction executions.
 */
#define VM_SUCCESS	0
/**
 * @brief A pre-defined value to indicate the failure of VMX instruction executions.
 */
#define VM_FAIL		(-1)

#ifndef ASSEMBLER
#include <types.h>
#include <asm/guest/vcpu.h>

/**
 * @brief The VM exit is caused by VM-entry failure.
 */
#define VMX_VMENTRY_FAIL	0x80000000U

#define APIC_ACCESS_OFFSET	0xFFFUL   /**< Bit[11:0], offset within the APIC page */
#define APIC_ACCESS_TYPE	0xF000UL  /**< Bit[15:12], APIC access type */
#define TYPE_LINEAR_APIC_INST_READ	(0UL << 12U)  /**< Linear APIC instruction read operation */
#define TYPE_LINEAR_APIC_INST_WRITE	(1UL << 12U)  /**< Linear APIC instruction write operation */

/* VM exit qualifications for APIC-access
 * Access type:
 *  0  = linear access for a data read during instruction execution
 *  1  = linear access for a data write during instruction execution
 *  2  = linear access for an instruction fetch
 *  3  = linear access (read or write) during event delivery
 *  10 = guest-physical access during event delivery
 *  15 = guest-physical access for an instruction fetch or during
 *       instruction execution
 */

/**
 * @brief Get APIC access type.
 *
 * This function extracts the APIC access type from the VM exit qualification for APIC access (bit[15:12]).
 * Refer to Table 28-6, Exit Qualification for APIC-Access VM Exits from Linear Accesses and Guest-Physical Accesses,
 * Chapter 28.8, Vol. 3, SDM 325462-078 for more details.
 *
 * @param[in] qual VM exit qualification
 *
 * @return APIC access type
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint64_t apic_access_type(uint64_t qual)
{
	return (qual & APIC_ACCESS_TYPE);
}

/**
 * @brief Get APIC access offset.
 *
 * This function extracts the APIC access offset from the VM exit qualification for APIC access (bit[11:0]).
 * Refer to Table 28-6, Exit Qualification for APIC-Access VM Exits from Linear Accesses and Guest-Physical Accesses,
 * Chapter 28.8, Vol. 3, SDM 325462-078 for more details.
 *
 * @param[in] qual VM exit qualification
 *
 * @return APIC access offset
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint64_t apic_access_offset(uint64_t qual)
{
	return (qual & APIC_ACCESS_OFFSET);
}

/**
 * @brief Clear one VMCS bit.
 *
 * This function clears the specified bit in the VMCS field. It reads the current value of the VMCS field,
 * clears the specified bit, and writes the updated value back to the VMCS field.
 *
 * @param[in] vmcs_field VMCS field
 * @param[in] bit Bit to be cleared
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline void clear_vmcs_bit(uint32_t vmcs_field, uint64_t bit)
{
	uint64_t val64;

	val64 = exec_vmread(vmcs_field);
	val64 &= ~bit;
	exec_vmwrite(vmcs_field, val64);
}

/**
 * @brief Set one VMCS bit.
 *
 * This function sets the specified bit in the VMCS field. It reads the current value of the VMCS field,
 * sets the specified bit, and writes the updated value back to the VMCS field.
 *
 * @param[in] vmcs_field VMCS field
 * @param[in] bit Bit to be set
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline void set_vmcs_bit(uint32_t vmcs_field, uint64_t bit)
{
	uint64_t val64;

	val64 = exec_vmread(vmcs_field);
	val64 |= bit;
	exec_vmwrite(vmcs_field, val64);
}

void init_vmcs(struct acrn_vcpu *vcpu);
void load_vmcs(const struct acrn_vcpu *vcpu);

#endif /* ASSEMBLER */

#endif /* VMCS_H_ */

/**
 * @}
 */