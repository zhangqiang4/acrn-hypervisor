/*
 * Copyright (C) 2018-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __TIME_PROFILING_H__
#define __TIME_PROFILING_H__

#define PROFILING_TIME_EN 1
enum time_seq_type {
	BOOT_TIME = 0,
	RESUME_TIME,
	MISC_TIME_us, /* Time unit of Microsecond */
	TIME_SEQ_MAX
};

enum time_node_type {
	RECORD_NODE = 0,
	RECORD_END
};

/**
 * @brief Read Time Stamp Counter (TSC).
 *
 * @return TSC value
 */
static inline uint64_t rdtsc(void)
{
	uint32_t lo, hi;

	asm volatile("rdtsc" : "=a" (lo), "=d" (hi));
	return ((uint64_t)hi << 32U) | lo;
}

uint32_t get_tsc_freq(void);

void time_profiling_add(const char *tag, enum time_seq_type seq_type,
			enum time_node_type node_type);
#endif /* __TIME_PROFILING_H__ */
