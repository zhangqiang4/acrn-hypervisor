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
#include "vpci_priv.h"
#include <asm/guest/ept.h>
#include <asm/guest/virq.h>
#include <logmsg.h>
#include <hash.h>

/**
 * @defgroup vp-dm_vpci vp-dm.vpci
 * @ingroup vp-dm
 * @brief Implementation of virtual PCI related functions.
 *
 * This module provides the interfaces to interact with virtual PCI devices.
 * These operations are essential to handle devices under virtualization
 * environment.
 *
 * @{
 */

/**
 * @file
 * @brief Functions to operate virtual PCI device
 *
 * This file implements functions to operate virtual PCI device configuration space.
 * These operations involve configuration space registers read/write, BAR update and
 * so on.
 */

/**
 * @brief Read a virtual PCI device Config Space
 *
 * @param[in] vdev   Pointer to the virtual PCI device which config space need to read.
 * @param[in] offset The offset in the config space to read.
 * @param[in] bytes  The size to read.
 *
 * @return The config value to read.
 *
 * @pre vdev != NULL
 * @pre bytes == 1 || bytes == 2 || bytes == 4
 *
 * @remark N/A
 */
uint32_t pci_vdev_read_vcfg(const struct pci_vdev *vdev, uint32_t offset, uint32_t bytes)
{
	uint32_t val;

	switch (bytes) {
	case 1U:
		val = vdev->cfgdata.data_8[offset];
		break;
	case 2U:
		val = vdev->cfgdata.data_16[offset >> 1U];
		break;
	default:
		val = vdev->cfgdata.data_32[offset >> 2U];
		break;
	}

	return val;
}

/**
 * @brief Write a virtual PCI device Config Space
 *
 * @param[inout] vdev   Pointer to the virtual PCI device which config space need to write.
 * @param[in]    offset The offset in the config space to write.
 * @param[in]    bytes  The size to write.
 * @param[in]    val    The value to write.
 *
 * @return None.
 *
 * @pre vdev != NULL
 * @pre bytes == 1 || bytes == 2 || bytes == 4
 *
 * @remark N/A
 */
void pci_vdev_write_vcfg(struct pci_vdev *vdev, uint32_t offset, uint32_t bytes, uint32_t val)
{
	switch (bytes) {
	case 1U:
		vdev->cfgdata.data_8[offset] = (uint8_t)val;
		break;
	case 2U:
		vdev->cfgdata.data_16[offset >> 1U] = (uint16_t)val;
		break;
	default:
		vdev->cfgdata.data_32[offset >> 2U] = val;
		break;
	}
}

/**
 * @brief Find a virtual PCI device
 *
 * In acrn_vcpi structure, all available virtual PCI devices are maintained by a list.
 * This function checks whether a virtual PCI device is available in this list by virtual BDF.
 *
 * @param[in] vpci The data structure of acrn_vpci to be searched in.
 * @param[in] vbdf The PCI device BDF to be searched.
 *
 * @return A pointer to indicate if the pci_vdev structure pointer is found.
 *
 * @retval non-NULL The PCI device is found.
 * @retval NULL The PCI device is not found.
 *
 * @pre vpci != NULL
 * @pre vpci->pci_vdev_cnt <= CONFIG_MAX_PCI_DEV_NUM
 */
struct pci_vdev *pci_find_vdev(struct acrn_vpci *vpci, union pci_bdf vbdf)
{
	struct pci_vdev *vdev = NULL, *tmp;
	struct hlist_node *n;

	hlist_for_each(n, &vpci->vdevs_hlist_heads[hash64(vbdf.value, VDEV_LIST_HASHBITS)]) {
		tmp = hlist_entry(n, struct pci_vdev, link);
		if (bdf_is_equal(vbdf, tmp->bdf)) {
			vdev = tmp;
			break;
		}
	}

	return vdev;
}

/**
 * @brief Check if the BAR base address is valid
 *
 * This function checks if the input BAR base address within the valid PCI hole range.
 *
 * @param[in] vm   The data structure of VM.
 * @param[in] base The base address of a BAR.
 *
 * @return A boolean value to indicate whether the base address is valid.
 *
 * @retval TRUE The base address is valid.
 * @retval FALSE The base address is invalid.
 *
 * @pre vm != NULL
 */
static bool is_pci_mem_bar_base_valid(struct acrn_vm *vm, uint64_t base)
{
	struct acrn_vpci *vpci = &vm->vpci;
	struct pci_mmio_res *res = (base < (1UL << 32UL)) ? &(vpci->res32): &(vpci->res64);

	return ((base >= res->start) &&  (base <= res->end));
}

/**
 * @brief Update the base address for a BAR
 *
 * This function updates the base address for a BAR when the guest tries to re-program it.
 * Then it will check whether the updated base address is valid. For a PIO BAR, would inject
 * General Protection Fault to guest if it tries to re-program the PIO BAR to a different
 * address; For a MMIO BAR, would (a) inject General Protection Fault to guest if it tries
 * to re-program the MMIO BAR to an address which is not page aligned (b) this BAR would not
 * allow guest to access it if it tries to re-program the MMIO BAR to an address which is not
 * aligned with its size. In addition, would also print the error log in all these wrong
 * conditions.
 *
 * @param[inout] vdev The data structure of virtual PCI device to access.
 * @param[in]    idx  The BAR ID to update.
 *
 * @return None.
 *
 * @pre vdev != NULL
 */
static void pci_vdev_update_vbar_base(struct pci_vdev *vdev, uint32_t idx)
{
	struct pci_vbar *vbar;
	uint64_t base = 0UL;
	uint32_t lo, hi, offset;
	struct pci_mmio_res *res;

	vbar = &vdev->vbars[idx];
	offset = pci_bar_offset(idx);
	lo = pci_vdev_read_vcfg(vdev, offset, 4U);
	if ((!is_pci_reserved_bar(vbar)) && !vbar->sizing) {
		base = lo & vbar->mask;

		if (is_pci_mem64lo_bar(vbar)) {
			vbar = &vdev->vbars[idx + 1U];
			if (!vbar->sizing) {
				hi = pci_vdev_read_vcfg(vdev, (offset + 4U), 4U);
				base |= ((uint64_t)hi << 32U);
			} else {
				base = 0UL;
			}
		}

		if (is_pci_io_bar(vbar)) {
			/* Because guest driver may write to upper 16-bits of PIO BAR and expect that
			 * should have no effect, PIO BAR base may be bigger than 0xffff after calculation,
			 * should mask the upper 16-bits.
			 */
			base &= 0xffffUL;
		}
	}

	if (base != 0UL) {
		if (is_pci_io_bar(vbar)) {
			/*
			 * ACRN-DM and acrn-config should ensure the identical mapping of PIO bar of
			 * pass-thru devs. Currently, we don't support the reprogram of PIO bar of
			 * pass-thru devs. If guest tries to reprogram, hv will inject #GP to guest.
			 */
			if ((vdev->pdev != NULL) &&
			    ((lo & PCI_BASE_ADDRESS_IO_MASK) != (uint32_t)vbar->base_hpa)) {
				struct acrn_vcpu *vcpu = vcpu_from_pid(vpci2vm(vdev->vpci), get_pcpu_id());
				if (vcpu != NULL) {
					vcpu_inject_gp(vcpu, 0U);
				}
				pr_err("%s, PCI:%02x:%02x.%x PIO BAR%d couldn't be reprogramed, "
					"the valid value is 0x%lx, but the actual value is 0x%lx",
					__func__, vdev->bdf.bits.b, vdev->bdf.bits.d, vdev->bdf.bits.f, idx,
					vdev->vbars[idx].base_hpa, lo & PCI_BASE_ADDRESS_IO_MASK);
				base = 0UL;
			}
		} else {
			if (!mem_aligned_check(base, PAGE_SIZE)) {
				struct acrn_vcpu *vcpu = vcpu_from_pid(vpci2vm(vdev->vpci), get_pcpu_id());
				if (vcpu != NULL) {
					vcpu_inject_gp(vcpu, 0U);
				}
				pr_err("VBDF(%02x:%02x.%x): A reprogramming attempt of BAR%d to non-page-aligned "
					"address 0x%llx was dropped: Operation not supported",
					vdev->bdf.bits.b, vdev->bdf.bits.d, vdev->bdf.bits.f, idx, base);
				base = 0UL;
			} else if (!mem_aligned_check(base, vdev->vbars[idx].size)) {
				pr_err("VBDF(%02x:%02x.%x): A reprogramming attempt of BAR%d to non-size-aligned "
					"address 0x%llx  was dropped: Invalid argument",
					vdev->bdf.bits.b, vdev->bdf.bits.d, vdev->bdf.bits.f, idx, base);
				base = 0UL;
			} else if (!is_pci_mem_bar_base_valid(vpci2vm(vdev->vpci), base)) {
				/* VM tries to reprogram vbar address out of pci mmio bar window, it can be caused
				 * by:
				 * 1. For Service VM, <board>.xml is misaligned with the actual native platform,
				 *    and we get wrong mmio window.
				 * 2. Malicious operation from VM, it tries to reprogram vbar address out of
				 *    pci mmio bar window
				 */
				res = (base < (1UL << 32UL)) ? &(vdev->vpci->res32) : &(vdev->vpci->res64);
				pr_err("VBDF(%02x:%02x.%x): Guest attempts to re-program BAR%d to address 0x%llx, "
					"which is out of MMIO window [0x%llx, 0x%llx]. This is likely caused by "
					"BIOS bug or board mismatch",
					vdev->bdf.bits.b, vdev->bdf.bits.d, vdev->bdf.bits.f,
					idx, base, res->start, res->end);
			} else {
				/* Nothing to do. Skip. */
			}
		}
	}

	vdev->vbars[idx].base_gpa = base;
}

/**
 * @brief Check whether PIO BAR is supported for a passthrough PCI device
 *
 * For a passthrough PCI device, only support PIO BAR when the GPA and HPA is identical
 * mapping for this PIO BAR.
 *
 * @param[in] vdev Pointer to vdev instance.
 *
 * @return An integer to indicate whether PIO BAR is supported for a passthrough PCI device.
 *
 * @retval 0 BARs are valid PIO BARs.
 * @retval -EIO There is invalid BAR.
 *
 * @pre vdev != NULL
 *
 * @remark N/A
 */
int32_t check_pt_dev_pio_bars(struct pci_vdev *vdev)
{
	int32_t ret = 0;
	uint32_t idx;

	if (vdev->pdev != NULL) {
		for (idx = 0U; idx < vdev->nr_bars; idx++) {
			if ((is_pci_io_bar(&vdev->vbars[idx])) &&
			    (vdev->vbars[idx].base_gpa != vdev->vbars[idx].base_hpa)) {
				ret = -EIO;
				pr_err("%s, PCI:%02x:%02x.%x PIO BAR%d isn't identical mapping, "
					"host start addr is 0x%lx, while guest start addr is 0x%lx",
					__func__, vdev->bdf.bits.b, vdev->bdf.bits.d, vdev->bdf.bits.f, idx,
					vdev->vbars[idx].base_hpa, vdev->vbars[idx].base_gpa);
				break;
			}
		}
	}

	return ret;
}

/**
 * @brief Write value to a BAR of virtual PCI device
 *
 * This function writes BAR address to virtual PCI device BAR register according to
 * input BAR index. Then, it calls pci_vdev_update_vbar_base() to update vbar base.
 *
 * @param[inout] vdev Pointer to vdev instance.
 * @param[in]    idx  The idx of the BAR to update.
 * @param[in]    val  The value to write.
 *
 * @return None.
 *
 * @pre vdev != NULL
 *
 * @remark N/A
 */
void pci_vdev_write_vbar(struct pci_vdev *vdev, uint32_t idx, uint32_t val)
{
	struct pci_vbar *vbar;
	uint32_t bar, offset;
	uint32_t update_idx = idx;

	vbar = &vdev->vbars[idx];
	vbar->sizing = (val == ~0U);
	bar = val & vbar->mask;
	if (vbar->is_mem64hi) {
		update_idx -= 1U;
	} else {
		if (is_pci_io_bar(vbar)) {
			bar |= (vbar->bar_type.bits & (~PCI_BASE_ADDRESS_IO_MASK));
		} else {
			bar |= (vbar->bar_type.bits & (~PCI_BASE_ADDRESS_MEM_MASK));
		}
	}
	offset = pci_bar_offset(idx);
	pci_vdev_write_vcfg(vdev, offset, 4U, bar);

	pci_vdev_update_vbar_base(vdev, update_idx);
}

/**
 * @}
 */
