/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * Copyright (C) 2022-2024 Intel Corporation.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)rtc.h	7.1 (Berkeley) 5/12/91
 * $FreeBSD$
 */

#ifndef _MC146818_RTC_H_
#define _MC146818_RTC_H_

/*
 * MC146818 RTC Register locations
 */
/**
 * @addtogroup vp-dm_vperipheral
 *
 * @{
 */

/**
 * @file
 * @brief Definition for rtc register location.
 *
 * This file provides the macro definition fro rtc register location and offset, details refer to.MC146818 datasheet.
 */
#define RTC_SEC	0x00	/**< Seconds register address location */
#define RTC_SECALRM	0x01	/**< Seconds alarm register address location */
#define RTC_MIN	0x02	/**< Minutes register address location */
#define RTC_MINALRM	0x03	/**< Minutes alarm register address location */
#define RTC_HRS	0x04	/**< Hours register address location */
#define RTC_HRSALRM	0x05	/**< Hours alarm register address location */
#define RTC_WDAY	0x06	/**< Week day register address location */
#define RTC_DAY	0x07	/**< Day of month register address location */
#define RTC_MONTH	0x08	/**< Month of year register address location */
#define RTC_YEAR	0x09	/**< Year register address location*/
#define RTC_CENTURY	0x32	/**< Century register address location*/

#define RTC_STATUSA	0x0a	/**< Status register A address location */
#define RTCSA_TUP	0x80U	/**< Bit 7 of Status A register, 1 = update in progress, 0 = update complete */

#define RTC_STATUSB	0x0b	/**< Status register B address location */
#define RTCSB_24HR	0x02U	/**< Bit 1 of Status B register, 0 = 12 hours, 1 = 24 hours */
#define RTCSB_BCD	0x04U	/**< Bit 2 of Status B register, 0 = BCD, 1 = Binary coded time */
#define RTCSB_AINTR	0x20U	/**< Bit 5 of Status B register, 1 = enable alarm interrupt, 0 = disable alarm interrupt */
#define RTCSB_HALT	0x80U	/**< Bit 7 of Status B register, 1 = stop clock updates, 0 = allow update */

#define RTC_INTR	0x0c	/**< Status register C address location */
#define RTCIR_ALARM	0x20U	/**< Bit 5 of Status C register, alarm interrupt flag */
#define RTCIR_INT	0x80U	/**< Bit 7 of Status C register, Interrupt request flag */

#define RTC_STATUSD	0x0d	/**< Status register D address location */

#endif /* _MC146818_RTC_H_ */

/**
 * @}
 */
