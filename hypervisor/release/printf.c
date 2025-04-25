/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <common/logmsg.h>
/**
 * @addtogroup debug
 *
 * @{
 */

/**
 * @file
 * @brief Formatted Output Stub Implementations in Release Mode
 *
 * This file provides stub implementations for the `printf` and `vprintf` functions.
 * These functions are placeholders and currently do not perform any actual operations.
 * They are used to maintain a consistent interface and can be replaced with actual
 * implementations as needed. These stubs are useful in environments where formatted
 * output is not required or is handled differently.
 */

/**
 * @brief Print formatted output.
 *
 * This stub function simulates printing formatted output to a standard output stream.
 * In release mode, it performs no operation, serving as a placeholder.
 *
 * @param[in] fmt The format string that specifies how subsequent arguments are converted for output.
 * @param[in] ... Additional arguments to be formatted and printed.
 *
 * @return None.
 */
void printf(__unused const char *fmt, ...) {}

/**
 * @brief Print formatted output using a va_list.
 *
 * This stub function simulates printing formatted output using a va_list of arguments.
 * In release mode, it performs no operation, serving as a placeholder.
 *
 * @param[in] fmt The format string that specifies how subsequent arguments are converted for output.
 * @param[in] args A va_list of arguments to be formatted and printed.
 *
 * @return None.
 */
void vprintf(__unused const char *fmt, __unused va_list args) {}

/**
 * @}
 */
