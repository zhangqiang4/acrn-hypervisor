/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef LOGMSG_H
#define LOGMSG_H
#include <asm/cpu.h>

/**
 * @addtogroup debug
 *
 * @{
 */

/**
 * @file
 * @brief Logging and Debugging Interface
 *
 * This file provides definitions and macros for logging and debugging within
 * the system. It includes severity levels, buffer sizes, and macros for
 * logging messages at different severity levels. It also provides an assertion
 * mechanism and a panic function for handling critical errors.
 */

/* Logging severity levels */
#define LOG_FATAL		1U	/**< Fatal error level, highest severity. */
#define LOG_ACRN		2U	/**< ACRN-specific log level, used to differentiate hypervisor logs. */
#define LOG_ERROR		3U	/**< Error level for non-fatal errors. */
#define LOG_WARNING		4U	/**< Warning level for potential issues. */
#define LOG_INFO		5U	/**< Informational level for general messages. */
#define LOG_DEBUG		6U	/**< Debug level for detailed debugging information. */

#define LOG_ENTRY_SIZE	80U	/**< Size of each log entry. */
/* Size of buffer used to store a message being logged,
 * should align to LOG_ENTRY_SIZE.
 */
#define LOG_MESSAGE_MAX_SIZE	(4U * LOG_ENTRY_SIZE)	/**< Maximum size of a log message. */

#define TEMP_LOG_BUFFER_SIZE    256U	/**< Size of the temporary log buffer. */

#define DBG_LEVEL_LAPICPT	6U	/**< Debug level for LAPICPT. */

#if defined(HV_DEBUG)

void asm_assert(int32_t line, const char *file, const char *txt);

/**
 * @brief Assert macro for debugging.
 *
 * This macro checks a condition and triggers an assertion if the condition
 * is false. It is only active when HV_DEBUG is defined.
 *
 * @param[in] x The condition to check.
 */
#define ASSERT(x, ...) \
	do { \
		if (!(x)) {\
			asm_assert(__LINE__, __FILE__, "fatal error");\
		} \
	} while (false)

#else /* HV_DEBUG */

/**
 * @brief No-op assert macro.
 *
 * This macro does nothing in release mode.
 *
 * @param[in] x The condition to check.
 */
#define ASSERT(x, ...)	do { } while (false)

#endif /* HV_DEBUG */

void do_logmsg(uint32_t severity, const char *fmt, ...);
void printf(const char *fmt, ...);
void vprintf(const char *fmt, va_list args);

/**
 * @brief Define a macro for the print prefix if not already defined.
 *
 * This code checks if the macro `pr_prefix` is not defined. If it is not defined,
 * it defines `pr_prefix` as an empty macro. This is used to ensure that
 * a default prefix is available for logging or printing functions, allowing for
 * customization of output formats without causing redefinition errors.
 */
#ifndef pr_prefix
#define pr_prefix
#endif

/**
 * @brief Log a fatal error message.
 *
 * This macro logs a message with the fatal error severity level. It is used
 * for critical errors that require immediate attention.
 *
 * @param[in] ... The format string and additional arguments for the log message.
 */
#define pr_fatal(...)						\
	do {							\
		do_logmsg(LOG_FATAL, pr_prefix __VA_ARGS__);	\
	} while (false)

/**
 * @brief Log ACRN-specific messages with a predefined log level.
 *
 * This macro facilitates logging messages specific to the ACRN hypervisor by
 * using the `LOG_ACRN` log level. It formats the message with `pr_prefix` and
 * passes the arguments to `do_logmsg`, ensuring consistent logging of hypervisor
 * events and information.
 *
 * @param[in] ... The format string and additional arguments for the log message.
 */
#define pr_acrnlog(...)						\
	do {							\
		do_logmsg(LOG_ACRN, pr_prefix __VA_ARGS__);	\
	} while (false)

/**
 * @brief Log an error message.
 *
 * This macro logs a message with the error severity level. It is used for
 * non-fatal errors that need to be addressed.
 *
 * @param[in] ... The format string and additional arguments for the log message.
 */
#define pr_err(...)						\
	do {							\
		do_logmsg(LOG_ERROR, pr_prefix __VA_ARGS__);	\
	} while (false)

/**
 * @brief Log a warning message.
 *
 * This macro logs a message with the warning severity level. It is used for
 * potential issues that may require attention.
 *
 * @param[in] ... The format string and additional arguments for the log message.
 */
#define pr_warn(...)						\
	do {							\
		do_logmsg(LOG_WARNING, pr_prefix __VA_ARGS__);	\
	} while (false)

/**
 * @brief Log an informational message.
 *
 * This macro logs a message with the informational severity level. It is used
 * for general messages that provide information about the system's state.
 *
 * @param[in] ... The format string and additional arguments for the log message.
 */
#define pr_info(...)						\
	do {							\
		do_logmsg(LOG_INFO, pr_prefix __VA_ARGS__);	\
	} while (false)

/**
 * @brief Log a debug message.
 *
 * This macro logs a message with the debug severity level. It is used for
 * detailed debugging information that is useful during development.
 *
 * @param[in] ... The format string and additional arguments for the log message.
 */
#define pr_dbg(...)						\
	do {							\
		do_logmsg(LOG_DEBUG, pr_prefix __VA_ARGS__);	\
	} while (false)

/**
 * @brief Log a device-specific debug message.
 *
 * This macro logs a message with a specified debug level. It is used for
 * device-specific debugging information, allowing for different levels of
 * verbosity.
 *
 * @param[in] lvl The debug level for the message.
 * @param[in] ... The format string and additional arguments for the log message.
 */
#define dev_dbg(lvl, ...)					\
	do {							\
		if ((lvl) > 0) {                                \
			do_logmsg((lvl), pr_prefix __VA_ARGS__);\
		}                                               \
	} while (false)

/**
 * @brief Trigger a panic with a fatal error message.
 *
 * This macro logs a fatal error message and enters an infinite loop, effectively
 * halting the system. It is used for critical errors that cannot be recovered from.
 *
 * @param[in] ... The format string and additional arguments for the log message.
 */
#define panic(...) 							\
	do { pr_fatal("PANIC: %s line: %d\n", __func__, __LINE__);	\
		pr_fatal(__VA_ARGS__); 					\
		while (true) { asm_pause(); }; } while (false)

extern uint16_t mem_loglevel;

/**
 * @}
 */
#endif /* LOGMSG_H */
