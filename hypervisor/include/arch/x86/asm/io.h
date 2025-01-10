/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef IO_H
#define IO_H

#include <types.h>

/**
 * @defgroup hwmgmt_io hwmgmt.io
 * @ingroup hwmgmt
 * @brief IO operation
 *
 * This module implements various IO operations helpers in hypervisor. ACRN hypervisor could read or write IO port
 * or memory map IO.
 *
 * @{
 */

/**
 * @file
 * @brief Implementation of IO operation.
 *
 * This file implements all the APIs to support IO read or write operation.
 */

#define IO_SPACE_BITMASK 0xffffU  /**< X86 architecture only supports 16 bits IO space. */

/**
 * @brief Writes an 8-bit value to a specified I/O port.
 *
 * This function calls X86 assembly instruction "outb" to write the given 8-bit value to the specified port address.
 *
 * - It calls the inline assembly instruction. The value should be placed in the AL register, and port variable should
 * be placed in the DX register, which could also be a immediate value. Key word volatile should be added to avoid
 * potential reorder and optimization by compiler.
 *
 * @param[in] value    The 8-bit value to be written to the I/O port.
 * @param[in] port     The I/O port address where the value will be written.
 *
 * @return None.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline void pio_write8(uint8_t value, uint16_t port)
{
	asm volatile ("outb %0,%1"::"a" (value), "dN"(port));
}

/**
 * @brief Read 1 byte from specific I/O port.
 *
 * This function calls X86 assembly instruction "inb" to read value for one byte.
 *
 * - It calls the inline assembly instruction. Input parameter port value could be a immediate value or be placed in
 * DX register, and the value read from the specified port are returned in the AL register. Key word volatile should
 * be added to avoid potential reorder and optimization by compiler.
 *
 * @param[in] port     The I/O port address from which the value will be read.
 *
 * @return The 8-bit value read from the specified I/O port.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t pio_read8(uint16_t port)
{
	uint8_t value;

	asm volatile ("inb %1,%0":"=a" (value):"dN"(port));
	return value;
}

/**
 * @brief Writes an 16-bit value to a specified I/O port.
 *
 * This function calls X86 assembly instruction "outw" to write the given 16-bit value to the specified port address.
 *
 * - It calls the inline assembly instruction. The value should be placed in the AX register, and port variable
 * should be placed in the DX register, which could also be a immediate value. Key word volatile should be added
 * to avoid potential reorder and optimization by compiler.
 *
 * @param[in] value    The 16-bit value to be written to the I/O port.
 * @param[in] port     The I/O port address where the value will be written.
 *
 * @return None.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline void pio_write16(uint16_t value, uint16_t port)
{
	asm volatile ("outw %0,%1"::"a" (value), "dN"(port));
}

/**
 * @brief Read 2 bytes from specific I/O port.
 *
 * This function calls X86 assembly instruction "inw" to read value for two bytes.
 *
 * - It calls the inline assembly instruction. Input parameter port value could be a immediate value or be placed
 * in DX register, and output parameter value should be generated in the AX register. Key word volatile should be
 * added to avoid potential reorder and optimization by compiler.
 *
 * @param[in] port     The I/O port address from which the value will be read.
 *
 * @return A 16-bit value read from the specified I/O port.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint16_t pio_read16(uint16_t port)
{
	uint16_t value;

	asm volatile ("inw %1,%0":"=a" (value):"dN"(port));
	return value;
}

/**
 * @brief Writes an 32-bit value to a specified I/O port.
 *
 * This function calls X86 assembly instruction "outl" to write the given 32-bit value to the specified port address.
 *
 * - It calls the inline assembly instruction. The value should be placed in the EAX register, and port variable should
 * be placed in the DX register, which could also be a immediate value. Key word volatile should be added to avoid
 * potential reorder and optimization by compiler.
 *
 * @param[in] value    The 32-bit value to be written to the I/O port.
 * @param[in] port     The I/O port address where the value will be written.
 *
 * @return None.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline void pio_write32(uint32_t value, uint16_t port)
{
	asm volatile ("outl %0,%1"::"a" (value), "dN"(port));
}

/**
 * @brief Read 4 bytes from specific I/O port.
 *
 * This function calls X86 assembly instruction "inl" to read value for four bytes.
 *
 * - It calls the inline assembly instruction. Input parameter port value could be a immediate value or
 * be placed in DX register, and output parameter value should be generated in the EAX register. Key word
 * volatile should be added to avoid potential reorder and optimization by compiler.
 *
 * @param[in] port     The I/O port address from which the value will be read.
 *
 * @return The 32-bit value read from the specified I/O port.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint32_t pio_read32(uint16_t port)
{
	uint32_t value;

	asm volatile ("inl %1,%0":"=a" (value):"dN"(port));
	return value;
}

/**
 * @brief Value setting for specific I/O port.
 *
 * This inline function calls the different I/O write functions according to different size of specific I/O port.
 *
 * - If the input sz is 1, it calls the function pio_write8() with converted type uint8_t for the input v, for detail
 * operation, refer to function pio_write8().
 * - If the input sz is 2, it calls the function pio_write16() with converted type uint16_t for the input v, for detail
 * operation, refer to function pio_write16().
 * - If the input sz is of other values, it calls the function pio_write32(), for detail operation, refer to function
 * pio_write32().
 *
 * @param[in] v        The value need to be set
 * @param[in] addr     The I/O port need to be write
 * @param[in] sz       The size of I/O port
 *
 * @return None.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline void pio_write(uint32_t v, uint16_t addr, size_t sz)
{
	if (sz == 1U) {
		pio_write8((uint8_t)v, addr);
	} else if (sz == 2U) {
		pio_write16((uint16_t)v, addr);
	} else {
		pio_write32(v, addr);
	}
}
/**
 * @brief Value read for specific I/O port.
 *
 * This inline function calls the different I/O read functions according to different size of specific I/O port
 * and return the value.
 *
 * - If the input sz is 1, it calls the function pio_read8(), for detail operation, refer to function pio_read8().
 * - If the input sz is 2, it calls the function pio_read16(), for detail operation, refer to function pio_read16().
 * - If the input sz is of other values, it calls the function pio_read32(), for detail operation, refer to function
 * pio_read32().
 *
 * @param[in] addr     The I/O port address from which the value will be read.
 * @param[in] sz       The size of I/O port
 *
 * @return The 32-bit value read from the specified I/O port.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint32_t pio_read(uint16_t addr, size_t sz)
{
	uint32_t ret;
	if (sz == 1U) {
		ret = pio_read8(addr);
	} else if (sz == 2U) {
		ret = pio_read16(addr);
	} else {
		ret = pio_read32(addr);
	}
	return ret;
}

/**
 * @brief Writes an 64-bit value to a specific memory mapped I/O.
 *
 * This inline function implement the 64-bit write operation for specific memory mapped I/O.
 *
 * @param[in] value        The 64-bit value to be written to the memory mapped I/O.
 * @param[inout] addr      The memory mapped I/O where the value will be written.
 *
 * @return None.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline void mmio_write64(uint64_t value, void *addr)
{
	volatile uint64_t *addr64 = (volatile uint64_t *)addr;
	*addr64 = value;
}

/**
 * @brief Writes an 32-bit value to a specific memory mapped I/O.
 *
 * This inline function implement the 32-bit write operation for specific memory mapped I/O.
 *
 * @param[in] value        The 32-bit value to be written to the memory mapped I/O.
 * @param[inout] addr      The memory mapped I/O where the value will be written.
 *
 * @return None.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline void mmio_write32(uint32_t value, void *addr)
{
	volatile uint32_t *addr32 = (volatile uint32_t *)addr;
	*addr32 = value;
}

/**
 * @brief Writes an 16-bit value to a specific memory mapped I/O.
 *
 * This inline function implement the 16-bit write operation for specific memory mapped I/O.
 *
 * @param[in] value        The 16-bit value to be written to the memory mapped I/O.
 * @param[inout] addr      The memory mapped I/O where the value will be written.
 *
 * @return None.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline void mmio_write16(uint16_t value, void *addr)
{
	volatile uint16_t *addr16 = (volatile uint16_t *)addr;
	*addr16 = value;
}

/**
 * @brief Writes an 8-bit value to a specific memory mapped I/O.
 *
 * This inline function implement the 8-bit write operation for specific memory mapped I/O.
 *
 * @param[in] value        The 8-bit value to be written to the memory mapped I/O.
 * @param[inout] addr      The memory mapped I/O where the value will be written.
 *
 * @return None.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline void mmio_write8(uint8_t value, void *addr)
{
	volatile uint8_t *addr8 = (volatile uint8_t *)addr;
	*addr8 = value;
}

/**
 * @brief Read 64-bit value from specific memory mapped I/O.
 *
 * This inline function implement the 64-bit read operation for specific memory mapped I/O.
 *
 * - It converts the addr pointer to the type to a pointer to volatile const uint64_t, then read the value from
 * the address the pointer pointing to.
 *
 * @param[in] addr        The memory mapped I/O address from which the value will be read.
 *
 * @return The 64-bit value read from the specified memory mapped I/O.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint64_t mmio_read64(const void *addr)
{
	return *((volatile const uint64_t *)addr);
}

/**
 * @brief Read 32-bit value from specific memory mapped I/O.
 *
 * This inline function implement the 32-bit read operation for specific memory mapped I/O.
 *
 * - It converts the addr pointer to the type to a pointer to volatile const uint32_t, then read the value from the
 * address the pointer points to.
 *
 * @param[in] addr        The memory mapped I/O address from which the value will be read.
 *
 * @return The 32-bit value read from the specified memory mapped I/O.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint32_t mmio_read32(const void *addr)
{
	return *((volatile const uint32_t *)addr);
}

/**
 * @brief Read 16-bit value from specific memory mapped I/O.
 *
 * This inline function implement the 16-bit read operation for specific memory mapped I/O.
 *
 * - It converts the addr pointer to the type to a pointer to volatile const uint16_t, then read the value from the
 * address the pointer points to.
 *
 * @param[in] addr        The memory mapped I/O address from which the value will be read.
 *
 * @return The 16-bit value read from the specified memory mapped I/O.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint16_t mmio_read16(const void *addr)
{
	return *((volatile const uint16_t *)addr);
}

/**
 * @brief Read 8-bit value from specific memory mapped I/O.
 *
 * This inline function implement the 8-bit read operation for specific memory mapped I/O.
 *
 * - It converts the addr pointer to the type to a pointer to volatile const uint8_t, then read the value from the
 * address the pointer points to.
 *
 * @param[in] addr        The memory mapped I/O address from which the value will be read.
 *
 * @return The 8-bit value read from the specified memory mapped I/O.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint8_t mmio_read8(const void *addr)
{
	return *((volatile const uint8_t *)addr);
}

/**
 * @brief Value read for specific memory mapped I/O.
 *
 * This inline function calls the different memory mapped I/O read functions according to different size
 * of specific I/O port and return uint64_t type reading result.
 *
 * - If the input sz is 1, it calls the function mmio_read8() and returns converted type uint64_t value,
 * for detail operation, refer to functions mmio_read8().
 * - If the input sz is 2, it calls the function mmio_read16() and returns converted type uint64_t value,
 * for detail operation, refer to functions mmio_read16().
 * - If the input sz is 4, it calls the function mmio_read32() and returns converted type uint64_t value,
 * for detail operation, refer to functions mmio_read32().
 * - If the input sz is of other values, it calls the function mmio_read64() and return value, for detail
 * operation, refer to functions mmio_read64().
 *
 * @param[in] addr     The memory mapped I/O address from which the value will be read.
 * @param[in] sz       The size of memory mapped I/O
 *
 * @return The 64-bit value read from the specified memory mapped I/O.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint64_t mmio_read(const void *addr, uint64_t sz)
{
	uint64_t val;
	switch (sz) {
	case 1U:
		val = (uint64_t)mmio_read8(addr);
		break;
	case 2U:
		val = (uint64_t)mmio_read16(addr);
		break;
	case 4U:
		val = (uint64_t)mmio_read32(addr);
		break;
	default:
		val = mmio_read64(addr);
		break;
	}
	return val;
}

/**
 * @brief Value write for specific memory mapped I/O.
 *
 * This inline function calls the different memory mapped I/O write functions according to different size
 * of specific memory mapped I/O.
 *
 * - If the input sz is 1, it calls the function mmio_write8() with converted type uint8_t for the input val, for
 * detail operation, refer to functions mmio_write8().
 * - If the input sz is 2, it calls the function mmio_write16() with converted type uint16_t for the input val, for
 * detail operation, refer to functions mmio_write16().
 * - If the input sz is 4, it calls the function mmio_write32() with converted type uint32_t for the input val, for
 * detail operation, refer to functions mmio_write32().
 * - If the input sz is of other values, it calls the function mmio_write64(), for detail operation, refer to
 * functions mmio_write64().
 *
 * @param[inout] addr      The memory mapped I/O address where the value will be written.
 * @param[in] sz           The width size of memory mapped I/O
 * @param[in] val          The 64-bit value needed to be written
 *
 * @return None.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline void mmio_write(void *addr, uint64_t sz, uint64_t val)
{
	switch (sz) {
	case 1U:
		mmio_write8((uint8_t)val, addr);
		break;
	case 2U:
		mmio_write16((uint16_t)val, addr);
		break;
	case 4U:
		mmio_write32((uint32_t)val, addr);
		break;
	default:
		mmio_write64(val, addr);
		break;
	}
}

#endif /* _IO_H defined */

/**
 * @}
 */
