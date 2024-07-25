/*
 * Copyright (C) 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <asm/cpu.h>
#include <asm/irq.h>
#include <debug/dump.h>
#include <asm/mce.h>

void dispatch_exception(struct intr_excp_ctx *ctx)
{
	uint16_t pcpu_id = get_pcpu_id();

	if (ctx->vector == IDT_MC) {
		handle_mce();
	} else {
		/* Dump exception context */
		dump_exception(ctx, pcpu_id);

		/* Halt the CPU */
		cpu_dead();
	}
}
