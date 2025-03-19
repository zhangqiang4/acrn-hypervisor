/*
 * Copyright (c) 1997, Stefan Esser <se@freebsd.org>
 * Copyright (c) 2000, Michael Smith <msmith@freebsd.org>
 * Copyright (c) 2011 NetApp, Inc.
 * Copyright (c) 2018-2024 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 * Only support Type 0 and Type 1 PCI(e) device. Remove PC-Card type support.
 *
 */
#include <types.h>
#include <asm/lib/spinlock.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <pci.h>
#include <uart16550.h>
#include <logmsg.h>
#include <asm/pci_dev.h>
#include <asm/vtd.h>
#include <asm/lib/bits.h>
#include <asm/board.h>
#include <platform_acpi_info.h>
#include <hash.h>
#include <util.h>

/**
 * @defgroup hwmgmt_pci hwmgmt.pci
 * @ingroup hwmgmt
 * @brief Implementation of PCI related functions.
 *
 * This module provides the interfaces to interact with physical PCI devices. These operations are essential
 * to handle physical PCI devices.
 *
 * @{
 */

/**
 * @file
 * @brief Functions to operate PCI device
 *
 * This file implements functions to operate PCI device configuration space. These operations involve
 * PCI hierarchy scanning, configuration space registers read/write, iommu related operations and so on.
 */

#define PDEV_HLIST_HASHBITS 6U    /**< Bits used to create the key of hash list */
#define PDEV_HLIST_HASHSIZE (1U << PDEV_HLIST_HASHBITS)    /**< The size of hash list */

/**
 * @brief Number of PCI devices
 */
static uint32_t num_pci_pdev;
/**
 * @brief List of PCI devices
 *
 * This is a global list used to manage all initialized PCI devices.
 */
static struct pci_pdev pci_pdevs[CONFIG_MAX_PCI_DEV_NUM];
/**
 * @brief Hash list of PCI devices
 *
 * This is a global hash list used to quickly search a PCI device.
 */
static struct hlist_head pdevs_hlist_heads[PDEV_HLIST_HASHSIZE];

/**
 * @brief Number of PCI devices owned by hypervisor
 */
static uint32_t num_hv_owned_pci_pdev;
/**
 * @brief List of PCI devices
 *
 * This is a global list used to manage all hypervisor owned PCI devices, i.e. PCI bridge
 * or PCI debug uart device.
 */
static struct pci_pdev *hv_owned_pci_pdevs[CONFIG_MAX_PCI_DEV_NUM];

/**
 * @brief Get the number of physical PCI devices owned by hypervisor
 *
 * @return The number of physical PCI devices owned by hypervisor
 *
 * @remark N/A
 */
uint32_t get_hv_owned_pdev_num(void)
{
	return num_hv_owned_pci_pdev;
}

/**
 * @brief Get the list of physical PCI devices owned by hypervisor
 *
 * @return A pointer to indicate a list of physical PCI devices owned by hypervisor
 *
 * @remark N/A
 */
const struct pci_pdev **get_hv_owned_pdevs(void)
{
	return (const struct pci_pdev **)hv_owned_pci_pdevs;
}

/**
 * @brief Instance of data structure for PCI memory mapped configuration space (MMCFG) region
 *
 * The address indicates the MMCFG Base Address. The start_bus indicates the start bus number.
 * The end_bus indicates the end bus number.
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
static struct pci_mmcfg_region phys_pci_mmcfg = {
	.address = DEFAULT_PCI_MMCFG_BASE,
	.start_bus = DEFAULT_PCI_MMCFG_START_BUS,
	.end_bus = DEFAULT_PCI_MMCFG_END_BUS,
};

#ifdef CONFIG_ACPI_PARSE_ENABLED
/**
 * @brief Set the instance of data structure for MMCFG region
 *
 * @param[in] region Pointer to a data structure for MMCFG region.
 *
 * @return None
 *
 * @pre region != NULL
 *
 * @remark The function is valid when CONFIG_ACPI_PARSE_ENABLED is enabled
 */
void set_mmcfg_region(struct pci_mmcfg_region *region)
{
	phys_pci_mmcfg = *region;
}
#endif

/**
 * @brief Get the instance of data structure for MMCFG region
 *
 * @return A pointer to the instance of data structure for MMCFG region
 *
 */
struct pci_mmcfg_region *get_mmcfg_region(void)
{
	return &phys_pci_mmcfg;
}

#if defined(HV_DEBUG)
/**
 * @brief Calculate CONFIG_ADDRESS Register Vaule using PIO method
 *
 * Refer to Chapter 3.2.2.3.2 Software Generation of Configuration Transactions,
 * PCI Local Bus Specification Revision 3.0.
 *
 * @param[in] bdf    The data structure of PCI device BDF.
 * @param[in] offset The register offset in configuration space.
 *
 * @return An unsigned integer which indicates the CONFIG_ADDRESS Register Vaule
 *
 * @remark The function is valid when hypervisor debug mode is enabled
 */
static inline uint32_t pio_off_to_address(union pci_bdf bdf, uint32_t offset)

{
	uint32_t addr = (uint32_t)bdf.value;

	addr <<= 8U;
	addr |= (offset | PCI_CFG_ENABLE);
	return addr;
}

/**
 * @brief Read configuration space register value using PIO method
 *
 * Refer to Chapter 3.2.2.3.2 Software Generation of Configuration Transactions,
 * PCI Local Bus Specification Revision 3.0.
 *
 * @param[in] bdf    The data structure of PCI device BDF.
 * @param[in] offset The register offset in configuration space.
 * @param[in] bytes  The size to read.
 *
 * @return An unsigned integer which indicates the value read out from the register
 *
 * @remark The function is valid when hypervisor debug mode is enabled
 */
static uint32_t pci_pio_read_cfg(union pci_bdf bdf, uint32_t offset, uint32_t bytes)
{
	uint32_t addr;
	uint32_t val;

	addr = pio_off_to_address(bdf, offset);

	/* Write address to ADDRESS register */
	pio_write32(addr, (uint16_t)PCI_CONFIG_ADDR);

	/* Read result from DATA register */
	switch (bytes) {
	case 1U:
		val = (uint32_t)pio_read8((uint16_t)PCI_CONFIG_DATA + ((uint16_t)offset & 3U));
		break;
	case 2U:
		val = (uint32_t)pio_read16((uint16_t)PCI_CONFIG_DATA + ((uint16_t)offset & 2U));
		break;
	default:
		val = pio_read32((uint16_t)PCI_CONFIG_DATA);
		break;
	}

	return val;
}

/**
 * @brief Write configuration space register using PIO method
 *
 * Refer to Chapter 3.2.2.3.2 Software Generation of Configuration Transactions,
 * PCI Local Bus Specification Revision 3.0.
 *
 * @param[in] bdf    The data structure of PCI device BDF.
 * @param[in] offset The register offset in configuration space.
 * @param[in] bytes  The size to write.
 * @param[in] val    The value to write.
 *
 * @return None
 *
 * @remark The function is valid when hypervisor debug mode is enabled
 */
static void pci_pio_write_cfg(union pci_bdf bdf, uint32_t offset, uint32_t bytes, uint32_t val)
{
	uint32_t addr = pio_off_to_address(bdf, offset);

	/* Write address to ADDRESS register */
	pio_write32(addr, (uint16_t)PCI_CONFIG_ADDR);

	/* Write value to DATA register */
	switch (bytes) {
	case 1U:
		pio_write8((uint8_t)val, (uint16_t)PCI_CONFIG_DATA + ((uint16_t)offset & 3U));
		break;
	case 2U:
		pio_write16((uint16_t)val, (uint16_t)PCI_CONFIG_DATA + ((uint16_t)offset & 2U));
		break;
	default:
		pio_write32(val, (uint16_t)PCI_CONFIG_DATA);
		break;
	}
}
#else
/**
 * @brief Read configuration space register value using PIO method
 *
 * The function returns 0xFFFFFFFF directly because the PIO method is not supported in release mode.
 * Under such scenario, ECAM (MMIO) method should be used.
 *
 * @param[in] bdf    The data structure of PCI device BDF.
 * @param[in] offset The register offset in configuration space.
 * @param[in] bytes  The size to read.
 *
 * @return An unsigned integer which indicates the value read out from the register
 *
 * @remark PIO method is not valid when release mode is enabled
 */
static uint32_t pci_pio_read_cfg(__unused union pci_bdf bdf,
		__unused uint32_t offset, __unused uint32_t bytes)
{
	return ~0U;
}

/**
 * @brief Write configuration space register using PIO method
 *
 * The function does nothing because the PIO method is not supported in release mode.
 * Under such scenario, MMIO method should be used.
 *
 * @param[in] bdf    The data structure of PCI device BDF.
 * @param[in] offset The register offset in configuration space.
 * @param[in] bytes  The size to write.
 * @param[in] val    The value to write.
 *
 * @return None
 *
 * @remark PIO method is not valid when release mode is enabled
 */
static void pci_pio_write_cfg(__unused union pci_bdf bdf,
		__unused uint32_t offset, __unused uint32_t bytes, __unused uint32_t val)
{
}
#endif

/**
 * @brief Instance of data structure for PIO operations
 *
 * The PCI configuration space read and write callback functions are registered.
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
static const struct pci_cfg_ops pci_pio_cfg_ops = {
	.pci_read_cfg = pci_pio_read_cfg,
	.pci_write_cfg = pci_pio_write_cfg,
};

/**
 * @brief Calculate register address using ECAM method
 *
 * Refer to Chapter 7.2.2 PCI Express Enhanced Configuration Access Mechanism (ECAM),
 * PCI Express® Base Specification Revision 5.0.
 *
 * @param[in] bdf    The data structure of PCI device BDF.
 * @param[in] offset The register offset in configuration space.
 *
 * @return An unsigned integer which indicates the MMCFG address of the register
 *
 * @pre offset < 0x1000U
 * @pre phys_pci_mmcfg.address % 4096 == 0
 */
static inline uint32_t mmcfg_off_to_address(union pci_bdf bdf, uint32_t offset)
{
	return (uint32_t)phys_pci_mmcfg.address + (((uint32_t)bdf.value << 12U) | offset);
}

/**
 * @brief Read configuration space register value using ECAM method
 *
 * Refer to Chapter 7.2.2 PCI Express Enhanced Configuration Access Mechanism (ECAM),
 * PCI Express® Base Specification Revision 5.0.
 *
 * @param[in] bdf    The data structure of PCI device BDF.
 * @param[in] offset The register offset in configuration space.
 * @param[in] bytes  The size to read.
 *
 * @return An unsigned integer which indicates the value read out from the register
 *
 * @pre bytes == 1U || bytes == 2U || bytes == 4U
 * @pre pci_is_valid_access(offset, bytes) == true
 */
static uint32_t pci_mmcfg_read_cfg(union pci_bdf bdf, uint32_t offset, uint32_t bytes)
{
	uint32_t addr = mmcfg_off_to_address(bdf, offset);
	void *hva = hpa2hva(addr);

	ASSERT(pci_is_valid_access(offset, bytes), "the offset should be aligned with 2/4 byte\n");

	return (uint32_t)mmio_read(hva, bytes);
}

/**
 * @brief Write configuration space register using ECAM method
 *
 * Refer to Chapter 7.2.2 PCI Express Enhanced Configuration Access Mechanism (ECAM),
 * PCI Express® Base Specification Revision 5.0.
 *
 * @param[in] bdf    The data structure of PCI device BDF.
 * @param[in] offset The register offset in configuration space.
 * @param[in] bytes  The size to write.
 * @param[in] val    Value to write.
 *
 * @return None
 *
 * @pre bytes == 1U || bytes == 2U || bytes == 4U
 * @pre pci_is_valid_access(offset, bytes) == true
 */
static void pci_mmcfg_write_cfg(union pci_bdf bdf, uint32_t offset, uint32_t bytes, uint32_t val)
{
	uint32_t addr = mmcfg_off_to_address(bdf, offset);
	void *hva = hpa2hva(addr);

	ASSERT(pci_is_valid_access(offset, bytes), "the offset should be aligned with 2/4 byte\n");

	mmio_write(hva, bytes, val);
}

/**
 * @brief Instance of data structure for ECAM operations
 *
 * The PCI configuration space read and write callback functions are registered.
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
static const struct pci_cfg_ops pci_mmcfg_cfg_ops = {
	.pci_read_cfg = pci_mmcfg_read_cfg,
	.pci_write_cfg = pci_mmcfg_write_cfg,
};

/**
 * @brief Global variable to hold instance of data structure for PIO or ECAM operations
 */
static const struct pci_cfg_ops *acrn_pci_cfg_ops = &pci_pio_cfg_ops;

/**
 * @brief Switch PCI configuration space operation functions to ECAM (MMIO)
 *
 * The default PCI configuration space operation functions are PIO functions. This function
 * is used to switch them to ECAM (MMIO) functions.
 *
 * @return None
 */
void pci_switch_to_mmio_cfg_ops(void)
{
	acrn_pci_cfg_ops = &pci_mmcfg_cfg_ops;
}

/**
 * @brief Read configuration space register value
 *
 * This is the interface function. It calls registered PCI configuration space operation functions
 * to read register.
 *
 * @param[in] bdf    The data structure of PCI device BDF.
 * @param[in] offset The register offset in configuration space.
 * @param[in] bytes  The size to read.
 *
 * @return An unsigned integer which indicates the value read out from the register
 *
 * @pre bytes == 1U || bytes == 2U || bytes == 4U
 */
uint32_t pci_pdev_read_cfg(union pci_bdf bdf, uint32_t offset, uint32_t bytes)
{
	uint32_t val = ~0U;

	val = acrn_pci_cfg_ops->pci_read_cfg(bdf, offset, bytes);

	return val;
}

/**
 * @brief Write configuration space register
 *
 * This is the interface function. It calls registered PCI configuration space operation functions
 * to write register.
 *
 * @param[in] bdf    The data structure of PCI device BDF.
 * @param[in] offset The register offset in configuration space.
 * @param[in] bytes  The size to write.
 * @param[in] val    The value to write.
 *
 * @return None
 *
 * @pre bytes == 1U || bytes == 2U || bytes == 4U
 */
void pci_pdev_write_cfg(union pci_bdf bdf, uint32_t offset, uint32_t bytes, uint32_t val)
{
	acrn_pci_cfg_ops->pci_write_cfg(bdf, offset, bytes, val);
}

/**
 * @brief Check whether to restore the BARs value to physical device registers
 *
 * This function checks if the values of physical device's current BAR registers equal to
 * the ones saved when initializing the physical device. If they don't equal, return true.
 * Otherwise, return false.
 *
 * @param[in] pdev Pointer to the data structure of PCI physical device.
 *
 * @return A boolean value to indicate whether to restore the BARs value
 *
 * @retval TRUE  Restore the BARs value
 * @retval FALSE Do not restore the BARs value
 *
 * @pre pdev != NULL
 */
bool pdev_need_bar_restore(const struct pci_pdev *pdev)
{
	bool need_restore = false;
	uint32_t idx, bar;

	for (idx = 0U; idx < pdev->nr_bars; idx++) {
		bar = pci_pdev_read_cfg(pdev->bdf, pci_bar_offset(idx), 4U);
		if (bar != pdev->bars[idx].phy_bar) {
			need_restore = true;
			break;
		}
	}

	return need_restore;
}

/**
 * @brief Get the BAR resources from physical device
 *
 * This function gets the original BAR register value by reading physical device's BAR
 * register. Then, it writes 0xFFFFFFFF to the BAR register to get the BAR size. After
 * getting the BAR resources, the function restores the BAR register.
 *
 * @param[in]    bdf    The data structure of PCI device BDF.
 * @param[in]    offset The BAR register offset in configuration space.
 * @param[inout] res    Pointer to the data structure of PCI BAR resource.
 *
 * @return None
 *
 * @pre res != NULL
 */
static void get_pci_bar_resource(union pci_bdf bdf, uint32_t offset, struct pci_bar_resource *res)
{
       res->phy_bar = pci_pdev_read_cfg(bdf, offset, 4U);

       pci_pdev_write_cfg(bdf, offset, 4U, ~0U);
       res->size_mask = pci_pdev_read_cfg(bdf, offset, 4U);
       pci_pdev_write_cfg(bdf, offset, 4U, res->phy_bar);
}

/**
 * @brief Save the BAR resources from physical device
 *
 * This function iterates the physical device's BAR registers and saves the BAR resources.
 *
 * @param[in] pdev Pointer to the data structure of PCI physical device.
 *
 * @return None
 *
 * @pre pdev != NULL
 */
static inline void pdev_save_bar(struct pci_pdev *pdev)
{
	uint32_t idx;

	for (idx = 0U; idx < pdev->nr_bars; idx++) {
		get_pci_bar_resource(pdev->bdf, pci_bar_offset(idx), &pdev->bars[idx]);
	}
}

/**
 * @brief Restore the saved BAR registers and VF BARs registers values to physical device
 *
 * This function iterates the physical device's BAR registers and restores the BAR registers
 * values to physical device. The VF BAR is restored too if there is SRIOV capability.
 *
 * @param[in] pdev Pointer to the data structure of PCI physical device.
 *
 * @return None
 *
 * @pre pdev != NULL
 */
void pdev_restore_bar(const struct pci_pdev *pdev)
{
	uint32_t idx;

	for (idx = 0U; idx < pdev->nr_bars; idx++) {
		pci_pdev_write_cfg(pdev->bdf, pci_bar_offset(idx), 4U, pdev->bars[idx].phy_bar);
	}

	if (pdev->sriov.capoff != 0) {
		for (idx = 0U; idx < PCI_STD_NUM_BARS; idx++) {
			pci_pdev_write_cfg(pdev->bdf, pdev->sriov.capoff + PCIR_SRIOV_VF_BAR_OFF + idx * 4,
					4U, pdev->vf_bars[idx]);
		}
	}
}

/**
 * @brief Find the physical device through BDF
 *
 * This function iterates a global physical device list to find a device which BDF equals to
 * input BDF.
 *
 * @param[in] pbdf An unsigned integer which indicates the BDF of a device to find.
 *
 * @return A pointer to the data structure of PCI physical device
 *
 * @retval non-NULL A pointer to the data structure of PCI physical device
 * @retval NULL     No device is found
 */
static const struct pci_pdev *pci_find_pdev(uint16_t pbdf)
{
	struct hlist_node *n;
	const struct pci_pdev *found = NULL, *tmp;

	hlist_for_each (n, &pdevs_hlist_heads[hash64(pbdf, PDEV_HLIST_HASHBITS)]) {
		tmp = hlist_entry(n, struct pci_pdev, link);
		if (pbdf == tmp->bdf.value) {
			found = tmp;
			break;
		}
	}
	return found;
}

/**
 * @brief Find the DRHD index corresponding to a PCI physical device
 *
 * Run through a global physical device list to find a device which BDF equals to
 * the input one. Then, return the value in drhd_idx member from PCI physical device
 * data structure.
 *
 * @param[in] pbdf An unsigned integer which indicates the BDF of a device to find.
 *
 * @return An unsigned integer which indicates the DRHD index of a PCI device
 *
 * @retval drhd_index         The DRHD index corresponding to a PCI device found through input BDF
 * @retval INVALID_DRHD_INDEX No device found through the input BDF
 */
uint32_t pci_lookup_drhd_for_pbdf(uint16_t pbdf)
{
	const struct pci_pdev *pdev = pci_find_pdev(pbdf);
	return (pdev != NULL) ? pdev->drhd_index : INVALID_DRHD_INDEX;
}

/**
 * @brief Enable or disable the INTx of a PCI physical device
 *
 * @param[in] bdf    An unsigned integer which indicates the BDF of a device.
 * @param[in] enable A boolean to indicate to enable or disable the INTx of a device.
 *
 * @return None
 */
void enable_disable_pci_intx(union pci_bdf bdf, bool enable)
{
	uint32_t cmd, new_cmd;

	/* Set or clear the INTXDIS bit in COMMAND register */
	cmd = pci_pdev_read_cfg(bdf, PCIR_COMMAND, 2U);
	if (enable) {
		new_cmd = cmd & ~PCIM_CMD_INTxDIS;
	} else {
		new_cmd = cmd | PCIM_CMD_INTxDIS;
	}

	if ((cmd ^ new_cmd) != 0U) {
		pci_pdev_write_cfg(bdf, PCIR_COMMAND, 0x2U, new_cmd);
	}
}

/**
 * @brief Check if a PCI physical device is a platform hidden PCI device
 *
 * Run through a global hidden device list to find a device which BDF equals to
 * the input one. If the device can be found, that means this device is hidden by the
 * platform.
 *
 * @param[in] bdf An unsigned integer which indicates the BDF of a device to find.
 *
 * @return A boolean value which indicates whether the PCI device is hidden
 *
 * @retval TRUE  The device is hidden
 * @retval FALSE The device is not hidden
 */
#if (MAX_HIDDEN_PDEVS_NUM > 0)
bool is_plat_hidden_pdev(union pci_bdf bdf)
{
	static uint32_t hidden_pdevs_num = MAX_HIDDEN_PDEVS_NUM;
	uint32_t hidden_idx;
	bool hidden = false;

	for (hidden_idx = 0U; hidden_idx < hidden_pdevs_num; hidden_idx++) {
		if (bdf_is_equal(plat_hidden_pdevs[hidden_idx], bdf)) {
			hidden = true;
			break;
		}
	}

	return hidden;
}
#else
bool is_plat_hidden_pdev(__unused union pci_bdf bdf)
{
	return false;
}
#endif

/**
 * @brief Check if a PCI physical device is owned by hypervisor
 *
 * Run through a global hypervisor owned device list to find a device which BDF equals
 * to the input one. If the device can be found, that means this device is owned by the
 * hypervisor, e.g. PCI bridge.
 *
 * @param[in] pbdf An unsigned integer which indicates the BDF of a device to find.
 *
 * @return A boolean value which indicates whether the PCI device is owned by the hypervisor
 *
 * @retval TRUE  The device is owned by the hypervisor
 * @retval FALSE The device is not owned by the hypervisor
 */
bool is_hv_owned_pdev(union pci_bdf pbdf)
{
	bool ret = false;
	uint32_t i;

	for (i = 0U; i < num_hv_owned_pci_pdev; i++) {
		if (bdf_is_equal(pbdf, hv_owned_pci_pdevs[i]->bdf)) {
			pr_info("hv owned dev: (%x:%x:%x)", pbdf.bits.b, pbdf.bits.d, pbdf.bits.f);
			ret = true;
			break;
		}
	}
	return ret;
}

#define BUSES_BITMAP_LEN ((PCI_BUSMAX + 1U) >> 6U)  /**< Quantity of uint64_t to encode a bitmap of all bus values */

/**
 * @brief MAX Device Scope Entries of PCI Endpoint Device and PCI Sub-hierarchy in all DRDH in DMAR ACPI Table
 */
#define BDF_MAPPING_NUM 32U

/**
 * @brief Data structure to record the PCI device mapping to DRHD
 *
 * The data structure is used to record the PCI device BDF and the device corresponding
 * DRHD index. The device is within the device scope of a DRHD.
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
struct pci_bdf_to_drhd_index_mapping {
	union pci_bdf dev_scope_bdf;  /**< BDF of the device which is within DRHD device scope */
	uint32_t dev_scope_drhd_index;  /**< The corresponding DRHD index */
};

/**
 * @brief Data structure to record the DRHD mapping relationship of a group of PCI devices
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
struct pci_bdf_mapping_group {
	uint32_t pci_bdf_map_count;  /**< Count of the device which is within DRHD device scope */
	/**
	 * @brief List of devices which are within DRHD device scope
	 */
	struct pci_bdf_to_drhd_index_mapping bdf_map[BDF_MAPPING_NUM];
};

/**
 * @brief Data structure to record the PCI bus and its corresponding DRHD
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
struct pci_bus_num_to_drhd_index_mapping {
	uint8_t bus_under_scan;  /**< PCI bus number which is being scanned */
	uint32_t bus_drhd_index;  /**< The PCI bus corresponding DRHD index */
};

/**
 * @brief Get DRHD index of a PCI physical device
 *
 * Run through an input device list which belongs to DRHD to find a device which BDF
 * equals to the input one. If the device can be found, that means this device is owned
 * within the DRHD device scope. Then, return the DRHD index back. Otherwise, return the
 * input DRHD index.
 *
 * @param[in] pbdf               An unsigned integer which indicates the BDF of a device to find.
 * @param[in] bdfs_from_drhds    A pointer to a device list which belongs to DRHD.
 * @param[in] current_drhd_index Index of current DRHD to check.
 *
 * @return Index of DRHD that the PCI device belongs to
 *
 * @pre bdfs_from_drhds != NULL
 */
static uint32_t pci_check_override_drhd_index(union pci_bdf pbdf,
		const struct pci_bdf_mapping_group *const bdfs_from_drhds,
		uint32_t current_drhd_index)
{
	uint32_t bdfi;
	uint32_t bdf_drhd_index = current_drhd_index;

	for (bdfi = 0U; bdfi < bdfs_from_drhds->pci_bdf_map_count; bdfi++) {
		if (bdfs_from_drhds->bdf_map[bdfi].dev_scope_bdf.value == pbdf.value) {
			/*
			 * Override current_drhd_index
			 */
			bdf_drhd_index =
				bdfs_from_drhds->bdf_map[bdfi].dev_scope_drhd_index;
		}
	}

	return bdf_drhd_index;
}

/**
 * @brief Scan PCI physical devices from specific bus
 *
 * Walk through the bus to scan PCI physical devices and using the discovered physical devices
 * to initialize PCI physical device data structure instances, each instance can only provide
 * a physical device information (like bdf, bar, capabilities). Before using this instance, it
 * needs to use the instance to initialize a per VM device configuration (acrn_vm_pci_dev_config),
 * call init_one_dev_config or init_all_dev_config to do this.
 *
 * @param[in]    bus             Bus number to scan.
 * @param[inout] buses_visited   Bus bitmap array to record which bus has been visited.
 * @param[in]    bdfs_from_drhds A pointer to a device list which belongs to DRHD.
 * @param[in]    drhd_index      DRHD index.
 *
 * @return None
 *
 * @pre bdfs_from_drhds != NULL
 */
static void scan_pci_hierarchy(uint8_t bus, uint64_t buses_visited[BUSES_BITMAP_LEN],
		const struct pci_bdf_mapping_group *const bdfs_from_drhds, uint32_t drhd_index)
{
	/*
	 * TODO
	 * Refine the scan logic into:
	 * 1. Scan devices under bus 0
	 * 2. If there is a bridge, scan the bridge instead.
	 * 3. Record the parent for a child device
	 */
	uint32_t vendor;
	uint8_t hdr_type, dev, func, func_max;
	union pci_bdf pbdf;
	uint8_t current_bus_index;
	uint32_t current_drhd_index, bdf_drhd_index;
	struct pci_pdev *pdev;

	struct pci_bus_num_to_drhd_index_mapping bus_map[PCI_BUSMAX + 1U]; /* FIFO queue of buses to walk */
	uint32_t s = 0U, e = 0U; /* start and end index into queue */

	bus_map[e].bus_under_scan = bus;
	bus_map[e].bus_drhd_index = drhd_index;
	e = e + 1U;
	while (s < e) {
		current_bus_index = bus_map[s].bus_under_scan;
		current_drhd_index = bus_map[s].bus_drhd_index;
		s = s + 1U;

		bitmap_set_nolock(current_bus_index,
					&buses_visited[current_bus_index >> 6U]);

		pbdf.bits.b = current_bus_index;
		if ((pbdf.bits.b < phys_pci_mmcfg.start_bus) || (pbdf.bits.b > phys_pci_mmcfg.end_bus)) {
			continue;
		}

		for (dev = 0U; dev <= PCI_SLOTMAX; dev++) {
			pbdf.bits.d = dev;
			pbdf.bits.f = 0U;
			hdr_type = (uint8_t)pci_pdev_read_cfg(pbdf, PCIR_HDRTYPE, 1U);

			/* Do not probe beyond function 0 if not a multi-function device
			 * unless device supports ARI or SR-IOV (PCIe spec r5.0 §7.5.1.1.9)
			 */
			func_max = is_pci_cfg_multifunction(hdr_type) ? PCI_FUNCMAX : 0U;
			for (func = 0U; func <= func_max; func++) {

				pbdf.bits.f = func;

				vendor = pci_pdev_read_cfg(pbdf, PCIR_VENDOR, 2U);

				if (!is_pci_vendor_valid(vendor)) {
					continue;
				}

				bdf_drhd_index = pci_check_override_drhd_index(pbdf, bdfs_from_drhds,
									current_drhd_index);
				pdev = pci_init_pdev(pbdf, bdf_drhd_index);
				/* NOTE: This touch logic change: if a bridge own by HV as its children */
				if ((pdev != NULL) && is_bridge(pdev)) {
					bus_map[e].bus_under_scan =
						(uint8_t)pci_pdev_read_cfg(pbdf, PCIR_SECBUS_1, 1U);
					bus_map[e].bus_drhd_index = bdf_drhd_index;
					e = e + 1U;
				}
			}
		}
	}
}

/**
 * @brief Add a PCI device to the DRHD date structure which it unders
 *
 * When walking DMAR tables to search PCI device scope, add the device into a global DRHD
 * data structure which will be used later in scan_pci_hierarchy to map the right DRHD unit
 * to the PCI device.
 *
 * @param[in]    bdf             An unsigned integer which indicates the BDF of a device to find.
 * @param[inout] bdfs_from_drhds A pointer to a device list which belongs to DRHD.
 * @param[in]    drhd_index      DRHD index.
 *
 * @return None
 *
 * @pre bdfs_from_drhds != NULL
 */
static void pci_add_bdf_from_drhd(union pci_bdf bdf, struct pci_bdf_mapping_group *const bdfs_from_drhds,
					uint32_t drhd_index)
{
	if (bdfs_from_drhds->pci_bdf_map_count < BDF_MAPPING_NUM) {
		bdfs_from_drhds->bdf_map[bdfs_from_drhds->pci_bdf_map_count].dev_scope_bdf = bdf;
		bdfs_from_drhds->bdf_map[bdfs_from_drhds->pci_bdf_map_count].dev_scope_drhd_index = drhd_index;
		bdfs_from_drhds->pci_bdf_map_count++;
	} else {
		ASSERT(bdfs_from_drhds->pci_bdf_map_count < BDF_MAPPING_NUM,
				"Compare value in BDF_MAPPING_NUM against those in ACPI DMAR tables");
	}
}


/**
 * @brief Parse DRHD device scope
 *
 * Walk DMAR tables to search device scope, add the device which type is endpoint or bridge
 * into a global DRHD data structure which will be used later in scan_pci_hierarchy.
 *
 * @param[inout] bdfs_from_drhds  A pointer to a device list which belongs to DRHD.
 * @param[out]   drhd_idx_pci_all DRHD index which is updated when the DRHD has DRHD_FLAG_INCLUDE_PCI_ALL_MASK set.
 *
 * @return None
 *
 * @pre bdfs_from_drhds != NULL
 * @pre drhd_idx_pci_all != NULL
 */
static void pci_parse_iommu_devscopes(struct pci_bdf_mapping_group *const bdfs_from_drhds,
						uint32_t *drhd_idx_pci_all)
{
	union pci_bdf bdf;
	uint32_t drhd_index, devscope_index;

	for (drhd_index = 0U; drhd_index < plat_dmar_info.drhd_count; drhd_index++) {
		for (devscope_index = 0U; devscope_index < plat_dmar_info.drhd_units[drhd_index].dev_cnt;
						devscope_index++) {
			bdf.fields.bus = plat_dmar_info.drhd_units[drhd_index].devices[devscope_index].bus;
			bdf.fields.devfun = plat_dmar_info.drhd_units[drhd_index].devices[devscope_index].devfun;

			if ((plat_dmar_info.drhd_units[drhd_index].devices[devscope_index].type ==
						ACPI_DMAR_SCOPE_TYPE_ENDPOINT) ||
				(plat_dmar_info.drhd_units[drhd_index].devices[devscope_index].type ==
						ACPI_DMAR_SCOPE_TYPE_BRIDGE)) {
				pci_add_bdf_from_drhd(bdf, bdfs_from_drhds, drhd_index);
			} else {
				/*
				 * Do nothing for IOAPIC, ACPI namespace and
				 * MSI Capable HPET device scope
				 */
			}
		}

		if ((plat_dmar_info.drhd_units[drhd_index].flags & DRHD_FLAG_INCLUDE_PCI_ALL_MASK)
				== DRHD_FLAG_INCLUDE_PCI_ALL_MASK) {
			*drhd_idx_pci_all = drhd_index;
		}
	}
}

/**
 * @brief Configure PCI bridge
 *
 * There are some rules to config PCI bridge: try to avoid interference between Service VM and RTVM or
 * pre-launched VM; and to support some features like SRIOV by default, so as following:
 *  1. disable interrupt, including INTx and MSI.
 *  2. enable ARI if it's a PCIe bridge and all its sub devices support ARI (need check further).
 *  3. enable ACS. (now assume BIOS does it), could check and do it in HV in the future.
 *
 * @param[in] pdev Pointer to the data structure of PCI physical device.
 *
 * @return None
 *
 * @pre pdev != NULL
 */
static void config_pci_bridge(const struct pci_pdev *pdev)
{
	uint32_t offset, val, msgctrl;

	/* for command regsiters, disable INTx */
	val = pci_pdev_read_cfg(pdev->bdf, PCIR_COMMAND, 2U);
	pci_pdev_write_cfg(pdev->bdf, PCIR_COMMAND, 2U, val | PCIM_CMD_INTxDIS);

	/* disale MSI */
	if (pdev->msi_capoff != 0x00UL) {
		offset = pdev->msi_capoff + PCIR_MSI_CTRL;

		msgctrl = pci_pdev_read_cfg(pdev->bdf, offset, 2U);
		msgctrl &= ~PCIM_MSICTRL_MSI_ENABLE;
		pci_pdev_write_cfg(pdev->bdf, offset, 2U, msgctrl);
	}

	/* Enable ARI if PCIe bridge could support it for SRIOV needs it */
	if (pdev->pcie_capoff != 0x00UL) {
		offset = pdev->pcie_capoff + PCIR_PCIE_DEVCAP2;
		val = pci_pdev_read_cfg(pdev->bdf, offset, 2U);

		if ((val & PCIM_PCIE_DEVCAP2_ARI) != 0U) {
			offset = pdev->pcie_capoff + PCIR_PCIE_DEVCTL2;

			val = pci_pdev_read_cfg(pdev->bdf, offset, 2U);
			val |= PCIM_PCIE_DEVCTL2_ARI;
			pci_pdev_write_cfg(pdev->bdf, offset, 2U, val);
		}
	}
}

/**
 * @brief Initialize a per VM device configuration data structure for all PCI physical devices
 *
 * Walk through all PCI physical devices that have been initialized and initialize a per VM device
 * configuration data structure for the device. But for the SRIOV device, drop the device if no room
 * for its all of virtual functions. The device which can be initialized PCI device configuration for
 * a VM will be exposed to Service VM finally. For bridge, configure it to disable INTx/MSI and enable
 * ARI if if conditions are fulfilled.
 *
 * @return None
 */
static void init_all_dev_config(void)
{
	uint32_t idx, cnt = 0U;
	uint32_t total = num_pci_pdev;
	struct pci_pdev *pdev = NULL;

	for (idx = 0U; idx < num_pci_pdev; idx++) {
		pdev = &pci_pdevs[idx];

		if (is_bridge(pdev)) {
			config_pci_bridge(pdev);
		}

		/*
		 * FIXME: Mask the SR-IOV capability instead drop the device
		 * when supporting PCIe extended capabilities whitelist.
		 */
		if (pdev->sriov.capoff != 0U) {
			cnt = pci_pdev_read_cfg(pdev->bdf,
				pdev->sriov.capoff + PCIR_SRIOV_TOTAL_VFS, 2U);
			/*
			 * For SRIOV-Capable device, drop the device
			 * if no room for its all of virtual functions.
			 */
			if ((total + cnt) > CONFIG_MAX_PCI_DEV_NUM) {
				pr_err("%s, %x:%x.%x is dropped since no room for %u VFs",
						__func__, pdev->bdf.bits.b, pdev->bdf.bits.d, pdev->bdf.bits.f, cnt);
				continue;
			} else {
				total += cnt;
			}
		}
		(void)init_one_dev_config(pdev);
	}
}

/**
 * @brief Initialize PCI physical device list
 *
 * Walk the PCI hierarchy and initialize an array of PCI physical devices. Use DRHD info from
 * ACPI DMAR tables to cover the endpoints and bridges along with their hierarchy captured in
 * the device scope entries. Then, walk through the rest of the devices starting at bus 0 and
 * thru PCI_BUSMAX. Do necessary initializations for the PCI physical devices.
 *
 * @return None
 */
void init_pci_pdev_list(void)
{
	uint64_t buses_visited[BUSES_BITMAP_LEN] = {0UL};
	struct pci_bdf_mapping_group bdfs_from_drhds = {.pci_bdf_map_count = 0U};
	uint32_t drhd_idx_pci_all = INVALID_DRHD_INDEX;
	uint16_t bus;
	bool was_visited = false;

	set_paging_supervisor(phys_pci_mmcfg.address, get_pci_mmcfg_size(&phys_pci_mmcfg));

	pci_parse_iommu_devscopes(&bdfs_from_drhds, &drhd_idx_pci_all);

	/* TODO: iterate over list of PCI Host Bridges found in ACPI namespace */
	for (bus = 0U; bus <= PCI_BUSMAX; bus++) {
		was_visited = bitmap_test((bus & 0x3FU), &buses_visited[bus >> 6U]);
		if (!was_visited) {
			scan_pci_hierarchy((uint8_t)bus, buses_visited, &bdfs_from_drhds, drhd_idx_pci_all);
		}
	}
	init_all_dev_config();
}

/**
 * @brief Get the number of BARs according to header type
 *
 * @param[in] hdr_type Flag to indicate the header type.
 *
 * @return An unsigned integer to indicate the number of BARs
 *
 * @retval PCI_STD_NUM_BARS    Header type is 0
 * @retval PCIM_HDRTYPE_BRIDGE Header type is 1
 * @retval 0                   Other cases
 */
static inline uint32_t pci_pdev_get_nr_bars(uint8_t hdr_type)
{
	uint32_t nr_bars = 0U;

	switch (hdr_type & PCIM_HDRTYPE) {
	case PCIM_HDRTYPE_NORMAL:
		nr_bars = PCI_STD_NUM_BARS;
		break;

	case PCIM_HDRTYPE_BRIDGE:
		nr_bars = 2U;
		break;

	default:
		/*no actions are required for other cases.*/
		break;
	}

	return nr_bars;
}

/**
 * @brief Set PTM Enable and Root Select
 *
 * This function sets the PTM Enable and Root Select in PTM control register if the PTM capability
 * is supported.
 *
 * @param[in] pdev Pointer to the data structure of PCI physical device.
 * @param[in] pos  Position of the Precision Time Management Extended Capability header register.
 *
 * @return None
 *
 * @pre pdev != NULL
 */
static void pci_enable_ptm_root(struct pci_pdev *pdev, uint32_t pos)
{
	uint32_t ctrl = 0, cap;

	cap = pci_pdev_read_cfg(pdev->bdf, pos + PCIR_PTM_CAP, PCI_PTM_CAP_LEN);

	if ((cap & PCIM_PTM_CAP_ROOT_CAPABLE) != 0U) {
		ctrl = PCIM_PTM_CTRL_ENABLED | PCIM_PTM_CTRL_ROOT_SELECTED;

		pci_pdev_write_cfg(pdev->bdf, pos + PCIR_PTM_CTRL, 4, ctrl);

		ctrl = pci_pdev_read_cfg(pdev->bdf, pos + PCIR_PTM_CTRL, 4);

		pr_info("ptm info [%x:%x.%x]: pos=%x, enabled=%d, root_select=%d, granularity=%d.\n",
				pdev->bdf.bits.b, pdev->bdf.bits.d,
				pdev->bdf.bits.f, pos, (ctrl & PCIM_PTM_CTRL_ENABLED) != 0,
				(ctrl & PCIM_PTM_CTRL_ROOT_SELECTED) != 0,
				(ctrl & PCIM_PTM_GRANULARITY_MASK) >> 8);
	} else {
		/* acrn doesn't support this hw config currently */
		pr_err("%s: root port %x:%x.%x is not PTM root.\n", __func__,
			 	pdev->bdf.bits.b, pdev->bdf.bits.d, pdev->bdf.bits.f);

	}

	return;
}

/**
 * @brief Enumerate PCIe Extended Capabilities
 *
 * This function walk through each PCIe extend capabilities to decode SRIOV/ATS/PRI/ACS capabilities
 * and enable PTM capability which features need be emumerated by ACRN.
 *
 * @param[in] pdev Pointer to the data structure of PCI physical device.
 *
 * @return None
 *
 * @pre pdev != NULL
 */
static void pci_enumerate_ext_cap(struct pci_pdev *pdev)
{
	uint32_t hdr, pos;
	uint8_t pcie_dev_type;
	uint8_t idx;
	uint8_t vf_bar_zero = 0;

	/* guard against malformed list */
	int node_limit;

	pos = PCI_ECAP_BASE_PTR;

	/* minimum 8 bytes per cap */
	node_limit = (PCIE_CONFIG_SPACE_SIZE - PCI_CONFIG_SPACE_SIZE) / 8;

	/* PCI Express Extended Capability must have 4 bytes header */
	hdr = pci_pdev_read_cfg(pdev->bdf, pos, 4U);
	while ((hdr != 0U) && (node_limit > 0)) {
		if (PCI_ECAP_ID(hdr) == PCIZ_SRIOV) {
			pdev->sriov.capoff = pos;
			pdev->sriov.caplen = PCI_SRIOV_CAP_LEN;
			for (idx = 0; idx < PCI_STD_NUM_BARS; idx++) {
				pdev->vf_bars[idx] = pci_pdev_read_cfg(pdev->bdf,
						pos + PCIR_SRIOV_VF_BAR_OFF + idx * 4, 4U);
				if (pdev->vf_bars[idx] == 0) {
					vf_bar_zero++;
				}
			}
			if (vf_bar_zero == idx) {
				pr_err("BIOS is not right. It doesn't program VF BARs correctly!!!\n");
			}
		} else if (PCI_ECAP_ID(hdr) == PCIZ_PTM) {
			pcie_dev_type = (((uint8_t)pci_pdev_read_cfg(pdev->bdf,
				pdev->pcie_capoff + PCIER_FLAGS, 1)) & PCIEM_FLAGS_TYPE) >> 4;

			if ((pcie_dev_type == PCIEM_TYPE_ENDPOINT) ||
					(pcie_dev_type == PCIEM_TYPE_ROOT_INT_EP)) {
				/* No need to enable ptm on ep device.  If a PTM-capable ep pass
				 * through to guest, guest OS will enable it
				 */
				pr_acrnlog("%s: [%x:%x.%x] is PTM capable.\n", __func__,
					pdev->bdf.bits.b,
					pdev->bdf.bits.d, pdev->bdf.bits.f);
			} else if (pcie_dev_type == PCIEM_TYPE_ROOTPORT) {
				/* if root port is PTM root capable, we need to make sure that
				 * ptm is enabled in h/w
				 */
				pci_enable_ptm_root(pdev, pos);
			} else {
				/* Acrn supports a simple PTM hierarchy:  ptm capable ep is
				 * directly connected to a ptm-root capable root port or ep itself
				 * is rcie.  Report error for all other cases.
				 * */
				pr_err("%s: Do NOT enable PTM on [%x:%x.%x].\n", __func__,
					pdev->bdf.bits.b, pdev->bdf.bits.d, pdev->bdf.bits.f);
			}
		} else if (PCI_ECAP_ID(hdr) == PCIZ_ATS) {
			pdev->ats.capoff = pos;
			pdev->ats.caplen = PCI_ATS_CAP_LEN;
		} else if (PCI_ECAP_ID(hdr) == PCIZ_PRI) {
			pdev->pri.capoff = pos;
			pdev->pri.caplen = PCI_PRI_CAP_LEN;
		} else if (PCI_ECAP_ID(hdr) == PCIZ_ACS) {
			pdev->acs.capoff = pos;
			pdev->acs.caplen = PCI_ACS_CAP_LEN;
		} else {
			/* reserved for future use */
		}

		pos = PCI_ECAP_NEXT(hdr);
		if (pos == 0U) {
			break;
		}
		if (pos < PCI_CONFIG_SPACE_SIZE) {
			pr_err("pdev %x:%x.%x: Illegal PCIe extended capability offset %x",
				pdev->bdf.bits.b, pdev->bdf.bits.d, pdev->bdf.bits.f, pos);
			break;
		}
		hdr = pci_pdev_read_cfg(pdev->bdf, pos, 4U);
		node_limit--;
	};

	if (node_limit <= 0) {
		pr_err("%s: pdev[%x:%x.%x] Malformed linked list in PCIe extended \
			capability region detected\n", __func__, pdev->bdf.bits.b,
			pdev->bdf.bits.d, pdev->bdf.bits.f);
	}
}

/**
 * @brief Enumerate PCI and PCIe Capabilities
 *
 * This function walk through each PCI and PCIe Capabilities to decode the MSI, MSI-X, PMC, AF,
 * PCIe Capability and PCIe Extended Capabilities which need be emuerated by ACRN.
 *
 * @param[in] pdev Pointer to the data structure of PCI physical device.
 *
 * @return None
 *
 * @pre pdev != NULL
 */
static void pci_enumerate_cap(struct pci_pdev *pdev)
{
	uint32_t pos, cap;
	uint32_t msgctrl;
	uint32_t len, idx;
	uint32_t table_info;
	uint32_t pcie_devcap, val;
	bool is_pcie = false;

	pos = pci_pdev_read_cfg(pdev->bdf, PCIR_CAP_PTR, 1U);

	while ((pos != 0U) && (pos < 0xFFU)) {
		cap = pci_pdev_read_cfg(pdev->bdf, pos + PCICAP_ID, 1U);

		if (cap == PCIY_MSI) {
			pdev->msi_capoff = pos;
		} else if (cap == PCIY_MSIX) {
			pdev->msix.capoff = pos;
			pdev->msix.caplen = MSIX_CAPLEN;
			len = pdev->msix.caplen;

			msgctrl = pci_pdev_read_cfg(pdev->bdf, pdev->msix.capoff + PCIR_MSIX_CTRL, 2U);

			/* Read Table Offset and Table BIR */
			table_info = pci_pdev_read_cfg(pdev->bdf, pdev->msix.capoff + PCIR_MSIX_TABLE, 4U);

			pdev->msix.table_bar = (uint8_t)(table_info & PCIM_MSIX_BIR_MASK);

			pdev->msix.table_offset = table_info & ~PCIM_MSIX_BIR_MASK;
			pdev->msix.table_count = (msgctrl & PCIM_MSIXCTRL_TABLE_SIZE) + 1U;

			ASSERT(pdev->msix.table_count <= CONFIG_MAX_MSIX_TABLE_NUM);

			/* Copy MSIX capability struct into buffer */
			for (idx = 0U; idx < len; idx++) {
				pdev->msix.cap[idx] = (uint8_t)pci_pdev_read_cfg(pdev->bdf, pos + idx, 1U);
			}
		} else if (cap == PCIY_PMC) {
			val = pci_pdev_read_cfg(pdev->bdf, pos + PCIR_PMCSR, 4U);
			pdev->has_pm_reset = ((val & PCIM_PMCSR_NO_SOFT_RST) == 0U);
		} else if (cap == PCIY_PCIE) {
			is_pcie = true;
			pcie_devcap = pci_pdev_read_cfg(pdev->bdf, pos + PCIR_PCIE_DEVCAP, 4U);
			pdev->pcie_capoff = pos;
			pdev->has_flr = ((pcie_devcap & PCIM_PCIE_FLRCAP) != 0U);
		} else if (cap == PCIY_AF) {
			val = pci_pdev_read_cfg(pdev->bdf, pos, 4U);
			pdev->has_af_flr = ((val & PCIM_AF_FLR_CAP) != 0U);
		} else {
			/* Ignore all other Capability IDs for now */
		}

		pos = pci_pdev_read_cfg(pdev->bdf, pos + PCICAP_NEXTPTR, 1U);
	}

	if (is_pcie) {
		pci_enumerate_ext_cap(pdev);
	}
}

/**
 * @brief Initialize a PCI physical device data structure
 *
 * Initialize a PCI physical device data structure with a physical device BDF and
 * DRHD index. Then, add the device into a global device hash list to manage. But
 * the function fails if below scenarios happen:
 *   - The number of created PCI physical devices exceeds CONFIG_MAX_PCI_DEV_NUM;
 *   - The Header Type of the PCI physical device is not PCIM_HDRTYPE_NORMAL or
 *     PCIM_HDRTYPE_BRIDGE.
 *
 * The caller of the function init_pdev should guarantee execution atomically.
 *
 * @param[in] bdf        Physical device BDF.
 * @param[in] drhd_index DRHD index.
 *
 * @return A pointer to indicate PCI physical device data structure instance
 *
 * @retval non-NULL Succeed to initialize PCI physical device data structure
 * @retval NULL     Fail to initialize the PCI physical device data structure
 */
struct pci_pdev *pci_init_pdev(union pci_bdf bdf, uint32_t drhd_index)
{
	uint8_t hdr_type, hdr_layout;
	struct pci_pdev *pdev = NULL;
	bool is_hv_owned = false;

	if (num_pci_pdev < CONFIG_MAX_PCI_DEV_NUM) {
		hdr_type = (uint8_t)pci_pdev_read_cfg(bdf, PCIR_HDRTYPE, 1U);
		hdr_layout = (hdr_type & PCIM_HDRTYPE);

		if ((hdr_layout == PCIM_HDRTYPE_NORMAL) || (hdr_layout == PCIM_HDRTYPE_BRIDGE)) {
			pdev = &pci_pdevs[num_pci_pdev];
			pdev->bdf = bdf;
			pdev->hdr_type = hdr_type;
			pdev->base_class = (uint8_t)pci_pdev_read_cfg(bdf, PCIR_CLASS, 1U);
			pdev->sub_class = (uint8_t)pci_pdev_read_cfg(bdf, PCIR_SUBCLASS, 1U);
			pdev->vendor_id = (uint16_t)pci_pdev_read_cfg(bdf, PCIR_VENDOR, 2U);
			pdev->device_id = (uint16_t)pci_pdev_read_cfg(bdf, PCIR_DEVICE, 2U);
			pdev->nr_bars = pci_pdev_get_nr_bars(hdr_type);
			pdev_save_bar(pdev);

			if ((pci_pdev_read_cfg(bdf, PCIR_STATUS, 2U) & PCIM_STATUS_CAPPRESENT) != 0U) {
				pci_enumerate_cap(pdev);
			}

#if (PRE_VM_NUM != 0U)
			/* HV owned pdev: 1.typ1 pdev if pre-launched VM exist; 2.pci debug uart */
			is_hv_owned = (hdr_layout == PCIM_HDRTYPE_BRIDGE) || is_pci_dbg_uart(bdf);
#else
			/* HV owned pdev: 1.pci debug uart */
			is_hv_owned = is_pci_dbg_uart(bdf);
#endif
			if (is_hv_owned) {
				hv_owned_pci_pdevs[num_hv_owned_pci_pdev] = pdev;
				num_hv_owned_pci_pdev++;
			}
			hlist_add_head(&pdev->link, &pdevs_hlist_heads[hash64(bdf.value, PDEV_HLIST_HASHBITS)]);
			pdev->drhd_index = drhd_index;
			num_pci_pdev++;
		} else {
			pr_err("%s, %x:%x.%x unsupported headed type: 0x%x\n",
				__func__, bdf.bits.b, bdf.bits.d, bdf.bits.f, hdr_type);
		}
	} else {
		pr_err("%s, failed to alloc pci_pdev!\n", __func__);
	}

	return pdev;
}

/**
 * @}
 */
