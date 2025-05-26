/*
 * Copyright (C) 2018-2025 Intel Corporation.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef VMTRR_H
#define VMTRR_H

#include <board_info.h>
#include <asm/lib/spinlock.h>

/**
 * @defgroup vp-base_vmtrr vp-base.vmtrr
 * @ingroup vp-base
 * @brief MTRR Virtualization
 *
 * This module provides virtualization support for Memory Type Range Registers (MTRRs),
 * which allow the system to specify cacheability attributes (memory types) for
 * different regions of physical memory.
 *
 * @{
 */

/**
 * @file
 * @brief MTRR Virtualization header file
 *
 * This file defines the data structures and APIs for virtualizing Memory Type Range
 * Registers (MTRRs) in the ACRN hypervisor, enabling guest VMs to configure memory
 * caching attributes for different memory regions.
 */

/**
 * @brief Maximum number of variable MTRRs.
 *
 * Refer to Table 2-2. IA-32 Architectural MSRs, Volume 4, SDM 325462-078, there are 10 pairs of Variable MTRR MSRs.
 * This value is set equal to 10.
 */
#define VARIABLE_RANGE_MTRR_NUM_MAX	10UL

/**
 * @brief IA32_MTRR_CAP MSR union.
 *
 * This union defines the IA32_MTRR_CAP MSR structure.
 * Refer to Figure 12-5. IA32_MTRRCAP Register, Volume 3, SDM 325462-078 for more details.
 */
union mtrr_cap_reg {
	/**
	 * @brief Raw 64-bit value of the IA32_MTRR_CAP MSR.
	 *
	 * This field provides access to the complete 64-bit value of this MSR.
	 */
	uint64_t value;
	/**
	 * @brief Bit fields of the IA32_MTRR_CAP MSR.
	 *
	 * This structure provides direct access to the individual bits and fields of the MSR.
	 */
	struct {
		uint64_t vcnt:8;  /**< Number of variable range registers */
		uint64_t fix:1;   /**< Fixed range registers supported */
		uint64_t res0:1;  /**< Reserved */
		uint64_t wc:1;    /**< Write-combining memory type supported */
		uint64_t smrr:1;  /**< SMRR interface supported */
		uint64_t prmrr:1; /**< PRMRR interface supported */
		uint64_t res1:19; /**< Reserved */
		uint64_t res2:32; /**< Reserved */
	} bits;
};

/**
 * @brief IA32_MTRR_DEF_TYPE MSR union.
 *
 * This union defines the IA32_MTRR_DEF_TYPE MSR structure.
 * Refer to Figure 12-6. IA32_MTRR_DEF_TYPE Register, Volume 3, SDM 325462-078 for more details.
 */
union mtrr_def_type_reg {
	/**
	 * @brief Raw 64-bit value of the IA32_MTRR_DEF_TYPE MSR.
	 *
	 * This field provides access to the complete 64-bit value of this MSR.
	 */
	uint64_t value;
	/**
	 * @brief Bit fields of the IA32_MTRR_DEF_TYPE MSR.
	 *
	 * This structure provides direct access to the individual bits and fields of the MSR.
	 */
	struct {
		uint64_t type:8;   /**< Default memory type */
		uint64_t res0:2;   /**< Reserved */
		uint64_t fixed_enable:1;  /**< Fixed-range MTRRs enable/disable */
		uint64_t enable:1; /**< MTRR enable/disable */
		uint64_t res1:20;  /**< Reserved */
		uint64_t res2:32;  /**< Reserved */
	} bits;
};

/**
 * @brief IA32_MTRR_PHYSBASEn MSR union.
 *
 * This union defines the IA32_MTRR_PHYSBASEn MSR structure. Refer to Figure 12-7. IA32_MTRR_PHYSBASEn and
 * IA32_MTRR_PHYSMASKn Variable-Range Register Pair, Volume 3, SDM 325462-078 for more details.
 */
union mtrr_physbase{
	/**
	 * @brief Raw 64-bit value of the IA32_MTRR_PHYSBASEn MSR.
	 *
	 * This field provides access to the complete 64-bit value of this MSR.
	 */
	uint64_t value;
	/**
	 * @brief Bit fields of the IA32_MTRR_PHYSBASEn MSR.
	 *
	 * This structure provides direct access to the individual bits and fields of the MSR.
	 */
	struct {
		uint64_t type:8;  /**< Memory type for range */
		uint64_t res0:4;  /**< Reserved */
		uint64_t physbase:(MAXIMUM_PA_WIDTH - 8 - 4); /**< Base address of range */
		uint64_t res1:(64 - MAXIMUM_PA_WIDTH); /**< Reserved */
	} bits;
};

/**
 * @brief IA32_MTRR_PHYSMASKn MSR union.
 *
 * This union defines the IA32_MTRR_PHYSMASKn MSR structure. Refer to Figure 12-7. IA32_MTRR_PHYSBASEn and
 * IA32_MTRR_PHYSMASKn Variable-Range Register Pair, Volume 3, SDM 325462-078 for more details.
 */
union mtrr_physmask{
	/**
	 * @brief Raw 64-bit value of the IA32_MTRR_PHYSMASKn MSR.
	 *
	 * This field provides access to the complete 64-bit value of this MSR.
	 */
	uint64_t value;
	/**
	 * @brief Bit fields of the IA32_MTRR_PHYSMASKn MSR.
	 *
	 * This structure provides direct access to the individual bits and fields of the MSR.
	 */
	struct {
		uint64_t res0:11;  /**< Reserved */
		uint64_t valid:1;  /**< Whether this range is valid */
		uint64_t physmask:(MAXIMUM_PA_WIDTH - 8 - 4);  /**< Sets range mask */
		uint64_t res1:(64 - MAXIMUM_PA_WIDTH);  /**< Reserved */
	} bits;
};

/**
 * @brief Variable Range MTRR register pair.
 *
 * Each Variable Range MTRR register pair consists of IA32_MTRR_PHYSBASEn and IA32_MTRR_PHYSMASKn MSRs.
 */
struct mtrr_variable_range_reg {
	union mtrr_physbase physbase;  /**< IA32_MTRR_PHYSBASEn MSR */
	union mtrr_physmask physmask;  /**< IA32_MTRR_PHYSMASKn MSR */
};

/**
 * @brief Virtual MTRR data structure.
 *
 * This structure defines the virtual MTRR data structure. It contains the IA32_MTRR_CAP MSR, IA32_MTRR_DEF_TYPE MSR,
 * and an array of IA32_MTRR_PHYSBASEn and IA32_MTRR_PHYSMASKn MSRs.
 */
struct acrn_vmtrr {
	union mtrr_cap_reg cap; /**< IA32_MTRR_CAP MSR */
	union mtrr_def_type_reg def_type; /**< IA32_MTRR_DEF_TYPE MSR */
	/**
	 * @brief IA32_MTRR_PHYSBASEn and IA32_MTRR_PHYSMASKn MSRs
	 */
	struct mtrr_variable_range_reg variable_range[VARIABLE_RANGE_MTRR_NUM_MAX];
	spinlock_t lock; /**< Spinlock to protect variable MTRR structure */
};

struct acrn_vcpu;
struct acrn_vm;

int32_t write_vmtrr(struct acrn_vcpu *vcpu, uint32_t msr, uint64_t value);

uint64_t read_vmtrr(const struct acrn_vcpu *vcpu, uint32_t msr);

void init_vmtrr(struct acrn_vm *vm);
void add_variable_mtrr(struct acrn_vm *vm, uint64_t base, uint64_t size, uint64_t type);

#endif /* VMTRR_H */

/**
 * @}
 */