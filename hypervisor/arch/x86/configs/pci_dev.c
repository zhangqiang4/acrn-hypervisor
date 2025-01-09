/*
 * Copyright (C) 2019-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @addtogroup vp-base_vm_config
 *
 * @{
 */

/**
 * @file
 * @brief PCI device helper utilities
 *
 * This file contains helper utility on initializing ACRN device
 * configuration structure.
 */

#include <asm/vm_config.h>
#include <pci.h>
#include <asm/pci_dev.h>
#include <vpci.h>
#include <logmsg.h>

/**
 * @brief Check if a physical PCI device is allocated to any Pre-Launched VM.
 *
 * Check if a given physical PCI device \p pdev is allocated to any Pre-Launched VM
 * by comparing their BDFs.
 *
 * @pre pdev != NULL
 * @post N/A
 *
 * @param[in] pdev Physical PCI device structure to be checked
 *
 * @return true if \p pdev is allocated to any Pre-Launched VM, false otherwise.
 */
static bool allocate_to_prelaunched_vm(struct pci_pdev *pdev)
{
	bool found = false;
	uint16_t vmid;
	uint16_t pci_idx;
	struct acrn_vm_config *vm_config;
	struct acrn_vm_pci_dev_config *dev_config;

	for (vmid = 0U; (vmid < CONFIG_MAX_VM_NUM) && !found; vmid++) {
		vm_config = get_vm_config(vmid);
		if (vm_config->load_order == PRE_LAUNCHED_VM) {
			for (pci_idx = 0U; pci_idx < vm_config->pci_dev_num; pci_idx++) {
				dev_config = &vm_config->pci_devs[pci_idx];
				if ((dev_config->emu_type == PCI_DEV_TYPE_PTDEV) &&
						bdf_is_equal(dev_config->pbdf, pdev->bdf)) {
					dev_config->pdev = pdev;
					found = true;
					break;
				}
			}
		}
	}

	return found;
}


/**
 * @brief Initialize an acrn_vm_pci_dev_config structure
 *
 * Initialize an acrn_vm_pci_dev_config structure with a specified pdev structure
 * and add to Service VM's PCI device configuration list.
 * An acrn_vm_pci_dev_config is used to store a PCI device configuration for a VM.
 *
 * ACRN follows below rules to config device configuration:
 * - Hypervisor owns configured debug UART. Additionally if there's Pre-Launched VM,
 *   HV owns all Type 1 devices (bridges). These are hidden from all VMs.
 * - If physical devices are allocated to Pre-Launched VM, hide them from Service VM.
 * - If a multi-function PCI device function 0 is allocated to either Pre-Launched VM or
 *   hypervisor, a dummy placeholder device for function 0 is created and assigned to Service VM
 *   so that the devices with function 1 or higher can be detected from Service VM.
 * - The device configuration of Pre-Launched VM owned devices (physical or virtual) are
 *   initialized by config-tool.
 * - The device configuration of Post-Launched VMs contains only HV emulated virtual devices (for
 *   example, IVSHMEM), as Post-Launched VMs does not own physical devices.
 *
 * @remark The caller should guarantee atomic execution.
 *
 * @pre pdev != NULL
 *
 * @return A pointer to Service VM's device devconfig is returned.
 * @retval non-NULL-pointer One of the following applies:
 * - the \p pdev is a passed-through device being allocated to Service-VM. Service VM
 *   is granted access to the physical device. In this case the return value points to
 *   devconfig of physical device.
 * - the \p pdev is a Type-1 device. Service VM is granted access to hypervisor-emulated
 *   virtual device only. In this case the return value points to devconfig of
 *   hypervisor-emulated counterpart.
 * - the \p pdev is function 0 of a multi-function PCI device, but being allocated
 *   to Pre-Launched VM or hypervisor. Service VM is granted access to a dummy placeholder
 *   device. In this case the return value points to devconfig of dummy device.
 *
 * @retval NULL The physical device \p pdev shall be hidden from Service-VM.
 */
struct acrn_vm_pci_dev_config *init_one_dev_config(struct pci_pdev *pdev)
{
	struct acrn_vm_pci_dev_config *dev_config = NULL;
	bool is_allocated_to_prelaunched_vm = allocate_to_prelaunched_vm(pdev);
	bool is_allocated_to_hv = is_hv_owned_pdev(pdev->bdf);

	if (service_vm_config != NULL) {
		dev_config = &service_vm_config->pci_devs[service_vm_config->pci_dev_num];

		if (is_allocated_to_hv) {
			/* Service VM need to emulate the type1 pdevs owned by HV */
			dev_config->emu_type = PCI_DEV_TYPE_SERVICE_VM_EMUL;
			if (is_bridge(pdev)) {
				dev_config->vdev_ops = &vpci_bridge_ops;
			} else if (is_host_bridge(pdev)) {
				dev_config->vdev_ops = &vhostbridge_ops;
			} else {
				/* May have type0 device, E.g. debug pci uart */
				dev_config = NULL;
			}
		} else if (is_allocated_to_prelaunched_vm) {
			dev_config = NULL;
		} else {
			dev_config->emu_type = PCI_DEV_TYPE_PTDEV;
		}

		if ((is_allocated_to_hv || is_allocated_to_prelaunched_vm)
				&& (dev_config == NULL)
				&& is_pci_cfg_multifunction(pdev->hdr_type)
				&& (pdev->bdf.bits.f == 0U))
		{
#ifdef CONFIG_PCI_MULTIFUNCTION_SUPPORT
			dev_config = &service_vm_config->pci_devs[service_vm_config->pci_dev_num];
			dev_config->emu_type = PCI_DEV_TYPE_DUMMY_MF_EMUL;
			dev_config->vdev_ops = &vpci_mf_dev_ops;
#else
			pr_err("Function 0 of PCI device %02x:%02x is not allocated to service VM, "
			       "This will prevent other functions from getting probed. "
			       "Enable PCI_MULTIFUNCTION_SUPPORT in scenario file to resolve this.",
			       pdev->bdf.bits.b, pdev->bdf.bits.d);
#endif
		}
	}

	if (dev_config != NULL) {
		dev_config->vbdf.value = pdev->bdf.value;
		dev_config->pbdf.value = pdev->bdf.value;
		dev_config->pdev = pdev;
		service_vm_config->pci_dev_num++;
	}
	return dev_config;
}

/**
 * @}
 */
