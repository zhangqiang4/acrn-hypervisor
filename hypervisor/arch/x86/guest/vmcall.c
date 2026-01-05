/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <errno.h>
#include <asm/lib/spinlock.h>
#include <asm/guest/vcpu.h>
#include <asm/guest/vm.h>
#include <asm/guest/vmexit.h>
#include <asm/guest/virq.h>
#include <acrn_hv_defs.h>
#include <hypercall.h>
#ifdef HV_DEBUG
#include <debug/hypercall.h>
#endif
#include <trace.h>
#include <logmsg.h>

/**
 * @addtogroup vp-base_hypercall
 *
 * @{
 */

/**
 * @file
 *
 * @brief This file provides the VMCALL VM exit handler and dispatches hypercalls according to the hypercall ID.
 *
 * This file is responsible for managing hypercalls. It handles VM exits caused by the VMCALL instruction and
 * dispatches hypercalls to the appropriate handlers.
 */

/**
 * @brief Hypercall dispatch structure.
 *
 * This structure defines a hypercall dispatch entry, including the handler function and permission flags.
 */
struct hc_dispatch {
	/**
	 * @brief Hypercall handler function.
	 *
	 * This function pointer points to the handler function for the hypercall.
	 */
	int32_t (*handler)(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, uint64_t param1, uint64_t param2);

	/**
	 * @brief Permission flags for the hypercall.
	 *
	 * The permission_flags is a bitmap of guest flags indicating whether a VM can invoke this hypercall:
	 *
	 * - If permission_flags == 0UL (which is the default value), this hypercall can only be invoked by the
	 *   Service VM.
	 * - Otherwise, this hypercall can only be invoked by a VM whose guest flags have ALL set bits in
	 *   permission_flags.
	 */
	uint64_t permission_flags;
};

/**
 * @brief Hypercall dispatch table.
 *
 * This table maps hypercall IDs to their corresponding handler functions.
 * Each entry in the table contains a handler function that will be called
 * when the corresponding hypercall is invoked.
 */
static const struct hc_dispatch hc_dispatch_table[] = {
	[HC_IDX(HC_PROBE_FEATURE)] = {
		.handler = hcall_probe_feature},	/**< Handler for HC_SERVICE_VM_OFFLINE_CPU hypercall */
	[HC_IDX(HC_SERVICE_VM_OFFLINE_CPU)] = {
		.handler = hcall_service_vm_offline_cpu},	/**< Handler for HC_SERVICE_VM_OFFLINE_CPU hypercall */
	[HC_IDX(HC_SERVICE_VM_ONLINE_CPU)] = {
		.handler = hcall_service_vm_online_cpu},	/**< Handler for HC_SERVICE_VM_ONLINE_CPU hypercall */
	[HC_IDX(HC_SET_CALLBACK_VECTOR)] = {
		.handler = hcall_set_callback_vector},		/**< Handler for HC_SET_CALLBACK_VECTOR hypercall */
	[HC_IDX(HC_CREATE_VM)] = {
		.handler = hcall_create_vm},			/**< Handler for HC_CREATE_VM hypercall */
	[HC_IDX(HC_DESTROY_VM)] = {
		.handler = hcall_destroy_vm},			/**< Handler for HC_DESTROY_VM hypercall */
	[HC_IDX(HC_START_VM)] = {
		.handler = hcall_start_vm},			/**< Handler for HC_START_VM hypercall */
	[HC_IDX(HC_RESET_VM)] = {
		.handler = hcall_reset_vm},			/**< Handler for HC_RESET_VM hypercall */
	[HC_IDX(HC_PAUSE_VM)] = {
		.handler = hcall_pause_vm},			/**< Handler for HC_PAUSE_VM hypercall */
	[HC_IDX(HC_SET_VCPU_REGS)] = {
		.handler = hcall_set_vcpu_regs},		/**< Handler for HC_SET_VCPU_REGS hypercall */
	[HC_IDX(HC_SET_IRQLINE)] = {
		.handler = hcall_set_irqline},			/**< Handler for HC_SET_IRQLINE hypercall */
	[HC_IDX(HC_INJECT_MSI)] = {
		.handler = hcall_inject_msi},			/**< Handler for HC_INJECT_MSI hypercall */
	[HC_IDX(HC_SET_IOREQ_BUFFER)] = {
		.handler = hcall_set_ioreq_buffer},		/**< Handler for HC_SET_IOREQ_BUFFER hypercall */
	[HC_IDX(HC_ASYNCIO_ASSIGN)] = {
		.handler = hcall_asyncio_assign},		/**< Handler for HC_ASYNCIO_ASSIGN hypercall */
	[HC_IDX(HC_ASYNCIO_DEASSIGN)] = {
		.handler = hcall_asyncio_deassign},		/**< Handler for HC_ASYNCIO_DEASSIGN hypercall */
	[HC_IDX(HC_NOTIFY_REQUEST_FINISH)] = {
		.handler = hcall_notify_ioreq_finish},		/**< Handler for HC_NOTIFY_REQUEST_FINISH hypercall */
	[HC_IDX(HC_VM_SET_MEMORY_REGIONS)] = {
		.handler = hcall_set_vm_memory_regions},	/**< Handler for HC_VM_SET_MEMORY_REGIONS hypercall */
	[HC_IDX(HC_VM_WRITE_PROTECT_PAGE)] = {
		.handler = hcall_write_protect_page},		/**< Handler for HC_VM_WRITE_PROTECT_PAGE hypercall */
	[HC_IDX(HC_ASSIGN_PCIDEV)] = {
		.handler = hcall_assign_pcidev},		/**< Handler for HC_ASSIGN_PCIDEV hypercall */
	[HC_IDX(HC_DEASSIGN_PCIDEV)] = {
		.handler = hcall_deassign_pcidev},		/**< Handler for HC_DEASSIGN_PCIDEV hypercall */
	[HC_IDX(HC_ASSIGN_MMIODEV)] = {
		.handler = hcall_assign_mmiodev},		/**< Handler for HC_ASSIGN_MMIODEV hypercall */
	[HC_IDX(HC_DEASSIGN_MMIODEV)] = {
		.handler = hcall_deassign_mmiodev},		/**< Handler for HC_DEASSIGN_MMIODEV hypercall */
	[HC_IDX(HC_ADD_VDEV)] = {
		.handler = hcall_add_vdev},			/**< Handler for HC_ADD_VDEV hypercall */
	[HC_IDX(HC_REMOVE_VDEV)] = {
		.handler = hcall_remove_vdev},			/**< Handler for HC_REMOVE_VDEV hypercall */
	[HC_IDX(HC_SET_PTDEV_INTR_INFO)] = {
		.handler = hcall_set_ptdev_intr_info},		/**< Handler for HC_SET_PTDEV_INTR_INFO hypercall */
	[HC_IDX(HC_RESET_PTDEV_INTR_INFO)] = {
		.handler = hcall_reset_ptdev_intr_info},	/**< Handler for HC_RESET_PTDEV_INTR_INFO hypercall */
	[HC_IDX(HC_PM_GET_CPU_STATE)] = {
		.handler = hcall_get_cpu_pm_state},		/**< Handler for HC_PM_GET_CPU_STATE hypercall */
	[HC_IDX(HC_VM_INTR_MONITOR)] = {
		.handler = hcall_vm_intr_monitor},		/**< Handler for HC_VM_INTR_MONITOR hypercall */
	[HC_IDX(HC_SETUP_SBUF)] = {
		.handler = hcall_setup_sbuf},			/**< Handler for HC_SETUP_SBUF hypercall */
#ifdef HV_DEBUG
	[HC_IDX(HC_SETUP_HV_NPK_LOG)] = {
		.handler = hcall_setup_hv_npk_log},		/**< Handler for HC_SETUP_HV_NPK_LOG hypercall */
	[HC_IDX(HC_PROFILING_OPS)] = {
		.handler = hcall_profiling_ops},		/**< Handler for HC_PROFILING_OPS hypercall */
	[HC_IDX(HC_GET_HW_INFO)] = {
		.handler = hcall_get_hw_info},			/**< Handler for HC_GET_HW_INFO hypercall */
#endif
};

/**
 * @brief Parse the target VM for a given hypercall.
 *
 * This function parses the target VM for a given hypercall. It determines the target VM
 * based on the hypercall ID and parameters. For certain hypercalls, it may create a new VM
 * or retrieve an existing VM based on the provided parameters.
 *
 * How the VM ID is determined:
 * 1. For `HC_CREATE_VM`: The vm_id is determined by checking if the VM name exists in the predefined
 *    VM list using `get_vmid_by_name`. If not found, a new vm_id is allocated dynamically using
 *    `get_unused_vmid`.
 * 2. For hypercalls like `HC_SERVICE_VM_OFFLINE_CPU`, `HC_SET_CALLBACK_VECTOR`,
 *    `HC_SETUP_HV_NPK_LOG`, `HC_PROFILING_OPS`, and `HC_GET_HW_INFO`: The target VM is
 *    the Service VM itself, and no vm_id conversion is needed.
 * 3. For `HC_PM_GET_CPU_STATE`:  The vm_id is extracted from `param1` as a relative
 *    vm_id and converted to an absolute vm_id using `rel_vmid_2_vmid`.
 * 4. For `HC_VM_SET_MEMORY_REGIONS`: The vm_id is extracted from the `set_regions` structure as a
 *    relative vm_id and converted to an absolute vm_id using `rel_vmid_2_vmid`.
 * 5. For other hypercalls: The vm_id is directly converted from a relative vm_id provided in
 *    `param1` using `rel_vmid_2_vmid`.
 *
 * Note: If `target_vm` is `NULL` and the resulting `vm_id` is greater than or equal to `CONFIG_MAX_VM_NUM`,
 * a NULL pointer is returned, indicating that the target VM cannot be determined.
 *
 * Note: These three hypercalls are debug only: `HC_SETUP_HV_NPK_LOG`, `HC_PROFILING_OPS`,
 * and `HC_GET_HW_INFO`.
 *
 * @param[inout] service_vm Pointer to the acrn_vm structure representing the service VM.
 * @param[in] hcall_id The hypercall ID.
 * @param[in] param1 The first parameter of the hypercall.
 * @param[in] param2 The second parameter of the hypercall (unused).
 *
 * @return Pointer to the target acrn_vm structure.
 *
 * @retval non-NULL If get the target VM successfully.
 * @retval NULL If cannot get the target VM.
 *
 * @pre is_service_vm(service_vm) == true
 */
static struct acrn_vm *parse_target_vm(struct acrn_vm *service_vm, uint64_t hcall_id, uint64_t param1,
	__unused uint64_t param2)
{
	struct acrn_vm *target_vm = NULL;
	uint16_t vm_id = ACRN_INVALID_VMID;
	struct acrn_vm_creation cv;
	struct set_regions regions;
	struct acrn_vm_config *vm_config;
	uint16_t relative_vm_id;

	switch (hcall_id) {
	case HC_CREATE_VM:
		if (copy_from_gpa(service_vm, &cv, param1, sizeof(cv)) == 0) {
			vm_id = get_vmid_by_name((char *)cv.name);
			/* if the vm-name is not found, it indicates that it is not in pre-defined vm_list.
			 * So try to allocate one free slot to start one vm based on user-requirement
			 */
			if (vm_id == ACRN_INVALID_VMID) {
				vm_id = get_unused_vmid((char *)cv.name);
				/* it doesn't find the available vm_slot for the given vm_name.
				 * Maybe the CONFIG_MAX_VM_NUM is too small to start the VM.
				 */
				if (vm_id == ACRN_INVALID_VMID) {
					pr_err("The VM name provided (%s) is invalid, cannot create VM", cv.name);
				} else {
					vm_config = get_vm_config(vm_id);
					vm_config->cpu_affinity = cv.cpu_affinity;
				}
			}
		}
		break;

	case HC_PM_GET_CPU_STATE:
		vm_id = rel_vmid_2_vmid(service_vm->vm_id, (uint16_t)((param1 & PMCMD_VMID_MASK) >> PMCMD_VMID_SHIFT));
		break;

	case HC_VM_SET_MEMORY_REGIONS:
		if (copy_from_gpa(service_vm, &regions, param1, sizeof(regions)) == 0) {
			/* the vmid in regions is a relative vm id, need to convert to absolute vm id */
			vm_id = rel_vmid_2_vmid(service_vm->vm_id, regions.vmid);
		}
		break;
	case HC_SERVICE_VM_OFFLINE_CPU:
	case HC_SERVICE_VM_ONLINE_CPU:
	case HC_SET_CALLBACK_VECTOR:
#ifdef HV_DEBUG
	case HC_SETUP_HV_NPK_LOG:
	case HC_PROFILING_OPS:
	case HC_GET_HW_INFO:
#endif
		target_vm = service_vm;
		break;
	default:
		relative_vm_id = (uint16_t)param1;
		vm_id = rel_vmid_2_vmid(service_vm->vm_id, relative_vm_id);
		break;
	}

	if ((target_vm == NULL) && (vm_id  < CONFIG_MAX_VM_NUM)) {
		target_vm = get_vm_from_vmid(vm_id);
		if (hcall_id == HC_CREATE_VM) {
			target_vm->vm_id = vm_id;
		}
	}

	return target_vm;
}

/**
 * @brief Dispatch a hypercall to the appropriate handler.
 *
 * This function dispatches a hypercall to the appropriate handler based on the
 * hypercall ID. It performs permission checks to ensure that the hypercall is
 * allowed for the calling VM. If the hypercall is allowed, it invokes the handler
 * with the appropriate parameters. The function returns a status code
 * indicating the result of the hypercall dispatch.
 *
 * Key points regarding hypercall handling:
 * 1. The hypercall ID is retrieved from the R8 register of the vCPU.
 * 2. The function checks if the hypercall ID is within the bounds of the dispatch table.
 * 3. If a handler exists for the hypercall, the function retrieves the first and second parameters
 *    from the RDI and RSI registers, respectively.
 * 4. If the permission flags are zero and the VM is a Service VM, the hypercall is intended for
 *    managing post-launched VMs. The function attempts to find the target VM using `parse_target_vm`.
 * 5. If the target VM is found and is not a pre-launched VM, the hypercall handler is invoked with
 *    the target VM.
 * 6. If the permission flags are non-zero, the function checks if the guest flags allow the hypercall.
 *    If allowed, the handler is invoked with the current VM.
 * 7. If the vCPU is not allowed to invoke the hypercall, the function returns -ENOTTY.
 *
 * @param[inout] vcpu Pointer to the acrn_vcpu structure representing the calling VCPU.
 *
 * @return An int32_t value indicating whether this hypercall is successful.
 *
 * @retval 0 On successful execution of the hypercall.
 * @retval <0 On failure, with -ENOTTY if the hypercall is not supported or the vCPU lacks permission.
 *
 * @pre vcpu != NULL
 */
static int32_t dispatch_hypercall(struct acrn_vcpu *vcpu)
{
	int32_t ret = -ENOTTY;
	struct acrn_vm *vm = vcpu->vm;
	uint64_t guest_flags = get_vm_config(vm->vm_id)->guest_flags;  /* hypercall ID from guest */
	uint64_t hcall_id = vcpu_get_gpreg(vcpu, CPU_REG_R8);  /* hypercall ID from guest */

	if (HC_IDX(hcall_id) < ARRAY_SIZE(hc_dispatch_table)) {
		const struct hc_dispatch *dispatch = &(hc_dispatch_table[HC_IDX(hcall_id)]);
		uint64_t permission_flags = dispatch->permission_flags;

		if (dispatch->handler != NULL) {
			uint64_t param1 = vcpu_get_gpreg(vcpu, CPU_REG_RDI);  /* hypercall param1 from guest */
			uint64_t param2 = vcpu_get_gpreg(vcpu, CPU_REG_RSI);  /* hypercall param2 from guest */

			if ((permission_flags == 0UL) && is_service_vm(vm)) {
				/* A permission_flags of 0 indicates that this hypercall is for Service VM to manage
				 * post-launched VMs.
				 *
				 * Though REE VM has its load order to be Service_VM, it does not offer services as
				 * Service VM does. The only hypercalls allowed for REE are the ones with permission
				 * flag GUEST_FLAG_REE.
				 */
				struct acrn_vm *target_vm = parse_target_vm(vm, hcall_id, param1, param2);

				if ((target_vm != NULL) && !is_prelaunched_vm(target_vm)) {
					get_vm_lock(target_vm);
					ret = dispatch->handler(vcpu, target_vm, param1, param2);
					put_vm_lock(target_vm);
				}
			} else if ((permission_flags != 0UL) &&
					((guest_flags & permission_flags) != 0UL)) {
				ret = dispatch->handler(vcpu, vcpu->vm, param1, param2);
			} else {
				/* The vCPU is not allowed to invoke the given hypercall. Keep `ret` as -ENOTTY and no
				 * further actions required.
				 */
			}
		}
	}

	return ret;
}

/**
 * @brief Handle VM exit caused by VMCALL instruction.
 *
 * This function handles the VM exit caused by the VMCALL instruction. It performs
 * various permission checks to ensure that only allowed VMs can invoke hypercalls
 * and that they are invoked from the correct privilege level. Depending on the
 * result of these checks, it either dispatches the hypercall or injects an exception.
 * The function sets the hypercall result in the RAX register.
 * This function always return 0, since we don't deal with hypercall error in hypervisor.
 *
 * Permission checks applied to hypercalls:
 * 1. ACRN hypervisor only supports hypercalls sent from Service VMs. If a hypercall is not
 *    sent from a Service VM, the ACRN hypervisor injects a \#UD.
 * 2. ACRN hypervisor only supports hypercalls under 64-bit mode. For non-64-bit modes, the
 *    ACRN hypervisor injects a \#UD.
 * 3. When a vCPU of a Service VM attempts to execute a hypercall, the ACRN hypervisor shall
 *    check if the vCPU is in ring 0. Otherwise, the ACRN hypervisor injects a \#GP(0) to the vCPU.
 *
 * @param[inout] vcpu Pointer to the acrn_vcpu structure representing the calling VCPU.
 *
 * @return An int32_t value indicating whether this vmexit was successfully handled, always returning 0.
 *
 * @retval 0 Always return 0.
 *
 * @pre vcpu != NULL
 */
int32_t vmcall_vmexit_handler(struct acrn_vcpu *vcpu)
{
	int32_t ret = -EACCES;
	struct acrn_vm *vm = vcpu->vm;
	/* hypercall ID from guest*/
	uint64_t hypcall_id = vcpu_get_gpreg(vcpu, CPU_REG_R8);

	/*
	 * The following permission checks are applied to hypercalls.
	 *
	 * 1. Only Service VM and VMs with specific guest flags (referred to as 'allowed VMs' hereinafter) can invoke
	 *    hypercalls by executing the `vmcall` instruction. Attempts to execute the `vmcall` instruction in the
	 *    other VMs will return -EACCES. No exception is triggered in this case.
	 * 2. Hypercall can only be executed under 64bit mode. Otherwise, return -EACCES.
	 * 2. Attempts to execute the `vmcall` instruction from ring 1, 2 or 3 in an allowed VM will trigger #GP(0).
	 * 3. An allowed VM is permitted to only invoke some of the supported hypercalls depending on its load order and
	 *    guest flags. Attempts to invoke an unpermitted hypercall will make a vCPU see -EINVAL as the return
	 *    value. No exception is triggered in this case.
	 */
	if (!is_service_vm(vm) || (get_vcpu_mode(vcpu) != CPU_MODE_64BIT)) {
		vcpu_set_gpreg(vcpu, CPU_REG_RAX, (uint64_t)ret);
	} else if (!is_hypercall_from_ring0()) {
		vcpu_inject_gp(vcpu, 0U);
	} else {
		ret = dispatch_hypercall(vcpu);
		vcpu_set_gpreg(vcpu, CPU_REG_RAX, (uint64_t)ret);
	}

	if (ret < 0) {
		pr_err("ret=%d hypercall=0x%lx failed in %s\n", ret, hypcall_id, __func__);
	}
	TRACE_2L(TRACE_VMEXIT_VMCALL, vm->vm_id, hypcall_id);

	return 0;
}

/**
 * @}
 */
