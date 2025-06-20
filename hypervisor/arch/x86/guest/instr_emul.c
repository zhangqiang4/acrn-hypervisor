/*-
 * Copyright (c) 2012 Sandvine, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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
 * or APIC access, etc.
 */

#include <types.h>
#include <errno.h>
#include <asm/guest/instr_emul.h>
#include <asm/vmx.h>
#include <asm/guest/vmcs.h>
#include <asm/mmu.h>
#include <asm/per_cpu.h>
#include <logmsg.h>
#include <asm/guest/virq.h>

#define CPU_REG_FIRST			CPU_REG_RAX	/**< The first register. */
#define CPU_REG_LAST			CPU_REG_GDTR	/**< The last register. */
#define CPU_REG_GENERAL_FIRST		CPU_REG_RAX	/**< The first general-purpose register. */
#define CPU_REG_GENERAL_LAST		CPU_REG_R15	/**< The last general-purpose register. */
#define CPU_REG_NONGENERAL_FIRST	CPU_REG_CR0	/**< The first non-general-purpose register. */
#define CPU_REG_NONGENERAL_LAST		CPU_REG_GDTR	/**< The last non-general-purpose register. */
#define CPU_REG_NATURAL_FIRST		CPU_REG_CR0	/**< The first natural-width register. */
#define CPU_REG_NATURAL_LAST		CPU_REG_RFLAGS	/**< The last natural-width register. */
#define CPU_REG_64BIT_FIRST		CPU_REG_EFER	/**< The first 64-bit register. */
#define CPU_REG_64BIT_LAST		CPU_REG_PDPTE3	/**< The last 64-bit register. */
#define CPU_REG_SEG_FIRST		CPU_REG_ES	/**< The first segment register. */
#define CPU_REG_SEG_LAST		CPU_REG_GS	/**< The last segment register. */

#define	PSL_C		0x00000001U	/**< carry bit */
#define	PSL_PF		0x00000004U	/**< parity bit */
#define	PSL_AF		0x00000010U	/**< bcd carry bit */
#define	PSL_Z		0x00000040U	/**< zero bit */
#define	PSL_N		0x00000080U	/**< negative bit */
#define	PSL_D		0x00000400U	/**< string instruction direction bit */
#define	PSL_V		0x00000800U	/**< overflow bit */
#define	PSL_AC		0x00040000U	/**< alignment checking */

/*
 * Protections are chosen from these bits, or-ed together
 */
#define	PROT_READ	0x01U	/**< pages can be read */
#define	PROT_WRITE	0x02U	/**< pages can be written */

/**
 * @defgroup VIE_OP_TYPE vie_op.op_type
 *
 * Operation types of the Virtual Instruction Emulation.
 *
 * @{
 */
#define VIE_OP_TYPE_NONE	0U  /**< No operation type. */
#define VIE_OP_TYPE_MOV		1U  /**< Move operation. */
#define VIE_OP_TYPE_MOVSX	2U  /**< Move with sign-extension operation. */
#define VIE_OP_TYPE_MOVZX	3U  /**< Move with zero-extension operation. */
#define VIE_OP_TYPE_AND		4U  /**< Logical AND operation. */
#define VIE_OP_TYPE_OR		5U  /**< Logical OR operation. */
#define VIE_OP_TYPE_SUB		6U  /**< Subtract operation. */
#define VIE_OP_TYPE_TWO_BYTE	7U  /**< Two-byte opcode operation. */
#define VIE_OP_TYPE_PUSH		8U  /**< Push operation. */
#define VIE_OP_TYPE_CMP			9U  /**< Compare operation. */
#define VIE_OP_TYPE_POP			10U /**< Pop operation. */
#define VIE_OP_TYPE_MOVS		11U /**< Move string operation. */
#define VIE_OP_TYPE_GROUP1		12U /**< Group 1 operation. */
#define VIE_OP_TYPE_STOS		13U /**< Store string operation. */
#define VIE_OP_TYPE_BITTEST		14U /**< Bit test operation. */
#define VIE_OP_TYPE_TEST		15U /**< Test operation. */
#define VIE_OP_TYPE_XCHG		16U /**< Exchange operation. */
/**
 * @}
 */

/**
 * @defgroup VIE_OP_FLAG vie_op.op_flags
 *
 * The VIE_OP_F_BYTE_OP only set when the instruction support
 * Encoding of Operand Size (w) Bit and the w bit of opcode is 0.
 * according B.2 GENERAL-PURPOSE INSTRUCTION FORMATS AND ENCODINGS
 * FOR NON-64-BIT MODES, Vol 2, Intel SDM.
 *
 * @{
 */
#define VIE_OP_F_IMM		(1U << 0U)  /**< 16/32-bit immediate operand */
#define VIE_OP_F_IMM8		(1U << 1U)  /**< 8-bit immediate operand */
#define VIE_OP_F_MOFFSET	(1U << 2U)  /**< 16/32/64-bit immediate moffset */
#define VIE_OP_F_NO_MODRM	(1U << 3U)	/**< No ModR/M byte */
#define VIE_OP_F_CHECK_GVA_DI	(1U << 4U)  /**< for movs, need to check DI */
#define VIE_OP_F_BYTE_OP	(1U << 5U)  /**< 8-bit operands. */
#define VIE_OP_F_WORD_OP	(1U << 6U)  /**< 16-bit operands. */
/**
 * @}
 */

/**
 * @brief Define two-byte opcode operations which ACRN Hypervisor supports to emulate.
 *
 * This array defines the operation types and flags for specific two-byte opcodes.
 */
static const struct instr_emul_vie_op two_byte_opcodes[256] = {
	[0xB6] = {
		.op_type = VIE_OP_TYPE_MOVZX,
		.op_flags = VIE_OP_F_BYTE_OP,
	},
	[0xB7] = {
		.op_type = VIE_OP_TYPE_MOVZX,
		.op_flags = VIE_OP_F_WORD_OP,
	},
	[0xBA] = {
		.op_type = VIE_OP_TYPE_BITTEST,
		.op_flags = VIE_OP_F_IMM8,
	},
	[0xBE] = {
		.op_type = VIE_OP_TYPE_MOVSX,
		.op_flags = VIE_OP_F_BYTE_OP,
	},
};

/**
 * @brief Define one-byte opcode operations which ACRN Hypervisor supports to emulate.
 *
 * This array defines the operation types and flags for specific one-byte opcodes.
 */
static const struct instr_emul_vie_op one_byte_opcodes[256] = {
	[0x0F] = {
		.op_type = VIE_OP_TYPE_TWO_BYTE
	},
	[0x2B] = {
		.op_type = VIE_OP_TYPE_SUB,
	},
	[0x39] = {
		.op_type = VIE_OP_TYPE_CMP,
	},
	[0x3B] = {
		.op_type = VIE_OP_TYPE_CMP,
	},
	[0x88] = {
		.op_type = VIE_OP_TYPE_MOV,
		.op_flags = VIE_OP_F_BYTE_OP,
	},
	[0x89] = {
		.op_type = VIE_OP_TYPE_MOV,
	},
	[0x8A] = {
		.op_type = VIE_OP_TYPE_MOV,
		.op_flags = VIE_OP_F_BYTE_OP,
	},
	[0x8B] = {
		.op_type = VIE_OP_TYPE_MOV,
	},
	[0xA1] = {
		.op_type = VIE_OP_TYPE_MOV,
		.op_flags = VIE_OP_F_MOFFSET | VIE_OP_F_NO_MODRM,
	},
	[0xA3] = {
		.op_type = VIE_OP_TYPE_MOV,
		.op_flags = VIE_OP_F_MOFFSET | VIE_OP_F_NO_MODRM,
	},
	[0xA4] = {
		.op_type = VIE_OP_TYPE_MOVS,
		.op_flags = VIE_OP_F_NO_MODRM | VIE_OP_F_CHECK_GVA_DI | VIE_OP_F_BYTE_OP,
	},
	[0xA5] = {
		.op_type = VIE_OP_TYPE_MOVS,
		.op_flags = VIE_OP_F_NO_MODRM | VIE_OP_F_CHECK_GVA_DI,
	},
	[0xAA] = {
		.op_type = VIE_OP_TYPE_STOS,
		.op_flags = VIE_OP_F_NO_MODRM | VIE_OP_F_BYTE_OP,
	},
	[0xAB] = {
		.op_type = VIE_OP_TYPE_STOS,
		.op_flags = VIE_OP_F_NO_MODRM
	},
	[0xC6] = {
		/* XXX Group 11 extended opcode - not just MOV */
		.op_type = VIE_OP_TYPE_MOV,
		.op_flags = VIE_OP_F_IMM8 | VIE_OP_F_BYTE_OP,
	},
	[0xC7] = {
		.op_type = VIE_OP_TYPE_MOV,
		.op_flags = VIE_OP_F_IMM,
	},
	[0x23] = {
		.op_type = VIE_OP_TYPE_AND,
	},
	[0x80] = {
		/* Group 1 extended opcode */
		.op_type = VIE_OP_TYPE_GROUP1,
		.op_flags = VIE_OP_F_IMM8,
	},
	[0x81] = {
		/* Group 1 extended opcode */
		.op_type = VIE_OP_TYPE_GROUP1,
		.op_flags = VIE_OP_F_IMM,
	},
	[0x83] = {
		/* Group 1 extended opcode */
		.op_type = VIE_OP_TYPE_GROUP1,
		.op_flags = VIE_OP_F_IMM8,
	},
	[0x84] = {
		.op_type = VIE_OP_TYPE_TEST,
		.op_flags = VIE_OP_F_BYTE_OP,
	},
	[0x85] = {
		.op_type = VIE_OP_TYPE_TEST,
	},
	[0x86] = {
		.op_type = VIE_OP_TYPE_XCHG,
	},
	[0x87] = {
		.op_type = VIE_OP_TYPE_XCHG,
	},
	[0x08] = {
		.op_type = VIE_OP_TYPE_OR,
		.op_flags = VIE_OP_F_BYTE_OP,
	},
	[0x09] = {
		.op_type = VIE_OP_TYPE_OR,
	},
};

/**
 * @brief Indirect addressing mode.
 *
 * This macro represents the indirect addressing mode in the `mod` field in Instruction
 * (refer to Figure 2-1. Intel 64 and IA-32 Architectures Instruction Format, Vol 2, Intel®
 * 64 and IA-32 Architectures Software Developer's Manual Order Number: 325462-078US).
 * In this mode, the effective address is computed using a base register without any displacement.
 */
 #define VIE_MOD_INDIRECT 0U

 /**
  * @brief Indirect addressing with 8-bit displacement.
  *
  * This macro represents the indirect addressing mode with an 8-bit displacement in the `mod` field
  * in Instruction(refer to Figure 2-1. Intel 64 and IA-32 Architectures Instruction Format, Vol 2, Intel®
  * 64 and IA-32 Architectures Software Developer's Manual Order Number: 325462-078US). In this mode, the
  * effective address is computed using a base register and an 8-bit displacement.
  */
 #define VIE_MOD_INDIRECT_DISP8 1U

 /**
  * @brief Indirect addressing with 32-bit displacement.
  *
  * This macro represents the indirect addressing mode with a 32-bit displacement in the `mod` field
  * in Instruction(refer to Figure 2-1. Intel 64 and IA-32 Architectures Instruction Format, Vol 2, Intel®
  * 64 and IA-32 Architectures Software Developer's Manual Order Number: 325462-078US). In this mode, the
  * effective address is computed using a base register and a 32-bit displacement.
  */
 #define VIE_MOD_INDIRECT_DISP32 2U

 /**
  * @brief Direct addressing mode.
  *
  * This macro represents the direct addressing mode in the `mod` field in Instruction(refer to Figure 2-1.
  * Intel 64 and IA-32 Architectures Instruction Format, Vol 2, Intel® 64 and IA-32 Architectures Software
  * Developer's Manual Order Number: 325462-078US). In this mode, of the `vie` structure. In this mode, the
  * effective address is directly specified by the instruction.
  */
 #define VIE_MOD_DIRECT 3U

 /**
  * @brief Scale-Index-Base (SIB) addressing mode.
  *
  * This macro represents the Scale-Index-Base (SIB) addressing mode in the `rm` field in Instruction(refer
  * to Figure 2-1. Intel 64 and IA-32 Architectures Instruction Format, Vol 2, Intel® 64 and IA-32 Architectures
  * Software Developer's Manual Order Number: 325462-078US). This mode allows for complex address calculations
  * using a combination of base, index, and scale factors.
  */
 #define VIE_RM_SIB 4U

 /**
  * @brief 32-bit displacement without base register.
  *
  * This macro represents an addressing mode with a 32-bit displacement and no base register in the `rm` field
  * in Instruction(refer to Figure 2-1. Intel 64 and IA-32 Architectures Instruction Format, Vol 2, Intel® 64 and
  * IA-32 Architectures Software Developer's Manual Order Number: 325462-078US). In this mode, the effective
  * address is computed using only the 32-bit displacement.
  */
 #define VIE_RM_DISP32 5U

/**
 * @brief Define masks for different operand sizes.
 *
 * This array defines bit masks for various operand sizes (1, 2, 4, and 8 bytes).
 */
static uint64_t size2mask[9] = {
	[1] = (1UL << 8U) - 1UL,
	[2] = (1UL << 16U) - 1UL,
	[4] = (1UL << 32U) - 1UL,
	[8] = ~0UL,
};

/**
 * @brief Invalid VMCS field value.
 *
 * This macro defines a constant used to represent an invalid field value
 * in the Virtual Machine Control Structure (VMCS). However, we return this
 * MACRO in one function, but we didn't check the return value.
 */
 #define VMX_INVALID_VMCS_FIELD 0xffffffffU

/**
 * @brief Structure representing VMCS segment fields.
 *
 * This structure is used to hold the VMCS field addresses related to a segment
 * selector in a virtualized environment. It encapsulates the base, limit, and
 * access rights fields of a segment, which are essential for managing segment
 * descriptors in the Virtual Machine Control Structure (VMCS).
 */
struct vmcs_seg_field {
	uint32_t	base_field;   /**< The base address field of the segment. */
	uint32_t	limit_field;  /**< The limit field of the segment, defining its size. */
	uint32_t	access_field; /**< The access rights field, indicating the segment's permissions and type. */
};

/**
 * @brief Structure representing a segment descriptor.
 *
 * This structure is used to define a segment descriptor, which is a data structure
 * used in x86 architecture to define the characteristics of a segment. The segment
 * descriptor includes the base address, limit, and access rights of the segment.
 */
struct seg_desc {
	uint64_t	base;	/**< The base address of the segment. */
	uint32_t	limit;	/**< The limit of the segment, defining its size. */
	uint32_t	access;	/**< Access rights and attributes of the segment. */
};

/**
 * @brief Segment type
 *
 * Get segment type and descriptor type from the access rights field.
 * The access rights field is contained in the guest-state area of the VMCS(refer
 * to Table 25-2. Format of Access Rights, Vol 3, Intel® 64 and IA-32 Architectures
 * Software Developer's Manual Order Number 325462-078US).
 *
 * @param[in] access Value of access rights.
 *
 * @return Segment type.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline uint32_t seg_desc_type(uint32_t access)
{
	return (access & 0x001fU);
}

/**
 * @brief Segment present
 *
 * Get segment present from the access rights field.
 * The access rights field is contained in the guest-state area of the VMCS(refer
 * to Table 25-2. Format of Access Rights, Vol 3, Intel® 64 and IA-32 Architectures
 * Software Developer's Manual Order Number 325462-078US).
 *
 * @param[in] access Value of access rights.
 *
 * @return Segment present value.
 *
 * @pre N/A
 *
 * @post N/A
 */
__unused static inline bool seg_desc_present(uint32_t access)
{
	return ((access & 0x0080U) != 0U);
}

/**
 * @brief Default operation size
 *
 * Get the default operation size from the access rights field.
 * The access rights field is contained in the guest-state area of the VMCS(refer
 * to Table 25-2. Format of Access Rights, Vol 3, Intel® 64 and IA-32 Architectures
 * Software Developer's Manual Order Number 325462-078US).
 *
 * @param[in] access Value of access rights.
 *
 * @return true for 32-bit default, false for 16-bit default.
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline bool seg_desc_def32(uint32_t access)
{
	return ((access & 0x4000U) != 0U);
}

/**
 * @brief Segment unusable
 *
 * Get the segment unusable value from the access rights field.
 * The access rights field is contained in the guest-state area of the VMCS(refer
 * to Table 25-2. Format of Access Rights, Vol 3, Intel® 64 and IA-32 Architectures
 * Software Developer's Manual Order Number 325462-078US).
 *
 * @param[in] access Value of access rights.
 *
 * @return true if unusable, false if usable.
 *
 * @pre N/A
 *
 * @post N/A
 */
__unused static inline bool seg_desc_unusable(uint32_t access)
{
	return ((access & 0x10000U) != 0U);
}

/**
 * @brief Get segment descriptor field register VMCS address for a Segment.
 *
 * Get the segment register field address. These fields lie in the guest-state area of the VMCS. If the
 * segment register is not in VMCS, all fields set to '0'.
 *
 * @param[in] seg Segment register name to read.
 * @param[out] desc Pointer to the segment field description.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
static void encode_vmcs_seg_desc(enum cpu_reg_name seg,
	struct vmcs_seg_field *desc)
{
	switch (seg) {
	case CPU_REG_ES:
		desc->base_field = VMX_GUEST_ES_BASE;
		desc->limit_field = VMX_GUEST_ES_LIMIT;
		desc->access_field = VMX_GUEST_ES_ATTR;
		break;
	case CPU_REG_CS:
		desc->base_field = VMX_GUEST_CS_BASE;
		desc->limit_field = VMX_GUEST_CS_LIMIT;
		desc->access_field = VMX_GUEST_CS_ATTR;
		break;
	case CPU_REG_SS:
		desc->base_field = VMX_GUEST_SS_BASE;
		desc->limit_field = VMX_GUEST_SS_LIMIT;
		desc->access_field = VMX_GUEST_SS_ATTR;
		break;
	case CPU_REG_DS:
		desc->base_field = VMX_GUEST_DS_BASE;
		desc->limit_field = VMX_GUEST_DS_LIMIT;
		desc->access_field = VMX_GUEST_DS_ATTR;
		break;
	case CPU_REG_FS:
		desc->base_field = VMX_GUEST_FS_BASE;
		desc->limit_field = VMX_GUEST_FS_LIMIT;
		desc->access_field = VMX_GUEST_FS_ATTR;
		break;
	case CPU_REG_GS:
		desc->base_field = VMX_GUEST_GS_BASE;
		desc->limit_field = VMX_GUEST_GS_LIMIT;
		desc->access_field = VMX_GUEST_GS_ATTR;
		break;
	case CPU_REG_TR:
		desc->base_field = VMX_GUEST_TR_BASE;
		desc->limit_field = VMX_GUEST_TR_LIMIT;
		desc->access_field = VMX_GUEST_TR_ATTR;
		break;
	case CPU_REG_LDTR:
		desc->base_field = VMX_GUEST_LDTR_BASE;
		desc->limit_field = VMX_GUEST_LDTR_LIMIT;
		desc->access_field = VMX_GUEST_LDTR_ATTR;
		break;
	case CPU_REG_IDTR:
		desc->base_field = VMX_GUEST_IDTR_BASE;
		desc->limit_field = VMX_GUEST_IDTR_LIMIT;
		desc->access_field = 0xffffffffU;
		break;
	case CPU_REG_GDTR:
		desc->base_field = VMX_GUEST_GDTR_BASE;
		desc->limit_field = VMX_GUEST_GDTR_LIMIT;
		desc->access_field = 0xffffffffU;
		break;
	default:
		desc->base_field = 0U;
		desc->limit_field = 0U;
		desc->access_field = 0U;
		pr_err("%s: invalid seg %d", __func__, seg);
		break;
	}
}

/**
 * @brief Convert register name to field index in VMCS
 *
 * This local function is to convert register names into the corresponding field index MACROs in VMCS.
 *
 * @param[in] ident Register name to convert.
 *
 * @return For register names CPU_REG_CR2, CPU_REG_IDTR, and CPU_REG_GDTR, this function returns VMX_INVALID_VMCS_FIELD.
 * @return For other register names, it returns the corresponding field index MACROs in VMCS.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark \p ident is in the non-general register names group (CPU_REG_CR0~CPU_REG_GDTR).
 */
static uint32_t get_vmcs_field(enum cpu_reg_name ident)
{
	uint32_t ret;

	switch (ident) {
	case CPU_REG_CR0:
		ret = VMX_GUEST_CR0;
		break;
	case CPU_REG_CR3:
		ret = VMX_GUEST_CR3;
		break;
	case CPU_REG_CR4:
		ret = VMX_GUEST_CR4;
		break;
	case CPU_REG_DR7:
		ret = VMX_GUEST_DR7;
		break;
	case CPU_REG_RSP:
		ret = VMX_GUEST_RSP;
		break;
	case CPU_REG_RIP:
		ret = VMX_GUEST_RIP;
		break;
	case CPU_REG_RFLAGS:
		ret = VMX_GUEST_RFLAGS;
		break;
	case CPU_REG_ES:
		ret = VMX_GUEST_ES_SEL;
		break;
	case CPU_REG_CS:
		ret = VMX_GUEST_CS_SEL;
		break;
	case CPU_REG_SS:
		ret = VMX_GUEST_SS_SEL;
		break;
	case CPU_REG_DS:
		ret = VMX_GUEST_DS_SEL;
		break;
	case CPU_REG_FS:
		ret = VMX_GUEST_FS_SEL;
		break;
	case CPU_REG_GS:
		ret = VMX_GUEST_GS_SEL;
		break;
	case CPU_REG_TR:
		ret = VMX_GUEST_TR_SEL;
		break;
	case CPU_REG_LDTR:
		ret = VMX_GUEST_LDTR_SEL;
		break;
	case CPU_REG_EFER:
		ret = VMX_GUEST_IA32_EFER_FULL;
		break;
	case CPU_REG_PDPTE0:
		ret = VMX_GUEST_PDPTE0_FULL;
		break;
	case CPU_REG_PDPTE1:
		ret = VMX_GUEST_PDPTE1_FULL;
		break;
	case CPU_REG_PDPTE2:
		ret = VMX_GUEST_PDPTE2_FULL;
		break;
	case CPU_REG_PDPTE3:
		ret = VMX_GUEST_PDPTE3_FULL;
		break;
	default: /* Never get here */
		ret = VMX_INVALID_VMCS_FIELD;
		break;
	}

	return ret;
}

/**
 * @brief Read register.
 *
 * Read register values.
 * For general registers, the value is read from the vCPU's \p run_context, which is saved when the VM traps.
 * For non-general registers, the value is read from the guest-state area of the VMCS.
 *
 * @param[in] vcpu Pointer to the vCPU that processes the instruction.
 * @param[in] reg Register name of the vCPU.
 *
 * @return Register value, 0 on unsupported.
 *
 * @pre ((reg <= CPU_REG_LAST) && (reg >= CPU_REG_FIRST))
 * @pre ((reg != CPU_REG_CR2) && (reg != CPU_REG_IDTR) && (reg != CPU_REG_GDTR))
 *
 * @post N/A
 */
static uint64_t vm_get_register(const struct acrn_vcpu *vcpu, enum cpu_reg_name reg)
{
	uint64_t reg_val = 0UL;

	if (reg <= CPU_REG_GENERAL_LAST) {
		reg_val = vcpu_get_gpreg(vcpu, reg);
	} else if (reg <= CPU_REG_NONGENERAL_LAST) {
		uint32_t field = get_vmcs_field(reg);

		if (reg <= CPU_REG_NATURAL_LAST) {
			reg_val = exec_vmread(field);
		} else if (reg <= CPU_REG_64BIT_LAST) {
			reg_val = exec_vmread64(field);
		} else {
			reg_val = (uint64_t)exec_vmread16(field);
		}
	} else {
		/* No other state currently, do nothing */
	}

	return reg_val;
}

/**
 * @brief Set register.
 *
 * Set register values.
 * For general registers, the value is set to the vCPU's \p run_context, which will be restored
 * when the VM enters. For non-general registers, the value is set to the guest-state area of
 * the VMCS.
 *
 * @param[inout] vcpu Pointer to the vCPU that processes the instruction.
 * @param[in] reg Register name of the vCPU.
 * @param[in] val Value to set.
 *
 * @return None.
 *
 * @pre ((reg <= CPU_REG_LAST) && (reg >= CPU_REG_FIRST))
 * @pre ((reg != CPU_REG_CR2) && (reg != CPU_REG_IDTR) && (reg != CPU_REG_GDTR))
 *
 * @post N/A
 */
static void vm_set_register(struct acrn_vcpu *vcpu, enum cpu_reg_name reg,
								uint64_t val)
{

	if (reg <= CPU_REG_GENERAL_LAST) {
		vcpu_set_gpreg(vcpu, reg, val);
	} else if ((reg >= CPU_REG_NONGENERAL_FIRST) &&
		(reg <= CPU_REG_NONGENERAL_LAST)) {
		uint32_t field = get_vmcs_field(reg);

		if (reg <= CPU_REG_NATURAL_LAST) {
			exec_vmwrite(field, val);
		} else if (reg <= CPU_REG_64BIT_LAST) {
			exec_vmwrite64(field, val);
		} else {
			exec_vmwrite16(field, (uint16_t)val);
		}
	} else {
		/* No other state currently, do nothing */
	}
}

/**
 * @brief Get segment descriptor field value from VMCS for a Segment
 *
 * Read segment register fields. These fields lie in the guest-state area of the VMCS.
 * This information is used to calculate the linear address in the guest VM.
 *
 * @param[in] seg Segment register name to read.
 * @param[out] desc Description pointer for the segment state.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark seg must be one of the segment registers (CPU_REG_CS/ES/DS/SS/FS/GS)
 *	  or CPU_REG_TR/LDTR
 */
static void vm_get_seg_desc(enum cpu_reg_name seg, struct seg_desc *desc)
{
	struct vmcs_seg_field tdesc;

	/* tdesc->access != 0xffffffffU in this function */
	encode_vmcs_seg_desc(seg, &tdesc);

	desc->base = exec_vmread(tdesc.base_field);
	desc->limit = exec_vmread32(tdesc.limit_field);
	desc->access = exec_vmread32(tdesc.access_field);
}

/**
 * @brief Canonical check
 *
 * In 64-bit mode, check if the memory address is in a non-canonical form.
 * For IA-32 processors with Intel 64 architecture, which supports a 48-bit linear address, a canonical address
 * must have bits 63 through 48 set to zeros or ones (depending on whether bit 47 is a zero or one).
 *
 * @param[in] cpu_mode vCPU's CPU mode.
 * @param[in] gla Guest linear address.
 *
 * @return 0 on canonical, 1 on non-canonical.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark In a non-canonical form, inject an exception (SS or GP) to the guest VM.
 */
static int32_t vie_canonical_check(enum vm_cpu_mode cpu_mode, uint64_t gla)
{
	int32_t ret = 0;
	uint64_t mask;

	if (cpu_mode == CPU_MODE_64BIT) {
		/*
		 * The value of the bit 47 in the 'gla' should be replicated in the
		 * most significant 16 bits.
		 */
		mask = ~((1UL << 48U) - 1UL);
		if ((gla & (1UL << 47U)) != 0U) {
			ret = ((gla & mask) != mask) ? 1 : 0;
		} else {
			ret = ((gla & mask) != 0U) ? 1 : 0;
		}
	}

	return ret;
}

/**
 * @brief Check permissions for a segment is valid
 *
 * Check the segment access rights descriptor in the Guest-state area.
 * Determine whether it's a valid access rights type and if R/W access right is available.
 *
 * @param[in] desc Pointer to the segment descriptor.
 * @param[in] prot Requested protection bits.
 *
 * @return True on valid, false on invalid.
 *
 * @pre N/A
 *
 * @post N/A
 */
static bool is_desc_valid(const struct seg_desc *desc, uint32_t prot)
{
	bool ret = true;
	uint32_t type;

	/* The descriptor type must indicate a code/data segment. */
	type = seg_desc_type(desc->access);

	if (type < 16U) {
		ret = false;
	} else {
		if ((prot & PROT_READ) != 0U) {
			/* #GP on a read access to a exec-only code segment */
			if ((type & 0xAU) == 0x8U) {
				ret = false;
			}
		}

		if ((prot & PROT_WRITE) != 0U) {
			/*
			 * #GP on a write access to a code segment or a
			 * read-only data segment.
			 */
			if ((type & 0x8U) != 0U) {	/* code segment */
				ret = false;
			}

			if ((type & 0xAU) == 0U) {	/* read-only data seg */
				ret = false;
			}
		}
	}

	return ret;
}


/**
 * @brief Calculate guest linear address
 *
 * Calculate the guest linear address by adding the offset to the segment base address
 * in the register.
 *
 * @param[in] cpu_mode vCPU's CPU mode.
 * @param[in] seg Segment register index.
 * @param[in] desc Description of the segment register.
 * @param[in] offset Linear address offset from the segment base.
 * @param[in] addrsize Address size.
 * @param[out] gla Pointer to return the guest linear address.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark seg must be a segment register index.
 * @remark addrsize must be 1, 2, 4, or 8.
 * @remark prot must be PROT_READ or PROT_WRITE.
 */
static void vie_calculate_gla(enum vm_cpu_mode cpu_mode, enum cpu_reg_name seg,
	const struct seg_desc *desc, uint64_t offset, uint8_t addrsize, uint64_t *gla)
{
	uint64_t firstoff, segbase;
	uint8_t glasize;

	firstoff = offset;
	glasize = (cpu_mode == CPU_MODE_64BIT) ? 8U : 4U;

	/*
	 * In 64-bit mode all segments except %fs and %gs have a segment
	 * base address of 0.
	 */
	if ((cpu_mode == CPU_MODE_64BIT) && (seg != CPU_REG_FS) &&
		(seg != CPU_REG_GS)) {
		segbase = 0UL;
	} else {
		segbase = desc->base;
	}

	/*
	 * Truncate 'firstoff' to the effective address size before adding
	 * it to the segment base.
	 */
	firstoff &= size2mask[addrsize];
	*gla = (segbase + firstoff) & size2mask[glasize];
}

/**
 * @brief Get mmio_request value
 *
 * Get the value of mmio_request.
 *
 * @param[in] vcpu Pointer to the vCPU that processes the instruction.
 * @param[out] rval Pointer to return the mmio_request value.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline void vie_mmio_read(const struct acrn_vcpu *vcpu, uint64_t *rval)
{
	*rval = vcpu->req.reqs.mmio_request.value;
}

/**
 * @brief Write mmio_request value
 *
 * Write a value to mmio_request.
 *
 * @param[inout] vcpu Pointer to the vCPU that processes the instruction.
 * @param[in] wval Value to write to mmio_request.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
static inline void vie_mmio_write(struct acrn_vcpu *vcpu, uint64_t wval)
{
	vcpu->req.reqs.mmio_request.value = wval;
}

/**
 * @brief Calculate byte register index.
 *
 * Calculate byte register index(AL, CL, DL, DL, AH, CH, DH, or BH refers to Figure 3-5. Alternate General-Purpose
 * Register Names, Vol 1, Intel® 64 and IA-32 Architectures Software Developer's Manual Order Number: 325462-078US).
 * For 16-bit mode, refers to Table 2-1. 16-Bit Addressing Forms with the ModR/M Byte (r8(/r) and (In binary) REG =),
 * Vol 2, Intel® 64 and IA-32 Architectures Software Developer's Manual Order Number: 325462-087US. Use Bits 0-1 of
 * \p REG as the Register Index and Bit 2 of \p REG as the indicator for legacy high byte registers (lhbr).
 * For 32-bit mode, refers to Table 2-2. 32-Bit Addressing Forms with the ModR/M Byte (r8(/r) and (In binary) REG =),
 * Vol 2, Intel® 64 and IA-32 Architectures Software Developer's Manual Order Number: 325462-087US. Use Bits 0-1 of
 * \p REG as the Register Index and Bit 2 of \p REG as the indicator for legacy high byte registers (lhbr).
 * For 64-bit mode, With a REX prefix in 64-bit mode, attempts to access AH, BH, CH, or DH will instead access SPL,
 * DIL, BPL, or SIL, respectively. Just use Bits 0-2 of \p REG as the Register Index.
 *
 * @param[in] vie Pointer to instruction emulation information.
 * @param[out] reg Pointer to return the register index.
 * @param[out] lhbr Pointer to mask the legacy high byte register.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
static void vie_calc_bytereg(const struct instr_emul_vie *vie,
					enum cpu_reg_name *reg, int32_t *lhbr)
{
	*lhbr = 0;
	*reg = (enum cpu_reg_name)(vie->reg);

	/*
	 * 64-bit mode imposes limitations on accessing legacy high byte
	 * registers (lhbr).
	 *
	 * The legacy high-byte registers cannot be addressed if the REX
	 * prefix is present. In this case the values 4, 5, 6 and 7 of the
	 * 'ModRM:reg' field address %spl, %bpl, %sil and %dil respectively.
	 *
	 * If the REX prefix is not present then the values 4, 5, 6 and 7
	 * of the 'ModRM:reg' field address the legacy high-byte registers,
	 * %ah, %ch, %dh and %bh respectively.
	 */
	if (vie->rex_present == 0U) {
		if ((vie->reg & 0x4U) != 0U) {
			*lhbr = 1;
			*reg = (enum cpu_reg_name)(vie->reg & 0x3U);
		}
	}
}

/**
 * @brief Read byte register.
 *
 * Read a byte from byte register.(AL, CL, DL, DL, AH, CH, DH, or BH refers to Figure 3-5. Alternate
 * General-Purpose Register Names, Vol 1, Intel® 64 and IA-32 Architectures Software Developer's Manual
 * Order Number: 325462-078US)
 * For legacy high byte registers (lhbr) (AH, CH, DH, or BH), shift the base register right by 8 bits.
 * Otherwise, return the value directly.
 *
 * @param[in] vcpu Pointer to the vCPU that processes the instruction.
 * @param[in] vie Pointer to instruction emulation information.
 *
 * @return The value read from the register.
 *
 * @pre N/A
 *
 * @post N/A
 */
static uint8_t vie_read_bytereg(const struct acrn_vcpu *vcpu, const struct instr_emul_vie *vie)
{
	int32_t lhbr;
	uint64_t val;
	uint8_t reg_val;
	enum cpu_reg_name reg;

	vie_calc_bytereg(vie, &reg, &lhbr);
	val = vm_get_register(vcpu, reg);

	/*
	 * To obtain the value of a legacy high byte register shift the
	 * base register right by 8 bits (%ah = %rax >> 8).
	 */
	if (lhbr != 0) {
		reg_val = (uint8_t)(val >> 8U);
	} else {
		reg_val = (uint8_t)val;
	}

	return reg_val;
}

/**
 * @brief Write a byte to a byte register.
 *
 * Write a byte to byte register.(AL, CL, DL, DL, AH, CH, DH, or BH refers to Figure 3-5. Alternate
 * General-Purpose Register Names, Vol 1, Intel® 64 and IA-32 Architectures Software Developer's Manual
 * Order Number: 325462-078US)
 * For legacy high byte registers (lhbr) (AH, CH, DH, or BH), shift left by 8 bits to store 'byte' in high-byte
 * register. Otherwise, write the value directly.
 *
 * @param[inout] vcpu Pointer to the vCPU that processes the instruction.
 * @param[in] vie Pointer to instruction emulation information.
 * @param[in] byte The value to write.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
static void vie_write_bytereg(struct acrn_vcpu *vcpu, const struct instr_emul_vie *vie,
								uint8_t byte)
{
	uint64_t origval, val, mask;
	enum cpu_reg_name reg;
	int32_t lhbr;

	vie_calc_bytereg(vie, &reg, &lhbr);
	origval = vm_get_register(vcpu, reg);

	val = byte;
	mask = 0xffU;
	if (lhbr != 0) {
		/*
		 * Shift left by 8 to store 'byte' in a legacy high
		 * byte register.
		 */
		val <<= 8U;
		mask <<= 8U;
	}
	val |= origval & ~mask;
	vm_set_register(vcpu, reg, val);
}

/**
 * @brief Update register.
 *
 * Update the register values.
 * For general registers, the value is updated to the vCPU's \p run_context, which will be restored
 * when the VM enters. For non-general registers, the value is updated to the guest-state area of
 * the VMCS.
 *
 * @param[inout] vcpu Pointer to the vCPU that processes the instruction.
 * @param[in] reg Register name of the vCPU.
 * @param[in] val_arg Value to set.
 * @param[in] size Register size (bytes).
 *
 * @return None.
 *
 * @pre size == 1 || size == 2 || size == 4 || size == 8
 * @pre ((reg <= CPU_REG_LAST) && (reg >= CPU_REG_FIRST))
 * @pre ((reg != CPU_REG_CR2) && (reg != CPU_REG_IDTR) && (reg != CPU_REG_GDTR))
 *
 * @post N/A
 */
static void vie_update_register(struct acrn_vcpu *vcpu, enum cpu_reg_name reg,
					uint64_t val_arg, uint8_t size)
{
	uint64_t origval;
	uint64_t val = val_arg;

	switch (size) {
	case 1U:
	case 2U:
		origval = vm_get_register(vcpu, reg);
		val &= size2mask[size];
		val |= origval & ~size2mask[size];
		break;
	case 4U:
		val &= 0xffffffffUL;
		break;
	default: /* size == 8 */
		break;
	}

	vm_set_register(vcpu, reg, val);
}

/**
 * @brief Mask for status bits in the RFLAGS register.
 *
 * This macro defines a bitmask for the status flags within the RFLAGS
 * register of the x86 architecture. The status flags include:
 * - PSL_C: Carry Flag
 * - PSL_PF: Parity Flag
 * - PSL_AF: Auxiliary Carry Flag
 * - PSL_Z: Zero Flag
 * - PSL_N: Sign Flag
 * - PSL_V: Overflow Flag
 */
#define	RFLAGS_STATUS_BITS	(PSL_C | PSL_PF | PSL_AF | PSL_Z | PSL_N | PSL_V)

/**
 * @brief Update RFLAGS
 *
 * Set the specific status bits in RFLAGS, which is in the vCPU's \p run_context.
 *
 * @param[inout] vcpu Pointer to the vCPU that processes the instruction.
 * @param[in] rflags2 Value to update.
 * @param[in] psl Specific bits mask for rflags.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
static void vie_update_rflags(struct acrn_vcpu *vcpu, uint64_t rflags2, uint64_t psl)
{
	uint8_t size;
	uint64_t rflags;

	rflags = vm_get_register(vcpu, CPU_REG_RFLAGS);

	rflags &= ~RFLAGS_STATUS_BITS;
	rflags |= rflags2 & psl;
	size = 8U;

	vie_update_register(vcpu, CPU_REG_RFLAGS, rflags, size);
}

/**
 * @brief Template for getting status flags.
 *
 * This macro serves as a template to obtain RFLAGS by performing (x - y).
 *
 * The following functions are included:
 * - build_getcc(getcc8, uint8_t)
 * - build_getcc(getcc16, uint16_t)
 * - build_getcc(getcc32, uint32_t)
 * - build_getcc(getcc64, uint64_t)
 *
 * @param[in] name The name of the function.
 * @param[in] type The C type of the variable (uint8_t, uint16_t, uint32_t, uint64_t).
 *
 * @return RFLAGS content.
 *
 * @pre N/A
 *
 * @post N/A
 */
#define build_getcc(name, type)					\
static uint64_t name(type x, type y)				\
{								\
	uint64_t rflags;					\
								\
	__asm __volatile("sub %2,%1; pushfq; popq %0" :		\
			"=r" (rflags), "+r" (x) : "m" (y));	\
	return rflags;						\
}

/**
 * @brief Get RFLAGS for 8-bit operands.
 *
 * Perform a subtraction operation to 8-bit operands and return the RFLAGS.
 *
 * @param[in] x Operand which is the minuend.
 * @param[in] y Operand which is the subtrahend.
 *
 * @return RFLAGS content.
 *
 * @pre N/A
 *
 * @post N/A
 */
build_getcc(getcc8, uint8_t)

/**
 * @brief Get RFLAGS for 16-bit operands.
 *
 * Perform a subtraction operation to 16-bit operands and return the RFLAGS.
 *
 * @param[in] x Operand which is the minuend.
 * @param[in] y Operand which is the subtrahend.
 *
 * @return RFLAGS content.
 *
 * @pre N/A
 *
 * @post N/A
 */
build_getcc(getcc16, uint16_t)

/**
 * @brief Get RFLAGS for 32-bit operands.
 *
 * Perform a subtraction operation to 32-bit operands and return the RFLAGS.
 *
 * @param[in] x Operand which is the minuend.
 * @param[in] y Operand which is the subtrahend.
 *
 * @return RFLAGS content.
 *
 * @pre N/A
 *
 * @post N/A
 */
build_getcc(getcc32, uint32_t)

/**
 * @brief Get RFLAGS for 64-bit operands.
 *
 * Perform a subtraction operation to 64-bit operands and return the RFLAGS.
 *
 * @param[in] x Operand which is the minuend.
 * @param[in] y Operand which is the subtrahend.
 *
 * @return RFLAGS content.
 *
 * @pre N/A
 *
 * @post N/A
 */
build_getcc(getcc64, uint64_t)

/**
 * @brief Get RFLAGS.
 *
 * Perform a subtraction operation and return the RFLAGS.
 *
 * @param[in] opsize Size of operands.
 * @param[in] x Operand which is the minuend.
 * @param[in] y Operand which is the subtrahend.
 *
 * @return RFLAGS content.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark opsize = 1, 2, 4, or 8
 */
static uint64_t getcc(uint8_t opsize, uint64_t x, uint64_t y)
{
	uint64_t rflags;
	switch (opsize) {
	case 1U:
		rflags = getcc8((uint8_t) x, (uint8_t) y);
		break;
	case 2U:
		rflags = getcc16((uint16_t) x, (uint16_t) y);
		break;
	case 4U:
		rflags = getcc32((uint32_t) x, (uint32_t) y);
		break;
	default:	/* opsize == 8 */
		rflags = getcc64(x, y);
		break;
	}

	return rflags;
}

/**
 * @brief Emulate MOV instruction.
 *
 * Emulate Move instruction (Refer to MOV-Move, in 4.3 Instructions, Vol 2, Intel® 64 and IA-32
 * Architectures Software Developer's Manual Order Number: 325462-078US). In this function, copies the
 * source operand to the destination operand.
 *
 * @param[in] vie Pointer to instruction emulation information.
 * @param[inout] vcpu Pointer to the vCPU that processes the instruction.
 *
 * @return 0 on success, -EINVAL for unsupported opcodes.
 *
 * @pre N/A
 *
 * @post N/A
 */
static int32_t emulate_mov(struct acrn_vcpu *vcpu, const struct instr_emul_vie *vie)
{
	int32_t error;
	uint8_t size;
	enum cpu_reg_name reg;
	uint8_t byte;
	uint64_t val;

	size = vie->opsize;
	error = 0;
	switch (vie->opcode) {
	case 0x88U:
	/*
	 * MOV byte from reg (ModRM:reg) to mem (ModRM:r/m)
	 * 88/r:	mov r/m8, r8
	 * REX + 88/r:	mov r/m8, r8 (%ah, %ch, %dh, %bh not available)
	 */
		byte = vie_read_bytereg(vcpu, vie);
		vie_mmio_write(vcpu, byte);
		break;
	case 0x89U:
		/*
		 * MOV from reg (ModRM:reg) to mem (ModRM:r/m)
		 * 89/r:	mov r/m16, r16
		 * 89/r:	mov r/m32, r32
		 * REX.W + 89/r	mov r/m64, r64
		 */

		reg = (enum cpu_reg_name)(vie->reg);
		val = vm_get_register(vcpu, reg);
		val &= size2mask[size];
		vie_mmio_write(vcpu, val);
		break;
	case 0x8AU:
		/*
		 * MOV byte from mem (ModRM:r/m) to reg (ModRM:reg)
		 * 8A/r:	mov r8, r/m8
		 * REX + 8A/r:	mov r8, r/m8
		 */
		vie_mmio_read(vcpu, &val);
		vie_write_bytereg(vcpu, vie, (uint8_t)val);
		break;
	case 0x8BU:
		/*
		 * MOV from mem (ModRM:r/m) to reg (ModRM:reg)
		 * 8B/r:	mov r16, r/m16
		 * 8B/r:	mov r32, r/m32
		 * REX.W 8B/r:	mov r64, r/m64
		 */
		vie_mmio_read(vcpu, &val);
		reg = (enum cpu_reg_name)(vie->reg);
		vie_update_register(vcpu, reg, val, size);
		break;
	case 0xA1U:
		/*
		 * MOV from seg:moffset to AX/EAX/RAX
		 * A1:		mov AX, moffs16
		 * A1:		mov EAX, moffs32
		 * REX.W + A1:	mov RAX, moffs64
		 */
		vie_mmio_read(vcpu, &val);
		reg = CPU_REG_RAX;
		vie_update_register(vcpu, reg, val, size);
		break;
	case 0xA3U:
		/*
		 * MOV from AX/EAX/RAX to seg:moffset
		 * A3:		mov moffs16, AX
		 * A3:		mov moffs32, EAX
		 * REX.W + A3:	mov moffs64, RAX
		 */
		val = vm_get_register(vcpu, CPU_REG_RAX);
		val &= size2mask[size];
		vie_mmio_write(vcpu, val);
		break;
	case 0xC6U:
		/*
		 * MOV from imm8 to mem (ModRM:r/m)
		 * C6/0		mov r/m8, imm8
		 * REX + C6/0	mov r/m8, imm8
		 */
		vie_mmio_write(vcpu, (uint64_t)vie->immediate);
		break;
	case 0xC7U:
		/*
		 * MOV from imm16/imm32 to mem (ModRM:r/m)
		 * C7/0		mov r/m16, imm16
		 * C7/0		mov r/m32, imm32
		 * REX.W + C7/0	mov r/m64, imm32
		 *		(sign-extended to 64-bits)
		 */
		val = (uint64_t)(vie->immediate) & size2mask[size];
		vie_mmio_write(vcpu, val);
		break;
	default:
		/*
		 * For the opcode that is not handled (an invalid opcode), the
		 * error code is assigned to a default value (-EINVAL).
		 * Gracefully return this error code if prior case clauses have
		 * not been met.
		 */
		error = -EINVAL;
		break;
	}

	return error;
}

/**
 * @brief Emulate MOV With Extend instruction.
 *
 * Emulate Move With Extend(Refer to MOVZX-Move With Zero-Extend, MOVSX/MOVSXD-Move With
 * Sign-Extension, in 4.3 Instructions, Vol 2, Intel® 64 and IA-32 Architectures Software Developer's
 * Manual Order Number: 325462-078US). 
 * In this function, copies the source operand to the destination operand and extends the value.
 * The size of the converted value depends on the operand-size attribute.
 * - For MOVZX, zero extends the value.
 * - For MOVSX, sign extends the value.
 *
 * @param[in] vie Pointer to instruction emulation information.
 * @param[in] vcpu Pointer to the vCPU that processes the instruction.
 *
 * @return 0 on success, -EINVAL for unsupported opcodes.
 *
 * @pre N/A
 *
 * @post N/A
 */
static int32_t emulate_movx(struct acrn_vcpu *vcpu, const struct instr_emul_vie *vie)
{
	int32_t error;
	uint8_t size;
	enum cpu_reg_name reg;
	uint64_t val;

	size = vie->opsize;
	error = 0;

	switch (vie->opcode) {
	case 0xB6U:
		/*
		 * MOV and zero extend byte from mem (ModRM:r/m) to
		 * reg (ModRM:reg).
		 *
		 * 0F B6/r		movzx r16, r/m8
		 * 0F B6/r		movzx r32, r/m8
		 * REX.W + 0F B6/r	movzx r64, r/m8
		 */

		/* get the first operand */
		vie_mmio_read(vcpu, &val);

		/* get the second operand */
		reg = (enum cpu_reg_name)(vie->reg);

		/* zero-extend byte */
		val = (uint8_t)val;

		/* write the result */
		vie_update_register(vcpu, reg, val, size);
		break;
	case 0xB7U:
		/*
		 * MOV and zero extend word from mem (ModRM:r/m) to
		 * reg (ModRM:reg).
		 *
		 * 0F B7/r		movzx r32, r/m16
		 * REX.W + 0F B7/r	movzx r64, r/m16
		 */
		vie_mmio_read(vcpu, &val);

		reg = (enum cpu_reg_name)(vie->reg);

		/* zero-extend word */
		val = (uint16_t)val;

		vie_update_register(vcpu, reg, val, size);
		break;
	case 0xBEU:
		/*
		 * MOV and sign extend byte from mem (ModRM:r/m) to
		 * reg (ModRM:reg).
		 *
		 * 0F BE/r		movsx r16, r/m8
		 * 0F BE/r		movsx r32, r/m8
		 * REX.W + 0F BE/r	movsx r64, r/m8
		 */

		/* get the first operand */
		vie_mmio_read(vcpu, &val);

		/* get the second operand */
		reg = (enum cpu_reg_name)(vie->reg);

		/* sign extend byte */
		val = (int8_t)val;

		/* write the result */
		vie_update_register(vcpu, reg, val, size);
		break;
	default:
		/*
		 * For the opcode that is not handled (an invalid opcode), the
		 * error code is assigned to a default value (-EINVAL).
		 * Gracefully return this error code if prior case clauses have
		 * not been met.
		 */
		error = -EINVAL;
		break;
	}
	return error;
}

/**
 * @brief Get GVA from RSI without check.
 *
 * Get the GVA from the segment without any check. In this function, base address is got from the segment
 * register, and linear address offset is got from RSI register saved in vCPU's \p run_context. Then call
 * \p vie_calculate_gla to calculate the guest linear address by adding the offset to the segment base address
 * in the register.
 *
 * This function is only used by MOVS during instruction emulation to get GVA from RSI without check.
 * This check was done during instruction decode.
 *
 * @param[in] vcpu Pointer to the vCPU that processes the instruction.
 * @param[in] addrsize Address size.
 * @param[in] seg Segment register name.
 * @param[out] gva Pointer to return the guest virtual address.
 *
 * @return None.
 *
 * @pre N/A
 *
 * @post N/A
 */
static void get_gva_si_nocheck(const struct acrn_vcpu *vcpu, uint8_t addrsize,
		enum cpu_reg_name seg, uint64_t *gva)
{
	uint64_t val;
	struct seg_desc desc;
	enum vm_cpu_mode cpu_mode;

	val = vm_get_register(vcpu, CPU_REG_RSI);
	vm_get_seg_desc(seg, &desc);
	cpu_mode = get_vcpu_mode(vcpu);

	vie_calculate_gla(cpu_mode, seg, &desc, val, addrsize, gva);

	return;
}

/**
 * @brief Check GVA from ES:(E)DI
 *
 * Get the GVA from the segment and check. Get the base address from \p CPU_REG_ES, and get the linear address
 * offset from \p CPU_REG_RDI. Call \p vie_calculate_gla to calculate the GVA, then call \p vie_canonical_check
 * to perform a canonical check and call \p gva2gpa to check if the GPA is valid. If any check fails during this
 * process, inject an exception to the guest.
 *
 * This function is only used by MOVS instruction emulation during the decode phase.
 *
 * @param[inout] vcpu Pointer to the vCPU that processes the instruction.
 * @param[inout] vie Pointer to instruction emulation information.
 * @param[in] addrsize Address size.
 * @param[out] gva Pointer to return the guest virtual address.
 *
 * @return A value to indicate whether the operation succeeds or not.
 *
 * @retval 0 on success.
 * @retval -EFAULT if a page fault occurs.
 * @retval -EINVAL if an invalid parameter is encountered.
 *
 * @pre N/A
 *
 * @post N/A
 */
static int32_t get_gva_di_check(struct acrn_vcpu *vcpu, struct instr_emul_vie *vie,
		uint8_t addrsize, uint64_t *gva)
{
	int32_t ret = -EFAULT;
	uint32_t err_code;
	struct seg_desc desc;
	enum vm_cpu_mode cpu_mode;
	uint64_t val, gpa;
	bool has_exception = false;

	vm_get_seg_desc(CPU_REG_ES, &desc);
	cpu_mode = get_vcpu_mode(vcpu);

	if (cpu_mode == CPU_MODE_64BIT) {
		if ((addrsize != 4U) && (addrsize != 8U)) {
			has_exception = true;
		}
	} else {
		if ((addrsize != 2U) && (addrsize != 4U)) {
			has_exception = true;
		} else {
			if (!is_desc_valid(&desc, PROT_WRITE)) {
				has_exception = true;
			}
		}
	}

	if (!has_exception) {
		val = vm_get_register(vcpu, CPU_REG_RDI);
		vie_calculate_gla(cpu_mode, CPU_REG_ES, &desc, val, addrsize, gva);

		if (vie_canonical_check(cpu_mode, *gva) != 0) {
			has_exception = true;
		} else {
			err_code = PAGE_FAULT_WR_FLAG;
			ret = gva2gpa(vcpu, *gva, &gpa, &err_code);
			if (ret < 0) {
				(void)handle_gva_translation_error(ret, vcpu, (uint64_t)gva, err_code);
			} else {

				/* If we are checking the dest operand for movs instruction,
				 * we cache the gpa if check pass. It will be used during
				 * movs instruction emulation.
				 */
				vie->dst_gpa = gpa;

				ret = 0;
			}
		}

	}

	if (has_exception) {
		vcpu_inject_gp(vcpu, 0U);
	}

	return ret;
}

/**
 * @brief Emulate Move Data from String to String.
 *
 * Emulate Move Data From String to String (Refer to MOVS/MOVSB/MOVSW/MOVSD/MOVSQ-Move Data From
 * String to String, in 4.3 Instructions, Vol 2, Intel® 64 and IA-32 Architectures Software Developer's
 * Manual Order Number: 325462-078US).
 * Moves the byte, word, or doubleword from the source operand to the destination operand. Then update
 * RSI (source operand), RDI (destination operand), and RCX (count) for the next instruction. If the
 * count register (RCX) is not zero, retain RIP and repeat this instruction.
 *
 * @param[in] vie Pointer to instruction emulation information.
 * @param[inout] vcpu Pointer to the vCPU that processes the instruction.
 *
 * @return Always return 0 to indicate a successful emulation of MMIO read/write.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark MOVS gets the operands from RSI and RDI. Both operands could be memory.
 * With VMX enabled, one of the operands triggers an EPT violation.
 *
 * If it's an RSI access that triggers the EPT violation, it's the source operand and always
 * a read operation. It is not necessary to check whether to inject a fault (done
 * by VMX already). We do need to check the RDI.
 *
 * If it's an RDI access that triggers the EPT violation, we need to check RDI because it's
 * always a write operation and VMX doesn't cover write access checks.
 * It is not necessary to check RSI, because VMX covers it for us.
 *
 * In summary,
 * For the MOVS instruction, we always check RDI during the instruction decoding phase.
 * And access RSI without any check during the instruction emulation phase.
 */
static int32_t emulate_movs(struct acrn_vcpu *vcpu, const struct instr_emul_vie *vie)
{
	uint64_t src_gva, gpa, val = 0UL;
	uint64_t rcx = 0U, rdi, rsi, rflags;
	uint32_t err_code;
	enum cpu_reg_name seg;
	uint8_t repeat, opsize;
	bool is_mmio_write, done = false;

	/* update the Memory Operand byte size if necessary */
	opsize = ((vie->op.op_flags & VIE_OP_F_BYTE_OP) != 0U) ? 1U : vie->opsize;
	is_mmio_write = (vcpu->req.reqs.mmio_request.direction == ACRN_IOREQ_DIR_WRITE);

	/*
	 * XXX although the MOVS instruction is only supposed to be used with
	 * the "rep" prefix some guests like FreeBSD will use "repnz" instead.
	 *
	 * Empirically the "repnz" prefix has identical behavior to "rep"
	 * and the zero flag does not make a difference.
	 */
	repeat = vie->repz_present | vie->repnz_present;

	if (repeat != 0U) {
		rcx = vm_get_register(vcpu, CPU_REG_RCX);

		/*
		 * The count register is %rcx, %ecx or %cx depending on the
		 * address size of the instruction.
		 */
		if ((rcx & size2mask[vie->addrsize]) == 0UL) {
			done = true;
		}
	}

	if (!done) {
		seg = (vie->seg_override != 0U) ? (vie->segment_register) : CPU_REG_DS;

		if (is_mmio_write) {
			get_gva_si_nocheck(vcpu, vie->addrsize, seg, &src_gva);

			err_code = PAGE_FAULT_WR_FLAG;
			/* we are sure it will success */
			(void)gva2gpa(vcpu, src_gva, &gpa, &err_code);
			(void)copy_from_gpa(vcpu->vm, &val, gpa, opsize);
			vie_mmio_write(vcpu, val);
		} else {
			vie_mmio_read(vcpu, &val);

			/* The dest gpa is saved during dst check instruction
			 * decoding.
			 */
			(void)copy_to_gpa(vcpu->vm, &val, vie->dst_gpa, opsize);
		}

		rsi = vm_get_register(vcpu, CPU_REG_RSI);
		rdi = vm_get_register(vcpu, CPU_REG_RDI);
		rflags = vm_get_register(vcpu, CPU_REG_RFLAGS);

		if ((rflags & PSL_D) != 0U) {
			rsi -= opsize;
			rdi -= opsize;
		} else {
			rsi += opsize;
			rdi += opsize;
		}

		vie_update_register(vcpu, CPU_REG_RSI, rsi, vie->addrsize);
		vie_update_register(vcpu, CPU_REG_RDI, rdi, vie->addrsize);

		if (repeat != 0U) {
			rcx = rcx - 1;
			vie_update_register(vcpu, CPU_REG_RCX, rcx, vie->addrsize);

			/*
			 * Repeat the instruction if the count register is not zero.
			 */
			if ((rcx & size2mask[vie->addrsize]) != 0UL) {
				vcpu_retain_rip(vcpu);
			}
		}
	}

	return 0;
}


/**
 * @brief Emulate Store String
 *
 * Emulate store string instruction (Refer to STOS/STOSB/STOSW/STOSD/STOSQ-Store String, in 4.3
 * Instructions, Vol 2, Intel® 64 and IA-32 Architectures Software Developer's Manual Order Number:
 * 325462-078US). In this function, stores a byte, word, or doubleword from the RAX register into the
 * destination operand. The destination operand is a memory location, the address of which is read from
 * RDI. Then update RDI for the next instruction. If the count register (RCX) is not zero, retain RIP and
 * repeat this instruction.
 *
 * @param[in] vie Pointer to instruction emulation information.
 * @param[inout] vcpu Pointer to the vCPU that processes the instruction.
 *
 * @return Always return 0 to indicate a successful emulation of MMIO read/write.
 *
 * @pre N/A
 *
 * @post N/A
 */
static int32_t emulate_stos(struct acrn_vcpu *vcpu, const struct instr_emul_vie *vie)
{
	bool done = false;
	uint8_t repeat, opsize;
	uint64_t val;
	uint64_t rcx = 0U, rdi, rflags;

	/* update the Memory Operand byte size if necessary */
	opsize = ((vie->op.op_flags & VIE_OP_F_BYTE_OP) != 0U) ? 1U : vie->opsize;
	repeat = vie->repz_present | vie->repnz_present;

	if (repeat != 0U) {
		rcx = vm_get_register(vcpu, CPU_REG_RCX);

		/*
		 * The count register is %rcx, %ecx or %cx depending on the
		 * address size of the instruction.
		 */
		if ((rcx & size2mask[vie->addrsize]) == 0UL) {
			done = true;
		}
	}

	if (!done) {
		val = vm_get_register(vcpu, CPU_REG_RAX);

		vie_mmio_write(vcpu, val);

		rdi = vm_get_register(vcpu, CPU_REG_RDI);
		rflags = vm_get_register(vcpu, CPU_REG_RFLAGS);

		if ((rflags & PSL_D) != 0U) {
			rdi -= opsize;
		} else {
			rdi += opsize;
		}

		vie_update_register(vcpu, CPU_REG_RDI, rdi, vie->addrsize);

		if (repeat != 0U) {
			rcx = rcx - 1;
			vie_update_register(vcpu, CPU_REG_RCX, rcx, vie->addrsize);

			/*
			 * Repeat the instruction if the count register is not zero.
			 */
			if ((rcx & size2mask[vie->addrsize]) != 0UL) {
				vcpu_retain_rip(vcpu);
			}
		}
	}

	return 0;
}

/**
 * @brief Emulate Logical Compare.
 *
 * Computes the bit-wise logical AND of operands and sets RFLAGS (SF, ZF, PF) according to the result.
 * Emulate Test instruction (Refer to TEST-Logical Compare, in 4.3 Instructions, Vol 2, Intel® 64 and IA-32
 * Architectures Software Developer's Manual Order Number: 325462-078US). In this function, computes the
 * bit-wise logical AND of the two source operands and update RFLAGS according to the result.
 *
 * @param[in] vie Pointer to instruction emulation information.
 * @param[inout] vcpu Pointer to the vCPU that processes the instruction.
 *
 * @return 0 on success, -EINVAL for unsupported opcodes.
 *
 * @pre N/A
 *
 * @post N/A
 */
static int32_t emulate_test(struct acrn_vcpu *vcpu, const struct instr_emul_vie *vie)
{
	int32_t error;
	uint8_t size;
	enum cpu_reg_name reg;
	uint64_t result, rflags2, val1, val2;

	size = vie->opsize;
	error = 0;

	switch (vie->opcode) {
	case 0x84U:
		/*
		 * 84/r		test r8, r/m8
		 */
		size = 1U; /*override size for 8-bit operation*/
		/* fallthrough */
	case 0x85U:
		/*
		 * AND reg (ModRM:reg) and mem (ModRM:r/m) and discard
		 * the result.
		 *
		 *
		 * 85/r		test r16, r/m16
		 * 85/r		test r32, r/m32
		 * REX.W + 85/r	test r64, r/m64
		 */

		/* get the first operand */
		reg = (enum cpu_reg_name)(vie->reg);
		val1 = vm_get_register(vcpu, reg);

		/* get the second operand */
		vie_mmio_read(vcpu, &val2);

		/* perform the operation and write the result */
		result = val1 & val2;
		break;
	default:
		/*
		 * For the opcode that is not handled (an invalid opcode), the
		 * error code is assigned to a default value (-EINVAL).
		 * Gracefully return this error code if prior case clauses have
		 * not been met.
		 */
		error = -EINVAL;
		break;
	}

	if (error == 0) {
		/*
		 * OF and CF are cleared; the SF, ZF and PF flags are set
		 * according to the result; AF is undefined.
		 *
		 * The updated status flags are obtained by subtracting 0 from
		 * 'result'.
		 */
		rflags2 = getcc(size, result, 0UL);
		vie_update_rflags(vcpu, rflags2, PSL_PF | PSL_Z | PSL_N);
	}

	return error;
}

/**
 * @brief Emulate logical AND instruction.
 *
 * Emulate the logical AND instruction (Refer to AND-Logical AND, in 3.3 Instructions, Vol 2, Intel® 64 and
 * IA-32 Architectures Software Developer's Manual Order Number: 325462-078US). Get the two operands following the
 * instruction, and calculate the logical AND result. Write the result to \p mmio_request or target register and
 * update RFLAGS.
 *
 * @param[in] vie Pointer to instruction emulation information.
 * @param[inout] vcpu Pointer to the vCPU that processes the instruction.
 *
 * @return 0 on success, -EINVAL for unsupported opcodes.
 *
 * @pre N/A
 *
 * @post N/A
 */
static int32_t emulate_and(struct acrn_vcpu *vcpu, const struct instr_emul_vie *vie)
{
	int32_t error;
	uint8_t size;
	enum cpu_reg_name reg;
	uint64_t result, rflags2, val1, val2;

	size = vie->opsize;
	error = 0;

	switch (vie->opcode) {
	case 0x23U:
		/*
		 * AND reg (ModRM:reg) and mem (ModRM:r/m) and store the
		 * result in reg.
		 *
		 * 23/r		and r16, r/m16
		 * 23/r		and r32, r/m32
		 * REX.W + 23/r	and r64, r/m64
		 */

		/* get the first operand */
		reg = (enum cpu_reg_name)(vie->reg);
		val1 = vm_get_register(vcpu, reg);

		/* get the second operand */
		vie_mmio_read(vcpu, &val2);

		/* perform the operation and write the result */
		result = val1 & val2;
		vie_update_register(vcpu, reg, result, size);
		break;
	case 0x81U:
	case 0x83U:
		/*
		 * AND mem (ModRM:r/m) with immediate and store the
		 * result in mem.
		 *
		 * 81 /4		and r/m16, imm16
		 * 81 /4		and r/m32, imm32
		 * REX.W + 81 /4	and r/m64, imm32 sign-extended to 64
		 *
		 * 83 /4		and r/m16, imm8 sign-extended to 16
		 * 83 /4		and r/m32, imm8 sign-extended to 32
		 * REX.W + 83/4		and r/m64, imm8 sign-extended to 64
		 */

		/* get the first operand */
		vie_mmio_read(vcpu, &val1);

		/*
		 * perform the operation with the pre-fetched immediate
		 * operand and write the result
		 */
		result = val1 & (uint64_t)vie->immediate;
		vie_mmio_write(vcpu, result);
		break;
	default:
		/*
		 * For the opcode that is not handled (an invalid opcode), the
		 * error code is assigned to a default value (-EINVAL).
		 * Gracefully return this error code if prior case clauses have
		 * not been met.
		 */
		error = -EINVAL;
		break;
	}

	if (error == 0) {
		/*
		 * OF and CF are cleared; the SF, ZF and PF flags are set
		 * according to the result; AF is undefined.
		 *
		 * The updated status flags are obtained by subtracting 0 from
		 * 'result'.
		 */
		rflags2 = getcc(size, result, 0UL);
		vie_update_rflags(vcpu, rflags2, PSL_PF | PSL_Z | PSL_N);
	}

	return error;
}

/**
 * @brief Emulate logical OR instruction.
 *
 * Emulate the logical OR instruction (Refer to OR-Logical Inclusive OR, in 4.3 Instructions, Vol 2, Intel® 64 and
 * IA-32 Architectures Software Developer's Manual Order Number: 325462-078US). Get the two operands following the
 * instruction, and calculate the logical OR result. Write the result to \p mmio_request and update RFLAGS.
 *
 * @param[in] vie Pointer to instruction emulation information.
 * @param[inout] vcpu Pointer to the vCPU that processes the instruction.
 *
 * @return 0 on success, -EINVAL for unsupported opcodes.
 *
 * @pre N/A
 *
 * @post N/A
 */
static int32_t emulate_or(struct acrn_vcpu *vcpu, const struct instr_emul_vie *vie)
{
	int32_t error;
	uint8_t size;
	enum cpu_reg_name reg;
	uint64_t val1, val2, result, rflags2;

	size = vie->opsize;
	error = 0;

	switch (vie->opcode) {
	case 0x81U:
	case 0x83U:
		/*
		 * OR mem (ModRM:r/m) with immediate and store the
		 * result in mem.
		 *
		 * 81 /1		or r/m16, imm16
		 * 81 /1		or r/m32, imm32
		 * REX.W + 81 /1	or r/m64, imm32 sign-extended to
		 *			64
		 *
		 * 83 /1		or r/m16, imm8 sign-extended to
		 *			16
		 * 83 /1		or r/m32, imm8 sign-extended to
		 *			32
		 * REX.W + 83/1		or r/m64, imm8 sign-extended to
		 *			64
		 */

		/* get the first operand */
		vie_mmio_read(vcpu, &val1);

		/*
		 * perform the operation with the pre-fetched immediate
		 * operand and write the result
		 */
		result = val1 | (uint64_t)vie->immediate;
		vie_mmio_write(vcpu, result);
		break;
	case 0x09U:
		/*
		 * OR mem (ModRM:r/m) with reg (ModRM:reg) and store the
		 * result in mem.
		 * 09/r:	OR r/m16, r16
		 * 09/r:	OR r/m32, r32
		 */

		/* get the first operand */
		vie_mmio_read(vcpu, &val1);

		/* get the second operand */
		reg = (enum cpu_reg_name)(vie->reg);
		val2 = vm_get_register(vcpu, reg);

		/* perform the operation and write the result */
		result = val1 | val2;
		result &= size2mask[size];

		vie_mmio_write(vcpu, result);
		break;
	default:
		/*
		 * For the opcode that is not handled (an invalid opcode), the
		 * error code is assigned to a default value (-EINVAL).
		 * Gracefully return this error code if prior case clauses have
		 * not been met.
		 */
		error = -EINVAL;
		break;
	}
	if (error == 0) {
		/*
		 * OF and CF are cleared; the SF, ZF and PF flags are set
		 * according to the result; AF is undefined.
		 *
		 * The updated status flags are obtained by subtracting 0 from
		 * 'result'.
		 */
		rflags2 = getcc(size, result, 0UL);
		vie_update_rflags(vcpu, rflags2, PSL_PF | PSL_Z | PSL_N);
	}

	return error;
}

/**
 * @brief Emulate compare two operands.
 *
 * Emulate the compare instruction (Refer to CMP-Compare Two Operands, in 3.3 Instructions, Vol 2, Intel® 64 and
 * IA-32 Architectures Software Developer's Manual Order Number: 325462-078US). Get the two operands following the
 * instruction, compare them, and update RFLAGS.
 *
 * @param[in] vie Pointer to instruction emulation information.
 * @param[inout] vcpu Pointer to the vCPU that processes the instruction.
 *
 * @return 0 on success, -EINVAL for unsupported opcodes.
 *
 * @pre N/A
 *
 * @post N/A
 */
static int32_t emulate_cmp(struct acrn_vcpu *vcpu, const struct instr_emul_vie *vie)
{
	int32_t ret = 0;
	uint8_t size;
	uint64_t regop, memop, op1, op2, rflags2;
	enum cpu_reg_name reg;

	size = vie->opsize;
	switch (vie->opcode) {
	case 0x39U:
	case 0x3BU:
		/*
		 * 39/r		CMP r/m16, r16
		 * 39/r		CMP r/m32, r32
		 * REX.W 39/r	CMP r/m64, r64
		 *
		 * 3B/r		CMP r16, r/m16
		 * 3B/r		CMP r32, r/m32
		 * REX.W + 3B/r	CMP r64, r/m64
		 *
		 * Compare the first operand with the second operand and
		 * set status flags in EFLAGS register. The comparison
		 * is performed by subtracting the second operand from
		 * the first operand and then setting the status flags.
		 */

		/* Get the register operand */
		reg = (enum cpu_reg_name)(vie->reg);
		regop = vm_get_register(vcpu, reg);

		/* Get the memory operand */
		vie_mmio_read(vcpu, &memop);

		if (vie->opcode == 0x3BU) {
			op1 = regop;
			op2 = memop;
		} else {
			op1 = memop;
			op2 = regop;
		}
		break;
	case 0x80U:
	case 0x81U:
	case 0x83U:
		/*
		 * 80 /7		cmp r/m8, imm8
		 * REX + 80 /7		cmp r/m8, imm8
		 *
		 * 81 /7		cmp r/m16, imm16
		 * 81 /7		cmp r/m32, imm32
		 * REX.W + 81 /7	cmp r/m64, imm32 sign-extended
		 *			to 64
		 *
		 * 83 /7		cmp r/m16, imm8 sign-extended
		 *			to 16
		 * 83 /7		cmp r/m32, imm8 sign-extended
		 *			to 32
		 * REX.W + 83 /7	cmp r/m64, imm8 sign-extended
		 *			to 64
		 *
		 * Compare mem (ModRM:r/m) with immediate and set
		 * status flags according to the results.  The
		 * comparison is performed by subtracting the
		 * immediate from the first operand and then setting
		 * the status flags.
		 *
		 */
		if (vie->opcode == 0x80U) {
			size = 1U;
		}

		/* get the first operand */
		vie_mmio_read(vcpu, &op1);
		op2 = (uint64_t)vie->immediate;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret == 0) {
		rflags2 = getcc(size, op1, op2);
		vie_update_rflags(vcpu, rflags2, RFLAGS_STATUS_BITS);
	}
	return ret;
}

/**
 * @brief Emulate Subtract.
 *
 * Emulate the subtract instruction (Refer to SUB-Subtract, in 4.3 Instructions, Vol 2, Intel® 64 and
 * IA-32 Architectures Software Developer's Manual Order Number: 325462-078US). Subtracts the source operand
 * from the destination operand and stores the result in the destination operand. Then update RFLAGS (OF, CF).
 *
 * @param[in] vie Pointer to instruction emulation information.
 * @param[inout] vcpu Pointer to the vCPU that processes the instruction.
 *
 * @return 0 on success, -EINVAL for unsupported opcodes.
 *
 * @pre N/A
 *
 * @post N/A
 */
static int32_t emulate_sub(struct acrn_vcpu *vcpu, const struct instr_emul_vie *vie)
{
	int32_t error;
	uint8_t size;
	uint64_t nval, rflags2, val1, val2;
	enum cpu_reg_name reg;

	size = vie->opsize;
	error = 0;

	switch (vie->opcode) {
	case 0x2BU:
		/*
		 * SUB r/m from r and store the result in r
		 *
		 * 2B/r			SUB r16, r/m16
		 * 2B/r			SUB r32, r/m32
		 * REX.W + 2B/r	SUB r64, r/m64
		 */

		/* get the first operand */
		reg = (enum cpu_reg_name)(vie->reg);
		val1 = vm_get_register(vcpu, reg);

		/* get the second operand */
		vie_mmio_read(vcpu, &val2);

		/* perform the operation and write the result */
		nval = val1 - val2;
		vie_update_register(vcpu, reg, nval, size);
		break;
	default:
		/*
		 * For the opcode that is not handled (an invalid opcode), the
		 * error code is assigned to a default value (-EINVAL).
		 * Gracefully return this error code if prior case clauses have
		 * not been met.
		 */
		error = -EINVAL;
		break;
	}

	if (error == 0) {
		rflags2 = getcc(size, val1, val2);
		vie_update_rflags(vcpu, rflags2, RFLAGS_STATUS_BITS);
	}

	return error;
}

/**
 * @brief Emulate a group of instructions.
 *
 * Emulate a group of instructions with first opcodes 0x80, 0x81, and 0x83 (Refers to AND-Logical AND, CMP-Compare
 * Two Operands, in 3.3 instructions; OR-Logical Inclusive OR, in 4.3 instructions, Vol 2, Intel® 64 and
 * IA-32 Architectures Software Developer's Manual Order Number: 325462-078US). These instructions have
 * two operands: an immediate data and the content of a general-purpose register. And the second opcode
 * is defined by the ModR/M Opcode field.
 *
 * @param[in] vie Pointer to instruction emulation information.
 * @param[inout] vcpu Pointer to the vCPU that processes the instruction.
 *
 * @return 0 on success, -EINVAL for unsupported operation type.
 *
 * @pre N/A
 *
 * @post N/A
 */
static int32_t emulate_group1(struct acrn_vcpu *vcpu, const struct instr_emul_vie *vie)
{
	int32_t error;

	switch (vie->reg & 7U) {
	case 0x1U:	/* OR */
		error = emulate_or(vcpu, vie);
		break;
	case 0x4U:	/* AND */
		error = emulate_and(vcpu, vie);
		break;
	case 0x7U:	/* CMP */
		error = emulate_cmp(vcpu, vie);
		break;
	default:
		error = -EINVAL;
		break;
	}

	return error;
}

/**
 * @brief Emulate Bit Test.
 *
 * Emulate Bit Test instruction (Refer to BT-Bit Test, in 3.3 Instructions, Vol 2, Intel®
 * 64 and IA-32 Architectures Software Developer's Manual Order Number: 325462-078US). This
 * function selects the bit in a bit string at the bit-position designated by the bit offset
 * and stores the value of the bit in the CF flag.
 *
 * @param[in] vie Pointer to instruction emulation information.
 * @param[inout] vcpu Pointer to the vCPU that processes the instruction.
 *
 * @return 0 on success, -EINVAL for unsupported operation type.
 *
 * @pre N/A
 *
 * @post N/A
 */
static int32_t emulate_bittest(struct acrn_vcpu *vcpu, const struct instr_emul_vie *vie)
{
	uint64_t val, rflags, bitmask;
	uint64_t bitoff;
	uint8_t size;
	int32_t ret;

	/*
	 * 0F BA is a Group 8 extended opcode.
	 *
	 * Currently we only emulate the 'Bit Test' instruction which is
	 * identified by a ModR/M:reg encoding of 100b.
	 */
	if ((vie->reg & 7U) == 4U) {
		rflags = vm_get_register(vcpu, CPU_REG_RFLAGS);

		vie_mmio_read(vcpu, &val);

		/*
		 * Intel SDM, Vol 2, Table 3-2:
		 * "Range of Bit Positions Specified by Bit Offset Operands"
		 */
		bitmask = ((uint64_t)vie->opsize * 8UL) - 1UL;
		bitoff = (uint64_t)(vie->immediate) & bitmask;

		/* Copy the bit into the Carry flag in %rflags */
		if ((val & (1UL << bitoff)) != 0U) {
			rflags |= PSL_C;
		} else {
			rflags &= ~PSL_C;
		}
		size = 8U;
		vie_update_register(vcpu, CPU_REG_RFLAGS, rflags, size);
		ret = 0;
	} else {
		ret = -EINVAL;
	}

	return ret;
}

#ifdef CONFIG_LOCK_INSTRUCTION_EMULATION
/**
 * @brief Emulate Exchange Rigester/Memory with Register.
 *
 * Emulate XCHG instruction (Refer to XCHG-Exchange Register/Memory With Register, in 4.3 Instructions,
 * Vol 2, Intel® 64 and IA-32 Architectures Software Developer's Manual Order Number: 325462-078US). In
 * this function, exchanges the contents of the destination and source operands. Inject an exception (PF)
 * to the VM if a page fault occurs.
 *
 * @param[in] vie Pointer to instruction emulation information.
 * @param[inout] vcpu Pointer to the vCPU that processes the instruction.
 *
 * @return 0 on success, non-zero for data copy fail.
 *
 * @pre N/A
 *
 * @post N/A
 */
static __attribute__((noinline)) int32_t emulate_xchg_for_splitlock(struct acrn_vcpu *vcpu, const struct instr_emul_vie *vie)
{
	enum cpu_reg_name reg;
	uint64_t reg_val, data = 0UL;
	uint8_t opsize = vie->opsize;
	int32_t ret = 0;
	uint32_t err_code = 0U;
	uint64_t fault_addr;

	reg = (enum cpu_reg_name)(vie->reg);
	reg_val = vm_get_register(vcpu, reg);

	ret = copy_from_gva(vcpu, &data, vie->gva, opsize, &err_code, &fault_addr);
	if (ret == 0) {
		err_code = PAGE_FAULT_WR_FLAG;
		ret = copy_to_gva(vcpu, &reg_val, vie->gva, opsize, &err_code, &fault_addr);
		if (ret == 0) {
			vie_update_register(vcpu, reg, data, opsize);
		}
	}

	if (ret < 0) {
		pr_err("Error copy xchg data!");
		(void)handle_gva_translation_error(ret, vcpu, fault_addr, err_code);
	}

	return ret;
}
#endif

/**
 * @brief Fetch instruction from guest memory.
 *
 * Copy the instruction from guest memory using the guest virtual address.
 * If a page fault occurs, inject an exception (PF) to the VM.
 *
 * @param[inout] vie Pointer to instruction emulation information.
 *
 * @return A value to indicate whether the operation succeeds or not.
 *
 * @retval 0 on success.
 * @retval -EFAULT on GVA page fault.
 * @retval -EINVAL on invalid instruction length.
 *
 * @pre N/A
 *
 * @post N/A
 */
static int32_t vie_fetch_instruction(struct instr_emul_vie *vie)
{
	uint32_t fetch_len;
	uint64_t fetch_gva;
	uint32_t err_code;
	uint64_t fault_addr;
	int32_t ret;

	if (vie->num_valid == 0) {
		fetch_len = min(VIE_INST_SIZE, (uint32_t)(PAGE_SIZE - (vie->rip & ~PAGE_MASK)));
		fetch_gva = vie->rip;
	} else {
		fetch_len = (uint32_t)(VIE_INST_SIZE - vie->num_valid);
		fetch_gva = vie->rip + vie->num_valid;
	}

	err_code = PAGE_FAULT_ID_FLAG;
	ret = copy_from_gva(vie->vcpu, vie->inst + vie->num_valid, fetch_gva, fetch_len,
				&err_code, &fault_addr);
	if (ret < 0) {
		(void)handle_gva_translation_error(ret, vie->vcpu, fault_addr, err_code);
	} else {
		vie->num_valid += (uint8_t)fetch_len;
		ret = 0;
	}

	return ret;
}

/**
 * @brief Initialize instruction emulation information.
 *
 * This function would first clear all the vie structure. Then use the CS as the segment, the RIP as the segment
 * offset, call \p vie_calculate_gla to calculate the guest linear address by adding the offset to the segment
 * base address in the register. Then call \p vie_fetch_instruction to copy the instruction from the gla.
 * If a page fault occurs, inject an exception (PF) to the VM.
 *
 * @param[out] vie Pointer to instruction emulation information.
 * @param[in] vcpu Pointer to the vCPU that processes the instruction.
 *
 * @return A value to indicate whether the operation succeeds or not.
 *
 * @retval 0 on success.
 * @retval -EFAULT on GVA page fault.
 * @retval -EINVAL on invalid instruction length.
 *
 * @pre N/A
 *
 * @post N/A
 */
static int32_t vie_init(struct instr_emul_vie *vie, struct acrn_vcpu *vcpu)
{
	enum vm_cpu_mode cpu_mode;
	struct seg_desc desc;

	(void)memset(vie, 0U, sizeof(struct instr_emul_vie));

	vie->vcpu = vcpu;

	/* init register fields in vie. */
	vie->base_register = CPU_REG_LAST;
	vie->index_register = CPU_REG_LAST;
	vie->segment_register = CPU_REG_LAST;

	cpu_mode = get_vcpu_mode(vcpu);
	vm_get_seg_desc(CPU_REG_CS, &desc);

	/* VMX_GUEST_RIP is a natural-width field */
	vie_calculate_gla(cpu_mode, CPU_REG_CS, &desc, vcpu_get_rip(vcpu),
			  8U, &(vie->rip));

	return vie_fetch_instruction(vie);
}

/**
 * @brief Get one byte of instruction to emulate.
 *
 * Get a byte of instruction which is saved in the \p inst array. The instruction is copied from guest memory
 * when initializing \p vie.
 *
 * @param[in] vie Pointer to instruction emulation information.
 * @param[out] x Pointer to instruction byte.
 *
 * @return 0 on success, or -1 if it fails to fetch the instruction from the guest.
 *
 * @pre N/A
 *
 * @post N/A
 */
static int32_t vie_peek(struct instr_emul_vie *vie, uint8_t *x)
{
	int32_t ret;

	if (vie->num_processed < vie->num_valid) {
		ret = 0;
	} else { /* vie->num_processed == vie->num_valid */
		if (vie->num_valid < VIE_INST_SIZE) {
			ret = vie_fetch_instruction(vie);
		} else {
			ret = -1;
		}
	}

	if (ret == 0) {
		*x = vie->inst[vie->num_processed];
	}

	return ret;
}

/**
 * @brief Add processed number.
 *
 * Add the processed number after decoding a byte.
 *
 * @param[out] vie Pointer to instruction emulation information.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 */
static void vie_advance(struct instr_emul_vie *vie)
{

	vie->num_processed++;
}

/**
 * @brief Decode segment override.
 *
 * Decode segment override for Group 2 Instruction Prefixes(Refer to 2.1.1 Instruction Prefixes, Vol 2,
 * Intel® 64 and IA-32 Architectures Software Developer's Manual Order Number: 325462-078US)
 *
 * @param[in] x Instruction prefix code.
 * @param[out] seg Pointer to return the segment register name.
 *
 * @return true on segment override, false on no segment override.
 *
 * @pre N/A
 *
 * @post N/A
 */
static bool segment_override(uint8_t x, enum cpu_reg_name *seg)
{
	bool override = true;
	switch (x) {
	case 0x2EU:
		*seg = CPU_REG_CS;
		break;
	case 0x36U:
		*seg = CPU_REG_SS;
		break;
	case 0x3EU:
		*seg = CPU_REG_DS;
		break;
	case 0x26U:
		*seg = CPU_REG_ES;
		break;
	case 0x64U:
		*seg = CPU_REG_FS;
		break;
	case 0x65U:
		*seg = CPU_REG_GS;
		break;
	default:
		override = false;
		break;
	}
	return override;
}

/**
 * @brief Decode address and operand size.
 *
 * Decode address size and operand size from the prefix field.
 *
 * @param[inout] vie Pointer to instruction emulation information.
 * @param[in] cpu_mode vCPU's CPU mode.
 * @param[in] cs_d Guest CS default bits: true for 32-bit default, false for 16-bit default.
 *
 * @return None
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark vie->addrsize must be 1, 2, 4, or 8
 * @remark vie->opsize must be 1, 2, 4, or 8
 */
static void decode_op_and_addr_size(struct instr_emul_vie *vie, enum vm_cpu_mode cpu_mode, bool cs_d)
{
	/*
	 * Section "Operand-Size And Address-Size Attributes", Intel SDM, Vol 1
	 */
	if (cpu_mode == CPU_MODE_64BIT) {
		/*
		 * Default address size is 64-bits and default operand size
		 * is 32-bits.
		 */
		vie->addrsize = ((vie->addrsize_override != 0U) ? 4U : 8U);
		if (vie->rex_w != 0U) {
			vie->opsize = 8U;
		} else if (vie->opsize_override != 0U) {
			vie->opsize = 2U;
		} else {
			vie->opsize = 4U;
		}
	} else if (cs_d) {
		/* Default address and operand sizes are 32-bits */
		vie->addrsize = ((vie->addrsize_override != 0U) ? 2U : 4U);
		vie->opsize = ((vie->opsize_override != 0U) ? 2U : 4U);
	} else {
		/* Default address and operand sizes are 16-bits */
		vie->addrsize = ((vie->addrsize_override != 0U) ? 4U : 2U);
		vie->opsize = ((vie->opsize_override != 0U) ? 4U : 2U);
	}

}

/**
 * @brief Decode Instruction Prefixes.
 *
 * This function decodes instruction prefixes and the REX prefix according to SDM definitions and sets members in
 * the \p instr_emul_vie.
 * - There are four groups of instruction prefixes (Refer to 2.1.1 Instruction Prefixes, Vol 2, Intel® 64 and IA-32
 *   Architectures Software Developer's Manual Order Number: 325462-078US). These four groups may be placed in any
 *   order relative to each other.
 * - In 64-bit mode, the REX prefix may be used (Refer to 2.2.1 REX Prefixes, Vol 2, Intel® 64 and IA-32
 *   Architectures Software Developer's Manual Order Number: 325462-078US).
 *
 * @param[inout] vie Pointer to instruction emulation information.
 * @param[in] cpu_mode vCPU's CPU mode.
 * @param[in] cs_d Guest CS default bits: true for 32-bit default, false for 16-bit default.
 *
 * @return 0 on success, -1 if instruction is less than legacy prefix size.
 *
 * @pre N/A
 *
 * @post N/A
 */
static int32_t decode_prefixes(struct instr_emul_vie *vie, enum vm_cpu_mode cpu_mode, bool cs_d)
{
	uint8_t x, i;
	int32_t ret = 0;

	for (i = 0U; i < VIE_PREFIX_SIZE; i++) {
		if (vie_peek(vie, &x) != 0) {
			ret = -1;
			break;
		} else {
			if (x == 0x66U) {
				vie->opsize_override = 1U;
			} else if (x == 0x67U) {
				vie->addrsize_override = 1U;
			} else if (x == 0xF3U) {
				vie->repz_present = 1U;
			} else if (x == 0xF2U) {
				vie->repnz_present = 1U;
			} else if (segment_override(x, &vie->segment_register)) {
				vie->seg_override = 1U;
			} else {
				break;
			}

			vie_advance(vie);
		}
	}

	if (ret == 0) {
		/*
		 * From section 2.2.1, "REX Prefixes", Intel SDM Vol 2:
		 * - Only one REX prefix is allowed per instruction.
		 * - The REX prefix must immediately precede the opcode byte or the
		 *   escape opcode byte.
		 * - If an instruction has a mandatory prefix (0x66, 0xF2 or 0xF3)
		 *   the mandatory prefix must come before the REX prefix.
		 */
		if ((cpu_mode == CPU_MODE_64BIT) && (x >= 0x40U) && (x <= 0x4FU)) {
			vie->rex_present = 1U;
			vie->rex_w = (x >> 0x3U) & 1U;
			vie->rex_r = (x >> 0x2U) & 1U;
			vie->rex_x = (x >> 0x1U) & 1U;
			vie->rex_b = (x >> 0x0U) & 1U;
			vie_advance(vie);
		}
		decode_op_and_addr_size(vie, cpu_mode, cs_d);
	}

	return ret;
}

/**
 * @brief Decode two-byte opcodes.
 *
 * Get the second byte of opcodes and decode the operation type.
 *
 * @param[inout] vie Pointer to instruction emulation information.
 *
 * @return 0 on success, or -1 if it fails to fetch the instruction.
 *
 * @pre N/A
 *
 * @post N/A
 */
static int32_t decode_two_byte_opcode(struct instr_emul_vie *vie)
{
	int32_t ret = 0;
	uint8_t x;

	if (vie_peek(vie, &x) != 0) {
		ret = -1;
	} else {
		vie->opcode = x;
		vie->op = two_byte_opcodes[x];

		if (vie->op.op_type == VIE_OP_TYPE_NONE) {
			ret = -1;
		} else {
			vie_advance(vie);
		}
	}

	return ret;
}

/**
 * @brief Decode opcodes.
 *
 * This function decodes opcodes (Refer to 2.1.2 Opcode, Vol 2, Intel® 64 and IA-32 Architectures Software
 * Developer's Manual Order Number: 325462-078US) and translates them to \p instr_emul_vie_op. One-byte and
 * two-byte opcode decoding is supported.
 * - One-byte opcodes are translated by \p one_byte_opcodes.
 * - Two-byte opcodes are translated by \p two_byte_opcodes, consisting of an escape opcode byte 0x0F as the
 *   primary opcode and a second opcode byte.
 *
 * @param[inout] vie Pointer to instruction emulation information.
 *
 * @return 0 on success, -1 on failure to get instruction or invalid op_type.
 *
 * @pre N/A
 *
 * @post N/A
 */
static int32_t decode_opcode(struct instr_emul_vie *vie)
{
	int32_t ret = 0;
	uint8_t x;

	if (vie_peek(vie, &x) != 0) {
		ret = -1;
	} else {
		vie->opcode = x;
		vie->op = one_byte_opcodes[x];

		if (vie->op.op_type == VIE_OP_TYPE_NONE) {
			ret = -1;
		} else {
			vie_advance(vie);

			if (vie->op.op_type == VIE_OP_TYPE_TWO_BYTE) {
				ret = decode_two_byte_opcode(vie);
			}
		}
	}

	return ret;
}

/**
 * @brief Decode ModR/M byte.
 *
 * Decode the addressing-form specifier byte called the ModR/M (Refer to 2.1.3 ModR/M and SIB Bytes, Vol 2, Intel®
 * 64 and IA-32 Architectures Software Developer's Manual Order Number: 325462-078US). The ModR/M byte contains
 * three fields of information.
 * Bits 6-7 for the mod field, which specifies direct addressing or indirect addressing.
 * Bits 5-3 for the Reg/Opcode field, which specifies a register number or three more bits of opcode information.
 * Bits 2-0 for the R/M field, which specifies a register as an operand or combines with the mod field to encode an
 * addressing mode.
 * The combination of REX and ModR/M is decoded here.
 *
 * @param[inout] vie Pointer to instruction emulation information.
 * @param[in] cpu_mode vCPU's CPU mode.
 *
 * @return 0 on success, -1 on failure to get instruction or indirect addressing mode.
 *
 * @pre N/A
 *
 * @post N/A
 */
static int32_t decode_modrm(struct instr_emul_vie *vie, enum vm_cpu_mode cpu_mode)
{
	uint8_t x;
	int32_t ret = -1;

	if ((vie->op.op_flags & VIE_OP_F_NO_MODRM) != 0U) {
		ret = 0;
	} else if (vie_peek(vie, &x) != 0) {
	} else {
		vie->mod = (x >> 6U) & 0x3U;
		vie->rm =  (x >> 0U) & 0x7U;
		vie->reg = (x >> 3U) & 0x7U;

		/*
		 * A direct addressing mode makes no sense in the context of an EPT
		 * fault. There has to be a memory access involved to cause the
		 * EPT fault.
		 */
		if (vie->mod != VIE_MOD_DIRECT) {
			if (((vie->mod == VIE_MOD_INDIRECT) && (vie->rm == VIE_RM_DISP32)) ||
					(vie->rm == VIE_RM_SIB)) {
				/*
				 * Table 2-5: Special Cases of REX Encodings
				 *
				 * mod=0, r/m=5 is used in the compatibility mode to
				 * indicate a disp32 without a base register.
				 *
				 * mod!=3, r/m=4 is used in the compatibility mode to
				 * indicate that the SIB byte is present.
				 *
				 * The 'b' bit in the REX prefix is don't care in
				 * this case.
				 */
			} else {
				vie->rm |= (vie->rex_b << 3U);
			}

			vie->reg |= (vie->rex_r << 3U);

			/* SIB */
			if (vie->rm == VIE_RM_SIB) {
				/* done */
			} else {
				vie->base_register = (enum cpu_reg_name)vie->rm;

				switch (vie->mod) {
				case VIE_MOD_INDIRECT_DISP8:
					vie->disp_bytes = 1U;
					break;
				case VIE_MOD_INDIRECT_DISP32:
					vie->disp_bytes = 4U;
					break;
				default:
					/* VIE_MOD_INDIRECT */
					if (vie->rm == VIE_RM_DISP32) {
						vie->disp_bytes = 4U;
					/*
					 * Table 2-7. RIP-Relative Addressing
					 *
					 * In 64-bit mode mod=00 r/m=101 implies [rip] + disp32
					 * whereas in compatibility mode it just implies disp32.
					 */

						if (cpu_mode == CPU_MODE_64BIT) {
							vie->base_register = CPU_REG_RIP;
							pr_err("VM exit with RIP as indirect access");
						}
						else {
							vie->base_register = CPU_REG_LAST;
						}
					}
					break;
				}

			}
			vie_advance(vie);

			ret = 0;
		}
	}

	return ret;
}

/**
 * @brief Decode SIB byte.
 *
 * This function decodes the SIB byte (Refer to 2.1.3 ModR/M and SIB Bytes, Vol 2, Intel® 64 and IA-32 Architectures
 * Software Developer's Manual Order Number: 325462-078US). SIB is a second addressing byte:
 * - Bits 6-7 for the scale field, which specifies the scale factor.
 * - Bits 5-3 for the index field, which specifies the register number of the index register.
 * - Bits 2-0 for the base field, which specifies the register number of the base register.
 * The combination of REX and SIB is decoded here.
 *
 * @param[inout] vie Pointer to instruction emulation information.
 *
 * @return 0 on success, -1 on failure to get instruction.
 *
 * @pre N/A
 *
 * @post N/A
 */
static int32_t decode_sib(struct instr_emul_vie *vie)
{
	uint8_t x;
	int32_t ret;

	/* Proceed only if SIB byte is present */
	if ((vie->mod == VIE_MOD_DIRECT) || (vie->rm != VIE_RM_SIB)) {
		ret = 0;
	} else if (vie_peek(vie, &x) != 0) {
		ret = -1;
	} else {

		/* De-construct the SIB byte */
		vie->ss = (x >> 6U) & 0x3U;
		vie->index = (x >> 3U) & 0x7U;
		vie->base = (x >> 0U) & 0x7U;

		/* Apply the REX prefix modifiers */
		vie->index |= vie->rex_x << 3U;
		vie->base |= vie->rex_b << 3U;

		switch (vie->mod) {
		case VIE_MOD_INDIRECT_DISP8:
			vie->disp_bytes = 1U;
			break;
		case VIE_MOD_INDIRECT_DISP32:
			vie->disp_bytes = 4U;
			break;
		default:
			/*
			 * All possible values of 'vie->mod':
			 * 1. VIE_MOD_DIRECT
			 *	has been handled at the start of this function
			 * 2. VIE_MOD_INDIRECT_DISP8
			 *	has been handled in prior case clauses
			 * 3. VIE_MOD_INDIRECT_DISP32
			 *	has been handled in prior case clauses
			 * 4. VIE_MOD_INDIRECT
			 *	will be handled later after this switch statement
			 */
			break;
		}

		if ((vie->mod == VIE_MOD_INDIRECT) && ((vie->base == 5U) || (vie->base == 13U))) {
			/*
			 * Special case when base register is unused if mod = 0
			 * and base = %rbp or %r13.
			 *
			 * Documented in:
			 * Table 2-3: 32-bit Addressing Forms with the SIB Byte
			 * Table 2-5: Special Cases of REX Encodings
			 */
			vie->disp_bytes = 4U;
		} else {
			vie->base_register = (enum cpu_reg_name)vie->base;
		}

		/*
		 * All encodings of 'index' are valid except for %rsp (4).
		 *
		 * Documented in:
		 * Table 2-3: 32-bit Addressing Forms with the SIB Byte
		 * Table 2-5: Special Cases of REX Encodings
		 */
		if (vie->index != 4U) {
			vie->index_register = (enum cpu_reg_name)vie->index;
		}

		/* 'scale' makes sense only in the context of an index register */
		if (vie->index_register < CPU_REG_LAST) {
			vie->scale = 1U << vie->ss;
		}

		vie_advance(vie);

		ret = 0;
	}

	return ret;
}

/**
 * @brief Decode Displacement Bytes.
 *
 * Decode the Displacement and Immediate Bytes (Refer to 2.1.4 Displacement and Immediate Bytes, Vol 2, Intel® 64
 * and IA-32 Architectures Software Developer's Manual Order Number: 325462-078US). Get the displacement bytes
 * which can be 1, 2, or 4 bytes; it is defined by ModR/W and SIB.
 *
 * @param[inout] vie Pointer to instruction emulation information.
 *
 * @return A value to indicate whether the operation succeeds or not.
 *
 * @retval 0 on success.
 * @retval -1 on failure to get instruction
 * @retval -EINVAL on displacement bytes error.
 *
 * @pre N/A
 *
 * @post N/A
 */
static int32_t decode_displacement(struct instr_emul_vie *vie)
{
	uint8_t n, i, x;
	int32_t ret = 0;

	union {
		uint8_t	buf[4];
		int8_t	signed8;
		int32_t	signed32;
	} u;

	n = vie->disp_bytes;
	if (n != 0U) {
		if ((n != 1U) && (n != 4U)) {
			pr_err("%s: decode_displacement: invalid disp_bytes %d", __func__, n);
			ret = -EINVAL;
		} else {

			for (i = 0U; i < n; i++) {
				if (vie_peek(vie, &x) != 0) {
					ret = -1;
					break;
				}

				u.buf[i] = x;
				vie_advance(vie);
			}

			if (ret == 0) {
				if (n == 1U) {
					vie->displacement = u.signed8;		/* sign-extended */
				} else {
					vie->displacement = u.signed32;		/* sign-extended */
				}
			}
		}
	}

	return ret;
}

/**
 * @brief Decode Immediate bytes.
 *
 * Decode the Immediate Bytes (Refer to 2.1.4 Displacement and Immediate Bytes, Vol 2, Intel® 64
 * and IA-32 Architectures Software Developer's Manual Order Number: 325462-078US). Get the Immediate bytes
 * which can be 1, 2, or 4 bytes; it is consistent with operand size which define by Instruction Prefixes.
 *
 * @param[inout] vie Pointer to instruction emulation information.
 *
 * @return A value to indicate whether the operation succeeds or not.
 *
 * @retval 0 on success.
 * @retval -1 on failure to get instruction
 * @retval -EINVAL on immediate operand size error.
 *
 * @pre N/A
 *
 * @post N/A
 */
static int32_t decode_immediate(struct instr_emul_vie *vie)
{
	uint8_t i, n, x;
	int32_t ret = 0;
	union {
		uint8_t	buf[4];
		int8_t	signed8;
		int16_t	signed16;
		int32_t	signed32;
	} u;

	/* Figure out immediate operand size (if any) */
	if ((vie->op.op_flags & VIE_OP_F_IMM) != 0U) {
		/*
		 * Section 2.2.1.5 "Immediates", Intel SDM:
		 * In 64-bit mode the typical size of immediate operands
		 * remains 32-bits. When the operand size if 64-bits, the
		 * processor sign-extends all immediates to 64-bits prior
		 * to their use.
		 */
		if ((vie->opsize == 4U) || (vie->opsize == 8U)) {
			vie->imm_bytes = 4U;
		}
		else {
			vie->imm_bytes = 2U;
		}
	} else if ((vie->op.op_flags & VIE_OP_F_IMM8) != 0U) {
		vie->imm_bytes = 1U;
	} else {
		/* No op_flag on immediate operand size */
	}

	n = vie->imm_bytes;
	if (n != 0U) {
		if ((n != 1U) && (n != 2U) && (n != 4U)) {
			pr_err("%s: invalid number of immediate bytes: %d", __func__, n);
			ret = -EINVAL;
		} else {
			for (i = 0U; i < n; i++) {
				if (vie_peek(vie, &x) != 0) {
					ret = -1;
					break;
				}

				u.buf[i] = x;
				vie_advance(vie);
			}

			if (ret == 0) {
				/* sign-extend the immediate value before use */
				if (n == 1U) {
					vie->immediate = u.signed8;
				} else if (n == 2U) {
					vie->immediate = u.signed16;
				} else {
					vie->immediate = u.signed32;
				}
			}
		}
	}

	return ret;
}

/**
 * @brief Decode direct memory-offset.
 *
 * Decode the Direct Memory-Offset(Refer to 2.2.1.4 Direct Memory-Offset MOVs, Vol 2, Intel® 64
 * and IA-32 Architectures Software Developer's Manual Order Number: 325462-078US).
 * In 64-bit mode, direct memory-offset forms of the MOV instruction are extended to specify a 64-bit immediate
 * absolute address. For MOV instructions, the size of the memory offset follows the address-size.
 *
 * @param[inout] vie Pointer to instruction emulation information.
 *
 * @return A value to indicate whether the operation succeeds or not.
 *
 * @retval 0 on success.
 * @retval -1 on failure to get instruction
 * @retval -EINVAL on address size error.
 *
 * @pre N/A
 *
 * @post N/A
 */
static int32_t decode_moffset(struct instr_emul_vie *vie)
{
	uint8_t i, n, x;
	int32_t ret = 0;
	union {
		uint8_t  buf[8];
		uint64_t u64;
	} u;

	if ((vie->op.op_flags & VIE_OP_F_MOFFSET) != 0U) {
		/*
		 * Section 2.2.1.4, "Direct Memory-Offset MOVs", Intel SDM:
		 * The memory offset size follows the address-size of the instruction.
		 */
		n = vie->addrsize;
		if ((n != 2U) && (n != 4U) && (n != 8U)) {
			pr_err("%s: invalid moffset bytes: %hhu", __func__, n);
			ret = -EINVAL;
		} else {
			u.u64 = 0UL;
			for (i = 0U; i < n; i++) {
				if (vie_peek(vie, &x) != 0) {
					ret = -1;
					break;
				}

				u.buf[i] = x;
				vie_advance(vie);
			}
			if (ret == 0) {
				vie->displacement = (int64_t)u.u64;
			}
		}
	}

	return ret;
}

/**
 * @brief Decode instruction.
 *
 * Decode the instruction saved in the VIE pointer. (The instruction format refers to Figure 2-1, Intel 64 and
 * IA-32 Architectures Instruction Format, Vol 2, Intel® 64 and IA-32 Architectures Software Developer's Manual
 * Order Number: 325462-078US) The instruction consists of optional prefixes, primary opcode bytes, an
 * addressing-form specifier consisting of the ModR/M byte and the SIB, a displacement, an immediate data field,
 * and sometimes a memory offset. Instruction encodings are a subset of these bytes.
 * In this function, the instruction is examined byte by byte, translated according to definitions in the SDM,
 * and set to members in the \p instr_emul_vie.
 *
 * @param[in] cpu_mode vCPU's CPU mode.
 * @param[in] cs_d Guest CS default bits: true for 32-bit default, false for 16-bit default.
 * @param[inout] vie Pointer to instruction emulation information.
 *
 * @return 0 on success, -1 on decode instruction failure.
 *
 * @pre N/A
 *
 * @post N/A
 */
static int32_t local_decode_instruction(enum vm_cpu_mode cpu_mode,
				bool cs_d, struct instr_emul_vie *vie)
{
	int32_t ret;

	if (decode_prefixes(vie, cpu_mode, cs_d) != 0) {
		ret = -1;
	} else if (decode_opcode(vie) != 0) {
		ret = -1;
	} else if (decode_modrm(vie, cpu_mode) != 0) {
		ret = -1;
	} else if (decode_sib(vie) != 0) {
		ret = -1;
	} else if (decode_displacement(vie) != 0) {
		ret = -1;
	} else if (decode_immediate(vie) != 0) {
		ret = -1;
	} else if (decode_moffset(vie) != 0) {
		ret = -1;
	} else {
		vie->decoded = 1U;	/* success */
		ret = 0;
	}

	return ret;
}

/**
 * @brief Check destination operand.
 *
 * Check the destination operand for MOVS, which addresses in ES:(E)DI. If unavailable, inject an exception to
 * the guest accordingly. In late instruction emulation, it will always succeed.
 *
 * @param[in] vcpu Pointer to the vCPU that processes the instruction.
 *
 * @return 0 on success, -EFAULT for GVA check failure.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark For MOVS only.
 * @remark Inject an exception to the guest accordingly on failure.
 * @remark During the instruction decode phase only.
 */
static int32_t instr_check_di(struct acrn_vcpu *vcpu)
{
	int32_t ret;
	struct instr_emul_vie *vie = &vcpu->inst_ctxt.vie;
	uint64_t gva;

	ret = get_gva_di_check(vcpu, vie, vie->addrsize, &gva);

	if (ret < 0) {
		ret = -EFAULT;
	} else {
		ret = 0;
	}

	return ret;
}

/**
 * @brief Check guest virtual address.
 *
 * This function first calculate the GVA base on the CPU mode, then it check whether GVA is valid
 * by the guest memory page tables. If this GVA is not translated by the guest memory page tables, it
 * would inject an exception(PF) to the guest accordingly.
 *
 * @param[inout] vcpu Pointer to the vCPU that processes the instruction.
 * @param[in] cpu_mode vCPU's CPU mode.
 *
 * @return A value to indicate whether the operation succeeds or not.
 *
 * @retval 0 on success.
 * @retval -EFAULT on canonical check failure or paging fault.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark Inject an exception to the guest accordingly on failure.
 * @remark During the instruction decode phase only.
 */
static int32_t instr_check_gva(struct acrn_vcpu *vcpu, enum vm_cpu_mode cpu_mode)
{
	int32_t ret = 0;
	uint64_t base, segbase, idx, gva, gpa;
	uint32_t err_code;
	enum cpu_reg_name seg;
	struct instr_emul_vie *vie = &vcpu->inst_ctxt.vie;

	base = 0UL;
	if (vie->base_register != CPU_REG_LAST) {
		base = vm_get_register(vcpu, vie->base_register);

		/* RIP relative addressing starts from the
		 * following instruction
		 */
		if (vie->base_register == CPU_REG_RIP) {
			base += vie->num_processed;
		}

	}

	idx = 0UL;
	if (vie->index_register != CPU_REG_LAST) {
		idx = vm_get_register(vcpu, vie->index_register);
	}

	/* "Specifying a Segment Selector" of SDM Vol1 3.7.4
	 *
	 * In legacy IA-32 mode, when ESP or EBP register is used as
	 * base, the SS segment is default segment.
	 *
	 * All data references, except when relative to stack or
	 * string destination, DS is default segment.
	 *
	 * segment override could overwrite the default segment
	 *
	 * 64bit mode, segmentation is generally disabled. The
	 * exception are FS and GS.
	 */
	if (vie->seg_override != 0U) {
		seg = vie->segment_register;
	} else if ((vie->base_register == CPU_REG_RSP) ||
			(vie->base_register == CPU_REG_RBP)) {
		seg = CPU_REG_SS;
	} else {
		seg = CPU_REG_DS;
	}

	if ((cpu_mode == CPU_MODE_64BIT) && (seg != CPU_REG_FS) &&
			(seg != CPU_REG_GS)) {
		segbase = 0UL;
	} else {
		struct seg_desc desc;

		vm_get_seg_desc(seg, &desc);

		segbase = desc.base;
	}

	gva = segbase + base + (uint64_t)vie->scale * idx + (uint64_t)vie->displacement;
	vie->gva = gva;

	if (vie_canonical_check(cpu_mode, gva) != 0) {
		if (seg == CPU_REG_SS) {
			vcpu_inject_ss(vcpu);
		} else {
			vcpu_inject_gp(vcpu, 0U);
		}
		ret = -EFAULT;
	} else {
		err_code = (vcpu->req.reqs.mmio_request.direction == ACRN_IOREQ_DIR_WRITE) ? PAGE_FAULT_WR_FLAG : 0U;

		ret = gva2gpa(vcpu, gva, &gpa, &err_code);
		if (ret < 0) {
			(void)handle_gva_translation_error(ret, vcpu, gva, err_code);
		} else {
			ret = 0;
		}
	}

	return ret;
}

/**
 * @brief Decode VM instruction.
 *
 * This function decodes the instruction which causes VM-exit.
 * It first copy the instruction from this Guest RIP, then it decodes the instructions following Vol 2, Intel® 64
 * and IA-32 Architectures Software Developer's Manual Order Number: 325462-078US. Now ACRN only supports \p CMP,
 * \p MOV, \p MOVZX, \p MOVSX, \p MOVS, \p STOS, \p AND, \p TEST, \p OR, \p SUB, \p BT, XCHG.
 * It also performs some checks during the decode, such as whether the GVA is valid: is it a valid canonical
 * address, and whether di/si is valid for MOVS. If any check fails, inject an exception accordingly.
 *
 * @param[inout] vcpu Pointer to the vCPU that processes the instruction.
 * @param[in] full_decode Inject exception (UD) for unknown instruction.
 *
 * @return A value to indicate whether the operation succeeds or not.
 *
 * @retval 0 on success.
 * @retval -1 if the instruction is unknown and full_decode is false, or the cpu is in real mode.
 * @retval -EFAULT on paging fault while fetching instruction or operand.
 * @retval memory_operand_byte_size if instruction check fails.
 *
 * @pre N/A
 *
 * @post N/A
 *
 * @remark For unknown instructions, when full_decode is false, will keep retval = -1, and do not inject exception(UD).
 * @remark If decoding the instruction is successful, instruction emulation will always succeed.
 */
int32_t decode_instruction(struct acrn_vcpu *vcpu, bool full_decode)
{
	struct instr_emul_ctxt *emul_ctxt;
	uint32_t csar;
	int32_t retval;
	enum vm_cpu_mode cpu_mode;

	cpu_mode = get_vcpu_mode(vcpu);
	if (cpu_mode == CPU_MODE_REAL) {
		pr_err("Instruction emulation doesn't support under REAL MODE(vm %d, cpu %d)\n",
			vcpu->vm->vm_id, vcpu->vcpu_id);
		retval = -1;
	} else {
		emul_ctxt = &vcpu->inst_ctxt;
		retval = vie_init(&emul_ctxt->vie, vcpu);

		if (retval < 0) {
			if (retval != -EFAULT) {
				pr_err("init vie failed @ 0x%016lx:", vcpu_get_rip(vcpu));
			}
		} else {
			csar = exec_vmread32(VMX_GUEST_CS_ATTR);

			retval = local_decode_instruction(cpu_mode, seg_desc_def32(csar), &emul_ctxt->vie);

			if (retval != 0) {
				if (full_decode) {
					pr_err("decode instruction failed @ 0x%016lx:", vcpu_get_rip(vcpu));
					vcpu_inject_ud(vcpu);
					retval = -EFAULT;
				}
			} else {
				vcpu->arch.inst_len = emul_ctxt->vie.num_processed;

				/*
				* We do operand check in instruction decode phase and
				* inject exception accordingly. In late instruction
				* emulation, it will always success.
				*
				* We only need to do dst check for movs. For other instructions,
				* they always has one register and one mmio which trigger EPT
				* by access mmio. With VMX enabled, the related check is done
				* by VMX itself before hit EPT violation.
				*
				*/
				if ((emul_ctxt->vie.op.op_flags & VIE_OP_F_CHECK_GVA_DI) != 0U) {
					retval = instr_check_di(vcpu);
				} else {
					retval = instr_check_gva(vcpu, cpu_mode);
				}

				if (retval >= 0) {
					/* return the Memory Operand byte size */
					if ((emul_ctxt->vie.op.op_flags & VIE_OP_F_BYTE_OP) != 0U) {
						retval = 1;
					} else if ((emul_ctxt->vie.op.op_flags & VIE_OP_F_WORD_OP) != 0U) {
						retval = 2;
					} else {
						retval = (int32_t)emul_ctxt->vie.opsize;
					}
				}
			}
		}
	}

	return retval;
}

/**
 * @brief Emulate instruction which caused a trap.
 *
 * This function emulates the instruction behavior following  Vol 2, Intel® 64 and IA-32 Architectures Software
 * Developer's Manual Order Number: 325462-078US. Current ACRN Hypervisor only supports \p CMP, \p MOV, \p MOVZX,
 * \p MOVSX, \p MOVS, \p STOS, \p AND, \p TEST, \p OR, \p SUB, \p BT, XCHG.
 *
 * @param[inout] vcpu Pointer to the vCPU that processes the instruction.
 *
 * @retval 0       Success
 * @retval -EINVAL Unsupported opcodes or error occurs during copy process.
 * @retval -EFAULT A page fault occurs during GPA to GPA address translation.
 *
 * @pre N/A
 *
 * @post N/A
 */
int32_t emulate_instruction(struct acrn_vcpu *vcpu)
{
	struct instr_emul_vie *vie = &vcpu->inst_ctxt.vie;
	int32_t error;

	if (vie->decoded != 0U) {
		switch (vie->op.op_type) {
		case VIE_OP_TYPE_GROUP1:
			error = emulate_group1(vcpu, vie);
			break;
		case VIE_OP_TYPE_CMP:
			error = emulate_cmp(vcpu, vie);
			break;
		case VIE_OP_TYPE_MOV:
			error = emulate_mov(vcpu, vie);
			break;
		case VIE_OP_TYPE_MOVSX:
		case VIE_OP_TYPE_MOVZX:
			error = emulate_movx(vcpu, vie);
			break;
		case VIE_OP_TYPE_MOVS:
			error = emulate_movs(vcpu, vie);
			break;
		case VIE_OP_TYPE_STOS:
			error = emulate_stos(vcpu, vie);
			break;
		case VIE_OP_TYPE_AND:
			error = emulate_and(vcpu, vie);
			break;
		case VIE_OP_TYPE_TEST:
			error = emulate_test(vcpu, vie);
			break;
		case VIE_OP_TYPE_OR:
			error = emulate_or(vcpu, vie);
			break;
		case VIE_OP_TYPE_SUB:
			error = emulate_sub(vcpu, vie);
			break;
		case VIE_OP_TYPE_BITTEST:
			error = emulate_bittest(vcpu, vie);
			break;
#ifdef CONFIG_LOCK_INSTRUCTION_EMULATION
		case VIE_OP_TYPE_XCHG:
			if (vcpu->arch.emulating_lock) {
				error = emulate_xchg_for_splitlock(vcpu, vie);
			} else {
				error = -EINVAL;
			}
			break;
#endif
		default:
			error = -EINVAL;
			break;
		}
	} else {
		error = -EINVAL;
	}

	return error;
}

/**
 * @brief Check if the opcode is XCHG (Exchange).
 *
 * Check the instruction to emulate for a vCPU. If the opcode is XCHG (Exchange the contents of operands),
 * return true.
 *
 * @param[in] vcpu Pointer to the vCPU that processes the instruction.
 *
 * @return True if the opcode is exchange, false otherwise.
 *
 * @pre N/A
 *
 * @post N/A
 */
bool is_current_opcode_xchg(struct acrn_vcpu *vcpu)
{
	return (vcpu->inst_ctxt.vie.op.op_type == VIE_OP_TYPE_XCHG);
}

/**
 * @}
 */