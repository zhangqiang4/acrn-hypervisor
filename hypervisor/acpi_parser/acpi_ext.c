/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 John Baldwin <jhb@FreeBSD.org>
 * Copyright (c) 2018-2025 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <types.h>
#include <rtl.h>
#include <acpi.h>
#include <asm/pgtable.h>
#include <asm/ioapic.h>
#include <logmsg.h>
#include <asm/host_pm.h>
#include <pci.h>
#include <acrn_common.h>

/**
 * @addtogroup hwmgmt_hw-caps
 *
 * @{
 */

/**
 * @file
 * @brief Implementation for parsing ACPI data tables and fixing up configuration data.
 *
 * According to the ACPI specification, there are two fundamental types of ACPI tables:
 *
 * 1. Tables that contain AML code produced from the ACPI Source Language (ASL). These include the DSDT, any SSDTs,
 *    and sometimes OEM-specific tables (OEMx).
 *
 * 2. Tables that contain simple data and no AML byte code. These are known as ACPI Data Tables. They include tables
 *    such as the FADT, MADT, ECDT, SRAT, etc. Essentially, any table other than a DSDT or SSDT.
 *
 * The second type of table, the ACPI Data Table, can be parsed here.
 *
 * The fixed data include Power Management Sx states, PCI Memory Mapped Regions, and Reset Register.
 */

/**
 * @brief Internal function to retrieve a DWORD (32-bit) value from a given ACPI table at a specified offset.
 *
 * This function takes a pointer to an ACPI table and an offset and returns the DWORD value located at that offset
 * within the table.
 *
 * @param[in] dt_addr Pointer to the base address of the ACPI table.
 * @param[in] dt_offset Offset within the ACPI table where the DWORD value is located.
 *
 * @return The 32-bit DWORD value at the specified offset within the ACPI table.
 */
static inline uint32_t get_acpi_dt_dword(const uint8_t *dt_addr, uint32_t dt_offset)
{
	return *(const uint32_t *)(dt_addr + dt_offset);
}

/**
 * @brief Internal function to retrieve a QWORD (64-bit) value from a given ACPI table at a specified offset.
 *
 * This function takes a pointer to an ACPI table and an offset and returns the QWORD value located at that offset
 * within the table.
 *
 * @param[in] dt_addr Pointer to the base address of the ACPI table.
 * @param[in] dt_offset Offset within the ACPI table where the QWORD value is located.
 *
 * @return The 64-bit QWORD value at the specified offset within the ACPI table.
 */
static inline uint64_t get_acpi_dt_qword(const uint8_t *dt_addr, uint32_t dt_offset)
{
	return *(const uint64_t *)(dt_addr + dt_offset);
}

/**
 * @brief Internal function to get a GAS structure from a given table and its offset.
 *
 * This function retrieves a Generic Address Structure (GAS) from an ACPI table at a specified offset. The ACPI
 * table stores the GAS in a packed format, but the struct acrn_acpi_generic_address is not guaranteed to be packed,
 * so memcpy should not be used in this function.
 *
 * @param[in] dt_addr Pointer to the base address of the ACPI table.
 * @param[in] dt_offset Offset within the ACPI table where the GAS is located.
 * @param[inout] gas Pointer to the struct acrn_acpi_generic_address to be filled with the GAS data.
 *
 * @pre dt_addr != NULL
 * @pre gas != NULL
 */
static inline void get_acpi_dt_gas(const uint8_t *dt_addr, uint32_t dt_offset, struct acrn_acpi_generic_address *gas)
{
	const struct packed_gas *dt_gas = (const struct packed_gas *)(dt_addr + dt_offset);

	gas->space_id = dt_gas->space_id;
	gas->bit_width = dt_gas->bit_width;
	gas->bit_offset = dt_gas->bit_offset;
	gas->access_size = dt_gas->access_size;
	gas->address = dt_gas->address;
}

/**
 * @brief Internal function to retrieve the Firmware ACPI Control Structure (FACS) table address.
 *
 * This function retrieves the address of the FACS table from the provided FACP (Fixed ACPI Description Table)
 * address. It first checks the 32-bit FACS address and then, if the FACP revision is 2 or higher, it checks the
 * 64-bit FACS address. The function verifies that the FACS signature is 'FACS' and that the length is not less
 * than 64 bytes to ensure the table is valid.
 *
 * @param[in] facp_addr Pointer to the FACP table.
 *
 * @return Pointer to the FACS table if found and valid, otherwise NULL.
 *
 * @pre facp_addr != NULL
 */

static void *get_facs_table(const uint8_t *facp_addr)
{
	uint8_t *facs_addr, *facs_x_addr;
	uint32_t signature, length;
	const struct acpi_table_header *table = (const struct acpi_table_header *)facp_addr;

	facs_addr = (uint8_t *)(uint64_t)get_acpi_dt_dword(facp_addr, OFFSET_FACS_ADDR);

	if (table->revision >= 2U) {
		facs_x_addr = (uint8_t *)get_acpi_dt_qword(facp_addr, OFFSET_FACS_X_ADDR);

		if (facs_x_addr != NULL) {
			facs_addr = facs_x_addr;
		}
	}

	if (facs_addr != NULL) {
		signature = get_acpi_dt_dword(facs_addr, OFFSET_FACS_SIGNATURE);

		if (signature != ACPI_SIG_FACS) {
			facs_addr = NULL;
		} else {
			length = get_acpi_dt_dword(facs_addr, OFFSET_FACS_LENGTH);

			if (length < 64U) {
				facs_addr = NULL;
			}
		}
	}
	return (void *)facs_addr;
}

/**
 * @brief Internal function to parse the MCFG allocation tables from the given address.
 *
 * This function parses the MCFG (Memory-Mapped Configuration Space Base Address Description Table) allocation tables
 * from the provided address. It retrieves the length of the MCFG table and checks if multiple PCI segment groups are
 * present. If multiple PCI segment groups are detected, an error message is logged. Otherwise, it returns a pointer to
 * the MCFG allocation table.
 *
 * @param[in] mcfg_addr Pointer to the MCFG table address.
 *
 * @return Pointer to the parsed MCFG allocation table, or NULL if multiple PCI segment groups are detected.
 *
 * @pre mcfg_addr != NULL
 */
static const struct acpi_mcfg_allocation *parse_mcfg_allocation_tables(const uint8_t *mcfg_addr)
{
	const struct acpi_mcfg_allocation *mcfg_table = NULL;
	uint32_t length = get_acpi_dt_dword(mcfg_addr, OFFSET_MCFG_LENGTH);

	if (length > OFFSET_MCFG_ENTRY1) {
		pr_fatal("Multiple PCI segment groups is not supported!");
	} else {
		mcfg_table = (const struct acpi_mcfg_allocation *)(mcfg_addr + OFFSET_MCFG_ENTRY0_BASE);
	}
	return mcfg_table;
}

/**
 * @brief Parse ACPI data tables to update related data structures.
 *
 * This function processes ACPI tables (FADT, FACS, MCFG) to update ACPI-related data structures used by the
 * hypervisor, including Power Management Sx states, PCI Memory Mapped regions, and reset registers.
 *
 * This function performs the following tasks:
 * - Retrieves the system state data structure using get_host_sstate_data(), then retrieves the Fixed ACPI
 *   Description Table (FADT) and extracts power management event and control registers as defined in section 5.2.9
 *   of the ACPI Specification Release 6.5. It updates the system state data structure with the extracted registers.
 * - If the FADT revision is 2 or higher, retrieves the host reset registers data structure using
 *   get_host_reset_reg_data(), and updates it with data from the FADT.
 * - Retrieves the Firmware ACPI Control Structure (FACS) as defined in section 5.2.10 of the ACPI Specification
 *   Release 6.5 and sets wake vectors in the system state data structure.
 * - Retrieves the Memory Mapped Configuration Space Base Address Description Table (MCFG) as defined in section 4.1.2
 *   of the PCI Firmware Specification Revision 3.3 and parses the memory configuration allocation tables. Uses the
 *   parsed MCFG table to set the memory-mapped configuration region using set_mmcfg_region().
 *
 * @return int32_t indicating success or failure.
 * @retval 0 on success
 * @retval -1 on failure (if any required ACPI table is not found or processed correctly)
 */
int32_t acpi_fixup(void)
{
	uint8_t *facp_addr = NULL, *facs_addr = NULL, *mcfg_addr = NULL;
	const struct acpi_mcfg_allocation *mcfg_table = NULL;
	int32_t ret = 0;
	struct acrn_acpi_generic_address pm1a_cnt, pm1a_evt;
	struct pm_s_state_data *sx_data = get_host_sstate_data();

	facp_addr = (uint8_t *)get_acpi_tbl(ACPI_SIG_FADT);
	if (facp_addr != NULL) {
		get_acpi_dt_gas((const uint8_t *)facp_addr, OFFSET_PM1A_EVT, &pm1a_evt);
		get_acpi_dt_gas((const uint8_t *)facp_addr, OFFSET_PM1A_CNT, &pm1a_cnt);
		(void)memcpy_s((void *)&sx_data->pm1a_evt, sizeof(struct acrn_acpi_generic_address),
				(const void *)&pm1a_evt, sizeof(struct acrn_acpi_generic_address));
		(void)memcpy_s((void *)&sx_data->pm1a_cnt, sizeof(struct acrn_acpi_generic_address),
				(const void *)&pm1a_cnt, sizeof(struct acrn_acpi_generic_address));

		facs_addr = (uint8_t *)get_facs_table((const uint8_t *)facp_addr);
		if (facs_addr != NULL) {
			sx_data->wake_vector_32 = (uint32_t *)(facs_addr + OFFSET_WAKE_VECTOR_32);
			sx_data->wake_vector_64 = (uint64_t *)(facs_addr + OFFSET_WAKE_VECTOR_64);
		}

		struct acpi_table_header *table = (struct acpi_table_header *)facp_addr;

		if (table->revision >= 2U) {
			struct acpi_reset_reg *rr_data = get_host_reset_reg_data();

			get_acpi_dt_gas((const uint8_t *)facp_addr, OFFSET_RESET_REGISTER, &(rr_data->reg));
			rr_data->val = *(facp_addr + OFFSET_RESET_VALUE);
		}
	}

	mcfg_addr = (uint8_t *)get_acpi_tbl(ACPI_SIG_MCFG);
	if (mcfg_addr != NULL) {
		mcfg_table = parse_mcfg_allocation_tables(mcfg_addr);
		if (mcfg_table != NULL) {
			set_mmcfg_region((const struct pci_mmcfg_region*)mcfg_table);
		}
	}

	if ((facp_addr == NULL) || (facs_addr == NULL)
				|| (mcfg_addr == NULL) || (mcfg_table == NULL)) {
		ret = -1;
	}

	return ret;
}
/**
 * @}
 */
