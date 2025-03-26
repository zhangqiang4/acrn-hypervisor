/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <acrn_hv_defs.h>

/**
 * @addtogroup debug
 *
 * @{
 */

/**
 * @file
 * @brief NPK Logging Stub Implementations in Release Mode
 *
 * This file provides stub implementations for functions related to NPK (North Peak PCI device)
 * logging. These functions are placeholders and currently do not perform any actual operations.
 * They can be replaced with actual implementations as needed.
 */

/**
 * @brief Set up NPK logging.
 *
 * This stub function simulates setting up logging using NPK.
 * In release mode, it performs no operation, serving as a placeholder.
 *
 * @param[in] param The parameters for setting up NPK logging.
 *
 * @return None.
 */
void npk_log_setup(__unused struct hv_npk_log_param *param) {}

/**
 * @brief Write a log entry to NPK.
 *
 * This stub function simulates writing a log entry to NPK.
 * In release mode, it performs no operation, serving as a placeholder.
 *
 * @param[in] buf The buffer containing the log entry to write.
 * @param[in] len The length of the log entry.
 *
 * @return None.
 */
void npk_log_write(__unused const char *buf, __unused size_t len) {}

/**
 * @brief Check if NPK logging is needed based on severity.
 *
 * This stub function simulates checking if logging to NPK is needed based on the severity level.
 *
 * @param[in] severity The severity level of the log.
 *
 * @return A boolean indicating if NPK logging is needed.
 * @retval false Always returns false, indicating logging is not needed.
 */
bool npk_need_log(__unused uint32_t severity) { return false; }

/**
 * @brief Log a message to NPK.
 *
 * This stub function simulates logging a message to NPK.
 * In release mode, it performs no operation, serving as a placeholder.
 *
 * @param[in] buffer The message to log.
 *
 * @return None.
 */
void npk_log(__unused char *buffer) {}

/**
 * @}
 */