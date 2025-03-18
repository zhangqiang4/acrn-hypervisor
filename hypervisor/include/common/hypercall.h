/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef HYPERCALL_H
#define HYPERCALL_H

/**
 * @addtogroup vp-base_hypercall
 *
 * @{
 */

/**
 * @file
 *
 * @brief Declaration of hypercall handler interfaces.
 *
 * This file defines the hypercall handler interfaces and provides these interfaces to other modules.
 */

bool is_hypercall_from_ring0(void);

int32_t hcall_service_vm_offline_cpu(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, uint64_t param1,
		 uint64_t param2);

int32_t hcall_create_vm(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, uint64_t param1, uint64_t param2);

int32_t hcall_destroy_vm(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, uint64_t param1, uint64_t param2);

int32_t hcall_reset_vm(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, uint64_t param1, uint64_t param2);

int32_t hcall_start_vm(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, uint64_t param1, uint64_t param2);

int32_t hcall_pause_vm(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, uint64_t param1, uint64_t param2);

int32_t hcall_set_vcpu_regs(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, uint64_t param1, uint64_t param2);

int32_t hcall_set_irqline(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, uint64_t param1, uint64_t param2);

int32_t hcall_inject_msi(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, uint64_t param1, uint64_t param2);

int32_t hcall_set_ioreq_buffer(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, uint64_t param1, uint64_t param2);

int32_t hcall_notify_ioreq_finish(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, uint64_t param1, uint64_t param2);

int32_t hcall_set_vm_memory_regions(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm,
		uint64_t param1, uint64_t param2);

int32_t hcall_write_protect_page(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, uint64_t param1, uint64_t param2);

int32_t hcall_assign_pcidev(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, uint64_t param1, uint64_t param2);

int32_t hcall_deassign_pcidev(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, uint64_t param1, uint64_t param2);

int32_t hcall_assign_mmiodev(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, uint64_t param1, uint64_t param2);

int32_t hcall_deassign_mmiodev(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, uint64_t param1, uint64_t param2);

int32_t hcall_add_vdev(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, uint64_t param1, uint64_t param2);

int32_t hcall_remove_vdev(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, uint64_t param1, uint64_t param2);

int32_t hcall_set_ptdev_intr_info(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, uint64_t param1, uint64_t param2);

int32_t hcall_reset_ptdev_intr_info(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm,
		uint64_t param1, uint64_t param2);

int32_t hcall_get_cpu_pm_state(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, uint64_t param1, uint64_t param2);

int32_t hcall_vm_intr_monitor(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, uint64_t param1, uint64_t param2);

int32_t hcall_set_callback_vector(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, uint64_t param1, uint64_t param2);

int32_t hcall_setup_sbuf(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, uint64_t param1, uint64_t param2);

int32_t hcall_asyncio_assign(__unused struct acrn_vcpu *vcpu, struct acrn_vm *target_vm,
		 __unused uint64_t param1, uint64_t param2);

int32_t hcall_asyncio_deassign(__unused struct acrn_vcpu *vcpu, struct acrn_vm *target_vm,
		 __unused uint64_t param1, uint64_t param2);
/**
 * @}
 */
/* End of acrn_hypercall */

#endif /* HYPERCALL_H*/
