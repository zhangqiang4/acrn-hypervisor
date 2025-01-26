/*
 * Copyright (C) 2017-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <rtl.h>
#include <errno.h>
#include <asm/cpu.h>
#include <asm/per_cpu.h>
#include <vm_event.h>
#include <asm/mmu.h>

/**
 * @defgroup lib_sbuf lib.sbuf
 * @ingroup lib
 * @brief Manipulates shared ring buffers between Service VM and hypervisor.
 *
 * The sbuf refers shared ring buffers between hypervisor and Service VM.
 * It is created by Service VM through hypercall and used to obtain information filled by the hypervisor.
 * From the information flow perspective, the hypervisor is producer and the Service VM is consumer.
 *
 * @{
 */

/**
 * @file
 * @brief Implementation of sbuf operation functions.
 *
 * This file provides some common functions needed to create and use sbuf.
 * These functions manage sbuf memory resources and copy data to the valid sbuf memory areas.
 */

/**
 * @brief Calculate the next position in a circular buffer of sbuf.
 *
 * This function is used to get the next position in a circular buffer when writting span byes into sbuf memory.
 * It ensures that the position wraps around correctly when it reaches the end of the buffer.
 * When writing data to sbuf, should firstly use this function to get the next position.
 * This position is useful for checking whether the ring buffer is overrun.
 *
 * It calculates position value based on the given parameters and always return the result.
 *
 * @param[in] pos_arg The current position in sbuf ring buffer.
 * @param[in] span The length of the data to be written.
 * @param[in] scope The upper bound of the ring buffer.
 *
 * @return The next buffer position that can be written.
 *
 * @pre pos_arg < scope
 * @pre span < scope
 *
 * @post retval < scope
 */
uint32_t sbuf_next_ptr(uint32_t pos_arg,
		uint32_t span, uint32_t scope)
{
	uint32_t pos = pos_arg;
	pos += span;
	pos = (pos >= scope) ? (pos - scope) : pos;
	return pos;
}

/**
 * @brief Put data into a shared buffer.
 *
 * This function puts data into a shared buffer (sbuf). It handles buffer
 * overrun scenarios based on the buffer's configuration flags. If the buffer
 * is full and overwriting is not enabled, it returns 0. If overwriting is
 * enabled, it overwrites the oldest data and updates the buffer's head and
 * tail pointers accordingly.
 *
 * - When the following conditions are met, data fragment is put into the sbuf mem:
 *   1. sbuf->ele_size is no more than max_len.
 *   2. the sbuf ring buffer will not overrun after writing ele_size bytes,
 *      or the sbuf ring buffer will overrun but the OVERWRITE_EN is set in sbuf flags.
 *
 * - If above conditions are not met, the data fragment will be discarded.
 *
 * As sbuf->ele_size is possibly setup by some sources outside of the
 * HV (e.g. the service VM), it is not meant to be trusted. So caller
 * should provide the max_len bytes of data for safety reason.
 *
 * This function should guarantee execution atomically.
 *
 * @param[inout] sbuf Pointer to the shared buffer structure where the ring buffer resides.
 * @param[in] data Pointer to the data to be put into the buffer.
 * @param[in] max_len Maximum length of the data to be put into the buffer..
 *
 * @return Length of data put into to sbuf.
 *
 * @retval sbuf->ele_size Write succeeded.
 * @retval 0 The sbuf is full and overwriting is disabled.
 * @retval UINT32_MAX An error occurs due to the element size being larger than the maximum allowed length.
 *
 * @pre sbuf != NULL
 * @pre data != NULL
 */

uint32_t sbuf_put(struct shared_buf *sbuf, uint8_t *data, uint32_t max_len)
{
	void *to;
	uint32_t next_tail;
	uint32_t ele_size, ret;
	bool trigger_overwrite = false;

	stac();
	ele_size = sbuf->ele_size;
	next_tail = sbuf_next_ptr(sbuf->tail, ele_size, sbuf->size);

	if ((next_tail == sbuf->head) && ((sbuf->flags & OVERWRITE_EN) == 0U)) {
		/* if overrun is not enabled, return 0 directly */
		ret = 0U;
	} else if (ele_size <= max_len) {
		if (next_tail == sbuf->head) {
			/* accumulate overrun count if necessary */
			sbuf->overrun_cnt += sbuf->flags & OVERRUN_CNT_EN;
			trigger_overwrite = true;
		}
		to = (void *)sbuf + SBUF_HEAD_SIZE + sbuf->tail;

		(void)memcpy_s(to, ele_size, data, max_len);
		/* make sure write data before update head */
		cpu_write_memory_barrier();

		if (trigger_overwrite) {
			sbuf->head = sbuf_next_ptr(sbuf->head,
					ele_size, sbuf->size);
		}
		sbuf->tail = next_tail;
		ret = ele_size;
	} else {
		/* there must be something wrong */
		ret = UINT32_MAX;
	}
	clac();

	return ret;
}

/**
 * @brief Flush temporary log buffer to a shared buffer.
 *
 * This function flushes the temporary log buffer for a specific physical CPU (pcpu)
 * to a shared buffer (sbuf). It handles the log entries in the temporary buffer
 * and writes them to the shared buffer. Note that some logs may be lost during
 * the flush process.
 *
 * @param[in] pcpu_id The ID of the physical CPU whose log buffer is to be flushed.
 * @param[inout] sbuf Pointer to the shared buffer where the logs will be written.
 *
 * @return None
 *
 * @pre sbuf != NULL
 */
static void flush_temp_log(uint16_t pcpu_id, struct shared_buf *sbuf) {
	uint32_t *log_count = &per_cpu(temp_logbuf_count, pcpu_id);
	char (*temp_logbuf)[LOG_MESSAGE_MAX_SIZE] = per_cpu(temp_logbuf, pcpu_id);
	uint32_t index = 0;

	/* We may lose some log at the time of flush*/
	while (index < (*log_count)) {
		uint32_t msg_len = strnlen_s(temp_logbuf[index], LOG_MESSAGE_MAX_SIZE);
		(void)sbuf_put_many(sbuf, LOG_ENTRY_SIZE, (uint8_t *)temp_logbuf[index],
			LOG_ENTRY_SIZE * (((msg_len - 1U) / LOG_ENTRY_SIZE) + 1));
		index++;
	}

	*log_count = 0;
}

/**
 * @brief Set up a shared buffer for a specific physical CPU.
 *
 * This function sets up a shared buffer (sbuf) for a specific physical CPU (pcpu) and a given sbuf ID, if the
 * provided pcpu ID and sbuf ID are within valid ranges. Otherwise, it returns an error code to indicate the failure.
 *
 * @param[in] pcpu_id The ID of the physical CPU for which the shared buffer is to be set up.
 * @param[in] sbuf_id The ID of the shared buffer to be set up.
 * @param[inout] sbuf Pointer to the shared buffer structure.
 *
 * @return An int32_t value indicating whether the sbuf was successfully set up.
 *
 * @retval 0 On success.
 * @retval -EINVAL On failure.
 *
 * @pre sbuf != NULL
 */
static int32_t sbuf_share_setup(uint16_t pcpu_id, uint32_t sbuf_id, struct shared_buf *sbuf)
{
	int ret = -EINVAL;

	if ((pcpu_id < get_pcpu_nums()) && (sbuf_id < ACRN_SBUF_PER_PCPU_ID_MAX)) {
		if (sbuf_id == ACRN_HVLOG) {
			flush_temp_log(pcpu_id, sbuf);
		}

		per_cpu(sbuf, pcpu_id)[sbuf_id] = sbuf;

		pr_info("%s share sbuf for pCPU[%u] with sbuf_id[%u] setup successfully",
				__func__, pcpu_id, sbuf_id);
		ret = 0;
	}

	return ret;
}

/**
 * @brief Reset all shared buffers for all physical CPUs.
 *
 * This function resets all shared buffers for all physical CPUs (pcpus) by
 * setting each shared buffer pointer to 0 (NULL). It iterates through all
 * possible pcpu IDs and shared buffer IDs, ensuring that all shared buffers
 * are reset.
 *
 * @return None
 */
void sbuf_reset(void)
{
	uint16_t pcpu_id, sbuf_id;

	for (pcpu_id = 0U; pcpu_id < get_pcpu_nums(); pcpu_id++) {
		for (sbuf_id = 0U; sbuf_id < ACRN_SBUF_PER_PCPU_ID_MAX; sbuf_id++) {
			per_cpu(sbuf, pcpu_id)[sbuf_id] = 0U;
		}
	}
}

/**
 * @brief Set up a shared buffer for a specific VM and CPU.
 *
 * This function sets up a shared buffer (sbuf) for a specific virtual machine (VM)
 * and physical CPU (pcpu). It ensures that the guest physical address (GPA) is
 * mapped to a continuous host virtual address (HVA) range. Otherwise, it will
 * return -1 to indicate the failure. Depending on the sbuf ID,
 * it performs different setup operations.
 *
 * - For the valid sbuf_id, this function will initialize sbuf area by calling corresponding functions.
 *   1. For the sbuf_id of ACRN_TRACE, ACRN_HVLOG, ACRN_SEP and ACRN_SOCWATCH: call sbuf_share_setup()
 *	to initialize sbuf for storing log and profiling information.
 *   2. For the sbuf_id of ACRN_ASYNCIO: call init_asyncio() to initialize sbuf for accepting
 *	asynchronous io request completion notification.
 *   3. For the sbuf_id of ACRN_VM_EVENT: call init_vm_event() to initialize sbuf for accepting
 * 	vm state change notification.
 *
 * - Otherwise, it will stop initializing sbuf and return 0.
 *
 * @param[inout] vm Pointer to the acrn_vm structure representing the VM whose shared buffer is to be set up.
 * @param[in] cpu_id The ID of the physical CPU for which the shared buffer is to be set up.
 * @param[in] sbuf_id The ID of the shared buffer to be set up.
 * @param[in] gpa The guest physical address of the shared buffer.
 * @param[in] sbuf Pointer to the shared buffer structure.
 *
 * @return A int32_t value to indicate the status of the sbuf setup.
 *
 * @retval 0 On success.
 * @retval <0 On sbuf setup failure.
 *
 * @pre vm != NULL
 * @pre pcpu_id < MAX_PCPU_NUM
 */
int32_t sbuf_setup_common(struct acrn_vm *vm, uint16_t cpu_id, uint32_t sbuf_id, uint64_t gpa, struct shared_buf *sbuf)
{
	int32_t ret = 0;
	uint32_t size;
	uint64_t hva;
	uint64_t temp_gpa;

	/* pr_* breaks stac/clac */
	stac();
	size = sbuf->size + SBUF_HEAD_SIZE;
	clac();
	
	/* sbuf requires hva to be continuous */
	hva = (uint64_t)sbuf + PAGE_SIZE;
	for (temp_gpa = (gpa + PAGE_SIZE); temp_gpa < (gpa + size); temp_gpa += PAGE_SIZE) {
		if ((uint64_t)gpa2hva(vm, temp_gpa) != hva) {
			pr_err("sbuf: gpa 0x%016lx is not mapped to continous hva", temp_gpa);
			ret = -EINVAL;
			break;
		}
		hva += PAGE_SIZE;
	}

	if (ret == 0) {
		switch (sbuf_id) {
			case ACRN_TRACE:
			case ACRN_HVLOG:
			case ACRN_SEP:
			case ACRN_SOCWATCH:
				ret = sbuf_share_setup(cpu_id, sbuf_id, sbuf);
				break;
			case ACRN_ASYNCIO:
				ret = init_asyncio(vm, sbuf);
				break;
#ifdef CONFIG_VMEVENT_ENABLED
			case ACRN_VM_EVENT:
				ret = init_vm_event(vm, sbuf);
				break;
#endif
			default:
				ret = -EINVAL;
				pr_err("sbuf: unsupported sbuf_id %u", sbuf_id);
				break;
		}
	}

	return ret;
}

/**
 * @brief Put multiple elements into a shared buffer.
 *
 * This function puts multiple elements into a shared buffer (sbuf). It iterates
 * through the data and calls `sbuf_put` for each element. If an error occurs
 * during the process, it stops and returns the number of bytes successfully sent
 * or `UINT32_MAX` if a critical error occurs.
 *
 * @param[inout] sbuf Pointer to the shared buffer structure.
 * @param[in] elem_size The size of each element to be put into the buffer.
 * @param[in] data Pointer to the data to be put into the buffer.
 * @param[in] data_size The total size of the data to be put into the buffer.
 *
 * @return A int32_t value to indicate the bytes put into sbuf.
 *
 * @retval <UINT32_MAX The number of bytes successfully put in sbuf.
 * @retval UINT32_MAX Failed, sbuf corrupted.
 *
 * @pre sbuf != NULL
 * @pre data_size % elem_size == 0
 */
uint32_t sbuf_put_many(struct shared_buf *sbuf, uint32_t elem_size, uint8_t *data, uint32_t data_size)
{
	uint32_t ret, sent = 0U;
	uint32_t i;

	for (i = 0U; i < (data_size / elem_size); i++) {
		ret = sbuf_put(sbuf, data + i * elem_size, elem_size);
		if (ret == elem_size) {
			sent += ret;
		} else {
			if (ret == UINT32_MAX) {
				sent = UINT32_MAX;
			}
			break;
		}
	}
	return sent;
}

/**
 * @}
 */
