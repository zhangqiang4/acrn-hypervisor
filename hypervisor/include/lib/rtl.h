/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef RTL_H
#define RTL_H

#include <types.h>

/**
 * @addtogroup lib_util
 *
 * @{
 */

/**
 * @file
 * @brief Declaration of string operation function.
 *
 * This file provides space and end of line character checking.
 */

/**
 * @brief Allow store uint32_t or uint64_t types of data in the same memory space
 */
union u_qword {
	/**
	 * @brief Store two uint32_t type of values
	 */
	struct {
		uint32_t low; /**< Low 32bit value */
		uint32_t high; /**< High 32bit value */
	} dwords;

	uint64_t qword; /**< 64bit value */

};

#define ULONG_MAX	((uint64_t)(~0UL))	/**< 0xFFFFFFFF */
#define LONG_MAX	(ULONG_MAX >> 1U)	/**< 0x7FFFFFFF */
#define LONG_MIN	(~LONG_MAX)		/**< 0x80000000 */

/**
 * @brief Check whitespace character.
 *
 * This inline function check if the given character is whitespace character.
 *
 * - It checks if input parameter c character is a space or a horizontal tab, return
 * the checking result.
 *
 * @param[in] c      Character value to check
 *
 * @return A bool type value to indicate checking result.
 *
 * @retval true  When the given character is whitespace.
 * @retval false When the given character is not whitespace.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline bool is_space(char c)
{
	return ((c == ' ') || (c == '\t'));
}

/**
 * @brief Check end of line marker.
 *
 * This inline function check if the given character is an commonly used end of line marker.
 *
 * - It checks if input parameter c is '\\r' or '\\n' or '\\0', return the checking result.
 *
 * @param[in] c      Character value to check
 *
 * @return A bool type value to indicate checking result.
 *
 * @retval true   When the given character is end of line marker.
 * @retval false  When the given character is not end of line marker.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline bool is_eol(char c)
{
	return ((c == 0x0d) || (c == 0x0a) || (c == '\0'));
}

/* Function prototypes */
int32_t strcmp(const char *s1_arg, const char *s2_arg);
int32_t strncmp(const char *s1_arg, const char *s2_arg, size_t n_arg);
int32_t strncpy_s(char *d, size_t dmax, const char *s, size_t slen);
char *strchr(char *s_arg, char ch);
size_t strnlen_s(const char *str_arg, size_t maxlen_arg);
void *memset(void *base, uint8_t v, size_t n);
int32_t memcpy_s(void *d, size_t dmax, const void *s, size_t slen);
void memcpy_erms(void *d, const void *s, size_t slen);
void memcpy_erms_backwards(void *d, const void *s, size_t slen);
uint64_t strtoul_hex(const char *nptr);
const char *strstr_s(const char *str1, size_t maxlen1, const char *str2, size_t maxlen2);
int32_t strncat_s(char *dest, size_t dmax, const char *src, size_t slen);

/**
 * @}
 */

#endif /* RTL_H */
