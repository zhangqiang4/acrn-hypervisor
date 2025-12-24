/*-
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
#include <errno.h>
#include <ptdev.h>
#include <asm/guest/assign.h>
#include <asm/vtd.h>
#include <asm/guest/ept.h>
#include <asm/mmu.h>
#include <asm/io.h>
#include <logmsg.h>
#include <config.h>
#include "vpci_priv.h"

/**
 * @addtogroup vp-dm_vpci
 *
 * @{
 */

/**
 * @file
 * @brief This file declares functions to operate passthrough PCI device configuration space
 *
 * This file declares functions to initialize/deinit passthrough PCI device, operate BAR/MSI-X table and so on.
 *
 */

/**
 * @brief Get physical MSI-X table entry
 *
 * This function is used to get a physical MSI-X table entry according to input index.
 *
 * @param[in] vdev  Pointer to virtual PCI device to get physical MSI-X table entry.
 * @param[in] index Unsigned integer to indicate the MSI-X table entry index.
 *
 * @return A pointer points to the MSI-X table entry memory address.
 *
 * @pre vdev != NULL
 */
static inline struct msix_table_entry *get_msix_table_entry(const struct pci_vdev *vdev, uint32_t index)
{
	void *hva = hpa2hva(vdev->msix.mmio_hpa + vdev->msix.table_offset);

	return ((struct msix_table_entry *)hva + index);
}

/**
 * @brief Write MSI-X capability structure
 *
 * This function emulates MSI-X capability structrue write. It will first tries to write value into
 * the virtual MSI-X register if it writes writable MSI-X register (Only Function Mask and MSI-X Enable
 * bits). If this write operation writes writable register, this function would disable INTx when the
 * guest tries to enable MSI-X and write the virtual Message Control register into physical Message
 * Control register.
 *
 * @param[in] vdev   Pointer to virtual PCI device to write MSI-X capability structure.
 * @param[in] offset Offset to the device MSI-X capability structure.
 * @param[in] bytes  Size to write the device MSI-X capability structure.
 * @param[in] val    Value to be written into the register.
 *
 * @return None.
 *
 * @pre vdev != NULL
 * @pre vdev->pdev != NULL
 */
void write_pt_vmsix_cap_reg(struct pci_vdev *vdev, uint32_t offset, uint32_t bytes, uint32_t val)
{
	uint32_t msgctrl;

	if (write_vmsix_cap_reg(vdev, offset, bytes, val)) {
		msgctrl = pci_vdev_read_vcfg(vdev, vdev->msix.capoff + PCIR_MSIX_CTRL, 2U);
		/* If MSI Enable is being set, make sure INTxDIS bit is set */
		if ((msgctrl & PCIM_MSIXCTRL_MSIX_ENABLE) != 0U) {
			enable_disable_pci_intx(vdev->pdev->bdf, false);
		}
		pci_pdev_write_cfg(vdev->pdev->bdf, vdev->msix.capoff + PCIR_MSIX_CTRL, 2U, msgctrl);
	}
}

/**
 * @brief Set mask bit of vector control register
 *
 * This function is used to mask a MSI-X Interrupt by setting mask bit in physical vector control
 * register in a MSI-X table entry.
 *
 * @param[in] vdev  Pointer to virtual PCI device to set mask bit of vector control register.
 * @param[in] index Index of the MSI-X table entry.
 *
 * @return None.
 *
 * @pre vdev != NULL
 */
static void mask_one_msix_vector(const struct pci_vdev *vdev, uint32_t index)
{
	uint32_t vector_control;
	struct msix_table_entry *pentry = get_msix_table_entry(vdev, index);

	stac();
	vector_control = pentry->vector_control | PCIM_MSIX_VCTRL_MASK;
	mmio_write32(vector_control, (void *)&(pentry->vector_control));
	clac();
}


/**
 * @brief Do Interrupt Remapping for MSI-X
 *
 * This function uses VTD IRTE to do Interrupt Remapping for one MSI-X table entry when guest trying to
 * enable this MSI-X table entry. It will first disable corresponding physical MSI-X table entry by setting
 * physical Vector Control register Mask Bit. Then, it uses VTD IRTE to do the Interrupt Remapping. If
 * remapping succeeds, it update physical MSI-X table entry in Remappable format (refer to Chap 5.1.5.2
 * MSI and MSI-X Register Programming, Intel® Virtualization Technology for Directed I/O Revision 5.0)
 * and enable corresponding physical MSI-X table entry.
 *
 * @param[in] vdev  Pointer to virtual PCI device to do interrupt remapping. 
 * @param[in] index Index of the MSI-X table entry.
 *
 * @return None.
 *
 * @pre vdev != NULL
 * @pre vdev->vpci != NULL
 * @pre vdev->pdev != NULL
 *
 * @remark This function is called when MSI is disabled.
 */
static void remap_one_vmsix_entry(struct pci_vdev *vdev, uint32_t index)
{
	const struct msix_table_entry *ventry;
	struct msix_table_entry *pentry;
	struct msi_info info = {};
	int32_t ret;

	mask_one_msix_vector(vdev, index);
	ventry = &vdev->msix.table_entries[index];
	if ((ventry->vector_control & PCIM_MSIX_VCTRL_MASK) == 0U) {
		info.addr.full = vdev->msix.table_entries[index].addr;
		info.data.full = vdev->msix.table_entries[index].data;

		ret = ptirq_prepare_msix_remap(vpci2vm(vdev->vpci), vdev->bdf.value, vdev->pdev->bdf.value,
					       (uint16_t)index, &info);
		if (ret == 0) {
			/* Write the table entry to the physical structure */
			pentry = get_msix_table_entry(vdev, index);

			/*
			 * PCI 3.0 Spec allows writing to Message Address and Message Upper Address
			 * fields with a single QWORD write, but some hardware can accept 32 bits
			 * write only
			 */
			stac();
			mmio_write32((uint32_t)(info.addr.full), (void *)&(pentry->addr));
			mmio_write32((uint32_t)(info.addr.full >> 32U),
						(void *)((char *)&(pentry->addr) + 4U));

			mmio_write32(info.data.full, (void *)&(pentry->data));
			mmio_write32(vdev->msix.table_entries[index].vector_control,
				     (void *)&(pentry->vector_control));
			clac();
		}
	}

}

/**
 * @brief Handle guest's mmio access to the MSI-X table
 *
 * This function is used to handle guest's mmio access to the MSI-X table. It will first check whether
 * the user of the vdev is current VM, e.g. a vdev once owned by Service VM is assigned to a post-launched
 * VM, we need make sure the user is the vdev allocated for the post-launched VM. If the check fails,
 * return EFAULT back. Otherwise, read/write MSI-X table and do the Interrupt Remapping if the guest
 * is doing write and the table entry index is within the range.
 *
 * @param[inout] io_req    Pointer to the IO request from VM.
 * @param[in]    priv_data Private data which should points to the virtual device to access mmio.
 *
 * @return An integer to indicate whether the operation is successful or not.
 *
 * @retval 0       Success.
 * @retval -EFAULT Fail because the device is not passthrough PCI device.
 *
 * @pre io_req != NULL
 * @pre priv_data != NULL
 */
static int32_t pt_vmsix_handle_table_mmio_access(struct io_request *io_req, void *priv_data)
{
	struct acrn_mmio_request *mmio = &io_req->reqs.mmio_request;
	struct pci_vdev *vdev;
	uint32_t index;
	int32_t ret = 0;

	vdev = (struct pci_vdev *)priv_data;
	if (vdev->user == vdev) {
		index = rw_vmsix_table(vdev, io_req);

		if ((mmio->direction == ACRN_IOREQ_DIR_WRITE) && (index < vdev->msix.table_count)) {
			remap_one_vmsix_entry(vdev, index);
		}
	} else {
		ret = -EFAULT;
	}

	return ret;
}

/**
 * @brief Unmap MSI-X table
 *
 * This function is used to unmap a virtual PCI device's MSI-X table. Firstly, it clears
 * the virtual MSI-X table content and set the mask bit of vector control register to 1
 * to disable sending message. Then, it unregisters the mmio handler for the MSI-X table
 * corresponding GPA to avoid handling the mmio access.
 *
 * @param[in] vdev Pointer to virtual PCI device to unmap MSI-X table.
 *
 * @return None.
 *
 * @pre vdev != NULL
 */
static void vdev_pt_unmap_msix(struct pci_vdev *vdev)
{
	uint32_t i;
	uint64_t addr_hi, addr_lo;
	struct pci_msix *msix = &vdev->msix;

	/* Mask all table entries */
	for (i = 0U; i < msix->table_count; i++) {
		msix->table_entries[i].vector_control = PCIM_MSIX_VCTRL_MASK;
		msix->table_entries[i].addr = 0U;
		msix->table_entries[i].data = 0U;
	}

	if (msix->mmio_gpa != 0UL) {
		addr_lo = msix->mmio_gpa + msix->table_offset;
		addr_hi = addr_lo + (msix->table_count * MSIX_TABLE_ENTRY_SIZE);
		addr_lo = round_page_down(addr_lo);
		addr_hi = round_page_up(addr_hi);
		unregister_mmio_emulation_handler(vpci2vm(vdev->vpci), addr_lo, addr_hi);
		msix->mmio_gpa = 0UL;
	}
}

/**
 * @brief Map MSI-X table
 *
 * This function is used to map a virtual PCI device's MSI-X table. If the MSI-X table corresponding
 * BAR's GPA is valid (not zero), it registers the mmio handler (pt_vmsix_handle_table_mmio_access) for
 * the MSI-X table corresponding GPA to handle the mmio access from VM and removes such mmio region from
 * the EPT and DMAR mapping.
 *
 * @param[in] vdev      Pointer to virtual PCI device map MSI-X table.
 * @param[in] hold_lock A boolean to indicate whether it is required to hold lock when
 *                      handling the mmio access.
 *
 * @return None.
 *
 * @pre vdev != NULL
 * @pre vdev->vpci != NULL
 */
void vdev_pt_map_msix(struct pci_vdev *vdev, bool hold_lock)
{
	struct pci_vbar *vbar;
	uint64_t addr_hi, addr_lo;
	struct pci_msix *msix = &vdev->msix;

	vbar = &vdev->vbars[msix->table_bar];
	if (vbar->base_gpa != 0UL) {
		struct acrn_vm *vm = vpci2vm(vdev->vpci);

		addr_lo = vbar->base_gpa + msix->table_offset;
		addr_hi = addr_lo + (msix->table_count * MSIX_TABLE_ENTRY_SIZE);
		addr_lo = round_page_down(addr_lo);
		addr_hi = round_page_up(addr_hi);
		register_mmio_emulation_handler(vm, pt_vmsix_handle_table_mmio_access,
				addr_lo, addr_hi, vdev, hold_lock);
		ept_del_mr(vm, (uint64_t *)vm->arch_vm.nworld_eptp, addr_lo, addr_hi - addr_lo);
		msix->mmio_gpa = vbar->base_gpa;
	}
}

/**
 * @brief Unmap memory mapping for a BAR of a virtual PCI device which points to a passthrough PCI device
 *
 * This function is used to unmap a memory mapped for a BAR of a virtual PCI device which points to a
 * passthrough PCI device by deleting the memory mapping in EPT table and DMAR table. It also unregisters
 * MSI-X Table BAR handler by unmapping the MSI-X table if the virtual PCI device has MSI-X capability
 * and the current vBAR is a MSI-X Table BAR.
 *
 * @param[in] vdev Pointer to virtual PCI device unmap a vBAR.
 * @param[in] idx  Index of the vBAR.
 *
 * @return None.
 *
 * @pre vdev != NULL
 * @pre vdev->vpci != NULL
 */
static void vdev_pt_unmap_mem_vbar(struct pci_vdev *vdev, uint32_t idx)
{
	struct pci_vbar *vbar = &vdev->vbars[idx];

	if (vbar->base_gpa != 0UL) {
		struct acrn_vm *vm = vpci2vm(vdev->vpci);

		ept_del_mr(vm, (uint64_t *)(vm->arch_vm.nworld_eptp),
			vbar->base_gpa, /* GPA (old vbar) */
			vbar->size);
	}

	if ((has_msix_cap(vdev) && (idx == vdev->msix.table_bar))) {
		vdev_pt_unmap_msix(vdev);
	}
}

/**
 * @brief Map memory for a BAR of a virtual PCI device which points to a passthrough PCI device
 *
 * This function is used to map memory mapping for a BAR of a virtual PCI device which points to a passthrough
 * PCI device by adding the memory mapping in EPT table and DMAR table. It also register MSI-X Table BAR handler
 * by mapping the MSI-X table (delete the MSI-X Table memory mapping and register a MSI-X Table MMIO access
 * handler) if the virtual PCI device has MSI-X capability and the current vBAR is a MSI-X Table BAR.
 *
 * @param[in] vdev Pointer to virtual PCI device map a vBAR.
 * @param[in] idx  Index of the vBAR.
 *
 * @return None.
 *
 * @pre vdev != NULL
 * @pre vdev->vpci != NULL
 */
static void vdev_pt_map_mem_vbar(struct pci_vdev *vdev, uint32_t idx)
{
	struct pci_vbar *vbar = &vdev->vbars[idx];

	if (vbar->base_gpa != 0UL) {
		struct acrn_vm *vm = vpci2vm(vdev->vpci);

		ept_add_mr(vm, (uint64_t *)(vm->arch_vm.nworld_eptp),
			vbar->base_hpa, /* HPA (pbar) */
			vbar->base_gpa, /* GPA (new vbar) */
			vbar->size,
			EPT_WR | EPT_RD | EPT_UNCACHED);
	}

	if (has_msix_cap(vdev) && (idx == vdev->msix.table_bar)) {
		vdev_pt_map_msix(vdev, true);
	}
}

/**
 * @brief Allow IO BAR access
 *
 * This function is responsible for allowing I/O port access (set I/O bitmaps to enable direct access
 * from the given vm to the port I/O space) for a PIO BAR of a virtual PCI device if the VM is not
 * Service VM.
 *
 * @param[in] vdev Pointer to virtual PCI device to allow IO BAR access.
 * @param[in] idx  Index of the vBAR.
 *
 * @return None.
 *
 * @pre vdev != NULL
 * @pre vdev->vpci != NULL
 */
static void vdev_pt_allow_io_vbar(struct pci_vdev *vdev, uint32_t idx)
{
	struct acrn_vm *vm = vpci2vm(vdev->vpci);

	/* For Service VM, all port IO access is allowed by default, so skip Service VM here */
	if (!is_service_vm(vm)) {
		struct pci_vbar *vbar = &vdev->vbars[idx];
		if (vbar->base_gpa != 0UL) {
			allow_guest_pio_access(vm, (uint16_t)vbar->base_gpa, (uint32_t)(vbar->size));
		}
	}
}

/**
 * @brief Deny IO BAR access
 *
 * This function is responsible for denying I/O port access (set I/O bitmaps to disable direct access
 * from the given vm to the port I/O space) for a PIO BAR of a virtual PCI device if the VM is not
 * Service VM.
 *
 * @param[in] vdev Pointer to virtual PCI device deny IO BAR access.
 * @param[in] idx  Index of the vBAR.
 *
 * @return None.
 *
 * @pre vdev != NULL
 * @pre vdev->vpci != NULL
 */
static void vdev_pt_deny_io_vbar(struct pci_vdev *vdev, uint32_t idx)
{
	struct acrn_vm *vm = vpci2vm(vdev->vpci);

	/* For Service VM, all port IO access is allowed by default, so skip Service VM here */
	if (!is_service_vm(vm)) {
		struct pci_vbar *vbar = &vdev->vbars[idx];
		if (vbar->base_gpa != 0UL) {
			deny_guest_pio_access(vm, (uint16_t)(vbar->base_gpa), (uint32_t)(vbar->size));
		}

	}
}

/**
 * @brief Emulate write to a vBAR of a virtual PCI device
 *
 * This function updates the specified virtual BAR of a virtual PCI device with the given value. It
 * handles both I/O and memory BARs, updating the necessary mappings and permissions.
 *
 * For a virtual PCI device which points to a passthrough PCI device, we could not just allow the Guest OS
 * to write the guest physical address of a vBAR to the physical BAR as a host physical address. This
 * function emulates the BAR writing action. For a PIO BAR in non-service vm, we would first deny the old
 * PIO BAR access, then update the new virtual PIO BAR. If the new virtual PIO BAR is valid (the new virtual
 * PIO BAR is equal to the old virtual PIO BAR which means only support guest OS PIO BAR sizing, not support
 * guest OS PIO BAR re-programming), we would allow the PIO access. For MMIO BAR, we would unmap memory
 * mapping for the old MMIO BAR, then map memory of the new virtual MMIO BAR.
 *
 * @param[in] vdev Pointer to virtual PCI device to write to a vBAR.
 * @param[in] idx  Index of the vBAR.
 * @param[in] val  Value to be written to the vBAR.
 *
 * @return None.
 *
 * @pre vdev != NULL
 */
void vdev_pt_write_vbar(struct pci_vdev *vdev, uint32_t idx, uint32_t val)
{
	struct pci_vbar *vbar = &vdev->vbars[idx];

	if (is_pci_io_bar(vbar)) {
		vpci_update_one_vbar(vdev, idx, val, vdev_pt_allow_io_vbar, vdev_pt_deny_io_vbar);
	} else {
		/* pci mem bar */
		vpci_update_one_vbar(vdev, idx, val, vdev_pt_map_mem_vbar, vdev_pt_unmap_mem_vbar);
	}
}

/**
 * @brief Restore IO Base/Limit and Memory Base/Limit for a virtual PCI bridge device
 *
 * This function restores IO Base and Memory Base to the value saved in virtual configuration space
 * of a virtual PCI device which points to a passthrough PCI bridge.
 *
 * @param[in] vdev Pointer to virtual PCI bridge device to restore the IO Base and Memory Base.
 *
 * @return None.
 *
 * @pre vdev != NULL
 * @pre vdev->pdev != NULL
 */
void vdev_bridge_pt_restore_space(struct pci_vdev *vdev)
{
	struct pci_pdev *pdev = vdev->pdev;
	uint32_t pre_val;
	uint32_t offset;

	/* I/O Base (0x1c) and I/O Limit (0x1d) */
	pre_val = pci_vdev_read_vcfg(vdev, PCIR_IO_BASE, 2U);
	if (pre_val != pci_pdev_read_cfg(vdev->pdev->bdf, PCIR_IO_BASE, 2U)) {
		pci_pdev_write_cfg(pdev->bdf, PCIR_IO_BASE, 2U, pre_val);
	}

	/* From Memory Base (0x20) to I/O Base Limit 16 Bits (0x32) */
	for (offset = PCIR_MEM_BASE; offset < PCIR_IO_BASE_UPPER_16; offset += 4) {
		pre_val = pci_vdev_read_vcfg(vdev, offset, 4U);
		if (pre_val != pci_pdev_read_cfg(vdev->pdev->bdf, offset, 4U)) {
			pci_pdev_write_cfg(pdev->bdf, offset, 4U, pre_val);
		}
	}
}

/**
 * @brief Restore Bus Number for a virtual PCI bridge device
 *
 * This function restores the Primary Bus Number, Secondary Bus Number and Subordinate Bus Number to
 * the value saved in virtual configuration space of a virtual PCI device which points to a passthrough
 * PCI bridge.
 *
 * @param[in] vdev Pointer to virtual PCI bridge device to restore Bus Number.
 *
 * @return None.
 *
 * @pre vdev != NULL
 * @pre vdev->pdev != NULL
 */
void vdev_bridge_pt_restore_bus(struct pci_vdev *vdev)
{
	struct pci_pdev *pdev = vdev->pdev;
	uint32_t pre_val;

	/* Primary Bus Number (0x18) and Secondary Bus Number (0x19) */
	pre_val = pci_vdev_read_vcfg(vdev, PCIR_PRIBUS_1, 2U);
	if (pre_val != pci_pdev_read_cfg(vdev->pdev->bdf, PCIR_PRIBUS_1, 2U)) {
		pci_pdev_write_cfg(pdev->bdf, PCIR_PRIBUS_1, 2U, pre_val);
	}

	/* Subordinate Bus Number (0x1a) */
	pre_val = pci_vdev_read_vcfg(vdev, PCIR_SUBBUS_1, 1U);
	if (pre_val != pci_pdev_read_cfg(vdev->pdev->bdf, PCIR_SUBBUS_1, 1U)) {
		pci_pdev_write_cfg(pdev->bdf, PCIR_SUBBUS_1, 1U, pre_val);
	}
}

/**
 * @brief Initialize the Base Address Registers (BARs) of a virtual PCI device
 *
 * This function is responsible for initializing the Base Address Registers (BARs) of a virtual PCI device
 * which points to a passthrough PCI device. This includes setting up the BARs by recording their information
 * (refer to struct pci_vbar for the BAR details that need to be captured) for both standard PCI devices and
 * SRIOV VF devices. Additionally, it also recode the MSI-X Table BAR HPA and size if the device has MSI-X
 * capability.
 *
 * If the is_sriov_bar is false, init_bars is used to initialize normal PCI BARs for the virtual PCI device.
 * If the is_sriov_bar is true, init_bars is used to initialize SRIOV VF BARs for the virtual PCI device.
 * It gets the BAR value and size by accessing the physical PCI device register and update the value to
 * virtual configuration space. Refer PCIe spec r5.0 Chapter 7.5.2.1.
 *
 * The is_sriov_bar should be false if the virtual PCI device doesn't have SRIOV capability.
 *
 * @param[in] vdev         Pointer to virtual PCI device to initialize the BARs.
 * @param[in] is_sriov_bar A boolean value to indicate whether the accessed BARs are SRIOV VF BARs.
 *
 * @return None.
 *
 * @pre vdev != NULL
 * @pre vdev->vpci != NULL
 * @pre vdev->pdev != NULL
 */
static void init_bars(struct pci_vdev *vdev, bool is_sriov_bar)
{
	uint32_t idx, bar_cnt;
	struct pci_vbar *vbar;
	uint32_t size32, offset, lo, hi = 0U;
	union pci_bdf pbdf;
	uint64_t mask;

	if (is_sriov_bar) {
		bar_cnt = PCI_BAR_COUNT;
	} else {
		vdev->nr_bars = vdev->pdev->nr_bars;
		bar_cnt = vdev->nr_bars;
	}
	pbdf.value = vdev->pdev->bdf.value;

	for (idx = 0U; idx < bar_cnt; idx++) {
		if (is_sriov_bar) {
			vbar = &vdev->sriov.vbars[idx];
			offset = sriov_bar_offset(vdev, idx);
		} else {
			vbar = &vdev->vbars[idx];
			offset = pci_bar_offset(idx);
		}
		lo = pci_pdev_read_cfg(pbdf, offset, 4U);
		vbar->bar_type.bits = lo;

		if (is_pci_reserved_bar(vbar)) {
			continue;
		}

		if (is_pci_io_bar(vbar)) {
			if ((lo & ~IO_SPACE_BITMASK) != 0U) {
				/*
				 * Some buggy x86 BIOS may program an invalid I/O BAR whose upper 16 bits
				 * are not zero. Such I/O BAR is not addressable on x86 platforms. Skip
				 * it when initializing the virtual PCI function as I/O BAR reprogramming
				 * in VM is currently unsupported.
				 */
				pr_warn("%s: %02x:%02x.%x: IO BAR%d value 0x%08x has invalid bits, "
					"IO_SPACE_BITMASK is 0x%08x, Ignore this BAR in vdev",
					__func__, vdev->bdf.bits.b, vdev->bdf.bits.d, vdev->bdf.bits.f, idx, lo,
					IO_SPACE_BITMASK);
				continue;
			}
			mask = PCI_BASE_ADDRESS_IO_MASK;
		} else {
			mask = PCI_BASE_ADDRESS_MEM_MASK;
		}
		vbar->base_hpa = (uint64_t)lo & mask;

		if (is_pci_mem64lo_bar(vbar)) {
			if ((idx + 1U) < bar_cnt) {
				hi = pci_pdev_read_cfg(pbdf, offset + 4U, 4U);
				vbar->base_hpa |= ((uint64_t)hi << 32U);
			} else {
				pr_err("%02x:%02x.%x: Failed to init 64-bit BAR%d due to no high 32-bit address.",
					vdev->bdf.bits.b, vdev->bdf.bits.d, vdev->bdf.bits.f, idx);
				vbar->base_hpa = 0UL;
			}
		}

		if (vbar->base_hpa != 0UL) {
			pci_pdev_write_cfg(pbdf, offset, 4U, ~0U);
			size32 = pci_pdev_read_cfg(pbdf, offset, 4U);
			pci_pdev_write_cfg(pbdf, offset, 4U, lo);

			vbar->mask = size32 & mask;
			vbar->bar_type.bits &= (uint32_t)(~mask);
			vbar->size = (uint64_t)size32 & mask;

			if (is_prelaunched_vm(vpci2vm(vdev->vpci))) {
				lo = (uint32_t)vdev->pci_dev_config->vbar_base[idx];
			}

			if (is_pci_mem64lo_bar(vbar)) {
				idx++;
				if (is_sriov_bar) {
					offset = sriov_bar_offset(vdev, idx);
				} else {
					offset = pci_bar_offset(idx);
				}
				pci_pdev_write_cfg(pbdf, offset, 4U, ~0U);
				size32 = pci_pdev_read_cfg(pbdf, offset, 4U);
				pci_pdev_write_cfg(pbdf, offset, 4U, hi);

				vbar->size |= ((uint64_t)size32 << 32U);
				vbar->size = vbar->size & ~(vbar->size - 1UL);
				vbar->size = round_page_up(vbar->size);

				if (is_sriov_bar) {
					vbar = &vdev->sriov.vbars[idx];
				} else {
					vbar = &vdev->vbars[idx];
				}

				vbar->mask = size32;
				vbar->is_mem64hi = true;

				if (is_prelaunched_vm(vpci2vm(vdev->vpci))) {
					hi = (uint32_t)(vdev->pci_dev_config->vbar_base[idx - 1U] >> 32U);
				}
				/* if it is parsing SRIOV VF BARs, no need to write vdev bars */
				if (!is_sriov_bar) {
					pci_vdev_write_vbar(vdev, idx - 1U, lo);
					pci_vdev_write_vbar(vdev, idx, hi);
				}
			} else {
				vbar->size = vbar->size & ~(vbar->size - 1UL);
				if (is_pci_mem32_bar(vbar)) {
					vbar->size = round_page_up(vbar->size);
				}

				/* if it is parsing SRIOV VF BARs, no need to write vdev bar */
				if (!is_sriov_bar) {
					pci_vdev_write_vbar(vdev, idx, lo);
				}
			}
		}
	}

	/* Initialize MSIx mmio hpa and size after BARs initialization */
	if (has_msix_cap(vdev) && (!is_sriov_bar)) {
		vdev->msix.mmio_hpa = vdev->vbars[vdev->msix.table_bar].base_hpa;
		vdev->msix.mmio_size = vdev->vbars[vdev->msix.table_bar].size;
	}
}

/**
 * @brief Initialize MSI-X capability for a virtual PCI device which points to a passthrough PCI device
 *
 * This function initializes the MSI-X capability structure for a given virtual PCI device by caching MSI-X
 * Capability Structure in virtual configure space and recording their information (refer to struct pci_msix
 * for the MSI-X details that need to be captured). Refer PCIe spec r5.0 Chapter 7.7.2.
 *
 * @param[in] vdev Pointer to virtual PCI device.
 *
 * @return None.
 *
 * @pre vdev != NULL
 * @pre vdev->pdev != NULL
 */
void init_vmsix_pt(struct pci_vdev *vdev)
{
	struct pci_pdev *pdev = vdev->pdev;

	vdev->msix.capoff = pdev->msix.capoff;
	vdev->msix.caplen = pdev->msix.caplen;
	vdev->msix.table_bar = pdev->msix.table_bar;
	vdev->msix.table_offset = pdev->msix.table_offset;
	vdev->msix.table_count = pdev->msix.table_count;

	if (has_msix_cap(vdev)) {
		(void)memcpy_s((void *)&vdev->cfgdata.data_8[pdev->msix.capoff], pdev->msix.caplen,
			(void *)&pdev->msix.cap[0U], pdev->msix.caplen);
	}
}

/**
 * @brief Deinitialize MSI-X capability for a virtual PCI device which points to a passthrough PCI device
 *
 * This function removes MSI-X Interrupt Remapping from VTD IRTE and clears the MSI-X table entries of a
 * given virtual PCI device.
 *
 * @param[in] vdev Pointer to virtual PCI device.
 *
 * @return None.
 *
 * @pre vdev != NULL
 * @pre vdev->vpci != NULL
 */
void deinit_vmsix_pt(struct pci_vdev *vdev)
{
	if (has_msix_cap(vdev)) {
		if (vdev->msix.table_count != 0U) {
			ptirq_remove_msix_remapping(vpci2vm(vdev->vpci), vdev->pdev->bdf.value,
						    vdev->msix.table_count);
			(void)memset((void *)&vdev->msix.table_entries, 0U, sizeof(vdev->msix.table_entries));
		}
	}
}

/**
 * @brief Passthrough GPU OpRegion to the guest
 *
 * This function maps the GPU OpRegion from the host to the guest's address space. When a GPU physical device
 * is assigned to guest, the OpRegion should be passthrough too so that guest can communicates with the GPU.
 * The memory of the OpRegion is added into EPT and DMAR mapping so that the guest can access it directly.
 * Furthermore, the OpRegion GPA is written into virtual PCIR_ASLS_CTL register.
 *
 * @param[in] vdev Pointer to virtual PCI device.
 *
 * @return None.
 *
 * @pre vdev != NULL
 * @pre vdev->vpdev != NULL
 * @pre vdev->vpci != NULL
 */
static void passthru_gpu_opregion(struct pci_vdev *vdev)
{
	uint32_t gpu_opregion_hpa, gpu_opregion_gpa, gpu_asls_phys;

	/* TODO:
	 * The OpRegion is not 4KB aligned, while under some platforms, it will take up to 16KB. In this case,
	 * OpRegion overlay 5 pages.So set GPU_OPREGION_SIZE to 0x5000U(20KB) here.
	 */
	gpu_opregion_gpa = GPU_OPREGION_GPA;
	gpu_asls_phys = pci_pdev_read_cfg(vdev->pdev->bdf, PCIR_ASLS_CTL, 4U);
	gpu_opregion_hpa = gpu_asls_phys & PCIM_ASLS_OPREGION_MASK;
	ept_add_mr(vpci2vm(vdev->vpci), vpci2vm(vdev->vpci)->arch_vm.nworld_eptp,
			gpu_opregion_hpa, gpu_opregion_gpa,
			GPU_OPREGION_SIZE, EPT_RD | EPT_UNCACHED);
	pci_vdev_write_vcfg(vdev, PCIR_ASLS_CTL, 4U, gpu_opregion_gpa | (gpu_asls_phys & ~PCIM_ASLS_OPREGION_MASK));
}

/**
 * @brief Initialize a specified virtual PCI device structure which points to a passthrough PCI device
 *
 * The function is used to initialize a virtual PCI device structure. First, it copies physical PCI Configuration
 * Space Header to virtual PCI Configuration Space. Then, it does further initialization in different ways.
 *
 * If the given vdev represents a standard passthrough PCI device:
 *   - initialize BARs by recording their information (refer to struct pci_vbar for the BAR details that need to
 *     be captured).
 *   - copy physical PCIR_ASLS_CTL register value to virtual register if the passthrough PCI device target VM is
 *     Service VM and the device is CONFIG_IGD_SBDF.
 *   - if the passthrough PCI device target VM is pre-launched VM and the device is not SRIOV VF, disable INTx on
 *     physical device. Then, passthrough GPU OpRegion to guest if the device BDF is CONFIG_IGD_SBDF.
 *
 * If the given vdev represents a passthrough SR-IOV virtual function(VF):
 *   - if the VF is assigned to a VM, initialize the Vendor ID and Device ID in VF's virtual Configuration Space
 *     by reading the corresponding physical device's Vendor ID and Device ID. Furthermore, set the number of BARs
 *     to PCI_BAR_COUNT.
 *   - if the VF is unassigned, i.e. when VF was first created, map the vBARs one by one by adding the memory of
 *     vBAR to the EPT and DMAR mapping. Then, it maps the MSI-X table if the vBAR points to the MSI-X table and
 *     device supports the MSI-X.
 *
 * @param[in] vdev       Pointer to virtual PCI device data structure to initialize.
 * @param[in] is_pf_vdev A bool value to indicate whether the first parameter vdev is the data
 *                       structure of a PF, which contains the SRIOV capability.
 *
 * @return None.
 *
 * @pre vdev != NULL
 * @pre vdev->vpci != NULL
 * @pre vdev->pdev != NULL
 */
void init_vdev_pt(struct pci_vdev *vdev, bool is_pf_vdev)
{
	uint16_t pci_command;
	uint32_t offset;

	for (offset = 0U; offset < PCI_CFG_HEADER_LENGTH; offset += 4U) {
		pci_vdev_write_vcfg(vdev, offset, 4U, pci_pdev_read_cfg(vdev->pdev->bdf, offset, 4U));
	}

	/* Construct an unrecognizabel vender id, so that guest os treats it as a generic pci device.
	 * This is useful when we need the guest OS manage the pdev's pm and link state.
	 */
	if (vdev->flags & ACRN_PTDEV_PM_STUB) {
		pci_vdev_write_vcfg(vdev, PCIR_VENDOR, 2U, 0x0U);
		pci_vdev_write_vcfg(vdev, PCIV_SUB_VENDOR_ID, 2U, 0x0U);
		pci_vdev_write_vcfg(vdev, PCIV_SUB_SYSTEM_ID, 2U, 0x0U);
	}

	/* Initialize the vdev BARs except SRIOV VF, VF BARs are initialized directly from create_vf function */
	if (vdev->phyfun == NULL) {
		init_bars(vdev, is_pf_vdev);
		if (is_service_vm(vpci2vm(vdev->vpci)) && (vdev->pdev->bdf.value == CONFIG_IGD_SBDF)) {
			pci_vdev_write_vcfg(vdev, PCIR_ASLS_CTL, 4U, pci_pdev_read_cfg(vdev->pdev->bdf,
					    PCIR_ASLS_CTL, 4U));
		}
		if (is_prelaunched_vm(vpci2vm(vdev->vpci)) && (!is_pf_vdev)) {
			pci_command = (uint16_t)pci_pdev_read_cfg(vdev->pdev->bdf, PCIR_COMMAND, 2U);

			/* Disable INTX */
			pci_command |= 0x400U;
			pci_pdev_write_cfg(vdev->pdev->bdf, PCIR_COMMAND, 2U, pci_command);

			if (vdev->pdev->bdf.value == CONFIG_IGD_SBDF) {
				passthru_gpu_opregion(vdev);
			}
		}
	} else {
		if (vdev->phyfun->vpci != vdev->vpci) {
			/* VF is assigned to a User VM */
			uint32_t vid, did;

			vdev->nr_bars = PCI_BAR_COUNT;
			/* SRIOV VF Vendor ID and Device ID initialization */
			vid = pci_pdev_read_cfg(vdev->phyfun->bdf, PCIR_VENDOR, 2U);
			did = pci_pdev_read_cfg(vdev->phyfun->bdf,
				(vdev->phyfun->sriov.capoff + PCIR_SRIOV_VF_DEV_ID), 2U);
			pci_vdev_write_vcfg(vdev, PCIR_VENDOR, 2U, vid);
			pci_vdev_write_vcfg(vdev, PCIR_DEVICE, 2U, did);
		} else {
			/* VF is unassigned: when VF was first created, the VF's BARs hasn't been assigned */
			uint32_t bar_idx;

			for (bar_idx = 0U; bar_idx < vdev->nr_bars; bar_idx++) {
				vdev_pt_map_mem_vbar(vdev, bar_idx);
			}
		}
	}
}

/**
 * @brief Destruct a specified virtual PCI device structure which points to a passthrough PCI device
 *
 * This function is the destructor corresponding to the function init_vdev_pt. It unmaps the vdev's vBARs
 * one by one by deleting the memory mapping for a vBAR from guest address space.
 *
 * @param[in] vdev Pointer to virtual PCI device data structure.
 *
 * @return None.
 *
 * @pre vdev != NULL
 */
void deinit_vdev_pt(struct pci_vdev *vdev) {

	uint32_t bar_idx;

	for (bar_idx = 0U; bar_idx < vdev->nr_bars; bar_idx++) {
		vdev_pt_unmap_mem_vbar(vdev, bar_idx);
	}
}

/**
 * @}
 */
