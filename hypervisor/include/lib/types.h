/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef TYPES_H
#define TYPES_H

/**
 * @addtogroup lib_util
 *
 * @{
 */

/**
 * @file
 * @brief Alias creation.
 *
 * This file creates alias for some common data type and defines
 * some unique numbers.
 */

/**
 * @brief Get the number of elements in \a arr as is statically defined in the declaration or definition of the array.
 *
 * Expand to a value calculated by dividing the size of the whole \a arr by size of the single element embedded in
 * \a arr.
 *
 * @param[in] arr    An array with statically defined capacity.
 *
 * @return The number of elements in \a arr as is statically defined in the declaration or definition of the array.
 *
 * @pre arr != NULL && arr[0] != '\0'
 *
 * @post N/A
 */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/**
 * @brief Restrict the compiler to place instances of a struct/union type or a global variable at \a x-byte aligned
 * host virtual addresses.
 *
 * @param[in] x     An integer constant indicating the number of bytes the struct, union or global variable shall be
 * aligned to.
 *
 * @pre (x & (x - 1)) == 0
 * @pre x <= 4096
 *
 * @post N/A
 *
 * @remark \a x shall be an integer constant.
 */
#define __aligned(x)		__attribute__((aligned(x)))
/**
 * @brief A compiler attribute that specify the members of a structure or union shall be placed with no padding in.
 * between.
 */
#define __packed	__attribute__((packed))
/**
 * @brief A compiler attribute that is attached to formal parameters that are not used within the function.
 */
#define	__unused	__attribute__((unused))

#ifndef ASSEMBLER

/**
 * @brief Represent an signed 8 bit integer type
 */
typedef signed char int8_t;
/**
 * @brief Represent an unsigned 8 bit integer type
 */
typedef unsigned char uint8_t;
/**
 * @brief Represent an signed 16 bit integer type
 */
typedef signed short int16_t;
/**
 * @brief Represent an unsigned 16 bit integer type
 */
typedef unsigned short uint16_t;
/**
 * @brief Represent a signed 32 bit integer type
 */
typedef signed int int32_t;
/**
 * @brief Represent an unsigned 32 bit integer type
 */
typedef unsigned int uint32_t;
/**
 * @brief Represent an unsigned 64 bit integer type
 */
typedef unsigned long uint64_t;
/**
 * @brief Represent an signed 64 bit integer type
 */
typedef signed long int64_t;
/**
 * @brief Represent an unsigned 32 bit integer type
 *
 * It is used as the type of integers representing lengths.
 */
typedef unsigned int size_t;
/**
 * @brief Wrap for compiler specific type __builtin_va_list
 */
typedef __builtin_va_list va_list;

/**
 * @brief Represent an Boolean type
 */
typedef _Bool bool;

#ifndef NULL
/**
 * @brief A wild pointer.
 *
 * Dereferenced the wild pointer will cause page fault exception.
 */
#define         NULL                                ((void *) 0)
#endif

#ifndef true
/**
 * @brief The Boolean value of true
 */
#define true		((_Bool) 1)
/**
 * @brief The Boolean value of false
 */
#define false		((_Bool) 0)
#endif

#ifndef UINT64_MAX
/**
 * @brief The maximum unsigned 64 bit integer value.
 */
#define UINT64_MAX	(0xffffffffffffffffUL)
#endif

#ifndef UINT32_MAX
/**
 * @brief The maximum unsigned 32 bit integer value.
 */
#define UINT32_MAX	(0xffffffffU)
#endif

#endif /* ASSEMBLER */

/**
 * @}
 */

#endif /* INCLUDE_TYPES_H defined */
