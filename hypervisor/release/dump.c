/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <asm/irq.h>
#include <dump.h>

/**
 * @addtogroup debug
 *
 * @{
 */

/**
 * @file
 * @brief Interfaces for Information Dumping in Release Mode
 *
 * This file provides stub implementations for functions related to handling
 * and dumping information about interrupts and exceptions. These functions
 * are placeholders and do not perform any actual operations. They are used
 * to maintain a consistent interface and can be replaced with actual
 * implementations as needed.
 */

/**
 * @brief Dump the interrupt/exception context frame.
 *
 * This stub function simulates dumping the context frame of an interrupt
 * or exception. It is intended to be used for debugging purposes.
 * In release mode, it performs no operation, serving as a placeholder.
 *
 * @param[in] ctx The context of the interrupt or exception.
 *
 * @return None.
 */
void dump_intr_excp_frame(__unused const struct intr_excp_ctx *ctx) {}

/**
 * @brief Dump exception information for a specific CPU.
 *
 * This stub function simulates dumping exception information for a given
 * physical CPU. It is intended to be used for debugging purposes.
 * In release mode, it performs no operation, serving as a placeholder.
 *
 * @param[in] ctx The context of the exception.
 * @param[in] pcpu_id The physical CPU ID where the exception occurred.
 *
 * @return None.
 */
void dump_exception(__unused struct intr_excp_ctx *ctx, __unused uint16_t pcpu_id) {}

/**
 * @brief Trigger a core dump.
 *
 * This stub function simulates triggering a core dump, which is typically
 * used to capture the state of a program for debugging purposes.
 * In release mode, it performs no operation, serving as a placeholder.
 *
 * @param[in] dump A boolean indicating whether to perform the core dump.
 *
 * @return None.
 */
void trigger_coredump(__unused bool dump) {}

/**
 * @}
 */
