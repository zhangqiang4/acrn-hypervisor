/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <asm/per_cpu.h>
#include <ticks.h>
#include <trace.h>

/**
 * @addtogroup debug
 *
 * @{
 */

/**
 * @file
 * @brief Trace logging system for debugging and performance monitoring.
 *
 * This file provides functionality for logging trace entries with various data formats.
 * It includes functions to log entries with 64-bit values, 32-bit integers, and strings.
 */

#ifdef CONFIG_ACRNTRACE_ENABLED

/**
 * @brief Trace entry structure.
 *
 * This structure represents a trace entry, which includes a timestamp counter (TSC),
 * an identifier, the number of data elements, and the CPU ID. The payload can store
 * data in various formats, including 32-bit fields, 8-bit fields, 64-bit fields, or a string.
 * The structure is aligned to 8 bytes for efficient access.
 *
 * @alignment 8
 */
struct trace_entry {
	uint64_t tsc;	/**< Time Stamp Counter (TSC) value for high-resolution timing. */
	uint64_t id:48;	/**< 48-bit identifier for the trace entry, allowing a large range of unique IDs. */
	uint8_t n_data;	/**< Number of data elements in the trace entry, indicating how much of the payload is used. */
	uint8_t cpu; /**< Physical CPU ID where the trace entry was recorded, useful in multi-core systems. */

	/**
	 * @brief Stores additional data for the trace entry in various formats.
	 *
	 * The payload can be used to store data as 32-bit integers, 8-bit bytes,
	 * 64-bit integers, or a string, depending on the context of the trace.
	 */
	union {
		/**
		 * @brief Anonymous struct for storing four 32-bit integer fields.
		 *
		 * This struct provides storage for four 32-bit integers, allowing
		 * the trace entry to record multiple data points in a compact format.
		 */
		struct {
			uint32_t a; /**< First 32-bit integer field for data storage. */
			uint32_t b; /**< Second 32-bit integer field for data storage. */
			uint32_t c; /**< Third 32-bit integer field for data storage. */
			uint32_t d; /**< Fourth 32-bit integer field for data storage. */
		} fields_32;

		/**
		 * @brief Anonymous struct for storing sixteen 8-bit fields.
		 *
		 * This struct provides storage for sixteen 8-bit fields, useful for
		 * byte-level data recording in the trace entry.
		 */
		struct {
			uint8_t a1; /**< First 8-bit field in the first group. */
			uint8_t a2; /**< Second 8-bit field in the first group. */
			uint8_t a3; /**< Third 8-bit field in the first group. */
			uint8_t a4; /**< Fourth 8-bit field in the first group. */
			uint8_t b1; /**< First 8-bit field in the second group. */
			uint8_t b2; /**< Second 8-bit field in the second group. */
			uint8_t b3; /**< Third 8-bit field in the second group. */
			uint8_t b4; /**< Fourth 8-bit field in the second group. */
			uint8_t c1; /**< First 8-bit field in the third group. */
			uint8_t c2; /**< Second 8-bit field in the third group. */
			uint8_t c3; /**< Third 8-bit field in the third group. */
			uint8_t c4; /**< Fourth 8-bit field in the third group. */
			uint8_t d1; /**< First 8-bit field in the fourth group. */
			uint8_t d2; /**< Second 8-bit field in the fourth group. */
			uint8_t d3; /**< Third 8-bit field in the fourth group. */
			uint8_t d4; /**< Fourth 8-bit field in the fourth group. */
		} fields_8;

		/**
		 * @brief Anonymous struct for storing two 64-bit fields.
		 *
		 * This struct provides storage for two 64-bit integers, suitable for
		 * larger data values that need to be recorded in the trace entry.
		 */
		struct {
			uint64_t e;	/**< First 64-bit field for larger data values. */
			uint64_t f;	/**< Second 64-bit field for larger data values. */
		} fields_64;
		char str[16];	/**< 16-character string for textual data or identifiers. */
	} payload;
} __aligned(8);

/**
 * @brief Check if tracing is enabled for a given CPU.
 *
 * This inline function checks if the shared buffer for tracing is available
 * for the specified CPU ID.
 *
 * @param[in] cpu_id The physical CPU ID to check for trace buffer availability.
 *
 * @return A boolean value indicating the availability of the trace buffer.
 *
 * @retval true  The trace buffer is available for the specified CPU.
 * @retval false The trace buffer is not available for the specified CPU.
 */
static inline bool trace_check(uint16_t cpu_id)
{
	return (per_cpu(sbuf, cpu_id)[ACRN_TRACE] != NULL);
}

/**
 * @brief Store a trace entry in the shared buffer.
 *
 * This inline function populates a trace entry with the current timestamp,
 * event ID, number of data elements, and CPU ID, then stores it in the shared buffer associated with the given CPU.
 *
 * @param[in] cpu_id The physical CPU ID for which the trace entry is recorded.
 * @param[in] evid The event identifier for the trace entry.
 * @param[in] n_data The number of data elements in the trace entry.
 * @param[inout] entry A pointer to the `trace_entry` structure to be populated and stored.
 *
 * @return None.
 */
static inline void trace_put(uint16_t cpu_id, uint32_t evid, uint32_t n_data, struct trace_entry *entry)
{
	struct shared_buf *sbuf = per_cpu(sbuf, cpu_id)[ACRN_TRACE];

	entry->tsc = cpu_ticks();
	entry->id = evid;
	entry->n_data = (uint8_t)n_data;
	entry->cpu = (uint8_t)cpu_id;
	(void)sbuf_put(sbuf, (uint8_t *)entry, sizeof(*entry));
}

/**
 * @brief Trace an event with two 64-bit values.
 *
 * This function creates a trace entry with the specified event ID and two 64-bit data fields,
 * and records it into the trace system for the current CPU. It first checks if tracing is
 * enabled for the CPU, and if so, it populates the trace entry with the provided data and
 * stores it in the shared buffer.
 *
 * @param[in] evid The event identifier for the trace entry.
 * @param[in] e The first 64-bit data value to be logged.
 * @param[in] f The second 64-bit data value to be logged.
 *
 * @return None.
 */
void TRACE_2L(uint32_t evid, uint64_t e, uint64_t f)
{
	struct trace_entry entry;
	uint16_t cpu_id = get_pcpu_id();

	if (trace_check(cpu_id)) {
		entry.payload.fields_64.e = e;
		entry.payload.fields_64.f = f;
		trace_put(cpu_id, evid, 2U, &entry);
	}
}

/**
 * @brief Log a trace entry with four 32-bit integer data fields.
 *
 * This function creates a trace entry with the specified event ID and four 32-bit data fields,
 * and records it into the trace system for the current CPU. It first checks if tracing is
 * enabled for the CPU, and if so, it populates the trace entry with the provided data and
 * stores it in the shared buffer.
 *
 * @param[in] evid The event identifier for the trace entry.
 * @param[in] a The first 32-bit data value to be logged.
 * @param[in] b The second 32-bit data value to be logged.
 * @param[in] c The third 32-bit data value to be logged.
 * @param[in] d The fourth 32-bit data value to be logged.
 *
 * @return None.
 */
void TRACE_4I(uint32_t evid, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
	struct trace_entry entry;
	uint16_t cpu_id = get_pcpu_id();

	if (trace_check(cpu_id)) {
		entry.payload.fields_32.a = a;
		entry.payload.fields_32.b = b;
		entry.payload.fields_32.c = c;
		entry.payload.fields_32.d = d;
		trace_put(cpu_id, evid, 4U, &entry);
	}
}

/**
 * @brief Log a trace entry with a string payload of up to 16 characters.
 *
 * This function creates a trace entry with the specified event ID and a string payload,
 * and records it into the trace system for the current CPU. It first checks if tracing is
 * enabled for the CPU, and if so, it populates the trace entry with the provided string.
 * The string is truncated to 16 characters if necessary and is ensured to be null-terminated.
 *
 * @param[in] evid The event identifier for the trace entry.
 * @param[in] name The string to be logged, which will be truncated to 16 characters if necessary.
 *
 * @return None.
 */
void TRACE_16STR(uint32_t evid, const char name[])
{
	struct trace_entry entry;
	uint16_t cpu_id = get_pcpu_id();
	size_t len, i;

	if (trace_check(cpu_id)) {
		entry.payload.fields_64.e = 0UL;
		entry.payload.fields_64.f = 0UL;

		len = strnlen_s(name, 20U);
		len = (len > 16U) ? 16U : len;
		for (i = 0U; i < len; i++) {
			entry.payload.str[i] = name[i];
		}

		entry.payload.str[15] = 0;
		trace_put(cpu_id, evid, 16U, &entry);
	}
}

#else

/**
 * @brief Dummy implementation of TRACE_2L when tracing is disabled.
 *
 * @return None.
 */
void TRACE_2L(__unused uint32_t evid, __unused uint64_t e, __unused uint64_t f) {}

/**
 * @brief Dummy implementation of TRACE_4I when tracing is disabled.
 *
 * @return None.
 */
void TRACE_4I(__unused uint32_t evid, __unused uint32_t a, __unused uint32_t b,
		__unused uint32_t c, __unused uint32_t d)
{
}

/**
 * @brief Dummy implementation of TRACE_4I when tracing is disabled.
 *
 * @return None.
 */
void TRACE_16STR(__unused uint32_t evid, __unused const char name[]) {}

#endif

/**
 * @}
 */