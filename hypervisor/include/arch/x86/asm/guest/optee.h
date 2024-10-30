/*
 * Copyright (C) 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef TEE_H_
#define TEE_H_
#include <asm/guest/vm.h>

bool is_tee_vm(struct acrn_vm *vm);

#endif /* TEE_H_ */
