/*
 * Copyright (C) 2018-2025 Intel Corporation.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef INIT_H
#define INIT_H

/**
 * @defgroup init init
 * @brief Initialization
 *
 * This module handles the initialization of the primary and secondary processors, setting up the necessary
 * environment for the hypervisor to operate. It includes functions for setting up the debug environment,
 * initializing VMX mode, and configuring the physical CPUs (pCPUs). The module also includes assembly routines
 * for CPU mode transitioning and multiboot header information.
 *
 * @{
 */

/**
 * @file arch/x86/boot/cpu_primary.S
 * @brief Provides entry point and Multiboot header of the hypervisor.
 *
 * This file implements the entry point of ACRN hypervisor, namely cpu_primary_start_32, which sets up a proper C
 * execution environment in 64-bit mode. The GDT and page table are properly set up to establish an identical mapping
 * from the first 4G in the logical address space (in any segment) to the first 4G in the physical address space.
 *
 * This file begins with a Multiboot header, which is a structure that contains information to ensure the bootloader
 * can correctly identify and load the hypervisor. This file supports both Muitiboot and Multiboot2 specifications.
 */

/**
 * @file arch/x86/boot/trampoline.S
 * @brief Provides starting point of application processors.
 *
 * This file implements a starting point, namely trampoline_start_16, which sets up a proper C execution environment in
 * 64-bit mode for application processors which start execution from real-address mode. The GDT and page table are
 * properly set up to establish an identical mapping from the first 4G in the logical address space (in any segment) to
 * the first 4G in the physical address space.
 */

/**
 * @file
 * @brief Initialization header file.
 *
 * This file declares the initialization functions and macros used for setting up the hypervisor. It includes the
 * definitions for stack magic numbers and function prototypes for initializing the primary and secondary pCPUs.
 */

/**
 * @brief Hypervisor stack bottom magic.
 *
 * This magic number represents "intl" in ASCII.
 */
#define SP_BOTTOM_MAGIC    0x696e746cUL

void init_primary_pcpu(void);
void init_secondary_pcpu(void);

#endif /* INIT_H */

/**
 * @}
 */