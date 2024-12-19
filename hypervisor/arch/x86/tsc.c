/*
 * Copyright (C) 2021-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <util.h>
#include <asm/cpuid.h>
#include <asm/cpu_caps.h>
#include <asm/io.h>
#include <asm/tsc.h>
#include <asm/cpu.h>
#include <logmsg.h>
#include <acpi.h>

/**
 * @addtogroup hwmgmt_time
 *
 * @{
 */

/**
 * @file
 * @brief TSC Management
 *
 * This file contains the implementation of functions for managing the Time Stamp Counter (TSC). It includes
 * functions to calibrate the TSC frequency, initialize the HPET, and read the TSC value.
 */

#define CAL_MS	10U  /**< Calibration duration in milliseconds. */

#define HPET_PERIOD	0x004U  /**< HPET period register offset. */
#define HPET_CFG	0x010U  /**< HPET configuration register offset. */
#define HPET_COUNTER	0x0F0U  /**< HPET main counter register offset. */

#define HPET_CFG_ENABLE	0x001UL /**< HPET configuration enable register offset. */

/**
 * @brief TSC frequency in kHz.
 *
 * This global variable stores the calibrated TSC frequency in kHz. It is calculated in the function calibrate_tsc()
 * and used by various functions to get the TSC frequency.
 */
static uint32_t tsc_khz;

/**
 * @brief HPET base address.
 *
 * This global variable stores the base address of the HPET. It is initialized in the function hpet_init() and used
 * by various functions to interact with the HPET registers.
 */
static void *hpet_hva;

#define PIT_TICK_RATE	1193182U  /**< PIT tick rate in Hz. */
#define PIT_TARGET	0x3FFFU  /**< PIT target value. */
#define PIT_MAX_COUNT	0xFFFFU  /**< Maximum count value for PIT. */

/**
 * @brief Calibrate the TSC frequency using the PIT.
 *
 * This function calibrates the Time Stamp Counter (TSC) frequency using the Programmable Interval Timer (PIT). It is
 * used as a fallback method when the High Precision Event Timer (HPET) is not available or not capable. The function
 * measures the TSC frequency by using the PIT to generate a delay and then calculating the TSC frequency based on the
 * elapsed TSC value during that delay.
 *
 * The function is invoked by pit_hpet_calibrate_tsc() when HPET is not available.
 *
 * @param[in] cal_ms_arg The calibration duration in milliseconds.
 *
 * @return A uint64_t value, the calibrated TSC frequency in Hz.
 *
 * @pre cal_ms_arg > 0
 *
 * @post N/A
 */
static uint64_t pit_calibrate_tsc(uint32_t cal_ms_arg)
{
	uint32_t cal_ms = cal_ms_arg;
	uint32_t initial_pit;
	uint16_t current_pit;
	uint32_t max_cal_ms;
	uint64_t current_tsc;
	uint8_t initial_pit_high, initial_pit_low;

	max_cal_ms = ((PIT_MAX_COUNT - PIT_TARGET) * 1000U) / PIT_TICK_RATE;
	cal_ms = min(cal_ms, max_cal_ms);

	/* Assume the 8254 delivers 18.2 ticks per second when 16 bits fully
	 * wrap.  This is about 1.193MHz or a clock period of 0.8384uSec
	 */
	initial_pit = (cal_ms * PIT_TICK_RATE) / 1000U;
	initial_pit += PIT_TARGET;
	initial_pit_high = (uint8_t)(initial_pit >> 8U);
	initial_pit_low = (uint8_t)initial_pit;

	/* Port 0x43 ==> Control word write; Data 0x30 ==> Select Counter 0,
	 * Read/Write least significant byte first, mode 0, 16 bits.
	 */

	pio_write8(0x30U, 0x43U);
	pio_write8(initial_pit_low, 0x40U);	/* Write LSB */
	pio_write8(initial_pit_high, 0x40U);		/* Write MSB */

	current_tsc = rdtsc();

	do {
		/* Port 0x43 ==> Control word write; 0x00 ==> Select
		 * Counter 0, Counter Latch Command, Mode 0; 16 bits
		 */
		pio_write8(0x00U, 0x43U);

		current_pit = (uint16_t)pio_read8(0x40U);	/* Read LSB */
		current_pit |= (uint16_t)pio_read8(0x40U) << 8U;	/* Read MSB */
		/* Let the counter count down to PIT_TARGET */
	} while (current_pit > PIT_TARGET);

	current_tsc = rdtsc() - current_tsc;

	return (current_tsc / cal_ms) * 1000U;
}

/**
 * @brief HPET initialization.
 *
 * This function initializes the High Precision Event Timer (HPET). It gets the address of the ACPI HPET
 * Description Table and sets the general configuration register (offset 0x10) ENABLE_CNF (bit 0) if HPET is
 * available on the physical platform. This bit is for HPET overall enable. This bit must be set to enable any of
 * the timers to generate interrupts.
 *
 * There are some ways to get TSC frequency: Read from CPUID or use HPET/PIT to measure it. On some physical
 * platforms, CPUID.15H:ECX (reports the nominal frequency of the core crystal clock in Hz) is equal to 0, and
 * CPUID.16H:ECX[bit15] can only report frequency in MHz, which is not accurate enough. Thus we use HPET to measure
 * the TSC frequency when CPUID.15H equals 0 and HPET is available.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
void hpet_init(void)
{
	uint64_t cfg;

	hpet_hva = parse_hpet();
	if (hpet_hva != NULL) {
		cfg = mmio_read64(hpet_hva + HPET_CFG);
		if ((cfg & HPET_CFG_ENABLE) == 0UL) {
			cfg |= HPET_CFG_ENABLE;
			mmio_write64(cfg, hpet_hva + HPET_CFG);
		}
	}
}

/**
 * @brief Check if HPET is capable.
 *
 * This function checks whether HPET is capable via the global variable hpet_hva. The hpet_hva refers to the base
 * address of the ACPI HPET Description Table. If hpet_hva is a null pointer, HPET is not capable.
 *
 * @return A boolean value indicating whether HPET is capable.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline bool is_hpet_capable(void)
{
	return (hpet_hva != NULL);
}

/**
 * @brief Read HPET memory-mapped registers.
 *
 * This function reads HPET memory-mapped registers based on the given offset. For example, if the offset == 0x0F0,
 * this function will read the main counter value register. Refer to HPET specification for the register definitions.
 *
 * @param[in] offset Specify which register would like to read.
 *
 * @return The 32bit value read from the given offset.
 *
 * @pre offset <= 0x3FF
 *
 * @post N/A
 */
static inline uint32_t hpet_read(uint32_t offset)
{
	return mmio_read32(hpet_hva + offset);
}

/**
 * @brief Read TSC and HPET counter.
 *
 * This function first reads the HPET counter value and stores it in the provided pointer, then reads the TSC value.
 *
 * @param[out] p Pointer to store the HPET counter value.
 *
 * @return The current TSC value.
 *
 * @pre p != NULL
 *
 * @post N/A
 */
static inline uint64_t tsc_read_hpet(uint64_t *p)
{
	uint64_t current_tsc;

	/* read hpet first */
	*p = hpet_read(HPET_COUNTER);
	current_tsc = rdtsc();

	return current_tsc;
}

/**
 * @brief Calibrate the TSC frequency using HPET.
 *
 * This function calibrates the Time Stamp Counter (TSC) frequency using the High Precision Event Timer (HPET). It
 * measures the TSC frequency by recording the TSC and HPET values before and after a delay generated by the PIT.
 * The function calculates the TSC frequency based on the elapsed TSC and HPET values during the delay.
 *
 * The function handles the case where the HPET counter wraps around during the measurement.
 *
 * @param[in] cal_ms_arg The calibration duration in milliseconds.
 *
 * @return A uint64_t value, the calibrated TSC frequency in Hz.
 *
 * @pre cal_ms_arg > 0
 *
 * @post N/A
 */
static uint64_t hpet_calibrate_tsc(uint32_t cal_ms_arg)
{
	uint64_t tsc1, tsc2, hpet1, hpet2;
	uint64_t delta_tsc, delta_fs;
	uint64_t rflags, tsc_khz;

	CPU_INT_ALL_DISABLE(&rflags);
	tsc1 = tsc_read_hpet(&hpet1);
	pit_calibrate_tsc(cal_ms_arg);
	tsc2 = tsc_read_hpet(&hpet2);
	CPU_INT_ALL_RESTORE(rflags);

	/* in case counter wrap happened in the low 32 bits */
	if (hpet2 <= hpet1) {
		hpet2 |= (1UL << 32U);
	}
	delta_fs = (hpet2 - hpet1) * hpet_read(HPET_PERIOD);
	delta_tsc = tsc2 - tsc1;
	/*
	 * FS_PER_S = 10 ^ 15
	 *
	 * tsc_khz = delta_tsc / (delta_fs / FS_PER_S) / 1000UL;
	 *         = delta_tsc / delta_fs * (10 ^ 12)
	 *         = (delta_tsc * (10 ^ 6)) / (delta_fs / (10 ^ 6))
	 */
	tsc_khz = (delta_tsc * 1000000UL) / (delta_fs / 1000000UL);
	return tsc_khz * 1000U;
}

/**
 * @brief Calibrate the TSC frequency using HPET or PIT.
 *
 * This function calibrates the Time Stamp Counter (TSC) frequency using either the High Precision Event Timer (HPET)
 * or the Programmable Interval Timer (PIT), depending on the availability of HPET. It first checks if HPET is capable
 * and uses it for calibration if available. Otherwise, it falls back to using the PIT for calibration.
 *
 * The function also compares the measured TSC frequency with a reference TSC frequency. If the difference exceeds 5%,
 * the reference TSC frequency is used as the calibrated value.
 *
 * @param[in] cal_ms_arg The calibration duration in milliseconds.
 * @param[in] tsc_ref_hz The reference TSC frequency in Hz.
 *
 * @return A uint64_t value, the calibrated TSC frequency in Hz.
 *
 * @pre cal_ms_arg > 0
 *
 * @post N/A
 */
static uint64_t pit_hpet_calibrate_tsc(uint32_t cal_ms_arg, uint64_t tsc_ref_hz)
{
	uint64_t tsc_hz, delta;

	if (is_hpet_capable()) {
		tsc_hz = hpet_calibrate_tsc(cal_ms_arg);
	} else {
		tsc_hz = pit_calibrate_tsc(cal_ms_arg);
	}

	if (tsc_ref_hz != 0UL) {
		delta = (tsc_hz * 100UL) / tsc_ref_hz;
		if ((delta < 95UL) || (delta > 105UL)) {
			tsc_hz = tsc_ref_hz;
		}
	}

	return tsc_hz;
}

/**
 * @brief Determine TSC frequency via CPUID 0x15.
 *
 * This function calculates TSC frequency following this formula if CPUID.15H is available on the physical platform,
 * and CPUID.15:EAX and CPUID.EBX are both non-zero: tsc_hz = CPUID.15H:ECX * CPUID.15H:EBX / CPUID.15H:EAX.
 * Otherwise, this function returns 0.
 *
 * Refer to Chapter 3.3 CPUID Instruction, Vol. 2, SDM 325426-078 for more details about CPUID.15H.
 *
 * @return A uint64_t value, the TSC frequency value in Hz.
 *
 * @pre N/A
 *
 * @post N/A
 */
static uint64_t native_calculate_tsc_cpuid_0x15(void)
{
	uint64_t tsc_hz = 0UL;
	const struct cpuinfo_x86 *cpu_info = get_pcpu_info();

	if (cpu_info->cpuid_level >= 0x15U) {
		uint32_t eax_denominator, ebx_numerator, ecx_hz, reserved;

		cpuid_subleaf(0x15U, 0x0U, &eax_denominator, &ebx_numerator,
			&ecx_hz, &reserved);

		if ((eax_denominator != 0U) && (ebx_numerator != 0U)) {
			tsc_hz = ((uint64_t) ecx_hz *
				ebx_numerator) / eax_denominator;
		}
	}

	return tsc_hz;
}

/**
 * @brief Determine TSC frequency via CPUID 0x16.
 *
 * This function calculates TSC frequency following this formula if CPUID.16H is available on the physical platform:
 * tsc_hz = CPUID.16H:EAX * 1000000U.
 *
 * Refer to Chapter 3.3 CPUID Instruction, Vol. 2, SDM 325426-078 for more details about CPUID.16H.
 *
 * @return A uint64_t value, the TSC frequency value in Hz.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark CPUID.16H:EAX can only report Processor Base Frequency in MHz, which is not accurate enough.
 */
static uint64_t native_calculate_tsc_cpuid_0x16(void)
{
	uint64_t tsc_hz = 0UL;
	const struct cpuinfo_x86 *cpu_info = get_pcpu_info();

	if (cpu_info->cpuid_level >= 0x16U) {
		uint32_t eax_base_mhz, ebx_max_mhz, ecx_bus_mhz, edx;

		cpuid_subleaf(0x16U, 0x0U, &eax_base_mhz, &ebx_max_mhz, &ecx_bus_mhz, &edx);
		tsc_hz = (uint64_t) eax_base_mhz * 1000000U;
	}

	return tsc_hz;
}

/**
 * @brief Calibrate the TSC frequency.
 *
 * This function calibrates the Time Stamp Counter (TSC) frequency. The TSC frequency is determined by CPUID.15H if
 * it reports a non-zero value. Otherwise, the TSC frequency is further calibrated by HPET, PIT and CPUID.16H. It
 * first measures the TSC frequency using HPET if available; otherwise, it relies on PIT. It then compares the
 * measured TSC frequency (from either HPET or PIT) with a reference TSC frequency obtained from CPUID.16H (if
 * available). If the difference exceeds 5%, the reference frequency is used as the calibrated value.
 *
 * How to measure TSC frequency using HPET:
 * - Record the TSC value delta and HPET value delta before and after executing pit_calibrate_tsc(). Divide the TSC
 *   delta by the HPET delta to get the frequency.
 *
 * How to measure TSC frequency using PIT:
 * - When HPET is not available, use the 8254 Timer: Select counter 0, perform a read operation through port 40h. Use
 *   a loop to repeatedly read the current value of the PIT counter until it reaches the target value. Calculate the
 *   elapsed TSC value and return.
 *
 * This function does not return a value. The TSC frequency in kHz is recorded in a global variable tsc_khz.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
void calibrate_tsc(void)
{
	uint64_t tsc_hz;

	tsc_hz = native_calculate_tsc_cpuid_0x15();
	if (tsc_hz == 0UL) {
		tsc_hz = pit_hpet_calibrate_tsc(CAL_MS, native_calculate_tsc_cpuid_0x16());
	}
	tsc_khz = (uint32_t)(tsc_hz / 1000UL);
	if (tsc_khz == 0UL) {
		panic("tsc_khz is zero, failed to calibrate TSC frequency.");
	}

	pr_acrnlog("%s: tsc_khz = %u", __func__, tsc_khz);

}

/**
 * @brief Get TSC frequency.
 *
 * This function returns the global variable tsc_khz. tsc_khz is calculated in function calibrate_tsc().
 *
 * @return A uint32_t value indicates TSC value in kHz.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark This function shall be called after calibrate_tsc() has been called once on the bootstrap processor.
 */
uint32_t get_tsc_khz(void)
{
	return tsc_khz;
}

/* external API */

/**
 * @brief Get CPU ticks.
 *
 * This function returns CPU ticks via reading TSC value of the current logical CPU.
 *
 * @return A uint64_t value of CPU ticks.
 *
 * @pre N/A
 *
 * @post N/A
 */
uint64_t cpu_ticks(void)
{
	return rdtsc();
}

/**
 * @brief Get CPU tick rate.
 *
 * This function returns the CPU tick rate in kHz via global variable tsc_khz. tsc_khz is calculated in function
 * calibrate_tsc().
 *
 * @return A uint32_t value indicating CPU tick rate in kHz.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark This function shall be called after calibrate_tsc() has been called once on the bootstrap processor.
 */
uint32_t cpu_tickrate(void)
{
	return tsc_khz;
}

/**
 * @}
 */