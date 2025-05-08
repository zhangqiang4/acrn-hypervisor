/*
 * Copyright (C) 2020-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef RDT_H
#define RDT_H
/**
 * @addtogroup hwmgmt_cpu
 *
 * @{
 */
/**
 * @file
 * @brief Defines macros, data structures and APIs related to support Intel Resource Director Technology (RDT).
 */

#define RDT_NUM_RESOURCES	3U /**< Number of RDT resource types. */
#define RDT_RESID_L3    	1U /**< Macro representing the resource ID of L3 Cache Allocation Technology (CAT). */
#define RDT_RESID_L2    	2U /**< Macro representing the resource ID of L2 Cache Allocation Technology (CAT). */
#define RDT_RESID_MBA   	3U /**< Macro representing the resource ID of Memory Bandwidth Allocation (MBA). */

/* External constant representing the hypervisor CLOS (Class of Service). */
extern const uint16_t hv_clos;

/**
 * @brief Structure representing the instance of Resource Director Technology (RDT).
 */
struct rdt_ins {
	union {
		struct  {
			uint32_t bitmask;	/**< A bitmask where each set bit indicates the corresponding cache way may be used by
						     other entities in the platform (e.g., GPU). */
			uint16_t cbm_len;	/**< Length of the capability bitmask. */
			bool is_cdp_enabled;	/**< True if CDP is supported. */
		} cache;			/**< Cache-specific information. */
		struct  {
			uint16_t mba_max;	/**< Maximum MBA delay throttling value supported. */
			bool delay_linear;	/**< True if memory bandwidth delay is in linear scale. */
		} membw;			/**< Memory bandwidth-specific information. */
	} res;					/**< Resource-specific information. */

	uint16_t num_closids;			/**< Number of CLOSIDs available, 0 indicates resource is not
						     supported. */
	uint32_t num_clos_config;		/**< Number of elements in the clos_config_array. */
	union clos_config *clos_config_array;	/**< Pointer to an array of CLOS configuration structures. */
	uint64_t cpu_mask;			/**< Bitmask of CPUs to which this RDT applies. */
};

/**
 * @brief Structure representing the information to support Resource Director Technology (RDT).
 */
struct rdt_type {
	uint32_t res_id;		/**< Resource identifier for the RDT type. Possible values are:
					     * - RDT_RESID_L3
					     * - RDT_RESID_L2
					     * - RDT_RESID_MBA
					*/
	uint32_t msr_qos_cfg;		/**< MSR address for the IA32_L3/L2_QOS_CFG register. */
	uint32_t msr_base;		/**< Base MSR address used to program the CLOS (Class of Service) value. */
	uint32_t num_ins;		/**< Number of element in ins_array */
	struct rdt_ins *ins_array;	/**< Pointer to an array of RDT instance structures. */
};

void setup_clos(uint16_t pcpu_id);
uint64_t clos2pqr_msr(uint16_t clos);
bool is_platform_rdt_capable(void);

#endif	/* RDT_H */
/**
 * @}
 */
