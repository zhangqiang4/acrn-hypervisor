/*
 * Copyright (C) 2019-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef IO_EMUL_H
#define IO_EMUL_H

#include <types.h>

/**
 * @addtogroup vp-dm_io-req
 *
 * @{
 */

/**
 * @file
 * @brief VM exit triggered by IO access.
 *
 * This file provides functions deal with VM exit triggered by EPT violations and IO instructions.
 * These functions are used in PIO/MMIO emulation.
 *
 */

/**
 * @defgroup PORT_IO_INDEX Define emulated Port I/O handler index
 *
 * @{
 */
#define PIC_PRIMARY_PIO_IDX     0U	  /**< Primary PIC. */
#define PIC_SECONDARY_PIO_IDX   (PIC_PRIMARY_PIO_IDX + 1U)      /**< Secondary PIC. */
#define PIC_ELC_PIO_IDX         (PIC_SECONDARY_PIO_IDX + 1U)    /**< Event Link Controller for PIC. */
#define PCI_CFGADDR_PIO_IDX 	(PIC_ELC_PIO_IDX + 1U)          /**< PCI config address. */
#define PCI_CFGDATA_PIO_IDX		(PCI_CFGADDR_PIO_IDX + 1U)      /**< PCI config data. */
#define UART_PIO_IDX0           (PCI_CFGDATA_PIO_IDX + 1U)      /**< Legacy virtual UART0. */
#define PM1A_EVT_PIO_IDX        (UART_PIO_IDX0 + MAX_VUART_NUM_PER_VM)  /**< PM1A event register. */
#define PM1A_CNT_PIO_IDX        (PM1A_EVT_PIO_IDX + 1U)         /**< PM1A control register. */
#define PM1B_EVT_PIO_IDX        (PM1A_CNT_PIO_IDX + 1U)         /**< PM1B event register. */
#define PM1B_CNT_PIO_IDX        (PM1B_EVT_PIO_IDX + 1U)         /**< PM1B control register. */
#define RTC_PIO_IDX             (PM1B_CNT_PIO_IDX + 1U)         /**< RTC address and data register. */
#define VIRTUAL_PM1A_CNT_PIO_IDX	(RTC_PIO_IDX + 1U)          /**< PM1A control register for RT vm. */
#define KB_PIO_IDX              (VIRTUAL_PM1A_CNT_PIO_IDX + 1U) /**< Keyboard. */
#define CF9_PIO_IDX             (KB_PIO_IDX + 1U)               /**< Fixed ACPI reset register */
#define PIO_RESET_REG_IDX		(CF9_PIO_IDX + 1U)              /**< ACPI reset register. */
#define SLEEP_CTL_PIO_IDX		(PIO_RESET_REG_IDX + 1U)        /**< ACPI sleep registers. */
#define OPTEE_CTL_PIO_IDX		(SLEEP_CTL_PIO_IDX + 1U)	/**< OPTEE control register. */
#define EMUL_PIO_IDX_MAX        (OPTEE_CTL_PIO_IDX + 1U)       /**< Maximum registrable PIO address range. */
/**
 * @}
 */

int32_t pio_instr_vmexit_handler(struct acrn_vcpu *vcpu);
int32_t ept_violation_vmexit_handler(struct acrn_vcpu *vcpu);
void emulate_pio_complete(struct acrn_vcpu *vcpu, const struct io_request *io_req);
void   allow_guest_pio_access(struct acrn_vm *vm, uint16_t port_address, uint32_t nbytes);
void deny_guest_pio_access(struct acrn_vm *vm, uint16_t port_address, uint32_t nbytes);
void arch_fire_hsm_interrupt(void);

/**
 * @}
 */
#endif /* IO_EMUL_H */
