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
#include "acpi.h"
#include <asm/pgtable.h>
#include <asm/ioapic.h>
#include <logmsg.h>
#include <acrn_common.h>
#include <util.h>
#include <asm/e820.h>
#include <boot.h>
/**
 * @addtogroup hwmgmt_hw-caps
 * @{
 */
/**
 * @file
 * @brief ACPI (Advanced Configuration and Power Interface) management implementation for the hwmgmt.hw-caps module.
 *
 * This file contains data structures and functions to retrieve hardware capabilities from ACPI and provide interfaces
 * to other modules.
 */

/**
 * @brief Pointer to the ACPI Root System Description Pointer (RSDP) structure.
 *
 * This static variable holds a pointer to the ACPI RSDP structure, which is used to locate the ACPI tables in memory.
 * The RSDP structure contains information about the ACPI tables and their locations, which are essential for the ACPI
 * subsystem to function correctly.
 */
static struct acpi_table_rsdp *acpi_rsdp;

/**
 * @brief Search for the Root System Description Pointer (RSDP) in a memory region.
 *
 * This function searches for the ACPI RSDP structure within a specified memory region. Once the signature "RSD PTR "
 * is found, it validates the checksum of the RSDP structure. If it is correct, the RSDP structure is returned.
 * Otherwise, the function continues searching. The search is performed on 16-byte boundaries.
 *
 * @param[in] base The base address of the memory region to search.
 * @param[in] length The length of the memory region to search.
 *
 * @return A pointer to the found RSDP structure, or NULL if not found.
 */
static struct acpi_table_rsdp *found_rsdp(char *base, uint64_t length)
{
	struct acpi_table_rsdp *rsdp, *ret = NULL;
	uint64_t ofs;

	/* search on 16-byte boundaries */
	for (ofs = 0UL; ofs < length; ofs += 16UL) {
		rsdp = (struct acpi_table_rsdp *)(base + ofs);

		/* compare signature, validate checksum */
		if ((strncmp(rsdp->signature, ACPI_SIG_RSDP, strnlen_s(ACPI_SIG_RSDP, sizeof(rsdp->signature))) == 0)
			&& (calculate_sum8(rsdp, ACPI_RSDP_CHECKSUM_LENGTH) == 0U)) {
			ret = rsdp;
			break;
		}
	}

	return ret;
}

/**
 * @brief Retrieve the Root System Description Pointer (RSDP).
 *
 * This function returns the RSDP parsed from the BIOS region (stored in the global variable acpi_rsdp).
 *
 * @return Pointer to the ACPI RSDP structure.
 */
static struct acpi_table_rsdp *get_rsdp(void)
{
	return acpi_rsdp;
}

/**
 * @brief Initialize the ACPI sub-system.
 *
 * This function locates the Root System Description Pointer (RSDP) which is essential for ACPI initialization. It
 * attempts to find the RSDP in several locations:
 * 1. The address provided by the ACRN boot information.
 * 2. The Extended BIOS Data Area (EBDA).
 * 3. The upper memory BIOS space (0xE0000 - 0xFFFFF).
 * 4. The ACPI Reclaim memory regions as specified by the E820 memory map.
 *
 * This function is critical for the proper initialization of the ACPI subsystem. If the RSDP is not found in any of
 * these locations, the function will trigger a panic. Once the RSDP is found, it is assigned to the global variable
 * `acpi_rsdp`.
 *
 * @pre N/A
 * @post acpi_rsdp!=NULL
 */
void init_acpi(void)
{
	struct acpi_table_rsdp *rsdp = NULL;

	rsdp = (struct acpi_table_rsdp *)(get_acrn_boot_info()->acpi_rsdp_va);
	if (rsdp == NULL) {
		uint16_t *addr;

		/* EBDA is addressed by the 16 bit pointer at 0x40E */
		addr = (uint16_t *)hpa2hva(0x40eUL);

		rsdp = found_rsdp((char *)hpa2hva((uint64_t)(*addr) << 4U), 0x400UL);
	}
	if (rsdp == NULL) {
		/* Check the upper memory BIOS space, 0xe0000 - 0xfffff. */
		rsdp = found_rsdp((char *)hpa2hva(0xe0000UL), 0x20000UL);
	}

	if (rsdp == NULL) {
		/* Check ACPI RECLAIM region, there might be multiple ACPI reclaimable regions. */
		uint32_t i;
		const struct e820_entry *entry = get_e820_entry();
		uint32_t entries_count = get_e820_entries_count();

		for (i = 0U; i < entries_count; i++) {
			if (entry[i].type == E820_TYPE_ACPI_RECLAIM) {
				rsdp = found_rsdp((char *)hpa2hva(entry[i].baseaddr), entry[i].length);
				if (rsdp != NULL) {
					break;
				}
			}
		}
	}

	if (rsdp == NULL) {
		panic("No RSDP is found");
	}

	/* After RSDP is parsed, it will be assigned to acpi_rsdp */
	acpi_rsdp = rsdp;
}

/**
 * @brief Probe an ACPI table at a given address for a specific signature.
 *
 * This function checks if the ACPI table located at the specified physical address matches the provided signature.
 *
 * @param[in] address The physical address of the ACPI table to probe.
 * @param[in] signature The expected signature of the ACPI table.
 *
 * @return true if the table's signature matches the provided signature,
 *         false otherwise.
 */
static bool probe_table(uint64_t address, const char *signature)
{
	void *va =  hpa2hva(address);
	struct acpi_table_header *table = (struct acpi_table_header *)va;
	bool ret;

	if (strncmp(table->signature, signature, ACPI_NAME_SIZE) != 0) {
		ret = false;
	} else {
		ret = true;
	}

	return ret;
}

/**
 * @brief Retrieve the ACPI table with the specified signature.
 *
 * This function searches for an ACPI table with the given signature by examining the Root System Description Pointer
 * (RSDP). If the RSDP revision is version 2.0 (or higher) and the address of the XSDT is not zero, the table will be
 * found in the Extended System Description Table (XSDT). Otherwise, the table will be found in the Root System
 * Description Table (RSDT).
 *
 * @param[in] signature A pointer to a string containing the ACPI table signature.
 *
 * @return A pointer to the ACPI table in the host virtual address space if found, or NULL if the table is not found.
 */
void *get_acpi_tbl(const char *signature)
{
	struct acpi_table_rsdp *rsdp;
	struct acpi_table_rsdt *rsdt;
	struct acpi_table_xsdt *xsdt;
	uint64_t addr = 0UL;
	uint32_t i, count;

	/* the returned RSDP should always exist. Otherwise the hypervisor
	 * can't be booted.
	 */
	rsdp = get_rsdp();

	if ((rsdp->revision >= 2U) && (rsdp->xsdt_physical_address != 0UL)) {
		/*
		 * AcpiOsGetRootPointer only verifies the checksum for
		 * the version 1.0 portion of the RSDP.  Version 2.0 has
		 * an additional checksum that we verify first.
		 */
		xsdt = (struct acpi_table_xsdt *)hpa2hva(rsdp->xsdt_physical_address);
		count = (xsdt->header.length - sizeof(struct acpi_table_header)) / sizeof(uint64_t);

		for (i = 0U; i < count; i++) {
			if (probe_table(xsdt->table_offset_entry[i], signature)) {
				addr = xsdt->table_offset_entry[i];
				break;
			}
		}
	} else {
		/* Root table is an RSDT (32-bit physical addresses) */
		rsdt = (struct acpi_table_rsdt *)hpa2hva((uint64_t)rsdp->rsdt_physical_address);
		count = (rsdt->header.length - sizeof(struct acpi_table_header)) / sizeof(uint32_t);

		for (i = 0U; i < count; i++) {
			if (probe_table(rsdt->table_offset_entry[i], signature)) {
				addr = rsdt->table_offset_entry[i];
				break;
			}
		}
	}

	return hpa2hva(addr);
}

/**
 * @brief Parses the MADT (Multiple APIC Description Table) to extract Local APIC and x2APIC processor IDs.
 *
 * This function iterates through the entries in the provided MADT table to extract Local APIC and x2APIC
 * processor IDs, storing them in the provided array. It counts the number of processors found and returns this
 * count. The ACPI specification allows the MADT to include both Local APIC and x2APIC structures for compatibility.
 * The BIOS should ensure that the MADT table is well-formed and contains only unique processor IDs. The ACRN
 * hypervisor does not perform further checks here.
 * Refer to section 5.2.12 of the ACPI specification 6.5.2 for more information on the MADT table.
 *
 * @param[in] madt Pointer to the ACPI MADT table.
 * @param[out] lapic_id_array Array to store the extracted Local APIC and x2APIC processor IDs. The array must have a
 *                            size of at least MAX_PCPU_NUM.
 *
 * @return The number of processors found and stored in the lapic_id_array.
 */
static uint16_t
local_parse_madt(struct acpi_table_madt *madt, uint32_t lapic_id_array[MAX_PCPU_NUM])
{
	uint16_t pcpu_num = 0U;
	struct acpi_madt_local_apic *processor;
	struct acpi_madt_x2apic *processor_x2apic;
	struct acpi_table_madt *madt_ptr;
	void *first, *end, *iterator;
	struct acpi_subtable_header *entry;

	madt_ptr = madt;

	first = madt_ptr + 1;
	end = (void *)madt_ptr + madt_ptr->header.length;

	for (iterator = first; (iterator) < (end); iterator += entry->length) {
		entry = (struct acpi_subtable_header *)iterator;
		if (entry->length < sizeof(struct acpi_subtable_header)) {
			break;
		}

		if (entry->type == ACPI_MADT_TYPE_LOCAL_APIC) {
			processor = (struct acpi_madt_local_apic *)iterator;
			if ((processor->lapic_flags & ACPI_MADT_ENABLED) != 0U) {
				if (pcpu_num < MAX_PCPU_NUM) {
					lapic_id_array[pcpu_num] = processor->id;
				}
				pcpu_num++;
			}
		} else if (entry->type == ACPI_MADT_TYPE_X2APIC) {
			processor_x2apic = (struct acpi_madt_x2apic *)iterator;
			if ((processor_x2apic->lapic_flags & ACPI_MADT_ENABLED) != 0U) {
				if (pcpu_num < MAX_PCPU_NUM) {
					lapic_id_array[pcpu_num] = processor_x2apic->processor_id;
				}
				pcpu_num++;
			}
		} else {
			/* do nothing */
		}
	}

	return pcpu_num;
}

/**
 * @brief Parses the MADT (Multiple APIC Description Table) to retrieve Local APIC and x2APIC processor IDs.
 *
 * This function retrieves the MADT from the ACPI tables and parses it to extract Local APIC and x2APIC processor
 * IDs, storing them in the provided array. If the MADT is not found, the function returns 0, which could cause
 * ACRN hypervisor initialization to fail.
 *
 * @param[out] lapic_id_array An array to store the Local APIC and x2APIC processor IDs. The array size should be at
 *                            least MAX_PCPU_NUM.
 *
 * @return The number of LAPIC IDs parsed and stored in the array.
 */
uint16_t parse_madt(uint32_t lapic_id_array[MAX_PCPU_NUM])
{
	uint16_t ret = 0U;
	struct acpi_table_madt *madt = (struct acpi_table_madt *)get_acpi_tbl(ACPI_SIG_MADT);

	if (madt != NULL) {
		ret = local_parse_madt(madt, lapic_id_array);
	}

	return ret;
}

/**
 * @brief Parses the MADT (Multiple APIC Description Table) to extract IOAPIC information.
 *
 * This function parses the ACPI MADT to locate IOAPIC entries and stores the relevant information (ID, address, and
 * GSI base) in the provided ioapic_id_array. If the MADT is not found, the function returns 0, which could cause ACRN
 * hypervisor initialization to fail.
 * Refer to section 5.2.12 of the ACPI specification 6.5.2 for more information on the MADT table.
 *
 * @param[out] ioapic_id_array Pointer to an array of ioapic_info structures where the parsed IOAPIC information will
 *                             be stored.
 *
 * @return The number of IOAPIC entries found and stored in the ioapic_id_array.
 */
uint8_t parse_madt_ioapic(struct ioapic_info *ioapic_id_array)
{
	uint8_t ioapic_idx = 0U;
	uint64_t entry, end;
	const struct acpi_madt_ioapic *ioapic;
	const struct acpi_table_madt *madt = (const struct acpi_table_madt *)get_acpi_tbl(ACPI_SIG_MADT);

	if (madt != NULL) {
		end = (uint64_t)madt + madt->header.length;

		for (entry = (uint64_t)(madt + 1); entry < end; entry += ioapic->header.length) {
			ioapic = (const struct acpi_madt_ioapic *)entry;

			if (ioapic->header.type == ACPI_MADT_TYPE_IOAPIC) {
				if (ioapic_idx < CONFIG_MAX_IOAPIC_NUM) {
					ioapic_id_array[ioapic_idx].id = ioapic->id;
					ioapic_id_array[ioapic_idx].addr = ioapic->addr;
					ioapic_id_array[ioapic_idx].gsi_base = ioapic->gsi_base;
				}
				ioapic_idx++;
			}
		}
	}

	return ioapic_idx;
}

/**
 * @brief Parses the HPET (High Precision Event Timer) ACPI table.
 *
 * This function retrieves the HPET ACPI table using the ACPI signature for HPET, extracts the address from the table,
 * and converts it from a host physical address to a host virtual address.
 *
 * @return A pointer to the HPET table in the host virtual address space, or NULL if the table is not found.
 */
void *parse_hpet(void)
{
	const struct acpi_table_hpet *hpet = (const struct acpi_table_hpet *)get_acpi_tbl(ACPI_SIG_HPET);
	uint64_t addr = 0UL;

	if (hpet != NULL) {
		addr = hpet->address.address;
	}

	return hpa2hva(addr);
}

/**
 * @}
 */
