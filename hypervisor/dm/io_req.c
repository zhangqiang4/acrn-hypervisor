/*
 * Copyright (C) 2019-2025 Intel Corporation.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <asm/guest/vm.h>
#include <asm/irq.h>
#include <errno.h>
#include <logmsg.h>
#include <sbuf.h>


/**
 * @defgroup vp-dm_io-req vp-dm.io-req
 * @ingroup vp-dm
 * @brief PIO/MMIO read & write emulation module
 *
 * This module emulates I/O and MMIO based on the VMCS I/O bitmap and EPT. VMCS traps violate I/O and MMIO options
 * to the hypervisor. Then the hypervisor determines if the emulation should be implemented by the hypervisor or
 * the device model.
 *
 * @{
 */

 /**
 * @file
 * @brief I/O Emulation
 *
 * This file Emulate IO/MMIO base on VMCS I/O bitmap and EPT. VMCS trap violate IO/MMIO option to hypervisor.
 *
 */

 #define DBG_LEVEL_IOREQ 6U /**< Debug level for I/O request logging. */

 /**
 * @brief Notification vector for Hypervisor Service Module.
 *
 * This global variable holds the vector number used for notifications related to Hypervisor Service Module (HSM)
 * in the ACRN hypervisor. The HSM upcall is from the hypervisor to the guest VM. The hypervisor and Service VM
 * both use the same vector (0xF3) by default, which is reserved as the x86 platform IPI vector for hypervisor
 * notification to the Service VM.
 */
static uint32_t acrn_hsm_notification_vector = HYPERVISOR_CALLBACK_HSM_VECTOR;

#define MMIO_DEFAULT_VALUE_SIZE_1 (0xFFUL) /**< Default MMIO value for 1-byte size. */
#define MMIO_DEFAULT_VALUE_SIZE_2 (0xFFFFUL) /**< Default MMIO value for 2-byte size.. */
#define MMIO_DEFAULT_VALUE_SIZE_4 (0xFFFFFFFFUL) /**< Default MMIO value for 4-byte size. */
#define MMIO_DEFAULT_VALUE_SIZE_8 (0xFFFFFFFFFFFFFFFFUL) /**< Default MMIO value for 8-byte size. */


#if defined(HV_DEBUG)
__unused static void acrn_print_request(uint16_t vcpu_id, const struct acrn_io_request *req)
{
	switch (req->type) {
	case ACRN_IOREQ_TYPE_MMIO:
		dev_dbg(DBG_LEVEL_IOREQ, "[vcpu_id=%hu type=MMIO]", vcpu_id);
		dev_dbg(DBG_LEVEL_IOREQ,
			"gpa=0x%lx, R/W=%d, size=%ld value=0x%lx processed=%lx",
			req->reqs.mmio_request.address,
			req->reqs.mmio_request.direction,
			req->reqs.mmio_request.size,
			req->reqs.mmio_request.value,
			req->processed);
		break;
	case ACRN_IOREQ_TYPE_PORTIO:
		dev_dbg(DBG_LEVEL_IOREQ, "[vcpu_id=%hu type=PORTIO]", vcpu_id);
		dev_dbg(DBG_LEVEL_IOREQ,
			"IO=0x%lx, R/W=%d, size=%ld value=0x%lx processed=%lx",
			req->reqs.pio_request.address,
			req->reqs.pio_request.direction,
			req->reqs.pio_request.size,
			req->reqs.pio_request.value,
			req->processed);
		break;
	default:
		dev_dbg(DBG_LEVEL_IOREQ, "[vcpu_id=%hu type=%d] NOT support type",
			vcpu_id, req->type);
		break;
	}
}
#endif

/**
 * @brief Reset all IO requests status of the VM.
 *
 * Reset all IO requests status of the VM to ACRN_IOREQ_STATE_FREE.
 * This function is called when reset a VM.
 *
 * @param[in] vm Pointer to a VM whose I/O requests needs to be reset.
 *
 * @return None.
 *
 * @pre N/A
 *
 * @post N/A
 */
void reset_vm_ioreqs(struct acrn_vm *vm)
{
	uint16_t i;

	for (i = 0U; i < ACRN_IO_REQUEST_MAX; i++) {
		set_io_req_state(vm, i, ACRN_IOREQ_STATE_FREE);
	}
}

/**
 * @brief Check if there is conflicting asyncio information in the queue.
 *
 * Search the asyncio descriptor queue to find if there is conflicting asyncio information.
 *
 * @param[in] vm Pointer to the target VM to deliver asynchronous I/O.
 * @param[in] async_info Pointer to asyncio information to check.
 *
 * @return true if a conflicting asyncio information is found, false if none.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark vm->asyncio_lock is held
 */
static bool asyncio_is_conflict(struct acrn_vm *vm,
	const struct acrn_asyncio_info *async_info)
{
	struct list_head *pos;
	struct asyncio_desc *p;
	struct acrn_asyncio_info *info;
	bool ret = false;

	/* When either one's match_data is 0, the data matching will be skipped. */
	list_for_each(pos, &vm->aiodesc_queue) {
		p = container_of(pos, struct asyncio_desc, list);
		info = &(p->asyncio_info);
		if ((info->addr == async_info->addr) &&
			(info->type == async_info->type) &&
			((info->match_data == 0U) || (async_info->match_data == 0U) ||
				(info->data == async_info->data))) {
			ret = true;
			break;
		}
	}

	return ret;
}

/**
 * @brief Add asyncio to queue.
 *
 * Search address in \p vm->aiodesc_queue. If not added, add the io address in the \p async_info
 * to \p vm->aiodesc_queue.
 *
 * @param[in] vm Pointer to target VM data structure.
 * @param[in] async_info Pointer to asyncio info to add.
 *
 * @return 0 on successfully adding asyncio information to the Async IO descriptor queue, otherwise returns -1.
 *
 * @pre N/A
 *
 * @post N/A
 */
int add_asyncio(struct acrn_vm *vm, const struct acrn_asyncio_info *async_info)
{
	uint32_t i;
	int ret = -1;
	bool b_conflict;
	struct asyncio_desc *desc;

	if (async_info->addr != 0UL) {
		spinlock_obtain(&vm->asyncio_lock);
		b_conflict = asyncio_is_conflict(vm, async_info);
		if (!b_conflict) {
			for (i = 0U; i < ACRN_ASYNCIO_MAX; i++) {
				desc = &(vm->aio_desc[i]);
				if ((desc->asyncio_info.addr == 0UL) && (desc->asyncio_info.fd == 0UL)) {
					(void)memcpy_s(&(desc->asyncio_info), sizeof(struct acrn_asyncio_info),
						async_info, sizeof(struct acrn_asyncio_info));
					INIT_LIST_HEAD(&(desc->list));
					list_add(&(desc->list), &vm->aiodesc_queue);
					ret = 0;
					break;
				}
			}
			spinlock_release(&vm->asyncio_lock);
			if (i == ACRN_ASYNCIO_MAX) {
				pr_fatal("too much fastio, would not support!");
			}
		} else {
			spinlock_release(&vm->asyncio_lock);
			pr_err("%s, already registered!", __func__);
		}
	} else {
		pr_err("%s: base = 0 is not supported!", __func__);
	}
	return ret;
}

/**
 * @brief Remove asyncio node.
 *
 * Search address in \p vm->aiodesc_queue. If matched, remove the io address in the \p async_info
 * from \p vm->aiodesc_queue and clear \p aio_desc array.
 *
 * @param[in] vm Pointer to target VM data structure.
 * @param[in] async_info Pointer to asyncio info to remove.
 *
 * @return 0 on successfully removing asyncio information from the Async IO descriptor queue, otherwise returns -1.
 *
 * @pre N/A
 *
 * @post N/A
 */
int remove_asyncio(struct acrn_vm *vm, const struct acrn_asyncio_info *async_info)
{
	uint32_t i;
	int ret = -1;
	struct asyncio_desc *desc;
	struct acrn_asyncio_info *info;

	if (async_info->addr != 0UL) {
		spinlock_obtain(&vm->asyncio_lock);
		for (i = 0U; i < ACRN_ASYNCIO_MAX; i++) {
			desc = &(vm->aio_desc[i]);
			info = &(desc->asyncio_info);
			if ((info->type == async_info->type)
					&& (info->addr == async_info->addr)
					&& (info->fd == async_info->fd)
					&& ((info->match_data == 0U) == (async_info->match_data == 0U))
					&& (info->data == async_info->data)) {
				list_del_init(&(desc->list));
				(void)memset(desc, 0, sizeof(vm->aio_desc[0]));
				ret = 0;
				break;
			}
		}
		spinlock_release(&vm->asyncio_lock);
		if (i == ACRN_ASYNCIO_MAX) {
			pr_fatal("Failed to find asyncio req on addr: %lx!", async_info->addr);
		}
	} else {
		pr_err("%s: base = 0 is not supported!", __func__);
	}
	return ret;
}

/**
 * @brief Check if the io request has finished processing.
 *
 * Read the state of io request in share memory. Check if DM has finish processing the io request.
 *
 * @param[in] vcpu Check IO requests state for the vCPU.
 *
 * @return True on complete, false on processing.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline bool has_complete_ioreq(const struct acrn_vcpu *vcpu)
{
	return (get_io_req_state(vcpu->vm, vcpu->vcpu_id) == ACRN_IOREQ_STATE_COMPLETE);
}

/**
 * @brief Find match \p asyncio_desc.
 *
 * Find a match \p asyncio_desc which address is consistent with the io request.
 *
 * @param[in] vcpu Pointer to vcpu which process the io request.
 * @param[in] io_req The I/O request holding the details of the IO access.
 *
 * @return Return \p asyncio_desc pointer on success, otherwise return NULL.
 *
 * @pre N/A
 *
 * @post N/A
 */
static struct asyncio_desc *get_asyncio_desc(struct acrn_vcpu *vcpu, const struct io_request *io_req)
{
	uint64_t addr = 0UL;
	uint32_t type;
	uint64_t value;
	struct list_head *pos;
	struct asyncio_desc *iter_desc;
	struct acrn_asyncio_info *iter_info;
	struct acrn_vm *vm = vcpu->vm;
	struct asyncio_desc *ret = NULL;
	struct shared_buf *sbuf =
		(struct shared_buf *)vm->sw.asyncio_sbuf;

	if (sbuf != NULL) {
		switch (io_req->io_type) {
		case ACRN_IOREQ_TYPE_PORTIO:
			addr = io_req->reqs.pio_request.address;
			value = io_req->reqs.pio_request.value;
			type = ACRN_ASYNCIO_PIO;
			break;

		case ACRN_IOREQ_TYPE_MMIO:
			addr = io_req->reqs.mmio_request.address;
			value = io_req->reqs.mmio_request.value;
			type = ACRN_ASYNCIO_MMIO;
			break;
		default:
			/* no actions are required */
			break;
		}

		if (addr != 0UL) {
			spinlock_obtain(&vm->asyncio_lock);
				list_for_each(pos, &vm->aiodesc_queue) {
					iter_desc = container_of(pos, struct asyncio_desc, list);
					iter_info = &(iter_desc->asyncio_info);
					if ((iter_info->addr == addr) && (iter_info->type == type) &&
						((iter_info->match_data == 0U) || (iter_info->data == value))) {
						ret = iter_desc;
						break;
					}
				}
			spinlock_release(&vm->asyncio_lock);
		}
	}

	return ret;

}

/**
 * @brief Deliver async io request to Service VM.
 *
 * Copy the async io request to share memory and to then fire HSM Vector Interrupt to
 * notify HSM to handle it. Return immediately after delivery.
 *
 * @param[in] vcpu The virtual CPU that triggers the MMIO access
 * @param[in] asyncio_fd The I/O request holding the details of the MMIO access
 *
 * @return 0 on successful delivery of asyncio fd to the Service VM, -ENODEV if no shared buffer is available.
 *
 * @pre N/A
 *
 * @post N/A
 */
static int acrn_insert_asyncio(struct acrn_vcpu *vcpu, uint64_t asyncio_fd)
{
	struct acrn_vm *vm = vcpu->vm;
	struct shared_buf *sbuf =
		(struct shared_buf *)vm->sw.asyncio_sbuf;
	int ret = -ENODEV;

	if (sbuf != NULL) {
		spinlock_obtain(&vm->asyncio_lock);
		while (sbuf_put(sbuf, (uint8_t *)&asyncio_fd, sizeof(asyncio_fd)) == 0U) {
			/* sbuf is full, try later.. */
			spinlock_release(&vm->asyncio_lock);
			asm_pause();
			if (need_reschedule(pcpuid_from_vcpu(vcpu))) {
				schedule();
			}
			spinlock_obtain(&vm->asyncio_lock);
		}

		spinlock_release(&vm->asyncio_lock);
		arch_fire_hsm_interrupt();
		ret = 0;
	}
	return ret;
}

/**
 * @brief Deliver \p io_req to the Service VM.
 *
 * Insert \p io_req into \p vm->sw.io_shared_page, which is memory shared between the hypervisor and the Service OS
 * specifically for I/O requests. Then trigger the HSM interrupt. Suspend \p vcpu until its completion.
 *
 * If I/O completion polling mode is enabled, periodically check whether the DM has completed the processing
 * of the I/O request. The state used for checking is written inside shared memory between the Hypervisor and
 * the Service OS.
 * If I/O completion polling mode is disabled, the thread will sleep until it is awakened by the \p VCPU_EVENT_IOREQ
 * event. When the DM finishes completing the I/O request, the HSM sends a hypercall as \p HC_NOTIFY_REQUEST_FINISH,
 * which wakes the sleeping thread.
 *
 * Rescheduling will be conducted as needed in this process.
 *
 * @param[in] vcpu The virtual CPU that triggers the MMIO access
 * @param[in] io_req The I/O request holding the details of the MMIO access
 *
 * @retval 0 Successfully insert the io request.
 * @retval -EINVAL No usable buffer for io request.
 *
 * @pre N/A
 *
 * @post N/A
 */
static int32_t acrn_insert_request(struct acrn_vcpu *vcpu, const struct io_request *io_req)
{
	union acrn_io_request_buffer *req_buf = NULL;
	struct acrn_io_request *acrn_io_req;
	bool is_polling = false;
	int32_t ret = 0;
	uint16_t cur;

	if ((vcpu->vm->sw.io_shared_page != NULL)
		 && (get_io_req_state(vcpu->vm, vcpu->vcpu_id) == ACRN_IOREQ_STATE_FREE)) {

		req_buf = (union acrn_io_request_buffer *)(vcpu->vm->sw.io_shared_page);
		cur = vcpu->vcpu_id;

		stac();
		acrn_io_req = &req_buf->req_slot[cur];
		/* ACRN insert request to HSM and inject upcall */
		acrn_io_req->type = io_req->io_type;
		(void)memcpy_s(&acrn_io_req->reqs, sizeof(acrn_io_req->reqs),
			&io_req->reqs, sizeof(acrn_io_req->reqs));
		if (vcpu->vm->sw.is_polling_ioreq) {
			acrn_io_req->completion_polling = 1U;
			is_polling = true;
		}
		clac();

		/* Before updating the acrn_io_req state, enforce all fill acrn_io_req operations done */
		cpu_write_memory_barrier();

		/* Must clear the signal before we mark req as pending
		 * Once we mark it pending, HSM may process req and signal us
		 * before we perform upcall.
		 * because HSM can work in pulling mode without wait for upcall
		 */
		set_io_req_state(vcpu->vm, vcpu->vcpu_id, ACRN_IOREQ_STATE_PENDING);

		/* signal HSM */
		arch_fire_hsm_interrupt();

		/* Polling completion of the request in polling mode */
		if (is_polling) {
			while (true) {
				if (has_complete_ioreq(vcpu)) {
					/* we have completed ioreq pending */
					break;
				}
				asm_pause();
				if (need_reschedule(pcpuid_from_vcpu(vcpu))) {
					schedule();
				}
			}
		} else {
			wait_event(&vcpu->events[VCPU_EVENT_IOREQ]);
		}
	} else {
		ret = -EINVAL;
	}

	return ret;
}

/**
 * @brief Get IO requests state
 *
 * Get IO requests state for the vCPU. This state is used to synchronize the processing state
 * of io request between HSM and hypervisor.
 *
 * @param[in] vm Pointer that points to VM data structure.
 * @param[in] vcpu_id  'vcpu_id' for which vcpu to get IO requests state.
 *
 * @return The io request state for the vCPU.
 *
 * @pre vcpu_id < ACRN_IO_REQUEST_MAX
 *
 * @post N/A
 */
uint32_t get_io_req_state(struct acrn_vm *vm, uint16_t vcpu_id)
{
	uint32_t state;
	union acrn_io_request_buffer *req_buf = NULL;
	struct acrn_io_request *acrn_io_req;

	req_buf = (union acrn_io_request_buffer *)vm->sw.io_shared_page;
	if (req_buf == NULL) {
	        state =  0xffffffffU;
	} else {
		stac();
		acrn_io_req = &req_buf->req_slot[vcpu_id];
		state = acrn_io_req->processed;
		clac();
	}

	return state;
}

/**
 * @brief Set IO requests status
 *
 * Set IO requests state for the vCPU. This state is used to synchronize the processing state
 * of io request between HSM and hypervisor.
 *
 * @param[in] vm Pointer that points to VM data structure.
 * @param[in] vcpu_id 'vcpu_id' for which vcpu to set IO requests state.
 * @param[in] state The state to set ioreq.
 *
 * @return None.
 *
 * @pre vcpu_id < ACRN_IO_REQUEST_MAX
 *
 * @post N/A
 */
void set_io_req_state(struct acrn_vm *vm, uint16_t vcpu_id, uint32_t state)
{
	union acrn_io_request_buffer *req_buf = NULL;
	struct acrn_io_request *acrn_io_req;

	req_buf = (union acrn_io_request_buffer *)vm->sw.io_shared_page;
	if (req_buf != NULL) {
		stac();
		acrn_io_req = &req_buf->req_slot[vcpu_id];
		/*
		 * HV will only set processed to ACRN_IOREQ_STATE_PENDING or ACRN_IOREQ_STATE_FREE.
		 * we don't need to sfence here is that even if the Service-VM/DM sees the previous state,
		 * the only side effect is that it will defer the processing of the new IOReq.
		 * It won't lead wrong processing.
		 */
		acrn_io_req->processed = state;
		clac();
	}
}

/**
 * @brief Initialize asyncio module.
 *
 * This function used to initialize asyncio module. Set the share memory address for asyncio,
 * initialize \p aiodesc_queue and \p asyncio_lock.
 *
 * @param[in] vm Pointer to target VM data structure.
 * @param[in] sbuf Pointer to the share memory.
 *
 * @return Always return 0 to indicate a successful emulation of MMIO read/write.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark Asyncio module initializes when Device Model set share memory address by hypercall.
 * For a VM, it's effective only the first time this function execute.
 */
int init_asyncio(struct acrn_vm *vm, struct shared_buf *sbuf)
{
	vm->sw.asyncio_sbuf = sbuf;
	INIT_LIST_HEAD(&vm->aiodesc_queue);
	spinlock_init(&vm->asyncio_lock);

	return 0;
}

/**
 * @brief Set HSM notifier vector.
 *
 * Switch the vector by which hypervisor notify HSM in the Service OS kernel.
 *
 * @param[in] vector Vector id used to call HSM.
 *
 * @return None.
 *
 * @pre vector < NR_MAX_VECTOR && vector > VECTOR_DYNAMIC_START
 *
 * @post N/A
 */
void set_hsm_notification_vector(uint32_t vector)
{
	acrn_hsm_notification_vector = vector;
}

/**
 * @brief Get HSM notifier vector
 *
 * Get the vector by which hypervisor notify HSM in the Service OS kernel.
 *
 * @return Vector id used to notify HSM.
 *
 * @pre N/A
 *
 * @post N/A
 */
uint32_t get_hsm_notification_vector(void)
{
	return acrn_hsm_notification_vector;
}

/**
 * @brief General complete-work for MMIO emulation
 *
 * This function is called in the completion workflow for MMIO operations emulated
 * by the device model. For read operations, call emulate_instruction to do the
 * instruction emulation for which caused a trap.
 *
 * @param[in] vcpu The virtual CPU that triggers the MMIO access
 * @param[in] io_req The I/O request holding the details of the MMIO access
 *
 * @return None.
 *
 * @pre io_req->io_type == ACRN_IOREQ_TYPE_MMIO
 *
 * @post N/A
 *
 * @remark This function must be called when \p io_req is completed, after
 * either a previous call to emulate_io() returning 0 or the corresponding HSM
 * request transferring to the COMPLETE state.
 */
static void emulate_mmio_complete(struct acrn_vcpu *vcpu, const struct io_request *io_req)
{
	const struct acrn_mmio_request *mmio_req = &io_req->reqs.mmio_request;

	if (mmio_req->direction == ACRN_IOREQ_DIR_READ) {
		/* Emulate instruction and update vcpu register set */
		(void)emulate_instruction(vcpu);
	}
}

/**
 * @brief Common complete-work for all kinds of HSM requests for I/O emulation
 *
 * This function finishes the I/O request process which is emulated in the Service VM.
 * First, it copies the I/O request result value from the shared memory between the hypervisor
 * and the Service OS. Then, it clears the I/O request state for the vCPU to prepare for the
 * next I/O request.
 *
 * @param[in] vcpu The virtual CPU that triggers the IO access
 * @param[in] io_req Pointer to the I/O operation request info.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
static void complete_ioreq(struct acrn_vcpu *vcpu, struct io_request *io_req)
{
	union acrn_io_request_buffer *req_buf = NULL;
	struct acrn_io_request *acrn_io_req;

	req_buf = (union acrn_io_request_buffer *)(vcpu->vm->sw.io_shared_page);

	stac();
	acrn_io_req = &req_buf->req_slot[vcpu->vcpu_id];
	if (io_req != NULL) {
		switch (vcpu->req.io_type) {
		case ACRN_IOREQ_TYPE_PORTIO:
			io_req->reqs.pio_request.value = acrn_io_req->reqs.pio_request.value;
			break;

		case ACRN_IOREQ_TYPE_MMIO:
			io_req->reqs.mmio_request.value = acrn_io_req->reqs.mmio_request.value;
			break;

		default:
			/*no actions are required for other cases.*/
			break;
		}
	}

	/*
	 * Only HV will check whether processed is ACRN_IOREQ_STATE_FREE on per-vCPU before inject a ioreq.
	 * Only HV will set processed to ACRN_IOREQ_STATE_FREE when ioreq is done.
	 */
	acrn_io_req->processed = ACRN_IOREQ_STATE_FREE;
	clac();
}

/**
 * @brief Complete-work of HSM requests for port I/O emulation
 *
 * This is the completion workflow for port I/O operations emulated by the device model. In the process,
 * copies the I/O request result value from the shared memory between the hypervisor and the Service OS,
 * and clear the I/O request state in the shared memory to prepare for the next I/O request. Then, for
 * read operations, call emulate_pio_complete to emulate the behavior of the trapped
 * instruction and update the RAX in the guest context.
 *
 * @param[in] vcpu The virtual CPU that triggers the MMIO access.
 *
 * @return None.
 *
 * @pre vcpu->req.io_type == ACRN_IOREQ_TYPE_PORTIO
 *
 * @post N/A
 *
 * @remark This function must be called after the HSM request corresponding to
 * \p vcpu being transferred to the COMPLETE state.
 */
static void dm_emulate_pio_complete(struct acrn_vcpu *vcpu)
{
	struct io_request *io_req = &vcpu->req;

	complete_ioreq(vcpu, io_req);

	emulate_pio_complete(vcpu, io_req);
}

/**
 * @brief Complete-work of HSM requests for MMIO emulation
 *
 * This is the completion workflow for port I/O operations emulated by the device model. In the process,
 * copies the I/O request result value from the shared memory between the hypervisor and the Service OS,
 * and clear the I/O request state in the shared memory to prepare for the next I/O request. Then, for
 * read operations, call emulate_mmio_complete to do the instruction emulation for which caused a trap.
 *
 * @param[in] vcpu The virtual CPU that triggers the MMIO access.
 *
 * @return None.
 *
 * @pre vcpu->req.io_type == ACRN_IOREQ_TYPE_MMIO
 *
 * @post N/A
 *
 * @remark This function must be called after the HSM request corresponding to
 * \p vcpu being transferred to the COMPLETE state.
 */
static void dm_emulate_mmio_complete(struct acrn_vcpu *vcpu)
{
	struct io_request *io_req = &vcpu->req;

	complete_ioreq(vcpu, io_req);

	emulate_mmio_complete(vcpu, io_req);
}

/**
 * @brief General complete-work for all kinds of HSM requests for I/O emulation
 *
 * This is the general completion workflow for I/O operations emulated by the device model.
 * If vcpu is in Zombie state and will be destroyed soon, just mark ioreq done and don't resume vcpu.
 * In other states, this function processes the completion according to io type:
 * - For \p ACRN_IOREQ_TYPE_MMIO, call \p dm_emulate_mmio_complete to obtain the result value from the
 * Service OS, and clear the I/O request state in the shared memory to prepare for the next I/O request.
 * For read operations, emulate the behavior of the trapped instruction.
 * - For \p ACRN_IOREQ_TYPE_PORTIO and \p ACRN_IOREQ_TYPE_PCICFG, call \p dm_emulate_pio_complete to
 * obtain the result, mark ioreq done. For read operations, update the RAX register directly.
 * - For other io types, call \p complete_ioreq to mark ioreq done.
 *
 * @param[in] vcpu The virtual CPU that triggers the MMIO access
 *
 * @return None.
 *
 * @pre N/A
 *
 * @post N/A
 */
static void dm_emulate_io_complete(struct acrn_vcpu *vcpu)
{
	if (get_io_req_state(vcpu->vm, vcpu->vcpu_id) == ACRN_IOREQ_STATE_COMPLETE) {
		/*
		 * If vcpu is in Zombie state and will be destroyed soon. Just
		 * mark ioreq done and don't resume vcpu.
		 */
		if (vcpu->state == VCPU_ZOMBIE) {
			complete_ioreq(vcpu, NULL);
		} else {
			switch (vcpu->req.io_type) {
			case ACRN_IOREQ_TYPE_MMIO:
				dm_emulate_mmio_complete(vcpu);
				break;

			case ACRN_IOREQ_TYPE_PORTIO:
			case ACRN_IOREQ_TYPE_PCICFG:
				/*
				 * ACRN_IOREQ_TYPE_PORTIO on 0xcf8 & 0xcfc may switch to
				 * ACRN_IOREQ_TYPE_PCICFG in some cases. It works to apply the post-work
				 * for ACRN_IOREQ_TYPE_PORTIO on ACRN_IOREQ_TYPE_PCICFG because the
				 * format of the first 28 bytes of ACRN_IOREQ_TYPE_PORTIO &
				 * ACRN_IOREQ_TYPE_PCICFG requests are exactly the same and post-work
				 * is mainly interested in the read value.
				 */
				dm_emulate_pio_complete(vcpu);
				break;

			default:
				/*
				 * ACRN_IOREQ_TYPE_WP can only be triggered on writes which do
				 * not need post-work. Just mark the ioreq done.
				 */
				complete_ioreq(vcpu, NULL);
				break;
			}

		}
	}
}

/**
 * @brief Set a default value to PIO read request.
 *
 * Default PIO read handler for Service VM and Pre-launch VM :
 * - For PIO read,  set all bit to 1 as a default value.
 *
 * @param[in] vcpu Pointer to vCPU which receive the io request
 * @param[in] addr Unused parameter in this function
 * @param[in] width Bytes of the value.
 *
 * @return Always return true to indicate a successful emulation of PIO read.
 *
 * @pre width < 4U
 *
 * @post N/A
 */
static bool pio_default_read(struct acrn_vcpu *vcpu,
	__unused uint16_t addr, size_t width)
{
	struct acrn_pio_request *pio_req = &vcpu->req.reqs.pio_request;

	pio_req->value = (uint32_t)((1UL << (width * 8U)) - 1UL);

	return true;
}

/**
 * @brief Set a default value to PIO write request.
 *
 * Default PIO write handler for Service VM and Pre-launch VM :
 * - For PIO write,  do nothing just return success.
 *
 * @param[in] vcpu Pointer to vCPU which receive the io request.
 * @param[in] addr Unused parameter in this function.
 * @param[in] width Bytes of the value.
 * @param[in] v Unused parameter in this function.
 *
 * @return Always return true to indicate a successful emulation of PIO write.
 *
 * @pre N/A
 *
 * @post N/A
 */
static bool pio_default_write(__unused struct acrn_vcpu *vcpu, __unused uint16_t addr,
	__unused size_t width, __unused uint32_t v)
{
	return true; /* ignore write */
}

/**
 * @brief handle a MMIO access request on dummy address.
 *
 * This function is used to handle MMIO access on dummy address. The dummy address
 * is neither mapped to any host mmio address nor used to emulate any function. Write access
 * to dummy address will be ignored. Read access to dummy address will return all 1 bits.
 *
 * @param[in] io_req The I/O request holding the details of the IO access.
 * @param[in] handler_private_data Unused parameter.
 *
 * @return Always return 0 to indicate a successful emulation of MMIO read/write.
 *
 * @pre (io_req->reqs.mmio.size == 1U) || (io_req->reqs.mmio.size == 2U) ||
 *      (io_req->reqs.mmio.size == 4U) || (io_req->reqs.mmio.size == 8U)
 *
 * @post N/A
 */
static int32_t mmio_dummy_access_handler(struct io_request *io_req,
	__unused void *handler_private_data)
{
	struct acrn_mmio_request *mmio = &io_req->reqs.mmio_request;

	if (mmio->direction == ACRN_IOREQ_DIR_READ) {
		switch (mmio->size) {
		case 1U:
			mmio->value = MMIO_DEFAULT_VALUE_SIZE_1;
			break;
		case 2U:
			mmio->value = MMIO_DEFAULT_VALUE_SIZE_2;
			break;
		case 4U:
			mmio->value = MMIO_DEFAULT_VALUE_SIZE_4;
			break;
		case 8U:
			mmio->value = MMIO_DEFAULT_VALUE_SIZE_8;
			break;
		default:
			/* This case is unreachable, this is guaranteed by the design. */
			break;
		}
	}

	return 0;
}

/**
 * @brief Default MMIO access request handler.
 *
 * The default MMIO read/write handler does the following work:
 * - Print error message as the access is unexpected.
 * - Call mmio_dummy_access_handler to treat the MMIO read/write with dummy value.
 *
 * @param[in] io_req The I/O request holding the details of the IO access.
 * @param[in] handler_private_data Unused parameter.
 *
 * @return Always return 0 to indicate a successful emulation of MMIO read/write.
 *
 * @pre (io_req->reqs.mmio.size == 1U) || (io_req->reqs.mmio.size == 2U) ||
 *      (io_req->reqs.mmio.size == 4U) || (io_req->reqs.mmio.size == 8U)
 *
 * @post N/A
 */
static int32_t mmio_default_access_handler(struct io_request *io_req,
	void *handler_private_data)
{
	struct acrn_mmio_request *mmio = &io_req->reqs.mmio_request;

	pr_err("unexpected MMIO %s on addr %016llx, size %u",
		(mmio->direction == ACRN_IOREQ_DIR_READ) ? "read" : "write",
		mmio->address, mmio->size);
	return mmio_dummy_access_handler(io_req, handler_private_data);
}

/**
 * @brief Try emulating port I/O in the hypervisor.
 *
 * Search the port I/O nodes by address. If there is a node whose address range contains the
 * accessing address, call the response read/write handler of the node to emulate the PIO.
 * If there is no suitable node, return \p -ENODEV. Then the \p io_req will be delivered
 * to the HSM, and the Service OS will emulate the I/O behavior.
 *
 * @param[in] vcpu Pointer to vCPU that initiates the hypercall.
 * @param[in] io_req The I/O request holding the details of the IO access.
 *
 * @return An int32_t value indicating whether port I/O emulated by hypervisor successfully.
 *
 * @retval 0 Successfully emulated by registered handlers.
 * @retval -ENODEV No proper handler found.
 *
 * @pre io_req->io_type == ACRN_IOREQ_TYPE_PORTIO
 *
 * @post N/A
 */
static int32_t
hv_emulate_pio(struct acrn_vcpu *vcpu, struct io_request *io_req)
{
	int32_t status = -ENODEV;
	uint16_t port, size;
	uint32_t idx;
	struct acrn_vm *vm = vcpu->vm;
	struct acrn_pio_request *pio_req = &io_req->reqs.pio_request;
	struct vm_io_handler_desc *handler;
	io_read_fn_t io_read = NULL;
	io_write_fn_t io_write = NULL;

	if (is_service_vm(vcpu->vm) || is_prelaunched_vm(vcpu->vm)) {
		io_read = pio_default_read;
		io_write = pio_default_write;
	}

	port = (uint16_t)pio_req->address;
	size = (uint16_t)pio_req->size;

	for (idx = 0U; idx < EMUL_PIO_IDX_MAX; idx++) {
		handler = &(vm->emul_pio[idx]);

		if ((port < handler->port_start) || (port >= handler->port_end)) {
			continue;
		}

		if (handler->io_read != NULL) {
			io_read = handler->io_read;
		}
		if (handler->io_write != NULL) {
			io_write = handler->io_write;
		}
		break;
	}

	if ((pio_req->direction == ACRN_IOREQ_DIR_WRITE) && (io_write != NULL)) {
		if (io_write(vcpu, port, size, pio_req->value)) {
			status = 0;
		}
	} else if ((pio_req->direction == ACRN_IOREQ_DIR_READ) && (io_read != NULL)) {
		if (io_read(vcpu, port, size)) {
			status = 0;
		}
	} else {
		/* do nothing */
	}

	pr_dbg("IO %s on port %04x, data %08x",
		(pio_req->direction == ACRN_IOREQ_DIR_READ) ? "read" : "write", port, pio_req->value);

	return status;
}

/**
 * @brief Set up dummy MMIO access handlers for a specified address range in a VM.
 *
 * This function unregisters any existing MMIO emulation handlers for the given address range
 * and registers a dummy MMIO access handler. This is useful for handling accesses to MMIO
 * regions that have been deassigned, preventing unexpected exceptions.
 *
 * @param[inout] vm Pointer to the VM for which to set the dummy MMIO access.
 * @param[in] start The start address of the MMIO range.
 * @param[in] end The end address of the MMIO range.
 *
 * @return None.
 */
void set_dummy_mmio_access(struct acrn_vm *vm, uint64_t start, uint64_t end)
{
	unregister_mmio_emulation_handler(vm, start, end);
	register_mmio_emulation_handler(vm, mmio_dummy_access_handler, 
		start, end, NULL, false);
}

/**
 * @brief Remove dummy MMIO access handlers for a specified address range in a VM.
 *
 * This function unregisters the dummy MMIO access handler for the given address range.
 *
 * @param[inout] vm Pointer to the VM for which to remove the dummy MMIO access.
 * @param[in] start The start address of the MMIO range.
 * @param[in] end The end address of the MMIO range.
 *
 * @return None.
 */
void remove_dummy_mmio_access(struct acrn_vm *vm, uint64_t start, uint64_t end)
{
	unregister_mmio_emulation_handler(vm, start, end);
}

/**
 * @brief Try emulating MMIO in the hypervisor.
 *
 * Search the MMIO nodes by address. If there is a node whose address range contains the
 * accessing address, call the response read/write handler of the node to emulate the PIO.
 * If there is no suitable node, return \p -ENODEV. If the request address spans multiple
 * devices, return \p -EIO. Then the \p io_req will be delivered to the HSM, and the
 * Service OS will emulate the I/O behavior.
 *
 * @param[in] vcpu Pointer to vCPU that initiates the hypercall.
 * @param[in] io_req The I/O request holding the details of the IO access.
 *
 * @return An int32_t value indicating whether MMIO emulated by hypervisor successfully.
 *
 * @retval 0 Successfully emulated by registered handlers.
 * @retval -ENODEV No proper handler found.
 * @retval -EIO The request spans multiple devices and cannot be emulated.
 *
 * @pre io_req->io_type == ACRN_IOREQ_TYPE_MMIO || io_req->io_type == ACRN_IOREQ_TYPE_WP
 *
 * @post N/A
 */
static int32_t
hv_emulate_mmio(struct acrn_vcpu *vcpu, struct io_request *io_req)
{
	int32_t status = -ENODEV;
	bool hold_lock = true;
	uint16_t idx;
	uint64_t address, size, base, end;
	struct acrn_mmio_request *mmio_req = &io_req->reqs.mmio_request;
	struct mem_io_node *mmio_handler = NULL;
	hv_mem_io_handler_t read_write = NULL;
	void *handler_private_data = NULL;

	if (is_service_vm(vcpu->vm) || is_prelaunched_vm(vcpu->vm)) {
		read_write = mmio_default_access_handler;
	}

	address = mmio_req->address;
	size = mmio_req->size;

	spinlock_obtain(&vcpu->vm->emul_mmio_lock);
	for (idx = 0U; idx <= vcpu->vm->nr_emul_mmio_regions; idx++) {
		mmio_handler = &(vcpu->vm->emul_mmio[idx]);
		if (mmio_handler->read_write != NULL) {
			base = mmio_handler->range_start;
			end = mmio_handler->range_end;

			if (((address + size) <= base) || (address >= end)) {
				continue;
			} else {
				 if ((address >= base) && ((address + size) <= end)) {
					hold_lock = mmio_handler->hold_lock;
					read_write = mmio_handler->read_write;
					handler_private_data = mmio_handler->handler_private_data;
				} else {
					pr_fatal("Err MMIO, address:0x%lx, size:%lx", address, size);
					status = -EIO;
				}
				break;
			}
		}
	}

	if ((status == -ENODEV) && (read_write != NULL)) {
		/* This mmio_handler will never modify once register, so we don't
		 * need to hold the lock when handling the MMIO access.
		 */
		if (!hold_lock) {
			spinlock_release(&vcpu->vm->emul_mmio_lock);
		}
		status = read_write(io_req, handler_private_data);
		if (!hold_lock) {
			spinlock_obtain(&vcpu->vm->emul_mmio_lock);
		}
	}
	spinlock_release(&vcpu->vm->emul_mmio_lock);

	return status;
}

/**
 * @brief Emulation PIO/MMIO device behavior.
 *
 * Handle an I/O request by either invoking a hypervisor-internal handler or
 * deliver to HSM.
 * Try emulating I/O in hypervisor first. If no proper handler found, send the request to
 * HSM and devicemodle implements the emulation.
 * Besides asyncio, the emulation finishes in this function.
 *
 * @param[in] vcpu The pointer to vCPU that process the instruction.
 * @param[in] io_req The pointer I/O request holding the details of the IO access.
 *
 * @return An int32_t value indicating whether I/O request handle successfully.
 *
 * @retval 0 The I/O request is delivered to HSM.
 * @retval -EIO The request spans multiple devices and cannot be emulated.
 * @retval -EINVAL \p io_req has an invalid io_type or fail to deliver io_req to Seveice VM.
 * @retval -ENODEV A pre-launched VM accessed an address that does not match any I/O address range.
 *
 * @pre N/A
 *
 * @post N/A
 */
int32_t
emulate_io(struct acrn_vcpu *vcpu, struct io_request *io_req)
{
	int32_t status;
	struct acrn_vm_config *vm_config;
	struct asyncio_desc *aio_desc;

	vm_config = get_vm_config(vcpu->vm->vm_id);

	switch (io_req->io_type) {
	case ACRN_IOREQ_TYPE_PORTIO:
		status = hv_emulate_pio(vcpu, io_req);
		if (status == 0) {
			emulate_pio_complete(vcpu, io_req);
		}
		break;
	case ACRN_IOREQ_TYPE_MMIO:
	case ACRN_IOREQ_TYPE_WP:
		status = hv_emulate_mmio(vcpu, io_req);
		if (status == 0) {
			emulate_mmio_complete(vcpu, io_req);
		}
		break;
	default:
		/* Unknown I/O request io_type */
		status = -EINVAL;
		break;
	}

	if ((status == -ENODEV) && (vm_config->load_order == POST_LAUNCHED_VM)) {
		/*
		 * No handler from HV side, search from HSM in Service VM
		 *
		 * ACRN insert request to HSM and inject upcall.
		 */
		aio_desc = get_asyncio_desc(vcpu, io_req);
		if (aio_desc != NULL) {
			status = acrn_insert_asyncio(vcpu, aio_desc->asyncio_info.fd);
		} else {
			status = acrn_insert_request(vcpu, io_req);
			if (status == 0) {
				dm_emulate_io_complete(vcpu);
			}
		}
		if (status != 0) {
			/* here for both IO & MMIO, the direction, address,
			 * size definition is same
			 */
			struct acrn_pio_request *pio_req = &io_req->reqs.pio_request;

			pr_fatal("%s Err: access dir %d, io_type %d, addr = 0x%lx, size=%lu", __func__,
				pio_req->direction, io_req->io_type,
				pio_req->address, pio_req->size);
		}
	}

	return status;
}


/**
 * @brief Register a port I/O handler
 *
 * This API registers a pio handler to vm.
 * This function creates a pio emulate descriptor with an address range and read/write handlers.
 * When Guest OS accesses PIO addresses, hypervisor searches in pio emulation array.
 * If the address range matches, hypervisor calls the handler in the pio emulation descriptor and
 * return emulation result.
 * For Service OS has direct access to whole PIO by default, disable the direct access to the
 * address range to trap this PIO access for Service OS.
 *
 * @param[in] vm Pointer that points to VM data structure.
 * @param[in] pio_idx The emulated port io index
 * @param[in] range The emulated port io range
 * @param[in] io_read_fn_ptr The handler for emulating reads from the given range
 * @param[in] io_write_fn_ptr The handler for emulating writes to the given range
 *
 * @return None.
 *
 * @pre pio_idx < EMUL_PIO_IDX_MAX
 *
 * @post N/A
 */
void register_pio_emulation_handler(struct acrn_vm *vm, uint32_t pio_idx,
		const struct vm_io_range *range, io_read_fn_t io_read_fn_ptr, io_write_fn_t io_write_fn_ptr)
{
	if (is_service_vm(vm)) {
		deny_guest_pio_access(vm, range->base, range->len);
	}
	vm->emul_pio[pio_idx].port_start = range->base;
	vm->emul_pio[pio_idx].port_end = range->base + range->len;
	vm->emul_pio[pio_idx].io_read = io_read_fn_ptr;
	vm->emul_pio[pio_idx].io_write = io_write_fn_ptr;
}

/**
 * @brief Find MMIO node
 *
 * This API find match MMIO node which address range is completely consistent in the VM.
 *
 * @param[in] vm Pointer that points to VM data structure.
 * @param[in] start The base address of the range can emulate read/write.
 * @param[in] end The end of the range (exclusive) can emulate read/write.
 *
 * @return If there's a match mmio_node return it, otherwise return NULL;
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline struct mem_io_node *find_match_mmio_node(struct acrn_vm *vm,
				uint64_t start, uint64_t end)
{
	bool found = false;
	uint16_t idx;
	struct mem_io_node *mmio_node;

	for (idx = 0U; idx < CONFIG_MAX_EMULATED_MMIO_REGIONS; idx++) {
		mmio_node = &(vm->emul_mmio[idx]);
		if ((mmio_node->range_start == start) && (mmio_node->range_end == end)) {
			found = true;
			break;
		}
	}

	if (!found) {
		pr_info("%s, vm[%d] no match mmio region [0x%lx, 0x%lx] is found",
				__func__, vm->vm_id, start, end);
		mmio_node = NULL;
	}

	return mmio_node;
}

/**
 * @brief Find a free MMIO node
 *
 * This API find a free MMIO node from \p vm.
 *
 * @param[in] vm Pointer that points to VM data structure.
 *
 * @return If there's a free mmio_node return it, otherwise return NULL.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline struct mem_io_node *find_free_mmio_node(struct acrn_vm *vm)
{
	uint16_t idx;
	struct mem_io_node *mmio_node = find_match_mmio_node(vm, 0UL, 0UL);

	if (mmio_node != NULL) {
		idx = (uint16_t)(uint64_t)(mmio_node - &(vm->emul_mmio[0U]));
		if (vm->nr_emul_mmio_regions < idx) {
			vm->nr_emul_mmio_regions = idx;
		}
	}

	return mmio_node;
}

/**
 * @brief Register a MMIO handler
 *
 * This API registers a MMIO handler to vm.
 * This function creates a mmio node with an address range and a callback handler.
 * When Guest OS accesses MMIO addresses, hypervisor searches in mmio nodes. If the address range matches,
 * hypervisor calls the handler in the mmio node and return emulation result.
 *
 * @param[in] vm Pointer that points to VM data structure.
 * @param[in] read_write The handler for emulating accesses to the given range.
 * @param[in] start The base address of the range \p read_write can emulate.
 * @param[in] end The end of the range (exclusive) \p read_write can emulate.
 * @param[in] handler_private_data Handler-specific data which will be passed to \p read_write when called.
 * @param[in] hold_lock Indicate whether hold the spin lock while hypervisor emulate MMIO read/write.
 *
 * @return None.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark A free mmio_node is required.
 */
void register_mmio_emulation_handler(struct acrn_vm *vm,
	hv_mem_io_handler_t read_write, uint64_t start,
	uint64_t end, void *handler_private_data, bool hold_lock)
{
	struct mem_io_node *mmio_node;

	/* Ensure both a read/write handler and range check function exist */
	if ((read_write != NULL) && (end > start)) {
		spinlock_obtain(&vm->emul_mmio_lock);
		mmio_node = find_free_mmio_node(vm);
		if (mmio_node != NULL) {
			/* Fill in information for this node */
			mmio_node->hold_lock = hold_lock;
			mmio_node->read_write = read_write;
			mmio_node->handler_private_data = handler_private_data;
			mmio_node->range_start = start;
			mmio_node->range_end = end;
		}
		spinlock_release(&vm->emul_mmio_lock);
	}

}

/**
 * @brief Unregister a MMIO handler
 *
 * This API unregisters an MMIO handler whose address range matches by resetting the mmio_node to 0.
 *
 * @param[in] vm Pointer that points to VM data structure.
 * @param[in] start The base address of the range which wants to unregister
 * @param[in] end The end of the range (exclusive) which wants to unregister
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
void unregister_mmio_emulation_handler(struct acrn_vm *vm,
					uint64_t start, uint64_t end)
{
	struct mem_io_node *mmio_node;

	spinlock_obtain(&vm->emul_mmio_lock);
	mmio_node = find_match_mmio_node(vm, start, end);
	if (mmio_node != NULL) {
		(void)memset(mmio_node, 0U, sizeof(struct mem_io_node));
	}
	spinlock_release(&vm->emul_mmio_lock);
}
/**
 * @brief Deinit IO emulation
 *
 * Deinit hypervisor IO emulation module. Reset PIO & MMIO emulate nodes to zero.
 *
 * @param[inout] vm The VM to which the IO nodes are belong to.
 *
 * @return None.
 *
 * @pre N/A
 *
 * @post N/A
 */
void deinit_emul_io(struct acrn_vm *vm)
{
	(void)memset(vm->emul_mmio, 0U, sizeof(vm->emul_mmio));
	(void)memset(vm->emul_pio, 0U, sizeof(vm->emul_pio));
}

/**
 * @}
 */