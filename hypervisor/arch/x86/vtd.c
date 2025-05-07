/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @brief The prefix used for debug messages in this file.
 */
#define pr_prefix		"iommu: "

#include <types.h>
#include <errno.h>
#include <asm/lib/bits.h>
#include <asm/lib/spinlock.h>
#include <asm/cpu_caps.h>
#include <irq.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/mmu.h>
#include <asm/lapic.h>
#include <asm/vtd.h>
#include <ticks.h>
#include <logmsg.h>
#include <asm/board.h>
#include <asm/vm_config.h>
#include <pci.h>
#include <asm/platform_caps.h>

/**
 * @defgroup hwmgmt_vtd hwmgmt.vtd
 * @ingroup hwmgmt
 * @brief Support Intel Virtualization Technology for Directed I/O.
 *
 * This module provides the following resource isolation and protection
 * capabilities on Intel platforms for I/O device virtualization:
 *
 * DMA remapping: for supporting address translations for Direct Memory
 * Accesses (DMA) from devices.
 * Interrupt remapping: for supporting isolation and routing of interrupts
 * from devices and external interrupt controllers to appropriate VMs.
 * Interrupt posting:for supporting direct delivery of virtual interrupts
 * from devices and external interrupt controllers to virtual processors.
 * Reliability: for recording and reporting of DMA and interrupt errors to
 * system software that may otherwise corrupt memory or impact VM isolation.
 *
 * This module is used by hypervisor to pass through devices to VMs.
 *
 * @{
 */

/**
 * @file
 * @brief Implement VT-d core capabilities on Intel platform.
 *
 * This file manages DMA-remapping hardware (with interrupt-remapping/posting
 * capabilities). Provides external APIs to create/destroy iommu domain, map
 * device to domain, remap device interrupt and control the DMA-remapping
 * hardware.
 *
 */

#define DBG_IOMMU 0	/**< debug control flag */

#if DBG_IOMMU
#define DBG_LEVEL_IOMMU LOG_DEBUG	/**< debug log severity level as DEBUG */
#else
#define DBG_LEVEL_IOMMU 0		/**< no debug log */
#endif
/**
 * @brief The stride length (in bits) used for page-table walks.
 *
 * This value is defined in VT-d specification.
 */
#define LEVEL_WIDTH 9U

/**
 * @brief Starting bit position of Present field in low 64-bits of Root Entry.
 *
 * Present field is located at bit [0] in the Root Entry (128-bit).
 */
#define ROOT_ENTRY_LOWER_PRESENT_POS        (0U)
/**
 * @brief Bit mask of Present field in low 64-bits of Root Entry.
 *
 * Present field is located at bit [0] in the Root Entry (128-bit).
 */
#define ROOT_ENTRY_LOWER_PRESENT_MASK       (1UL << ROOT_ENTRY_LOWER_PRESENT_POS)
/**
 * @brief Starting bit position of CTP (Context-table Pointer) field in low 64-bits of Root Entry.
 *
 * CTP field is located at bits [63:12] in the Root Entry (128-bit).
 */
#define ROOT_ENTRY_LOWER_CTP_POS            (12U)
/**
 * @brief Bit mask of CTP (Context-table Pointer) field in low 64-bits of Root Entry.
 *
 * CTP field is located at bits [63:12] in the Root Entry (128-bit).
 */
#define ROOT_ENTRY_LOWER_CTP_MASK           (0xFFFFFFFFFFFFFUL << ROOT_ENTRY_LOWER_CTP_POS)

/**
 * @brief Maximum number of DMA-remapping hardware units
 */
#define CONFIG_MAX_IOMMU_NUM		DRHD_COUNT

#define	IOMMU_FAULT_REGISTER_STATE_NUM	4U /**< number of fault registers to save/restore in IOMMU suspend/resume */
#define	IOMMU_FAULT_REGISTER_SIZE	4U /**< size in bytes of every fault register */

/**
 * @brief Starting bit position of AW (Address Width) field in high 64-bits of Context Entry.
 *
 * AW field is located at bits [66:64] in the Context Entry (128-bit).
 */
#define CTX_ENTRY_UPPER_AW_POS          (0U)
/**
 * @brief Bit mask of AW (Address Width) field in high 64-bits of Context Entry.
 *
 * AW field is located at bits [66:64] in the Context Entry (128-bit).
 */
#define CTX_ENTRY_UPPER_AW_MASK         (0x7UL << CTX_ENTRY_UPPER_AW_POS)
/**
 * @brief Starting bit position of DID (Domain Identifier) field in high 64-bits of Context Entry.
 *
 * DID field is located at bits [87:72] in the Context Entry (128-bit).
 */
#define CTX_ENTRY_UPPER_DID_POS         (8U)
/**
 * @brief Bit mask of DID (Domain Identifier) field in high 64-bits of Context Entry.
 *
 * DID field is located at bits [87:72] in the Context Entry (128-bit).
 */
#define CTX_ENTRY_UPPER_DID_MASK        (0xFFFFUL << CTX_ENTRY_UPPER_DID_POS)
/**
 * @brief Starting bit position of Present field in low 64-bits of Context Entry.
 *
 * Present field is located at bit [0] in the Context Entry (128-bit).
 */
#define CTX_ENTRY_LOWER_P_POS           (0U)
/**
 * @brief Bit mask of Present field in low 64-bits of Context Entry.
 *
 * Present field is located at bit [0] in the Context Entry (128-bit).
 */
#define CTX_ENTRY_LOWER_P_MASK          (0x1UL << CTX_ENTRY_LOWER_P_POS)
/**
 * @brief Starting bit position of FPD (Fault Processing Disable) field in low 64-bits of Context Entry.
 *
 * FPD field is located at bit [1] in the Context Entry (128-bit).
 */
#define CTX_ENTRY_LOWER_FPD_POS         (1U)
/**
 * @brief Bit mask of FPD (Fault Processing Disable) field in low 64-bits of Context Entry.
 *
 * FPD field is located at bit [1] in the Context Entry (128-bit).
 */
#define CTX_ENTRY_LOWER_FPD_MASK        (0x1UL << CTX_ENTRY_LOWER_FPD_POS)
/**
 * @brief Starting bit position of TT (Translation Type) field in low 64-bits of Context Entry.
 *
 * TT field is located at bits [3:2] in the Context Entry (128-bit).
 */
#define CTX_ENTRY_LOWER_TT_POS          (2U)
/**
 * @brief Bit mask of TT (Translation Type) field in low 64-bits of Context Entry.
 *
 * TT field is located at bits [3:2] in the Context Entry (128-bit).
 */
#define CTX_ENTRY_LOWER_TT_MASK         (0x3UL << CTX_ENTRY_LOWER_TT_POS)
/**
 * @brief Starting bit position of SLPTPTR field in low 64-bits of Context Entry.
 *
 * SLPTPTR (Second Level Page Translation Pointer) field is located at bits [63:12] in the Context Entry (128-bit).
 */
#define CTX_ENTRY_LOWER_SLPTPTR_POS     (12U)
/**
 * @brief Bit mask of SLPTPTR field in low 64-bits of Context Entry.
 *
 * SLPTPTR (Second Level Page Translation Pointer) field is located at bits [63:12] in the Context Entry (128-bit).
 */
#define CTX_ENTRY_LOWER_SLPTPTR_MASK    (0xFFFFFFFFFFFFFUL <<  CTX_ENTRY_LOWER_SLPTPTR_POS)

/**
 * @brief Extracts a bit slice from a given 64-bit variable.
 *
 * This function extracts a specific bit slice from the input variable `var`
 * by applying a bitmask `mask` and then shifting the result to the right by `pos` positions.
 *
 * @param[in] var The input variable from which the bit slice is to be extracted.
 * @param[in] mask The bitmask to apply to the input variable. This mask should have contiguous bits set to 1 in the
 *                 positions that are part of the bit slice.
 * @param[in] pos The number of positions to shift the masked result to the right. This effectively moves the bit slice
 *                to the least significant bits.
 *
 * @return The extracted bit slice, right-aligned.
 *
 * @pre pos < 64
 *
 * @post N/A
 */
static inline uint64_t dmar_get_bitslice(uint64_t var, uint64_t mask, uint32_t pos)
{
	return ((var & mask) >> pos);
}

/**
 * @brief Sets a specific bit slice in a 64-bit variable.
 *
 * This function modifies a specific bit slice of a 64-bit variable `var` by
 * setting it to the value `val`. The bit slice is defined by a `mask` and
 * a position `pos`. The function first clears the bits in `var` that are
 * specified by the `mask`, then sets those bits to the value `val` shifted
 * to the correct position.
 *
 * @param[in] var The original 64-bit variable.
 * @param[in] mask The mask defining the bit slice to be modified.
 * @param[in] pos The position to shift the value `val` to.
 * @param[in] val The value to set in the specified bit slice.
 *
 * @return The modified 64-bit variable with the specified bit slice set to `val`.
 *
 * @pre pos < 64
 *
 * @post N/A
 */
static inline uint64_t dmar_set_bitslice(uint64_t var, uint64_t mask, uint32_t pos, uint64_t val)
{
	return ((var & ~mask) | ((val << pos) & mask));
}

/**
 * @brief A flag which indicates that TT (Translation Type) field in Context Entry is set to 00b.
 *
 * Please refer to chapter 9.3 Context Entry, Intel Virtualization Technology for Directed I/O Architecture
 * Specification, for details of this field.
 */
#define DMAR_CTX_TT_UNTRANSLATED    0x0UL
/**
 * @brief A flag which indicates that TT (Translation Type) field in Context Entry is set to 01b.
 *
 * Please refer to chapter 9.3 Context Entry, Intel Virtualization Technology for Directed I/O Architecture
 * Specification, for details of this field.
 */
#define DMAR_CTX_TT_ALL             0x1UL
/**
 * @brief A flag which indicates that TT (Translation Type) field in Context Entry is set to 10b.
 *
 * Please refer to chapter 9.3 Context Entry, Intel Virtualization Technology for Directed I/O Architecture
 * Specification, for details of this field.
 */
#define DMAR_CTX_TT_PASSTHROUGH     0x2UL

/**
 * @brief Left shift (bit position) of Delivery Mode (DLM) field in Interrupt Remapping Table Entry (IRTE) for Remapped
 * Interrupts
 */
#define DMAR_MSI_DELIVERY_MODE_SHIFT     (5U)
/**
 * @brief Value of fixed delivery mode for IRTE DLM field
 */
#define DMAR_MSI_DELIVERY_FIXED          (0U << DMAR_MSI_DELIVERY_MODE_SHIFT)
/**
 * @brief Value of lowest priority delivery mode for IRTE DLM field
 */
#define DMAR_MSI_DELIVERY_LOWPRI         (1U << DMAR_MSI_DELIVERY_MODE_SHIFT)

/**
 * @brief Left shift (bit position) of Destination Mode (DM) field in Interrupt Remapping Table Entry (IRTE) for
 * Remapped Interrupts
 */
#define DMAR_MSI_DEST_MODE_SHIFT         (2U)
/**
 * @brief Value of physical destination mode for IRTE DM field
 */
#define DMAR_MSI_DEST_MODE_PHYS          (0U << DMAR_MSI_DEST_MODE_SHIFT)
/**
 * @brief Value of logical destination mode for IRTE DM field
 */
#define DMAR_MSI_DEST_MODE_LOGIC         (1U << DMAR_MSI_DEST_MODE_SHIFT)
/**
 * @brief Left shift (bit position) of Redirection Hint (RH) field in Interrupt Remapping Table Entry (IRTE) for
 * Remapped Interrupts
 */
#define DMAR_MSI_REDIRECTION_SHIFT       (3U)
/**
 * @brief IRTE Redirection Hint indication (RH) value 0, when RH is 0, the interrupt is directed to the processor
 * listed in the Destination ID field.
 */
#define DMAR_MSI_REDIRECTION_CPU         (0U << DMAR_MSI_REDIRECTION_SHIFT)
/**
 * @brief IRTE Redirection Hint indication (RH) value 1, when RH is 1, the interrupt is directed to the processor with
 * the lowest interrupt priority among processors that can receive the interrupt.
 */
#define DMAR_MSI_REDIRECTION_LOWPRI      (1U << DMAR_MSI_REDIRECTION_SHIFT)

/**
 * @brief The size (in bytes) of the invalidation queue.
 */
#define DMAR_INVALIDATION_QUEUE_SIZE	4096U
/**
 * @brief The size (in bytes) of Context-cache invalidate Descriptor/IOTLB invalidate/Interrupt Entry Cache Invalidate
 * Descriptor/Invalidation Wait Descriptor submitted into invalidation queue.
 */
#define DMAR_QI_INV_ENTRY_SIZE		16U
/**
 * @brief The number of Interrupt Remapping Table Entries (IRTE) contained in one 4-KByte page.
 */
#define DMAR_NUM_IR_ENTRIES_PER_PAGE	256U

/**
 * @brief Starting bit position of Status Write field in Invalidation Wait Descriptor.
 *
 * Status Write field is located at bit [5] in the Invalidation Wait Descriptor (128-bit).
 */
#define DMAR_INV_STATUS_WRITE_SHIFT	5U
/**
 * @brief A flag to indicate that the descriptor type is Context-cache Invalidate Descriptor.
 */
#define DMAR_INV_CONTEXT_CACHE_DESC	0x01UL
/**
 * @brief A flag to indicate that the descriptor type is IOTLB Invalidate Descriptor.
 */
#define DMAR_INV_IOTLB_DESC		0x02UL
/**
 * @brief A flag to indicate that the descriptor type is Interrupt Entry Cache Invalidate Descriptor.
 */
#define DMAR_INV_IEC_DESC		0x04UL
/**
 * @brief A flag to indicate that the descriptor type is Invalidation Wait Descriptor.
 */
#define DMAR_INV_WAIT_DESC		0x05UL
/**
 * @brief Setting of '1' in Status Write Field in Invalidation Wait Descriptor.
 *
 * Setting of Status Write field in Invalidation Wait Descriptor indicates hardware performs a coherent write of the
 * status data to the status address field.
 */
#define DMAR_INV_STATUS_WRITE		(1UL << DMAR_INV_STATUS_WRITE_SHIFT)
/**
 * @brief A value to indicate that the invalidation request to hardware is not completed.
 */
#define DMAR_INV_STATUS_INCOMPLETE	0UL
/**
 * @brief A value to indicate that the invalidation request to hardware is completed.
 */
#define DMAR_INV_STATUS_COMPLETED	1UL
/**
 * @brief Starting bit position of Status Data field in Invalidation Wait Descriptor.
 *
 * Status Data field is located at bits [63:32] in the Invalidation Wait Descriptor (128-bit).
 */
#define DMAR_INV_STATUS_DATA_SHIFT	32U
/**
 * @brief The initial setting (invalidation request completed) of the Status Data field in an Invalidation Wait
 * Descriptor.
 */
#define DMAR_INV_STATUS_DATA		(DMAR_INV_STATUS_COMPLETED << DMAR_INV_STATUS_DATA_SHIFT)
/**
 * @brief The initial setting of low 64-bits of an Invalidation Wait Descriptor.
 */
#define DMAR_INV_WAIT_DESC_LOWER	(DMAR_INV_STATUS_WRITE | DMAR_INV_WAIT_DESC | DMAR_INV_STATUS_DATA)

/**
 * @brief Starting bit position of EIME((Extended Interrupt Mode Enable)) field in Interrupt Remapping Table Address
 * Register.
 */
#define DMAR_IR_ENABLE_EIM_SHIFT	11UL
/**
 * @brief Bit mask of EIME (Extended Interrupt Mode Enable) field in Interrupt Remapping Table Address Register.
 */
#define DMAR_IR_ENABLE_EIM		(1UL << DMAR_IR_ENABLE_EIM_SHIFT)

/**
 * @brief Context Invalidation Request Granularity
 *
 * Indicates the requested invalidation granularity in the context cache
 * invalidate descriptor.
 *
 * It is supposed to be used when hypervisor attempts to submit a context-cache invalidation request.
 */
enum dmar_cirg_type {
	DMAR_CIRG_RESERVED = 0,		/**< reserved value. The value is fixed to 0. */
	DMAR_CIRG_GLOBAL,		/**< global invalidation */
	DMAR_CIRG_DOMAIN,		/**< domain-selective invalidation */
	DMAR_CIRG_DEVICE		/**< device-selective invalidation */
};

/**
 * @brief IOTLB invalidation request granularity
 *
 * Indicates the requested invalidation granularity in the IOTLB
 * invalidate descriptor.
 */
enum dmar_iirg_type {
	DMAR_IIRG_RESERVED = 0,		/**< reserved value. The value is fixed to 0. */
	DMAR_IIRG_GLOBAL,		/**< global invalidation */
	DMAR_IIRG_DOMAIN,		/**< domain-selective invalidation */
	DMAR_IIRG_PAGE			/**< page-selective-within-domain invalidation */
};

/**
 * @brief DMA-remapping (DMAR) hardware unit runtime data structure
 *
 * Contains DMAR unit HW info got from ACPI tables and SW data in runtime.
 */
struct dmar_drhd_rt {
	uint32_t index;			/**< index for the DMAR unit */
	spinlock_t lock;		/**< spinlock for DMAR unit protection */

	struct dmar_drhd *drhd;		/**< pointer to DMA remapping hardware unit definition data */

	uint64_t root_table_addr;	/**< sw cache of root table address */
	uint64_t ir_table_addr;		/**< sw cache of interrupt remapping table address */
	/* MAX_IR_ENTRIES is roundup (to power of 2) of CONFIG_MAX_PT_IRQ_ENTRIES. */
	uint64_t irte_alloc_bitmap[MAX_IR_ENTRIES / 64U]; /**< bitmap of allocated Interrupt Remapping Table Entry */
	uint32_t desc_status;	/**< invalidation descriptor status pointed by status address in Invalidation Wait
                                     Descriptor */
	uint64_t qi_queue;	/**< sw cache value of invalidation queue address*/
	uint16_t qi_tail;	/**< sw cache value of invalidation queue tail */

	uint64_t cap;		/**< sw cache value of capability */
	uint64_t ecap;		/**< sw cache value of extended capability */
	uint32_t gcmd;		/**< sw cache value of global cmd */

	uint32_t dmar_irq;	/**< DMAR fault event interrupt number */

	bool cap_pw_coherency;  /**< page-walk coherency */
	uint8_t cap_msagaw;	/**< maximum width in SAGAW field of capability register */
	uint16_t cap_num_fault_regs; /**< number of fault-recording registers in capability register */
	uint16_t cap_fault_reg_offset;	/**< fault-recording register offset in capability register */
	uint16_t ecap_iotlb_offset;	/**< IOTLB register offset in extended capability register */
	uint32_t fault_state[IOMMU_FAULT_REGISTER_STATE_NUM]; /**< sw cache of fault event control/data/address */
};

/**
 * @brief context table for DMA address translation
 *
 * Context table maps devices on a bus to the assigned domains. One context-
 * entry points to the address translation structures of a domain.
 */
struct context_table {
	struct page buses[ACFG_MAX_PCI_BUS_NUM];	/**< array of domain context tables indexed by bus number */
};

/**
 * @brief interrupt remapping table
 *
 * Interrupt remapping table is a memory-resident single-level table containing Interrupt Remapping Table Entries
 * (IRTE). Each IRTE is 16-bytes in size and defines interrupt remapping or posting information.
 */
struct intr_remap_table {
	struct page tables[MAX_IR_ENTRIES/DMAR_NUM_IR_ENTRIES_PER_PAGE]; /**< array of interrupt remapping table
                                                                              entries */
};

/**
 * @brief Root table array
 *
 * This array is indexed by IOMMU number and 4kB size aligned. Every array element is one root table.
 */
static struct page root_tables[CONFIG_MAX_IOMMU_NUM] __aligned(PAGE_SIZE);

/**
 * @brief Allocate a root table for a DMAR unit
 *
 * Return root table pointer from root_tables array using input DMAR index.
 *
 * @param[in] dmar_index index of a DMAR unit
 *
 * @return pointer to the DMAR unit's root table
 *
 * @pre root_tables != NULL
 * @pre dmar_index < CONFIG_MAX_IOMMU_NUM
 *
 * @post N/A
 */
static inline uint8_t *get_root_table(uint32_t dmar_index)
{
	return root_tables[dmar_index].contents;
}

/**
 * @brief Context table array
 *
 * This array is indexed by IOMMU number and 4KB size aligned. Every array element is one context table.
 */
static struct context_table ctx_tables[CONFIG_MAX_IOMMU_NUM] __aligned(PAGE_SIZE);

/**
 * @brief Get context table based on PCI bus number of a specified DMAR unit
 *
 * Return context table pointer for a DMAR unit and PCI bus number from ctx_tables array.
 *
 * @param[in] dmar_index index of the specified DMAR unit
 * @param[in] bus_no PCI bus number which the context table belongs to
 *
 * @return pointer to the bus' context table
 *
 * @pre cxt_tables != NULL
 * @pre dmar_index < CONFIG_MAX_IOMMU_NUM
 * @pre bus_no < ACFG_MAX_PCI_BUS_NUM
 *
 * @post N/A
 */
static inline uint8_t *get_ctx_table(uint32_t dmar_index, uint8_t bus_no)
{
	return ctx_tables[dmar_index].buses[bus_no].contents;
}

/**
 * @brief Invalidation Queue array
 *
 * This array is indexed by IOMMU number and 4KB size aligned. Every array element is one Invalidation Queue.
 */
static struct page qi_queues[CONFIG_MAX_IOMMU_NUM] __aligned(PAGE_SIZE);

/**
 * @brief Get Invalidation Queue base address for a specified DMAR unit
 *
 * @param[in] dmar_index index of the specified DMAR unit
 *
 * @return pointer to the DMAR unit's Invalidation Queue
 *
 * @pre qi_queues != NULL
 * @pre dmar_index < CONFIG_MAX_IOMMU_NUM
 *
 * @post N/A
 */
static inline void *get_qi_queue(uint32_t dmar_index)
{
	return (void *)qi_queues[dmar_index].contents;
}

/**
 * @brief Interrupt Remapping Table array
 *
 * This array is indexed by IOMMU number and 4KB size aligned. Every array element is one Interrupt Remapping Table.
 */
static struct intr_remap_table ir_tables[CONFIG_MAX_IOMMU_NUM] __aligned(PAGE_SIZE);
/**
 * @brief Get Interrupt Remapping Table address for a specified DMAR unit
 *
 * @param[in] dmar_index index of the specified DMAR unit
 *
 * @return pointer to the DMAR unit's Interrupt Remapping Table
 *
 * @pre ir_tables != NULL
 * @pre dmar_index < CONFIG_MAX_IOMMU_NUM
 *
 * @post N/A
 */
static inline void *get_ir_table(uint32_t dmar_index)
{
	return (void *)ir_tables[dmar_index].tables[0].contents;
}

/**
 * @brief DMAR runtime data structure array
 *
 * This array is indexed by IOMMU number. Every array element is one DMAR runtime data structure.
 */
static struct dmar_drhd_rt dmar_drhd_units[MAX_DRHDS];
/**
 * @brief Flag of IOMMU page walk coherent
 *
 * This flag indicates if hardware access to the root, scalable-mode root, context, scalable-mode-context, scalable-
 * mode PASID-directory, scalable-mode PASID-table, and interrupt-remapping tables, and legacy-mode second-stage paging
 * structures are coherent (snooped) or not.
 */
static bool iommu_page_walk_coherent = true;
/**
 * @brief Pointer to DMAR information structure
 *
 * DMAR information is extracted from ACPI tables, contains DMAR hardware information reported by BIOS.
 */
static struct dmar_info *platform_dmar_info = NULL;

/**
 * @brief Maximum number of domains.
 */
#define MAX_DOMAIN_NUM CONFIG_MAX_VM_NUM

/**
 * @brief Get domain ID from a VM ID
 *
 * Simply returns VM ID as the domain ID.
 *
 * @param[in] vm_id ID of a VM
 *
 * @return Domain ID of the VM
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint16_t vmid_to_domainid(uint16_t vm_id)
{
	return vm_id;
}

static int32_t dmar_register_hrhd(struct dmar_drhd_rt *dmar_unit);
static struct dmar_drhd_rt *device_to_dmaru(uint8_t bus, uint8_t devfun);

/**
 * @brief Register DMAR units information in dmar_drhd_units
 *
 * This function saves all platform DMAR units information in dmar_drhd_units array. Sets units' register
 * page(s) owned by hypervisor. Disables units' DMA remapping. Unsets posted interrupt flag in global platform
 * capability variable if any DMAR unit doesn't support posted interrupt or CPU doesn't support APIC virtualization
 * advanced feature.
 *
 * @return DMAR unit capability checking result
 *
 * @retval -ENODEV DMAR unit doesn't support 2MB/1GB page, or queued invalidation, or interrupt remapping, or
 *                 extended interrupt mode.
 * @retval 0 Otherwise
 *
 * @pre N/A
 *
 * @post N/A
 */
static int32_t register_hrhd_units(void)
{
	struct dmar_drhd_rt *drhd_rt;
	uint32_t i;
	int32_t ret = 0;

	for (i = 0U; i < platform_dmar_info->drhd_count; i++) {
		drhd_rt = &dmar_drhd_units[i];
		drhd_rt->index = i;
		drhd_rt->drhd = &platform_dmar_info->drhd_units[i];
		drhd_rt->dmar_irq = IRQ_INVALID;

		set_paging_supervisor(drhd_rt->drhd->reg_base_addr, PAGE_SIZE);

		ret = dmar_register_hrhd(drhd_rt);
		if (ret != 0) {
			break;
		}

		if ((iommu_cap_pi(drhd_rt->cap) == 0U)) {
			platform_caps.pi = false;
		}
	}

	return ret;
}

/**
 * @brief Reads a 32-bit register value from the given offset of the given DMAR unit
 *
 * @param[in] dmar_unit pointer to the DMAR unit read from
 * @param[in] offset register offset
 *
 * @return 32-bit register value
 *
 * @pre dmar_unit != NULL
 *
 * @post N/A
 */
static uint32_t iommu_read32(const struct dmar_drhd_rt *dmar_unit, uint32_t offset)
{
	return mmio_read32(hpa2hva(dmar_unit->drhd->reg_base_addr + offset));
}

/**
 * @brief Reads a 64-bit register value from the given offset of the given DMAR unit
 *
 * @param[in] dmar_unit pointer to the DMAR unit read from
 * @param[in] offset register offset
 *
 * @return 64-bit register value
 *
 * @pre dmar_unit != NULL
 *
 * @post N/A
 */
static uint64_t iommu_read64(const struct dmar_drhd_rt *dmar_unit, uint32_t offset)
{
	return mmio_read64(hpa2hva(dmar_unit->drhd->reg_base_addr + offset));
}

/**
 * @brief Writes a 32-bit register value to the given offset of the given DMAR unit
 *
 * @param[in] dmar_unit pointer to the DMAR unit written to
 * @param[in] offset register offset
 * @param[in] value value to be written
 *
 * @return None
 *
 * @pre dmar_unit != NULL
 *
 * @post N/A
 */
static void iommu_write32(const struct dmar_drhd_rt *dmar_unit, uint32_t offset, uint32_t value)
{
	mmio_write32(value, hpa2hva(dmar_unit->drhd->reg_base_addr + offset));
}

/**
 * @brief Writes a 64-bit register value to the given offset of the given DMAR unit
 *
 * @param[in] dmar_unit pointer to the DMAR unit written to
 * @param[in] offset register offset
 * @param[in] value value to be written
 *
 * @return None
 *
 * @pre dmar_unit != NULL
 *
 * @post N/A
 */
static void iommu_write64(const struct dmar_drhd_rt *dmar_unit, uint32_t offset, uint64_t value)
{
	mmio_write64(value, hpa2hva(dmar_unit->drhd->reg_base_addr + offset));
}

/**
 * @brief Waits completion of DMAR operation(s) indicated in a given register
 *
 * This function starts a spin-wait loop to check the DMAR register value in
 * the given offset. Exits the loop if the value after applying mask equals
 * pre_condition. Reports timeout fault if waits more than 1 second.
 *
 * @param[in] dmar_unit pointer to the DMAR unit
 * @param[in] offset DMAR register offset
 * @param[in] mask defines the bits in the register to check
 * @param[in] pre_condition expected register bits value to quit loop
 * @param[inout] status pointer to the register  value read from the given offset
 *
 * @return None
 *
 * @pre dmar_unit != NULL
 * @pre offset < PAGE_SIZE
 * @pre status != NULL
 *
 * @post N/A
 */
static inline void dmar_wait_completion(const struct dmar_drhd_rt *dmar_unit, uint32_t offset,
	uint32_t mask, uint32_t pre_condition, uint32_t *status)
{
	/* variable start isn't used when built as release version */
	__unused uint64_t start = cpu_ticks();

	do {
		ASSERT(((cpu_ticks() - start) < (1000 * TICKS_PER_MS)), "DMAR OP Timeout!");
		asm_pause();
		*status = iommu_read32(dmar_unit, offset);
	} while( (*status & mask) == pre_condition);
}

/**
 * @brief Flush cache lines that contains the linear addresses from p to p + size
 *
 * This function flushes CPU cache when root table, context table or second-level translation table is updated.
 * In the context of ACRN, it assumes that the Guest OS would not try to re-program the PCI device's BAR to a different
 * address,GPA to HPA mapping relationship is not changed after VM created, skip flushing IOTLB to avoid performance
 * penalty.
 *
 * If DMAR unit supports page-walk coherency, SW will not do cache flush for the above tables changes.
 *
 * @param[in] p start address of linear addresses whose cache line to be flushed
 * @param[in] size size of the linear addresses whose cache line to be flushed
 *
 * @return None
 *
 * @pre p != NULL
 *
 * @post N/A
 */
void iommu_flush_cache(const void *p, uint32_t size)
{
	/* if vtd support page-walk coherency, no need to flush cacheline */
	if (!iommu_page_walk_coherent) {
		flush_cache_range(p, size);
	}
}

/**
 * @brief Get Required Write-Buffer Flushing (RWBF) bit from the given DMAR capability register.
 *
 * @param[in] cap value of DMAR capability register
 *
 * @return value of RWBF bit
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_cap_rwbf(uint64_t cap)
{
	return ((uint8_t)(cap >> 4U) & 1U);
}

/**
 * @brief Get Snoop Control (SC) bit from the given DMAR extended capability register.
 *
 * @param[in] ecap value of DMAR extended capability register
 *
 * @return value of SC bit
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t iommu_ecap_sc(uint64_t ecap)
{
	return ((uint8_t)(ecap >> 7U) & 1U);
}

/**
 * @brief Print capability information of the given DMAR unit.
 *
 * The information includes DMAR unit registers base address, capabilities setting in the Capability Register and the
 * Extended Capability Register.
 *
 * @param[in] dmar_unit pointer to the given DMAR unit
 *
 * @return None
 *
 * @pre dmar_unit != NULL
 *
 * @post N/A
 */
static void dmar_unit_show_capability(struct dmar_drhd_rt *dmar_unit)
{
	dev_dbg(DBG_LEVEL_IOMMU, "dmar unit[0x%x]", dmar_unit->drhd->reg_base_addr);
	dev_dbg(DBG_LEVEL_IOMMU, "\tNumDomain:%d", iommu_cap_ndoms(dmar_unit->cap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tAdvancedFaultLogging:%d", iommu_cap_afl(dmar_unit->cap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tRequiredWBFlush:%d", iommu_cap_rwbf(dmar_unit->cap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tProtectedLowMemRegion:%d", iommu_cap_plmr(dmar_unit->cap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tProtectedHighMemRegion:%d", iommu_cap_phmr(dmar_unit->cap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tCachingMode:%d", iommu_cap_caching_mode(dmar_unit->cap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tSAGAW:0x%x", iommu_cap_sagaw(dmar_unit->cap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tMGAW:%d", iommu_cap_mgaw(dmar_unit->cap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tZeroLenRead:%d", iommu_cap_zlr(dmar_unit->cap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tLargePageSupport:0x%x", iommu_cap_super_page_val(dmar_unit->cap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tPageSelectiveInvalidation:%d", iommu_cap_pgsel_inv(dmar_unit->cap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tPageSelectInvalidation:%d", iommu_cap_pgsel_inv(dmar_unit->cap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tNumOfFaultRecordingReg:%d", iommu_cap_num_fault_regs(dmar_unit->cap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tMAMV:0x%x", iommu_cap_max_amask_val(dmar_unit->cap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tWriteDraining:%d", iommu_cap_write_drain(dmar_unit->cap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tReadDraining:%d", iommu_cap_read_drain(dmar_unit->cap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tPostInterrupts:%d\n", iommu_cap_pi(dmar_unit->cap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tPage-walk Coherency:%d", iommu_ecap_c(dmar_unit->ecap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tQueuedInvalidation:%d", iommu_ecap_qi(dmar_unit->ecap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tDeviceTLB:%d", iommu_ecap_dt(dmar_unit->ecap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tInterruptRemapping:%d", iommu_ecap_ir(dmar_unit->ecap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tExtendedInterruptMode:%d", iommu_ecap_eim(dmar_unit->ecap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tPassThrough:%d", iommu_ecap_pt(dmar_unit->ecap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tSnoopControl:%d", iommu_ecap_sc(dmar_unit->ecap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tIOTLB RegOffset:0x%x", iommu_ecap_iro(dmar_unit->ecap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tMHMV:0x%x", iommu_ecap_mhmv(dmar_unit->ecap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tMTS:%d", iommu_ecap_mts(dmar_unit->ecap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tNEST:%d", iommu_ecap_nest(dmar_unit->ecap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tPRS:%d", iommu_ecap_prs(dmar_unit->ecap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tERS:%d", iommu_ecap_ers(dmar_unit->ecap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tSRS:%d", iommu_ecap_srs(dmar_unit->ecap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tNWFS:%d", iommu_ecap_nwfs(dmar_unit->ecap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tEAFS:%d", iommu_ecap_eafs(dmar_unit->ecap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tPSS:0x%x", iommu_ecap_pss(dmar_unit->ecap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tPASID:%d", iommu_ecap_pasid(dmar_unit->ecap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tDIT:%d", iommu_ecap_dit(dmar_unit->ecap));
	dev_dbg(DBG_LEVEL_IOMMU, "\tPDS:%d\n", iommu_ecap_pds(dmar_unit->ecap));
}

/**
 * @brief Calculate the number of level of translation page walk
 *
 * @param[in] width width of address to do translation
 *
 * @return the number of translation level
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t width_to_level(uint32_t width)
{
	return (uint8_t)(((width - 12U) + (LEVEL_WIDTH)-1U) / (LEVEL_WIDTH));
}

/**
 * @brief Calculate Adjusted Guest Address Widths from the given address width
 *
 * @param[in] width width of address to do translation
 *
 * @return Adjusted Guest Address Widths
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t width_to_agaw(uint32_t width)
{
	return width_to_level(width) - 2U;
}

/**
 * @brief Get DMAR unit's maximum supported adjusted guest address width
 *
 * @param[in] dmar_unit pointer to DMAR unit
 *
 * @return the highest bit position of Adjusted Guest Address Widths in capability register
 *
 * @pre dmar_unit != NULL
 *
 * @post N/A
 */
static uint8_t dmar_unit_get_msagaw(const struct dmar_drhd_rt *dmar_unit)
{
	uint8_t i;
	uint8_t sgaw = iommu_cap_sagaw(dmar_unit->cap);

	for (i = 5U; i > 0U; ) {
		i--;
		if (((1U << i) & sgaw) != 0U) {
			break;
		}
	}
	return i;
}

/**
 * @brief Check if the given guest address width is supported by the DMAR unit
 *
 * @param[in] dmar_unit pointer to DMAR unit
 * @param[in] addr_width the given guest address width to check
 *
 * @return address width support result
 * @retval true address width supported by the DMAR unit
 * @retval false address width not supported by the DMAR unit
 *
 * @pre dmar_unit != NULL
 *
 * @post N/A
 */
static bool dmar_unit_support_aw(const struct dmar_drhd_rt *dmar_unit, uint32_t addr_width)
{
	uint8_t aw;

	aw = width_to_agaw(addr_width);

	return (((1U << aw) & iommu_cap_sagaw(dmar_unit->cap)) != 0U);
}

/**
 * @brief Enable interrupt remapping on the specified DRHD.
 *
 * This function sets Interrupt Remapping Enable (IRE) bit in the global command register to enable interrupt remapping
 * of the specified DRHD if this feature is not enabled yet. It then waits until command completion indicated in the
 * global status register or timeout occurs. Concurrent modification to the command register is avoided by using
 * spinlock.
 *
 * @param[inout] dmar_unit A pointer to a data structure that stores the runtime information for the specified
 *                         DRHD whose interrupt remapping is to be enabled.
 *
 * @return None
 *
 * @pre dmar_unit != NULL
 *
 * @post N/A
 *
 */
static void dmar_enable_intr_remapping(struct dmar_drhd_rt *dmar_unit)
{
	uint32_t status = 0;

	spinlock_obtain(&(dmar_unit->lock));
	if ((dmar_unit->gcmd & DMA_GCMD_IRE) == 0U) {
		dmar_unit->gcmd |= DMA_GCMD_IRE;
		iommu_write32(dmar_unit, DMAR_GCMD_REG, dmar_unit->gcmd);
		/* 32-bit register */
		dmar_wait_completion(dmar_unit, DMAR_GSTS_REG, DMA_GSTS_IRES, 0U, &status);
		dev_dbg(DBG_LEVEL_IOMMU, "%s: Global Status Register:0x%x", __func__,
			iommu_read32(dmar_unit, DMAR_GSTS_REG));
	}

	spinlock_release(&(dmar_unit->lock));
}

/**
 * @brief Enable DMA remapping on the specified DRHD.
 *
 * This function sets Translation Enable (TE) bit in the global command register to enable DMA remapping of the
 * specified DRHD if this feature is not enabled yet. It then waits until command completion indicated in the
 * global status register or timeout occurs. Concurrent modification to the command register is avoided by using
 * spinlock.
 *
 * @param[inout] dmar_unit A pointer to a data structure that stores the runtime information for the specified
 *                         DMAR unit whose DMA remapping is to be enabled.
 *
 * @return None
 *
 * @pre dmar_unit != NULL
 *
 * @post N/A
 *
 */
static void dmar_enable_translation(struct dmar_drhd_rt *dmar_unit)
{
	uint32_t status = 0;

	spinlock_obtain(&(dmar_unit->lock));
	if ((dmar_unit->gcmd & DMA_GCMD_TE) == 0U) {
		dmar_unit->gcmd |= DMA_GCMD_TE;
		iommu_write32(dmar_unit, DMAR_GCMD_REG, dmar_unit->gcmd);
		/* 32-bit register */
		dmar_wait_completion(dmar_unit, DMAR_GSTS_REG, DMA_GSTS_TES, 0U, &status);
		dev_dbg(DBG_LEVEL_IOMMU, "%s: Global Status Register:0x%x", __func__,
			iommu_read32(dmar_unit, DMAR_GSTS_REG));
	}
	spinlock_release(&(dmar_unit->lock));

	pr_info("%s: gsr:0x%x, TE enabled:%d", __func__, status, (status & DMA_GSTS_TES) ? 1 : 0);
}

/**
 * @brief Disable interrupt remapping on the specified DMAR unit.
 *
 * This function clears Interrupt Remapping Enable (IRE) bit in the global command register to disable interrupt
 * remapping of the specified DRHD if this feature is enabled. It then waits until command completion indicated in
 * the global status register or timeout occurs. Concurrent modification to the command register is avoided by using
 * spinlock.
 *
 * @param[inout] dmar_unit A pointer to a data structure that stores the runtime information for the specified
 *                         DMAR unit whose interrupt remapping is to be disabled.
 *
 * @return None
 *
 * @pre dmar_unit != NULL
 *
 * @post N/A
 *
 */
static void dmar_disable_intr_remapping(struct dmar_drhd_rt *dmar_unit)
{
	uint32_t status;

	spinlock_obtain(&(dmar_unit->lock));
	if ((dmar_unit->gcmd & DMA_GCMD_IRE) != 0U) {
		dmar_unit->gcmd &= ~DMA_GCMD_IRE;
		iommu_write32(dmar_unit, DMAR_GCMD_REG, dmar_unit->gcmd);
		/* 32-bit register */
		dmar_wait_completion(dmar_unit, DMAR_GSTS_REG, DMA_GSTS_IRES, DMA_GSTS_IRES, &status);
		dev_dbg(DBG_LEVEL_IOMMU, "%s: Global Status Register:0x%x", __func__,
			iommu_read32(dmar_unit, DMAR_GSTS_REG));
	}

	spinlock_release(&(dmar_unit->lock));
}

/**
 * @brief Disable DMA remapping on the specified DMAR unit.
 *
 * This function clears Translation Enable (TE) bit in the global command register to disable DMA remapping of the
 * specified DRHD if this feature is enabled. It then waits until command completion indicated in the global status
 * register or timeout occurs. Concurrent modification to the command register is avoided by using spinlock.
 *
 * @param[inout] dmar_unit A pointer to a data structure that stores the runtime information for the specified
 *                         DMAR unit whose DMA remapping is to be disabled.
 *
 * @return None
 *
 * @pre dmar_unit != NULL
 * @pre dmar_unit->drhd != NULL
 *
 * @post N/A
 *
 * @remark N/A
 *
 */
static void dmar_disable_translation(struct dmar_drhd_rt *dmar_unit)
{
	uint32_t status = 0;

	spinlock_obtain(&(dmar_unit->lock));
	if ((dmar_unit->gcmd & DMA_GCMD_TE) != 0U) {
		dmar_unit->gcmd &= ~DMA_GCMD_TE;
		iommu_write32(dmar_unit, DMAR_GCMD_REG, dmar_unit->gcmd);
		/* 32-bit register */
		dmar_wait_completion(dmar_unit, DMAR_GSTS_REG, DMA_GSTS_TES, DMA_GSTS_TES, &status);
		dev_dbg(DBG_LEVEL_IOMMU, "%s: Global Status Register:0x%x", __func__,
			iommu_read32(dmar_unit, DMAR_GSTS_REG));
	}
	spinlock_release(&(dmar_unit->lock));

	pr_info("%s: gsr:0x%x, TE enabled:%d", __func__, status, (status & DMA_GSTS_TES) ? 1 : 0);
}

/**
 * @brief Register the specified DMAR unit.
 *
 * This function saves DMAR unit registers information in DRHD runtime data structure, checks the unit's capabilities
 * and returns immediately with error code -ENODEV on the following cases:
 * - the unit doesn't support 2MB large-page size,
 * - the unit doesn't support 1GB large-page size,
 * - the unit doesn't support Queued Invalidation,
 * - the unit doesn't support Interrupt Remapping,
 * - the unit doesn't support Extended Interrupt Mode.
 *
 * If all of these capabilities are supported, check page-walk coherence bit in the Extended Capabilities Register and
 * save the value.
 *
 * @param[inout] dmar_unit A pointer to a data structure that stores the runtime information for the specified
 *                         DRHD structure which is to be registered.
 *
 * @return registration result
 * @retval 0 Succeeded
 * @retval -ENODEV Failed due to expected capabilities not supported
 *
 * @pre dmar_unit != NULL
 * @pre dmar_unit->drhd != NULL
 *
 * @post N/A
 *
 */
static int32_t dmar_register_hrhd(struct dmar_drhd_rt *dmar_unit)
{
	int32_t ret = 0;

	dev_dbg(DBG_LEVEL_IOMMU, "%s: Register dmar unit [%d] @0x%lx", __func__, dmar_unit->index,
		dmar_unit->drhd->reg_base_addr);

	spinlock_init(&dmar_unit->lock);

	dmar_unit->cap = iommu_read64(dmar_unit, DMAR_CAP_REG);
	dmar_unit->ecap = iommu_read64(dmar_unit, DMAR_ECAP_REG);

	/*
	 * The initialization of "dmar_unit->gcmd" shall be done via reading from Global Status Register rather than
	 * Global Command Register.
	 * According to Chapter 10.4.4 Global Command Register in VT-d spec, Global Command Register is a write-only
	 * register to control remapping hardware. Global Status Register is the corresponding read-only register to
	 * report remapping hardware status.
	 */
	dmar_unit->gcmd = iommu_read32(dmar_unit, DMAR_GSTS_REG) & 0x96FFFFFFU; // Reset the one-shot bits;

	dmar_unit->cap_msagaw = dmar_unit_get_msagaw(dmar_unit);

	dmar_unit->cap_num_fault_regs = iommu_cap_num_fault_regs(dmar_unit->cap);
	dmar_unit->cap_fault_reg_offset = iommu_cap_fault_reg_offset(dmar_unit->cap);
	dmar_unit->ecap_iotlb_offset = iommu_ecap_iro(dmar_unit->ecap) * 16U;
	dmar_unit->root_table_addr = hva2hpa(get_root_table(dmar_unit->index));
	dmar_unit->ir_table_addr = hva2hpa(get_ir_table(dmar_unit->index));

	dev_dbg(DBG_LEVEL_IOMMU, "version:0x%x, cap:0x%lx, ecap:0x%lx",
		iommu_read32(dmar_unit, DMAR_VER_REG), dmar_unit->cap, dmar_unit->ecap);
	dev_dbg(DBG_LEVEL_IOMMU, "sagaw:0x%x, msagaw:0x%x, iotlb offset 0x%x",
		iommu_cap_sagaw(dmar_unit->cap), dmar_unit->cap_msagaw, dmar_unit->ecap_iotlb_offset);

	dmar_unit_show_capability(dmar_unit);

	/* check capability */
	if ((iommu_cap_super_page_val(dmar_unit->cap) & 0x1U) == 0U) {
		pr_fatal("dmar unit doesn't support 2MB page!");
		ret = -ENODEV;
	} else if ((iommu_cap_super_page_val(dmar_unit->cap) & 0x2U) == 0U) {
		pr_fatal("dmar unit doesn't support 1GB page!");
		ret = -ENODEV;
	} else if (iommu_ecap_qi(dmar_unit->ecap) == 0U) {
		pr_fatal("dmar unit doesn't support Queued Invalidation!");
		ret = -ENODEV;
	} else if (iommu_ecap_ir(dmar_unit->ecap) == 0U) {
		pr_fatal("dmar unit doesn't support Interrupt Remapping!");
		ret = -ENODEV;
	} else if (iommu_ecap_eim(dmar_unit->ecap) == 0U) {
		pr_fatal("dmar unit doesn't support Extended Interrupt Mode!");
		ret = -ENODEV;
	} else {
		if (iommu_ecap_c(dmar_unit->ecap) == 0U) {
			iommu_page_walk_coherent = false;
		}
	}

	return ret;
}

/**
 * @brief Find DMAR unit that the device with the specified IOAPIC ID is under the scope of.
 *
 * This function goes through all DMAR units in the platform. For every IOAPIC type of device under the scope of these
 * remapping hardwares, compare their IOAPIC ID with the specified ID. For the matched one, fill PCI ID information in
 * the given sid union and return its DMAR unit.
 *
 * If no matched devices, return NULL.
 *
 * @param[in] ioapic_id Id of the IOAPIC device to look for its DMAR unit
 * @param[out] sid pointer to the device's PCI ID info
 *
 * @return pointer to the DMAR unit runtime data structure or NULL
 * @retval NULL No DMAR unit found
 * @retval Others valid pointer to the DMAR unit runtime data structure
 *
 * @pre sid != NULL
 *
 * @post N/A
 *
 */
static struct dmar_drhd_rt *ioapic_to_dmaru(uint16_t ioapic_id, union pci_bdf *sid)
{
	struct dmar_drhd_rt *dmar_unit = NULL;
	uint32_t i, j;
	bool found = false;

	for (j = 0U; j < platform_dmar_info->drhd_count; j++) {
		dmar_unit = &dmar_drhd_units[j];
		for (i = 0U; i < dmar_unit->drhd->dev_cnt; i++) {
			if ((dmar_unit->drhd->devices[i].type == ACPI_DMAR_SCOPE_TYPE_IOAPIC) &&
					((uint16_t)dmar_unit->drhd->devices[i].id == ioapic_id)) {
				sid->fields.devfun = dmar_unit->drhd->devices[i].devfun;
				sid->fields.bus = dmar_unit->drhd->devices[i].bus;
				found = true;
				break;
			}
		}
		if (found) {
			break;
		}
	}

	if (j == platform_dmar_info->drhd_count) {
		dmar_unit = NULL;
		pr_err("IOAPIC device with id 0x%x has no IOMMU", ioapic_id);
	}

	return dmar_unit;
}

/**
 * @brief Find DMAR unit that the device with the specified PCI ID is under the scope of.
 *
 * @param[in] bus Bus ID of the PCI device, which is to be searched for its corresponding DMAR unit.
 * @param[in] devfun Device and function IDs of the PCI device, which is to be searched for its corresponding DMAR unit.
 *
 * @return pointer to the DMAR unit runtime data structure or NULL
 * @retval NULL No DMAR unit found
 * @retval Others valid pointer to the DMAR unit runtime data structure
 *
 * @post N/A
 *
 */
static struct dmar_drhd_rt *device_to_dmaru(uint8_t bus, uint8_t devfun)
{
	struct dmar_drhd_rt *dmaru = NULL;
	uint16_t bdf = ((uint16_t)bus << 8U) | devfun;
	uint32_t index = pci_lookup_drhd_for_pbdf(bdf);

	if (index == INVALID_DRHD_INDEX) {
		pr_err("BDF %02x:%02x:%x has no IOMMU", bus, devfun >> 3U, devfun & 7U);
		/*
		 * pci_lookup_drhd_for_pbdf would return -1U for any of the reasons
		 * 1) PCI device with bus, devfun does not exist on platform
		 * 2) ACRN had issues finding the device with bus, devfun during init
		 * 3) DMAR tables provided by ACPI for this platform are incorrect
		 */
	} else {
		dmaru = &dmar_drhd_units[index];
	}

	return dmaru;
}

/**
 * @brief Submit the specified invalidation request to the specified DMAR unit and wait until hardware completes it.
 *
 * The submission is done by writing the Invalidation Descriptor plus a Wait Invalidation Descriptor to the Invalidation
 * Queue. The Invalidation Wait Descriptor (inv_wait_dsc) descriptor allows software to synchronize with hardware for
 * the invalidation request descriptors submitted before the wait descriptor. Then it waits the invalidation wait
 * descriptor completion by check the Status Address and Data set by inv_wait_dsc or until a 1-second timeout occurs.
 *
 * @param[inout] dmar_unit A pointer to a data structure that stores the runtime information for the specified
 *                         DMAR unit where the invalidation request is submitted to.
 * @param[in] invalidate_desc The specified invalidation descriptor.
 *
 * @return None
 *
 * @pre dmar_unit != NULL
 * @pre dmar_unit->drhd != NULL
 *
 * @post N/A
 *
 */
static void dmar_issue_qi_request(struct dmar_drhd_rt *dmar_unit, struct dmar_entry invalidate_desc)
{
	struct dmar_entry *invalidate_desc_ptr;
	uint64_t start;

	spinlock_obtain(&(dmar_unit->lock));

	invalidate_desc_ptr = (struct dmar_entry *)(dmar_unit->qi_queue + dmar_unit->qi_tail);

	invalidate_desc_ptr->hi_64 = invalidate_desc.hi_64;
	invalidate_desc_ptr->lo_64 = invalidate_desc.lo_64;
	dmar_unit->qi_tail = (dmar_unit->qi_tail + DMAR_QI_INV_ENTRY_SIZE) % DMAR_INVALIDATION_QUEUE_SIZE;

	invalidate_desc_ptr++;

	/* FIXME: pls use local variables instead, but solve the stack memory
	 * being overwritten issue
	 */
	invalidate_desc_ptr->hi_64 = hva2hpa(&dmar_unit->desc_status);
	invalidate_desc_ptr->lo_64 = DMAR_INV_WAIT_DESC_LOWER;
	dmar_unit->qi_tail = (dmar_unit->qi_tail + DMAR_QI_INV_ENTRY_SIZE) % DMAR_INVALIDATION_QUEUE_SIZE;

	dmar_unit->desc_status = DMAR_INV_STATUS_INCOMPLETE;
	iommu_write32(dmar_unit, DMAR_IQT_REG, dmar_unit->qi_tail);

	start = cpu_ticks();
	while (dmar_unit->desc_status != DMAR_INV_STATUS_COMPLETED) {
		if ((cpu_ticks() - start) > (1000 * TICKS_PER_MS)) {
			pr_err("DMAR Queued Invalidation OP Timeout!");
			break;
		}
		asm_pause();
	}

	spinlock_release(&(dmar_unit->lock));
}

/**
 * @brief Construct Context-cache Invalidate Descriptor from the specified data and submit the invalidation request to
 * the specified DMAR unit.
 *
 * Construct Context-cache Invalidate Descriptor from the given Domain ID, Source ID, Function Mask and Granularity
 * data, refer to 6.5.2.1 Context-cache Invalidate Descriptor, VT-d specification for details. Then submit non-zero
 * descriptor to the specified DMAR unit's Queued Invalidation Interface via API dmar_issue_qi_request.
 *
 * @param[inout] dmar_unit A pointer to a data structure that stores the runtime information for the specified
 *                         DMAR unit where the invalidation request is submitted to.
 * @param[in] did The specified value for Domain ID field, which is to be set to a context-cache invalidate descriptor.
 * @param[in] sid The specified value for Source ID field, which is to be set to a context-cache invalidate descriptor.
 * @param[in] fm The specified value for Function Mask field, which is to be set to a context-cache invalidate
 *               descriptor.
 * @param[in] cirg The specified value for Context Invalidation Request Granularity field, which is to be set to a
 *                 context-cache invalidate descriptor.
 *
 * @return None
 *
 * @pre dmar_unit != NULL
 * @pre dmar_unit->drhd != NULL
 * @pre (cirg == DMAR_CIRG_GLOBAL) || (cirg == DMAR_CIRG_DEVICE)
 *
 * @post N/A
 *
 */
static void dmar_invalid_context_cache(struct dmar_drhd_rt *dmar_unit,
	uint16_t did, uint16_t sid, uint8_t fm, enum dmar_cirg_type cirg)
{
	struct dmar_entry invalidate_desc;

	invalidate_desc.hi_64 = 0UL;
	invalidate_desc.lo_64 = DMAR_INV_CONTEXT_CACHE_DESC;
	switch (cirg) {
	case DMAR_CIRG_GLOBAL:
		invalidate_desc.lo_64 |= DMA_CONTEXT_GLOBAL_INVL;
		break;
	case DMAR_CIRG_DOMAIN:
		invalidate_desc.lo_64 |= DMA_CONTEXT_DOMAIN_INVL | dma_ccid_did(did);
		break;
	case DMAR_CIRG_DEVICE:
		invalidate_desc.lo_64 |= DMA_CONTEXT_DEVICE_INVL | dma_ccid_did(did) | dma_ccid_sid(sid) |
					dma_ccid_fm(fm);
		break;
	default:
		invalidate_desc.lo_64 = 0UL;
		pr_err("unknown Granularity value 0x%x for context-cache invalidation descriptor.", cirg);
		break;
	}

	if (invalidate_desc.lo_64 != 0UL) {
		dmar_issue_qi_request(dmar_unit, invalidate_desc);
	}
}

/**
 * @brief Submit a global context-cache invalidation request to the specified DMAR unit.
 *
 * The submission is done via calling API dmar_invalid_context_cache. All context-cache entries cached at the remapping
 * hardware will be invalidated with this invalidation request.
 *
 * It is supposed to be called when hypervisor attempts to invalidate the context cache globally.
 *
 * @param[inout] dmar_unit A pointer to a data structure that stores the runtime information for the specified
 *                         DMAR unit where the invalidation request is submitted to.
 *
 * @return None
 *
 * @pre dmar_unit != NULL
 * @pre dmar_unit->drhd != NULL
 *
 * @post N/A
 */
static void dmar_invalid_context_cache_global(struct dmar_drhd_rt *dmar_unit)
{
	dmar_invalid_context_cache(dmar_unit, 0U, 0U, 0U, DMAR_CIRG_GLOBAL);
}

/**
 * @brief Construct IOTLB Invalidate Descriptor from the specified data and submit the invalidation request to the
 * specified DMAR unit.
 *
 * Construct IOTLB Invalidate Descriptor from the given Domain ID, Address, Address Mask, Hint and Granularity data,
 * refer to 6.5.2.3 IOTLB Invalidate Descriptor, VT-d specification for details. Then submit non-zero descriptor to
 * the specified DMAR unit's Queued Invalidation Interface via API dmar_issue_qi_request.
 *
 * It is supposed to be called when hypervisor attempts to invalidate the IOTLB and paging structure caches.
 *
 * @param[inout] dmar_unit A pointer to a data structure that stores the runtime information for the specified
 *                         DMAR unit  where the invalidation request is submitted to.
 * @param[in] did The specified value for Domain ID field, which is to be set to an IOTLB invalidate descriptor.
 * @param[in] address The specified value for Address field, which is to be set to an IOTLB invalidate descriptor.
 *                    This argument is only needed for page-selective-within-domain invalidation, which is not
 *                    supported in current scope, keeping it here for scope extension in the future.
 * @param[in] am The specified value for Address Mask field, which is to be set to an IOTLB invalidate descriptor.
 *               This argument is only needed for page-selective-within-domain invalidation, which is not
 *               supported in current scope, keeping it here for scope extension in the future.
 * @param[in] hint The specified value for Invalidation Hint field, which is to be set to an IOTLB invalidate
 *                 descriptor. This argument is only needed for page-selective-within-domain invalidation, which
 *                 is not supported in current scope, keeping it here for scope extension in the future.
 * @param[in] iirg The specified value for IOTLB Invalidation Request Granularity field, which is to be set to an IOTLB
 *                 invalidate descriptor.
 *
 * @return None
 *
 * @pre dmar_unit != NULL
 * @pre dmar_unit->drhd != NULL
 * @pre (iirg == DMAR_IIRG_GLOBAL) || (iirg == DMAR_IIRG_DOMAIN)
 *
 * @post N/A
 *
 */
static void dmar_invalid_iotlb(struct dmar_drhd_rt *dmar_unit, uint16_t did, uint64_t address, uint8_t am,
			       bool hint, enum dmar_iirg_type iirg)
{
	/* set Drain Reads & Drain Writes,
	 * if hardware doesn't support it, will be ignored by hardware
	 */
	struct dmar_entry invalidate_desc;
	uint64_t addr = 0UL;

	invalidate_desc.hi_64 = 0UL;

	invalidate_desc.lo_64 = DMA_IOTLB_DR | DMA_IOTLB_DW | DMAR_INV_IOTLB_DESC;

	switch (iirg) {
	case DMAR_IIRG_GLOBAL:
		invalidate_desc.lo_64 |= DMA_IOTLB_GLOBAL_INVL;
		break;
	case DMAR_IIRG_DOMAIN:
		invalidate_desc.lo_64 |= DMA_IOTLB_DOMAIN_INVL | dma_iotlb_did(did);
		break;
	case DMAR_IIRG_PAGE:
		invalidate_desc.lo_64 |= DMA_IOTLB_PAGE_INVL | dma_iotlb_did(did);
		addr = address | dma_iotlb_invl_addr_am(am);
		if (hint) {
			addr |= DMA_IOTLB_INVL_ADDR_IH_UNMODIFIED;
		}
		invalidate_desc.hi_64 |= addr;
		break;
	default:
		invalidate_desc.lo_64 = 0UL;
		pr_err("unknown Granularity value 0x%x for IOTLB invalidation descriptor.", iirg);
		break;
	}

	if (invalidate_desc.lo_64 != 0UL) {
		dmar_issue_qi_request(dmar_unit, invalidate_desc);
	}
}

/**
 * @brief Submit a global IOTLB invalidation request to the specified DMAR unit.
 *
 * The submission is done via calling API dmar_invalid_iotlb. All IOTLB entries will be invalidated and all
 * paging-structure-cache entries will be invalidated with this invalidation request.
 *
 * It is supposed to be called when hypervisor attempts to invalidate the IOTLB and paging structure caches globally.
 *
 * @param[inout] dmar_unit A pointer to a data structure that stores the runtime information for the specified
 *                         DMAR unit where the invalidation request is submitted to.
 *
 * @return None
 *
 * @pre dmar_unit != NULL
 * @pre dmar_unit->drhd != NULL
 *
 * @post N/A
 *
 */
static void dmar_invalid_iotlb_global(struct dmar_drhd_rt *dmar_unit)
{
	dmar_invalid_iotlb(dmar_unit, 0U, 0UL, 0U, false, DMAR_IIRG_GLOBAL);
}

/**
 * @brief Set up the Interrupt Remapping Table for the specified DMAR unit.
 *
 * This function performs the following actions.
 * - gets interrupt remapping table address from the input DMAR unit structure,
 * - writes the address to the Interrupt Remapping Table Address register,
 * - sets the "Set Interrupt Remap Table Pointer" field in the Global Command register to set/update the table pointer
 *   used by hardware,
 * - waits for the hardware completion of the table pointer update operation or until a 1-second timeout occurs.
 *
 * It is one step of DMAR preparation before enabling DMAR functions.
 *
 * @param[inout] dmar_unit A pointer to a data structure that stores the runtime information for the specified
 *                         DMAR unit whose interrupt remapping table is to be set up.
 *
 * @return None
 *
 * @pre dmar_unit != NULL
 * @pre dmar_unit->drhd != NULL
 *
 * @post N/A
 *
 */
static void dmar_set_intr_remap_table(struct dmar_drhd_rt *dmar_unit)
{
	uint64_t address;
	uint32_t status;
	uint8_t size;

	spinlock_obtain(&(dmar_unit->lock));

	/* Set number of bits needed to represent the entries minus 1 */
	size = (uint8_t) fls32(MAX_IR_ENTRIES) - 1U;
	address = dmar_unit->ir_table_addr | DMAR_IR_ENABLE_EIM | size;

	iommu_write64(dmar_unit, DMAR_IRTA_REG, address);

	iommu_write32(dmar_unit, DMAR_GCMD_REG, dmar_unit->gcmd | DMA_GCMD_SIRTP);

	dmar_wait_completion(dmar_unit, DMAR_GSTS_REG, DMA_GSTS_IRTPS, 0U, &status);

	spinlock_release(&(dmar_unit->lock));
}

/**
 * @brief Construct Interrupt Entry Cache Invalidate Descriptor from the specified data and submit the invalidation
 * request to the specified DMAR unit.
 *
 * Construct Interrupt Entry Cache Invalidate Descriptor from the given interrupt index, index mask, and global flag
 * data, refer to 6.5.2.7 Interrupt Entry Cache Invalidate Descriptor, VT-d specification for details. Then submit the
 * descriptor to the specified DMAR unit's Queued Invalidation Interface via API dmar_issue_qi_request.
 *
 * It is supposed to be called when hypervisor attempts to invalidate the interrupt entry cache.
 *
 * @param[inout] dmar_unit A pointer to a data structure that stores the runtime information for the specified
 *                         DRHD structure where the invalidation request is submitted to.
 * @param[in] intr_index The specified value for Interrupt Index field, which is to be set to an interrupt entry
 *                       cache invalidate descriptor.
 * @param[in] index_mask The specified value for Index Mask field, which is to be set to an interrupt entry cache
 *                       invalidate descriptor.
 * @param[in] is_global A flag to indicate whether the specified invalidation request is global or not.
 *                      It is true if the specified invalidation request is global, otherwise, it is false.
 *
 * @return None
 *
 * @pre dmar_unit != NULL
 * @pre dmar_unit->drhd != NULL
 *
 * @post N/A
 *
 */
static void dmar_invalid_iec(struct dmar_drhd_rt *dmar_unit, uint16_t intr_index,
				uint8_t index_mask, bool is_global)
{
	struct dmar_entry invalidate_desc;

	invalidate_desc.hi_64 = 0UL;
	invalidate_desc.lo_64 = DMAR_INV_IEC_DESC;

	if (is_global) {
		invalidate_desc.lo_64 |= DMAR_IEC_GLOBAL_INVL;
	} else {
		invalidate_desc.lo_64 |= DMAR_IECI_INDEXED | dma_iec_index(intr_index, index_mask);
	}

	dmar_issue_qi_request(dmar_unit, invalidate_desc);
}

/**
 * @brief Submit a global interrupt entry cache invalidation request to the specified DMAR unit.
 *
 * The submission is done via calling API dmar_invalid_iec. All interrupt remapping entries cached at the remapping
 * hardware will be invalidated with this invalidation request.
 *
 * It is supposed to be called when hypervisor attempts to invalidate the interrupt entry cache globally.
 *
 * @param[inout] dmar_unit A pointer to a data structure that stores the runtime information for the specified
 *                         DMAR unit where the invalidation request is submitted to.
 *
 * @return None
 *
 * @pre dmar_unit != NULL
 * @pre dmar_unit->drhd != NULL
 *
 * @post N/A
 *
 */
static void dmar_invalid_iec_global(struct dmar_drhd_rt *dmar_unit)
{
	dmar_invalid_iec(dmar_unit, 0U, 0U, true);
}

/**
 * @brief Set up the root table for the specified DMAR unit.
 *
 * This function writes root table address to Root Table Address register. And then sets Set Root Table Pointer field
 * in the Global Command register to set/update the root table pointer used by hardware. It waits for the hardware
 * completion of the table pointer update operation or until a 1-second timeout occurs.
 *
 * It is one step of DMAR preparation before enabling DMAR functions.
 *
 * @param[inout] dmar_unit A pointer to a data structure that stores the runtime information for the specified
 *                         DMAR unit whose root table is to be set up.
 *
 * @return None
 *
 * @pre dmar_unit != NULL
 * @pre dmar_unit->drhd != NULL
 *
 * @post N/A
 *
 */
static void dmar_set_root_table(struct dmar_drhd_rt *dmar_unit)
{
	uint32_t status;

	spinlock_obtain(&(dmar_unit->lock));
	iommu_write64(dmar_unit, DMAR_RTADDR_REG, dmar_unit->root_table_addr);

	iommu_write32(dmar_unit, DMAR_GCMD_REG, dmar_unit->gcmd | DMA_GCMD_SRTP);

	/* 32-bit register */
	dmar_wait_completion(dmar_unit, DMAR_GSTS_REG, DMA_GSTS_RTPS, 0U, &status);
	spinlock_release(&(dmar_unit->lock));
}

/**
 * @brief Mask fault event interrupt for the specified DMAR unit.
 *
 * @param[inout] dmar_unit A pointer to a data structure that stores the runtime information for the specified
 *                         DMAR unit whose fault event control register is to be set up.
 *
 * @return None
 *
 * @pre dmar_unit != NULL
 * @pre dmar_unit->drhd != NULL
 *
 * @post N/A
 *
 */
static void dmar_fault_event_mask(struct dmar_drhd_rt *dmar_unit)
{
	spinlock_obtain(&(dmar_unit->lock));
	iommu_write32(dmar_unit, DMAR_FECTL_REG, DMA_FECTL_IM);
	spinlock_release(&(dmar_unit->lock));
}

/**
 * @brief Unmask fault event interrupt for the specified DMAR unit.
 *
 * @param[inout] dmar_unit A pointer to a data structure that stores the runtime information for the specified
 *                         DMAR unit whose fault event control register is to be set up.
 *
 * @return None
 *
 * @pre dmar_unit != NULL
 * @pre dmar_unit->drhd != NULL
 *
 * @post N/A
 *
 */
static void dmar_fault_event_unmask(struct dmar_drhd_rt *dmar_unit)
{
	spinlock_obtain(&(dmar_unit->lock));
	iommu_write32(dmar_unit, DMAR_FECTL_REG, 0U);
	spinlock_release(&(dmar_unit->lock));
}

/**
 * @brief Program Fault Event Interrupt for the specified DMAR unit.
 *
 * This function programs Fault Event Interrupt by writing Fault Event Data and Address registers.
 *
 * @param[inout] dmar_unit A pointer to a data structure that stores the runtime information for the specified DMAR unit
 * 			   whose fault event data and address registers are to be set up.
 * @param[in] vector vector of the interrupt
 *
 * @return None
 *
 * @pre dmar_unit != NULL
 * @pre dmar_unit->drhd != NULL
 *
 * @post N/A
 *
 */
static void dmar_fault_msi_write(struct dmar_drhd_rt *dmar_unit,
			uint32_t vector)
{
	uint32_t data;
	uint32_t addr_low;
	uint32_t lapic_id = get_cur_lapic_id();

	data = DMAR_MSI_DELIVERY_LOWPRI | vector;
	/* redirection hint: 0
	 * destination mode: 0
	 */
	addr_low = 0xFEE00000U | ((uint32_t)(lapic_id) << 12U);

	spinlock_obtain(&(dmar_unit->lock));
	iommu_write32(dmar_unit, DMAR_FEDATA_REG, data);
	iommu_write32(dmar_unit, DMAR_FEADDR_REG, addr_low);
	spinlock_release(&(dmar_unit->lock));
}

/**
 * @brief Print the fault recording information from the given fault recording register value.
 *
 * @param[in] dmar_unit pointer to the DMAR runtime information structure 
 * @param[in] low low 64-bit of the fault recording register value
 * @param[in] high high 64-bit of the fault recording register value
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 *
 */
static void fault_record_analysis(struct dmar_drhd_rt *dmar_unit, __unused uint64_t low, uint64_t high)
{
	union pci_bdf dmar_bdf;

	if (!dma_frcd_up_f(high)) {
		dmar_bdf.value = dma_frcd_up_sid(high);
		/* currently skip PASID related parsing */
		pr_info("DMAR fault record: %s, Reason: 0x%x, SID: %x.%x.%x @0x%lx",
			(dma_frcd_up_t(high) != 0U) ? "Read/Atomic" : "Write", dma_frcd_up_fr(high),
			dmar_bdf.bits.b, dmar_bdf.bits.d, dmar_bdf.bits.f, low);
		if (iommu_ecap_dt(dmar_unit->ecap) != 0U) {
			pr_info("Address Type: 0x%x", dma_frcd_up_at(high));
		}
	}
}

/**
 * @brief Handle the DMAR fault interrupt.
 *
 * This function checks Primary Pending Fault bit (PPF) in the Fault Status Register. Print every pending fault event's
 * recorded information from the Fault Recording Registers and then clear the registers, until PPF is 0 indicating no
 * pending faults.
 *
 * @param[in] irq interrupt number
 * @param[inout] data pointer to the DMAR runtime information structure
 *
 * @return None
 *
 * @pre data !=NULL
 * @pre data->drhd !=NULL
 *
 * @post N/A
 *
 */
static void dmar_fault_handler(uint32_t irq, void *data)
{
	struct dmar_drhd_rt *dmar_unit = (struct dmar_drhd_rt *)data;
	uint32_t fsr;
	uint16_t index;
	uint32_t record_reg_offset;
	struct dmar_entry fault_record;
	int32_t loop = 0;

	dev_dbg(DBG_LEVEL_IOMMU, "%s: irq = %d", __func__, irq);

	fsr = iommu_read32(dmar_unit, DMAR_FSTS_REG);
	pr_info("Fault Status Register: 0x%x", fsr);

	while (dma_fsts_ppf(fsr)) {
		loop++;
		index = dma_fsts_fri(fsr);
		record_reg_offset = (uint32_t)dmar_unit->cap_fault_reg_offset + (index * 16U);
		if (index >= dmar_unit->cap_num_fault_regs) {
			pr_fatal("Invalid FR Index 0x%x in Fault Status Register", index);
			break;
		}

		/* read 128-bit fault recording register */
		fault_record.lo_64 = iommu_read64(dmar_unit, record_reg_offset);
		fault_record.hi_64 = iommu_read64(dmar_unit, record_reg_offset + 8U);
		fault_record_analysis(dmar_unit, fault_record.lo_64, fault_record.hi_64);

		/* write to clear */
		iommu_write64(dmar_unit, record_reg_offset, fault_record.lo_64);
		iommu_write64(dmar_unit, record_reg_offset + 8U, fault_record.hi_64);

		fsr = iommu_read32(dmar_unit, DMAR_FSTS_REG);
		pr_info("Fault Status Register: 0x%x", fsr);
	}
}

/**
 * @brief Set up fault interrupt for the specified DMAR unit.
 *
 * Request interrupt number for the fault interrupt. Set up fault event registers to enable the interrupt.
 *
 * @param[inout] dmar_unit pointer to the DMAR runtime information structure
 *
 * @return None
 *
 * @pre dmar_unit !=NULL
 * @pre dmar_unit->drhd !=NULL
 *
 * @post N/A
 *
 */
static void dmar_setup_interrupt(struct dmar_drhd_rt *dmar_unit)
{
	uint32_t vector;
	int32_t retval = 0;

	spinlock_obtain(&(dmar_unit->lock));
	if (dmar_unit->dmar_irq == IRQ_INVALID) {
		retval = request_irq(IRQ_INVALID, dmar_fault_handler, dmar_unit, IRQF_NONE);
		dmar_unit->dmar_irq = (uint32_t)retval;
	}
	spinlock_release(&(dmar_unit->lock));
	/* the panic will only happen before any VM starts running */
	if (retval < 0) {
		panic("dmar[%d] fail to setup interrupt", dmar_unit->index);
	}

	vector = irq_to_vector(dmar_unit->dmar_irq);
	dev_dbg(DBG_LEVEL_IOMMU, "irq#%d vector#%d for dmar_unit", dmar_unit->dmar_irq, vector);

	dmar_fault_msi_write(dmar_unit, vector);
	dmar_fault_event_unmask(dmar_unit);
}

/**
 * @brief Enable queued invalidation for the specified DMAR unit.
 *
 * This function sets up Invalidation Queue Address and Tail registers. Sets Queued Invalidation Enable field in Global
 * Command register to enable queued invalidations of the specified DMAR unit. It waits for hardware completion of the
 * enable operation until a 1-second timeout occurs. Concurrent modification to the global command register is avoided
 * by using spinlock.
 *
 * @param[inout] dmar_unit A pointer to a data structure that stores the runtime information for the specified
 *                         DMAR unit whose queued invalidation is to be enabled.
 *
 * @return None
 *
 * @pre dmar_unit != NULL
 * @pre dmar_unit->drhd != NULL
 *
 * @post N/A
 *
 */
static void dmar_enable_qi(struct dmar_drhd_rt *dmar_unit)
{
	uint32_t status = 0;

	spinlock_obtain(&(dmar_unit->lock));

	dmar_unit->qi_queue = hva2hpa(get_qi_queue(dmar_unit->index));
	iommu_write64(dmar_unit, DMAR_IQA_REG, dmar_unit->qi_queue);

	iommu_write32(dmar_unit, DMAR_IQT_REG, 0U);

	if ((dmar_unit->gcmd & DMA_GCMD_QIE) == 0U) {
		dmar_unit->gcmd |= DMA_GCMD_QIE;
		iommu_write32(dmar_unit, DMAR_GCMD_REG,	dmar_unit->gcmd);
		dmar_wait_completion(dmar_unit, DMAR_GSTS_REG, DMA_GSTS_QIES, 0U, &status);
	}

	spinlock_release(&(dmar_unit->lock));
}

/**
 * @brief Disable queued invalidation for the specified DMAR unit.
 *
 * This function clears Queued Invalidation Enable field to disable queued invalidations of the specified DMAR unit.
 * It waits for hardware completion of the disable operation until a 1-second timeout occurs. Concurrent modification
 * to the global command register is avoided by using spinlock.
 *
 * @param[inout] dmar_unit A pointer to a data structure that stores the runtime information for the specified
 *                         DMAR unit whose queued invalidation is to be disabled.
 *
 * @return None
 *
 * @pre dmar_unit != NULL
 * @pre dmar_unit->drhd != NULL
 *
 * @post N/A
 *
 */
static void dmar_disable_qi(struct dmar_drhd_rt *dmar_unit)
{
	uint32_t status = 0;

	spinlock_obtain(&(dmar_unit->lock));

	if ((dmar_unit->gcmd & DMA_GCMD_QIE) == DMA_GCMD_QIE) {
		dmar_unit->gcmd &= ~DMA_GCMD_QIE;
		iommu_write32(dmar_unit, DMAR_GCMD_REG,	dmar_unit->gcmd);
		dmar_wait_completion(dmar_unit, DMAR_GSTS_REG, DMA_GSTS_QIES, DMA_GSTS_QIES, &status);
	}

	spinlock_release(&(dmar_unit->lock));
}

/**
 * @brief Set up DMAR unit hardware for the specified DMAR unit.
 *
 * This function does hardware preparation for the specified DMAR unit before enabling its functions, including
 * the root table set-up, the interrupt remapping table set-up, and queued invalidation enabling.
 *
 * @param[inout] dmar_unit A pointer to a data structure that stores the runtime information for the specified
 *                         DMAR unit whose resources are to be prepared.
 *
 * @return None
 *
 * @pre dmar_unit != NULL
 * @pre dmar_unit->drhd != NULL
 *
 * @post N/A
 */
static void prepare_dmar(struct dmar_drhd_rt *dmar_unit)
{
	dev_dbg(DBG_LEVEL_IOMMU, "enable dmar unit [0x%lx]", dmar_unit->drhd->reg_base_addr);
	dmar_setup_interrupt(dmar_unit);
	dmar_set_root_table(dmar_unit);
	dmar_enable_qi(dmar_unit);
	dmar_set_intr_remap_table(dmar_unit);
}

/**
 * @brief Enable DMAR unit functions.
 *
 * This function invalidates cached entries globally including:
 * - all context-cache entries,
 * - all IOTLB entries and all paging-structure-cache entries,
 * - all interrupt remapping entries.
 *
 * Then enable DMA remapping and interrupt remapping functions of the DMAR unit.
 *
 * @param[inout] dmar_unit A pointer to a data structure that stores the runtime information for the specified
 *                         DMAR unit whose DMA remapping is to be enabled.
 *
 * @return None
 *
 * @pre dmar_unit != NULL
 * @pre dmar_unit->drhd != NULL
 *
 * @post N/A
 *
 */
static void enable_dmar(struct dmar_drhd_rt *dmar_unit)
{
	dev_dbg(DBG_LEVEL_IOMMU, "enable dmar unit [0x%lx]", dmar_unit->drhd->reg_base_addr);
	dmar_invalid_context_cache_global(dmar_unit);
	dmar_invalid_iotlb_global(dmar_unit);
	dmar_invalid_iec_global(dmar_unit);
	dmar_enable_translation(dmar_unit);
	dmar_enable_intr_remapping(dmar_unit);
}

/**
 * @brief Disable functions of the specified DMAR unit.
 *
 * Disable queued invalidation, DMA remapping, fault event interrupt reporting, and interrupt remapping functions of
 * the specified DMAR unit.
 *
 * @param[inout] dmar_unit A pointer to a data structure that stores the runtime information for the specified
 *                         DMAR unit whose functions are to be disabled.
 *
 * @return None
 *
 * @pre dmar_unit != NULL
 * @pre dmar_unit->drhd != NULL
 *
 * @post N/A
 *
 */
static void disable_dmar(struct dmar_drhd_rt *dmar_unit)
{
	dmar_disable_qi(dmar_unit);
	dmar_disable_translation(dmar_unit);
	dmar_fault_event_mask(dmar_unit);
	dmar_disable_intr_remapping(dmar_unit);
}

/**
 * @brief Suspend the specified DMAR unit.
 *
 * This function invalidates context cache, IOTLB and interrupt entry cache. Disables DMAR functions. Then saves fault
 * registers states.
 *
 * @param[inout] dmar_unit A pointer to a data structure that stores the runtime information for the specified
 *                         DMAR unit to be suspended.
 *
 * @return None
 *
 * @pre dmar_unit != NULL
 * @pre dmar_unit->drhd != NULL
 *
 * @post N/A
 *
 */
static void suspend_dmar(struct dmar_drhd_rt *dmar_unit)
{
	uint32_t i;

	dmar_invalid_context_cache_global(dmar_unit);
	dmar_invalid_iotlb_global(dmar_unit);
	dmar_invalid_iec_global(dmar_unit);

	disable_dmar(dmar_unit);

	/* save IOMMU fault register state */
	for (i = 0U; i < IOMMU_FAULT_REGISTER_STATE_NUM; i++) {
		dmar_unit->fault_state[i] =  iommu_read32(dmar_unit, DMAR_FECTL_REG + (i * IOMMU_FAULT_REGISTER_SIZE));
	}
}

/**
 * @brief Resume the specified DMAR unit.
 *
 * This function sets up DMAR unit hardware, enables DMAR functions, and then restores the fault event registers.
 *
 * @param[inout] dmar_unit A pointer to a data structure that stores the runtime information for the specified
 *                         DMAR unit to be resumed.
 *
 * @return None
 *
 * @pre dmar_unit != NULL
 * @pre dmar_unit->drhd != NULL
 *
 * @post N/A
 *
 */
static void resume_dmar(struct dmar_drhd_rt *dmar_unit)
{
	uint32_t i;

	prepare_dmar(dmar_unit);
	enable_dmar(dmar_unit);
	/* restore IOMMU fault register state */
	for (i = 0U; i < IOMMU_FAULT_REGISTER_STATE_NUM; i++) {
		iommu_write32(dmar_unit, DMAR_FECTL_REG + (i * IOMMU_FAULT_REGISTER_SIZE), dmar_unit->fault_state[i]);
	}
}

/**
 * @brief Attach a device to its DMAR unit by setting up the DMA remapping tables for the device.
 *
 * Find device's DMAR unit. If the device's DMAR unit is not present or the DMAR unit doesn't support the device
 * domain's address width, do nothing and return error code -EINVAL. Otherwise, do the following actions:
 * - get device's root tabel entry indexed by device's bus number,
 * - if the entry is not present, create context table, set context table address in the entry, and flush cache lines
 *   to the entry,
 * - if the entry is present, get context table address from the entry,
 * - get device's context table entry indexed by device and function numbers,
 * - if the entry is already present, print error messages and return error code -EBUSY.
 * - if the entry is not present, set up context table entry pointing to the given domain of the device and flush
 *   cache lines to the updated tables.
 *
 * @param[in] domain a pointer to a data structure that stores the device's IOMMU domain information
 * @param[in] bus device's bus number
 * @param[in] devfun device's device and function numbers
 *
 * @return attaching result
 * @retval 0 device attaching succeeded
 * @retval -EINVAL DMAR unit is not present or doesn't support domain's address width
 * @retval -EBUSY device's context table entry is already present
 *
 * @pre domain != NULL
 * @pre bus < ACFG_MAX_PCI_BUS_NUM
 *
 * @post N/A
 *
 */
static int32_t iommu_attach_device(const struct iommu_domain *domain, uint8_t bus, uint8_t devfun)
{
	struct dmar_drhd_rt *dmar_unit;
	struct dmar_entry *root_table;
	uint64_t context_table_addr;
	struct dmar_entry *context;
	struct dmar_entry *root_entry;
	struct dmar_entry *context_entry;
	uint64_t hi_64 = 0UL;
	uint64_t lo_64 = 0UL;
	int32_t ret = -EINVAL;
	/* source id */
	union pci_bdf sid;

	sid.fields.bus = bus;
	sid.fields.devfun = devfun;

	dmar_unit = device_to_dmaru(bus, devfun);
	if ((dmar_unit != NULL) && dmar_unit_support_aw(dmar_unit, domain->addr_width)) {
		root_table = (struct dmar_entry *)hpa2hva(dmar_unit->root_table_addr);
		root_entry = root_table + bus;

		if (dmar_get_bitslice(root_entry->lo_64,
					ROOT_ENTRY_LOWER_PRESENT_MASK,
					ROOT_ENTRY_LOWER_PRESENT_POS) == 0UL) {
			/* create context table for the bus if not present */
			context_table_addr = hva2hpa(get_ctx_table(dmar_unit->index, bus));

			context_table_addr = context_table_addr >> PAGE_SHIFT;

			lo_64 = dmar_set_bitslice(lo_64,
					ROOT_ENTRY_LOWER_CTP_MASK, ROOT_ENTRY_LOWER_CTP_POS, context_table_addr);
			lo_64 = dmar_set_bitslice(lo_64,
					ROOT_ENTRY_LOWER_PRESENT_MASK, ROOT_ENTRY_LOWER_PRESENT_POS, 1UL);

			root_entry->hi_64 = 0UL;
			root_entry->lo_64 = lo_64;
			iommu_flush_cache(root_entry, sizeof(struct dmar_entry));
		} else {
			context_table_addr = dmar_get_bitslice(root_entry->lo_64,
					ROOT_ENTRY_LOWER_CTP_MASK, ROOT_ENTRY_LOWER_CTP_POS);
		}

		context_table_addr = context_table_addr << PAGE_SHIFT;

		context = (struct dmar_entry *)hpa2hva(context_table_addr);
		context_entry = context + devfun;

		if (dmar_get_bitslice(context_entry->lo_64, CTX_ENTRY_LOWER_P_MASK, CTX_ENTRY_LOWER_P_POS) != 0UL) {
			/* the context entry should not be present */
			pr_err("DMAR context table entry@0x%p, (Lower:%lx) ", context_entry, context_entry->lo_64);
			pr_err("already present for %x:%x.%x", bus, sid.bits.d, sid.bits.f);
			ret = -EBUSY;
		} else {
			/* setup context entry for the devfun */
			/* TODO: add Device TLB support */
			hi_64 = dmar_set_bitslice(hi_64, CTX_ENTRY_UPPER_AW_MASK, CTX_ENTRY_UPPER_AW_POS,
					(uint64_t)width_to_agaw(domain->addr_width));
			lo_64 = dmar_set_bitslice(lo_64, CTX_ENTRY_LOWER_TT_MASK, CTX_ENTRY_LOWER_TT_POS,
					DMAR_CTX_TT_UNTRANSLATED);
			hi_64 = dmar_set_bitslice(hi_64, CTX_ENTRY_UPPER_DID_MASK, CTX_ENTRY_UPPER_DID_POS,
				(uint64_t)vmid_to_domainid(domain->vm_id));
			lo_64 = dmar_set_bitslice(lo_64, CTX_ENTRY_LOWER_SLPTPTR_MASK, CTX_ENTRY_LOWER_SLPTPTR_POS,
				domain->trans_table_ptr >> PAGE_SHIFT);
			lo_64 = dmar_set_bitslice(lo_64, CTX_ENTRY_LOWER_P_MASK, CTX_ENTRY_LOWER_P_POS, 1UL);

			context_entry->hi_64 = hi_64;
			context_entry->lo_64 = lo_64;
			iommu_flush_cache(context_entry, sizeof(struct dmar_entry));

			pr_info("VM[%d] attach device:%02x:%02x.%x, SSPTPTR:%016lx, Translation Type:%lu, Present:%lu",
			 	domain->vm_id, bus, sid.bits.d, sid.bits.f, domain->trans_table_ptr,
				dmar_get_bitslice(context_entry->lo_64, CTX_ENTRY_LOWER_TT_MASK, CTX_ENTRY_LOWER_TT_POS),
				dmar_get_bitslice(context_entry->lo_64, CTX_ENTRY_LOWER_P_MASK, CTX_ENTRY_LOWER_P_POS));

			ret = 0;
		}
	} else {
		pr_err("Failed to attach VM[%d] device:%02x:%02x.%x to IOMMU due to invalid DMAR or domain's address \
			width not supported", domain->vm_id, bus, sid.bits.d, sid.bits.f);
	}

	return ret;
}

/**
 * @brief Detach a device from its DMAR unit by clearing context entry for the device.
 *
 * Find device's DMAR unit. If not found, do nothing and return error -EINVAL. For the valid DMAR unit, find device's
 * context entry. If the context table is not present or the entry is not present, print error message and return error
 * code -EINVAL. If domain identifier in the entry doesn't match the ID of the specified domain, print error message
 * and return error code -EPERM. Otherwise, clear the context table entry pointing to the device's domain, flush cache
 * line to the context entry, invalidate DMAR context cache and IOTLB.
 *
 * @param[in] domain a pointer to a data structure that stores the device's IOMMU domain information
 * @param[in] bus device's bus number
 * @param[in] devfun device's device and function numbers
 *
 * @return detaching result
 * @retval 0 device detaching succeeded
 * @retval -EINVAL DMAR unit, context table or context entry is not present
 * @retval -EPERM domain ID in the device's context table entry doesn't match the one in the specified domain structure
 *
 * @pre domain != NULL
 * @pre bus < ACFG_MAX_PCI_BUS_NUM
 *
 * @post N/A
 *
 */
static int32_t iommu_detach_device(const struct iommu_domain *domain, uint8_t bus, uint8_t devfun)
{
	struct dmar_drhd_rt *dmar_unit;
	struct dmar_entry *root_table;
	uint64_t context_table_addr;
	struct dmar_entry *context;
	struct dmar_entry *root_entry;
	struct dmar_entry *context_entry;
	/* source id */
	union pci_bdf sid;
	int32_t ret = -EINVAL;

	dmar_unit = device_to_dmaru(bus, devfun);

	sid.fields.bus = bus;
	sid.fields.devfun = devfun;

	if (dmar_unit != NULL) {
		root_table = (struct dmar_entry *)hpa2hva(dmar_unit->root_table_addr);
		root_entry = root_table + bus;
		ret = 0;

		context_table_addr = dmar_get_bitslice(root_entry->lo_64,  ROOT_ENTRY_LOWER_CTP_MASK,
							ROOT_ENTRY_LOWER_CTP_POS);
		context_table_addr = context_table_addr << PAGE_SHIFT;
		context = (struct dmar_entry *)hpa2hva(context_table_addr);

		context_entry = context + devfun;

		if ((context == NULL) || (context_entry == NULL)) {
			pr_err("DMAR context table entry is invalid for the detached device (devfun: 0x%x).", devfun);
			ret = -EINVAL;
		} else if ((uint16_t)dmar_get_bitslice(context_entry->hi_64, CTX_ENTRY_UPPER_DID_MASK,
						CTX_ENTRY_UPPER_DID_POS) != vmid_to_domainid(domain->vm_id)) {
			pr_err("DMAR context table domain id doesn't match the detched domain.");
			ret = -EPERM;
		} else {
			/* clear the present bit first */
			context_entry->lo_64 = 0UL;
			context_entry->hi_64 = 0UL;
			iommu_flush_cache(context_entry, sizeof(struct dmar_entry));

			dmar_invalid_context_cache(dmar_unit, vmid_to_domainid(domain->vm_id), sid.value, 0U,
							DMAR_CIRG_DEVICE);
			dmar_invalid_iotlb(dmar_unit, vmid_to_domainid(domain->vm_id), 0UL, 0U, false,
							DMAR_IIRG_DOMAIN);

			pr_info("VM[%d], detach device:%02x:%02x.%x, SSPTPTR:%016lx, Translation Type:%lu, Present:%lu",
				domain->vm_id, bus, sid.bits.d, sid.bits.f, domain->trans_table_ptr,
				dmar_get_bitslice(context_entry->lo_64, CTX_ENTRY_LOWER_TT_MASK, CTX_ENTRY_LOWER_TT_POS),
				dmar_get_bitslice(context_entry->lo_64, CTX_ENTRY_LOWER_P_MASK, CTX_ENTRY_LOWER_P_POS));
		}
	} else {
		pr_err("Failed to detach VM[%d] device:%02x:%02x.%x due to invalid DMAR.",
			domain->vm_id, bus, sid.bits.d, sid.bits.f);
	}

	return ret;
}

/**
 * @brief Perform the specified action on all DMAR units.
 *
 * @param[in] action A callback function specifies the action to be performed on all DMAR units
 *                   This callback function is a void function and has one argument with type 'struct dmar_drhd_rt *'.
 *                   The argument is a pointer to a data structure that stores the runtime information for the
 *                   specified DMAR unit where the action is to be performed.
 *
 * @return None
 *
 * @pre action != NULL
 *
 * @post N/A
 *
 */
static void do_action_for_iommus(void (*action)(struct dmar_drhd_rt *drhd_rt))
{
	struct dmar_drhd_rt *dmar_unit;
	uint32_t i;

	for (i = 0U; i < platform_dmar_info->drhd_count; i++) {
		dmar_unit = &dmar_drhd_units[i];
		action(dmar_unit);
	}
}

/**
 * @brief The array that represents all the IOMMU domains
 *
 * The number of elements in this array is MAX_DOMAIN_NUM. Every element is one iommu_domain structure.
 */
static struct iommu_domain iommu_domains[MAX_DOMAIN_NUM];
/**
 * @brief Create an IOMMU domain structure for the given VM ID, translation table and address width.
 *
 * This function calculates domain id from the given VM ID, uses domain id as index to get
 * iommu_domain structure from iommu_domains array, and sets the iommu_domain structure with
 * the given translation table address and address width.
 *
 * If the given translation table address is 0, print error message and return NULL.
 *
 * @param[in] vm_id The given VM identifier.
 * @param[in] translation_table The given host physical base address of the translation table.
 * @param[in] addr_width The given address width (in bit) of the IOMMU domain.
 *
 * @return A pointer to an IOMMU domain which contains the given information.
 * @retval NULL no IOMMU domain structure created
 * @retval others pointer to a valid IOMMU domain structure
 *
 * @pre vm_id < MAX_DOMAIN_NUM
 *
 * @post N/A
 *
 */
struct iommu_domain *create_iommu_domain(uint16_t vm_id, uint64_t translation_table, uint32_t addr_width)
{
	struct iommu_domain *domain;

	/* TODO: check if a domain with the vm_id exists */

	if (translation_table == 0UL) {
		pr_err("The given DMA translation table address is NULL");
		domain = NULL;
	} else {
		/*
		 * A hypercall is called to create an iommu domain for a valid VM,
		 * and hv code limit the VM number to CONFIG_MAX_VM_NUM.
		 * So the array iommu_domains will not be accessed out of range.
		 */
		domain = &iommu_domains[vmid_to_domainid(vm_id)];

		domain->vm_id = vm_id;
		domain->trans_table_ptr = translation_table;
		domain->addr_width = addr_width;

		dev_dbg(DBG_LEVEL_IOMMU, "Created DMA translation domain [%d]: vm_id = %hu, ept@0x%lx",
			vmid_to_domainid(domain->vm_id), domain->vm_id, domain->trans_table_ptr);
	}

	return domain;
}

/**
 * @brief Destroy the specified IOMMU domain via resetting the contents inside to all 0s.
 *
 * @param[inout] domain A pointer to an IOMMU domain whose data inside is to be cleared.
 *
 * @return None
 *
 * @pre domain != NULL
 *
 * @post N/A
 *
 */
void destroy_iommu_domain(struct iommu_domain *domain)
{
	/* TODO: check if any device assigned to this domain */
	(void)memset(domain, 0U, sizeof(*domain));
}

/**
 * @brief Move a device specified by bus & devfun from a specified old domain to a specified new IOMMU domain.
 *
 * Remove the device from the old domain (if non-NULL), and add it to the new domain (if non-NULL).
 *
 * API silently fails to add/remove devices to/from domains with any one of the following reasons:
 * 1. the specified bus number is not less than ACFG_MAX_PCI_BUS_NUM, or
 * 2. the specified new domain is NULL, or
 * 3. other failures reported by APIs iommu_detach_device and iommu_attach_device to remove from and add to domains.
 *
 * @param[in]    from_domain old iommu domain from which the device is removed from
 * @param[in]    to_domain new iommu domain to which the device is assigned to
 * @param[in]    bus the 8-bit bus number of the device
 * @param[in]    devfun the 8-bit device(5-bit):function(3-bit) of the device
 *
 * @return an integer indicating moving operation result
 * @retval 0 Moving device is succeeded, or only removing device is succeeded and to_domain is NULL
 * @retval -EINVAL device bus number is not less than ACFG_MAX_PCI_BUS_NUM, or DMAR unit is not present, or old
 *                 domain's context table or context entry is not present, or DMAR doesn't support new domain's address
 *                 width.
 * @retval -EPERM domain ID in the context table entry pointing to old domain doesn't match the one in the old domain
 *                structure
 * @retval -EBUSY context table entry pointing to new domain is already present
 *
 * @pre N/A
 *
 * @post N/A
 *
 */
int32_t move_pt_device(const struct iommu_domain *from_domain, const struct iommu_domain *to_domain, uint8_t bus,
                       uint8_t devfun)
{
	int32_t status = 0;
	uint16_t bus_local = bus;

	/* TODO: check if the device assigned */

	if (bus_local < ACFG_MAX_PCI_BUS_NUM) {
		if (from_domain != NULL) {
			status = iommu_detach_device(from_domain, bus, devfun);
		}

		if ((status == 0) && (to_domain != NULL)) {
			status = iommu_attach_device(to_domain, bus, devfun);
		}
	} else {
		pr_err("Failed to do DMA translation domain move due to device's bus number %d is bigger than %d.",
			bus_local, ACFG_MAX_PCI_BUS_NUM);
		status = -EINVAL;
	}

	return status;
}

/**
 * @brief Enable IOMMUs.
 *
 * For all DMAR units, this function enables them, including invalidating the context cache globally, invalidating the
 * IOTLB and paging structure caches globally, invalidating the interrupt entry cache globally, and enabling DMA
 * remapping and interrupt remapping functions.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark This API shall be called after init_iommu has been called once on any processor.
 *
 */
static void enable_iommu(void)
{
	do_action_for_iommus(enable_dmar);
}

/**
 * @brief Suspend IOMMUs.
 *
 * For all DMAR units, this function suspends them, including invalidating context cache globally, invalidating iotlb
 * globally, invalidating interrupt entry cache, disabling DMAR unit and saving IOMMU fault registers states.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 *
 */
void suspend_iommu(void)
{
	do_action_for_iommus(suspend_dmar);
}

/**
 * @brief Resume IOMMUs.
 *
 * For all DMAR units, this function resumes them, including restoring IOMMU fault registers state, preparing DMAR unit,
 * enabling DMAR unit DMA remapping and interrupt remapping functions.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 *
 */
void resume_iommu(void)
{
	do_action_for_iommus(resume_dmar);
}

/**
 * @brief Get the physical information of all DMAR units from ACPI tables.
 *
 * @return A pointer to the data structure which contains the physical information of the remapping hardware.
 *
 * @pre N/A
 *
 * @post return != NULL
 * @post return->drhd_count > 0U
 */
static struct dmar_info *get_dmar_info(void)
{
	struct dmar_info *platform_dmar = &plat_dmar_info;

#ifdef CONFIG_ACPI_PARSE_ENABLED
	if (parse_dmar_table(platform_dmar)) {
		platform_dmar = NULL;
	}
#endif
	return platform_dmar;
}

/**
 * @brief Initialize all DMAR units
 *
 * This function first gets platform information about all DMAR units. Register all DMAR units information, prepare the
 * hardware and enable DMAR DMA remapping function.
 *
 * This function does nothing if no information got or the number of the DMAR Hardware Unit Definition Structure is not
 * a valid value.
 *
 * @return A 32-bit integer representing the initialization result
 * @retval 0 Succeeded
 * @retval -ENODEV Failed to get platform information on DMAR units, or the number of DMAR Hardware Unit Definition
 *                 Structure is 0. Or failure reported by API register_hrhd_units indicating DMAR unit doesn't support
 *                 2MB/1GB page, or queued invalidation, or interrupt remapping, or extended interrupt mode.
 * @retval -EINVAL The number of DMAR Hardware Unit Definition Structure is bigger than CONFIG_MAX_IOMMU_NUM
 *
 * @pre N/A
 *
 * @post return <= 0
 *
 */
int32_t init_iommu(void)
{
	int32_t ret = 0;

	platform_dmar_info = get_dmar_info();

	if ((platform_dmar_info == NULL) || (platform_dmar_info->drhd_count == 0U)) {
		pr_fatal("Can't find dmar info");
		ret = -ENODEV;
	} else if (platform_dmar_info->drhd_count > CONFIG_MAX_IOMMU_NUM) {
		pr_fatal("DMAR count(%d) beyond the limitation(%d)\n",
				platform_dmar_info->drhd_count, CONFIG_MAX_IOMMU_NUM);
		ret = -EINVAL;
	} else {
		ret = register_hrhd_units();
		if (ret == 0) {
			do_action_for_iommus(prepare_dmar);
		}

		enable_iommu();
	}

	return ret;
}

/**
 * @brief Allocate the specified number of continuous interrupt remapping entries
 *
 * @param[inout] dmar_unit Pointer to the DMAR unit runtime structure
 * @param[in] num The specified number of entries to be allocated. Num should be the exponential power of 2 and
 *                no more than 32.
 *
 * @return An integer representing the allocated entries starting index, or an invalid value in failing case
 * @retval INVALID_IRTE_ID Allocation failed
 * @retval others Starting index of the successfully allocated entries
 *
 * @pre dmar_unit != NULL
 *
 * @post N/A
 *
 */
static uint16_t alloc_irtes(struct dmar_drhd_rt *dmar_unit, const uint16_t num)
{
	uint16_t irte_idx;
	uint64_t mask = (1UL << num) - 1U;
	uint64_t test_mask;

	ASSERT((bitmap_weight(num) == 1U) && (num <= 32U));

	spinlock_obtain(&dmar_unit->lock);
	for (irte_idx = 0U; irte_idx < MAX_IR_ENTRIES; irte_idx += num) {
		test_mask = mask << (irte_idx & 0x3FU);
		if ((dmar_unit->irte_alloc_bitmap[irte_idx >> 6U] & test_mask) == 0UL) {
			dmar_unit->irte_alloc_bitmap[irte_idx >> 6U] |= test_mask;
			break;
		}
	}
	spinlock_release(&dmar_unit->lock);

	return (irte_idx < MAX_IR_ENTRIES) ? irte_idx: INVALID_IRTE_ID;
}

/**
 * @brief Assign an interrupt remapping entry (IRTE) for the specified interrupt request source.
 *
 * This function performs the following work:
 * - Get DMAR unit runtime structure from the interrupt source.
 * - If the DMAR unit is empty, do nothing and return error code -EINVAL.
 * - Otherwise, allocate IRTE if the input index is invalid.
 * - If failed to allocate a new entry, do nothing and return error code -EINVAL.
 * - For valid IRTE index, fill the IRTE content based on the specified entry information.
 * - Flush cache line to the entry address.
 * - Invalidate this IRTE's Interrupt Entry Cache.
 *
 * @param[in] intr_src The specified interrupt request source.
 * @param[out] irte Pointer to the specified interrupt remapping entry structure to be assigned.
 * @param[in] idx_in The given expected index of the interrupt remapping entry in the interrupt remapping table.
 * @param[out] idx_out The pointer to the final index of the interrupt remapping entry in the interrupt remapping table.
 *                     It equals to idx_in if idx_in is a valid index or a new allocated one.
 *
 * @return Interrupt remapping entry assignment result
 * @retval 0 interrupt remapping entry assignment succeeded or failed to allocate new entry
 * @retval -EINVAL DMAR unit is not present.
 *
 * @pre intr_src != NULL
 * @pre irte != NULL
 *
 * @post return <= 0
 *
 */
int32_t dmar_assign_irte(const struct intr_source *intr_src, union dmar_ir_entry *irte,
	uint16_t idx_in, uint16_t *idx_out)
{
	struct dmar_drhd_rt *dmar_unit;
	union dmar_ir_entry *ir_table, *ir_entry;
	union pci_bdf sid;
	uint64_t trigger_mode;
	int32_t ret = -EINVAL;

	if (intr_src->is_msi) {
		dmar_unit = device_to_dmaru((uint8_t)intr_src->src.msi.bits.b, intr_src->src.msi.fields.devfun);
		sid.value = (uint16_t)(intr_src->src.msi.value);
		trigger_mode = 0x0UL;
	} else {
		dmar_unit = ioapic_to_dmaru(intr_src->src.ioapic_id, &sid);
		trigger_mode = irte->bits.remap.trigger_mode;
	}

	if (dmar_unit != NULL) {
		*idx_out = idx_in;
		if (idx_in == INVALID_IRTE_ID) {
			*idx_out = alloc_irtes(dmar_unit, 1U);
		}
		if (*idx_out < MAX_IR_ENTRIES) {
			ir_table = (union dmar_ir_entry *)hpa2hva(dmar_unit->ir_table_addr);
			ir_entry = ir_table + *idx_out;

			if (intr_src->pid_paddr != 0UL) {
				union dmar_ir_entry irte_pi;

				/* irte is in remapped mode format, convert to posted mode format */
				irte_pi.value.lo_64 = 0UL;
				irte_pi.value.hi_64 = 0UL;

				irte_pi.bits.post.vector = irte->bits.remap.vector;

				irte_pi.bits.post.svt = 0x1UL;
				irte_pi.bits.post.sid = sid.value;
				irte_pi.bits.post.present = 0x1UL;
				irte_pi.bits.post.mode = 0x1UL;

				irte_pi.bits.post.pda_l = (intr_src->pid_paddr) >> 6U;
				irte_pi.bits.post.pda_h = (intr_src->pid_paddr) >> 32U;

				*ir_entry = irte_pi;
			} else {
				/* Fields that have not been initialized explicitly default to 0 */
				irte->bits.remap.svt = 0x1UL;
				irte->bits.remap.sid = sid.value;
				irte->bits.remap.present = 0x1UL;
				irte->bits.remap.trigger_mode = trigger_mode;

				*ir_entry = *irte;
			}
			iommu_flush_cache(ir_entry, sizeof(union dmar_ir_entry));
			dmar_invalid_iec(dmar_unit, *idx_out, 0U, false);
			ret = 0;
		} else {
			pr_err("Failed to allocate DMAR Interrupt Remapping Table Entry.");
		}
	} else {
		pr_err("Failed to assign DMAR Interrupt Remapping Table Entry due to invalid DMAR.");
	}

	return ret;
}

/**
 * @brief Free the interrupt remapping entry associated with the given interrupt index.
 *
 * Get DMAR unit runtime structure from the specified interrupt source. For valid DMAR unit, clear indexed
 * interrupt remapping entry present flag, flush cache line to the entry address, invalidate this interrupt
 * entry cache in DMAR unit, and clear its bitmap in irte_alloc_bitmap array.
 *
 * @param[in] intr_src The specified interrupt request source.
 * @param[in] index The index of the interrupt remapping entry in the interrupt remapping table.
 *
 * @return None
 *
 * @pre intr_src != NULL
 *
 * @post N/A
 *
 */
void dmar_free_irte(const struct intr_source *intr_src, uint16_t index)
{
	struct dmar_drhd_rt *dmar_unit;
	union dmar_ir_entry *ir_table, *ir_entry;
	union pci_bdf sid;

	if (intr_src->is_msi) {
		dmar_unit = device_to_dmaru((uint8_t)intr_src->src.msi.bits.b, intr_src->src.msi.fields.devfun);
		sid.value = (uint16_t)(intr_src->src.msi.value);
	} else {
		dmar_unit = ioapic_to_dmaru(intr_src->src.ioapic_id, &sid);
	}

	if ((dmar_unit != NULL) && (index < MAX_IR_ENTRIES)) {
		ir_table = (union dmar_ir_entry *)hpa2hva(dmar_unit->ir_table_addr);
		ir_entry = ir_table + index;
		ir_entry->bits.remap.present = 0x0UL;

		iommu_flush_cache(ir_entry, sizeof(union dmar_ir_entry));
		dmar_invalid_iec(dmar_unit, index, 0U, false);

		spinlock_obtain(&dmar_unit->lock);
		bitmap_clear_nolock(index & 0x3FU, &dmar_unit->irte_alloc_bitmap[index >> 6U]);
		spinlock_release(&dmar_unit->lock);
	}

}

/**
 * @}
 */
