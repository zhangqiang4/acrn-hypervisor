/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <asm/mmu.h>
#include <asm/per_cpu.h>
#include <asm/trampoline.h>
#include <boot.h>
#include <asm/e820.h>

/**
 * @addtogroup boot
 *
 * @{
 */

/**
 * @file
 * @brief Define variables and implement trampoline APIs to initialize the environment before start APs.
 *
 * 1.Define variables used to boot APs.
 * 2.Implementation of trampoline APIs to initialize the environment before start APs.
 */

/**
 * @brief An integer value representing the AP startup address.
 */
static uint64_t trampoline_start16_paddr;

/**
 * @brief Calculate relocated symbol address for trampoline code
 *
 * This function calculates the relocated address for a given symbol pointer in trampoline
 * code by adjusting it based on the hypervisor image's load delta. The trampoline
 * code is relocated differently from the hypervisor code, so any relative addressing
 * from the hypervisor to the trampoline must account for this difference by removing
 * the hypervisor relocation delta.
 *
 * @param[in] addr symbol address within the trampoline code that needs to be relocated.
 *
 * @return The relocated address, adjusted by subtracting the hypervisor
 *                  image's load delta from the original address.
 *
 * @pre addr != NULL
 *
 * @post N/A
 *
 * @remark This function is always valid because:
 *         - Config tool will guarantee CONFIG_HV_RAM_START be higher than MEM_1M;
 *         - Boot loader will guarantee HV code be located higher than CONFIG_HV_RAM_START;
 *         - The trampoline code is located under MEM_1M;
 *
 * @remark This function relies on `get_hv_image_delta` to obtain the load delta,
 *         which is used to compute the correct relocated address.
 */
static uint64_t trampoline_relo_addr(const void *addr)
{
	return (uint64_t)addr - get_hv_image_delta();
}

/**
 * @brief Read symbol from trampoline code
 *
 * This function reads the value of a symbol located in the trampoline code. It
 * calculates the host virtual address (HVA) of the symbol by adding the relocated
 * address of the symbol to the base address of the trampoline code. The function
 * then returns the value stored at that calculated address.
 *
 * @param[in] sym pointer to the original address of the symbol within the trampoline
 *            code that needs to be read.
 *
 * @return The value of the symbol located in the trampoline code.
 *
 * @pre sym != NULL
 *
 * @post N/A
 */
uint64_t read_trampoline_sym(const void *sym)
{
	uint64_t *hva = (uint64_t *)(hpa2hva(trampoline_start16_paddr) + trampoline_relo_addr(sym));
	return *hva;
}

/**
 * @brief Write value to symbol in trampoline code
 *
 * This function writes a specified value to a symbol located in the trampoline
 * code. It calculates the host virtual address (HVA) of the symbol by adding the
 * relocated address of the symbol to the base address of the trampoline code.
 * After writing the value, the function flushes the cache line to ensure memory
 * consistency.
 *
 * @param[out] sym A pointer to the original address of the symbol within the trampoline
 *            code that needs to be written to.
 * @param[in] val The value to be written to the symbol.
 *
 * @return None
 *
 * @pre sym != NULL
 *
 * @post N/A
 */
void write_trampoline_sym(const void *sym, uint64_t val)
{
	uint64_t *hva = (uint64_t *)(hpa2hva(trampoline_start16_paddr) + trampoline_relo_addr(sym));
	*hva = val;
	clflush(hva);
}

/**
 * @brief Write stack pointer address to trampoline symbol
 *
 * This function writes the stack pointer address for a specific physical CPU
 * (pcpu_id) to a symbol located in the trampoline code. It calculates the host
 * virtual address (HVA) of the symbol by adding the relocated address of the
 * symbol to the base address of the trampoline code. The function computes the
 * aligned stack address for the specified CPU, writes it to the symbol, and
 * flushes the cache line to ensure memory consistency.
 *
 * @param[in] pcpu_id The physical CPU ID for which the stack pointer address is being written.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
void write_trampoline_stack_sym(uint16_t pcpu_id)
{
	uint64_t *hva, stack_sym_addr;
	hva = (uint64_t *)(hpa2hva(trampoline_start16_paddr) + trampoline_relo_addr(secondary_cpu_stack));

	stack_sym_addr = (uint64_t)&per_cpu(stack, pcpu_id)[CONFIG_STACK_SIZE - 1];
	stack_sym_addr &= ~(CPU_STACK_ALIGN - 1UL);
	*hva = stack_sym_addr;

	clflush(hva);
}

/**
 * @brief Get Physical Address of Trampoline Start (16-bit real mode)
 *
 * This function returns the physical address of the start of the 16-bit real mode trampoline
 * code. The trampoline code is typically used for transitioning between different execution
 * modes or for initializing secondary processors in a multiprocessor system.
 *
 * @return The physical address of the start of the 16-bit real mode trampoline code.
 *
 * @pre N/A
 *
 * @post N/A
 */
uint64_t get_trampoline_start16_paddr(void)
{
	return trampoline_start16_paddr;
}

/**
 * @brief Update Trampoline Code References
 *
 * This function updates various references in the trampoline code to account for
 * its relocation to a new physical address. It adjusts the code segment (CS) and
 * instruction pointer (IP) values, updates temporary page tables, and modifies
 * other critical pointers (the gdt base pointer, trampoline jump pointer, trampoline's
 * main entry pointer) to ensure the trampoline code functions correctly after
 * being relocated.
 *
 * @param[in] dest_pa The destination physical address where the trampoline code is relocated.
 *
 * @pre N/A
 *
 * @post N/A
 */
static void update_trampoline_code_refs(uint64_t dest_pa)
{
	void *ptr;
	uint64_t val;
	uint32_t i;

	/*
	 * calculate the fixup CS:IP according to fixup target address
	 * dynamically.
	 *
	 * trampoline code starts in real mode,
	 * so the target addres is HPA
	 */
	val = dest_pa + trampoline_relo_addr(&trampoline_fixup_target);

	ptr = hpa2hva(dest_pa + trampoline_relo_addr(&trampoline_fixup_cs));
	*(uint16_t *)(ptr) = (uint16_t)((val >> 4U) & 0xFFFFU);

	ptr = hpa2hva(dest_pa + trampoline_relo_addr(&trampoline_fixup_ip));
	*(uint16_t *)(ptr) = (uint16_t)(val & 0xfU);

	/* Update temporary page tables */
	ptr = hpa2hva(dest_pa + trampoline_relo_addr(&cpu_boot_page_tables_ptr));
	*(uint32_t *)(ptr) += (uint32_t)dest_pa;

	ptr = hpa2hva(dest_pa + trampoline_relo_addr(&cpu_boot_page_tables_start));
	*(uint64_t *)(ptr) += dest_pa;

	ptr = hpa2hva(dest_pa + trampoline_relo_addr(&trampoline_pdpt_addr));
	for (i = 0U; i < 4U; i++) {
		*(uint64_t *)(ptr + sizeof(uint64_t) * i) += dest_pa;
	}

	/* update the gdt base pointer with relocated offset */
	ptr = hpa2hva(dest_pa + trampoline_relo_addr(&trampoline_gdt_ptr));
	*(uint64_t *)(ptr + 2) += dest_pa;

	/* update trampoline jump pointer with relocated offset */
	ptr = hpa2hva(dest_pa + trampoline_relo_addr(&trampoline_start64_fixup));
	*(uint32_t *)ptr += (uint32_t)dest_pa;

	/* update trampoline's main entry pointer */
	ptr = hpa2hva(dest_pa + trampoline_relo_addr(main_entry));
	*(uint64_t *)ptr += get_hv_image_delta();
}

/**
 * @brief Prepare Trampoline Code for Execution
 *
 * This function prepares the trampoline code for execution by performing the following steps:
 * 1. Allocates memory for the trampoline code below 1MB.
 * 2. Copies the trampoline code to the allocated memory.
 * 3. Updates code references to account for the new location.
 * 4. Ensures memory consistency by flushing the cache.
 *
 * The trampoline code is used for initializing secondary processors in a multiprocessor system.
 *
 * @return The physical address where the trampoline code is loaded.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark This function assumes that the trampoline code must be loaded below
 *       1MB, which is a common requirement for real-mode execution.
 */
uint64_t prepare_trampoline(void)
{
	uint64_t trampline_size, trampoline_pa;

	trampline_size = (uint64_t)(&ld_trampoline_end - &ld_trampoline_start);
	trampoline_pa = e820_alloc_memory(trampline_size, MEM_1M);

	pr_dbg("trampoline code: %lx trampline_size %lx", trampoline_pa, trampline_size);

	/* Copy segment for AP initialization code below 1MB */
	(void)memcpy_s(hpa2hva(trampoline_pa), (size_t)trampline_size, &ld_trampoline_load,
			(size_t)trampline_size);
	update_trampoline_code_refs(trampoline_pa);

	cpu_memory_barrier();
	flush_cache_range(hpa2hva(trampoline_pa), trampline_size);

	trampoline_start16_paddr = trampoline_pa;

	return trampoline_pa;
}
/**
 * @}
 */
