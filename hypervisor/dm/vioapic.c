/*-
 * Copyright (c) 2013 Tycho Nightingale <tycho.nightingale@pluribusnetworks.com>
 * Copyright (c) 2013 Neel Natu <neel@freebsd.org>
 * Copyright (c) 2017-2022 Intel Corporation.
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


/**
 * @brief define a common prefix for log messages related to the vioapic module
 *
 * This helps in identifying and filtering log messages specific to vioapic when debugging or analyzing logs
 *
 */
#define pr_prefix	"vioapic: "

#include <asm/guest/vm.h>
#include <errno.h>
#include <asm/irq.h>
#include <asm/guest/ept.h>
#include <asm/guest/assign.h>
#include <logmsg.h>
#include <asm/ioapic.h>

/**
 * @defgroup vp-dm_vioapic vp-dm.vioapic
 * @ingroup vp-dm
 *
 * @brief The definition and implementation of virtual IOAPIC.
 *
 * It provides external APIs for the operation on the virtual IOAPIC
 * @{
 */

 /**
 * @file vioapic.c
 *
 * @brief implement APIs for virtual IOAPIC
 *
 * This file implement the interface to operate virtual IOAPIC device.
 *
 */

 /**
 * @brief Macro to mark the read-only bits in the IOAPIC redirection table.
 *
 * This macro is to mark the read-only bits within the IOAPIC redirection
 * table entries. Specifically, it highlights the Remote IRR and Delivery
 * Status bits, which are crucial for managing interrupt delivery and
 * acknowledgment in a virtualized environment.
 *
 */
#define	RTBL_RO_BITS	((uint32_t)0x00004000U | (uint32_t)0x00001000U)

#define DBG_LEVEL_VIOAPIC	6U /**< Debug level for virtual IOAPIC operations */
#define ACRN_IOAPIC_VERSION	0x11U /**< Version number for ACRN IOAPIC */

#define MASK_ALL_INTERRUPTS   0x0000000000010000UL /**< Mask to disable all interrupts */

/**
 * @brief Retrieve the pointer to the VIOAPICs structure associated with a VM.
 *
 * This function accesses the specific VM structure to obtain the VIOAPICs structure,
 * which is responsible for managing virtual IOAPICs within the VM.
 * The VIOAPICs structure is crucial for handling INTx interrupt requests and routing
 * them appropriately within the virtualized environment.
 *
 * @param[in] vm vm Pointer to the ACRN VM structure, this parameter represents the
 *               virtual machine for which the VIOAPICs structure is being retrieved.
 *
 * @return Pointer to the ACRN VIOAPICs structure.
 *
 */
static inline struct acrn_vioapics *vm_ioapics(struct acrn_vm *vm)
{
	return (struct acrn_vioapics *)&(vm->arch_vm.vioapics);
}

/**
 * @brief Generate an interrupt on a specified VIOAPIC pin.
 *
 * This function is responsible for generating an interrupt on a specified pin
 * of a virtual IOAPIC (VIOAPIC), it only raises the interrupt when:
 *
 * (1) the IOAPIC PIN is unmask
 * (2) for a level trigger INTx, only when remote irr is not set.
 *
 * If all these conditions met, it will call vlapic_receive_intr to inject the INTx to
 * virtual local APIC, for level trigger INTx, it also needs set remote irr before
 * injecting this INTx to the virtual local APIC.
 *
 * @pre pin < vioapic->chipinfo.nr_pins
 *
 * @param[in] vioapic Pointer to the ACRN single VIOAPIC structure.
 *                    This parameter provides access to the VIOAPIC instance where the
 *                    interrupt is to be generated, allowing the function to interact
 *                    with its configuration and state.
 * @param[in] pin The pin number on the VIOAPIC to generate the interrupt.
 *                This parameter specifies which pin should be used for the
 *                interrupt generation, and must be a valid pin number.
 *
 * @return None
 *
 */
static void
vioapic_generate_intr(struct acrn_single_vioapic *vioapic, uint32_t pin)
{
	uint32_t vector, dest, delmode;
	union ioapic_rte rte;
	bool level, phys;

	rte = vioapic->rtbl[pin];

	/*
	* Checks if the specified pin is masked.
	*/
	if (rte.bits.intr_mask == IOAPIC_RTE_MASK_SET) {
		dev_dbg(DBG_LEVEL_VIOAPIC, "ioapic pin%hhu: masked", pin);
	} else {
		phys = (rte.bits.dest_mode == IOAPIC_RTE_DESTMODE_PHY);
		delmode = rte.bits.delivery_mode;
		level = (rte.bits.trigger_mode == IOAPIC_RTE_TRGRMODE_LEVEL);

		/* For level trigger irq, avoid send intr if
		 * previous one hasn't received EOI
		 */
		if (!level || (vioapic->rtbl[pin].bits.remote_irr == 0UL)) {
			if (level) {
				vioapic->rtbl[pin].bits.remote_irr = IOAPIC_RTE_REM_IRR;
			}
			vector = rte.bits.vector;
			dest = rte.bits.dest_field;
			int32_t ret = vlapic_receive_intr(vioapic->vm, level, dest, phys, delmode, vector, false);
			if (ret != 0) {
				pr_err("%s: fail to generate interrupt", __func__);
			}
		}
	}
}

/**
 * @brief set the state of a given pin
 *
 * This function set the state of a given pin on the VIOAPIC, it's used to emulate the
 * voltage level state of a pin on the virtual machine, this function first checks whether
 * the passed pin less then the nr_pins to detect whether it is a valid value.
 *
 * If the pin is valid, it obtains the corresponding RTE for the pin and then clear the bit for
 * low state or set the bit for high state, that clear or set not relying on old states.
 *
 * Once the setting is complete, it determines whether to generate an interrupt base on the
 * Interrupt Input Pin Polarity bit in the I/O REDIRECTION TABLE REGISTER, if the pin state
 * changes low to high for High active or if the pin state changes high to low for Low active,
 * an interrupt will be generated.
 *
 * @pre pin < vioapic->chipinfo.nr_pins
 *
 * @param[in] vioapic Pointer to the ACRN single VIOAPIC structure. This structure contains the
 *                    configuration and state information for the virtual IOAPIC.
 * @param[in] pin The pin number on the VIOAPIC to update. This identifies which pin's state is being set.
 * @param[in] level The new state of the pin. A value of 0 represents a low state, while a non-zero value
 *                  represents a high state.
 *
 * @return None
 *
 */
static void
vioapic_set_pinstate(struct acrn_single_vioapic *vioapic, uint32_t pin, uint32_t level)
{
	uint32_t old_lvl;
	union ioapic_rte rte;

	if (pin < vioapic->chipinfo.nr_pins) {
		rte = vioapic->rtbl[pin];
		old_lvl = (uint32_t)bitmap_test((uint16_t)(pin & 0x3FU), &vioapic->pin_state[pin >> 6U]);
		if (level == 0U) {
			/* clear pin_state and deliver interrupt according to polarity */
			bitmap_clear_nolock((uint16_t)(pin & 0x3FU), &vioapic->pin_state[pin >> 6U]);
			if ((rte.bits.intr_polarity == IOAPIC_RTE_INTPOL_ALO)
				&& (old_lvl != level)) {
				vioapic_generate_intr(vioapic, pin);
			}
		} else {
			/* set pin_state and deliver interrupt according to polarity */
			bitmap_set_nolock((uint16_t)(pin & 0x3FU), &vioapic->pin_state[pin >> 6U]);
			if ((rte.bits.intr_polarity == IOAPIC_RTE_INTPOL_AHI)
				&& (old_lvl != level)) {
				vioapic_generate_intr(vioapic, pin);
			}
		}
	}
}

/**
 * @brief convert a VGSI to a VIOAPIC and VPIN.
 *
 * The function retrieves the corresponding vioapic using the given GSI value.
 * For Service VM, ACRN Hypervisor exposes the native APIC ACPI Table to it, it may contain
 * multiple IOAPICs. So it first calls the "get_gsi_to_ioapic_index" to obtain the index of
 * the IOAPIC corresponding to the current vgsi, then gets the specific Pin value by calling
 * "gsi_to_ioapic_pin".
 *
 * For a Guest VM, since only one vioapic is emulated, the vioapic_index is 0, and there is
 * no need to look up a table. The vgsi and vpin have a 1:1 mapping.
 *
 * After determining the values of vioapic_index and vpin, use the ioapic_index to find the
 * corresponding vioapic address from the vioapic_array array.
 *
 * @param[in] vm Pointer to the ACRN VM structure. This represents the virtual machine context
 *               within which the VGSI conversion is performed. It must be a valid pointer to an
 *               initialized VM structure.
 * @param[in] vgsi The Target GSI number.
 * @param[out] vpin Pointer to store the resulting VPIN. This may be NULL if the VPIN is not needed.
 *                  If provided, it will be updated with the pin number corresponding to the VGSI.
 *
 * @return Pointer to the ACRN single VIOAPIC structure associated with the VGSI
 *
 */
static struct acrn_single_vioapic *
vgsi_to_vioapic_and_vpin(struct acrn_vm *vm, uint32_t vgsi, uint32_t *vpin)
{
	struct acrn_single_vioapic *vioapic;
	uint8_t vioapic_index = 0U;

	if (is_service_vm(vm)) {
		/*
		 * Utilize platform ioapic_info for Service VM
		 */
		vioapic_index = get_gsi_to_ioapic_index(vgsi);
		if (vpin != NULL) {
			*vpin = gsi_to_ioapic_pin(vgsi);
		}
	} else {
		if (vpin != NULL) {
			*vpin = vgsi;
		}
	}
	vioapic = (struct acrn_single_vioapic *)&(vm->arch_vm.vioapics.vioapic_array[vioapic_index]);
	return vioapic;
}

/**
 * @brief Set vIOAPIC IRQ line status without lock
 *
 * This function emulates setting vIOAPIC IRQ line status without lock.
 * For Level trigger Mode interrupt, just set the pin state to high or low.
 * For Edge trigger Mode interrupt, set the pin state to low then high to emulate a falling pulse
 * on the IRQ line, set the pin state to high then low to emulate a rising pulse on the IRQ line.
 *
 * This assumes that the device driver would set the default Pin Polarity to high before asserting
 * a falling pulse to trigger an interrupt or set the default Pin Polarity to low before asserting
 * a raising pulse to trigger an interrupt.
 *
 * Base on the Interrupt Input Pin Polarity bit in the I/O REDIRECTION TABLE REGISTER, there may trigger an interrupt
 * if the pin state changes low to high for High active or if the pin state changes high to low for Low active.
 *
 * @param[in] vm Pointer to the target VM structure. This represents the virtual machine context
 *               within which the IRQ line state is being set.
 * @param[in] vgsi The Target GSI number.
 * @param[in] operation The action to perform on the IRQ line. Valid options are:
 *                      - GSI_SET_HIGH: Set the IRQ line to a high state.
 *                      - GSI_SET_LOW: Set the IRQ line to a low state.
 *                      - GSI_RAISING_PULSE: emulate a rising pulse on the IRQ line.
 *                      - GSI_FALLING_PULSE: emulate a falling pulse on the IRQ line.
 *
 * @pre vgsi < get_vm_gsicount(vm)
 * @pre vm != NULL
 *
 * @return None
 *
 */
void
vioapic_set_irqline_nolock(struct acrn_vm *vm, uint32_t vgsi, uint32_t operation)
{
	struct acrn_single_vioapic *vioapic;
	uint32_t pin;

	vioapic = vgsi_to_vioapic_and_vpin(vm, vgsi, &pin);

	switch (operation) {
	case GSI_SET_HIGH:
		vioapic_set_pinstate(vioapic, pin, 1U);
		break;
	case GSI_SET_LOW:
		vioapic_set_pinstate(vioapic, pin, 0U);
		break;
	case GSI_RAISING_PULSE:
		vioapic_set_pinstate(vioapic, pin, 1U);
		vioapic_set_pinstate(vioapic, pin, 0U);
		break;
	case GSI_FALLING_PULSE:
		vioapic_set_pinstate(vioapic, pin, 0U);
		vioapic_set_pinstate(vioapic, pin, 1U);
		break;
	default:
		/*
		 * The function caller could guarantee the pre condition.
		 */
		break;
	}
}

/**
 * @brief Set vIOAPIC IRQ line status with lock.
 *
 * Similar with vioapic_set_irqline_nolock(),but make sure operation be done with ioapic lock.
 *
 * @param[in] vm Pointer to the target VM structure. This represents the virtual machine context
 *               within which the IRQ line state is being set.
 * @param[in] vgsi The Target GSI number
 * @param[in] operation The action to perform on the IRQ line. Valid options are:
 *                      - GSI_SET_HIGH: Set the IRQ line to a high state.
 *                      - GSI_SET_LOW: Set the IRQ line to a low state.
 *                      - GSI_RAISING_PULSE: emulate a rising pulse on the IRQ line.
 *                      - GSI_FALLING_PULSE: emulate a falling pulse on the IRQ line.
 *
 * @pre vgsi < get_vm_gsicount(vm)
 * @pre vm != NULL
 *
 * @return none
 *
 */
void
vioapic_set_irqline_lock(struct acrn_vm *vm, uint32_t vgsi, uint32_t operation)
{
	uint64_t rflags;
	struct acrn_single_vioapic *vioapic;

	vioapic = vgsi_to_vioapic_and_vpin(vm, vgsi, NULL);
	spinlock_irqsave_obtain(&(vioapic->lock), &rflags);
	vioapic_set_irqline_nolock(vm, vgsi, operation);
	spinlock_irqrestore_release(&(vioapic->lock), rflags);
}

/**
 * @brief Reads a register from the Virtual IOAPIC (VIOAPIC).
 *
 * This function just returns the cached value for the target writable register directly.
 *
 * For Read-only IOAPIC VERSION REGISTER,in addition to the version number, bits 23:16 need to be
 * written with the Maximum Redirection Entry, which is equal to the number of interrupt input pins
 * for the IOAPIC minus one.
 *
 * For Read-only bit Remote IRR- in I/O REDIRECTION TABLE REGISTER, for local APIC passthrough case,
 * EOI would not trigger VM-exit. So virtual 'Remote IRR' would not be updated. Needs to read physical
 * IOxAPIC RTE to update virtual 'Remote IRR' field each time when guest wants to read I/O REDIRECTION
 * TABLE REGISTERS
 *
 * @param[in] vioapic Pointer to the VIOAPIC device structure. This structure contains the
 *                    configuration and state information for the virtual I/O APIC.
 * @param[in] addr Address of the register to read. This specifies the register within the VIOAPIC
 *                 that is being accessed, and determines the type of data returned.
 *
 * @return The value of the read register.
 *
 */
static uint32_t
vioapic_indirect_read(struct acrn_single_vioapic *vioapic, uint32_t addr)
{
	uint32_t regnum, ret = 0U;
	uint32_t pin, pincount = vioapic->chipinfo.nr_pins;

	regnum = addr & 0xffU;
	switch (regnum) {
	case IOAPIC_ID:
		ret = (uint32_t)vioapic->chipinfo.id << IOAPIC_ID_SHIFT;
		break;
	case IOAPIC_VER:
		ret = ((pincount - 1U) << MAX_RTE_SHIFT) | ACRN_IOAPIC_VERSION;
		break;
	case IOAPIC_ARB:
		ret = (uint32_t)vioapic->chipinfo.id << IOAPIC_ID_SHIFT;
		break;
	default:
		/*
		 * In this switch statement, regnum shall either be IOAPIC_ID or
		 * IOAPIC_VER or IOAPIC_ARB.
		 * All the other cases will be handled properly later after this
		 * switch statement.
		 */
		break;
	}

	/* redirection table entries */
	if ((regnum >= IOAPIC_REDTBL) &&
	    (regnum < (IOAPIC_REDTBL + (pincount * 2U)))) {
		uint32_t addr_offset = regnum - IOAPIC_REDTBL;
		uint32_t rte_offset = addr_offset >> 1U;
		pin = rte_offset;
		if ((addr_offset & 0x1U) != 0U) {
			ret = vioapic->rtbl[pin].u.hi_32;
		} else {
			if (is_lapic_pt_configured(vioapic->vm) && (vioapic->rtbl[pin].bits.trigger_mode != 0UL)) {
				/*
				 * For local APIC passthrough case, EOI would not trigger VM-exit. So virtual
				 * 'Remote IRR' would not be updated. Needs to read physical IOxAPIC RTE to
				 * update virtual 'Remote IRR' field each time when guest wants to read I/O
				 * REDIRECTION TABLE REGISTERS
				 */
				struct ptirq_remapping_info *entry = NULL;
				union ioapic_rte phys_rte = {};
				DEFINE_INTX_SID(virt_sid, vioapic->rtbl[pin].bits.vector);

				entry = find_ptirq_entry(PTDEV_INTR_INTX, &virt_sid, vioapic->vm);
				if (entry != NULL) {
					ioapic_get_rte(entry->allocated_pirq, &phys_rte);
					vioapic->rtbl[pin].bits.remote_irr = phys_rte.bits.remote_irr;
				}
			}
			ret = vioapic->rtbl[pin].u.lo_32;
		}
	}

	return ret;
}

/**
 * @brief Checks if an interrupt is needed for a specific pin on the VIOAPIC.
 *
 * This function determines whether to generate an interrupt by checking if the pin state
 * matches the settings of Interrupt polarity.
 *
 * If the Interrupt polarity setting is active high and the pin state is high or the Interrupt
 * polarity setting is active low and the pin state is low, return true, otherwise, return
 * false.
 *
 * @param[in] vioapic Pointer to the VIOAPIC device structure. This structure contains the
 *                    configuration and state information for the virtual I/O APIC.
 * @param[in] pin The pin number to check. This identifies which pin's interrupt condition is
 *                being evaluated, and must be within the valid range of pins for the VIOAPIC.
 *
 * @return true if an interrupt is needed, false otherwise.
 *
 * @remark This function is intended for internal use within the vioapic module and it is a
 *         necessary condition for final deciding whether an interrupt should be generated.
 *         When calling this function, it is necessary to check whetherthe pin is not masked
 *         and that the previous interrupt has been EOIed (Remote IRR status is 0).
 *
 */
static inline bool vioapic_need_intr(const struct acrn_single_vioapic *vioapic, uint16_t pin)
{
	uint32_t lvl;
	union ioapic_rte rte;
	bool ret = false;

	if ((uint32_t)pin < vioapic->chipinfo.nr_pins) {
		rte = vioapic->rtbl[pin];
		lvl = (uint32_t)bitmap_test(pin & 0x3FU, &vioapic->pin_state[pin >> 6U]);
		ret = !!(((rte.bits.intr_polarity == IOAPIC_RTE_INTPOL_ALO) && (lvl == 0U)) ||
			((rte.bits.intr_polarity == IOAPIC_RTE_INTPOL_AHI) && (lvl != 0U)));
	}

	return ret;
}

/**
 * @brief Writes to the indirect registers of the VIOAPIC.
 *
 * This function will ignore the write operation to read-only register and read-only bits in register.
 * For writable bits or register, it will cache the write value.
 * And For I/O REDIRECTION TABLE REGISTER:
 * 1 Set Trigger Mode to Edge will clear Remote IRR for some OSes will use EOI Register to clear it for 0x20
 * version IOAPIC, otherwise use switch Trigger Mode to Edge Sensitive to clear it.
 * 2 if Interrupt Mask is changed, it will call ptirq_intx_pin_remap to do INTx interrupt remapping for passthrough
 * device
 * 3 call vlapic_receive_intr to generate an interrupt if the following conditions are met:
 * - pin is not masked
 * - Remote IRR is clear to zero
 * - pin level is asserted
 *
 * @param[inout] vioapic Pointer to the acrn_single_vioapic structure. This structure contains the
 *                       configuration and state information for the virtual I/O APIC, and is
 *                       modified by this function.
 * @param[in] addr The address of the register to write to. This specifies the register within the
 *                 VIOAPIC that is being accessed, and determines the type of data written.
 * @param[in] data The data to write to the specified register. This is a 32-bit value representing
 *                 the new configuration or state for the register.
 * @remark In some special scenarios, the LAPIC somehow hasn't send EOI to IOAPIC which cause the Remote
 *         IRR bit can't be clear.
 *         To clear it, some OSes will use EOI Register to clear it for  0x20 version IOAPIC, otherwise
 *         use switch Trigger Mode to Edge Sensitive to clear it.
 *
 * @return none
 *
 */
static void vioapic_indirect_write(struct acrn_single_vioapic *vioapic, uint32_t addr, uint32_t data)
{
	union ioapic_rte last, new;
	uint32_t regnum;
	uint32_t pin, pincount = vioapic->chipinfo.nr_pins;

	regnum = addr & 0xffUL;
	switch (regnum) {
	case IOAPIC_ID:
		vioapic->chipinfo.id = (uint8_t)((data & IOAPIC_ID_MASK) >> IOAPIC_ID_SHIFT);
		break;
	case IOAPIC_VER:
	case IOAPIC_ARB:
		/* readonly */
		break;
	default:
		/*
		 * In this switch statement, regnum shall either be IOAPIC_ID or
		 * IOAPIC_VER or IOAPIC_ARB.
		 * All the other cases will be handled properly later after this
		 * switch statement.
		 */
		break;
	}

	/* redirection table entries */
	if ((regnum >= IOAPIC_REDTBL) && (regnum < (IOAPIC_REDTBL + (pincount * 2U)))) {
		uint32_t addr_offset = regnum - IOAPIC_REDTBL;
		uint32_t rte_offset = addr_offset >> 1U;
		pin = rte_offset;

		last = vioapic->rtbl[pin];
		new = last;
		if ((addr_offset & 1U) != 0U) {
			new.u.hi_32 = data;
		} else {
			new.u.lo_32 &= RTBL_RO_BITS;
			new.u.lo_32 |= (data & ~RTBL_RO_BITS);
		}

		/* In some special scenarios, the LAPIC somehow hasn't send
		 * EOI to IOAPIC which cause the Remote IRR bit can't be clear.
		 * To clear it, some OSes will use EOI Register to clear it for
		 * 0x20 version IOAPIC, otherwise use switch Trigger Mode to
		 * Edge Sensitive to clear it.
		 */
		if (new.bits.trigger_mode == IOAPIC_RTE_TRGRMODE_EDGE) {
			new.bits.remote_irr = 0U;
		}

		vioapic->rtbl[pin] = new;
		dev_dbg(DBG_LEVEL_VIOAPIC, "ioapic pin%hhu: redir table entry %#lx",
			pin, vioapic->rtbl[pin].full);

		/* remap for ptdev */
		if ((new.bits.intr_mask == IOAPIC_RTE_MASK_CLR) || (last.bits.intr_mask  == IOAPIC_RTE_MASK_CLR)) {
			/* VM enable intr */
			/* NOTE: only support max 256 pin */

			(void)ptirq_intx_pin_remap(vioapic->vm, vioapic->chipinfo.gsi_base + pin);
		}

		/*
		 * Generate an interrupt if the following conditions are met:
		 * - pin is not masked
		 * - previous interrupt has been EOIed
		 * - pin level is asserted
		 */
		if ((vioapic->rtbl[pin].bits.intr_mask == IOAPIC_RTE_MASK_CLR) &&
			(vioapic->rtbl[pin].bits.remote_irr == 0UL) &&
			vioapic_need_intr(vioapic, (uint16_t)pin)) {
			dev_dbg(DBG_LEVEL_VIOAPIC, "ioapic pin%hhu: asserted at rtbl write", pin);
			vioapic_generate_intr(vioapic, pin);
		}
	}
}

/**
 * @brief Reads or writes to the VIOAPIC MMIO registers.
 *
 * This function emulates read/write the Accessing IOAPIC Registers under lock protection, refers to
 * "Table 1. Memory Mapped Registers For Accessing IOAPIC Registers, 82093AA I/O ADVANCED PROGRAMMABLE
 * INTERRUPT CONTROLLER (IOAPIC).
 *
 * For I/O Register Select Register, just return the cached value for read operation or cache the write
 * value for write operation.
 *
 * For I/O Window Register, just emulate the IOAPIC Registers read/write operation, refers to "Table 2.
 * IOAPIC Registers 82093AA I/O ADVANCED PROGRAMMABLE INTERRUPT CONTROLLER (IOAPIC).
 *
 * For the reading process:
 * 1 Returns the cached value for the target writable register directly
 *
 * 2 For Read-only IOAPIC VERSION REGISTER,in addition to the version number, bits 23:16 need to be
 * written with the Maximum Redirection Entry, which is equal to the number of interrupt input pins
 * for the IOAPIC minus one.
 *
 * 3 For Read-only bit Remote IRR- in I/O REDIRECTION TABLE REGISTER, for local APIC passthrough case,
 * EOI would not trigger VM-exit. So virtual 'Remote IRR' would not be updated. Needs to read physical
 * IOxAPIC RTE to update virtual 'Remote IRR' field each time when guest wants to read I/O REDIRECTION
 * TABLE REGISTERS
 *
 * For the writing process:
 * 1 Ignore the write operation to read-only register and read-only bits in register, for writable bits
 *   or register, it will cache the write value.
 *
 * 2 For I/O REDIRECTION TABLE REGISTER:
 * 1) Set Trigger Mode to Edge will clear Remote IRR for some OSes will use EOI Register to clear it for 0x20
 * version IOAPIC, otherwise use switch Trigger Mode to Edge Sensitive to clear it.
 * 2) if Interrupt Mask is changed, it will call ptirq_intx_pin_remap to do INTx interrupt remapping for passthrough
 * device
 * 3) call vlapic_receive_intr to generate an interrupt if the following conditions are met:
 * - pin is not masked
 * - Remote IRR is clear to zero
 * - pin level is asserted
 *
 * If the offset is neither of the above values, return 0xFFFFFFFFU to indicate a read/write error.
 *
 * @param[inout] vioapic Pointer to the VIOAPIC instance, representing the virtualized IOAPIC.
 * @param[in] gpa Guest physical address indicating the MMIO register to access.
 * @param[inout] data Pointer to the 32-bit data buffer used for reading from or writing to the register.
 * @param[in] do_read Boolean flag indicating the operation type: true for read, false for write.
 *
 * @pre vioapic != NULL
 * @pre data != NULL
 * @pre (gpa >= vioapic->chipinfo.addr) && (gpa < (vioapic->chipinfo.addr + VIOAPIC_SIZE))
 *
 * @remark The IOAPIC specification allows 32-bit wide accesses to the IOAPIC_REGSEL (offset 0)
 *         and IOAPIC_WINDOW (offset 16) registers.
 *         On read, *data contains the value from the specified MMIO address.
 *         On write, the specified MMIO address is updated with the value in *data.
 *
 * @return none
 *
 */
static void
vioapic_mmio_rw(struct acrn_single_vioapic *vioapic, uint64_t gpa,
		uint32_t *data, bool do_read)
{
	uint32_t offset;
	uint64_t rflags;

	offset = (uint32_t)(gpa - vioapic->chipinfo.addr);

	spinlock_irqsave_obtain(&(vioapic->lock), &rflags);

	/* The IOAPIC specification allows 32-bit wide accesses to the
	 * IOAPIC_REGSEL (offset 0) and IOAPIC_WINDOW (offset 16) registers.
	 */
	switch (offset) {
	case IOAPIC_REGSEL:
		if (do_read) {
			*data = vioapic->ioregsel;
		} else {
			vioapic->ioregsel = *data & 0xFFU;
		}
		break;
	case IOAPIC_WINDOW:
		if (do_read) {
			*data = vioapic_indirect_read(vioapic,
							vioapic->ioregsel);
		} else {
			vioapic_indirect_write(vioapic,
						 vioapic->ioregsel, *data);
		}
		break;
	default:
		if (do_read) {
			*data = 0xFFFFFFFFU;
		}
		break;
	}

	spinlock_irqrestore_release(&(vioapic->lock), rflags);
}

/**
 * @brief Processes an EOI for a specific vector.
 *
 * This function first get the ioapic pin by the EOI vector and Remote IRR bit under the lock protection.
 * Then it call ptirq_intx_ack to Acknowledge a virtual INTx interrupt for passthrough device with this IOAPIC pin.
 * At last, it will clear the Remote IRR in this pin I/O REDIRECTION TABLE REGISTER and call vlapic_receive_intr
 * Receive an interrupt from the IOAPIC and inject it to the virtual local APIC if the IOAPIC pin interrupt pending
 * there becuase of this Remote IRR and then release the lock.
 *
 * @param[inout] vioapic Pointer to the VIOAPIC instance, representing the virtualized IOAPIC.
 * @param[in] vector The interrupt vector for which to process the EOI, indicating the specific
 *                   interrupt to acknowledge.
 *
 * @pre vioapic != NULL
 * @pre vioapic->vm != NULL
 * @pre (vector >= VECTOR_DYNAMIC_START) && (vector <= NR_MAX_VECTOR)
 *
 * @remark The Remote IRR bit is cleared for any pin associated with the specified vector.
 * @remark If a pin needs an interrupt after processing the EOI, an interrupt is generated.
 *
 * @return none
 *
 */
static void
vioapic_process_eoi(struct acrn_single_vioapic *vioapic, uint32_t vector)
{
	uint32_t pin, pincount = vioapic->chipinfo.nr_pins;
	union ioapic_rte rte;
	uint64_t rflags;

	if ((vector < VECTOR_DYNAMIC_START) || (vector > NR_MAX_VECTOR)) {
		pr_err("vioapic_process_eoi: invalid vector %u", vector);
	}

	dev_dbg(DBG_LEVEL_VIOAPIC, "ioapic processing eoi for vector %u", vector);

	/*
	 * XXX keep track of the pins associated with this vector instead
	 * of iterating on every single pin each time.
	 */
	spinlock_irqsave_obtain(&(vioapic->lock), &rflags);
	for (pin = 0U; pin < pincount; pin++) {
		rte = vioapic->rtbl[pin];
		if ((rte.bits.vector != vector) ||
			(rte.bits.remote_irr == 0U)) {
			continue;
		}

		/* notify device to ack if assigned pin */
		ptirq_intx_ack(vioapic->vm, vioapic->chipinfo.gsi_base + pin);

		vioapic->rtbl[pin].bits.remote_irr = 0U;
		if (vioapic_need_intr(vioapic, (uint16_t)pin)) {
			dev_dbg(DBG_LEVEL_VIOAPIC,
				"ioapic pin%hhu: asserted at eoi", pin);
			vioapic_generate_intr(vioapic, pin);
		}
	}
	spinlock_irqrestore_release(&(vioapic->lock), rflags);
}

/**
 * @brief Broadcasts an EOI to all VIOAPICs in the VM.
 *
 * This function emulates the behavior of broadcasting an EOI message from the Local APIC (LAPIC) to
 * all IO-APICs in a physical platform.
 *
 * It iterates over all virtual IOAPICs (VIOAPICs) associated with the specified VM and processes the
 * EOI for the given interrupt vector on all these VIOAPICs.
 *
 * For each VIOAPIC,call ptirq_intx_ack which will call an lock protection interface "vioapic_set_irqline_lock"
 * to acknowledge a virtual INTx interrupt for passthrough device with this IOAPIC pin and clear the Remote IRR
 * in this pin I/O REDIRECTION TABLE REGISTER and call vlapic_receive_intr receive an interrupt from the IOAPIC
 * and inject it to the virtual local APIC if the IOAPIC pin interrupt pending there because of this Remote IRR
 * and then release the lock.
 *
 * @param[in] vm Pointer to the VM instance, representing the virtual machine containing the VIOAPICs.
 * @param[in] vector The interrupt vector for which to broadcast the EOI, indicating the specific
 *                   interrupt to acknowledge across all VIOAPICs.
 *
 * @pre vm != NULL
 * @pre (vector >= VECTOR_DYNAMIC_START) && (vector <= NR_MAX_VECTOR)
 *
 * @remark Each VIOAPIC in the VM processes the EOI for the specified vector.
 *
 * @return none
 *
 */
void vioapic_broadcast_eoi(struct acrn_vm *vm, uint32_t vector)
{
	struct acrn_single_vioapic *vioapic;
	uint8_t vioapic_index;

	/*
	 * For platforms with multiple IO-APICs, EOI message from LAPIC is
	 * broadcast to all IO-APICs. Emulating the same behavior here.
	 */

	for (vioapic_index = 0U; vioapic_index < vm->arch_vm.vioapics.ioapic_num; vioapic_index++) {
		vioapic = &(vm_ioapics(vm)->vioapic_array[vioapic_index]);
		vioapic_process_eoi(vioapic, vector);
	}
}

/**
 * @brief Resets a single VIOAPIC instance.
 *
 * This function resets the specified virtual IOAPIC (VIOAPIC) instance, ensuring that it returns
 * to a known default state.
 *
 * It masks all interrupt redirection entries, sets the CHIPINFO ID and IOREGSEL register to 0.
 *
 * This is typically used during VM initialization or when reconfiguring the VIOAPIC to ensure a
 * clean state.
 *
 * @param[out] vioapic Pointer to the VIOAPIC instance to reset, representing the virtualized IOAPIC
 *                     that needs to be returned to its default state.
 *
 * @pre vioapic != NULL
 *
 * @remark All redirection entries are masked.
 * @remark The ID of CHIPINFO is set to 0.
 * @remark The IOREGSEL register is set to 0.
 *
 * @return none
 *
 */
static void reset_one_vioapic(struct acrn_single_vioapic *vioapic)
{
	uint32_t pin, pincount;

	/* Initialize all redirection entries to mask all interrupts */
	pincount = vioapic->chipinfo.nr_pins;
	for (pin = 0U; pin < pincount; pin++) {
		vioapic->rtbl[pin].full = MASK_ALL_INTERRUPTS;
	}
	vioapic->chipinfo.id = 0U;
	vioapic->ioregsel = 0U;
}

/**
 * @brief Resets all VIOAPICs in the VM.
 *
 * This function iterates over all VIOAPICs associated with the specified VM and reset the IOAPIC's
 * internal logic and sets the register bits to their default value..
 *
 * It ensures that all VIOAPICs are returned to a known, clean state, and it is a necessary step
 * for VM reset.
 *
 * It masks all interrupt redirection entries, sets the CHIPINFO ID and IOREGSEL register to 0.
 *
 * @param[in] vm Pointer to the VM instance, representing the virtual machine containing the VIOAPICs
 *               that need to be reset.
 *
 * @pre vm != NULL
 *
 * @remark Each VIOAPIC in the VM is reset.
 *
 * @return none
 *
 */

void reset_vioapics(struct acrn_vm *vm)
{
	struct acrn_vioapics *vioapics = vm_ioapics(vm);
	uint8_t vioapic_index;

	for (vioapic_index = 0U; vioapic_index < vioapics->ioapic_num; vioapic_index++) {
		reset_one_vioapic(&vioapics->vioapic_array[vioapic_index]);
	}
}

static int32_t vioapic_mmio_access_handler(struct io_request *io_req, void *handler_private_data);

/**
 * @brief Initializes the VIOAPICs for a specific VM.
 *
 * This function sets up the VIOAPICs for the given VM, ensuring that they are properly initialized
 * and registered for memory-mapped I/O (MMIO) emulation. It determines the number of IOAPICs based
 * on whether the VM is a service VM and configures each VIOAPIC accordingly.
 *
 * - For a service VM, it is the same as the one on the physical platform.
 * - For other VMs, only one virtual IOAPIC is provided, simplifying the emulation setup.
 *
 * The initialization is mainly completed in three steps:
 * 1 Reset all the vioapics.
 * 2 Register callback function for the address range of the vioapic.
 * 3 Remove the address range of the vioapic from the EPT table.
 *
 * The function also computes the number of GSIs for the specified VM, the Maximum number of GSI is
 * computed as GSI base of the IOAPIC i.e. enumerated last in ACPI MADT plus the number of interrupt
 * pins of that IOAPIC.
 *
 * @param[in] vm Pointer to the VM instance to initialize, representing the virtual machine for
 *               which the VIOAPICs need to be set up.
 * @pre vm != NULL
 *
 * @remark Each VIOAPIC in the VM is initialized and registered for MMIO emulation.
 * @remark The number of GSIs is computed and set for the VM.
 *
 * @return none
 *
 */
void
vioapic_init(struct acrn_vm *vm)
{
	static struct ioapic_info virt_ioapic_info = {
		.nr_pins = VIOAPIC_RTE_NUM,
		.addr = VIOAPIC_BASE
	};

	struct ioapic_info *vioapic_info;
	uint8_t vioapic_index;
	struct acrn_single_vioapic *vioapic = NULL;

	/*
	* Check if the given VM is a service virtual machine.
	* If it is, retrieve the platform IOAPIC (Input/Output Advanced Programmable Interrupt Controller) information
	* and set the IOAPIC count for the virtual machine (vm).
	*/
	if (is_service_vm(vm)) {
		vm->arch_vm.vioapics.ioapic_num = get_platform_ioapic_info(&vioapic_info);
	} else {
		/*
		* If it is not, set the IOAPIC count to a default of 1 and use the virtual IOAPIC information.
		*/
		vm->arch_vm.vioapics.ioapic_num = 1U;
		vioapic_info = &virt_ioapic_info;
	}

	for (vioapic_index = 0U; vioapic_index < vm->arch_vm.vioapics.ioapic_num; vioapic_index++) {
		vioapic = &vm->arch_vm.vioapics.vioapic_array[vioapic_index];
		spinlock_init(&(vioapic->lock));
		vioapic->chipinfo = vioapic_info[vioapic_index];

		vioapic->vm = vm;
		reset_one_vioapic(vioapic);

		register_mmio_emulation_handler(vm, vioapic_mmio_access_handler, (uint64_t)vioapic->chipinfo.addr,
					(uint64_t)vioapic->chipinfo.addr + VIOAPIC_SIZE, (void *)vioapic, false);
		ept_del_mr(vm, (uint64_t *)vm->arch_vm.nworld_eptp, (uint64_t)vioapic->chipinfo.addr, VIOAPIC_SIZE);
	}

	/*
	 * Maximum number of GSI is computed as GSI base of the IOAPIC i.e. enumerated last in ACPI MADT
	 * plus the number of interrupt pins of that IOAPIC.
	 */
	if (vioapic != NULL) {
		vm->arch_vm.vioapics.nr_gsi = vioapic->chipinfo.gsi_base + vioapic->chipinfo.nr_pins;
	}
}

/**
 * @brief Gets the total number of GSIs for the VM.
 *
 * This function returns the total number of GSIs configured for the specified VM. GSIs are used to
 * manage interrupt routing within the VM, and the count is determined during the initialization of
 * the VM's virtual IOAPICs (VIOAPICs).
 *
 * @param[in] vm Pointer to the VM instance, representing the virtual machine for which the GSI count
 *               needs to be retrieved.
 * @pre vm != NULL
 *
 * @return The total number of GSIs for the VM.
 *
 */
uint32_t
get_vm_gsicount(const struct acrn_vm *vm)
{
	return vm->arch_vm.vioapics.nr_gsi;
}

/**
 * @brief Handles MMIO access requests to the VIOAPIC.
 *
 * This function is a callback function for the vioapic address range and will be registered by the
 * "register_mmio_emulation_handler" when the vioapic init, it will processes MMIO access requests
 * directed at the VIOAPIC, supporting both read and write operations, it ensures that all accesses
 * are 32-bit in size, as required by the IOAPIC specification.
 *
 * Whether to read or write is determined by the direction specified in the struct acrn_mmio_request:
 * - ACRN_IOREQ_DIR_READ, read
 * - ACRN_IOREQ_DIR_WRITE, write
 *
 * @param[in] io_req Pointer to the IO request structure, containing details of the MMIO operation
 *                   such as the address, size, direction, and value.
 * @param[out] handler_private_data Private data passed to the handler, which is a pointer to the
 *                                  VIOAPIC instance being accessed.
 *
 * @return 0 on success, -EINVAL on invalid access.
 *
 * @pre io_req != NULL
 * @pre handler_private_data != NULL
 *
 * @remark On read, io_req->reqs.mmio_request.value is updated with the read data.
 * @remark On write, the specified MMIO address in the VIOAPIC is updated.
 *
 */
static int32_t vioapic_mmio_access_handler(struct io_request *io_req, void *handler_private_data)
{
	struct acrn_single_vioapic *vioapic = (struct acrn_single_vioapic *)handler_private_data;
	struct acrn_mmio_request *mmio = &io_req->reqs.mmio_request;
	uint64_t gpa = mmio->address;
	int32_t ret = 0;

	/* Note all RW to IOAPIC are 32-Bit in size */
	if (mmio->size == 4UL) {
		uint32_t data = (uint32_t)mmio->value;

		if (mmio->direction == ACRN_IOREQ_DIR_READ) {
			vioapic_mmio_rw(vioapic, gpa, &data, true);
			mmio->value = (uint64_t)data;
		} else if (mmio->direction == ACRN_IOREQ_DIR_WRITE) {
			vioapic_mmio_rw(vioapic, gpa, &data, false);
		} else {
			ret = -EINVAL;
		}
	} else {
		pr_err("All RW to IOAPIC must be 32-bits in size");
		ret = -EINVAL;
	}

	return ret;
}

/**
 * @brief Gets the RTE for a specific vgsi.
 *
 * This function accesses the VIOAPIC associated with the given VM and retrieves the RTE for the specified
 * virtual GSI (vgsi), first, get the correct vioapic and the pin id by vgsi, then the pin will be an index
 * of the rtbl to get the RTE.
 *
 * The RTE contains configuration details for interrupt redirection, such as the
 * interrupt vector, delivery mode, and destination. The function ensures that the RTE is correctly
 * retrieved and stored in the provided `rte` pointer.
 *
 * @param[in] vm Pointer to the VM instance, representing the virtual machine containing the VIOAPICs.
 * @param[in] vgsi The Target GSI number.
 * @param[out] rte Pointer to store the retrieved RTE, providing access to the interrupt configuration.
 *
 * @pre vm->arch_vm.vioapics != NULL
 * @pre vgsi < get_vm_gsicount(vm)
 * @pre rte != NULL
 *
 * @remark *rte contains the RTE for the specified vgsi.
 *
 * @return none
 *
 */
void vioapic_get_rte(struct acrn_vm *vm, uint32_t vgsi, union ioapic_rte *rte)
{
	struct acrn_single_vioapic *vioapic;
	uint32_t pin;
	vioapic = vgsi_to_vioapic_and_vpin(vm, vgsi, &pin);

	*rte = vioapic->rtbl[pin];
}
/**
 *
 * @}
 */
