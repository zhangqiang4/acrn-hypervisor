/*
 * Copyright (C) 2020-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef MULTIBOOT_PRIV_H
#define MULTIBOOT_PRIV_H

#include <multiboot_std.h>
/**
 * @addtogroup boot
 *
 * @{
 */

/**
 * @file
 * @brief API to check boot protocol
 *
 * The file provides API to check whether the system booted using the multiboot1 or multiboot2 protocol.
 */
#ifdef CONFIG_MULTIBOOT2
/**
 * @brief Check for Multiboot2 Boot Protocol
 *
 * This function checks if the system was booted using a Multiboot2-compliant
 * bootloader by verifying the magic number passed to it. The magic number is
 * used to confirm that the bootloader adheres to the Multiboot2 specification.
 *
 * @param[in] magic The magic number provided by the bootloader, which is used to
 *              identify the boot protocol.
 *
 * @return Returns true if the magic number matches the Multiboot2 magic
 *              number, indicating a Multiboot2-compliant boot. Returns false
 *              otherwise.
 *
 * @remark According to the Multiboot specification, the Multiboot information
 *       structure can be placed anywhere in memory by the bootloader. Historically,
 *       GRUB could place the Multiboot2 information structure at address 0 until
 *       a specific commit (0f3f5b7c13fa9b67) on Dec 26, 2019, which set a minimum
 *       address for the Multiboot2 information structure allocation to 0x1000.
 */
static inline bool boot_from_multiboot2(uint32_t magic)
{
	/*
	 * Multiboot spec states that the Multiboot information structure may be placed
	 * anywhere in memory by the boot loader.
	 *
	 * Seems both SBL and GRUB won't place multiboot1 MBI structure at 0 address,
	 * but GRUB could place Multiboot2 MBI structure at 0 address until commit
	 * 0f3f5b7c13fa9b67 ("multiboot2: Set min address for mbi allocation to 0x1000")
	 * which dates on Dec 26 2019.
	 */
	return (magic == MULTIBOOT2_INFO_MAGIC);
}

int32_t multiboot2_to_acrn_bi(struct acrn_boot_info *abi, void *mb2_info);
#endif

/**
 * @brief Check for Multiboot Boot Protocol
 *
 * This function checks if the system was booted using a Multiboot-compliant
 * bootloader by verifying the magic number and the information structure address
 * provided to it.
 *
 * @param[in] magic The magic number provided by the bootloader, used to identify
 *              the boot protocol.
 * @param[in] info The address of the boot information structure, which must be
 *             non-zero to indicate valid Multiboot information.
 *
 * @return Returns true if the magic number matches the Multiboot magic
 *              number and the information structure address is non-zero,
 *              indicating a Multiboot-compliant boot. Returns false otherwise.
 */
static inline bool boot_from_multiboot(uint32_t magic, uint32_t info)
{
	return ((magic == MULTIBOOT_INFO_MAGIC) && (info != 0U));
}
/**
 * @}
 */
#endif /* MULTIBOOT_PRIV_H */
