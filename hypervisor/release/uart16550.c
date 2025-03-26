/*
 * Copyright (C) 2019-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <pci.h>

/**
 * @addtogroup debug
 *
 * @{
 */

/**
 * @file
 * @brief UART Debugging Stub Implementations in Release Mode
 *
 * This file provides stub implementations for functions related to UART initialization
 * and configuration for debugging purposes. These functions are placeholders and currently
 * do not perform any actual operations. They are used to maintain a consistent interface
 * and can be replaced with actual implementations as needed. The stubs include UART
 * initialization, checking for PCI-based debug UART, and retrieving PIO debug UART
 * configuration.
 */

/**
 * @brief Initialize the 16550 UART.
 *
 * This stub function simulates the initialization of a 16550 UART, which is commonly
 * used for serial communication. It is intended to be used during system boot or
 * for setting up UART for debugging. In release mode,
 * it performs no operation, serving as a placeholder.
 *
 * @param[in] early_boot A boolean indicating if the initialization is occurring during early boot.
 *
 * @return None.
 */
void uart16550_init(__unused bool early_boot) {}

/**
 * @brief Check if the given PCI BDF corresponds to a debug UART.
 *
 * This stub function simulates checking if a given PCI Bus-Device-Function (BDF)
 * value corresponds to a debug UART. It is used to identify if a PCI device is
 * configured as a debug UART.
 *
 * @param[in] bdf_value The PCI BDF value to check.
 *
 * @return A boolean indicating if the PCI BDF is a debug UART.
 * @retval false Always returns false, indicating it is not a debug UART.
 */
bool is_pci_dbg_uart(__unused union pci_bdf bdf_value) { return false; }

/**
 * @brief Get the PIO configuration for a debug UART.
 *
 * This stub function simulates retrieving the Port I/O (PIO) configuration for a
 * debug UART. It provides the PIO address and the number of bytes for the UART
 * configuration.
 *
 * @param[in] pio_address A pointer to store the PIO address of the debug UART.
 * @param[in] nbytes A pointer to store the number of bytes for the UART configuration.
 *
 * @return A boolean indicating if the PIO configuration was successfully retrieved.
 * @retval false Always returns false, indicating the configuration was not retrieved.
 */
bool get_pio_dbg_uart_cfg(__unused uint16_t *pio_address, __unused uint32_t *nbytes) {
	return false;
}

/**
 * @}
 */