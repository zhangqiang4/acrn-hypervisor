/*
 * hypercall definition
 *
 * Copyright (C) 2017-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ACRN_HV_DEFS_H
#define ACRN_HV_DEFS_H

/**
 * @addtogroup lib_public
 *
 * @{
 */

/**
 * @file
 * @brief ACRN hypercall definitions.
 *
 * This file contains definitions for ACRN hypercalls. These definitions are used to perform various operations
 * such as VM management, interrupt handling, and memory management in the hypervisor.
 */

/*
 * Common structures for HV/HSM
 */

/**
 * @brief Construct a hypercall ID.
 *
 * This macro is to construct a hypercall ID by combining a base ID and an offset. The base ID is shifted left
 * by 24 bits to occupy the higher bits of the ID.
 *
 * @param[in] x The base ID.
 * @param[in] y The offset.
 *
 * @return The hypercall ID.
 *
 * @pre N/A
 *
 * @post N/A
 */
#define BASE_HC_ID(x, y) (((x) << 24U) | (y))

/**
 * @brief Extract the index part of a hypercall ID.
 *
 * This macro is to extract the index part of a hypercall ID by masking the lower 8 bits of the ID.
 *
 * @param[in] id The hypercall ID.
 *
 * @return The index part of the hypercall ID.
 *
 * @pre N/A
 *
 * @post N/A
 */
#define HC_IDX(id) ((id) & (0xFFUL))

#define HC_ID 0x80UL  /**< Base ID for hypercalls */

/* general */
#define HC_ID_GEN_BASE               0x0UL  /**< Base offset for general hypercalls */
/**
 * @brief Hypercall to get the API version.
 */
#define HC_GET_API_VERSION          BASE_HC_ID(HC_ID, HC_ID_GEN_BASE + 0x00UL)
/**
 * @brief Hypercall to offline a CPU from Service OS.
 */
#define HC_SERVICE_VM_OFFLINE_CPU   BASE_HC_ID(HC_ID, HC_ID_GEN_BASE + 0x01UL)
/**
 * @brief Hypercall to set the callback vector.
 */
#define HC_SET_CALLBACK_VECTOR      BASE_HC_ID(HC_ID, HC_ID_GEN_BASE + 0x02UL)
/**
 * @brief Hypercall to assign a CPU back to Service OS.
 */
#define HC_SERVICE_VM_ONLINE_CPU    BASE_HC_ID(HC_ID, HC_ID_GEN_BASE + 0x03UL)

/* VM management */
#define HC_ID_VM_BASE               0x10UL  /**< Base offset for VM management hypercalls. */
/**
 * @brief Hypercall to create a new VM.
 */
#define HC_CREATE_VM                BASE_HC_ID(HC_ID, HC_ID_VM_BASE + 0x00UL)
/**
 * @brief Hypercall to destroy a VM.
 */
#define HC_DESTROY_VM               BASE_HC_ID(HC_ID, HC_ID_VM_BASE + 0x01UL)
/**
 * @brief Hypercall to start a VM.
 */
#define HC_START_VM                 BASE_HC_ID(HC_ID, HC_ID_VM_BASE + 0x02UL)
/**
 * @brief Hypercall to pause a VM.
 */
#define HC_PAUSE_VM                 BASE_HC_ID(HC_ID, HC_ID_VM_BASE + 0x03UL)
/**
 * @brief Hypercall to create a vCPU.
 */
#define HC_CREATE_VCPU              BASE_HC_ID(HC_ID, HC_ID_VM_BASE + 0x04UL)
/**
 * @brief Hypercall to reset a VM.
 */
#define HC_RESET_VM                 BASE_HC_ID(HC_ID, HC_ID_VM_BASE + 0x05UL)
/**
 * @brief Hypercall to set vCPU state.
 */
#define HC_SET_VCPU_REGS            BASE_HC_ID(HC_ID, HC_ID_VM_BASE + 0x06UL)

/* IRQ and Interrupts */
#define HC_ID_IRQ_BASE              0x20UL /**< Base offset for IRQ and interrupt management hypercalls */
/**
 * @brief Hypercall to inject a MSI into a VM.
 */
#define HC_INJECT_MSI               BASE_HC_ID(HC_ID, HC_ID_IRQ_BASE + 0x03UL)
/**
 * @brief Hypercall to monitor interrupts for a VM.
 */
#define HC_VM_INTR_MONITOR          BASE_HC_ID(HC_ID, HC_ID_IRQ_BASE + 0x04UL)
/**
 * @brief Hypercall to set an IRQ line for a VM.
 */
#define HC_SET_IRQLINE              BASE_HC_ID(HC_ID, HC_ID_IRQ_BASE + 0x05UL)

/* DM ioreq management */
#define HC_ID_IOREQ_BASE            0x30UL  /**< Base offset for device model I/O request management hypercalls */
/**
 * @brief Hypercall to set the I/O request buffer.
 */
#define HC_SET_IOREQ_BUFFER         BASE_HC_ID(HC_ID, HC_ID_IOREQ_BASE + 0x00UL)
/**
 * @brief Hypercall to notify the completion of an I/O request.
 */
#define HC_NOTIFY_REQUEST_FINISH    BASE_HC_ID(HC_ID, HC_ID_IOREQ_BASE + 0x01UL)
/**
 * @brief Hypercall to assign asynchronous I/O operations.
 */
#define HC_ASYNCIO_ASSIGN           BASE_HC_ID(HC_ID, HC_ID_IOREQ_BASE + 0x02UL)
/**
 * @brief Hypercall to deassign asynchronous I/O operations.
 */
#define HC_ASYNCIO_DEASSIGN         BASE_HC_ID(HC_ID, HC_ID_IOREQ_BASE + 0x03UL)

/* Guest memory management */
#define HC_ID_MEM_BASE              0x40UL  /**< Base offset for guest memory management hypercalls */
/**
 * @brief Hypercall to translate a guest physical address to a host physical address.
 */
#define HC_VM_GPA2HPA               BASE_HC_ID(HC_ID, HC_ID_MEM_BASE + 0x01UL)
/**
 * @brief Hypercall to set a guest memory region.
 */
#define HC_VM_SET_MEMORY_REGIONS    BASE_HC_ID(HC_ID, HC_ID_MEM_BASE + 0x02UL)
/**
 * @brief Hypercall to write-protect a guest memory page.
 */
#define HC_VM_WRITE_PROTECT_PAGE    BASE_HC_ID(HC_ID, HC_ID_MEM_BASE + 0x03UL)
/**
 * @brief Hypercall to set up a shared buffer.
 */
#define HC_SETUP_SBUF               BASE_HC_ID(HC_ID, HC_ID_MEM_BASE + 0x04UL)

/* PCI assignment*/
#define HC_ID_PCI_BASE              0x50UL  /**< Base offset for PCI assignment hypercalls */
/**
 * @brief Hypercall to assign a pass-through device.
 */
#define HC_ASSIGN_PTDEV             BASE_HC_ID(HC_ID, HC_ID_PCI_BASE + 0x00UL)
/**
 * @brief Hypercall to deassign a pass-through device.
 */
#define HC_DEASSIGN_PTDEV           BASE_HC_ID(HC_ID, HC_ID_PCI_BASE + 0x01UL)
/**
 * @brief Hypercall to remap MSI-X vectors.
 */
#define HC_VM_PCI_MSIX_REMAP        BASE_HC_ID(HC_ID, HC_ID_PCI_BASE + 0x02UL)
/**
 * @brief Hypercall to set interrupt information for a pass-through device.
 */
#define HC_SET_PTDEV_INTR_INFO      BASE_HC_ID(HC_ID, HC_ID_PCI_BASE + 0x03UL)
/**
 * @brief Hypercall to reset interrupt information for a pass-through device.
 */
#define HC_RESET_PTDEV_INTR_INFO    BASE_HC_ID(HC_ID, HC_ID_PCI_BASE + 0x04UL)
/**
 * @brief Hypercall to assign a PCI device.
 */
#define HC_ASSIGN_PCIDEV            BASE_HC_ID(HC_ID, HC_ID_PCI_BASE + 0x05UL)
/**
 * @brief Hypercall to deassign a PCI device.
 */
#define HC_DEASSIGN_PCIDEV          BASE_HC_ID(HC_ID, HC_ID_PCI_BASE + 0x06UL)
/**
 * @brief Hypercall to assign a MMIO device.
 */
#define HC_ASSIGN_MMIODEV           BASE_HC_ID(HC_ID, HC_ID_PCI_BASE + 0x07UL)
/**
 * @brief Hypercall to deassign a MMIO device.
 */
#define HC_DEASSIGN_MMIODEV         BASE_HC_ID(HC_ID, HC_ID_PCI_BASE + 0x08UL)
/**
 * @brief Hypercall to add a virtual device.
 */
#define HC_ADD_VDEV                 BASE_HC_ID(HC_ID, HC_ID_PCI_BASE + 0x09UL)
/**
 * @brief Hypercall to remove a virtual device.
 */
#define HC_REMOVE_VDEV              BASE_HC_ID(HC_ID, HC_ID_PCI_BASE + 0x0AUL)

#ifdef HV_DEBUG

/* DEBUG */
#define HC_ID_DBG_BASE              0x60UL  /**< Base offset for debug hypercalls */
/**
 * @brief Hypercall to set up hypervisor logging using NPK.
 */
#define HC_SETUP_HV_NPK_LOG         BASE_HC_ID(HC_ID, HC_ID_DBG_BASE + 0x01UL)
/**
 * @brief Hypercall to perform profiling operations.
 */
#define HC_PROFILING_OPS            BASE_HC_ID(HC_ID, HC_ID_DBG_BASE + 0x02UL)
/**
 * @brief Hypercall to retrieve hardware information.
 */
#define HC_GET_HW_INFO              BASE_HC_ID(HC_ID, HC_ID_DBG_BASE + 0x03UL)

#endif

/* Power management */
#define HC_ID_PM_BASE               0x80UL  /**< Base offset for power management hypercalls */
/**
 * @brief Hypercall to get CPU state.
 */
#define HC_PM_GET_CPU_STATE         BASE_HC_ID(HC_ID, HC_ID_PM_BASE + 0x00UL)

#define ACRN_INVALID_VMID (0xffffU) /**< Invalid VM ID */
#define ACRN_INVALID_HPA (~0UL)     /**< Invalid Host Physical Address (HPA) */

/* Generic memory attributes */
#define	MEM_ACCESS_READ                 0x00000001U  /**< Read access permission */
#define	MEM_ACCESS_WRITE                0x00000002U  /**< Write access permission */
#define	MEM_ACCESS_EXEC	                0x00000004U  /**< Execute access permission */
/**
 * @brief Read, Write, Execute access permission
 */
#define	MEM_ACCESS_RWX			(MEM_ACCESS_READ | MEM_ACCESS_WRITE | MEM_ACCESS_EXEC)
#define MEM_ACCESS_RIGHT_MASK           0x00000007U  /**< Mask for access right */
#define	MEM_TYPE_WB                     0x00000040U  /**< Write-back memory type */
#define	MEM_TYPE_WT                     0x00000080U  /**< Write-through memory type */
#define	MEM_TYPE_UC                     0x00000100U  /**< Uncacheable memory type */
#define	MEM_TYPE_WC                     0x00000200U  /**< Write-combining memory type */
#define	MEM_TYPE_WP                     0x00000400U  /**< Write-protected memory type */
#define MEM_TYPE_MASK                   0x000007C0U  /**< Mask for memory type */

#define MR_ADD		0U  /**< Add memory region */
#define MR_DEL		2U  /**< Delete memory region */
#define MR_MODIFY	3U  /**< Modify memory region */

/**
 * @brief Info to set guest memory region mapping
 *
 * The parameter for HC_VM_SET_MEMORY_REGION hypercall
 *
 * @alignment 8
 */
struct vm_memory_region {
	uint32_t type; /**< Set memory region type: MR_ADD or MR_DEL */

	uint32_t prot;  /**< Memory attributes: memory type + RWX access right */

	uint64_t gpa;  /**< The beginning guest physical address of the memory region */

	uint64_t service_vm_gpa;  /**< Service VM's guest physical address which gpa will be mapped to */

	uint64_t size;  /**< Size of the memory region */
} __aligned(8);

/**
 * @brief Set memory regions.
 *
 * Set multi memory regions, these parameter are used for HC_VM_SET_MEMORY_REGIONS.
 *
 * @alignment 8
 */
struct set_regions {
	uint16_t vmid; 	/**< VM ID for this hypercall */
	uint16_t reserved0;  /**< Reserved */
	uint32_t reserved1;  /**< Reserved */
	uint32_t mr_num; 	/**< Memory region numbers */
	/**
	 * @brief the gpa of regions buffer
	 *
	 * This points to the regions array: struct vm_memory_region regions[mr_num].
	 * The max buffer size is one page.
	 */
	uint64_t regions_gpa;
} __aligned(8);

/**
 * @brief Info to change guest one page write protect permission
 *
 * The parameter for HC_VM_WRITE_PROTECT_PAGE hypercall
 *
 * @alignment 8
 */
struct wp_data {
	/**
	 * @brief Set page write protect permission.
	 *
	 * True: set the wp; flase: clear the wp.
	 */
	uint8_t set;

	uint64_t pad:56;  /**< Reserved */

	uint64_t gpa; /**< The guest physical address of the page to change */
} __aligned(8);

/**
 * @brief Shared buffer parameters
 *
 * Setup parameter for shared buffer, used for HC_SETUP_SBUF hypercall
 *
 * @alignment 8
 */
struct acrn_sbuf_param {
	uint16_t cpu_id; 	/**< Shared buffer CPU ID */

	uint16_t reserved; /**< Reserved */

	uint32_t sbuf_id; /**< Shared buffer ID */

	uint64_t gpa;  /**< Shared buffer's guest physical address */
} __aligned(8);

/**
 * @brief Info to setup the hypervisor NPK log
 *
 * The parameter for HC_SETUP_HV_NPK_LOG hypercall
 *
 * @alignment 8
 */
struct hv_npk_log_param {
	uint16_t cmd;  /**< The setup command for the hypervisor NPK log */
	uint16_t res;  /**< The setup result for the hypervisor NPK log */
	uint16_t loglevel;  /**< The loglevel for the hypervisor NPK log */
	uint16_t reserved; /**< Reserved */
	uint64_t mmio_addr;  /**< The MMIO address for the hypervisor NPK log */
} __aligned(8);

/**
 * @brief Get ACRN hardware info.
 *
 * The parameter for HC_GET_HW_INFO hypercall
 *
 * @alignment 8
 */
struct acrn_hw_info {
	uint16_t cpu_num; /**< Physical CPU number */
	uint16_t reserved[3];  /**< Reserved */
} __aligned(8);

#define IRQ_INTX 0U  /**< Type for INT-x interrupt */
#define IRQ_MSI 1U   /**< Type for MSI interrupt */
#define IRQ_MSIX 2U  /**< Type for MSI-X interrupt */

/**
 * @brief INTX remapping info
 */
struct intx_info {
	uint32_t virt_pin;  /**< Virtual IOAPIC/PIC pin */

	uint32_t phys_pin;  /**< Physical IOAPIC pin */

	bool pic_pin;  /**< Is virtual pin from PIC */

	uint8_t reserved[3];  /**< Reserved */
};

/**
 * @brief Info to set interrupt information for a pass-through device
 *
 * This structure is used as a parameter for the HC_SET_PTDEV_INTR_INFO hypercall to set interrupt information for a
 * pass-through device.
 *
 * @alignment 8
 */
struct hc_ptdev_irq {
	uint32_t type;  /**< IRQ mapping type: INTX or MSI */

	uint16_t virt_bdf;  /**< Virtual BDF of the ptdev */

	uint16_t phys_bdf;  /**< Physical BDF of the ptdev */

	/**
	 * @brief INTX remapping info
	 */
	struct intx_info intx;

} __aligned(8);

#define ACRN_PLATFORM_LAPIC_IDS_MAX	64U  /**< Maximum number of LAPIC IDs */


/**
 * @brief Enumeration for profiling command types in the hypervisor.
 *
 * This enumeration defines the different types of profiling commands that can be used in the hypervisor. Each command
 * type corresponds to a specific profiling operation.
 */
enum profiling_cmd_type {
	PROFILING_MSR_OPS = 0U, /**< Command to perform operations on MSRs */
	PROFILING_GET_VMINFO,   /**< Command to get information about VMs */
	PROFILING_GET_VERSION,  /**< Command to get the version of the profiling interface */
	PROFILING_GET_CONTROL_SWITCH, /**< Command to get the current state of profiling control switches */
	PROFILING_SET_CONTROL_SWITCH, /**< Command to set the state of profiling control switches */
	PROFILING_CONFIG_PMI,  /**< Command to configure Performance Monitoring Interrupts */
	PROFILING_CONFIG_VMSWITCH,  /**< Command to configure VM switches */
	PROFILING_GET_PCPUID,  /**< Command to get the physical CPU ID */
	PROFILING_GET_STATUS   /**< Command to get the status of the profiling interface */
};

#endif /* ACRN_HV_DEFS_H */

/**
 * @}
 */
