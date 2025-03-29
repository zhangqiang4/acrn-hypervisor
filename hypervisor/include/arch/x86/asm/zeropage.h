/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @addtogroup vp-base_vboot
 *
 * @{
 */

/**
 * @file
 * @brief Structure and Macro definition for ELF format file.
 *
 * Structure and Macro definition for Headers of 32-bit and 64-bit ELF files.
 *
 * @remark please refer source in https://github.com/freebsd/freebsd-src/blob/main/sys/sys/elf_common.h
 */

#ifndef ZEROPAGE_H
#define ZEROPAGE_H
#include <asm/e820.h>
#include <efi.h>

/**
 * @brief Zero Page Structure for Linux Kernel Boot
 *
 * This structure represents the "zero page" used during the booting of a Linux
 * kernel. It contains various fields that provide information about the boot
 * environment, including EFI information, memory map entries, and kernel setup
 * parameters. The zero page is a critical part of the boot process, providing
 * the kernel with necessary data to initialize and manage the system.
 *
 * @remark This structure shall be packed.
 */
struct zero_page {
	uint8_t pad0[0x1c0];                /**< Padding bytes for alignment. offset 0x000 */

	struct efi_info boot_efi_info;      /**< EFI information. offset 0x1c0 */

	uint8_t pad1[0x8];                  /**< Additional padding bytes. offset 0x1e0 */
	uint8_t e820_nentries;              /**< Number of E820 memory map entries. offset 0x1e8 */
	uint8_t pad2[0x8];                  /**< Additional padding bytes. offset 0x1e9 */

	/**
	 * @brief Represents the header structure for kernel setup parameters.
	 *
	 * This structure contains various fields used during the setup and boot process
	 * of a kernel. It includes information about setup sectors, loader type, load flags,
	 * ramdisk details, boot arguments, and kernel alignment. The structure also contains
	 * padding bytes to ensure proper alignment and layout in memory. The fields are packed
	 * to maintain the expected layout without additional padding.
	 *
	 * @remark This structure shall be packed.
	 */
	struct {
		uint8_t setup_sects;        /**< Number of setup sectors. offset 0x1f1 */
		uint8_t hdr_pad1[0x1e];     /**< Padding bytes. offset 0x1f2 */
		uint8_t loader_type;        /**< Type of loader. offset 0x210 */
		uint8_t load_flags;         /**< Load flags. offset 0x211 */
		uint8_t hdr_pad2[0x6];      /**< Padding bytes. offset 0x212 */
		uint32_t ramdisk_addr;      /**< Ramdisk address. offset 0x218 */
		uint32_t ramdisk_size;      /**< Ramdisk size. offset 0x21c */
		uint8_t hdr_pad3[0x8];      /**< Padding bytes. offset 0x220 */
		uint32_t bootargs_addr;     /**< Boot arguments address. offset 0x228 */
		uint32_t initrd_addr_max;   /**< Maximum initrd address. offset 0x22c */
		uint32_t kernel_alignment;  /**< Kernel alignment. offset 0x230 */
		uint8_t relocatable_kernel; /**< Relocatable kernel flag. offset 0x234 */
		uint8_t hdr_pad5[0x13];     /**< Padding bytes. offset 0x235 */
		uint32_t payload_offset;    /**< Payload offset. offset 0x248 */
		uint32_t payload_length;    /**< Payload length. offset 0x24c */
		uint8_t hdr_pad6[0x8];	    /**< Padding bytes. offset 0x250 */
		uint64_t pref_addr;         /**< Preferred address. offset 0x258 */
		uint32_t init_size;         /**< Initial setup size. offset 0x260 */
		uint8_t hdr_pad7[4];        /**< Padding bytes. offset 0x264 */
	} __packed hdr;

	uint8_t pad3[0x68];                 /**< Additional padding bytes. offset 0x268 */
	struct e820_entry entries[0x80];    /**< E820 memory map entries. offset 0x2d0 */
	uint8_t pad4[0x330];                /**< Additional padding bytes. offset 0xcd0 */
} __packed;

#endif
/**
 * @}
 */
