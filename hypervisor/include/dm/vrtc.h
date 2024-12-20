/*
 * Copyright (C) 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef VRTC_H
#define VRTC_H

/**
 * @defgroup vp-dm_vperipheral vp-dm.vperipheral
 * @ingroup vp-dm
 * @brief Implementation of virtual peripheral devices in hypervisor.
 *
 * This module implements the virtualization of all peripheral devices in hypervisor. The virtual device initial
 * function is usually called by the VM initialization function and registers their port IO and memory IO access
 * functions. So when a guest VM accesses its peripheral device by port IO or memory IO, it would cause VM exit and then
 * call their registered functions.
 * @{
 */

/**
 * @file
 * @brief Definitions for the virtual RTC device.
 *
 * This file defines types and data structure for the virtual RTC device.
 */

/**
 * @brief Represent an 32 bit integer type for time.
 */
typedef int32_t time_t;

/**
 * @brief Register layout of the RTC
 *
 * Data structure to present RTC register layout, includes time, status, control register.
 *
 * @remark N/A
 */
struct rtcdev {
	uint8_t	sec; /**< Seconds. */
	uint8_t	alarm_sec; /**< Seconds alarm. */
	uint8_t	min; /**< Minutes. */
	uint8_t	alarm_min; /**< Minutes alarm. */
	uint8_t	hour; /**< Hour. */
	uint8_t	alarm_hour; /**< Hour alarm. */
	uint8_t	day_of_week; /**< Day of week. */
	uint8_t	day_of_month; /**< Day of month. */
	uint8_t	month; /**< Month. */
	uint8_t	year; /**< Year. */
	uint8_t	reg_a; /**< Status register A, . */
	uint8_t	reg_b; /**< Status register B, . */
	uint8_t	reg_c; /**< Status register C, . */
	uint8_t	reg_d; /**< Status register D, . */
	uint8_t	res[36]; /**< Reserve. */
	uint8_t	century; /**< Century. */
};

/**
 * @brief Data structure to illustrate a virtual RTC device.
 *
 * This structure contains the information of a virtual RTC device.
 *
 * @consistency self.vm->vrtc == self
 * @alignment N/A
 *
 * @remark N/A
 */
struct acrn_vrtc {
	struct acrn_vm	*vm; /**< Pointer to the VM that owns the virtual RTC device. */
	/**
	 * @brief The RTC register to read or write.
	 *
	 * To access RTC registers, the guest writes the register index to the RTC address port and then reads/writes
	 * the register value from/to the RTC data port. This field is used to store the register index.
	 */
	uint32_t	addr;
	time_t		base_rtctime; /**< Base time calculated from physical RTC register. */
	time_t		offset_rtctime; /**< RTC offset against base time. */
	time_t		last_rtctime; /**< Last RTC time, to keep monotonicity. */
	uint64_t	base_tsc; /**< Base TSC value. */
	struct rtcdev	rtcdev; /**< Register layout of RTC. */
};

void suspend_vrtc(void);
void resume_vrtc(void);
void vrtc_init(struct acrn_vm *vm);

#endif /* VRTC_H */

/**
 * @}
 */
