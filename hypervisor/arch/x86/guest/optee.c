/*
 * Copyright (C) 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <types.h>
#include <asm/guest/vm.h>
#include <asm/guest/virq.h>
#include <asm/vm_config.h>
#include <logmsg.h>

/**
 * @defgroup vp-base_optee vp-base.optee
 * @ingroup vp-base
 * @brief Implementation of TEE related functions.
 *
 * This module provides interfaces for TEE VM management in ACRN. It includes functionality
 * for identifying TEE VMs, freezing and thawing VM execution states, and managing the
 * lifecycle of TEE-related virtual machine resources.
 *
 * @{
 */

/**
 * @file
 * @brief Implementation of TEE VM operations
 *
 * This file provides functions to manage Trusted Execution Environment (TEE) virtual machines,
 * including identification of TEE VMs, freezing and thawing VM execution states, and
 * maintaining TEE VM lifecycle operations.
 */

/**
 * @brief TEE VM pointer
 *
 * A pointer to ACRN Virtual Machine structure of TEE VM.
 */
static struct acrn_vm *tee_vm_ptr = NULL;

/**
 * @brief Get ACRN Virtual Machine data structure associated with the TEE VM.
 *
 * This function gets ACRN Virtual Machine data structure pointer `tee_vm_ptr` associated with TEE VM.
 * Assuming there is at most one TEE VM.
 *
 * @return ACRN Virtual Machine structure pointer associated with TEE VM, or NULL if there is no TEE VM.
 */
struct acrn_vm *get_tee_vm(void)
{
	return tee_vm_ptr;
}

/**
 * @brief Set ACRN Virtual Machine data structure associated with the TEE VM.
 *
 * This function sets ACRN Virtual Machine data structure pointer `tee_vm_ptr` associated with TEE VM.
 * Assuming there is at most one TEE VM.
 *
 * @param[in] vm Pointer to the VM structure to be set as the TEE VM.
 */
void set_tee_vm(struct acrn_vm *vm)
{
	tee_vm_ptr = vm;
}

/**
 * @brief Check if a VM is a Trusted Execution Environment (TEE) VM.
 *
 * This function determines whether the specified virtual machine (VM) is configured as a Trusted Execution
 * Environment (TEE) VM by checking its guest flags.
 *
 * @param[in] vm A pointer to the VM structure to be checked.
 *
 * @return true if the VM is a TEE VM, false otherwise.
 *
 * @pre vm != NULL
 */
bool is_tee_vm(struct acrn_vm *vm)
{
	return (get_vm_config(vm->vm_id)->guest_flags & GUEST_FLAG_TEE) != 0;
}

/**
 * @brief Freezes a virtual CPU and waits for a thaw event
 *
 * This function temporarily suspends a virtual CPU's execution by:
 * 1. Clearing its Virtual Machine Control Structure (VMCS)
 * 2. Setting it to a frozen state
 * 3. Waiting for a thaw event, and clears the frozen state once it is thawed.
 * 4. Resuming execution once thawed
 *
 * When the VCPU is thawed, if an instruction length is set, the guest RIP is advanced by that length
 * before resuming execution.
 *
 * @param[in] vcpu Pointer to the virtual CPU structure to freeze
 *
 * @pre vcpu != NULL
 */
void enter_refrigerator(struct acrn_vcpu *vcpu)
{
	clear_va_vmcs(vcpu->arch.vmcs);
	pr_info("vmcs cleared, is going to enter refrigerator");
	vcpu->frozen = true;
	wait_event(&vcpu->events[VCPU_EVENT_THAW]);
	if (vcpu->arch.inst_len != 0U) {
		exec_vmwrite(VMX_GUEST_RIP, vcpu_get_rip(vcpu) + vcpu->arch.inst_len);
	}
	pr_info("vcpu thread resumed");
	vcpu->frozen = false;
}

/**
 * @brief Freezes a TEE VM by transitioning all its vCPUs to frozen state.
 *
 * This function requests all vCPUs of the specified TEE VM to enter the "refrigerator" (frozen) state by sending
 * ACRN_REQUEST_ENTER_REFRIGERATOR request to each vCPU. It then waits until all vCPUs have transitioned to the
 * BLOCKED state and are marked as frozen before updating the VM's state to VM_FROZEN.
 *
 * The function only acts if the VM is currently in VM_RUNNING state.
 *
 * @param[in] vm Pointer to the TEE VM structure to be frozen
 *
 * @pre vm != NULL
 */
void freeze_vm(struct acrn_vm *vm)
{
	uint16_t i;
	struct acrn_vcpu *vcpu = NULL;

	if (vm->state == VM_RUNNING) {
		foreach_vcpu(i, vm, vcpu) {
			pr_info("request vm%d vcpu%d to freeze", vm->vm_id, vcpu->vcpu_id);
			vcpu_make_request(vcpu, ACRN_REQUEST_ENTER_REFRIGERATOR);
			/* wake vcpu thread up from waiting HLT */
			signal_event(&vcpu->events[VCPU_EVENT_VIRTUAL_INTERRUPT]);
		}
		while (true) {
			bool wait = false;
			foreach_vcpu(i, vm, vcpu) {
				if ((vcpu->thread_obj.status != THREAD_STS_BLOCKED) || (vcpu->frozen == false)) {
					wait = true;
					break;
				}
			}
			if (wait) {
				schedule();
			} else {
				pr_info("vm%d all vcpus have entered blocked state", vm->vm_id);
				break;
			}
		}
		vm->state = VM_FROZEN;
	}
}

/**
 * @brief Thaw a frozen TEE VM by transitioning it back to running state
 *
 * This function transitions a TEE VM from the frozen state back to the running state. It iterates through all vCPUs
 * in the VM, resets their launched state, signals a thaw event to each VCPU, and set the VM state to VM_RUNNING.
 *
 * The function only acts if the VM is currently in VM_FROZEN state.
 *
 * @param[in] vm Pointer to the TEE VM structure to be thawed
 *
 * @pre vm != NULL
 */
void thaw_vm(struct acrn_vm *vm)
{
	uint16_t i;
	struct acrn_vcpu *vcpu = NULL;

	if (vm->state == VM_FROZEN) {
		foreach_vcpu(i, vm, vcpu) {
			pr_info("thaw vm%d vcpu%d", vm->vm_id, vcpu->vcpu_id);
			vcpu->launched = false;
			signal_event(&vcpu->events[VCPU_EVENT_THAW]);
		}
		vm->state = VM_RUNNING;
	}
}

/**
 * @}
 */
