/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <errno.h>
#include <asm/pgtable.h>
#include <asm/msr.h>
#include <asm/cpuid.h>
#include <asm/guest/vcpu.h>
#include <asm/guest/virq.h>
#include <asm/guest/vm.h>
#include <asm/guest/vmexit.h>
#include <asm/vmx.h>
#include <asm/sgx.h>
#include <asm/guest/guest_pm.h>
#include <asm/guest/ucode.h>
#include <asm/cpufeatures.h>
#include <asm/rdt.h>
#include <asm/tsc.h>
#include <trace.h>
#include <logmsg.h>

/**
 * @defgroup vp-base_vmsr vp-base.vmsr
 *
 * @ingroup vp-base
 *
 * @brief Implement MSR related interfaces and data structures
 *
 * This module provides interface to configure VMCS MSR-Bitmap fields to control guest MSRs read/write operations.
 * For those MSRs which need ACRN Hypervisor emulation, this module also provides interfaces to emulate these MSRs
 * read/write operations.
 *
 * @{
 */

/**
 * @file
 * @brief Implement guest MSR related functions
 *
 * This file implements MSR related operations such as enable/disable MSR interception, emulate MSR read/write
 * and handle guest MSR read/write induced VM-Exit etc.
 */

#define INTERCEPT_DISABLE		(0U) /**< Disable MSR intercept */
#define INTERCEPT_READ			(1U << 0U) /**< Intercept MSR read */
#define INTERCEPT_WRITE 		(1U << 1U) /**< Intercept MSR write */
#define INTERCEPT_READ_WRITE		(INTERCEPT_READ | INTERCEPT_WRITE) /**< Intercept MSR read and write */

static void set_tsc_msr_interception(struct acrn_vcpu *vcpu, bool interception);

/**
 * @brief Guest MSRs emulated by HV
 *
 * These MSRs are intercepted and emulated by HV. Writing or reading the MSRs from guest shall cause
 * VM-Exit and HV shall emulate the behavior of corresponding operation.
 */
static uint32_t emulated_guest_msrs[NUM_EMULATED_MSRS] = {
	/*
	 * This may include MSR_IA32_STAR, MSR_IA32_LSTAR, MSR_IA32_FMASK,
	 * MSR_IA32_KERNEL_GS_BASE, MSR_IA32_SYSENTER_ESP, MSR_IA32_SYSENTER_CS, MSR_IA32_SYSENTER_EIP
	 */
	MSR_IA32_PAT,
	MSR_IA32_EFER,
	MSR_IA32_TSC_ADJUST,

	/*
	 * MSRs don't need isolation between worlds
	 * Number of entries: NUM_COMMON_MSRS
	 */
	MSR_IA32_UMWAIT_CONTROL,
	MSR_IA32_TSC_DEADLINE,
	MSR_IA32_BIOS_UPDT_TRIG,
	MSR_IA32_BIOS_SIGN_ID,
	MSR_IA32_TIME_STAMP_COUNTER,
	MSR_IA32_APIC_BASE,
	MSR_IA32_PERF_STATUS,
	MSR_IA32_PERF_CTL,
	MSR_IA32_FEATURE_CONTROL,

	MSR_IA32_MCG_CAP,
	MSR_IA32_MCG_STATUS,
	MSR_IA32_MISC_ENABLE,

	/* Don't support SGX Launch Control yet, read only */
	MSR_IA32_SGXLEPUBKEYHASH0,
	MSR_IA32_SGXLEPUBKEYHASH1,
	MSR_IA32_SGXLEPUBKEYHASH2,
	MSR_IA32_SGXLEPUBKEYHASH3,
	/* Read only */
	MSR_IA32_SGX_SVN_STATUS,

	MSR_IA32_XSS,

	/* KeyLocker backup MSRs */
	MSR_IA32_COPY_LOCAL_TO_PLATFORM,
	MSR_IA32_COPY_PLATFORM_TO_LOCAL,
	MSR_IA32_COPY_STATUS,
	MSR_IA32_IWKEY_BACKUP_STATUS,

	MSR_TEST_CTL,

	MSR_PLATFORM_INFO,

	MSR_IA32_PM_ENABLE,
	MSR_IA32_HWP_CAPABILITIES,
	MSR_IA32_HWP_REQUEST,
	MSR_IA32_HWP_STATUS,

	/* If CPUID.(EAX=07H, ECX=0):EDX[29]=1 */
	MSR_IA32_XAPIC_DIS_STATUS,
	MSR_IA32_ARCH_CAPABILITIES,
};

/**
 * @brief MTRR related MSRs exclusive of Fixed Range and System-Management Range MTRRs
 *
 * This array is used when HV initializes emulated MSRs to the default interception mode.
 * For MSRs in this array, they're initialized to read and write intercepted.
 */
static const uint32_t mtrr_msrs[] = {
	MSR_IA32_MTRR_CAP,
	MSR_IA32_MTRR_DEF_TYPE,

	/* Variable MTRRs are supported */
	MSR_IA32_MTRR_PHYSBASE_0,
	MSR_IA32_MTRR_PHYSMASK_0,
	MSR_IA32_MTRR_PHYSBASE_1,
	MSR_IA32_MTRR_PHYSMASK_1,
	MSR_IA32_MTRR_PHYSBASE_2,
	MSR_IA32_MTRR_PHYSMASK_2,
	MSR_IA32_MTRR_PHYSBASE_3,
	MSR_IA32_MTRR_PHYSMASK_3,
	MSR_IA32_MTRR_PHYSBASE_4,
	MSR_IA32_MTRR_PHYSMASK_4,
	MSR_IA32_MTRR_PHYSBASE_5,
	MSR_IA32_MTRR_PHYSMASK_5,
	MSR_IA32_MTRR_PHYSBASE_6,
	MSR_IA32_MTRR_PHYSMASK_6,
	MSR_IA32_MTRR_PHYSBASE_7,
	MSR_IA32_MTRR_PHYSMASK_7,
	MSR_IA32_MTRR_PHYSBASE_8,
	MSR_IA32_MTRR_PHYSMASK_8,
	MSR_IA32_MTRR_PHYSBASE_9,
	MSR_IA32_MTRR_PHYSMASK_9,
};

/**
 * @brief Performance monitoring related MSRs
 *
 * These MSRs include:
 * - Fixed counter MSRs: MSR_IA32_PMC{0,1,2,3,4,5,6,7} and their aliases MSR_IA32_A_PMC{0,1,2,3,4,5,6,7}.
 * - General purpose counter MSRs: MSR_IA32_PERFEVTSEL(0,1,2,3}.
 * - Performance monitoring status/control MSRs: MSR_IA32_PERF_GLOBAL_{STATUS, CTRL, OVF_CTRL, STATUS_SET, INUSE),
 *   MSR_IA32_FIXED_{CTR_CTL, CTR0, CTR1, CTR2}, MSR_IA32_PERF_CAPABILITIES, MSR_IA32_DS_AREA and MSR_IA32_PEBS_ENABLE.
 */
static const uint32_t pmc_msrs[] = {
	MSR_IA32_PMC0,
	MSR_IA32_PMC1,
	MSR_IA32_PMC2,
	MSR_IA32_PMC3,
	MSR_IA32_PMC4,
	MSR_IA32_PMC5,
	MSR_IA32_PMC6,
	MSR_IA32_PMC7,
	MSR_IA32_PERFEVTSEL0,
	MSR_IA32_PERFEVTSEL1,
	MSR_IA32_PERFEVTSEL2,
	MSR_IA32_PERFEVTSEL3,
	MSR_IA32_PERFEVTSEL4,
	MSR_IA32_PERFEVTSEL5,
	MSR_IA32_PERFEVTSEL6,
	MSR_IA32_PERFEVTSEL7,
	MSR_IA32_A_PMC0,
	MSR_IA32_A_PMC1,
	MSR_IA32_A_PMC2,
	MSR_IA32_A_PMC3,
	MSR_IA32_A_PMC4,
	MSR_IA32_A_PMC5,
	MSR_IA32_A_PMC6,
	MSR_IA32_A_PMC7,
	/* CPUID.0AH.EAX[7:0] */
	MSR_IA32_FIXED_CTR_CTL,
	MSR_IA32_PERF_GLOBAL_STATUS,
	MSR_IA32_PERF_GLOBAL_CTRL,
	MSR_IA32_PERF_GLOBAL_OVF_CTRL,
	MSR_IA32_PERF_GLOBAL_STATUS_SET,
	MSR_IA32_PERF_GLOBAL_INUSE,
	/* CPUID.0AH.EDX[4:0] */
	MSR_IA32_FIXED_CTR0,
	MSR_IA32_FIXED_CTR1,
	MSR_IA32_FIXED_CTR2,

	/* Performance Monitoring: CPUID.01H.ECX[15] X86_FEATURE_PDCM */
	MSR_IA32_PERF_CAPABILITIES,

	/* Debug Store disabled: CPUID.01H.EDX[21] X86_FEATURE_DTES */
	MSR_IA32_DS_AREA,

	/* PEBS facility */
	MSR_IA32_PEBS_ENABLE
};

/**
 * @brief MSRs not supported
 *
 * These MSRs are not supported in current HV design, leave them for future.
 * They're intercepted but not emulated by HV, instead HV shall inject \#GP
 * for guest access.
 */
static const uint32_t unsupported_msrs[] = {

	/* Fixed MTRRs are not supported */
	MSR_IA32_MTRR_FIX64K_00000,
	MSR_IA32_MTRR_FIX16K_80000,
	MSR_IA32_MTRR_FIX16K_A0000,
	MSR_IA32_MTRR_FIX4K_C0000,
	MSR_IA32_MTRR_FIX4K_C8000,
	MSR_IA32_MTRR_FIX4K_D0000,
	MSR_IA32_MTRR_FIX4K_D8000,
	MSR_IA32_MTRR_FIX4K_E0000,
	MSR_IA32_MTRR_FIX4K_E8000,
	MSR_IA32_MTRR_FIX4K_F0000,
	MSR_IA32_MTRR_FIX4K_F8000,
	MSR_IA32_SMRR_PHYSBASE,
	MSR_IA32_SMRR_PHYSMASK,

	/* MPX disabled: CPUID.07H.EBX[14] */
	MSR_IA32_BNDCFGS,

	/* SGX disabled : CPUID.12H.EAX[0] */
	MSR_SGXOWNEREPOCH0,
	MSR_SGXOWNEREPOCH1,

	/* QOS Configuration disabled: CPUID.10H.ECX[2] */
	MSR_IA32_L3_QOS_CFG,
	MSR_IA32_L2_QOS_CFG,

	/* RDT-M disabled: CPUID.07H.EBX[12], CPUID.07H.EBX[15] */
	MSR_IA32_QM_EVTSEL,
	MSR_IA32_QM_CTR,
	MSR_IA32_PQR_ASSOC,

	/* RDT-A disabled: CPUID.07H.EBX[12], CPUID.10H */
	/* MSR 0xC90 ... 0xD8F, not in this array */

	/* RTIT disabled: CPUID.07H.EBX[25], CPUID.14H.ECX[0,2] (X86_FEATURE_INTEL_PT) */
	MSR_IA32_RTIT_OUTPUT_BASE,
	MSR_IA32_RTIT_OUTPUT_MASK_PTRS,
	MSR_IA32_RTIT_CTL,
	MSR_IA32_RTIT_STATUS,
	MSR_IA32_RTIT_CR3_MATCH,
	/* Region Address: CPUID.07H.EAX[2:0] (subleaf 1) */
	MSR_IA32_RTIT_ADDR0_A,
	MSR_IA32_RTIT_ADDR0_B,
	MSR_IA32_RTIT_ADDR1_A,
	MSR_IA32_RTIT_ADDR1_B,
	MSR_IA32_RTIT_ADDR2_A,
	MSR_IA32_RTIT_ADDR2_B,
	MSR_IA32_RTIT_ADDR3_A,
	MSR_IA32_RTIT_ADDR3_B,

	/* SMM Monitor Configuration: CPUID.01H.ECX[5] and CPUID.01H.ECX[6] */
	MSR_IA32_SMM_MONITOR_CTL,

	/* Silicon Debug Feature: CPUID.01H.ECX[11] (X86_FEATURE_SDBG) */
	MSR_IA32_DEBUG_INTERFACE,

	/* Machine Check Exception: CPUID.01H.EDX[5] (X86_FEATURE_MCE) */
	MSR_IA32_MCG_CAP,
	MSR_IA32_MCG_STATUS,
	MSR_IA32_MCG_CTL,
	MSR_IA32_MCG_EXT_CTL,
	/* MSR 0x280 ... 0x29F, not in this array */
	/* MSR 0x400 ... 0x473, not in this array */

	/* PRMRR related MSRs are configured by native BIOS / bootloader */
	MSR_PRMRR_PHYS_BASE,
	MSR_PRMRR_PHYS_MASK,
	MSR_PRMRR_VALID_CONFIG,
	MSR_UNCORE_PRMRR_PHYS_BASE,
	MSR_UNCORE_PRMRR_PHYS_MASK,

	/*
	 * CET disabled:
	 * CPUID.07H.ECX[7] (CPUID_ECX_CET_SS)
	 * CPUID.07H.EDX[20] (CPUID_ECX_CET_IBT)
	 */
	MSR_IA32_U_CET,
	MSR_IA32_S_CET,
	MSR_IA32_PL0_SSP,
	MSR_IA32_PL1_SSP,
	MSR_IA32_PL2_SSP,
	MSR_IA32_PL3_SSP,
	MSR_IA32_INTERRUPT_SSP_TABLE_ADDR,

	/*
	 * PKS disabled:
	 * CPUID.07H.ECX[31] (CPUID_ECX_PKS)
	 */
	MSR_IA32_PKRS,

	/*
	 * HWP package ctrl disabled:
	 * CPUID.06H.EAX[11] (MSR_IA32_HWP_REQUEST_PKG)
	 * CPUID.06H.EAX[16] (MSR_IA32_HWP_PECI_REQUEST_INFO)
	 * CPUID.06H.EAX[22] (MSR_IA32_HWP_CTL)
	 */
	MSR_IA32_HWP_REQUEST_PKG,
	MSR_IA32_HWP_CTL,
	MSR_IA32_HWP_PECI_REQUEST_INFO,

	/*
	 * HWP interrupt disabled:
	 * CPUID.06H.EAX[8]
	 */
	MSR_IA32_HWP_INTERRUPT,

	/*
	 * HFI and IDT registers disabled:
	 * CPUID.06H.EAX[19]
	 * CPUID.06H.EAX[23]
	 */
	IA32_HW_FEEDBACK_PTR,
	IA32_HW_FEEDBACK_CONFIG,
	IA32_THREAD_FEEDBACK_CHAR,
	IA32_HW_FEEDBACK_THREAD_CONFIG,
	IA32_HRESET_ENABLE,

	/*
	 * Hardware Duty Cycling disabled:
	 * CPUID.06H.EAX[13]
	 */
	 MSR_IA32_PKG_HDC_CTL,
	 MSR_IA32_PM_CTL1,
	 MSR_IA32_THREAD_STALL,

	/*
	 * TME registers disabled:
	 * CPUID.07H.ECX[13]
	 * CPUID.07H.EDX[18]
	 * CPUID.1BH
	 */
	MSR_IA32_TME_CAPABILITY,
	MSR_IA32_TME_ACTIVATE,
	MSR_IA32_TME_EXCLUDE_MASK,
	MSR_IA32_TME_EXCLUDE_BASE,
	MSR_CORE_MKTME_ACTIVATION,

	/*
	 * VMX related MSRs disabled:
	 * CPUID.01H.ECX[5]
	 */
	MSR_IA32_VMX_BASIC,
	MSR_IA32_VMX_PINBASED_CTLS,
	MSR_IA32_VMX_PROCBASED_CTLS,
	MSR_IA32_VMX_EXIT_CTLS,
	MSR_IA32_VMX_ENTRY_CTLS,
	MSR_IA32_VMX_MISC,
	MSR_IA32_VMX_CR0_FIXED0,
	MSR_IA32_VMX_CR0_FIXED1,
	MSR_IA32_VMX_CR4_FIXED0,
	MSR_IA32_VMX_CR4_FIXED1,
	MSR_IA32_VMX_VMCS_ENUM,
	MSR_IA32_VMX_PROCBASED_CTLS2,
	MSR_IA32_VMX_EPT_VPID_CAP,
	MSR_IA32_VMX_TRUE_PINBASED_CTLS,
	MSR_IA32_VMX_TRUE_PROCBASED_CTLS,
	MSR_IA32_VMX_TRUE_EXIT_CTLS,
	MSR_IA32_VMX_TRUE_ENTRY_CTLS,
	MSR_IA32_VMX_VMFUNC,
	MSR_IA32_VMX_PROCBASED_CTLS3,
	MSR_IA32_VMX_EXIT_CTLS2,

	/*
	 * User Read/Write MSR feature disabled:
	 * CPUID.(EAX=07H,ECX=1):EDX[15]
	 */
	MSR_IA32_USER_MSR_CTL,

	/*
	 * RDMSRLIST/WRMSRLIST disabled, hide IA32_BARRIER:
	 * CPUID.(EAX=07H,ECX=1):EAX[27]
	 */
	MSR_IA32_BARRIER,
};

/**
 * @brief Get MSR index of the emulated MSR array
 *
 * This function translates MSR encoding to corresponding index of the emulated MSR array.
 * If the MSR is not in the array, NUM_EMULATED_MSRS is returned as an invalid index. The
 * caller shall check the return value before access the array slot.
 *
 * Normal case behaviors:
 * - Return array index of the emulated MSR.
 *
 * Error case behaviors:
 * - Return NUM_EMULATED_MSRS as an invalid index.
 *
 * @param[in] msr Target MSR encoding
 *
 * @return Emulated MSR array index if target MSR is found, otherwise NUM_EMULATED_MSRS.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark The caller shall check whether the return value is valid before use it.
 */
uint32_t vmsr_get_guest_msr_index(uint32_t msr)
{
	uint32_t index;

	for (index = 0U; index < NUM_EMULATED_MSRS; index++) {
		if (emulated_guest_msrs[index] == msr) {
			break;
		}
	}

	if (index == NUM_EMULATED_MSRS) {
		pr_err("%s, MSR %x is not defined in array emulated_guest_msrs[]", __func__, msr);
	}

	return index;
}

/**
 * @brief Enable or disable MSR interception
 *
 * This function is used to set VMCS MSR-Bitmap. For MSR in range [0, 0x1fffU] and
 * [0xc0000000U, 0xc0001fffU], update read bitmaps and write bitmaps according to the interception
 * mode. Read or write to the MSR in guest is intercepted if corresponding bit is set in the bitmaps,
 * otherwise guest accesses the MSR directly. If input MSR is out of the range, skip bitmap update
 * and print an error message.
 * Refer to 25.6.9 MSR-Bitmap Address, SDM Vol. 3 325462-078US for details.
 *
 * @param[inout] bitmap Target MSR interception bitmap
 * @param[in] msr_arg Target MSR to configure in bitmap
 * @param[in] mode Interception mode for target MSR
 *
 * @return None
 *
 * @pre bitmap != NULL
 *
 * @post N/A
 */
static void enable_msr_interception(uint8_t *bitmap, uint32_t msr_arg, uint32_t mode)
{
	uint32_t read_offset = 0U;
	uint32_t write_offset = 2048U;
	uint32_t msr = msr_arg;
	uint8_t msr_bit;
	uint32_t msr_index;

	if ((msr <= 0x1FFFU) || ((msr >= 0xc0000000U) && (msr <= 0xc0001fffU))) {
		if ((msr & 0xc0000000U) != 0U) {
			read_offset = read_offset + 1024U;
			write_offset = write_offset + 1024U;
		}

		msr &= 0x1FFFU;
		msr_bit = (uint8_t)(1U << (msr & 0x7U));
		msr_index = msr >> 3U;

		if ((mode & INTERCEPT_READ) == INTERCEPT_READ) {
			bitmap[read_offset + msr_index] |= msr_bit;
		} else {
			bitmap[read_offset + msr_index] &= ~msr_bit;
		}

		if ((mode & INTERCEPT_WRITE) == INTERCEPT_WRITE) {
			bitmap[write_offset + msr_index] |= msr_bit;
		} else {
			bitmap[write_offset + msr_index] &= ~msr_bit;
		}
	} else {
		pr_err("%s, Invalid MSR: 0x%x", __func__, msr);
	}
}

/**
 * @brief Set thermal related MSR interception
 *
 * This function is used to initialize thermal related MSR interception per VM configuration.
 * If per-core thermal monitor is not passed through, intercept read/write to MSR_IA32_CLOCK_MODULATION
 * and MSR_IA32_THERM_INTERRUPT. And if VM virtual HWP is not set, also intercept read/write to
 * MSR_IA32_THERM_STATUS. If package thermal monitor is not passed through, intercept read/write to
 * MSR_IA32_PACKAGE_THERM_STATUS and MSR_IA32_PACKAGE_THERM_INTERRUPT.
 *
 * @param[inout] vcpu Target vCPU to configure thermal MSR interception
 *
 * @return None
 *
 * @pre vcpu != NULL
 * @pre vcpu->arch.msr_bitmap != NULL
 *
 * @post N/A
 */
static void init_thermal_msrs(struct acrn_vcpu *vcpu)
{
	uint8_t *msr_bitmap = vcpu->arch.msr_bitmap;
	if (!vm_cpu_feature_enabled(vcpu->vm, GUEST_CPU_FEATURE_TM_PT)) {
		enable_msr_interception(msr_bitmap, MSR_IA32_CLOCK_MODULATION, INTERCEPT_READ_WRITE);
		enable_msr_interception(msr_bitmap, MSR_IA32_THERM_INTERRUPT, INTERCEPT_READ_WRITE);
		if (!vm_cpu_feature_enabled(vcpu->vm, GUEST_CPU_FEATURE_VHWP)) {
			enable_msr_interception(msr_bitmap, MSR_IA32_THERM_STATUS, INTERCEPT_READ_WRITE);
		}
	}
	if (!vm_cpu_feature_enabled(vcpu->vm, GUEST_CPU_FEATURE_PTM_PT)) {
		enable_msr_interception(msr_bitmap, MSR_IA32_PACKAGE_THERM_STATUS, INTERCEPT_READ_WRITE);
		enable_msr_interception(msr_bitmap, MSR_IA32_PACKAGE_THERM_INTERRUPT, INTERCEPT_READ_WRITE);
	}
}

/**
 * @brief Set LBR MSR interception
 *
 * This function is used to configure interception for LBR related MSRs. If LBR feature is not passed
 * through to guest, the function performs below configuration:
 *  - Intercept read and write to the LBR MSRs.
 *  - If PMU is not passed through to guest, intercept read and write to MSR_IA32_DEBUGCTL.
 *
 * Otherwise pass through LBR MSRs and MSR_IA32_DEBUGCTL to guest.
 * Refer to 19.2 MSRS, SDM Vol. 3 325462-078US for details.
 *
 * @param[inout] vcpu Target vCPU to set MSR interception
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static void init_lbr_msrs(struct acrn_vcpu *vcpu)
{
	uint8_t *msr_bitmap = vcpu->arch.msr_bitmap;
	uint32_t msr;
	if (!vm_cpu_feature_enabled(vcpu->vm, GUEST_CPU_FEATURE_LBR_PT)) {
		/* Enable read and write msr interception for all MSR_LASTBRANCH_x_FROM_IP MSRs (0x680U ~ 0x69FU) */
		for (msr = (MSR_LASTBRANCH_31_FROM_IP - 31); msr <= MSR_LASTBRANCH_31_FROM_IP; msr ++) {
			enable_msr_interception(msr_bitmap, msr, INTERCEPT_READ_WRITE);
		}
		/* Enable read and write msr interception for all MSR_IA32_LBR_x_INFO MSRs (0x1200U ~ 0x121FU) */
		for (msr = (MSR_IA32_LBR_31_INFO - 31); msr <= MSR_IA32_LBR_31_INFO; msr ++) {
			enable_msr_interception(msr_bitmap, msr, INTERCEPT_READ_WRITE);
		}
		/* Enable read and write msr interception for all MSR_IA32_LBR_x_FROM_IP MSRs (0x1500U ~ 0x151FU) */
		for (msr = (MSR_IA32_LBR_31_FROM_IP - 31); msr <= MSR_IA32_LBR_31_FROM_IP; msr ++) {
			enable_msr_interception(msr_bitmap, msr, INTERCEPT_READ_WRITE);
		}
		/* Enable read and write msr interception for all MSR_IA32_LBR_x_TO_IP MSRs (0x1600U ~ 0x161FU) */
		for (msr = (MSR_IA32_LBR_31_TO_IP - 31); msr <= MSR_IA32_LBR_31_TO_IP; msr ++) {
			enable_msr_interception(msr_bitmap, msr, INTERCEPT_READ_WRITE);
		}
		/* Intercept MSR_IA32_DEBUGCTL (0x1D9U) if PMU_PT is also not enabled */
		if (!is_pmu_pt_configured(vcpu->vm)) {
			enable_msr_interception(msr_bitmap, MSR_IA32_DEBUGCTL, INTERCEPT_READ_WRITE);
		}
		/* Intercept MSR_IA32_LER_FROM_LIP (0x1DDU) */
		enable_msr_interception(msr_bitmap, MSR_IA32_LER_FROM_LIP, INTERCEPT_READ_WRITE);
		/* Intercept MSR_IA32_LER_TO_LIP (0x1DEU) */
		enable_msr_interception(msr_bitmap, MSR_IA32_LER_TO_LIP, INTERCEPT_READ_WRITE);
		/* Intercept MSR_IA32_LER_INFO (0x1E0U) */
		enable_msr_interception(msr_bitmap, MSR_IA32_LER_INFO, INTERCEPT_READ_WRITE);
		/* Intercept MSR_IA32_LBR_CTL (0x14CEU) */
		enable_msr_interception(msr_bitmap, MSR_IA32_LBR_CTL, INTERCEPT_READ_WRITE);
		/* Intercept MSR_IA32_LBR_DEPTH (0x14CFU) */
		enable_msr_interception(msr_bitmap, MSR_IA32_LBR_DEPTH, INTERCEPT_READ_WRITE);
	}
}

/**
 * @brief Set x2APIC MSR interception
 *
 * This function is used to configure interception for x2APIC related MSRs, i.e., MSRs in range:
 * [0x800U, 0x900U]. It iterates x2APIC MSR range and set corresponding bit per interception mode.
 *
 * @param[inout] msr_bitmap_arg Target bitmap to configure x2APIC MSR interception
 * @param[in] mode Target MSR interception mode to set
 *
 * @return None
 *
 * @pre msr_bitmap_arg != NULL
 *
 * @post N/A
 */
static void intercept_x2apic_msrs(uint8_t *msr_bitmap_arg, uint32_t mode)
{
	uint8_t *msr_bitmap = msr_bitmap_arg;
	uint32_t msr;

	for (msr = 0x800U; msr < 0x900U; msr++) {
		enable_msr_interception(msr_bitmap, msr, mode);
	}
}


/**
 * @brief Initialize x2APIC MSR interception
 *
 * This function is used to configure read and write interception to x2APIC MSRs.
 * If LAPIC is passed through to VM, the function performs below configuration:
 * 1. Intercept read to MSR_IA32_EXT_XAPICID and MSR_IA32_EXT_APIC_LDR.
 * 2. Intercept write to MSR_IA32_EXT_APIC_ICR.
 * 3. Disable interception to other x2APIC MSRs.
 * 4. Intercept read/write to MSR_IA32_EXT_APIC_LVT_THERMAL if per-core thermal monitor is not passed through.
 * 5. Intercept TSC related MSRs according to current VMCS VMX_TSC_OFFSET_FULL field value.
 *
 * Otherwise it performs:
 * 1. Intercept write to x2APIC MSRs.
 * 2. If IPI virtualization is enabled, disabled interception to MSR_IA32_EXT_APIC_ICR.
 * 3. Intercept read to MSR_IA32_EXT_APIC_CUR_COUNT, MSR_IA32_EXT_APIC_EOI and MSR_IA32_EXT_APIC_SELF_IPI.
 * 4. Disable interception for MSR_IA32_EXT_APIC_TPR.
 *
 * @param[inout] vcpu Target vCPU to set x2APIC MSR interception
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static void init_x2apic_msrs(struct acrn_vcpu *vcpu)
{
	uint8_t *msr_bitmap = vcpu->arch.msr_bitmap;

	if (is_lapic_pt_configured(vcpu->vm)) {
		/*
		 * After switch to x2apic mode, most MSRs are passthrough to guest.
		 * for virtualization of some MSRs for security consideration:
		 * - XAPICID/LDR: Read to XAPICID/LDR need to be trapped to guarantee guest always see right vlapic_id.
		 * - ICR: Write to ICR need to be trapped to avoid malicious IPI.
		 */
		intercept_x2apic_msrs(msr_bitmap, INTERCEPT_DISABLE);
		enable_msr_interception(msr_bitmap, MSR_IA32_EXT_XAPICID, INTERCEPT_READ);
		enable_msr_interception(msr_bitmap, MSR_IA32_EXT_APIC_LDR, INTERCEPT_READ);
		enable_msr_interception(msr_bitmap, MSR_IA32_EXT_APIC_ICR, INTERCEPT_WRITE);
		if (!vm_cpu_feature_enabled(vcpu->vm, GUEST_CPU_FEATURE_TM_PT)) {
			enable_msr_interception(msr_bitmap, MSR_IA32_EXT_APIC_LVT_THERMAL, INTERCEPT_READ_WRITE);
		}
		set_tsc_msr_interception(vcpu, exec_vmread64(VMX_TSC_OFFSET_FULL) != 0UL);

	} else {
		intercept_x2apic_msrs(msr_bitmap, INTERCEPT_WRITE);

		if (can_ipiv_enabled(vcpu->vm)) {
			enable_msr_interception(msr_bitmap, MSR_IA32_EXT_APIC_ICR, INTERCEPT_DISABLE);
		}

		/*
		 * Open read-only interception for write-only
		 * registers to inject gp on reads. EOI and Self-IPI
		 * Writes are disabled for EOI, TPR and Self-IPI as
		 * writes to them are virtualized with Register Virtualization
		 * Refer to Section 30.5 in Vol.3, Intel SDM-325384-085US.
		 */
		enable_msr_interception(msr_bitmap, MSR_IA32_EXT_APIC_CUR_COUNT, INTERCEPT_READ);
		enable_msr_interception(msr_bitmap, MSR_IA32_EXT_APIC_EOI, INTERCEPT_READ);
		enable_msr_interception(msr_bitmap, MSR_IA32_EXT_APIC_SELF_IPI, INTERCEPT_READ);

		/*
		 * TPR is virtualized even when register virtualization is not supported
		 */
		enable_msr_interception(msr_bitmap, MSR_IA32_EXT_APIC_TPR, INTERCEPT_DISABLE);
	}
}

/**
 * @brief Prepare save/restore MSR lists
 *
 * This function is used to set up MSR lists (auto-MSR lists) which shall be automatically saved/restored
 * at VM-Exit/VM-Entry. If VM PMU passthrough is set, add MSR_IA32_PERF_GLOBAL_CTRL to the auto-MSR lists
 * for guest and host side. If RDT is available on the platform and guest/host have different settings,
 * add MSR_IA32_PQR_ASSOC to auto-MSR lists for guest and host. Currently max auto-MSR list size is 2.
 *
 * @param[inout] vcpu Target vCPU to configure auto-MSR lists
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static void prepare_auto_msr_area(struct acrn_vcpu *vcpu)
{
	vcpu->arch.msr_area.count = 0U;

	/* in HV, disable perf/PMC counting, just count in guest VM */
	if (is_pmu_pt_configured(vcpu->vm)) {
		vcpu->arch.msr_area.guest[vcpu->arch.msr_area.count].msr_index = MSR_IA32_PERF_GLOBAL_CTRL;
		vcpu->arch.msr_area.guest[vcpu->arch.msr_area.count].value = 0;
		vcpu->arch.msr_area.host[vcpu->arch.msr_area.count].msr_index = MSR_IA32_PERF_GLOBAL_CTRL;
		vcpu->arch.msr_area.host[vcpu->arch.msr_area.count].value = 0;
		vcpu->arch.msr_area.count++;
	}

	if (is_platform_rdt_capable()) {
		struct acrn_vm_config *cfg = get_vm_config(vcpu->vm->vm_id);
		uint16_t vcpu_clos;

		ASSERT(cfg->num_pclosids != 0, "error, cfg->num_pclosids is 0");
		ASSERT(cfg->pclosids != NULL, "error, cfg->pclosids is NULL");

		vcpu_clos = cfg->pclosids[vcpu->vcpu_id%cfg->num_pclosids];

		/* RDT: only load/restore MSR_IA32_PQR_ASSOC when hv and guest have different settings
		 */
		if (vcpu_clos != hv_clos) {
			vcpu->arch.msr_area.guest[vcpu->arch.msr_area.count].msr_index = MSR_IA32_PQR_ASSOC;
			vcpu->arch.msr_area.guest[vcpu->arch.msr_area.count].value = clos2pqr_msr(vcpu_clos);
			vcpu->arch.msr_area.host[vcpu->arch.msr_area.count].msr_index = MSR_IA32_PQR_ASSOC;
			vcpu->arch.msr_area.host[vcpu->arch.msr_area.count].value = clos2pqr_msr(hv_clos);
			vcpu->arch.msr_area.index_of_pqr_assoc = vcpu->arch.msr_area.count;
			vcpu->arch.msr_area.count++;

			pr_acrnlog("switch clos for VM %u vcpu_id %u, host 0x%x, guest 0x%x",
				vcpu->vm->vm_id, vcpu->vcpu_id, hv_clos, vcpu_clos);
		}
	}

	ASSERT(vcpu->arch.msr_area.count <= MSR_AREA_COUNT, "error, please check MSR_AREA_COUNT!");
}

/**
 * @brief Initialize emulated MSRs
 *
 * This function sets up initial values for emulated MSRs. Currently only IA32_FEATURE_CONTROL MSR is
 * initialized here. MSR_IA32_FEATURE_CONTROL_LOCK bit is always set for guest. If VM virtual SGX is set,
 * bit 18(SGX Global Enable bit) shall be set in IA32_FEATURE_CONTROL.
 *
 * @param[inout] vcpu Target vCPU to initialize the emulated MSRs
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static void init_emulated_msrs(struct acrn_vcpu *vcpu)
{
	uint64_t val64 = MSR_IA32_FEATURE_CONTROL_LOCK;

#ifdef CONFIG_SGX_ENABLED
	if (is_vsgx_supported(vcpu->vm->vm_id)) {
		val64 |= MSR_IA32_FEATURE_CONTROL_SGX_GE;
	}
#endif

	/* MSR_IA32_FEATURE_CONTROL */
	vcpu_set_guest_msr(vcpu, MSR_IA32_FEATURE_CONTROL, val64);
}

/**
 * @brief Set Machine-Check(MC) MSR interception for one bank
 *
 * This function sets MSR interception mode for one MC bank, i.e., MSR_IA32_MC_{CTL, STATUS, ADDR, MISC}_x.
 * If pCPU support CMCI, it also configures interception for MSR_IA32_MC_CTL2_x.
 *
 * @param[inout] vcpu Target vCPU to set MC MSR interception
 * @param[in] bank_idx Target MC bank IDi
 * @param[in] mode Interception mode
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static void enable_mc_bank_interception(struct acrn_vcpu *vcpu, uint16_t bank_idx, uint32_t mode)
{
	uint8_t *msr_bitmap = vcpu->arch.msr_bitmap;

	enable_msr_interception(msr_bitmap, MSR_IA32_MC_CTL(bank_idx), mode);
	enable_msr_interception(msr_bitmap, MSR_IA32_MC_STATUS(bank_idx), mode);
	enable_msr_interception(msr_bitmap, MSR_IA32_MC_ADDR(bank_idx), mode);
	enable_msr_interception(msr_bitmap, MSR_IA32_MC_MISC(bank_idx), mode);

	if (is_cmci_supported()) {
		enable_msr_interception(msr_bitmap, MSR_IA32_MC_CTL2(bank_idx), mode);
	}
}

/**
 * @brief Pass through one Machine-Check(MC) bank
 *
 * This function disables MSR interception for one MC bank per the bank index.
 *
 * @param[inout] vcpu Target vCPU to pass through the MC bank
 * @param[in] bank_idx Target MC bank's index
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static void mc_bank_pt_by_idx(struct acrn_vcpu *vcpu, uint16_t bank_idx)
{
	enable_mc_bank_interception(vcpu, bank_idx, INTERCEPT_DISABLE);
}

/**
 * @brief Hide one Machine-Check(MC) bank
 *
 * This function enables MSR read/write interception for one MC bank per the bank index.
 *
 * @param[inout] vcpu Target vCPU to hide MC bank
 * @param[in] bank_idx Target MC bank's index
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static void mc_bank_hide_by_idx(struct acrn_vcpu *vcpu, uint16_t bank_idx)
{
	enable_mc_bank_interception(vcpu, bank_idx, INTERCEPT_READ_WRITE);
}

/**
 * @brief Get configured Machine-Check(MC) bank count
 *
 * This function returns the smaller one between VM configured machine-check bank count and
 * pCPU supported bank count. The return value is used to synthesize MSR_IA32_MCG_CAP when
 * guest reads the MSR.
 *
 * @param[in] vcpu Target vCPU to get the configured MC bank count
 *
 * @return Guest available MC bank count
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static uint8_t mc_bank_configured_count(struct acrn_vcpu *vcpu)
{
	struct acrn_vm_config *vm_config = get_vm_config(vcpu->vm->vm_id);
	uint8_t nbanks_configured = bitmap_weight(vm_config->mcbanks_bitmask);
	/* this bitmask needs to be continuous */
	return min(nbanks_configured, mc_bank_count());
}

/**
 * @brief Pass through or hide MC related MSRs
 *
 * This function firstly checks whether SER_P(Software Error Recovery support Present) or LMCE_P
 * (Local Machine-Check Exception) feature is supported, prints warning message if either of them
 * is available. In this case passthrough MC related MSRs to guest is not appropriate. Otherwise,
 * it prints error messages when detects below cases:
 * 1. Not all MC banks are passed through.
 * 2. !(is_service_vm(vm) || vm_cpu_feature_enabled(vm, GUEST_CPU_FEATURE_VHWP) || is_lapic_pt_configured(vm))
 * 3. The number of MC banks passed through is large than HW supported MC banks.
 *
 * For guest that configures MC bank passthrough, pass through all MC banks listed in mcbanks_bitmask
 * and MSR_IA32_MCG_STATUS. For guest that doesn't configure MC bank passthrough, it hides all MC banks.
 *
 * Currently we support only MSR_IA32_MCG_CAP_CMCI_P and MSR_IA32_MCG_CAP_TES_P, everything else is
 * un-supported. Which means
 * 1. Recoverable errors are NOT supported.
 * 2. Local MCE is NOT supported.
 *
 * It is the user/config-tool's responsibility to make sure:
 * 1. P-core hyper-thread and E-core in the same core/cluster will NOT be assigned to different VMs
 * 2. Each pCPU will have exactly one "governing vcpu"
 * 3. Guests who exclusively own pCPU will claim this ownership, either by setting own_pcpu to y in
 * scenario.xml, or set LAPIC-PT (partitioned guest)
 *
 * It also hides legacy MC registers: MSR_IA32_P5_MC_ADDR and MSR_IA32_P5_MC_TYPE.
 *
 * @param[inout] vcpu Target vCPU to set Machine-Check MSR interception
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static void init_mc_msrs(struct acrn_vcpu *vcpu)
{
	struct acrn_vm *vm = vcpu->vm;
	struct acrn_vm_config *vm_config = get_vm_config(vm->vm_id);
	uint64_t mcb_bm = 0UL;
	uint64_t conf_bm = vm_config->mcbanks_bitmask;
	uint8_t *msr_bitmap = vcpu->arch.msr_bitmap;
	uint16_t i;

	if (is_sw_error_recovery_supported() || is_local_mc_supported()) {
		/* If any of these are physically supported, we stop passthrough and hide everything */
		pr_err("Physical platform supports software error recovery and/or local machine check,");
		pr_err("Passing-through MC banks under this case is inappropriate. Stop passthrough and hide everything.");
	} else {
		if (is_mc_pt_configured(vm)) {
			if (bitmap_weight(conf_bm) != (1 + fls64(conf_bm))) {
				/* TODO: Non-continuous bank pt currently not supported */
				pr_err("Non-continous mcbanks_bitmask or bitmask doesn't start from 0. For VM%d, MC banks will NOT be passed-through", vm->vm_id);
			} else if (!(is_service_vm(vm) || vm_cpu_feature_enabled(vm, GUEST_CPU_FEATURE_VHWP) || is_lapic_pt_configured(vm))) {
				/* GUEST_CPU_FEATURE_VHWP will be set if own_pcpu was set to y in scenario XML */
				pr_err("Guest mcbanks_bitmask configured but this guest does NOT exclusively own pCPU.");
				pr_err("MC banks will NOT be passed-through.");
			} else {
				if (bitmap_weight(conf_bm) > mc_bank_count()) {
					pr_err("Warning: Invalid mcbanks_bitmask configuration 0x%lx, platform supports only up to %u banks", conf_bm, mc_bank_count());
					pr_err("Warning: Extraneous banks ignored");
				}

				/* CAP will be emulated, pt STATUS only */
				enable_msr_interception(msr_bitmap, MSR_IA32_MCG_STATUS, INTERCEPT_DISABLE);
				vcpu->arch.mc_pt_enabled = true;
				mcb_bm = conf_bm;

			}
		}
	}

	for (i = 0; i < mc_bank_count(); i++) {
		if (bitmap_test(i, &mcb_bm)) {
			pr_info("%s: passing through MC%d to vm%d, vcpu%d", __func__, i, vm->vm_id, vcpu->vcpu_id);
			mc_bank_pt_by_idx(vcpu, i);
		} else {
			mc_bank_hide_by_idx(vcpu, i);
		}
	}

	/* Hide legacy P5 MC registers */
	enable_msr_interception(msr_bitmap, MSR_IA32_P5_MC_ADDR, INTERCEPT_READ_WRITE);
	enable_msr_interception(msr_bitmap, MSR_IA32_P5_MC_TYPE, INTERCEPT_READ_WRITE);
}

/**
 * @brief Initialize interception settings for MSRs
 *
 * This function configures guest MSR interception as below:
 * - For Guest MSRs emulated by HV: intercept read and write.
 * - For MTRR related MSRs exclusive of Fixed and SMRR MTRRs: intercept read and write.
 * - For Performance monitoring related MSRs: if PMU is not passed through to VM, intercept read and write,
 *   otherwise pass through the MSRs.
 *
 * - x2APIC MSRs:
 *   - When `GUEST_FLAG_LOCKED_IN_XAPIC` is not set (i.e., the VM is locked in x2APIC mode):
 *       - Interception behavior is configured based on whether LAPIC pass-through is enabled for the VM.
 *
 *   - When `GUEST_FLAG_LOCKED_IN_XAPIC` is set (i.e., the VM is restricted to xAPIC mode):
 *       - Access to x2APIC MSRs is not expected; intercept both reads and writes to capture and handle such
 *         invalid access attempts.
 *
 * - For CAT Mask MSRs: intercept read and write.
 * - For MSR_IA32_TIME_STAMP_COUNTER and MSR_IA32_XSS: intercept write.
 * - For Machine Check MSRs: if machine-check is not passed through to VM, intercept read and write,
 *   otherwise pass through the MSRs.
 * - For thermal monitor related MSRs: if the feature is not passed through to VM, intercept read and write,
 *   otherwise pass through the MSRs.
 * - For Last Branch Recording MSRs: if the feature is not passed through to VM, intercept read and write,
 *   otherwise pass through the MSRs.
 * - Other MSRs including unsupported MSRs: intercept read and write.
 *
 * This function also initializes guest MSR_IA32_FEATURE_CONTROL MSR and configures MSRs that are automatically
 * switched at VM-Entry/VM-Exit for guest and host.
 *
 * Normal case behaviors:
 * - Initialize guest MSR interception and set up MSR-Bitmap Address VMCS field.
 *
 * Error case behaviors:
 * - None.
 *
 * @param[inout] vcpu Target vCPU to configure MSR interception
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
void init_msr_emulation(struct acrn_vcpu *vcpu)
{
	uint8_t *msr_bitmap = vcpu->arch.msr_bitmap;
	uint32_t msr, i;
	uint64_t value64;

	for (i = 0U; i < NUM_EMULATED_MSRS; i++) {
		enable_msr_interception(msr_bitmap, emulated_guest_msrs[i], INTERCEPT_READ_WRITE);
	}

	for (i = 0U; i < ARRAY_SIZE(mtrr_msrs); i++) {
		enable_msr_interception(msr_bitmap, mtrr_msrs[i], INTERCEPT_READ_WRITE);
	}

	/* for core partition VM (like RTVM), passthrou PMC MSRs for performance profiling/tuning; hide to other VMs */
	if (!is_pmu_pt_configured(vcpu->vm)) {
		for (i = 0U; i < ARRAY_SIZE(pmc_msrs); i++) {
			enable_msr_interception(msr_bitmap, pmc_msrs[i], INTERCEPT_READ_WRITE);
		}
	}

	if (!is_locked_in_xapic_configured(vcpu->vm)) {
		init_x2apic_msrs(vcpu);
	} else {
		intercept_x2apic_msrs(msr_bitmap, INTERCEPT_READ_WRITE);
	}

	for (i = 0U; i < ARRAY_SIZE(unsupported_msrs); i++) {
		enable_msr_interception(msr_bitmap, unsupported_msrs[i], INTERCEPT_READ_WRITE);
	}

	/* RDT-A disabled: CPUID.07H.EBX[12], CPUID.10H */
	for (msr = MSR_IA32_L3_MASK_BASE; msr < MSR_IA32_BNDCFGS; msr++) {
		enable_msr_interception(msr_bitmap, msr, INTERCEPT_READ_WRITE);
	}

	/* don't need to intercept rdmsr for these MSRs */
	enable_msr_interception(msr_bitmap, MSR_IA32_TIME_STAMP_COUNTER, INTERCEPT_WRITE);
	enable_msr_interception(msr_bitmap, MSR_IA32_XSS, INTERCEPT_WRITE);

	/* Setup MSR bitmap - Intel SDM Vol3 24.6.9 */
	value64 = hva2hpa(vcpu->arch.msr_bitmap);
	exec_vmwrite64(VMX_MSR_BITMAP_FULL, value64);
	pr_dbg("VMX_MSR_BITMAP: 0x%016lx ", value64);

	/* Initialize the MSR save/store area */
	prepare_auto_msr_area(vcpu);

	/* Setup initial value for emulated MSRs */
	init_emulated_msrs(vcpu);

	/* Initialize Machine Check MSRs for passthrough */
	init_mc_msrs(vcpu);

	/* Initialize thermal MSRs interception */
	init_thermal_msrs(vcpu);

	/*  Initialize Last Branch Records MSRs interception */
	init_lbr_msrs(vcpu);
}

/**
 * @brief Emulate write MSR_IA32_PAT MSR
 *
 * This function checks validity of MSR value before cache it. If CR0.CD is set, the new PAT value is
 * deferred to be written to VMCS field until guest CR0.CD is cleared.
 *
 * @param[inout] vcpu Target vCPU to set PAT MSR value
 * @param[in] value Target MSR data
 *
 * @return A integer value to indicate the operation result
 *
 * @retval -EINVAL Input MSR value is invalid
 * @retval 0 Write operation is successful
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static int32_t write_pat_msr(struct acrn_vcpu *vcpu, uint64_t value)
{
	uint32_t i;
	uint64_t field;
	int32_t ret = 0;

	for (i = 0U; i < 8U; i++) {
		field = (value >> (i * 8U)) & 0xffUL;
		if (is_pat_mem_type_invalid(field)) {
			pr_err("invalid guest IA32_PAT: 0x%016lx", value);
			ret = -EINVAL;
			break;
		}
	}

	if (ret == 0) {
		vcpu_set_guest_msr(vcpu, MSR_IA32_PAT, value);

		/*
		 * If context->cr0.CD is set, we defer any further requests to write
		 * guest's IA32_PAT, until the time when guest's CR0.CD is being cleared
		 */
		if ((vcpu_get_cr0(vcpu) & CR0_CD) == 0UL) {
			exec_vmwrite64(VMX_GUEST_IA32_PAT_FULL, value);
		}
	}

	return ret;
}

/*
 * Use the base frequency state of pCPU as the emulated reg field:
 * - IA32_PERF_STATUS[15:0] Current performance State Value
 * Assuming (base frequency ratio << 8) is a valid state value for all CPU models.
 */

/**
 * @brief Get emulated IA32_PERF_STATUS MSR value
 *
 * This function queries CPUID(16H).EAX[15:0] and ECX[15:0] to emulate current CPU performance status.
 * The emulated result is calculated by formula: ((EAX[15:0]/ECX[15:0]) & 0xFFU) << 8.
 *
 * @return A integer value calculated by the formula.
 *
 * @pre N/A
 *
 * @post N/A
 */
static uint64_t get_perf_status(void)
{
	uint32_t eax, ecx, unused;
	/*
	 * CPUID.16H:eax[15:0] Base CPU Frequency (MHz)
	 * CPUID.16H:ecx[15:0] Bus Frequency (MHz)
	 * ratio = CPU_frequency/bus_frequency
	 */
	cpuid_subleaf(CPUID_FREQ, 0U, &eax, &unused, &ecx, &unused);
	return (uint64_t)(((eax/ecx) & 0xFFU) << 8);
}

/**
 * @brief Check if Key Locker(KL) and Internal Wrapping Key(IWKEY) backing up are supported
 *
 * This function queries CPUID(19H).EBX[4] to determine whether KL and IWKEY backing up features
 * are supported. The bit is set if the features are supported.
 *
 * @param[in] vcpu Target vCPU to check the features
 *
 * @return A boolean value indicating if the features are supported
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static bool is_iwkey_backup_support(struct acrn_vcpu *vcpu)
{
	uint32_t eax = CPUID_KEY_LOCKER, ebx = 0U, ecx = 0U, edx = 0U;

	guest_cpuid(vcpu, &eax, &ebx, &ecx, &edx);
	return (ebx & CPUID_EBX_KL_BACKUP_MSR) == CPUID_EBX_KL_BACKUP_MSR;
}

/**
 * @brief Handler for rdmsr-induced VM-Exit
 *
 * This function handles VM-Exit induced by guest MSR read to the emulated, hidden or unsupported
 * MSRs. Refer to description of init_msr_emulation() for which MSRs are read-intercepted.
 * For emulated MSRs, HV may return cached MSR values or synthesize values based on pCPU features and
 * cached values. For hidden and unsupported MSRs,this function returns an error to caller.
 *
 * Refer to section 6.3.4 of Software Architecture Design Specification for the detailed design.
 *
 * Normal case behaviors:
 * - Emulate MSR value and return it in guest RAX and RDX if the MSR is supported.
 *
 * Error case behaviors:
 * - Return an error code to caller if the MSR is hidden or unsupported.
 *
 * @param[inout] vcpu Target vCPU to read the MSR value
 *
 * @return An integer to indicate whether the operation is successful.
 *
 * @retval -EACCES The MSR is hidden or unsupported.
 * @retval -EPERM The target MSR cannot be accessed.
 * @retval 0 The read operation is successful and the MSR value is updated to guest RAX and RDX.
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
int32_t rdmsr_vmexit_handler(struct acrn_vcpu *vcpu)
{
	int32_t err = 0;
	uint32_t msr;
	uint64_t v = 0UL;

	/* Read the msr value */
	msr = (uint32_t)vcpu_get_gpreg(vcpu, CPU_REG_RCX);

	/* Do the required processing for each msr case */
	switch (msr) {
	case MSR_IA32_TSC_DEADLINE:
	{
		v = vlapic_get_tsc_deadline_msr(vcpu_vlapic(vcpu));
		break;
	}
	case MSR_IA32_TSC_ADJUST:
	{
		v = vcpu_get_guest_msr(vcpu, MSR_IA32_TSC_ADJUST);
		break;
	}
	case MSR_IA32_MTRR_CAP:
	case MSR_IA32_MTRR_DEF_TYPE:
	case MSR_IA32_MTRR_PHYSBASE_0:
	case MSR_IA32_MTRR_PHYSMASK_0:
	case MSR_IA32_MTRR_PHYSBASE_1:
	case MSR_IA32_MTRR_PHYSMASK_1:
	case MSR_IA32_MTRR_PHYSBASE_2:
	case MSR_IA32_MTRR_PHYSMASK_2:
	case MSR_IA32_MTRR_PHYSBASE_3:
	case MSR_IA32_MTRR_PHYSMASK_3:
	case MSR_IA32_MTRR_PHYSBASE_4:
	case MSR_IA32_MTRR_PHYSMASK_4:
	case MSR_IA32_MTRR_PHYSBASE_5:
	case MSR_IA32_MTRR_PHYSMASK_5:
	case MSR_IA32_MTRR_PHYSBASE_6:
	case MSR_IA32_MTRR_PHYSMASK_6:
	case MSR_IA32_MTRR_PHYSBASE_7:
	case MSR_IA32_MTRR_PHYSMASK_7:
	case MSR_IA32_MTRR_PHYSBASE_8:
	case MSR_IA32_MTRR_PHYSMASK_8:
	case MSR_IA32_MTRR_PHYSBASE_9:
	case MSR_IA32_MTRR_PHYSMASK_9:
	{
		v = read_vmtrr(vcpu, msr);
		break;
	}
	case MSR_IA32_BIOS_SIGN_ID:
	{
		v = get_microcode_version();
		break;
	}
	case MSR_IA32_PERF_STATUS:
	{
		if (vm_cpu_feature_enabled(vcpu->vm, GUEST_CPU_FEATURE_VHWP)) {
			v = msr_read(msr);
		} else {
			v = get_perf_status();
		}
		break;
	}
	case MSR_IA32_PERF_CTL:
	{
		v = vcpu_get_guest_msr(vcpu, MSR_IA32_PERF_CTL);
		break;
	}
	case MSR_IA32_PM_ENABLE:
	case MSR_IA32_HWP_CAPABILITIES:
	case MSR_IA32_HWP_REQUEST:
	case MSR_IA32_HWP_STATUS:
	{
		if (vm_cpu_feature_enabled(vcpu->vm, GUEST_CPU_FEATURE_VHWP)) {
			v = msr_read(msr);
		} else {
			err = -EACCES;
		}
		break;
	}
	case MSR_IA32_PAT:
	{
		/*
		 * note: if run_ctx->cr0.CD is set, the actual value in guest's
		 * IA32_PAT MSR is PAT_ALL_UC_VALUE, which may be different from
		 * the saved value guest_msrs[MSR_IA32_PAT]
		 */
		v = vcpu_get_guest_msr(vcpu, MSR_IA32_PAT);
		break;
	}
	case MSR_IA32_EFER:
	{
		v = vcpu_get_efer(vcpu);
		break;
	}
	case MSR_IA32_APIC_BASE:
	{
		/* Read APIC base */
		v = vlapic_get_apicbase((const struct acrn_vlapic *)vcpu_vlapic(vcpu));
		break;
	}
	case MSR_IA32_FEATURE_CONTROL:
	{
		v = vcpu_get_guest_msr(vcpu, MSR_IA32_FEATURE_CONTROL);
		break;
	}
	case MSR_IA32_MCG_CAP:
	{
		if (is_mc_pt_enabled(vcpu)) {
			v = msr_read(msr);
			v &= (MSR_IA32_MCG_CAP_CMCI_P | MSR_IA32_MCG_CAP_TES_P);
			v |= mc_bank_configured_count(vcpu);
		} else {
			v = 0U;
		}
		break;
	}
	case MSR_IA32_MCG_STATUS:
	{
		v = 0U;
		break;
	}
	case MSR_IA32_MISC_ENABLE:
	{
		v = vcpu_get_guest_msr(vcpu, MSR_IA32_MISC_ENABLE);
		break;
	}
#ifdef CONFIG_SGX_ENABLED
	case MSR_IA32_SGXLEPUBKEYHASH0:
	case MSR_IA32_SGXLEPUBKEYHASH1:
	case MSR_IA32_SGXLEPUBKEYHASH2:
	case MSR_IA32_SGXLEPUBKEYHASH3:
	case MSR_IA32_SGX_SVN_STATUS:
	{
		if (is_vsgx_supported(vcpu->vm->vm_id)) {
			v = msr_read(msr);
		} else {
			err = -EACCES;
		}
		break;
	}
#endif
	case MSR_IA32_COPY_STATUS:
	{
		if (is_iwkey_backup_support(vcpu)) {
			v = vcpu->arch.iwkey_copy_status;
		} else {
			err = -EACCES;
		}
		break;
	}
	case MSR_IA32_IWKEY_BACKUP_STATUS:
	{
		if (is_iwkey_backup_support(vcpu)) {
			v = vcpu->vm->arch_vm.iwkey_backup_status;
		} else {
			err = -EACCES;
		}
		break;
	}
	case MSR_IA32_UMWAIT_CONTROL:
	{
		/* Feature X86_FEATURE_WAITPKG is always presented */
		if (pcpu_has_cap(X86_FEATURE_WAITPKG)) {
			v = vcpu_get_guest_msr(vcpu, msr);
		} else {
			err = -EACCES;
		}
		break;
	}
	case MSR_TEST_CTL:
	{
		/* If has MSR_TEST_CTL, give emulated value
		 * If don't have MSR_TEST_CTL, trigger #GP
		 */
		if (has_core_cap(CORE_CAP_SPLIT_LOCK) || has_core_cap(CORE_CAP_UC_LOCK)) {
			v = vcpu_get_guest_msr(vcpu, MSR_TEST_CTL);
		} else {
			vcpu_inject_gp(vcpu, 0U);
		}
		break;
	}
	case MSR_PLATFORM_INFO:
	{
		if (is_service_vm(vcpu->vm) || vm_cpu_feature_enabled(vcpu->vm, GUEST_CPU_FEATURE_VHWP)) {
			v = msr_read(msr);
			v &= MSR_PLATFORM_INFO_MAX_NON_TURBO_LIM_RATIO_MASK |
			     MSR_PLATFORM_INFO_MAX_EFFICIENCY_RATIO_MASK |
			     MSR_PLATFORM_INFO_MIN_OPERATING_RATIO_MASK |
			     MSR_PLATFORM_INFO_SAMPLE_PART;
		} else {
			/* Allow read by non-service vm for compatibility */
			v = 0UL;
			pr_warn("%s(): vm%d read MSR_PLATFORM_INFO", __func__, vcpu->vm->vm_id);
		}
		break;
	}
	case MSR_IA32_XAPIC_DIS_STATUS:
	{
		if (!is_locked_in_xapic_configured(vcpu->vm)) {
			/* When GUEST_FLAG_LOCKED_IN_XAPIC is not set, the VM is locked in x2APIC. */
			v = IA32_LEGACY_XAPIC_DISABLED;
		} else {
			/*
			 * Guest should access IA32_XAPIC_DISABLE_STATUS only after verifying that
			 * MSR_IA32_ARCH_CAPABILITIES indicates support for it.
			 */
			err = -EACCES;
		}
		break;
	}
	case MSR_IA32_ARCH_CAPABILITIES:
	{
		v = msr_read(MSR_IA32_ARCH_CAPABILITIES);

		if (!is_locked_in_xapic_configured(vcpu->vm)) {
			/* always emulate the IA32_XAPIC_DISABLE_STATUS MSR exists */
			v |= IA32_ARCH_CAP_XAPIC_DIS_STATUS;
		} else {
			/* always emulate the IA32_XAPIC_DISABLE_STATUS MSR does not exist */
			v &= ~IA32_ARCH_CAP_XAPIC_DIS_STATUS;
		}
		break;
	}
	default:
	{
		if (is_x2apic_msr(msr)) {
			err = vlapic_x2apic_read(vcpu, msr, &v);
		} else {
			err = -EACCES;
			v = 0UL;
		}
		break;
	}
	}

	if (err == 0) {
		/* Store the MSR contents in RAX and RDX */
		vcpu_set_gpreg(vcpu, CPU_REG_RAX, v & 0xffffffffU);
		vcpu_set_gpreg(vcpu, CPU_REG_RDX, v >> 32U);
	} else {
		pr_warn("%s(): vm%d vcpu%d reading MSR %x not supported\n",
			__func__, vcpu->vm->vm_id, vcpu->vcpu_id, msr);
	}
	TRACE_2L(TRACE_VMEXIT_RDMSR, msr, v);
	return err;
}

/**
 * @brief Set interception for Time-Stamp Counter(TSC) related MSRs
 *
 * This function configures interception for MSR_IA32_TSC_DEADLINE and MSR_IA32_TSC_ADJUST per current
 * and required interception status.
 * If VMX_TSC_OFFSET_FULL is 0, no need to trap the write of IA32_TSC_DEADLINE because there is no offset
 * between vTSC and pTSC, in this case, only write to vTSC_ADJUST is trapped.
 * The function handles below two cases:
 * -# If MSR IA32_TSC_DEADLINE currently is set to be intercepted and required interception is off:
 *    1. Disable interception for MSR_IA32_TSC_DEADLINE, enable interception for write to MSR_IA32_TSC_ADJUST.
 *    2. If TSC_DEADLINE timer hasn't expired, sync virtual TSC_DEADLINE to physical TSC_DEADLINE MSR.
 *    3. If TSC_DEADLINE timer has expired, write 0 to the virtual TSC_DEADLINE.
 * -# If MSR IA32_TSC_DEADLINE currently is set to be not intercepted and required interception is on
 *    1. Enable interception for read/write to MSR_IA32_TSC_DEADLINE and MSR_IA32_TSC_ADJUST.
 *    2. Sync physical TSC_DEADLINE MSR to virtual TSC_DEADLINE MSR.
 *
 * @param[inout] vcpu Target vCPU to set the interception
 * @param[in] interception Target interception mode
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static void set_tsc_msr_interception(struct acrn_vcpu *vcpu, bool interception)
{
	uint8_t *msr_bitmap = vcpu->arch.msr_bitmap;
	bool is_intercepted =
		((msr_bitmap[MSR_IA32_TSC_DEADLINE >> 3U] & (1U << (MSR_IA32_TSC_DEADLINE & 0x7U))) != 0U);

	if (!interception && is_intercepted) {
		enable_msr_interception(msr_bitmap, MSR_IA32_TSC_DEADLINE, INTERCEPT_DISABLE);
		enable_msr_interception(msr_bitmap, MSR_IA32_TSC_ADJUST, INTERCEPT_WRITE);
		/* If the timer hasn't expired, sync virtual TSC_DEADLINE to physical TSC_DEADLINE, to make the guest read the same tsc_deadline
		 * as it writes. This may change when the timer actually trigger.
		 * If the timer has expired, write 0 to the virtual TSC_DEADLINE.
		 */
		if (msr_read(MSR_IA32_TSC_DEADLINE) != 0UL) {
			msr_write(MSR_IA32_TSC_DEADLINE, vcpu_get_guest_msr(vcpu, MSR_IA32_TSC_DEADLINE));
		} else {
			vcpu_set_guest_msr(vcpu, MSR_IA32_TSC_DEADLINE, 0UL);
		}
	} else if (interception && !is_intercepted) {
		enable_msr_interception(msr_bitmap, MSR_IA32_TSC_DEADLINE, INTERCEPT_READ_WRITE);
		enable_msr_interception(msr_bitmap, MSR_IA32_TSC_ADJUST, INTERCEPT_READ_WRITE);
		/* sync physical TSC_DEADLINE to virtual TSC_DEADLINE */
		vcpu_set_guest_msr(vcpu, MSR_IA32_TSC_DEADLINE, msr_read(MSR_IA32_TSC_DEADLINE));
	} else {
		/* Do nothing */
	}
}

/**
 * @brief Emulate write MSR_IA32_TIME_STAMP_COUNTER MSR
 *
 * This function sets guest MSR_IA32_TSC_ADJUST MSR and VMX_TSC_OFFSET_FULL VMCS field per guest TSC value.
 * It updates guest MSR_IA32_TSC_ADJUST MSR and VMCS VMX_TSC_OFFSET_FULL field as below:
 * - VMCS.OFFSET = vTSC - pTSC
 * - vAdjust += VMCS.OFFSET's delta
 * Update interception for MSR_IA32_TSC_{DEADLINE, ADJUST} according to guest/host TSC delta.
 * Refer to 18.17.3 Time-Stamp Counter Adjustment, SDM Vol. 3 325462-078US for details.
 *
 * @param[inout] vcpu Target vCPU to set the MSR and VMCS field
 * @param[in] guest_tsc Guest TSC value
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static void set_guest_tsc(struct acrn_vcpu *vcpu, uint64_t guest_tsc)
{
	uint64_t tsc_delta, tsc_offset_delta, tsc_adjust;

	tsc_delta = guest_tsc - rdtsc();

	/* the delta between new and existing TSC_OFFSET */
	tsc_offset_delta = tsc_delta - exec_vmread64(VMX_TSC_OFFSET_FULL);

	/* apply this delta to TSC_ADJUST */
	tsc_adjust = vcpu_get_guest_msr(vcpu, MSR_IA32_TSC_ADJUST);
	vcpu_set_guest_msr(vcpu, MSR_IA32_TSC_ADJUST, tsc_adjust + tsc_offset_delta);

	/* write to VMCS because rdtsc and rdtscp are not intercepted */
	exec_vmwrite64(VMX_TSC_OFFSET_FULL, tsc_delta);

	set_tsc_msr_interception(vcpu, tsc_delta != 0UL);
}

/*
 * The policy of vART is that software in native can run in VM too. And in native side,
 * the relationship between the ART hardware and TSC is:
 *
 *   pTSC = (pART * M) / N + pAdjust
 *
 * The vART solution is:
 *   - Present the ART capability to guest through CPUID leaf
 *     15H for M/N which identical to the physical values.
 *   - PT devices see the pART (vART = pART).
 *   - Guest expect: vTSC = vART * M / N + vAdjust.
 *   - VMCS.OFFSET = vTSC - pTSC = vAdjust - pAdjust.
 *
 * So to support vART, we should do the following:
 *   1. if vAdjust and vTSC are changed by guest, we should change
 *      VMCS.OFFSET accordingly.
 *   2. Make the assumption that the pAjust is never touched by ACRN.
 *
 * Intel SDM 17.17.3: "If an execution of WRMSR to the IA32_TSC_ADJUST
 * MSR adds (or subtracts) value X from that MSR, the logical
 * processor also adds (or subtracts) value X from the TSC."
 *
 * So, here we should update VMCS.OFFSET and vAdjust accordingly.
 *   - VMCS.OFFSET += vAdjust's delta
 *   - vAdjust = new vAdjust set by guest
 */


/**
 * @brief Emulate write MSR_IA32_TSC_ADJUST MSR
 *
 * This function sets guest MSR_IA32_TSC_ADJUST MSR and VMCS VMX_TSC_OFFSET_FULL field per guest TSC adjust value.
 * It updates guest MSR_IA32_TSC_ADJUST MSR and VMCS VMX_TSC_OFFSET_FULL field as below:
 * - VMCS.OFFSET += vAdjust's delta
 * - vAdjust = new vAdjust set by guest
 * Update interception for MSR_IA32_TSC_{DEADLINE, ADJUST} according to guest TSC_offset + TSC_adjust_delta.
 * Refer to 18.17.3 Time-Stamp Counter Adjustment, SDM Vol. 3 325462-078US for details.
 *
 * @param[inout] vcpu Target vCPU to set the MSR and VMCS field
 * @param[in] tsc_adjust Guest TSC adjust value
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static void set_guest_tsc_adjust(struct acrn_vcpu *vcpu, uint64_t tsc_adjust)
{
	uint64_t tsc_offset, tsc_adjust_delta;

	/* delta of the new and existing IA32_TSC_ADJUST */
	tsc_adjust_delta = tsc_adjust - vcpu_get_guest_msr(vcpu, MSR_IA32_TSC_ADJUST);

	/* apply this delta to existing TSC_OFFSET */
	tsc_offset = exec_vmread64(VMX_TSC_OFFSET_FULL);
	exec_vmwrite64(VMX_TSC_OFFSET_FULL, tsc_offset + tsc_adjust_delta);

	/* IA32_TSC_ADJUST is supposed to carry the value it's written to */
	vcpu_set_guest_msr(vcpu, MSR_IA32_TSC_ADJUST, tsc_adjust);

	set_tsc_msr_interception(vcpu, (tsc_offset + tsc_adjust_delta) != 0UL);
}

/**
 * @brief Emulate write IA32_MISC_ENABLE MSR
 *
 * This function updates IA32_MISC_ENABLE MSR and related MSR/VMCS field. It performs below steps:
 * 1. If MSR_IA32_MISC_ENABLE_XD_DISABLE is set, update guest EFER MSR to reset MSR_IA32_EFER_NXE_BIT.
 * 2. If MISC_ENABLE_MONITOR_ENA is to be changed:
 *    - Check if guest CPUID(EAX=01H).ECX[0] is clear, if it is, inject \#GP to guest per 2.1 ARCHITECTURAL MSRS
 *      Table 2-2, SDM Vol.4, 325462-078US.
 *    - Otherwise, if LAPIC is passed through to VM, set VMX_PROCBASED_CTLS_MONITOR and VMX_PROCBASED_CTLS_MWAIT
 *      in vcpu->arch.proc_vm_exec_ctrls if MSR_IA32_MISC_ENABLE_MONITOR_ENA is to be cleared, otherwise clear
 *      the two bits in vcpu->arch.proc_vm_exec_ctrls.
 * 3. Reset MSR_IA32_MISC_ENABLE_LIMIT_CPUID bit in new MSR value.
 * 4. Update guest MSR_IA32_MISC_ENABLE with new MSR value.
 *
 * @param[inout] vcpu Target vCPU to update IA32_MISC_ENABLE MSR
 * @param[in] v Guest MSR value
 *
 * @return None
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */
static void set_guest_ia32_misc_enable(struct acrn_vcpu *vcpu, uint64_t v)
{
	uint32_t eax, ebx = 0U, ecx = 0U, edx = 0U;
	bool update_vmsr = true;
	uint64_t effective_guest_msr = v;

	/* According to SDM Vol4 2.1 & Vol 3A 4.1.4,
	 * EFER.NXE should be cleared if guest disable XD in IA32_MISC_ENABLE
	 */
	if ((v & MSR_IA32_MISC_ENABLE_XD_DISABLE) != 0UL) {
		vcpu_set_efer(vcpu, vcpu_get_efer(vcpu) & ~MSR_IA32_EFER_NXE_BIT);
	}

	/* Handle MISC_ENABLE_MONITOR_ENA
	 * - if guest try to set this bit, MISC_ENABLE_MONITOR_ENA bit of guest MSR_IA32_MISC_ENABLE
	 *   shall be set. lapic_pt guest does not trap MONITOR/MWAIT
	 * - if guest try to clear this bit, MISC_ENABLE_MONITOR_ENA bit of guest MSR_IA32_MISC_ENABLE
	 *   shall be cleared. lapic_pt guest traps MONITOR/MWAIT, so that it can inject UD
	 * - guest cpuid.01H will be updated when guest executes 'cpuid' with leaf 01H
	 */
	if (((v ^ vcpu_get_guest_msr(vcpu, MSR_IA32_MISC_ENABLE)) & MSR_IA32_MISC_ENABLE_MONITOR_ENA) != 0UL) {
		eax = CPUID_FEATURES;
		guest_cpuid(vcpu, &eax, &ebx, &ecx, &edx);
		/* According to SDM Vol4 2.1 Table 2-2,
		 * Writing this bit when the SSE3 feature flag is set to 0 may generate a #GP exception.
		 */
		if ((ecx & CPUID_ECX_SSE3) == 0U) {
			vcpu_inject_gp(vcpu, 0U);
			update_vmsr = false;
		} else if (is_lapic_pt_configured(vcpu->vm)) {
			if ((v & MSR_IA32_MISC_ENABLE_MONITOR_ENA) == 0UL) {
				/* guest attempts execute MONITOR/MWAIT will cause #UD */
				vcpu->arch.proc_vm_exec_ctrls |= VMX_PROCBASED_CTLS_MONITOR;
				vcpu->arch.proc_vm_exec_ctrls |= VMX_PROCBASED_CTLS_MWAIT;
				exec_vmwrite32(VMX_PROC_VM_EXEC_CONTROLS, vcpu->arch.proc_vm_exec_ctrls);
			} else {
				/* allows guest to use MONITOR/MWAIT */
				vcpu->arch.proc_vm_exec_ctrls &= ~VMX_PROCBASED_CTLS_MONITOR;
				vcpu->arch.proc_vm_exec_ctrls &= ~VMX_PROCBASED_CTLS_MWAIT;
				exec_vmwrite32(VMX_PROC_VM_EXEC_CONTROLS, vcpu->arch.proc_vm_exec_ctrls);
			}
		} else {
			/* vmsr value be updated */
		}
	}

	/* Ignore the write of IA32_MISC_ENABLE[bit 22] */
	effective_guest_msr &= ~MSR_IA32_MISC_ENABLE_LIMIT_CPUID;

	if (update_vmsr) {
		vcpu_set_guest_msr(vcpu, MSR_IA32_MISC_ENABLE, effective_guest_msr);
	}
}

/**
 * @brief Handler for wrmsr-induced VM-Exit
 *
 * This function handles VM-Exit induced by guest MSR write to the emulated, hidden or unsupported
 * MSRs. Refer to description of init_msr_emulation() for which MSRs are write-intercepted.
 * For emulated MSRs, HV may cache the MSR values or synthesize values based on pCPU features
 * and target MSR values then update the physical MSRs or VMCS fields. For MSRs that write is not
 * supported, the written value shall be ignored. For hidden and unsupported MSRs, this function
 * returns an error to caller.
 *
 * Refer to section 6.3.4 of Software Architecture Design Specification for the detailed design.
 *
 * Normal case behaviors:
 * - Cache the MSR value or write a synthesized value to physical MSR if the MSR is supported.
 * - Ignore the value if the MSR is read-only or HV doesn't support write to the MSR.
 *
 * Error case behaviors:
 * - Return an error code to caller if the MSR is hidden or unsupported.
 *
 * @param[inout] vcpu Target vCPU to write MSR value.
 *
 * @return An integer to indicate whether the operation is successful.
 *
 * @retval -EACCES The MSR is hidden or unsupported.
 * @retval -EINVAL The target MSR value is invalid.
 * @retval -EPERM The target MSR cannot be accessed
 * @retval 0 Otherwise
 *
 * @pre vcpu != NULL
 *
 * @post N/A
 */

int32_t wrmsr_vmexit_handler(struct acrn_vcpu *vcpu)
{
	int32_t err = 0;
	uint32_t msr;
	uint64_t v;

	/* Read the MSR ID */
	msr = (uint32_t)vcpu_get_gpreg(vcpu, CPU_REG_RCX);

	/* Get the MSR contents */
	v = (vcpu_get_gpreg(vcpu, CPU_REG_RDX) << 32U) |
		vcpu_get_gpreg(vcpu, CPU_REG_RAX);

	/* Do the required processing for each msr case */
	switch (msr) {
	case MSR_IA32_TSC_DEADLINE:
	{
		vlapic_set_tsc_deadline_msr(vcpu_vlapic(vcpu), v);
		break;
	}
	case MSR_IA32_TSC_ADJUST:
	{
		set_guest_tsc_adjust(vcpu, v);
		break;
	}
	case MSR_IA32_TIME_STAMP_COUNTER:
	{
		set_guest_tsc(vcpu, v);
		break;
	}
	case MSR_IA32_MTRR_DEF_TYPE:
	case MSR_IA32_MTRR_PHYSBASE_0:
	case MSR_IA32_MTRR_PHYSMASK_0:
	case MSR_IA32_MTRR_PHYSBASE_1:
	case MSR_IA32_MTRR_PHYSMASK_1:
	case MSR_IA32_MTRR_PHYSBASE_2:
	case MSR_IA32_MTRR_PHYSMASK_2:
	case MSR_IA32_MTRR_PHYSBASE_3:
	case MSR_IA32_MTRR_PHYSMASK_3:
	case MSR_IA32_MTRR_PHYSBASE_4:
	case MSR_IA32_MTRR_PHYSMASK_4:
	case MSR_IA32_MTRR_PHYSBASE_5:
	case MSR_IA32_MTRR_PHYSMASK_5:
	case MSR_IA32_MTRR_PHYSBASE_6:
	case MSR_IA32_MTRR_PHYSMASK_6:
	case MSR_IA32_MTRR_PHYSBASE_7:
	case MSR_IA32_MTRR_PHYSMASK_7:
	case MSR_IA32_MTRR_PHYSBASE_8:
	case MSR_IA32_MTRR_PHYSMASK_8:
	case MSR_IA32_MTRR_PHYSBASE_9:
	case MSR_IA32_MTRR_PHYSMASK_9:
	{
		err = write_vmtrr(vcpu, msr, v);
		break;
	}
	case MSR_IA32_BIOS_SIGN_ID:
	{
		break;
	}
	case MSR_IA32_BIOS_UPDT_TRIG:
	{
		/* We only allow Service VM to do uCode update */
		if (is_service_vm(vcpu->vm)) {
			acrn_update_ucode(vcpu, v);
		}
		break;
	}
	case MSR_IA32_PERF_STATUS:
	{
		break;
	}
	case MSR_IA32_PERF_CTL:
	{
		vcpu_set_guest_msr(vcpu, MSR_IA32_PERF_CTL, v);
		break;
	}
	case MSR_IA32_PM_ENABLE:
	{
		if (!vm_cpu_feature_enabled(vcpu->vm, GUEST_CPU_FEATURE_VHWP)) {
			err = -EACCES;
		}
		/* Set by HV. Writing from guests will have no effect */
		break;
	}
	case MSR_IA32_HWP_CAPABILITIES:
	{
		/* RO */
		break;
	}
	case MSR_IA32_HWP_REQUEST:
	{
		if (vm_cpu_feature_enabled(vcpu->vm, GUEST_CPU_FEATURE_VHWP)) {
			err = msr_write_safe(msr, v, (MSR_IA32_HWP_REQUEST_RSV_BITS | MSR_IA32_HWP_REQUEST_PKG_CTL));
		} else {
			err = -EACCES;
		}
		break;
	}
	case MSR_IA32_HWP_STATUS:
	{
		if (vm_cpu_feature_enabled(vcpu->vm, GUEST_CPU_FEATURE_VHWP)) {
			err = msr_write_safe(msr, v, MSR_IA32_HWP_STATUS_RSV_BITS);
		} else {
			err = -EACCES;
		}
		break;
	}
	case MSR_IA32_PAT:
	{
		err = write_pat_msr(vcpu, v);
		break;
	}
	case MSR_IA32_EFER:
	{
		/* IA32_EFER.LMA bit is RO, modifying by WRMSR shall be ignored. */
		struct run_context *ctx = &vcpu->arch.contexts.run_ctx;
		v = (v & ~MSR_IA32_EFER_LMA_BIT) | (ctx->ia32_efer & MSR_IA32_EFER_LMA_BIT);

		uint64_t reserved = ~(MSR_IA32_EFER_SCE_BIT |
					MSR_IA32_EFER_LME_BIT |
					MSR_IA32_EFER_LMA_BIT |
					MSR_IA32_EFER_NXE_BIT);
		if (v & reserved) {
			err = -EINVAL;
		} else {
			uint64_t guest_ia32_misc_enable = vcpu_get_guest_msr(vcpu, MSR_IA32_MISC_ENABLE);

			/*
			 * When paging is enabled, changing LME bit should generate #GP.
			 * See 64-bit mode consistency checks in Chapter 10.8.5, Vol. 3, SDM.
			 */
			if ((ctx->cr0 & CR0_PG) && ((v ^ ctx->ia32_efer) & MSR_IA32_EFER_LME_BIT)) {
				err = -EINVAL;
			} else if (((v & MSR_IA32_EFER_NXE_BIT) != 0UL) &&
			           (guest_ia32_misc_enable & MSR_IA32_MISC_ENABLE_XD_DISABLE)) {
				/*
				 * SDM (March 2025) 6.13.1 Detecting and Enabling the Execute-Disable Capability
				 * If the execute-disable capability is not available, a write to set IA32_EFER.NXE
				 * produces a #GP exception.
				 */
				err = -EINVAL;
			} else {
				vcpu_set_efer(vcpu, v);
			}
		}

		break;
	}
	case MSR_IA32_APIC_BASE:
	{
		err = vlapic_set_apicbase(vcpu_vlapic(vcpu), v);
		break;
	}
	case MSR_IA32_MCG_STATUS:
	{
		if (v != 0U) {
			err = -EACCES;
		}
		break;
	}
	case MSR_IA32_MCG_CAP:
	case MSR_IA32_FEATURE_CONTROL:
	case MSR_IA32_SGXLEPUBKEYHASH0:
	case MSR_IA32_SGXLEPUBKEYHASH1:
	case MSR_IA32_SGXLEPUBKEYHASH2:
	case MSR_IA32_SGXLEPUBKEYHASH3:
	case MSR_IA32_SGX_SVN_STATUS:
	{
		err = -EACCES;
		break;
	}
	case MSR_IA32_MISC_ENABLE:
	{
		uint64_t ro_mask = MSR_IA32_MISC_ENABLE_PMA | MSR_IA32_MISC_ENABLE_BTS_UNAVAIL |
			MSR_IA32_MISC_ENABLE_PEBS_UNAVAIL | MSR_IA32_MISC_ENABLE_XTPR;

		if (((v ^ vcpu_get_guest_msr(vcpu, MSR_IA32_MISC_ENABLE)) & ro_mask) != 0UL) {
			err = -EACCES;
		} else {
			set_guest_ia32_misc_enable(vcpu, v);
		}
		break;
	}
	case MSR_IA32_XSS:
	{
		if (vcpu->arch.xsave_enabled) {
			if ((v & ~(MSR_IA32_XSS_PT | MSR_IA32_XSS_HDC)) != 0UL) {
				err = -EACCES;
			} else {
				vcpu_set_guest_msr(vcpu, MSR_IA32_XSS, v);
				msr_write(msr, v);
			}
		} else {
			err = -EACCES;
		}
		break;
	}
	case MSR_IA32_COPY_LOCAL_TO_PLATFORM:
	{
		/* check feature support and avoid setting reserved MSR bits */
		if (is_iwkey_backup_support(vcpu) && ((v & ~0x1UL) == 0x0UL)) {
			if (v == 0x1UL) {
				vcpu->vm->arch_vm.iwkey_backup_status = 0UL;
				spinlock_obtain(&vcpu->vm->arch_vm.iwkey_backup_lock);
				vcpu->vm->arch_vm.iwkey_backup = vcpu->arch.IWKey;
				spinlock_release(&vcpu->vm->arch_vm.iwkey_backup_lock);
				/*
				* Keylocker spec 0.76 Table 4-1:
				* 'Backup/restore valid' bit and 'IWKeyBackup consumed' bit
				*/
				vcpu->vm->arch_vm.iwkey_backup_status = 0x9UL;
				vcpu->arch.iwkey_copy_status = 1UL;
			}
		} else {
			err = -EINVAL;
		}
		break;
	}
	case MSR_IA32_COPY_PLATFORM_TO_LOCAL:
	{
		/* check feature support and avoid setting reserved MSR bits */
		if (is_iwkey_backup_support(vcpu) && ((v & ~0x1UL) == 0x0UL)) {
			if ((v == 0x1UL) && (vcpu->vm->arch_vm.iwkey_backup_status == 0x9UL)) {
				spinlock_obtain(&vcpu->vm->arch_vm.iwkey_backup_lock);
				vcpu->arch.IWKey = vcpu->vm->arch_vm.iwkey_backup;
				spinlock_release(&vcpu->vm->arch_vm.iwkey_backup_lock);
				/* Load the new iwkey for this vcpu */
				get_cpu_var(whose_iwkey) = NULL;
				load_iwkey(vcpu);
				vcpu->arch.iwkey_copy_status = 1UL;
			}
		} else {
			err = -EINVAL;
		}
		break;
	}
	case MSR_IA32_UMWAIT_CONTROL:
	{
		/* Feature X86_FEATURE_WAITPKG is always presented */
		if (pcpu_has_cap(X86_FEATURE_WAITPKG)) {
			vcpu_set_guest_msr(vcpu, msr, v);
			msr_write(msr, v);
		} else {
			err = -EACCES;
		}
		break;
	}
	case MSR_TEST_CTL:
	{
		/* If VM has MSR_TEST_CTL, ignore write operation
		 * If don't have MSR_TEST_CTL, trigger #GP
		 */
		if (has_core_cap(CORE_CAP_SPLIT_LOCK) || has_core_cap(CORE_CAP_UC_LOCK)) {
			vcpu_set_guest_msr(vcpu, MSR_TEST_CTL, v);
			pr_warn("Ignore writing 0x%llx to MSR_TEST_CTL from VM%d", v, vcpu->vm->vm_id);
		} else {
			vcpu_inject_gp(vcpu, 0U);
		}
		break;
	}
	default:
	{
		if (is_x2apic_msr(msr)) {
			err = vlapic_x2apic_write(vcpu, msr, v);
		} else {
			err = -EACCES;
		}
		break;
	}
	}
	if (err != 0) {
		pr_warn("%s(): vm%d vcpu%d writing MSR %x fail\n",
			__func__, vcpu->vm->vm_id, vcpu->vcpu_id, msr);
	}
	TRACE_2L(TRACE_VMEXIT_WRMSR, msr, v);
	return err;
}

/**
 * @}
 */
