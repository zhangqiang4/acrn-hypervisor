/*
 * Copyright (C) 2019-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef VM_EVENT_H
#define VM_EVENT_H

#ifdef CONFIG_VMEVENT_ENABLED

#include <types.h>
#include <acrn_common.h>

void init_vm_event(struct acrn_vm *vm, struct shared_buf *sbuf);
void send_vm_event(struct acrn_vm *vm, struct vm_event *event);

#endif

#endif /* VM_EVENT_H */
