/*
 * common definition
 *
 * Copyright (C) 2017-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ACRN_COMMON_H
#define ACRN_COMMON_H

#include <types.h>

/*
 * Common structures for ACRN/HSM/DM
 */

/**
 * @defgroup lib_public lib.public
 * @ingroup lib
 * @brief Public interfaces and common definitions for ACRN hypervisor.
 *
 * This module provides the essential data structures, enumerations, and definitions that are shared across
 * various components of the ACRN Hypervisor. It includes definitions for hypercalls, I/O handling, VM and
 * vCPU state management, and power management. These interfaces facilitate communication between the
 * hypervisor, Service VM, and User VMs, ensuring a consistent and efficient virtualization environment.
 *
 * @{
 */

/**
 * @file
 * @brief Common data structures, enumerations, and definitions for ACRN Hypervisor hypercalls and I/O handling.
 *
 * This header defines structures and types used for communication between the hypervisor, Service VM,
 * and User VMs. It includes VM/VCPU state management, I/O request handling, interrupt operations,
 * power management, shared buffer configurations and VM event configurations.
 */

/**
 * @brief VM reset modes.
 *
 * This enumeration defines the reset mode of a VM. These modes determine how hardware and
 * virtualized components (e.g., registers, MSRs) are initialized during a reset event.
 */
enum vm_reset_mode {
	VM_POWER_ON_RESET,  /**< Reset by hardware power-on */
	VM_COLD_RESET,      /**< Hardware cold reset */
	/**
	 * @brief Hardware warm reset.
	 *
	 * In hardware warm reset, the behavior slightly differs from cold reset, that some MSRs might be retained.
	 */
	VM_WARM_RESET,
	VM_RESUME_FROM_S3,  /**< Reset core states after resuming from S3 */
	VM_RESET_MAX,       /**< The max value of reset mode */
};

/**
 * @brief VCPU reset modes.
 *
 * Refer to Chapter 10.1.1 Processor State After Reset, Vol.3, SDM 325462-078 for more details about vCPU reset mode.
 */
enum vcpu_reset_mode {
	VCPU_POWER_UP,   /**< Reset by hardware power up */
	VCPU_COLD_RESET, /**< Hardware cold reset */
	/**
	 * @brief Hardware warm reset.
	 *
	 * In hardware warm reset, the behavior slightly differs from cold reset, that some MSRs might be retained.
	 */
	VCPU_WARM_RESET,
	VCPU_INIT_RESET, /**< Reset by INIT */
	VCPU_RESET_MAX,  /**< The max value of reset mode */
	VCPU_RESET_INVALID = VCPU_RESET_MAX, /**< Invalid reset mode */
};

/**
 * @brief VLAPIC reset modes.
 *
 * Refer to Chapter 11.4.7 Local APIC State, Vol.3, SDM 325462-078 for more details about vLAPIC reset state.
 */
enum vlapic_reset_mode {
	VLAPIC_POWER_UP,   /**< APIC reset to power-up state */
	VLAPIC_SOFTWARE_DISABLE, /**< Clear APIC Software Enable/Disable flag in SVR */
	VLAPIC_INIT_RESET, /**< APIC reset via INIT signal */
	VLAPIC_RESET_MAX,  /**< The max value for valid vLAPIC reset modes */
	VLAPIC_RESET_INVALID = VLAPIC_RESET_MAX, /**< Invalid reset mode */
};

/**
 * @brief VM reset mode to vCPU reset mode mapping.
 *
 * This array is to map the reset mode of a VM to the reset mode of a vCPU.
 */
static const enum vcpu_reset_mode vm2vcpu[VM_RESET_MAX] = {
	[VM_POWER_ON_RESET]	= VCPU_POWER_UP,
	[VM_COLD_RESET]		= VCPU_COLD_RESET,
	[VM_WARM_RESET]		= VCPU_WARM_RESET,
	[VM_RESUME_FROM_S3]	= VCPU_COLD_RESET,
};

/**
 * @brief Convert VM reset mode to vCPU reset mode.
 *
 * This function converts the reset mode of a VM to the reset mode of a vCPU. If the mode is valid (in the range of
 * [VM_POWER_ON_RESET, VM_RESET_MAX) ), it returns the corresponding vCPU reset mode. Otherwise, it returns
 * VCPU_RESET_INVALID.
 *
 * @param[in] mode The reset mode of a VM.
 *
 * @return The reset mode of a vCPU.
 *
 * @pre N/A
 *
 * @post return <= VCPU_RESET_MAX
 */
static inline enum vcpu_reset_mode reset_mode_vm2vcpu(enum vm_reset_mode mode)
{
	return ((VM_POWER_ON_RESET <= mode) && (mode < VM_RESET_MAX)) ?
		vm2vcpu[mode] : VCPU_RESET_INVALID;
}

/**
 * @brief VCPU reset mode to VLAPIC reset mode mapping.
 *
 * This array is to map the reset mode of a vCPU to the reset mode of a VLAPIC.
 */
static const enum vlapic_reset_mode vcpu2vlapci[VCPU_RESET_MAX] = {
	[VCPU_POWER_UP]		= VLAPIC_POWER_UP,
	[VCPU_COLD_RESET]	= VLAPIC_POWER_UP,
	[VCPU_WARM_RESET]	= VLAPIC_POWER_UP,
	[VCPU_INIT_RESET]	= VLAPIC_INIT_RESET,
};

/**
 * @brief Convert vCPU reset mode to VLAPIC reset mode.
 *
 * This function converts the reset mode of a vCPU to the reset mode of a VLAPIC. If the mode is valid (in the range of
 * [VCPU_POWER_UP, VCPU_RESET_MAX) ) and the mode is not VCPU_RESET_INVALID, it returns the corresponding VLAPIC reset
 * mode. Otherwise, it returns VLAPIC_RESET_INVALID.
 *
 * @param[in] mode The reset mode of a vCPU.
 *
 * @return The reset mode of a VLAPIC.
 *
 * @pre N/A
 *
 * @post return <= VLAPIC_RESET_MAX
 */
static inline enum vlapic_reset_mode reset_mode_vcpu2vlapic(enum vcpu_reset_mode mode)
{
	return ((VCPU_POWER_UP <= mode) && (mode < VCPU_RESET_MAX)) ?
		vcpu2vlapci[mode] : VLAPIC_RESET_INVALID;
}

/*
 * IO request
 */

#define ACRN_IO_REQUEST_MAX		16U  /**< Max I/O requests */
#define ACRN_ASYNCIO_MAX		64U  /**< Max async I/O requests */

#define ACRN_IOREQ_STATE_PENDING	0U	/**< The I/O request is pending */
#define ACRN_IOREQ_STATE_COMPLETE	1U  /**< The I/O request has been completed */
#define ACRN_IOREQ_STATE_PROCESSING	2U  /**< The I/O request is being processed */
#define ACRN_IOREQ_STATE_FREE		3U  /**< The I/O request slot is free */

#define ACRN_IOREQ_TYPE_PORTIO      0U  /**< Port I/O request type */
#define ACRN_IOREQ_TYPE_MMIO        1U  /**< MMIO request type */
#define ACRN_IOREQ_TYPE_PCICFG      2U  /**< PCI configuration space access type */
#define ACRN_IOREQ_TYPE_WP          3U  /**< Write-protection fault handling type */

#define ACRN_IOREQ_DIR_READ     0U  /**< The I/O request direction is read */
#define ACRN_IOREQ_DIR_WRITE    1U  /**< The I/O request direction is write */

/* IOAPIC device model info */
#define VIOAPIC_RTE_NUM	48U  /**< Number of Redirection Table Entries in vIOAPIC */

#if VIOAPIC_RTE_NUM < 24U
#error "VIOAPIC_RTE_NUM must be larger than 23"
#endif

/* Generic VM flags from guest OS */
#define GUEST_FLAG_LAPIC_PASSTHROUGH     (1UL << 1U)  /**< Whether LAPIC is passed through */
#define GUEST_FLAG_IO_COMPLETION_POLLING (1UL << 2U)  /**< Whether need hypervisor poll IO completion */
#define GUEST_FLAG_RT				(1UL << 4U)  /**< Whether the vm is RT-VM */
/**
 * @brief Whether this VM needs to do security-vm related fixup (TPM2 andSMBIOS passthrough)
 */
#define GUEST_FLAG_SECURITY_VM		(1UL << 6U)
#define GUEST_FLAG_STATIC_VM		(1UL << 8U)  /**< Whether this VM uses static VM configuration */
#define GUEST_FLAG_TEE				(1UL << 9U)	 /**< Whether the VM is TEE VM */
#define GUEST_FLAG_REE				(1UL << 10U) /**< Whether the VM is REE VM */
#define GUEST_FLAG_PMU_PASSTHROUGH	(1UL << 11U) /**< Whether PMU is passed through */
/* TODO: remove these DEPRECATED items along with config tools */
#define GUEST_FLAG_VHWP  (1UL << 12U)  /**< [DEPRECATED] Whether the VM supports vHWP */
#define GUEST_FLAG_VTM   (1UL << 13U)  /**< [DEPRECATED] Whether the VM supports virtual thermal monitor */
#define GUEST_FLAG_VPTM  (1UL << 15U)  /**< [DEPRECATED] Whether the VM supports virtual package thermal management */

/**
 * @brief Whether the vCPU supports vHWP
 */
#define GUEST_CPU_FEATURE_VHWP		(1UL << 0U)
/**
 * @brief Whether passthrough per-core thermal monitor to guest
 */
#define GUEST_CPU_FEATURE_TM_PT		(1UL << 1U)
/**
 * @brief Whether passthrough package thermal monitor to guest
 */
#define GUEST_CPU_FEATURE_PTM_PT	(1UL << 2U)
/**
 * @brief Whether passthrough last branch records to guest
 */
#define GUEST_CPU_FEATURE_LBR_PT	(1UL << 3U)

/* TODO: We may need to get this addr from guest ACPI instead of hardcode here */

/* Pre-launched VM uses ACPI reduced HW mode and sleep control register */
#define VIRTUAL_SLEEP_CTL_ADDR    0x400U  /**< Sleep control register address */

#define VIRTUAL_PM1A_CNT_ADDR     0x404U  /**< PM1A control register address */
#define	VIRTUAL_PM1A_SCI_EN       0x0001  /**< SCI_EN bit in PM1A_CNT */
#define VIRTUAL_PM1A_SLP_TYP      0x1c00U /**< SLP_TYP field in PM1A_CNT */
#define VIRTUAL_PM1A_SLP_EN       0x2000U /**< SLP_EN bit in PM1A_CNT */
#define	VIRTUAL_PM1A_ALWAYS_ZERO  0xc003  /**< PM1A reserved bits forced to zero */

#define MAX_VM_NAME_LEN        (16U)  /**< The maximum length of a VM name */

/**
 * @brief Representation of a MMIO request
 *
 * This structure represents a MMIO request, containing the direction, address, size, and value of the request.
 */
struct acrn_mmio_request {
	/**
	 * @brief Direction of the access.
	 *
	 * Either \p ACRN_IOREQ_DIR_READ or \p ACRN_IOREQ_DIR_WRITE.
	 */
	uint32_t direction;

	/**
	 * @brief reserved
	 */
	uint32_t reserved;

	/**
	 * @brief Address of the I/O access
	 */
	uint64_t address;

	/**
	 * @brief Width of the I/O access in byte
	 */
	uint64_t size;

	/**
	 * @brief The value read for I/O reads or to be written for I/O writes
	 */
	uint64_t value;
};

/**
 * @brief Representation of a port I/O request.
 *
 * This structure represents a PIO request, containing the direction, address, size, and value of the request.
 */
struct acrn_pio_request {
	/**
	 * @brief Direction of the access
	 *
	 * Either \p ACRN_IOREQ_DIR_READ or \p ACRN_IOREQ_DIR_WRITE.
	 */
	uint32_t direction;

	uint32_t reserved; /**< Reserved */

	uint64_t address; /**< Port address of the I/O access */

	uint64_t size; /**< Width of the I/O access in byte */

	uint32_t value; /**< The value read for I/O reads or to be written for I/O writes */
};

/**
 * @brief Representation of a PCI configuration space access request.
 *
 * This structure represents a pci configuration space access, containing the direction, size, value, bus, device,
 * function, and register of the request.
 */
struct acrn_pci_request {
	/**
	 * @brief Direction of the access
	 *
	 * Either \p ACRN_IOREQ_DIR_READ or \p ACRN_IOREQ_DIR_WRITE.
	 */
	uint32_t direction;

	/* need keep same header fields with pio_request */
	uint32_t reserved[3]; /**< Reserved */

	int64_t size; /**< Width of the I/O access in byte */

	int32_t value; /**< The value read for I/O reads or to be written for I/O writes */

	int32_t bus; /**< The \p bus part of the BDF of the device */

	int32_t dev; /**< The \p device part of the BDF of the device */

	int32_t func; /**< The \p function part of the BDF of the device */

	int32_t reg; /**< The register to be accessed in the configuration space */
};

/**
 * @brief 256-byte I/O requests
 *
 * The state transitions of a I/O request are:
 *
 *    FREE -> PENDING -> PROCESSING -> COMPLETE -> FREE -> ...
 *
 * When a request is in COMPLETE or FREE state, the request is owned by the
 * hypervisor. Service VM (HSM or DM) shall not read or write the internals of the
 * request except the state.
 *
 * When a request is in PENDING or PROCESSING state, the request is owned by
 * Service VM. The hypervisor shall not read or write the request other than the state.
 *
 * Based on the rules above, a typical I/O request lifecycle should looks like
 * the following.
 *
 * @alignment 256
 *
 * @verbatim embed:rst:leading-asterisk
 *
 * +-----------------------+-------------------------+----------------------+
 * | Service VM vCPU 0     | Service VM vCPU x       | User VM vCPU y       |
 * +=======================+=========================+======================+
 * |                       |                         | Hypervisor:          |
 * |                       |                         |                      |
 * |                       |                         | - Fill in type,      |
 * |                       |                         |   addr, etc.         |
 * |                       |                         | - Pause User VM vCPU |
 * |                       |                         | - Set state to       |
 * |                       |                         |   PENDING (a)        |
 * |                       |                         | - Fire upcall to     |
 * |                       |                         |   Service VM vCPU 0  |
 * |                       |                         |                      |
 * +-----------------------+-------------------------+----------------------+
 * | HSM:                  |                         |                      |
 * |                       |                         |                      |
 * | - Scan for pending    |                         |                      |
 * |   requests            |                         |                      |
 * | - Set state to        |                         |                      |
 * |   PROCESSING (b)      |                         |                      |
 * | - Assign requests to  |                         |                      |
 * |   clients (c)         |                         |                      |
 * |                       |                         |                      |
 * +-----------------------+-------------------------+----------------------+
 * |                       | Client:                 |                      |
 * |                       |                         |                      |
 * |                       | - Scan for assigned     |                      |
 * |                       |   requests              |                      |
 * |                       | - Handle the            |                      |
 * |                       |   requests (d)          |                      |
 * |                       | - Set state to COMPLETE |                      |
 * |                       | - Notify the hypervisor |                      |
 * |                       |                         |                      |
 * +-----------------------+-------------------------+----------------------+
 * |                       | Hypervisor:             |                      |
 * |                       |                         |                      |
 * |                       | - resume User VM vCPU y |                      |
 * |                       |   (e)                   |                      |
 * |                       |                         |                      |
 * +-----------------------+-------------------------+----------------------+
 * |                       |                         | Hypervisor:          |
 * |                       |                         |                      |
 * |                       |                         | - Post-work (f)      |
 * |                       |                         | - set state to FREE  |
 * |                       |                         |                      |
 * +-----------------------+-------------------------+----------------------+
 *
 * @endverbatim
 *
 * Note that the following shall hold.
 *
 *   1. (a) happens before (b)
 *   2. (c) happens before (d)
 *   3. (e) happens before (f)
 *   4. One vCPU cannot trigger another I/O request before the previous one has
 *      completed (i.e. the state switched to FREE)
 *
 * Accesses to the state of a acrn_io_request shall be atomic and proper barriers
 * are needed to ensure that:
 *
 *   1. Setting state to PENDING is the last operation when issuing a request in
 *      the hypervisor, as the hypervisor shall not access the request any more.
 *
 *   2. Due to similar reasons, setting state to COMPLETE is the last operation
 *      of request handling in HSM or clients in Service VM.
 *
 */
struct acrn_io_request {
	/**
	 * @brief Type of this request.
	 *
	 * Byte offset: 0.
	 */
	uint32_t type;

	/**
	 * @brief Hypervisor will poll completion if set.
	 *
	 * Byte offset: 4.
	 */
	uint32_t completion_polling;

	/**
	 * @brief Reserved.
	 *
	 * Byte offset: 8.
	 */
	uint32_t reserved0[14];

	/**
	 * @brief Details about this request.
	 *
	 * Byte offset: 64.
	 */
	union {
		struct acrn_pio_request		pio_request;  /**< PIO request header */
		struct acrn_pci_request		pci_request;  /**< PCI request header */
		struct acrn_mmio_request	mmio_request; /**< MMIO request header */
		uint64_t			data[8];  /**< Request content */
	} reqs;

	/**
	 * @brief Reserved.
	 *
	 * Byte offset: 128.
	 */
	uint32_t reserved1;

	/**
	 * @brief If this request has been handled by HSM driver.
	 *
	 * Byte offset: 132.
	 */
	int32_t kernel_handled;

	/**
	 * @brief The status of this request.
	 *
	 * Taking ACRN_IOREQ_STATE_xxx as values.
	 *
	 * Byte offset: 136.
	 */
	uint32_t processed;
} __aligned(256);

/**
 * @brief The I/O requests.
 *
 * This union is used to store multiple I/O requests.
 */
union acrn_io_request_buffer {
	struct acrn_io_request	req_slot[ACRN_IO_REQUEST_MAX];  /**< IO requests slots */
	int8_t			reserved[4096];  /**< Reserved */
};

/**
 * @brief Info for asynchronous I/O operations.
 *
 * Used to register I/O register regions for asynchronous I/O polling.
 */
struct acrn_asyncio_info {
	uint32_t type;        /**< Type of the I/O operation (ACRN_ASYNCIO_PIO or ACRN_ASYNCIO_MMIO) */
	/**
	 * @brief Indicates whether data matching is required during I/O event checks.
	 *
	 * 0 means that data matching can be skipped. Otherwise, it is required.
	 */
	uint32_t match_data;
	uint64_t addr;        /**< Base address of the I/O region */
	uint64_t fd;          /**< Eventfd descriptor for notifications */
	uint64_t data;        /**< The data to be checked when data matching is required */
};

/**
 * @brief Info to create a VM, the parameter for HC_CREATE_VM hypercall.
 *
 * The VM creation info is used to create a VM with specified vCPUs, name, and flags.
 */
struct acrn_vm_creation {

	/* created vmid return to HSM. Keep it first field */
	uint16_t vmid;      /**< Assigned VM ID */

	uint16_t reserved0; /**< Reserved */

	uint16_t vcpu_num;  /**< VCPU numbers this VM want to create */

	uint16_t reserved1; /**< Reserved */

	uint8_t	 name[MAX_VM_NAME_LEN];  /**< VM name string */

	uint64_t vm_flag;   /**< Guest flags */

	uint64_t ioreq_buf; /**< GPA of I/O request buffer */

	/*
	 *   The least significant set bit is the PCPU # the VCPU 0 maps to;
	 *   second set least significant bit is the PCPU # the VCPU 1 maps to;
	 *   and so on...
	 */
	uint64_t cpu_affinity; /**< Bitmap of allowed physical CPUs */
};

/**
 * @brief General-purpose registers layout.
 *
 * Mirrors the general-purpose registers. Refer to Chapter 28.6 Loading MSRs, Vol.3, SDM 325462-078 for more details.
 */
struct acrn_gp_regs {
	uint64_t rax;  /**< Value in the RAX register */
	uint64_t rcx;  /**< Value in the RCX register */
	uint64_t rdx;  /**< Value in the RDX register */
	uint64_t rbx;  /**< Value in the RBX register */
	uint64_t rsp;  /**< Value in the RSP register */
	uint64_t rbp;  /**< Value in the RBP register */
	uint64_t rsi;  /**< Value in the RSI register */
	uint64_t rdi;  /**< Value in the RDI register */
	uint64_t r8;   /**< Value in the R8 register */
	uint64_t r9;   /**< Value in the R9 register */
	uint64_t r10;  /**< Value in the R10 register */
	uint64_t r11;  /**< Value in the R11 register */
	uint64_t r12;  /**< Value in the R12 register */
	uint64_t r13;  /**< Value in the R13 register */
	uint64_t r14;  /**< Value in the R14 register */
	uint64_t r15;  /**< Value in the R15 register */
};

/**
 * @brief Descriptor table pointer.
 *
 * This structure is to define the descriptor table pointer, which is used to store the limit and base address of the
 * descriptor table. Refer to Chapter 3.5.1 Segment Descriptor Tables, Vol.3, SDM 325462-078 for more details.
 *
 * @remark This structure shall be packed.
 */
struct acrn_descriptor_ptr {
	uint16_t limit;  /**< Table limit */
	uint64_t base;   /**< 64-bit linear address of the table */
	uint16_t reserved[3];   /**< Align struct size to 64bit */
} __packed;

/**
 * @brief registers info for vCPU.
 *
 * This structure is used to store the register information of a vCPU.
 */
struct acrn_regs {
	struct acrn_gp_regs gprs;        /**< Values in guest general-purpose registers */
	struct acrn_descriptor_ptr gdt;  /**< Value in the guest global descriptor table register (GDTR) */
	struct acrn_descriptor_ptr idt;  /**< Value in the guest interrupt descriptor table register (IDTR) */

	uint64_t rip;               /**< Value in guest RIP */
	uint64_t cs_base;           /**< Value in the 'base address' field of the guest CS hidden part */
	uint64_t cr0;               /**< Value in guest CR0 */
	uint64_t cr4;               /**< Value in guest CR4 */
	uint64_t cr3;               /**< Value in guest CR3 */
	uint64_t ia32_efer;         /**< Value in guest IA32_EFER */
	uint64_t rflags;            /**< Value in guest RFLAGS */
	uint64_t reserved_64[4];    /**< Reserved for future extensions */

	uint32_t cs_ar;             /**< Value in the 'access information' field of the CS hidden part */
	uint32_t cs_limit;          /**< Value in the 'limit' field of the CS hidden part */
	uint32_t reserved_32[3];    /**< Reserved for future extensions */

	/* don't change the order of following sel */
	uint16_t cs_sel;            /**< The value in the guest CS selector */
	uint16_t ss_sel;            /**< The value in the guest SS selector */
	uint16_t ds_sel;            /**< The value in the guest DS selector */
	uint16_t es_sel;            /**< The value in the guest ES selector */
	uint16_t fs_sel;            /**< The value in the guest FS selector */
	uint16_t gs_sel;            /**< The value in the guest GS selector */
	uint16_t ldt_sel;           /**< The value in the guest LDTR selector */
	uint16_t tr_sel;            /**< The value in the guest TR selector */
};

/**
 * @brief Info to set vCPU state
 *
 * the parameter for HC_SET_VCPU_STATE
 */
struct acrn_vcpu_regs {
	uint16_t vcpu_id;      /**< The virtual CPU ID for the vCPU to set state */

	uint16_t reserved[3];  /**< Reserved space to make cpu_state aligned to 8 bytes */

	struct acrn_regs vcpu_regs; /**< The structure to hold vCPU state */
};

/* Operation types for setting IRQ line */
#define GSI_SET_HIGH        0U  /**< Set the IRQ line to high */
#define GSI_SET_LOW         1U  /**< Set the IRQ line to low */
#define GSI_RAISING_PULSE   2U  /**< Generate a rising edge pulse on the IRQ line */
#define GSI_FALLING_PULSE   3U  /**< Generate a falling edge pulse on the IRQ line */

/**
 * @brief Info to Set/Clear/Pulse a virtual IRQ line for a VM
 *
 * the parameter for HC_SET_IRQLINE hypercall
 */
struct acrn_irqline_ops {
	uint32_t gsi; /**< The GSI number of the IRQ line */
	uint32_t op;  /**< The operation to perform on the IRQ line */
};


/**
 * @brief Info to inject a MSI interrupt to VM
 *
 * the parameter for HC_INJECT_MSI hypercall
 */
struct acrn_msi_entry {
	uint64_t msi_addr;  /**< MSI addr[19:12] with destination vCPU ID */

	uint64_t msi_data;  /**< MSI data[7:0] with vector */
};

#define SPACE_SYSTEM_MEMORY     0U  /**< System memory address space. */
#define SPACE_SYSTEM_IO         1U  /**< System I/O address space. */
#define SPACE_PCI_CONFIG        2U  /**< PCI configuration space. */
#define SPACE_Embedded_Control  3U  /**< Embedded controller address space. */
#define SPACE_SMBUS             4U  /**< SMBus address space. */
#define SPACE_PLATFORM_COMM     10U /**< Platform communication address space. */
#define SPACE_FFixedHW          0x7FU /**< Fixed hardware address space. */

/**
 * @brief ACPI Generic Address Structure (GAS)
 *
 * This structure is used to define the ACPI Generic Address Structure (GAS), which is used to describe the address
 * space for a device.
 *
 * @remark This structure shall be packed.
 */
struct acrn_acpi_generic_address {
	uint8_t 	space_id;   /**< The address space where the register resides */
	uint8_t 	bit_width;  /**< The bit width of the register */
	uint8_t 	bit_offset; /**< The bit offset within the register */
	uint8_t 	access_size; /**< The access size for the register */
	uint64_t	address;    /**< The address of the register */
} __attribute__ ((__packed__));

/**
 * @brief CPU frequency policy types
 *
 * This enumeration defines the different CPU frequency policy types that can be applied to a vCPU.
 */
enum acrn_cpufreq_policy_type {
	CPUFREQ_POLICY_PERFORMANCE,  /**< Performance-oriented frequency policy */
	CPUFREQ_POLICY_NOMINAL,      /**< Nominal frequency policy */
};

/**
 * @brief CPU frequency limits
 *
 * This structure holds the limits for CPU frequency, including performance levels for Hardware-Controlled Performance
 * States (HWP) indices.
 */
struct acrn_cpufreq_limits {
	/* Performance levels for HWP */
	uint8_t guaranteed_hwp_lvl; /**< Guaranteed performance level for HWP */
	uint8_t highest_hwp_lvl;    /**< Highest performance level for HWP */
	uint8_t lowest_hwp_lvl;     /**< Lowest performance level for HWP */
};

/**
 * @brief ACPI Sx package.
 *
 * This structure represents the ACPI Sx package, which contains values for the PM1a and PM1b registers.
 *
 * @alignment 8
 */
struct acpi_sx_pkg {
	uint8_t		val_pm1a;  /**< Value for the PM1a register */
	uint8_t		val_pm1b;  /**< Value for the PM1b register */
	uint16_t	reserved;  /**< Reserved for alignment */
} __aligned(8);

/**
 * @brief Power management S-state data
 *
 * This structure holds information about the power management S-states of a vCPU.
 *
 * @alignment 8
 */
struct pm_s_state_data {
	struct acrn_acpi_generic_address pm1a_evt; /**< The ACPI GAS for the PM1a event register */
	struct acrn_acpi_generic_address pm1b_evt; /**< The ACPI GAS for the PM1b event register */
	struct acrn_acpi_generic_address pm1a_cnt; /**< The ACPI GAS for the PM1a control register */
	struct acrn_acpi_generic_address pm1b_cnt; /**< The ACPI GAS for the PM1b control register */
	struct acpi_sx_pkg s3_pkg;  /**< The ACPI Sx package for the S3 state */
	struct acpi_sx_pkg s5_pkg;  /**< The ACPI Sx package for the S5 state */
	uint32_t *wake_vector_32;   /**< 32-bit wake vector for the VM */
	uint64_t *wake_vector_64;   /**< 64-bit wake vector for the VM */
	uint32_t pm1a_cnt_ready;    /**< Whether pm1a_cnt is already written */
} __aligned(8);

/**
 * @defgroup PMCMD Info PM command from DM/HSM.
 *
 * The command would specify request type(e.g. get px count or data) for specific VM and specific vCPU with specific
 * state number.
 * ACRN supports only HWP and MONITOR/MWAIT platforms; legacy Px and Cx states are unsupported.
 * This command remains for compatibility.
 * @{
 */
#define PMCMD_VMID_MASK		0xff000000U  /**< Bitmask to extract the VM ID from the PM command */
#define PMCMD_TYPE_MASK		0x000000ffU  /**< Bitmask to extract the command type from the PM command */
#define PMCMD_VMID_SHIFT	24U     /**< Bit shift value to position the VM ID in the PM command */
/**
 * @}
 */

/**
 * @brief Power management command types.
 *
 * This enumeration defines the different types of power management commands.
 */
enum acrn_pm_cmd_type {
	ACRN_PMCMD_GET_PX_CNT,  /**< Get the count of P-states */
	ACRN_PMCMD_GET_PX_DATA, /**< Get the data of P-states */
	ACRN_PMCMD_GET_CX_CNT,  /**< Get the count of C-states */
	ACRN_PMCMD_GET_CX_DATA, /**< Get the data of C-states */
};

/**
 * @brief Maximum number of passthrough devices.
 *
 * Predefined maximum number of passthrough devices for each VM when getting a VM interrupt count data. For each
 * passthrough device, its allocated physical interrupt along with its interrupt count are recorded.
 */
#define MAX_PTDEV_NUM 24U

/**
 * @brief Interrupt monitor structure
 *
 * This structure is used to manage and store information related to interrupt monitoring for a VM.
 *
 * @alignment 8
 */
struct acrn_intr_monitor {
	uint32_t cmd;     /**< Sub command for interrupt monitor */
	uint32_t buf_cnt; /**< The count of this buffer to save */
	uint64_t buffer[MAX_PTDEV_NUM * 2];  /**< The buffer which save each interrupt count */
} __aligned(8);

/**
 * @defgroup INTR_CMD Command for interrupt monitor.
 *
 * These commands are used to manage the interrupt monitor for a VM.
 *
 * @{
 */
#define INTR_CMD_GET_DATA 0U   /**< Retrieve the current interrupt data */
#define INTR_CMD_DELAY_INT 1U  /**< Delay the interrupt */

/**
 * @}
 */

/*
 * PRE_LAUNCHED_VM is launched by ACRN hypervisor, with LAPIC_PT;
 * Service VM is launched by ACRN hypervisor, without LAPIC_PT;
 * POST_LAUNCHED_VM is launched by ACRN devicemodel, with/without LAPIC_PT depends on usecases.
 *
 * Assumption: vm_configs array is completely initialized w.r.t. load_order member of
 * 		acrn_vm_config for all the VMs.
 */

 /**
  * @brief ACRN VM load order types
  *
  * This enumeration defines the different types of ACRN VM load orders. Pre-launched VMs are launched by the ACRN
  * hypervisor first. Service VMs are launched by the ACRN hypervisor, and post-launched VMs are launched by the ACRN
  * devicemodel at the last.
  */
enum acrn_vm_load_order {
	PRE_LAUNCHED_VM = 0, /**< Pre-launched VM */
	SERVICE_VM,          /**< Service VM */
	POST_LAUNCHED_VM,	 /**< Post-launched VM */
	MAX_LOAD_ORDER       /**< Maximum number of load order types */
};

/**
 * @brief VM configuration header
 *
 * This structure holds the header information for a VM configuration, including the load order, name, severity, CPU
 * affinity, and guest flags.
 *
 * @alignment 8
 */
struct acrn_vm_config_header {
       enum acrn_vm_load_order load_order; /**< The load order of the VM */
       char name[MAX_VM_NAME_LEN];  /**< The name of the VM */
       uint8_t reserved[2];    /**< Reserved */
       uint8_t severity;       /**< The severity level of the VM */
       uint64_t cpu_affinity;  /**< The CPU affinity of the VM */
       uint64_t guest_flags;   /**< The guest flags for the VM */
       /*
        * The following are hv-specific members and are thus opaque.
        * vm_config_entry_size determines the real size of this structure.
        */
} __aligned(8);

/**
 * @brief Info to configure virtual root port.
 *
 * Configuration passed to hypervisor when adding a virtual root port which is used as PTM root.
 */
struct vrp_config {
	uint16_t phy_bdf;    /**< The physical BDF of the root port */
	uint8_t max_payload; /**< The maximum payload size for the device */
	uint8_t primary_bus; /**< The primary bus number for the root port */
	uint8_t secondary_bus;   /**< The secondary bus number for the root port */
	uint8_t subordinate_bus; /**< The subordinate bus number for the root port */
	uint8_t ptm_capable; /**< Whether the root port is PTM capable */
	uint32_t ptm_cap_offset; /**< The offset of the PTM capability in the PCI configuration space */
};

/* Type of PCI device assignment */
#define ACRN_PTDEV_QUIRK_ASSIGN	(1U << 0)  /**< PCI device assignment quirk */

#define ACRN_PCI_NUM_BARS	6U  /**< Number of BARs in a PCI device */

/**
 * @brief Info to assign or deassign PCI for a VM
 *
 * This structure holds the information required to assign or deassign a PCI device to/from a VM.
 */
struct acrn_pcidev {
	uint32_t type;     /**< the type of the the pass-through PCI device */

	uint16_t virt_bdf; /**< virtual BDF# of the pass-through PCI device */

	uint16_t phys_bdf; 	/**< physical BDF# of the pass-through PCI device */

	/**
	 * @brief The PCI interrupt line
	 *
	 * The PCI Interrupt Line, initialized by ACRN-DM, which is RO and ideally not used for pass-through
	 * MSI/MSI-x devices.
	 */
	uint8_t intr_line;

	/**
	 * @brief The PCI interrupt pin
	 *
	 * The PCI Interrupt Pin, initialized by ACRN-DM, which is RO and ideally not used for pass-through
	 * MSI/MSI-x devices.
	 */
	uint8_t intr_pin;

	uint32_t bar[ACRN_PCI_NUM_BARS]; /**< the base address of the PCI BAR, initialized by ACRN-DM. */
};

#define MMIODEV_RES_NUM        3  /**< Number of MMIO resources for a device */

/**
 * @brief The MMIO resources for the MMIO device.
 */
struct acrn_mmiores {
	uint64_t user_vm_pa;  /**< the gpa of the MMIO region for the MMIO device */

	/**
	 * @brief The HPA of the MMIO region for the MMIO device:
	 *
	 * For post-launched VM it's PA in service vm; for pre-launched VM it's PA in HV.
	 */
	uint64_t host_pa;
	uint64_t size; /**< The size of the MMIO region for the MMIO resource */
	uint64_t mem_type; /**< The memory type of the MMIO region for the MMIO resource */
};

/**
 * @brief Info to assign or deassign a MMIO device for a VM
 *
 * This structure holds the information required to assign or deassign an MMIO device to/from a VM.
 */
struct acrn_mmiodev {
	char name[8];   /**< The name of the MMIO device */

	/**
	 * @brief The MMIO resources for the MMIO device.
	 */
	struct acrn_mmiores res[MMIODEV_RES_NUM];
};

/**
 * @brief Info to create or destroy a virtual PCI or legacy device for a VM.
 *
 * the parameter for HC_CREATE_VDEV or HC_DESTROY_VDEV hypercall
 */
struct acrn_vdev {
	/**
	 * @brief the identifier of the device
	 *
	 * The low 32 bits represent the vendor ID and device ID of PCI device and the high 32 bits represent the
	 * device number of the legacy device.
	 */
	union {
		uint64_t value;  /**< The value of the device identifier */
		/**
		 * @brief The fields of the device identifier.
		 */
		struct {
			uint16_t vendor;    /**< The vendor ID of the PCI device */
			uint16_t device;    /**< The device ID of the PCI device */
			uint32_t legacy_id; /**< The legacy device ID */
		} fields;
	} id;

	/**
	 * @brief The slot of the device
	 *
	 * If the device is a PCI device, the slot represents BDF, otherwise it represents legacy device slot number.
	 */
	uint64_t slot;

	uint32_t io_addr[ACRN_PCI_NUM_BARS]; /**< The IO resource address of the device, initialized by ACRN-DM */

	uint32_t io_size[ACRN_PCI_NUM_BARS]; /**< The IO resource size of the device, initialized by ACRN-DM */

	uint8_t	args[128]; /**< The options for the virtual device, initialized by ACRN-DM. */
};

#define ACRN_ASYNCIO_PIO	(0x01U)  /**< PIO type for asynchronous I/O */
#define ACRN_ASYNCIO_MMIO	(0x02U)  /**< MMIO type for asynchronous I/O */

#define SBUF_MAGIC	0x5aa57aa71aa13aa3UL  /**< Magic number for the shared buffer */
#define SBUF_MAX_SIZE	(1UL << 22U)      /**< Maximum size of the shared buffer */
#define SBUF_HEAD_SIZE	64U               /**< Size of the shared buffer header */

/**
 * @defgroup OVERRUN/OVERWRITE Configuration flags for shared buffer.
 *
 * These flags are used to configure the behavior of the shared buffer (sbuf). They control features such as overrun
 * counting and overwrite capability.
 *
 * @{
 */

/**
 * @brief Enable overrun counting.
 *
 * This flag is used to enable overrun counting in the shared buffer (sbuf).
 * When this flag is set, the buffer will count the number of times it has
 * been overrun.
 */
#define OVERRUN_CNT_EN	(1U << 0U)

/**
 * @brief Enable overwrite.
 *
 * This flag is used to enable the overwrite capability in the shared buffer (sbuf).
 * When this flag is set, the buffer will overwrite the oldest data when it becomes full.
 */
#define OVERWRITE_EN	(1U << 1U)
/**
 * @}
 */

/*
 * (sbuf) head + buf (store (ele_num - 1) elements at most)
 * buffer empty: tail == head
 * buffer full:  (tail + ele_size) % size == head
 *
 *             Base of memory for elements
 *                |
 *                |
 * ----------------------------------------------------------------------
 * | struct shared_buf | raw data (ele_size)| ... | raw data (ele_size) |
 * ----------------------------------------------------------------------
 * |
 * |
 * struct shared_buf *buf
 */

/**
 * @brief Enumeration of shared buffer (sbuf) IDs.
 *
 * This enumeration defines the IDs for different types of shared buffers (sbufs) used in the system.
 * Some sbufs (whose ID is less than ACRN_SBUF_PER_PCPU_ID_MAX) are created for each physical CPU (pcpu),
 * while others are created for specific purposes such as asynchronous I/O or VM events.
 */
enum {
	ACRN_TRACE = 0U,          	/**< ID for trace buffer */
	ACRN_HVLOG,               	/**< ID for hypervisor log buffer */
	ACRN_SEP,                 	/**< ID for SEP (System Event Processor) buffer */
	ACRN_SOCWATCH,            	/**< ID for SOCWATCH buffer */
	/* The sbuf with above ids are created each pcpu */
	ACRN_SBUF_PER_PCPU_ID_MAX,	/**< Maximum ID for per-pcpu sbufs */
	ACRN_ASYNCIO = 64,		/**< ID for asynchronous I/O buffer, which is 64. */
	ACRN_VM_EVENT, 			/**< ID for VM event buffer */
};

/**
 * @brief Structure representing a shared buffer.
 *
 * This structure defines a shared buffer (sbuf) used for communication between
 * Service VM and hypervisor. It includes metadata about the buffer, such as the number
 * and size of elements, head and tail pointers, flags, and overrun count.
 *
 * @remark Make sure sizeof(struct shared_buf) == SBUF_HEAD_SIZE
 */
struct shared_buf {
	uint64_t magic;        /**< Magic number to identify the buffer */
	uint32_t ele_num;      /**< Number of elements in the buffer */
	uint32_t ele_size;     /**< Size of each element in the buffer */
	uint32_t head;         /**< Offset from base to read (head pointer) */
	uint32_t tail;         /**< Offset from base to write (tail pointer) */
	uint32_t flags;        /**< Flags for buffer configuration and status */
	uint32_t reserved;     /**< Reserved for future use */
	uint32_t overrun_cnt;  /**< Count of buffer overruns */
	uint32_t size;         /**< Total size of the buffer (ele_num * ele_size) */
	uint32_t padding[6];   /**< Padding to align the structure size */
};

/*
 * VM event architecture:
 * +------------------------------------------------------+
 * |    Service VM                                        |
 * | +----------------------------+                       |
 * | | DM  +--------------------+ |                       |
 * | |     | [ event source ]   | |                       |
 * | |     +-+------------+-----+ |                       |
 * | |       | (eventfd)  |(sbuf) |                       |
 * | |       v            v       |                       |
 * | | +------------------------+ | (socket)  +---------+ |
 * | | |  [event deliver logic] |-+---------->| Libvirt | |
 * | | +------------------------+ |           +---------+ |
 * | |       ^            ^       |                       |
 * | +-------|------------|-------+                       |
 * |         | (eventfd)  | (sbuf)                        |
 * +---------|------------|-------------------------------+
 * +---------|------------|-------------------------------+
 * | kernel [HSM]         |                               |
 * |         ^            |                               |
 * +---------|------------|-------------------------------+
 *           |upcall      |
 * +---------+------------+-------------------------------+
 * | HV     [ event source ]                              |
 * +------------------------------------------------------+
 *
 * For event sources in HV
 *  - HV puts the event in the shared ring sbuf.
 *  - The hypervisor notifies the service VM via upcall.
 *  - HSM in the service VM notifies device model via eventfd.
 *  - The device model fetches and handles events from the shared ring sbuf.
 * For event sources in DM
 *  - DM puts the event in the DM event ring sbuf.
 *  - DM notifies event delivery logic via eventfd.
 *  - The event delivery logic fetches and handles events from the DM event ring sbuf.
 */

#define VM_EVENT_RTC_CHG	0U  /**< RTC change event */
#define VM_EVENT_POWEROFF	1U  /**< VM poweroff event */
#define VM_EVENT_TRIPLE_FAULT	2U /**< VM triple fault event */

#define VM_EVENT_COUNT		3U  /**< Total number of VM event types */

#define VM_EVENT_DATA_LEN	28U /**< Length of the event data */

/**
 * @brief VM event structure
 */
struct vm_event {
	uint32_t type;  /**< The type of the VM event */
	uint8_t event_data[VM_EVENT_DATA_LEN];  /**< The data associated with the event */
};

/**
 * @brief RTC change event data.
 *
 * This structure holds the data for an RTC change event, including the last known time and the delta
 * time since the last change.
 */
struct rtc_change_event_data {
	int64_t	last_time;  /**< Time (in seconds) of the RTC before been set */
	int64_t delta_time; /**< \p delta of time (in seconds) the RTC has been changed */
};

/* DM vrtc's reference time is besed on sys time, while the HV vrtc is based on pRTC.
 * When the delta time is sent to users, they need to know what it is relative to.
 */
#define RTC_CHG_RELATIVE_PHYSICAL_RTC         0  /**< RTC change relative to the physical RTC change */
#define RTC_CHG_RELATIVE_SERVICE_VM_SYS_TIME  1  /**< RTC change relative to the SOS system time change */

#endif /* ACRN_COMMON_H */

/**
 * @}
 */
