/*-
* Copyright (c) 2011 NetApp, Inc.
* Copyright (c) 2018-2025 Intel Corporation.
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
*/

#ifndef VPCI_PRIV_H_
#define VPCI_PRIV_H_

#include <list.h>
#include <pci.h>
#include <vpci.h>

/**
 * @addtogroup vp-dm_vpci
 *
 * @{
 */

/**
 * @file
 * @brief This file declares internal APIs used by vPCI module
 */

/*
 * For hypervisor emulated PCI devices, vMSIX Table contains 128 entries
 * at most. vMSIX Table begins at an offset of 0, and maps the vMSIX PBA
 * beginning at an offset of 2 KB.
 */
#define VMSIX_MAX_TABLE_ENTRY_NUM  128U      /**< Number of MSI-X Table Entry */
#define VMSIX_MAX_ENTRY_TABLE_SIZE 2048U     /**< Size of MSI-X Table */
#define VMSIX_ENTRY_TABLE_PBA_BAR_SIZE 4096U /**< Size of PBA BAR */

/**
 * @brief Check if input value is within the range
 *
 * Check if input value is within the range, i.e. no less than input lower and less than
 * (lower + len).
 *
 * @param[in] value The value to check.
 * @param[in] lower The bottom of the range.
 * @param[in] len   The length of the range.
 *
 * @return A boolean to indicate whether the input value is within the range
 *
 * @retval true  The value is within the range.
 * @retval false The value is not within the range.
 */
static inline bool in_range(uint32_t value, uint32_t lower, uint32_t len)
{
	return ((value >= lower) && (value < (lower + len)));
}

/**
 * @brief Check if the virtual PCI device supports MSI-X
 *
 * @param[in] vdev Pointer to a virtual PCI device to check.
 *
 * @return A boolean value to indicate whether the virtual PCI device supports MSI-X
 *
 * @retval true  The virtual PCI device supports MSI-X.
 * @retval false The virtual PCI device does not support MSI-X.
 *
 * @pre vdev != NULL
 */
static inline bool has_msix_cap(const struct pci_vdev *vdev)
{
	return (vdev->msix.capoff != 0U);
}

/**
 * @brief Check if the input offset is in the range of virtual PCI device MSI-X capability
 *
 * @param[in] vdev   Pointer to a virtual PCI device to check.
 * @param[in] offset Offset to check.
 *
 * @return A boolean value to indicate whether the input offset is in the range
 *
 * @retval true  The input offset is in the range of virtual PCI device MSI-X capability.
 * @retval false The virtual PCI device does not support MSI-X capability or the input
 *               offset is not in the range of virtual PCI device MSI-X capability.
 *
 * @pre vdev != NULL
 */
static inline bool msixcap_access(const struct pci_vdev *vdev, uint32_t offset)
{
	return (has_msix_cap(vdev) && in_range(offset, vdev->msix.capoff, vdev->msix.caplen));
}

/**
 * @brief Check if the input offset is in the virtual PCI device MSI-X Table range
 *
 * @param[in] vdev   Pointer to a virtual PCI device to check.
 * @param[in] offset Offset to check.
 *
 * @return A boolean value to indicate whether the input offset is in the range
 *
 * @retval true  The input offset is in the virtual PCI device MSI-X Table range.
 * @retval false The input offset is not in the virtual PCI device MSI-X Table range.
 *
 * @pre vdev != NULL
 */
static inline bool msixtable_access(const struct pci_vdev *vdev, uint32_t offset)
{
	return in_range(offset, vdev->msix.table_offset, vdev->msix.table_count * MSIX_TABLE_ENTRY_SIZE);
}

/**
 * @brief Check if the virtual PCI device supports SR-IOV
 *
 * @param[in] vdev Pointer to a virtual PCI device to check.
 *
 * @return A boolean value to indicate whether the virtual PCI device supports SR-IOV
 *
 * @retval true  The virtual PCI device supports SR-IOV.
 * @retval false The virtual PCI device does not support SR-IOV.
 *
 * @pre vdev != NULL
 */
static inline bool has_sriov_cap(const struct pci_vdev *vdev)
{
	return (vdev->sriov.capoff != 0U);
}

/**
 * @brief Check if the virtual PCI device supports ATS
 *
 * @param[in] vdev Pointer to a virtual PCI device to check.
 *
 * @return A boolean value to indicate whether the virtual PCI device supports ATS
 *
 * @retval true  The virtual PCI device supports ATS.
 * @retval false The virtual PCI device does not support ATS.
 *
 * @pre vdev != NULL
 */
static inline bool has_ats_cap(const struct pci_vdev *vdev)
{
	return (vdev->ats.capoff != 0U);
}

/**
 * @brief Check if the virtual PCI device supports PRI
 *
 * @param[in] vdev Pointer to a virtual PCI device to check.
 *
 * @return A boolean value to indicate whether the virtual PCI device supports PRI
 *
 * @retval true  The virtual PCI device supports PRI.
 * @retval false The virtual PCI device does not support PRI.
 *
 * @pre vdev != NULL
 */
static inline bool has_pri_cap(const struct pci_vdev *vdev)
{
	return (vdev->pri.capoff != 0U);
}

/**
 * @brief Check if the virtual PCI device supports ACS
 *
 * @param[in] vdev Pointer to a virtual PCI device to check.
 *
 * @return A boolean value to indicate whether the virtual PCI device supports ACS
 *
 * @retval true  The virtual PCI device supports ACS.
 * @retval false The virtual PCI device does not support ACS.
 *
 * @pre vdev != NULL
 */
static inline bool has_acs_cap(const struct pci_vdev *vdev)
{
	return (vdev->acs.capoff != 0U);
}

/**
 * @brief Check if the input offset is in the range of virtual PCI device SR-IOV capability
 *
 * @param[in] vdev   Pointer to a virtual PCI device to check.
 * @param[in] offset Offset to check.
 *
 * @return A boolean value to indicate whether the input offset is in the range
 *
 * @retval true  The input offset is in the range of virtual PCI device SR-IOV capability.
 * @retval false The virtual PCI device does not support SR-IOV capability or the input
 *               offset is not in the range of virtual PCI device SR-IOV capability.
 *
 * @pre vdev != NULL
 */
static inline bool sriovcap_access(const struct pci_vdev *vdev, uint32_t offset)
{
	return (has_sriov_cap(vdev) && in_range(offset, vdev->sriov.capoff, vdev->sriov.caplen));
}

/**
 * @brief Check if the input offset is in the range of virtual PCI device ATS capability
 *
 * @param[in] vdev   Pointer to a virtual PCI device to check.
 * @param[in] offset Offset to check.
 *
 * @return A boolean value to indicate whether the input offset is in the range
 *
 * @retval true  The input offset is in the range of virtual PCI device ATS capability.
 * @retval false The virtual PCI device does not support ATS capability or the input
 *               offset is not in the range of virtual PCI device ATS capability.
 *
 * @pre vdev != NULL
 */
static inline bool atscap_access(const struct pci_vdev *vdev, uint32_t offset)
{
	return (has_ats_cap(vdev) && in_range(offset, vdev->ats.capoff, vdev->ats.caplen));
}

/**
 * @brief Check if the input offset is in the range of virtual PCI device PRI capability
 *
 * @param[in] vdev   Pointer to a virtual PCI device to check.
 * @param[in] offset Offset to check.
 *
 * @return A boolean value to indicate whether the input offset is in the range
 *
 * @retval true  The input offset is in the range of virtual PCI device PRI capability.
 * @retval false The virtual PCI device does not support PRI capability or the input
 *               offset is not in the range of virtual PCI device PRI capability.
 *
 * @pre vdev != NULL
 */
static inline bool pricap_access(const struct pci_vdev *vdev, uint32_t offset)
{
	return (has_pri_cap(vdev) && in_range(offset, vdev->pri.capoff, vdev->pri.caplen));
}

/**
 * @brief Check if the input offset is in the range of virtual PCI device ACS capability
 *
 * @param[in] vdev   Pointer to a virtual PCI device to check.
 * @param[in] offset Offset to check.
 *
 * @return A boolean value to indicate whether the input offset is in the range
 *
 * @retval true  The input offset is in the range of virtual PCI device ACS capability.
 * @retval false The virtual PCI device does not support ACS capability or the input
 *               offset is not in the range of virtual PCI device ACS capability.
 *
 * @pre vdev != NULL
 */
static inline bool acscap_access(const struct pci_vdev *vdev, uint32_t offset)
{
	return (has_acs_cap(vdev) && in_range(offset, vdev->acs.capoff, vdev->acs.caplen));
}

/**
 * @brief Check if the input offset is in the range of virtual PCI device vBARs
 *
 * @param[in] vdev   Pointer to a virtual PCI device to check.
 * @param[in] offset Offset to check.
 *
 * @return A boolean value to indicate whether the input offset is in the range
 *
 * @retval true  The input offset is in the range of virtual PCI device vBARs.
 * @retval false The input offset is not in the range of virtual PCI device vBARs.
 *
 * @pre vdev != NULL
 */
static inline bool vbar_access(const struct pci_vdev *vdev, uint32_t offset)
{
	return is_bar_offset(vdev->nr_bars, offset);
}

/**
 * @brief Check if the input offset is in the range of PCI Configuration Space Header
 *
 * @param[in] offset Offset to check.
 *
 * @return A boolean value to indicate whether the input offset is in the range
 *
 * @retval true  The input offset is in the range of PCI Configuration Space Header.
 * @retval false The input offset is not in the range of PCI Configuration Space Header.
 */
static inline bool cfg_header_access(uint32_t offset)
{
	return (offset < PCI_CFG_HEADER_LENGTH);
}

/**
 * @brief Check if the virtual PCI device supports MSI
 *
 * @param[in] vdev Pointer to a virtual PCI device to check.
 *
 * @return A boolean value to indicate whether the virtual PCI device supports MSI
 *
 * @retval true  The virtual PCI device supports MSI.
 * @retval false The virtual PCI device does not support MSI.
 *
 * @pre vdev != NULL
 */
static inline bool has_msi_cap(const struct pci_vdev *vdev)
{
	return (vdev->msi.capoff != 0U);
}

/**
 * @brief Check if the input offset is in the range of virtual PCI device MSI capability
 *
 * @param[in] vdev   Pointer to a virtual PCI device to check.
 * @param[in] offset Offset to check.
 *
 * @return A boolean value to indicate whether the input offset is in the range
 *
 * @retval true  The input offset is in the range of virtual PCI device MSI capability.
 * @retval false The virtual PCI device does not support MSI capability or the input
 *               offset is not in the range of virtual PCI device MSI capability.
 *
 * @pre vdev != NULL
 */
static inline bool msicap_access(const struct pci_vdev *vdev, uint32_t offset)
{
	return (has_msi_cap(vdev) && in_range(offset, vdev->msi.capoff, vdev->msi.caplen));
}

/**
 * @brief Check if the specified virtual PCI device is a zombie VF instance
 *
 * This function is used check if the specified virtual PCI device is a zombie VF instance,
 * i.e. the VF has been disabled so that there is no user.
 *
 * @param[in] vdev Pointer to a virtual PCI device to check.
 *
 * @return A boolean value to indicate whether the virtual PCI device is a zombie VF instance
 *
 * @retval true  The virtual PCI device is a zombie VF instance.
 * @retval false The virtual PCI device is not a zombie VF instance.
 *
 * @pre vdev != NULL
 */
static inline bool is_zombie_vf(const struct pci_vdev *vdev)
{
	return (vdev->user == NULL);
}

void init_vdev_pt(struct pci_vdev *vdev, bool is_pf_vdev);
void deinit_vdev_pt(struct pci_vdev *vdev);
void vdev_pt_write_vbar(struct pci_vdev *vdev, uint32_t idx, uint32_t val);
void vdev_pt_map_msix(struct pci_vdev *vdev, bool hold_lock);

void init_vmsi(struct pci_vdev *vdev);
void write_vmsi_cap_reg(struct pci_vdev *vdev, uint32_t offset, uint32_t bytes, uint32_t val);
void deinit_vmsi(struct pci_vdev *vdev);

void init_vmsix_pt(struct pci_vdev *vdev);
int32_t add_vmsix_capability(struct pci_vdev *vdev, uint32_t entry_num, uint8_t bar_num);
bool write_vmsix_cap_reg(struct pci_vdev *vdev, uint32_t offset, uint32_t bytes, uint32_t val);
void write_pt_vmsix_cap_reg(struct pci_vdev *vdev, uint32_t offset, uint32_t bytes, uint32_t val);
uint32_t rw_vmsix_table(struct pci_vdev *vdev, struct io_request *io_req);
int32_t vmsix_handle_table_mmio_access(struct io_request *io_req, void *priv_data);
bool vpci_vmsix_enabled(const struct pci_vdev *vdev);
void deinit_vmsix_pt(struct pci_vdev *vdev);

void init_vsriov(struct pci_vdev *vdev);
void write_sriov_cap_reg(struct pci_vdev *vdev, uint32_t offset, uint32_t bytes, uint32_t val);
uint32_t sriov_bar_offset(const struct pci_vdev *vdev, uint32_t bar_idx);

uint32_t pci_vdev_read_vcfg(const struct pci_vdev *vdev, uint32_t offset, uint32_t bytes);
void pci_vdev_write_vcfg(struct pci_vdev *vdev, uint32_t offset, uint32_t bytes, uint32_t val);
uint32_t vpci_add_capability(struct pci_vdev *vdev, uint8_t *capdata, uint8_t caplen);

void pci_vdev_write_vbar(struct pci_vdev *vdev, uint32_t idx, uint32_t val);

void vdev_bridge_pt_restore_space(struct pci_vdev *vdev);
void vdev_bridge_pt_restore_bus(struct pci_vdev *vdev);

int32_t check_pt_dev_pio_bars(struct pci_vdev *vdev);

/**
 * @brief Callback function definition to map a BAR
 *
 * This is a definition for a callback function which is used to map a BAR of the virtual PCI device.
 *
 * @param[in] vdev    Pointer to a virtual PCI device to map the BAR.
 * @param[in] bar_idx Index of the BAR to map.
 *
 * @return None
 *
 * @pre vdev != NULL
 */
typedef void (*map_pcibar)(struct pci_vdev *vdev, uint32_t bar_idx);

/**
 * @brief Callback function definition to unmap a BAR
 *
 * This is a definition for a callback function which is used to unmap a BAR of the virtual PCI device.
 *
 * @param[in] vdev    Pointer to a virtual PCI device to unmap the BAR.
 * @param[in] bar_idx Index of the BAR to unmap.
 *
 * @return None
 *
 * @pre vdev != NULL
 */
typedef void (*unmap_pcibar)(struct pci_vdev *vdev, uint32_t bar_idx);
void vpci_update_one_vbar(struct pci_vdev *vdev, uint32_t bar_idx, uint32_t val,
			  map_pcibar map_cb, unmap_pcibar unmap_cb);

/**
 * @}
 */
#endif /* VPCI_PRIV_H_ */
