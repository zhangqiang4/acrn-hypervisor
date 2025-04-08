/*
 * Copyright (C) 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <asm/host_pm.h>
#include <asm/guest/vm.h>
#include <asm/io.h>
#include <logmsg.h>
#include <platform_acpi_info.h>
#include <asm/guest/guest_pm.h>
#include <asm/guest/optee.h>
#include <asm/per_cpu.h>
#include <asm/platform_caps.h>

/* This function is for power management Sx state implementation,
 * VM need to load the Sx state data to implement S3/S5.
 */
static int32_t vm_load_pm_s_state(struct acrn_vm *vm)
{
	int32_t ret;
	struct pm_s_state_data *sx_data = get_host_sstate_data();

	if ((sx_data->pm1a_evt.address == 0UL) || (sx_data->pm1a_cnt.address == 0UL)
			|| (sx_data->wake_vector_32 == NULL)) {
		pr_err("System S3/S5 is NOT supported.");
		ret = -1;
	} else {
		pr_info("System S3/S5 is supported.");
		vm->pm.sx_state_data = sx_data;
		ret = 0;
	}
	return ret;
}

static inline bool is_s3_enabled(uint32_t pm1_cnt)
{
	return ((pm1_cnt & (1U << BIT_SLP_EN)) != 0U);
}

static inline uint8_t get_slp_typx(uint32_t pm1_cnt)
{
	return (uint8_t)((pm1_cnt & 0x1fffU) >> BIT_SLP_TYPx);
}

static bool pm1ab_io_read(struct acrn_vcpu *vcpu, uint16_t addr, size_t width)
{
	struct acrn_pio_request *pio_req = &vcpu->req.reqs.pio_request;

	pio_req->value = pio_read(addr, width);

	return true;
}

static inline void enter_s5(struct acrn_vcpu *vcpu, uint32_t pm1a_cnt_val, uint32_t pm1b_cnt_val)
{
	struct acrn_vm *vm = vcpu->vm;
	uint16_t pcpu_id = pcpuid_from_vcpu(vcpu);

	get_vm_lock(vm);
	/*
	 * Currently, we assume Service VM has full ACPI power management stack.
	 * That means the value from Service VM should be saved and used to shut
	 * down the system.
	 */
	if (is_service_vm(vm)) {
		save_s5_reg_val(pm1a_cnt_val, pm1b_cnt_val);
	}
	pause_vm(vm);
	put_vm_lock(vm);

	bitmap_set_nolock(vm->vm_id, &per_cpu(shutdown_vm_bitmap, pcpu_id));
	make_shutdown_vm_request(pcpu_id);
}

static inline void enter_s3(struct acrn_vm *vm, uint32_t pm1a_cnt_val, uint32_t pm1b_cnt_val)
{
	uint32_t guest_wakeup_vec32;

	get_vm_lock(vm);
	/* Save the wakeup vec set by guest OS. Will return to guest
	 * with this wakeup vec as entry.
	 */
	stac();
	guest_wakeup_vec32 = *(vm->pm.sx_state_data->wake_vector_32);
	clac();

	struct acrn_vm *tee_vm = get_tee_vm();
	if (tee_vm != NULL) {
		freeze_vm(tee_vm);
	}
	pause_vm(vm);	/* pause Service VM before suspend system */
	host_enter_s3(vm->pm.sx_state_data, pm1a_cnt_val, pm1b_cnt_val);
	resume_vm_from_s3(vm, guest_wakeup_vec32);	/* jump back to vm */
	if (tee_vm != NULL) {
		thaw_vm(tee_vm);
	}
	put_vm_lock(vm);
}

/**
 * @pre vcpu != NULL
 * @pre vcpu->vm != NULL
 */
static bool pm1ab_io_write(struct acrn_vcpu *vcpu, uint16_t addr, size_t width, uint32_t v)
{
	static uint32_t pm1a_cnt_ready = 0U;
	uint32_t pm1a_cnt_val;
	bool to_write = true;
	struct acrn_vm *vm = vcpu->vm;

	if (width == 2U) {
		uint8_t val = get_slp_typx(v);

		if ((addr == vm->pm.sx_state_data->pm1a_cnt.address) && is_s3_enabled(v)) {

			if (vm->pm.sx_state_data->pm1b_cnt.address != 0UL) {
				pm1a_cnt_ready = v;
			} else {
				if (vm->pm.sx_state_data->s3_pkg.val_pm1a == val) {
					enter_s3(vm, v, 0U);
				} else if (vm->pm.sx_state_data->s5_pkg.val_pm1a == val) {
					enter_s5(vcpu, v, 0U);
				} else {
					/* other Sx value should be ignored */
				}
			}

			to_write = false;
		} else if ((addr == vm->pm.sx_state_data->pm1b_cnt.address) && is_s3_enabled(v)) {

			if (pm1a_cnt_ready != 0U) {
				pm1a_cnt_val = pm1a_cnt_ready;
				pm1a_cnt_ready = 0U;

				if (vm->pm.sx_state_data->s3_pkg.val_pm1b == val) {
					enter_s3(vm, pm1a_cnt_val, v);
				} else if (vm->pm.sx_state_data->s5_pkg.val_pm1b == val) {
					enter_s5(vcpu, pm1a_cnt_val, v);
				} else {
					/* other Sx value should be ignored */
				}
			} else {
				/* the case broke ACPI spec */
				pr_err("PM1B_CNT write error!");
			}

			to_write = false;
		} else {
			/* No other state currently, do nothing */
		}
	}

	if (to_write) {
		pio_write(v, addr, width);
	}

	return true;
}

static void register_gas_io_handler(struct acrn_vm *vm, uint32_t pio_idx, const struct acrn_acpi_generic_address *gas)
{
	struct vm_io_range gas_io;

	if ((gas->address != 0UL) && (gas->space_id == SPACE_SYSTEM_IO) && (gas->bit_width != 0U)) {
		gas_io.base = (uint16_t)gas->address;
		gas_io.len = gas->bit_width / 8;

		register_pio_emulation_handler(vm, pio_idx, &gas_io, &pm1ab_io_read, &pm1ab_io_write);

		pr_dbg("Enable PM1A trap for VM %d, port 0x%x, size %d\n", vm->vm_id, gas_io.base, gas_io.len);
	}
}

static void register_pm1ab_handler(struct acrn_vm *vm)
{
	struct pm_s_state_data *sx_data = vm->pm.sx_state_data;

	register_gas_io_handler(vm, PM1A_EVT_PIO_IDX, &(sx_data->pm1a_evt));
	register_gas_io_handler(vm, PM1B_EVT_PIO_IDX, &(sx_data->pm1b_evt));
	register_gas_io_handler(vm, PM1A_CNT_PIO_IDX, &(sx_data->pm1a_cnt));
	register_gas_io_handler(vm, PM1B_CNT_PIO_IDX, &(sx_data->pm1b_cnt));
}

static bool rt_vm_pm1a_io_read(__unused struct acrn_vcpu *vcpu,
						 __unused uint16_t addr, __unused size_t width)
{
	return false;
}

/*
 * retval true means that we complete the emulation in HV and no need to re-inject the request to DM.
 * retval false means that we should re-inject the request to DM.
 */
/**
 * @pre vcpu != NULL
 * @pre vcpu->vm != NULL
 */
static bool rt_vm_pm1a_io_write(struct acrn_vcpu *vcpu, uint16_t addr, size_t width, uint32_t v)
{
	if (width != 2U) {
		pr_dbg("Invalid address (0x%x) or width (0x%x)", addr, width);
	} else {
		if (((v & VIRTUAL_PM1A_SLP_EN) != 0U) && (((v & VIRTUAL_PM1A_SLP_TYP) >> 10U) == 5U)) {
			poweroff_if_rt_vm(vcpu->vm);
		}
	}

	return false;
}

static void register_rt_vm_pm1a_ctl_handler(struct acrn_vm *vm)
{
	struct vm_io_range io_range;

	io_range.base = VIRTUAL_PM1A_CNT_ADDR;
	io_range.len = 1U;

	register_pio_emulation_handler(vm, VIRTUAL_PM1A_CNT_PIO_IDX, &io_range,
					&rt_vm_pm1a_io_read, &rt_vm_pm1a_io_write);
}

/*
 * @pre vcpu != NULL
 */
static bool prelaunched_vm_sleep_io_read(struct acrn_vcpu *vcpu, __unused uint16_t addr, __unused size_t width)
{
	vcpu->req.reqs.pio_request.value = 0U;

	return true;
}

/*
 * @pre vcpu != NULL
 * @pre vcpu->vm != NULL
 */
static bool prelaunched_vm_sleep_io_write(struct acrn_vcpu *vcpu, uint16_t addr, size_t width, uint32_t v)
{
	if ((width == 1U) && (addr == VIRTUAL_SLEEP_CTL_ADDR)) {
		bool slp_en;
		uint32_t slp_type;
		struct acrn_vm *vm = vcpu->vm;

		/* ACPI sleep control register:
		 *
		 * Bits 2~4: SLP_TYPx, defines the type of sleeping state the system enters when the
		 * SLP_EN bit is set to one. This 3-bit field defines the type of hardware sleep state
		 * the system enters when the SLP_EN bit is set. The \_S5 object contains 3-bit binary
		 * value (0x5) associated with the S5 sleeping state
		 *
		 * Bit 5: SLP_EN, This is a write-only bit and reads to it always return a zero. Setting
		 * this bit causes the system to sequence into the sleeping state associated with the
		 * SLP_TYPx fields programmed with the values from the \_S5 object
		 */
		slp_type = (v >> 2U) & 0x7U;
		slp_en  = ((v >> 5U) & 0x1U) != 0U;

		if (slp_en && (slp_type == 5U)) {
			get_vm_lock(vm);
			poweroff_if_rt_vm(vm);
			pause_vm(vm);
			put_vm_lock(vm);

			bitmap_set_nolock(vm->vm_id, &per_cpu(shutdown_vm_bitmap, pcpuid_from_vcpu(vcpu)));
			make_shutdown_vm_request(pcpuid_from_vcpu(vcpu));
		}
	}

	return true;
}

static void register_prelaunched_vm_sleep_handler(struct acrn_vm *vm)
{
	struct vm_io_range io_range;

	/* ACPI reduced HW mode is used for pre-launched VM
	 *
	 * The optional ACPI sleep registers (SLEEP_CONTROL_REG and SLEEP_STATUS_REG) specify
	 * a standard mechanism for system sleep state entry on HW-Reduced ACPI systems. When
	 * implemented, the Sleep registers are a replacement for the SLP_TYP, SLP_EN and WAK_STS
	 * registers in the PM1_BLK.
	 */
	io_range.base = VIRTUAL_SLEEP_CTL_ADDR;
	io_range.len = 2U;

	register_pio_emulation_handler(vm, SLEEP_CTL_PIO_IDX, &io_range,
					&prelaunched_vm_sleep_io_read, &prelaunched_vm_sleep_io_write);
}

void init_guest_pm(struct acrn_vm *vm)
{
	struct pm_s_state_data *sx_data = get_host_sstate_data();

	/*
	 * In enter_s5(), it will call save_s5_reg_val() to initialize system_pm1a_cnt_val/system_pm1b_cnt_val when the
	 * vm is Service VM.
	 * If there is no Service VM, save_s5_reg_val() will not be called and these 2 variables will not be initialized properly
	 * so shutdown_system() will fail, explicitly init here to avoid this
	 */
	save_s5_reg_val(((uint32_t)sx_data->s5_pkg.val_pm1a << BIT_SLP_TYPx) | (1U << BIT_SLP_EN),
			((uint32_t)sx_data->s5_pkg.val_pm1b << BIT_SLP_TYPx) | (1U << BIT_SLP_EN));

	if (is_service_vm(vm)) {
		/* Load pm S state data */
		if (vm_load_pm_s_state(vm) == 0) {
			register_pm1ab_handler(vm);
		}
	} else if (is_postlaunched_vm(vm)) {
		if (is_rt_vm(vm)) {
			/* Intercept the virtual pm port for post launched RTVM */
			register_rt_vm_pm1a_ctl_handler(vm);
		}
	} else { /* prelaunched vm */
		/* Intercept the virtual sleep control/status registers for pre-launched VM */
		register_prelaunched_vm_sleep_handler(vm);
	}
}
