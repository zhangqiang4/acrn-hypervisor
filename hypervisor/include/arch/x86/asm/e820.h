/*
 * Copyright (C) 2018-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef E820_H
#define E820_H
#include <types.h>

/**
 * @defgroup hwmgmt_memory hwmgmt.memory
 * @ingroup hwmgmt
 * @brief Memory management
 *
 * This module implements the memory operations in hypervisor. ACRN hypervisor gets initial memory map from firmware
 * and owns the real physical memory. The hypervisor virtualizes the physical memory so the VM can manage its own
 * contiguous physical memory. The hypervisor enables hardware virtualization features like virtual-processor
 * identifiers (VPID) and extended page-table mechanism (EPT) to translate guest physical address into host physical
 * address, establishes EPT page tables for Service VM and User VM, and provide EPT page tables operation interfaces
 * to others.
 *
 * @{
 */

/**
 * @file
 * @brief E820 memory map management functions
 *
 * This file contains the struct and declarations of functions that manage the E820 memory map, including
 * initialization from EFI or multiboot, allocation of memory regions, and retrieval of memory information. The
 * hypervisor will get memory info from its MMU setup and its memory will be hide from Service VM.
 */

/* E820 memory types */
#define E820_TYPE_RAM		1U	/* EFI 1, 2, 3, 4, 5, 6, 7 */
#define E820_TYPE_RESERVED	2U
/* EFI 0, 11, 12, 13 (everything not used elsewhere) */
#define E820_TYPE_ACPI_RECLAIM	3U	/* EFI 9 */
#define E820_TYPE_ACPI_NVS	4U	/* EFI 10 */
#define E820_TYPE_UNUSABLE	5U	/* EFI 8 */

#define E820_MAX_ENTRIES	64U

#define MEM_SIZE_MAX (~0UL)

/**
 * @brief E820 memory map entry.
 *
 * This structure defines a single entry in an E820 memory map.
 *
 * @remark This structure shall be packed.
 */
struct e820_entry {
	uint64_t baseaddr; /**< The base address of the memory range. */
	uint64_t length; /**< The length of the memory range. */
	uint32_t type; /**< The type of memory region. */
} __packed;

struct mem_range {
	uint64_t mem_bottom;
	uint64_t mem_top;
	uint64_t total_mem_size;
};

/* HV read multiboot header to get E820 entries info and calc total RAM info */
void init_e820(void);

uint64_t e820_alloc_memory(uint64_t size_arg, uint64_t max_addr);
uint64_t get_e820_ram_size(void);
/* get total number of the E820 entries */
uint32_t get_e820_entries_count(void);

/* get the e802 entiries */
const struct e820_entry *get_e820_entry(void);

#endif /* E820_H */

/**
 * @}
 */