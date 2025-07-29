/*
 * Copyright (C) 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <types.h>
#include <errno.h>
#include <asm/guest/vm.h>
#include <sprintf.h>
#include <logmsg.h>
#include <asm/tsc.h>


/* used to control vmexit sample function */
static bool sample_vmexit_enabled = false;

static const char *level_info[MAX_VMEXIT_LEVEL] = {
	"   0us -   2us",
	"   2us -   4us",
	"   4us -   8us",
	"   8us -  16us",
	"  16us -  32us",
	"  32us -  64us",
	"  64us - 128us",
	" 128us - 256us",
	" 256us - 512us",
	" 512us -1024us",
	"1024us -2048us",
	"2048us -4096us",
	"4096us -8192us",
	"8192us -  more",
};

/* if target_vm_id valid, just output this vm or output all VMs */
static bool output_id_per_vm_vcpu(char **str_arg, size_t *size_max, uint16_t target_vm_id)
{
	char *str = *str_arg;
	size_t len, size = *size_max;
	uint16_t vm_id, vcpu_id;
	struct acrn_vm *vm;
	struct acrn_vcpu *vcpu;

	if (target_vm_id != CONFIG_MAX_VM_NUM) {
		vm = get_vm_from_vmid(target_vm_id);
		foreach_vcpu(vcpu_id, vm, vcpu) {
			len = snprintf(str, size, "   VM%u/vCPU%u", target_vm_id, vcpu_id);
			if (len >= size) {
				return false;
			}
			size -= len;
			str += len;
		}
	} else {
		for (vm_id = 0U; vm_id < CONFIG_MAX_VM_NUM; vm_id++) {
			vm = get_vm_from_vmid(vm_id);
			if (is_poweroff_vm(vm))
				continue;

			foreach_vcpu(vcpu_id, vm, vcpu) {
				len = snprintf(str, size, "   VM%u/vCPU%u", vm_id, vcpu_id);
				if (len >= size) {
					return false;
				}
				size -= len;
				str += len;
			}
		}
	}

	*str_arg = str;
	*size_max = size;
	return true;
}


/* if target_vm_id valid, just check this vm or check all VMs; if count is 0, just ignore it */
static bool check_vmexit_count_per_reason(uint16_t target_vm_id, uint16_t reason, uint16_t level)
{
	bool has_vmexit = false;
	uint16_t vm_id, vcpu_id;
	struct acrn_vm *vm;
	struct acrn_vcpu *vcpu;

	if (target_vm_id != CONFIG_MAX_VM_NUM) {
		vm = get_vm_from_vmid(target_vm_id);
		foreach_vcpu(vcpu_id, vm, vcpu) {
			if (vcpu->vmexit_stat.vmexit_cnt[reason][level] != 0) {
				has_vmexit = true;
				break;
			}
		}
	} else {
		for (vm_id = 0U; vm_id < CONFIG_MAX_VM_NUM; vm_id++) {
			vm = get_vm_from_vmid(vm_id);
			if (is_poweroff_vm(vm))
				continue;

			foreach_vcpu(vcpu_id, vm, vcpu) {
				if (vcpu->vmexit_stat.vmexit_cnt[reason][level] != 0) {
					has_vmexit = true;
					break;
				}
			}

			if (has_vmexit)
				break;
		}
	}

	return has_vmexit;
}

static bool output_vmexit_info_per_vcpu(char **str_arg, size_t *size_max, uint16_t target_vm_id,
	uint16_t reason, uint16_t level)
{
	char *str = *str_arg;
	size_t len, size = *size_max;
	uint16_t vm_id, vcpu_id;
	struct acrn_vm *vm;
	struct acrn_vcpu *vcpu;

	if (target_vm_id != CONFIG_MAX_VM_NUM) {
		vm = get_vm_from_vmid(target_vm_id);

		foreach_vcpu(vcpu_id, vm, vcpu) {
			 /* if level is MAX, output the max time */
			if (level == MAX_VMEXIT_LEVEL) {
				len = snprintf(str, size, "%12lld",
					vcpu->vmexit_stat.vmexit_time[reason]);
			} else {
				len = snprintf(str, size, "%12lld",
					vcpu->vmexit_stat.vmexit_cnt[reason][level]);
			}

			if (len >= size) {
				return false;
			}

			size -= len;
			str += len;
		}
	} else {
		for (vm_id = 0U; vm_id < CONFIG_MAX_VM_NUM; vm_id++) {
			vm = get_vm_from_vmid(vm_id);
			if (is_poweroff_vm(vm))
				continue;

			foreach_vcpu(vcpu_id, vm, vcpu) {
				/* if level is MAX, output the max time */
				if (level == MAX_VMEXIT_LEVEL) {
					len = snprintf(str, size, "%12lld",
						vcpu->vmexit_stat.vmexit_time[reason]);
				} else {
					len = snprintf(str, size, "%12lld",
						vcpu->vmexit_stat.vmexit_cnt[reason][level]);
				}

				if (len >= size) {
					return false;
				}

				size -= len;
				str += len;
			}
		}
	}

	*str_arg = str;
	*size_max = size;
	return true;
}

/* to sample vmexit tsc data */
static void sample_vmexit_end(struct acrn_vcpu *vcpu)
{
	uint32_t basic_exit_reason = vcpu->arch.exit_reason & 0xFFFFUL;

	if (vcpu->vmexit_stat.vmexit_begin != 0UL) {
		uint64_t delta = rdtsc() - vcpu->vmexit_stat.vmexit_begin;
		uint32_t us = (uint32_t)ticks_to_us(delta);
		uint16_t fls = (uint16_t)(fls32(us) + 1); /* to avoid us = 0 case, then fls=0xFFFF */
		uint16_t index = 0;

		if (fls >= MAX_VMEXIT_LEVEL) {
			index = MAX_VMEXIT_LEVEL - 1;
		} else if (fls > 0) { /* if fls == 0, it means the us = 0 */
			index = fls - 1;
		}

		vcpu->vmexit_stat.vmexit_cnt[basic_exit_reason][index]++;

		if (us > vcpu->vmexit_stat.vmexit_time[basic_exit_reason]) {
			vcpu->vmexit_stat.vmexit_time[basic_exit_reason] = us;
		}
	}
}

static void sample_vmexit_begin(struct acrn_vcpu *vcpu)
{
	uint32_t basic_exit_reason = vcpu->arch.exit_reason & 0xFFFFUL;

	vcpu->vmexit_stat.vmexit_begin = rdtsc();
	vcpu->vmexit_stat.vmexit_cnt[basic_exit_reason][TOTAL_ARRAY_LEVEL - 1]++;
}

int get_vmexit_details_per_vcpu(char *str_arg, size_t str_max, uint16_t target_vm_id)
{
	char *str = str_arg;
	uint16_t reason, level;
	size_t len, size = str_max;
	int ret = -ENOMEM;

	len = snprintf(str, size, "\r\nNow=%lldus, for detailed latency of each vmexit on %s VM/vCPU:",
		ticks_to_us(rdtsc()), (target_vm_id == CONFIG_MAX_VM_NUM) ? "each" : "one");
	if (len >= size) {
		goto overflow;
	}
	size -= len;
	str += len;

	for (reason = 0U; reason < NR_VMX_EXIT_REASONS; reason++) {

		if (!check_vmexit_count_per_reason(target_vm_id, reason, TOTAL_ARRAY_LEVEL - 1))
			continue;

		len = snprintf(str, size, "\r\n\r\n   VMEXIT/0x%02x", reason);
		if (len >= size) {
			goto overflow;
		}
		size -= len;
		str += len;

		if (!output_id_per_vm_vcpu(&str, &size, target_vm_id)) {
			goto overflow;
		}

		for (level = 0; level < MAX_VMEXIT_LEVEL; level++) {

			if (!check_vmexit_count_per_reason(target_vm_id, reason, level)) {
				continue;
			}

			len = snprintf(str, size, "\r\n%s", level_info[level]);
			if (len >= size) {
				goto overflow;
			}
			size -= len;
			str += len;

			if (!output_vmexit_info_per_vcpu(&str, &size, target_vm_id, reason, level)) {
				goto overflow;
			}
		}

		len = snprintf(str, size, "\r\n  Max Lat(us):");
		if (len >= size) {
			goto overflow;
		}
		size -= len;
		str += len;

		if (!output_vmexit_info_per_vcpu(&str, &size, target_vm_id, reason, MAX_VMEXIT_LEVEL)) {
			goto overflow;
		}
	}

	snprintf(str, size, "\r\n");

	ret = 0;

overflow:
	return ret;
}

void clear_vmexit_info_buffer(void)
{
	uint16_t vm_id, vcpu_id;
	struct acrn_vm *vm;
	struct acrn_vcpu *vcpu;

	for (vm_id = 0U; vm_id < CONFIG_MAX_VM_NUM; vm_id++) {
		vm = get_vm_from_vmid(vm_id);
		if (!is_poweroff_vm(vm)) {
			foreach_vcpu(vcpu_id, vm, vcpu) {
				memset(vcpu->vmexit_stat.vmexit_cnt, 0, sizeof(vcpu->vmexit_stat.vmexit_cnt));
				memset(vcpu->vmexit_stat.vmexit_time, 0, sizeof(vcpu->vmexit_stat.vmexit_time));
			}
		}
	}
}

/* ================== vCPU stat feature ================== */
static bool vcpu_stat_enabled = false;

/* Record vCPU stat sample start time */
static uint64_t vcpu_stat_exec_start_tsc;

static void vcpu_stat_vmentry(struct acrn_vcpu *vcpu)
{
	if (!is_lapic_pt_configured(vcpu->vm)) {
		vcpu->vmexit_stat.vmentry_begin = rdtsc();
	}
}

static void vcpu_stat_vmexit(struct acrn_vcpu *vcpu)
{
	if (!is_lapic_pt_configured(vcpu->vm)) {
		vcpu->vmexit_stat.exec_period +=
			(rdtsc() - vcpu->vmexit_stat.vmentry_begin);
	}
}

void clear_vcpu_stat_info(void)
{
	uint16_t vm_id, vcpu_id;
	struct acrn_vm *vm;
	struct acrn_vcpu *vcpu;

	vcpu_stat_exec_start_tsc = rdtsc();
	for (vm_id = 0U; vm_id < CONFIG_MAX_VM_NUM; vm_id++) {
		vm = get_vm_from_vmid(vm_id);
		if (!is_poweroff_vm(vm) && !is_lapic_pt_configured(vm)) {
			foreach_vcpu(vcpu_id, vm, vcpu) {
				vcpu->vmexit_stat.exec_period = 0UL;
				vcpu->vmexit_stat.idle_period = 0UL;
			}
		}
	}
}

static void vcpu_stat_format_rate_to_string(uint64_t sample_period_tsc,
	uint64_t sub_time_tsc, char *str_buf, size_t str_max)
{
	const uint64_t scale = 100;  /* Scale factor for 2 decimal places */
	uint64_t percentage_scaled;
	uint64_t remainder;


	if (sub_time_tsc == 0) {
		snprintf(str_buf, str_max, "0.00%%");
	} else {
		/*
		 * Calculate percentage with rounding:
		 * 1. Multiply numerator by 100*scale*10 for extra precision
		 * 2. Division gives us remainder for rounding decision
		 * 3. Round up if remainder >= 5
		 */
		remainder = (sub_time_tsc * 100 * scale * 10) / sample_period_tsc;
		percentage_scaled = remainder / 10;
		if (remainder % 10 >= 5) {
			percentage_scaled++;  /* Round up */
		}

		/* Format the result with 2 decimal places */
		snprintf(str_buf, str_max, "%llu.%02llu%%",
			percentage_scaled / scale, /* Integer part */
			percentage_scaled % scale  /* Fractional part */);
	}
}

int get_vcpu_stat_info(char *str_arg, size_t str_max)
{
	char *str = str_arg;
	char vcpu_state_str[32];
	char vcpu_idle_str[32];
	struct acrn_vm *vm;
	struct acrn_vcpu *vcpu;
	uint16_t i, idx;
	size_t len, size = str_max;
	uint64_t sample_period;
	int ret = -ENOMEM;

	len = snprintf(str, size, "\r\nVM ID    PCPU ID    VCPU ID    CPU Usage    vCPU Idle"
			"\r\n=====    =======    =======    =========    =========  \r\n");
	if (len >= size) {
		goto overflow;
	}

	size -= len;
	str += len;

	sample_period = rdtsc() - vcpu_stat_exec_start_tsc;
	if (sample_period > 0UL) {
		for (idx = 0U; idx < CONFIG_MAX_VM_NUM; idx++) {
			vm = get_vm_from_vmid(idx);
			if (is_poweroff_vm(vm) || is_lapic_pt_configured(vm)) {
				continue;
			}

			foreach_vcpu(i, vm, vcpu) {
				/* get vcpu usage rate string */
				vcpu_stat_format_rate_to_string(sample_period,
					vcpu->vmexit_stat.exec_period, vcpu_state_str, 32U);

				/*
				 *get vcpu idle rate string:
				 *vcpu ilde period / (vcpu period time + vcpu idle period)
				 */
				vcpu_stat_format_rate_to_string((vcpu->vmexit_stat.exec_period
					+ vcpu->vmexit_stat.idle_period),
					vcpu->vmexit_stat.idle_period, vcpu_idle_str, 32U);

				len = snprintf(str, size, "  %-9d %-10d %-7hu %-12s %-12s\r\n",
						vm->vm_id,
						pcpuid_from_vcpu(vcpu),
						vcpu->vcpu_id,
						vcpu_state_str,
						vcpu_idle_str);

				if (len >= size) {
					goto overflow;
				}

				size -= len;
				str += len;
			}
		}
	}
	ret = 0;

overflow:
	return ret;
}

/* opt: 0 -> enter idle,  1 -> exit idle */
void sample_vcpu_idle(struct acrn_vcpu *vcpu, uint64_t *data,  uint8_t opt)
{
	if (vcpu_stat_enabled && (data != NULL)) {
		if (opt == 0U) {
			*data = rdtsc();
		} else if (opt == 1U) {
			vcpu->vmexit_stat.idle_period += (rdtsc() - *data);
		}
	}
}

void set_vcpu_stat_profiling(bool enable)
{
	vcpu_stat_enabled = enable;
}

bool is_vcpu_stat_profiling_enabled(void)
{
	return vcpu_stat_enabled;
}

void set_vmexit_profiling(bool enable)
{
	sample_vmexit_enabled = enable;
}

bool is_vmexit_profiling_enabled(void)
{
	return sample_vmexit_enabled;
}

void sample_vmexit(struct acrn_vcpu *vcpu)
{
	if (sample_vmexit_enabled)
		sample_vmexit_begin(vcpu);

	if (vcpu_stat_enabled)
		vcpu_stat_vmexit(vcpu);
}

void sample_vmentry(struct acrn_vcpu *vcpu)
{
	if (sample_vmexit_enabled)
		sample_vmexit_end(vcpu);

	if (vcpu_stat_enabled)
		vcpu_stat_vmentry(vcpu);
}
