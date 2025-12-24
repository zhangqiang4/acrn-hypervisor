/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef UART16550_H
#define UART16550_H

#include <vuart.h>

/**
 * @addtogroup debug
 *
 * @{
 */

/**
 * @file
 * @brief 16550 UART Register and Function Definitions
 *
 * This file defines register addresses, bit masks, and function prototypes
 * for interacting with a 16550 UART. It provides the necessary definitions
 * for configuring and using the UART for serial communication, including
 * initialization, data transmission, and configuration settings.
 */

enum serial_dev_type;

bool uart16550_init(bool early_boot);

char uart16550_getc(void);
size_t uart16550_puts(const char *buf, uint32_t len);
void uart16550_set_property(bool enabled, enum serial_dev_type uart_type, uint64_t base_addr);
bool is_pci_dbg_uart(union pci_bdf bdf_value);
bool get_pio_dbg_uart_cfg(uint16_t *pio_address, uint32_t *nbytes);
uint64_t uart16550_get_tx_load_interval(void);
int32_t uart16550_get_tx_headroom(void);
bool resume_uart16550(void);

/**
 * @}
 */
#endif /* !UART16550_H */
