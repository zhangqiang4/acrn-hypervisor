/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <logmsg.h>
#include <asm/io.h>
#include <asm/lib/spinlock.h>
#include <asm/cpu_caps.h>
#include <pci.h>
#include <asm/vtd.h>
#include <acpi.h>

/**
 * @addtogroup hwmgmt_hw-caps
 *
 * @{
 */
/**
 * @file
 * @brief DMAR (DMA Remapping) parsing implementation for the ACRN hypervisor.
 *
 * This file contains the data structures and functions to parse ACPI DMAR tables and extract relevant information.
 */

/**
 * @brief Counter for the number of DMAR units.
 *
 * This static variable keeps track of the number of DMAR (DMA Remapping) units detected or processed by the ACPI
 * parser. It is used internally within the DMAR parsing logic to maintain a count of these units.
 */
static uint32_t dmar_unit_cnt;

/**
 * @brief Array to store DRHD (DMA Remapping Hardware Unit Definition) information.
 *
 * This array holds the information for each DRHD unit parsed from the ACPI DMAR table.
 * The size of the array is defined by the MAX_DRHDS macro.
 */
static struct dmar_drhd drhd_info_array[MAX_DRHDS];

/**
 * @brief 2D array to store device scope entries for DRHD structures.
 *
 * This array is used to store the device scope entries for each DRHD (DMA Remapping Hardware Unit Definition)
 * structure. Each DRHD can have multiple device scope entries, which are stored in this 2D array.
 *
 * The maximum number of DRHD structures is defined by the MAX_DRHDS macro.
 * The maximum number of device scope entries per DRHD structure is defined by the MAX_DRHD_DEVSCOPES macro.
 */
static struct dmar_dev_scope drhd_dev_scope[MAX_DRHDS][MAX_DRHD_DEVSCOPES];

/**
 * @brief Internal function to retrieve the DMAR (DMA Remapping) table.
 *
 * This function calls `get_acpi_tbl` with the signature `DMAR` to obtain the DMAR table, which contains information
 * about DMA remapping hardware in the system.
 *
 * @return A pointer to the DMAR table if found, otherwise NULL.
 */
static void *get_dmar_table(void)
{
	return get_acpi_tbl(ACPI_SIG_DMAR);
}

/**
 * @brief Internal function to retrieve the secondary bus number for a given PCI device.
 *
 * This function reads the secondary bus number from the PCI configuration space of a specified PCI device using
 * Programmed Input/Output (PIO) interfaces.
 *
 * @param[in] bus The bus number of the PCI device.
 * @param[in] dev The device number on the specified bus.
 * @param[in] func The function number of the specified device.
 *
 * @return The secondary bus number.
 */
static uint8_t get_secondary_bus(uint8_t bus, uint8_t dev, uint8_t func)
{
	uint32_t data;

	pio_write32(PCI_CFG_ENABLE | ((uint32_t)bus << 16U) | ((uint32_t)dev << 11U) |
		((uint32_t)func << 8U) | 0x18U, PCI_CONFIG_ADDR);

	data = pio_read32(PCI_CONFIG_DATA);

	return (data >> 8U) & 0xffU;
}

/**
 * @brief Internal function to parse the DMAR path and return the PCI Bus/Device/Function (BDF) address.
 *
 * This function takes the length of the path, the initial bus number, and a pointer to the ACPI DMAR PCI path
 * structure. It iterates through the path to determine the BDF address by updating the bus number using the
 * secondary bus number obtained from the current BDF.
 *
 * @param[in] path_len Length of the DMAR path.
 * @param[in] busno Initial bus number.
 * @param[in] path Pointer to the ACPI DMAR PCI path structure.
 *
 * @return The final PCI BDF address as a union pci_bdf.
 */
static union pci_bdf dmar_path_bdf(int32_t path_len, uint8_t busno, const struct acpi_dmar_pci_path *path)
{
	int32_t i;
	union pci_bdf dmar_bdf;

	dmar_bdf.bits.b = busno;
	dmar_bdf.bits.d = path->device;
	dmar_bdf.bits.f = path->function;

	for (i = 1; i < path_len; i++) {
		dmar_bdf.bits.b = get_secondary_bus(dmar_bdf.bits.b, dmar_bdf.bits.d, dmar_bdf.bits.f);
		dmar_bdf.bits.d = path[i].device;
		dmar_bdf.bits.f = path[i].function;
	}
	return dmar_bdf;
}

/**
 * @brief Internal function to handle the DMAR device scope and extracts relevant information.
 *
 * This function processes the DMAR (DMA Remapping) device scope structure and extracts the bus, device,
 * and function information from the given address. It also updates the provided `dev_scope` structure
 * with the extracted information. If the remaining length is less than the size of the DMAR device scope
 * structure or less than the length specified in the DMAR device scope header, the function will return -1
 * to indicate a failure.
 *
 * @param[inout] dev_scope Pointer to the structure where the extracted device scope information will be stored.
 * @param[in] addr Pointer to the address containing the DMAR device scope structure.
 * @param[in] remaining The remaining length of the data to be processed.
 *
 * @return The length of the DMAR device scope structure if successful, otherwise -1.
 */
static int32_t handle_dmar_devscope(struct dmar_dev_scope *dev_scope, void *addr, int32_t remaining)
{
	int32_t path_len, ret = -1;
	union pci_bdf dmar_bdf;
	struct acpi_dmar_pci_path *path;
	struct acpi_dmar_device_scope *apci_devscope = addr;

	if ((remaining >= (int32_t)sizeof(struct acpi_dmar_device_scope)) &&
	    (remaining >= (int32_t)apci_devscope->length)) {
		path = (struct acpi_dmar_pci_path *)(apci_devscope + 1);
		path_len = (int32_t)((apci_devscope->length - sizeof(struct acpi_dmar_device_scope)) /
				sizeof(struct acpi_dmar_pci_path));

		dmar_bdf = dmar_path_bdf(path_len, apci_devscope->bus, path);
		dev_scope->id = apci_devscope->enumeration_id;
		dev_scope->type = apci_devscope->entry_type;
		dev_scope->bus = dmar_bdf.fields.bus;
		dev_scope->devfun = dmar_bdf.fields.devfun;
		ret = (int32_t)apci_devscope->length;
	}

	return ret;
}

/**
 * @brief Internal function to count the device scopes in a given DRHD (DMA Remapping Hardware Unit) structure.
 *
 * This function calculates the number of device scopes present in the specified DRHD structure. It iterates
 * through the device scopes within the DRHD structure and counts those that are valid (i.e., not of type
 * ACPI_DMAR_SCOPE_TYPE_NOT_USED and less than ACPI_DMAR_SCOPE_TYPE_RESERVED). For more details, refer to
 * section 8.3.1 of the Intel Virtualization Technology for Directed I/O Architecture Specification Revision 5.0.
 *
 * @param[in] drhd Pointer to the DRHD structure to be parsed.
 *
 * @return The number of valid device scopes in the given DRHD structure.
 */
static uint32_t get_drhd_dev_scope_cnt(struct acpi_dmar_hardware_unit *drhd)
{
	struct acpi_dmar_device_scope *scope;
	char *start;
	char *end;
	uint32_t count = 0;

	start = (char *)drhd + sizeof(struct acpi_dmar_hardware_unit);
	end = (char *)drhd + drhd->header.length;

	while (start < end) {
		scope = (struct acpi_dmar_device_scope *)start;
		if ((scope->entry_type != ACPI_DMAR_SCOPE_TYPE_NOT_USED) &&
			(scope->entry_type < ACPI_DMAR_SCOPE_TYPE_RESERVED)) {
			count++;
		}
		start += scope->length;
	}
	return count;
}

/**
 * @brief Internal function to handle a single DMA Remapping Hardware Unit (DRHD) structure.
 *
 * This function processes an ACPI DMAR hardware unit structure and populates the corresponding
 * DMAR DRHD structure. It also handles device scopes within the DRHD and applies platform-specific
 * constraints.
 * For more details, refer to Section 8.3 of the Intel Virtualization Technology for Directed
 * I/O (VT-d) Architecture Specification, Revision 5.0.
 *
 * @param[in] acpi_drhd Pointer to the ACPI DMAR hardware unit structure.
 * @param[inout] drhd Pointer to the DMAR DRHD structure to be populated.
 *
 * @return Always returns 0.
 *
 * @pre (acpi_drhd != NULL) && (drhd != NULL)
 *
 * @remark Application constraint: The dedicated DMAR unit for Intel integrated GPU shall be available
 * on the physical platform. This function disables GPU IOMMU on APL platforms as GVT-d is not enabled
 * on APL yet.
 */
static int32_t handle_one_drhd(struct acpi_dmar_hardware_unit *acpi_drhd, struct dmar_drhd *drhd)
{
	struct dmar_dev_scope *dev_scope;
	struct acpi_dmar_device_scope *ads;
	int32_t remaining, consumed;
	char *cp;
	uint32_t dev_count;

	drhd->segment = acpi_drhd->segment;
	drhd->flags = acpi_drhd->flags;
	drhd->reg_base_addr = acpi_drhd->address;

	dev_count = get_drhd_dev_scope_cnt(acpi_drhd);
	ASSERT(dev_count <= MAX_DRHD_DEVSCOPES, "parsed dev_count > MAX_DRHD_DEVSCOPES");

	drhd->dev_cnt = dev_count;

	remaining = (int32_t)(acpi_drhd->header.length - sizeof(struct acpi_dmar_hardware_unit));

	dev_scope = drhd->devices;

	while (remaining > 0) {
		cp = (char *)acpi_drhd + acpi_drhd->header.length - remaining;

		consumed = handle_dmar_devscope(dev_scope, cp, remaining);

		if (consumed <= 0) {
			break;
		}

		remaining -= consumed;
		/* skip IOAPIC & HPET */
		ads = (struct acpi_dmar_device_scope *)cp;
		if ((ads->entry_type != ACPI_DMAR_SCOPE_TYPE_NOT_USED) &&
			(ads->entry_type < ACPI_DMAR_SCOPE_TYPE_RESERVED)) {
			dev_scope++;
		} else {
			pr_dbg("drhd: skip dev_scope type %d", ads->entry_type);
		}
	}

	return 0;
}

/**
 * @brief Interface to parse the DMAR (DMA Remapping) table and populate platform DMAR information.
 *
 * This function parses the DMAR table to extract DMA Remapping Hardware Unit (DRHD) information and populates
 * the provided `dmar_info` structure with the parsed data. For more details, refer to Section 8.1 of the Intel
 * Virtualization Technology for Directed I/O (VT-d) Architecture Specification, Revision 5.0.
 *
 * The following invalid cases are handled to ensure the integrity of the DMAR table and its entries:
 * - DMAR table is NULL: The function will assert and halt execution.
 * - The length value in the DMAR table header is less than the size of the DMAR table header: The function will
 *   assert and halt execution.
 * - DRHD entry with a zero base address: The function will assert and halt execution.
 * - Multiple PCI segments: The function will panic and halt execution.
 * - DMAR table with more than MAX_DRHDS DRHD entries: The function will assert and halt execution.
 * - More than one DRHD with the INCLUDE_PCI_ALL flag: The function will log an error message.
 *
 * @param[inout] plat_dmar Pointer to the structure where the parsed DMAR information will be stored.
 *
 * @return Always returns 0.
 *
 * @pre plat_dmar != NULL
 */
int32_t parse_dmar_table(struct dmar_info *plat_dmar)
{
	struct acpi_table_dmar *dmar_tbl;
	struct acpi_dmar_header *dmar_header;
	struct acpi_dmar_hardware_unit *acpi_drhd;
	char *ptr, *ptr_end;
	uint32_t include_all_idx = ~0U;
	uint16_t segment = 0;

	dmar_tbl = (struct acpi_table_dmar *)get_dmar_table();
	ASSERT(dmar_tbl != NULL, "");

	ptr = (char *)dmar_tbl + sizeof(*dmar_tbl);
	ptr_end = (char *)dmar_tbl + dmar_tbl->header.length;

	plat_dmar->drhd_units = drhd_info_array;
	for (; ptr < ptr_end; ptr += dmar_header->length) {
		dmar_header = (struct acpi_dmar_header *)ptr;
		ASSERT(dmar_header->length >= sizeof(struct acpi_dmar_header), "corrupted DMAR table");

		if (dmar_header->type == ACPI_DMAR_TYPE_HARDWARE_UNIT) {
			acpi_drhd = (struct acpi_dmar_hardware_unit *)dmar_header;
			/* Treat a valid DRHD has a non-zero base address */
			ASSERT(acpi_drhd->address != 0UL, "a zero base address DRHD. Please fix the BIOS.");

			if (dmar_unit_cnt == 0U) {
				segment = acpi_drhd->segment;
			} else {
				/* Only support single PCI Segment */
				if (segment != acpi_drhd->segment) {
					panic("Only support single PCI Segment.");
				}
			}

			if ((acpi_drhd->flags & DRHD_FLAG_INCLUDE_PCI_ALL_MASK) != 0U) {
				/* Check more than one DRHD with INCLUDE_PCI_ALL flag ? */
				include_all_idx = dmar_unit_cnt;
			}

			dmar_unit_cnt++;
			plat_dmar->drhd_units[dmar_unit_cnt - 1].devices = drhd_dev_scope[dmar_unit_cnt - 1];
			handle_one_drhd(acpi_drhd, &(plat_dmar->drhd_units[dmar_unit_cnt - 1]));
		}
	}

	if ((include_all_idx != ~0U) && (dmar_unit_cnt != (include_all_idx + 1U))) {
		pr_err("DRHD%d with INCLUDE_PCI_ALL flag is NOT the last one. Please fix the BIOS.", include_all_idx);
	}

	ASSERT(dmar_unit_cnt <= MAX_DRHDS, "parsed dmar_unit_cnt > MAX_DRHDS");
	plat_dmar->drhd_count = dmar_unit_cnt;

	return 0;
}
/**
 * @}
 */
