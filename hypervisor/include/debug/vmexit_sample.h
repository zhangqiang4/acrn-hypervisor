/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef VMEXIT_SAMPLE_H
#define VMEXIT_SAMPLE_H


/**
 * @addtogroup debug
 *
 * @{
 */

/**
 * @file
 * @brief Profiling Handler Interface
 *
 * This file declares functions for handling vmexit profiling.  These functions
 * provide an interface for profiling during virtual machine entry and exit.
 * They are used to facilitate performance monitoring and analysis within the system.
 */

//struct acrn_vcpu;

#define MAX_VMEXIT_LEVEL 14 /**< Maximum VM-Exit profiling level, from 0 to 14 for counts */
#define TOTAL_ARRAY_LEVEL (MAX_VMEXIT_LEVEL + 1) /**< Profiling Array size */
struct vmexit_sample {
	uint64_t vmentry_begin; /**< VM-Entry profiling begin time-stamp counter */
	uint64_t vmexit_begin;  /**< VM-Exit profiling begin time-stamp counter */
	uint64_t exec_period;   /**< Total vCPU execution time in time-stamp counter */
	uint64_t idle_period;   /**< Total time period vCPU in idle in time-stamp counter */
	uint64_t vmexit_cnt[NR_VMX_EXIT_REASONS][TOTAL_ARRAY_LEVEL]; /**< TSC counts for different VM-Exit reasons */
	uint64_t vmexit_time[NR_VMX_EXIT_REASONS]; /**< VM-Exit latency for different VM-Exit reasons */
};

/* for vmexit sample */
void clear_vmexit_info_buffer(void);
void set_vmexit_profiling(bool enable);
bool is_vmexit_profiling_enabled(void);
void sample_vmentry(struct acrn_vcpu *vcpu);
void sample_vmexit(struct acrn_vcpu *vcpu);
int get_vmexit_details_per_vcpu(char *str_arg, size_t str_max, uint16_t target_vm_id);

void clear_vcpu_stat_info(void);
int get_vcpu_stat_info(char *str_arg, size_t str_max);
void set_vcpu_stat_profiling(bool enable);
bool is_vcpu_stat_profiling_enabled(void);
void sample_vcpu_idle(struct acrn_vcpu *vcpu, uint64_t *data,  uint8_t opt);
/**
 * @}
 */
#endif /* VMEXIT_SAMPLE_H */
