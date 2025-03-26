/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef DUMP_H
#define DUMP_H

/**
 * @addtogroup debug
 *
 * @{
 */

/**
 * @file
 * @brief Interfaces for Information Dumping
 *
 * This file declares functions for handling and dumping information about
 * interrupts and exceptions, as well as triggering a core dump. These functions
 * are used for debugging and diagnostic purposes, providing insights into the
 * system's state during exceptional events.
 */

struct intr_excp_ctx;

void dump_intr_excp_frame(const struct intr_excp_ctx *ctx);
void dump_exception(struct intr_excp_ctx *ctx, uint16_t pcpu_id);
void trigger_coredump(bool dump);

/**
 * @}
 */
#endif /* DUMP_H */
