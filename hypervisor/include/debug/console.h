/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef CONSOLE_H
#define CONSOLE_H

#include <vuart.h>
#include <asm/guest/vcpu.h>

/**
 * @addtogroup debug
 *
 * @{
 */

/**
 * @file
 * @brief Console Operations Interface
 *
 * This file declares functions for console operations in a virtualized environment.
 * These functions provide an interface for console input/output, logging, and
 * related operations. They are used to manage console interactions and logging
 * within the system.
 */

void console_init(void);

size_t console_write(const char *s, size_t len);
void console_putc(const char *ch);
char console_getc(void);

void console_setup_timer(void);
void console_vmexit_callback(struct acrn_vcpu *vcpu);

void suspend_console(void);
void resume_console(void);
bool is_using_init_ipi(void);

bool console_need_log(uint32_t severity);
void console_log(char *buffer);

struct acrn_vuart *vm_console_vuart(struct acrn_vm *vm);

void printf(const char *fmt, ...);
void vprintf(const char *fmt, va_list args);

/**
 * @}
 */
#endif /* CONSOLE_H */
