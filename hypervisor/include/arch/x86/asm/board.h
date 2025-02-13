/*
 * Copyright (C) 2019-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @defgroup config-data config_data
 * @brief Configuration data module
 *
 * This module contains data structure related to VM configurations.
 *
 * @{
 */

/**
 * @file
 * @brief Header for target board-related data structure definition.
 *
 * This header contains several board-related data structure definition.
 */

#ifndef BOARD_H
#define BOARD_H

#include <types.h>
#include <board_info.h>
#include <asm/host_pm.h>
#include <pci.h>
#include <misc_cfg.h>
#include <asm/rdt.h>
#include <dmi.h>

/* forward declarations */
struct acrn_vm;

/**
 * @brief CLOS configuration union
 *
 * This union records two types of RDT configurations:
 * Memory Bandwidth Allocation delay, and CLoS (CLass of Service)
 * cache capacity bitmask.
 */
union clos_config {
	/**
	 * @brief Memory Bandwidth Allocation delay
	 *
	 * This controls the memory bandwidth allocated to a thread.
	 * Smaller value indicates larger allocated bandwith.
	 */
	uint16_t mba_delay;
	/**
	 * @brief CLass of Service Cache Capacity bitmask
	 *
	 * This bitmask provides a hint to the hardware of the cache space
	 * a thread should use.
	 */
	uint32_t clos_mask;
};

/**
 * @brief DMAR information for target board
 *
 * Information structure for DMAR (DMA Remapping) information on target board.
 */
extern struct dmar_info plat_dmar_info;

#ifdef CONFIG_RDT_ENABLED
/**
 * @brief Resource capability information
 *
 * Information structure on RDT resource capability for target board.
 */
extern struct rdt_type res_cap_info[RDT_NUM_RESOURCES];
#endif

/**
 * @brief CPU frequency limits
 *
 * An array storing CPU frequency limits for all physical CPUs.
 */
extern struct acrn_cpufreq_limits cpufreq_limits[MAX_PCPU_NUM];

/**
 * @brief Hidden physical devices array
 *
 * An array of BDFs of physical devices to be hidden.
 */
extern const union pci_bdf plat_hidden_pdevs[MAX_HIDDEN_PDEVS_NUM];

#ifdef CONFIG_DMI_CHECK_ENABLED
struct smbios_board_info* get_smbios_info_from_board();
#endif

#endif /* BOARD_H */

/**
 * @}
 */
