/*
 * Copyright (C) 2019-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef IOREQ_H
#define IOREQ_H

#include <types.h>
#include <acrn_common.h>
#include <list.h>

/**
 * @addtogroup vp-dm_io-req
 *
 * @{
 */

/**
 * @file
 * @brief I/O Emulation
 *
 * This file defines the structs used in emulating I/O and MMIO operations based on the VMCS I/O bitmap and EPT.
 * It also declares the relevant functions.
 */

/**
 * @brief Internal representation of an I/O request.
 *
 * This structure encapsulates the details of an I/O request, which can be of various types
 * such as PIO (Programmed I/O), MMIO (Memory-Mapped I/O), or others. It is used to handle
 * I/O operations in a virtualized environment.
 */
struct io_request {
	/**
	 * @brief Type of the request (PIO, MMIO, etc).
	 *
	 * Refer to acrn_io_request for detailed description of I/O request types.
	 */
	uint32_t	io_type;

	/**
	 * @brief Details of this request in the same format as acrn_io_request.
	 */
	union {
		struct acrn_pio_request		pio_request;	/**< Details for PIO request */
		struct acrn_pci_request		pci_request;	/**< Details for PCI request */
		struct acrn_mmio_request	mmio_request;	/**< Details for MMIO request */
		uint64_t			data[8];		/**< Generic data array */
	} reqs;
};

/**
 * @brief Structure representing an asynchronous I/O descriptor.
 *
 * This structure is used to manage asynchronous I/O operations. It contains
 * information about the asynchronous I/O and is linked in a list for
 * managing multiple asynchronous operations. It is typically used in
 * environments where non-blocking I/O is required, allowing for efficient
 * handling of I/O without stalling the execution of the program.
 */
struct asyncio_desc {
	struct acrn_asyncio_info	asyncio_info;	/**< Information about the asynchronous I/O operation. */
	struct list_head			list;	/**< List node for linking multiple asynchronous I/O descriptors. */
};


/**
 * @brief Definition of an I/O port range.
 *
 * This structure represents a range of I/O ports, defined by a base
 * address and a length. It is used to specify the range of I/O ports
 * that a virtual machine can access. This is particularly useful in
 * virtualized environments where specific I/O port ranges need to be
 * allocated to different virtual machines to prevent conflicts and
 * ensure proper isolation.
 */
struct vm_io_range {
	uint16_t	base;	/**< I/O port base address. */
	uint16_t	len;	/**< Length of the I/O port range. */
};

struct vm_io_handler_desc;
struct acrn_vm;
struct acrn_vcpu;

/**
 * @brief Function pointer type for I/O read operations.
 *
 * This type defines a function pointer for performing I/O read
 * operations in a virtualized environment.
 */
typedef
bool (*io_read_fn_t)(struct acrn_vcpu *vcpu, uint16_t port, size_t size);

/**
 * @brief Function pointer type for I/O write operations.
 *
 * This type defines a function pointer for performing I/O write
 * operations in a virtualized environment.
 */
typedef
bool (*io_write_fn_t)(struct acrn_vcpu *vcpu, uint16_t port, size_t size, uint32_t val);

/**
 * @brief Describes a single I/O handler description entry.
 *
 * This structure is used to define a handler for a specific range of I/O ports.
 * It includes the start and end of the port range, as well as function pointers
 * for handling read and write operations on those ports. This is typically used
 * in a hypervisor environment to manage I/O operations for virtual machines,
 * allowing the hypervisor to intercept and handle I/O requests made by the guest
 * operating systems.
 */
struct vm_io_handler_desc {
	uint16_t	port_start;	/**< The base port number of the I/O range for this description. */
	uint16_t	port_end;	/**< The last port number of the I/O range for this description (non-inclusive). */

	/**
	 * @brief A pointer to the "read" function.
	 *
	 * The read function is called from the hypervisor whenever
	 * a read access to a range described in "ranges" occurs.
	 * The arguments to the callback are:
	 *
	 * - The address of the port to read from.
	 * - The width of the read operation (1, 2, or 4).
	 *
	 * The implementation must return the port's content as
	 * byte, word, or doubleword (depending on the width).
	 *
	 * If the pointer is null, a read of 1's is assumed.
	 */
	io_read_fn_t	io_read;

	/**
	 * @brief A pointer to the "write" function.
	 *
	 * The write function is called from the hypervisor code
	 * whenever a write access to a range described in "ranges"
	 * occurs. The arguments to the callback are:
	 *
	 * - The address of the port to write to.
	 * - The width of the write operation (1, 2, or 4).
	 * - The value to write as byte, word, or doubleword
	 *   (depending on the width).
	 *
	 * The implementation must write the value to the port.
	 *
	 * If the pointer is null, the write access is ignored.
	 */
	io_write_fn_t   io_write;
};

/**
 * @brief Function pointer type for handling MMIO operations and range checks.
 *
 * This type defines a function pointer for handling memory-mapped I/O (MMIO)
 * operations and performing range checks within a hypervisor.
 */
typedef int32_t (*hv_mem_io_handler_t)(struct io_request *io_req, void *handler_private_data);

/**
 * @brief Structure for MMIO handler node.
 *
 * This structure represents a node in a linked list of memory-mapped I/O (MMIO)
 * handlers. Each node contains information about a specific range of memory
 * addresses and the handler function responsible for managing read and write
 * operations within that range. It is used in environments where MMIO access
 * needs to be emulated or intercepted, such as in hypervisors or virtualized
 * systems.
 */
struct mem_io_node {
	bool	hold_lock;	/**< Indicates whether a lock needs to be held when handling the MMIO access. */

	/**
	 * @brief A pointer to the handler function.
	 *
	 * This function is responsible for handling MMIO accesses to the specified range.
	 * It is called whenever an MMIO operation occurs within the range defined by
	 * `range_start` and `range_end`.
	 */
	hv_mem_io_handler_t	read_write;

	/**
	 * @brief Private data used by the handler.
	 *
	 * This is a pointer to any data specified at registration. It is passed to the
	 * handler function whenever the handler is called, allowing the handler to
	 * access context-specific information.
	 */
	void	*handler_private_data;

	/**
	 * @brief The struct to make a bi-directional linked list.
	 *
	 * This field is used to link multiple `mem_io_node` structures together, forming
	 * a bi-directional linked list. This allows for efficient traversal and management
	 * of multiple MMIO handler nodes.
	 */
	struct list_head	list;

	/**
	 * @brief The starting address of the MMIO range.
	 *
	 * This member, in conjunction with `range_end`, specifies the address range that
	 * this handler is expected to emulate. The range is inclusive of `range_start`.
	 */
	uint64_t	range_start;

	/**
	 * @brief The ending address of the MMIO range.
	 *
	 * The `range_start` (inclusive) and `range_end` (exclusive) together define the
	 * address range that this handler is expected to emulate. The bytes to be accessed
	 * must completely fall within this range before the handler is called to emulate
	 * that access. Specifically, the condition:
	 *
	 *	`range_start <= address < address + size <= range_end`
	 *
	 * must be satisfied, where `address` and `size` are the starting address of the
	 * MMIO access and the number of bytes to be accessed, respectively. Otherwise,
	 * the behavior is undefined.
	 */
	uint64_t	range_end;
};

/* External Interfaces */

void reset_vm_ioreqs(struct acrn_vm *vm);
uint32_t get_io_req_state(struct acrn_vm *vm, uint16_t vcpu_id);
void set_io_req_state(struct acrn_vm *vm, uint16_t vcpu_id, uint32_t state);
void set_hsm_notification_vector(uint32_t vector);
uint32_t get_hsm_notification_vector(void);
int32_t emulate_io(struct acrn_vcpu *vcpu, struct io_request *io_req);

void   register_pio_emulation_handler(struct acrn_vm *vm, uint32_t pio_idx,
		const struct vm_io_range *range, io_read_fn_t io_read_fn_ptr, io_write_fn_t io_write_fn_ptr);

void register_mmio_emulation_handler(struct acrn_vm *vm,
	hv_mem_io_handler_t read_write, uint64_t start,
	uint64_t end, void *handler_private_data, bool hold_lock);

void unregister_mmio_emulation_handler(struct acrn_vm *vm,
					uint64_t start, uint64_t end);
void deinit_emul_io(struct acrn_vm *vm);

int init_asyncio(struct acrn_vm *vm, struct shared_buf *sbuf);

int add_asyncio(struct acrn_vm *vm, const struct acrn_asyncio_info *async_info);

int remove_asyncio(struct acrn_vm *vm, const struct acrn_asyncio_info *async_info);
/**
 * @}
 */

#endif /* IOREQ_H */
