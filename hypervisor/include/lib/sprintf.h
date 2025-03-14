/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef SPRINTF_H
#define SPRINTF_H

/**
 * @addtogroup lib_util
 *
 * @{
 */

/**
 * @file
 * @brief Declaration of print and format function.
 *
 * This file defines structure snprint_param and print_param and macro definition for emit function command.
 */

/** Command for the emit function: copy string to output. */
#define PRINT_CMD_COPY			0x00000000U

/** Command for the emit function: fill output with first character. */
#define PRINT_CMD_FILL			0x00000001U

/** Structure used to call back emit lived in print_param */
struct snprint_param {
	/** The destination buffer. */
	char *dst;
	/** The size of the destination buffer. */
	uint32_t sz;
	/** Counter for written chars. */
	uint32_t wrtn;
};

/**
 * Structure used to parse parameters and variables to subroutines. It is used to
 * support the implementation of the printing and formatting operations in Hypervisor
 */
struct print_param {
	/** A pointer to the function that is used to emit characters. */
	void (*emit)(size_t cmd, const char *s_arg, uint32_t sz_arg, struct snprint_param *param);
	/**
	 * An opaque pointer that is passed as forth argument to the emit
	 * function.
	 */
        struct snprint_param *data;
	/** Contains variables which are recalculated for each argument. */
	struct {
		/** A bitfield with the parsed format flags. */
		uint32_t flags;
		/** The parsed format width. */
		uint32_t width;
		/** The parsed format precision. */
		uint32_t precision;
		/** The bitmask for unsigned values. */
		uint64_t mask;
		/** A pointer to the pre-formatted value. */
		const char *value;
		/** The number of characters in the pre-formatted value buffer. */
		uint32_t valuelen;
		/** A pointer to the values prefix. */
		const char *prefix;
		/** The number of characters in the prefix buffer. */
		uint32_t prefixlen;
	} vars;
};

void do_print(const char *fmt_arg, struct print_param *param,
		__builtin_va_list args);
size_t vsnprintf(char *dst_arg, size_t sz_arg, const char *fmt, va_list args);
size_t snprintf(char *dest, size_t sz, const char *fmt, ...);

/**
 * @}
 */

#endif /* SPRINTF_H */
