/*
 * Copyright (C) 2021-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*   Copyright (C) 1999,2003,2007,2008,2009,2010  Free Software Foundation, Inc.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *  sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL ANY
 *  DEVELOPER OR DISTRIBUTOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 *  WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
 *  IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef MULTIBOOT_STD_H
#define MULTIBOOT_STD_H
/**
 * @addtogroup boot
 *
 * @{
 */

/**
 * @file
 * @brief MACRO and structure definitions for multiboot/multiboot2 standard protocol
 *
 * The file provides MACRO and structure definitions for multiboot/multiboot2 standard protocol.
 * The multiboot and multiboot2 protocol can be referenced below:
 * https://www.gnu.org/software/grub/manual/multiboot/multiboot.html
 * https://www.gnu.org/software/grub/manual/multiboot2/multiboot.html
 */

/**
 * @brief Multiboot Header Magic Number
 *
 * This macro defines the magic number used to identify a Multiboot header. It is
 * a required field in the Multiboot header structure and is used by Multiboot-compliant
 * bootloaders to recognize the header as conforming to the Multiboot specification.
 *
 * The magic number is a constant value that must be set to `0x1BADB002U`. This value
 * is specifically chosen to help ensure that the header is correctly identified and
 * processed by the bootloader.
 */
#define	MULTIBOOT_HEADER_MAGIC				0x1BADB002U
/**
 * @brief Multiboot Information Magic Number
 *
 * This macro defines the magic number used to identify a Multiboot information
 * structure. It is a key value passed by a Multiboot-compliant bootloader to the
 * operating system kernel to indicate that the boot information provided is valid
 * and conforms to the Multiboot specification.
 *
 * The magic number is a constant value that must be set to `0x2BADB002U`. This value
 * is specifically chosen to help ensure that the information structure is correctly
 * identified and processed by the operating system.
 */
#define	MULTIBOOT_INFO_MAGIC				0x2BADB002U

/* MULTIBOOT HEADER FLAGS */
/**
 * @brief Multiboot Header Need Memory Information Flag
 *
 * This macro defines a flag used in the Multiboot header to indicate that the
 * operating system kernel requires memory information from the bootloader. When
 * this flag is set, the bootloader is expected to provide details about the
 * system's memory layout, which is essential for the kernel's memory management
 * and initialization processes.
 *
 * The flag is represented by the value `0x00000002U`. It is used as part of the
 * `flags` field in the Multiboot header to specify the kernel's requirements.
 */
#define	MULTIBOOT_HEADER_NEED_MEMINFO			0x00000002U

/* Unused MULTIBOOT HEADER FLAGS:
 * #define MULTIBOOT_HEADER_PAGE_ALIGN			0x00000001U
 * #define MULTIBOOT_HEADER_VIDEO_MODE			0x00000004U
 * #define MULTIBOOT_HEADER_AOUT_KLUDGE			0x00010000U
 */

/* MULTIBOOT INFO FLAGS */
/**
 * @brief Multiboot Information Has Command Line Flag
 *
 * This macro defines a flag used in the Multiboot information structure to indicate
 * that a command line is available. When this flag is set, the bootloader provides
 * a command line string that the operating system kernel can use for configuration
 * and initialization purposes.
 *
 * The flag is represented by the value `0x00000004U`. It is used as part of the
 * `mi_flags` field in the Multiboot information structure to specify the presence
 * of a command line.
 */
#define	MULTIBOOT_INFO_HAS_CMDLINE			0x00000004U
/**
 * @brief Multiboot Information Has Modules Flag
 *
 * This macro defines a flag used in the Multiboot information structure to indicate
 * that one or more modules are available. When this flag is set, the bootloader provides
 * information about additional modules that have been loaded, which the operating system
 * kernel can use for various purposes, such as loading drivers or additional components.
 *
 * The flag is represented by the value `0x00000008U`. It is used as part of the
 * `mi_flags` field in the Multiboot information structure to specify the presence
 * of modules.
 */
#define	MULTIBOOT_INFO_HAS_MODS				0x00000008U
/**
 * @brief Multiboot Information Has Memory Map Flag
 *
 * This macro defines a flag used in the Multiboot information structure to indicate
 * that a memory map is available. When this flag is set, the bootloader provides
 * detailed information about the system's memory layout, including the types and
 * attributes of each memory region. This information is crucial for the operating
 * system kernel to manage memory effectively.
 *
 * The flag is represented by the value `0x00000040U`. It is used as part of the
 * `mi_flags` field in the Multiboot information structure to specify the presence
 * of a memory map.
 */
#define	MULTIBOOT_INFO_HAS_MMAP				0x00000040U
/**
 * @brief Multiboot Information Has Loader Name Flag
 *
 * This macro defines a flag used in the Multiboot information structure to indicate
 * that the name of the bootloader is available. When this flag is set, the bootloader
 * provides a string containing its name, which can be used by the operating system
 * kernel for informational or diagnostic purposes.
 *
 * The flag is represented by the value `0x00000200U`. It is used as part of the
 * `mi_flags` field in the Multiboot information structure to specify the presence
 * of the bootloader's name.
 */
#define	MULTIBOOT_INFO_HAS_LOADER_NAME			0x00000200U

/* Unused MULTIBOOT INFO FLAGS:
 * #define MULTIBOOT_INFO_HAS_MEMINFO			0x00000001U
 * #define MULTIBOOT_INFO_HAS_BOOTDEV			0x00000002U
 * #define MULTIBOOT_INFO_HAS_AOUT_SYMS			0x00000010U
 * #define MULTIBOOT_INFO_HAS_ELF_SHDR			0X00000020U
 * #define MULTIBOOT_INFO_HAS_DRIVES			0x00000080U
 * #define MULTIBOOT_INFO_HAS_CONFIG_TABLE		0x00000100U
 * #define MULTIBOOT_INFO_HAS_APM_TABLE			0x00000400U
 * #define MULTIBOOT_INFO_HAS_VBE_INFO			0x00000800U
 * #define MULTIBOOT_INFO_HAS_FB_INFO			0x00001000U
 */

#ifndef ASSEMBLER

/**
 * @brief Multiboot Header Structure
 *
 * This structure defines the multiboot header used by boot loaders to boot
 * operating systems in a standardized way. It includes fields for magic number,
 * feature flags, and checksum, as well as optional fields for specific boot
 * configurations such as a.out kludge and video mode settings. The checksum
 * ensures that the sum of the first three fields is zero modulo 2^32, which
 * is a requirement for multiboot compliance.
 *
 * @remark This structure shall be packed.
 */
struct multiboot_header {
	uint32_t magic;	       /**< Must be MULTIBOOT_MAGIC - identifies the multiboot header. */
	uint32_t flags;        /**< Feature flags indicating supported options and requirements. */
	uint32_t checksum;     /**< Ensures the sum of the first three fields is zero modulo 2^32. */

	/**
	 * @defgroup MULTIBOOT_AOUT_KLUDGE Fields related to MULTIBOOT_HEADER_AOUT_KLUDGE
	 * @brief Valid if MULTIBOOT_HEADER_AOUT_KLUDGE flag is set in the Multiboot Header flags.
	 *
	 * These fields specify memory addresses related to loading the kernel and
	 * setting up the execution environment when the a.out kludge is used.
	 *
	 * @{
	 */
	uint32_t header_addr;  /**< Address of the multiboot header itself. */
	uint32_t load_addr;    /**< Address where the kernel image should be loaded. */
	uint32_t load_end_addr;/**< Address marking the end of the loaded kernel image. */
	uint32_t bss_end_addr; /**< Address marking the end of the BSS segment. */
	uint32_t entry_addr;   /**< Entry point address for the kernel. */
	/**
	 * @}
	 */

	/**
	 * @defgroup MULTIBOOT_VIDEO_MODE Fields related to MULTIBOOT_HEADER_VIDEO_MODE
	 * @brief Valid if MULTIBOOT_HEADER_VIDEO_MODE flag is set in the Multiboot Header flags.
	 *
	 * These fields specify the desired video mode settings for the boot loader
	 * to configure the display environment.
	 *
	 * @{
	 */
	uint32_t mode_type;    /**< Type of video mode to be set. */
	uint32_t width;        /**< Width of the display in pixels. */
	uint32_t height;       /**< Height of the display in pixels. */
	uint32_t depth;        /**< Color depth of the display in bits per pixel. */
	/**
	 * @}
	 */
} __packed;

/**
 * @brief Multiboot Memory Available Type
 *
 * This macro defines a constant used in the Multiboot memory map to indicate
 * that a memory region is available for use by the operating system. Memory
 * regions marked with this type are considered usable RAM and can be allocated
 * for various purposes, such as loading the kernel, user applications, or other
 * data structures.
 *
 * The constant is represented by the value `1U`. It is used in the `type` field
 * of memory map entries within the Multiboot information structure to specify
 * that the memory region is available.
 */
#define MULTIBOOT_MEMORY_AVAILABLE			1U
/**
 * @brief Multiboot Memory Reserved Type
 *
 * This macro defines a constant used in the Multiboot memory map to indicate
 * that a memory region is reserved and should not be used by the operating system.
 * Memory regions marked with this type are typically reserved for system use or
 * hardware devices and should not be allocated for general-purpose use by the OS.
 *
 * The constant is represented by the value `2U`. It is used in the `type` field
 * of memory map entries within the Multiboot information structure to specify
 * that the memory region is reserved.
 */
#define MULTIBOOT_MEMORY_RESERVED			2U
/**
 * @brief Multiboot Memory ACPI Reclaimable Type
 *
 * This macro defines a constant used in the Multiboot memory map to indicate
 * that a memory region is ACPI reclaimable. Memory regions marked with this type
 * contain ACPI (Advanced Configuration and Power Interface) tables that are needed
 * during the boot process but can be reclaimed and used by the operating system
 * after the tables have been processed.
 *
 * The constant is represented by the value `3U`. It is used in the `type` field
 * of memory map entries within the Multiboot information structure to specify
 * that the memory region is ACPI reclaimable.
 */
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE		3U
/**
 * @brief Multiboot Memory Non-Volatile Storage (NVS) Type
 *
 * This macro defines a constant used in the Multiboot memory map to indicate
 * that a memory region is designated as Non-Volatile Storage (NVS). Memory regions
 * marked with this type are used by the firmware to store data that must be preserved
 * across reboots, such as certain configuration settings or system state information.
 * The operating system should not use or overwrite these regions.
 *
 * The constant is represented by the value `4U`. It is used in the `type` field
 * of memory map entries within the Multiboot information structure to specify
 * that the memory region is NVS.
 */
#define MULTIBOOT_MEMORY_NVS				4U
/**
 * @brief Multiboot Memory Bad RAM Type
 *
 * This macro defines a constant used in the Multiboot memory map to indicate
 * that a memory region is considered bad RAM. Memory regions marked with this
 * type are known to be defective or unreliable and should not be used by the
 * operating system for any purpose. The presence of bad RAM entries helps the
 * operating system avoid using these regions, preventing potential data corruption
 * or system instability.
 *
 * The constant is represented by the value `5U`. It is used in the `type` field
 * of memory map entries within the Multiboot information structure to specify
 * that the memory region is bad RAM.
 */
#define MULTIBOOT_MEMORY_BADRAM				5U
/**
 * @brief Multiboot Memory Map Entry Structure
 *
 * This structure defines an entry in the memory map provided by a Multiboot-compliant
 * bootloader. Each entry describes a contiguous block of physical memory, including
 * its base address, length, and type.
 *
 * @remark This structure shall be packed.
 */
struct multiboot_mmap {
	uint32_t size;       /**< Size of the structure, excluding this field. */
	uint64_t baseaddr;   /**< Base address of the memory region. */
	uint64_t length;     /**< Length of the memory region in bytes. */
	uint32_t type;       /**< Type of the memory region. */
} __packed;

/**
 * @brief Multiboot Module Structure
 *
 * This structure defines a module loaded by a Multiboot-compliant bootloader.
 * Each module entry provides information about the module's memory location
 * and an optional string description.
 *
 * @remark This structure shall be packed.
 */
struct multiboot_module {
	uint32_t mm_mod_start; /**< Starting address of the module in memory. */
	uint32_t mm_mod_end;   /**< Ending address of the module in memory. */
	uint32_t mm_string;    /**< Address of a null-terminated string description. */
	uint32_t mm_reserved;  /**< Reserved field, should be set to zero. */
} __packed;


/**
 * @brief Multiboot Information Structure
 *
 * This structure provides information from a Multiboot-compliant bootloader to
 * the operating system kernel. The fields in this structure are conditionally
 * valid based on the flags set in `mi_flags`.
 *
 * @remark This structure shall be packed.
 */
struct multiboot_info {
	uint32_t mi_flags;                   /**< Flags indicating valid fields. */

	/**
	 * @defgroup MULTIBOOT_INFO_HAS_MEMINFO Fields related to MULTIBOOT_INFO_HAS_MEMINFO
	 * @brief Valid if MULTIBOOT_INFO_HAS_MEMINFO flag is set in the Multiboot Info mi_flags.
	 *
	 * @{
	 */
	uint32_t mi_mem_lower;               /**< Lower memory in kilobytes. */
	uint32_t mi_mem_upper;               /**< Upper memory in kilobytes. */
	/**
	 * @}
	 */

	/**
	 * @defgroup MULTIBOOT_INFO_HAS_BOOTDEV Fields related to MULTIBOOT_INFO_HAS_BOOTDEV
	 * @brief Valid if MULTIBOOT_INFO_HAS_BOOTDEV flag is set in the Multiboot Info mi_flags.
	 *
	 * @{
	 */
	uint8_t  mi_boot_device_part3;       /**< Boot device partition 3. */
	uint8_t  mi_boot_device_part2;       /**< Boot device partition 2. */
	uint8_t  mi_boot_device_part1;       /**< Boot device partition 1. */
	uint8_t  mi_boot_device_drive;       /**< Boot device drive number. */
	/**
	 * @}
	 */

	/**
	 * @defgroup MULTIBOOT_INFO_HAS_CMDLINE Fields related to MULTIBOOT_INFO_HAS_CMDLINE
	 * @brief Valid if MULTIBOOT_INFO_HAS_CMDLINE flag is set in the Multiboot Info mi_flags.
	 *
	 * @{
	 */
	uint32_t mi_cmdline;                 /**< Address of kernel command line. */
	/**
	 * @}
	 */

	/**
	 * @defgroup MULTIBOOT_INFO_HAS_MODS Fields related to MULTIBOOT_INFO_HAS_MODS
	 * @brief Valid if MULTIBOOT_INFO_HAS_MODS flag is set in the Multiboot Info mi_flags.
	 *
	 * @{
	 */
	uint32_t mi_mods_count;              /**< Number of modules loaded. */
	uint32_t mi_mods_addr;               /**< Address of first module structure. */
	/**
	 * @}
	 */

	/**
	 * @defgroup MULTIBOOT_INFO_HAS_ELF_SHDR Fields related to MULTIBOOT_INFO_HAS_ELF_SHDR
	 * @brief Valid if MULTIBOOT_INFO_HAS_ELF_SHDR flag is set in the Multiboot Info mi_flags.
	 *
	 * @{
	 */
	uint32_t mi_elfshdr_num;             /**< Number of ELF section headers. */
	uint32_t mi_elfshdr_size;            /**< Size of each ELF section header. */
	uint32_t mi_elfshdr_addr;            /**< Address of ELF section headers. */
	uint32_t mi_elfshdr_shndx;           /**< ELF section header string table index. */
	/**
	 * @}
	 */

	/**
	 * @defgroup MULTIBOOT_INFO_HAS_MMAP Fields related to MULTIBOOT_INFO_HAS_MMAP
	 * @brief Valid if MULTIBOOT_INFO_HAS_MMAP flag is set in the Multiboot Info mi_flags.
	 *
	 * @{
	 */
	uint32_t mi_mmap_length;             /**< Length of the memory map. */
	uint32_t mi_mmap_addr;               /**< Address of the memory map. */
	/**
	 * @}
	 */

	/**
	 * @defgroup MULTIBOOT_INFO_HAS_DRIVES Fields related to MULTIBOOT_INFO_HAS_DRIVES
	 * @brief Valid if MULTIBOOT_INFO_HAS_DRIVES flag is set in the Multiboot Info mi_flags.
	 *
	 * @{
	 */
	uint32_t mi_drives_length;           /**< Length of the drive information. */
	uint32_t mi_drives_addr;             /**< Address of the drive information. */
	/**
	 * @}
	 */

	/**
	 * @defgroup MULTIBOOT_INFO_HAS_CONFIG_TABLE Fields related to MULTIBOOT_INFO_HAS_CONFIG_TABLE
	 * @brief Valid if MULTIBOOT_INFO_HAS_CONFIG_TABLE flag is set in the Multiboot Info mi_flags.
	 *
	 * @{
	 */
	uint32_t unused_mi_config_table;     /**< Reserved for future use. */
	/**
	 * @}
	 */

	/**
	 * @defgroup MULTIBOOT_INFO_HAS_LOADER_NAME Fields related to MULTIBOOT_INFO_HAS_LOADER_NAME
	 * @brief Valid if MULTIBOOT_INFO_HAS_LOADER_NAME flag is set in the Multiboot Info mi_flags.
	 *
	 * @{
	 */
	uint32_t mi_loader_name;             /**< Address of bootloader name. */
	/**
	 * @}
	 */

	/**
	 * @defgroup MULTIBOOT_INFO_HAS_APM_TABLE Fields related to MULTIBOOT_INFO_HAS_APM_TABLE
	 * @brief Valid if MULTIBOOT_INFO_HAS_APM_TABLE flag is set in the Multiboot Info mi_flags.
	 *
	 * @{
	 */
	uint32_t unused_mi_apm_table;        /**< Reserved for future use. */
	/**
	 * @}
	 */

	/**
	 * @defgroup MULTIBOOT_INFO_HAS_VBE_INFO Fields related to MULTIBOOT_INFO_HAS_VBE_INFO
	 * @brief Valid if MULTIBOOT_INFO_HAS_VBE_INFO flag is set in the Multiboot Info mi_flags.
	 *
	 * @{
	 */
	uint32_t unused_mi_vbe_control_info; /**< Reserved for future use. */
	uint32_t unused_mi_vbe_mode_info;    /**< Reserved for future use. */
	uint16_t unused_mi_vbe_mode;         /**< Reserved for future use. */
	uint16_t unused_mi_vbe_interface_seg;/**< Reserved for future use. */
	uint16_t unused_mi_vbe_interface_off;/**< Reserved for future use. */
	uint16_t unused_mi_vbe_interface_len;/**< Reserved for future use. */
	/**
	 * @}
	 */
} __packed;
#endif

#ifdef CONFIG_MULTIBOOT2
#define MULTIBOOT2_HEADER_ALIGN				8

#define MULTIBOOT2_HEADER_MAGIC				0xe85250d6U

/*  This should be in %eax. */
#define MULTIBOOT2_INFO_MAGIC				0x36d76289U

/*  Alignment of the multiboot info structure. */
#define MULTIBOOT2_INFO_ALIGN				0x00000008U

/*  Flags set in the 'flags' member of the multiboot header. */

#define MULTIBOOT2_TAG_ALIGN				8U
#define MULTIBOOT2_TAG_TYPE_END				0U
#define MULTIBOOT2_TAG_TYPE_CMDLINE			1U
#define MULTIBOOT2_TAG_TYPE_BOOT_LOADER_NAME		2U
#define MULTIBOOT2_TAG_TYPE_MODULE			3U
#define MULTIBOOT2_TAG_TYPE_BASIC_MEMINFO		4U
#define MULTIBOOT2_TAG_TYPE_BOOTDEV			5U
#define MULTIBOOT2_TAG_TYPE_MMAP			6U
#define MULTIBOOT2_TAG_TYPE_VBE				7U
#define MULTIBOOT2_TAG_TYPE_FRAMEBUFFER			8U
#define MULTIBOOT2_TAG_TYPE_ELF_SECTIONS		9U
#define MULTIBOOT2_TAG_TYPE_APM				10U
#define MULTIBOOT2_TAG_TYPE_EFI32			11U
#define MULTIBOOT2_TAG_TYPE_EFI64			12U
#define MULTIBOOT2_TAG_TYPE_SMBIOS			13U
#define MULTIBOOT2_TAG_TYPE_ACPI_OLD			14U
#define MULTIBOOT2_TAG_TYPE_ACPI_NEW			15U
#define MULTIBOOT2_TAG_TYPE_NETWORK			16U
#define MULTIBOOT2_TAG_TYPE_EFI_MMAP			17U
#define MULTIBOOT2_TAG_TYPE_EFI_BS			18U
#define MULTIBOOT2_TAG_TYPE_EFI32_IH			19U
#define MULTIBOOT2_TAG_TYPE_EFI64_IH			20U
#define MULTIBOOT2_TAG_TYPE_LOAD_BASE_ADDR		21U

#define MULTIBOOT2_HEADER_TAG_END			0
#define MULTIBOOT2_HEADER_TAG_INFORMATION_REQUEST	1
#define MULTIBOOT2_HEADER_TAG_ADDRESS			2
#define MULTIBOOT2_HEADER_TAG_ENTRY_ADDRESS		3
#define MULTIBOOT2_HEADER_TAG_CONSOLE_FLAGS		4
#define MULTIBOOT2_HEADER_TAG_FRAMEBUFFER		5
#define MULTIBOOT2_HEADER_TAG_MODULE_ALIGN		6
#define MULTIBOOT2_HEADER_TAG_EFI_BS			7
#define MULTIBOOT2_HEADER_TAG_ENTRY_ADDRESS_EFI32	8
#define MULTIBOOT2_HEADER_TAG_ENTRY_ADDRESS_EFI64	9
#define MULTIBOOT2_HEADER_TAG_RELOCATABLE		10

#define MULTIBOOT2_ARCHITECTURE_I386			0
#define MULTIBOOT2_ARCHITECTURE_MIPS32			4

#ifndef ASSEMBLER

struct multiboot2_mmap_entry {
	uint64_t	addr;
	uint64_t	len;
	uint32_t	type;
	uint32_t	zero;
};

struct multiboot2_tag {
	uint32_t	type;
	uint32_t	size;
};

struct multiboot2_tag_string {
	uint32_t	type;
	uint32_t	size;
	char		string[0];
};

struct multiboot2_tag_module {
	uint32_t	type;
	uint32_t	size;
	uint32_t	mod_start;
	uint32_t	mod_end;
	char		cmdline[0];
};

struct multiboot2_tag_mmap {
	uint32_t	type;
	uint32_t	size;
	uint32_t	entry_size;
	uint32_t	entry_version;
	struct		multiboot2_mmap_entry entries[0];
};

struct multiboot2_tag_new_acpi {
	uint32_t	type;
	uint32_t	size;
	uint8_t		rsdp[0];
};

struct multiboot2_tag_efi64 {
	uint32_t	type;
	uint32_t	size;
	uint64_t	pointer;
};

struct multiboot2_tag_efi_mmap {
	uint32_t	type;
	uint32_t	size;
	uint32_t	descr_size;
	uint32_t	descr_vers;
	uint8_t		efi_mmap[0];
};
#endif

#endif /* CONFIG_MULTIBOOT2 */
/**
 * @}
 */
#endif /*  MULTIBOOT2_STD_H */
