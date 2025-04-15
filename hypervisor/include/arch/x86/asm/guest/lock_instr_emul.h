/*
 * Copyright (C) 2021-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef SPLITLOCK_H_
#define SPLITLOCK_H_

#ifdef CONFIG_LOCK_INSTRUCTION_EMULATION

void vcpu_complete_lock_instr_emulation(struct acrn_vcpu *cur_vcpu);
int32_t emulate_lock_instr(struct acrn_vcpu *vcpu, uint32_t exception_vector, bool *queue_exception);

#endif /* CONFIG_LOCK_INSTRUCTION_EMULATION */

#endif /* SPLITLOCK_H_ */
