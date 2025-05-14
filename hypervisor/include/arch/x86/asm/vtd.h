/*
 * Copyright (C) 2018-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef VTD_H
#define VTD_H
#include <types.h>
#include <pci.h>
#include <platform_acpi_info.h>

/**
 * @addtogroup hwmgmt_vtd
 *
 * @{
 */

/**
 * @file vtd.h
 *
 * @brief VT-d module header file
 *
 * This file defines constants, structures and inline functions per Intel Virtualization Technology for Directed I/O
 * Architecture Specification. Also declares public APIs exposed in VT-d module.
 */

#define DMAR_OPERATION_TIMEOUT	0x10000U	/**< DMAR operation timeout value in millisecond unit */

#define INVALID_DRHD_INDEX 0xFFFFFFFFU /**< Value representing invalid DRHD index */
#define INVALID_IRTE_ID 0xFFFFU /**< Value representing invalid IRTE ID */

#define DMAR_VER_REG    0x0U     /**< Offset of Version Register */
#define DMAR_CAP_REG    0x8U     /**< Offset of Capability Register */
#define DMAR_ECAP_REG   0x10U    /**< Offset of Extended Capability Register */
#define DMAR_GCMD_REG   0x18U    /**< Offset of Global Command Register */
#define DMAR_GSTS_REG   0x1cU    /**< Offset of Global Status Register */
#define DMAR_RTADDR_REG 0x20U    /**< Offset of Root Table Address Register */
#define DMAR_CCMD_REG   0x28U    /**< Offset of Context Command Register */
#define DMAR_FSTS_REG   0x34U    /**< Offset of Fault Status Register */
#define DMAR_FECTL_REG  0x38U    /**< Offset of Fault Event Control Register */
#define DMAR_FEDATA_REG 0x3cU    /**< Offset of Fault Event Data Register */
#define DMAR_FEADDR_REG 0x40U    /**< Offset of Fault Event Address Register */
#define DMAR_FEUADDR_REG 0x44U   /**< Offset of Fault Event Upper Address Register */
#define DMAR_AFLOG_REG  0x58U    /**< Offset of Advanced Fault Log Register */
#define DMAR_PMEN_REG   0x64U    /**< Offset of Protected Memory Enable Register */
#define DMAR_PLMBASE_REG 0x68U   /**< Offset of Protect Low-Memory Base Register */
#define DMAR_PLMLIMIT_REG 0x6cU  /**< Offset of Protect Low-Memory Limit Register */
#define DMAR_PHMBASE_REG 0x70U   /**< Offset of Protected High-Memory Base Register */
#define DMAR_PHMLIMIT_REG 0x78U  /**< Offset of Protected High-Memory Limit Register */
#define DMAR_IQH_REG    0x80U    /**< Offset of Invalidation Queue Head Register */
#define DMAR_IQT_REG    0x88U    /**< Offset of Invalidation Queue Tail Register */
#define DMAR_IQ_SHIFT   4        /**< Invalidation queue head/tail shift number */
#define DMAR_IQA_REG    0x90U    /**< Offset of Invalidation Queue Address Register */
#define DMAR_ICS_REG    0x9cU    /**< Offset of Invalidation Completion Status Register */
#define DMAR_IRTA_REG   0xb8U    /**< Offset of Interrupt Remapping Table Address Register */

#define DMAR_FSTS_PFO	(1U << 0U)	/**< Primary Fault Overflow bit in Fault Status Register */

/**
 * @brief Values of Type field in ACPI DMAR Device Scope Structure
 */
enum acpi_dmar_scope_type {
	ACPI_DMAR_SCOPE_TYPE_NOT_USED       = 0, /**< Unused value */
	ACPI_DMAR_SCOPE_TYPE_ENDPOINT       = 1, /**< PCI endpoint device */
	ACPI_DMAR_SCOPE_TYPE_BRIDGE         = 2, /**< PCI sub-hierarchy device */
	ACPI_DMAR_SCOPE_TYPE_IOAPIC         = 3, /**< IOAPIC device */
	ACPI_DMAR_SCOPE_TYPE_HPET           = 4, /**< HPET device */
	ACPI_DMAR_SCOPE_TYPE_NAMESPACE      = 5, /**< ACPI name-space enumeration device */
	ACPI_DMAR_SCOPE_TYPE_RESERVED       = 6 /**< 6 and greater are reserved */
};

/**
 * @brief IOMMU domain structure
 */
struct iommu_domain {
	uint16_t vm_id;           /**< VM ID which domain belongs to */
	uint32_t addr_width;      /**< Address width of the domain */
	uint64_t trans_table_ptr; /**< Pointer to domain's translation table */
};

/**
 * @brief Source union for interrupt source structure
 */
union source {
	uint16_t ioapic_id; /**< IOAPIC ID */
	union pci_bdf msi;  /**< PCI ID */
};

/**
 * @brief Interrupt source structure
 */
struct intr_source {
	bool is_msi;      /**< Flag indicating Message Signaled Interrupt */
	union source src; /**< Source of the interrupt */
	/**
	 * @brief Address of posted interrupt descriptor
	 *
	 * pid_paddr = 0: invalid address, indicate that remapped mode shall be used
	 *
	 * pid_paddr != 0: physical address of posted interrupt descriptor, indicate
	 * that posted mode shall be used
	 */
	uint64_t pid_paddr;
};

/**
 * @brief Return major number of the Version Register
 *
 * @param[in] version the Version Register to parse
 *
 * @return major number of the Version Register
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t dmar_ver_major(uint64_t version)
{
	return (((uint8_t)version & 0xf0U) >> 4U);
}

/**
 * @brief Return minor number of the Version Register
 *
 * @param[in] version the Version Register to parse
 *
 * @return minor number of the Version Register
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t dmar_ver_minor(uint64_t version)
{
	return ((uint8_t)version & 0x0fU);
}

/**
 * @brief Return Posted Interrupts Support bit in the Capability Register
 *
 * @param[in] cap the Capability Register
 *
 * @return Posted Interrupts Support bit
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_cap_pi(uint64_t cap)
{
	return ((uint8_t)(cap >> 59U) & 1U);
}

/**
 * @brief Return Read Draining bit in the Capability Register
 *
 * @param[in] cap the Capability Register
 *
 * @return Read Draining bit
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_cap_read_drain(uint64_t cap)
{
	return ((uint8_t)(cap >> 55U) & 1U);
}

/**
 * @brief Return Write Draining bit in the Capability Register
 *
 * @param[in] cap the Capability Register
 *
 * @return Write Draining bit
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_cap_write_drain(uint64_t cap)
{
	return ((uint8_t)(cap >> 54U) & 1U);
}

/**
 * @brief Return Maximum Address Mask Value in the Capability Register
 *
 * @param[in] cap the Capability Register
 *
 * @return Maximum Address Mask Value
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_cap_max_amask_val(uint64_t cap)
{
	return ((uint8_t)(cap >> 48U) & 0x3fU);
}

/**
 * @brief Return Number of Fault-recording Registers field in the Capability Register
 *
 * @param[in] cap the Capability Register
 *
 * @return the number of fault-recording registers
 *
 * @pre N/A
 *
 * @post retval >= 1
 */
static inline uint16_t iommu_cap_num_fault_regs(uint64_t cap)
{
	return (((uint16_t)(cap >> 40U) & 0xffU) + 1U);
}

/**
 * @brief Return Page Selective Invalidation bit in the Capability Register
 *
 * @param[in] cap the Capability Register
 *
 * @return Page Selective Invalidation bit
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_cap_pgsel_inv(uint64_t cap)
{
	return ((uint8_t)(cap >> 39U) & 1U);
}

/**
 * @brief Return Second Stage Large Page Support field in the Capability Register
 *
 * @param[in] cap the Capability Register
 *
 * @return value of Second Stage Large Page Support
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_cap_super_page_val(uint64_t cap)
{
	return ((uint8_t)(cap >> 34U) & 0xfU);
}

/**
 * @brief Return Fault-recording Register Offset field in the Capability Register
 *
 * @param[in] cap the Capability Register
 *
 * @return Fault-recording register offset
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint16_t iommu_cap_fault_reg_offset(uint64_t cap)
{
	return (((uint16_t)(cap >> 24U) & 0x3ffU) * 16U);
}

/**
 * @brief Calculate maximum fault-recording register offset for the Capability Register
 *
 * @param[in] cap the Capability Register
 *
 * @return Maximum fault-recording register offset
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint16_t iommu_cap_max_fault_reg_offset(uint64_t cap)
{
	return (iommu_cap_fault_reg_offset(cap) +
		(iommu_cap_num_fault_regs(cap) * 16U));
}

/**
 * @brief Return Zero Length Read bit in the Capability Register
 *
 * @param[in] cap the Capability Register
 *
 * @return Zero Length Read bit
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_cap_zlr(uint64_t cap)
{
	return ((uint8_t)(cap >> 22U) & 1U);
}

/**
 * @brief Return Spatial Separation bit in the Capability Register
 *
 * @param[in] cap the Capability Register
 *
 * @return Spatial Separation bit
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_cap_isoch(uint64_t cap)
{
	return ((uint8_t)(cap >> 23U) & 1U);
}

/**
 * @brief Calculate Maximum Guest Address Width for the Capability Register
 *
 * @param[in] cap the Capability Register
 *
 * @return Maximum Guest Address width
 *
 * @pre N/A
 *
 * @post retval >= 1
 */
static inline uint8_t iommu_cap_mgaw(uint64_t cap)
{
	return (((uint8_t)(cap >> 16U) & 0x3fU) + 1U);
}

/**
 * @brief Return Supported Adjusted Guest Address Width fields in the Capability Register
 *
 * @param[in] cap the Capability Register
 *
 * @return Supported Adjusted Guest Address width
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_cap_sagaw(uint64_t cap)
{
	return ((uint8_t)(cap >> 8U) & 0x1fU);
}

/**
 * @brief Return Caching Mode bit in the Capability Register
 *
 * @param[in] cap the Capability Register
 *
 * @return Caching Mode bit
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_cap_caching_mode(uint64_t cap)
{
	return ((uint8_t)(cap >> 7U) & 1U);
}

/**
 * @brief Return Protected High-Memory Region bit in the Capability Register
 *
 * @param[in] cap the Capability Register
 *
 * @return Protected High-Memory Region bit
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_cap_phmr(uint64_t cap)
{
	return ((uint8_t)(cap >> 6U) & 1U);
}

/**
 * @brief Return Protected Low-Memory Region bit in the Capability Register
 *
 * @param[in] cap the Capability Register
 *
 * @return Protected Low-Memory Region bit
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_cap_plmr(uint64_t cap)
{
	return ((uint8_t)(cap >> 5U) & 1U);
}

/**
 * @brief Return Advanced Fault Logging bit in the Capability Register
 *
 * @param[in] cap the Capability Register
 *
 * @return Advanced Fault Logging bit
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_cap_afl(uint64_t cap)
{
	return ((uint8_t)(cap >> 3U) & 1U);
}

/**
 * @brief Calculate the number of domains supported from the Capability Register
 *
 * @param[in] cap the Capability Register
 *
 * @return The number of domains supported
 *
 * @pre N/A
 *
 * @post retval >= 16
 */
static inline uint32_t iommu_cap_ndoms(uint64_t cap)
{
	return (1U << (((cap & 0x7U) * 2U) + 4U));
}

/**
 * @brief Return Page-walk Coherency bit in the Extended Capability Register
 *
 * @param[in] ecap the Extended Capability Register
 *
 * @return Page-walk Coherency bit
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_ecap_c(uint64_t ecap)
{
	return ((uint8_t)(ecap >> 0U) & 1U);
}

/**
 * @brief Return Queued Invalidation Support bit in the Extended Capability Register
 *
 * @param[in] ecap the Extended Capability Register
 *
 * @return Queued Invalidation Support bit
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_ecap_qi(uint64_t ecap)
{
	return ((uint8_t)(ecap >> 1U) & 1U);
}

/**
 * @brief Return Device-TLB Support bit in the Extended Capability Register
 *
 * @param[in] ecap the Extended Capability Register
 *
 * @return Device-TLB Support bit
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_ecap_dt(uint64_t ecap)
{
	return ((uint8_t)(ecap >> 2U) & 1U);
}

/**
 * @brief Return Interrupt Remapping Support bit in the Extended Capability Register
 *
 * @param[in] ecap the Extended Capability Register
 *
 * @return Interrupt Remapping Support bit
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_ecap_ir(uint64_t ecap)
{
	return ((uint8_t)(ecap >> 3U) & 1U);
}

/**
 * @brief Return Extended Interrupt Mode bit in the Extended Capability Register
 *
 * @param[in] ecap the Extended Capability Register
 *
 * @return Extended Interrupt Mode bit
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_ecap_eim(uint64_t ecap)
{
	return ((uint8_t)(ecap >> 4U) & 1U);
}

/**
 * @brief Return Pass Through bit in the Extended Capability Register
 *
 * @param[in] ecap the Extended Capability Register
 *
 * @return Pass Through bit
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_ecap_pt(uint64_t ecap)
{
	return ((uint8_t)(ecap >> 6U) & 1U);
}

/**
 * @brief Return IOTLB Register Offset field in the Extended Capability Register
 *
 * @param[in] ecap the Extended Capability Register
 *
 * @return IOTLB register offset
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint16_t iommu_ecap_iro(uint64_t ecap)
{
	return ((uint16_t)(ecap >> 8U) & 0x3ffU);
}

/**
 * @brief Return Maximum Handle Mask Value field in the Extended Capability Register
 *
 * @param[in] ecap the Extended Capability Register
 *
 * @return Maximum Handle Mask value
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_ecap_mhmv(uint64_t ecap)
{
	return ((uint8_t)(ecap >> 20U) & 0xfU);
}

/**
 * @brief Return Memory Type Support bit in the Extended Capability Register
 *
 * @param[in] ecap the Extended Capability Register
 *
 * @return Memory Type Support bit
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_ecap_mts(uint64_t ecap)
{
	return ((uint8_t)(ecap >> 25U) & 1U);
}

/**
 * @brief Return Nested Translation Support bit in the Extended Capability Register
 *
 * @param[in] ecap the Extended Capability Register
 *
 * @return Nested Translation Support bit
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_ecap_nest(uint64_t ecap)
{
	return ((uint8_t)(ecap >> 26U) & 1U);
}

/**
 * @brief Return Page Request Support bit in the Extended Capability Register
 *
 * @param[in] ecap the Extended Capability Register
 *
 * @return Page Request Support bit
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_ecap_prs(uint64_t ecap)
{
	return ((uint8_t)(ecap >> 29U) & 1U);
}

/**
 * @brief Return Execute Request Support bit in the Extended Capability Register
 *
 * @param[in] ecap the Extended Capability Register
 *
 * @return Execute Request Support bit
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_ecap_ers(uint64_t ecap)
{
	return ((uint8_t)(ecap >> 30U) & 1U);
}

/**
 * @brief Return Supervisor Request Support bit in the Extended Capability Register
 *
 * @param[in] ecap the Extended Capability Register
 *
 * @return Supervisor Request Support bit
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_ecap_srs(uint64_t ecap)
{
	return ((uint8_t)(ecap >> 31U) & 1U);
}

/**
 * @brief Return No Write Flag Support bit in the Extended Capability Register
 *
 * @param[in] ecap the Extended Capability Register
 *
 * @return No Write Flag Support bit
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_ecap_nwfs(uint64_t ecap)
{
	return ((uint8_t)(ecap >> 33U) & 1U);
}

/**
 * @brief Return Extended Accessed Flag Support bit in the Extended Capability Register
 *
 * @param[in] ecap the Extended Capability Register
 *
 * @return Extended Accessed Flag Support bit
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_ecap_eafs(uint64_t ecap)
{
	return ((uint8_t)(ecap >> 34U) & 1U);
}

/**
 * @brief Return PASID Size Supported field in the Extended Capability Register
 *
 * @param[in] ecap the Extended Capability Register
 *
 * @return PASID Size Supported bit
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_ecap_pss(uint64_t ecap)
{
	return ((uint8_t)(ecap >> 35U) & 0x1fU);
}

/**
 * @brief Return Process Address Space ID Support bit in the Extended Capability Register
 *
 * @param[in] ecap the Extended Capability Register
 *
 * @return Process Address Space ID Support bit
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_ecap_pasid(uint64_t ecap)
{
	return ((uint8_t)(ecap >> 40U) & 1U);
}

/**
 * @brief Return Device-TLB Invalidation Throttle bit in the Extended Capability Register
 *
 * @param[in] ecap the Extended Capability Register
 *
 * @return Device-TLB Invalidation Throttle bit
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_ecap_dit(uint64_t ecap)
{
	return ((uint8_t)(ecap >> 41U) & 1U);
}

/**
 * @brief Return Page-request Drain Support bit in the Extended Capability Register
 *
 * @param[in] ecap the Extended Capability Register
 *
 * @return Page-request Drain Support bit
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_ecap_pds(uint64_t ecap)
{
	return ((uint8_t)(ecap >> 42U) & 1U);
}

/**
 * @brief Protected Memory Enable Register
 */
#define DMA_PMEN_EPM (1U << 31U) /**< Enable Protected Memory bit */
#define DMA_PMEN_PRS (1U << 0U)  /**< Protected Region Status bit */

/**
 * @brief Global Command Register
 */
#define DMA_GCMD_TE (1U << 31U)    /**< Translation Enable bit */
#define DMA_GCMD_SRTP (1U << 30U)  /**< Set Root Table Pointer bit */
#define DMA_GCMD_SFL (1U << 29U)   /**< Set Fault Log bit */
#define DMA_GCMD_EAFL (1U << 28U)  /**< Enable Advanced Fault Logging bit */
#define DMA_GCMD_WBF (1U << 27U)   /**< Write Buffer Flush bit */
#define DMA_GCMD_QIE (1U << 26U)   /**< Queued Invalidation Enable bit */
#define DMA_GCMD_SIRTP (1U << 24U) /**< Set Interrupt Remap Table Pointer bit */
#define DMA_GCMD_IRE (1U << 25U)   /**< Interrupt Remapping Enable bit */
#define DMA_GCMD_CFI (1U << 23U)   /**< Compatibility Format Interrupt bit */

/**
 * @brief Global Status Register
 */
#define DMA_GSTS_TES (1U << 31U)   /**< Translation Enable Status bit */
#define DMA_GSTS_RTPS (1U << 30U)  /**< Rot Table Pointer Status bit */
#define DMA_GSTS_FLS (1U << 29U)   /**< Fault Log Status bit */
#define DMA_GSTS_AFLS (1U << 28U)  /**< Advanced Fault Logging Status bit */
#define DMA_GSTS_WBFS (1U << 27U)  /**< Write Buffer Flush Status bit */
#define DMA_GSTS_QIES (1U << 26U)  /**< Queued Invalidation Enable Status bit */
#define DMA_GSTS_IRTPS (1U << 24U) /**< Interrupt Remapping Table Pointer Status bit */
#define DMA_GSTS_IRES (1U << 25U)  /**< Interrupt Remapping Enable Status bit */
#define DMA_GSTS_CFIS (1U << 23U)  /**< Compatibility Format Interrupt Status bit */

/**
 * @brief Context-cache Invalidate Descriptor
 */
#define DMA_CONTEXT_GLOBAL_INVL (1UL << 4U) /**< Global invalidation granularity */
#define DMA_CONTEXT_DOMAIN_INVL (2UL << 4U) /**< Domain-selective invalidation granularity */
#define DMA_CONTEXT_DEVICE_INVL (3UL << 4U) /**< Device-selective invalidation granularity */
/**
 * @brief Construct Function Mask field in Context-cache Invalidate Descriptor from the specified value
 *
 * @param[in] fm The value of Function Mask
 *
 * @return Constructed Function Mask field in Context-cache Invalidate Descriptor
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint64_t dma_ccid_fm(uint8_t fm)
{
	return (((uint64_t)fm & 0x3UL) << 48UL);
}

/**
 * @brief Construct Source-ID field in Context-cache Invalidate Descriptor from the specified value
 *
 * @param[in] sid The value of Source ID
 *
 * @return Constructed Source-ID field in Context-cache Invalidate Descriptor
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint64_t dma_ccid_sid(uint16_t sid)
{
	return (((uint64_t)sid & 0xffffUL) << 32UL);
}

/**
 * @brief Construct Domain-ID field in Context-cache Invalidate Descriptor from the specified value
 *
 * @param[in] did The value of Domain ID
 *
 * @return Constructed Domain-ID field in Context-cache Invalidate Descriptor
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint64_t dma_ccid_did(uint16_t did)
{
	return (((uint64_t)did & 0xffffUL) << 16UL);
}

/**
 * @brief IOTLB Invalidate Descriptor
 */
#define DMA_IOTLB_GLOBAL_INVL		(((uint64_t)1UL) << 4U)  /**< Global invalidation granularity */
#define DMA_IOTLB_DOMAIN_INVL		(((uint64_t)2UL) << 4U)  /**< Domain-selective invalidation granularity */
#define DMA_IOTLB_PAGE_INVL		(((uint64_t)3UL) << 4U)  /**< Page-selective-within-domain invalidation
                                                                      granularity */
#define DMA_IOTLB_DR			(((uint64_t)1UL) << 7U)  /**< Drain Reads bit */
#define DMA_IOTLB_DW			(((uint64_t)1UL) << 6U)  /**< Drain Writes bit */
/**
 * @brief Construct Domain-ID field in IOTLB Invalidate Descriptor from the specified value
 *
 * @param[in] did value of Domain-ID
 *
 * @return Constructed Domain-ID field in IOTLB Invalidate Descriptor
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint64_t dma_iotlb_did(uint16_t did)
{
	return (((uint64_t)did & 0xffffUL) << 16UL);
}

/**
 * @brief Get Address Mask field from the specified value
 *
 * @param[in] am value to extract Address Mask from
 *
 * @return Value of Address Mask
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t dma_iotlb_invl_addr_am(uint8_t am)
{
	return (am & 0x3fU);
}

/**
 * @brief Interrupt Entry Cache Invalidate Descriptor
 */
#define DMAR_IECI_INDEXED		(((uint64_t)1UL) << 4U) /**< Index-selective invalidation */
#define DMAR_IEC_GLOBAL_INVL		(((uint64_t)0UL) << 4U) /**< Global invalidation */
/**
 * @brief Construct Interrupt Index and Index Mask fields in Interrupt Entry Cache Invalidate Descriptor
 *
 * @param[in] index Value of interrupt index
 * @param[in] index_mask Value of index mask
 *
 * @return Constructed Interrupt Index and Index Mask fields
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint64_t dma_iec_index(uint16_t index, uint8_t index_mask)
{
	return ((((uint64_t)index & 0xFFFFU) << 32U) | (((uint64_t)index_mask & 0x1FU) << 27U));
}

/**
 * @brief Invalidation Hint bit for page-selective-within-domain invalidations
 */
#define DMA_IOTLB_INVL_ADDR_IH_UNMODIFIED	(((uint64_t)1UL) << 6U)

/**
 * @brief Interrupt Mask bit in Fault Event Control register
 */
#define DMA_FECTL_IM				(((uint32_t)1U) << 31U)

/**
 * @brief Return whether Primary Fault Overflow bit in Fault Status register is set or not
 *
 * @param[in] fsr Fault Status register
 *
 * @return One boolean value indicating the bit is set or not
 * @retval true The bit is set.
 * @retval false The bit is not set.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline bool dma_fsts_pfo(uint32_t fsr)
{
	return (((fsr >> 0U) & 1U) == 1U);
}

/**
 * @brief Return whether Primary Pending Fault bit in Fault Status register is set or not
 *
 * @param[in] fsr Fault Status register
 *
 * @return One boolean value indicating the bit is set or not
 * @retval true The bit is set.
 * @retval false The bit is not set.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline bool dma_fsts_ppf(uint32_t fsr)
{
	return (((fsr >> 1U) & 1U) == 1U);
}

/**
 * @brief Return Invalidation Queue Error bit in Fault Status register is set or not
 *
 * @param[in] iqe Fault Status register
 *
 * @return One boolean value indicating the bit is set or not
 * @retval true The bit is set.
 * @retval false The bit is not set.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline bool dma_fsts_iqe(uint32_t iqe)
{
	return (((iqe >> 4U) & 1U) == 1U);
}

/**
 * @brief Return whether Invalidation Completion Error bit in Fault Status register is set or not
 *
 * @param[in] ice Fault Status register
 *
 * @return One boolean value indicating the bit is set or not
 * @retval true The bit is set.
 * @retval false The bit is not set.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline bool dma_fsts_ice(uint32_t ice)
{
	return (((ice >> 5U) & 1U) == 1U);
}

/**
 * @brief Return whether Invalidation Time-out Error bit in Fault Status register is set or not
 *
 * @param[in] ite Fault Status register
 *
 * @return One boolean value indicating the bit is set or not
 * @retval true The bit is set.
 * @retval false The bit is not set.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline bool dma_fsts_ite(uint32_t ite)
{
	return (((ite >> 6U) & 1U) == 1U);
}

/**
 * @brief Return Fault Record Index value in Fault Status register
 *
 * @param[in] fri Fault Status register
 *
 * @return value of Fault Record Index
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t dma_fsts_fri(uint32_t fri)
{
	return ((uint8_t)(fri >> 8U) & 0xFFU);
}

/**
 * @brief Return whether Fault bit in upper 64-bit of Fault Recording register is set or not
 *
 * @param[in] frr_hi upper 64-bit of Fault Recording register
 *
 * @return One boolean value indicating Fault bit is set or not
 * @retval true The bit is set
 * @retval false The bit is not set
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline bool dma_frcd_up_f(uint64_t frr_hi)
{
	return (((frr_hi >> 63U) & 1UL) == 1UL);
}

/**
 * @brief Return Value of Type bit in upper 64-bit of Fault Recording register
 *
 * @param[in] frr_hi upper 64-bit of Fault Recording register
 *
 * @return Value of Type bit
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t dma_frcd_up_t(uint64_t frr_hi)
{
	return ((uint8_t)(frr_hi >> 62U) & 1U);
}

/**
 * @brief Return Value of Address Type field in upper 64-bit of Fault Recording register
 *
 * @param[in] frr_hi upper 64-bit of Fault Recording register
 *
 * @return Value of Address Type field
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t dma_frcd_up_at(uint64_t frr_hi)
{
	return ((uint8_t)(frr_hi >> 60U) & 3U);
}

/**
 * @brief Return Value of PASID field in upper 64-bit of Fault Recording register
 *
 * @param[in] frr_hi upper 64-bit of Fault Recording register
 *
 * @return Value of PASID field
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint32_t dma_frcd_up_pasid(uint64_t frr_hi)
{
	return ((uint32_t)(frr_hi >> 40U) & 0xfffffU);
}

/**
 * @brief Return Value of Fault Reason field in upper 64-bit of Fault Recording register
 *
 * @param[in] frr_hi upper 64-bit of Fault Recording register
 *
 * @return Value of Fault Reason field
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t dma_frcd_up_fr(uint64_t frr_hi)
{
	return ((uint8_t)(frr_hi >> 32U) & 0xffU);
}

/**
 * @brief Return whether PASID Present bit in upper 64-bit of Fault Recording register is set or not
 *
 * @param[in] frr_hi upper 64-bit of Fault Recording register
 *
 * @return One boolean value indicating PASID Present bit is set or not
 * @retval true The bit is set.
 * @retval false The bit is not set.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline bool dma_frcd_up_pp(uint64_t frr_hi)
{
	return (((frr_hi >> 31U) & 1UL) == 1UL);
}

/**
 * @brief Return whether Execute Permission Requested bit in upper 64-bit of Fault Recording register is set or not
 *
 * @param[in] frr_hi upper 64-bit of Fault Recording register
 *
 * @return One boolean value indicating Execute Permission Requested bit is set or not
 * @retval true The bit is set.
 * @retval false The bit is not set.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline bool dma_frcd_up_exe(uint64_t frr_hi)
{
	return (((frr_hi >> 30U) & 1UL) == 1UL);
}

/**
 * @brief Return whether Privilege Mode Requested bit in upper 64-bit of Fault Recording register is set or not
 *
 * @param[in] frr_hi upper 64-bit of Fault Recording register
 *
 * @return One boolean value indicating Privilege Mode Requested bit is set or not
 * @retval true The bit is set.
 * @retval false The bit is not set.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline bool dma_frcd_up_priv(uint64_t frr_hi)
{
	return (((frr_hi >> 29U) & 1UL) == 1UL);
}

/**
 * @brief Return value of Source Identifier field in upper 64-bit of Fault Recording register
 *
 * @param[in] frr_hi upper 64-bit of Fault Recording register
 *
 * @return Value of Source Identifier field
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint16_t dma_frcd_up_sid(uint64_t frr_hi)
{
	return ((uint16_t)frr_hi & 0xffffU);
}

#define MAX_DRHDS		DRHD_COUNT /**< Maximum number of DMA Remapping Hardware Unit Definition Structures */
#define MAX_DRHD_DEVSCOPES	DRHD_MAX_DEVSCOPE_COUNT /**< Maximum number of DRHR Device Scope Structures */

#define DRHD_FLAG_INCLUDE_PCI_ALL_MASK      (1U) /**< Mask of Flags field bit 0 in DMA Remapping Hardware Unit
                                                      Definition Structure */

/**
 * @brief Macro to combine PCI device number and function number into one 32-bit integer.
 *
 * Device number (ANDed with a 5-bit mask) starts at bit position 3 in the integer. Function number (ANDed with a 3-bit
 * mask) starts at bit position 0.
 *
 * @param[in] dev the specified device number
 * @param[in] fun the specified function number
 *
 * @return an integer constructed from the specified device number and function number
 *
 * @pre N/A
 * @post N/A
 */
#define DEVFUN(dev, fun)            ((((dev) & 0x1FU) << 3U) | (((fun) & 0x7U)))

/**
 * @brief Device scope structure that identifies device in the specified segment and under DMAR
 */
struct dmar_dev_scope {
	enum acpi_dmar_scope_type type; /**< Device type */
	uint8_t id;                     /**< Enumeration ID */
	uint8_t bus;                    /**< Bus number under which the device identified by this device scope
                                             resides */
	uint8_t devfun;                 /**< Device and function ID of the device identified by this device scope */
};

/**
 * @brief DMA remapping hardware unit definition structure
 */
struct dmar_drhd {
	uint32_t dev_cnt;               /**< Number of devices under this DMAR unit */
	uint16_t segment;               /**< The PCI segment associated with this unit */
	uint8_t flags;                  /**< DRHD flags */
	bool ignore;                    /**< Indicate if this unit is ignored */
	uint64_t reg_base_addr;         /**< Base address of remapping hardware register-set for this unit */
	/* assume no pci device hotplug support */
	struct dmar_dev_scope *devices; /**< Pointer to device scopes under this unit */
};

/**
 * @brief DMAR information structure
 */
struct dmar_info {
	uint32_t drhd_count;          /**< Number of DRHD structures */
	struct dmar_drhd *drhd_units; /**< Pointer to DRHD structures */
};

/**
 * @brief 128-bit DMAR entry structure
 */
struct dmar_entry {
	uint64_t lo_64; /**< Low 64-bit of entry */
	uint64_t hi_64; /**< High 64-bit of entry */
};

/**
 * @brief DMAR interrupt remapping table entry union structure
 *
 * @remark This structure shall be packed.
 */
union dmar_ir_entry {
	struct dmar_entry value; /**< Raw entry value */

	union {
		/**
                 * @brief Entry structure for remapped interrupts
                 */
		struct {
			uint64_t present:1;       /**< Entry present */
			uint64_t fpd:1;           /**< Fault processing disable */
			uint64_t dest_mode:1;     /**< Destination mode */
			uint64_t rh:1;            /**< Redirection hint */
			uint64_t trigger_mode:1;  /**< Trigger mode */
			uint64_t delivery_mode:3; /**< Delivery mode */
			uint64_t avail:4;         /**< Available */
			uint64_t rsvd_1:3;        /**< Reserved */
			uint64_t mode:1;          /**< IRTE mode */
			uint64_t vector:8;        /**< Vector */
			uint64_t rsvd_2:8;        /**< Reserved */
			uint64_t dest:32;         /**< Destination ID */

			uint64_t sid:16;          /**< Source identifier */
			uint64_t sq:2;            /**< Source-id qualifier */
			uint64_t svt:2;           /**< Source validation type */
			uint64_t rsvd_3:44;       /**< Reserved */
		} remap;

		/**
                 * @brief Entry structure for posted interrupts
                 */
		struct {
			uint64_t present:1;      /**< Entry present */
			uint64_t fpd:1;          /**< Fault processing disable */
			uint64_t rsvd_1:6;       /**< Reserved */
			uint64_t avail:4;        /**< Available */
			uint64_t rsvd_2:2;       /**< Reserved */
			uint64_t urgent:1;       /**< Urgent */
			uint64_t mode:1;         /**< IRTE mode */
			uint64_t vector:8;       /**< Virtual vector */
			uint64_t rsvd_3:14;      /**< Reserved */
			uint64_t pda_l:26;       /**< Posted descriptor address low */

			uint64_t sid:16;         /**< Source identifier */
			uint64_t sq:2;           /**< Source-id qualifier */
			uint64_t svt:2;          /**< Source validation type */
			uint64_t rsvd_4:12;      /**< Reserved */
			uint64_t pda_h:32;       /**< Posted descriptor address high */
		} post;
	} bits __packed;
};

#ifdef CONFIG_ACPI_PARSE_ENABLED
int parse_dmar_table(struct dmar_info *plat_dmar);
#endif

struct iommu_domain;

int32_t move_pt_device(const struct iommu_domain *from_domain, const struct iommu_domain *to_domain, uint8_t bus,
                       uint8_t devfun);

struct iommu_domain *create_iommu_domain(uint16_t vm_id, uint64_t translation_table, uint32_t addr_width);

void destroy_iommu_domain(struct iommu_domain *domain);

void suspend_iommu(void);

void resume_iommu(void);

int32_t init_iommu(void);

int32_t dmar_assign_irte(const struct intr_source *intr_src, union dmar_ir_entry *irte,
	uint16_t idx_in, uint16_t *idx_out);

void dmar_free_irte(const struct intr_source *intr_src, uint16_t index);

void iommu_flush_cache(const void *p, uint32_t size);

/**
  * @}
  */
#endif
