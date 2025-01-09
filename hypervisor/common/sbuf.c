/*
 * SHARED BUFFER
 *
 * Copyright (C) 2017-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Li Fei <fei1.li@intel.com>
 *
 */

#include <types.h>
#include <rtl.h>
#include <errno.h>
#include <asm/cpu.h>
#include <asm/per_cpu.h>
#include <vm_event.h>
#include <asm/mmu.h>

uint32_t sbuf_next_ptr(uint32_t pos_arg,
		uint32_t span, uint32_t scope)
{
	uint32_t pos = pos_arg;
	pos += span;
	pos = (pos >= scope) ? (pos - scope) : pos;
	return pos;
}

/**
 * The high caller should guarantee each time there must have
 * sbuf->ele_size data can be write form data.
 *
 * As sbuf->ele_size is possibly setup by some sources outside of the
 * HV (e.g. the service VM), it is not meant to be trusted. So caller
 * should provide the max length of the data for safety reason.
 *
 * And this function should guarantee execution atomically.
 *
 * flag:
 * If OVERWRITE_EN set, buf can store (ele_num - 1) elements at most.
 * Should use lock to guarantee that only one read or write at
 * the same time.
 * if OVERWRITE_EN not set, buf can store (ele_num - 1) elements
 * at most. Shouldn't modify the sbuf->head.
 *
 * return:
 * ele_size:	write succeeded.
 * 0:		no write, buf is full
 * UINT32_MAX:	failed, sbuf corrupted.
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

int32_t sbuf_share_setup(uint16_t pcpu_id, uint32_t sbuf_id, struct shared_buf *sbuf)
{
	int ret = -EINVAL;

	if ((pcpu_id < get_pcpu_nums()) && (sbuf_id < ACRN_SBUF_PER_PCPU_ID_MAX)) {
		per_cpu(sbuf, pcpu_id)[sbuf_id] = sbuf;
		pr_info("%s share sbuf for pCPU[%u] with sbuf_id[%u] setup successfully",
				__func__, pcpu_id, sbuf_id);
		ret = 0;
	}

	return ret;
}

void sbuf_reset(void)
{
	uint16_t pcpu_id, sbuf_id;

	for (pcpu_id = 0U; pcpu_id < get_pcpu_nums(); pcpu_id++) {
		for (sbuf_id = 0U; sbuf_id < ACRN_SBUF_PER_PCPU_ID_MAX; sbuf_id++) {
			per_cpu(sbuf, pcpu_id)[sbuf_id] = 0U;
		}
	}
}

int32_t sbuf_setup_common(struct acrn_vm *vm, uint16_t cpu_id, uint32_t sbuf_id, uint64_t gpa, struct shared_buf *sbuf)
{
	int32_t ret = 0;
	uint64_t size;
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
			ret = -1;
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
			case ACRN_VM_EVENT:
				ret = init_vm_event(vm, sbuf);
				break;
			default:
				pr_err("sbuf: unsupported sbuf_id %u", sbuf_id);
		}
	}

	return ret;
}

/* try put a batch of elememts from data to sbuf
 * data_size should be equel to n*elem_size, data not enough to fill the elem_size will be ignored.
 *
 * return:
 * elem_size * n:   bytes put in sbuf
 * UINT32_MAX:	failed, sbuf corrupted.
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
