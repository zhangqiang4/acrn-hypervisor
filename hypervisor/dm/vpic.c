/*
 * Copyright (C) 2024-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <asm/guest/vm.h>

/**
 * @addtogroup vp-dm_vperipheral
 *
 * @{
 */

/**
 * @file
 * @brief Implementation of virtual 8259 Programmable Interrupt Controller (PIC)
 *
 * This file defines operations to support virtual 8259 Programmable Interrupt Controller (PIC).
 */

/**
 * @brief Hide 8259 Programmable Interrupt Controller (PIC) from guest by registering PIO emulation handlers.
 *
 * This function hides PIC from guest by registering NULL handlers at 8259 Programmable Interrupt Controller (PIC)
 * I/O ranges, including primary PIC, secondary PIC, and ELCR.
 *
 * @param[in] vm Pointer to the VM structure to hide PIC ranges on.
 *
 * @return None
 *
 * @pre vm != NULL
 */
void vpic_hide(struct acrn_vm *vm)
{
	struct vm_io_range primary_vPIC_range = {
		.base = 0x20U,
		.len = 2U
	};
	struct vm_io_range secondary_vPIC_range = {
		.base = 0xa0U,
		.len = 2U
	};
	struct vm_io_range elcr_range = {
		.base = 0x4d0U,
		.len = 2U
	};

	register_pio_emulation_handler(vm, PIC_PRIMARY_PIO_IDX, &primary_vPIC_range, NULL, NULL);
	register_pio_emulation_handler(vm, PIC_SECONDARY_PIO_IDX, &secondary_vPIC_range, NULL, NULL);
	register_pio_emulation_handler(vm, PIC_ELC_PIO_IDX, &elcr_range, NULL, NULL);
}

/**
 * @}
 */
