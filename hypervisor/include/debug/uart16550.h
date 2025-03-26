/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef UART16550_H
#define UART16550_H

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

/* Register / bit definitions for 16550 UART */

#define UART16550_RBR           0x00U   /**< Receive Buffer Register (RBR) | base+00h, DLAB=0b, read-only */
#define UART16550_THR           0x00U   /**< Transmit Holding Register (THR) | base+00h, DLAB=0b, write-only */
#define UART16550_DLL           0x00U   /**< Divisor Latch Low (DLL) | base+00h, DLAB=1b, read/write */
#define UART16550_IER           0x01U   /**< Interrupt Enable Register (IER) | base+01h, DLAB=0b, read/write */
#define UART16550_DLM           0x01U   /**< Divisor Latch High (DLM) | base+01h, DLAB=1b, read/write */
#define UART16550_IIR           0x02U   /**< Interrupt Identification Register (IIR) | base+02h, DLAB=0b, read-only */
#define UART16550_FCR           0x02U   /**< FIFO Control Register (FCR) | base+02h, DLAB=0b, write-only */
#define UART16550_LCR           0x03U   /**< Line Control Register (LCR) | base+03h, DLAB=xb, read/write */
#define UART16550_MCR           0x04U   /**< Modem Control Register (MCR), only UART0 | base+04h, DLAB=xb, read/write */
#define UART16550_LSR           0x05U   /**< Line Status Register (LSR) | base+05h, DLAB=xb, read-only */
#define UART16550_MSR           0x06U   /**< Modem Status Register (MSR), only UART0 | base+06h, DLAB=xb, read-only */
#define UART16550_SCR           0x07U   /**< Scratch Pad Register (SCR) | base+07h, DLAB=xb, read/write */

/* Value definitions for IIR (Interrupt Identification Register) */
#define IIR_FIFO_MASK		0xc0U   /**< Mask for FIFO status in IIR */
#define IIR_RXTOUT		0x0cU   /**< Receiver timeout interrupt */
#define IER_EMSC		0x08U   /**< Modem status change interrupt */
#define IIR_RLS			0x06U   /**< Receiver line status interrupt */
#define IIR_RXRDY		0x04U   /**< Receiver data ready interrupt */
#define IIR_TXRDY		0x02U   /**< Transmitter holding register empty interrupt */
#define IIR_NOPEND		0x01U   /**< No pending interrupt */
#define IIR_MLSC		0x00U   /**< Modem status interrupt */

/* Interrupt Enable Register (IER) bit definitions */
#define IER_EDSSI		(0x0008U) /**< Enable/disable modem status interrupt */
#define IER_ELSI		(0x0004U) /**< Enable/disable receive data error interrupt */
#define IER_ETBEI		(0x0002U) /**< Enable/disable transmit data write request interrupt */
#define IER_ERBFI		(0x0001U) /**< Enable/disable receive data read request interrupt */

/* Line Control Register (LCR) bit definitions */
#define LCR_DLAB	(1U << 7U) /**< Divisor Latch Access Bit (DLAB) */
#define LCR_SB		(1U << 6U) /**< Break control on/off */
#define LCR_SP		(1U << 5U) /**< Specifies the operation of parity bit */
#define LCR_EPS		(1U << 4U) /**< Specifies the logic of a parity bit */
#define LCR_PEN		(1U << 3U) /**< Specifies whether to add a parity bit */
#define LCR_STB		(1U << 2U) /**< Stop bit length */
#define LCR_WL8		(0x03U)      /**< Number of bits of serial data: 8 bits */
#define LCR_WL7		(0x02U)      /**< Number of bits of serial data: 7 bits */
#define LCR_WL6		(0x01U)      /**< Number of bits of serial data: 6 bits */
#define LCR_WL5		(0x00U)      /**< Number of bits of serial data: 5 bits */
#define LCR_PARITY_ODD		(LCR_PEN)	/**< Odd parity */
#define LCR_PARITY_NONE		0x0U       	/**< No parity */
#define LCR_PARITY_EVEN		(LCR_PEN | LCR_EPS) /**< Even parity */
#define LCR_NB_STOP_BITS_1	0x0U       	/**< 1 stop bit */
#define LCR_NB_STOP_BITS_2	(LCR_STB)    	/**< 2 stop bits */

/* Line Status Register (LSR) bit definitions */
#define LSR_ERR		(1U << 7U) /**< At least one error in data within FIFO */
#define LSR_TEMT	(1U << 6U) /**< Transmit data present */
#define LSR_THRE	(1U << 5U) /**< Transmit data write request present */
#define LSR_BI		(1U << 4U) /**< Break interrupt data present */
#define LSR_FE		(1U << 3U) /**< Framing error occurred */
#define LSR_PE		(1U << 2U) /**< Parity error occurred */
#define LSR_OE		(1U << 1U) /**< Overrun error */
#define LSR_DR		(1U << 0U) /**< Readable received data is present */

/* Modem Control Register (MCR) bit definitions */
#define MCR_PRESCALE	(1U << 7U) /**< Prescale mode, only available on 16650 up */
#define MCR_LOOPBACK	(1U << 4U) /**< Loopback mode */
#define MCR_IE		(1U << 3U) /**< Interrupt enable */
#define MCR_IENABLE	MCR_IE 	   /**< Alias for MCR_IE, used to enable interrupts in MCR */
#define MCR_DRS		(1U << 2U) /**< Data Ready Signal */
#define MCR_RTS		(1U << 1U) /**< Request to Send */
#define MCR_DTR		(1U << 0U) /**< Data Terminal Ready */

/* Modem Status Register (MSR) bit definitions */
#define MSR_DCD		(1U << 7U) /**< Data Carrier Detect */
#define MSR_RI		(1U << 6U) /**< Ring Indicator */
#define MSR_DSR		(1U << 5U) /**< Data Set Ready */
#define MSR_CTS		(1U << 4U) /**< Clear to Send */
#define MSR_DDCD	(1U << 3U) /**< Delta Data Carrier Detect */
#define MSR_TERI	(1U << 2U) /**< Trailing Edge Ring Indicator */
#define MSR_DDSR	(1U << 1U) /**< Delta Data Set Ready */
#define MSR_DCTS	(1U << 0U) /**< Delta Clear to Send */

#define MCR_OUT2	(1U << 3U) /**< Output 2 */
#define MCR_OUT1	(1U << 2U) /**< Output 1 */

#define MSR_DELTA_MASK	0x0FU      /**< Mask for delta status bits in MSR */

/* FIFO Control Register (FCR) bit definitions */
#define FCR_RX_MASK	0xc0U      /**< Mask for receive FIFO */
#define FCR_DMA		(1U << 3U) /**< DMA mode select */
#define FCR_TFR		(1U << 2U) /**< Reset Transmit FIFO */
#define FCR_RFR		(1U << 1U) /**< Reset Receive FIFO */
#define FCR_FIFOE	(1U << 0U) /**< FIFO Enable */

#define UART_IER_DISABLE_ALL	0x00000000U /**< Disable all interrupts in IER */

/* Baud rate definitions */
#define BAUD_9600      9600U      /**< Baud rate 9600 */
#define BAUD_115200    115200U    /**< Baud rate 115200 */
#define BAUD_460800    460800U    /**< Baud rate 460800 */

/* UART oscillator clock */
#define UART_CLOCK_RATE	1843200U   /**< UART clock rate: 1.8432 MHz */

/**
 * @brief Enumeration for serial device types.
 *
 * This enumeration defines the types of serial devices that can be used
 * with the UART interface.
 */
 enum serial_dev_type {
	INVALID, /**< Invalid device type. */
	PIO,     /**< Port I/O device type. */
	PCI,     /**< PCI device type. */
	MMIO,    /**< Memory-mapped I/O device type. */
};

void uart16550_init(bool early_boot);

#if defined(HV_DEBUG)
char uart16550_getc(void);
size_t uart16550_puts(const char *buf, uint32_t len);
void uart16550_set_property(bool enabled, enum serial_dev_type uart_type, uint64_t base_addr);
#endif

bool is_pci_dbg_uart(union pci_bdf bdf_value);
bool get_pio_dbg_uart_cfg(uint16_t *pio_address, uint32_t *nbytes);

/**
 * @}
 */
#endif /* !UART16550_H */
