/*
 * Copyright (C) 2020-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef EFI_H
#define EFI_H

/**
 * @addtogroup boot
 *
 * @{
 */

/**
 * @file
 * @brief efi_info struct definition
 *
 * The file provides the definition of efi_info struct, which is a member of
 * acrn_boot_info struct and zero_page struct.
 */

#ifdef CONFIG_MULTIBOOT2

/* EFI Memory Attribute values: */
#define EFI_MEMORY_UC			0x0000000000000001UL	/* uncached */
#define EFI_MEMORY_WC			0x0000000000000002UL	/* write-coalescing */
#define EFI_MEMORY_WT			0x0000000000000004UL	/* write-through */
#define EFI_MEMORY_WB			0x0000000000000008UL	/* write-back */
/*
#define EFI_MEMORY_UCE			0x0000000000000010UL
#define EFI_MEMORY_WP			0x0000000000001000UL
#define EFI_MEMORY_RP			0x0000000000002000UL
#define EFI_MEMORY_XP			0x0000000000004000UL
#define EFI_MEMORY_NV			0x0000000000008000UL
#define EFI_MEMORY_MORE_RELIABLE 	0x0000000000010000UL
#define EFI_MEMORY_RO			0x0000000000020000UL
#define EFI_MEMORY_SP			0x0000000000040000UL
#define EFI_MEMORY_CPU_CRYPTO		0x0000000000080000UL
#define EFI_MEMORY_RUNTIME		0x8000000000000000UL
*/

enum efi_memory_type {
	EFI_RESERVED_MEMORYTYPE,
	EFI_LOADER_CODE,
	EFI_LOADER_DATA,
	EFI_BOOT_SERVICES_CODE,
	EFI_BOOT_SERVICES_DATA,
	EFI_RUNTIME_SERVICES_CODE,
	EFI_RUNTIME_SERVICES_DATA,
	EFI_CONVENTIONAL_MEMORY,
	EFI_UNUSABLE_MEMORY,
	EFI_ACPI_RECLAIM_MEMORY,
	EFI_ACPI_MEMORY_NVS,
	EFI_MEMORYMAPPED_IO,
	EFI_MEMORYMAPPED_IOPORTSPACE,
	EFI_PALCODE,
	EFI_PERSISTENT_MEMORY,
	EFI_MAX_MEMORYTYPE
};

struct efi_memory_desc {
	uint32_t type;
	uint32_t pad;
	uint64_t phys_addr;
	uint64_t virt_addr;
	uint64_t num_pages;
	uint64_t attribute;
};

#endif /* CONFIG_MULTIBOOT2 */

/**
 * @brief EFI information struct defined by Linux kernel
 *
 * The `efi_info` structure contains information related to the Extensible Firmware Interface (EFI), which
 * is used during the boot process of the Linux kernel. The struct is referenced as part of the Linux boot
 * params(commonly known as the 'zeropage'), starting from the zeropage offset 0x1c0. This structure includes
 * fields for loader signature, system table addresses, memory descriptor details, and memory map information.
 *
 * @remark This struct is x86 specific.
 * @remark This structure shall be packed.
 */
struct efi_info {
	uint32_t loader_signature;  /**< Signature identifying the EFI loader. Zeropage Offset: 0x1c0 */
	uint32_t systab;            /**< Address of the EFI system table. Zeropage Offset: 0x1c4 */
	uint32_t memdesc_size;      /**< Size of each memory descriptor entry. Zeropage Offset: 0x1c8 */
	uint32_t memdesc_version;   /**< Version of the memory descriptor format. Zeropage Offset: 0x1cc */
	uint32_t memmap;            /**< Address of the memory map. Zeropage Offset: 0x1d0 */
	uint32_t memmap_size;       /**< Total size of the memory map. Zeropage Offset: 0x1d4 */
	uint32_t systab_hi;         /**< High 32 bits of the EFI system table address. Zeropage Offset: 0x1d8 */
	uint32_t memmap_hi;         /**< High 32 bits of the memory map address. Zeropage Offset: 0x1dc */
} __packed;

/**
 * @}
 */
#endif
