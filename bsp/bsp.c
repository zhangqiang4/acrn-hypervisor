/*
 * Copyright (C) 2018-2025 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <asm/cpu.h>
#include <logmsg.h>

static uint32_t get_lapic_id(void)
{
	uint32_t ebx;
	asm volatile("cpuid" : "=b"(ebx)
			: "a" (1)
			: "memory");
	return (ebx >> 24);
}

/*
 * bsp_init shall check whether the physical platform provides all capabilities required by the BSP and the hypervisor.
 * It shall switch the whole system into a safe state and never return if the physical platform fails to provide the
 * capabilities required.
 */
void bsp_init(void)
{
	pr_info("%s: lapic id = %x\n", __func__, get_lapic_id());
}

/*
 * bsp_fatal_error shall switch the whole system into a safe state and never return.
 */
void bsp_fatal_error(void)
{
	while (true) { asm_pause();};
}
