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

#ifndef VIOAPIC_H
#define VIOAPIC_H

#include <asm/apicreg.h>
#include <asm/ioapic.h>
#include <util.h>

/**
 * @addtogroup vp-dm_vioapic
 * @{
 */

/**
 * @file vioapic.h
 *
 * @brief public APIs for virtual IOAPIC
 *
 * This file define the interface to operate virtual IOAPIC device.
 *
 */

#define	VIOAPIC_BASE	0xFEC00000UL /**< Base address for the virtual IOAPIC */
#define	VIOAPIC_SIZE	4096UL /**< Size of the virtual IOAPIC memory region in bytes */

/**
 * @brief Define the number of redirection entries for hardware
 *
 * The number should aligned with native IOAPIC for Service VM
 *
 */
#define REDIR_ENTRIES_HW	120U

/**
 * @brief Define the size of the state bitmap
 *
 * The size calculated by rounding up the division of redirection entries by 64
 *
 */
#define STATE_BITMAP_SIZE	INT_DIV_ROUNDUP(REDIR_ENTRIES_HW, 64U)

/**
 * @brief Represents a single virtual IOAPIC instance.
 *
 * This structure is used to emulate the behavior of an IOAPIC for a virtual machine.
 * It includes information about the associated VM, the IOAPIC chip, and the state of
 * the redirection table entries and pin states.
 *
 * @remark Access to the fields of this structure should be protected by the `lock` to
 * ensure thread safety.
 */
struct acrn_single_vioapic {
	/**
	 * @brief Spinlock to protect access to the fields of this structure.
	 *
	 * Ensures thread safety during operations by preventing concurrent
	 * modifications to the vIOAPIC's state and configuration.
	 *
	 */
	spinlock_t	lock;

	/**
	 * @brief Pointer to the virtual machine associated with this vIOAPIC.
	 *
	 * Links the vIOAPIC to its corresponding VM context, allowing it to
	 * interact with the VM's virtualized environment.
	 *
	 */
	struct acrn_vm  *vm;

	/**
	 * @brief Information about the IOAPIC chip being emulated.
	 *
	 * Includes details such as the IOAPIC's ID, address, and configuration
	 * settings, which are necessary for accurate emulation.
	 *
	 */
	struct ioapic_info chipinfo;
	/**
	 * @brief Register selector for IOAPIC operations.
	 *
	 * Used to select which register is being accessed or modified during
	 * IOAPIC operations, facilitating register-based interactions.
	 *
	 */
	uint32_t	ioregsel;
	/**
	 * @brief Array of redirection table entries.
	 *
	 * Represents the state and configuration of each interrupt input pin,
	 * allowing the vIOAPIC to manage and route interrupts within the VM.
	 *
	 */
	union ioapic_rte rtbl[REDIR_ENTRIES_HW];
	/**
	 * @brief Bitmap representing the state of each pin.
	 *
	 * Tracks the current status of IOAPIC pins, where 1 indicates high and
	 * 0 indicates low, providing a snapshot of pin states for interrupt
	 * management.
	 */
	uint64_t pin_state[STATE_BITMAP_SIZE];
};

/**
 * @brief Represents a collection of virtual IOAPICs for a VM.
 *
 * This structure holds the number of IOAPICs and the maximum number of GSIs
 * emulated for a virtual machine. It also contains an array of individual
 * virtual IOAPIC instances.
 *
 * @consistency The `ioapic_num` represents the number of IO-APICs emulated for the VM.
 * The `nr_gsi` represents the maximum number of GSI emulated for the VM.
 * @alignment 8
 *
 * @remark Ensure that the number of IOAPICs does not exceed `CONFIG_MAX_IOAPIC_NUM`.
 */
struct acrn_vioapics {
	/**
	 * @brief Number of IOAPICs emulated for the virtual machine.
	 *
	 * This value should not exceed `CONFIG_MAX_IOAPIC_NUM`, ensuring that
	 * the emulation stays within the configured limits for the VM.
	 *
	 */
	uint8_t ioapic_num;

	/**
	 * @brief Maximum number of Global System Interrupts (GSIs) emulated for the virtual machine.
	 *
	 * Represents the total interrupt capacity of the emulated IOAPICs, allowing
	 * the VM to handle a specified number of interrupts through its virtualized
	 * IOAPICs.
	 *
	 */
	uint32_t nr_gsi;
	/**
	 * @brief Array of individual virtual IOAPIC instances.
	 *
	 * Each element in the array represents an emulated IOAPIC for the virtual machine,
	 * providing the necessary infrastructure to manage interrupts within the VM's
	 * virtualized environment.
	 *
	 */
	struct acrn_single_vioapic vioapic_array[CONFIG_MAX_IOAPIC_NUM];
};

void vioapic_init(struct acrn_vm *vm);
void reset_vioapics(struct acrn_vm *vm);

void	vioapic_set_irqline_lock(struct acrn_vm *vm, uint32_t vgsi, uint32_t operation);
void	vioapic_set_irqline_nolock(struct acrn_vm *vm, uint32_t vgsi, uint32_t operation);
void	vioapic_assert_irq_lock(struct acrn_vm *vm, uint32_t vgsi);
void	vioapic_assert_irq_nolock(struct acrn_vm *vm, uint32_t vgsi);
void	vioapic_deassert_irq_lock(struct acrn_vm *vm, uint32_t vgsi);
void	vioapic_deassert_irq_nolock(struct acrn_vm *vm, uint32_t vgsi);

uint32_t get_vm_gsicount(const struct acrn_vm *vm);
void	vioapic_broadcast_eoi(struct acrn_vm *vm, uint32_t vector);
void	vioapic_get_rte(struct acrn_vm *vm, uint32_t vgsi, union ioapic_rte *rte);

/**
 * @}
 */
/* End of acrn_vioapic */
#endif /* VIOAPIC_H */
