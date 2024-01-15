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
 * @brief TEE VM pointer
 *
 * A pointer to ACRN Virtual Machine structure of TEE VM. Assuming there is at most one TEE VM.
 */
static struct acrn_vm *tee_vm_ptr = NULL;

/**
 * @brief Get ACRN Virtual Machine data structure associated with the TEE VM.
 *
 * This function returns ACRN Virtual Machine data structure pointer associated
 * with TEE VM. Assuming there is at most one TEE VM.
 *
 * @pre N/A
 * @post N/A
 *
 * @return ACRN Virtual Machine structure pointer associated with TEE VM.
 */
struct acrn_vm *get_tee_vm(void)
{
	return tee_vm_ptr;
}

void set_tee_vm(struct acrn_vm *vm)
{
	tee_vm_ptr = vm;
}

bool is_tee_vm(struct acrn_vm *vm)
{
	return (get_vm_config(vm->vm_id)->guest_flags & GUEST_FLAG_TEE) != 0;
}

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
		while(1) {
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
