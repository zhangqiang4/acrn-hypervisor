/*
 * Copyright (C) 2019-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <errno.h>
#include <asm/lib/atomic.h>
#include <io_req.h>
#include <asm/guest/vcpu.h>
#include <asm/guest/vm.h>
#include <asm/guest/instr_emul.h>
#include <asm/guest/vmexit.h>
#include <asm/vmx.h>
#include <asm/guest/ept.h>
#include <asm/pgtable.h>
#include <trace.h>
#include <logmsg.h>

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
 * @brief Signal HSM (Hypervisor Service Module).
 *
 * Trigger an interrupt to signal HSM in the Service OS. HSM distributes the requests in the buffer to DM.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark Service VM has started up.
 */
void arch_fire_hsm_interrupt(void)
{
	/*
	 * use vLAPIC to inject vector to Service VM vcpu 0 if vlapic is enabled
	 * otherwise, send IPI hardcoded to BSP_CPU_ID
	 */
	struct acrn_vm *service_vm;
	struct acrn_vcpu *vcpu;

	service_vm = get_service_vm();
	vcpu = vcpu_from_vid(service_vm, BSP_CPU_ID);

	if (is_lapic_pt_configured(service_vm)) {
		uint32_t papic_id = per_cpu(lapic_id, pcpuid_from_vcpu(vcpu));
		msr_write(MSR_IA32_EXT_APIC_ICR, (((uint64_t)papic_id) << 32U) | get_hsm_notification_vector());
	} else {
		vlapic_set_intr(vcpu, get_hsm_notification_vector(), LAPIC_TRIG_EDGE);
	}

}

/**
 * @brief General complete-work for port I/O emulation.
 *
 * This function is called in the completion workflow for port I/O operations emulated
 * by the device model. For read operations, emulate the behavior of the trapped
 * instruction and update the RAX in the guest context.
 *
 * @param[inout] vcpu The virtual CPU which triggers the VM exit on I/O instruction.
 * @param[in] io_req Pointer to the I/O operation request info.
 *
 * @return None.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark This function must be called when \p io_req is completed, after
 * either a previous call to emulate_io() returning 0 or the corresponding I/O
 * request having transferred to the COMPLETE state.
 */
void
emulate_pio_complete(struct acrn_vcpu *vcpu, const struct io_request *io_req)
{
	const struct acrn_pio_request *pio_req = &io_req->reqs.pio_request;
	uint64_t mask = 0xFFFFFFFFUL >> (32UL - (8UL * pio_req->size));

	if (pio_req->direction == ACRN_IOREQ_DIR_READ) {
		uint64_t value = (uint64_t)pio_req->value;
		uint64_t rax = vcpu_get_gpreg(vcpu, CPU_REG_RAX);

		rax = ((rax) & ~mask) | (value & mask);
		vcpu_set_gpreg(vcpu, CPU_REG_RAX, rax);
	}
}

/**
 * @brief The handler of VM exits on I/O instructions.
 *
 * Guest software attempted to execute an I/O instruction to access the ports which a bit in
 * the I/O bitmap was 1 cause this VM-exit. This handler deals with the I/O instruction exit
 * and implements PIO emulation.
 *
 * This function decodes I/O Instruction Size of access, Direction of the attempted access and
 * Port number from Exit Qualification for I/O Instructions (refer to Table 29-5. Exit Qualification
 * for I/O Instructions, Vol 3, Intel® 64 and IA-32 Architectures Software Developer's Manual Order
 * Number: 325462-078US) and call emulate_io to simulate the read/write option based on its
 * instructions.
 *
 * @param[inout] vcpu The virtual CPU which triggers the VM exit on I/O instruction
 *
 * @return An int32_t value indicating whether emulation is successful.
 *
 * @retval 0 PIO emulation success.
 * @retval -ENODEV PIO read/write failure.
 * @retval -EINVAL Fail to send io_req to Service VM.
 *
 * @pre N/A
 *
 * @post N/A
 */
int32_t pio_instr_vmexit_handler(struct acrn_vcpu *vcpu)
{
	int32_t status;
	uint64_t exit_qual;
	uint32_t mask;
	struct io_request *io_req = &vcpu->req;
	struct acrn_pio_request *pio_req = &io_req->reqs.pio_request;

	exit_qual = vcpu->arch.exit_qualification;

	io_req->io_type = ACRN_IOREQ_TYPE_PORTIO;
	pio_req->size = vm_exit_io_instruction_size(exit_qual) + 1UL;
	pio_req->address = vm_exit_io_instruction_port_number(exit_qual);
	if (vm_exit_io_instruction_access_direction(exit_qual) == 0UL) {
		mask = 0xFFFFFFFFU >> (32U - (8U * pio_req->size));
		pio_req->direction = ACRN_IOREQ_DIR_WRITE;
		pio_req->value = (uint32_t)vcpu_get_gpreg(vcpu, CPU_REG_RAX) & mask;
	} else {
		pio_req->direction = ACRN_IOREQ_DIR_READ;
	}

	TRACE_4I(TRACE_VMEXIT_IO_INSTRUCTION,
		(uint32_t)pio_req->address,
		(uint32_t)pio_req->direction,
		(uint32_t)pio_req->size,
		0U);

	status = emulate_io(vcpu, io_req);

	return status;
}

/**
 * @brief The handler of VM exits on EPT violation.
 *
 * Guest physical addresses are translated by traversing a set of EPT paging structures to produce host physical
 * addresses that are used to access memory. When an attempt to access memory with a guest physical address is
 * disallowed by the configuration of the EPT paging structures, an EPT violation occurs.
 *
 * This function decodes the access causing the EPT violation. If the EPT violation was caused by an instruction
 * fetch, it add execute rights for the page covering the guest-physical address that caused the EPT
 * violation and retain the RIP. If the violation was not caused by an instruction fetch, it decodes the Size of the
 * access, the direction of the attempted access, and the guest-physical address related to EPT violation. It then
 * calls decode_instruction to Decode VM instruction. If the access causing the EPT violation was a data write, it
 * calls emulate_instruction to emulate the operation of the instruction on the target address. Finally it
 * calls emulate_io to emulate the behavior of target IO device.
 *
 * @param[inout] vcpu The virtual CPU which triggers the VM exit on I/O instruction.
 *
 * @return An int32_t value indicating whether emulation is successful.
 *
 * @retval 0 Successfully handled the EPT violation.
 * @retval -1 The instruction being emulated is unknown.
 * @retval -EIO The request spans multiple devices and cannot be emulated.
 * @retval -EFAULT Emulation of the instruction failed.
 * @retval -EINVAL Failed to deliver io_req to the Service VM.
 *
 * @pre N/A
 *
 * @post N/A
 */
int32_t ept_violation_vmexit_handler(struct acrn_vcpu *vcpu)
{
	int32_t status = -EINVAL, ret;
	uint64_t exit_qual;
	uint64_t gpa;
	struct io_request *io_req = &vcpu->req;
	struct acrn_mmio_request *mmio_req = &io_req->reqs.mmio_request;

	/* Handle page fault from guest */
	exit_qual = vcpu->arch.exit_qualification;
	/* Get the guest physical address */
	gpa = exec_vmread64(VMX_GUEST_PHYSICAL_ADDR_FULL);

	TRACE_2L(TRACE_VMEXIT_EPT_VIOLATION, exit_qual, gpa);

	/*caused by instruction fetch */
	if ((exit_qual & 0x4UL) != 0UL) {
		/* TODO: check whether the gpa is not a MMIO address. */
		ept_modify_mr(vcpu->vm, (uint64_t *)vcpu->vm->arch_vm.nworld_eptp,
				gpa & PAGE_MASK, PAGE_SIZE, EPT_EXE, 0UL);
		vcpu_retain_rip(vcpu);
		status = 0;
	} else {

		io_req->io_type = ACRN_IOREQ_TYPE_MMIO;

		/* Specify if read or write operation */
		if ((exit_qual & 0x2UL) != 0UL) {
			/* Write operation */
			mmio_req->direction = ACRN_IOREQ_DIR_WRITE;
			mmio_req->value = 0UL;

			/* XXX: write access while EPT perm RX -> WP */
			if ((exit_qual & 0x38UL) == 0x28UL) {
				io_req->io_type = ACRN_IOREQ_TYPE_WP;
			}
		} else {
			/* Read operation */
			mmio_req->direction = ACRN_IOREQ_DIR_READ;

			/* TODO: Need to determine how sign extension is determined for
			 * reads
			 */
		}

		/* Adjust IPA appropriately and OR page offset to get full IPA of abort
		 */
		mmio_req->address = gpa;

		ret = decode_instruction(vcpu, true);
		if (ret > 0) {
			mmio_req->size = (uint64_t)ret;
			/*
			 * For MMIO write, ask DM to run MMIO emulation after
			 * instruction emulation. For MMIO read, ask DM to run MMIO
			 * emulation at first.
			 */

			/* Determine value being written. */
			if (mmio_req->direction == ACRN_IOREQ_DIR_WRITE) {
				status = emulate_instruction(vcpu);
				if (status != 0) {
					ret = -EFAULT;
				}
			}

			if (ret > 0) {
				status = emulate_io(vcpu, io_req);
			}
		} else {
			if (ret == -EFAULT) {
				pr_info("page fault happen during decode_instruction");
				status = 0;
			}
		}
		if (ret <= 0) {
			pr_acrnlog("Guest Linear Address: 0x%016lx", exec_vmread(VMX_GUEST_LINEAR_ADDR));
			pr_acrnlog("Guest Physical Address address: 0x%016lx", gpa);
		}
	}

	return status;
}

/**
 * @brief Allow a VM to access a port I/O range by clearing the corresponding bits in its IO bitmap.
 *
 * This API enables direct access from the given \p vm to the port I/O space
 * starting from \p port_address to \p port_address + \p nbytes - 1.
 *
 * @param[inout] vm The VM whose port I/O access permissions are to be changed.
 * @param[in] port_address The start address of the port I/O range.
 * @param[in] nbytes The size of the range, in bytes.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
void allow_guest_pio_access(struct acrn_vm *vm, uint16_t port_address,
		uint32_t nbytes)
{
	uint16_t address = port_address;
	uint32_t *b;
	uint32_t i;

	b = (uint32_t *)vm->arch_vm.io_bitmap;
	for (i = 0U; i < nbytes; i++) {
		b[address >> 5U] &= ~(1U << (address & 0x1fU));
		address++;
	}
}

/**
 * @brief Deny a VM access to a port I/O range by setting the corresponding bits in its IO bitmap.
 *
 * This API disables direct access from the given \p vm to the port I/O space
 * starting from \p port_address to \p port_address + \p nbytes - 1.
 *
 * @param[inout] vm The VM whose port I/O access permissions are to be changed.
 * @param[in] port_address The start address of the port I/O range.
 * @param[in] nbytes The size of the range, in bytes.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
void deny_guest_pio_access(struct acrn_vm *vm, uint16_t port_address,
		uint32_t nbytes)
{
	uint16_t address = port_address;
	uint32_t *b;
	uint32_t i;

	b = (uint32_t *)vm->arch_vm.io_bitmap;
	for (i = 0U; i < nbytes; i++) {
		b[address >> 5U] |= (1U << (address & 0x1fU));
		address++;
	}
}

/**
 * @}
 */