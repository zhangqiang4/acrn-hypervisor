/*
 * Copyright (C) 2018-2025 Intel Corporation.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef TRAMPOLINE_H
#define TRAMPOLINE_H

/**
 * @addtogroup boot
 *
 * @{
 */

/**
 * @file
 * @brief Declare trampoline APIs used to initialize the environment before start APs.
 */

extern uint64_t read_trampoline_sym(const void *sym);
extern void write_trampoline_sym(const void *sym, uint64_t val);
extern void write_trampoline_stack_sym(uint16_t pcpu_id);
extern uint64_t prepare_trampoline(void);
extern uint64_t get_trampoline_start16_paddr(void);

/**
 * @brief CS segment selector that the trampoline jumps to
 *
 * The trampoline code first conducts a far jump (after disabling external interrupts) with new values to the CS segment
 * selector and instruction pointer register to mitigate non-deterministic initial value in the IP register. This
 * variable holds the new CS segment selector the trampoline code will jump to, and shall be initialized together with
 * trampoline_fixup_ip to point to the physical address of trampoline_fixup_target in the relocated trampoline section
 * before the trampoline code is executed.
 */
extern uint8_t		trampoline_fixup_cs;

/**
 * @brief EIP that the trampoline jumps to
 *
 * The trampoline code first conducts a far jump (after disabling external interrupts) with new values to the CS segment
 * selector and instruction pointer register to mitigate non-deterministic initial value in the IP register. This
 * variable holds the new instruction pointer the trampoline code will jump to, and shall be initialized together with
 * trampoline_fixup_cs to point to the physical address of trampoline_fixup_target in the relocated trampoline section
 * before the trampoline code is executed.
 */
extern uint8_t		trampoline_fixup_ip;

/**
 * @brief Offset to the code that the trampoline jumps to
 *
 * This assembly label is an offset (based on the address of ld_trampoline_start) to a code snippet that sets an C
 * execution environment on a real-address mode physical processor by switching to 64-bit mode with an appropriate
 * stack, global descriptor table and paging structure.
 */
extern uint8_t		trampoline_fixup_target;

/**
 * @brief PML4 page of the initial page table
 *
 * This variable holds the host physical address of the PML4 table of the initial paging structure and shall be
 * initialized properly in the relocated trampoline section before the trampoline code is executed.
 */
extern uint8_t		cpu_boot_page_tables_start;

/**
 * @brief Pointer to the initial page table
 *
 * This variable holds the host physical address of the paging structure that the trampoline code shall use after
 * switching to 64-bit mode and shall be initialized properly in the relocated trampoline section before the trampoline
 * code is executed.
 */
extern uint8_t		cpu_boot_page_tables_ptr;

/**
 * @brief PDPT address of the trampoline code
 *
 * This variable holds the host physical address of the PDPT table that the trampoline code shall use after
 * switching to 64-bit mode and shall be initialized properly in the relocated trampoline section before the trampoline
 * code is executed.
 */
extern uint8_t		trampoline_pdpt_addr;

/**
 * @brief Limit and base of the initial GDT
 *
 * This variable holds the limit and base (as a host linear address) of the global descriptor table (GDT) that the
 * trampoline code shall use after switching to 64-bit mode and shall be initialized properly in the relocated
 * trampoline section before the trampoline code is executed
 */
extern uint8_t		trampoline_gdt_ptr;

/**
 * @brief RIP that the trampoline jumps to after entering 64-bit mode
 *
 * The trampoline code conducts a far jump to a 64-bit code segment after entering 64-bit mode. This variable holds the
 * new instruction pointer to the 64-bit code segment the trampoline will jump to, and shall be initialized to the
 * physical address of trampoline_start64 in the relocated trampoline section before the trampoline code is executed.
 */
extern uint8_t		trampoline_start64_fixup;

/**
 * @}
 */
#endif /* TRAMPOLINE_H */
