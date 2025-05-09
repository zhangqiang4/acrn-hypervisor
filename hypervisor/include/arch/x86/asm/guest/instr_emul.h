/*-
 * Copyright (c) 2012 NetApp, Inc.
 * Copyright (c) 2017-2025 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef INSTR_EMUL_H
#define INSTR_EMUL_H

#include <types.h>
#include <asm/cpu.h>
#include <asm/guest/guest_memory.h>

/**
 * @addtogroup vp-dm_io-req
 *
 * @{
 */

/**
 * @file
 * @brief Instruction emulation
 *
 * Simulate the execution of machine-level instructions which are trapped by EPT violations
 * or APIC access, etc. This file defines structures and declares functions.
 *
 */

struct acrn_vcpu;

/**
 * @brief Structure representing an instruction emulation operation.
 *
 * This structure is used to define the characteristics of an instruction
 * operation that is being emulated.
 */
 struct instr_emul_vie_op {
	uint8_t		op_type;	/**< Type of operation (e.g., MOV, ADD, etc.). */
	uint16_t	op_flags;	/**< Flags associated with the operation. */
};

#define VIE_PREFIX_SIZE 4U  /**< Maximum size of instruction prefixes. */
#define VIE_INST_SIZE 15U   /**< Maximum size of an instruction. */

/**
 * @brief Structure representing Virtual Instruction Emulation information.
 *
 * This structure is used to store information necessary for emulating
 * instructions in a virtualized environment.
 */
struct instr_emul_vie {
	uint8_t		inst[VIE_INST_SIZE];	/**< Instruction bytes. */
	uint8_t		num_valid;		/**< Size of the instruction in bytes. */
	uint8_t		num_processed;	/**< Number of bytes processed so far. */

	struct acrn_vcpu 	*vcpu;	/**< Pointer to the virtual CPU structure. */
	uint64_t		rip;		/**< Instruction pointer (RIP) value. */

	uint8_t		addrsize:4,		/**< Address sizes. */
			opsize:4;		/**< Operand sizes. */
	uint8_t		rex_w:1,				/**< REX.W prefix. */
			rex_r:1,					/**< REX.R prefix. */
			rex_x:1,					/**< REX.X prefix. */
			rex_b:1,					/**< REX.B prefix. */
			rex_present:1,				/**< REX prefix present. */
			repz_present:1,				/**< REP/REPE/REPZ prefix present. */
			repnz_present:1,			/**< REPNE/REPNZ prefix present. */
			opsize_override:1,			/**< Operand size override present. */
			addrsize_override:1,		/**< Address size override present. */
			seg_override:1;				/**< Segment override present. */

	uint8_t		mod:2,			/**< Mod field of the ModRM byte. */
			reg:4,				/**< Reg field of the ModRM byte. */
			rm:4;				/**< R/M field of the ModRM byte. */

	uint8_t		ss:2,			/**< Scale field in the SIB byte. */
			index:4,			/**< Index field in the SIB byte. */
			base:4;				/**< Base field in the SIB byte. */

	uint8_t		disp_bytes;		/**< Number of displacement bytes. */
	uint8_t		imm_bytes;		/**< Number of immediate bytes. */

	uint8_t		scale;			/**< Scale factor for index calculation. */
	enum cpu_reg_name	base_register;		/**< Base register (CPU_REG_xyz). */
	enum cpu_reg_name	index_register;		/**< Index register (CPU_REG_xyz). */
	enum cpu_reg_name	segment_register;		/**< Segment register (CPU_REG_xyz). */

	int64_t		displacement;		/**< Optional address displacement. */
	int64_t		immediate;			/**< Optional immediate operand. */

	uint8_t		decoded;			/**< Set to 1 if the instruction is successfully decoded. */

	uint8_t		opcode;				/**< Opcode of the instruction. */
	struct instr_emul_vie_op	op;	/**< Opcode description. */

	uint64_t	dst_gpa;	/**< Saved destination operand GPA, only for MOVS. */
	uint64_t	gva;		/**< Saved guest virtual address for instruction emulation. */
};

/**
 * @brief Structure representing the context for instruction emulation.
 *
 * This structure encapsulates the context required for emulating
 * instructions within a virtualized environment.
 */
struct instr_emul_ctxt {
	struct instr_emul_vie	vie;	/**< Virtual Instruction Emulation (VIE) information. */
};

int32_t emulate_instruction(struct acrn_vcpu *vcpu);
int32_t decode_instruction(struct acrn_vcpu *vcpu, bool full_decode);
bool is_current_opcode_xchg(struct acrn_vcpu *vcpu);

/**
 * @}
 */

#endif
