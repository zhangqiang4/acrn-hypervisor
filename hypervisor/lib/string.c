/*
 * Copyright (C) 2018-2025 Intel Corporation.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <types.h>
#include <rtl.h>
#include <logmsg.h>

/**
 * @defgroup lib_util lib.util
 * @ingroup lib
 * @brief Implementation of misc utility functions.
 *
 * This module provides a collection of helper functions that serve common tasks, such as string manipulation,
 * I/O operations, math calculations and list manipulation.
 *
 * @{
 */

/**
 * @file
 * @brief Implementation of string manipulation.
 *
 * This file implements all the APIs to support string operation.
 */

/**
 * @brief Convert a hexadecimal character into numerical value.
 *
 * This static inline function returns integer value for valid hexadecimal character.
 *
 * - It generates the corresponding integer value from 0 to 9 by subtracting '0' from the
 * input character, when the input character varies from '0' to '9'.
 * - It generates the corresponding integer value from 10 to 15 by subtracting 'a' from the
 * input character and adding 10 to adjust the result, when the input character varies from 'a' to 'f'.
 * - It generates the corresponding integer value from 10 to 15 by subtracting 'A' from the input
 * character and adding 10 to adjust the result, when the input character varies from 'A' to 'F'.
 * - It generates -1 when the input character is not valid.
 *
 * @param[in] ch      Character value to convert
 *
 * @return A numerical value corresponding to the given character
 *
 * @retval -1    When input ch is not valid.
 * @retval 0-15  When input ch is valid.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline int8_t hex_digit_value(char ch)
{
	int8_t c;
	if (('0' <= ch) && (ch <= '9')) {
		c = (int8_t)(ch - '0');
	} else if (('a' <= ch) && (ch <= 'f')) {
		c = (int8_t)(ch - 'a') + 10;
	} else if (('A' <= ch) && (ch <= 'F')) {
		c = (int8_t)(ch - 'A') + 10;
	} else {
		c = -1;
	}
	return c;
}

/**
 * @brief Convert a hexadecimal string into a 64 bits uint64_t type integer.
 *
 * This function returns a 64 bits integer value for a given hexadecimal string.
 *
 * - It calls function is_space() to skip any leading spaces, for detail operation, refer to function is_space().
 * If the string starts with 0x or 0X, it skips those characters. Then it continues to read characters from input
 * string one by one and converts them to numeric values by calling function hex_digit_value(), For detail operation,
 * refer to function hex_digit_value(), and accumulate the numeric values in question and return the result.
 * - If there is any overflow detected by checking conditions that the accumulated result is more than 0xFFFFFFF,
 * or that the accumulated result equals 0xFFFFFFFF, the accumulation calculation will stop, and set the final
 * result to 0xFFFFFFFF.
 *
 * @param[in] nptr      Constant char pointer pointing to the string to convert
 *
 * @return A numerical value corresponding to the given string
 *
 * @pre nptr != NULL
 *
 * @post N/A
 */
uint64_t strtoul_hex(const char *nptr)
{
	const char *s = nptr;
	char c;
	int8_t digit;
	uint64_t acc, cutoff;
	uint64_t base = 16UL;
	int32_t any;

	/*
	 * See strtol for comments as to the logic used.
	 */
	do {
		c = *s;
		s++;
	} while (is_space(c));

	if ((c == '0') && ((*s == 'x') || (*s == 'X'))) {
		c = s[1];
		s += 2;
	}

	cutoff = ULONG_MAX / base;
	acc = 0UL;
	any = 0;
	digit = hex_digit_value(c);
	while (digit >= 0) {
                /* The original overflow checking logic has cutoff and
		   cutlim checks at the same time:
		   (acc > cutoff) || ((acc == off) && (digit > cutlim)),
		   where cutlim is ULONG_MAX % base. Since we're implementing
		   specifically for base 16, digit > cutlim will never be
		   true, so simplify the logic to (acc > cutoff). */
		if (acc > cutoff) {
			any = -1;
			break;
		} else {
			acc *= base;
			acc += (uint64_t)digit;
		}

		c = *s;
		s++;
		digit = hex_digit_value(c);
	}

	if (any < 0) {
		acc = ULONG_MAX;
	}
	return acc;
}

/**
 * @brief Search for the first occurrence of a specified character.
 *
 * This function searches for the first occurrence of a specified character in a null-terminated string.
 *
 * - It does a loop iterating through the string until either the null character or the target input
 * character is found, if the target character is found, it returns the pointer.
 * - It returns NULL if the loop reaches the end of the string.
 *
 * @param[in] s_arg      Character pointer pointing to a string to search through
 * @param[in] ch         Character type value to search
 *
 * @return A char pointer pointing to the first occurrence of given character
 *
 * @retval NULL        When the character ch is not found in the string.
 * @retval non-NULL    Pointing to the first occurrence of the character in the string.
 *
 * @pre s_arg != NULL
 *
 * @post N/A
 */
char *strchr(char *s_arg, char ch)
{
	char *s = s_arg;
	while ((*s != '\0') && (*s != ch)) {
		++s;
	}

	return ((*s) != '\0') ? s : NULL;
}

/**
 * @brief Copy string.
 *
 * This function copies from a source string to a destination buffer up to maximum count of characters.
 *
 * - It firstly ensures that the destination buffer is larger enough for the copy operation, and then calls memcpy_s()
 * to perform copy operation. If memcpy_s() succeeds, the destination buffer is explicitly null-terminated and the
 * return value is set 0.
 *
 * - If the destination buffer is not larger enough or memcpy_s() fails, the return value is set to -1 to indicate
 * the failure. It further sets the destination buffer to an empty string if provided d is not NULL and dmax > 0.
 *
 * @param[in] d          Char pointer pointing to destination buffer
 * @param[in] dmax       Maximum length of dest buffer
 * @param[in] s          Constant char pointer pointing to the source string
 * @param[in] slen       The maximum number of characters to copy from source string
 *
 * @return A numerical value indicating status of copy operation
 *
 * @retval 0          If source string is copied successfully.
 * @retval non-zero   If there is a failure.
 *
 * @pre N/A
 *
 * @post N/A
 */
int32_t strncpy_s(char *d, size_t dmax, const char *s, size_t slen)
{
	char *dest = d;
	int32_t ret = -1;
	size_t len = strnlen_s(s, dmax);

	if ((slen < dmax) || (dmax > len)) {
		ret = memcpy_s(d, dmax, s, len);
	}

	if (ret == 0) {
		*(dest + len) = '\0';
	} else {
		if ((d != NULL) && (dmax > 0U)) {
			*dest = '\0';
		}
	}

	return ret;
}

/**
 * @brief Calculates the length of the string.
 *
 * This function determines the length of a string up to a maximum number of characters excluding null terminator.
 *
 * - If the input string is null, then return length result 0.
 *
 * @param[in] str_arg      Constant char pointer pointing to the string
 * @param[in] maxlen_arg   Maximum number of characters to calculate
 *
 * @return The number of bytes in the string pointed to by str_arg.
 *
 * @retval >0  When the input string is not NULL and input maxlen_arg is not 0.
 * @retval 0  When the input string is NULL or input maxlen_arg is 0.
 *
 * @pre N/A
 *
 * @post N/A
 */
size_t strnlen_s(const char *str_arg, size_t maxlen_arg)
{
	const char *str = str_arg;
	size_t count = 0U;

	if (str != NULL) {
		size_t maxlen = maxlen_arg;
		while ((*str) != '\0') {
			if (maxlen == 0U) {
				break;
			}

			count++;
			maxlen--;
			str++;
		}
	}

	return count;
}

/**
 * @brief Compares two null-terminated strings.
 *
 * This function does the difference calculation based on the first differing character's value.
 *
 * - It does the loop iterating through both input strings until one of them reaches its null terminator,
 * or current characters of both strings are equal, and then compare the two values at the first differing
 * position, returns the comparison result.
 *
 * @param[in] s1_arg      Constant char pointer pointing to the first string
 * @param[in] s2_arg      Constant char pointer pointing to the second string
 *
 * @return An int32_t value indicating the comparison result
 *
 * @retval >0  When the first string is more than the second.
 * @retval 0    When strings are identical
 * @retval <0  When the first string is less than the second.
 *
 * @pre s1_arg != NULL
 * @pre s2_arg != NULL
 *
 * @post N/A
 */
int32_t strcmp(const char *s1_arg, const char *s2_arg)
{
	const char *str1 = s1_arg;
	const char *str2 = s2_arg;

	while (((*str1) != '\0') && ((*str2) != '\0') && ((*str1) == (*str2))) {
		str1++;
		str2++;
	}

	return *str1 - *str2;
}

/**
 * @brief Compares two strings for a number of character.
 *
 * This function does the difference calculation based on the first differing character's value
 * for up to the given number of characters.
 *
 * - If the input parameter is more than to 0, the function enters a loop iterating from the beginning
 * of the two input strings, compare the two characters and increment the two input string pointer at each
 * iteration step. The loop will continue only when it does not reach the maximum comparison limitation
 * defined by input parameter n_arg, and neither of the two strings reaches their null terminator, and
 * the two characters from two string equal. when the loop exits, two pointers will pointer to the two
 * string position, the function calculates the difference between the two characters pointed by the two
 * pointers in question, the difference determines the result of the comparison, and convert the result
 * to int32_t type and return.

 * - If the input parameter is less than or equal to 0, then return 0.
 *
 * @param[in] s1_arg      Constant char pointer pointing to the first string
 * @param[in] s2_arg      Constant char pointer pointing to the second string
 * @param[in] n_arg       Maximum number of characters to compare
 *
 * @return An int32_t value indicating the comparison result
 *
 * @retval >0  When the first string is more than the second.
 * @retval 0    When strings are identical
 * @retval <0  When the first string is less than the second.
 *
 * @pre s1_arg != NULL
 * @pre s2_arg != NULL
 *
 * @post N/A
 */
int32_t strncmp(const char *s1_arg, const char *s2_arg, size_t n_arg)
{
	const char *str1 = s1_arg;
	const char *str2 = s2_arg;
	size_t n = n_arg;
	int32_t ret = 0;

	if (n > 0U) {
		while (((n - 1) != 0U) && ((*str1) != '\0') && ((*str2) != '\0') && ((*str1) == (*str2))) {
			str1++;
			str2++;
			n--;
		}
		ret = (int32_t) (*str1 - *str2);
	}

	return ret;
}

/**
 * @brief Search one string
 *
 * This function is to find the first occurrence of a substring within another string.
 *
 * - It performs initial NULL checks for str1 and str2, if one of them is NULL, then set
 * the return pointer as NULL, and return.
 * - It performs initial length limitation checks for str1 and str2, if one of maxlen1 and
 * maxlen2 is 0, then set the return pointer as NULL, and return.
 * - It calls the function strnlen_s() to get the actual length of str1 bounded by maxlen1,
 * and the actual length of str2 bounded by maxlen2, refer to function strnlen_s() for detail.
 * If the substring can not fit in str1, that is the actual length of str1 is less then the
 * one of str2, then return NULL. If the two strings are equal or the actual length of str2
 * is 0, then return str1.
 *
 * - It performs the search operation by launching a while loop with the condition that the
 * actual length of str1 is not less then the actual length of str2, in the while loop, a for
 * loop is started to compare the characters of str1 with str2 up to the actual length of str2
 * from the beginning of the str1, if the two characters are not equal, the for loop breaks and
 * continues the while loop with the next character in str1, otherwise if the for loop ends
 * normally, meaning that it has found the first substring str2, then record the position in return
 * pointer and break the while loop and return.
 *
 * @param[in] str1      Constant char pointer pointing to the string to search in
 * @param[in] maxlen1   Maximum length to consider for str1
 * @param[in] str2      Constant char pointer pointing to the substring to search for
 * @param[in] maxlen2   Maximum length to consider for str2
 *
 * @return A pointer to the beginning of the located substring, or NULL if the substring is not found.
 *
 * @retval non-NULL      Pointing to the location of the first occurrence of str2 in str1.
 * @retval NULL          If str2 is not found.
 *
 * @pre N/A
 *
 * @post N/A
 */
char *strstr_s(const char *str1, size_t maxlen1, const char *str2, size_t maxlen2)
{
	size_t len1, len2;
	size_t i;
	const char *pstr, *pret;

	if ((str1 == NULL) || (str2 == NULL)) {
		pret = NULL;
	} else if ((maxlen1 == 0U) || (maxlen2 == 0U)) {
		pret = NULL;
	} else {
		len1 = strnlen_s(str1, maxlen1);
		len2 = strnlen_s(str2, maxlen2);

		if (len1 < len2) {
			pret = NULL;
		} else if ((str1 == str2) || (len2 == 0U)) {
			/* return str1 if str2 equals to str1 or str2 points to a string with zero length*/
			pret = str1;
		} else {
			pret = NULL;
			pstr = str1;
			while (len1 >= len2) {
				for (i = 0U; i < len2; i++) {
					if (pstr[i] != str2[i]) {
						break;
					}
				}
				if (i == len2) {
					pret = pstr;
					break;
				}
				pstr++;
				len1--;
			}
		}
	}

	return (char *)pret;
}

/**
 * @brief Append source string to the end of destination string
 *
 * This function is to copy and append source string to destination string with '\0' appended at last.
 *
 * - It firstly does the validation checks below, the destination
 * and source string are not NULL, and there is enough space in destination string to append the actual length
 * of source string and the null terminator, and there is no memory overlap between the destination and the
 * source buffer.
 *
 * - If the validation checks succeeds, it calls the function memcpy_s() to copy the actual length
 * of source string from source string to the end of the destination string, and append a null terminator after
 * the copied string and set return value to 0 and return.
 *
 * - If the validation checks fail, it ensures the first character of the destination string is set to the
 * null character, and return -1.
 *
 * @param[in] dest    Pointer casted to char * pointing to the destination string to be appended.
 * @param[in] dmax    Maximum length of destination buffer.
 * @param[in] src     Constant pointer casted to char * pointing to the string to append.
 * @param[in] slen    Maximum length of source buffer to append with.
 *
 * @return A int32_t value to indicate the status of string appending operation.
 *
 * @retval 0    On success.
 * @retval -1   If the string appending operation fails.
 *
 * @pre N/A
 *
 * @post N/A
 */
int32_t strncat_s(char *dest, size_t dmax, const char *src, size_t slen)
{
	int32_t ret = -1;
	size_t len_d, len_s;
	char *d = dest, *start;

	len_d = strnlen_s(dest, dmax);
	len_s = strnlen_s(src, slen);
	start = dest + len_d;

	if ((dest != NULL) && (src != NULL) && (dmax > (len_d + len_s))
			&& ((dest > (src + len_s)) || (src > (dest + len_d)))) {
		(void)memcpy_s(start, (dmax - len_d), src, len_s);
		*(start + len_s) = '\0';
		ret = 0;
	} else {
		if (dest != NULL) {
			*d = '\0';	/* set dest[0] to NULL char on runtime-constraint violation */
		}
	}
	return ret;
}

/**
 * @}
 */
