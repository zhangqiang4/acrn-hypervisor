/*
* Copyright (C) 2018-2025 Intel Corporation.
*
* SPDX-License-Identifier: BSD-3-Clause
*/

#ifndef VMEXIT_H_
#define VMEXIT_H_

/**
 * @addtogroup vp-base_hv-main
 *
 * @{
 */

/**
 * @file
 * @brief VM exit handling header.
 *
 * This file contains the declarations and inline functions for handling VM exits
 * in the ACRN hypervisor. It defines the structure for VM exit dispatch and provides
 * utility functions to extract information from the exit qualification field.
 *
 */

struct acrn_vcpu;

/**
 * @brief This structure is used to store VM exit handler and qualification information.
 */
struct vm_exit_dispatch {
	/**
	 * @brief A function pointer to the specified VM exit.
	 */
	int32_t (*handler)(struct acrn_vcpu * vcpu);
	/**
	 * @brief Indicating whether the exit qualification information is necessary for the handler.
	 */
	uint32_t need_exit_qualification;
};

int32_t vmexit_handler(struct acrn_vcpu *vcpu);

/**
 * @brief Intercept bit[msb:lsb] of the exit_qual.
 *
 * This function returns a 64 bit value whose bits [msb:lsb] is equal to bits [msb:lsb] of \p exit_qual and
 * all the other bits are 0.
 *
 * @param[in] exit_qual The exit qualification.
 * @param[in] msb The most significant bit of the bit range.
 * @param[in] lsb The least significant bit of the bit range.
 *
 * @return  A 64 bit value whose bits [msb:lsb] is equal to bits [msb:lsb] of exit_qual and all the other
 *          bits are 0.
 *
 * @pre msb >= lsb
 *
 * @post N/A
 */
static inline uint64_t
vm_exit_qualification_bit_mask(uint64_t exit_qual, uint32_t msb, uint32_t lsb)
{
	return (exit_qual &
			(((1UL << (msb + 1U)) - 1UL) - ((1UL << lsb) - 1UL)));
}

/**
 * @brief Get number of control registers using exit qualification field.
 *
 * This function returns the bit[3:0] of Exit Qualification for Control-Register Accesses to get the number of
 * control register. Refer to Table 28-3 Exit Qualification for Control-Register Accesses, Vol. 3, SDM 325462-078
 * for more details.
 *
 * @param[in] exit_qual The exit qualification.
 *
 * @return The bit[3:0] of \p exit_qual.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint64_t vm_exit_cr_access_cr_num(uint64_t exit_qual)
{
	return (vm_exit_qualification_bit_mask(exit_qual, 3U, 0U) >> 0U);
}

/**
 * @brief Get access type using exit qualification field.
 *
 * This function returns the bit[5:4] of Exit Qualification for Control-Register Accesses to get access type.
 * Refer to Table 28-3 Exit Qualification for Control-Register Accesses, Vol. 3, SDM 325462-078 for more details.
 *
 * @param[in] exit_qual The exit qualification.
 *
 * @return The bit[5:4] of \p exit_qual.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint64_t vm_exit_cr_access_type(uint64_t exit_qual)
{
	return (vm_exit_qualification_bit_mask(exit_qual, 5U, 4U) >> 4U);
}

/**
 * @brief Get LMSW operand type using exit qualification field.
 *
 * This function returns the bit[6] of the Exit Qualification for Control-Register Accesses to get the LMSW
 * operand type. Refer to Table 28-3 Exit Qualification for Control-Register Accesses, Vol. 3, SDM 325462-078
 * for more details.
 *
 * @param[in] exit_qual The exit qualification.
 *
 * @return The bit[6] of \p exit_qual.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint64_t vm_exit_cr_access_lmsw_op(uint64_t exit_qual)
{
	return (vm_exit_qualification_bit_mask(exit_qual, 6U, 6U) >> 6U);
}

/**
 * @brief Get general-purpose register index using exit qualification field.
 *
 * This function returns the bit[11:8] of the Exit Qualification for Control-Register Accesses to get
 * the general-purpose register index. Refer to Table 28-3 Exit Qualification for Control-Register
 * Accesses, Vol. 3, SDM 325462-078 for more details.
 *
 * @param[in] exit_qual The exit qualification.
 *
 * @return The bit[11:8] of \p exit_qual.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint64_t vm_exit_cr_access_reg_idx(uint64_t exit_qual)
{
	return (vm_exit_qualification_bit_mask(exit_qual, 11U, 8U) >> 8U);
}

/**
 * @brief Get LMSW source data using exit qualification field.
 *
 * This function returns the bit[31:16] of Exit Qualification for Control-Register Accesses to get the LMSW
 * source data.
 * Refer to Table 28-3 Exit Qualification for Control-Register Accesses, Vol. 3, SDM 325462-078 for more details.
 *
 * @param[in] exit_qual The exit qualification.
 *
 * @return The bit[31:16] of \p exit_qual.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint64_t vm_exit_cr_access_lmsw_src_date(uint64_t exit_qual)
{
	return (vm_exit_qualification_bit_mask(exit_qual, 31U, 16U) >> 16U);
}

/**
 * @brief Get size of I/O instruction using exit qualification field.
 *
 * This function returns the bit[2:0] of Exit Qualification for I/O Instructions to get size of access.
 * Refer to Table 28-5 Exit Qualification for I/O Instructions, Vol. 3, SDM 325462-078 for more details.
 *
 * @param[in] exit_qual The exit qualification.
 *
 * @return The bit[2:0] of \p exit_qual.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint64_t vm_exit_io_instruction_size(uint64_t exit_qual)
{
	return (vm_exit_qualification_bit_mask(exit_qual, 2U, 0U) >> 0U);
}

/**
 * @brief Get direction of the attempted access of I/O instruction using exit qualification field.
 *
 * This function returns the bit[3] of Exit Qualification for I/O Instructions to get direction of the attempted
 * access. Refer to Table 28-5 Exit Qualification for I/O Instructions, Vol. 3, SDM 325462-078 for more details.
 *
 * @param[in] exit_qual The exit qualification.
 *
 * @return The bit[3] of \p exit_qual.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint64_t
vm_exit_io_instruction_access_direction(uint64_t exit_qual)
{
	return (vm_exit_qualification_bit_mask(exit_qual, 3U, 3U) >> 3U);
}

/**
 * @brief Get if this I/O instruction is string using exit qualification field.
 *
 * This function returns the bit[4] of Exit Qualification for I/O Instructions to get if this I/O instruction
 * is string instruction. Refer to Table 28-5 Exit Qualification for I/O Instructions, Vol. 3, SDM 325462-078 for
 * more details.
 *
 * @param[in] exit_qual The exit qualification.
 *
 * @return The bit[4] of \p exit_qual.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint64_t vm_exit_io_instruction_is_string(uint64_t exit_qual)
{
	return (vm_exit_qualification_bit_mask(exit_qual, 4U, 4U) >> 4U);
}

/**
 * @brief Get if this I/O instruction is REP prefixed using exit qualification field.
 *
 * This function returns the bit[5] of Exit Qualification for I/O Instructions to get if this I/O instruction
 * is REP prefixed. Refer to Table 28-5 Exit Qualification for I/O Instructions, Vol. 3, SDM 325462-078 for
 * more details.
 *
 * @param[in] exit_qual The exit qualification.
 *
 * @return The bit[5] of \p exit_qual.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint64_t
vm_exit_io_instruction_is_rep_prefixed(uint64_t exit_qual)
{
	return (vm_exit_qualification_bit_mask(exit_qual, 5U, 5U) >> 5U);
}

/**
 * @brief Get operand encoding for I/O instruction using exit qualification field.
 *
 * This function returns the bit[6] of Exit Qualification for I/O Instructions to get operand encoding.
 * Refer to Table 28-5 Exit Qualification for I/O Instructions, Vol. 3, SDM 325462-078 for more details.
 *
 * @param[in] exit_qual The exit qualification.
 *
 * @return The bit[6] of \p exit_qual.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint64_t
vm_exit_io_instruction_is_operand_encoding(uint64_t exit_qual)
{
	return (vm_exit_qualification_bit_mask(exit_qual, 6U, 6U) >> 6U);
}

/**
 * @brief Get port number for I/O instruction using exit qualification field.
 *
 * This function returns the bit[31:16] of Exit Qualification for I/O instructions to get port number.
 * Refer to Table 28-5 Exit Qualification for I/O Instructions, Vol. 3, SDM 325462-078 for more details.
 *
 * @param[in] exit_qual The exit qualification.
 *
 * @return The bit[31:16] of \p exit_qual.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint64_t vm_exit_io_instruction_port_number(uint64_t exit_qual)
{
	return (vm_exit_qualification_bit_mask(exit_qual, 31U, 16U) >> 16U);
}

#endif /* VMEXIT_H_ */

/**
 * @}
 */