/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

 #ifndef DEBUG_HYPERCALL_H
 #define DEBUG_HYPERCALL_H

int32_t hcall_setup_hv_npk_log(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, uint64_t param1, uint64_t param2);

int32_t hcall_get_hw_info(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, uint64_t param1, uint64_t param2);

int32_t hcall_profiling_ops(struct acrn_vcpu *vcpu, struct acrn_vm *target_vm, uint64_t param1, uint64_t param2);

#endif /* DEBUG_HYPERCALL_H*/
