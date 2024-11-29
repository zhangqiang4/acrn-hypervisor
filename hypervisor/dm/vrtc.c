/*
 * Copyright (c) 2014, Neel Natu (neel@freebsd.org)
 * Copyright (c) 2024 Intel Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <asm/guest/vm.h>
#include <asm/io.h>
#include <asm/tsc.h>
#include <vrtc.h>
#include <logmsg.h>
#include <vm_event.h>

#include "mc146818rtc.h"

/**
 * @addtogroup vp-dm_vperipheral
 *
 * @{
 */

/**
 * @file
 * @brief Implementation of virtual RTC device.
 *
 * This file provides the implementation of the virtual RTC device. The virtual RTC device is used to provide the RTC
 * service to the guest VMs. It is a part of the virtual peripheral devices.
 */

/**
 * @brief Debugging macros for vrtc.
 *
 * If macro DEBUG_RTC is not defined, RTC_DEBUG is printing nothing, if macro DEBUG_RTC is defined, RTC_DEBUG is using
 * pr_info to print log.
 *
 */
#ifdef DEBUG_RTC
# define RTC_DEBUG  pr_info
#else
# define RTC_DEBUG(format, ...)      do { } while (false)
#endif

static time_t vrtc_get_physical_rtc_time(struct acrn_vrtc *vrtc);
static void vrtc_update_basetime(time_t physical_time, time_t offset);

/**
 * @brief Data structure to represent clock time.
 *
 * This structure holds the components of date and time, including the year, month, day, hour, minute, second, and day
 * of the week.
 */
struct clktime {
	uint32_t	year;	/**< Year (4 digit year) */
	uint32_t	mon;	/**< Month (1 - 12) */
	uint32_t	day;	/**< Day (1 - 31) */
	uint32_t	hour;	/**< Hour (0 - 23) */
	uint32_t	min;	/**< Minute (0 - 59) */
	uint32_t	sec;	/**< Second (0 - 59) */
	uint32_t	dow;	/**< Day of week (0 - 6; 0 = Sunday) */
};

/**
 * @brief Local spinlock_t variable used to avoid vrtc access race.
 *
 * Spinlock to avoid race for accessing vrtc base_rtctime, offset_rtctime, last_rtctime, base_tsc.
 */
static spinlock_t vrtc_rebase_lock = { .head = 0U, .tail = 0U };

#define POSIX_BASE_YEAR	1970 /**< Base year is from 1970 UTC. */
#define SECDAY		(24 * 60 * 60) /**< Total seconds in one day. */
#define SECYR		(SECDAY * 365) /**< Total seconds in one year. */
#define VRTC_BROKEN_TIME	((time_t)-1) /**< Broken time for time initialization. */

#define FEBRUARY	2U /**< February is 2nd month in a year. */

/**
 * @brief Number of days in each month for a non-leap year.
 *
 * This array holds the number of days in each month, starting from January (index 0) to December (index 11). The
 * values are based on a non-leap year, where February has 28 days.
 */
static const uint32_t month_days[12] = {
	31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U
};

/**
 * @brief Compute leap year.
 *
 * This function is to compute leap year. If a year is divisible by 4, it is a leap year, however, if the year is also
 * divisible by 100, it is not a leap year unless year is also divisible by 400.
 *
 * @param[in] year Year to be computed.
 *
 * @return A uint32_t value to indicate whether year is leapyear.
 *
 * @retval 1 Year is leapyear
 * @retval 0 Year is not leapyear
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark It is an internal function called within vRTC.
 */
static inline uint32_t leapyear(uint32_t year)
{
	uint32_t rv = 0U;

	if ((year & 3U) == 0) {
		rv = 1U;
		if ((year % 100U) == 0) {
			rv = 0U;
			if ((year % 400U) == 0) {
				rv = 1U;
			}
		}
	}
	return rv;
}

/**
 * @brief Calculate the number of days in a given year.
 *
 * If the year is leapyear, days in year is 366U, if the year is not leapyear, days in year is 365U.
 *
 * @param[in] year Year to be calculated.
 *
 * @return The number of days in the specified year.
 *
 * @retval 366U Year is leapyear.
 * @retval 365U Year is not leapyear.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark It is an internal function called within vRTC.
 */
static inline uint32_t days_in_year(uint32_t year)
{
        return leapyear(year) ? 366U : 365U;
}

/**
 * @brief Calculate total days in a month.
 *
 * Calculate total days for the month through array month_days, it stores days in a month, FEBRUARY is special, total
 * days of FEBRUARY is 29 when the year is leapyear, total days of FEBRUARY is 28 when the year is not leapyear.
 *
 * @param[in] year Year to be calculated.
 * @param[in] month Month to be calculated.
 *
 * @return Days of month.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark It is an internal function called within vRTC.
 */
static inline uint32_t days_in_month(uint32_t year, uint32_t month)
{
        return month_days[(month) - 1U] + ((month == FEBRUARY) ? leapyear(year) : 0U);
}

/**
 * @brief Calculate day of week.
 *
 * This function calculates the day of the week given a number of days since a reference point. The reference point is
 * January 1, 1970 (which was a Thursday). This function uses the formula`((days) + 4) % 7` to determine the day of the
 * week.
 *
 * @param[in] days The days counted from 1/1/1970.
 *
 * @return Day of week.
 *
 * @pre N/A
 *
 * @post retval < 7 && retval >= 0
 *
 * @remark It is an internal function called within vRTC.
 */
static inline uint32_t day_of_week(uint32_t days)
{
        return ((days) + 4U) % 7U;
}

/**
 * @brief Lookup table for binary format value to BCD (Binary-Coded Decimal)  format value.
 *
 * This array provides a lookup table for converting binary values (0-99) to their corresponding BCD format value.
 */
uint8_t const bin2bcd_data[] = {
	0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U, 0x09U,
	0x10U, 0x11U, 0x12U, 0x13U, 0x14U, 0x15U, 0x16U, 0x17U, 0x18U, 0x19U,
	0x20U, 0x21U, 0x22U, 0x23U, 0x24U, 0x25U, 0x26U, 0x27U, 0x28U, 0x29U,
	0x30U, 0x31U, 0x32U, 0x33U, 0x34U, 0x35U, 0x36U, 0x37U, 0x38U, 0x39U,
	0x40U, 0x41U, 0x42U, 0x43U, 0x44U, 0x45U, 0x46U, 0x47U, 0x48U, 0x49U,
	0x50U, 0x51U, 0x52U, 0x53U, 0x54U, 0x55U, 0x56U, 0x57U, 0x58U, 0x59U,
	0x60U, 0x61U, 0x62U, 0x63U, 0x64U, 0x65U, 0x66U, 0x67U, 0x68U, 0x69U,
	0x70U, 0x71U, 0x72U, 0x73U, 0x74U, 0x75U, 0x76U, 0x77U, 0x78U, 0x79U,
	0x80U, 0x81U, 0x82U, 0x83U, 0x84U, 0x85U, 0x86U, 0x87U, 0x88U, 0x89U,
	0x90U, 0x91U, 0x92U, 0x93U, 0x94U, 0x95U, 0x96U, 0x97U, 0x98U, 0x99U
};

/**
 * @brief  Convert binary format value to the vrtc device required format.
 *
 * This function converts binary format value to the vrtc device required format according to BCD mode of vrtc device,
 * it checks the status register B of the vrtc device to get BCD mode, if BCD data mode is enabled, translate binary
 * format to BCD format, if BCD data mode is not enabled, directly return binary format value.
 *
 * @param[in] rtc The pointer to the `rtcdev` struct that representing RTC device.
 * @param[in] val The value to be set.
 *
 * @return value of rtc required format.
 *
 * @pre rtc != NULL
 * @pre val < 100
 *
 * @post N/A
 *
 * @remark It is an internal function called within vRTC.
 */
static inline uint8_t rtcset(struct rtcdev *rtc, uint32_t val)
{
	return ((rtc->reg_b & RTCSB_BCD) ? val : bin2bcd_data[val]);
}

/**
 * @brief Convert value in the vrtc device required format to binary format.
 *
 * This function converts value in the vrtc device required format to binary format according to BCD mode of vrtc
 * device, it checks the status register B of the vrtc device to get BCD mode, if BCD mode is enabled, translate value
 * from BCD format to binary format, if BCD mode is not enabled, directly use value from the vrtc device.
 *
 * @param[in] rtc The pointer to the `rtcdev` struct representing RTC device.
 * @param[in] val The value to be get.
 * @param[out] retval The pointer to a uint32_t value where the retrieved value will be stored.
 *
 * @return A 32bit value to indicate whether it is successful to retrieve.
 *
 * @retval 0 On success
 * @retval -EINVAL Parameter val is invalid
 *
 * @pre rtc != NULL
 * @pre retval != NULL
 *
 * @post N/A
 *
 * @remark It is an internal function called within vRTC.
 */
static int32_t rtcget(const struct rtcdev *rtc, uint8_t val, uint32_t *retval)
{
	uint8_t upper, lower;
	int32_t errno = 0;

	if (rtc->reg_b & RTCSB_BCD) {
		*retval = val;
	} else {
		lower = val & 0xfU;
		upper = (val >> 4) & 0xfU;

		if ((lower > 9U) || (upper > 9U)) {
			errno = -EINVAL;
		} else {
			*retval = upper * 10U + lower;
		}
	}
	return errno;
}

/**
 * @brief Convert a clktime struct value to seconds.
 *
 * This function converts a given clktime structure to seconds, it performs sanity checks on the input values and
 * calculates the total number of seconds since the POSIX base year (1970).
 *
 * @param[in] ct The pointer to the `clktime` structure representing the date and time to be converted.
 * @param[out] sec The pointer to the `time_t` structure where the resulting seconds will be stored.
 *
 * @return A 32bit value to indicate whether it is successful.
 *
 * @retval 0 On success
 * @retval -EINVAL Input parameter check failure
 *
 * @pre ct != NULL
 * @pre sec != NULL
 *
 * @post N/A
 *
 * @remark It is an internal function called within vRTC.
 */
static int32_t clk_ct_to_ts(struct clktime *ct, time_t *sec)
{
	uint32_t i, year, days;
	int32_t err = 0;

	year = ct->year;

	/* Sanity checks. */
	if ((ct->mon < 1U) || (ct->mon > 12U) || (ct->day < 1U) ||
			(ct->day > days_in_month(year, ct->mon)) ||
			(ct->hour > 23U) ||  (ct->min > 59U) || (ct->sec > 59U) ||
			(year < POSIX_BASE_YEAR) || (year > 2037U)) {
		/* time_t overflow */
		err = -EINVAL;
	} else {
		/*
		 * Compute days since start of time
		 * First from years, then from months.
		 */
		days = 0U;
		for (i = POSIX_BASE_YEAR; i < year; i++) {
			days += days_in_year(i);
		}

		/* Months */
		for (i = 1; i < ct->mon; i++) {
			days += days_in_month(year, i);
		}
		days += (ct->day - 1);

		*sec = (((time_t)days * 24 + ct->hour) * 60 + ct->min) * 60 + ct->sec;
	}
	return err;
}

/**
 * @brief Convert seconds to a clktime struct value.
 *
 * This function converts a given timestamp (seconds since the base year) to a clktime structure. It calculates the
 * corresponding year, month, day, hour, minute, second, and day of the week. Finally check the results, if month is
 * larger than 12, or year is larger than 2037(time_t is defined as int32_t, so year should not be more than 2037), or
 * day is larger than days in month, it will return -EINVAL.
 *
 * @param[in] secs Value to be converted.
 * @param[out] ct The pointer to the `clktime` structure where the resulting date and time will be stored.
 *
 * @return A 32bit value to indicate whether it is successful.
 *
 * @retval 0 On success
 * @retval -EINVAL Input parameter secs is invalid
 *
 * @pre ct != NULL
 *
 * @post N/A
 *
 * @remark It is an internal function called within vRTC.
 */
static int32_t clk_ts_to_ct(time_t secs, struct clktime *ct)
{
	uint32_t i, year, days;
	time_t rsec;	/* remainder seconds */
	int32_t err = 0;

	days = secs / SECDAY;
	rsec = secs % SECDAY;

	ct->dow = day_of_week(days);

	/* Substract out whole years, counting them in i. */
	for (year = POSIX_BASE_YEAR; days >= days_in_year(year); year++) {
		days -= days_in_year(year);
	}
	ct->year = year;

	/* Substract out whole months, counting them in i. */
	for (i = 1; days >= days_in_month(year, i); i++) {
		days -= days_in_month(year, i);
	}
	ct->mon = i;

	/* Days are what is left over (+1) from all that. */
	ct->day = days + 1;

	/* Hours, minutes, seconds are easy */
	ct->hour = rsec / 3600U;
	rsec = rsec % 3600U;
	ct->min  = rsec / 60U;
	rsec = rsec % 60U;
	ct->sec  = rsec;

	/* time_t is defined as int32_t, so year should not be more than 2037. */
	if ((ct->mon > 12U) || (ct->year > 2037) || (ct->day > days_in_month(ct->year, ct->mon))) {
		pr_err("Invalid vRTC param mon %d, year %d, day %d\n", ct->mon, ct->year, ct->day);
		err = -EINVAL;
	}
	return err;
}

/**
 * @brief Convert vrtc device time and date to second.
 *
 * This function converts the current time from the RTC device to seconds. It retrieves the date and time components
 * from the RTC device, performs necessary conversions, and calculates the total number of seconds since the POSIX
 * base year (typically 1970) through calling function `clk_ct_to_ts`.
 *
 * @param[in] vrtc The pointer to the `acrn_vrtc` struct representing virtual RTC device.
 *
 * @return A time_t type value to indicate the converted time in second.
 *
 * @pre vrtc != NULL
 *
 * @post N/A
 *
 * @remark It is an internal function called within vRTC.
 */
static time_t rtc_to_secs(const struct acrn_vrtc *vrtc)
{
	struct clktime ct;
	time_t second = VRTC_BROKEN_TIME;
	const struct rtcdev *rtc= &vrtc->rtcdev;
	uint32_t hour = 0, pm = 0;
	uint32_t century = 0, year = 0;

	do {
		if ((rtcget(rtc, rtc->sec, &ct.sec) < 0) || (rtcget(rtc, rtc->min, &ct.min) < 0) ||
				(rtcget(rtc, rtc->day_of_month, &ct.day) < 0) ||
				(rtcget(rtc, rtc->month, &ct.mon) < 0) || (rtcget(rtc, rtc->year, &year) < 0) ||
				(rtcget(rtc, rtc->century, &century) < 0)) {
			pr_err("Invalid RTC sec %#x hour %#x day %#x mon %#x year %#x century %#x\n",
					rtc->sec, rtc->min, rtc->day_of_month, rtc->month,
					rtc->year, rtc->century);
			break;
		}

		/*
		 * If 12 hour format is inuse, translate it to 24 hour format here.
		 */
		pm = 0;
		hour = rtc->hour;
		if ((rtc->reg_b & RTCSB_24HR) == 0) {
			if (hour & 0x80U) {
				hour &= ~0x80U;
				pm = 1;
			}
		}
		if (rtcget(rtc, hour, &ct.hour) != 0) {
			pr_err("Invalid RTC hour %#x\n", rtc->hour);
			break;
		}
		if ((rtc->reg_b & RTCSB_24HR) == 0) {
			if ((ct.hour >= 1) && (ct.hour <= 12)) {
				/*
				 * Convert from 12-hour format to internal 24-hour
				 * representation as follows:
				 *
				 *    12-hour format		ct.hour
				 *	12	AM		0
				 *	1 - 11	AM		1 - 11
				 *	12	PM		12
				 *	1 - 11	PM		13 - 23
				 */
				if (ct.hour == 12) {
					ct.hour = 0;
				}
				if (pm) {
					ct.hour += 12;
				}
			} else {
				pr_err("Invalid RTC 12-hour format %#x/%d\n",
						rtc->hour, ct.hour);
				break;
			}
		}

		/*
		 * Ignore 'rtc->dow' because some guests like Linux don't bother
		 * setting it at all while others like OpenBSD/i386 set it incorrectly.
		 *
		 * clock_ct_to_ts() does not depend on 'ct.dow' anyways so ignore it.
		 */
		ct.dow = -1;

		ct.year = century * 100 + year;
		if (ct.year < POSIX_BASE_YEAR) {
			pr_err("Invalid RTC century %x/%d\n", rtc->century,
					ct.year);
			break;
		}

		if (clk_ct_to_ts(&ct, &second) != 0) {
			pr_err("Invalid RTC clocktime.date %04d-%02d-%02d\n",
					ct.year, ct.mon, ct.day);
			pr_err("Invalid RTC clocktime.time %02d:%02d:%02d\n",
					ct.hour, ct.min, ct.sec);
			break;
		}
	} while (false);

	return second;
}

/**
 * @brief Convert seconds to RTC time and date.
 *
 * This function converts a given POSIX timestamp (seconds since the base year) to the RTC time format and updates the
 * RTC device with the corresponding date and time component, it handles both 24-hour and 12-hour formats.
 *
 * @param[in] rtctime The seconds to be converted.
 * @param[inout] vrtc The pointer to the `acrn_vrtc` struct representing the virtual rtc device.
 *
 * @return None
 *
 * @pre vrtc != NULL
 *
 * @post N/A
 *
 * @remark It is an internal function called within vRTC.
 */
static void secs_to_rtc(time_t rtctime, struct acrn_vrtc *vrtc)
{
	struct clktime ct;
	struct rtcdev *rtc;
	uint32_t hour;

	if ((rtctime > 0) && (clk_ts_to_ct(rtctime, &ct) == 0)) {
		rtc = &vrtc->rtcdev;
		rtc->sec = rtcset(rtc, ct.sec);
		rtc->min = rtcset(rtc, ct.min);

		if (rtc->reg_b & RTCSB_24HR) {
			hour = ct.hour;
		} else {
			/*
			 * Convert to the 12-hour format.
			 */
			switch (ct.hour) {
			case 0:			/* 12 AM */
			case 12:		/* 12 PM */
				hour = 12;
				break;
			default:
				/*
				 * The remaining 'ct.hour' values are interpreted as:
				 * [1  - 11] ->  1 - 11 AM
				 * [13 - 23] ->  1 - 11 PM
				 */
				hour = ct.hour % 12;
				break;
			}
		}

		rtc->hour = rtcset(rtc, hour);

		if (((rtc->reg_b & RTCSB_24HR) == 0) && (ct.hour >= 12)) {
			rtc->hour |= 0x80;	    /* set MSB to indicate PM */
		}

		rtc->day_of_week = rtcset(rtc, ct.dow + 1);
		rtc->day_of_month = rtcset(rtc, ct.day);
		rtc->month = rtcset(rtc, ct.mon);
		rtc->year = rtcset(rtc, ct.year % 100);
		rtc->century = rtcset(rtc, ct.year / 100);
	}
}

/**
 * @brief Get the current time from the virtual RTC device.
 *
 * This function retrieves the current time from the virtual RTC device associated with the given acrn_vrtc structure.
 * It calculates the current time based on the base RTC time, the offset RTC time, and the elapsed CPU ticks since the
 * last base time was set. If the base RTC time is not set, it returns `VRTC_BROKEN_TIME`.
 *
 * @param[in] vrtc The pointer to the `acrn_vrtc` struct representing the virtual rtc device.
 *
 * @return A `time_t` type value, indicate current time and date in second.
 *
 * @pre vrtc != NULL
 *
 * @post N/A
 *
 * @remark It is an internal function called within vRTC.
 */
static time_t vrtc_get_current_time(struct acrn_vrtc *vrtc)
{
	uint64_t offset;
	time_t second = VRTC_BROKEN_TIME;

	spinlock_obtain(&vrtc_rebase_lock);
	if (vrtc->base_rtctime > 0) {
		offset = (cpu_ticks() - vrtc->base_tsc) / (get_tsc_khz() * 1000U);
		second = vrtc->base_rtctime + vrtc->offset_rtctime + (time_t)offset;
		if (second < vrtc->last_rtctime) {
			second = vrtc->last_rtctime;
		} else {
			vrtc->last_rtctime = second;
		}
	}
	spinlock_release(&vrtc_rebase_lock);
	return second;
}

#define CMOS_ADDR_PORT		0x70U /**< Pre-defined port IO to access RTC register address. */
#define CMOS_DATA_PORT		0x71U /**< Pre-defined port IO to access RTC register data. */

/**
* @brief Local spinlock_t variable used to avoid the physical RTC is accessed by different guest VMs in parallel.
*/
static spinlock_t cmos_lock = { .head = 0U, .tail = 0U };

/**
 * @brief Read a byte from RTC register with the given register address
 *
 * This function reads a byte of data from a given register address. It writes the address to the CMOS address port and
 * reads the data from the CMOS data port.
 *
 * @param[in] addr The address of the RTC register to read from.
 *
 * @return The byte of data read from the give register address.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark It is an internal function called within vRTC.
 */
static uint8_t cmos_read(uint8_t addr)
{
	pio_write8(addr, CMOS_ADDR_PORT);
	return pio_read8(CMOS_DATA_PORT);
}

/**
 * @brief Write a byte to RTC register with the given register address
 *
 * This function writes a byte of data to a given register address. It writes the address to the CMOS address port
 * and writes the a byte of data to the CMOS data port.
 *
 * @param[in] addr The address of the RTC register to write to.
 * @param[in] value The byte of data to write to the given CMOS register.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark It is an internal function called within vRTC.
 */
static void cmos_write(uint8_t addr, uint8_t value)
{
	pio_write8(addr, CMOS_ADDR_PORT);
	pio_write8(value, CMOS_DATA_PORT);
}

/**
 * @brief Check whether the RTC register is in updating status
 *
 * This function is called to check whether the RTC register is in updating status, read value from the status register
 * A, and check bit RTCSA_TUP.
 *
 * @return A boolean value to indicate whether the RTC register is in updating status
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark It is an internal function called within vRTC.
 */
static bool cmos_update_in_progress(void)
{
	return (cmos_read(RTC_STATUSA) & RTCSA_TUP) ? 1 : 0;
}

/**
 * @brief Get a physical RTC register's value
 *
 * This function is called to read a physical RTC register's value. It will check whether the physical RTC is updating
 * firstly, if it's in updating progress, then retry it, maximum retry times is 2000, if it's not in updating progress,
 * then read it. A spin lock is used to avoid that the physical RTC is accessed by different guest VMs in parallel.
 *
 * @param[in] addr The address of the RTC register to read from.
 *
 * @return The byte of data read from the register.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark It is an internal function called within vRTC.
 */
static uint8_t cmos_get_reg_val(uint8_t addr)
{
	uint8_t reg;
	int32_t tries = 2000;

	spinlock_obtain(&cmos_lock);

	/* Make sure an update isn't in progress */
	while (cmos_update_in_progress() && (tries != 0)) {
		tries -= 1;
	}

	reg = cmos_read(addr);

	spinlock_release(&cmos_lock);
	return reg;
}

/**
 * @brief Set a physical RTC register's value.
 *
 * This function is called to set a physical RTC register's value. It will check Whether the physical RTC is updating
 * firstly, if it's in updating progress, then retry it, maximum retry times is 2000, if it's not in updating progress,
 * then write it. A spin lock is used to avoid that the physical RTC is accessed by different guest VMs in parallel.
 *
 * @param[in] addr The address of the RTC register to write to.
 * @param[in] value The byte of data to write to the register.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark It is an internal function called by within vRTC.
 */
static void cmos_set_reg_val(uint8_t addr, uint8_t value)
{
	int32_t tries = 2000;

	spinlock_obtain(&cmos_lock);

	/* Make sure an update isn't in progress */
	while (cmos_update_in_progress() && (tries != 0)) {
		tries -= 1;
	}

	cmos_write(addr, value);

	spinlock_release(&cmos_lock);
}

#define TRIGGER_ALARM	(RTCIR_ALARM | RTCIR_INT) /**< Register value for triggerring alarm. */
#define RTC_DELTA	1	/**< For RTC and system time may out of sync for no more than 1s. */

/**
 * @brief Get status register C value from virtual RTC device.
 *
 * This function is called to return status register C value from virtual RTC device, if alarm interrupt is enabled and
 * rtc time is in alarm time scale, it sets the alarm interrupt flag in the returned value, and the status register C
 * of the vrtc device is reset to zero.
 *
 * @param[in] vrtc The pointer to the `acrn_vrtc` struct representing the virtual RTC device.
 *
 * @return The value of the RTC register C.
 *
 * @pre vrtc != NULL
 *
 * @post N/A
 *
 * @remark It is an internal function called by within vRTC.
 */
static uint8_t vrtc_get_reg_c(struct acrn_vrtc *vrtc)
{
	uint8_t	ret = vrtc->rtcdev.reg_c;
	struct rtcdev *rtc = &vrtc->rtcdev;
	time_t current, alarm;

	if ((rtc->reg_b & RTCSB_AINTR) != 0U) {
		current = rtc->hour * 3600 + rtc->min * 60 + rtc->sec;
		alarm = rtc->alarm_hour * 3600 + rtc->alarm_min * 60 + rtc->alarm_sec;

		if ((current >= (alarm - RTC_DELTA)) && (current <= (alarm + RTC_DELTA))) {
			/*
			 * Linux RTC driver will trigger alarm interrupt when getting
			 * RTC time, and then read the interrupt flag register. If the value was not
			 * correct, read failure will occurs. So if alarm interrupt is enabled
			 * and rtc time is in alarm time scale, set the interrupt flag. The
			 * interrupt is not acturally triggered for driver will read the register
			 * proactively.
			 */
			ret |= TRIGGER_ALARM;
		}
	}

	vrtc->rtcdev.reg_c = 0;
	return ret;
}

/**
 * @brief Set status register B value to the virtual RTC device.
 *
 * This function is called to set status register B value to the virtual RTC device.
 *
 * @param[out] vrtc The pointer to the acrn_vrtc structure representing the virtual RTC.
 * @param[in] newval The new value of status register B to be set.
 *
 * @return None
 *
 * @pre vrtc != NULL
 *
 * @post N/A
 *
 * @remark It is an internal function called by within vRTC.
 */
static void vrtc_set_reg_b(struct acrn_vrtc *vrtc, uint8_t newval)
{
	vrtc->rtcdev.reg_b = newval;
}

/**
 * @brief Read from the virtual RTC device.
 *
 * This function reads the value from the RTC register specified by the address port. To read from the virtual RTC
 * device, the guest writes the register index to the RTC address port and then reads the register value from the RTC
 * data port. This function is used to simulate the behavior of reading in a virtualized environment.
 *
 * - If the accessed port is CMOS_ADDR_PORT, it will set the value as index cached in last write (0 by default) and
 *   return.
 * - If the accessed port is CMOS_DATA_PORT,
 *   - For Service VM, it will directly read the value from the physical CMOS register.
 *   - For a non-Service VM, it will return false indicating the read operation failed if the address is greater than
 *     RTC_CENTURY. Otherwise, the read operation will be emulated.
 *
 * @param[inout] vcpu Pointer to the virtual CPU that is reading from the virtual RTC. The value read from the virtual
 *                    RTC will be stored in the PIO request.
 * @param[in] addr The address port to read from.
 * @param[in] width The width of the data to be read. This is not used in this function.
 *
 * @return A boolean value indicating whether the read operation is successful.
 *
 * @retval true Successfully read from the virtual RTC device.
 * @retval false Failed to read from the virtual RTC device.
 *
 * @pre vcpu != NULL
 * @pre vcpu->vm != NULL
 * @pre addr == 0x70U || addr == 0x71U
 *
 * @post N/A
 */
static bool vrtc_read(struct acrn_vcpu *vcpu, uint16_t addr, __unused size_t width)
{
	uint8_t offset;
	time_t current;
	struct acrn_vrtc *vrtc = &vcpu->vm->vrtc;
	struct acrn_pio_request *pio_req = &vcpu->req.reqs.pio_request;
	struct acrn_vm *vm = vcpu->vm;
	bool ret = true;

	offset = vrtc->addr;

	if (addr == CMOS_ADDR_PORT) {
		pio_req->value = offset;
	} else {
		if (is_service_vm(vm)) {
			pio_req->value = cmos_get_reg_val(offset);
		} else {
			if (offset <= RTC_CENTURY) {
				current = vrtc_get_current_time(vrtc);
				secs_to_rtc(current, vrtc);

				if(offset == 0xCU) {
					pio_req->value = vrtc_get_reg_c(vrtc);
				} else {
					pio_req->value = *((uint8_t *)&vrtc->rtcdev + offset);
				}
				RTC_DEBUG("read 0x%x, 0x%x", offset, pio_req->value);
			} else {
				pr_err("vrtc read invalid addr 0x%x", offset);
				ret = false;
			}
		}
	}

	return ret;
}

/**
 * @brief Check whether given offset is correspondsing to time register.
 *
 * This function checks if the given offset corresponds to one of the time related registers in the RTC. The time
 * related registers include seconds, minutes, hours, day, month, year, and century.
 *
 * @param[in] offset The offset to be checked.
 *
 * @return A boolean value whether offset is correspondsing to time register.
 *
 * @retval 1 Offset is correspondsing to time register.
 * @retval 0 Offset is not correspondsing to time register.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark It is an internal function called by within vRTC.
 */
static inline bool vrtc_is_time_register(uint32_t offset)
{
	return ((offset == RTC_SEC) || (offset == RTC_MIN) || (offset == RTC_HRS) || (offset == RTC_DAY)
			|| (offset == RTC_MONTH) || (offset == RTC_YEAR) || (offset == RTC_CENTURY));
}

/**
 * @brief Write a value to the virtual RTC.
 *
 * This function writes a specified value to the virtual RTC at a given offset. To write to the virtual RTC, the guest
 * writes the register index to the RTC address port and then writes the register value to the RTC data port. This
 * function is used to simulate the behavior of writing in a virtualized environment.
 *
 * - If the accessed port is CMOS_ADDR_PORT and the width is 1 byte, it will store the value as the index and return.
 * - If the accessed port is CMOS_DATA_PORT,
 *   - For Service VM, it will directly write the value to the physical CMOS register. If the physical date/time is
 *     changed, for RT VMs and pre-launched VMs, the RTC/TSC snapshots will be updated. Those snapshots are used to
 *     emulate the virtual date/time for non-Service VM.
 *   - For a non-Service VM, it will ignore the write to the RTC_STATUSA, RTC_INTR and RTC_STATUSD. Otherwise, it will
 *     update the virtual register value and RTC time. And for Post-launched VM, it will send a VM event to notify the
 *     VM of the change in the RTC time if the address port is in the range of the time registers.
 *
 * @param[inout] vcpu Pointer to the virtual CPU that is writing to the virtual RTC.
 * @param[in] addr The address port to write to.
 * @param[in] width Width of the value to be written to the virtual RTC.
 * @param[in] value Value to be written to the virtual RTC.
 *
 * @return A boolean value indicating whether the write operation is handled successfully, which is always true in
 *         current design. It either updates the physical registers, updates the virtual registers, or ignores the
 *         write.
 *
 * @retval true The write operation is handled successfully.
 *
 * @pre vcpu != NULL
 * @pre vcpu->vm != NULL
 * @pre addr == 0x70U || addr == 0x71U
 *
 * @post N/A
 *
 * @remark N/A
 */
static bool vrtc_write(struct acrn_vcpu *vcpu, uint16_t addr, size_t width,
			uint32_t value)
{
	time_t current, after;
	struct acrn_vrtc *vrtc = &vcpu->vm->vrtc;
	struct acrn_vrtc temp_vrtc;
	uint8_t mask = 0xFFU;
	struct vm_event rtc_chg_event;
	struct rtc_change_event_data *edata = (struct rtc_change_event_data *)rtc_chg_event.event_data;

	if ((width == 1U) && (addr == CMOS_ADDR_PORT)) {
		vrtc->addr = (uint8_t)(value & 0x7FU);
	} else {
		if (is_service_vm(vcpu->vm)) {
			if (vrtc_is_time_register(vrtc->addr)) {
				current = vrtc_get_physical_rtc_time(&temp_vrtc);
				cmos_set_reg_val(vcpu->vm->vrtc.addr, (uint8_t)(value & 0xFFU));
				after = vrtc_get_physical_rtc_time(&temp_vrtc);
				vrtc_update_basetime(after, current - after);
			} else {
				cmos_set_reg_val(vcpu->vm->vrtc.addr, (uint8_t)(value & 0xFFU));
			}
		} else {
			switch (vrtc->addr) {
			case RTC_STATUSA:
			case RTC_INTR:
			case RTC_STATUSD:
				RTC_DEBUG("RTC reg_%x set to %#x (ignored)\n", vrtc->addr, value);
				break;
			case RTC_STATUSB:
				vrtc_set_reg_b(vrtc, value);
				RTC_DEBUG("RTC reg_b set to %#x\n", value);
				break;
			case RTC_SECALRM:
			case RTC_MINALRM:
				/* FALLTHRU */
			case RTC_HRSALRM:
				*((uint8_t *)&vrtc->rtcdev + vrtc->addr) = (uint8_t)(value & 0x7FU);
				RTC_DEBUG("RTC alarm reg(%d) set to %#x (ignored)\n", vrtc->addr, value);
				break;
			case RTC_SEC:
				/*
				 * High order bit of 'seconds' is readonly.
				 */
				mask = 0x7FU;
				/* FALLTHRU */
			default:
				RTC_DEBUG("RTC offset %#x set to %#x\n", vrtc->addr, value);
				*((uint8_t *)&vrtc->rtcdev + vrtc->addr) = (uint8_t)(value & mask);
				current = vrtc_get_current_time(vrtc);
				after = rtc_to_secs(vrtc);
				spinlock_obtain(&vrtc_rebase_lock);
				vrtc->offset_rtctime += after - current;
				vrtc->last_rtctime = VRTC_BROKEN_TIME;
				spinlock_release(&vrtc_rebase_lock);
				if (is_postlaunched_vm(vcpu->vm) && vrtc_is_time_register(vrtc->addr)) {
					rtc_chg_event.type = VM_EVENT_RTC_CHG;
					edata->delta_time = after - current;
					edata->last_time = current;
					send_vm_event(vcpu->vm, &rtc_chg_event);
				}
				break;
			}
		}
	}

	return true;
}

#define CALIBRATE_PERIOD	(3 * 3600 * 1000)	/**< By ms, totally 3 hours. */
static struct hv_timer calibrate_timer; /**<  timer used to calibrate. */

/**
 * @brief Get rtc time and date from physical register.
 *
 * This function retrieves the current time from the physical RTC and updates the virtual RTC structure with the
 * retrieved values. It reads the time related registers from the CMOS and converts the time to seconds.
 *
 * @param[inout] vrtc The pointer to the `acrn_vrtc` struct representing the virtual RTC device.
 *
 * @return The current physical RTC time as a `time_t` value.
 *
 * @pre vrtc != NULL
 *
 * @post N/A
 *
 * @remark It is an internal function called by within vRTC.
 */
static time_t vrtc_get_physical_rtc_time(struct acrn_vrtc *vrtc)
{
	struct rtcdev *vrtcdev = &vrtc->rtcdev;

	vrtcdev->sec = cmos_get_reg_val(RTC_SEC);
	vrtcdev->min = cmos_get_reg_val(RTC_MIN);
	vrtcdev->hour = cmos_get_reg_val(RTC_HRS);
	vrtcdev->day_of_month = cmos_get_reg_val(RTC_DAY);
	vrtcdev->month = cmos_get_reg_val(RTC_MONTH);
	vrtcdev->year = cmos_get_reg_val(RTC_YEAR);
	vrtcdev->century = cmos_get_reg_val(RTC_CENTURY);
	vrtcdev->reg_b = cmos_get_reg_val(RTC_STATUSB);

	return rtc_to_secs(vrtc);
}

/**
 * @brief Update the base time of the virtual RTC for RT and Prelaunch VMs.
 *
 * This function updates the base time of the virtual RTC for all real-time (RT) and pre-launched VMs.
 * It sets the base TSC, base RTC time, and adjusts the offset RTC time.
 *
 * @param[in] physical_time The [hysical time to be updated to virtual RTC.
 * @param[in] offset The offset to be added to the physical time to calculate the new base time.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark It is an internal function called by within vRTC.
 */
static void vrtc_update_basetime(time_t physical_time, time_t offset)
{
	struct acrn_vm *vm;
	uint32_t vm_id;

	for (vm_id = 0U; vm_id < CONFIG_MAX_VM_NUM; vm_id++) {
		vm = get_vm_from_vmid(vm_id);
		if (is_rt_vm(vm) || is_prelaunched_vm(vm)) {
			spinlock_obtain(&vrtc_rebase_lock);
			vm->vrtc.base_tsc = cpu_ticks();
			vm->vrtc.base_rtctime = physical_time;
			vm->vrtc.offset_rtctime += offset;
			spinlock_release(&vrtc_rebase_lock);
		}
	}
}

/**
 * @brief Callback of calibrate timer.
 *
 * This function is a timer callback that calibrates the virtual RTC by updating its base time with the current
 * physical RTC time. It retrieves the current physical RTC time and updates base TSC, base RTC time of virtual RTC
 * device.
 *
 * @param[in] data Unused parameter.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark It is an internal function called by within vRTC.
 */
static void calibrate_timer_callback(__unused void *data)
{
	struct acrn_vrtc temp_vrtc;
	time_t physical_time = vrtc_get_physical_rtc_time(&temp_vrtc);

	vrtc_update_basetime(physical_time, 0);
}

/**
 * @brief Set up and calibrate timer.
 *
 * This function sets up and starts a periodic calibration timer. It calculates the period and the fire time for the
 * timer, initializes the timer with the `calibrate_timer_callback` function, and adds the timer to the system.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark It is an internal function called by within vRTC.
 */
static void calibrate_setup_timer(void)
{
	uint64_t period_in_cycle, fire_tsc;

	period_in_cycle = TICKS_PER_MS * CALIBRATE_PERIOD;
	fire_tsc = cpu_ticks() + period_in_cycle;
	initialize_timer(&calibrate_timer,
			calibrate_timer_callback, NULL,
			fire_tsc, period_in_cycle);

	/* Start an periodic timer */
	if (add_timer(&calibrate_timer) != 0) {
		pr_err("Failed to add calibrate timer");
	}
}

/**
 * @brief Set the base time of the virtual RTC.
 *
 * This function sets the base time of the virtual RTC by reading the current time from the physical RTC and updating
 * the virtual RTC structure with the retrieved values. It also updates the base and last RTC time.
 *
 * @param[out] vrtc The pointer to the `acrn_vrtc` struct representing the virtual rtc device.
 *
 * @return None
 *
 * @pre vrtc != NULL
 *
 * @post N/A
 *
 * @remark It is an internal function called by within vRTC.
 */
static void vrtc_set_basetime(struct acrn_vrtc *vrtc)
{
	struct rtcdev *vrtcdev = &vrtc->rtcdev;
	time_t current;

	/*
	 * Read base time from physical rtc.
	 */
	vrtcdev->sec = cmos_get_reg_val(RTC_SEC);
	vrtcdev->min = cmos_get_reg_val(RTC_MIN);
	vrtcdev->hour = cmos_get_reg_val(RTC_HRS);
	vrtcdev->day_of_month = cmos_get_reg_val(RTC_DAY);
	vrtcdev->month = cmos_get_reg_val(RTC_MONTH);
	vrtcdev->year = cmos_get_reg_val(RTC_YEAR);
	vrtcdev->century = cmos_get_reg_val(RTC_CENTURY);
	vrtcdev->reg_a = cmos_get_reg_val(RTC_STATUSA) & (~RTCSA_TUP);
	vrtcdev->reg_b = cmos_get_reg_val(RTC_STATUSB);
	vrtcdev->reg_c = cmos_get_reg_val(RTC_INTR);
	vrtcdev->reg_d = cmos_get_reg_val(RTC_STATUSD);

	current = rtc_to_secs(vrtc);
	spinlock_obtain(&vrtc_rebase_lock);
	vrtc->base_rtctime = current;
	vrtc->last_rtctime = VRTC_BROKEN_TIME;
	spinlock_release(&vrtc_rebase_lock);
}

/**
 * @brief Suspend virtual RTC.
 *
 * This function is to delete calibrate timer in system suspend routine, which is only allowed for service vm.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark N/A
 */
void suspend_vrtc(void)
{
	/* For service vm */
	del_timer(&calibrate_timer);
}

/**
 * @brief Resume virtual RTC.
 *
 * This function is to set up calibrate timer in system resume routine, which is only allowed for service vm.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark N/A
 */
void resume_vrtc(void)
{
	/* For service vm */
	calibrate_setup_timer();
}

/**
 * @brief Initialize the virtual RTC.
 *
 * This function initializes the virtual RTC (Real-Time Clock) device for the given virtual machine. It sets up the
 * necessary data structures and state required for the RTC to function correctly. This function should be called during
 * the initialization phase of the virtual machine.
 *
 * - When Service VM's vRTC device is initialized, a periodic timer (every 3 hours) is set up to calibrate the virtual
 *   date/time of other VMs. When the calibration timer is triggered, for RT VMs and pre-launched VMs, the TSC/RTC
 *   snapshots are updated to reflect the physical TSC/RTC-time at that moment.
 * - When non-Service VM's vRTC device is initialized, the TSC/RTC snapshots are initialized to reflect the physical
 *   TSC/RTC-time at the moment.
 *
 * @param[inout] vm The virtual machine that contains the virtual RTC to be initialized.
 *
 * @return None
 *
 * @pre vm != NULL
 *
 * @post N/A
 *
 * @remark N/A
 */
void vrtc_init(struct acrn_vm *vm)
{
	struct vm_io_range range = {
	.base = CMOS_ADDR_PORT, .len = 2U};

	/* Initializing the CMOS RAM offset to 0U */
	vm->vrtc.addr = 0U;

	vm->vrtc.vm = vm;
	register_pio_emulation_handler(vm, RTC_PIO_IDX, &range, vrtc_read, vrtc_write);

	if (is_service_vm(vm)) {
		calibrate_setup_timer();
	} else {
		vrtc_set_basetime(&vm->vrtc);
		vm->vrtc.base_tsc = cpu_ticks();
	}
}

/**
 * @}
 */
