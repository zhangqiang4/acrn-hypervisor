/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ACPI_H
#define ACPI_H

#include <board_info.h>
/**
 * @addtogroup hwmgmt_hw-caps
 * @{
 */
/**
 * @file
 * @brief Interface definitions for parsing ACPI in the hwmgmt.hw-caps module.
 *
 * This file contains the definitions of structures and macros, as well as the declarations of functions for parsing
 * ACPI tables and fixing up the ACPI tables if CONFIG_ACPI_PARSE_ENABLED is enabled.
 */
#define ACPI_RSDP_CHECKSUM_LENGTH   20U /**< Length of RSDP checksum */
#define ACPI_RSDP_XCHECKSUM_LENGTH  36U /**< Length of RSDP checksum for ACPI 2.0+ */

#define ACPI_NAME_SIZE              4U	/**< Size of ACPI table name */
#define ACPI_OEM_ID_SIZE            6U	/**< Size of ACPI OEM ID */
#define ACPI_OEM_TABLE_ID_SIZE      8U	/**< Size of ACPI OEM table ID */

#define ACPI_MADT_TYPE_LOCAL_APIC     0U	/**< Type for Local APIC */
#define ACPI_MADT_TYPE_X2APIC         9U	/**< Type for x2APIC */
#define ACPI_MADT_TYPE_IOAPIC         1U	/**< Type for I/O APIC */
#define ACPI_MADT_ENABLED             1U	/**< Enabled Local APIC, x2APIC, or I/O APIC */
#define ACPI_MADT_TYPE_LOCAL_APIC_NMI 4U	/**< Type for Local APIC NMI */

#define ACPI_DMAR_TYPE_HARDWARE_UNIT      0U	/**< Type for Hardware Unit Definition */
#define ACPI_DMAR_TYPE_RESERVED_MEMORY    1U	/**< Type for Reserved Memory Definition */
#define ACPI_DMAR_TYPE_ROOT_ATS           2U	/**< Type for Root Port ATS Capability Reporting */
#define ACPI_DMAR_TYPE_HARDWARE_AFFINITY  3U	/**< Type for Hardware Affinity Reporting */
#define ACPI_DMAR_TYPE_NAMESPACE          4U	/**< Type for Namespace Device Declaration */
#define ACPI_DMAR_TYPE_RESERVED           5U	/**< Reserved type */

/* FACP field offsets */
#define OFFSET_FACS_ADDR        36U	/**< Offset for FACS address */
#define OFFSET_RESET_REGISTER   116U	/**< Offset for Reset register address */
#define OFFSET_RESET_VALUE      128U	/**< Offset for value to write to reset register */
#define OFFSET_FACS_X_ADDR      132U	/**< Offset for FACS 64-bit address */
#define OFFSET_PM1A_EVT         148U	/**< Offset for PM1a event address */
#define OFFSET_PM1A_CNT         172U	/**< Offset for PM1a control address */

/* FACS field offsets */
#define OFFSET_FACS_SIGNATURE    0U	/**< Offset for FACS signature */
#define OFFSET_FACS_LENGTH       4U	/**< Offset for FACS length */
#define OFFSET_WAKE_VECTOR_32    12U	/**< Offset for Wake vector 32-bit address */
#define OFFSET_WAKE_VECTOR_64    24U	/**< Offset for Wake vector 64-bit address */

/* MCFG field offsets */
#define OFFSET_MCFG_LENGTH       4U	/**< Offset for MCFG length */
#define OFFSET_MCFG_ENTRY0       44U	/**< Offset for MCFG entry0 */
#define OFFSET_MCFG_ENTRY0_BASE  44U	/**< Offset for MCFG entry0 base address */
#define OFFSET_MCFG_ENTRY1       60U	/**< Offset for MCFG entry1 */

#define ACPI_SIG_FADT           "FACP"		/**< Signature for Fixed ACPI Description Table */
#define ACPI_SIG_FACS           0x53434146U	/**< Signature for Firmware ACPI Control Structure */
#define ACPI_SIG_RSDP           "RSD PTR "	/**< Signature for Root System Description Pointer */
#define ACPI_SIG_XSDT           "XSDT"		/**< Signature for Extended System Description Table */
#define ACPI_SIG_MADT           "APIC"		/**< Signature for Multiple APIC Description Table */
#define ACPI_SIG_DMAR           "DMAR"		/**< Signature for DMA Remapping table */
#define ACPI_SIG_MCFG           "MCFG"		/**< Signature for Memory Mapped Configuration table */
#define ACPI_SIG_DSDT           "DSDT"		/**< Signature for Differentiated System Description Table */
#define ACPI_SIG_TPM2           "TPM2"		/**< Signature for Trusted Platform Module hardware interface table */
#define ACPI_SIG_HPET           "HPET"		/**< Signature for High Precision Event Timer table */

/**
 * @brief Data structure to represent a Generic Address Structure (GAS) in ACPI.
 *
 * This structure is used to define the address space and access parameters for a specific hardware resource in the
 * ACPI (Advanced Configuration and Power Interface) specification.
 *
 * @remark This structure shall be packed.
 */
struct packed_gas {
	/** @brief Identifies the address space where the resource is located.
	 *         Common values include:
	 *         - 0x00 - System Memory
	 *         - 0x01 - System I/O
	 *         - 0x02 - PCI Configuration Space
	 *         - 0x03 - Embedded Controller
	 *         - 0x04 - SMBus
	 */
	uint8_t 	space_id;
	uint8_t 	bit_width;	/**< The width, in bits, of the resource being accessed. */

	uint8_t 	bit_offset;	/**< The bit offset within the resource. */
	/** @brief The size of the access, in bytes.
	 *	   Common values include:
	 *         - 0 - Undefined
	 *         - 1 - Byte Access
	 *         - 2 - Word Access
	 *         - 3 - DWord Access
	 *         - 4 - QWord Access
	 */
	uint8_t 	access_size;
	uint64_t	address;	/**< The address of the resource. */
} __packed;

/**
 * @brief Data structure representing the ACPI table header.
 *
 * This structure contains common information found in ACPI table headers.
 *
 * @remark This structure shall be packed.
 */
struct acpi_table_header {
	char                    signature[4];		/**< ASCII signature of this table */
	uint32_t                length;			/**< Length of table in bytes, including this header */
	uint8_t                 revision;		/**< ACPI Specification minor version number */
	uint8_t                 checksum;		/**< Checksum of entire table == 0 */
	char                    oem_id[6];		/**< ASCII OEM identification */
	char                    oem_table_id[8];	/**< ASCII OEM table identification */
	uint32_t                oem_revision;		/**< OEM revision number */
	char                    asl_compiler_id[4];	/**< ASCII ASL compiler vendor ID */
	uint32_t                asl_compiler_revision;	/**< ASL compiler version */
} __packed;

/**
 * @brief Data structure representing the RSDP table.
 *
 * This structure contains all the information for the RSDP (Root System Description Pointer) table.
 *
 * @remark This structure shall be packed.
 */
struct acpi_table_rsdp {
	char signature[8];		/**< RSDP table signature, contains "RSD PTR" */
	uint8_t checksum;		/**< Checksum of this table */
	char oem_id[ACPI_OEM_ID_SIZE];	/**< OEM identification */
	uint8_t revision;		/**< Revision number, must be (0) for ACPI 1.0 or (2) for ACPI 2.0+ */
	uint32_t rsdt_physical_address;	/**< 32bits GPA of the RSDT */
	uint32_t length;		/**< Table length in bytes, including header (ACPI 2.0+) */
	uint64_t xsdt_physical_address;	/**< 64bits GPA of the XSDT (ACPI 2.0+) */
	uint8_t extended_checksum;	/**< Checksum of entire table (ACPI 2.0+) */
	uint8_t reserved[3];		/**< Reserved, must be zero */
} __packed;

/**
 * @brief Data structure representing the RSDT table.
 *
 * This structure represents the Root System Description Table (RSDT) and contains information about its sub-tables.
 *
 * @remark This structure shall be packed.
 */
struct acpi_table_rsdt {
	struct acpi_table_header   header;			/**< Common ACPI table header */
	uint32_t                   table_offset_entry[1];	/**< Array of pointers to ACPI tables */
} __packed;

/**
 * @brief Data structure representing the XSDT table.
 *
 * This structure represents the Extended System Description Table (XSDT) and contains information about its
 * sub-tables.
 *
 * @remark This structure shall be packed.
 */
struct acpi_table_xsdt {

	struct acpi_table_header header;	/**< Common ACPI table header */
	uint64_t table_offset_entry[1];		/**< Array of pointers to ACPI tables */
} __packed;

/**
 * @brief Represents the Fixed ACPI Description Table (FADT).
 *
 * This structure contains information about the Fixed ACPI Description Table (FADT), which provides various fixed
 * hardware information to the operating system.
 *
 * @remark This structure shall be packed.
 */

struct acpi_table_fadt {
	struct acpi_table_header header;/**< Common ACPI table header */
	uint32_t facs;			/**< 32-bit physical address of FACS */
	uint32_t dsdt;			/**< 32-bit physical address of DSDT */
	uint8_t unused0[12];		/**< ACRN doesn't use these fields */
	uint32_t pm1a_event_block;	/**< 32-bit port address of Power Mgt 1a Event Reg Blk */
	uint32_t pm1b_event_block;	/**< 32-bit port address of Power Mgt 1b Event Reg Blk */
	uint32_t pm1a_control_block;	/**< 32-bit port address of Power Mgt 1a Control Reg Blk */
	uint32_t pm1b_control_block;	/**< 32-bit port address of Power Mgt 1b Control Reg Blk */
	uint8_t unused1[16];		/**< ACRN doesn't use these fields */
	uint8_t pm1_event_length;	/**< Byte Length of ports at pm1x_event_block */
	uint8_t pm1_control_length;	/**< Byte Length of ports at pm1x_control_block */
	uint8_t unused2[22];		/**< ACRN doesn't use these fields */
	uint32_t flags;			/**< Miscellaneous flag bits (see below for individual flags) */
	uint8_t unused3[128];		/**< ACRN doesn't use these fields */
} __packed;

/**
 * @brief Represents the ACPI MCFG table structure.
 *
 * This structure defines the Memory-Mapped Configuration Space Base Address Description Table (MCFG) as per the ACPI
 * specification. It includes a common ACPI table header and a reserved field.
 *
 * @remark This structure shall be packed.
 */
struct acpi_table_mcfg {
	struct acpi_table_header header;	/**< Common ACPI table header */
	uint8_t reserved[8];			/**< Reserved field for future use */
} __packed;

/**
 * @brief Represents an ACPI MCFG (Memory-Mapped Configuration Space) allocation structure.
 *
 * This structure describes the base address and bus number range for a PCI segment group.
 *
 * @remark This structure shall be packed.
 */
struct acpi_mcfg_allocation {
	uint64_t address;		/**< Base address, processor-relative */
	uint16_t pci_segment;		/**< PCI segment group number */
	uint8_t start_bus_number;	/**< Starting PCI Bus number */
	uint8_t end_bus_number;		/**< Final PCI Bus number */
	uint32_t reserved; 		/**< Reserved field for future use */
} __packed;

/**
 * @brief Represents the ACPI Multiple APIC Description Table (MADT).
 *
 * This structure contains the common ACPI table header, the physical address of the local APIC, and flags associated
 * with the MADT table.
 *
 * @remark This structure shall be packed.
 */

struct acpi_table_madt {
	struct acpi_table_header     header;	/**< Common ACPI table header */

	uint32_t                     address;	/**< Physical address of local APIC */
	uint32_t                     flags;	/**< Flags associated with the MADT table. */
} __packed;

/**
 * @brief Represents the header of an ACPI sub-table.
 *
 * This structure defines the common header for ACPI sub-tables, which includes the type and length of the sub-table.
 *
 * @remark This structure shall be packed.
 */
struct acpi_subtable_header {
	uint8_t                   type; 	/**< Sub-table type */
	uint8_t                   length; 	/**< Length of the sub-table */
} __packed;

/**
 * @brief Represents a local APIC entry in the ACPI MADT (Multiple APIC Description Table).
 *
 * This structure defines the format of a local APIC entry in the ACPI MADT.
 *
 * @remark This structure shall be packed.
 */
struct acpi_madt_local_apic {
	struct acpi_subtable_header    header;		/**< Common ACPI sub-table header */
	uint8_t                        processor_id;	/**< Processor ID */
	uint8_t                        id;		/**< Processor's local APIC ID */
	uint32_t                       lapic_flags;	/**< Flags associated with the local APIC */
} __packed;

/**
 * @brief Represents an ACPI MADT x2APIC structure.
 *
 * This structure is used to describe the x2APIC entries in the ACPI MADT table.
 *
 * @remark This structure shall be packed.
 */
struct acpi_madt_x2apic {
	struct acpi_subtable_header    header;		/**< Common ACPI sub-table header */
	uint16_t                       reserved;	/**< Reserved field, must be zero */
	uint32_t                       processor_id;	/**< Processor ID */
	uint32_t                       lapic_flags;	/**< Flags associated with the local APIC */
	uint32_t                       uid;		/**< Unique identifier for the x2APIC */
} __packed;

/**
 * @brief Represents the ACPI MADT Local APIC NMI structure.
 *
 * This structure defines the Non-Maskable Interrupt (NMI) configuration for a local APIC in the ACPI MADT table.
 *
 * @remark This structure shall be packed.
 */
struct acpi_madt_local_apic_nmi {
	struct acpi_subtable_header    header;	/**< Common ACPI sub-table header */
	uint8_t                   processor_id;	/**< Processor ID */
	uint16_t                  flags;	/**< Flags associated with the NMI */
	uint8_t                   lint;		/**< LINT number */
} __packed;

/**
 * @brief Structure representing an ACPI MADT IOAPIC entry.
 *
 * This structure defines the layout of an IOAPIC entry in the ACPI Multiple APIC Description Table (MADT).
 *
 * @remark This structure shall be packed.
 */
struct acpi_madt_ioapic {
	struct acpi_subtable_header    header;	/**< CommonACPI sub-table header */
	uint8_t   id;				/**< IOAPIC id */
	uint8_t   rsvd;				/**< Reserved byte */
	uint32_t  addr;				/**< IOAPIC address */
	uint32_t  gsi_base;			/**< Global System Interrupt base */
} __packed;

/**
 * @brief Structure representing the ACPI DMAR table.
 *
 * This structure defines the ACPI DMAR (DMA Remapping) table, which provides information about the DMA remapping
 * hardware in the system.
 *
 * @remark This structure shall be packed.
 */
struct acpi_table_dmar {
	struct acpi_table_header  header;	/**< Common ACPI table header */
	uint8_t                   width;	/**< Host address width */
	uint8_t                   flags;	/**< Flags indicating various properties */
	uint8_t                   reserved[10];	/**< Reserved bytes for future use */
} __packed;

/**
 * @brief Represents the header for ACPI DMAR (DMA Remapping) table entries.
 *
 * This structure defines the common header for entries in the ACPI DMAR table.
 *
 * @remark This structure shall be packed.
 */
struct acpi_dmar_header {
	uint16_t                  type;		/**< Type of DMAR table entry */
	uint16_t                  length;	/**< Length of DMAR table entry */
} __packed;

/**
 * @brief Represents an ACPI DMAR hardware unit structure.
 *
 * This structure defines the layout of an ACPI DMAR hardware unit.
 *
 * @remark This structure shall be packed.
 */
struct acpi_dmar_hardware_unit {
	struct acpi_dmar_header   header;	/**< ACPI DMAR header */
	uint8_t                   flags;	/**< Flags associated with the hardware unit */
	uint8_t                   reserved;	/**< Reserved byte for alignment */
	uint16_t                  segment;	/**< Segment number of the hardware unit */
	uint64_t                  address;	/**< Register base address of the hardware unit */
} __packed;

/**
 * @brief Represents a PCI device path in ACPI DMAR table.
 *
 * This structure is used to describe the path to a PCI device in the ACPI DMAR (DMA Remapping) table.
 *
 * @remark This structure shall be packed.
 */
struct acpi_dmar_pci_path {
	uint8_t                   device;	/**< The device number on the PCI bus. */
	uint8_t                   function;	/**< The function number of the device. */
} __packed;

/**
 * @brief Represents a device scope entry in the DMAR table.
 *
 * This structure is used to describe the scope of devices that are affected by a DMA remapping hardware unit in the
 * ACPI DMAR table.
 *
 * @remark This structure shall be packed.
 */
struct acpi_dmar_device_scope {
	uint8_t                   entry_type;		/**< Type of the device scope entry */
	uint8_t                   length;		/**< Length of the device scope entry */
	uint16_t                  reserved;		/**< Reserved field, must be zero */
	uint8_t                   enumeration_id;	/**< Enumeration ID of the device */
	uint8_t                   bus;			/**< Bus number of the device */
} __packed;

/**
 * @brief Represents the ACPI TPM2 table structure.
 *
 * This structure defines the layout of the TPM2 (Trusted Platform Module 2.0) ACPI table.
 * It includes the common ACPI table header and specific fields related to TPM2.
 *
 * @remark This structure shall be packed.
 */
struct acpi_table_tpm2 {
	struct acpi_table_header header;	/**< Common ACPI table header */
	uint16_t platform_class;		/**< Platform class */
	uint16_t reserved;			/**< Reserved field */
	uint64_t control_address;		/**< Control address */
	uint32_t start_method;			/**< Start method */
#ifdef CONFIG_SECURITY_VM_FIXUP
	uint8_t start_method_spec_para[12];	/**< Start method specific parameters */
	uint32_t laml;				/**< Length of the area */
	uint64_t lasa;				/**< Address of the area */
#endif
} __packed;

/**
 * @brief Represents the ACPI HPET (High Precision Event Timer) table.
 *
 * This structure defines the layout of the ACPI HPET table, which provides information about the High Precision Event
 * Timer in the system.
 *
 * @remark This structure shall be packed.
 */
struct acpi_table_hpet {
	struct acpi_table_header header;	/**< Common ACPI table header */
	uint32_t id;				/**< HPET ID */
	struct packed_gas address;		/**< HPET address in GAS format */
	uint8_t sequence;			/**< Sequence number */
	uint16_t minimum_tick;			/**< Minimum clock tick */
	uint8_t flags;				/**< Flags */
} __packed;

void init_acpi(void);
void *get_acpi_tbl(const char *signature);

struct ioapic_info;
uint16_t parse_madt(uint32_t lapic_id_array[MAX_PCPU_NUM]);
uint8_t parse_madt_ioapic(struct ioapic_info *ioapic_id_array);
void *parse_hpet(void);

#ifdef CONFIG_ACPI_PARSE_ENABLED
int32_t acpi_fixup(void);
#endif

/**
 * @}
 */

#endif /* !ACPI_H */
