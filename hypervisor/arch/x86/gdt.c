/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <asm/gdt.h>
#include <asm/per_cpu.h>

/**
 * @addtogroup hwmgmt_cpu
 *
 * @{
 */
/**
 * @file
 * @brief This file implements APIs to set TSS descriptor and load GDTR and TR.
 */

/**
 * @brief Set up a Task State Segment (TSS) descriptor.
 *
 * This function initializes a TSS descriptor with the provided TSS base address, limit, and type.
 *
 * @param[inout] desc Pointer to the TSS descriptor to be initialized.
 * @param[in] tss The base address of the TSS.
 * @param[in] tss_limit The limit of the TSS.
 * @param[in] type The type of the TSS descriptor.
 *
 * @return None
 *
 * @pre desc != NULL
 * @post N/A
 */
static void set_tss_desc(struct tss_64_descriptor *desc,
		uint64_t tss, size_t tss_limit, uint32_t type)
{
	uint32_t u1, u2, u3;
	uint32_t tss_hi_32 = (uint32_t)(tss >> 32U);
	uint32_t tss_lo_32 = (uint32_t)tss;

	u1 = tss_lo_32 << 16U;
	u2 = tss_lo_32 & 0xFF000000U;
	u3 = (tss_lo_32 & 0x00FF0000U) >> 16U;


	desc->low32_value = u1 | (tss_limit & 0xFFFFU);
	desc->base_addr_63_32 = tss_hi_32;
	desc->high32_value = u2 | (type << 8U) | 0x8000U | u3;
}

/**
 * @brief Loads the Global Descriptor Table (GDT) register with the specified descriptor.
 *
 * This function uses the `lgdt` instruction to load the GDT register with the provided descriptor, which contains the
 * base address and limit of the GDT.
 *
 * @param[in] gdtr Pointer to a `host_gdt_descriptor` structure containing the GDT descriptor.
 *
 * @return None
 *
 * @pre gdtr != NULL
 * @post N/A
 */
static inline void load_gdt(struct host_gdt_descriptor *gdtr)
{
	asm volatile ("lgdt %0" ::"m"(*gdtr));
}

/**
 * @brief Load Global Descriptor Table Register (GDTR) and Task Register (TR)
 *
 * This function initializes and loads the Global Descriptor Table (GDT) and Task State Segment (TSS) for the current
 * CPU. It sets up the GDT entries for the code and data segments, initializes the Interrupt Stack Table (IST) pointers
 * in the TSS, and configures the TSS descriptor in the GDT. Finally, it loads the GDTR and the Task Register (TR) with
 * the appropriate values.
 *
 * The GDT entries are configured as follows:
 * - The first entry is reserved and unused.
 * - The second entry is the ring 0 code segment descriptor.
 * - The third entry is the ring 0 data segment descriptor.
 *
 * The TSS IST pointers are set to the tops of the respective stacks:
 * - IST1 is set to the top of the machine check stack.
 * - IST2 is set to the top of the double fault stack.
 * - IST3 is set to the top of the stack fault stack.
 * - IST4 is reserved and set to 0.
 *
 * The TSS descriptor is then set in the GDT, and the GDTR is loaded with the address and size of the GDT.
 * The Task Register (TR) is loaded with the selector for the ring 0 TSS.
 *
 * @return None
 *
 * @pre N/A
 * @post N/A
 */
void load_gdtr_and_tr(void)
{
	struct host_gdt *gdt = &get_cpu_var(gdt);
	struct host_gdt_descriptor gdtr;
	struct tss_64 *tss = &get_cpu_var(tss);

	/* first entry is not used */
	gdt->rsvd = 0xAAAAAAAAAAAAAAAAUL;
	/* ring 0 code sel descriptor */
	gdt->code_segment_descriptor = 0x00Af9b000000ffffUL;
	/* ring 0 data sel descriptor */
	gdt->data_segment_descriptor = 0x00cf93000000ffffUL;

	tss->ist1 = (uint64_t)get_cpu_var(mc_stack) + CONFIG_STACK_SIZE;
	tss->ist2 = (uint64_t)get_cpu_var(df_stack) + CONFIG_STACK_SIZE;
	tss->ist3 = (uint64_t)get_cpu_var(sf_stack) + CONFIG_STACK_SIZE;
	tss->ist4 = 0UL;

	/* tss descriptor */
	set_tss_desc(&gdt->host_gdt_tss_descriptors,
		(uint64_t)tss, sizeof(struct tss_64), TSS_AVAIL);

	gdtr.len = sizeof(struct host_gdt) - 1U;
	gdtr.gdt = gdt;

	load_gdt(&gdtr);

	CPU_LTR_EXECUTE(HOST_GDT_RING0_CPU_TSS_SEL);
}
/**
 * @}
 */
