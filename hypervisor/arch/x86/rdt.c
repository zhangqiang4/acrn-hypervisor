/*
 * Copyright (C) 2020-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <asm/cpu.h>
#include <asm/cpu_caps.h>
#include <asm/cpufeatures.h>
#include <asm/cpuid.h>
#include <errno.h>
#include <logmsg.h>
#include <asm/rdt.h>
#include <asm/lib/bits.h>
#include <asm/board.h>
#include <asm/vm_config.h>
#include <asm/msr.h>
/**
 * @addtogroup hwmgmt_cpu
 *
 * @{
 */
/**
 * @file
 * @brief This file implements APIs to support Intel Resource Director Technology (RDT).
 */

/**
 * @brief Hypervisor Class of Service (CLOS) identifier.
 *
 * This constant variable represents the CLOS identifier used by the hypervisor. It is initialized to 0.
 */
const uint16_t hv_clos = 0U;
/* RDT features can support different numbers of CLOS. Set the lowest numerical
 * clos value (common_num_closids - 1) that is common between the resources as
 * each resource's clos max value to have consistent allocation.
 */
#ifdef CONFIG_RDT_ENABLED
/**
 * @brief Setup RDT CLOS MSRs for a given physical CPU.
 *
 * This function configures the MSRs for the specified RDT resource and CLOS configurations. It handles enabling Code
 * and Data Prioritization (CDP) if required and sets the appropriate MSR values based on the resource type.
 *
 * @param[in] pcpu_id The physical CPU ID on which to set the MSRs.
 * @param[in] info Pointer to the rdt_type structure containing resource type information.
 * @param[in] ins Pointer to the rdt_ins structure containing CLOS configuration array.
 *
 * @return None
 *
 * @pre info != NULL
 * @pre ins != NULL
 * @post N/A
 */
static void setup_res_clos_msr(uint16_t pcpu_id, struct rdt_type *info, struct rdt_ins *ins)
{
	uint16_t i;
	uint32_t msr_index;
	uint64_t val = 0;
	uint32_t res = info->res_id;
	union clos_config *cfg = ins->clos_config_array;

	if ((res != RDT_RESID_MBA) && ins->res.cache.is_cdp_enabled) {
		/* enable CDP before setting COS to simplify CAT mask remapping
		 * and prevent unintended behavior.
		 */
		msr_write(info->msr_qos_cfg, 0x1UL);
	}

	for (i = 0U; i < ins->num_clos_config; i++) {
		switch (res) {
		case RDT_RESID_L3:
		case RDT_RESID_L2:
			val = (uint64_t)cfg[i].clos_mask;
			break;
		case RDT_RESID_MBA:
			val = (uint64_t)cfg[i].mba_delay;
			break;
		default:
			panic("Support only 3 RDT resources. res_id=%d is invalid", res);
			break;
		}
		msr_index = info->msr_base + i;
		msr_write_pcpu(msr_index, val, pcpu_id);
	}
}

/**
 * @brief Set up Class of Service (CLOS) for a given physical CPU.
 *
 * This function configures the CLOS for the specified physical CPU. It iterates through all RDT resources and their
 * instances defined in res_cap_info[], and sets up the CLOS MSRs for each instance that includes the given physical
 * CPU in its CPU mask. Finally, it sets the hypervisor RDT resource CLOS to MSR_IA32_PQR_ASSOC.
 *
 * @param[in] pcpu_id The ID of the physical CPU for which to set up the CLOS.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
void setup_clos(uint16_t pcpu_id)
{
	uint16_t i, j;
	struct rdt_type *info;
	struct rdt_ins *ins;

	for (i = 0U; i < RDT_NUM_RESOURCES; i++) {
		info = &res_cap_info[i];
		for (j = 0U; j < info->num_ins; j++) {
			ins = &info->ins_array[j];
			if (bitmap_test(pcpu_id, &ins->cpu_mask)) {
				setup_res_clos_msr(pcpu_id, info, ins);
			}
		}
	}

	/* set hypervisor RDT resource clos */
	msr_write_pcpu(MSR_IA32_PQR_ASSOC, clos2pqr_msr(hv_clos), pcpu_id);
}

/**
 * @brief Convert CLOS value to PQR MSR format.
 *
 * This function reads the current value of the IA32_PQR_ASSOC MSR, then updates the CLOS portion of the MSR with the
 * provided CLOS value.
 *
 * @param[in] clos The Class of Service (CLOS) value to be converted.
 *
 * @return The updated value of the IA32_PQR_ASSOC MSR with the new CLOS value.
 *
 * @pre N/A
 * @post N/A
 */
uint64_t clos2pqr_msr(uint16_t clos)
{
	uint64_t pqr_assoc;

	pqr_assoc = msr_read(MSR_IA32_PQR_ASSOC);
	pqr_assoc = (pqr_assoc & 0xffffffffUL) | ((uint64_t)clos << 32U);

	return pqr_assoc;
}

/**
 * @brief Check if the RDT type is capable.
 *
 * This function checks if the given RDT type is capable by verifying if any of the RDT instances have a non-zero
 * number of CLOSIDs (Class of Service IDs).
 *
 * @param[in] info Pointer to the rdt_type structure containing RDT information.
 *
 * @return true if the RDT type is capable (i.e., at least one instance has a non-zero number of CLOSIDs), false
 * otherwise.
 *
 * @pre info != NULL
 * @post N/A
 */
static bool is_rdt_type_capable(struct rdt_type *info)
{
	uint32_t i;
	struct rdt_ins *ins;
	bool ret = false;

	if (info->num_ins > 0U) {
		for (i = 0U; i < info->num_ins; i++) {
			ins = &info->ins_array[i];
			if (ins->num_closids > 0U) {
				ret = true;
				break;
			}
		}
	}

	return ret;
}

/**
 * @brief Check if the platform supports Resource Director Technology (RDT).
 *
 * This function uses is_rdt_type_capable() to check if the platform supports any RDT resources, including L3 Cache
 * Allocation Technology (CAT), L2 Cache Allocation Technology (CAT), or Memory Bandwidth Allocation (MBA).
 * The platform's RDT capabilities are defined in the res_cap_info array.
 *
 * @return true if the platform supports any RDT resource, false otherwise.
 *
 * @pre N/A
 * @post N/A
 */
bool is_platform_rdt_capable(void)
{
	bool ret = false;
	uint16_t i;
	/* Check if any of the RDT resources are capable */
	for (i = 0U; i < RDT_NUM_RESOURCES; i++) {
		if (is_rdt_type_capable(&res_cap_info[i])) {
			ret = true;
			break;
		}
	}
	return ret;
}
#else
/**
 * @brief Convert CLOS value to PQR MSR format.
 *
 * @param[in] clos The Class of Service (CLOS) value.
 *
 * @return Always returns 0UL as a placeholder as CONFIG_RDT_ENABLED is not defined.
 *
 * @pre N/A
 * @post N/A
 */
uint64_t clos2pqr_msr(__unused uint16_t clos)
{
	return 0UL;
}

/**
 * @brief Check if the platform supports Resource Director Technology (RDT).
 *
 * @return Always returns false in case as CONFIG_RDT_ENABLED is not defined.
 *
 * @pre N/A
 * @post N/A
 */
bool is_platform_rdt_capable(void)
{
	return false;
}
#endif
/**
 * @}
 */
