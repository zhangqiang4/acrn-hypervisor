/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <asm/lib/atomic.h>
#include <sprintf.h>
#include <asm/per_cpu.h>
#include <logmsg.h>
#include <ticks.h>
#ifdef HV_DEBUG
#include <console.h>
#include <framebuffer.h>
#include <npk_log.h>
#endif

/**
 * @defgroup debug debug
 * @brief The debug module includes logging, tracing, and debugging interfaces.
 *
 * The debug module provides comprehensive logging, tracing, and debugging interfaces to facilitate
 * efficient diagnosis and troubleshooting of hypervisor issues.
 *
 * @{
 */

/**
 * @file
 * @brief Logging Subsystem for ACRN Hypervisor
 *
 * This file implements the logging subsystem for the ACRN hypervisor. It provides
 * functionality to log messages with varying severity levels to different outputs,
 * such as memory buffers, the console, and NPK (North Peak PCI device). The logging
 * system is designed to be efficient and safe, supporting concurrent operations
 * across multiple CPUs. It includes mechanisms to format log messages with metadata
 * like timestamps, CPU IDs, thread names, severity levels, and sequence numbers.
 * The subsystem ensures that logs are captured even if the primary logging mechanism
 * is unavailable by using temporary buffers.
 */

/**
 * @brief Sequence number for logging.
 *
 * This static variable is used to maintain a sequence number for log entries.
 * It is initialized to 0 and is incremented each time a new log entry is created.
 * The sequence number helps in tracking the order of log messages.
 */
static int32_t log_seq = 0;

/**
 * @brief Memory log level threshold.
 *
 * This variable sets the threshold for memory logging. Logs with a severity level
 * less than or equal to this value will be logged to memory. The default value is
 * set by CONFIG_MEM_LOGLEVEL_DEFAULT, and it can be adjusted in debug mode using
 * ACRN shell commands.
 */
uint16_t mem_loglevel = CONFIG_MEM_LOGLEVEL_DEFAULT;

/**
 * @brief Determine if a message should be logged to memory based on severity.
 *
 * This function checks if the given severity level is less than or equal to the
 * memory log level threshold, `mem_loglevel`. If so, it indicates that the message
 * should be logged to memory.
 *
 * @param[in] severity The severity level of the log message.
 *
 * @return A boolean value indicating whether the message should be logged to memory
 *
 * @retval true If the message should be logged to memory.
 * @retval false If the message should not be logged to memory.
 */
static inline bool mem_need_log(uint32_t severity)
{
	return (severity <= mem_loglevel);
}

/**
 * @brief Log a message to a shared buffer or temporary buffer.
 *
 * This function attempts to log a message contained in the `buffer` to a shared buffer
 * associated with the specified physical CPU (`pcpu_id`). If the shared buffer is
 * not available, the message is stored in a temporary buffer specific to the CPU.
 *
 * The function performs the following steps:
 * - Determines the length of the message in `buffer` using `strnlen_s`.
 * - Checks if the shared buffer (`sbuf`) is available for the specified CPU.
 *   - If available, it stores the message in the shared buffer using `sbuf_put_many`.
 *   - If not available, it stores the message in the per-CPU temporary buffer (`temp_logbuf`) and
 *     increments the count of messages stored in the temporary buffer (`temp_logbuf_count`).
 *
 * @param[in] pcpu_id The physical CPU ID.
 * @param[inout] buffer The log message to be stored.
 *
 * @return none
 *
 * @pre buffer != NULL
 */
static void mem_log(uint16_t pcpu_id, char *buffer)
{
	uint32_t msg_len;
	struct shared_buf *sbuf = per_cpu(sbuf, pcpu_id)[ACRN_HVLOG];
	msg_len = strnlen_s(buffer, LOG_MESSAGE_MAX_SIZE);

	if (sbuf != NULL) {
		(void)sbuf_put_many(sbuf, LOG_ENTRY_SIZE, (uint8_t *)buffer, roundup(msg_len, LOG_ENTRY_SIZE));
	} else {
		/* store logs in temp buffer */
		char (*temp_buffer)[LOG_MESSAGE_MAX_SIZE] = per_cpu(temp_logbuf, pcpu_id);
		uint32_t *temp_count = &per_cpu(temp_logbuf_count, pcpu_id);

		if (*temp_count < TEMP_LOG_BUFFER_SIZE) {
			(*temp_count)++;
			(void)memcpy_s(temp_buffer[(*temp_count) - 1U], LOG_MESSAGE_MAX_SIZE, buffer, msg_len);
		}
	}
}

/**
 * @brief Log a formatted message with severity to various outputs.
 *
 * This function logs a message with a given severity level to memory, console, and NPK,
 * based on the logging needs determined by the severity level. It formats the message
 * with a timestamp, CPU ID, thread name, severity, and a sequence number.
 *
 * The function performs the following steps:
 * - Checks if logging is needed for memory, console, or NPK based on the severity.
 * - Retrieves the current timestamp and scales it to microseconds.
 * - Gets the current physical CPU ID and the current thread object.
 * - Clears the log buffer and formats the initial part of the message with timestamp, CPU ID,
 *   thread name, severity, and a sequence number.
 * - Formats the remaining part of the message using the provided format string and arguments.
 * - Logs the message to memory, console, and NPK as needed based on the severity.
 *
 * @param[in] severity The severity level of the log message.
 * @param[in] fmt The format string for the log message, similar to printf.
 * @param[in] ... Additional arguments for the format string.
 *
 * @return none
 */
void do_logmsg(uint32_t severity, const char *fmt, ...)
{
	va_list args;
	uint64_t timestamp;
	uint16_t pcpu_id;
	char *buffer;
	struct thread_object *current;
#ifdef HV_DEBUG
	if (mem_need_log(severity) || console_need_log(severity) || npk_need_log(severity)) {
#else
	if (mem_need_log(severity)) {
#endif
		/* Get time-stamp value */
		timestamp = cpu_ticks();

		/* Scale time-stamp appropriately */
		timestamp = ticks_to_us(timestamp);

		/* Get CPU ID */
		pcpu_id = get_pcpu_id();
		buffer = per_cpu(logbuf, pcpu_id);
		current = sched_get_current(pcpu_id);

		(void)memset(buffer, 0U, LOG_MESSAGE_MAX_SIZE);
		/* Put time-stamp, CPU ID and severity into buffer */
		(void)snprintf(buffer, LOG_MESSAGE_MAX_SIZE, "[%luus][cpu=%hu][%s][sev=%u][seq=%u]:",
				timestamp, pcpu_id, current->name, severity, atomic_inc_return(&log_seq));

		/* Put message into remaining portion of local buffer */
		va_start(args, fmt);
		(void)vsnprintf(buffer + strnlen_s(buffer, LOG_MESSAGE_MAX_SIZE),
			LOG_MESSAGE_MAX_SIZE
			- strnlen_s(buffer, LOG_MESSAGE_MAX_SIZE), fmt, args);
		va_end(args);

		/* Check whether output to memory */
		if (mem_need_log(severity)) {
			mem_log(pcpu_id, buffer);
		}
#ifdef HV_DEBUG
		/* Check whether output to stdout */
		if (console_need_log(severity)) {
			console_log(buffer);
			/* framebuffer output shares the same loglevel with console */
			if (fb_need_log()) {
				fb_log(buffer);
			}
		}

		/* Check whether output to NPK */
		if (npk_need_log(severity)) {
			npk_log(buffer);
		}
#endif
	}
}

/**
 * @}
 */
