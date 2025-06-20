/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * this file contains pure vmx operations
 */

#include <types.h>
#include <asm/msr.h>
#include <asm/per_cpu.h>
#include <asm/pgtable.h>
#include <asm/vmx.h>

/**
 * @addtogroup hwmgmt_vmx
 *
 * @{
 */

/**
 * @file
 * @brief Define basic VMX operations.
 *
 * This file implements the APIs to {enter,leave} VMX operations, {load, clear} VMCS memory region and
 * {read, write} VMCS fields etc.
 *
 */

/**
 * @brief Execute vmxon instruction to enter VMX operation
 *
 * Refer to 31.3 VMX INSTRUCTIONS, Vol. 3, SDM 325462-078US for details.
 *
 * @param[in] addr Pointer to physical address of VMXON data structure
 *
 * @return None
 *
 * @pre addr != NULL
 *
 * @post N/A
 */
static inline void exec_vmxon(void *addr)
{
	/* Turn VMX on, pre-conditions can avoid VMfailInvalid
	 * here no need check RFLAGS since it will generate #GP or #UD
	 * except VMsuccess. SDM 30.3
	 */
	asm volatile (
			"vmxon (%%rax)\n"
			:
			: "a"(addr)
			: "cc", "memory");

}

/**
 * @brief Prepare and enter processor VMX operation.
 *
 * The function is used to enter VMX operation before HV launches VMs or resumes VMs after host from S3.
 * It first initializes per-CPU data structure vmxon_region with data read from VMX_BASIC MSR, then enables
 * CR4.VMX bit and configures MSR_IA32_FEATURE_CONTROL MSR with below changes if MSR_IA32_FEATURE_CONTROL_LOCK
 * bit is not set:
 *  - Set MSR_IA32_FEATURE_CONTROL_LOCK bit and MSR_IA32_FEATURE_CONTROL_VMX_OUT_SMX bit.
 *  - Clear MSR_IA32_FEATURE_CONTROL_VMX_IN_SMX bit because HV doesn't support Intel(R) Trusted Execution Technology.
 *
 * Otherwise skip configuring the MSR. Finally it puts processor into VMX operation.
 *
 * Normal case behaviors:
 * - Put current logical processor in VMX operation
 *
 * Error case behaviors:
 * - N/A
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
void vmx_on(void)
{
	uint64_t tmp64;
	uint32_t tmp32;
	void *vmxon_region_va = (void *)get_cpu_var(vmxon_region);
	uint64_t vmxon_region_pa;

	/* Initialize vmxon page with revision id from IA32 VMX BASIC MSR */
	tmp32 = (uint32_t)msr_read(MSR_IA32_VMX_BASIC);
	(void)memcpy_s(vmxon_region_va, 4U, (void *)&tmp32, 4U);

	/* Turn on CR4.VMXE */
	CPU_CR_READ(cr4, &tmp64);
	CPU_CR_WRITE(cr4, tmp64 | CR4_VMXE);

	/* Read Feature control MSR */
	tmp64 = msr_read(MSR_IA32_FEATURE_CONTROL);

	/* Check if feature control is locked */
	if ((tmp64 & MSR_IA32_FEATURE_CONTROL_LOCK) == 0U) {
		/* Lock and enable VMX support */
		tmp64 |= (MSR_IA32_FEATURE_CONTROL_LOCK |
			  MSR_IA32_FEATURE_CONTROL_VMX_OUT_SMX);

		/* ACRN doesn't support Intel(R) Trusted Execution Technology. */
		tmp64 &= ~MSR_IA32_FEATURE_CONTROL_VMX_IN_SMX;

		msr_write(MSR_IA32_FEATURE_CONTROL, tmp64);
	}

	/* Turn ON VMX */
	vmxon_region_pa = hva2hpa(vmxon_region_va);
	exec_vmxon(&vmxon_region_pa);
}

/**
 * @brief Execute vmxoff instruction to leave VMX operation
 *
 * Refer to 31.3 VMX INSTRUCTIONS, Vol. 3, SDM 325462-078US for details.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline void exec_vmxoff(void)
{
	asm volatile ("vmxoff" : : : "memory");
}

/**
 * @brief Execute vmclear instruction to clear Virtual-Machine Control Structure
 *
 * Refer to Chapter 31.3 VMX INSTRUCTIONS, Vol. 3, SDM 325462-078US for details.
 *
 * @param[in] addr Pointer to VMCS physical address
 *
 * @return None
 *
 * @pre addr != NULL
 *
 * @post N/A
 */
static void exec_vmclear(void *addr)
{

	/* pre-conditions can avoid VMfail
	 * here no need check RFLAGS since it will generate #GP or #UD
	 * except VMsuccess. SDM 30.3
	 */
	asm volatile (
		"vmclear (%%rax)\n"
		:
		: "a"(addr)
		: "cc", "memory");
}

/**
 * @brief Execute vmptrld instruction to load pointer to Virtual-Machine Control Structure
 *
 * Refer to Chapter 31.3 VMX INSTRUCTIONS, Vol. 3, SDM 325462-078US for details.
 *
 * @param[in] addr Pointer to VMCS physical address
 *
 * @return None
 *
 * @pre addr != NULL
 *
 * @post N/A
 */
static void exec_vmptrld(void *addr)
{
	/* pre-conditions can avoid VMfail
	 * here no need check RFLAGS since it will generate #GP or #UD
	 * except VMsuccess. SDM 30.3
	 */
	asm volatile (
		"vmptrld (%%rax)\n"
		:
		: "a"(addr)
		: "cc", "memory");
}

/**
 * @brief Load target VMCS as current-VMCS
 *
 * This function is used to set a VMCS memory region as current-VMCS. HV needs to call this
 * function to configure a VMCS data structure as current-VMCS before run a vCPU. Scheduler
 * calls this function to load the next vCPU's VMCS as current-VMCS before schedule to next
 * vCPU thread. The function translates VMCS virtual address to physical address and supplies
 * it as vmptrld instruction operand.
 *
 * Normal case behaviors:
 * - Load target VMCS memory region as current-VMCS
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] vmcs_va Pointer to VMCS virtual address
 *
 * @return None
 *
 * @pre vmcs_va != NULL
 *
 * @post N/A
 */
void load_va_vmcs(const uint8_t *vmcs_va)
{
	uint64_t vmcs_pa;

	vmcs_pa = hva2hpa(vmcs_va);
	exec_vmptrld((void *)&vmcs_pa);
}

/**
 * @brief Clear VMCS data structure
 *
 * This function is used to initialize VMCS memory region before it is used for a vCPU, or it's used
 * when current physical CPU is going to leave VMX operation and needs to flush CPU cached data to the
 * VMCS memory region. It translates VMCS virtual address to physical address then supplies it as vmclear
 * instruction operand.
 *
 * Normal case behaviors:
 * - Initialize the VMCS data structure and flush CPU cached data to it.
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] vmcs_va Pointer to VMCS virtual address
 *
 * @return None
 *
 * @pre vmcs_val != NULL
 *
 * @post N/A
 */
void clear_va_vmcs(const uint8_t *vmcs_va)
{
	uint64_t vmcs_pa;

	vmcs_pa = hva2hpa(vmcs_va);
	exec_vmclear((void *)&vmcs_pa);
}

/**
 * @brief Prepare and move current logical processor out of VMX operation
 *
 * This function resets current-VMCS memory region and flushes CPU cached data to the VMCS, then executes vmxoff
 * to move current processor out of VMX operation. When current physical CPU is going to halt or host is going to
 * enter S3, HV calls this function to move the processor out of VMX operation.
 *
 * Normal case behaviors:
 * - Reset current-VMCS and move current processor out of VMX operation.
 *
 * Error case behaviors:
 * - N/A
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
void vmx_off(void)
{
	void **vmcs_ptr = &get_cpu_var(vmcs_run);

	if (*vmcs_ptr != NULL) {
		clear_va_vmcs(*vmcs_ptr);
		*vmcs_ptr = NULL;
	}

	exec_vmxoff();
}

/**
 * @brief Read a 64bit value from VMCS field
 *
 * This function reads a 64bit value from current-VMCS field.
 * Refer to 31.3 VMX INSTRUCTIONS, Vol. 3, SDM 325462-078US for details.
 *
 * Normal case behaviors:
 * - Read a 64bit value from VMCS
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] field_full Target VMCS field encoding
 *
 * @return A 64bit value from the VMCS field
 *
 * @pre N/A
 *
 * @post N/A
 */
uint64_t exec_vmread64(uint32_t field_full)
{
	uint64_t value;

	asm volatile (
		"vmread %%rdx, %%rax "
		: "=a" (value)
		: "d"(field_full)
		: "cc");

	return value;
}

/**
 * @brief Read a 32bit value from VMCS field
 *
 * This function is used to read a 32bit value from current-VMCS field. It relies on exec_vmread64 to read the value
 * and converts the returned data to a 32bit value.
 *
 * Normal case behaviors:
 * - Read a 32bit value from VMCS
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] field Target VMCS field encoding
 *
 * @return A 32bit value from the VMCS field
 *
 * @pre N/A
 *
 * @post N/A
 */
uint32_t exec_vmread32(uint32_t field)
{
	uint64_t value;

	value = exec_vmread64(field);

	return (uint32_t)value;
}

/**
 * @brief Read a 16bit value from VMCS field
 *
 * This function is used to read a 16bit value from current-VMCS field. It relies on exec_vmread64 to read the value
 * and converts the returned data to a 16bit value.
 *
 * Normal case behaviors:
 * - Read a 16bit value from VMCS
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] field Target VMCS field encoding
 *
 * @return A 16bit value from the VMCS field
 *
 * @pre N/A
 *
 * @post N/A
 */
uint16_t exec_vmread16(uint32_t field)
{
        uint64_t value;

        value = exec_vmread64(field);

        return (uint16_t)value;
}

/**
 * @brief Write a 64bit value to VMCS field
 *
 * This function writes a 64bit value to current-VMCS field.
 * Refer to 31.3 VMX INSTRUCTIONS, Vol. 3, SDM 325462-078US for details.
 *
 * Normal case behaviors:
 * - Write a 64bit value to VMCS
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] field_full Target VMCS field encoding
 * @param[in] value The data to write
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
void exec_vmwrite64(uint32_t field_full, uint64_t value)
{
	asm volatile (
		"vmwrite %%rax, %%rdx "
		: : "a" (value), "d"(field_full)
		: "cc");
}

/**
 * @brief Write a 32bit value to VMCS field
 *
 * This function is used to write a 32bit value to current-VMCS field. It converts the 32bit value to 64bit
 * and calls exec_vmwrite64 to write the data.
 *
 * Normal case behaviors:
 * - Write a 32bit value to VMCS
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] field VMCS field encoding.
 * @param[in] value The data to write.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
void exec_vmwrite32(uint32_t field, uint32_t value)
{
	exec_vmwrite64(field, (uint64_t)value);
}

/**
 * @brief Write a 16bit value to VMCS field
 *
 * This function is used to write a 16bit value to current-VMCS field. It converts the 16bit value to 64bit
 * and calls exec_vmwrite64 to write the data.
 *
 * Normal case behaviors:
 * - Write a 16bit value to VMCS
 *
 * Error case behaviors:
 * - N/A
 *
 * @param[in] field Target VMCS field encoding
 * @param[in] value The data to write.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
void exec_vmwrite16(uint32_t field, uint16_t value)
{
	exec_vmwrite64(field, (uint64_t)value);
}

/**
 * @}
 */
