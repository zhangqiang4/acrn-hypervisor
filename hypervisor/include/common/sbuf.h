/*
 * Copyright (C) 2017-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef SHARED_BUFFER_H
#define SHARED_BUFFER_H
#include <acrn_common.h>
#include <asm/guest/vm.h>

 /**
 * @addtogroup lib_sbuf
 *
 * @{
 */

/**
 * @file
 * @brief This file declares the external APIs of the lib.sbuf module.
 *
 * This file defines external interface of sbuf module. Other modules can
 * call these functions to operate sbuf.
 */

uint32_t sbuf_put(struct shared_buf *sbuf, uint8_t *data, uint32_t max_len);
uint32_t sbuf_put_many(struct shared_buf *sbuf, uint32_t elem_size, uint8_t *data, uint32_t data_size);
int32_t sbuf_share_setup(uint16_t pcpu_id, uint32_t sbuf_id, struct shared_buf *sbuf);
void sbuf_reset(void);
uint32_t sbuf_next_ptr(uint32_t pos, uint32_t span, uint32_t scope);
int32_t sbuf_setup_common(struct acrn_vm *vm, uint16_t cpu_id, uint32_t sbuf_id, uint64_t gpa, struct shared_buf *sbuf);
/**
 * @}
 */
#endif /* SHARED_BUFFER_H */
