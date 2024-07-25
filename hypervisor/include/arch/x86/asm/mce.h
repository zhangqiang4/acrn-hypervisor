/*
 * Copyright (C) 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef MCE_H_
#define MCE_H_

void handle_mce(void);
void inject_mc_event_to_governing_vcpu(uint16_t pcpu_id, bool is_cmci);
void init_machine_check_events(void);

#endif /* MCE_H_ */
