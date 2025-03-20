/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <types.h>
#include <rtl.h>
#include <util.h>
#include <sprintf.h>

/**
 * @addtogroup lib_util
 *
 * @{
 */

/**
 * @file
 * @brief Implementation of printing and formatting data interfaces.
 *
 * This file implements all the APIs to format and print various type of data, such as string, number.
 */

/** maximum length for a string that can be printed */
#define PRINT_STRING_MAX_LEN		4096U
/** The length of the digits translation table */
#define HEX_DIGITS_LEN			17U

/** Use upper case letters for hexadecimal format. */
#define PRINT_FLAG_UPPER		0x00000001U

/** Use alternate form. */
#define PRINT_FLAG_ALTERNATE_FORM 	0x00000002U

/** Use '0' instead of ' ' for padding. */
#define PRINT_FLAG_PAD_ZERO		0x00000004U

/** Use left instead of right justification. */
#define PRINT_FLAG_LEFT_JUSTIFY		0x00000008U

/** Always use the sign as prefix. */
#define PRINT_FLAG_SIGN			0x00000010U

/** Use ' ' as prefix if no sign is used. */
#define PRINT_FLAG_SPACE		0x00000020U

/** The original value was a (unsigned) char. */
#define PRINT_FLAG_CHAR			0x00000040U

/** The original value was a (unsigned) short. */
#define PRINT_FLAG_SHORT		0x00000080U

/** The original value was a (unsigned) long. 64bit on ACRN also */
#define PRINT_FLAG_LONG			0x00000200U

/** The original value was a (unsigned) long long. */
#define PRINT_FLAG_LONG_LONG		0x00000200U

/** The value is interpreted as unsigned. */
#define PRINT_FLAG_UINT32		0x00000400U

/** The characters to use for upper case hexadecimal conversion.
 *
 *  Note that this array is 17 bytes long. The first 16 characters
 *  are used to convert a 4 bit number to a printable character.
 *  The last character is used to determine the prefix for the
 *  alternate form.
 */

static const char upper_hex_digits[] = {
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
	'A', 'B', 'C', 'D', 'E', 'F', 'X'
};

/** The characters to use for lower case hexadecimal conversion.
 *
 *  Note that this array is 17 bytes long. The first 16 characters
 *  are used to convert a 4 bit number to a printable character.
 *  The last character is used to determine the prefix for the
 *  alternate form.
 */

static const char lower_hex_digits[] = {
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
	'a', 'b', 'c', 'd', 'e', 'f', 'x'
};

/**
 * @brief Parse a number from a given string.
 *
 * This function parses an unsigned integer (uint32_t) from a given string.
 *
 * - If the first character is a minus sign (-), it is ignored. The function moves to the next character to process.
 * - It launches a loop to parse the number in the string until a non-numeric char is found. The parsed number is
 * stored in \p x. For example, if a string "-456zzz" is given, the parsed number 456 will be stored in \p x.
 *
 * @param[in] s_arg       A pointer to the input string to parse the number from
 * @param[inout] x        A pointer to a uint32_t variable where the parsed number will be stored
 *
 * @return A pointer to the character in the string where parsing stopped
 *
 * @pre s_arg != NULL
 * @pre x != NULL
 *
 * @post N/A
 */
static const char *get_param(const char *s_arg, uint32_t *x)
{
	const char *s = s_arg;
	*x = 0U;

	/** ignore '-' for negative numbers, it will be handled in flags*/
	if (*s == '-') {
		++s;
	}

	/** parse uint32_teger */
	while ((*s >= '0') && (*s <= '9')) {
		char delta = *s - '0';
		*x = ((*x) * 10U) + (uint32_t)delta;
		s++;
	}

	return s;
}

/**
 * @brief Parse specific formatting flags from a string.
 *
 * This function parses specific formatting flags from a string and output corresponding flags.
 *
 * - It launches a loop to iterates over each character in the input string s_arg, and it checks if it matches one of
 * the flag characters in "#0- +". If a match is found, the corresponding flag value is applied using a bitwise OR
 * operation, the loop moves to the next character. The loop continues until no matching flag character is found.
 *
 * - If the left justification flag (PRINT_FLAG_LEFT_JUSTIFY) is set, the pad-zero flag (PRINT_FLAG_PAD_ZERO) is cleared
 *
 * - If the sign flag (PRINT_FLAG_SIGN) is set, the space flag (PRINT_FLAG_SPACE) is cleared
 *
 * @param[in] s_arg           A pointer to the input string that contains flags
 * @param[inout] flags        A pointer to a uint32_t variable where the parsed flags will be stored
 *
 * @return A pointer to the character in the string where flag parsing stopped
 *
 * @pre s_arg != NULL
 * @pre flags != NULL
 *
 * @post N/A
 */
static const char *get_flags(const char *s_arg, uint32_t *flags)
{
	const char *s = s_arg;
	/** contains the flag characters */
	static const char flagchars[5] = "#0- +";
	/** contains the numeric flags for the characters above */
	static const uint32_t fl[sizeof(flagchars)] = {
		PRINT_FLAG_ALTERNATE_FORM,	/**<  # */
		PRINT_FLAG_PAD_ZERO,	/**< 0 */
		PRINT_FLAG_LEFT_JUSTIFY,	/**< - */
		PRINT_FLAG_SPACE,	/**< ' ' */
		PRINT_FLAG_SIGN		/**< + */
	};
	uint32_t i;
	bool found;

	/** parse multiple flags */
	while ((*s) != '\0') {
		/*
		 * Get index of flag.
		 * Terminate loop if no flag character was found.
		 */
		found = false;
		for (i = 0U; i < sizeof(flagchars); i++) {
			if (*s == flagchars[i]) {
				found = true;
				break;
			}
		}
		if (!found) {
			break;
		}

		/** apply matching flags and continue with the next character */
		++s;
		*flags |= fl[i];
	}

	/** Spec says that '-' has a higher priority than '0' */
	if ((*flags & PRINT_FLAG_LEFT_JUSTIFY) != 0U) {
		*flags &= ~PRINT_FLAG_PAD_ZERO;
	}

	/** Spec says that '+' has a higher priority than ' ' */
	if ((*flags & PRINT_FLAG_SIGN) != 0U) {
		*flags &= ~PRINT_FLAG_SPACE;
	}

	return s;
}

/**
 * @brief Parse length modifiers from a given string.
 *
 * This function checks for the presence of the h or l length modifiers and adjusts the flags and mask accordingly.
 *
 * - If the characters are hh, it sets the PRINT_FLAG_CHAR flag (indicating a char type) and sets the mask to
 * 0x000000FFU and return
 * - If the character is h, it sets the PRINT_FLAG_SHORT flag (indicating a short type) and sets the mask to 0x0000FFFFU
 * and return
 * - If the characters are ll, it sets the PRINT_FLAG_LONG_LONG flag, and return
 * - If the character is l, it sets the PRINT_FLAG_LONG flag, and return
 * - If above conditions are not satisfied, the function returns without any further operation
 *
 * @param[in] s_arg       A pointer to the input string containing the format specifier
 * @param[inout] flags    A pointer to a uint32_t variable where flags related to the length modifier are stored
 * @param[inout] mask     A pointer to a uint64_t variable where a mask related to the length modifier is stored
 *
 * @return A pointer to the character in the string where parsing stopped
 *
 * @pre s_arg != NULL
 * @pre flags != NULL
 * @pre mask != NULL
 *
 * @post N/A
 */
static const char *get_length_modifier(const char *s_arg,
			uint32_t *flags, uint64_t *mask)
{
	const char *s = s_arg;
	if (*s == 'h') {
		/** check for h[h] (char/short) */
		s++;
		if (*s == 'h') {
			*flags |= PRINT_FLAG_CHAR;
			*mask = 0x000000FFU;
			++s;
		} else {
			*flags |= PRINT_FLAG_SHORT;
			*mask = 0x0000FFFFU;
		}
	} else if (*s == 'l') {
		/** check for l[l] (long/long long) */
		s++;
		if (*s == 'l') {
			*flags |= PRINT_FLAG_LONG_LONG;
			++s;
		} else {
			*flags |= PRINT_FLAG_LONG;
		}
	} else {
		/** No length modifiers found. */
	}

	return s;
}

/**
 * @brief Format a number according to given parameters.
 *
 * This function formats a number according to various formatting parameters and prints it to buffer.
 *
 * - If the precision is greater than the total width (value length + prefix length), additional zero-padding for
 * precision is calculated.
 *
 * - If the width specified is greater than the total width (including precision padding), the remaining width is
 * calculated.
 *
 * - If the value is not left-justified, the padding character is set to a space ' '. If zero-padding
 * (PRINT_FLAG_PAD_ZERO) is specified, the padding character is changed to '0'. The prefix is emitted first.
 * Then, the function fills the width with the chosen padding character.
 *
 * - The prefix (e.g., "+" or "-") is emitted before the number itself and insert additional 0's for precision
 * and emit the actual value.
 *
 * - If left justification (PRINT_FLAG_LEFT_JUSTIFY) is specified, the function emits the required number of
 * spaces after the value to fill the remaining width.
 *
 * @param[inout] param    A pointer to a print_param structure that holds the formatting options
 *
 * @return None
 *
 * @pre param != NULL
 *
 * @post N/A
 */
static void format_number(struct print_param *param)
{
	/* contains the character used for padding */
	char pad;
	/* effective width of the result */
	uint32_t width;
	/* number of characters to insert for width (w) and precision (p) */
	uint32_t p = 0U, w = 0U;

	/* initialize variables */
	width = param->vars.valuelen + param->vars.prefixlen;

	/* calculate additional characters for precision */
	if (param->vars.precision > width) {
		p = param->vars.precision - width;
	}

	/* calculate additional characters for width */
	if (param->vars.width > (width + p)) {
		w = param->vars.width - (width + p);
	}

	/* handle case of right justification */
	if ((param->vars.flags & PRINT_FLAG_LEFT_JUSTIFY) == 0U) {
		/* assume ' ' as padding character */
		pad = ' ';

		/*
		 * if padding with 0 is used, we have to emit the prefix (if any
		 * ) first to achieve the expected result. However, if a blank is
		 * used for padding, the prefix is emitted after the padding.
		 */

		if ((param->vars.flags & PRINT_FLAG_PAD_ZERO) != 0U) {
			/* use '0' for padding */
			pad = '0';

			/* emit prefix, return early if an error occurred */
			param->emit(PRINT_CMD_COPY, param->vars.prefix,
					param->vars.prefixlen, param->data);

			/* invalidate prefix */
			param->vars.prefix = NULL;
			param->vars.prefixlen = 0U;
		}

		/* fill the width with the padding character, return early if
		 * an error occurred
		 */
		param->emit(PRINT_CMD_FILL, &pad, w, param->data);
	}

	/* emit prefix (if any), return early in case of an error */
	param->emit(PRINT_CMD_COPY, param->vars.prefix,
			param->vars.prefixlen, param->data);

	/* insert additional 0's for precision, return early if an error
	 * occurred
	 */
	param->emit(PRINT_CMD_FILL, "0", p, param->data);

	/* emit the pre-calculated result, return early in case of an error */
	param->emit(PRINT_CMD_COPY, param->vars.value,
			param->vars.valuelen, param->data);

	/* handle left justification */
	if ((param->vars.flags & PRINT_FLAG_LEFT_JUSTIFY) != 0U) {
		/* emit trailing blanks, return early in case of an error */
		param->emit(PRINT_CMD_FILL, " ", w, param->data);
	}

}

/**
 * @brief Format a number into a power-of-2-based number
 *
 * This function Formats a number in a specific base (either octal, hexadecimal, etc.) after performing
 * bitwise shifts. It does so by first applying a bit mask, converting the number to the desired base,
 * and then formatting it according to the specified parameters (e.g., width, precision, flags). Parses an
 * unsigned integer (uint32_t) from a given string.
 *
 * @param[inout]  param    A pointer to a print_param structure, which holds the formatting parameters and data.
 * @param[in] v_arg        The value to be formatted and printed
 * @param[in] shift        The number of bits to shift, which determines the numeral base. 1 indicates binary,
 *                         3 indicates octal, 4 indicates hexadecimal
 *
 * @return None
 *
 * @pre param != NULL
 *
 * @post N/A
 */
static void print_pow2(struct print_param *param,
		uint64_t v_arg, uint32_t shift)
{
	uint64_t v = v_arg;
	/* max buffer required for octal representation of uint64_t long */
	char digitbuff[22];
	/* Insert position for the next character+1 */
	char *pos = digitbuff + sizeof(digitbuff);
	/* pointer to the digits translation table */
	const char (*digits)[HEX_DIGITS_LEN];
	/* mask to extract next character */
	uint64_t mask;

	/* calculate mask */
	mask = (1UL << shift) - 1UL;

	/* determine digit translation table */
	digits = ((param->vars.flags & PRINT_FLAG_UPPER) != 0U) ? &upper_hex_digits : &lower_hex_digits;

	/* apply mask for short/char */
	v &= param->vars.mask;

	/* determine prefix for alternate form */
	if ((v == 0UL) &&
		((param->vars.flags & PRINT_FLAG_ALTERNATE_FORM) != 0U)) {

		if (shift == 4U) {
			param->vars.prefixlen = 2U;
			param->vars.prefix = (((*digits)[16] == 'x') ? "0x" : "0X");
		} else {
			param->vars.prefixlen = 1U;
			param->vars.prefix = "0";
		}
	}

	/* determine digits from right to left */
	do {
		pos--;
		*pos = (*digits)[(v & mask)];
		v >>= shift;
	} while (v != 0UL);

	/* assign parameter and apply width and precision */
	param->vars.value = pos;
	param->vars.valuelen = (uint32_t)((digitbuff + sizeof(digitbuff)) - pos);

	format_number(param);

	param->vars.value = NULL;
	param->vars.valuelen = 0U;

}

/**
 * @brief Format a signed or unsigned 64-bit integer value in decimal format
 *
 * This function formats a number in a decimal base with correct sign handling
 *
 * - If the number is negative and is treated as a signed number by checking the flag PRINT_FLAG_UINT32, then prefix "-"
 * is set as a minus sign.
 *
 * - If a sign is explicitly requested by param, it updates the prefix accordingly (either +, a space, or nothing).
 *
 * - Two loops are launched to process the higher 32 bits and lower 32 bits respectively to determine digits from
 * right to left by using division by 10 and modulo and then calls function format_number() to do the final format,
 * refer to function format_number() for details.
 *
 * @param[inout] param    A pointer to a print_param structure, which holds the formatting parameters and data.
 * @param[in] value       The value to be printed
 *
 * @return None
 *
 * @pre param != NULL
 *
 * @post N/A
 */
static void print_decimal(struct print_param *param, int64_t value)
{
	/* max. required buffer for uint64_t long in decimal format */
	char digitbuff[20];
	/* pointer to the next character position (+1) */
	char *pos = digitbuff + sizeof(digitbuff);
	/* current value in 32/64 bit */
	union u_qword v;
	/* next value in 32/64 bit */
	union u_qword nv;

	/* assume an unsigned 64 bit value */
	v.qword = ((uint64_t)value) & param->vars.mask;

	/*
	 * assign sign and correct value if value is negative and
	 * value must be interpreted as signed
	 */
	if (((param->vars.flags & PRINT_FLAG_UINT32) == 0U) && (value < 0)) {
		v.qword = (uint64_t)-value;
		param->vars.prefix = "-";
		param->vars.prefixlen = 1U;
	}

	/* determine sign if explicit requested in the format string */
	if (param->vars.prefix == NULL) {
		if ((param->vars.flags & PRINT_FLAG_SIGN) != 0U) {
			param->vars.prefix = "+";
			param->vars.prefixlen = 1U;
		} else if ((param->vars.flags & PRINT_FLAG_SPACE) != 0U) {
			param->vars.prefix = " ";
			param->vars.prefixlen = 1U;
		} else {
			/* No prefix specified. */
		}
	}

	/* process 64 bit value as long as needed */
	while (v.dwords.high != 0U) {
		/* determine digits from right to left */
		pos--;
		*pos = (int8_t)(v.qword % 10UL) + '0';
		v.qword = v.qword / 10UL;
	}

	nv.dwords.low = v.dwords.low;
	/* process 32 bit (or reduced 64 bit) value */
	do {
		/* determine digits from right to left. The compiler should be
		 * able to handle a division and multiplication by the constant
		 * 10.
		 */
		pos--;
		*pos = (int8_t)(nv.dwords.low % 10U) + '0';
		nv.dwords.low = nv.dwords.low / 10U;
	} while (nv.dwords.low != 0U);

	/* assign parameter and apply width and precision */
	param->vars.value = pos;
	param->vars.valuelen = (digitbuff + sizeof(digitbuff)) - pos;

	format_number(param);

	param->vars.value = NULL;
	param->vars.valuelen = 0U;

}

/**
 * @brief Format and print a string.
 *
 * This function handles the formatting and printing of a string based on given param.
 *
 * - It truncates the precision for the string with given param, and then if the width given by param is greater
 * than the length of the string, extra characters will be added to the output to fill the width.
 *
 * - If the PRINT_FLAG_LEFT_JUSTIFY flag is not set, the function first emits spaces to fill the left side of
 * the string, then, prints the string.
 *
 * - If the PRINT_FLAG_LEFT_JUSTIFY flag is set, the function prints the string first, then emits spaces to the right.
 *
 * @param[inout] param    A pointer to a print_param structure that holds the formatting options
 * @param[in] s           The string to be printed
 *
 * @return None
 *
 * @pre param != NULL
 * @pre s != NULL
 *
 * @post N/A
 */
static void print_string(const struct print_param *param, const char *s)
{
	/** the length of the string (-1) if unknown */
	uint32_t len;
	/* the number of additional characters to insert to reach the required
	 * width
	 */
	uint32_t w = 0U;

	len = strnlen_s(s, PRINT_STRING_MAX_LEN);

	/** precision gives the max. number of characters to emit. */
	if ((param->vars.precision != 0U) && (len > param->vars.precision)) {
		len = param->vars.precision;
	}

	/* calculate the number of additional characters to get the required
	 * width
	 */
	if ((param->vars.width > 0U) && (param->vars.width > len)) {
		w = param->vars.width - len;
	}

	/* emit additional characters for width, return early if an error
	 * occurred
	 */
	if ((param->vars.flags & PRINT_FLAG_LEFT_JUSTIFY) == 0U) {
		param->emit(PRINT_CMD_FILL, " ", w, param->data);
	}

	param->emit(PRINT_CMD_COPY, s, len, param->data);

	/* emit additional characters on the right, return early if an error
	 * occurred
	 */
	if ((param->vars.flags & PRINT_FLAG_LEFT_JUSTIFY) != 0U) {
		param->emit(PRINT_CMD_FILL, " ", w, param->data);
	}

}

/**
 * @brief Print data given format string
 *
 * This function processes a format string and emits data based on that format
 *
 * - The function starts by iterating through the format string. For each non-format character, it emit it.
 * Upon encountering a format specifier indicated by %, the function processes it by parsing the flags, width,
 * precision, and length modifiers, and retrieving the corresponding argument from args. Then it calls the
 * appropriate helper function (print_decimal(), print_pow2(), print_string()) to print the argument according
 * to the format. After processing all characters in the format string, the function completes.
 *
 * @param[in] fmt_arg     A pointer to the format string containing format specifiers
 * @param[inout] param    A pointer to a print_param structure, which contains configuration details
 * @param[in] args        A variable argument list that contains the values to be printed
 *
 * @return None
 *
 * @pre fmt_arg != NULL
 * @pre param != NULL
 *
 * @post N/A
 */
void do_print(const char *fmt_arg, struct print_param *param,
		__builtin_va_list args)
{
	const char *fmt = fmt_arg;

	/** temp. storage for the next character */
	char ch;
	/** temp. pointer to the start of an analysed character sequence */
	const char *start;

	/** main loop: analyse until there are no more characters */
	while ((*fmt) != '\0') {
		/** mark the current position and search the next '%' */
		start = fmt;

		while (((*fmt) != '\0') && (*fmt != '%')) {
			fmt++;
		}

		/*
		 * pass all characters until the next '%' to the emit function.
		 * Return early if the function fails
		 */
			param->emit(PRINT_CMD_COPY, start, fmt - start,
				param->data);

		/** continue only if the '%' character was found */
		if (*fmt == '%') {
			/** mark current position in the format string */
			start = fmt;
			fmt++;

			/** initialize the variables for the next argument */
			(void)memset(&(param->vars), 0U, sizeof(param->vars));
			param->vars.mask = 0xFFFFFFFFFFFFFFFFUL;

			/**
			 * analyze the format specification:
			 *   - get the flags
			 *   - get the width
			 *   - get the precision
			 *   - get the length modifier
			 */
			fmt = get_flags(fmt, &(param->vars.flags));
			fmt = get_param(fmt, &(param->vars.width));

			if (*fmt == '.') {
				fmt++;
				fmt = get_param(fmt, &(param->vars.precision));
			}

			fmt = get_length_modifier(fmt, &(param->vars.flags),
						&(param->vars.mask));
			ch = *fmt;
			fmt++;

			/** a single '%'? => print out a single '%' */
			if (ch == '%') {
				param->emit(PRINT_CMD_COPY, &ch, 1U,
						param->data);
			} else if ((ch == 'd') || (ch == 'i')) {
			/** decimal number */
				if ((param->vars.flags &
					PRINT_FLAG_LONG_LONG) != 0U) {
					print_decimal(param,
						__builtin_va_arg(args, int64_t));
				} else {
					print_decimal(param,
						__builtin_va_arg(args, int32_t));
				}
			}
			/** unsigned decimal number */
			else if (ch == 'u') {
				param->vars.flags |= PRINT_FLAG_UINT32;
				if ((param->vars.flags &
					PRINT_FLAG_LONG_LONG) != 0U) {
					print_decimal(param,
						(int64_t)__builtin_va_arg(args,
						uint64_t));
				} else {
					print_decimal(param,
						(int64_t)__builtin_va_arg(args,
						uint32_t));
				}
			}
			/** hexadecimal number */
			else if ((ch == 'X') || (ch == 'x')) {
				if (ch == 'X') {
					param->vars.flags |= PRINT_FLAG_UPPER;
				}
				if ((param->vars.flags &
					PRINT_FLAG_LONG_LONG) != 0U) {
					print_pow2(param,
						__builtin_va_arg(args,
						uint64_t), 4U);
				} else {
					print_pow2(param,
						__builtin_va_arg(args,
						uint32_t), 4U);
				}
			}
			/** pointer */
			else if (ch == 'p') {
				print_pow2(param,
					   (uint64_t)__builtin_va_arg(args, void *),
					   4U);
			}
			/** string argument */
			else if (ch == 's') {
				const char *s = __builtin_va_arg(args, char *);

				if (s == NULL) {
					s = "(null)";
				}
				print_string(param, s);
			}
			/** single character argument */
			else if (ch == 'c') {
				char c[2];

				c[0] = (char)__builtin_va_arg(args, int32_t);
				c[1] = '\0';
				print_string(param, c);
			}
			/** default: print the format specifier as it is */
			else {
				param->emit(PRINT_CMD_COPY, start,
						fmt - start, param->data);
			}
		}
	}

}

/**
 * @brief Copy or fill data to a buffer
 *
 * This function handles two modes of writing data to a buffer, copy mode and fill mode, using a source string
 * and a destination buffer.
 *
 * - If the command is PRINT_CMD_COPY, The function checks if sz is greater than 0. If true, it enters a loop
 * that copies characters from the source string into the destination buffer provided by param. The loop continues
 * until either the source string ends or the number of characters copied reaches the specified size sz.
 * param->wrtn is updated with the addition to copied bytes.
 *
 * - If the command is anything other than PRINT_CMD_COPY, the function writes the first byte of the string s_arg
 * repeatedly into the destination buffer, up to the size specified by sz. param->wrtn is updated with the addition
 * to filled bytes.
 *
 * @param[in] cmd         Value to specify the operation to perform
 * @param[in] s_arg       A pointer to the source string or data used for writing
 * @param[in] sz          The number of characters to write
 * @param[inout] param    A pointer to a print_param structure that holds the destination buffer information
 *
 * @return None
 *
 * @pre s_arg != NULL
 * @pre param != NULL
 *
 * @post N/A
 */
static void
charmem(size_t cmd, const char *s_arg, uint32_t sz, struct snprint_param *param)
{
	const char *s = s_arg;
	/** pointer to the destination */
	char *p = param->dst + param->wrtn;
	/** characters actually written */
	uint32_t n = 0U;

	/** copy mode ? */
	if (cmd == PRINT_CMD_COPY) {
		if (sz > 0U) {
			while (((*s) != '\0') && (n < sz)) {
				if (n < (param->sz - param->wrtn)) {
					*p = *s;
				}
				p++;
				s++;
				n++;
			}
		}

		param->wrtn += n;
	}
	/** fill mode */
	else {
		n = (sz < (param->sz - param->wrtn)) ? sz : 0U;
		param->wrtn += sz;
		(void)memset(p, (uint8_t)*s, n);
	}

}

/**
 * @brief Format a string with a list of arguments
 *
 * This function formats and write a string to a buffer with a list of arguments. It call the function
 * do_print() to do the actual formatting.
 *
 * - If there is enough space in the buffer, the written string is null-terminated. Otherwise, it null-terminates
 * at the last valid position in the buffer and the output is truncated.
 *
 * @param[inout] dst_arg  A pointer to destination buffer
 * @param[in] sz_arg      The buffer size
 * @param[in] fmt         A pointer to format string
 * @param[in] args        A list of arguments
 *
 * @return The number of characters written to the output (excluding the terminating null byte).
 * A return value of \p sz_arg or more indicates that the output is truncated.
 *
 * @pre dst_arg != NULL
 * @pre fmt != NULL
 *
 * @post N/A
 */
size_t vsnprintf(char *dst_arg, size_t sz_arg, const char *fmt, va_list args)
{
	char *dst = dst_arg;
	uint32_t sz = sz_arg;
	size_t res = 0U;


	/** struct to store all necessary parameters */
	struct print_param param;

	/** struct to store snprintf specific parameters */
	struct snprint_param snparam;

	/** initialize parameters */
	(void)memset(&snparam, 0U, sizeof(snparam));
	snparam.dst = dst;
	snparam.sz = sz;
	(void)memset(&param, 0U, sizeof(param));
	param.emit = charmem;
	param.data = &snparam;

	/* execute the printf()*/
	do_print(fmt, &param, args);

	/** ensure the written string is NULL terminated */
	if (snparam.wrtn < sz) {
		snparam.dst[snparam.wrtn] = '\0';
	}
	else {
		snparam.dst[sz - 1] = '\0';
	}

	/** return the number of chars which would be written */
	res = snparam.wrtn;

	/** done */
	return res;
}

/**
 * @brief Format a string with variable arguments
 *
 * This function formats the string into dest, with a specified buffer size (sz), using the format string fmt and
 * the variable arguments provided. It initializes the argument list and call function vsnprintf() to do the formatting
 * and then returns the number of characters that would have been written to the output.
 *
 * @param[inout] dest        A pointer to destination buffer
 * @param[in] sz             The buffer size
 * @param[in] fmt            A pointer to format string
 * @param ... varargs
 *
 * @return A number of characters written to the output
 *
 * @pre dest != NULL
 * @pre fmt != NULL
 *
 * @post N/A
 */
size_t snprintf(char *dest, size_t sz, const char *fmt, ...)
{
	/** variable argument list needed for do_print() */
	va_list args;
	/** the result of this function */
	size_t res;

	va_start(args, fmt);

	/** execute the printf() */
	res = vsnprintf(dest, sz, fmt, args);

	/** destroy parameter list */
	va_end(args);

	/** done */
	return res;
}
/**
 * @}
 */
