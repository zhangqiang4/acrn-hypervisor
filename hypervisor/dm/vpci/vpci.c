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

#include <errno.h>
#include <ptdev.h>
#include <asm/guest/vm.h>
#include <asm/vtd.h>
#include <asm/io.h>
#include <asm/mmu.h>
#include <vacpi.h>
#include <logmsg.h>
#include "vpci_priv.h"
#include <asm/pci_dev.h>
#include <hash.h>
#include <board_info.h>

/**
 * @addtogroup vp-dm_vpci
 *
 * @{
 */

/**
 * @file
 * @brief This file implements all external APIs that shall be provided by vPCI component
 *
 * This file implements external functions that allow the Guest VM to probe and access the virtual PCI devices
 * which assigned to it through PIO or ECAM. This file also implements external functions that assign/de-assign
 * a PCI device to/from a Guest VM.
 */

static int32_t vpci_init_vdevs(struct acrn_vm *vm);
static int32_t vpci_read_cfg(struct acrn_vpci *vpci, union pci_bdf bdf, uint32_t offset,
			     uint32_t bytes, uint32_t *val);
static int32_t vpci_write_cfg(struct acrn_vpci *vpci, union pci_bdf bdf, uint32_t offset,
			      uint32_t bytes, uint32_t val);
static struct pci_vdev *find_available_vdev(struct acrn_vpci *vpci, union pci_bdf bdf);

/**
 * @brief Check if the virtual PCI device is a quirk passthrough device
 *
 * ACRN_PTDEV_QUIRK_ASSIGN is the flag to indicate whether the vdev is a quirk passthrough device.
 * Check if the vdev->flags has ACRN_PTDEV_QUIRK_ASSIGN to decide whether the vdev is a quirk passthrough
 * device.
 *
 * @param[in] vdev Pointer to a virtual PCI device.
 *
 * @return A boolean value to indicate whether the virtual PCI device is a quirk passthrough device
 *
 * @retval true  A quirk passthrough device.
 * @retval false Not a quirk passthrough device.
 *
 * @pre vdev != NULL
 */
static inline bool is_quirk_ptdev(const struct pci_vdev *vdev)
{
	return ((vdev->flags & ACRN_PTDEV_QUIRK_ASSIGN) != 0U);
}

/**
 * @brief Read virtual CONFIG_ADDRESS register value
 *
 * This function is used to read virtual CONFIG_ADDRESS register. Refer PCI LOCAL BUS SPECIFICATION,
 * REV. 3.0, Chapter 3.2.2.3.2 for details.
 *
 * @param[inout] vcpu  Pointer to the vcpu to read CONFIG_ADDRESS register.
 * @param[in]    addr  The address of I/O ports.
 * @param[in]    bytes Size to read.
 *
 * @return Always return true to indicate the read operation succeeds
 *
 * @pre vcpu != NULL
 * @pre vcpu->vm != NULL
 */
static bool vpci_pio_cfgaddr_read(struct acrn_vcpu *vcpu, uint16_t addr, size_t bytes)
{
	uint32_t val = ~0U;
	struct acrn_vpci *vpci = &vcpu->vm->vpci;
	union pci_cfg_addr_reg *cfg_addr = &vpci->addr;
	struct acrn_pio_request *pio_req = &vcpu->req.reqs.pio_request;

	if ((addr == (uint16_t)PCI_CONFIG_ADDR) && (bytes == 4U)) {
		val = cfg_addr->value;
	}

	pio_req->value = val;

	return true;
}

/**
 * @brief Write virtual CONFIG_ADDRESS register
 *
 * This function is used to write virtual CONFIG_ADDRESS register. Refer PCI LOCAL BUS SPECIFICATION,
 * REV. 3.0, Chapter 3.2.2.3.2 for details. For post-launched VM, if the corresponding virtual PCI
 * device cannot be found or it is a quirk passthrough device, return false back to indicate the write
 * fails.
 *
 * @param[inout] vcpu  Pointer to the vcpu to write CONFIG_ADDRESS register.
 * @param[in]    addr  The address of I/O ports.
 * @param[in]    bytes Size to write.
 * @param[in]    val   Value to write.
 *
 * @return A boolean value to indicate whether the write is successful or not
 *
 * @retval true  Succeed.
 * @retval false Fail if the vdev cannot be found or it is a quirk devcie (ACRN will deliver this
 *               IO request to DM to handle for post-launched VM).
 *
 * @pre vcpu != NULL
 * @pre vcpu->vm != NULL
 */
static bool vpci_pio_cfgaddr_write(struct acrn_vcpu *vcpu, uint16_t addr, size_t bytes, uint32_t val)
{
	bool ret = true;
	struct acrn_vpci *vpci = &vcpu->vm->vpci;
	union pci_cfg_addr_reg *cfg_addr = &vpci->addr;
	union pci_bdf vbdf;

	if ((addr == (uint16_t)PCI_CONFIG_ADDR) && (bytes == 4U)) {
		/* unmask reserved fields: BITs 24-30 and BITs 0-1 */
		cfg_addr->value = val & (~0x7f000003U);

		if (is_postlaunched_vm(vcpu->vm)) {
			const struct pci_vdev *vdev;

			vbdf.value = cfg_addr->bits.bdf;
			vdev = find_available_vdev(vpci, vbdf);
			/* For post-launched VM, ACRN HV will only handle PT device,
			 * all virtual PCI device and QUIRK PT device
			 * still need to deliver to ACRN DM to handle.
			 */
			if ((vdev == NULL) || is_quirk_ptdev(vdev)) {
				ret = false;
			}
		}
	}

	return ret;
}

/**
 * @brief Read Configuration Space register through CONFIG_DATA register
 *
 * This function is used to read Configuration Space register value out through CONFIG_DATA
 * register. Refer PCI LOCAL BUS SPECIFICATION, REV. 3.0, Chapter 3.2.2.3.2 for details.
 *
 * @param[inout] vcpu  Pointer to the vcpu to read the register.
 * @param[in]    addr  The address of I/O ports.
 * @param[in]    bytes Size to read.
 *
 * @return A boolean value to indicate whether the read is successful or not
 *
 * @retval true  Succeed.
 * @retval false Fail to read the register (ACRN will deliver this IO request to DM to handle
 *               for post-launched VM).
 *
 * @pre vcpu != NULL
 * @pre vcpu->vm != NULL
 */
static bool vpci_pio_cfgdata_read(struct acrn_vcpu *vcpu, uint16_t addr, size_t bytes)
{
	int32_t ret = 0;
	struct acrn_vm *vm = vcpu->vm;
	struct acrn_vpci *vpci = &vm->vpci;
	union pci_cfg_addr_reg cfg_addr;
	union pci_bdf bdf;
	uint32_t val = ~0U;
	struct acrn_pio_request *pio_req = &vcpu->req.reqs.pio_request;

	cfg_addr.value = atomic_readandclear32(&vpci->addr.value);
	if (cfg_addr.bits.enable != 0U) {
		uint32_t offset = cfg_addr.bits.reg_num + ((uint32_t)addr - PCI_CONFIG_DATA);
		if (pci_is_valid_access(offset, bytes)) {
			bdf.value = cfg_addr.bits.bdf;
			ret = vpci_read_cfg(vpci, bdf, offset, bytes, &val);
		}
	}

	pio_req->value = val;
	return (ret == 0);
}

/**
 * @brief Write Configuration Space register through CONFIG_DATA register
 *
 * This function is used to write Configuration Space register value through CONFIG_DATA
 * register. Refer PCI LOCAL BUS SPECIFICATION, REV. 3.0, Chapter 3.2.2.3.2 for details.
 *
 * @param[inout] vcpu  Pointer to the vcpu to write the register.
 * @param[in]    addr  The address of I/O ports.
 * @param[in]    bytes Size to write.
 * @param[in]    val   Value to write.
 *
 * @return A boolean value to indicate whether the write is successful or not
 *
 * @retval true  Succeed.
 * @retval false Fail to write the register (ACRN will deliver this IO request to DM to
 *               handle for post-launched VM).
 *
 * @pre vcpu != NULL
 * @pre vcpu->vm != NULL
 */
static bool vpci_pio_cfgdata_write(struct acrn_vcpu *vcpu, uint16_t addr, size_t bytes, uint32_t val)
{
	int32_t ret = 0;
	struct acrn_vm *vm = vcpu->vm;
	struct acrn_vpci *vpci = &vm->vpci;
	union pci_cfg_addr_reg cfg_addr;
	union pci_bdf bdf;

	cfg_addr.value = atomic_readandclear32(&vpci->addr.value);
	if (cfg_addr.bits.enable != 0U) {
		uint32_t offset = cfg_addr.bits.reg_num + ((uint32_t)addr - PCI_CONFIG_DATA);
		if (pci_is_valid_access(offset, bytes)) {
			bdf.value = cfg_addr.bits.bdf;
			ret = vpci_write_cfg(vpci, bdf, offset, bytes, val);
		}
	}

	return (ret == 0);
}

/**
 * @brief Read/Write Configuration Space register through MMIO (ECAM)
 *
 * This function is used to read/write Configuration Space register value through MMIO (ECAM).
 * Refer PCI Express® Base Specification Revision 5.0 Version 1.0, Chapter 7.2.2.
 *
 * @param[inout] io_req       Pointer to the IO request instance.
 * @param[in]    private_data Pointer to the virtual PCI instance.
 *
 * @return A signed integer to indicate whether the read/write is successful or not
 *
 * @retval 0  Succeed.
 * @retval <0 Fail to read or write the register (ACRN will deliver this MMIO request to
 *            DM to handle for post-launched VM).
 *
 * @pre io_req != NULL
 * @pre private_data != NULL
 */
static int32_t vpci_mmio_cfg_access(struct io_request *io_req, void *private_data)
{
	int32_t ret = 0;
	struct acrn_mmio_request *mmio = &io_req->reqs.mmio_request;
	struct acrn_vpci *vpci = (struct acrn_vpci *)private_data;
	uint64_t pci_mmcofg_base = vpci->pci_mmcfg.address;
	uint64_t address = mmio->address;
	uint32_t reg_num = (uint32_t)(address & 0xfffUL);
	union pci_bdf bdf;

	/**
	 * Enhanced Configuration Address Mapping
	 * A[(20+n-1):20] Bus Number 1 ≤ n ≤ 8
	 * A[19:15] Device Number
	 * A[14:12] Function Number
	 * A[11:8] Extended Register Number
	 * A[7:2] Register Number
	 * A[1:0] Along with size of the access, used to generate Byte Enables
	 */
	bdf.value = (uint16_t)((address - pci_mmcofg_base) >> 12U);

	if (mmio->direction == ACRN_IOREQ_DIR_READ) {
		uint32_t val = ~0U;

		if (pci_is_valid_access(reg_num, (uint32_t)mmio->size)) {
			ret = vpci_read_cfg(vpci, bdf, reg_num, (uint32_t)mmio->size, &val);
		}
		mmio->value = val;
	} else {
		if (pci_is_valid_access(reg_num, (uint32_t)mmio->size)) {
			ret = vpci_write_cfg(vpci, bdf, reg_num, (uint32_t)mmio->size, (uint32_t)mmio->value);
		}
	}

	return ret;
}

/**
 * @brief Initialize the virtual PCI component for a VM
 *
 * This function is the top level virtual PCI initialization for the VM:
 *   - set up IOMMU domain;
 *   - get VM PCI MMCONFIG and set up. For Service VM, the virtual PCI MMCONFIG is same as the
 *     physical value. For other VMs, set dedicated values to the virtual PCI MMCONFIG.
 *   - build up all virtual PCI devices given to this VM and register MMIO/PIO access handlers:
 *       - handler for MMCONFIG: vpci_mmio_cfg_access;
 *       - handler for PIO CFGADDR range (0xCF8): vpci_pio_cfgaddr_read and vpci_pio_cfgaddr_write;
 *       - handler for PIO CFGDATA range (0xCFC-0xCFF): vpci_pio_cfgdata_read and vpci_pio_cfgdata_write.
 *
 * @param[inout] vm Pointer to the VM instance.
 *
 * @return A signed integer to indicate whether the initialization is successful or not
 *
 * @retval 0  Succeed.
 * @retval <0 Fail to initialize virtual PCI devices.
 *
 * @pre vm != NULL
 * @pre vm->vm_id < CONFIG_MAX_VM_NUM
 */
int32_t init_vpci(struct acrn_vm *vm)
{
	struct vm_io_range pci_cfgaddr_range = {
		.base = PCI_CONFIG_ADDR,
		.len = 1U
	};

	struct vm_io_range pci_cfgdata_range = {
		.base = PCI_CONFIG_DATA,
		.len = 4U
	};

	struct acrn_vm_config *vm_config;
	struct pci_mmcfg_region *pci_mmcfg;
	int32_t ret = 0;

	vm->iommu = create_iommu_domain(vm->vm_id, hva2hpa(vm->arch_vm.nworld_eptp), 48U);

	vm_config = get_vm_config(vm->vm_id);
	/* virtual PCI MMCONFIG for Service VM is same with the physical value */
	if (vm_config->load_order == SERVICE_VM) {
		pci_mmcfg = get_mmcfg_region();
		vm->vpci.pci_mmcfg = *pci_mmcfg;
		vm->vpci.res32.start = MMIO32_START;
		vm->vpci.res32.end = MMIO32_END;
		vm->vpci.res64.start = MMIO64_START;
		vm->vpci.res64.end = MMIO64_END;
	} else {
		vm->vpci.pci_mmcfg.address = USER_VM_VIRT_PCI_MMCFG_BASE;
		vm->vpci.pci_mmcfg.start_bus = USER_VM_VIRT_PCI_MMCFG_START_BUS;
		vm->vpci.pci_mmcfg.end_bus = USER_VM_VIRT_PCI_MMCFG_END_BUS;
		vm->vpci.res32.start = USER_VM_VIRT_PCI_MEMBASE32;
		vm->vpci.res32.end = USER_VM_VIRT_PCI_MEMLIMIT32;
		vm->vpci.res64.start = USER_VM_VIRT_PCI_MEMBASE64;
		vm->vpci.res64.end = USER_VM_VIRT_PCI_MEMLIMIT64;
	}

	/* Build up vdev list for vm */
	ret = vpci_init_vdevs(vm);

	if (ret == 0) {
		register_mmio_emulation_handler(vm, vpci_mmio_cfg_access, vm->vpci.pci_mmcfg.address,
			vm->vpci.pci_mmcfg.address + get_pci_mmcfg_size(&vm->vpci.pci_mmcfg), &vm->vpci, false);

		/* Intercept and handle I/O ports CF8h */
		register_pio_emulation_handler(vm, PCI_CFGADDR_PIO_IDX, &pci_cfgaddr_range,
			vpci_pio_cfgaddr_read, vpci_pio_cfgaddr_write);

		/* Intercept and handle I/O ports CFCh -- CFFh */
		register_pio_emulation_handler(vm, PCI_CFGDATA_PIO_IDX, &pci_cfgdata_range,
			vpci_pio_cfgdata_read, vpci_pio_cfgdata_write);

		spinlock_init(&vm->vpci.lock);
	}

	return ret;
}

/**
 * @brief Deinitialize the virtual PCI devices for the input VM
 *
 * This function is the top level virtual PCI deinitialization for the VM:
 *   - deinitialize each virtual PCI device from the VM by calling deinit_vdev callback function if
 *     the vdev is owned by the VM. If there is parent of the vdev, re-initialize the parent vdev.
 *   - unmap IRQs of passthrough PCI devices by calling ptdev_release_all_entries;
 *   - clear vm->vpci to zero;
 *   - clean up IOMMU setting for the VM by calling destroy_iommu_domain.
 *
 * @param[inout] vm Pointer to the VM instance.
 *
 * @return None
 *
 * @pre vm != NULL
 */
void deinit_vpci(struct acrn_vm *vm)
{
	struct pci_vdev *vdev, *parent_vdev;
	uint32_t i;

	for (i = 0U; i < CONFIG_MAX_PCI_DEV_NUM; i++) {
		vdev = (struct pci_vdev *) &(vm->vpci.pci_vdevs[i]);

		/* Only deinit the VM's own devices */
		if (vdev->user == vdev) {
			parent_vdev = vdev->parent_user;

			vdev->vdev_ops->deinit_vdev(vdev);

			if (parent_vdev != NULL) {
				spinlock_obtain(&parent_vdev->vpci->lock);
				parent_vdev->vdev_ops->init_vdev(parent_vdev);
				spinlock_release(&parent_vdev->vpci->lock);
			}
		}
	}

	ptdev_release_all_entries(vm);
	(void)memset(&vm->vpci, 0U, sizeof(struct acrn_vpci));

	/* Free iommu */
	destroy_iommu_domain(vm->iommu);
}

/**
 * @brief Assign a virtual PCI device to VM's IOMMU domain
 *
 * This function moves a device specified by bus & devfun from a specified old domain (NULL domain) to a specified
 * new IOMMU domain (the IOMMU domain for the VM which virtual PCI device is assigned to). If the movement fails,
 * it triggers panic with a fatal error message.
 *
 * @param[in] vdev Pointer to a virtual PCI device to assign.
 *
 * @return None
 *
 * @pre vdev != NULL
 * @pre vdev->vpci != NULL
 * @pre vdev->pdev != NULL
 * @pre vpci2vm(vdev->vpci)->iommu != NULL
 */
static void assign_vdev_pt_iommu_domain(struct pci_vdev *vdev)
{
	int32_t ret;
	struct acrn_vm *vm = vpci2vm(vdev->vpci);

	ret = move_pt_device(NULL, vm->iommu, (uint8_t)vdev->pdev->bdf.bits.b,
		(uint8_t)(vdev->pdev->bdf.value & 0xFFU));
	if (ret != 0) {
		panic("failed to assign iommu device!");
	}
}

/**
 * @brief Remove a virtual PCI device from VM's IOMMU domain
 *
 * This function moves a device specified by bus & devfun from a specified old domain (the IOMMU domain for the VM
 * which virtual PCI device is assigned to) to a specified new IOMMU domain (NULL domain). If the movement fails,
 * it triggers panic with a fatal error message.
 *
 * @param[in] vdev Pointer to a virtual PCI device to remove the assignment.
 *
 * @return None
 *
 * @pre vdev != NULL
 * @pre vdev->vpci != NULL
 * @pre vdev->pdev != NULL
 * @pre vpci2vm(vdev->vpci)->iommu != NULL
 */
static void remove_vdev_pt_iommu_domain(const struct pci_vdev *vdev)
{
	int32_t ret;
	const struct acrn_vm *vm = vpci2vm(vdev->vpci);

	ret = move_pt_device(vm->iommu, NULL, (uint8_t)vdev->pdev->bdf.bits.b,
		(uint8_t)(vdev->pdev->bdf.value & 0xFFU));
	if (ret != 0) {
		/*
		 *TODO
		 * panic needs to be removed here
		 * Currently unassign_pt_device can fail for multiple reasons
		 * Once all the reasons and methods to avoid them can be made sure
		 * panic here is not necessary.
		 */
		panic("failed to unassign iommu device!");
	}
}

/**
 * @brief Find an available virtual PCI device with BDF from a specified vpci structure 
 *
 * This function is used to find an available virtual PCI device with BDF.
 *   - if the vdev's user is the same as the vdev, the vdev is available.
 *   - if the vdev's user is not the same as the vdev, the vdev has already been assigned and it is unavailable
 *     for Service VM.
 *   - if the vdev's user is NULL, the vdev is a orphan/zombie instance, it can't be accessed by any vpci.
 *
 * @param[in] vpci Pointer to a specified acrn_vcpi structure instance.
 * @param[in] bdf  The BDF of the virtual PCI device to find.
 *
 * @return A pointer to the virtual PCI device.
 *
 * @retval non-NULL The virtual PCI device is found.
 * @retval NULL     The virtual PCI device is not found.
 *
 * @pre vpci != NULL
 */
static struct pci_vdev *find_available_vdev(struct acrn_vpci *vpci, union pci_bdf bdf)
{
	struct pci_vdev *vdev = pci_find_vdev(vpci, bdf);

	if ((vdev != NULL) && (vdev->user != vdev)) {
		if (vdev->user != NULL) {
			/*
			 * The Service VM is able to access, if and only if the Service VM has
			 * higher severity than the User VM.
			 */
			if (get_vm_severity(vpci2vm(vpci)->vm_id) <
					get_vm_severity(vpci2vm(vdev->user->vpci)->vm_id)) {
				vdev = NULL;
			}
		} else {
			vdev = NULL;
		}
	}

	return vdev;
}

/**
 * @brief Initialize ATS cap of a virtual PCI device
 *
 * This function initializes virtual PCI device ATS capability offset and length to be same as
 * physical device.
 *
 * @param[in] vdev Pointer to a virtual PCI device to initialize ATS.
 *
 * @return None
 *
 * @pre vdev != NULL
 * @pre vdev->pdev != NULL
 */
static void init_vats(struct pci_vdev *vdev)
{
	struct pci_pdev *pdev = vdev->pdev;

	vdev->ats.capoff = pdev->ats.capoff;
	vdev->ats.caplen = pdev->ats.caplen;
}

/**
 * @brief Initialize PRI cap of a virtual PCI device
 *
 * This function initializes virtual PCI device PRI capability offset and length to be same as
 * physical device.
 *
 * @param[in] vdev Pointer to a virtual PCI device to initialize PRI.
 *
 * @return None
 *
 * @pre vdev != NULL
 * @pre vdev->pdev != NULL
 */
static void init_vpri(struct pci_vdev *vdev)
{
	struct pci_pdev *pdev = vdev->pdev;

	vdev->pri.capoff = pdev->pri.capoff;
	vdev->pri.caplen = pdev->pri.caplen;
}

/**
 * @brief Initialize ACS cap of a virtual PCI device
 *
 * This function initializes virtual PCI device ACS capability offset and length to be same as
 * physical device.
 *
 * @param[in] vdev Pointer to a virtual PCI device to initialize ACS.
 *
 * @return None
 *
 * @pre vdev != NULL
 * @pre vdev->pdev != NULL
 */
static void init_vacs(struct pci_vdev *vdev)
{
	struct pci_pdev *pdev = vdev->pdev;

	vdev->acs.capoff = pdev->acs.capoff;
	vdev->acs.caplen = pdev->acs.caplen;
}

/**
 * @brief Initialize a virtual PCI device which points to a passthrough PCI device
 *
 * This function initializes a virtual PCI device which points to a passthrough PCI device (in order
 * to emulate Configure Space access and BAR access form Guest OS).
 *
 * @param[in] vdev Pointer to a virtual PCI device to be initialized.
 *
 * @return None
 *
 * @pre vdev != NULL
 */
static void vpci_init_pt_dev(struct pci_vdev *vdev)
{
	vdev->parent_user = NULL;
	vdev->user = vdev;

	/*
	 * Here init_vdev_pt() needs to be called after init_vmsix_pt() for the following reason:
	 * init_vdev_pt() will indirectly call has_msix_cap(), which
	 * requires init_vmsix_pt() to be called first.
	 */
	init_vmsi(vdev);
	init_vmsix_pt(vdev);
	init_vsriov(vdev);
	init_vats(vdev);
	init_vpri(vdev);
	init_vacs(vdev);
	init_vdev_pt(vdev, false);

	assign_vdev_pt_iommu_domain(vdev);
}

/**
 * @brief Deinitialize a virtual PCI device which points to a passthrough PCI device
 *
 * This function deinitializes a virtual PCI device which points to a passthrough PCI device (in order
 * to deny Configure Space access and BAR access form Guest OS)
 *
 * @param[in] vdev Pointer to a virtual PCI device to be deinitialized.
 *
 * @return None
 *
 * @pre vdev != NULL
 */
static void vpci_deinit_pt_dev(struct pci_vdev *vdev)
{
	deinit_vdev_pt(vdev);
	remove_vdev_pt_iommu_domain(vdev);
	deinit_vmsix_pt(vdev);
	deinit_vmsi(vdev);

	vdev->user = NULL;
	vdev->parent_user = NULL;
}

/**
 * @brief Data structure to define Configuration Space Header permission
 *
 * This structure defines Type 0 and Type 1 Configuration Space Header permission to emulate access from
 * Guest OS. The pt_mask decides which PCI config registers can be passthrough. The ro_mask decides which
 * PCI config registers are read-only.
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
struct cfg_header_perm {
	/* For each 4-byte register defined in PCI config space header,
	 * there is one bit dedicated for it in pt_mask and ro_mask.
	 * For example, bit 0 for CFG Vendor ID and Device ID register,
	 * Bit 1 for CFG register Command and Status register, and so on.
	 *
	 * For each mask, only low 16-bits takes effect.
	 *
	 * When guest read:
	 * If bit x is set the pt_mask, it indicates that the corresponding 4 Bytes register
	 * for bit x is pass through to guest. Otherwise, bit x is not set the pt_mask, it's
	 * virtualized.
	 *
	 * When guest write:
	 * If bit x is set the ro_mask, it indicates that the corresponding 4 Bytes register
	 * for bit x is not writable. Otherwise, that is, bit x is not set the ro_mask,
	 * If bit x is set the pt_mask, it indicates that the corresponding 4 Bytes register
	 * for bit x is pass through to guest. If bit x is not set the pt_mask, it's virtualized.
	 */
	/* For type 0 device */
	uint32_t type0_pt_mask;  /**< Passthrough mask for Type 0 Configuration Space Header registers */
	uint32_t type0_ro_mask;  /**< Read-only mask for Type 0 Configuration Space Header registers */
	/* For type 1 device */
	uint32_t type1_pt_mask;  /**< Passthrough mask for Type 1 Configuration Space Header registers */
	uint32_t type1_ro_mask;  /**< Read-only mask for Type 1 Configuration Space Header registers */
};

/**
 * @brief Instance of data structure for Configuration Space Header permission
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
static const struct cfg_header_perm cfg_hdr_perm = {
	/* Only Command (0x04-0x05) and Status (0x06-0x07) Registers are pass through */
	.type0_pt_mask = 0x0002U,
	/* Command (0x04-0x05) and Status (0x06-0x07) Registers and
	 * Base Address Registers (0x10-0x27) are writable */
	.type0_ro_mask = (uint16_t)~0x03f2U,
	/* Command (0x04-0x05) and Status (0x06-0x07) Registers and
	 * from Primary Bus Number to I/O Base Limit 16 Bits (0x18-0x33)
	 * are pass through
	 */
	.type1_pt_mask = 0x1fc2U,
	/* Command (0x04-0x05) and Status (0x06-0x07) Registers and
	 * Base Address Registers (0x10-0x17) and
	 * Secondary Status (0x1e-0x1f) are writable
	 * Note: should handle I/O Base (0x1c) specially
	 */
	.type1_ro_mask = (uint16_t)~0xb2U
};

/**
 * @brief Read Configuration Space Header register
 *
 * This function is used to read Configuration Space Header register value out base on Configuration
 * Space Header permission. If the register is a passthrough register, the native register value is
 * returned. Otherwise, the virtual register value is returned. For BARs access, the size to read must
 * be 4 bytes and the offset must also be 4 bytes aligned, then the virutal register value is returned.
 * Otherwise, 0xFFFFFFFF is returned. If the read is for PCIR_BIOS register of quirk passthrough device,
 * return ENODEV. If the read is for PCIR_COMMAND register of VF, always set MSE(Memory Space Enable)
 * bit.
 *
 * @param[in]  vdev   Pointer to a virtual PCI device to read the register.
 * @param[in]  offset Offset of the register to read.
 * @param[in]  bytes  Size to read.
 * @param[out] val    Value to be read out.
 *
 * @return A signed integer to indicate whether the read is successful or not
 *
 * @retval 0       Succeed.
 * @retval -ENODEV When it accesses PCIR_BIOS register of a quirk passthrough device.
 *
 * @pre vdev != NULL
 * @pre offset + bytes < PCI_CFG_HEADER_LENGTH
 * @pre val != NULL
 */
static int32_t read_cfg_header(const struct pci_vdev *vdev,
		uint32_t offset, uint32_t bytes, uint32_t *val)
{
	int32_t ret = 0;
	uint32_t pt_mask;

	if ((offset == PCIR_BIOS) && is_quirk_ptdev(vdev)) {
		/* the access of PCIR_BIOS is emulated for quirk_ptdev */
		ret = -ENODEV;
	} else if (vbar_access(vdev, offset)) {
		/* bar access must be 4 bytes and offset must also be 4 bytes aligned */
		if ((bytes == 4U) && ((offset & 0x3U) == 0U)) {
			*val = pci_vdev_read_vcfg(vdev, offset, bytes);
		} else {
			*val = ~0U;
		}
	} else {
		if (is_bridge(vdev->pdev)) {
			pt_mask = cfg_hdr_perm.type1_pt_mask;
		} else {
			pt_mask = cfg_hdr_perm.type0_pt_mask;
		}

		if (bitmap32_test(((uint16_t)offset) >> 2U, &pt_mask)) {
			*val = pci_pdev_read_cfg(vdev->pdev->bdf, offset, bytes);

			/* MSE(Memory Space Enable) bit always be set for an assigned VF */
			if ((vdev->phyfun != NULL) && (offset == PCIR_COMMAND) &&
					(vdev->vpci != vdev->phyfun->vpci)) {
				*val |= PCIM_CMD_MEMEN;
			}
		} else {
			*val = pci_vdev_read_vcfg(vdev, offset, bytes);
		}
	}
	return ret;
}

#define PCIM_SPACE_EN (PCIM_CMD_PORTEN | PCIM_CMD_MEMEN) /**< Indicating I/O Space Enable and Memory Space Enable set */

/**
 * @brief Write Configuration Space Header register
 *
 * This function is used to write Configuration Space Header register base on Configuration Space Header
 * permission. If the register is not read-only and is a passthrough register, the input value is written
 * into native register. If the register is not read-only and is not passthrough register, the virtual
 * register is written. If the register is read-only, the write is ignored.
 *
 * There is a special case that the write to Interrupt Line register is written into virtual register
 * without read-only or passthrough mask check.
 *
 * For write to Command register to enable I/O Space or Memory Space, we need to check whether to
 * restore BARs or restore bridge's registers.
 *
 * For BARs access, the size to write must be 4 bytes and the offset must also be 4 bytes aligned.
 * Otherwise, the write is ignored.
 *
 * If the write is for PCIR_BIOS register of quirk passthrough device, return ENODEV.
 *
 * @param[in]  vdev   Pointer to a virtual PCI device to write the register.
 * @param[in]  offset Offset of the register to write.
 * @param[in]  bytes  Size to write.
 * @param[out] val    Value to be written.
 *
 * @return A signed integer to indicate whether the write is successful or not
 *
 * @retval 0       Succeed.
 * @retval -ENODEV When it accesses PCIR_BIOS register of a quirk passthrough device.
 *
 * @pre vdev != NULL
 * @pre offset + bytes < PCI_CFG_HEADER_LENGTH
 */
static int32_t write_cfg_header(struct pci_vdev *vdev,
		uint32_t offset, uint32_t bytes, uint32_t val)
{
	bool dev_is_bridge = is_bridge(vdev->pdev);
	int32_t ret = 0;
	uint32_t pt_mask, ro_mask;

	if ((offset == PCIR_BIOS) && is_quirk_ptdev(vdev)) {
		/* the access of PCIR_BIOS is emulated for quirk_ptdev */
		ret = -ENODEV;
	} else if (vbar_access(vdev, offset)) {
		/* bar write access must be 4 bytes and offset must also be 4 bytes aligned */
		if ((bytes == 4U) && ((offset & 0x3U) == 0U)) {
			vdev_pt_write_vbar(vdev, pci_bar_index(offset), val);
		}
	} else {
		if (offset == PCIR_COMMAND) {
			uint16_t phys_cmd = (uint16_t)pci_pdev_read_cfg(vdev->pdev->bdf, PCIR_COMMAND, 2U);

			if (((phys_cmd & PCIM_SPACE_EN) == 0U) && ((val & PCIM_SPACE_EN) != 0U)) {
				/* check whether need to restore BAR because some kind of reset */
				if (pdev_need_bar_restore(vdev->pdev)) {
					pdev_restore_bar(vdev->pdev);
				}

				/*
				 * check whether need to restore bridge mem/IO related registers because
				 * some kind of reset.
				 */
				if (dev_is_bridge) {
					vdev_bridge_pt_restore_space(vdev);
				}
			}
			/*
			 * check whether need to restore Primary/Secondary/Subordinate Bus Number registers
			 * because some kind of reset.
			 */
			if (dev_is_bridge && ((phys_cmd & PCIM_CMD_BUSEN) == 0U) &&
			    ((val & PCIM_CMD_BUSEN) != 0U)) {
				vdev_bridge_pt_restore_bus(vdev);
			}
		}

		if (dev_is_bridge) {
			ro_mask = cfg_hdr_perm.type1_ro_mask;
			pt_mask = cfg_hdr_perm.type1_pt_mask;
		} else {
			ro_mask = cfg_hdr_perm.type0_ro_mask;
			pt_mask = cfg_hdr_perm.type0_pt_mask;
		}

		if (!bitmap32_test(((uint16_t)offset) >> 2U, &ro_mask)) {
			if (bitmap32_test(((uint16_t)offset) >> 2U, &pt_mask)) {
				/* I/O Base (0x1c) and I/O Limit (0x1d) are read-only */
				if (!((offset == PCIR_IO_BASE) && (bytes <= 2)) && (offset != PCIR_IO_LIMIT)) {
					uint32_t value = val;
					if ((offset == PCIR_IO_BASE) && (bytes == 4U)) {
						uint16_t phys_val = (uint16_t)pci_pdev_read_cfg(vdev->pdev->bdf,
												offset, 2U);
						value = (val & PCIR_SECSTATUS_LINE_MASK) | phys_val;
					}
					pci_pdev_write_cfg(vdev->pdev->bdf, offset, bytes, value);
				}
			} else {
				pci_vdev_write_vcfg(vdev, offset, bytes, val);
			}
		}

		/* According to PCIe Spec, for a RW register bits, If the optional feature
		 * that is associated with the bits is not implemented, the bits are permitted
		 * to be hardwired to 0b. However Zephyr would use INTx Line Register as writable
		 * even this PCI device has no INTx, so emulate INTx Line Register as writable.
		 */
		if (offset == PCIR_INTERRUPT_LINE) {
			pci_vdev_write_vcfg(vdev, offset, bytes, (val & 0xfU));
		}

	}
	return ret;
}

/**
 * @brief Write register of a virtual PCI device which points to a passthrough PCI device
 *
 * In gerernal, this function emulates Configuration Space Header, MSI, MSI-X and SR-IOV registers write
 * operations. It also prevent guest to write ATS, PRI as ACRN Hypervisor doesn't support vIOMMU and ACS
 * capabilities to prevent P2P access for DMA isolation.
 *
 * If the register is not above registers and the virtual PCI device is a quirk passthrough device, return
 * ENODEV. Otherwise, write the value into physical configure space register if device is not IGD or the
 * register is not PCIR_ASLS_CTL.
 *
 * @param[in] vdev   Pointer to a virtual PCI device to write the register value.
 * @param[in] offset Offset of the register to write.
 * @param[in] bytes  Size to write.
 * @param[in] val    Value to be written.
 *
 * @return A signed integer to indicate whether the write is successful or not
 *
 * @retval 0       Succeed.
 * @retval -ENODEV When the register is not in scope described above and the virtual PCI device
 *                 is a quirk passthrough device.
 * @retval <0      When write_cfg_header fails.
 *
 * @pre vdev != NULL
 * @pre offset + bytes < PCI_CFG_HEADER_LENGTH
 */
static int32_t write_pt_dev_cfg(struct pci_vdev *vdev, uint32_t offset,
		uint32_t bytes, uint32_t val)
{
	int32_t ret = 0;

	if (cfg_header_access(offset)) {
		ret = write_cfg_header(vdev, offset, bytes, val);
	} else if (msicap_access(vdev, offset)) {
		write_vmsi_cap_reg(vdev, offset, bytes, val);
	} else if (msixcap_access(vdev, offset)) {
		write_pt_vmsix_cap_reg(vdev, offset, bytes, val);
	} else if (sriovcap_access(vdev, offset)) {
		write_sriov_cap_reg(vdev, offset, bytes, val);
	} else if (atscap_access(vdev, offset) || pricap_access(vdev, offset) || acscap_access(vdev, offset)) {
		/* Ignore write to ats/pri/acs. */
	} else {
		if (!is_quirk_ptdev(vdev)) {
			if ((vdev->pdev->bdf.value != CONFIG_IGD_SBDF) || (offset != PCIR_ASLS_CTL)) {
				/* passthru to physical device */
				pci_pdev_write_cfg(vdev->pdev->bdf, offset, bytes, val);
			}
		} else {
			ret = -ENODEV;
		}
	}

	return ret;
}

/**
 * @brief Read register of a virtual PCI device which points to a passthrough PCI device
 *
 * In gerernal, this function emulates Configuration Space Header, MSI and MSI-X registers read operations. If
 * the register is not above registers and the virtual PCI device is not a quirk passthrough device, read physical
 * configure space register. But if the register is PCIR_ASLS_CTL of IGD device, read the virtual register. For
 * other cases, return ENODEV.
 *
 * @param[in]  vdev   Pointer to a virtual PCI device to read the register value.
 * @param[in]  offset Offset of the register to read.
 * @param[in]  bytes  Size to read.
 * @param[out] val    Value to be read.
 *
 * @return A signed integer to indicate whether the read is successful or not
 *
 * @retval 0       Succeed.
 * @retval -ENODEV When the register is not in scope described above and the virtual PCI device
 *                 is a quirk passthrough device.
 * @retval <0      When read_cfg_header fails.
 *
 * @pre vdev != NULL
 * @pre offset + bytes < PCI_CFG_HEADER_LENGTH
 * @pre val != NULL
 */
static int32_t read_pt_dev_cfg(struct pci_vdev *vdev, uint32_t offset,
		uint32_t bytes, uint32_t *val)
{
	int32_t ret = 0;

	if (cfg_header_access(offset)) {
		ret = read_cfg_header(vdev, offset, bytes, val);
	} else if (msicap_access(vdev, offset) || msixcap_access(vdev, offset)) {
		*val = pci_vdev_read_vcfg(vdev, offset, bytes);
	} else {
		if (!is_quirk_ptdev(vdev)) {
			/* passthru to physical device */
			*val = pci_pdev_read_cfg(vdev->pdev->bdf, offset, bytes);
			if ((vdev->pdev->bdf.value == CONFIG_IGD_SBDF) && (offset == PCIR_ASLS_CTL)) {
				*val = pci_vdev_read_vcfg(vdev, offset, bytes);
			}
		} else {
			ret = -ENODEV;
		}
	}

	return ret;
}

/**
 * @brief Instance of data structure for callback functions used to operate the virtual PCI device which
 *        points to a passthrough PCI device
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
static const struct pci_vdev_ops pci_pt_dev_ops = {
	.init_vdev	= vpci_init_pt_dev,
	.deinit_vdev	= vpci_deinit_pt_dev,
	.write_vdev_cfg	= write_pt_dev_cfg,
	.read_vdev_cfg	= read_pt_dev_cfg,
};

/**
 * @brief Read Configuration Space register
 *
 * This function is used to emulate Configuration Space register read from Guest VM. It first checks whether
 * the Guest VM has this virtual PCI device base on its BDF. If the virtual PCI device can be found, it calls
 * the read_vdev_cfg callback function for the virtual PCI device to execute the read operation.
 *
 * If there is no virtual PCI device found out, there are different ways to handle:
 *   - if the VM is a post-launched VM, return error.
 *   - if the BDF corresponding device is a platform hidden device, read the physical configuration space
 *     register.
 *   - for other cases, take no action.
 *
 * @param[in]  vpci   Pointer to a specified acrn_vcpi structure instance.
 * @param[in]  bdf    Device BDF (bus/device/function).
 * @param[in]  offset Offset of the register to read.
 * @param[in]  bytes  Size to read.
 * @param[out] val    Value to be read out.
 *
 * @return A signed integer to indicate whether the read is successful or not
 *
 * @retval 0       Succeed.
 * @retval -ENODEV When the device belongs to post-launched VM.
 * @retval <0      When corresponding read_vdev_cfg callback function fails.
 *
 * @pre vpci != NULL
 * @pre val != NULL
 */
static int32_t vpci_read_cfg(struct acrn_vpci *vpci, union pci_bdf bdf,
	uint32_t offset, uint32_t bytes, uint32_t *val)
{
	int32_t ret = 0;
	struct pci_vdev *vdev;

	spinlock_obtain(&vpci->lock);
	vdev = find_available_vdev(vpci, bdf);
	if (vdev != NULL) {
		ret = vdev->vdev_ops->read_vdev_cfg(vdev, offset, bytes, val);
	} else {
		if (is_postlaunched_vm(vpci2vm(vpci))) {
			ret = -ENODEV;
		} else if (is_plat_hidden_pdev(bdf)) {
			/* expose and pass through platform hidden devices */
			*val = pci_pdev_read_cfg(bdf, offset, bytes);
		} else {
			/* no action: e.g., PCI scan */
		}
	}
	spinlock_release(&vpci->lock);
	return ret;
}

/**
 * @brief Write Configuration Space register
 *
 * This function is used to emulate Configuration Space register write to Guest VM. It first checks whether
 * the Guest VM has this virtual PCI device base on its BDF. If the virtual PCI device can be found, it calls
 * the write_vdev_cfg callback function for the virtual PCI device to execute the write operation.
 *
 * If there is no virtual PCI device found out, there are different ways to handle:
 *   - if the VM is a post-launched VM, return error.
 *   - if the BDF corresponding device is a platform hidden device, write the native register.
 *   - for other cases, take no action.
 *
 * @param[in] vpci   Pointer to a specified acrn_vcpi structure instance.
 * @param[in] bdf    Device BDF (bus/device/function).
 * @param[in] offset Offset of the register to write.
 * @param[in] bytes  Size to write.
 * @param[in] val    Value to be written.
 *
 * @return A signed integer to indicate whether the write is successful or not
 *
 * @retval 0       Succeed.
 * @retval -ENODEV When the device belongs to post-launched VM.
 * @retval <0      When corresponding write_vdev_cfg callback function fails.
 *
 * @pre vpci != NULL
 */
static int32_t vpci_write_cfg(struct acrn_vpci *vpci, union pci_bdf bdf,
	uint32_t offset, uint32_t bytes, uint32_t val)
{
	int32_t ret = 0;
	struct pci_vdev *vdev;

	spinlock_obtain(&vpci->lock);
	vdev = find_available_vdev(vpci, bdf);
	if (vdev != NULL) {
		ret = vdev->vdev_ops->write_vdev_cfg(vdev, offset, bytes, val);
	} else {
		if (is_postlaunched_vm(vpci2vm(vpci))) {
			ret = -ENODEV;
		} else if (is_plat_hidden_pdev(bdf)) {
			/* expose and pass through platform hidden devices */
			pci_pdev_write_cfg(bdf, offset, bytes, val);
		} else {
			pr_warn("%s %x:%x.%x not found! off: 0x%x, val: 0x%x\n", __func__,
				bdf.bits.b, bdf.bits.d, bdf.bits.f, offset, val);
		}
	}
	spinlock_release(&vpci->lock);
	return ret;
}

/**
 * @brief Initialize a virtual PCI device
 *
 * This function is used to initialize a virtual PCI device structure with an input PCI device configuration
 * (dev_config). First it checks whether there's a free pci_vdevs slot by getting a free ID and checks below
 * criteria:
 *   - the free ID is valid (less than CONFIG_MAX_PCI_DEV_NUM) and,
 *   - there is callback ops in dev_config or there is no callback ops in dev_config but the device type is
 *     PCI_DEV_TYPE_PTDEV so that we can set pci_pt_dev_ops to the device.
 *
 * Then, we get the free vdev from the vdev array and initialize it. After that, we add the vdev into the current
 * VM by adding the vdev into the hash list and set the callback ops for it. The callback ops of passthrough
 * physical device is pci_pt_dev_ops. But the callback ops of other devices is got from the input PCI device
 * configuration (dev_config). At last, the init_vdev callback function is called to further initialize vdev.
 * If this function initializes a SRIOV Virtual Function (VF) vdev structure, the parameter parent_pf_vdev is
 * the VF associated Physical Function (PF) vdev structure, otherwise the parameter parent_pf_vdev is NULL. The
 * caller of this function shall guarantee execution atomically.
 *
 * @param[in] vpci           Pointer to a specified acrn_vcpi structure instance.
 * @param[in] dev_config     Pointer to a acrn_vm_pci_dev_config structure instance.
 * @param[in] parent_pf_vdev For SRIOV VF, it is the pointer to indicate the parent PF.
 *
 * @return Pointer to a initialized virtual PCI device structure instance
 *
 * @retval non-NULL Succeed to initialize a virtual PCI device structure.
 * @retval NULL     There is no free member in array vpci->pci_vdevs[].
 *
 * @pre vpci != NULL
 * @pre dev_config != NULL
 */
struct pci_vdev *vpci_init_vdev(struct acrn_vpci *vpci, struct acrn_vm_pci_dev_config *dev_config,
				struct pci_vdev *parent_pf_vdev)
{
	struct pci_vdev *vdev = NULL;
	uint32_t id = (uint32_t)ffz64_ex(vpci->vdev_bitmaps, CONFIG_MAX_PCI_DEV_NUM);

	if ((id < CONFIG_MAX_PCI_DEV_NUM) &&
	    (dev_config->vdev_ops ||
	    ((!dev_config->vdev_ops) && (dev_config->emu_type == PCI_DEV_TYPE_PTDEV)))) {
		bitmap_set_nolock((id & 0x3FU), &vpci->vdev_bitmaps[id >> 6U]);

		vdev = &vpci->pci_vdevs[id];
		vdev->id = id;
		vdev->vpci = vpci;
		vdev->bdf.value = dev_config->vbdf.value;
		vdev->pdev = dev_config->pdev;
		vdev->pci_dev_config = dev_config;
		vdev->phyfun = parent_pf_vdev;

		hlist_add_head(&vdev->link,
			       &vpci->vdevs_hlist_heads[hash64(dev_config->vbdf.value, VDEV_LIST_HASHBITS)]);
		if (dev_config->vdev_ops != NULL) {
			vdev->vdev_ops = dev_config->vdev_ops;
		} else {
			vdev->vdev_ops = &pci_pt_dev_ops;
		}
		vdev->vdev_ops->init_vdev(vdev);
	}

	return vdev;
}

/**
 * @brief Deinitialize a virtual PCI device
 *
 * This function is used to deinitialize a virtual PCI device structure. It calls deinit_vdev callback
 * function to do some deinitializing actions. Then, remove the vdev from the current VM by deleting the
 * vdev from the hash list, clear the bitmap to make the member of vdev array be free and clear the vdev
 * structure be all zero.
 *
 * @param[in] vdev Pointer to a virtual PCI device to be deinitialized.
 *
 * @return None
 *
 * @pre vdev != NULL
 * @pre vdev->vdev_ops != NULL
 * @pre vdev->vpci != NULL
 */
void vpci_deinit_vdev(struct pci_vdev *vdev)
{
	vdev->vdev_ops->deinit_vdev(vdev);

	hlist_del(&vdev->link);
	bitmap_clear_nolock((vdev->id & 0x3FU), &vdev->vpci->vdev_bitmaps[vdev->id >> 6U]);
	(void)memset(vdev, 0U, sizeof(struct pci_vdev));
}

/**
 * @brief Initialize virtual PCI devices
 *
 * This function is used to initialize each virtual PCI device in PCI device configurations list in each
 * VM config which configured by ACRN configure tool.
 *
 * Before initializing a virtual PCI device, this function would do some checks first:
 *   - for passthrough device, it checks if the corresponding physical device exists. If the physical device
 *     doesn't exist, return ENODEV. Check if the device Vendor ID and Device ID can match for pre-launched
 *     VM. If the ID of physical device doesn't match to virtual device, the error is returned.
 *   - for device which doesn't belong to post-launched VM, initialize the vdev and check whether PIO BAR is
 *     supported.
 *
 * @param[in] vm Pointer to a VM instance.
 *
 * @return A signed integer to indicate whether the write is successful or not
 *
 * @retval 0       Succeed.
 * @retval -ENODEV When dev_config->pdev is NULL or the input VM is pre-launched VM but
 *                 the Vendor ID and Device ID of dev_config don't match dev_config->pdev,
 *                 i.e. the configuration of the device doesn't match the physical device.
 * @retval <0      When check_pt_dev_pio_bars fails.
 *
 * @pre vm != NULL
 */
static int32_t vpci_init_vdevs(struct acrn_vm *vm)
{
	uint16_t idx;
	struct pci_vdev *vdev;
	struct acrn_vpci *vpci = &(vm->vpci);
	const struct acrn_vm_config *vm_config = get_vm_config(vpci2vm(vpci)->vm_id);
	struct acrn_vm_pci_dev_config *dev_config;
	int32_t ret = 0;

	for (idx = 0U; idx < vm_config->pci_dev_num; idx++) {
		dev_config = &vm_config->pci_devs[idx];

		if (dev_config->emu_type == PCI_DEV_TYPE_PTDEV) {
			if (dev_config->pdev == NULL) {
				pr_err("vm(name: %s) PTDev pbdf: %02x:%02x.%x does not exist on host platform."
					" Device ignored!\n", vm_config->name,
					dev_config->pbdf.bits.b, dev_config->pbdf.bits.d, dev_config->pbdf.bits.f);
				ret = -ENODEV;
				break;
			}
			/* Check PTdevs for prelaunch VM, mismatched PID&VID will fail to init vpci */
			if (is_prelaunched_vm(vm) && ((dev_config->pdev->vendor_id != dev_config->vendor_id) ||
						(dev_config->pdev->device_id != dev_config->device_id))) {
				pr_err("vm(name: %s) PTDev pbdf: %02x:%02x.%x mismatch, configured vendor_id: %x,"
					" device_id: %x, but detected vendor_id: %x, device_id: %x\n",
					vm_config->name, dev_config->pbdf.bits.b, dev_config->pbdf.bits.d,
					dev_config->pbdf.bits.f, dev_config->vendor_id, dev_config->device_id,
					dev_config->pdev->vendor_id, dev_config->pdev->device_id);
				ret = -ENODEV;
				break;
			}
		}

		/* the vdev whose vBDF is unassigned will be created by hypercall */
		if (!is_postlaunched_vm(vm)) {
			vdev = vpci_init_vdev(vpci, dev_config, NULL);
			if (vdev == NULL) {
				pr_err("%s: failed to initialize vpci, increase MAX_PCI_DEV_NUM in scenario!\n",
					__func__);
				ret = -EFAULT;
				break;
			}
			ret = check_pt_dev_pio_bars(&vpci->pci_vdevs[idx]);
			if (ret != 0) {
				break;
			}
		}
	}

	return ret;
}

/**
 * @brief Assign a PCI device from Service VM to target post-launched VM
 *
 * This function is used to assign a physical PCI device from Service VM to target post-launched VM. It would
 * first try to get the Service VM vPCI spinlock, then it would try to check whether the Service VM owns this
 * physical PCI device. If yes, it would try to remove the physicall PCI device access from SOS and try to assign
 * the physical PCI device to the target post-launched VM before releasing the Service VM vPCI spinlock. Assigning
 * the physical PCI device to the target post-launched VM is also under target VM vPCI spinlock protection.
 *
 * First, it finds a virtual PCI device through BDF from Service VM. Then, it does sanity check:
 * the found vdev is not NULL, the vdev has not been assigned to any VM, there is corresponding physical device
 * and the physical device is not a host bridge or bridge. If these criteria cannot be fulfilled, return ENODEV.
 * After passing these checks, follow below steps to assign the device to target VM:
 *   - For device which supports FLR or PM reset, restore the physical device BARs to initial
 *     values. Otherwise, print fatal error out.
 *   - Excute callback function to deinitialize the found out vdev.
 *   - Create and initialize a new virtual PCI device. If this action fails, error is returned.
 *   - Configure Interrupt Line/Interrupt Pin/BARs registers for the new virtual PCI device.
 *   - If PIO BAR is supported for the new vdev, add the device into virtual PCI device hash
 *     list and set necessary data (device type, virtual BDF, parent user and user).
 *   - If PIO BAR is not supported for the new vdev, deinitialize the new vdev and initialize
 *     the found out vdev again to restore it. Then, return the error.
 *
 * @param[in] tgt_vm Pointer to a VM instance.
 * @param[in] pcidev Pointer to acrn_pcidev structure instance which contains necessary information.
 *
 * @return A signed integer to indicate whether the assignment is successful or not
 *
 * @retval 0       Succeed.
 * @retval -EFAULT When fail to initialize PCI device by calling vpci_init_vdev.
 * @retval -ENODEV When fail to find vdev through BDF.
 * @retval <0      When check_pt_dev_pio_bars fails.
 *
 * @pre tgt_vm != NULL
 * @pre pcidev != NULL
 */
int32_t vpci_assign_pcidev(struct acrn_vm *tgt_vm, struct acrn_pcidev *pcidev)
{
	int32_t ret = 0;
	uint32_t idx;
	struct pci_vdev *vdev_in_service_vm, *vdev;
	struct acrn_vpci *vpci;
	union pci_bdf bdf;
	struct acrn_vm *service_vm;

	bdf.value = pcidev->phys_bdf;
	service_vm = get_service_vm();
	spinlock_obtain(&service_vm->vpci.lock);
	vdev_in_service_vm = pci_find_vdev(&service_vm->vpci, bdf);
	if ((vdev_in_service_vm != NULL) && (vdev_in_service_vm->user == vdev_in_service_vm) &&
			(vdev_in_service_vm->pdev != NULL) &&
			!is_host_bridge(vdev_in_service_vm->pdev) && !is_bridge(vdev_in_service_vm->pdev)) {

		/* ToDo: Each PT device must support one type reset */
		if (!vdev_in_service_vm->pdev->has_pm_reset && !vdev_in_service_vm->pdev->has_flr &&
				!vdev_in_service_vm->pdev->has_af_flr) {
			pr_fatal("%s %x:%x.%x not support FLR or not support PM reset\n",
				__func__, bdf.bits.b,  bdf.bits.d,  bdf.bits.f);
		} else {
			/* DM will reset this device before assigning it */
			pdev_restore_bar(vdev_in_service_vm->pdev);
		}

		vdev_in_service_vm->vdev_ops->deinit_vdev(vdev_in_service_vm);

		vpci = &(tgt_vm->vpci);

		spinlock_obtain(&tgt_vm->vpci.lock);
		vdev = vpci_init_vdev(vpci, vdev_in_service_vm->pci_dev_config, vdev_in_service_vm->phyfun);
		if (vdev != NULL) {
			pci_vdev_write_vcfg(vdev, PCIR_INTERRUPT_LINE, 1U, pcidev->intr_line);
			pci_vdev_write_vcfg(vdev, PCIR_INTERRUPT_PIN, 1U, pcidev->intr_pin);
			for (idx = 0U; idx < vdev->nr_bars; idx++) {
				/* VF is assigned to a User VM */
				if (vdev->phyfun != NULL) {
					vdev->vbars[idx] = vdev_in_service_vm->vbars[idx];
					if (has_msix_cap(vdev) && (idx == vdev->msix.table_bar)) {
						vdev->msix.mmio_hpa = vdev->vbars[idx].base_hpa;
						vdev->msix.mmio_size = vdev->vbars[idx].size;
					}
				}
				pci_vdev_write_vbar(vdev, idx, pcidev->bar[idx]);
			}

			ret = check_pt_dev_pio_bars(vdev);

			if (ret == 0) {
				vdev->flags |= pcidev->type;
				vdev->bdf.value = pcidev->virt_bdf;
				/*We should re-add the vdev to hashlist since its vbdf has changed */
				hlist_del(&vdev->link);
				hlist_add_head(&vdev->link,
					       &vpci->vdevs_hlist_heads[hash64(vdev->bdf.value, VDEV_LIST_HASHBITS)]);
				vdev->parent_user = vdev_in_service_vm;
				vdev_in_service_vm->user = vdev;
			} else {
				vdev->vdev_ops->deinit_vdev(vdev);
				vdev_in_service_vm->vdev_ops->init_vdev(vdev_in_service_vm);
			}
		} else {
			pr_fatal("%s, Failed to initialize PCI device %x:%x.%x for vm [%d]\n", __func__,
				pcidev->phys_bdf >> 8U, (pcidev->phys_bdf >> 3U) & 0x1fU, pcidev->phys_bdf & 0x7U,
				tgt_vm->vm_id);
			ret = -EFAULT;
		}
		spinlock_release(&tgt_vm->vpci.lock);
	} else {
		pr_fatal("%s, can't find PCI device %x:%x.%x for vm[%d] %x:%x.%x\n", __func__,
			pcidev->phys_bdf >> 8U, (pcidev->phys_bdf >> 3U) & 0x1fU, pcidev->phys_bdf & 0x7U,
			tgt_vm->vm_id,
			pcidev->virt_bdf >> 8U, (pcidev->virt_bdf >> 3U) & 0x1fU, pcidev->virt_bdf & 0x7U);
		ret = -ENODEV;
	}
	spinlock_release(&service_vm->vpci.lock);

	return ret;
}

/**
 * @brief De-assign a PCI device from target post-launched VM to Service VM
 *
 * This function is used to de-assign a PCI device from target post-launched VM to Service VM. If a virtual PCI
 * device can be found from Service VM with BDF and the sanity check can pass, remove the virtual PCI device from
 * the post-launched VM by deleting the virtual PCI device from hash list. The details are below.
 *
 * First, the function finds the vdev through BDF and does sanity check:
 *   - vdev is not NULL;
 *   - vdev has been assigned to target post-launched VM;
 *   - vdev has corresponding physical device;
 *   - BDF of vdev->pdev equals to input physical BDF.
 *
 * If the sanity check passes, deinitialize it. Then, call init_vdev callback function of parent vdev to restore
 * parent vdev if there is.
 *
 * If the sanity check fails, return ENODEV.
 *
 * @param[in] tgt_vm Pointer to a VM instance.
 * @param[in] pcidev Pointer to acrn_pcidev structure instance which contains necessary information.
 *
 * @return A signed integer to indicate whether the de-assignment is successful or not
 *
 * @retval 0       Succeed.
 * @retval -ENODEV When the vdev sanity check described above fails.
 *
 * @pre tgt_vm != NULL
 * @pre pcidev != NULL
 */
int32_t vpci_deassign_pcidev(struct acrn_vm *tgt_vm, struct acrn_pcidev *pcidev)
{
	int32_t ret = 0;
	struct pci_vdev *parent_vdev, *vdev;
	struct acrn_vpci *vpci;
	union pci_bdf bdf;

	bdf.value = pcidev->virt_bdf;
	vdev = pci_find_vdev(&tgt_vm->vpci, bdf);
	if ((vdev != NULL) && (vdev->user == vdev) && (vdev->pdev != NULL) &&
			(vdev->pdev->bdf.value == pcidev->phys_bdf)) {
		vpci = vdev->vpci;
		parent_vdev = vdev->parent_user;

		spinlock_obtain(&vpci->lock);
		vpci_deinit_vdev(vdev);
		spinlock_release(&vpci->lock);

		if (parent_vdev != NULL) {
			spinlock_obtain(&parent_vdev->vpci->lock);
			parent_vdev->vdev_ops->init_vdev(parent_vdev);
			spinlock_release(&parent_vdev->vpci->lock);
		}
	} else {
		pr_fatal("%s, can't find PCI device %x:%x.%x for vm[%d] %x:%x.%x\n", __func__,
			pcidev->phys_bdf >> 8U, (pcidev->phys_bdf >> 3U) & 0x1fU, pcidev->phys_bdf & 0x7U,
			tgt_vm->vm_id,
			pcidev->virt_bdf >> 8U, (pcidev->virt_bdf >> 3U) & 0x1fU, pcidev->virt_bdf & 0x7U);
		ret = -ENODEV;
	}

	return ret;
}

/**
 * @brief Update the vBAR value for a virtual PCI device
 *
 * This function is used to emulate BAR sizing or BAR re-program of a virtual PCI device. First, it calls
 * unmap callback function to deny PIO access by clearing I/O bitmap or prevent MMIO access by deleting EPT/
 * DMAR mapping according to the space type. Then, it updates the vBAR value in virutal configuration space. At
 * last, it calls map callback function to map the memory pointed by the new vBAR value if map callback
 * function exists and the vBAR value is updated successfully, i.e. set I/O bitmap or add EPT/DMAR mapping
 * according to the space type.
 *
 * @param[in] vdev     Pointer to a virtual PCI device.
 * @param[in] bar_idx  Index of the vBAR.
 * @param[in] val      Value to update to the vBAR.
 * @param[in] map_cb   Callback function to map the memory.
 * @param[in] unmap_cb Callback function to unmap the memory.
 *
 * @return None
 *
 * @pre vdev != NULL
 * @pre unmap_cb != NULL
 */
void vpci_update_one_vbar(struct pci_vdev *vdev, uint32_t bar_idx, uint32_t val,
		map_pcibar map_cb, unmap_pcibar unmap_cb)
{
	struct pci_vbar *vbar = &vdev->vbars[bar_idx];
	uint32_t update_idx = bar_idx;

	if (vbar->is_mem64hi) {
		update_idx -= 1U;
	}
	unmap_cb(vdev, update_idx);
	pci_vdev_write_vbar(vdev, bar_idx, val);
	if ((map_cb != NULL) && (vdev->vbars[update_idx].base_gpa != 0UL)) {
		map_cb(vdev, update_idx);
	}
}

#define CAP_START_OFFSET PCI_CFG_HEADER_LENGTH /**< Indicating PCI capability starting offset */

/**
 * @brief Add emulated PCIe capability support for virtual PCI device
 *
 * This function is used to add emulated PCIe capability support for virtual PCI device. First, it reads
 * Status register to get the new capability offset according to whether the Capabilities List is supported.
 * Then, it checks if the new capability offset plus the new capability length is not greater than the
 * limitation of the size of Configuration Space, i.e. 256 bytes. At last, it updates the virtual Configuration
 * Space registers to add the new capability and record the new capability offet/length in vdev.
 *
 * @param[in] vdev    Pointer to a virtual PCI device.
 * @param[in] capdata Pointer to buffer that holds the capability data to be added.
 * @param[in] caplen  Length of buffer that holds the capability data to be added.
 *
 * @return An unsigned integer to indicate the offset of the added capability
 *
 * @retval 0   Fail to add the capability when the corresponding capability range exceeds PCI_CONFIG_SPACE_SIZE.
 * @retval !=0 Capability is added successfully.
 *
 * @pre vdev != NULL
 * @pre vdev->vpci != NULL
 */
uint32_t vpci_add_capability(struct pci_vdev *vdev, uint8_t *capdata, uint8_t caplen)
{
	uint8_t capoff, reallen;
	uint32_t sts;
	uint32_t ret = 0U;

	reallen = roundup(caplen, 4U); /* dword aligned */

	sts = pci_vdev_read_vcfg(vdev, PCIR_STATUS, 2U);
	if ((sts & PCIM_STATUS_CAPPRESENT) == 0U) {
		capoff = CAP_START_OFFSET;
	} else {
		capoff = vdev->free_capoff;
	}

	/* Check if we have enough space */
	if (((uint16_t)capoff + reallen) <= PCI_CONFIG_SPACE_SIZE) {
		/* Set the previous capability pointer */
		if ((sts & PCIM_STATUS_CAPPRESENT) == 0U) {
			pci_vdev_write_vcfg(vdev, PCIR_CAP_PTR, 1U, capoff);
			pci_vdev_write_vcfg(vdev, PCIR_STATUS, 2U, sts|PCIM_STATUS_CAPPRESENT);
		} else {
			pci_vdev_write_vcfg(vdev, (uint32_t)vdev->prev_capoff + 1U, 1U, capoff);
		}

		/* Copy the capability */
		(void)memcpy_s((void *)&vdev->cfgdata.data_8[capoff], caplen, (void *)capdata, caplen);

		/* Set the next capability pointer */
		pci_vdev_write_vcfg(vdev, (uint32_t)capoff + 1U, 1U, 0U);

		vdev->prev_capoff = capoff;
		vdev->free_capoff = capoff + reallen;
		ret = capoff;
	}

	return ret;
}

/**
 * @brief Check whether the MSI-X is enabled for a virtual PCI device
 *
 * This function is used to check whether the MSI-X is enabled for a virtual PCI device. First, it checks
 * if the MSI-X capability exists by checking the MSI-X Capability offset. Then, it checks the MSI-X Enable
 * bit is set and the Function Mask bit is cleared in MSI-X Message Control register to judge whether the
 * MSI-X is enabled.
 *
 * @param[in] vdev Pointer to a virtual PCI device.
 *
 * @return A boolean value to indicate whether the MSI-X is enabled
 *
 * @retval true  Enabled.
 * @retval false Not enabled.
 *
 * @pre vdev != NULL
 */
bool vpci_vmsix_enabled(const struct pci_vdev *vdev)
{
	uint32_t msgctrl;
	bool ret = false;

	if (vdev->msix.capoff != 0U) {
		msgctrl = pci_vdev_read_vcfg(vdev, vdev->msix.capoff + PCIR_MSIX_CTRL, 2U);
		if (((msgctrl & PCIM_MSIXCTRL_MSIX_ENABLE) != 0U) &&
			((msgctrl & PCIM_MSIXCTRL_FUNCTION_MASK) == 0U)) {
			ret = true;
		}
	}
	return ret;
}

/**
 * @}
 */
