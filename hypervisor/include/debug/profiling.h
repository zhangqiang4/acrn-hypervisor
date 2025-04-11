/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef PROFILING_H
#define PROFILING_H

#ifdef PROFILING_ON
#include <debug/profiling_internal.h>
#endif

/**
 * @addtogroup debug
 *
 * @{
 */

/**
 * @file
 * @brief Profiling Handler Interface
 *
 * This file declares functions for handling profiling events in a virtualized
 * environment. These functions provide an interface for managing profiling
 * during virtual machine entry and exit, as well as setting up profiling
 * resources. They are used to facilitate performance monitoring and analysis
 * within the system.
 */

struct acrn_vcpu;

void profiling_vmenter_handler(struct acrn_vcpu *vcpu);
void profiling_pre_vmexit_handler(struct acrn_vcpu *vcpu);
void profiling_post_vmexit_handler(struct acrn_vcpu *vcpu);
void profiling_setup(void);

/* for vmexit sample */
void sample_vmexit_end(struct acrn_vcpu *vcpu);
void sample_vmexit_begin(struct acrn_vcpu *vcpu);

/**
 * @}
 */
#endif /* PROFILING_H */
