/*
 * Copyright (C) 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <types.h>
#include <asm/guest/optee.h>
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
 * @brief VM id to be read.
 *
 * Records the VM ID to be read.
 */
static uint32_t optee_read_vm_id = 0xffffffffU;

/**
 * @brief Bitmap for resetting VMs.
 *
 * Bitmap used to record which VM is resetting.
 */
static uint64_t vm_reset_bitmap;

/**
 * @brief Get the address of the bitmap for resetting VMs.
 *
 * Get the address of the bitmap used for resetting VMs.
 *
 * @return The address of the bitmap for resetting VMs.
 */
uint64_t *optee_vm_reset_bitmap(void)
{
	return &vm_reset_bitmap;
}

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
 * @brief Test and clear the VM reset bitmap.
 *
 * Tests and clears the \p vm_reset_bitmap for the VM recorded in \p optee_read_vm_id.
 *
 * @param[inout] vcpu Pointer to the virtual CPU that is reading.
 * @param[in] addr The address port to read from. This parameter is not used in this function.
 * @param[in] width The width of the data to be read. This parameter is not used in this function.
 *
 * @return Always returns true, indicating the read operation is successful.
 *
 * @pre N/A
 *
 * @post N/A
 */
static bool optee_ctl_io_read(struct acrn_vcpu *vcpu, __unused uint16_t addr, __unused size_t width)
{
	struct acrn_pio_request *pio_req = &vcpu->req.reqs.pio_request;

	pio_req->value = (optee_read_vm_id < CONFIG_MAX_VM_NUM) ?
				bitmap_test_and_clear_lock(optee_read_vm_id, &vm_reset_bitmap) : 0U;

	pr_err("%s_2 vm_reset_bitmap: 0x%x, ret val: %d\n", __func__, vm_reset_bitmap, pio_req->value);
	return true;
}

/**
 * @brief Set the VM ID for the VM to read.
 *
 * Sets \p optee_read_vm_id to record which VM to read.
 *
 * @param[inout] vcpu Pointer to the virtual CPU that is writing. This parameter is not used in this function.
 * @param[in] addr The address port to write to. This parameter is not used in this function.
 * @param[in] width The width of the value to be written to the virtual RTC. This parameter is not used in this function.
 * @param[in] v The VM ID for the VM to read, which is recorded in \p optee_read_vm_id.
 *
 * @return Always returns true, indicating the operation is successful.
 *
 * @pre N/A
 *
 * @post N/A
 */
static bool optee_ctl_io_write(__unused struct acrn_vcpu *vcpu, __unused uint16_t addr, __unused size_t width, uint32_t v)
{
	optee_read_vm_id = v;
	return true;
}

/**
 * @brief Register TEE VM control register's PIO handler.
 *
 * Registers the TEE VM control register's PIO handler to the VM's PIO handler array. The OPTEE control register
 * address range (which base address is 0x600U and length is 1U) is aligned with OPTEE.
 *
 * @param[inout] vm The VM to which the port I/O handlers are registered.
 *
 * @return N/A
 *
 * @pre N/A
 *
 * @post N/A
 */
void optee_register_io_handler(struct acrn_vm *vm)
{
	struct vm_io_range optee_ctl_range = {
		.base = 0x600U,
		.len = 1U
	};

	register_pio_emulation_handler(vm, OPTEE_CTL_PIO_IDX, &optee_ctl_range,
			optee_ctl_io_read, optee_ctl_io_write);
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
