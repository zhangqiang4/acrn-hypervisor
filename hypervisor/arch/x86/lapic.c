/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <asm/lib/bits.h>
#include <asm/msr.h>
#include <asm/cpu.h>
#include <asm/per_cpu.h>
#include <asm/cpu_caps.h>
#include <asm/lapic.h>
#include <asm/apicreg.h>
#include <asm/irq.h>
#include <delay.h>

/**
 * @defgroup hwmgmt_apic hwmgmt.apic
 * @ingroup hwmgmt
 *
 * @brief public APIs for Local APIC
 *
 * It provides external APIs for Local APIC
 * @{
 */

 /**
 * @file lapic.c
 * @brief Functions to operate Local APIC.
 */

/**
 * @defgroup LAPIC_ICR_DELIVERY_MODES Macros related to LAPIC ICR delivery modes.
 *
 * @brief LAPIC ICR delivery modes.
 *
 * These macros define the delivery modes for the Local APIC's Interrupt Command Register (ICR).
 * They specify how an interrupt should be delivered to the target processor(s).
 *
 * @remark These macros are used to configure the delivery mode in LAPIC ICR.
 *
 * @{
 */
#define INTR_LAPIC_ICR_FIXED           0x0U /**< Fixed delivery mode */
#define INTR_LAPIC_ICR_LP              0x1U /**< Lowest Priority delivery mode */
#define INTR_LAPIC_ICR_SMI             0x2U /**< System Management Interrupt delivery mode */
#define INTR_LAPIC_ICR_NMI             0x4U /**< Non-maskable Interrupt delivery mode */
#define INTR_LAPIC_ICR_INIT            0x5U /**< INIT delivery mode */
#define INTR_LAPIC_ICR_STARTUP         0x6U /**< Startup delivery mode */

/**
 * @}
 */

/* End of LAPIC_ICR_DELIVERY_MODES */

/**
 * @defgroup LAPIC_ICR_DESTINATION_MODES Macros related to LAPIC ICR destination modes.
 *
 * @brief LAPIC ICR destination modes.
 *
 * These macros define the destination modes for the Local APIC's Interrupt Command Register (ICR).
 * They specify whether the interrupt is sent to a physical or logical destination.
 *
 * @remark These macros are used to configure the destination mode in LAPIC ICR.
 *
 * @{
 */
#define INTR_LAPIC_ICR_PHYSICAL        0x0U /**< Physical destination mode */
#define INTR_LAPIC_ICR_LOGICAL         0x1U /**< Logical destination mode */

/**
 * @}
 */

/* End of LAPIC_ICR_DESTINATION_MODES */

/**
 * @defgroup LAPIC_ICR_LEVEL_SETTINGS Macros related to LAPIC ICR level settings.
 * 
 * @brief LAPIC ICR level settings.
 *
 * These macros define the level settings for the Local APIC's Interrupt Command Register (ICR).
 * They specify whether the interrupt level is asserted or deasserted.
 *
 * @remark These macros are used to configure the level setting in LAPIC ICR.
 *
 * @{
 */
#define INTR_LAPIC_ICR_DEASSERT        0x0U /**< Deassert level */
#define INTR_LAPIC_ICR_ASSERT          0x1U /**< Assert level */

/**
 * @}
 */

/* End of LAPIC_ICR_LEVEL_SETTINGS */

/**
 * @defgroup LAPIC_ICR_TRIGGER_MODES Macros related to LAPIC ICR trigger modes.
 *
 * @brief LAPIC ICR trigger modes.
 *
 * These macros define the trigger modes for the Local APIC's Interrupt Command Register (ICR).
 * They specify whether the interrupt is edge-triggered or level-triggered.
 *
 * @remark These macros are used to configure the trigger mode in LAPIC ICR.
 *
 * @{
 */
#define INTR_LAPIC_ICR_EDGE            0x0U /**< Edge-triggered mode */
#define INTR_LAPIC_ICR_LEVEL           0x1U /**< Level-triggered mode */

/**
 * @}
 */

/* End of LAPIC_ICR_TRIGGER_MODES */

/**
 * @defgroup LAPIC_ICR_DESTINATION_SHORTHAND_OPTIONS Macros related to LAPIC ICR destination shorthand options.
 *
 * @brief LAPIC ICR destination shorthand options.
 *
 * These macros define destination shorthand options for the Local APIC's Interrupt Command Register (ICR).
 * They specify how the interrupt should be sent to the processors.
 *
 * @remark These macros are used to configure the shorthand option in LAPIC ICR.
 *
 * @{
 */
#define INTR_LAPIC_ICR_USE_DEST_ARRAY  0x0U /**< Use destination array */
#define INTR_LAPIC_ICR_SELF            0x1U /**< Send interrupt to self */
#define INTR_LAPIC_ICR_ALL_INC_SELF    0x2U /**< Send interrupt to all processors including self */
#define INTR_LAPIC_ICR_ALL_EX_SELF     0x3U /**< Send interrupt to all processors excluding self */

/**
 * @}
 */

/* End of LAPIC_ICR_SHORTHAND_OPTIONS */

/**
 * @brief Union representing LAPIC base MSR IA32_APIC_BASE.
 *
 * This union is used to represent the LAPIC base MSR IA32_APIC_BASE (1BH).
 * It provides access to the LAPIC base address and configuration flags.
 *
 * @remark This union is used to manipulate LAPIC base MSR settings.
 */
union lapic_base_msr {
	uint64_t value; /**< Full MSR value */
	struct {
		uint32_t rsvd_1:8; /**< Reserved bits */
		uint32_t bsp:1; /**< Bootstrap processor flag */
		uint32_t rsvd_2:1; /**< Reserved bit */
		uint32_t x2APIC_enable:1; /**< x2APIC enable flag */
		uint32_t xAPIC_enable:1; /**< xAPIC enable flag */
		uint32_t lapic_paddr:24; /**< LAPIC physical address */
		uint32_t rsvd_3:28; /**< Reserved bits */
	} fields; /**< Field-based access to MSR components */
};

/**
 * @brief Structure for saving LAPIC registers.
 *
 * This static structure is used to save the state of LAPIC registers.
 * It is typically used during system suspend or context switch operations.
 *
 * @remark This structure is used internally to preserve LAPIC state.
 */
static struct lapic_regs saved_lapic_regs;

/**
 * @brief Union for saving LAPIC base MSR IA32_APIC_BASE.
 *
 * This static union is used to save the LAPIC base MSR IA32_APIC_BASE (1BH) value.
 * It is typically used during system suspend or context switch operations.
 *
 * @remark This union is used internally to preserve LAPIC base MSR state.
 */
static union lapic_base_msr saved_lapic_base_msr;

/**
 * @brief Clear Local APIC In-Service Registers
 *
 * This function follows the Intel recommended procedure to ensure that the
 * processor does not get hung up due to already set "in-service" interrupts left
 * over from the boot loader environment. This can actually occur in real-world
 * scenarios, therefore the function ensures that all the in-service bits are
 * cleared.
 *
 * The procedure repeatedly reads each Local APIC In-Service Register (ISR)
 * starting from IA32_X2APIC_ISR7 down to IA32_X2APIC_ISR0. If any
 * bit in the ISR is found to be set, an End of Interrupt (EOI) signal is issued.
 * This operation is performed iteratively until all "in-service" bits are confirmed
 * cleared.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
static void clear_lapic_isr(void)
{
	uint32_t i;
	uint32_t isr_reg;

	/* This is a Intel recommended procedure and assures that the processor
	 * does not get hung up due to already set "in-service" interrupts left
	 * over from the boot loader environment. This actually occurs in real
	 * life, therefore we will ensure all the in-service bits are clear.
	 */
	for (isr_reg = MSR_IA32_EXT_APIC_ISR7; isr_reg >= MSR_IA32_EXT_APIC_ISR0; isr_reg--) {
		for (i = 0U; i < 32U; i++) {
			if (msr_read(isr_reg) != 0U) {
				msr_write(MSR_IA32_EXT_APIC_EOI, 0U);
			} else {
				break;
			}
		}
	}
}

/**
 * @brief Perform Early Initialization of Local APIC
 *
 * This function initializes the Local APIC during early system bootstrap.
 * The initialization process consists of these key steps:
 *
 * - Configure the LAPIC base address by reading MSR_IA32_APIC_BASE
 * - Enable the LAPIC in x2APIC mode through a two-step process:
 *    - First enable LAPIC in xAPIC mode.
 *    - Then enable LPAIC in x2APIC mode.
 * - Disable all Local Vector Table (LVT) entries by writing 0 to the Spurious Interrupt Vector Register (SIVR)
 *   IA32_X2APIC_SIVR,  which effectively masks all LVT entries by disabling the local APIC.
 * - Enable the Local APIC by setting the APIC_SVR_ENABLE bit and configuring the spurious vector in the SIVR.
 * - Clear all potential "in-service" interrupt bits by checking all In-Service Registers (ISR0-ISR7) and issuing
 *   EOI commands for any active interrupts by calling \p clear_lapic_isr().
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
void early_init_lapic(void)
{
	union lapic_base_msr base;

	/* Get local APIC base address */
	base.value = msr_read(MSR_IA32_APIC_BASE);

	/* Enable LAPIC in x2APIC mode*/
	/* The following sequence of msr writes to enable x2APIC
	 * will work irrespective of the state of LAPIC
	 * left by BIOS
	 */
	/* Step1: Enable LAPIC in xAPIC mode */
	base.fields.xAPIC_enable = 1U;
	msr_write(MSR_IA32_APIC_BASE, base.value);
	/* Step2: Enable LAPIC in x2APIC mode */
	base.fields.x2APIC_enable = 1U;
	msr_write(MSR_IA32_APIC_BASE, base.value);

	/* Set the mask bits for all the LVT entries by disabling a local APIC software. */
	msr_write(MSR_IA32_EXT_APIC_SIVR, 0UL);

	/* Enable Local APIC */
	/* TODO: add spurious-interrupt handler */
	msr_write(MSR_IA32_EXT_APIC_SIVR, APIC_SVR_ENABLE | APIC_SVR_VECTOR);

	/* Ensure there are no ISR bits set. */
	clear_lapic_isr();
}

/**
 * @brief Initialize Local APIC with the specific physical CPU ID
 *
 * The function reads the physical LDR register (IA32_X2APIC_LDR MSR) value and records it in the
 * per-CPU data structure for the designated physical CPU (pCPU).
 *
 * @param[in] pcpu_id  The physical CPU ID to be set in the LAPIC's Logical
 *                     Destination Register.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark Can not put this to early_init_lapic because logical ID is not updated yet.
 */
void init_lapic(uint16_t pcpu_id)
{
	per_cpu(lapic_ldr, pcpu_id) = (uint32_t) msr_read(MSR_IA32_EXT_APIC_LDR);
}

/**
 * @brief Save the current state of the Local APIC registers
 *
 * This function reads and saves the current values of various Local APIC
 * registers into a provided structure.
 *
 * @param[out] regs  A pointer to a lapic_regs structure where the LAPIC register
 *              values will be stored.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
static void save_lapic(struct lapic_regs *regs)
{
	regs->tpr.v = (uint32_t) msr_read(MSR_IA32_EXT_APIC_TPR);
	regs->ppr.v = (uint32_t) msr_read(MSR_IA32_EXT_APIC_PPR);
	regs->tmr[0].v = (uint32_t) msr_read(MSR_IA32_EXT_APIC_TMR0);
	regs->tmr[1].v = (uint32_t) msr_read(MSR_IA32_EXT_APIC_TMR1);
	regs->tmr[2].v = (uint32_t) msr_read(MSR_IA32_EXT_APIC_TMR2);
	regs->tmr[3].v = (uint32_t) msr_read(MSR_IA32_EXT_APIC_TMR3);
	regs->tmr[4].v = (uint32_t) msr_read(MSR_IA32_EXT_APIC_TMR4);
	regs->tmr[5].v = (uint32_t) msr_read(MSR_IA32_EXT_APIC_TMR5);
	regs->tmr[6].v = (uint32_t) msr_read(MSR_IA32_EXT_APIC_TMR6);
	regs->tmr[7].v = (uint32_t) msr_read(MSR_IA32_EXT_APIC_TMR7);
	regs->svr.v = (uint32_t) msr_read(MSR_IA32_EXT_APIC_SIVR);
	regs->lvt[APIC_LVT_TIMER].v =
		(uint32_t) msr_read(MSR_IA32_EXT_APIC_LVT_TIMER);
	regs->lvt[APIC_LVT_LINT0].v =
		(uint32_t) msr_read(MSR_IA32_EXT_APIC_LVT_LINT0);
	regs->lvt[APIC_LVT_LINT1].v =
		(uint32_t) msr_read(MSR_IA32_EXT_APIC_LVT_LINT1);
	regs->lvt[APIC_LVT_ERROR].v =
		(uint32_t) msr_read(MSR_IA32_EXT_APIC_LVT_ERROR);
	regs->icr_timer.v = (uint32_t) msr_read(MSR_IA32_EXT_APIC_INIT_COUNT);
	regs->ccr_timer.v = (uint32_t) msr_read(MSR_IA32_EXT_APIC_CUR_COUNT);
	regs->dcr_timer.v =
		(uint32_t) msr_read(MSR_IA32_EXT_APIC_DIV_CONF);
}

/**
 * @brief Restore the previous state of the Local APIC registers
 *
 * This function writes the values saved in a lapic_regs structure back to the
 * corresponding Local APIC registers.
 *
 * @param[in] regs  A pointer to a lapic_regs structure containing the LAPIC register
 *              values to be restored.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
static void restore_lapic(const struct lapic_regs *regs)
{
	msr_write(MSR_IA32_EXT_APIC_TPR, (uint64_t) regs->tpr.v);
	msr_write(MSR_IA32_EXT_APIC_SIVR, (uint64_t) regs->svr.v);
	msr_write(MSR_IA32_EXT_APIC_LVT_TIMER,
			(uint64_t) regs->lvt[APIC_LVT_TIMER].v);

	msr_write(MSR_IA32_EXT_APIC_LVT_LINT0,
			(uint64_t) regs->lvt[APIC_LVT_LINT0].v);
	msr_write(MSR_IA32_EXT_APIC_LVT_LINT1,
			(uint64_t) regs->lvt[APIC_LVT_LINT1].v);

	msr_write(MSR_IA32_EXT_APIC_LVT_ERROR,
			(uint64_t) regs->lvt[APIC_LVT_ERROR].v);
	msr_write(MSR_IA32_EXT_APIC_INIT_COUNT, (uint64_t) regs->icr_timer.v);
	msr_write(MSR_IA32_EXT_APIC_DIV_CONF, (uint64_t) regs->dcr_timer.v);
}

/**
 * @brief Suspend the Local APIC by saving its state and disabling it
 *
 * This function saves the current state of Local APIC: Save IA32_APIC_BASE MSR and all writeable Local
 * APIC registers and then put the Local APIC into software disable state by clearing the software enable
 * bit in the IA32_X2APIC_SIVR MSR.
 *
 * This ensures the APIC is safely suspended for the current CPU context,
 * preserving all necessary state information to allow resumption.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
void suspend_lapic(void)
{
	uint64_t val;

	saved_lapic_base_msr.value = msr_read(MSR_IA32_APIC_BASE);
	save_lapic(&saved_lapic_regs);

	/* disable APIC with software flag */
	val = msr_read(MSR_IA32_EXT_APIC_SIVR);
	val = (~(uint64_t)APIC_SVR_ENABLE) & val;
	msr_write(MSR_IA32_EXT_APIC_SIVR, val);
}

/**
 * @brief Resume the Local APIC by restoring its state
 *
 * This function restores the Local APIC state that was previously saved,
 * including the IA32_APIC_BASE MSR. The restoration process involves
 * re-establishing both the base MSR and various writeable LAPIC registers that were saved
 * by the \p suspend_lapic() function. This allows for the complete resumption of the
 * APIC functionality, including the ACPI software flag.
 *
 * By restoring all critical state information, the function ensures that
 * the Local APIC can resume its operation reliably in the current CPU context.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
void resume_lapic(void)
{
	msr_write(MSR_IA32_APIC_BASE, saved_lapic_base_msr.value);

	/* ACPI software flag will be restored also */
	restore_lapic(&saved_lapic_regs);
}

/**
 * @brief Send an EOI signal to the Local APIC
 *
 * This function sends an EOI signal to the Local APIC by writing zero to IA32_X2APIC_EOI to indicate that
 * an interrupt has been processed.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
void send_lapic_eoi(void)
{
	msr_write(MSR_IA32_EXT_APIC_EOI, 0U);
}

/**
 * @brief Get the current Local APIC ID
 *
 * This function reads and returns the current Local APIC ID from the IA32_X2APIC_APICID MSR.
 *
 * @return  The current Local APIC ID.
 *
 * @pre N/A
 *
 * @post N/A
 */
uint32_t get_cur_lapic_id(void)
{
	uint32_t lapic_id;

	lapic_id = (uint32_t) msr_read(MSR_IA32_EXT_XAPICID);

	return lapic_id;
}

/**
 * @brief Send a Startup IPI (SIPI) sequence to initialize a specific AP.
 *
 * This function implements the multi-step Application Processor(AP) initialization sequence:
 *
 * - Sends an INIT IPI by writing to the target CPU's IA32_X2APIC_ICR MSR with:
 *    - Physical destination mode (INTR_LAPIC_ICR_PHYSICAL)
 *    - Destination specified through destination array (INTR_LAPIC_ICR_USE_DEST_ARRAY)
 *    - INIT delivery mode (INTR_LAPIC_ICR_INIT)
 *
 * - Waits for the INIT sequence to complete:
 *    - 10ms delay for older processors (family != 6)
 *    - 10μs delay for modern processors (family == 6)
 *
 * - Sends the first Startup IPI by writing to IA32_X2APIC_ICR with:
 *    - STARTUP delivery mode (INTR_LAPIC_ICR_STARTUP) 
 *    - Vector field containing the page number of reset code (cpu_startup_start_address >> 12)
 *
 * - Waits for the first SIPI sequence to complete:
 *    - 200μs delay for older processors
 *    - 10μs delay for modern processors
 *
 * @param[in] dest_pcpu_id        The ID of the destination processor.
 * @param[in] cpu_startup_start_address  The start address for the secondary reset code.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 *
 */
void
send_startup_ipi(uint16_t dest_pcpu_id, uint64_t cpu_startup_start_address)
{
	union apic_icr icr;
	struct cpuinfo_x86 *cpu_info = get_pcpu_info();

	icr.value = 0U;
	icr.value_32.hi_32 = per_cpu(lapic_id, dest_pcpu_id);

	/* Assert INIT IPI */
	icr.bits.destination_mode = INTR_LAPIC_ICR_PHYSICAL;
	icr.bits.shorthand = INTR_LAPIC_ICR_USE_DEST_ARRAY;
	icr.bits.delivery_mode = INTR_LAPIC_ICR_INIT;
	msr_write(MSR_IA32_EXT_APIC_ICR, icr.value);

	/* Give 10ms for INIT sequence to complete for old processors.
	 * BWG states that a delay cannot be avoided between the INIT IPI
	 * and first Startup IPI, so on Modern processors (family == 6)
	 * setting a delay value of 10us.
	 */
	if (cpu_info->displayfamily != 6U) {
		/* delay 10ms */
		udelay(10000U);
	} else {
		udelay(10U); /* 10us is enough for Modern processors */
	}

	/* Send Start IPI with page number of secondary reset code */
	icr.value_32.lo_32 = 0U;
	icr.bits.shorthand = INTR_LAPIC_ICR_USE_DEST_ARRAY;
	icr.bits.delivery_mode = INTR_LAPIC_ICR_STARTUP;
	icr.bits.vector = (uint8_t)(cpu_startup_start_address >> 12U);
	msr_write(MSR_IA32_EXT_APIC_ICR, icr.value);

	if (cpu_info->displayfamily == 6U) {
		udelay(10U); /* 10us is enough for Modern processors */
	} else {
		udelay(200U); /* 200us for old processors */
	}

	/* Send another start IPI as per the Intel Arch specification */
	msr_write(MSR_IA32_EXT_APIC_ICR, icr.value);
}


/**
 * @brief Send an IPI to a subset of processors specified by a bitmask
 *
 * This function iterates over the bits in the `dest_mask` and calls \p send_single_ipi() to send an
 * IPI to each processor whose corresponding bit is set.
 *
 * @param[in] dest_mask  A bitmask where each set bit represents a destination processor.
 * @param[in] vector     The vector number for the IPI.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
void send_dest_ipi_mask(uint32_t dest_mask, uint32_t vector)
{
	uint16_t pcpu_id;
	uint32_t mask = dest_mask;

	pcpu_id = ffs64(mask);
	while (pcpu_id < MAX_PCPU_NUM) {
		bitmap32_clear_nolock(pcpu_id, &mask);
		send_single_ipi(pcpu_id, vector);
		pcpu_id = ffs64(mask);
	}
}

/**
 * @brief Send a single IPI to a specified processor
 *
 * This function checks if the specified processor is active first. If not, it logs an error and return. 
 * In the contrary situation, it sends a single IPI to the specified processor with the given vector
 * number by writing IA32_X2APIC_IPI MSR. If the processor is the current processor, it uses a self-IPI mechanism;
 * otherwise, it uses the ICR to send the IPI by writing IA32_X2APIC_ICR MSR.
 *
 * @param[in] pcpu_id  The ID of the destination processor.
 * @param[in] vector   The vector number for the IPI.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
void send_single_ipi(uint16_t pcpu_id, uint32_t vector)
{
	union apic_icr icr;

	if (is_pcpu_active(pcpu_id)) {
		if (get_pcpu_id() == pcpu_id) {
			msr_write(MSR_IA32_EXT_APIC_SELF_IPI, vector);
		} else {
			/* Set the destination field to the target processor. */
			icr.value_32.hi_32 = per_cpu(lapic_id, pcpu_id);

			/* Write the vector ID to ICR. */
			icr.value_32.lo_32 = vector | (INTR_LAPIC_ICR_PHYSICAL << 11U);

			msr_write(MSR_IA32_EXT_APIC_ICR, icr.value);
		}
	} else {
		pr_err("pcpu_id %d not in active!", pcpu_id);
	}
}

/**
 * @brief Sends an initialization signal to a specific physical CPU.
 *
 * This function is used to send an INIT signal to the specified physical CPU by writing its IA32_X2APIC_ICR
 * MSR with INIT Delivery Mode.
 *
 * @param[in] pcpu_id   The identifier of the physical CPU to which the INIT signal will be sent.
 *                  This value must be less than MAX_PCPU_NUM.
 *
 * @pre pcpu_id < MAX_PCPU_NUM
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
void send_single_init(uint16_t pcpu_id)
{
	union apic_icr icr;

	/*
	 * Intel SDM Vol3 23.8:
	 *   The INIT signal is blocked whenever a logical processor is in VMX root operation.
	 *   It is not blocked in VMX non-root operation. Instead, INITs cause VM exits
	 */

	icr.value_32.hi_32 = per_cpu(lapic_id, pcpu_id);
	icr.value_32.lo_32 = (INTR_LAPIC_ICR_PHYSICAL << 11U) | (INTR_LAPIC_ICR_INIT << 8U);

	msr_write(MSR_IA32_EXT_APIC_ICR, icr.value);

}

/**
 * @brief Kick a specific CPU out of non-root mode into root-mode
 *
 * This function kicks a specific CPU out of non-root mode into root-mode depends on the \p mode_to_kick_pcpu for
 * each CPU. For INIT deliver mode, call \p send_single_init to kick off the CPU, otherwise, call
 * \p send_single_ipi() with \p NOTIFY_VCPU_VECTOR vector to kick off the CPU.
 *
 * @param[in] pcpu_id   The identifier of the physical CPU to be kicked.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
void kick_pcpu(uint16_t pcpu_id)
{
	if (per_cpu(mode_to_kick_pcpu, pcpu_id) == DEL_MODE_INIT) {
		send_single_init(pcpu_id);
	} else {
		send_single_ipi(pcpu_id, NOTIFY_VCPU_VECTOR);
	}
}
/**
 * @}
 */
