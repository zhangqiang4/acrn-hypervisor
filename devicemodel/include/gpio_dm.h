/*
 * Copyright (C) 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#ifndef _GPIO_DM_H_
#define _GPIO_DM_H_

#include <stdint.h>

#define GPIO_MOCK
#define GPIO_PHYSICAL

struct gpio_mock_line;
#ifdef GPIO_MOCK
struct gpio_mock_line *gpio_mock_line_find(const char *name);
int gpio_mock_line_set_value(struct gpio_mock_line *mline, uint8_t value);
#else
static inline struct gpio_mock_line *gpio_mock_line_find(const char *name)
{
	return NULL;
}
static int gpio_mock_line_set_value(struct gpio_mock_line *mline, uint8_t value)
{
	return 0;
}
#endif

#endif
