/*
 * Copyright (C) 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <asm/cpu.h>
#include <asm/irq.h>
#include <asm/mce.h>
#ifdef HV_DEBUG
#include <debug/dump.h>
#endif


/**
 * @addtogroup hwmgmt_irq
 *
 * @{
 */

/**
 * @file arch/x86/exception.c
 *
 * @brief Exception dispatcher implementation
 */

/**
 * @brief Dispatch exception to proper handler according to saved stack frame
 *
 * IDT exception handlers call this function after they have filled exception stack frame
 * defined in intr_excp_ctx, to invoke the proper handler identified by vector number.
 * For MC exceptions, hypervisor injects the MC events to the governing vCPU on current pCPU.
 * For other exceptions, which are not expected, hypervisor:
 *   1. Dumps host and guest contexts *to help debug.
 *   2. If CONFIG_HV_COREDUMP_ENABLED is enabled
 *        triggers warm reset to allow memory dump in bootloader.
 *   3. Otherwise, halt the CPU.
 *
 * @param[in] ctx Pointer to interrupt exception context
 *
 * @return None
 */
void dispatch_exception(struct intr_excp_ctx *ctx)
{
	if (ctx->vector == IDT_MC) {
		handle_mce();
	} else {

#ifdef HV_DEBUG
		uint16_t pcpu_id = get_pcpu_id();
		/* Dump exception context */
		dump_exception(ctx, pcpu_id);
#endif

		/* Halt the CPU */
		cpu_dead();
	}
}

/**
 * @}
 */
