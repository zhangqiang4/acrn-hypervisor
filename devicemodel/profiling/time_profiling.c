/*
 * Copyright (C) 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#include <sys/param.h>
#include <sys/stat.h>
#include <errno.h>
#include <paths.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <sys/time.h>
#include <stdint.h>
#include <cpuid.h>
#include <pthread.h>

#include "log.h"
#include "time_profiling.h"

#if PROFILING_TIME_EN
#define TIME_RECORD_NODE_MAX 32
#define TIME_NAME_MAX_LEN 32
struct time_node {
	uint64_t tsc;
	char name[TIME_NAME_MAX_LEN + 1];
};

struct time_seq {
	uint32_t node_num;
	struct time_node *time_nodes;
	pthread_mutex_t lock;
};
extern char *vmname;
static struct time_seq time_seqs[TIME_SEQ_MAX] = {
	{ /* BOOT_TIME */
		.lock = PTHREAD_MUTEX_INITIALIZER
	},
	{ /* RESUME_TIME */
		.lock = PTHREAD_MUTEX_INITIALIZER
	},
	{ /* MISC_TIME_us */
		.lock = PTHREAD_MUTEX_INITIALIZER
	},
};

static bool init_time_seq(struct time_seq *seq)
{
	struct time_node *ptr;

	ptr = calloc(1, TIME_RECORD_NODE_MAX * sizeof(struct time_node));
	if (ptr == NULL)
		return false;

	seq->time_nodes = ptr;
	seq->node_num = 0;
	return true;
}

static void deinit_time_seq(struct time_seq *seq)
{
	pthread_mutex_lock(&seq->lock);
	if (seq->time_nodes != NULL)
		free(seq->time_nodes);

	seq->time_nodes = NULL;
	seq->node_num = 0;
	pthread_mutex_unlock(&seq->lock);
}

static inline void get_timestamp(struct time_node *node)
{
	node->tsc = rdtsc();
}

static const char *get_seq_type_str(enum time_seq_type seq_type)
{
	switch (seq_type) {
	case BOOT_TIME:
		return "Coldboot";

	case RESUME_TIME:
		return "S3 Resume";

	default:
		break;
	}
	return "MISC-Time-Seq";
}

static void report_time_seq(enum time_seq_type seq_type)
{
	bool time_us;
	uint32_t idx, tsc_freq;
	uint64_t start_tsc, cur_tsc, next_tsc;
	struct time_seq *seq = &time_seqs[seq_type];
	const char *report_name = get_seq_type_str(seq_type);

	tsc_freq = get_tsc_freq();
	time_us = (seq_type == MISC_TIME_us);
	if (time_us)
		tsc_freq /= 1000;

	pr_notice("============================================\n");
	pr_notice("VM: %s %s                Period(%s)\n",
		vmname,	report_name, (time_us ? "us" : "ms"));

	start_tsc = seq->time_nodes[0].tsc;
	cur_tsc = start_tsc;
	for (idx = 0; idx < seq->node_num - 1; idx++) {
		next_tsc = seq->time_nodes[idx + 1].tsc;

		pr_notice("%-32s    %ld\n", seq->time_nodes[idx].name,
			(next_tsc - cur_tsc)/tsc_freq);

		if (idx == seq->node_num - 2) {
			pr_notice("VM: %s ACRN DM %s time:   %ld (%s)\n",
				vmname, report_name, ((next_tsc - start_tsc)/tsc_freq),
				(time_us ? "us" : "ms"));

			pr_notice("---------------------\n");
			pr_notice("TSC Freq:           %-16d(MHz)\n",
				(time_us ? tsc_freq : (tsc_freq / 1000)));
			pr_notice("Time Seq Start TSC: %-16ld\n", start_tsc);
			pr_notice("Time Seq End TSC:   %-16ld\n", next_tsc);
			pr_notice("============================================\n");
			break;
		}
		cur_tsc = next_tsc;
	}
}

/**
 * @brief Add a time point to be recorded.
 *
 * This function is an API to record a time point in the execution
 * of ACRN DM for guest coldboot, S3 resume or other time profiling
 * for debug purpose.
 *
 * @param[in] tag        Name for time point to be recorded.
 * @param[in] seq_type   BOOT_TIME and RESUME_TIME for cold boot or S3 resume case respectively.
 *                       MISC_TIME_us for other time profiling purpose with
 *                       time unit of millisecond and microsecond respectively.
 * @param[in] node_type  Time point type:
 *                       RECORD_NODE time points to be recorded before RECORD_END.
 *                       RECORD_END the last time point and report the time profiling.
 *
 * @return None
 *
 * @note   1) For given 'seq_type', call this function w/ RECORD_END as the last time point to
 *            stop the recording, else it induces risk of memory leakage.
 *         2) Recording stops either a) w/ RECORD_END  or b) record buffer is full(TIME_RECORD_NODE_MAX).
 */
void time_profiling_add(const char *tag, enum time_seq_type seq_type,
		enum time_node_type node_type)
{
	struct time_node *node;
	struct time_seq *seq;

	if (seq_type >= TIME_SEQ_MAX)
		return;

	seq = &time_seqs[seq_type];

	pthread_mutex_lock(&seq->lock);
	if (seq->time_nodes == NULL) {
		if (!init_time_seq(seq)) {
			pthread_mutex_unlock(&seq->lock);
			return;
		}
	}

	node = &seq->time_nodes[seq->node_num];
	get_timestamp(node);
	if (tag != NULL) {
		strncpy(node->name, tag, TIME_NAME_MAX_LEN);
		node->name[TIME_NAME_MAX_LEN] = '\0';
	}
	seq->node_num++;
	pthread_mutex_unlock(&seq->lock);

	if ((node_type == RECORD_END) || (seq->node_num == TIME_RECORD_NODE_MAX)) {
		if (seq->node_num >= 2)
			report_time_seq(seq_type);

		deinit_time_seq(seq);

		if ((seq_type == BOOT_TIME) || (seq_type == RESUME_TIME))
			pr_notice("ACRN DM: VM(%s) start to %s ...\n",
				vmname, get_seq_type_str(seq_type));
		return;
	}
}

/*
 * Get the TSC frequency (in KHz).
 */
uint32_t get_tsc_freq(void)
{
	uint32_t eax, ebx, ecx, edx;
	uint64_t tsc_freq = 0;

	if (__get_cpuid(0x15, &eax, &ebx, &ecx, &edx)) {
		if ((ebx != 0) && (ecx != 0)) {
			tsc_freq = (ecx * (ebx / eax))/1000; /* Refer to CPUID.15H */
		}
	}

	if ((tsc_freq == 0) && (__get_cpuid_max(0, NULL) >= 0x16)) {
		/* Get the CPU base frequency (in MHz) */
		if (__get_cpuid(0x16, &eax, &ebx, &ecx, &edx)) {
			tsc_freq = eax * 1000;
			pr_err("%s(), TSC frequency is enumerated via CPUID.16H, it is NOT accurate!\n", __func__);
		}
	}

	if (tsc_freq == 0) {
		pr_err("%s(), TSC frequency detection failed, Dummy value is used!\n", __func__);
		tsc_freq = 2800000; /* 2800 MHz*/
	}

	return (uint32_t)tsc_freq;
}

#else /* Dummy functions in case PROFILING_TIME_EN is 0 */
uint32_t get_tsc_freq(void)
{
	return 0;
}
void time_profiling_add(const char *tag, enum time_seq_type seq_type,
		enum time_node_type node_type)
{
}
#endif /* PROFILING_TIME_EN */
