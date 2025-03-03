/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef E820_H
#define E820_H
#include <types.h>
#include <config.h>

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

/**
 * @defgroup E820_TYPE E820 memory types.
 *
 * These macros define the types of memory regions in an E820 memory map. The types are used to describe the
 * characteristics of the memory regions, such as whether they are usable, reserved, or unusable.
 *
 * Refer to Chapter 15.3 UEFI GerMemoryMap() Boot Services Functions, ACPI Specification for more details about
 * the mappings between EFI type and ACPI address range type.
 *
 * @{
 */

/**
 * @brief RAM memory type for E820 entry.
 *
 * This type indicates memory regions that can be used by the operating system.
 * In UEFI, this corresponds to EfiConventionalMemory, EfiLoaderCode, EfiLoaderData,
 * EfiBootServicesCode, EfiBootServicesData, or any memory with the EFI_MEMORY_WB attribute.
 */
#define E820_TYPE_RAM		1U
/**
 * @brief Reserved memory type for E820 entry.
 *
 * This type indicates memory regions that are reserved and must not be used by the OS.
 * In UEFI, this corresponds to EfiReservedMemoryType, EfiRuntimeServicesCode,
 * EfiRuntimeServicesData, EfiMemoryMappedIO, EfiMemoryMappedIOPortSpace, and other
 * types not explicitly mapped elsewhere.
 */
#define E820_TYPE_RESERVED	2U
/**
 * @brief ACPI reclaim memory type for E820 entry.
 *
 * This type maps to EFI type 9.
 */
#define E820_TYPE_ACPI_RECLAIM	3U
/**
 * @brief ACPI NVS memory type for E820 entry.
 *
 * This type maps to EFI type 10.
 */
#define E820_TYPE_ACPI_NVS	4U
/**
 * @brief Unusable memory type for E820 entry.
 *
 * This type maps to EFI type 8.
 */
#define E820_TYPE_UNUSABLE	5U
/**
 * @}
 */

#define E820_MAX_ENTRIES	128U  /**< Maximum number of E820 entries supported by hypervisor */

#define MEM_SIZE_MAX (~0UL) /**< Maximum address below which memory can be allocated */

#define MEM_TOP_ADDRESS		max(CONFIG_MEMORY_TOP_ADDRESS, MEM_4G) /**< Top address of RAM */

/**
 * @brief E820 memory map entry.
 *
 * This structure defines a single entry in an E820 memory map, describing a contiguous
 * block of physical memory with its base address, length, and type.
 *
 * @remark This structure shall be packed.
 */
struct e820_entry {
	uint64_t baseaddr; /**< The base address of the memory range. */
	uint64_t length; /**< The length of the memory range. */
	uint32_t type; /**< The type of memory region. */
} __packed;

/**
 * @brief Definition of memory range information.
 */
struct mem_range {
	uint64_t mem_bottom;  /**< Bottom address of this memory range. */
	uint64_t mem_top;  /**< Top address of this memory range. */
	uint64_t total_mem_size;  /**< Bytes size of this memory range. */
};

/* HV read multiboot header to get E820 entries info and calc total RAM info */
void init_e820(void);

uint64_t e820_alloc_memory(uint64_t size_arg, uint64_t max_addr);
/* get total number of the E820 entries */
uint32_t get_e820_entries_count(void);

/* get the E820 entiries */
const struct e820_entry *get_e820_entry(void);

#endif /* E820_H */

/**
 * @}
 */