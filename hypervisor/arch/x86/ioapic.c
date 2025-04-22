/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <errno.h>
#include <irq.h>
#include <asm/lib/spinlock.h>
#include <asm/ioapic.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/mmu.h>
#include <acpi.h>
#include <logmsg.h>

/**
 * @defgroup hwmgmt_ioapic hwmgmt.ioapic
 *
 * @ingroup hwmgmt
 *
 * @brief The definition and implementation of physical IOAPIC.
 *
 * It provides external APIs for the operation on the physical IOAPIC
 * @{
 */

/**
 * @file ioapic.c
 *
 * @brief Functions to operate physical IOAPIC.
 *
 * This file implements functions to operate physical IOAPIC device.
 *
 */

/**
 * @brief The NR_MAX_GSI macro defines the maximum number of GSI, which is calculated by multiplying
 * the configured maximum number of IOAPICs (CONFIG_MAX_IOAPIC_NUM) by the maximum number of
 * lines per IOAPIC (CONFIG_MAX_IOAPIC_LINES).
 */
#define NR_MAX_GSI		(CONFIG_MAX_IOAPIC_NUM * CONFIG_MAX_IOAPIC_LINES)

/**
 * @brief The DEFAULT_DEST_MODE macro sets the default destination mode for IOAPIC redirection entries
 *  to logical destination mode (IOAPIC_RTE_DESTMODE_LOGICAL).
 */
#define DEFAULT_DEST_MODE	IOAPIC_RTE_DESTMODE_LOGICAL

/**
 * @brief The DEFAULT_DELIVERY_MODE macro sets the default delivery mode for IOAPIC redirection entries
 *   to lowest priority delivery mode (IOAPIC_RTE_DELMODE_LOPRI)
 */
#define DEFAULT_DELIVERY_MODE	IOAPIC_RTE_DELMODE_LOPRI

/**
 * @brief Represents a Global System Interrupt table entry
 *
 * This structure is used to store information about a specific GSI,
 * including its validity and associated IOAPIC details.
 *
 * @consistency The `is_valid` should be set to true if the corresponding
 * gsi falls in ranges identified by IOAPIC data in ACPI MADT in ioapic_setup_irqs.
 *
 * @remark Each item in global array gsi_table_data of this type is initialized, the `is_valid` set to false by
 * default, as all static variables in the `.bss` section are initialized to zero.
 *
 */
struct gsi_table {
	bool is_valid;/**< Indicates whether the GSI table entry is valid, true if valid, false otherwise. */
	struct {
		uint8_t acpi_id;/**< The IOAPIC’s ID in MADT. */
		uint8_t index;/**< Index of the IOAPIC,used to identify the specific IOAPIC in the system. */
		uint32_t pin;/**< Pin number on the IOAPIC,represents the specific interrupt pin being configured. */
		/**
		 * @brief Base address of the IOAPIC.
		 *
		 * Points to the memory-mapped I/O address for the APIC.
		 */
		void  *base_addr;
	} ioapic_info;/**< Structure containing detailed information about the IOAPIC configuration. */
};

/**
 * @brief Array to store Global System Interrupt table entries.
 *
 * This static array, `gsi_table_data`, is used to store multiple entries
 * of the GSI, each represented by a `gsi_table` structure. The array
 * size is defined by `NR_MAX_GSI`, which represents the maximum number of
 * GSI entries that can be handled. Each entry in the array contains information
 * about the IOAPIC configuration, including validity, ACPI ID, index, pin,
 * and base address. This array is crucial for managing and configuring interrupt
 * routing in the system.
 *
 */
static struct gsi_table gsi_table_data[NR_MAX_GSI];

/**
 * @brief This static variable, ioapic_max_nr_gsi, stores the maximum GSI number that the IOAPIC can handle.
 *
 */
static uint32_t ioapic_max_nr_gsi;

/**
 * @brief This static spinlock, ioapic_lock, is initialized with its head and tail pointers set to 0.
 *  It is used to ensure mutual exclusion when accessing IOAPIC-related resources.
 *
 */
static spinlock_t ioapic_lock = { .head = 0U, .tail = 0U, };

/**
 * @brief It is used to save the configuration of all RTEs before the system enters S3.
 *
 * The saved configuration of RTEs is restored to the hardware when the system resumes.
 *
 */
static union ioapic_rte saved_rte[CONFIG_MAX_IOAPIC_NUM][CONFIG_MAX_IOAPIC_LINES];

/**
 * @brief This static constant array defines the trigger modes for legacy IRQs.
 *
 * Each element corresponds to an IRQ pin, indexed from 0 to NR_LEGACY_PIN - 1.
 *
 */
static const uint32_t legacy_irq_trigger_mode[NR_LEGACY_PIN] = {
	IOAPIC_RTE_TRGRMODE_EDGE, /**< IRQ2*/
	IOAPIC_RTE_TRGRMODE_EDGE, /**< IRQ1*/
	IOAPIC_RTE_TRGRMODE_EDGE, /**< IRQ0*/
	IOAPIC_RTE_TRGRMODE_EDGE, /**< IRQ3*/
	IOAPIC_RTE_TRGRMODE_EDGE, /**< IRQ4*/
	IOAPIC_RTE_TRGRMODE_EDGE, /**< IRQ5*/
	IOAPIC_RTE_TRGRMODE_EDGE, /**< IRQ6*/
	IOAPIC_RTE_TRGRMODE_EDGE, /**< IRQ7*/
	IOAPIC_RTE_TRGRMODE_EDGE, /**< IRQ8*/
	IOAPIC_RTE_TRGRMODE_LEVEL, /**< IRQ9*/
	IOAPIC_RTE_TRGRMODE_EDGE, /**< IRQ10*/
	IOAPIC_RTE_TRGRMODE_EDGE, /**< IRQ11*/
	IOAPIC_RTE_TRGRMODE_EDGE, /**< IRQ12*/
	IOAPIC_RTE_TRGRMODE_EDGE, /**< IRQ13*/
	IOAPIC_RTE_TRGRMODE_EDGE, /**< IRQ14*/
	IOAPIC_RTE_TRGRMODE_EDGE, /**< IRQ15*/
};

/**
 * @brief This static constant array, pic_ioapic_pin_map, maps legacy IRQ pins to their corresponding IOAPIC pins.
 *
 * Each element in the array corresponds to a PIC pin number associated with a specific IOAPIC pin, which is
 * represented by the array index. It is used to retrieve the PIC pin ID from the IOAPIC pin index.
 *
 */
static const uint32_t pic_ioapic_pin_map[NR_LEGACY_PIN] = {
	2U, /**< pin0*/
	1U, /**< pin1*/
	0U, /**< pin2*/
	3U, /**< pin3*/
	4U, /**< pin4*/
	5U, /**< pin5*/
	6U, /**< pin6*/
	7U, /**< pin7*/
	8U, /**< pin8*/
	9U, /**< pin9*/
	10U, /**< pin10*/
	11U, /**< pin11*/
	12U, /**< pin12*/
	13U, /**< pin13*/
	14U, /**< pin14*/
	15U, /**< pin15*/
};

/**
 * @brief This static array, ioapic_array, holds the information for each IOAPIC in the system.
 * Its size is determined by CONFIG_MAX_IOAPIC_NUM, the maximum number of IOAPICs configured.
 *
 */
static struct ioapic_info ioapic_array[CONFIG_MAX_IOAPIC_NUM];

/**
 * @brief This static variable, ioapic_num, keeps track of the current number of IOAPICs detected
 *  and initialized in the system.
 *
 */
static uint8_t ioapic_num;

/**
 * @brief Retrieves the PIC pin ID from the IOAPIC pin index.
 *
 * This function maps an IOAPIC pin index to its corresponding PIC pin ID if the index is within the
 * range of legacy pins.
 *
 * @param[in] pin_index The index of the IOAPIC pin to be mapped to a PIC pin ID.
 *
 * @return Returns the PIC pin ID if the pin index is valid. Otherwise, returns INVALID_INTERRUPT_PIN.
 *
 */
uint32_t get_pic_pin_from_ioapic_pin(uint32_t pin_index)
{
	uint32_t pin_id = INVALID_INTERRUPT_PIN;
	if (pin_index < NR_LEGACY_PIN) {
		pin_id = pic_ioapic_pin_map[pin_index];
	}
	return pin_id;
}

/**
 * @brief Retrieves platform-specific IOAPIC information.
 *
 * This function provides a pointer to an array of IOAPIC information structures and return the number
 * of IOAPICs present on the platform.
 *
 * @param[out] plat_ioapic_info A pointer which will be set to the array of IOAPIC information structures.
 *
 * @return Returns the number of IOAPICs on the platform.
 *
 */
uint8_t get_platform_ioapic_info (struct ioapic_info **plat_ioapic_info)
{
	*plat_ioapic_info = ioapic_array;
	return ioapic_num;
}

/**
 * @brief Retrieves the IOAPIC index for a given GSI.
 *
 * This function return the index of the IOAPIC instance which was stored in the gsi_table_data table.
 *
 * @param[in] gsi The Global System Interrupt number for which to retrieve the IOAPIC index.
 *
 * @return Returns the IOAPIC index for the specified GSI.
 *
 */
uint8_t get_gsi_to_ioapic_index(uint32_t gsi)
{
	return gsi_table_data[gsi].ioapic_info.index;
}

/**
 * @brief Retrieves the base address of the IOAPIC for a given Global System Interrupt (GSI).
 *
 * This function maps a GSI to the base address of the corresponding IOAPIC in a predefined table
 * gsi_table_data[].
 *
 * @param[in] gsi The Global System Interrupt number for which to retrieve the IOAPIC base address.
 *
 * @return Returns a pointer to the base address of the IOAPIC for the specified GSI.
 *
 * @pre gsi < NR_MAX_GSI
 *
 */
void *gsi_to_ioapic_base(uint32_t gsi)
{

	return gsi_table_data[gsi].ioapic_info.base_addr;
}

/**
 * @brief Retrieves the maximum number of gsi supported.
 *
 * This function returns the maximum number of GSIs that can be handled by the system.
 *
 * @return Returns a 32-bit unsigned integer representing the maximum number of GSIs supported by the system.
 *
 */
uint32_t get_max_nr_gsi(void)
{
	return ioapic_max_nr_gsi;
}

/**
 * @brief Map the physical MMIO address of an IOAPIC to its corresponding virtual address.
 *
 * This function takes the physical address of an IOAPIC and returns its virtual address.
 * Currently, it assumes a 1:1 mapping between physical and virtual addresses, meaning the
 * physical address is directly translated to the virtual address without any offset or
 * complex translation.
 *
 * @param[in] ioapic_paddr The physical address of the IOAPIC to be mapped.
 *
 * @return Returns a pointer to the virtual address of the IOAPIC.
 *
 */
static void *map_ioapic(uint64_t ioapic_paddr)
{
	/* At some point we may need to translate this paddr to a vaddr.
	 * 1:1 mapping for now.
	 */
	return hpa2hva(ioapic_paddr);
}

/**
 * @brief Reads a 32-bit register from an IOAPIC.
 *
 * This function reads a 32-bit value from a specified offset within the IOAPIC's register space.
 * It writes the specified offset into the REGSEL register and retrieves the value from the register
 * at that offset by reading the IOWIN register.
 *
 * The function use the spinlock "ioapic_lock" to ensure atomic access and then performs the read
 * operation.
 *
 * @param[in] ioapic_base The base virtual address of the IOAPIC.
 * @param[in] offset The offset within the IOAPIC's register space to read from.
 *
 * @return Returns the 32-bit value read from the specified offset.
 *
 */
static inline uint32_t
ioapic_read_reg32(void *ioapic_base, const uint32_t offset)
{
	uint32_t v;
	uint64_t rflags;

	spinlock_irqsave_obtain(&ioapic_lock, &rflags);

	/* Write IOREGSEL */
	mmio_write32(offset, ioapic_base + IOAPIC_REGSEL);
	/* Read  IOWIN */
	v = mmio_read32(ioapic_base + IOAPIC_WINDOW);

	spinlock_irqrestore_release(&ioapic_lock, rflags);
	return v;
}

/**
 * @brief Writes a 32-bit value to a specified offset within an IOAPIC.
 *
 * This function writes a 32-bit value to a specified offset within the IOAPIC's register space.
 *
 * It writes the specified offset into the REGSEL register.
 *
 * This function use the spinlock "ioapic_lock" to ensure atomic access and then performs the write operation.
 *
 * @param[in] ioapic_base The base virtual address of the IOAPIC.
 * @param[in] offset The offset within the IOAPIC's register space to write to.
 * @param[in] value The 32-bit value to write to the specified offset.
 *
 * @return None
 *
 */
static inline void
ioapic_write_reg32(void *ioapic_base, const uint32_t offset, const uint32_t value)
{
	uint64_t rflags;

	spinlock_irqsave_obtain(&ioapic_lock, &rflags);

	/* Write IOREGSEL */
	mmio_write32(offset, ioapic_base + IOAPIC_REGSEL);
	/* Write IOWIN */
	mmio_write32(value, ioapic_base + IOAPIC_WINDOW);

	spinlock_irqrestore_release(&ioapic_lock, rflags);
}

/**
 * @brief Retrieves the base address of an IOAPIC from a globally defined array.
 *
 * This function retrieves the base address of an IOAPIC by indexing into a globally
 * defined array (`ioapic_array`) that is expected to be populated with IOAPIC
 * information during system initialization, typically by parsing the ACPI MADT table.
 *
 * @param[in] apic_id The unique identifier for the IOAPIC whose base address is to be
 *                retrieved. This value must be less than 2, as the function assumes
 *                that the `ioapic_array` has at least two elements.
 *
 * @return The base virtual address of the specified IOAPIC.
 *
 */
static inline uint64_t
get_ioapic_base(uint8_t apic_id)
{
	/* the ioapic base should be extracted from ACPI MADT table */
	return ioapic_array[apic_id].addr;
}

/**
 * @brief Retrieves the RTE Entry for a specified IOAPIC pin.
 *
 * This function retrieves the RTE for a specified pin on an IOAPIC. The RTE
 * contains configuration information for how interrupts from the specified pin
 * should be handled by the IOAPIC. The function reads the RTE from the specified
 * IOAPIC base address using two 32-bit reads to capture the full 64-bit RTE value.
 *
 * @param[in] ioapic_base The base virtual address of the IOAPIC whose RTE is to be
 *                    retrieved.
 * @param[in] pin         The pin number on the IOAPIC whose RTE is to be retrieved.
 *
 * @param[out] rte         A pointer to a union that will hold the retrieved RTE.
 *
 * @return None.
 *
 */
void ioapic_get_rte_entry(void *ioapic_base, uint32_t pin, union ioapic_rte *rte)
{
	uint32_t rte_addr = (pin * 2U) + 0x10U;
	rte->u.lo_32 = ioapic_read_reg32(ioapic_base, rte_addr);
	rte->u.hi_32 = ioapic_read_reg32(ioapic_base, rte_addr + 1U);
}

/**
 * @brief Sets the RTE for a specified IOAPIC pin.
 *
 * This function sets the RTE for a specified pin on an IOAPIC. The RTE contains
 * configuration information for how interrupts from the specified pin should be
 * handled by the IOAPIC. The function writes the RTE to the specified IOAPIC base
 * address using two 32-bit writes to capture the full 64-bit RTE value.
 *
 * @param[in] ioapic_base The base virtual address of the IOAPIC whose RTE is to be set.
 * @param[in] pin         The pin number on the IOAPIC whose RTE is to be set.
 *
 * @param[in] rte         A union containing the RTE to be set.
 *
 * @return None.
 */
static inline void
ioapic_set_rte_entry(void *ioapic_base,
		uint32_t pin, union ioapic_rte rte)
{
	uint32_t rte_addr = (pin * 2U) + 0x10U;
	ioapic_write_reg32(ioapic_base, rte_addr, rte.u.lo_32);
	ioapic_write_reg32(ioapic_base, rte_addr + 1U, rte.u.hi_32);
}

/**
 * @brief Creates an RTE for a legacy IRQ with specified vector and other default settings.
 *
 * This function creates an RTE for a legacy IRQ with specified vector and default settings
 * for other fields. Legacy IRQs are typically IRQs 0-15 and are used by older hardware and
 * software. The RTE is configured with a set of default values that are suitable for most
 * legacy IRQ scenarios, including masked interrupts, trigger mode, destination mode, delivery
 * mode, vector, polarity, and destination field.
 *
 * @param[in] irq The legacy IRQ number.
 * @param[in] vr  The vector number to be assigned to the IRQ.
 *
 * @return A union ioapic_rte containing the configured RTE.
 *
 */
static inline union ioapic_rte
create_rte_for_legacy_irq(uint32_t irq, uint32_t vr)
{
	union ioapic_rte rte;

	/* Legacy IRQ 0-15 setup, default masked
	 * are actually defined in either MPTable or ACPI MADT table
	 * before we have ACPI table parsing in HV we use common hardcode
	 */

	rte.full = 0UL;
	rte.bits.intr_mask  = IOAPIC_RTE_MASK_SET;
	rte.bits.trigger_mode = legacy_irq_trigger_mode[irq];
	rte.bits.dest_mode = DEFAULT_DEST_MODE;
	rte.bits.delivery_mode = DEFAULT_DELIVERY_MODE;
	rte.bits.vector = vr;

	/* Fixed to active high */
	rte.bits.intr_polarity = IOAPIC_RTE_INTPOL_AHI;

	/* Dest field: legacy irq fixed to CPU0 */
	rte.bits.dest_field = 1U;

	return rte;
}

/**
 * @brief Create an IOAPIC Redirection Table Entry (RTE) for a GSI IRQ.
 *
 * This function initializes an RTE for a given GSI IRQ and vector. It handles both
 * legacy and non-legacy IRQs.
 *
 * For legacy IRQs, which are typically IRQs 0-15 used by older hardware and software,
 * the function calls `create_rte_for_legacy_irq` to apply default settings such as
 * interrupt masking, predefined trigger mode, logical destination mode, fixed delivery
 * mode, and active high polarity, the vector is allocated dynamically..
 *
 * For non-legacy IRQs, the function sets default values for various fields such as
 * interrupt mask, trigger mode, destination mode, delivery mode, interrupt polarity,
 * and destination field, the vector being specified by the caller.
 *
 * @param[in] irq The GSI IRQ number to create the RTE for.
 * @param[in]  vr The vector number to associate with the IRQ.
 *
 * @return A union ioapic_rte structure initialized with the appropriate values for the given IRQ.
 *
 */
static inline union ioapic_rte
create_rte_for_gsi_irq(uint32_t irq, uint32_t vr)
{
	union ioapic_rte rte;

	rte.full = 0UL;

	if (irq < NR_LEGACY_PIN) {
		rte = create_rte_for_legacy_irq(irq, vr);
	} else {
		/* irq default masked, level trig */
		rte.bits.intr_mask  = IOAPIC_RTE_MASK_SET;
		rte.bits.trigger_mode = IOAPIC_RTE_TRGRMODE_LEVEL;
		rte.bits.dest_mode = DEFAULT_DEST_MODE;
		rte.bits.delivery_mode = DEFAULT_DELIVERY_MODE;
		rte.bits.vector = vr;

		/* Fixed to active high */
		rte.bits.intr_polarity = IOAPIC_RTE_INTPOL_AHI;

		/* Dest field */
		rte.bits.dest_field = (uint8_t) ALL_CPUS_MASK;
	}

	return rte;
}

/**
 * @brief Sets the routing for a GSI IRQ.
 *
 * This function is responsible for configuring the routing of a GSI.
 * This function sets up the routing configuration for a GSI by creating and assigning
 * a RTE to the IOAPIC pin associated with the specified GSI.
 * It ensures that the interrupt is correctly routed to the appropriate vector and updates
 * the trigger mode in the IRQ descriptor based on the RTE settings.
 *
 * @param[in] gsi  The GSI number of the IRQ to be configured.
 * @param[in] vr   The vector number to be assigned to the IRQ.
 *
 * @return None.
 *
 * @details
 * - The function first retrieves the base address of the IOAPIC associated with the GSI
 *   using `gsi_to_ioapic_base`.
 * - It then creates an RTE for the specified GSI and vector by call the `create_rte_for_gsi_irq`.
 * - The RTE is set for the IOAPIC pin using `ioapic_set_rte_entry`, which updates the IOAPIC's
 *   configuration to route the interrupt as specified.
 * - The function checks the trigger mode specified in the RTE and updates the IRQ descriptor
 *   accordingly by calling `set_irq_trigger_mode`. This ensures that the system correctly
 *   handles level-triggered or edge-triggered interrupts.
 * - Debug information is logged to provide insights into the configuration process, including
 *   the GSI, pin number, and full RTE value.
 *
 */
static void ioapic_set_routing(uint32_t gsi, uint32_t vr)
{
	void *ioapic_base;
	union ioapic_rte rte;

	ioapic_base = gsi_to_ioapic_base(gsi);
	rte = create_rte_for_gsi_irq(gsi, vr);
	ioapic_set_rte_entry(ioapic_base, gsi_table_data[gsi].ioapic_info.pin, rte);

	if (rte.bits.trigger_mode == IOAPIC_RTE_TRGRMODE_LEVEL) {
		set_irq_trigger_mode(gsi, true);
	} else {
		set_irq_trigger_mode(gsi, false);
	}

	dev_dbg(DBG_LEVEL_IRQ, "GSI: irq:%d pin:%hhu rte:%lx",
		gsi, gsi_table_data[gsi].ioapic_info.pin,
		rte.full);
}

/**
 * @brief Retrieves the Redirection Table Entry (RTE) for a specified IRQ.
 *
 * This function is used to obtain the current configuration of a RTE for a given IRQ by
 * calling ioapic_get_rte_entry.
 *
 * @param[in] irq  The IRQ number for which to retrieve the RTE.
 * @param[out] rte  A pointer to a `union ioapic_rte` structure where the retrieved RTE
 *             will be stored.
 * @pre rte != NULL
 * @pre is_ioapic_irq(irq) == true
 *
 * @return None.
 *
 */
void ioapic_get_rte(uint32_t irq, union ioapic_rte *rte)
{
	void *addr;

	addr = gsi_to_ioapic_base(irq);
	ioapic_get_rte_entry(addr, gsi_table_data[irq].ioapic_info.pin, rte);
}

/**
 * @brief Sets the RTE for a specified IRQ.
 *
 * This function is used to configure a RTE for a given IRQ by calling the ioapic_set_rte_entry.
 *
 * @param[in] irq  The IRQ number for which to set the RTE.
 * @param[in] rte  A `union ioapic_rte` structure containing the RTE value to set.
 *
 * @pre is_ioapic_irq(irq) == true
 *
 * @return None.
 *
 */
void ioapic_set_rte(uint32_t irq, union ioapic_rte rte)
{
	void *addr;

	addr = gsi_to_ioapic_base(irq);
	ioapic_set_rte_entry(addr, gsi_table_data[irq].ioapic_info.pin, rte);

	dev_dbg(DBG_LEVEL_IRQ, "GSI: irq:%d pin:%hhu rte:%lx",
		irq, gsi_table_data[irq].ioapic_info.pin,
		rte.full);
}

/**
 * @brief Checks if a given GSI is valid.
 *
 * This function verifies whether a specified GSI number is within a valid range and
 * has been marked as valid in the GSI table.
 * The validation criteria include:
 * 1) gsi < NR_MAX_GSI
 * 2) The GSI is valid on the platform according to the ACPI MADT information
 *
 * @param[in] gsi The GSI number to check
 *
 * @return Returns `true` if the GSI is within the valid range and has been marked
 *         as valid in the GSI table. Otherwise, returns `false`.
 *
 */
bool is_gsi_valid(uint32_t gsi)
{

	return (gsi < NR_MAX_GSI) && (gsi_table_data[gsi].is_valid);
}

/**
 * @brief Checks if a given IRQ number is associated with an IOAPIC.
 *
 * This function determines whether a specified IRQ number corresponds to an interrupt
 * that is handled by an I/O Advanced Programmable IOAPIC.
 * gsi and irq are identity mapped in ioapic_setup_irqs, so, gsi = irq for ACRN
 *
 * @param[in] irq  The IRQ number to check.
 *
 * @return Returns `true` if the IRQ number maps to a valid GSI and is therefore
 *         likely associated with an IOAPIC. Otherwise, returns `false`.
 *
 */
bool is_ioapic_irq(uint32_t irq)
{

	return is_gsi_valid(irq);
}

/**
 * @brief Converts a GSI number to the corresponding IOAPIC pin.
 *
 * This function maps a given GSI number to the specific pin on an IOAPIC that handles
 * that interrupt.
 *
 * @param[in] gsi  The GSI number to convert.
 *
 * @pre gsi < NR_MAX_GSI
 * @pre is_gsi_valid(gsi) == true
 *
 * @return Returns the IOAPIC pin number that corresponds to the given GSI number.
 *
 */
uint32_t gsi_to_ioapic_pin(uint32_t gsi)
{
	return gsi_table_data[gsi].ioapic_info.pin;
}

/**
 * @brief Converts a GSI number to the corresponding IRQ number.
 *
 * In this implementation, the function performs a direct identity mapping
 * between GSI numbers and IRQ numbers. This means that the GSI number is
 * returned as the IRQ number without any transformation.
 *
 * The GSI and IRQ are identity mapped in ioapic_setup_irqs function.
 *
 * @param[in] gsi  The GSI number to convert.
 *
 * @pre is_gsi_valid(gsi) == true
 *
 * @return Returns the IRQ number that corresponds to the given GSI number, in this
 *         implementation, simply returns the GSI number itself.
 *
 */
uint32_t ioapic_gsi_to_irq(uint32_t gsi)
{
	return gsi;
}

/**
 * @brief Masks or unmasks an IRQ based on its GSI number.
 *
 * This function masks (disables) or unmasks (enables) an IRQ based on its GSI number by
 * setting or clearing the intr_mask bit in the RTE.
 * All other bits in the RTE are left unchanged..
 *
 * @param[in] irq    The IRQ number.
 * @param[in] mask   A boolean value indicating whether to mask (true) or unmask (false)
 *               the IRQ.
 *
 * @return None.
 *
 */
static void
ioapic_irq_gsi_mask_unmask(uint32_t irq, bool mask)
{
	void *addr = NULL;
	uint32_t pin;
	union ioapic_rte rte;

	addr = gsi_to_ioapic_base(irq);
	pin = gsi_table_data[irq].ioapic_info.pin;

	if (addr != NULL) {
		ioapic_get_rte_entry(addr, pin, &rte);
		if (mask) {
			rte.bits.intr_mask = IOAPIC_RTE_MASK_SET;
		} else {
			rte.bits.intr_mask = IOAPIC_RTE_MASK_CLR;
		}
		ioapic_set_rte_entry(addr, pin, rte);
		dev_dbg(DBG_LEVEL_PTIRQ, "update: irq:%d pin:%hhu rte:%lx",
			irq, pin, rte.full);
	} else {
		dev_dbg(DBG_LEVEL_PTIRQ, "NULL Address returned from gsi_table_data");
	}
}

/**
 * @brief Masks an IRQ by its number using the IOAPIC.
 *
 * This function disables (masks) a specific IRQ by its number by call
 * the ioapic_irq_gsi_mask_unmask function.
 *
 * @param[in] irq The number of the IRQ to be masked.
 *
 * @return None.
 */
void ioapic_gsi_mask_irq(uint32_t irq)
{
	ioapic_irq_gsi_mask_unmask(irq, true);
}

/**
 * @brief Unmasks an IRQ by its number using the IOAPIC.
 *
 * This function enables (unmasks) a specific IRQ by calling the
 * ioapic_irq_gsi_mask_unmask.
 *
 * @param[in] irq The number of the IRQ to be unmasked.
 *
 * @return None.
 */
void ioapic_gsi_unmask_irq(uint32_t irq)
{
	ioapic_irq_gsi_mask_unmask(irq, false);
}

/**
 * @brief Determines the number of interrupt input pins supported by an IOAPIC.
 *
 * This function reads the version register of the IOAPIC at the provided base
 * address and extracts the number of interrupt input pins supported by the IOAPIC.
 *
 * This function reads the version register of the IOAPIC and extracts the number
 * of interrupt input pins supported.
 * The number of pins is derived from the value in bits 23:16 of the version register,
 * which represents the highest entry in the I/O redirection table. Since this value
 * is 1 smaller than the number of interrupt input pins, the function adds 1 to compute
 * the total number of pins.
 *
 * @param[in] ioapic_base The base address of the IOAPIC.
 *
 * @return The number of interrupt input pins supported by the IOAPIC.
 *
 */
static uint32_t
ioapic_nr_pins(void *ioapic_base)
{
	uint32_t version;
	uint32_t nr_pins;

	version = ioapic_read_reg32(ioapic_base, IOAPIC_VER);
	dev_dbg(DBG_LEVEL_IRQ, "IOAPIC version: %x", version);

	/* The 23:16 bits in the version register is the highest entry in the
	 * I/O redirection table, which is 1 smaller than the number of
	 * interrupt input pins. */
	nr_pins = (((version & IOAPIC_MAX_RTE_MASK) >> MAX_RTE_SHIFT) + 1U);


	return nr_pins;
}

/**
 * @brief Converts an IRQ number to the corresponding IOAPIC ID.
 *
 * This function takes an IRQ number as input and returns the ID of the IOAPIC
 * that handles this IRQ. It assumes that the provided IRQ number is valid and
 * corresponds to an IOAPIC-managed interrupt.
 * @pre is_ioapic_irq(irq) == true
 *
 * @param[in] irq The IRQ number to be converted.
 *
 * @return The ID of the IOAPIC that handles the specified IRQ.
 *
 */
uint8_t ioapic_irq_to_ioapic_id(uint32_t irq)
{

	return gsi_table_data[irq].ioapic_info.acpi_id;
}

/**
 * @brief Initializes IOAPIC information and validates configuration.
 *
 * This function initializes the IOAPIC array by parsing the Multiple APIC
 * Description Table (MADT) and validating the configuration of each IOAPIC,
 * the MMIO regions of all these IOAPIC instances are marked as supervisor memory
 *
 * @return A value to indicate the status of the initialization.
 *
 * @retval 0 Success
 * @retval -EINVAL Failure due to one of the following reasons:
 *         - The number of IOAPICs exceeds the maximum allowed by `CONFIG_MAX_IOAPIC_NUM`.
 *         - The number of pins on an IOAPIC exceeds the maximum allowed by `CONFIG_MAX_IOAPIC_LINES`.
 *         - The total pin count is less than the number of legacy IRQs (`NR_LEGACY_IRQ`).
 */
int32_t init_ioapic_id_info(void)
{
	int32_t ret = 0;
	uint8_t ioapic_id;
	void *addr;
	uint32_t nr_pins, gsi;

	ioapic_num = parse_madt_ioapic(&ioapic_array[0]);
	if (ioapic_num <= (uint8_t)CONFIG_MAX_IOAPIC_NUM) {
		/*
		 * Iterate thru all the IO-APICs on the platform
		 * Check the number of pins available on each IOAPIC is less
		 * than the CONFIG_MAX_IOAPIC_LINES
		 */

		gsi = 0U;
		for (ioapic_id = 0U; ioapic_id < ioapic_num; ioapic_id++) {
			addr = map_ioapic(ioapic_array[ioapic_id].addr);
			set_paging_supervisor((uint64_t)addr, PAGE_SIZE);

			nr_pins = ioapic_nr_pins(addr);
			if (nr_pins <= (uint32_t) CONFIG_MAX_IOAPIC_LINES) {
				gsi += nr_pins;
				ioapic_array[ioapic_id].nr_pins = nr_pins;
			} else {
				/*
				 * Error: The number of pins on the current IOAPIC exceeds the maximum
				 * allowed by CONFIG_MAX_IOAPIC_LINES. This suggests that the configuration
				 * for maximum IOAPIC lines should be increased to accommodate this IOAPIC.
				 */
				pr_err("Pin count %x of IOAPIC with %x > CONFIG_MAX_IOAPIC_LINES, bump up "
				       "CONFIG_MAX_IOAPIC_LINES!",
					nr_pins, ioapic_array[ioapic_id].id);
				ret = -EINVAL;
				break;
			}
		}

		/*
		 * Check if total pin count, can be inferred by GSI, is
		 * atleast same as the number of Legacy IRQs, NR_LEGACY_IRQ
		 */

		if (ret == 0) {
			if (gsi < (uint32_t) NR_LEGACY_PIN) {
				pr_err ("Total pin count (%x) is less than NR_LEGACY_IRQ!", gsi);
				ret = -EINVAL;
			}
		}
	} else {
		/*
		 * Error: The number of IOAPICs detected on the platform exceeds the maximum
		 * allowed by CONFIG_MAX_IOAPIC_NUM. This suggests that the configuration
		 * for maximum IOAPICs should be increased to accommodate all detected IOAPICs.
		 */
		pr_err("Number of IOAPIC on platform %x > CONFIG_MAX_IOAPIC_NUM, try bumping up CONFIG_MAX_IOAPIC_NUM!",
			ioapic_num);
		ret = -EINVAL;
	}

	return ret;
}

/**
 * @brief Setup IRQs for all IOAPICs
 *
 * This function initializes the IRQs for all IOAPICs in the system. It iterates through
 * each IOAPIC, maps its physical address to a virtual address, and then iterates through
 * each pin of the IOAPIC.
 *
 * Upon completion, the global variable `ioapic_max_nr_gsi` is initialized to the total number
 * of GSIs configured across all IOAPICs. This value represents the maximum number of GSIs that
 * the system can handle, and is used for validation and configuration purposes in other parts
 * of the system.
 *
 * @return None.
 */
void ioapic_setup_irqs(void)
{
	uint8_t ioapic_id;
	uint32_t gsi = 0U;
	uint32_t vr;

	for (ioapic_id = 0U;
	     ioapic_id < ioapic_num; ioapic_id++) {
		void *addr;
		uint32_t pin, nr_pins;

		addr = map_ioapic(ioapic_array[ioapic_id].addr);

		nr_pins = ioapic_array[ioapic_id].nr_pins;
		gsi = ioapic_array[ioapic_id].gsi_base;
		for (pin = 0U; pin < nr_pins; pin++) {
			gsi_table_data[gsi].is_valid = true;
			gsi_table_data[gsi].ioapic_info.acpi_id = ioapic_array[ioapic_id].id;
			gsi_table_data[gsi].ioapic_info.base_addr = addr;
			gsi_table_data[gsi].ioapic_info.pin = pin;
			gsi_table_data[gsi].ioapic_info.index = ioapic_id;

			/* pinned irq before use it */
			if (reserve_irq_num(gsi) == IRQ_INVALID) {
				pr_err("failed to alloc IRQ[%d]", gsi);
				gsi++;
				continue;
			}

			/* assign vector for this GSI
			 * for legacy irq, reserved vector and never free
			 */
			if (gsi < NR_LEGACY_PIN) {
				vr = alloc_irq_vector(gsi);
				if (vr == VECTOR_INVALID) {
					pr_err("failed to alloc VR");
					gsi++;
					continue;
				}
			} else {
				vr = 0U; /* not to allocate VR right now */
			}

			ioapic_set_routing(gsi, vr);
			gsi++;
		}
	}

	/* system max gsi numbers */
	ioapic_max_nr_gsi = gsi;
}

/**
 * @brief Suspend IOAPIC by saving its RTEs
 *
 * This function iterates through all IOAPICs and saves the RTE for each pin of each IOAPIC
 * into the saved_rte array.
 *
 * @return None.
 */
void suspend_ioapic(void)
{
	uint8_t ioapic_id;
	uint32_t ioapic_pin;

	for (ioapic_id = 0U; ioapic_id < ioapic_num; ioapic_id++) {
		void *addr;

		addr = map_ioapic(get_ioapic_base(ioapic_id));
		for (ioapic_pin = 0U; ioapic_pin < ioapic_array[ioapic_id].nr_pins; ioapic_pin++) {
			ioapic_get_rte_entry(addr, ioapic_pin,
				&saved_rte[ioapic_id][ioapic_pin]);
		}
	}
}

/**
 * @brief Resume IOAPIC by restoring its RTEs
 *
 * This function iterates through all IOAPICs and restores the RTE for each pin of each IOAPIC
 * from the `saved_rte` array.
 *
 * @return None.
 *
 */
void resume_ioapic(void)
{
	uint8_t ioapic_id;
	uint32_t ioapic_pin;

	for (ioapic_id = 0U; ioapic_id < ioapic_num; ioapic_id++) {
		void *addr;

		addr = map_ioapic(get_ioapic_base(ioapic_id));
		for (ioapic_pin = 0U; ioapic_pin < ioapic_array[ioapic_id].nr_pins; ioapic_pin++) {
			ioapic_set_rte_entry(addr, ioapic_pin,
				saved_rte[ioapic_id][ioapic_pin]);
		}
	}
}

/**
 * @}
 */
