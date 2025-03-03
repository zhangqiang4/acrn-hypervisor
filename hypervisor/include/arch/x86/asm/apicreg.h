/*-
 * Copyright (c) 1996, by Peter Wemm and Steve Passe
 * Copyright (c) 2017-2025 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the developer may NOT be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 *
 * $FreeBSD$
 */

#ifndef APICREG_H
#define APICREG_H

#include <asm/page.h>

/**
 * @addtogroup hwmgmt_apic
 *
 * @{
 */

/**
 * @file apicreg.h
 *
 * @brief Local and IOAPIC definitions
 *
 * This header file contains definitions and structures related to both Local APIC and IOAPIC
 * configurations. It provides the necessary declarations for managing APIC registers, including
 * offsets, data structures, and macros for controlling and accessing APIC functionalities in
 * multiprocessor systems.
 *
 */

/**
 * @brief Structure representing a Local APIC register.
 *
 * This structure is used to represent a register within the Local APIC.
 * It provides fields for the register value and extended value, which are used in x2APIC mode.
 *
 * @remark This structure is used internally to manage Local APIC register values.
 *
 */
struct lapic_reg {
	uint32_t v; /**< Register value */
	uint32_t v_ext; /**< Extended register value, used for ICR high in x2APIC mode */
	uint32_t pad[2]; /**< Padding for alignment */
};

/**
 * @brief Structure representing the set of Local APIC registers.
 *
 * This structure encapsulates all the registers of a Local APIC, providing access to various control and status
 * registers. It is used to manage the state and configuration of the Local APIC in a multiprocessor system.
 *
 * @remark This structure is used internally to preserve and manipulate Local APIC state.
 * @alignment 4096
 *
 */
struct lapic_regs { /* OFFSET(Hex) */
	struct lapic_reg rsv0[2]; /**< Reserved registers */
	struct lapic_reg id; /**< APIC ID register (offset: 0x020) */
	struct lapic_reg version; /**< APIC version register (offset: 0x30) */
	struct lapic_reg rsv1[4]; /**< Reserved registers */
	struct lapic_reg tpr; /**< Task Priority Register (offset: 0x80) */
	struct lapic_reg apr; /**< Arbitration Priority Register (offset: 0x90) */
	struct lapic_reg ppr; /**< Processor Priority Register (offset: 0xA0) */
	struct lapic_reg eoi; /**< End Of Interrupt register (offset: 0xB0) */
	struct lapic_reg rrd; /**< Remote Read register (offset: 0xC0) */
	struct lapic_reg ldr; /**< Logical Destination Register (offset: 0xD0) */
	struct lapic_reg dfr; /**< Destination Format Register (offset: 0xEO) */
	struct lapic_reg svr; /**< Spurious Interrupt Vector Register (offset: 0xF0) */
	struct lapic_reg isr[8]; /**< In-Service Register (offset: 0x100 to 0x170) */
	struct lapic_reg tmr[8]; /**< Trigger Mode Register (offset: 0x180 to 0x1F0) */
	struct lapic_reg irr[8]; /**< Interrupt Request Register (offset: 0x200 to 0x270) */
	struct lapic_reg esr; /**< Error Status Register (offset: 0x280) */
	struct lapic_reg rsv2[6]; /**< Reserved registers */
	struct lapic_reg lvt_cmci; /**< LVT Corrected Machine Check Interrupt register (offset: 0x2F0) */
	struct lapic_reg icr_lo; /**< Interrupt Command Register low (offset: 0x300) */
	struct lapic_reg icr_hi; /**< Interrupt Command Register high (offset: 0x310) */
	struct lapic_reg lvt[6]; /**< Local Vector Table registers (offset: 0x320 to 0x370) */
	struct lapic_reg icr_timer; /**< Timer Initial Count Register (offset: 0x380) */
	struct lapic_reg ccr_timer; /**< Timer Current Count Register (offset: 0x390) */
	struct lapic_reg rsv3[4]; /**< Reserved registers */
	struct lapic_reg dcr_timer; /**< Timer Divide Configuration Register (offset: 0x3E0) */
	struct lapic_reg self_ipi; /**< Self IPI register (offset: 0x3F0) */

	/* Roundup sizeof current struct to 4KB */
	struct lapic_reg rsv5[192]; /**< Reserved registers (offset: 0x400 to 0xFF0) */
} __aligned(PAGE_SIZE);

/**
 * @brief Enumeration of Local APIC register offsets.
 *
 * This enumeration defines the offsets for various registers within the Local APIC. These offsets are used to
 * access specific registers for configuration and status checking.
 *
 * @remark This enumeration is used internally to reference Local APIC register offsets.
 */
enum LAPIC_REGISTERS {
	LAPIC_ID = 0x2, /**< APIC ID register offset */
	LAPIC_VERSION      = 0x3, /**< APIC version register offset */
	LAPIC_TPR          = 0x8, /**< Task Priority Register offset */
	LAPIC_APR          = 0x9, /**< Arbitration Priority Register offset */
	LAPIC_PPR          = 0xa, /**< Processor Priority Register offset */
	LAPIC_EOI          = 0xb, /**< End Of Interrupt register offset */
	LAPIC_LDR          = 0xd, /**< Logical Destination Register offset */
	LAPIC_DFR          = 0xe, /**< Destination Format Register offset (Not in x2APIC) */
	LAPIC_SVR          = 0xf, /**< Spurious Interrupt Vector Register offset */
	LAPIC_ISR0         = 0x10, /**< In-Service Register 0 offset */
	LAPIC_ISR1         = 0x11, /**< In-Service Register 1 offset */
	LAPIC_ISR2         = 0x12, /**< In-Service Register 2 offset */
	LAPIC_ISR3         = 0x13, /**< In-Service Register 3 offset */
	LAPIC_ISR4         = 0x14, /**< In-Service Register 4 offset */
	LAPIC_ISR5         = 0x15, /**< In-Service Register 5 offset */
	LAPIC_ISR6         = 0x16, /**< In-Service Register 6 offset */
	LAPIC_ISR7         = 0x17, /**< In-Service Register 7 offset */
	LAPIC_TMR0         = 0x18, /**< Trigger Mode Register 0 offset */
	LAPIC_TMR1         = 0x19, /**< Trigger Mode Register 1 offset */
	LAPIC_TMR2         = 0x1a, /**< Trigger Mode Register 2 offset */
	LAPIC_TMR3         = 0x1b, /**< Trigger Mode Register 3 offset */
	LAPIC_TMR4         = 0x1c, /**< Trigger Mode Register 4 offset */
	LAPIC_TMR5         = 0x1d, /**< Trigger Mode Register 5 offset */
	LAPIC_TMR6         = 0x1e, /**< Trigger Mode Register 6 offset */
	LAPIC_TMR7         = 0x1f, /**< Trigger Mode Register 7 offset */
	LAPIC_IRR0         = 0x20, /**< Interrupt Request Register 0 offset */
	LAPIC_IRR1         = 0x21, /**< Interrupt Request Register 1 offset */
	LAPIC_IRR2         = 0x22, /**< Interrupt Request Register 2 offset */
	LAPIC_IRR3         = 0x23, /**< Interrupt Request Register 3 offset */
	LAPIC_IRR4         = 0x24, /**< Interrupt Request Register 4 offset */
	LAPIC_IRR5         = 0x25, /**< Interrupt Request Register 5 offset */
	LAPIC_IRR6         = 0x26, /**< Interrupt Request Register 6 offset */
	LAPIC_IRR7         = 0x27, /**< Interrupt Request Register 7 offset */
	LAPIC_ESR          = 0x28, /**< Error Status Register offset */
	LAPIC_LVT_CMCI     = 0x2f, /**< LVT Corrected Machine Check Interrupt register offset */
	LAPIC_ICR_LO       = 0x30, /**< Interrupt Command Register low offset */
	LAPIC_ICR_HI       = 0x31, /**< Interrupt Command Register high offset (Not in x2APIC) */
	LAPIC_LVT_TIMER    = 0x32, /**< Local Vector Table Timer register offset */
	LAPIC_LVT_THERMAL  = 0x33, /**< Local Vector Table Thermal register offset */
	LAPIC_LVT_PCINT    = 0x34, /**< Local Vector Table Performance Counter Interrupt register offset */
	LAPIC_LVT_LINT0    = 0x35, /**< Local Vector Table LINT0 register offset */
	LAPIC_LVT_LINT1    = 0x36, /**< Local Vector Table LINT1 register offset */
	LAPIC_LVT_ERROR    = 0x37, /**< Local Vector Table Error register offset */
	LAPIC_ICR_TIMER    = 0x38, /**< Timer Initial Count Register offset */
	LAPIC_CCR_TIMER    = 0x39, /**< Timer Current Count Register offset */
	LAPIC_DCR_TIMER    = 0x3e, /**< Timer Divide Configuration Register offset */
	LAPIC_SELF_IPI     = 0x3f, /**< Self IPI register offset (Only in x2APIC) */
	LAPIC_EXT_FEATURES = 0x40, /**< Extended Features Register offset (AMD) */
	LAPIC_EXT_CTRL     = 0x41, /**< Extended Control Register offset (AMD) */
	LAPIC_EXT_SEOI     = 0x42, /**< Extended End Of Interrupt Register offset (AMD) */
	LAPIC_EXT_IER0     = 0x48, /**< Extended Interrupt Enable Register 0 offset (AMD) */
	LAPIC_EXT_IER1     = 0x49, /**< Extended Interrupt Enable Register 1 offset (AMD) */
	LAPIC_EXT_IER2     = 0x4a, /**< Extended Interrupt Enable Register 2 offset (AMD) */
	LAPIC_EXT_IER3     = 0x4b, /**< Extended Interrupt Enable Register 3 offset (AMD) */
	LAPIC_EXT_IER4     = 0x4c, /**< Extended Interrupt Enable Register 4 offset (AMD) */
	LAPIC_EXT_IER5     = 0x4d, /**< Extended Interrupt Enable Register 5 offset (AMD) */
	LAPIC_EXT_IER6     = 0x4e, /**< Extended Interrupt Enable Register 6 offset (AMD) */
	LAPIC_EXT_IER7     = 0x4f, /**< Extended Interrupt Enable Register 7 offset (AMD) */
	LAPIC_EXT_LVT0     = 0x50, /**< Extended Local Vector Table 0 offset (AMD) */
	LAPIC_EXT_LVT1     = 0x51, /**< Extended Local Vector Table 1 offset (AMD) */
	LAPIC_EXT_LVT2     = 0x52, /**< Extended Local Vector Table 2 offset (AMD) */
	LAPIC_EXT_LVT3     = 0x53, /**< Extended Local Vector Table 3 offset (AMD) */
};

/**
 * @brief Macro for LAPIC memory multiplication factor.
 *
 * This macro defines the multiplication factor used to calculate the LAPIC memory region size.
 * It is used to determine the size of the memory region reserved for LAPIC registers.
 *
 * @remark This macro is used internally to calculate LAPIC memory region size.
 */
#define LAPIC_MEM_MUL 0x10

/**
 * @brief Macro for LAPIC memory region size.
 *
 * This macro defines the size of the memory region reserved for LAPIC registers, including AMD-specific
 * extended registers. It ensures that sufficient space is allocated for all potential LAPIC registers.
 *
 * @remark Although some registers are available on AMD processors only, it's not a big waste to reserve them
 *         on all platforms. However, we need to watch out for this space being assigned for non-APIC purposes
 *         in future processor models.
 */
#define LAPIC_MEM_REGION ((LAPIC_EXT_LVT3 + 1) * LAPIC_MEM_MUL)

/**
 * @brief Structure representing an IOAPIC.
 *
 * This structure is used to represent the IOAPIC (Input/Output Advanced Programmable Interrupt Controller),
 * which is responsible for managing external interrupts in a multiprocessor system. It provides fields for
 * selecting I/O registers and accessing the I/O window.
 *
 * The IOAPIC is a critical component in routing interrupts from peripheral devices to the appropriate
 * processor, allowing for efficient interrupt handling in complex systems.
 *
 * @remark This structure is used internally to manage IOAPIC register selection and access.
 */
struct ioapic {
	uint32_t ioregsel; /**< I/O register select field */
	uint32_t rsv0[3]; /**< Reserved fields for alignment */
	uint32_t iowin; /**< I/O window field for accessing register data */
	uint32_t rsv1[3]; /**< Reserved fields for alignment */
};

/**
 * @defgroup IOAPIC_RTE_MACROS Macros related to IOAPIC RTE.
 *
 * @brief Macros for configuring bits in the IOAPIC Redirection Table Entry (RTE).
 *
 * These macros define the bit values used to configure various aspects of the IOAPIC Redirection Table Entry (RTE).
 * The RTE is responsible for routing interrupts from peripheral devices to the appropriate processor, allowing for
 * efficient interrupt handling in multiprocessor systems.
 *
 * @remark These macros are used internally to set specific configurations in the IOAPIC RTE.
 *
 * @{
 */
#define IOAPIC_RTE_MASK_CLR		0x0U /**< Interrupt Mask: Clear */
#define IOAPIC_RTE_MASK_SET		0x1U /**< Interrupt Mask: Set */

#define IOAPIC_RTE_TRGRMODE_EDGE	0x0U /**< Trigger Mode: Edge */
#define IOAPIC_RTE_TRGRMODE_LEVEL	0x1U /**< Trigger Mode: Level */

#define IOAPIC_RTE_REM_IRR		0x1U /**< Remote IRR: Read-Only */

#define IOAPIC_RTE_INTPOL_AHI		0x0U /**< Interrupt Polarity: Active High */
#define IOAPIC_RTE_INTPOL_ALO		0x1U /**< Interrupt Polarity: Active Low */

#define IOAPIC_RTE_DELIVS		0x1U /**< Delivery Status: Read-Only */

#define IOAPIC_RTE_DESTMODE_PHY		0x0U /**< Destination Mode: Physical */
#define IOAPIC_RTE_DESTMODE_LOGICAL	0x1U /**< Destination Mode: Logical */

#define IOAPIC_RTE_DELMODE_FIXED	0x0U /**< Delivery Mode: Fixed */
#define IOAPIC_RTE_DELMODE_LOPRI	0x1U /**< Delivery Mode: Lowest Priority */
#define IOAPIC_RTE_DELMODE_INIT		0x5U /**< Delivery Mode: INIT Signal */
#define IOAPIC_RTE_DELMODE_EXINT	0x7U /**< Delivery Mode: External Interrupt */

/**
 * @}
 */

/* End of IOAPIC_RTE_MACROS */

/**
 * @brief Union representing an IOAPIC Redirection Table Entry (RTE).
 *
 * This union provides multiple views for accessing IOAPIC RTE data, which is responsible for routing
 * interrupts from peripheral devices to the appropriate processor in a multiprocessor system.
 *
 * The union offers four different access methods:
 * - Full 64-bit access via the `full` field
 * - 32-bit split access via the `u` structure
 * - Standard format access via the `bits` structure (compatibility format)
 * - Interrupt Remapping format access via the `ir_bits` structure (used with Intel VT-d)
 *
 * The IOAPIC RTE is a critical component in managing interrupt routing, allowing configuration of various
 * parameters such as delivery mode, destination mode, and interrupt polarity.
 *
 * @remark This union is used internally to manipulate IOAPIC RTE settings, enabling precise control over
 *         interrupt routing in multiprocessor environments.
 */
union ioapic_rte {
	uint64_t full; /**< Full 64-bit RTE value */
	struct {
		uint32_t lo_32; /**< Lower 32 bits of the RTE */
		uint32_t hi_32; /**< Higher 32 bits of the RTE */
	} u; /**< 32-bit access to RTE components */
	/**
	 * @brief Field-based access to RTE components.
	 *
	 * This structure provides access to the traditional IOAPIC RTE format used when
	 * interrupt remapping is not enabled. It directly specifies interrupt attributes
	 * and the physical or logical destination for the interrupt.
	 *
	 * @remark This structure shall be packed.
	 */
	struct {
		uint8_t vector:8; /**< Interrupt vector */
		uint64_t delivery_mode:3; /**< Delivery mode for the interrupt */
		uint64_t dest_mode:1; /**< Destination mode (physical/logical) */
		uint64_t delivery_status:1; /**< Delivery status (read-only) */
		uint64_t intr_polarity:1; /**< Interrupt polarity (active high/low) */
		uint64_t remote_irr:1; /**< Remote IRR status (read-only) */
		uint64_t trigger_mode:1; /**< Trigger mode (edge/level) */
		uint64_t intr_mask:1; /**< Interrupt mask (set/clear) */
		uint64_t rsvd_1:39; /**< Reserved bits */
		uint8_t dest_field:8; /**< Destination field specifying target processor(s) */
	} bits __packed;
	/**
	 * @brief IR-based access to RTE components.
	 *
	 * This structure provides access to the IOAPIC RTE format used when Intel VT-d
	 * interrupt remapping is enabled. Instead of directly specifying a destination,
	 * it contains an interrupt index that the remapping hardware uses to look up
	 * the actual destination and vector information from remapping tables.
	 *
	 * @remark This structure shall be packed.
	 */
	struct {
		uint32_t vector:8; /**< Interrupt vector */
		uint32_t constant:3; /**< Must be 0b010 for remappable interrupts */
		uint32_t intr_index_high:1; /**< High bit of the 16-bit interrupt index */
		uint32_t delivery_status:1; /**< Delivery status (read-only) */
		uint32_t intr_polarity:1; /**< Interrupt polarity (active high/low) */
		uint32_t remote_irr:1; /**< Remote IRR status (read-only) */
		uint32_t trigger_mode:1; /**< Trigger mode (edge/level) */
		uint32_t intr_mask:1; /**< Interrupt mask (set/clear) */
		uint32_t rsvd_1:15; /**< Reserved bits */
		uint32_t rsvd_2:16; /**< Reserved bits */
		uint32_t intr_format:1; /**< Must be 1 for remappable interrupts */
		uint32_t intr_index_low:15; /**< Low 15 bits of the interrupt index */
	} ir_bits __packed;
};

/******************************************************************************
 * Various code 'logical' values
 ******************************************************************************/

/******************************************************************************
 * LOCAL APIC defines
 ******************************************************************************/

/**
 * @brief Default physical base address for Local (CPU) APICs.
 *
 * This macro defines the default physical base address for Local APICs in a system. The Local APIC
 * is responsible for handling interrupts for its associated processor, providing efficient interrupt
 * management in multiprocessor environments.
 *
 * @remark This macro is used internally to reference the base address of Local APICs.
 */
#define DEFAULT_APIC_BASE	0xfee00000UL

/**
 * @defgroup APIC_ID_CONSTANTS Macros related to APIC ID registers.
 *
 * @brief Constants related to APIC ID registers.
 *
 * These macros define constants used to manipulate and interpret APIC ID registers. The APIC ID
 * is used to uniquely identify processors in a multiprocessor system, allowing for efficient
 * interrupt routing and processor management.
 *
 * @remark These macros are used internally to manage APIC ID values and configurations.
 *
 * @{
 */

#define APIC_ID_MASK		0xff000000U /**< Mask for APIC ID extraction */

#define APIC_ID_SHIFT		24U /**< Shift value for APIC ID extraction */

/* Constants for APIC ID cluster management */
#define APIC_ID_CLUSTER		0xf0U /**< APIC ID cluster mask */
#define APIC_ID_CLUSTER_ID	0x0fU /**< APIC ID cluster ID mask */
#define APIC_MAX_CLUSTER	0xeU /**< Maximum number of clusters */
#define APIC_MAX_INTRACLUSTER_ID 3 /**< Maximum intra-cluster ID */
#define APIC_ID_CLUSTER_SHIFT	4 /**< Shift value for cluster ID extraction */

/**
 * @}
 */

/* End of APIC_ID_CONSTANTS */

/**
 * @defgroup APIC_VERSION_REGISTERS Macros related to APIC Version Register.
 *
 * @brief Macros for fields in the APIC Version Register (VER).
 *
 * These macros define bit masks and shift values for accessing fields within the APIC Version Register.
 * The Version Register provides information about the APIC's version and capabilities, including the
 * maximum number of Local Vector Table (LVT) entries and support for extended features.
 *
 * @remark These macros are used internally to interpret and manipulate the APIC Version Register.
 *
 * @{
 */
#define APIC_VER_VERSION	0x000000ffU /**< Mask for APIC version */
#define APIC_VER_MAXLVT		0x00ff0000U /**< Mask for maximum LVT entries */
#define MAXLVTSHIFT		16U /**< Shift value for maximum LVT entries */
#define APIC_VER_EOI_SUPPRESSION 0x01000000U /**< Mask for EOI suppression capability */
#define APIC_VER_AMD_EXT_SPACE	0x80000000U /**< Mask for AMD extended space capability */

/**
 * @}
 */

/* End of APIC_VERSION_REGISTERS */

/**
 * @defgroup APIC_LOGICAL_DESTINATION_REGISTERS Macros related to APIC Logical Destination Register.
 *
 * @brief Macros for fields in the APIC Logical Destination Register (LDR).
 *
 * These macros define bit masks for accessing fields within the APIC Logical Destination Register.
 * The LDR is used to configure the logical destination of interrupts, allowing for efficient routing
 * in multiprocessor systems.
 *
 * @remark These macros are used internally to interpret and manipulate the APIC Logical Destination Register.
 *
 * @{
 */
#define APIC_LDR_RESERVED	0x00ffffffU /**< Mask for reserved bits in LDR */
#define X2APIC_LDR_LOGICAL_ID_MASK	0x0000ffffU /**< Mask for logical ID in x2APIC mode */
#define X2APIC_LDR_CLUSTER_ID_MASK	0xffff0000U /**< Mask for cluster ID in x2APIC mode */

/**
 * @}
 */

/* End of APIC_LOGICAL_DESTINATION_REGISTERS */

/**
 * @defgroup APIC_DESTINATION_FORMAT_REGISTERS Macros related to APIC Destination Format Register.
 *
 * @brief Macros for fields in the APIC Destination Format Register (DFR).
 *
 * These macros define bit masks for accessing fields within the APIC Destination Format Register.
 * The DFR is used to configure the format of the destination field, allowing for flat or clustered
 * destination models.
 *
 * @remark These macros are used internally to interpret and manipulate the APIC Destination Format Register.
 *
 * @{
 */
#define APIC_DFR_RESERVED	0x0fffffffU /**< Mask for reserved bits in DFR */
#define APIC_DFR_MODEL_MASK	0xf0000000U /**< Mask for destination model */
#define APIC_DFR_MODEL_FLAT	0xf0000000U /**< Flat destination model */
#define APIC_DFR_MODEL_CLUSTER	0x00000000U /**< Clustered destination model */

/**
 * @}
 */

/* End of APIC_DESTINATION_FORMAT_REGISTERS */

/**
 * @defgroup APIC_SPURIOUS_INTERRUPT_VECTOR_REGISTERS Macros related to APIC Spurious Interrupt Vector Register.
 *
 * @brief Macros for fields in the APIC Spurious Interrupt Vector Register (SVR).
 *
 * These macros define bit masks for accessing fields within the APIC Spurious Interrupt Vector Register.
 * The SVR is used to configure the spurious interrupt vector and enable or disable the APIC, as well as
 * manage focus and EOI suppression.
 *
 * @remark These macros are used internally to interpret and manipulate the APIC Spurious Interrupt Vector Register.
 *
 * @{
 */
#define APIC_SVR_VECTOR		0x000000ffU /**< Mask for spurious interrupt vector */
#define APIC_SVR_VEC_PROG	0x000000f0U /**< Mask for programmable vector */
#define APIC_SVR_VEC_FIX	0x0000000fU /**< Mask for fixed vector */
#define APIC_SVR_ENABLE		0x00000100U /**< Mask for APIC enable */
#define APIC_SVR_SWDIS		0x00000000U /**< APIC software disable */
#define APIC_SVR_SWEN		0x00000100U /**< APIC software enable */
#define APIC_SVR_FOCUS		0x00000200U /**< Mask for focus capability */
#define APIC_SVR_FEN		0x00000000U /**< Focus enable */
#define APIC_SVR_FDIS		0x00000200U /**< Focus disable */
#define APIC_SVR_EOI_SUPPRESSION 0x00001000U /**< Mask for EOI suppression capability */

/**
 * @}
 */

/* End of APIC_SPURIOUS_INTERRUPT_VECTOR_REGISTERS */

/**
 * @defgroup APIC_TASK_PRIORITY_REGISTERS Macros related to APIC Task Priority Register.
 *
 * @brief Macros for fields in the APIC Task Priority Register (TPR).
 *
 * These macros define bit masks for accessing fields within the APIC Task Priority Register.
 * The TPR is used to set the priority level of tasks, influencing the handling of interrupts
 * based on their priority.
 *
 * @remark These macros are used internally to interpret and manipulate the APIC Task Priority Register.
 *
 * @{
 */
#define APIC_TPR_PRIO		0x000000ffU /**< Mask for task priority level */
#define APIC_TPR_INT		0x000000f0U /**< Mask for interrupt priority */
#define APIC_TPR_SUB		0x0000000fU /**< Mask for sub-priority level */

/**
 * @}
 */

/* End of APIC_TASK_PRIORITY_REGISTERS */

/**
 * @defgroup APIC_ERROR_STATUS_REGISTERS Macros related to APIC Error Status Register.
 *
 * @brief Macros for fields in the APIC Error Status Register (ESR).
 *
 * These macros define bit masks for accessing fields within the APIC Error Status Register.
 * The ESR is used to report errors related to the sending and receiving of interrupts, as well
 * as illegal vector and register operations.
 *
 * @remark These macros are used internally to interpret and manipulate the APIC Error Status Register.
 *
 * @{
 */
#define APIC_ESR_SEND_CS_ERROR		0x00000001U /**< Mask for send checksum error */
#define APIC_ESR_RECEIVE_CS_ERROR	0x00000002U /**< Mask for receive checksum error */
#define APIC_ESR_SEND_ACCEPT		0x00000004U /**< Mask for send accept error */
#define APIC_ESR_RECEIVE_ACCEPT		0x00000008U /**< Mask for receive accept error */
#define APIC_ESR_SEND_ILLEGAL_VECTOR	0x00000020U /**< Mask for send illegal vector error */
#define APIC_ESR_RECEIVE_ILLEGAL_VECTOR	0x00000040U /**< Mask for receive illegal vector error */
#define APIC_ESR_ILLEGAL_REGISTER	0x00000080U /**< Mask for illegal register error */

/**
 * @}
 */

/* End of APIC_ERROR_STATUS_REGISTERS */

/**
 * @defgroup APIC_INTERRUPT_COMMAND_REGISTER_LOW Macros related to APIC Interrupt Command Register Low (ICR_LOW).
 *
 * @brief Macros for fields in the APIC Interrupt Command Register Low (ICR_LOW).
 *
 * These macros define bit masks and values for configuring fields within the APIC Interrupt Command Register Low.
 * The ICR_LOW is used to send interrupts to processors, allowing configuration of various parameters such as
 * vector, delivery mode, destination mode, and trigger mode.
 *
 * @remark These macros are used internally to interpret and manipulate the APIC Interrupt Command Register Low.
 *
 * @{
 */

#define APIC_VECTOR_MASK	0x000000ffU /**< Mask for interrupt vector */

/* Delivery mode settings */
#define APIC_DELMODE_MASK	0x00000700U /**< Mask for delivery mode */
#define APIC_DELMODE_FIXED	0x00000000U /**< Fixed delivery mode */
#define APIC_DELMODE_LOWPRIO	0x00000100U /**< Lowest priority delivery mode */
#define APIC_DELMODE_SMI	0x00000200U /**< System Management Interrupt delivery mode */
#define APIC_DELMODE_RR		0x00000300U /**< Remote Read delivery mode */
#define APIC_DELMODE_NMI	0x00000400U /**< Non-maskable Interrupt delivery mode */
#define APIC_DELMODE_INIT	0x00000500U /**< INIT delivery mode */
#define APIC_DELMODE_STARTUP	0x00000600U /**< Startup delivery mode */
#define APIC_DELMODE_RESV	0x00000700U /**< Reserved delivery mode */

/* Destination mode settings */
#define APIC_DESTMODE_MASK	0x00000800U /**< Mask for destination mode */
#define APIC_DESTMODE_PHY	0x00000000U /**< Physical destination mode */
#define APIC_DESTMODE_LOG	0x00000800U /**< Logical destination mode */

/* Delivery status settings */
#define APIC_DELSTAT_MASK	0x00001000U /**< Mask for delivery status */
#define APIC_DELSTAT_IDLE	0x00000000U /**< Idle delivery status */
#define APIC_DELSTAT_PEND	0x00001000U /**< Pending delivery status */

/* Level settings */
#define APIC_LEVEL_MASK		0x00004000U /**< Mask for level */
#define APIC_LEVEL_DEASSERT	0x00000000U /**< Deassert level */
#define APIC_LEVEL_ASSERT	0x00004000U /**< Assert level */

/* Trigger mode settings */
#define APIC_TRIGMOD_MASK	0x00008000U /**< Mask for trigger mode */
#define APIC_TRIGMOD_EDGE	0x00000000U /**< Edge-triggered mode */
#define APIC_TRIGMOD_LEVEL	0x00008000U /**< Level-triggered mode */

/* Remote Read status settings */
#define APIC_RRSTAT_MASK	0x00030000U /**< Mask for Remote Read status */
#define APIC_RRSTAT_INVALID	0x00000000U /**< Invalid Remote Read status */
#define APIC_RRSTAT_INPROG	0x00010000U /**< In-progress Remote Read status */
#define APIC_RRSTAT_VALID	0x00020000U /**< Valid Remote Read status */
#define APIC_RRSTAT_RESV	0x00030000U /**< Reserved Remote Read status */

/* Destination shorthand settings */
#define APIC_DEST_MASK		0x000c0000U /**< Mask for destination shorthand */
#define APIC_DEST_NOSHORT	0x00000000U /**< No shorthand destination */
#define APIC_DEST_SELF		0x00040000U /**< Self destination */
#define APIC_DEST_ALLISELF	0x00080000U /**< All including self destination */
#define APIC_DEST_ALLESELF	0x000c0000U /**< All excluding self destination */

#define APIC_ICRLO_RESV_MASK	0xfff33000U /**< Mask for reserved bits in ICR_LOW */

/**
 * @}
 */

/* End of APIC_INTERRUPT_COMMAND_REGISTER_LOW */

/**
 * @defgroup APIC_LOCAL_VECTOR_TABLE Macros related to APIC Local Vector Table (LVT).
 *
 * @brief Macros for fields in the APIC Local Vector Table (LVT) entries.
 *
 * These macros define bit masks and values for configuring fields within the APIC Local Vector Table entries.
 * The LVT entries are used to manage local interrupts, allowing configuration of various parameters such as
 * vector, delivery mode, and trigger mode.
 *
 * @remark These macros are used internally to interpret and manipulate the APIC Local Vector Table entries.
 *
 * @{
 */

#define APIC_LVT_VECTOR		0x000000ffU /**< Mask for interrupt vector */

/* Delivery mode settings */
#define APIC_LVT_DM		0x00000700U /**< Mask for delivery mode */
#define APIC_LVT_DM_FIXED	0x00000000U /**< Fixed delivery mode */
#define APIC_LVT_DM_SMI		0x00000200U /**< System Management Interrupt delivery mode */
#define APIC_LVT_DM_NMI		0x00000400U /**< Non-maskable Interrupt delivery mode */
#define APIC_LVT_DM_INIT	0x00000500U /**< INIT delivery mode */
#define APIC_LVT_DM_EXTINT	0x00000700U /**< External Interrupt delivery mode */

/* Delivery status settings */
#define APIC_LVT_DS		0x00001000U /**< Mask for delivery status */

/* Interrupt input polarity settings */
#define APIC_LVT_IIPP		0x00002000U /**< Mask for interrupt input polarity */
#define APIC_LVT_IIPP_INTALO	0x00002000U /**< Interrupt input polarity: active low */
#define APIC_LVT_IIPP_INTAHI	0x00000000U /**< Interrupt input polarity: active high */

#define APIC_LVT_RIRR		0x00004000U /**< Mask for remote IRR */

#define APIC_LVT_TM		0x00008000U /**< Mask for trigger mode */

#define APIC_LVT_M		0x00010000U /**< Mask for interrupt masking */

/**
 * @}
 */

/* End of APIC_LOCAL_VECTOR_TABLE */

/**
 * @defgroup APIC_LVT_TIMER Macros related to APIC Local Vector Table Timer (LVT Timer).
 *
 * @brief Macros for fields in the APIC Local Vector Table Timer (LVT Timer).
 *
 * These macros define bit masks and values for configuring fields within the APIC Local Vector Table Timer.
 * The LVT Timer is used to manage timer interrupts, allowing configuration of various parameters such as
 * vector, delivery status, and timer mode.
 *
 * @remark These macros are used internally to interpret and manipulate the APIC Local Vector Table Timer settings.
 *
 * @{
 */

#define APIC_LVTT_VECTOR	0x000000ffU /**< Mask for timer interrupt vector */

#define APIC_LVTT_DS		0x00001000U /**< Mask for delivery status */

#define APIC_LVTT_M		0x00010000U /**< Mask for interrupt masking */

/* Timer mode settings */
#define APIC_LVTT_TM		0x00060000U /**< Mask for timer mode */
#define APIC_LVTT_TM_ONE_SHOT	0x00000000U /**< One-shot timer mode */
#define APIC_LVTT_TM_PERIODIC	0x00020000U /**< Periodic timer mode */
#define APIC_LVTT_TM_TSCDLT	0x00040000U /**< TSC deadline timer mode */
#define APIC_LVTT_TM_RSRV	0x00060000U /**< Reserved timer mode */

/**
 * @}
 */

/* End of APIC_LVT_TIMER */

/**
 * @brief Macro for the maximum count of the APIC timer.
 *
 * This macro defines the maximum count value for the APIC timer, which is used to set the initial
 * count for timer operations.
 *
 * @remark This macro is used internally to configure the APIC timer's maximum count.
 */
#define APIC_TIMER_MAX_COUNT	0xffffffffU /**< Maximum count for APIC timer */

/**
 * @defgroup APIC_TIMER_DIVIDE_CONFIGURATION Macros related to APIC Timer Divide Configuration Register.
 *
 * @brief Macros for fields in the APIC Timer Divide Configuration Register (TDCR).
 *
 * These macros define values for configuring the divide ratio in the APIC Timer Divide Configuration Register.
 * The TDCR is used to set the timer's frequency by dividing the input clock.
 *
 * @remark These macros are used internally to interpret and manipulate the APIC Timer Divide Configuration Register.
 *
 * @{
 */
#define APIC_TDCR_2		0x00U /**< Divide by 2 */
#define APIC_TDCR_4		0x01U /**< Divide by 4 */
#define APIC_TDCR_8		0x02U /**< Divide by 8 */
#define APIC_TDCR_16		0x03U /**< Divide by 16 */
#define APIC_TDCR_32		0x08U /**< Divide by 32 */
#define APIC_TDCR_64		0x09U /**< Divide by 64 */
#define APIC_TDCR_128		0x0aU /**< Divide by 128 */
#define APIC_TDCR_1		0x0bU /**< Divide by 1 */

/**
 * @}
 */

/* End of APIC_TIMER_DIVIDE_CONFIGURATION */

/**
 * @defgroup APIC_EXTENDED_FEATURES Macros related to AMD Extended APIC Features Register.
 *
 * @brief Constants related to the AMD Extended APIC Features Register.
 *
 * These macros define bit masks and shift values for accessing fields within the AMD Extended APIC Features Register.
 * This register provides information about extended capabilities such as extended LVT, extended ID, and SEIO.
 *
 * @remark These macros are used internally to interpret and manipulate the AMD Extended APIC Features Register.
 *
 * @{
 */
#define APIC_EXTF_ELVT_MASK	0x00ff0000U /**< Mask for extended LVT */
#define APIC_EXTF_ELVT_SHIFT	16 /**< Shift value for extended LVT */
#define APIC_EXTF_EXTID_CAP	0x00000004U /**< Capability for extended ID */
#define APIC_EXTF_SEIO_CAP	0x00000002U /**< Capability for SEIO */
#define APIC_EXTF_IER_CAP	0x00000001U /**< Capability for IER */

/**
 * @}
 */

/* End of APIC_EXTENDED_FEATURES */

/**
 * @defgroup APIC_LVT Macros related to APIC Local Vector Table (LVT) indices.
 *
 * @brief Macros for indices in the APIC Local Vector Table (LVT).
 *
 * These macros define indices for accessing entries in the APIC Local Vector Table. The LVT is used to manage
 * local interrupts, including timer, thermal, PMC, LINT0, LINT1, error, and CMCI interrupts.
 *
 * @remark These macros are used internally to reference specific entries in the APIC Local Vector Table.
 *
 * @{
 */
#define APIC_LVT_TIMER		0U /**< Index for timer interrupt */
#define APIC_LVT_THERMAL	1U /**< Index for thermal interrupt */
#define APIC_LVT_PMC		2U /**< Index for performance monitoring counter interrupt */
#define APIC_LVT_LINT0		3U /**< Index for LINT0 interrupt */
#define APIC_LVT_LINT1		4U /**< Index for LINT1 interrupt */
#define APIC_LVT_ERROR		5U /**< Index for error interrupt */
#define APIC_LVT_CMCI		6U /**< Index for corrected machine check interrupt */
#define APIC_LVT_MAX		APIC_LVT_CMCI /**< Maximum index for LVT entries */

/**
 * @}
 */

/* End of APIC_LVT */

/**
 * @defgroup APIC_EXTENDED_LVT Macros related to AMD Extended Local Vector Table (ELVT).
 *
 * @brief AMD extended Local Vector Table (LVT) constants.
 *
 * These macros define indices for accessing extended entries in the AMD Local Vector Table. The extended LVT
 * entries are used to manage specific interrupts related to AMD processors, such as instruction-based sampling
 * and deferred error interrupts.
 *
 * @remark These macros are used internally to reference specific entries in the AMD extended Local Vector Table.
 *
 * @{
 */
#define APIC_ELVT_IBS		0 /**< Instruction Based Sampling */
#define APIC_ELVT_MCA		1 /**< Machine Check Architecture thresholding */
#define APIC_ELVT_DEI		2 /**< Deferred Error Interrupt */
#define APIC_ELVT_SBI		3 /**< Sideband Interface */
#define APIC_ELVT_MAX		APIC_ELVT_SBI /**< Maximum index for extended LVT entries */

/**
 * @}
 */

/* End of APIC_EXTENDED_LVT */

/******************************************************************************
 * IOAPIC defines
 ******************************************************************************/

/**
 * @defgroup IOAPIC_REGSEL_MACROS Macros related to IOAPIC register selection.
 *
 * @brief Macros for IOAPIC window register offsets.
 *
 * These macros define offsets for accessing the IOAPIC window registers. The IOAPIC is responsible for managing
 * external interrupts, and these offsets are used to select and access specific registers within the IOAPIC.
 *
 * @remark These macros are used internally to interpret and manipulate IOAPIC window register offsets.
 *
 * @{
 */
#define IOAPIC_REGSEL		0x00U /**< Offset for IOAPIC register select */
#define IOAPIC_WINDOW		0x10U /**< Offset for IOAPIC window register */

/**
 * @}
 */

/* End of IOAPIC_REGSEL_MACROS */

/**
 * @defgroup IOAPIC_REGISTERS Macros related to IOAPIC registers.
 *
 * @brief Macros for indexes into the IOAPIC.
 *
 * These macros define indexes for accessing specific entries in the IOAPIC. The IOAPIC is responsible for routing
 * interrupts from peripheral devices to the appropriate processor, and these indexes are used to reference specific
 * redirection table entries.
 *
 * @remark These macros are used internally to reference specific entries in the IOAPIC.
 *
 * @{
 */
#define IOAPIC_ID		0x00U /**< Index for IOAPIC ID register */
#define IOAPIC_VER		0x01U /**< Index for IOAPIC version register */
#define IOAPIC_ARB		0x02U /**< Index for IOAPIC arbitration register */
#define IOAPIC_REDTBL		0x10U /**< Base index for IOAPIC redirection table */
#define IOAPIC_REDTBL0		IOAPIC_REDTBL /**< Index for redirection table entry 0 */
#define IOAPIC_REDTBL1		(IOAPIC_REDTBL+0x02U) /**< Index for redirection table entry 1 */
#define IOAPIC_REDTBL2		(IOAPIC_REDTBL+0x04U) /**< Index for redirection table entry 2 */
#define IOAPIC_REDTBL3		(IOAPIC_REDTBL+0x06U) /**< Index for redirection table entry 3 */
#define IOAPIC_REDTBL4		(IOAPIC_REDTBL+0x08U) /**< Index for redirection table entry 4 */
#define IOAPIC_REDTBL5		(IOAPIC_REDTBL+0x0aU) /**< Index for redirection table entry 5 */
#define IOAPIC_REDTBL6		(IOAPIC_REDTBL+0x0cU) /**< Index for redirection table entry 6 */
#define IOAPIC_REDTBL7		(IOAPIC_REDTBL+0x0eU) /**< Index for redirection table entry 7 */
#define IOAPIC_REDTBL8		(IOAPIC_REDTBL+0x10U) /**< Index for redirection table entry 8 */
#define IOAPIC_REDTBL9		(IOAPIC_REDTBL+0x12U) /**< Index for redirection table entry 9 */
#define IOAPIC_REDTBL10		(IOAPIC_REDTBL+0x14U) /**< Index for redirection table entry 10 */
#define IOAPIC_REDTBL11		(IOAPIC_REDTBL+0x16U) /**< Index for redirection table entry 11 */
#define IOAPIC_REDTBL12		(IOAPIC_REDTBL+0x18U) /**< Index for redirection table entry 12 */
#define IOAPIC_REDTBL13		(IOAPIC_REDTBL+0x1aU) /**< Index for redirection table entry 13 */
#define IOAPIC_REDTBL14		(IOAPIC_REDTBL+0x1cU) /**< Index for redirection table entry 14 */
#define IOAPIC_REDTBL15		(IOAPIC_REDTBL+0x1eU) /**< Index for redirection table entry 15 */
#define IOAPIC_REDTBL16		(IOAPIC_REDTBL+0x20U) /**< Index for redirection table entry 16 */
#define IOAPIC_REDTBL17		(IOAPIC_REDTBL+0x22U) /**< Index for redirection table entry 17 */
#define IOAPIC_REDTBL18		(IOAPIC_REDTBL+0x24U) /**< Index for redirection table entry 18 */
#define IOAPIC_REDTBL19		(IOAPIC_REDTBL+0x26U) /**< Index for redirection table entry 19 */
#define IOAPIC_REDTBL20		(IOAPIC_REDTBL+0x28U) /**< Index for redirection table entry 20 */
#define IOAPIC_REDTBL21		(IOAPIC_REDTBL+0x2aU) /**< Index for redirection table entry 21 */
#define IOAPIC_REDTBL22		(IOAPIC_REDTBL+0x2cU) /**< Index for redirection table entry 22 */
#define IOAPIC_REDTBL23		(IOAPIC_REDTBL+0x2eU) /**< Index for redirection table entry 23 */

/* Mask and shift for IOAPIC ID */
#define IOAPIC_ID_MASK		0x0f000000U /**< Mask for IOAPIC ID */
#define IOAPIC_ID_SHIFT		24U /**< Shift value for IOAPIC ID extraction */

/**
 * @}
 */

/* End of IOAPIC_REGISTERS */

/**
 * @defgroup IOAPIC_VERSION_REGISTERS Macros related to IOAPIC version register.
 *
 * @brief Macros for fields in the IOAPIC version register related to redirection entries.
 *
 * These macros define bit masks and shift values for accessing fields within the IOAPIC version register,
 * specifically related to the maximum number of redirection table entries.
 *
 * @remark These macros are used internally to interpret and manipulate the IOAPIC version register.
 *
 * @{
 */
#define IOAPIC_MAX_RTE_MASK	0x00ff0000U /**< Mask for maximum redirection table entries */
#define MAX_RTE_SHIFT		16U /**< Shift value for maximum redirection table entries */

/**
 * @}
 */

/* End of IOAPIC_VERSION_REGISTERS */

#endif /* APICREG_H */
/**
 * @}
 */
