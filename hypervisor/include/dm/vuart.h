/*-
 * Copyright (c) 2013 Neel Natu <neel@freebsd.org>
 * Copyright (c) 2018-2024 Intel Corporation.
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

#ifndef VUART_H
#define VUART_H

#ifdef CONFIG_VUART_ENABLED

#include <types.h>
#include <asm/lib/spinlock.h>
#include <asm/vm_config.h>

/**
 * @addtogroup vp-dm_vperipheral
 *
 * @{
 */

/**
 * @file
 * @brief All APIs to support virtual UART device.
 *
 * This file defines macros, structures and function declarations for emulating virtual UART device.
 */

#define RX_BUF_SIZE		CONFIG_VUART_RX_BUF_SIZE
#define TX_BUF_SIZE		CONFIG_VUART_TX_BUF_SIZE
#define VUART_TIMER_CPU		CONFIG_VUART_TIMER_PCPU
#define INVAILD_VUART_IDX	0xFFU

#define COM1_BASE		0x3F8U
#define COM2_BASE		0x2F8U
#define COM3_BASE		0x3E8U
#define COM4_BASE		0x2E8U
#define INVALID_COM_BASE	0U

#define COM1_IRQ		4U
#define COM2_IRQ		3U
#define COM3_IRQ		6U
#define COM4_IRQ		7U

struct vuart_fifo {
	char *buf;
	uint32_t rindex;	/* index to read from */
	uint32_t windex;	/* index to write to */
	uint32_t num;		/* number of characters in the fifo */
	uint32_t size;		/* size of the fifo */
};

struct acrn_vuart;

/**
 * @brief Data structure to emulate a virtual transmitter.
 *
 * Virtual transmitter is a mechanism used to emulate the physical tx fifo and the data
 * transmit process that consumes time. With this implementation, guest kernel does not
 * spend all its time on the UART driver when there is a flood of message to be sent.
 *
 * The virtual transmitter is emulated by calculating time of how long a physical UART
 * sends out its chars, then use that time to delay asserting THRE/TEMT bit and TX IRQ.
 * The vUART would act more like a physical UART.
 *
 * Tx chars are still send to target fifo instantly. The virtual fifo only delays
 * the asserting of THRE/TEMT and TX IRQ.
 */
struct vuart_transmitter {
	uint64_t vfifo_level; /**< How many chars in the virtual fifo when it is last checked. */
	uint64_t vfifo_time_stamp; /**< Time stamp in CPU ticks when virtual fifo is last checked. */
	struct hv_timer trans_timer; /**< Timer used to emulate the physical transfer process */
	struct acrn_vuart *vu;	/**< pointer to the vUART which owns this transmitter */
};

/**
 * @brief Data structure to illustrate a virtual UART device.
 *
 * This structure contains the information of a virtual UART device.
 *
 * @consistency self.vm->vuart[X] == self
 * @alignment N/A
 *
 * @remark N/A
 */
struct acrn_vuart {
	uint8_t data; /**< Data register (R/W). */
	uint8_t ier; /**< Interrupt enable register (R/W). */
	uint8_t lcr; /**< Line control register (R/W). */
	uint8_t mcr; /**< Modem control register (R/W). */
	uint8_t lsr; /**< Line status register (R/W). */
	uint8_t msr; /**< Modem status register (R/W). */
	uint8_t fcr; /**< FIFO control register (W). */
	uint8_t scr; /**< Scratch register (R/W). */
	uint8_t dll; /**< Baudrate divisor latch LSB. */
	uint8_t dlh; /**< Baudrate divisor latch MSB. */

	struct vuart_fifo rxfifo; /**< FIFO queue for received data. */
	struct vuart_fifo txfifo; /**< FIFO queue for transmitted data. */
	struct vuart_transmitter vtx; /**< Virtual transmitter to emulate tx timing. */
	uint16_t port_base; /**< Base port address of the virtual UART device. */
	uint32_t irq; /**< IRQ number of the virtual UART device. */
	char vuart_rx_buf[RX_BUF_SIZE]; /**< Buffer for received data. */
	char vuart_tx_buf[TX_BUF_SIZE]; /**< Buffer for transmitted data. */
	bool thre_int_pending; /**< Whether Transmitter Holding Register Empty(THRE) interrupt is pending. */
	bool active; /**< Whether the vuart is active. */
	bool escaping; /**< Whether in escaping sequence, only for console vuarts. */
	struct acrn_vuart *target_vu; /**< Pointer to target vuart */
	struct acrn_vm *vm; /**< Pointer to the VM that owns the virtual UART device. */
	struct pci_vdev *vdev; /**< Pointer to the PCI device, only for a PCI vuart. */
	spinlock_t lock; /**< The spinlock to protect simultaneous access of all elements. */
	uint64_t char_vtx_time; /**< Time in CPU ticks to tx a char for a physical UART. */
};

void init_legacy_vuarts(struct acrn_vm *vm, const struct vuart_config *vu_config);
void deinit_legacy_vuarts(struct acrn_vm *vm);

#ifdef CONFIG_VMCS9900_ENABLED
void init_pci_vuart(struct pci_vdev *vdev);
void deinit_pci_vuart(struct pci_vdev *vdev);
#endif

void vuart_putchar(struct acrn_vuart *vu, char ch);
char vuart_getchar(struct acrn_vuart *vu);
void vuart_toggle_intr(const struct acrn_vuart *vu);

bool is_vuart_intx(const struct acrn_vm *vm, uint32_t intx_gsi);

uint8_t vuart_read_reg(struct acrn_vuart *vu, uint16_t offset);
void vuart_write_reg(struct acrn_vuart *vu, uint16_t offset, uint8_t value);


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

/**
 * @}
 */

#endif /* CONFIG_VUART_ENABLED */

#endif /* VUART_H */
