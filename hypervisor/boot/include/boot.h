/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef BOOT_H
#define BOOT_H

#include <multiboot_std.h>
#include <efi.h>
#include <vm_configurations.h>
#include <asm/boot/ld_sym.h>

/**
 * @defgroup boot ACRN boot
 * @brief Structure definitions and functions for booting and relocating the ACRN hypervisor.
 *
 * The component defines and initializes the data structure for ACRN HV boot information, which follows
 * the multiboot protocol. It provides the image location API, including scenarios where the HV is relocated
 * by the bootloader and where the AP trampoline code is relocated by the HV.
 *
 * @{
 */

/**
 * @file
 * @brief Structure definitions and API for HV boot
 *
 * The file defines ACRN boot info structure and provides API to access ACRN boot info, including how to
 * get boot module, how to calculate hv image position and size, etc.
 */

/**
 * @brief Maximum Number of Memory Map Entries
 *
 * This macro defines the maximum number of memory map entries that can be
 * handled by the system. It is used to limit the size of memory map arrays,
 * ensuring that the system does not exceed its capacity to manage memory
 * regions.
 */
#define MAX_MMAP_ENTRIES		128U

/**
 * @brief Maximum Size for Boot Arguments
 *
 * This macro defines the maximum size, in bytes, for boot arguments.
 */
#define MAX_BOOTARGS_SIZE		2048U
/**
 * @brief Maximum Size for Bootloader Name
 *
 * This macro defines the maximum size, in bytes, for the bootloader name.
 */
#define MAX_LOADER_NAME_SIZE		32U
/**
 * @brief Maximum Size for Protocol Name
 *
 * This macro defines the maximum size, in bytes, for the protocol name.
 */
#define MAX_PROTOCOL_NAME_SIZE		16U
/**
 * @brief Maximum Size for Module Strings
 *
 * This macro defines the maximum size, in bytes, for module strings.
 */
#define MAX_MOD_STRING_SIZE		2048U
/**
 * @brief Maximum Length for Module Tag
 *
 * This macro defines the maximum length, in bytes, for module tag.
 */
#define MAX_MOD_TAG_LEN			32U

/**
 * @brief Maximum Number of Modules
 *
 * This macro defines the maximum number of modules that can be handled by the
 * ACRN hypervisor. It calculates the total number of modules based on the number of
 * pre-launched VMs and service VM. Each pre-launched VM includes at most 3 modules
 * like kernel,ramdisk, and ACPI tables, while the service VM includes at most 2 modules
 * for kernel and ramdisk.
 *
 * The formula used is:
 * - 3 modules at most per pre-launched VM (kernel, ramdisk, ACPI)
 * - 2 modules at most per service VM (kernel, ramdisk), service VM number can only be 0 or 1.
 *
 * The value is calculated as `(3U * PRE_VM_NUM + 2U * SERVICE_VM_NUM)`.
 */
#define MAX_MODULE_NUM			(3U * PRE_VM_NUM + 2U * SERVICE_VM_NUM)

/**
 * @brief Fixed Size for vACPI Module
 *
 * This macro defines the fixed size for the virtual ACPI (vACPI) module. The
 * vACPI module is used to store ACPI tables and related data, which are essential
 * for system configuration and power management.
 *
 * The value is defined as `MEM_1M`, representing 1 megabyte which can accommodate
 * the necessary ACPI information.
 */
#define ACPI_MODULE_SIZE		MEM_1M

/**
 * @brief ACRN Boot Information Module Structure
 *
 * This structure represents a module in the ACRN boot information (ABI). It
 * contains details about a module loaded by the bootloader, including its
 * start address, size, and a descriptive string. This information is used
 * by the hypervisor to manage and utilize the modules during system
 * initialization and operation.
 */
struct abi_module {
	void *start;                            /**< Start address of the module (HVA). */
	uint32_t size;                          /**< Size of the module in bytes. */
	const char string[MAX_MOD_STRING_SIZE]; /**< Descriptive string for the module. */
};

/* ABI memory map types, compatible to Multiboot/Multiboot2/E820; */
/**
 * @brief Memory Map Type: RAM
 *
 * This macro defines a memory map type indicating that a memory region is usable
 * RAM. Regions marked with this type are available for use by the operating system
 * or hypervisor.
 *
 * The value is represented by `1U`.
 */
#define MMAP_TYPE_RAM		1U
/**
 * @brief Memory Map Type: Reserved
 *
 * This macro defines a memory map type indicating that a memory region is reserved.
 * Regions marked with this type are not available for general use and may be reserved
 * for system or hardware purposes.
 *
 * The value is represented by `2U`.
 */
#define MMAP_TYPE_RESERVED	2U
/**
 * @brief Memory Map Type: ACPI Reclaimable
 *
 * This macro defines a memory map type indicating that a memory region is ACPI
 * reclaimable. Regions marked with this type contain ACPI tables needed during
 * boot but can be reclaimed and used by the operating system after initialization.
 *
 * The value is represented by `3U`.
 */
#define MMAP_TYPE_ACPI_RECLAIM	3U
/**
 * @brief Memory Map Type: ACPI NVS
 *
 * This macro defines a memory map type indicating that a memory region is ACPI
 * Non-Volatile Storage (NVS). Regions marked with this type are used by the firmware
 * to store data that must be preserved across reboots.
 *
 * The value is represented by `4U`.
 */
#define MMAP_TYPE_ACPI_NVS	4U
/**
 * @brief Memory Map Type: Unusable
 *
 * This macro defines a memory map type indicating that a memory region is unusable.
 * Regions marked with this type are known to be defective or unreliable and should
 * not be used by the operating system or hypervisor.
 *
 * The value is represented by `5U`.
 */
#define MMAP_TYPE_UNUSABLE	5U

/**
 * @brief ACRN Boot Information Memory Map Entry Structure
 *
 * This structure represents a memory map entry in the ACRN boot information (ABI).
 * It provides details about a specific memory region, including its base address,
 * length, and type. This information is used by the hypervisor to manage memory
 * resources and understand the system's memory layout.
 */
struct abi_mmap {
	uint64_t baseaddr; /**< Base address of the memory region. */
	uint64_t length;   /**< Length of the memory region in bytes. */
	uint32_t type;     /**< Type of the memory region. */
};

/**
 * @brief ACRN Boot Information Structure
 *
 * This structure encapsulates the boot information required by the ACRN hypervisor.
 * It includes details about the boot protocol, command line arguments, loader name,
 * modules, memory map, ACPI, and UEFI information. This comprehensive set of data
 * is used by the hypervisor to initialize and manage the system effectively.
 */
struct acrn_boot_info {
	char protocol_name[MAX_PROTOCOL_NAME_SIZE];     /**< Boot protocol name. */
	const char cmdline[MAX_BOOTARGS_SIZE]; 	        /**< Command line arguments. */
	const char loader_name[MAX_LOADER_NAME_SIZE];		/**< Bootloader name. */

	uint32_t mods_count;                            /**< Number of loaded modules. */
	struct abi_module mods[MAX_MODULE_NUM];         /**< Array of loaded modules. */

	uint32_t mmap_entries;                          /**< Number of memory map entries. */
	struct abi_mmap mmap_entry[MAX_MMAP_ENTRIES];   /**< Array of memory map entries. */

	const void *acpi_rsdp_va;                       /**< Virtual address of ACPI RSDP. */
	struct efi_info uefi_info;                      /**< UEFI information. */
#ifdef HV_DEBUG
	struct multiboot_framebuffer_info framebuffer_info; /**< framebuffer basic information. */
	struct multiboot_framebuffer_color framebuffer_color; /**< framebuffer color information. */
#endif
};

/**
 * @brief Check for UEFI Boot
 *
 * This function checks whether the system was booted using UEFI (Unified Extensible
 * Firmware Interface). It examines the UEFI system table pointers in the ACRN boot
 * information structure to determine if UEFI booting was used. If either the low
 * or high part of the system table pointer is non-zero, it indicates that the system
 * was booted using UEFI.
 *
 * @param[in] abi A pointer to the ACRN boot information structure, which contains
 *            UEFI-related information.
 *
 * @return Returns true if the system was booted using UEFI, false otherwise.
 */
static inline bool boot_from_uefi(struct acrn_boot_info *abi)
{
	return !((abi->uefi_info.systab == 0U) && (abi->uefi_info.systab_hi == 0U));
}

const struct abi_module *get_mod_by_tag(const struct acrn_boot_info *abi, const char *tag);

int32_t init_multiboot_info(uint32_t *registers);

void init_acrn_boot_info(uint32_t *registers);
int32_t sanitize_acrn_boot_info(struct acrn_boot_info *abi);
struct acrn_boot_info *get_acrn_boot_info(void);

/**
 * @brief Get Hypervisor Image Load Address Delta
 *
 * This function calculates the delta between a predefined start address
 * (`CONFIG_HV_RAM_START`) and the actual load address of the hypervisor image.
 * It uses inline assembly to determine the current instruction pointer and
 * compute the offset from the expected start address.
 *
 * @return The calculated delta between `CONFIG_HV_RAM_START` and the actual
 *         load address of the hypervisor image.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint64_t get_hv_image_delta(void)
{
	uint64_t addr;

	asm volatile (" call 0f\n"
		"0: pop %%rax\n"
		"	sub $0b, %%rax\n"
		"	mov %%rax, %0\n"
		: "=m" (addr)
		:
		: "%rax");

	return addr;
}

/**
 * @brief Get Hypervisor Image Base Address
 *
 * This function calculates the actual load HVA of the hypervisor image. It computes this
 * by adding the delta obtained from get_hv_image_delta() to the predefined
 * start address `CONFIG_HV_RAM_START`.
 *
 * @return The actual load address (HVA) of the hypervisor image.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint64_t get_hv_image_base(void)
{
	return (get_hv_image_delta() + CONFIG_HV_RAM_START);
}

/**
 * @brief Get Hypervisor Image Size
 *
 * This function calculates the size of the hypervisor image in memory. It uses
 * linker-defined symbols `ld_ram_start` and `ld_ram_end` to determine the start
 * and end addresses of the hypervisor image, respectively. The size is computed
 * as the difference between these two addresses.
 *
 * @return The size of the hypervisor image in bytes.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark The symbols `ld_ram_start` and `ld_ram_end` is defined in the linker
 *       script file `link_ram.ld.in` to mark the boundaries of the hypervisor image in memory.
 */
static inline uint64_t get_hv_image_size(void)
{
	return (uint64_t)(&ld_ram_end - &ld_ram_start);
}

/**
 * @}
 */
#endif	/* BOOT_H */
