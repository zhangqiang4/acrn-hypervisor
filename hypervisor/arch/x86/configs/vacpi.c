/*
 * Copyright (C) 2019-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @addtogroup vp-base_vm_config
 *
 * @{
 */

/**
 * @file
 * @brief Virtual ACPI helper utility
 *
 * This file contains helper utility on building virtual ACPI.
 */

#include <asm/guest/vm.h>
#include <vacpi.h>

/**
 * @brief Generate virtual ACPI RSDP (Root System Descriptor Pointer)
 *
 * Construct and initialize virtual ACPI RSDP structure and put them
 * into address space of \p vm .
 *
 * The constructed virtual ACPI table is fixed to revision 2.0.
 *
 * @param[inout] vm The VM for which the virtual ACPI RSDP is built
 *
 * @pre vm != NULL
 * @pre vm->vm_id < CONFIG_MAX_VM_NUM
 * @post N/A
 *
 * @return None
 */
void build_vrsdp(struct acrn_vm *vm)
{
	struct acpi_table_rsdp rsdp = {
		.signature = ACPI_SIG_RSDP,
		.oem_id = ACPI_OEM_ID,
		.revision = 0x2U,
		.length = ACPI_RSDP_XCHECKSUM_LENGTH,
		.xsdt_physical_address = VIRT_XSDT_ADDR,
	};

	rsdp.checksum = calculate_checksum8(&rsdp, ACPI_RSDP_CHECKSUM_LENGTH);
	rsdp.extended_checksum = calculate_checksum8(&rsdp, ACPI_RSDP_XCHECKSUM_LENGTH);
	/* Copy RSDP table to guest physical memory F segment */
	(void)copy_to_gpa(vm, &rsdp, VIRT_RSDP_ADDR, ACPI_RSDP_XCHECKSUM_LENGTH);
}

/**
 * @}
 */
