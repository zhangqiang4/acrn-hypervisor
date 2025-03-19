/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <asm/guest/vm.h>
#include <asm/vmx.h>
#include <schedule.h>
#include <version.h>
#include <reloc.h>
#include <asm/vtd.h>
#include <asm/per_cpu.h>
#include <asm/lapic.h>
#include <asm/guest/assign.h>
#include <asm/guest/ept.h>
#include <asm/guest/vm.h>
#include <asm/mmu.h>
#include <hypercall.h>
#include <errno.h>
#include <logmsg.h>
#include <asm/ioapic.h>
#include <mmio_dev.h>
#include <ivshmem.h>
#include <vmcs9900.h>
#include <asm/irq.h>
#include <ticks.h>
#include <asm/cpuid.h>
#include <vroot_port.h>

/**
 * @defgroup vp-base_hypercall vp-base.hypercall
 * @ingroup vp-base
 * @brief The definition and implementation of hypercall module.
 *
 * This module provides public APIs for hypercalls, which are essential for
 * the Service VM to communicate with the hypervisor. These interfaces enable
 * operations, such as resource management and configuration, by interacting
 * directly with the hypervisor. This ensures efficient and secure management
 * of virtualized resources within the system.
 *
 * @{
 */

/**
 * @file
 * @brief This file provides the implementation of the hypercall handlers.
 *
 * This file contains the data structures and function implementations for hypercall handlers.
 * These handlers are responsible for processing hypercalls made by Service VM to the hypervisor,
 * enabling various operations such as resource management, configuration, and control
 * within the virtualized environment.
 */

/**
 * @brief Macro controls the debug level of the hypercall module.
 */
#define DBG_LEVEL_HYCALL	6U

/**
 * @brief The typedef for a function pointer named emul_dev_create to create an emulated device.
 *
 * This function pointer is intended to point to functions that take two parameters:
 * a pointer to a struct acrn_vm and a pointer to a struct acrn_vdev, and return an int32_t.
 */
typedef int32_t (*emul_dev_create) (struct acrn_vm *vm, struct acrn_vdev *dev);

/**
 * @brief The typedef for a function pointer named emul_dev_destroy to destroy an emulated device.
 *
 * This function pointer is intended to point to functions that take one parameters:
 * a pointer to a struct pci_vdev, and return an int32_t.
 */
typedef int32_t (*emul_dev_destroy) (struct pci_vdev *vdev);

/**
 * @brief emulated device operations structure.
 *
 * Use this structure to create and destroy an emulated device.
 */
struct emul_dev_ops {
	/**
	 * @brief A unique identifier for the emulated device.
	 *
	 * The low 32 bits represent the vendor id and device id of PCI device,
	 * and the high 32 bits represent the device number of the legacy device
	 */
	uint64_t dev_id;
	/* TODO: to re-use vdev_init/vdev_deinit directly in hypercall */
	emul_dev_create create;		/**< Function pointer for creating the emulated device */
	emul_dev_destroy destroy;	/**< Function pointer for destroying the emulated device */
};

/**
 * @brief A static array of emul_dev_ops.
 *
 * This array includes device operations for creating and destroying emulated devices.
 * The devices include Devices include IVSHMEM_DEVICE, MCS9900_DEV and VRP_DEVICE.
 */
static struct emul_dev_ops emul_dev_ops_tbl[] = {
#ifdef CONFIG_IVSHMEM_ENABLED
	/* If IVSHMEM is enabled, initialize with create and destroy functions for IVSHMEM */
	{(IVSHMEM_VENDOR_ID | (IVSHMEM_DEVICE_ID << 16U)), create_ivshmem_vdev , destroy_ivshmem_vdev},
#else
	/* If IVSHMEM is not enabled, set the create and destroy functions to NULL */
	{(IVSHMEM_VENDOR_ID | (IVSHMEM_DEVICE_ID << 16U)), NULL, NULL},
#endif
	{(MCS9900_VENDOR | (MCS9900_DEV << 16U)), create_vmcs9900_vdev, destroy_vmcs9900_vdev},
	{(VRP_VENDOR | (VRP_DEVICE << 16U)), create_vrp, destroy_vrp},
};

/**
 * @brief Check if the hypercall is made from ring 0 (privileged mode).
 *
 * This function reads the Code Segment (CS) selector of VM and checks the Current Privilege Level (CPL).
 * If the CPL is 0, it indicates that the hypercall is made from ring 0.
 *
 * @return A boolean value indicating whether the hypercall is made from ring 0.
 *
 * @retval true The hypercall is made from ring 0.
 * @retval false The hypercall is not made from ring 0.
 */
bool is_hypercall_from_ring0(void)
{
	uint16_t cs_sel;
	bool ret;

	cs_sel = exec_vmread16(VMX_GUEST_CS_SEL);
	/* cs_selector[1:0] is CPL */
	if ((cs_sel & 0x3U) == 0U) {
		ret = true;
	} else {
		ret = false;
	}

	return ret;
}

/**
 * @brief Handle hypercall to offline a vCPU from Service VM.
 *
 * This function handle hypercall of HC_SERVICE_VM_OFFLINE_CPU, which is used to offline designated vCPU.
 * The vCPU is found by its LAPIC ID. This function offline a vCPU by doing the following:
 * 1. Identify the target vCPU by iterating over all vCPUs and matching the APIC ID with lapicid.
 * 2. Initialize a mask for CPU operations and retrieve the physical CPU ID (pcpu_id).
 * 3. Set the corresponding bit in the mask for the target physical CPU.
 * 4. Check if the target vCPU is the BSP or the current vCPU, and exit if true.
 * 5. Mark the target vCPU as a "zombie" using zombie_vcpu.
 * 6. Take the target vCPU offline with offline_vcpu.
 * 7. Initiate offlining of the physical CPU using make_pcpu_offline.
 * 8. Wait for the physical CPU to be offline with wait_pcpus_offline.
 * 9. Restart the physical CPU using start_pcpus.
 *
 * On success, ACRN hypervisor shall return 0. If the LAPIC ID doesn't belong to Service VM, ACRN hypervisor
 * does nothing and returns 0. But if the input LAPIC ID related vCPU is the BSP or the current vCPU,
 * ACRN hypervisor shall return -1.
 *
 * @param[inout] vcpu Pointer to vCPU that initiates the hypercall.
 * @param[in] target_vm Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[in] param1 The LAPIC ID of the CPU to be offlined.
 * @param[in] param2 Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 *
 * @return An int32_t value indicating whether the hypercall is success.
 *
 * @retval 0 On success.
 * @retval -1 If attempting to offline the BSP or the current vCPU.
 *
 * @pre is_service_vm(vcpu->vm) == true
 */
int32_t hcall_service_vm_offline_cpu(struct acrn_vcpu *vcpu, __unused struct acrn_vm *target_vm,
		uint64_t param1, __unused uint64_t param2)
{
	struct acrn_vcpu *target_vcpu;
	uint16_t i;
	int32_t ret = 0;
	uint32_t lapicid = (uint32_t)param1;

	pr_info("Service VM offline cpu with lapicid %d", lapicid);

	foreach_vcpu(i, vcpu->vm, target_vcpu) {
		if (vlapic_get_apicid(vcpu_vlapic(target_vcpu)) == lapicid) {
			uint64_t mask = 0UL;
			uint16_t pcpu_id = pcpuid_from_vcpu(target_vcpu);
			bitmap_set_nolock(pcpu_id, &mask);

			/* should not offline BSP or current vCPU*/
			if ((target_vcpu->vcpu_id == BSP_CPU_ID) || (target_vcpu == vcpu)) {
				ret = -1;
				break;
			}
			zombie_vcpu(target_vcpu, VCPU_ZOMBIE);
			offline_vcpu(target_vcpu);

			/* restart pcpu so that it restores all its states */
			make_pcpu_offline(pcpu_id);
			wait_pcpus_offline(mask);
			start_pcpus(mask);
		}
	}

	return ret;
}

/**
 * @brief Handle hypercall to create a virtual machine (VM).
 *
 * This function handles the hypercall `HC_CREATE_VM`, which is used to create the target VM based on
 * the provided configuration and parameters. The configuration is copied from the guest's physical
 * address space, and the VM is created if the conditions are met.
 *
 * The function performs the following steps:
 * 1. Copy the VM creation configuration from the guest's physical address specified by `param1`.
 *    - Returns -1 if the copy fails.
 * 2. Check if the target VM is powered off and eligible for creation.
 *    - Returns -1 if the VM is not powered off.
 * 3. Filter and assign guest flags, ensuring that only allowed flags are set by the Device Model (DM).
 * 4. Validate the CPU affinity specified in the configuration, ensuring it is within allowed limits.
 *    - Returns -1 if the CPU affinity is invalid.
 * 5. Check for required guest flags, such as `GUEST_FLAG_LAPIC_PASSTHROUGH` when `GUEST_FLAG_RT` is set.
 *    - Returns -1 if required flags are missing.
 * 6. Attempt to create the VM with the specified configuration and CPU affinity.
 *    - If successful, set the relative VM ID and the number of created vCPUs to the `cv`.
 *    - If creation fails, log an error and set `cv.vmid` to `ACRN_INVALID_VMID`
 * 7. Copies the VM creation result back to the guest's physical address specified by `param1`.
 *    - The function's return value is set to the result of `copy_to_gpa`.
 * 8. If the operation fails or the VM ID is invalid, clear the VM name if the VM is not statically configured.
 *    - Ensures cleanup of VM configuration.
 *
 * @param[in] vcpu The vCPU making the hypercall.
 * @param[inout] target_vm The target VM to be created.
 * @param[in] param1 The guest physical memory address. This gpa points to struct acrn_vm_creation.
 * @param[in] param2 Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 *
 * @return An int32_t value indicating the result of the VM creation operation.
 *
 * @retval 0 On success, indicating that the VM was successfully created.
 * @retval !=0 On failure, indicating that the VM creation was unsuccessful.
 *
 * @pre is_service_vm(vcpu->vm) == true
 * @pre get_vm_config(target_vm->vm_id) != NULL
 * @remark Currently there is no limitation for calling times of this function, will add MAX_VM_NUM support later.
 */
int32_t hcall_create_vm(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, uint64_t param1, __unused uint64_t param2)
{
	struct acrn_vm *vm = vcpu->vm;
	uint16_t vmid = target_vm->vm_id;
	int32_t ret = -1;
	struct acrn_vm *tgt_vm = NULL;
	struct acrn_vm_creation cv;
	struct acrn_vm_config *vm_config = get_vm_config(vmid);

	if (copy_from_gpa(vm, &cv, param1, sizeof(cv)) == 0) {
		if (is_poweroff_vm(get_vm_from_vmid(vmid))) {

			/* Filter out the bits should not set by DM and then assign it to guest_flags */
			vm_config->guest_flags &= ~DM_OWNED_GUEST_FLAG_MASK;
			vm_config->guest_flags |= (cv.vm_flag & DM_OWNED_GUEST_FLAG_MASK);

			/* post-launched VM is allowed to choose pCPUs from vm_config->cpu_affinity only */
			if (((cv.cpu_affinity & ~(vm_config->cpu_affinity)) == 0UL) &&
					((cv.cpu_affinity & ~(ALL_CPUS_MASK)) == 0UL)) {
				/* By default launch VM with all the configured pCPUs */
				uint64_t pcpu_bitmap = vm_config->cpu_affinity;

				if (cv.cpu_affinity != 0UL) {
					/* overwrite the statically configured CPU affinity */
					pcpu_bitmap = cv.cpu_affinity;
				}

				/*
				 * GUEST_FLAG_LAPIC_PASSTHROUGH must be set if we have GUEST_FLAG_RT
				 * set in guest_flags
				 */
				if (((vm_config->guest_flags & GUEST_FLAG_RT) != 0UL)
						&& ((vm_config->guest_flags & GUEST_FLAG_LAPIC_PASSTHROUGH) == 0UL)) {
					pr_err("Wrong guest flags 0x%lx\n", vm_config->guest_flags);
				} else {
					if (create_vm(vmid, pcpu_bitmap, vm_config, &tgt_vm) == 0) {
						/* return a relative vm_id from Service VM view */
						cv.vmid = vmid_2_rel_vmid(vm->vm_id, vmid);
						cv.vcpu_num = tgt_vm->hw.created_vcpus;
					} else {
						dev_dbg(DBG_LEVEL_HYCALL, "HCALL: Create VM failed");
						cv.vmid = ACRN_INVALID_VMID;
					}

					ret = copy_to_gpa(vm, &cv, param1, sizeof(cv));
				}
			} else {
				pr_err("Post-launched VM%u chooses invalid pCPUs(0x%llx).",
						vmid, cv.cpu_affinity);
			}
		}

	}

	if (((ret != 0) || (cv.vmid == ACRN_INVALID_VMID)) && (!is_static_configured_vm(target_vm))) {
		memset(vm_config->name, 0U, MAX_VM_NAME_LEN);
	}

	return ret;
}

/**
 * @brief Handle hypercall to destroy a virtual machine (VM).
 *
 * This function handles the hypercall `HC_DESTROY_VM`, which is used to destroy the target VM. The
 * VM is destroyed if it is in a paused state.
 *
 * On success, the ACRN hypervisor shall return 0. However, the hypervisor shall return an error code
 * in the following scenarios:
 * 1. The target VM state is not paused.
 * 2. Failed in `shutdown_vm()`.
 *
 * @param[in] vcpu Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[inout] target_vm The target VM to be destroyed.
 * @param[in] param1 Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[in] param2 Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 *
 * @return An int32_t value indicating the result of the VM destruction operation.
 *
 * @retval 0 On success, indicating that the VM was successfully destroyed.
 * @retval !=0 On error, indicating that the VM could not be destroyed.
 *
 * @pre is_service_vm(vcpu->vm) == true
 */
int32_t hcall_destroy_vm(__unused struct acrn_vcpu *vcpu, struct acrn_vm *target_vm,
		__unused uint64_t param1, __unused uint64_t param2)
{
	int32_t ret = -1;

	if (is_paused_vm(target_vm)) {
		/* TODO: check target_vm guest_flags */
		ret = shutdown_vm(target_vm);
	}

	return ret;
}

/**
 * @brief Handle hypercall to start a virtual machine (VM).
 *
 * This function handles the hypercall `HC_START_VM`, which is used to start the target VM. The VM
 * is started if it is in a created state and has a valid I/O shared page. The I/O shared page is used
 * for facilitating communication between the VM and the hypervisor, particularly for handling I/O requests
 * efficiently. If the startup is successful, the target VM's vCPU will be scheduled to run.
 *
 * On success, the ACRN hypervisor shall return 0. However, the hypervisor shall return an error code
 * in the following scenarios:
 * 1. The target VM state is not created. The error code is -1.
 * 2. If the target VM IOReq buffer page is not ready. The error code is -1.
 *
 * @param[in] vcpu Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[inout] target_vm The target VM to be destroyed.
 * @param[in] param1 Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[in] param2 Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 *
 * @return An int32_t value indicating the result of the VM start operation.
 *
 * @retval 0 On success, indicating that the VM was successfully started.
 * @retval !=0 On failure, indicating that the VM could not be started.
 *
 * @pre is_service_vm(vcpu->vm) == true
 */
int32_t hcall_start_vm(__unused struct acrn_vcpu *vcpu, struct acrn_vm *target_vm,
		__unused uint64_t param1, __unused uint64_t param2)
{
	int32_t ret = -1;

	if ((is_created_vm(target_vm)) && (target_vm->sw.io_shared_page != NULL)) {
		/* TODO: check target_vm guest_flags */
		start_vm(target_vm);
		ret = 0;
	}

	return ret;
}

/**
 * @brief Handle hypercall to pause a virtual machine (VM).
 *
 * This function handles the hypercall `HC_PAUSE_VM`, which is used to pause the target VM. The VM
 * is paused if it is not in a powered-off state.
 *
 * On success, the ACRN hypervisor shall return 0. However, the hypervisor shall return an error code
 * -1 when the target VM state is powered off.
 *
 * @param[in] vcpu Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[inout] target_vm Pointer to target VM data structure.
 * @param[in] param1 Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[in] param2 Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 *
 * @return An int32_t value indicating the result of the VM pause operation.
 *
 * @retval 0 On success, indicating that the VM was successfully paused.
 * @retval -1 If the target VM is powered off, indicating that the VM could not be paused.
 *
 * @pre is_service_vm(vcpu->vm) == true
 */
int32_t hcall_pause_vm(__unused struct acrn_vcpu *vcpu, struct acrn_vm *target_vm,
		__unused uint64_t param1, __unused uint64_t param2)
{
	int32_t ret = -1;

	if (!is_poweroff_vm(target_vm)) {
		/* TODO: check target_vm guest_flags */
		pause_vm(target_vm);
		ret = 0;
	}
	return ret;
}

/**
 * @brief Handle hypercall to reset a virtual machine (VM).
 *
 * This function handles the hypercall `HC_RESET_VM`, which is used to reset the target VM. The VM
 * is reset if it is in a paused state. Comparing to start vm, this function reset
 * each vcpu state and do some initialization for guest.
 *
 * On success, ACRN hypervisor shall return 0. But ACRN hypervisor shall return error code in below scenarios.
 * 1. The target VM state is not paused.
 * 2. The `reset_vm` function fails.
 *
 * @param[in] vcpu Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[inout] target_vm Pointer to target VM data structure.
 * @param[in] param1 Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[in] param2 Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 *
 * @return An int32_t value indicating the result of the VM reset operation.
 *
 * @retval 0 On success, indicating that the VM was successfully reset.
 * @retval <0 On failure, indicating that the VM could not be reset.
 *
 * @pre is_service_vm(vcpu->vm) == true
 */
int32_t hcall_reset_vm(__unused struct acrn_vcpu *vcpu, struct acrn_vm *target_vm,
		__unused uint64_t param1, __unused uint64_t param2)
{
	int32_t ret = -1;

	if (is_paused_vm(target_vm)) {
		/* TODO: check target_vm guest_flags */
		ret = reset_vm(target_vm, VM_POWER_ON_RESET);
	}
	return ret;
}

/**
 * @brief Handle hypercall to set the registers of a virtual CPU (vCPU).
 *
 * This function handles the hypercall `HC_SET_VCPU_REGS`, which is used to configure the initial register state
 * of a vCPU. The second parameter (`param2`) is the GPA where the vCPU register information is located.
 * It sets the vCPU initial registers from the Device Model (DM). Currently, this is only applied to the
 * Bootstrap Processor (BSP). Application Processors (APs) always use fixed initial registers.
 *
 * On success, ACRN hypervisor shall return 0. But ACRN hypervisor shall return error code in below scenarios.
 * 1. The target VM state is powered off. The error code is -1.
 * 2. The GPA saved in the param2 is 0. The error code is -1.
 * 3. The target VM state is started. The error code is -1.
 * 4. Fail to copy vCPU register information from the GPA saved in the param2, i.e.
 *    the [param2, param2 + sizeof(struct acrn_vcpu_regs) bytes] is out of EPT scope. The error code is -1.
 * 5. The vCPU id contained in vCPU register information exceeds the max vCPU number per VM.
 *    The error code is -1.
 * 6. The target vCPU state is offline. The error code is -1.
 * 7. The vCPU CR0 or CR4 register value contained in vCPU register information got from guest memory is not valid,
 *    i.e. the reserved bits are set. The error code is -1.

 * @param[inout] vcpu Pointer to vCPU that initiates the hypercall.
 * @param[inout] target_vm Pointer to target VM data structure.
 * @param[in] param1 Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[in] param2 Guest physical address. This gpa points to struct acrn_vcpu_regs.
 *
 * @return An int32_t value indicating the result of the vCPU register setup operation.
 *
 * @retval 0 On success, indicating that the vCPU registers were successfully set.
 * @retval -1 On failure, indicating that the vCPU registers could not be set.
 *
 * @pre is_service_vm(vcpu->vm) == true
 */
int32_t hcall_set_vcpu_regs(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm,
		__unused uint64_t param1, uint64_t param2)
{
	struct acrn_vm *vm = vcpu->vm;
	struct acrn_vcpu_regs vcpu_regs;
	struct acrn_vcpu *target_vcpu;
	int32_t ret = -1;

	/* Only allow setup init ctx while target_vm is inactive */
	if ((!is_poweroff_vm(target_vm)) && (param2 != 0U) && (target_vm->state != VM_RUNNING)) {
		if (copy_from_gpa(vm, &vcpu_regs, param2, sizeof(vcpu_regs)) != 0) {
		} else if (vcpu_regs.vcpu_id >= MAX_VCPUS_PER_VM) {
			pr_err("%s: invalid vcpu_id for set_vcpu_regs\n", __func__);
		} else {
			target_vcpu = vcpu_from_vid(target_vm, vcpu_regs.vcpu_id);
			if (target_vcpu->state != VCPU_OFFLINE) {
				if (is_valid_cr0_cr4(target_vm, vcpu_regs.vcpu_regs.cr0, vcpu_regs.vcpu_regs.cr4)) {
					set_vcpu_regs(target_vcpu, &(vcpu_regs.vcpu_regs));
					ret = 0;
				}
			}
		}
	}

	return ret;
}

/**
 * @brief Handle hypercall to set an IRQ line for a virtual machine (VM).
 *
 * This function handles the hypercall `HC_SET_IRQLINE`, which is used to set or clear a virtual IRQ
 * line for the target VM. The IRQ line could be from ISA or IOAPIC, and it typically triggers an edge IRQ.
 *
 * The first parameter is the VM ID of the target VM. The second parameter contains the IRQ line information.
 * On success, the ACRN hypervisor shall return 0. However, the hypervisor shall return an error code
 * in the following scenarios:
 * 1. The target VM does not meet the required severity level. The error code is -1.
 * 2. The target VM state is powered off. The error code is -1.
 * 3. The GSI contained in the guest IRQ line information is greater than the target VM's maximum GSI.
 *    The error code is -1.
 *
 * @param[in] vcpu Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[inout] target_vm The target VM for which the IRQ line is to be set.
 * @param[in] param1 Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[in] param2 The parameter containing the IRQ line operations.
 *
 * @return An int32_t value indicating the result of the interrupt line setup operation.
 *
 * @retval 0 On success, indicating that the interrupt line state was successfully set.
 * @retval -1 On failure, indicating that the interrupt line state could not be set.
 * @pre is_service_vm(vcpu->vm) == true
 */
int32_t hcall_set_irqline(__unused struct acrn_vcpu *vcpu, struct acrn_vm *target_vm,
	__unused uint64_t param1, uint64_t param2)
{
	int32_t ret = -1;
	struct acrn_irqline_ops *ops = (struct acrn_irqline_ops *)&param2;

	if (is_severity_pass(target_vm->vm_id) && !is_poweroff_vm(target_vm)) {
		if (ops->gsi < get_vm_gsicount(target_vm)) {
			/* handle IOAPIC irqline */
			vioapic_set_irqline_lock(target_vm, ops->gsi, ops->op);
			ret = 0;
		}
	}

	return ret;
}

/**
 * @brief Handle hypercall to inject a Message Signaled Interrupt (MSI) into a virtual machine (VM).
 *
 * This function handles the hypercall `HC_INJECT_MSI`, which is used to injects an MSI into a VM.
 * The MSI information is copied from the guest's memory pointed to by `param2`,
 * and the interrupt is injected if all conditions are met.
 *
 * On success, the ACRN hypervisor shall return 0. However, the hypervisor shall return an error code
 * in the following scenarios:
 * 1. The target VM does not meet the required severity level. The error code is -1.
 * 2. The target VM state is powered off. The error code is -1.
 * 3. Fail to copy MSI information from guest memory. The error code is -1.
 * 4. Failure in `vlapic_inject_msi`. The error code can refer the `vlapic_inject_msi` function.
 *
 * @param[inout] vcpu Pointer to vCPU that initiates the hypercall.
 * @param[inout] target_vm Pointer to target VM data structure.
 * @param[in] param1 Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[in] param2 Guest physical address. This gpa points to struct acrn_msi_entry.
 *
 * @return An int32_t value indicating the result of the MSI injection operation.
 *
 * @retval 0 On successful MSI injection.
 * @retval !=0 On failure.
 *
 * @pre is_service_vm(vcpu->vm) == true
 */
int32_t hcall_inject_msi(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, __unused uint64_t param1, uint64_t param2)
{
	struct acrn_vm *vm = vcpu->vm;
	int32_t ret = -1;

	if (is_severity_pass(target_vm->vm_id) && !is_poweroff_vm(target_vm)) {
		struct acrn_msi_entry msi;

		if (copy_from_gpa(vm, &msi, param2, sizeof(msi)) == 0) {
			ret = vlapic_inject_msi(target_vm, msi.msi_addr, msi.msi_data);
		}
	}

	return ret;
}

/**
 * @brief Handle hypercall to set the I/O request buffer for a virtual machine (VM).
 *
 * This function handles the hypercall `HC_SET_IOREQ_BUFFER`,
 * which is used to sets the I/O request buffer for a VM if the VM is in the created state.
 * The I/O request buffer is essential for handling I/O request between the VM and the hypervisor.
 *
 * On success, ACRN hypervisor shall return 0. But ACRN hypervisor shall return error code in below scenarios.
 * 1. The target VM state is not created. The error code is -1.
 * 2. Fail to copy the guest shared buffer address from guest memory which GPA is saved in the param2.
 *    The error code is -1.
 * 3. The guest shared buffer host physical address is invalid. The error code is -1.
 *
 * @param[inout] vcpu Pointer to vCPU that initiates the hypercall.
 * @param[inout] target_vm Pointer to target VM data structure.
 * @param[in] param1 Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[in] param2 Guest physical address. This gpa points to buffer address.
 *
 * @return An int32_t value indicating whether the I/O request buffer setup is successful.
 *
 * @retval 0 On success, indicating that the I/O request buffer was successfully set.
 * @retval -1 On error, indicating that the I/O request buffer could not be set.
 *
 * @pre is_service_vm(vcpu->vm) == true
 */
int32_t hcall_set_ioreq_buffer(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm,
		__unused uint64_t param1, uint64_t param2)
{
	struct acrn_vm *vm = vcpu->vm;
	uint64_t hpa;
	uint16_t i;
	int32_t ret = -1;

	if (is_created_vm(target_vm)) {
		uint64_t iobuf;

		if (copy_from_gpa(vm, &iobuf, param2, sizeof(iobuf)) == 0) {
			dev_dbg(DBG_LEVEL_HYCALL, "[%d] SET BUFFER=0x%lx",
					target_vm->vm_id, iobuf);

			hpa = gpa2hpa(vm, iobuf);
			if (hpa == INVALID_HPA) {
				pr_err("%s,vm[%hu] gpa 0x%lx,GPA is unmapping.",
					__func__, vm->vm_id, iobuf);
				target_vm->sw.io_shared_page = NULL;
			} else {
				target_vm->sw.io_shared_page = hpa2hva(hpa);
				for (i = 0U; i < ACRN_IO_REQUEST_MAX; i++) {
					set_io_req_state(target_vm, i, ACRN_IOREQ_STATE_FREE);
				}
				ret = 0;
			}
		}
	}

	return ret;
}

/**
 * @brief Handle hypercall to set up a shared buffer (sbuf) for a virtual machine (VM).
 *
 * This function handles the hypercall `HC_SETUP_SBUF`, which is used to set up a shared buffer
 * between the Service VM and the hypervisor. The shared buffer facilitates communication between
 * the Service VM and the hypervisor.
 *
 * The function performs the following steps:
 * 1. Copies the shared buffer parameters from the guest's physical address specified by `param2`.
 *    - Returns -1 if the copy operation fails.
 * 2. Validates the GPA of the shared buffer to ensure it is non-zero and page-aligned.
 *    - Returns -1 if the GPA is zero or not aligned.
 * 3. Translates the GPA to a host virtual address (HVA) and checks if the translation is successful.
 *    - Returns -1 if the translation fails.
 * 4. Accesses the shared buffer to verify the magic number and calculate the total size.
 *    - Uses `stac()` and `clac()` to safely access the buffer.
 * 5. Validates the magic number and size of the shared buffer.
 *    - Returns -1 if the magic number is incorrect or the size is zero.
 * 6. Checks the validity of the memory range if the buffer size exceeds one page.
 *    - Returns a non-zero value if the memory range is invalid.
 * 7. Calls `sbuf_setup_common` to complete the setup of the shared buffer.
 *    - Returns a non-zero value if the setup fails.
 *
 * @param[inout] vcpu Pointer to vCPU that initiates the hypercall.
 * @param[inout] target_vm The target VM for which the shared buffer is to be set up.
 * @param[in] param1 Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[in] param2 Guest physical address. This gpa points to struct acrn_sbuf_param.
 *
 * @return An int32_t value indicating the result of the shared buffer setup operation.
 *
 * @retval 0 On successful setup of the shared buffer.
 * @retval !=0 On failure, indicating that the shared buffer could not be set up.
 *
 * @pre is_service_vm(vcpu->vm) == true
 */
int32_t hcall_setup_sbuf(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm,
		__unused uint64_t param1, uint64_t param2)
{
	struct acrn_vm *vm = vcpu->vm;
	struct acrn_sbuf_param asp;
	struct shared_buf *sbuf;
	uint64_t magic;
	uint32_t size;
	int ret = -1;

	if ((copy_from_gpa(vm, &asp, param2, sizeof(asp)) == 0) &&
	    (asp.gpa != 0U) && mem_aligned_check(asp.gpa, PAGE_SIZE) &&
	    ((sbuf = (struct shared_buf*)gpa2hva(vm, asp.gpa)) != NULL)) {
		/* pr_* breaks stac/clac */
		stac();
		magic = sbuf->magic;
		size = sbuf->size + SBUF_HEAD_SIZE;
		clac();

		if ((magic == SBUF_MAGIC) && (size != 0)) {
			ret = 0;
			pr_info("sbuf: setting up sbuf at gpa 0x%016lx, size 0x%x, type: %d",
				 asp.gpa, size, asp.sbuf_id);

			/* Check from the next page of header locates */
			if (size > PAGE_SIZE) {
				ret = !ept_is_valid_mr(vm, asp.gpa + PAGE_SIZE, (uint64_t)(size - PAGE_SIZE));
			}

			if (ret == 0) {
				ret = sbuf_setup_common(target_vm, asp.cpu_id, asp.sbuf_id, asp.gpa, sbuf);
			}
		}
	}

	if (ret != 0) {
		pr_err("sbuf: invalid sbuf gpa 0x%016lx", asp.gpa);
	}
	return ret;
}

/**
 * @brief Handle hypercall to assign asynchronous I/O (async I/O) request to a virtual machine (VM).
 *
 * This function handles the hypercall `HC_ASYNCIO_ASSIGN`, which is used to assign async I/O request
 * to a VM if the provided parameters are valid.  It ensures that the async I/O information is correctly
 * copied from the guest memory and that the async I/O request can be registered with the target VM.
 *
 * On success, the ACRN hypervisor shall return 0. However, the hypervisor shall return an error code -1
 * in the following scenarios:
 * 1. Fail to copy the async I/O information from guest memory, which GPA is saved in the param2.
 * 2. Failed in add_asyncio().
 *
 * @param[in] vcpu Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[inout] target_vm The target VM to which the async I/O request is to be assigned.
 * @param[in] param1 Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[in] param2 The guest physical address. This gpa points to struct acrn_asyncio_info.
 *
 * @return An int32_t value indicating whether the hypercall is success.
 *
 * @retval 0 On successful assignment of the async I/O request.
 * @retval -1 On error to assign the async I/O request.
 *
 * @pre is_service_vm(vcpu->vm) == true
 */
int32_t hcall_asyncio_assign(__unused struct acrn_vcpu *vcpu, struct acrn_vm *target_vm,
		 __unused uint64_t param1, uint64_t param2)
{
	struct acrn_asyncio_info asyncio_info;
	struct acrn_vm *vm = vcpu->vm;
	int ret = -1;

	if (copy_from_gpa(vm, &asyncio_info, param2, sizeof(asyncio_info)) == 0) {
		ret = add_asyncio(target_vm, &asyncio_info);
	}
	return ret;
}

/**
 * @brief Handle hypercall to deassign asynchronous I/O (async I/O) request from a virtual machine (VM).
 *
 * This function handles the hypercall HC_ASYNCIO_DEASSIGN, which removes asynchronous I/O resources
 * from a target VM. The asyncio information is copied from the guest's physical address space, and the
 * resources are deassigned from the VM if the copy operation is successful.
 *
 * The first parameter is the target VM ID. The second parameter is the GPA which saves the async I/O information.
 * On success, the ACRN hypervisor returns 0. The function returns an error code -1 in the following scenarios:
 * 1. Failure to copy the async I/O information from guest memory.
 * 2. Failed in remove_asyncio().
 *
 * @param[in] vcpu Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[inout] target_vm The target VM from which the async I/O operations are to be deassigned.
 * @param[in] param1 Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[in] param2 The guest physical address. This gpa points to struct acrn_asyncio_info.
 *
 * @return An int32_t value indicating whether the hypercall is success.
 *
 * @retval 0 On successful deassignment of the async I/O request.
 * @retval -1 On error to deassign the async I/O request.
 *
 * @pre is_service_vm(vcpu->vm) == true
 */
int32_t hcall_asyncio_deassign(__unused struct acrn_vcpu *vcpu, struct acrn_vm *target_vm,
		 __unused uint64_t param1, uint64_t param2)
{
	struct acrn_asyncio_info asyncio_info;
	struct acrn_vm *vm = vcpu->vm;
	int ret = -1;

	if (copy_from_gpa(vm, &asyncio_info, param2, sizeof(asyncio_info)) == 0) {
		ret = remove_asyncio(target_vm, &asyncio_info);
	}
	return ret;
}

/**
 * @brief Handle hypercall to notify that an I/O request has finished for a virtual machine (VM).
 *
 * This function handles the hypercall HC_NOTIFY_REQUEST_FINISH, which is used to notify the requestor
 * vCPU for the completion of an I/O request.
 *
 * The function performs the following steps:
 * 1. Validates that the target VM passes the severity check, is not powered off, and has a set I/O shared page.
 *    - Returns -1 if any of these conditions are not met.
 * 2. Checks if the vCPU ID specified in `param2` is valid.
 *    - Returns -1 if the vCPU ID exceeds the number of created vCPUs.
 * 3. Retrieves the target vCPU and signals the completion event if the VM is not polling for I/O requests.
 *    - Sets the return value to 0 to indicate success.
 *
 * @param[in] vcpu Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[inout] target_vm Pointer to target VM data structure.
 * @param[in] param1 Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[in] param2 The vcpu ID of the requestor.
 *
 * @return An int32_t value indicating whether the hypercall is success.
 *
 * @retval 0 On successful notification of the I/O request completion.
 * @retval -1 On error to notify the I/O request completion.
 *
 * @pre is_service_vm(vcpu->vm) == true
 */
int32_t hcall_notify_ioreq_finish(__unused struct acrn_vcpu *vcpu, struct acrn_vm *target_vm,
		__unused uint64_t param1, uint64_t param2)
{
	struct acrn_vcpu *target_vcpu;
	int32_t ret = -1;
	uint16_t vcpu_id = (uint16_t)param2;

	/* make sure we have set req_buf */
	if (is_severity_pass(target_vm->vm_id) &&
	    (!is_poweroff_vm(target_vm)) && (target_vm->sw.io_shared_page != NULL)) {
		dev_dbg(DBG_LEVEL_HYCALL, "[%d] NOTIFY_FINISH for vcpu %d",
			target_vm->vm_id, vcpu_id);

		if (vcpu_id >= target_vm->hw.created_vcpus) {
			pr_err("%s, failed to get VCPU %d context from VM %d\n",
				__func__, vcpu_id, target_vm->vm_id);
		} else {
			target_vcpu = vcpu_from_vid(target_vm, vcpu_id);
			if (!target_vcpu->vm->sw.is_polling_ioreq) {
				signal_event(&target_vcpu->events[VCPU_EVENT_IOREQ]);
			}
			ret = 0;
		}
	}

	return ret;
}

/**
 * @brief Add a memory region to a virtual machine (VM).
 *
 * This function sets up an EPT mapping for a specified memory region in a target VM. It translates
 * the GPA from the Service VM to a HPA and configures the appropriate access rights and memory types
 * for the region. The function then creates the GPA to HPA mapping in the target VM's EPT.
 *
 * @param[in] vm The VM from which the memory region is being added.
 * @param[inout] target_vm The target VM to which the memory region is being added.
 * @param[in] region The memory region to be added.
 * @param[in] pml4_page The PML4 page used for the EPT mapping.
 *
 * @return None.
 *
 * @pre is_service_vm(vm) == true
 * @pre gpa2hpa(vm, region->service_vm_gpa) != INVALID_HPA
 */
static void add_vm_memory_region(struct acrn_vm *vm, struct acrn_vm *target_vm,
				const struct vm_memory_region *region,uint64_t *pml4_page)
{
	uint64_t prot = 0UL;
	uint64_t hpa = gpa2hpa(vm, region->service_vm_gpa);

	/* access right */
	if ((region->prot & MEM_ACCESS_READ) != 0U) {
		prot |= EPT_RD;
	}
	if ((region->prot & MEM_ACCESS_WRITE) != 0U) {
		prot |= EPT_WR;
	}
	if ((region->prot & MEM_ACCESS_EXEC) != 0U) {
		prot |= EPT_EXE;
	}

	/* memory type */
	if ((region->prot & MEM_TYPE_WB) != 0U) {
		prot |= EPT_WB;
	} else if ((region->prot & MEM_TYPE_WT) != 0U) {
		prot |= EPT_WT;
	} else if ((region->prot & MEM_TYPE_WC) != 0U) {
		prot |= EPT_WC;
	} else if ((region->prot & MEM_TYPE_WP) != 0U) {
		prot |= EPT_WP;
	} else {
		prot |= EPT_UNCACHED;
	}

	/* create gpa to hpa EPT mapping */
	ept_add_mr(target_vm, pml4_page, hpa, region->gpa, region->size, prot);
}

/**
 * @brief Set a memory region for a virtual machine (VM).
 *
 * This function handles the addition or removal of a memory region for a target VM by modifying the
 * EPT mappings. The function supports both adding and removing memory regions based on the
 * specified type in the `region` parameter.
 *
 * The function performs the following steps:
 * 1. Checks if the region size is page-aligned.
 *    - Returns -EINVAL if the size is not aligned to the page size.
 * 2. Retrieves the PML4 page for the target VM's non-world EPT.
 * 3. Determines the operation type (add or remove) based on the region type.
 *    - If the type is `MR_ADD`, it checks if the GPA range is valid for the Service VM.
 *      - Adds the memory region and returns 0 if valid.
 *      - Returns -EINVAL if the GPA range is invalid.
 *    - If the type is not `MR_ADD`, it checks if the GPA range is valid for the target VM.
 *      - Removes the memory region and returns 0 if valid.
 *      - Returns -EINVAL if the GPA range is invalid.
 *
 * @param[inout] vm The VM from which the memory region is being set.
 * @param[inout] target_vm The target VM for which the memory region is being set.
 * @param[in] region The memory region to be set.
 *
 * @return An int32_t value indicating whether the memory region is set successfully.
 *
 * @retval 0 On successful setting of the memory regions.
 * @retval -EINVAL If the memory region size is not aligned to the page size or if the memory region is invalid.
 *
 * @pre is_service_vm(vm) == true
 */
static int32_t set_vm_memory_region(struct acrn_vm *vm,
	struct acrn_vm *target_vm, const struct vm_memory_region *region)
{
	uint64_t *pml4_page;
	int32_t ret = -EINVAL;

	if ((region->size & (PAGE_SIZE - 1UL)) == 0UL) {
		pml4_page = (uint64_t *)target_vm->arch_vm.nworld_eptp;
		if (region->type == MR_ADD) {
			/* if the GPA range is Service VM valid GPA or not */
			if (ept_is_valid_mr(vm, region->service_vm_gpa, region->size)) {
				/* FIXME: how to filter the alias mapping ? */
				add_vm_memory_region(vm, target_vm, region, pml4_page);
				ret = 0;
			}
		} else {
			if (ept_is_valid_mr(target_vm, region->gpa, region->size)) {
				ept_del_mr(target_vm, pml4_page, region->gpa, region->size);
				ret = 0;
			}
		}
	}

	dev_dbg((ret == 0) ? DBG_LEVEL_HYCALL : LOG_ERROR,
			"[vm%d] type=%d gpa=0x%lx service_vm_gpa=0x%lx sz=0x%lx",
			target_vm->vm_id, region->type, region->gpa,
			region->service_vm_gpa, region->size);
	return ret;
}

/**
 * @brief Handle hypercall to set multiple memory regions for a virtual machine (VM).
 *
 * This function handles the hypercall of HC_VM_SET_MEMORY_REGIONS, which is used to set up
 * EPT memory mapping for multiple input regions.
 *
 * The function performs the following steps:
 * 1. Copies the `set_regions` structure from the guest's physical address specified by `param1`.
 *    - Returns -1 if the copy operation fails.
 * 2. Validates the target VM is not powered off and either passes the severity check or is not currently running.
 *    - Returns -1 if the VM does not meet the required conditions.
 * 3. Iterates over each memory region specified in the `set_regions` structure.
 *    - For each region, copies the memory region entry from the guest's memory.
 *      - Returns -1 if the copy operation fails.
 *    - Set a memory region for a virtual machine (VM).
 *      - Returns -EINVAL if setting the memory region fails.
 *
 * @param[inout] vcpu Pointer to vCPU that initiates the hypercall.
 * @param[inout] target_vm Pointer to target VM data structure.
 * @param[in] param1 Guest physical address. This gpa points to struct set_memmaps.
 * @param[in] param2 Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 *
 * @return An int32_t value indicating whether the hypercall is success.
 *
 * @retval 0 On successful setting of all specified memory regions.
 * @retval !=0 On failure, indicating that one or more memory regions could not be set.
 *
 * @pre is_service_vm(vcpu->vm) == true
 */
int32_t hcall_set_vm_memory_regions(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm,
		uint64_t param1, __unused uint64_t param2)
{
	struct acrn_vm *vm = vcpu->vm;
	struct set_regions regions;
	struct vm_memory_region mr;
	uint32_t idx;
	int32_t ret = -1;

	if (copy_from_gpa(vm, &regions, param1, sizeof(regions)) == 0) {

		if (!is_poweroff_vm(target_vm) &&
		    (is_severity_pass(target_vm->vm_id) || (target_vm->state != VM_RUNNING))) {
			idx = 0U;
			while (idx < regions.mr_num) {
				if (copy_from_gpa(vm, &mr, regions.regions_gpa + idx * sizeof(mr), sizeof(mr)) != 0) {
					ret = -1;
					pr_err("%s: Copy mr entry fail from vm\n", __func__);
					break;
				}

				ret = set_vm_memory_region(vm, target_vm, &mr);
				if (ret < 0) {
					break;
				}
				idx++;
			}
		} else {
			pr_err("%p %s:target_vm is invalid or Targeting to service vm", target_vm, __func__);
		}
	}

	return ret;
}

/**
 * @brief Set or clear the write protection for a specific page in a virtual machine (VM).
 *
 * This function sets or clears the write protection for a specific page in a VM based on the provided parameters.
 *
 * The function performs the following steps:
 * 1. Checks if the VM passes the severity check.
 *    - Returns -EINVAL if the VM's severity level does not permit this operation.
 * 2. Validates that the GPA is page-aligned and a valid memory region.
 *    - Returns -EINVAL if the GPA is invalid or not aligned.
 * 3. Translates the GPA to a host physical address (HPA) and checks for validity.
 *    - Returns -EINVAL if the HPA is invalid.
 * 4. Modifies the memory region's protection settings using `ept_modify_mr`.
 *    - Sets the return value to 0 to indicate success.
 *
 * @param[inout] vm The VM for which the write protection is to be set or cleared.
 * @param[in] wp The write protection data containing the GPA and the set/clear flag.
 *
 * @return An int32_t value indicating whether the page modification is successful.
 *
 * @retval 0 On successful modification of the page protection.
 * @retval -EINVAL On error, indicating that the operation failed due to invalid parameters or conditions.
 *
 * @pre is_service_vm(vm) == true
 */
static int32_t write_protect_page(struct acrn_vm *vm, const struct wp_data *wp)
{
	uint64_t prot_set;
	uint64_t prot_clr;
	int32_t ret = -EINVAL;

	if (is_severity_pass(vm->vm_id)) {
		if ((!mem_aligned_check(wp->gpa, PAGE_SIZE)) ||
				(!ept_is_valid_mr(vm, wp->gpa, PAGE_SIZE))) {
			pr_err("%s,vm[%hu] gpa 0x%lx,GPA is invalid or not page size aligned.",
					__func__, vm->vm_id, wp->gpa);
		} else {
			if (gpa2hpa(vm, wp->gpa) == INVALID_HPA) {
				pr_err("%s,vm[%hu] gpa 0x%lx,GPA is unmapping.",
						__func__, vm->vm_id, wp->gpa);
			} else {
				dev_dbg(DBG_LEVEL_HYCALL, "[vm%d] gpa=0x%lx hpa=0x%lx",
						vm->vm_id, wp->gpa, gpa2hpa(vm, wp->gpa));

				prot_set = (wp->set != 0U) ? 0UL : EPT_WR;
				prot_clr = (wp->set != 0U) ? EPT_WR : 0UL;
				ept_modify_mr(vm, (uint64_t *)vm->arch_vm.nworld_eptp,
						wp->gpa, PAGE_SIZE, prot_set, prot_clr);
				ret = 0;
			}
		}
	}

	return ret;
}

/**
 * @brief Handle hypercall to set or clear the write protection for a specific page in a virtual machine (VM).
 *
 * This function processes the hypercall `HC_VM_WRITE_PROTECT_PAGE`, which applies write protection
 * to a specified page within a target VM.
 *
 * The function performs the following steps:
 * 1. Check if the target VM is powered off.
 *    - Returns -1 if the target VM is powered off.
 * 2. Copy the write-protect data from the calling VM's GPA space to the local structure.
 *    - Returns -1 if the copy operation fails.
 * 3. Set or clear the write protection for a specific page in a virtual machine (VM).
 *    - Returns 0 on success or -EINVAL if the write protection fails.
 *
 * @param[inout] vcpu Pointer to vCPU that initiates the hypercall.
 * @param[inout] target_vm Pointer to target VM data structure.
 * @param[in] param1 Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[in] param2 Guest physical address. This gpa points to struct wp_data.
 *
 * @return An int32_t value indicating whether the hypercall is success.
 *
 * @retval 0 On successful modification of the page protection.
 * @retval <0 An error occurred during the operation.
 *
 * @pre is_service_vm(vcpu->vm) == true
 */
int32_t hcall_write_protect_page(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm,
		__unused uint64_t param1, uint64_t param2)
{
	struct acrn_vm *vm = vcpu->vm;
	int32_t ret = -1;
	uint64_t wp_gpa = param2;

	if (!is_poweroff_vm(target_vm)) {
		struct wp_data wp;

		if (copy_from_gpa(vm, &wp, wp_gpa, sizeof(wp)) == 0) {
			ret = write_protect_page(target_vm, &wp);
		}
	} else {
		pr_err("%p %s: target_vm is invalid", target_vm, __func__);
	}

	return ret;
}

/**
 * @brief Handle hypercall to assign a PCI device to a virtual machine (VM).
 *
 * This function handles the hypercall of HC_ASSIGN_PCIDEV, which is used to assign one virtual PCI device to a VM.
 * It assigns a PCI device by copying the PCI device information from the guest physical address and then
 * calling `vpci_assign_pcidev` to perform the assignment.
 *
 * On success, the ACRN hypervisor returns 0. The function returns an error code in the following scenarios:
 * 1. Failure to copy the PCI device information from the guest's physical address specified by `param2`.
 *    The error code is -EINVAL.
 * 2. Failure in assigning the PCI device to the target VM. The error code is determined by `vpci_assign_pcidev`.
 *
 * @param[inout] vcpu Pointer to vCPU that initiates the hypercall.
 * @param[inout] target_vm Pointer to target VM data structure.
 * @param[in] param1 Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[in] param2 Guest physical address. This gpa points to data structure of
 *              acrn_pcidev including assign PCI device info.
 *
 * @return An int32_t value indicating whether the hypercall is success.
 *
 * @retval 0 On successful assignment of the PCI device.
 * @retval <0 On failure to assign the PCI device.
 *
 * @pre is_service_vm(vcpu->vm) == true
 */
int32_t hcall_assign_pcidev(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm,
		__unused uint64_t param1, uint64_t param2)
{
	struct acrn_vm *vm = vcpu->vm;
	int32_t ret = -EINVAL;
	struct acrn_pcidev pcidev;

	if (copy_from_gpa(vm, &pcidev, param2, sizeof(pcidev)) == 0) {
		ret = vpci_assign_pcidev(target_vm, &pcidev);
	}

	return ret;
}

/**
 * @brief Handle hypercall to deassign a PCI device from a virtual machine (VM).
 *
 * This function handles the hypercall HC_DEASSIGN_PCIDEV, which is used to deassign one virtual PCI device from a VM.
 * It deassigns a PCI device by copying the PCI device information from the guest physical address and then
 * calling `vpci_deassign_pcidev` to perform the deassignment.
 *
 * On success, the function returns 0. The function returns an error code in the following scenarios:
 * 1. Failure to copy the PCI device information from the guest's physical address specified by `param2`.
 *    The error code is -EINVAL.
 * 2. Failure in deassigning the PCI device from the target VM. The error code is determined by `vpci_deassign_pcidev`.
 *
 * @param[inout] vcpu Pointer to vCPU that initiates the hypercall.
 * @param[inout] target_vm Pointer to target VM data structure.
 * @param[in] param1 Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[in] param2 Guest physical address. This gpa points to data structure of
 *              acrn_pcidev including deassign PCI device info.
 *
 * @return An int32_t value indicating whether the hypercall is success.
 *
 * @retval 0 On successful deassignment of the PCI device.
 * @retval <0 On failure, indicating that the PCI device could not be deassigned.
 *
 * @pre is_service_vm(vcpu->vm) == true
 */
int32_t hcall_deassign_pcidev(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm,
		__unused uint64_t param1, uint64_t param2)
{
	struct acrn_vm *vm = vcpu->vm;
	int32_t ret = -EINVAL;
	struct acrn_pcidev pcidev;

	if (copy_from_gpa(vm, &pcidev, param2, sizeof(pcidev)) == 0) {
		ret = vpci_deassign_pcidev(target_vm, &pcidev);
	}

	return ret;
}

/**
 * @brief Handle hypercall to assign an MMIO device to a virtual machine (VM).
 *
 * This function processes the hypercall `HC_ASSIGN_MMIODEV`, which assigns an MMIO device to the
 * specified target VM.
 *
 * The function performs the following steps:
 * 1. Check if the target VM is in the created state.
 *    - Returns -EINVAL if the target VM is not in the created state.
 * 2. Copy the MMIO device data from the calling VM's GPA space to a local structure.
 *    - Returns -EINVAL if the copy operation fails.
 * 3. Deassign the MMIO device from the current VM(Service VM). The MMIO device initially belongs to the Service VM,
 *    which is responsible for managing hardware resources. By deassigning the device from the Service VM,
 *    we ensure exclusive access to the device for the target VM, preventing resource conflicts.
 *    - Returns a negative value if the deassignment fails.
 * 4. Assign the MMIO device to the target VM.
 *    - Returns 0 on success or a negative value if the assignment fails.
 *
 * @param[inout] vcpu Pointer to vCPU that initiates the hypercall.
 * @param[inout] target_vm Pointer to target VM data structure.
 * @param[in] param1 Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[in] param2 Guest physical address. This gpa points to data structure of
 *              acrn_mmiodev including assign MMIO device info.
 *
 * @return An int32_t value indicating whether the hypercall is success.
 *
 * @retval 0 On successful assignment of the MMIO device.
 * @retval <0 On failure, indicating that the MMIO device could not be assigned.
 *
 * @pre is_service_vm(vcpu->vm) == true
 */
int32_t hcall_assign_mmiodev(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm,
		__unused uint64_t param1, uint64_t param2)
{
	struct acrn_vm *vm = vcpu->vm;
	int32_t ret = -EINVAL;
	struct acrn_mmiodev mmiodev;

	/* We should only assign a device to a post-launched VM at creation time for safety,
	 * not at runtime or in other cases.
	 */
	if (is_created_vm(target_vm)) {
		if (copy_from_gpa(vm, &mmiodev, param2, sizeof(mmiodev)) == 0) {
			ret = deassign_mmio_dev(vm, &mmiodev);
			if (ret == 0) {
				ret = assign_mmio_dev(target_vm, &mmiodev);
			}
		}
	} else {
		pr_err("vm[%d] %s failed!\n",target_vm->vm_id,  __func__);
	}

	return ret;
}

/**
 * @brief Handle hypercall to deassign an MMIO device from a virtual machine (VM).
 *
 * This function processes the hypercall `HC_DEASSIGN_MMIODEV`, which removes an MMIO device from
 * the specified target VM.
 *
 * The function performs the following steps:
 * 1. Check if the target VM is in the "paused" or "created" state.
 *    - Returns -EINVAL if the target VM is not in one of these states.
 * 2. Copy the MMIO device data from the calling VM's GPA space to a local structure.
 *    - Returns -EINVAL if the copy operation fails.
 * 3. Deassign the MMIO device from the target VM.
 *    - Returns a negative value if the deassignment fails.
 * 4. Assign the MMIO device back to the Service VM (the calling VM).
 *    This step is necessary to return the device to the Service VM for management or reassignment.
 *    - Returns 0 on success or a negative value if the assignment fails.
 *
 * @param[inout] vcpu Pointer to vCPU that initiates the hypercall.
 * @param[inout] target_vm Pointer to target VM data structure.
 * @param[in] param1 Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[in] param2 Guest physical address. This gpa points to data structure of
 *              acrn_mmiodev including deassign MMIO device info.
 *
 * @return An int32_t value indicating whether the hypercall is success.
 *
 * @retval 0 On successful deassignment of the MMIO device.
 * @retval <0 If the MMIO device cannot be deassigned due to invalid parameters or VM state.
 *
 * @pre is_service_vm(vcpu->vm) == true
 */
int32_t hcall_deassign_mmiodev(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm,
		__unused uint64_t param1, uint64_t param2)
{
	struct acrn_vm *vm = vcpu->vm;
	int32_t ret = -EINVAL;
	struct acrn_mmiodev mmiodev;

	/* We should only de-assign a device from a post-launched VM at creating/shutdown/reset time */
	if ((is_paused_vm(target_vm) || is_created_vm(target_vm))) {
		if (copy_from_gpa(vm, &mmiodev, param2, sizeof(mmiodev)) == 0) {
			ret = deassign_mmio_dev(target_vm, &mmiodev);
			if (ret == 0) {
				ret = assign_mmio_dev(vm, &mmiodev);
			}
		}
	} else {
		pr_err("vm[%d] %s failed!\n",target_vm->vm_id,  __func__);
	}

	return ret;
}

/**
 * @brief Handle hypercall to set interrupt mapping information for a pass-through device in a virtual machine (VM).
 *
 * This function processes the hypercall `HC_SET_PTDEV_INTR_INFO`, which sets up interrupt mapping information for a
 * pass-through device. This function sets the interrupt information for a pass-through device in a VM by copying the
 * interrupt information from the guest physical address and configuring the interrupt remapping.
 *
 * The function performs the following steps:
 * 1. Copies the interrupt information from the guest's physical address specified by `param2`.
 *    - Returns -1 if the copy operation fails.
 * 2. Checks if the interrupt type is `IRQ_INTX`.
 *    - Returns -1 if the interrupt type is invalid.
 * 3. Finds the virtual PCI device (vdev) associated with the specified virtual BDF.
 *    - Returns -1 if the `vdev` is NULL or the BDFs do not match.
 * 4. Checks if the interrupt is not associated with a PIC pin, the virtual pin is within the
 *    valid range for the target VM, and the physical pin is valid.
 *    - Returns -1 if any of these conditions are not met.
 * 5. Removes existing interrupt remapping for the physical pin.
 * 6. Adds new interrupt remapping for the virtual and physical pins.
 *    - Returns the result of `ptirq_add_intx_remapping`.
 *
 * @param[inout] vcpu Pointer to vCPU that initiates the hypercall.
 * @param[inout] target_vm Pointer to target VM data structure.
 * @param[in] param1 Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[in] param2 Guest physical address. This gpa points to data structure of
 *              hc_ptdev_irq including intr remapping info.
 *
 * @return An int32_t value indicating whether the hypercall is success.
 *
 * @retval 0 On successful setting of the interrupt information for the pass-through device.
 * @retval <0 On error, indicating that the operation failed.
 *
 * @pre is_service_vm(vcpu->vm) == true
 */
int32_t hcall_set_ptdev_intr_info(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm,
	       __unused uint64_t param1, uint64_t param2)
{
	struct acrn_vm *vm = vcpu->vm;
	int32_t ret = -1;

		struct hc_ptdev_irq irq;

		if (copy_from_gpa(vm, &irq, param2, sizeof(irq)) == 0) {
			if (irq.type == IRQ_INTX) {
				struct pci_vdev *vdev;
				union pci_bdf bdf = {.value = irq.virt_bdf};
				struct acrn_vpci *vpci = &target_vm->vpci;

				spinlock_obtain(&vpci->lock);
				vdev = pci_find_vdev(vpci, bdf);
				spinlock_release(&vpci->lock);
				/*
				 * TODO: Change the hc_ptdev_irq structure member names
				 * virt_pin to virt_gsi
				 * phys_pin to phys_gsi
				 */
				if ((vdev != NULL) && (vdev->pdev->bdf.value == irq.phys_bdf)) {
					if ((!irq.intx.pic_pin) &&
							(irq.intx.virt_pin < get_vm_gsicount(target_vm)) &&
							is_gsi_valid(irq.intx.phys_pin)) {
						ptirq_remove_intx_remapping(get_service_vm(), irq.intx.phys_pin, true);
						ret = ptirq_add_intx_remapping(target_vm, irq.intx.virt_pin,
								irq.intx.phys_pin);
					} else {
						pr_err("%s: Invalid phys pin or virt pin\n", __func__);
						pr_err("%s: Invalid %s virt pin %d, %s gsi\n", __func__,
							irq.intx.pic_pin ? "PIC" : "IOAPIC", irq.intx.virt_pin,
							is_gsi_valid(irq.intx.phys_pin) ? "valid" : "invalid");
					}
				}
			} else {
				pr_err("%s: Invalid irq type: %u\n", __func__, irq.type);
			}
		}
	return ret;
}

/**
 * @brief Handle hypercall to reset the interrupt information for a pass-through device in a virtual machine (VM).
 *
 * The hypercall HC_RESET_PTDEV_INTR_INFO is used to clear interrupt mapping information of a pass-through device.
 * This function resets the interrupt information for a pass-through device in a VM by copying the interrupt
 * information from the guest physical address and removing the interrupt remapping.
 *
 * The function performs the following steps:
 * 1. Checks if the target VM is in a created or paused state.
 *    - Returns -1 if the VM is not in a valid state.
 * 2. Copies the interrupt information from the guest's physical address specified by `param2`.
 *    - Returns -1 if the copy operation fails.
 * 3. Checks if the interrupt type is `IRQ_INTX`.
 *    - Returns -1 if the interrupt type is invalid.
 * 4. Finds the virtual PCI device (vdev) associated with the specified virtual BDF.
 *    - Returns -1 if the `vdev` is NULL or the BDFs do not match.
 * 5. Checks if the interrupt is not associated with a PIC pin, the virtual pin is within the
 *    valid range for the target VM, and the physical pin is valid.
 *    - Returns -1 if any of these conditions are not met.
 * 6. Removes the interrupt remapping for the virtual pin.
 *    - Returns 0 to indicate the remapping is successfully removed.
 *
 * @param[inout] vcpu Pointer to vCPU that initiates the hypercall.
 * @param[inout] target_vm Pointer to target VM data structure.
 * @param[in] param1 Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[in] param2 Guest physical address. This gpa points to data structure of
 *              hc_ptdev_irq including intr remapping info.
 *
 * @return An int32_t value indicating whether the hypercall is success.
 *
 * @retval 0 On successful reset of the interrupt information for the pass-through device.
 * @retval -1 On failure to reset the interrupt information for the pass-through device.
 *
 * @pre is_service_vm(vcpu->vm) == true
 */
int32_t hcall_reset_ptdev_intr_info(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm,
		__unused uint64_t param1, uint64_t param2)
{
	struct acrn_vm *vm = vcpu->vm;
	int32_t ret = -1;

	if (is_created_vm(target_vm) || is_paused_vm(target_vm)) {
		struct hc_ptdev_irq irq;

		if (copy_from_gpa(vm, &irq, param2, sizeof(irq)) == 0) {
			if (irq.type == IRQ_INTX) {
				struct pci_vdev *vdev;
				union pci_bdf bdf = {.value = irq.virt_bdf};
				struct acrn_vpci *vpci = &target_vm->vpci;

				spinlock_obtain(&vpci->lock);
				vdev = pci_find_vdev(vpci, bdf);
				spinlock_release(&vpci->lock);
				/*
				 * TODO: Change the hc_ptdev_irq structure member names
				 * virt_pin to virt_gsi
				 * phys_pin to phys_gsi
				 */
				if ((vdev != NULL) && (vdev->pdev->bdf.value == irq.phys_bdf)) {
					if ((!irq.intx.pic_pin) && (irq.intx.virt_pin < get_vm_gsicount(target_vm))) {
						ptirq_remove_intx_remapping(target_vm, irq.intx.virt_pin, false);
						ret = 0;
					} else {
						pr_err("%s: Invalid %s virt pin %d\n", __func__,
							irq.intx.pic_pin ? "PIC" : "IOAPIC", irq.intx.virt_pin);
					}
				}
			} else {
				pr_err("%s: Invalid irq type: %u\n", __func__, irq.type);
			}
		}
	}

	return ret;
}

/**
 * @brief Check if the power state (P-state) is pass-through for a virtual machine (VM).
 *
 * This function checks if the P-state is pass-through for a VM. Currently, the CPU frequency
 * is managed by the hypervisor, so no pass-through is allowed for all VMs.
 *
 * @param[in] vm The VM to check (unused in this function).
 *
 * @return A boolean value indicating that P-state pass-through is allowed.
 *
 * @retval false Always return false.
 */
static bool is_pt_pstate(__unused const struct acrn_vm *vm)
{
	/* Currently VM's CPU frequency is managed in hypervisor. So no pass through for all VMs. */
	return false;
}

/**
 * @brief Handle hypercall to get the CPU power management (PM) state for a virtual machine (VM).
 *
 * The hypercall HC_PM_GET_CPU_STATE is used to get the vCPU power information and write the
 * information into guest memory. This function can get various PM state information
 * for a specified target VM. The information includes P-state and C-state data, which are
 * essential for managing power consumption and performance in virtualized environments.
 *
 * The function performs the following steps:
 * 1. Verify the target VM is in a "created" or "paused" state.
 *    - Return -1 if the VM is not in a valid state.
 * 2. Execute the command specified by `param1` to retrieve PM information:
 *    - ACRN_PMCMD_GET_PX_CNT: Determine the number of P-states available.
 *      - If the VM supports virtual Hardware P-states (vHWP), set `px_cnt` to 0.
 *      - If P-states are hidden or unavailable, return -1.
 *      - Otherwise, retrieve the P-state count.
 *    - ACRN_PMCMD_GET_PX_DATA: Retrieve data for a specific P-state.
 *      - Check if P-states are available; if not, return -1.
 *      - Validate the state number; if invalid, return -1.
 *      - Retrieve the P-state data.
 *    - ACRN_PMCMD_GET_CX_CNT: Retrieve the number of C-states.
 *      - Directly obtain the C-state count.
 *    - ACRN_PMCMD_GET_CX_DATA: Retrieve data for a specific C-state.
 *      - Check if C-states are available; if not, return -1.
 *      - Validate the state index; if invalid, return -1.
 *      - Retrieve the C-state data.
 * 3. If the data is successfully retrieved, use `copy_to_gpa` to transfer the information to the guest's memory.
 *    - Return the result of the `copy_to_gpa` operation.
 *
 * @param[inout] vcpu Pointer to vCPU that initiates the hypercall.
 * @param[inout] target_vm Pointer to target VM data structure.
 * @param[in] param1 cmd to show get which VCPU power state data.
 * @param[in] param2 VCPU power state data.
 *
 * @return An int32_t value indicating whether the hypercall is success.
 *
 * @retval 0 On successful retrieval and copying of the requested information.
 * @retval <0 If the operation failed.
 *
 * @pre is_service_vm(vcpu->vm) == true
 */
int32_t hcall_get_cpu_pm_state(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, uint64_t param1, uint64_t param2)
{
	struct acrn_vm *vm = vcpu->vm;
	int32_t ret = -1;
	uint64_t cmd = param1;

	if (is_created_vm(target_vm) || is_paused_vm(target_vm)) {
		switch (cmd & PMCMD_TYPE_MASK) {
		case ACRN_PMCMD_GET_PX_CNT: {
			uint8_t px_cnt;
			/*
			 * If the VM supports vHWP, then the guest is having continuous p-state.
			 * Thus it doesn't have a specific px_cnt. The hypercall returns success and px_cnt = 0.
			 * If the VM's p-state is hidden or hv doesn't have its p-state info, the hypercall returns -1.
			 */
			if (vm_cpu_feature_enabled(target_vm, GUEST_CPU_FEATURE_VHWP)) {
				px_cnt = 0U;
			} else if (!is_pt_pstate(target_vm)) {
				break;
			} else if (target_vm->pm.px_cnt == 0U) {
				break;
			} else {
				px_cnt = target_vm->pm.px_cnt;
			}
			ret = copy_to_gpa(vm, &px_cnt, param2, sizeof(px_cnt));
			break;
		}
		case ACRN_PMCMD_GET_PX_DATA: {
			uint8_t pn;
			struct acrn_pstate_data *px_data;

			if (!is_pt_pstate(target_vm)) {
				break;
			}

			/* For now we put px data as per-vm,
			 * If it is stored as per-cpu in the future,
			 * we need to check PMCMD_VCPUID_MASK in cmd.
			 */
			if (target_vm->pm.px_cnt == 0U) {
				break;
			}

			pn = (uint8_t)((cmd & PMCMD_STATE_NUM_MASK) >> PMCMD_STATE_NUM_SHIFT);
			if (pn >= target_vm->pm.px_cnt) {
				break;
			}

			px_data = target_vm->pm.px_data + pn;
			ret = copy_to_gpa(vm, px_data, param2, sizeof(struct acrn_pstate_data));
			break;
		}
		case ACRN_PMCMD_GET_CX_CNT: {
			ret = copy_to_gpa(vm, &(target_vm->pm.cx_cnt), param2, sizeof(target_vm->pm.cx_cnt));
			break;
		}
		case ACRN_PMCMD_GET_CX_DATA: {
			uint8_t cx_idx;
			struct acrn_cstate_data *cx_data;

			if (target_vm->pm.cx_cnt == 0U) {
				break;
			}

			cx_idx = (uint8_t)
				((cmd & PMCMD_STATE_NUM_MASK) >> PMCMD_STATE_NUM_SHIFT);
			if ((cx_idx == 0U) || (cx_idx > target_vm->pm.cx_cnt)) {
				break;
			}

			cx_data = target_vm->pm.cx_data + cx_idx;
			ret = copy_to_gpa(vm, cx_data, param2, sizeof(struct acrn_cstate_data));
			break;
		}
		default:
			/* invalid command */
			break;
		}
	}

	return ret;
}

/**
 * @brief Handle hypercall to monitor and manage interrupts for a virtual machine (VM).
 *
 * This function processes the HC_VM_INTR_MONITOR hypercall to either retrieve interrupt data or set a delay
 * for interrupt injection for a specified target VM.
 *
 * The function performs the following steps:
 * 1. Checks if the target VM is powered off.
 *    - Returns -EINVAL if the VM is powered off, as interrupt monitoring is not applicable.
 * 2. Converts the GPA specified by `param2` to a HPA.
 *    - Returns -EINVAL if the conversion fails.
 * 3. Converts the HPA to a HVA and casts it to a pointer to `struct acrn_intr_monitor`.
 * 4. Validates the interrupt monitor header by checking `intr_hdr->buf_cnt`.
 *    - Returns -EINVAL if `buf_cnt` exceeds the maximum allowed value.
 * 5. Processes the command specified in `intr_hdr->cmd`:
 *    - INTR_CMD_GET_DATA: Retrieves interrupt data for the target VM.
 *      - Retrieves interrupt data and updates `buf_cnt`.
 *      - Returns 0 to indicate successful data retrieval.
 *    - INTR_CMD_DELAY_INT: Sets a delay for interrupt injection based on the specified delay time.
 *      - Sets a delay for interrupt injection based on the specified delay time.
 *      - Returns 0 to indicate successful delay setting.
 *    - Default: Returns -EINVAL for unrecognized commands.
 *
 * @param[inout] vcpu Pointer to vCPU that initiates the hypercall.
 * @param[inout] target_vm Pointer to target VM data structure.
 * @param[in] param1 Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[in] param2 Guest physical address. This gpa points to data structure of acrn_intr_monitor.
 *
 * @return An int32_t value indicating whether the hypercall is success.
 *
 * @retval 0 On successful execution of the interrupt monitor command.
 * @retval -EINVAL If the operation failed due to invalid input or state.
 *
 * @pre is_service_vm(vcpu->vm) == true
 */
int32_t hcall_vm_intr_monitor(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm,
		__unused uint64_t param1, uint64_t param2)
{
	struct acrn_vm *vm = vcpu->vm;
	int32_t status = -EINVAL;
	struct acrn_intr_monitor *intr_hdr;
	uint64_t hpa;

	if (!is_poweroff_vm(target_vm)) {
		/* the param for this hypercall is page aligned */
		hpa = gpa2hpa(vm, param2);
		if (hpa != INVALID_HPA) {
			intr_hdr = (struct acrn_intr_monitor *)hpa2hva(hpa);
			stac();
			if (intr_hdr->buf_cnt <= (MAX_PTDEV_NUM * 2U)) {
				status = 0;

				switch (intr_hdr->cmd) {
				case INTR_CMD_GET_DATA:
					intr_hdr->buf_cnt = ptirq_get_intr_data(target_vm,
						intr_hdr->buffer, intr_hdr->buf_cnt);
					break;

				case INTR_CMD_DELAY_INT:
					/* buffer[0] is the delay time (in MS), if 0 to cancel delay */
					target_vm->intr_inject_delay_delta =
						intr_hdr->buffer[0] * TICKS_PER_MS;
					break;

				default:
					/* if cmd wrong it goes here should not happen */
					status = -EINVAL;
					break;
				}
			}
			clac();
		}
	}

	return status;
}

/**
 * @brief Handle hypercall to set a callback vector for a virtual machine (VM).
 *
 * This function handles the hypercall of HC_SET_CALLBACK_VECTOR to set the callback vector for a hypercall.
 * This function can switch the notifier vector. If this function is not called, the hypervisor will use the
 * default notifier vector to notify the Service VM kernel.
 *
 * The function performs the following steps:
 * 1. Validate the provided vector (`param1`) to ensure it is within the allowed range.
 *    - Return -EINVAL If the vector is invalid.
 * 2. If the vector is valid, set it as the hypervisor's notification vector using `set_hsm_notification_vector`.
 *    - Return 0 to indicate success.
 *
 * @param[in] vcpu  Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[in] target_vm Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[in] param1 the expected notifier vector from guest.
 * @param[in] param2 Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 *
 * @return An int32_t value indicating whether the hypercall is success.
 *
 * @retval 0 On successful setting of the callback vector.
 * @retval -EINVAL If the vector is outside the valid range.
 *
 * @pre is_service_vm(vcpu->vm) == true
 */
int32_t hcall_set_callback_vector(__unused struct acrn_vcpu *vcpu, __unused struct acrn_vm *target_vm,
		uint64_t param1, __unused uint64_t param2)
{
	int32_t ret;

	if ((param1 > NR_MAX_VECTOR) || (param1 < VECTOR_DYNAMIC_START)) {
		pr_err("%s: Invalid passed vector\n", __func__);
		ret = -EINVAL;
	} else {
		set_hsm_notification_vector((uint32_t)param1);
		ret = 0;
	}

	return ret;
}

/**
 * @brief Find the emulation device operations for a given virtual device.
 *
 * This function searches through the emul_dev_ops_tbl array to find an entry
 * that matches the device ID of the provided acrn_vdev structure. If a match
 * is found, a pointer to the corresponding emul_dev_ops structure is returned.
 * If no match is found, NULL is returned.
 *
 * @return A pointer to indicate if the emul_dev_ops structure pointer is found.
 *
 * @retval non-NULL The emul_dev_ops is found.
 * @retval NULL The emul_dev_ops is not found.
 *
 * @pre dev != NULL
 */
static struct emul_dev_ops *find_emul_dev_ops(struct acrn_vdev *dev)
{
	struct emul_dev_ops *op = NULL;
	uint32_t i;

	for (i = 0U; i < ARRAY_SIZE(emul_dev_ops_tbl); i++) {
		if (emul_dev_ops_tbl[i].dev_id == dev->id.value) {
			op = &emul_dev_ops_tbl[i];
			break;
		}
	}
	return op;
}

/**
 * @brief Hypercall to add a virtual device to a target VM.
 *
 * This function handles the hypercall of HC_ADD_VDEV to add a virtual device to a specified
 * target VM.
 *
  * The function performs the following steps:
 * 1. Check if the target VM is in the "created" or "paused" state.
 *    - Return -EINVAL if the VM is not in a valid state.
 * 2. Copy the virtual device configuration from the GPA to a local structure.
 *    - Return -EINVAL if the copy operation fails.
 * 3. Attempt to find the emulated device operations using the device information.
 *    - Return -EINVAL if the operations cannot be found.
 * 4. Check if the `create` callback function is available in the emulated device operations.
 *    - Return -EINVAL if the `create` function is not available, such as when trying to add an ivshmem device
 *      without CONFIG_IVSHMEM_ENABLED in the ACRN hypervisor.
 * 5. Invoke the `create` callback function to add the device to the VM.
 *    - Return the result of the `create` operation.
 *
 * @param[inout] vcpu Pointer to vCPU that initiates the hypercall.
 * @param[inout] target_vm Pointer to target VM data structure.
 * @param[in] param1 Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[in] param2 Guest physical address. This gpa points to data structure of
 *              acrn_vdev including information about PCI or legacy devices.
 *
 * @return An int32_t value indicating the success or failure of the hypercall.
 *
 * @retval 0 On successful addition of the virtual device.
 * @retval <0 If the operation fails due to invalid conditions or parameters.
 *
 * @pre is_service_vm(vcpu->vm) == true
 */
int32_t hcall_add_vdev(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, __unused uint64_t param1, uint64_t param2)
{
	struct acrn_vm *vm = vcpu->vm;
	int32_t ret = -EINVAL;
	struct acrn_vdev dev;
	struct emul_dev_ops *op;

	/*
	 * We should only create a device to a post-launched VM at creating or pausing time for safety,
	 * not runtime or other cases.
	 */
	if (is_created_vm(target_vm) || is_paused_vm(target_vm)) {
		if (copy_from_gpa(vm, &dev, param2, sizeof(dev)) == 0) {
			op = find_emul_dev_ops(&dev);
			if ((op != NULL) && (op->create != NULL)) {
				ret = op->create(target_vm, &dev);
			}
		}
	} else {
		pr_err("%s, vm[%d] is not a postlaunched VM, or not in CREATED/PAUSED status to create a vdev\n",
			 __func__, target_vm->vm_id);
	}
	return ret;
}

/**
 * @brief Hypercall to remove a virtual device from a target VM.
 *
 * This function handles the hypercall of HC_REMOVE_VDEV to remove a virtual device from a specified
 * target VM.
 *
 * The function performs the following steps:
 * 1. Check if the target VM is in the "created" or "paused" state.
 *    - Return -EINVAL if the VM is not in a valid state.
 * 2. Copy the virtual device configuration from the GPA to a local structure.
 *    - Return -EINVAL if the copy operation fails.
 * 3. Attempt to find the emulated device operations using the device information.
 *    - Return -EINVAL if the operations cannot be found, indicating an incorrect device ID.
 * 4. Locate the PCI virtual device (vdev) using the BDF value.
 *    - Return -EINVAL If the vdev cannot be found.
 * 5. Check if the `destroy` callback function is available in the emulated device operations.
 *    - If available, invoke the `destroy` function and return the result of the `destroy` operation.
 *    - If no `destroy` function is defined, return 0 to indicate success.
 *
 * @param[inout] vcpu Pointer to vCPU that initiates the hypercall.
 * @param[inout] target_vm Pointer to target VM data structure.
 * @param[in] param1 Unused, it is retained to maintain a unified interface for the hypercall dispatcher.
 * @param[in] param2 Guest physical address. This gpa points to data structure of
 *              acrn_vdev including information about PCI or legacy devices.
 *
 * @return An int32_t value indicating the success or failure of the hypercall.
 *
 * @retval 0 On successful removal of the virtual device.
 * @retval <0 If the operation fails due to invalid conditions or parameters.
 *
 * @pre is_service_vm(vcpu->vm) == true
 */
int32_t hcall_remove_vdev(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, __unused uint64_t param1, uint64_t param2)
{
	struct acrn_vm *vm = vcpu->vm;
	int32_t ret = -EINVAL;
	struct acrn_vdev dev;
	struct pci_vdev *vdev;
	struct emul_dev_ops *op;
	union pci_bdf bdf;

	/*
	 * We should only destroy a device to a post-launched VM at creating or pausing time for safety,
	 * not runtime or other cases.
	 */
	if (is_created_vm(target_vm) || is_paused_vm(target_vm)) {
		if (copy_from_gpa(vm, &dev, param2, sizeof(dev)) == 0) {
			op = find_emul_dev_ops(&dev);
			if (op != NULL) {
				bdf.value = (uint16_t) dev.slot;
				vdev = pci_find_vdev(&target_vm->vpci, bdf);
				if (vdev != NULL) {
					vdev->pci_dev_config->vbdf.value = UNASSIGNED_VBDF;
					if (op->destroy != NULL) {
						ret = op->destroy(vdev);
					} else {
						ret = 0;
					}
				} else {
					pr_warn("%s, failed to destroy emulated device %x:%x.%x\n",
						__func__, bdf.bits.b, bdf.bits.d, bdf.bits.f);
				}
			}
		}
	} else {
		pr_err("%s, vm[%d] is not a postlaunched VM, or not in CREATED/PAUSED status to destroy a vdev\n",
			 __func__, target_vm->vm_id);
	}
	return ret;
}

/**
 * @}
 */