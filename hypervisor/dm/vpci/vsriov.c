/*
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
 */

#include <asm/guest/vm.h>
#include <asm/guest/ept.h>
#include <ptdev.h>
#include <vpci.h>
#include <asm/pci_dev.h>
#include <logmsg.h>
#include <delay.h>

#include "vpci_priv.h"

/**
 * @addtogroup vp-dm_vpci
 *
 * @{
 */

/**
 * @file
 * @brief This file implements functions to operate SRIOV PF/VF
 */

/**
 * @brief Get VF device number and function number
 *
 * Refer to Chapter 9.2.1.2 VF Discovery, PCI Express® Base Specification Revision 6.0.1 for details.
 *
 * @param[in] pf_vdev Pointer to PF virtual PCI device.
 * @param[in] fst_off First VF Offset.
 * @param[in] stride  VF Stride.
 * @param[in] id      VF Index.
 *
 * @return VF device/function number
 *
 * @pre pf_vdev != NULL
 */
static inline uint8_t get_vf_devfun(const struct pci_vdev *pf_vdev, uint16_t fst_off, uint16_t stride, uint16_t id)
{
	return ((uint8_t)((pf_vdev->bdf.fields.devfun + fst_off + (stride * id)) & 0xFFU));
}

/**
 * @brief Get VF bus number
 *
 * Refer to Chapter 9.2.1.2 VF Discovery, PCI Express® Base Specification Revision 6.0.1 for details.
 *
 * @param[in] pf_vdev Pointer to PF virtual PCI device.
 * @param[in] fst_off First VF Offset.
 * @param[in] stride  VF Stride.
 * @param[in] id      VF Index.
 *
 * @return VF bus number
 *
 * @pre pf_vdev != NULL
 */
static inline uint8_t get_vf_bus(const struct pci_vdev *pf_vdev, uint16_t fst_off, uint16_t stride, uint16_t id)
{
	return ((uint8_t)(pf_vdev->bdf.fields.bus + ((pf_vdev->bdf.fields.devfun + fst_off + (stride * id)) >> 8U)));
}

/**
 * @brief Read two bytes SRIOV capability register from physical device
 *
 * @param[in] pf_vdev Pointer to PF virtual PCI device.
 * @param[in] reg     Offset of the SR-IOV Extended Capability register to read.
 *
 * @return Two bytes of register value
 *
 * @pre pf_vdev != NULL
 */
static inline uint16_t read_sriov_reg(const struct pci_vdev *pf_vdev, uint16_t reg)
{
	return ((uint16_t)(pci_pdev_read_cfg(pf_vdev->bdf, pf_vdev->sriov.capoff + reg, 2U)));
}

/**
 * @brief Check if VF is enabled
 *
 * @param[in] pf_vdev Pointer to PF virtual PCI device.
 *
 * @return A boolean value to indicate whether VF is enabled
 *
 * @retval True VF is enabled
 * @retval False VF is disabled
 *
 * @pre pf_vdev != NULL
 */
static bool is_vf_enabled(const struct pci_vdev *pf_vdev)
{
	uint16_t control;

	control = read_sriov_reg(pf_vdev, PCIR_SRIOV_CONTROL);
	return ((control & PCIM_SRIOV_VF_ENABLE) != 0U);
}

/**
 * @brief Initialize PF virtual PCI device
 *
 * This function calls init_vdev_pt to initialize the PF virtual PCI device.
 *
 * @param[in] pf_vdev Pointer to PF virtual PCI device.
 *
 * @return None
 *
 * @pre pf_vdev != NULL
 */
static void init_sriov_vf_bar(struct pci_vdev *pf_vdev)
{
	init_vdev_pt(pf_vdev, true);
}

/**
 * @brief Create a VF device
 *
 * This function is used to create a VF virtual PCI device. Firstly, it tries to initialize a physical PCI
 * device instance for the VF device. If this step succeeds, it creates the corresponding virtual PCI device
 * for VF.  If the VF device is not successfully initialized, disable the VF Enable flag in SRIOV Control
 * register. If the VF device can be successfully initialized, the function initializes VF's BARs: do the EPT
 * mapping for its valid VF BARs, and call vdev_pt_map_msix to register the MMIO handler for MSI-X Table Entry
 * if the VF support MSI-X capability.
 *
 * @param[in] pf_vdev Pointer to PF virtual PCI device.
 * @param[in] vf_bdf  BDF of the VF device to be created.
 * @param[in] vf_id   VF Index.
 *
 * @return None
 *
 * @pre pf_vdev != NULL
 */
static void create_vf(struct pci_vdev *pf_vdev, union pci_bdf vf_bdf, uint16_t vf_id)
{
	struct pci_pdev *vf_pdev;
	struct pci_vdev *vf_vdev = NULL;

	/*
	 * Per VT-d 8.3.3, the VFs are under the scope of the same
	 * remapping unit as the associated PF when SRIOV is enabled.
	 */
	vf_pdev = pci_init_pdev(vf_bdf, pf_vdev->pdev->drhd_index);
	if (vf_pdev != NULL) {
		struct acrn_vm_pci_dev_config *dev_cfg;

		dev_cfg = init_one_dev_config(vf_pdev);
		if (dev_cfg != NULL) {
			vf_vdev = vpci_init_vdev(&vpci2vm(pf_vdev->vpci)->vpci, dev_cfg, pf_vdev);
		}
	}

	/*
	 * if a VF vdev failed to be created, the VF number is less than requested number
	 * and the requested VF physical devices are ready at this time, clear VF_ENABLE.
	 */
	if (vf_vdev == NULL) {
		uint16_t control;

		control = read_sriov_reg(pf_vdev, PCIR_SRIOV_CONTROL);
		control &= (~PCIM_SRIOV_VF_ENABLE);
		pci_pdev_write_cfg(pf_vdev->bdf, pf_vdev->sriov.capoff + PCIR_SRIOV_CONTROL, 2U, control);
		pr_err("PF %x:%x.%x can't creat VF, unset VF_ENABLE",
			pf_vdev->bdf.bits.b, pf_vdev->bdf.bits.d, pf_vdev->bdf.bits.f);
	} else {
		uint32_t bar_idx;
		struct pci_vbar *vf_vbar;

		/* VF bars information from its PF SRIOV capability, no need to access physical device */
		vf_vdev->nr_bars = PCI_BAR_COUNT;
		for (bar_idx = 0U; bar_idx < PCI_BAR_COUNT; bar_idx++) {
			vf_vbar = &vf_vdev->vbars[bar_idx];
			*vf_vbar = vf_vdev->phyfun->sriov.vbars[bar_idx];
			vf_vbar->base_hpa += (vf_vbar->size * vf_id);
			vf_vbar->base_gpa = vf_vbar->base_hpa;

			/* Map VF's BARs when it's first created. */
			if (vf_vbar->base_gpa != 0UL) {
				struct acrn_vm *vm = vpci2vm(vf_vdev->vpci);
				ept_add_mr(vm, (uint64_t *)(vm->arch_vm.nworld_eptp), vf_vbar->base_hpa,
					vf_vbar->base_gpa, vf_vbar->size, EPT_WR | EPT_RD | EPT_UNCACHED);
			}

			if (has_msix_cap(vf_vdev) && (bar_idx == vf_vdev->msix.table_bar)) {
				vf_vdev->msix.mmio_hpa = vf_vbar->base_hpa;
				vf_vdev->msix.mmio_size = vf_vbar->size;
				vdev_pt_map_msix(vf_vdev, false);
			}

			/*
			 * VF BARs value are zero and read only, according to PCI Express
			 * Base 5.0 chapter 9.3.4.1.11, the VF
			 */
			pci_vdev_write_vcfg(vf_vdev, pci_bar_offset(bar_idx), 4U, 0U);
		}
	}
}

/**
 * @brief Enable VF devices
 *
 * This function is called when the VF enable bit in PCIR_SRIOV_CONTROL is switched to 1 from 0, i.e. enable
 * the VF. Then, it initialize the PF virtual PCI device which also initialize the VF BAR register. After
 * that, it waits for the VF physical devices to be ready by checking SUBSYSTEM VENDOR ID of the first VF.
 * Then, it iterates all VFs and check every VF's SUBSYSTEM VENDOR ID to make sure the VF is ready. If the VF
 * corresponding virtual PCI device doesn't exist, create it. If the VF is a zombie VF, re-activate it. If VF
 * is not ready, print the error message.
 *
 * @param[in] pf_vdev Pointer to PF virtual PCI device.
 *
 * @return None
 *
 * @pre pf_vdev != NULL
 * @pre is_vf_enabled(pf_dev) == true
 *
 * @remark PCIR_SRIOV_NUMVFS register value cannot be 0 if VF_ENABLE is set.
 */
static void enable_vfs(struct pci_vdev *pf_vdev)
{
	union pci_bdf vf_bdf;
	uint16_t idx;
	uint16_t sub_vid = 0U;
	uint16_t num_vfs, stride, fst_off;
	uint32_t timeout_us = 0U;
	bool vf_ready = false;

	/*
         * All VFs bars information are located at PF VF_BAR fields of SRIOV capability.
	 * Initialize the PF's VF_BAR registers before initialize each VF device bar.
	 */
	init_sriov_vf_bar(pf_vdev);

	/*
	 * Per PCIE base spec 9.3.3.3.1, VF Enable bit from cleared to set, the
	 * system is not perrmitted to issue requests to the VFs until one of
	 * the following is true:
	 * 1. at least 100ms has passed.
	 * 2. An FRS message has been received from the PF with a reason code
	 *    of VF Enabled.
	 * 3. At least VF Enable Time has passed since VF Enable was Set.
	 *    VF Enable Time is either the Reset Time value in the Readiness Time
	 *    Reporting capability associated with the VF or a value determined
	 *    by system software/firmware.
	 *
	 * Curerntly, we use the first way to wait for VF physical devices to be ready.
	 */
	fst_off = read_sriov_reg(pf_vdev, PCIR_SRIOV_FST_VF_OFF);
	stride = read_sriov_reg(pf_vdev, PCIR_SRIOV_VF_STRIDE);
	vf_bdf.fields.bus = get_vf_bus(pf_vdev, fst_off, stride, 0U);
	vf_bdf.fields.devfun = get_vf_devfun(pf_vdev, fst_off, stride, 0U);
	while (timeout_us <= (100U * 1000U)) {
		/*
		 * Due to VF's DEVICE ID and VENDOR ID are 0xFFFF, so check if VF physical
		 * device has been created by the value of SUBSYSTEM VENDOR ID.
		 * To check if all enabled VFs are ready, just check the first VF already exists,
		 * do not need to check all.
		 */
		sub_vid = (uint16_t) pci_pdev_read_cfg(vf_bdf, PCIV_SUB_VENDOR_ID, 2U);
		if ((sub_vid != 0xFFFFU) && (sub_vid != 0U)) {
			vf_ready = true;
			break;
		}
		udelay(10U * 1000U);
		timeout_us += (10U * 1000U);
	}

	if (vf_ready) {
		struct pci_vdev *vf_vdev;

		num_vfs = read_sriov_reg(pf_vdev, PCIR_SRIOV_NUMVFS);
		for (idx = 0U; idx < num_vfs; idx++) {
			vf_bdf.fields.bus = get_vf_bus(pf_vdev, fst_off, stride, idx);
			vf_bdf.fields.devfun = get_vf_devfun(pf_vdev, fst_off, stride, idx);

			sub_vid = (uint16_t) pci_pdev_read_cfg(vf_bdf, PCIV_SUB_VENDOR_ID, 2U);
			if ((sub_vid != 0xFFFFU) && (sub_vid != 0U)) {
				/*
				 * If one VF has never been created then create new one including pdev/vdev
				 * structures.
				 *
				 * The VF maybe have already existed but it is a zombie instance that
				 * vf_vdev->vpci is NULL, in this case, we need to make the vf_vdev
				 * available again in here.
				 */
				vf_vdev = pci_find_vdev(&vpci2vm(pf_vdev->vpci)->vpci, vf_bdf);
				if (vf_vdev == NULL) {
					create_vf(pf_vdev, vf_bdf, idx);
				} else {
					/* Re-activate a zombie VF */
					if (is_zombie_vf(vf_vdev)) {
						vf_vdev->vdev_ops->init_vdev(vf_vdev);
					}
				}
			} else {
				pr_err("PF %x:%x.%x: VF[%d] is not ready after %d ms.",
					pf_vdev->bdf.bits.b, pf_vdev->bdf.bits.d, pf_vdev->bdf.bits.f,
					idx, (timeout_us / 1000U));
			}
		}
	} else {
		/*
		 * If the VF physical device was not created successfully, the pdev/vdev
		 * will also not be created so that Service VM can aware of VF creation failure,
		 */
		pr_err("PF %x:%x.%x can't create VFs after %d ms",
			pf_vdev->bdf.bits.b, pf_vdev->bdf.bits.d, pf_vdev->bdf.bits.f, (timeout_us / 1000U));
	}
}

/**
 * @brief Disable VF devices
 *
 * This function is called when the VF enable bit in PCIR_SRIOV_CONTROL is switched to 0 from 1, i.e. disable
 * the VF. Then, it deinitialize all VF devices.
 *
 * @param[in] pf_vdev Pointer to PF virtual PCI device.
 *
 * @return None
 *
 * @pre pf_vdev != NULL
 */
static void disable_vfs(struct pci_vdev *pf_vdev)
{
	uint16_t idx, num_vfs, stride, first;
	struct pci_vdev *vf_vdev;

	/*
	 * PF can disable VFs only when all VFs are not used by any VM or any application
	 *
	 * Ideally, VF instances should be deleted after VFs are disabled, but for FuSa reasons,
	 * we simply set the VF instance status to "zombie" to avoid dynamically adding/removing
	 * resources
	 *
	 * If the VF drivers are still running in Service VM or User VM, the MMIO access will return 0xFF.
	 */
	num_vfs = read_sriov_reg(pf_vdev, PCIR_SRIOV_NUMVFS);
	first = read_sriov_reg(pf_vdev, PCIR_SRIOV_FST_VF_OFF);
	stride = read_sriov_reg(pf_vdev, PCIR_SRIOV_VF_STRIDE);
	for (idx = 0U; idx < num_vfs; idx++) {
		union pci_bdf bdf;

		bdf.fields.bus = get_vf_bus(pf_vdev, first, stride, idx);
		bdf.fields.devfun = get_vf_devfun(pf_vdev, first, stride, idx);
		vf_vdev = pci_find_vdev(&vpci2vm(pf_vdev->vpci)->vpci, bdf);
		if ((vf_vdev != NULL) && (!is_zombie_vf(vf_vdev))) {
			/* set disabled VF as zombie vdev instance */
			vf_vdev->vdev_ops->deinit_vdev(vf_vdev);
		}
	}
}

/**
 * @brief Initialize SRIOV information of a virtual PCI device
 *
 * This function is used to initialize SRIOV information of a virtual PCI device, i.e. set capability register
 * offset/length and set flag to inidicate if the SRIOV capability is read-only if the target VM is not Service
 * VM.
 *
 * @param[inout] vdev Pointer to virtual PCI device.
 *
 * @return None
 *
 * @pre vdev != NULL
 * @pre vdev->pdev != NULL
 */
void init_vsriov(struct pci_vdev *vdev)
{
	struct pci_pdev *pdev = vdev->pdev;

	vdev->sriov.capoff = pdev->sriov.capoff;
	vdev->sriov.caplen = pdev->sriov.caplen;
	if (!is_service_vm(vpci2vm(vdev->vpci))) {
		vdev->sriov.sriov_ro = true;
	}
}

/**
 * @brief Emulate VM write action to SRIOV Extended Capability registers
 *
 * This function is used to handle VM write action to SRIOV Extended Capability registers.
 * The write to register is ignored if the sriov_ro flag is set, i.e. only Service VM can
 * write SRIOV register. If PCIM_SRIOV_VF_ENABLE bit is set, enable_vfs or disable_vfs shall
 * be called. For write to NumVFs register, the sanity check is executed to make sure the
 * valid number is written. For write to VF BARs, ignore the write, if the value is not for
 * sizing (~0U) or not the initial VF BAR content, for guest could not re-program the host
 * phssical address for a PCI device BAR.
 *
 * @param[in] vdev   Pointer to virtual PCI device.
 * @param[in] offset Offset of the register to write.
 * @param[in] bytes  Bytes to write.
 * @param[in] val    Value to write.
 *
 * @return None
 *
 * @pre vdev != NULL
 * @pre vdev->pdev != NULL
 */
void write_sriov_cap_reg(struct pci_vdev *vdev, uint32_t offset, uint32_t bytes, uint32_t val)
{

	uint32_t reg;

	/*
	 * With sriov_ro limitation, pre/post VMs cannot sizing VF BARs so that they cannot
	 * set VF BARs and enable SRIOV.
	 */
	if (!vdev->sriov.sriov_ro) {
		reg = offset - vdev->sriov.capoff;
		if (reg == PCIR_SRIOV_CONTROL) {
			bool enable;

			enable = ((val & PCIM_SRIOV_VF_ENABLE) != 0U);
			if (enable != is_vf_enabled(vdev)) {
				if (enable) {
					/*
					 * set VF_ENABLE to PF physical device before enable_vfs
					 * since need to ask hardware to create VF physical
					 * devices firstly
					 */
					pci_pdev_write_cfg(vdev->pdev->bdf, offset, bytes, val);
					if (is_vf_enabled(vdev)) {
						/*
						 * Enable VFs only when the VF_ENABLE is really
						 * set on physical device.
						 */
						enable_vfs(vdev);
					}
				} else {
					disable_vfs(vdev);
					pci_pdev_write_cfg(vdev->pdev->bdf, offset, bytes, val);
				}
			} else {
				pci_pdev_write_cfg(vdev->pdev->bdf, offset, bytes, val);
			}
		} else if (reg == PCIR_SRIOV_NUMVFS) {
			uint16_t total;

			total = read_sriov_reg(vdev, PCIR_SRIOV_TOTAL_VFS);
			/*
			 * sanity check for NumVFs register based on PCE Express Base 4.0 9.3.3.7 chapter
			 * The results are undefined if NumVFs is set to a value greater than TotalVFs
			 * NumVFs may only be written while VF Enable is Clear
			 * If NumVFs is written when VF Enable is Set, the results are undefined
			 */
			if ((((uint16_t)(val & 0xFFU)) <= total) && (!is_vf_enabled(vdev))) {
				pci_pdev_write_cfg(vdev->pdev->bdf, offset, bytes, val);
			}
		} else if ((reg >= PCIR_SRIOV_VF_BAR_OFF) && (reg < PCIR_SRIOV_VF_MIG_ARR)) {
			/*
			 * Ignore write to VF BARs if the value is not for sizing (~0U) or not the initial VF
			 * BAR content. We are afraid of the VF's BAR address conflicting with Pre-launched
			 * VM HPA so we decide not to support the VF's BAR register update.
			 *
			 * But we allow to write System Page Size. When System Page Size is set, the VF
			 * associated with this PF is required to align all BAR resources on a System Page
			 * Size boundary. This is the limitatin to VM OS but doesn't affect the BAR limitation
			 * above.
			 */
			if ((bytes == 4U) && ((offset & 0x3U) == 0U)) {
				if ((val == ((uint32_t)~0U)) ||
				    (val == vdev->pdev->vf_bars[(reg - PCIR_SRIOV_VF_BAR_OFF)/4])) {
					pci_pdev_write_cfg(vdev->pdev->bdf, offset, bytes, val);
				}
			}
		} else {
			pci_pdev_write_cfg(vdev->pdev->bdf, offset, bytes, val);
		}
	}
}

/**
 * @brief Get PCI configure space register offset for a VF BAR according to its BAR index
 *
 * @param[in] vdev    Pointer to virtual PCI device.
 * @param[in] bar_idx VF BAR ID to get offset.
 *
 * @return VF BAR offset
 *
 * @pre vdev != NULL
 */
uint32_t sriov_bar_offset(const struct pci_vdev *vdev, uint32_t bar_idx)
{
	return (vdev->sriov.capoff + PCIR_SRIOV_VF_BAR_OFF + (bar_idx << 2U));
}

/**
 * @}
 */
