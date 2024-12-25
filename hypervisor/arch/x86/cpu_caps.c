/*
 * Copyright (C) 2018-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <asm/msr.h>
#include <asm/page.h>
#include <asm/cpufeatures.h>
#include <asm/cpuid.h>
#include <asm/cpu.h>
#include <asm/per_cpu.h>
#include <asm/vmx.h>
#include <asm/cpu_caps.h>
#include <errno.h>
#include <logmsg.h>
#include <asm/guest/vmcs.h>

/**
 * @defgroup hwmgmt_hw-caps hwmgmt.hw-caps
 * @ingroup hwmgmt
 * @brief The definition and implementation of HW capabilities related stuff.
 *
 * It provides external APIs for manipulating HW capabilities information.
 *
 * @{
 */

/**
 * @file
 * @brief The definition and implementation of CPU capabilities related stuff.
 *
 * This file contains the data structures and functions for the hwmgmt.hw-caps to detect CPU capabilities and provide
 * interfaces to other modules.
 */

/* TODO: add more capability per requirement */
/* APICv features */
/** @brief Bitmask indicates APICv feature: Virtualize APIC accesses */
#define VAPIC_FEATURE_VIRT_ACCESS      (1U << 0U)
/** @brief Bitmask indicates APICv feature: APIC-register virtualization */
#define VAPIC_FEATURE_VIRT_REG         (1U << 1U)
/** @brief Bitmask indicates APICv feature: Virtual-interrupt delivery */
#define VAPIC_FEATURE_INTR_DELIVERY    (1U << 2U)
/** @brief Bitmask indicates APICv feature: TPR shadow */
#define VAPIC_FEATURE_TPR_SHADOW       (1U << 3U)
/** @brief  Bitmask indicates APICv feature: Process posted interrupts */
#define VAPIC_FEATURE_POST_INTR        (1U << 4U)
/** @brief Bitmask indicates APICv feature: Virtualize x2APIC mode */
#define VAPIC_FEATURE_VX2APIC_MODE     (1U << 5U)
/** @brief Bitmask indicates APICv feature: IPI Virtualization */
#define VAPIC_FEATURE_IPI_VIRT         (1U << 6U)

/**
 * @brief APICv basic features
 *
 * Define basic APICv features that must be supported by the physical platform and will be enabled by default:
 * TPR shadow, Virtualize APIC accesses, Virtualize x2APIC mode.
 */
#define APICV_BASIC_FEATURE     (VAPIC_FEATURE_TPR_SHADOW | VAPIC_FEATURE_VIRT_ACCESS | VAPIC_FEATURE_VX2APIC_MODE)

/**
 * @brief APICv advanced features
 *
 * Define advanced APICv features, enable them by default if the physical platform support them all, otherwise disable
 * them all:
 * APIC-register virtualization, Virtual-interrupt delivery, Process posted interrupts.
 */
#define APICV_ADVANCED_FEATURE  (VAPIC_FEATURE_VIRT_REG | VAPIC_FEATURE_INTR_DELIVERY | VAPIC_FEATURE_POST_INTR)

/**
 * @brief Data structure to store the CPU capabilities
 *
 * It is intended to be used for obtaining and storing CPU capabilities data.
 */
struct cpu_capability {
	uint8_t apicv_features;	/**< Bits indication of APICv features. */
	uint8_t ept_features;	/**< Indication of EPT feature: 0 means not supporting, 1 means supporting. */
	uint64_t vmx_ept_vpid;	/**< Value of MSR register: MSR_IA32_VMX_EPT_VPID_CAP. */
	uint32_t core_caps;	/**< Value of MSR register: MSR_IA32_CORE_CAPABILITIES. */
	uint64_t mcg_caps;	/**< Value of MSR register: MSR_IA32_MCG_CAP. */
};

/**
 * @brief Global variable storing the CPU capabilities
 *
 * Declare static variable cpu_caps with type 'struct cpu_capability' to store the CPU capabilities.
 */
static struct cpu_capability cpu_caps;

/**
 * @brief Global variable storing the CPU information data.
 *
 * Declare static variable boot_cpu_data with type 'struct cpuinfo_x86' to store the CPU information data.
 */
static struct cpuinfo_x86 boot_cpu_data;

/**
 * @brief VMX capability structure
 *
 * This structure is used to define VMX MSR and its control bits to indicate VMX capabilities.
 * Refer to SDM APPENDIX A:
 *   Bits 31:0 indicate the allowed 0-settings of these controls. VM entry allows control X to be 0 if bit X in the MSR
 * is cleared to 0; if bit X in the MSR is set to 1, VM entry fails if control X is 0.
 *   Bits 63:32 indicate the allowed 1-settings of these controls. VM entry allows control X to be 1 if bit 32+X in the
 * MSR is set to 1; if bit 32+X in the MSR is cleared to 0, VM entry fails if control X is 1.
 */
struct vmx_capability{
	uint32_t msr;	/**< MSR index include capability indication of VMX */
	uint32_t bits;	/**< Bitmask of capability indication of VMX in MSR */
} ;

/**
 * @brief The static structure array variable contains all essential VMX capabilities.
 *
 * This array contains the essential VMX capabilities required for the hypervisor to function correctly.
 * Each element in the array specifies the MSR address and the bitmask of essential capabilities for
 * different VMX control categories, such as pin-based, processor-based, secondary processor-based,
 * VM-exit, and VM-entry controls.
 *
 * The function `check_essential_vmx_caps` uses this array to detect and verify the essential VMX capabilities.
 */
static struct vmx_capability vmx_caps[] = {
	{
		MSR_IA32_VMX_PINBASED_CTLS, VMX_PINBASED_CTLS_IRQ_EXIT
	},
	{
		MSR_IA32_VMX_PROCBASED_CTLS, VMX_PROCBASED_CTLS_TSC_OFF | VMX_PROCBASED_CTLS_TPR_SHADOW |
					VMX_PROCBASED_CTLS_IO_BITMAP | VMX_PROCBASED_CTLS_MSR_BITMAP |
					VMX_PROCBASED_CTLS_HLT | VMX_PROCBASED_CTLS_SECONDARY
	},
	{
		MSR_IA32_VMX_PROCBASED_CTLS2, VMX_PROCBASED_CTLS2_VAPIC | VMX_PROCBASED_CTLS2_EPT |
					VMX_PROCBASED_CTLS2_VPID | VMX_PROCBASED_CTLS2_RDTSCP |
					VMX_PROCBASED_CTLS2_UNRESTRICT
	},
	{
		MSR_IA32_VMX_EXIT_CTLS, VMX_EXIT_CTLS_ACK_IRQ | VMX_EXIT_CTLS_SAVE_PAT |
					VMX_EXIT_CTLS_LOAD_PAT | VMX_EXIT_CTLS_HOST_ADDR64
	},
	{
		MSR_IA32_VMX_ENTRY_CTLS, VMX_ENTRY_CTLS_LOAD_PAT | VMX_ENTRY_CTLS_IA32E_MODE
	}
};

/**
 * @brief The interface to check whether a specified feature is supported by the CPU.
 *
 * This function checks the stored CPU feature capability in the global variable `boot_cpu_data` and returns a boolean
 * value to indicate whether the specified feature is supported by the CPU.
 *
 * @param[in] bit The bit that specifies the feature to check. Higher 27 bits is the index of the array
 * `boot_cpu_data.cpuid_leaves[]`, indicating the to-be-checked register. Low 5 bits indicate the to-be-checked bit.
 *
 * @return A boolean value indicating whether the specified feature is supported by the CPU.
 *
 * @retval true The feature is supported by the CPU.
 * @retval false The feature is not supported by the CPU.
 *
 * @pre N/A
 * @post N/A
 * @remark This API shall be called after `init_pcpu_capabilities` has been called once on the bootstrap processor.
 */
bool pcpu_has_cap(uint32_t bit)
{
	uint32_t feat_idx = bit >> 5U;	/* Calculate the index of the feature word */
	uint32_t feat_bit = bit & 0x1fU;	/* Calculate the bit position within the feature word */
	bool ret;

	/* Check if the index is within the bounds of the FEATURE_WORDS array */
	if (feat_idx >= FEATURE_WORDS) {
		ret = false;
	} else {
		/* Check if the specified feature bit is set in the corresponding feature word */
		ret = ((boot_cpu_data.cpuid_leaves[feat_idx] & (1U << feat_bit)) != 0U);
	}

	return ret;
}

/**
 * @brief The interface to check whether the CPU supports MONITOR instructions.
 *
 * This function checks if the CPU supports the MONITOR instructions by examining the CPU feature for MONITOR
 * capability.
 * It returns a boolean value indicating whether the MONITOR instructions are supported.
 * It always returns `false` for APL platform. For other platforms, it returns the result of the check on the physical
 * platform.
 *
 * @return A boolean value indicating whether the CPU supports MONITOR instructions.
 *
 * @retval true  The CPU supports MONITOR instructions.
 * @retval false The CPU doesn't support MONITOR instructions.
 *
 * @pre N/A
 * @post N/A
 *
 * @remark This API shall be called after `init_pcpu_capabilities` has been called once on the bootstrap processor.
 */
bool has_monitor_cap(void)
{
	bool ret = false;

	/* Check if the CPU has the MONITOR capability */
	if (pcpu_has_cap(X86_FEATURE_MONITOR)) {
		/* Don't use MONITOR for CPU (family: 0x6 model: 0x5c)
		 * in hypervisor, but still expose it to the guests and
		 * let them handle it correctly.
		 */
		if (!is_apl_platform()) {
			ret = true;
		}
	}

	return ret;
}

/**
 * @brief The inline function to check if FAST_STRING is enabled and ERMS is supported.
 *
 * This function checks the status of the FAST_STRING feature by reading the dedicated MSR (Model-Specific Register)
 * bit and the ERMS (Enhanced REP MOVSB/STOSB) capability stored in the global variable `cpu_caps`.
 *
 * @return A boolean value indicating whether both features are supported and enabled.
 *
 * @retval true Both features are supported and enabled.
 * @retval false Otherwise.
 *
 * @pre N/A
 * @post N/A
 * @remark This API shall be called after `init_pcpu_capabilities` has been called once on the bootstrap processor.
 */
static inline bool is_fast_string_erms_supported_and_enabled(void)
{
	bool ret = false;
	uint64_t misc_enable = msr_read(MSR_IA32_MISC_ENABLE);

	/* Check if FAST_STRING is enabled */
	if ((misc_enable & MSR_IA32_MISC_ENABLE_FAST_STRING) == 0UL) {
		pr_fatal("%s, fast string is not enabled\n", __func__);
	} else {
		/* Check if ERMS (Enhanced REP MOVSB/STOSB) is supported */
		if (!pcpu_has_cap(X86_FEATURE_ERMS)) {
			pr_fatal("%s, enhanced rep movsb/stosb not supported\n", __func__);
		} else {
			ret = true;
		}
	}

	return ret;
}

/**
 * @brief The internal function to check if a setting in VMX MSR control is allowed.
 *
 * This function checks if a specific control setting in a VMX (Virtual Machine Extensions) MSR
 * (Model-Specific Register) is allowed. It refers to Intel SDM (Software Developer's Manual) Appendix A.3, which
 * specifies that a control bit X can be set to 1 only if bit 32+X in the MSR value is 1.
 *
 * @param[in] msr_val VMX MSR value.
 * @param[in] ctrl Bitmask of the control setting to check.
 *
 * @return A boolean value indicating whether the setting is allowed.
 *
 * @retval true The setting is allowed.
 * @retval false The setting is not allowed.
 *
 * @pre N/A
 * @post N/A
 */
static bool is_ctrl_setting_allowed(uint64_t msr_val, uint32_t ctrl)
{
	/*
	 * Intel SDM Appendix A.3
	 * - bit X in ctrl can be set to 1
	 *   only if bit 32+X in msr_val is 1
	 */
	return ((((uint32_t)(msr_val >> 32UL)) & ctrl) == ctrl);
}

/**
 * @brief The interface to check if the platform is APL (Apollo Lake).
 *
 * This function checks if the platform is APL (Apollo Lake) by using the data stored in the global variable
 * `boot_cpu_data`. The APL platform is identified by a DisplayFamily value of 0x6 and a DisplayModel value of 0x5C.
 *
 * @return A boolean value indicating whether the platform is APL.
 *
 * @retval true The platform is APL.
 * @retval false The platform is not APL.
 *
 * @pre N/A
 * @post N/A
 * @remark This API shall be called after `init_pcpu_capabilities` has been called once on the bootstrap processor.
 */
bool is_apl_platform(void)
{
	bool ret = false;

	/* Check if the platform is APL by comparing DisplayFamily and DisplayModel */
	if ((boot_cpu_data.displayfamily == 0x6U) && (boot_cpu_data.displaymodel == 0x5cU)) {
		ret = true;
	}

	return ret;
}

/**
 * @brief The interface to check if the specified CPU Core capability is supported.
 *
 * This function checks if the specified CPU Core capability is supported by using the global variable
 * `cpu_caps.core_caps`. The specified capability is identified by a bitmask.
 *
 * @param[in] bit_mask Bitmask of the specified CPU Core capability.
 *
 * @return A boolean value indicating whether the capability is supported.
 *
 * @retval true The capability is supported.
 * @retval false The capability is not supported.
 *
 * @pre N/A
 * @post N/A
 * @remark This API shall be called after `init_pcpu_capabilities` has been called once on the bootstrap processor.
 */
bool has_core_cap(uint32_t bit_mask)
{
	return ((cpu_caps.core_caps & bit_mask) != 0U);
}

/**
 * @brief The interface to check if CPU enables the \#AC (Alignment Checking) exception for split locked accesses.
 *
 * It uses the data stored in the global variable `boot_cpu_data` to determine if the Split Lock capability is
 * supported, and reads the dedicated MSR (Model-Specific Register) bit to determine if \#AC Split Lock is enabled.
 *
 * @return A boolean value indicating whether \#AC Split Lock is enabled.
 *
 * @retval true \#AC Split Lock is enabled.
 * @retval false \#AC Split Lock is disabled or not supported.
 *
 * @pre N/A
 * @post N/A
 * @remark This API shall be called after `init_pcpu_capabilities` has been called once on the bootstrap processor.
 */
bool is_ac_enabled(void)
{
	bool ac_enabled = false;

	/* Check if the Split Lock capability is supported and if the #AC Split Lock bit is set in the MSR */
	if (has_core_cap(CORE_CAP_SPLIT_LOCK) && ((msr_read(MSR_TEST_CTL) & MSR_TEST_CTL_AC_SPLITLOCK) != 0UL)) {
		ac_enabled = true;
	}

	return ac_enabled;
}

/**
 * @brief The interface to check if CPU enables the \#GP (General Protection) exception for UC (uncacheable) load lock.
 *
 * It uses the data stored in the global variable `cpu_caps` to determine if the UC load lock capability is supported,
 * and reads the dedicated MSR (Model-Specific Register) bit to determine if \#GP exception for UC load lock is
 * enabled.
 *
 * @return A boolean value indicating whether \#GP exception for UC load lock is enabled.
 *
 * @retval true \#GP exception for UC load lock is enabled.
 * @retval false \#GP exception for UC load lock is disabled or not supported.
 *
 * @pre N/A
 * @post N/A
 * @remark This API shall be called after `init_pcpu_capabilities` has been called once on the bootstrap processor.
 */
bool is_gp_enabled(void)
{
	bool gp_enabled = false;

	/* Check if the UC load lock capability is supported and if the #GP UC load Lock bit is set in the MSR */
	if (has_core_cap(CORE_CAP_UC_LOCK) && ((msr_read(MSR_TEST_CTL) & MSR_TEST_CTL_GP_UCLOCK) != 0UL)) {
		gp_enabled = true;
	}

	return gp_enabled;
}

/**
 * @brief The internal function to detect CPU EPT capability.
 *
 * This function reads the control bits in various VMX Model-Specific Registers (MSRs) to detect the EPT (Extended Page
 * Tables) capability. Refer to Chapter 26.6, Vol. 3, SDM 325462-078 to determine the capability.
 * The detected EPT feature is stored in the global variable `cpu_caps.ept_features`.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static void detect_ept_cap(void)
{
	uint64_t msr_val;

	cpu_caps.ept_features = 0U;

	/* Read primary processor based VM control. */
	msr_val = msr_read(MSR_IA32_VMX_PROCBASED_CTLS);

	/*
	 * According to SDM A.3.2 Primary Processor-Based VM-Execution Controls:
	 * The IA32_VMX_PROCBASED_CTLS MSR (index 482H) reports on the allowed
	 * settings of most of the primary processor-based VM-execution controls
	 * (see Section 24.6.2):
	 * Bits 63:32 indicate the allowed 1-settings of these controls.
	 * VM entry allows control X to be 1 if bit 32+X in the MSR is set to 1;
	 * if bit 32+X in the MSR is cleared to 0, VM entry fails if control X
	 * is 1.
	 */
	msr_val = msr_val >> 32U;

	/* Check if secondary processor based VM control is available. */
	if ((msr_val & VMX_PROCBASED_CTLS_SECONDARY) != 0UL) {
		/* Read secondary processor based VM control. */
		msr_val = msr_read(MSR_IA32_VMX_PROCBASED_CTLS2);

		if (is_ctrl_setting_allowed(msr_val, VMX_PROCBASED_CTLS2_EPT)) {
			cpu_caps.ept_features = 1U;
		}
	}
}

/**
 * @brief The internal function to detect CPU VMX APICv capabilities.
 *
 * This function reads the control bits in various VMX Model-Specific Registers (MSRs) to detect APICv (Advanced
 * Programmable Interrupt Controller virtualization) features. The detected features include TPR shadow,
 * Virtualize APIC accesses, Virtualize x2APIC mode, APIC-register virtualization, Virtual-interrupt delivery,
 * and Process posted interrupts. Refer to Chapter A.3, Vol. 3, SDM 325462-078 to determine the capability.
 * The results are stored in the global variable `cpu_caps.apicv_features`.
 * Finally, it sets the callbacks for APICv operations, either for basic mode or advanced mode (based on the detected
 * results).
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static void detect_apicv_cap(void)
{
	uint8_t features = 0U;
	uint64_t msr_val;

	/* Read the MSR_IA32_VMX_PROCBASED_CTLS MSR and check for TPR shadow support */
	msr_val = msr_read(MSR_IA32_VMX_PROCBASED_CTLS);
	if (is_ctrl_setting_allowed(msr_val, VMX_PROCBASED_CTLS_TPR_SHADOW)) {
		features |= VAPIC_FEATURE_TPR_SHADOW;
	}

	/* Check for IPI virtualization support using MSR_IA32_VMX_PROCBASED_CTLS3 */
	if (is_ctrl_setting_allowed(msr_val, VMX_PROCBASED_CTLS_TERTIARY)) {
		if (check_vmx_ctrl_64(MSR_IA32_VMX_PROCBASED_CTLS3, VMX_PROCBASED_CTLS3_IPI_VIRT) ==
				VMX_PROCBASED_CTLS3_IPI_VIRT) {
			features |= VAPIC_FEATURE_IPI_VIRT;
		}
	}

	/* Read the MSR_IA32_VMX_PROCBASED_CTLS2 MSR and check for various APICv features */
	msr_val = msr_read(MSR_IA32_VMX_PROCBASED_CTLS2);
	if (is_ctrl_setting_allowed(msr_val, VMX_PROCBASED_CTLS2_VAPIC)) {
		features |= VAPIC_FEATURE_VIRT_ACCESS;
	}
	if (is_ctrl_setting_allowed(msr_val, VMX_PROCBASED_CTLS2_VX2APIC)) {
		features |= VAPIC_FEATURE_VX2APIC_MODE;
	}
	if (is_ctrl_setting_allowed(msr_val, VMX_PROCBASED_CTLS2_VAPIC_REGS)) {
		features |= VAPIC_FEATURE_VIRT_REG;
	}
	if (is_ctrl_setting_allowed(msr_val, VMX_PROCBASED_CTLS2_VIRQ)) {
		features |= VAPIC_FEATURE_INTR_DELIVERY;
	}

	/* Read the MSR_IA32_VMX_PINBASED_CTLS MSR and check for posted interrupt support */
	msr_val = msr_read(MSR_IA32_VMX_PINBASED_CTLS);
	if (is_ctrl_setting_allowed(msr_val, VMX_PINBASED_CTLS_POST_IRQ)) {
		features |= VAPIC_FEATURE_POST_INTR;
	}

	/* Store the detected features in the cpu_caps.apicv_features variable */
	cpu_caps.apicv_features = features;

	/* Sets the callbacks for APICv operations, either for basic mode or advanced mode (based on the detected results).
	 */
	vlapic_set_apicv_ops();
}

/**
 * @brief The internal function to detect CPU VMX EPT and VPID capability.
 *
 * This function reads the Model-Specific Register (MSR) that contains the EPT (Extended Page Tables) and VPID
 * (Virtual Processor ID) capabilities. The value read from the MSR is then stored in the global variable
 * `cpu_caps.vmx_ept_vpid`.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static void detect_vmx_mmu_cap(void)
{
	/* SDM A.10: Read the EPT and VPID capabilities from the MSR */
	cpu_caps.vmx_ept_vpid = msr_read(MSR_IA32_VMX_EPT_VPID_CAP);
}

/**
 * @brief The internal function to detect if VMX has set 32-bit address width on CPU.
 *
 * This function reads the `MSR_IA32_VMX_BASIC` Model-Specific Register (MSR) and checks the bit that indicates
 * the basic address width. It returns a boolean value to indicate whether the 32-bit address width is set for VMX.
 *
 * @return A boolean value indicating whether VMX 32-bit address width is set.
 *
 * @retval true The 32-bit address width is set.
 * @retval false The 32-bit address width is not set.
 *
 * @pre N/A
 * @post N/A
 */
static bool pcpu_vmx_set_32bit_addr_width(void)
{
	return ((msr_read(MSR_IA32_VMX_BASIC) & MSR_IA32_VMX_BASIC_ADDR_WIDTH) != 0UL);
}

/**
 * @brief The internal function to detect CPU XSAVE capability.
 *
 * This function reads the CPUID sub-leaves related to XSAVE (Extended States Save/Restore) and stores the results
 * in the global variable `boot_cpu_data`. The XSAVE feature set provides mechanisms for saving and restoring the state
 * of extended processor features.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static void detect_xsave_cap(void)
{
	uint32_t unused;

	cpuid_subleaf(CPUID_XSAVE_FEATURES, 0x0U,
		&boot_cpu_data.cpuid_leaves[FEAT_D_0_EAX],
		&unused,
		&unused,
		&boot_cpu_data.cpuid_leaves[FEAT_D_0_EDX]);
	cpuid_subleaf(CPUID_XSAVE_FEATURES, 1U,
		&boot_cpu_data.cpuid_leaves[FEAT_D_1_EAX],
		&unused,
		&boot_cpu_data.cpuid_leaves[FEAT_D_1_ECX],
		&boot_cpu_data.cpuid_leaves[FEAT_D_1_EDX]);
}

/**
 * @brief The internal function to detect CPU Core capability.
 *
 * This function checks if the CPU Core capability feature is supported by examining the global variable
 * `boot_cpu_data`. If the feature is supported, it reads the MSR (Model-Specific Register) of Core capabilities and
 * stores the result in the global variable `cpu_caps.core_caps`.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static void detect_core_caps(void)
{
	if (pcpu_has_cap(X86_FEATURE_CORE_CAP)) {
		cpu_caps.core_caps = (uint32_t)msr_read(MSR_IA32_CORE_CAPABILITIES);
	}
}
/**
 * @brief The internal function to detect CPU Machine-Check Global capability.
 *
 * This function reads the MSR (Model-Specific Register) of Machine-Check Global (MCG) capabilities and stores the
 * result in the global variable `cpu_caps.mcg_caps`.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static void detect_mcg_caps(void)
{
	cpu_caps.mcg_caps = (uint64_t)msr_read(MSR_IA32_MCG_CAP);
}

/**
 * @brief The internal function to detect CPU capabilities.
 *
 * This function invokes a series of sub-functions to detect various CPU capabilities. These capabilities include
 * APICv (Advanced Programmable Interrupt Controller virtualization), EPT (Extended Page Tables), VPID (Virtual
 * Processor ID), XSAVE (Extended States Save/Restore), Core capabilities, and MCG (Machine Check Global) capabilities.
 *
 * The function ensures that all necessary CPU capabilities are detected and initialized for the hypervisor to function
 * correctly.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
static void detect_pcpu_cap(void)
{
	detect_apicv_cap();
	detect_ept_cap();
	detect_vmx_mmu_cap();
	detect_xsave_cap();
	detect_core_caps();
	detect_mcg_caps();
}

/**
 * @brief The internal function to get address bitmask.
 *
 * This function converts a bit number of an address to a page-aligned bitmask. The bitmask is created by shifting 1
 * left by the specified limit, subtracting 1 to set all lower bits, and then applying a page alignment mask.
 *
 * @param[in] limit The bit number of an address.
 *
 * @return A uint64_t value of the page-aligned bitmask.
 *
 * @pre N/A
 * @post N/A
 */
static uint64_t get_address_mask(uint8_t limit)
{
	return ((1UL << limit) - 1UL) & PAGE_MASK;
}

/**
 * @brief The interface initializes the capabilities of the physical CPU (pcpu).
 *
 * This function gathers and initializes various CPU capabilities and features by querying the CPU using the CPUID
 * instruction. The `boot_cpu_data` structure with all members (except `model_name`) defined in the data structure
 * `cpuinfo_x86` are initialized according to the physical platform.
 *
 * The function also calls `detect_pcpu_cap` to detect additional CPU capabilities that are not covered by the CPUID
 * instruction.
 *
 * @return none
 *
 * @pre N/A
 * @post N/A
 */
void init_pcpu_capabilities(void)
{
	uint32_t eax, unused;
	uint32_t family_id, model_id, displayfamily, displaymodel;

	cpuid_subleaf(CPUID_VENDORSTRING, 0x0U,
		&boot_cpu_data.cpuid_level,
		&unused, &unused, &unused);

	cpuid_subleaf(CPUID_FEATURES, 0x0U, &eax, &unused,
		&boot_cpu_data.cpuid_leaves[FEAT_1_ECX],
		&boot_cpu_data.cpuid_leaves[FEAT_1_EDX]);

	/* SDM Vol.2A 3-211 states the algorithm to calculate DisplayFamily and DisplayModel */
	family_id = (eax >> 8U) & 0xfU;
	displayfamily = family_id;
	if (family_id == 0xFU) {
		displayfamily += ((eax >> 20U) & 0xffU);
	}
	boot_cpu_data.displayfamily = (uint8_t)displayfamily;

	model_id = (eax >> 4U) & 0xfU;
	displaymodel = model_id;
	if ((family_id == 0x06U) || (family_id == 0xFU)) {
		displaymodel += ((eax >> 16U) & 0xfU) << 4U;
	}
	boot_cpu_data.displaymodel = (uint8_t)displaymodel;


	cpuid_subleaf(CPUID_EXTEND_FEATURE, 0x0U, &unused,
		&boot_cpu_data.cpuid_leaves[FEAT_7_0_EBX],
		&boot_cpu_data.cpuid_leaves[FEAT_7_0_ECX],
		&boot_cpu_data.cpuid_leaves[FEAT_7_0_EDX]);

	cpuid_subleaf(CPUID_EXTEND_FEATURE, 0x2U, &unused, &unused, &unused,
		&boot_cpu_data.cpuid_leaves[FEAT_7_2_EDX]);

	cpuid_subleaf(CPUID_MAX_EXTENDED_FUNCTION, 0x0U,
		&boot_cpu_data.extended_cpuid_level,
		&unused, &unused, &unused);

	if (boot_cpu_data.extended_cpuid_level >= CPUID_EXTEND_FUNCTION_1) {
		cpuid_subleaf(CPUID_EXTEND_FUNCTION_1, 0x0U, &unused, &unused,
			&boot_cpu_data.cpuid_leaves[FEAT_8000_0001_ECX],
			&boot_cpu_data.cpuid_leaves[FEAT_8000_0001_EDX]);
	}

	if (boot_cpu_data.extended_cpuid_level >= CPUID_EXTEND_INVA_TSC) {
		cpuid_subleaf(CPUID_EXTEND_INVA_TSC, 0x0U, &eax, &unused, &unused,
			&boot_cpu_data.cpuid_leaves[FEAT_8000_0007_EDX]);
	}

	if (boot_cpu_data.extended_cpuid_level >= CPUID_EXTEND_ADDRESS_SIZE) {
		cpuid_subleaf(CPUID_EXTEND_ADDRESS_SIZE, 0x0U, &eax,
			&boot_cpu_data.cpuid_leaves[FEAT_8000_0008_EBX],
			&unused, &unused);

			/* EAX bits 07-00: #Physical Address Bits
			 *     bits 15-08: #Linear Address Bits
			 */
			boot_cpu_data.virt_bits = (uint8_t)((eax >> 8U) & 0xffU);
			boot_cpu_data.phys_bits = (uint8_t)(eax & 0xffU);
			boot_cpu_data.physical_address_mask =
				get_address_mask(boot_cpu_data.phys_bits);
	}

	detect_pcpu_cap();
}

/**
 * @brief The internal function to query if the EPT feature is supported.
 *
 * This function checks the global variable `cpu_caps.ept_features` to determine if the EPT (Extended Page Tables)
 * feature is supported by the CPU. It returns a boolean value indicating the result.
 *
 * @return A boolean value indicating whether the EPT feature is supported.
 *
 * @retval true The EPT feature is supported.
 * @retval false The EPT feature is not supported.
 *
 * @pre N/A
 * @post N/A
 * @remark This API shall be called after `init_pcpu_capabilities` has been called once on the bootstrap processor.
 */
static bool is_ept_supported(void)
{
	return (cpu_caps.ept_features != 0U);
}

/**
 * @brief The internal function to query if APICv basic features are supported.
 *
 * This function checks the APICv (Advanced Programmable Interrupt Controller virtualization) basic feature bits
 * in the global variable `cpu_caps.apicv_features` and returns a boolean value indicating the result.
 *
 * @return A boolean value indicating whether all APICv basic features are supported.
 *
 * @retval true The basic APICv features are supported.
 * @retval false The basic APICv features are not supported.
 *
 * @pre N/A
 * @post N/A
 * @remark This API shall be called after `init_pcpu_capabilities` has been called once on the bootstrap processor.
 */
static inline bool is_apicv_basic_feature_supported(void)
{
	return ((cpu_caps.apicv_features & APICV_BASIC_FEATURE) == APICV_BASIC_FEATURE);
}

/**
 * @brief The interface to query if APICv advanced features are supported.
 *
 * This function checks the APICv (Advanced Programmable Interrupt Controller virtualization) advanced feature bits
 * in the global variable `cpu_caps.apicv_features` and returns a boolean value indicating the result.
 *
 * @return A boolean value indicating whether all APICv advanced features are supported.
 *
 * @retval true The advanced APICv features are supported.
 * @retval false The advanced APICv features are not supported.
 *
 * @pre N/A
 * @post N/A
 * @remark This API shall be called after `init_pcpu_capabilities` has been called once on the bootstrap processor.
 */
bool is_apicv_advanced_feature_supported(void)
{
	return ((cpu_caps.apicv_features & APICV_ADVANCED_FEATURE) == APICV_ADVANCED_FEATURE);
}

/**
 * @brief The interface to query if APICv IPI Virtualization feature is supported.
 *
 * This function checks if the APICv (Advanced Programmable Interrupt Controller virtualization) IPI (Inter-Processor
 * Interrupt) Virtualization feature is supported. It does this by checking the APICv advanced IPI Virtualization
 * feature bit in the global variable `cpu_caps.apicv_features` and the return result of
 * `is_apicv_advanced_feature_supported()`.
 *
 * @return A boolean value indicating whether APICv IPI Virtualization is supported.
 *
 * @retval true The APICv IPI Virtualization feature is supported.
 * @retval false The APICv IPI Virtualization feature is not supported.
 *
 * @pre N/A
 * @post N/A
 * @remark This API shall be called after `init_pcpu_capabilities` has been called once on the bootstrap processor.
 */
bool is_apicv_ipiv_feature_supported(void)
{
	return (is_apicv_advanced_feature_supported() &&
		((cpu_caps.apicv_features & VAPIC_FEATURE_IPI_VIRT) != 0U));
}

/**
 * @brief This interface checks if the specified VMX EPT/VPID capability is supported.
 *
 * This function checks if the specified VMX (Virtual Machine Extensions) EPT (Extended Page Tables) or VPID (Virtual
 * Processor ID) capability is supported by using the data stored in the global variable `cpu_caps.vmx_ept_vpid`.
 *
 * @param[in] bit_mask Bitmask of the specified VMX EPT/VPID capability.
 *
 * @return A boolean value indicating whether the specified capability is supported.
 *
 * @retval true The specified capability is supported.
 * @retval false The specified capability is not supported.
 *
 * @pre N/A
 * @post N/A
 * @remark This API shall be called after `init_pcpu_capabilities` has been called once on the bootstrap processor.
 */
bool pcpu_has_vmx_ept_vpid_cap(uint64_t bit_mask)
{
	return ((cpu_caps.vmx_ept_vpid & bit_mask) != 0U);
}

/**
 * @brief This interface checks if the MCG Corrected Machine Check Interrupt (CMCI) capability is supported.
 *
 * This function checks the bit of the MCG (Machine Check Global) Corrected Machine Check Interrupt (CMCI) capability
 * in the global variable `cpu_caps.mcg_caps` and returns a boolean value to indicate the result.
 *
 * @return A boolean value indicating whether the MCG CMCI capability is supported.
 *
 * @retval true The MCG Corrected Machine Check Interrupt capability is supported.
 * @retval false The MCG Corrected Machine Check Interrupt capability is not supported.
 *
 * @pre  N/A
 * @post N/A
 * @remark This API shall be called after `init_pcpu_capabilities` has been called once on the bootstrap processor.
 */
bool is_cmci_supported(void)
{
	return ((cpu_caps.mcg_caps & MSR_IA32_MCG_CAP_CMCI_P) != 0);
}

/**
 * @brief This interface checks if the MCG Software Error Recovery capability is supported.
 *
 * This function checks the bit of the MCG (Machine Check Global) Software Error Recovery capability in the global
 * variable `cpu_caps.mcg_caps` and returns a boolean value to indicate the result.
 *
 * @return A boolean value indicating whether the MCG Software Error Recovery capability is supported.
 *
 * @retval true The MCG Software Error Recovery capability is supported.
 * @retval false The MCG Software Error Recovery capability is not supported.
 *
 * @pre N/A
 * @post N/A
 * @remark This API shall be called after init_pcpu_capabilities has been called once on the bootstrap processor.
 */
bool is_sw_error_recovery_supported(void)
{
	return ((cpu_caps.mcg_caps & MSR_IA32_MCG_CAP_SER_P) != 0);
}


/**
 * @brief The interface to check if the MCG Local Machine Check Exception capability is supported.
 *
 * This function checks the bit of the MCG (Machine Check Global) Local Machine Check Exception (LMCE) capability
 * in the global variable `cpu_caps.mcg_caps` and returns a boolean value to indicate the result.
 *
 * @return A boolean value indicating whether the MCG Local Machine Check Exception capability is supported.
 *
 * @retval true The MCG Local Machine Check Exception capability is supported.
 * @retval false The MCG Local Machine Check Exception capability is not supported.
 *
 * @pre N/A
 * @post N/A
 * @remark This API shall be called after init_pcpu_capabilities has been called once on the bootstrap processor.
 */
bool is_local_mc_supported(void)
{
	return ((cpu_caps.mcg_caps & MSR_IA32_MCG_CAP_LMCE_P) != 0);
}

/**
 * @brief The interface to get the number of MCG reporting banks.
 *
 * This function returns the number of MCG (Machine Check Global) reporting banks, which is indicated by dedicated bits
 * in the global variable `cpu_caps.mcg_caps`.
 *
 * @return A uint16_t value indicating the number of MCG reporting banks.
 *
 * @pre N/A
 * @post N/A
 * @remark This API shall be called after init_pcpu_capabilities has been called once on the bootstrap processor.
 */
uint16_t mc_bank_count(void)
{
	return (uint16_t)(cpu_caps.mcg_caps & MSR_IA32_MCG_CAP_COUNT);
}

/**
 * @brief The interface to initialize CPU model name information
 *
 * This function detects the CPU model name by querying the CPUID instruction with specific extended function leaves
 * and stores the results into the global variable `boot_cpu_data.model_name`.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
void init_pcpu_model_name(void)
{
	cpuid_subleaf(CPUID_EXTEND_FUNCTION_2, 0x0U,
		(uint32_t *)(boot_cpu_data.model_name),
		(uint32_t *)(&boot_cpu_data.model_name[4]),
		(uint32_t *)(&boot_cpu_data.model_name[8]),
		(uint32_t *)(&boot_cpu_data.model_name[12]));
	cpuid_subleaf(CPUID_EXTEND_FUNCTION_3, 0x0U,
		(uint32_t *)(&boot_cpu_data.model_name[16]),
		(uint32_t *)(&boot_cpu_data.model_name[20]),
		(uint32_t *)(&boot_cpu_data.model_name[24]),
		(uint32_t *)(&boot_cpu_data.model_name[28]));
	cpuid_subleaf(CPUID_EXTEND_FUNCTION_4, 0x0U,
		(uint32_t *)(&boot_cpu_data.model_name[32]),
		(uint32_t *)(&boot_cpu_data.model_name[36]),
		(uint32_t *)(&boot_cpu_data.model_name[40]),
		(uint32_t *)(&boot_cpu_data.model_name[44]));

	boot_cpu_data.model_name[48] = '\0';
}

/**
 * @brief The internal function to query if VMX cannot be enabled and feature control is locked.
 *
 * This function checks if VMX (Virtual Machine Extensions) cannot be enabled and if the feature control is locked by
 * reading the MSR (Model-Specific Register) of feature control.
 *
 * @return A boolean value indicating if VMX is disabled.
 *
 * @retval true VMX cannot be enabled and feature control is locked.
 * @retval false Otherwise.
 *
 * @pre N/A
 * @post N/A
 */
static inline bool is_vmx_disabled(void)
{
	uint64_t msr_val;
	bool ret = false;

	/* Read Feature ControL MSR */
	msr_val = msr_read(MSR_IA32_FEATURE_CONTROL);

	/* Check if feature control is locked and vmx cannot be enabled */
	if (((msr_val & MSR_IA32_FEATURE_CONTROL_LOCK) != 0U) &&
		((msr_val & MSR_IA32_FEATURE_CONTROL_VMX_NO_SMX) == 0U)) {
		ret = true;
	}

	return ret;
}

/**
 * @brief The internal function to query if the CPU supports the unrestricted guest capability.
 *
 * This function checks the bit for the unrestricted guest capability in the MSR (Model-Specific Register) of VMX
 * (Virtual Machine Extensions) MISC and returns a boolean value to indicate the result.
 *
 * @return A boolean value indicating whether the unrestricted guest capability is supported.
 *
 * @retval true The unrestricted guest capability is supported.
 * @retval false The unrestricted guest capability is not supported.
 *
 * @pre N/A
 * @post N/A
 */
static inline bool pcpu_has_vmx_unrestricted_guest_cap(void)
{
	return ((msr_read(MSR_IA32_VMX_MISC) & MSR_IA32_MISC_UNRESTRICTED_GUEST) != 0UL);
}

/**
 * @brief The internal function to check if the CPU supports the required EPT capabilities.
 *
 * This function checks if the required EPT (Extended page table）capabilities are supported by the CPU.
 * It verifies support for the INVEPT instruction, the INVVPID instruction and EPT 2MB large pages by invoking the
 * `pcpu_has_vmx_ept_vpid_cap` function with the appropriate capability flags.
 *
 * @return An int32_t value indicating whether the required EPT capabilities are supported.
 *
 * @retval 0 The required EPT capabilities are supported.
 * @retval -ENODEV The required EPT capabilities are not supported.
 *
 * @pre N/A
 * @post N/A
 * @remark This API shall be called after `init_pcpu_capabilities` has been called once on the bootstrap processor.
 */
static int32_t check_vmx_mmu_cap(void)
{
	int32_t ret = 0;

	if (!pcpu_has_vmx_ept_vpid_cap(VMX_EPT_INVEPT)) {
		printf("%s, invept not supported\n", __func__);
		ret = -ENODEV;
	} else if (!pcpu_has_vmx_ept_vpid_cap(VMX_VPID_INVVPID) ||
		!pcpu_has_vmx_ept_vpid_cap(VMX_VPID_INVVPID_SINGLE_CONTEXT) ||
		!pcpu_has_vmx_ept_vpid_cap(VMX_VPID_INVVPID_GLOBAL_CONTEXT)) {
		printf("%s, invvpid not supported\n", __func__);
		ret = -ENODEV;
	} else if (!pcpu_has_vmx_ept_vpid_cap(VMX_EPT_2MB_PAGE)) {
		printf("%s, ept not support 2MB large page\n", __func__);
		ret = -ENODEV;
	} else {
		/* No other state currently, do nothing */
	}

	return ret;
}

/**
 * @brief The internal function to check if the specified VMX capabilities are supported.
 *
 * This function reads the specified Model-Specific Register (MSR) and checks the specified capability bits as defined
 * in the Intel Software Developer's Manual (SDM). It verifies whether the required VMX (Virtual Machine Extensions)
 * capabilities are supported by the CPU.
 *
 * @param[in]  msr The MSR index to check the specified VMX capabilities.
 * @param[in]  bits The bitmask of VMX capabilities to check.
 *
 * @return A boolean value indicating whether the specified VMX capabilities are all supported.
 *
 * @retval true The VMX capabilities are all supported.
 * @retval false The VMX capabilities are not all supported.
 *
 * @pre N/A
 * @post N/A
 * @remark This API shall be called after init_pcpu_capabilities has been called once on the bootstrap processor.
 */
static bool is_vmx_cap_supported(uint32_t msr, uint32_t bits)
{
	uint64_t vmx_msr;
	uint32_t vmx_msr_low, vmx_msr_high;

	vmx_msr = msr_read(msr);
	vmx_msr_low  = (uint32_t)vmx_msr;
	vmx_msr_high = (uint32_t)(vmx_msr >> 32U);
	/* Bits 31:0 indicate the allowed 0-settings
	 * Bits 63:32 indicate the allowed 1-settings
	 */
	return (((vmx_msr_high & bits) == bits) && ((vmx_msr_low & bits) == 0U));
}

/**
 * @brief The internal function to check essential VMX capabilities required for the hypervisor.
 *
 * This function verifies that the essential VMX (Virtual Machine Extensions) capabilities required for the hypervisor
 * to function correctly are supported by the physical CPU. It checks the following VMX capabilities:
 * 1. The required EPT capabilities.
 * 2. VMX unrestricted guest capability.
 * 3. All capabilities defined in global array vmx_caps[].
 * 4. Ensures that the 32-bit address width is not set for VMX.
 *
 * @return An int32_t value indicating whether all essential VMX capabilities are supported.
 *
 * @retval 0 If all essential VMX capabilities are supported.
 * @retval -ENODEV If any required capability is not supported.
 *
 * @pre N/A
 * @post N/A
 * @remark This API shall be called after init_pcpu_capabilities has been called once on the bootstrap processor.
 */
static int32_t check_essential_vmx_caps(void)
{
	int32_t ret = 0;
	uint32_t i;

	if (check_vmx_mmu_cap() != 0) {
		ret = -ENODEV;
	} else if (!pcpu_has_vmx_unrestricted_guest_cap()) {
		printf("%s, unrestricted guest not supported\n", __func__);
		ret = -ENODEV;
	} else if (pcpu_vmx_set_32bit_addr_width()) {
		printf("%s, Only support Intel 64 architecture.\n", __func__);
		ret = -ENODEV;
	} else {
		for (i = 0U; i < ARRAY_SIZE(vmx_caps); i++) {
			if (!is_vmx_cap_supported(vmx_caps[i].msr, vmx_caps[i].bits)) {
				printf("%s, check MSR[0x%x]:0x%lx bits:0x%x failed\n", __func__,
						vmx_caps[i].msr, msr_read(vmx_caps[i].msr), vmx_caps[i].bits);
				ret = -ENODEV;
				break;
			}
		}
	}

	return ret;
}

/**
 * @brief The function to detect and verify essential CPU hardware support for the hypervisor.
 *
 * This function checks for the presence of various essential hardware features and capabilities
 * required for the hypervisor to function correctly.
 * It checks the following the CPU capabilities and returns an error code if any required feature is not supported:
 * - Long Mode (x86-64, 64-bit support)
 * - Physical and linear address sizes
 * - Physical address width does not exceed the maximum allowed width
 * - Support for 1GB large pages if physical address width is greater than 39 bits
 * - Invariant TSC (Time Stamp Counter)
 * - TSC deadline timer
 * - Execute Disable (NX) feature
 * - Supervisor-Mode Execution Prevention (SMEP)
 * - Supervisor-Mode Access Prevention (SMAP)
 * - Memory Type Range Registers (MTRR)
 * - CLFLUSHOPT instruction
 * - VMX (Virtual Machine Extensions)
 * - Fast string ERMS (Enhanced REP MOVSB/STOSB) support and enabled
 * - Extended Page Tables (EPT)
 * - APICv (Advanced Programmable Interrupt Controller virtualization)
 * - Sufficient CPUID level for required features.
 *   For basic CPUID information, the supported maximum leaf shall be at least 15H.
 * - VMX can be enabled
 * - x2APIC (Extended xAPIC)
 * - POPCNT instruction
 * - SSE (Streaming SIMD Extensions)
 * - RDRAND instruction
 * - Additional VMX capability checks via `check_essential_vmx_caps()`
 *
 * @return An int32_t value indicating whether all required basic CPU hardware capabilities are supported.
 *
 * @retval 0 All essential CPU hardware features are supported.
 * @retval -ENODEV If any required feature is not supported.
 *
 * @pre N/A
 * @post N/A
 * @remark This API shall be called after init_pcpu_capabilities has been called once on the bootstrap processor.
 */
int32_t detect_hardware_support(void)
{
	int32_t ret;

	/* Long Mode (x86-64, 64-bit support) */
	if (!pcpu_has_cap(X86_FEATURE_LM)) {
		printf("%s, LM not supported\n", __func__);
		ret = -ENODEV;
	} else if ((boot_cpu_data.phys_bits == 0U) ||
		(boot_cpu_data.virt_bits == 0U)) {
		printf("%s, can't detect Linear/Physical Address size\n", __func__);
		ret = -ENODEV;
	} else if (boot_cpu_data.phys_bits > MAXIMUM_PA_WIDTH) {
		printf("%s, physical-address width (%d) over maximum physical-address width (%d)\n",
			__func__, boot_cpu_data.phys_bits, MAXIMUM_PA_WIDTH);
		ret = -ENODEV;
	} else if ((boot_cpu_data.phys_bits > 39U) && (!pcpu_has_cap(X86_FEATURE_PAGE1GB) ||
			!pcpu_has_vmx_ept_vpid_cap(VMX_EPT_1GB_PAGE))) {
		printf("%s, physical-address width %d over 39 bits must support 1GB large page\n",
			__func__, boot_cpu_data.phys_bits);
		ret = -ENODEV;
	} else if (!pcpu_has_cap(X86_FEATURE_INVA_TSC)) {
		/* check invariant TSC */
		printf("%s, invariant TSC not supported\n", __func__);
		ret = -ENODEV;
	} else if (!pcpu_has_cap(X86_FEATURE_TSC_DEADLINE)) {
		/* lapic TSC deadline timer */
		printf("%s, TSC deadline not supported\n", __func__);
		ret = -ENODEV;
	} else if (!pcpu_has_cap(X86_FEATURE_NX)) {
		/* Execute Disable */
		printf("%s, NX not supported\n", __func__);
		ret = -ENODEV;
	} else if (!pcpu_has_cap(X86_FEATURE_SMEP)) {
		/* Supervisor-Mode Execution Prevention */
		printf("%s, SMEP not supported\n", __func__);
		ret = -ENODEV;
	} else if (!pcpu_has_cap(X86_FEATURE_SMAP)) {
		/* Supervisor-Mode Access Prevention */
		printf("%s, SMAP not supported\n", __func__);
		ret = -ENODEV;
	} else if (!pcpu_has_cap(X86_FEATURE_MTRR)) {
		printf("%s, MTRR not supported\n", __func__);
		ret = -ENODEV;
	} else if (!pcpu_has_cap(X86_FEATURE_CLFLUSHOPT)) {
		printf("%s, CLFLUSHOPT not supported\n", __func__);
		ret = -ENODEV;
	} else if (!pcpu_has_cap(X86_FEATURE_VMX)) {
		printf("%s, vmx not supported\n", __func__);
		ret = -ENODEV;
	} else if (!is_fast_string_erms_supported_and_enabled()) {
		ret = -ENODEV;
	} else if (!is_ept_supported()) {
		printf("%s, EPT not supported\n", __func__);
		ret = -ENODEV;
	} else if (!is_apicv_basic_feature_supported()) {
		printf("%s, APICV not supported\n", __func__);
		ret = -ENODEV;
	} else if (boot_cpu_data.cpuid_level < 0x15U) {
		printf("%s, required CPU feature not supported\n", __func__);
		ret = -ENODEV;
	} else if (is_vmx_disabled()) {
		printf("%s, VMX can not be enabled\n", __func__);
		ret = -ENODEV;
	} else if (!pcpu_has_cap(X86_FEATURE_X2APIC)) {
		printf("%s, x2APIC not supported\n", __func__);
		ret = -ENODEV;
	} else if (!pcpu_has_cap(X86_FEATURE_POPCNT)) {
		printf("%s, popcnt instruction not supported\n", __func__);
		ret = -ENODEV;
	} else if (!pcpu_has_cap(X86_FEATURE_SSE)) {
		printf("%s, SSE not supported\n", __func__);
		ret = -ENODEV;
	} else if (!pcpu_has_cap(X86_FEATURE_RDRAND)) {
		printf("%s, RDRAND is not supported\n", __func__);
		ret = -ENODEV;
	} else {
		ret = check_essential_vmx_caps();
	}

	return ret;
}

/**
 * @brief The interface to get the CPU information.
 *
 * This function returns a pointer to the data structure containing the CPU information. The data structure is defined
 * as `struct cpuinfo_x86` and is stored in the global variable `boot_cpu_data`.
 *
 * @return A pointer to the global variable boot_cpu_data.
 *
 * @pre N/A
 * @post N/A
 * @remark This API shall be called after `init_pcpu_capabilities` has been called once on the physical bootstrap
 * processor.
 */
struct cpuinfo_x86 *get_pcpu_info(void)
{
	return &boot_cpu_data;
}

/**
 * @}
 */
