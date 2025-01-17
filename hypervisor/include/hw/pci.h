/*-
* Copyright (c) 1997, Stefan Esser <se@freebsd.org>
* Copyright (c) 2011 NetApp, Inc.
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
* THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
* OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
* LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
* OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
* SUCH DAMAGE.
*
* $FreeBSD$
*/

#ifndef PCI_H_
#define PCI_H_

#include <list.h>

/**
 * @addtogroup hwmgmt_pci
 *
 * @{
 */

/**
 * @file
 * @brief This file declares all external APIs that shall be provided by the hwmgmt.pci module.
 *
 * This file declares all external macros, data structures and functions that should be provided
 * by the hwmgmt.pci module.
 *
 * For the macros definition of PCI configuration space registers, there are some rules as following:
 *   - PCIM_xxx: mask to locate subfield in register
 *   - PCIR_xxx: config register offset
 *   - PCIC_xxx: device class
 *   - PCIS_xxx: device subclass
 *   - PCIP_xxx: device programming interface
 *   - PCIV_xxx: PCI vendor ID (only required to fixup ancient devices)
 *   - PCID_xxx: device ID
 *   - PCIY_xxx: capability identification number
 *   - PCIZ_xxx: extended capability identification number
 */

#define PCI_CFG_HEADER_LENGTH 0x40U  /**< PCI configuration space header length. */

/* some PCI bus constants */
#define PCI_BUSMAX            0xFFU  /**< Max bus number. */
#define PCI_SLOTMAX           0x1FU  /**< Max device number. */
#define PCI_FUNCMAX           0x7U   /**< Max function number. */
#define PCI_BAR_COUNT         0x6U   /**< PCI devices BAR count. */

#define PCI_CONFIG_SPACE_SIZE 0x100U       /**< Size of PCI configuration space. */
#define PCIE_CONFIG_SPACE_SIZE 0x1000U     /**< Size of PCIe configuration space. */

/* I/O ports */
#define PCI_CONFIG_ADDR       0xCF8U  /**< The I/O port of CONFIG_ADDRESS register. */
#define PCI_CONFIG_DATA       0xCFCU  /**< The I/O port of CONFIG_DATA register. */

/**
 * @brief Mask for enable flag of CONFIG_ADDRESS
 *
 * Bit 31 is an enable flag for determining when accesses to CONFIG_DATA are to be translated to configuration
 * transactions on the PCI bus.
 */
#define PCI_CFG_ENABLE        0x80000000U

/* PCI config header registers for all devices */
#define PCIR_VENDOR           0x00U  /**< Vendor ID register offset. */
#define PCIR_DEVICE           0x02U  /**< Device ID register offset. */
#define PCIR_COMMAND          0x04U  /**< Command register offset. */
#define	PCIM_CMD_PORTEN       0x01U  /**< Mask for Command register I/O Space Enable bit. */
#define	PCIM_CMD_MEMEN        0x02U  /**< Mask for Command register Memory Space Enable bit. */
#define	PCIM_CMD_BUSEN        0x04U  /**< Mask for Command register Bus Master Enable bit. */
#define PCIM_CMD_INTxDIS      0x400U /**< Mask for Command register Interrupt Disable bit. */
#define PCIR_STATUS           0x06U  /**< Status register offset. */
#define PCIM_STATUS_CAPPRESENT    0x0010U /**< Mask for Status register Capabilities List bit. */
#define PCIR_REVID            0x08U  /**< Revision ID register offset. */
#define PCIR_CLASS_CODE	      0x09U  /**< Class Code register offset. */
#define PCIR_SUBCLASS         0x0AU  /**< Sub-class Code register offset. */
#define PCIR_CLASS            0x0BU  /**< Base Class Code register offset. */
#define PCIR_HDRTYPE          0x0EU  /**< Header Type register offset. */
#define PCIM_HDRTYPE          0x7FU  /**< Mask for Header Type register Header Layout bits. */
#define PCIM_HDRTYPE_NORMAL   0x00U  /**< Mask for Type 0 Configuration Space Header. */
#define PCIM_HDRTYPE_BRIDGE   0x01U  /**< Mask for Type 1 Configuration Space Header. */
/**
 * @brief Mask for Type 2 Configuration Space Header which is reserved now
 */
#define	PCIM_HDRTYPE_CARDBUS  0x02U
#define PCIM_MFDEV            0x80U  /**< Mask for Header Type register Multi-Function Device bit. */
#define PCIR_BARS             0x10U  /**< BAR registers offset.*/
#define PCIM_BAR_SPACE        0x01U  /**< Mask used to check PCI BAR type: MMIO or I/O. */
#define PCIM_BAR_IO_SPACE     0x01U  /**< Mask used to indicate the BAR type: I/O mapped. */
#define PCIM_BAR_MEM_TYPE     0x06U  /**< Mask used to check the MMIO BAR is 32 or 64bits. */
#define PCIM_BAR_MEM_32       0x00U  /**< Mask used to indicate the MMIO BAR is 32bits. */
/**
 * @brief Mask used to indicate to support memory space below 1 MB which is reserved now
 */
#define PCIM_BAR_MEM_1MB      0x02U
#define PCIM_BAR_MEM_64       0x04U  /**< Mask used to indicate the MMIO BAR is 64bits. */
#define PCIM_BAR_MEM_BASE     0xFFFFFFF0U  /**< Mask used to calculate base address of a MMIO BAR. */
#define PCIV_SUB_VENDOR_ID    0x2CU  /**< Subsystem Vendor ID register offset. */
#define PCIV_SUB_SYSTEM_ID    0x2EU  /**< Subsystem ID register offset. */
#define PCIR_BIOS	      0x30U  /**< Expansion ROM Base Address register offset. */
#define PCIR_CAP_PTR          0x34U  /**< Capabilities Pointer register offset. */
#define PCIR_CAP_PTR_CARDBUS  0x28U  /**< Cardbus CIS Pointer register offset. */
#define PCI_BASE_ADDRESS_MEM_MASK (~0x0fUL)  /**< Mask used to calculate base address of a MMIO BAR. */
#define PCI_BASE_ADDRESS_IO_MASK  (~0x03UL)  /**< Mask used to calculate base address of a I/O Space BAR. */
#define PCIR_INTERRUPT_LINE   0x3cU  /**< Interrupt Line register offset. */
#define PCIR_INTERRUPT_PIN    0x3dU  /**< Interrupt Pin register offset. */

#define PCIC_SIMPLECOMM       0x07U  /**< Class codes of Simple communication controllers */
/* Config registers for header type 1 (PCI-to-PCI bridge) devices */
#define PCIR_PRIBUS_1         0x18U  /**< Primary Bus Number register offset. */
#define PCIR_SECBUS_1         0x19U  /**< Secondary Bus Number register offset. */
#define PCIR_SUBBUS_1         0x1AU  /**< Subordinate Bus Number register offset. */
#define PCIR_IO_BASE          0x1CU  /**< I/O Base register offset. */
#define PCIR_IO_LIMIT         0x1DU  /**< I/O Limit register offset. */
#define PCIR_MEM_BASE         0x20U  /**< Memory Base register offset. */
#define PCIR_IO_BASE_UPPER_16 0x30U  /**< I/O Base Upper 16 Bits register offset. */
#define PCIR_SECSTATUS_LINE_MASK   0xFFFF0000U  /**< Mask to get Secondary Status register value. */

/* Capability Register Offsets */
#define PCICAP_ID             0x0U  /**< PCI Express Capability ID register offset. */
#define PCICAP_NEXTPTR        0x1U  /**< PCI Express Next Capability Pointer register offset. */
#define PCICAP_EXP_CAP        0x2U  /**< PCI Express Capabilities register offset. */

/* Capability Identification Numbers */
#define PCIY_MSI              0x05U  /**< MSI Capability ID. */
#define PCIY_MSIX             0x11U  /**< MSI-X Capability ID. */

/* PCIe Extended Capability*/
/**
 * @brief Offset of extended configuration region where PCI Express Extended Capabilities start at
 */
#define PCI_ECAP_BASE_PTR	0x100U
/**
 * @brief Mask used to get PCI Express Extended Capability ID
 *
 * @param[in] hdr Value of PCI Express Extended Capability Header.
 *
 * @return Unsigned integer value to indicate PCI Express Extended Capability ID
 */
#define PCI_ECAP_ID(hdr)	((uint32_t)((hdr) & 0xFFFFU))
/**
 * @brief Mask used to get Next Capability Offset
 *
 * @param[in] hdr Value of PCI Express Extended Capability Header.
 *
 * @return Unsigned integer value to indicate Next Capability Offset
 */
#define PCI_ECAP_NEXT(hdr)	((uint32_t)(((hdr) >> 20U) & 0xFFCU))
#define PCIZ_SRIOV		0x10U  /**< SR-IOV Extended Capability ID. */
#define PCIZ_PTM 		0x1fU  /**< Precision Time Measurement Capability ID */

/* SRIOV Definitions */
#define PCI_SRIOV_CAP_LEN	0x40U  /**< Length of SR-IOV Extended Capability. */
#define PCIR_SRIOV_CONTROL	0x8U   /**< SR-IOV Control register offset. */
#define PCIR_SRIOV_TOTAL_VFS	0xEU   /**< TotalVFs register offset. */
#define PCIR_SRIOV_NUMVFS	0x10U  /**< NumVFs register offset. */
#define PCIR_SRIOV_FST_VF_OFF	0x14U  /**< FirstVF Offset register offset. */
#define PCIR_SRIOV_VF_STRIDE	0x16U  /**< VF Stride register offset. */
#define PCIR_SRIOV_VF_DEV_ID	0x1AU  /**< VF Device ID register offset. */
#define PCIR_SRIOV_VF_BAR_OFF	0x24U  /**< VF BAR0 register offset. */
#define PCIM_SRIOV_VF_ENABLE	0x1U   /**< Mask used to indicate VF Enable. */

/* PTM Definitions */
#define PCI_PTM_CAP_LEN			0x04U  /**< Length of PTM Extended Capability. */
#define PCIR_PTM_CAP			0x04U  /**< PTM Capability register offset. */
#define PCIM_PTM_CAP_ROOT_CAPABLE	0x4U   /**< Mask used to indicate PTM Root Capable bit. */
#define PCIM_PTM_GRANULARITY_MASK	0xFF00U /**< Mask used to get Local Clock Granularity. */
#define PCIR_PTM_CTRL			0x08U  /**< PTM Control register offset. */
#define PCIM_PTM_CTRL_ENABLED		0x1U   /**< Mask used to indicate PTM Enable bit. */
#define PCIM_PTM_CTRL_ROOT_SELECTED	0x2U   /**< Mask used to indicate Root Select bit. */

/* PCI Message Signalled Interrupts (MSI) */
#define PCIR_MSI_CTRL         0x02U  /**< MSI Message Control register offset. */
#define PCIM_MSICTRL_64BIT    0x80U  /**< Mask used to indicate 64-bit Address Capable bit. */
#define PCIM_MSICTRL_MSI_ENABLE  0x01U /**< Mask used to indicate MSI Enable bit. */
#define PCIR_MSI_ADDR         0x4U  /**< MSI Message Address register offset. */
#define PCIR_MSI_ADDR_HIGH    0x8U  /**< MSI Message Upper Address register offset. */
#define PCIR_MSI_DATA         0x8U  /**< MSI Message Data register offset. */
#define PCIR_MSI_DATA_64BIT   0xCU  /**< MSI Extended Message Data register offset. */
#define PCIR_MSI_MASK         0x10U /**< MSI Mask Bits register offset. */
#define PCIM_MSICTRL_MMC_MASK 0x000EU  /**< Mask used to indicate Multiple Message Capable bits. */
#define PCIM_MSICTRL_MME_MASK 0x0070U  /**< Mask used to indicate Multiple Message Enable bits. */

/* PCI device class */
#define PCIC_BRIDGE           0x06U  /**< The class value for PCI bridge and host bridge. */
#define PCIS_BRIDGE_HOST      0x00U  /**< The subclass value for PCI host bridge. */

/* PCI device subclass */
#define PCIS_BRIDGE_PCI       0x04U  /**< The subclass value for PCI bridge. */

/* MSI-X definitions */
#define PCIR_MSIX_CTRL        0x2U  /**< MSI-X Message Control register offset. */
#define PCIR_MSIX_TABLE       0x4U  /**< MSI-X Table Offset/Table BIR register offset. */
#define PCIR_MSIX_PBA         0x8U  /**< MSI-X PBA Offset/PBA BIR register offset. */

#define PCIM_MSIXCTRL_MSIX_ENABLE    0x8000U  /**< Mask used to indicate MSI-X Enable bit. */
#define PCIM_MSIXCTRL_FUNCTION_MASK  0x4000U  /**< Mask used to indicate Function Mask bit. */
#define PCIM_MSIXCTRL_TABLE_SIZE     0x07FFU  /**< Mask used to indicate Table Size bits. */
#define PCIM_MSIX_BIR_MASK    0x7U  /**< Mask used to indicate PBA BIR bits. */
#define PCIM_MSIX_VCTRL_MASK  0x1U  /**< Mask used to indicate Vector Control register Mask Bit. */

#define MSIX_CAPLEN           12U  /**< Length of MSI-X Capability. */
#define MSIX_TABLE_ENTRY_SIZE 16U  /**< Length of MSI-X Table Entry. */

/* PCI Power Management Capability */
#define PCIY_PMC              0x01U  /**< Power Management Capability ID. */
/* Power Management Control/Status Register */
#define PCIR_PMCSR            0x04U  /**< Power Management Control/Status register offset. */
#define PCIM_PMCSR_NO_SOFT_RST (0x1U << 3U)  /**< Mask used to indicate No_Soft_Reset bit. */

/* PCI Express Capability */
#define PCIY_PCIE             0x10U  /**< PCI Express Capability ID. */
#define PCIR_PCIE_DEVCAP      0x04U  /**< PCIe Device Capabilities register offset. */
#define PCIR_PCIE_DEVCTRL     0x08U  /**< PCIe Device Control register offset. */
#define PCIM_PCIE_DEV_CTRL_MAX_PAYLOAD    0x00E0U  /**< Mask used to indicate Max_Payload_Size bits. */
#define PCIM_PCIE_FLRCAP      (0x1U << 28U)  /**< Mask used to indicate Function Level Reset Capability bit. */
/**
 * @brief Mask used to indicate Bridge Configuration Retry Enable/Initiate Function Level Reset bit
 */
#define PCIM_PCIE_FLR         (0x1U << 15U)

/* PCI Express Device Type definitions */
#define PCIER_FLAGS                    0x2U  /**< PCI Express Capabilities register offset. */
#define PCIEM_FLAGS_TYPE               0x00F0U  /**< Mask used to indicate Device/Port Type bits. */
#define PCIEM_TYPE_ENDPOINT            0x0000U  /**< Value of PCI Express Endpoint. */
#define PCIEM_TYPE_ROOTPORT            0x0004U  /**< Value of Root Port of PCI Express Root Complex. */
#define PCIEM_TYPE_ROOT_INT_EP         0x0009U  /**< Value of RCiEP. */

#define PCIR_PCIE_DEVCAP2     0x24U  /**< PCIe Device Capabilities 2 register offset. */
#define PCIM_PCIE_DEVCAP2_ARI (0x1U << 5U)  /**< Mask used to indicate ARI Forwarding Supported bit. */
#define PCIR_PCIE_DEVCTL2     0x28U  /**< PCIe Device Control 2 register offset. */
#define PCIM_PCIE_DEVCTL2_ARI (0x1U << 5U)  /**< Mask used to indicate ARI Forwarding Enable bit. */

/* Conventional PCI Advanced Features Capability */
#define PCIY_AF               0x13U  /**< Advanced Features Capability ID. */
#define PCIM_AF_FLR_CAP       (0x1U << 25U)  /**< Mask used to indicate FLR_CAP bit. */
#define PCIR_AF_CTRL          0x4U  /**< Conventional PCI Advanced Features Control register offset. */
#define PCIM_AF_FLR           0x1U  /**< Mask used to indicate Function Level Reset bit. */

#define PCI_STD_NUM_BARS        6U  /**< Value of the number of PCI BARs. */

/**
 * @brief A union data structure to store PCI device BDF
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
union pci_bdf {
	uint16_t value;  /**< Value of the BDF */
	/**
	 * @brief A data structure to present B/D/F bits separately
	 */
	struct {
		uint8_t f : 3;  /**< BITs 0-2 */
		uint8_t d : 5;  /**< BITs 3-7 */
		uint8_t b;      /**< BITs 8-15 */
	} bits;
	/**
	 * @brief A data structure to present bus value and device-function value
	 */
	struct {
		uint8_t devfun; /**< BITs 0-7 */
		uint8_t bus;    /**< BITs 8-15 */
	} fields;
};

/**
 * @brief A union data structure to store PCI BAR register information
 *
 * For details, refer PCI Express® Base Specification Revision 5.0 Version 1.0 7.5.1.2.1
 * Base Address Registers (Offset 10h - 24h).
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
union pci_bar_type {
	uint32_t bits;  /**< Value of the BAR */
	/**
	 * @brief A data structure to present I/O BAR information
	 */
	struct {
		uint32_t indicator :1;    /**< BITs[0], mapped to I/O space if read as 1 */
		uint32_t reserved :1;     /**< BITs[1], reserved and must be "0" per spec. */
		uint32_t reserved2 : 30;  /**< BITs[31:2], Base Address value per spec. */
	} io_space;
	/**
	 * @brief A data structure to present memory BAR information
	 */
	struct {
		uint32_t indicator :1;    /**< BITs[0], mapped to memory space if read as 0 */
		uint32_t mem_type :2;     /**< BITs[1:2], 32-bit address if read as 00b, 64-bit address as 01b */
		uint32_t prefetchable :1; /**< BITs[3], set to 1b if the data is prefetchable and set to 0b
					   *   otherwise */
		uint32_t reserved2 : 28;  /**< BITs[31:4], Base Address value per spec. */
	} mem_space;
};

/**
 * @brief A data structure to present PCI MMCFG region information
 *
 * For details, please refer Memory Mapped Enhanced Configuration Space Base Address Allocation
 * Structure (PCI FIRMWARE SPECIFICATION, REV. 3.2, chapter 4.1.2. MCFG Table Description).
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark This structure shall be packed.
 */
struct pci_mmcfg_region {
	uint64_t address;	/**< Base address, processor-relative */
	uint16_t pci_segment;	/**< PCI segment group number */
	uint8_t start_bus;	/**< Starting PCI Bus number */
	uint8_t end_bus;	/**< Final PCI Bus number */
} __packed;

/**
 * @brief A data structure to present basic MSI-X capability information
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
struct pci_msix_cap {
	uint32_t  capoff;    /**< MSI-X capability register offset */
	uint32_t  caplen;    /**< Length of MSI-X capability register */
	uint8_t   table_bar;    /**< MSI-X Table BIR */
	uint32_t  table_offset;    /**< MSI-X Table Offset */
	uint32_t  table_count;    /**< Count of MSI-X Table Entry */
	uint8_t   cap[MSIX_CAPLEN];    /**< Buffer to save MSI-X capability register */
};

/**
 * @brief A data structure to present basic SRIOV capability information
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
struct pci_sriov_cap {
	uint32_t  capoff;  /**< SRIOV Capability Structure offset */
	uint32_t  caplen;  /**< Length of SRIOV Capability Structure */
	uint32_t  pre_pos; /**< Offset of previous PCIe Extended Capability */
	bool hide_sriov;   /**< Flag to indicate to whether to hide SRIOV */
};

/**
 * @brief A data structure to record PCI BAR data and size
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
struct pci_bar_resource {
	uint32_t phy_bar;	/**< The original raw data read from physical BAR */
	uint32_t size_mask;	/**< Read value of physical BAR after write 0xffffffff */
};

/**
 * @brief A data structure to present PCI physical device information
 *
 * A PCI device has many information need be recorded. This data structure saves the
 * device basic information, e.g. vendor/device id, BDF, BARs information, and some
 * other important information, e.g. index of the DRHD that the device belongs to,
 * and so on.
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
struct pci_pdev {
	uint8_t hdr_type;    /**< Value of Header Type register */
	uint8_t base_class;  /**< Value of Base Class Code register */
	uint8_t sub_class;   /**< Value of Sub-Class Code register */

	/* IOMMU responsible for DMA and Interrupt Remapping for this device */
	uint32_t drhd_index;  /**< DRHD index */
	/* Used for vMSI-x on MSI emulation */
	uint16_t irte_start;  /**< Index of the first Interrupt Remapping Table Entry of the device in the DRHD */
	uint16_t irte_count;  /**< Count of the Interrupt Remapping Table Entries of the device in the DRHD */

	/* The bar info of the physical PCI device. */
	uint32_t nr_bars; /**< Number of BARs: 6 for normal device, 2 for bridge, 1 for cardbus */
	struct pci_bar_resource bars[PCI_STD_NUM_BARS];	/**< Array to record common bar resource */

	/* The bus/device/function triple of the physical PCI device. */
	union pci_bdf bdf;  /**< Device BDF (bus/device/function) */

	uint16_t vendor_id; /**< The vendor id of the physical PCI device */
	uint16_t device_id; /**< The device id of the physical PCI device */

	uint32_t msi_capoff;  /**< MSI Capability Structure offset */
	uint32_t pcie_capoff; /**< PCI Express Capability Structure offset */

	struct pci_msix_cap msix;  /**< Instance of data structure to present basic MSI-X capability information */
	struct pci_sriov_cap sriov; /**< Instance of data structure to present basic SRIOV capability information */

	bool has_pm_reset;  /**< Flag to indicate to support soft reset */
	bool has_flr;  /**< Flag to indicate to support Function Level Reset */
	bool has_af_flr;  /**< Flag to indicate to support FLR of Conventional PCI Advanced Feature */
	struct hlist_node link;  /**< Link used to add into hash list of PCI devices */
};

/**
 * @brief A data structure for PIO/MMIO operations
 *
 * The PCI configuration space read and write callback functions are declared.
 *
 * @consistency N/A
 * @alignment N/A
 *
 * @remark N/A
 */
struct pci_cfg_ops {
	/**
	 * @brief Callback function to read PCI configuration space
	 */
	uint32_t (*pci_read_cfg)(union pci_bdf bdf, uint32_t offset, uint32_t bytes);
	/**
	 * @brief Callback function to write PCI configuration space
	 */
	void (*pci_write_cfg)(union pci_bdf bdf, uint32_t offset, uint32_t bytes, uint32_t val);
};

/**
 * @brief Check whether the PCI physical device is a host bridge
 *
 * @param[in] pdev Pointer to the data structure of PCI physical device.
 *
 * @return A boolean value to indicate whether the PCI physical device is a host bridge
 *
 * @retval TRUE  The PCI physical device is a host bridge
 * @retval FALSE The PCI physical device is not a host bridge
 *
 * @pre pdev != NULL
 */
static inline bool is_host_bridge(const struct pci_pdev *pdev)
{
	return (pdev->base_class == PCIC_BRIDGE) && (pdev->sub_class == PCIS_BRIDGE_HOST);
}

/**
 * @brief Check whether the PCI physical device is a bridge
 *
 * @param[in] pdev Pointer to the data structure of PCI physical device.
 *
 * @return A boolean value to indicate whether the PCI physical device is a bridge
 *
 * @retval TRUE  The PCI physical device is a bridge
 * @retval FALSE The PCI physical device is not a bridge
 *
 * @pre pdev != NULL
 */
static inline bool is_bridge(const struct pci_pdev *pdev)
{
	return ((pdev->hdr_type & PCIM_HDRTYPE) == PCIM_HDRTYPE_BRIDGE);
}

/**
 * @brief Get offset of a BAR
 *
 * This function returns a BAR's offset in the PCI configuration space according to
 * the input index of the BAR.
 *
 * @param[in] idx BAR index.
 *
 * @return Offset of the BAR
 */
static inline uint32_t pci_bar_offset(uint32_t idx)
{
	return PCIR_BARS + (idx << 2U);
}

/**
 * @brief Get index of a BAR
 *
 * This function calculates a BAR's index according to the offset of the BAR.
 *
 * @param[in] offset BAR offset in the PCI configuration space.
 *
 * @return Index of the BAR
 */
static inline uint32_t pci_bar_index(uint32_t offset)
{
	return (offset - PCIR_BARS) >> 2U;
}

/**
 * @brief Check whether the input offset is within the range of BARs
 *
 * @param[in] nr_bars Number of BARs.
 * @param[in] offset  BAR offset in the PCI configuration space.
 *
 * @return A boolean value to indicate whether the input offset is within the range of BARs
 *
 * @retval TRUE  The input offset is within the range of BARs
 * @retval FALSE The input offset is not within the range of BARs
 */
static inline bool is_bar_offset(uint32_t nr_bars, uint32_t offset)
{
	bool ret;

	if ((offset >= pci_bar_offset(0U))
		&& (offset < pci_bar_offset(nr_bars))) {
		ret = true;
	} else {
	    ret = false;
	}

	return ret;
}

/**
 * @brief Check whether the input two BDFs are equal
 *
 * @param[in] a One BDF to compare.
 * @param[in] b Another BDF to compare.
 *
 * @return A boolean value to indicate whether the input two BDFs are equal
 *
 * @retval TRUE  The input two BDFs are equal
 * @retval FALSE The input two BDFs are not equal
 */
static inline bool bdf_is_equal(union pci_bdf a, union pci_bdf b)
{
	return (a.value == b.value);
}

/**
 * @brief Get the size of MMCFG region
 *
 * This function calculates the size of a MMCFG region. Refer Chapter 7 Software Initialization
 * and Configuration, PCI Express® Base Specification Revision 5.0.
 *
 * @param[in] pci_mmcfg Pointer to a data structure for MMCFG region.
 *
 * @return Size of the MMCFG region
 */
static inline uint64_t get_pci_mmcfg_size(struct pci_mmcfg_region *pci_mmcfg)
{
	return 0x100000UL * (pci_mmcfg->end_bus - pci_mmcfg->start_bus + 1U);
}

#ifdef CONFIG_ACPI_PARSE_ENABLED
void set_mmcfg_region(struct pci_mmcfg_region *region);
#endif
struct pci_mmcfg_region *get_mmcfg_region(void);

struct pci_pdev *pci_init_pdev(union pci_bdf pbdf, uint32_t drhd_index);
uint32_t pci_pdev_read_cfg(union pci_bdf bdf, uint32_t offset, uint32_t bytes);
void pci_pdev_write_cfg(union pci_bdf bdf, uint32_t offset, uint32_t bytes, uint32_t val);
void enable_disable_pci_intx(union pci_bdf bdf, bool enable);

bool is_hv_owned_pdev(union pci_bdf pbdf);
uint32_t get_hv_owned_pdev_num(void);
const struct pci_pdev **get_hv_owned_pdevs(void);
void init_pci_pdev_list(void);

uint32_t pci_lookup_drhd_for_pbdf(uint16_t pbdf);

/**
 * @brief Check whether the input Vendor ID is valid
 *
 * @param[in] vendor_id Vendor ID of a PCI device.
 *
 * @return A boolean value to indicate whether the input Vendor ID is valid
 *
 * @retval TRUE  The input Vendor ID is valid
 * @retval FALSE The input Vendor ID is not valid
 */
static inline bool is_pci_vendor_valid(uint32_t vendor_id)
{
	return !((vendor_id == 0xFFFFFFFFU) || (vendor_id == 0U) ||
		 (vendor_id == 0xFFFF0000U) || (vendor_id == 0xFFFFU));
}

/**
 * @brief Check whether the PCI device is a multifunction device according to its header type
 *
 * @param[in] header_type Value of header type register of a PCI device.
 *
 * @return A boolean value to indicate whether the PCI device is a multifunction device
 *
 * @retval TRUE  The PCI device is a multifunction device
 * @retval FALSE The input Vendor ID is not valid
 */
static inline bool is_pci_cfg_multifunction(uint8_t header_type)
{
	return ((header_type != 0xffU) && ((header_type & PCIM_MFDEV) == PCIM_MFDEV));
}

/**
 * @brief Check whether the offset to access is aligned with size
 *
 * @param[in] offset Register offset to access.
 * @param[in] bytes  Size to read/write the register.
 *
 * @return A boolean value to indicate whether the offset to access is aligned with size
 *
 * @retval TRUE  The offset to access is aligned with size
 * @retval FALSE The offset to access is not aligned with size
 */
static inline bool pci_is_valid_access_offset(uint32_t offset, uint32_t bytes)
{
	return ((offset & (bytes - 1U)) == 0U);
}

/**
 * @brief Check whether the size to access is valid
 *
 * @param[in] bytes Size to read/write the register.
 *
 * @return A boolean value to indicate whether the size to access is valid
 *
 * @retval TRUE  The size to access is valid
 * @retval FALSE The size to access is not valid
 */
static inline bool pci_is_valid_access_byte(uint32_t bytes)
{
	return ((bytes == 1U) || (bytes == 2U) || (bytes == 4U));
}

/**
 * @brief Check whether the offset and size are both valid
 *
 * @param[in] offset Register offset to access.
 * @param[in] bytes  Size to read/write the register.
 *
 * @return A boolean value to indicate whether the offset and size are both valid
 *
 * @retval TRUE  The offset and size are both valid
 * @retval FALSE The offset and size are not both valid
 */
static inline bool pci_is_valid_access(uint32_t offset, uint32_t bytes)
{
	return (pci_is_valid_access_byte(bytes) && pci_is_valid_access_offset(offset, bytes));
}

bool is_plat_hidden_pdev(union pci_bdf bdf);
bool pdev_need_bar_restore(const struct pci_pdev *pdev);
void pdev_restore_bar(const struct pci_pdev *pdev);
void pci_switch_to_mmio_cfg_ops(void);
void reserve_vmsix_on_msi_irtes(struct pci_pdev *pdev);

/**
 * @}
 */

#endif /* PCI_H_ */
