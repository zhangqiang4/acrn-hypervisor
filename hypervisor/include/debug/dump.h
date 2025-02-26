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

/**
 * @brief Retrieve the current value of the RBP register.
 *
 * This function uses inline assembly to move the value of the RBP register into a uint64_t variable and returns it.
 *
 * @return The current value of the RBP register.
 *
 * @pre N/A
 * @post N/A
 */
 static inline uint64_t cpu_rbp_get(void)
 {
     uint64_t ret;

     asm volatile("movq %%rbp, %0" :  "=r"(ret));
     return ret;
 }

/**
 * @brief Retrieve the current value of the RSP register.
 *
 * This function uses inline assembly to move the value of the RSP register into a uint64_t variable and returns it.
 *
 * @return The current value of the RSP register.
 *
 * @pre N/A
 * @post N/A
 */
 static inline uint64_t cpu_rsp_get(void)
 {
     uint64_t ret;

     asm volatile("movq %%rsp, %0" :  "=r"(ret));
     return ret;
 }

struct intr_excp_ctx;

void dump_intr_excp_frame(const struct intr_excp_ctx *ctx);
void dump_exception(struct intr_excp_ctx *ctx, uint16_t pcpu_id);
void trigger_coredump(bool dump);

/**
 * @}
 */
#endif /* DUMP_H */
