/*
 * Copyright (C) 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PTDEV_H
#define PTDEV_H
#include <list.h>
#include <asm/lib/spinlock.h>
#include <timer.h>
#include <vacpi.h>

/**
 * @addtogroup vp-dm_ptirq vp-dm.ptirq
 *
 * @{
 */

/**
 * @file
 *
 * @brief Public APIs for device interrupt passthrough
 */

/**
 * @defgroup PTDEV_INTERRUPT_TYPE interrupt type to passthrough
 *
 * @{
 */
#define PTDEV_INTR_MSI		(1U << 0U)      /**< MSI interrupt to passthrough */
#define PTDEV_INTR_INTX		(1U << 1U)      /**< INTx/GSI interrupt to passthrough */
/**
 * @}
 */

#define GPU_OPREGION_SIZE	0x5000U         /**< Size of GPU Operation Region */
#define GPU_OPREGION_GPA	(VIRT_ACPI_DATA_ADDR  - GPU_OPREGION_SIZE)      /**< GPA of GPU Operation Region */
#define PCIR_ASLS_CTL		0xfcU           /**< ASLS_CTL (OpRegion Base) offset in PCIe config space */
#define PCIM_ASLS_OPREGION_MASK	0xfffff000U     /**< OpRegion need 4KB aligned */

/**
 * @brief Define a MSI interrupt source id with PCI bus, device, function numbers and entry number
 *
 * @param[in] name The name of the variable
 * @param[in] a PCI BDF of MSI interrupt source
 * @param[in] b MSI vector index
 */
#define DEFINE_MSI_SID(name, a, b)	\
union source_id (name) = {.msi_id = {.bdf = (a), .entry_nr = (b)} }

/**
 * @brief Define an INTx interrupt source id with GSI
 *
 * @param[in] name The name of the variable
 * @param[in] a GSI number of INTx interrupt source
 */
#define DEFINE_INTX_SID(name, a)	\
union source_id (name) = {.intx_id = {.gsi = (a)} }

/**
 * @brief IRET index format
 */
union irte_index {
	uint16_t index;                 /**< the whole word */
	struct {
		uint16_t index_low:15;  /**< Low 15 bits of IRTE index */
		uint16_t index_high:1;  /**< Highest bit of IRTE index */
	} bits __packed;                /**< the bits representation, __packed ensures total size is 2 bytes */
};


/**
 * @brief Source ID of a virtual interrupt or physical interrupt
 */
union source_id {
	uint64_t value;                 /**< The whole qword */
	struct {
		uint16_t bdf;           /**< BDF of virtual or physical device */
		uint16_t entry_nr;      /**< MSI/MSI-x vector index */
		uint32_t reserved;      /**< Reserved */
	} msi_id;                       /**< MSI interrupt id format */
	/*
	 * ctlr indicates the source of interrupt controller, now it is IO-APIC
	 * pin indicates the pin number of interrupt controller determined by ctlr
	 */
	struct {
		uint32_t gsi;           /**< Virtual or physical GSI of an INTx interrupt */
	} intx_id;                      /**< INTx interrupt id format */
};

#define	MSI_ADDR_BASE			0xfeeUL	/**< Base address for MSI messages */
#define	MSI_ADDR_RH			0x1U	/**< Redirection Hint */
#define	MSI_ADDR_DESTMODE_LOGICAL	0x1U	/**< Destination Mode: Logical */
#define	MSI_ADDR_DESTMODE_PHYS		0x0U	/**< Destination Mode: Physical */

/**
 * @brief Definition of MSI/MSI-x address register on x86 platforms
 */
union msi_addr_reg {
	uint64_t full;                  /**< The whole qword */
	struct {
		uint32_t rsvd_1:2;      /**< Reserved */
		uint32_t dest_mode:1;   /**< Destination mode: logical or physical */
		uint32_t rh:1;          /**< Redirection hint */
		uint32_t rsvd_2:8;      /**< Reserved */
		uint32_t dest_field:8;  /**< Interrupt destination */
		uint32_t addr_base:12;  /**< Address base, should be 0xFEEUL */
		uint32_t hi_32;         /**< High 32 bit of the address */
	} bits __packed;                /**< Compatibility format of the register value, __packed ensures total size
					     is 8 bytes */
	struct {
		uint32_t rsvd_1:2;      /**< Reserved */
		uint32_t intr_index_high:1;     /**< Highest bit of IRTE index */
		uint32_t shv:1;                 /**< Subhandle valid */
		uint32_t intr_format:1;         /**< "1" for remappable format */
		uint32_t intr_index_low:15;     /**< Lower bits of IRTE index */
		uint32_t constant:12;           /**< 0xFEEUL */
		uint32_t hi_32;         /**< High 32 bit of the address */
	} ir_bits __packed;             /**< Remappable format. __packed ensures total size is 8 bytes */

};


#define MSI_DATA_DELMODE_FIXED		0x0U	/**< MSI Delivery Mode: Fixed */
#define MSI_DATA_DELMODE_LOPRI		0x1U	/**< MSI Delivery Mode: Low Priority */
#define MSI_DATA_TRGRMODE_EDGE		0x0U	/**< MSI Trigger Mode: Edge */
#define MSI_DATA_TRGRMODE_LEVEL		0x1U	/**< MSI Trigger Mode: Level */

/**
 * @brief MSI/MSI-x data register definition for x86 platform
 */
union msi_data_reg {
	uint32_t full;                          /**< The whole dword */
	struct {
		uint32_t vector:8;              /**< MSI vector number */
		uint32_t delivery_mode:3;       /**< MSI Deliver mode */
		uint32_t rsvd_1:3;              /**< Reserved */
		uint32_t level:1;               /**< Level value for level triggered interrupts */
		uint32_t trigger_mode:1;        /**< Level or Edge triggered */
		uint32_t rsvd_2:16;             /**< Reserved */
	} bits __packed;                        /**< Bit format, __packed ensures total size is 4 bytes */
};

/**
 * @brief MSI address and data
 */
struct msi_info {
	union msi_addr_reg addr;        /**< The address register of MSI */
	union msi_data_reg data;        /**< The data register of MSI */
};

struct ptirq_remapping_info;

/**
 * @brief Signature of the release callback for a ptirq_remapping_info
 */
typedef void (*ptirq_arch_release_fn_t)(const struct ptirq_remapping_info *entry);

/**
 * @brief Information structure for an interrupt remapped to a VM for passthrough device.
 */
struct ptirq_remapping_info {
	struct hlist_node phys_link;    /**< Hash list node on phys_sid_htable */
	struct hlist_node virt_link;    /**< Hash list node on virt_sid_htable */
	uint16_t ptdev_entry_id;        /**< The bitmap id for the entry, mark it's in use */
	uint32_t intr_type;             /**< INTx or MSI/MSI-x */
	union source_id phys_sid;       /**< Source id of the physical interrupt in this mapping */
	union source_id virt_sid;       /**< Source id of the virtual interrupt in this mapping */
	struct acrn_vm *vm;             /**< VM the interrupt is remapped to */
	bool active;	                /**< true=active, false=inactive*/
	uint32_t allocated_pirq;        /**< Physical IRQ for the non posted interrupt */
	uint32_t polarity;              /**< 0=active high, 1=active low */
	struct list_head softirq_node;  /**< A list_head to insert into percpu pending softirq list */
	struct msi_info vmsi;           /**< Virtual MSI address and data register values */
	struct msi_info pmsi;           /**< Physical MSI address and data register values */
	uint16_t irte_idx;              /**< DMAR IRTE index for this mapping */

	uint64_t intr_count;            /**< Trapped interrupt count for non-posted interrupts */
	struct hv_timer intr_delay_timer;       /**< Timer used to delay injection to avoid interrupt flood */
	ptirq_arch_release_fn_t release_cb;     /**< Release callback for this entry */
};

/**
 * @brief Check if a ptirq_remapping_info is active
 *
 * @param[in] entry The ptirq_remapping_info to check
 *
 * @return A boolean value to indicate whether the ptirq_remapping_info is active
 * @retval True Given ptirq_remapping_info is active
 * @retval False Given ptirq_remapping_info is inactive
 */
static inline bool is_entry_active(const struct ptirq_remapping_info *entry)
{
	return entry->active;
}

extern struct ptirq_remapping_info ptirq_entries[CONFIG_MAX_PT_IRQ_ENTRIES];
extern spinlock_t ptdev_lock;


struct ptirq_remapping_info *find_ptirq_entry(uint32_t intr_type,
		const union source_id *sid, const struct acrn_vm *vm);

void ptirq_softirq(uint16_t pcpu_id);
void ptdev_init(void);
void ptdev_release_all_entries(const struct acrn_vm *vm);

struct ptirq_remapping_info *ptirq_dequeue_softirq(uint16_t pcpu_id);
struct ptirq_remapping_info *ptirq_alloc_entry(struct acrn_vm *vm, uint32_t intr_type);
void ptirq_release_entry(struct ptirq_remapping_info *entry);
int32_t ptirq_activate_entry(struct ptirq_remapping_info *entry, uint32_t phys_irq);
void ptirq_deactivate_entry(struct ptirq_remapping_info *entry);
uint32_t ptirq_get_intr_data(const struct acrn_vm *target_vm, uint64_t *buffer, uint32_t buffer_cnt);

/**
  * @}
  */

#endif /* PTDEV_H */
