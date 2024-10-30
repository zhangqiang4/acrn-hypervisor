/*
 * Copyright (C) 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <types.h>
#include <asm/guest/vm.h>
#include <asm/vm_config.h>

bool is_tee_vm(struct acrn_vm *vm)
{
	return (get_vm_config(vm->vm_id)->guest_flags & GUEST_FLAG_TEE) != 0;
}
