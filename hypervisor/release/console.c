/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <pci.h>
#include <console.h>

/**
 * @addtogroup debug
 *
 * @{
 */

/**
 * @file
 * @brief Console and Debugging Interface Stubs in Release Mode
 *
 * This file provides stub implementations for console and debugging functions.
 * These functions are placeholders and do not perform any actual operations.
 * They are used to maintain a consistent interface and can be replaced with
 * actual implementations as needed.
 */

/**
 * @brief Write a string to the console.
 *
 * This stub function simulates writing a string to the console.
 *
 * @param[in] str The string to write.
 * @param[in] len The length of the string.
 *
 * @return The number of characters written.
 * @retval 0 Always returns 0, indicating no characters were written.
 */

size_t console_write(__unused const char *str, __unused size_t len)
{
	return 0;
}

/**
 * @brief Get a character from the console.
 *
 * This stub function simulates reading a character from the console.
 *
 * @return The character read from the console.
 * @retval Null-character Always returns the null character, indicating no character was read.
 */
char console_getc(void)
{
	return '\0';
}

/**
 * @brief Write a character to the console.
 *
 * This stub function simulates writing a character to the console. In release mode,
 * it performs no operation, serving as a placeholder.
 *
 * @param[in] ch The character to write.
 *
 * @return None.
 */
void console_putc(__unused const char *ch) {}

/**
 * @brief Initialize the console.
 *
 * This stub function simulates console initialization. In release mode,
 * it performs no operation, serving as a placeholder.
 *
 * @return None.
 */
void console_init(void) {}

/**
 * @brief Set up the console timer.
 *
 * This stub function simulates setting up a timer for the console. In release mode,
 * it performs no operation, serving as a placeholder.
 *
 * @return None.
 */
void console_setup_timer(void) {}

/**
 * @brief Suspend console operations.
 *
 * This stub function simulates suspending console operations. In release mode,
 * it performs no operation, serving as a placeholder.
 *
 * @return None.
 */
void suspend_console(void) {}

/**
 * @brief Resume console operations.
 *
 * This stub function simulates resuming console operations. In release mode,
 * it performs no operation, serving as a placeholder.
 *
 * @return None.
 */
void resume_console(void) {}

/**
 * @brief Handle a debug command.
 *
 * This stub function simulates handling a debug command.
 *
 * @param[in] cmd The command to handle.
 * @param[in] len The length of the command.
 *
 * @return A boolean indicating if the command was handled.
 * @retval false Always returns false, indicating the command was not handled.
 */
bool handle_dbg_cmd(__unused const char *cmd, __unused int32_t len) { return false; }

/**
 * @brief Callback for console VM exit.
 *
 * This stub function simulates a callback for console VM exit. In release mode,
 * it performs no operation, serving as a placeholder.
 *
 * @param[in] vcpu The virtual CPU context.
 *
 * @return None.
 */
void console_vmexit_callback(__unused struct acrn_vcpu *vcpu) {}

/**
 * @brief Check if using INIT IPI.
 *
 * This stub function simulates checking if INIT IPI is being used.
 *
 * @return A boolean indicating if INIT IPI is used.
 * @retval false Always returns false.
 */
bool is_using_init_ipi(void) { return false; }

/**
 * @brief Initialize the shell.
 *
 * This stub function simulates shell initialization. In release mode,
 * it performs no operation, serving as a placeholder.
 *
 * @return None.
 */
void shell_init(void) {}

/**
 * @brief Kick the shell.
 *
 * This stub function simulates kicking the shell. In release mode,
 * it performs no operation, serving as a placeholder.
 *
 * @return None.
 */
void shell_kick(void) {}

/**
 * @brief Check if console logging is needed.
 *
 * This stub function simulates checking if console logging is needed.
 *
 * @param[in] severity The severity level of the log.
 *
 * @return A boolean indicating if logging is needed.
 * @retval false Always returns false, indicating logging is not needed.
 */
bool console_need_log(__unused uint32_t severity) { return false; }

/**
 * @brief Log a message to the console.
 *
 * This stub function simulates logging a message to the console. In release mode,
 * it performs no operation, serving as a placeholder.
 *
 * @param[in] buffer The message to log.
 *
 * @return None.
 */
void console_log(__unused char *buffer) {}

/**
 * @}
 */