/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <errno.h>
#include <asm/lib/spinlock.h>
#include <asm/cpu.h>
#include <asm/msr.h>
#include <asm/cpuid.h>
#include <asm/guest/ucode.h>
#include <asm/guest/guest_memory.h>
#include <asm/guest/virq.h>
#include <logmsg.h>

/**
 * @addtogroup vp-base_vmsr
 *
 * @{
 */

/**
 * @file
 * @brief Implement functions for guest ucode update
 *
 * Service VM can trigger ucode update by writing MSR_IA32_BIOS_UPDT_TRIG on behalf of guest.
 * HV shall check guest ucode data size is within MICRO_CODE_SIZE_MAX and update ucode signature
 * in MSR_IA32_BIOS_SIGN_ID by executing CPUID instruction.
 */

#define MICRO_CODE_SIZE_MAX    0x40000U /**< Max ucode length */

/**
 * @brief Array to hold ucode data
 */
static uint8_t micro_code[MICRO_CODE_SIZE_MAX];

/**
 * @brief Get ucode signature
 *
 * IA32_BIOS_SIGN_ID MSR is loaded with the update signature whenever CPUID executes. The signature
 * is returned in the upper DWORD. Refer to 10.11.1 Microcode Update, SDM Vol.3 325462-078US for
 * details.
 *
 * Normal case behaviors:
 * - Return ucode signature
 *
 * Error case behaviors:
 * - N/A
 *
 * @return A uint64_t value containing ucode signature
 *
 * @pre N/A
 *
 * @post N/A
 */
uint64_t get_microcode_version(void)
{
	uint64_t val;
	uint32_t eax, ebx, ecx, edx;

	msr_write(MSR_IA32_BIOS_SIGN_ID, 0U);
	cpuid_subleaf(CPUID_FEATURES, 0x0U, &eax, &ebx, &ecx, &edx);
	val = msr_read(MSR_IA32_BIOS_SIGN_ID);

	return val;
}

/**
 * @brief Get ucode data size
 *
 * This function returns ucode size from ucode header. If ucode size field is 0, return
 * default size 2000U. Refer to 10.11.1 Microcode Update, SDM Vol.3 325462-078US for
 * details.
 *
 * @param[in] uhdr Pointer to ucode header
 *
 * @return ucode data size
 *
 * @pre uhdr != NULL
 *
 * @post N/A
 */
static inline size_t get_ucode_data_size(const struct ucode_header *uhdr)
{
	return ((uhdr->data_size != 0U) ? uhdr->data_size : 2000U);
}

/**
 * @brief Check guest data and trigger microcode update
 *
 * This function follows below steps to conduct ucode update on behalf of guest:
 * -# Copy ucode header from guest virtual address with copy_from_gva(). If the function returns
 * -EFAULT, then inject \#PF to guest. Ignore -EINVAL error code from copy_from_gva().
 *  Refer to copy_from_gva() function description for error code details.
 * -# Check ucode data size in ucode header. If the size exceeds MICRO_CODE_SIZE_MAX, HV shall print
 * error message and skip update.
 * -# Copy ucode data to ucode buffer. It copies ucode data with copy_from_gva() and handles errors as above.
 * -# Write ucode data buffer address to MSR_IA32_BIOS_UPDT_TRIG to trigger ucode update.
 * -# Update ucode version information in MSR_IA32_BIOS_SIGN_ID by calling get_microcode_version().
 *
 * Guest operating system should guarantee it won't issue 2nd ucode update request when the
 * 1st ucode update is on-going.
 *
 * @param[in] vcpu Target vCPU to update ucode
 * @param[in] v Guest ucode data address written to MSR_IA32_BIOS_UPDT_TRIG
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
void acrn_update_ucode(struct acrn_vcpu *vcpu, uint64_t v)
{
	uint64_t gva, fault_addr = 0UL;
	struct ucode_header uhdr;
	size_t data_size;
	int32_t err;
	uint32_t err_code;

	gva = v - sizeof(struct ucode_header);

	err_code = 0U;
	err = copy_from_gva(vcpu, &uhdr, gva, sizeof(uhdr), &err_code,
			&fault_addr);
	if (err < 0) {
		(void)handle_gva_translation_error(err, vcpu, fault_addr, err_code);
	} else {
		data_size = get_ucode_data_size(&uhdr) + sizeof(struct ucode_header);
		if (data_size > MICRO_CODE_SIZE_MAX) {
			pr_err("The size of microcode is greater than 0x%x",
					MICRO_CODE_SIZE_MAX);
		} else {
			err_code = 0U;
			err = copy_from_gva(vcpu, micro_code, gva, data_size, &err_code,
					&fault_addr);
			if (err < 0) {
				(void)handle_gva_translation_error(err, vcpu, fault_addr, err_code);
			} else {
				msr_write(MSR_IA32_BIOS_UPDT_TRIG,
					(uint64_t)micro_code + sizeof(struct ucode_header));
				(void)get_microcode_version();
			}
		}
	}
}

/**
 * @}
 */
