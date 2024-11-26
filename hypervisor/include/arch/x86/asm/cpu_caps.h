/*
 * Copyright (C) 2018-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef CPUINFO_H
#define CPUINFO_H
/**
 * @addtogroup hwmgmt_hw-caps  hwmgmt.hw-caps
 *
 * @{
 */

/**
 * @file
 * @brief Declaration of external APIs for CPU capability management within the hwmgmt.hw-caps module
 *
 * This file declares all external functions, data structures, and macros that shall be provided by the hwmgmt.hw-caps
 * for CPU capability management.
 *
 */

#define MAX_PSTATE	20U	/**< Max num of supported Px state */
#define MAX_CSTATE	8U	/**< Max num of supported Cx state */

/**
 * @brief Max Cx entry
 *
 * We support MAX_CSTATE num of Cx, means have (MAX_CSTATE - 1) Cx entries,
 * i.e. supported Cx entry index range from 1 to MAX_CX_ENTRY.
 */
#define MAX_CX_ENTRY	(MAX_CSTATE - 1U)

/* index list of 'cpuinfo_x86.cpuid_leaves[]' contains value of registers indicating CPUID features */
/**
 * @brief The index of 'cpuinfo_x86.cpuid_leaves[]' contains value of ECX register returned from the CPUID instruction
 * CPUID.1H
 */
#define	FEAT_1_ECX		0U
/**
 * @brief The index of 'cpuinfo_x86.cpuid_leaves[]' contains value of EDX register returned from the CPUID instruction
 * CPUID.1H
 */
#define	FEAT_1_EDX		1U
/**
 * @brief The index of 'cpuinfo_x86.cpuid_leaves[]' contains value of EBX register returned from the CPUID instruction
 * CPUID.(EAX=7H,ECX=0H)
 */
#define	FEAT_7_0_EBX		2U
/**
 * @brief The index of 'cpuinfo_x86.cpuid_leaves[]' contains value of ECX register returned from the CPUID instruction
 * CPUID.(EAX=7H,ECX=0H)
 */
#define	FEAT_7_0_ECX		3U
/**
 * @brief The index of 'cpuinfo_x86.cpuid_leaves[]' contains value of EDX register returned from the CPUID instruction
 * CPUID.(EAX=7H,ECX=0H)
 */
#define	FEAT_7_0_EDX		4U
/**
 * @brief The index of 'cpuinfo_x86.cpuid_leaves[]' contains value of ECX register returned from the CPUID instruction
 * CPUID.80000001H
 */
#define	FEAT_8000_0001_ECX	5U
/**
 * @brief The index of 'cpuinfo_x86.cpuid_leaves[]' contains value of EDX register returned from the CPUID instruction
 * CPUID.80000001H
 */
#define	FEAT_8000_0001_EDX	6U
/**
 * @brief The index of 'cpuinfo_x86.cpuid_leaves[]' contains value of EDX register returned from the CPUID instruction
 * CPUID.80000007H
 */
#define	FEAT_8000_0007_EDX	7U
/**
 * @brief The index of 'cpuinfo_x86.cpuid_leaves[]' contains value of EBX register returned from the CPUID instruction
 * CPUID.80000008H
 */
#define	FEAT_8000_0008_EBX	8U
/**
 * @brief The index of 'cpuinfo_x86.cpuid_leaves[]' contains value of EAX register returned from the CPUID instruction
 * CPUID.(EAX=DH,ECX=0H)
 */
#define	FEAT_D_0_EAX		9U
/**
 * @brief The index of 'cpuinfo_x86.cpuid_leaves[]' contains value of EDX register returned from the CPUID instruction
 * CPUID.(EAX=DH,ECX=0H)
 */
#define	FEAT_D_0_EDX		10U
/**
 * @brief The index of 'cpuinfo_x86.cpuid_leaves[]' contains value of EAX register returned from the CPUID instruction
 * CPUID.(EAX=DH,ECX=1H)
 */
#define	FEAT_D_1_EAX		11U
/**
 * @brief The index of 'cpuinfo_x86.cpuid_leaves[]' contains value of ECX register returned from the CPUID instruction
 * CPUID.(EAX=DH,ECX=1H)
 */
#define	FEAT_D_1_ECX		13U
/**
 * @brief The index of 'cpuinfo_x86.cpuid_leaves[]' contains value of EDX register returned from the CPUID instruction
 * CPUID.(EAX=DH,ECX=1H)
 */
#define	FEAT_D_1_EDX		14U
/**
 * @brief The index of 'cpuinfo_x86.cpuid_leaves[]' contains value of EDX register returned from the CPUID instruction
 * CPUID.(EAX=7H,ECX=2H)
 */
#define	FEAT_7_2_EDX		15U
/**
 * @brief Total numbers of 'cpuinfo_x86.cpuid_leaves[]'
 */
#define	FEATURE_WORDS		16U

/**
 * @brief Data structure containing the CPU information data.
 *
 * This structure is used to store various pieces of information about the CPU, including its family, model, address
 * sizes, CPUID levels, feature details, and model name. It is intended to be used when obtaining and storing CPU
 * information data.
 */
struct cpuinfo_x86 {
	/**
	 * @brief The native processor display family value.
	 *
	 * This value is used to distinguish processor families and processor number series.
	 * It is obtained from the CPUID instruction.
	 */
	uint8_t displayfamily;

	/**
	 * @brief The native processor display model value.
	 *
	 * This value is used to distinguish processor models within a family.
	 * It is obtained from the CPUID instruction.
	 */
	uint8_t displaymodel;

	/**
	 * @brief Linear address size.
	 *
	 * This value represents the number of address bits supported by the CPU for linear addresses.
	 */
	uint8_t virt_bits;

	/**
	 * @brief Physical address size.
	 *
	 * This value represents the number of address bits supported by the CPU for physical addresses.
	 */
	uint8_t phys_bits;

	/**
	 * @brief Maximum input value for basic CPUID information.
	 *
	 * This value represents the maximum input value that can be used with the CPUID instruction to obtain basic CPU
	 * information.
	 */
	uint32_t cpuid_level;

	/**
	 * @brief Maximum input value for extended function CPUID information.
	 *
	 * This value represents the maximum input value that can be used with the CPUID instruction to obtain extended
	 * CPU information.
	 */
	uint32_t extended_cpuid_level;

	/**
	 * @brief Bitmask of physical address mapping to 'phys_bits'.
	 *
	 * This value is a bitmask that maps to the number of physical address bits supported by the CPU.
	 */
	uint64_t physical_address_mask;

	/**
	 * @brief Contains various feature details returned from a CPUID instruction.
	 *
	 * This array stores feature information returned from the CPUID instruction with different contents in the EAX
	 * and ECX registers. Each array element represents specific feature information, with indexes defined by FEAT_*
	 * macros. Refer to the documentation of these macros for details about each element.
	 */
	uint32_t cpuid_leaves[FEATURE_WORDS];

	/**
	 * @brief Model name of the processor.
	 *
	 * This string contains the model name of the processor, as obtained from the CPUID instruction.
	 */
	char model_name[64];
};

bool has_monitor_cap(void);
bool is_apl_platform(void);
bool is_apicv_advanced_feature_supported(void);
bool is_apicv_ipiv_feature_supported(void);
bool pcpu_has_cap(uint32_t bit);
bool pcpu_has_vmx_ept_vpid_cap(uint64_t bit_mask);
bool is_apl_platform(void);
bool has_core_cap(uint32_t bit_mask);
bool is_ac_enabled(void);
bool is_gp_enabled(void);
bool is_cmci_supported(void);
bool is_sw_error_recovery_supported(void);
bool is_local_mc_supported(void);
void init_pcpu_capabilities(void);
void init_pcpu_model_name(void);
int32_t detect_hardware_support(void);
uint16_t mc_bank_count(void);
struct cpuinfo_x86 *get_pcpu_info(void);

/**
 * @brief The bit enumerates whether the CPU supports generating \#AC (Alignment Check) exception when a split lock is
 * detected.
 */
#define CORE_CAP_SPLIT_LOCK    (1U << 5U)

/**
 * @brief The bit enumerates whether the CPU supports generating \#GP (General Protection) exception when a UC load
 * lock is detected.
 */
#define CORE_CAP_UC_LOCK       (1U << 4U)

/**
 * @}
 */

#endif /* CPUINFO_H */
