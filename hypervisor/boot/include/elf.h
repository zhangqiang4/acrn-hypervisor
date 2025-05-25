/*
 * Copyright (C) 2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 *  SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017, 2018 Dell EMC
 * Copyright (c) 2000, 2001, 2008, 2011, David E. O'Brien
 * Copyright (c) 1998 John D. Polstra.
 * All rights reserved.
 */

/**
 * @addtogroup vp-base_vboot
 *
 * @{
 */

/**
 * @file
 * @brief Structure and Macro definition for ELF format file.
 *
 * Structure and Macro definition for Headers of 32-bit and 64-bit ELF files.
 *
 * @remark please refer source in https://github.com/freebsd/freebsd-src/blob/main/sys/sys/elf_common.h
 */

#ifndef	ELF_H
#define	ELF_H

/**
 * @defgroup indexes_to_e_ident Indexes into the e_ident Array in ELF Header
 *
 * @brief Indexes into the e_ident Array in ELF Header
 *
 * These macros define indexes into the `e_ident` array of an ELF file header.
 * The `e_ident` array contains identification fields that provide essential
 * information about the ELF file format, including its magic number, class,
 * data format, version, and ABI compatibility. These indexes are used to
 * access specific fields within the array.
 *
 * @remark These indexes should be kept synchronized with the ELF specification
 *       as outlined in the System V Application Binary Interface (ABI) documentation.
 *       http://www.sco.com/developers/gabi/latest/ch4.eheader.html
 *
 * @{
 */
#define	EI_MAG0			0	/**< Magic number, byte 0. */
#define	EI_MAG1			1	/**< Magic number, byte 1. */
#define	EI_MAG2			2	/**< Magic number, byte 2. */
#define	EI_MAG3			3	/**< Magic number, byte 3. */
#define	EI_CLASS		4	/**< File's class, or capacity. */
#define	EI_DATA			5	/**< Data encoding. */
#define	EI_VERSION		6	/**< ELF header version number. */
#define	EI_OSABI		7	/**< Operating system / ABI identification */
#define	EI_ABIVERSION		8	/**< ABI version */
#define	OLD_EI_BRAND		8	/**< Start of architecture identification. */
#define	EI_PAD			9	/**< Start of padding bytes (per SVR4 ABI). */
#define	EI_NIDENT		16	/**< Size of e_ident array. */
/**
 * @}
 */

/**
 * @brief ELF Magic Number
 *
 * This macro defines the magic number used to identify ELF (Executable and Linkable
 * Format) files. The magic number is a sequence of bytes located at the beginning
 * of the ELF header, and it is used to verify that a file is in the ELF format.
 * The specific value of the magic number is `0x464C457f`, which corresponds to
 * the ASCII characters "ELF" followed by a byte with the value `0x7f`.
 *
 * The magic number is crucial for loaders and operating systems to recognize and
 * process ELF files correctly.
 */
#define ELFMAGIC		0x464C457f

/**
 * @defgroup elfclass_value Values for e_ident[CLASS] in ELF Header
 *
 * @brief Values for e_ident[EI_CLASS] in ELF Header
 *
 * These macros define values for the `e_ident[EI_CLASS]` field in the ELF header.
 * This field specifies the class of the ELF file, indicating whether it is intended
 * for a 32-bit or 64-bit architecture. The class is a crucial part of the ELF
 * identification, helping loaders and operating systems determine how to process
 * the file.
 *
 * @{
 */
#define	ELFCLASSNONE		0	/**< Unknown class. */
#define	ELFCLASS32		1	/**< 32-bit architecture. */
#define	ELFCLASS64		2	/**< 64-bit architecture. */
/**
 * @}
 */

/**
 * @defgroup e_type_value Values for e_type in ELF Header
 *
 * @brief Values for e_type in ELF Header
 *
 * These macros define values for the `e_type` field in the ELF header. This field
 * specifies the type of the ELF file, indicating its intended use, such as executable,
 * shared object, or core file. The type is a crucial part of the ELF identification,
 * helping loaders and operating systems determine how to process the file.
 *
 * @{
 */
#define	ET_NONE			0	/**< Unknown type. */
#define	ET_REL			1	/**< Relocatable. */
#define	ET_EXEC			2	/**< Executable. */
#define	ET_DYN			3	/**< Shared object. */
#define	ET_CORE			4	/**< Core file. */
#define	ET_LOOS			0xfe00	/**< First operating system specific. */
#define	ET_HIOS			0xfeff	/**< Last operating system-specific. */
#define	ET_LOPROC		0xff00	/**< First processor-specific. */
#define	ET_HIPROC		0xffff	/**< Last processor-specific. */
/**
 * @}
 */

/**
 * @defgroup p_type_value Values for p_type in ELF Program Header
 *
 * @brief Values for p_type in ELF Program Header
 *
 * These macros define values for the `p_type` field in the program header of an ELF
 * file. This field specifies the type of segment described by the program header
 * entry, indicating its role in the ELF file. The segment type helps loaders and
 * operating systems determine how to handle each segment during execution.
 *
 * @{
 */
#define	PT_NULL			0	/**< Unused entry. */
#define	PT_LOAD			1	/**< Loadable segment. */
#define	PT_DYNAMIC		2	/**< Dynamic linking information segment. */
#define	PT_INTERP		3	/**< Pathname of interpreter. */
#define	PT_NOTE			4	/**< Auxiliary information. */
#define	PT_SHLIB		5	/**< Reserved (not used). */
#define	PT_PHDR			6	/**< Location of program header itself. */
#define	PT_TLS			7	/**< Thread local storage segment */
/**
 * @}
 */

/**
 * @brief ELF64 Header Structure
 *
 * This structure represents the header of an ELF (Executable and Linkable Format)
 * file for 64-bit architectures. It contains essential information about the ELF
 * file, including its identification, type, machine, entry point, and various
 * offsets and sizes. This information is used by the operating system or loader
 * to interpret and execute the ELF file correctly.
 */
struct elf64_hdr {
	uint8_t e_ident[EI_NIDENT]; /**< ELF identification. */
	uint16_t e_type;            /**< Object file type. */
	uint16_t e_machine;         /**< Machine type. */
	uint32_t e_version;         /**< Object file version. */
	uint64_t e_entry;           /**< Entry point address. */
	uint64_t e_phoff;           /**< Program header offset. */
	uint64_t e_shoff;           /**< Section header offset. */
	uint32_t e_flags;           /**< Processor-specific flags. */
	uint16_t e_ehsize;          /**< ELF header size. */
	uint16_t e_phentsize;       /**< Size of program header entry. */
	uint16_t e_phnum;           /**< Number of program header entries. */
	uint16_t e_shentsize;       /**< Size of section header entry. */
	uint16_t e_shnum;           /**< Number of section header entries. */
	uint16_t e_shstrndx;        /**< Section name string table index. */
};

/**
 * @brief ELF64 Program Header Entry Structure
 *
 * This structure represents an entry in the program header table of an ELF
 * (Executable and Linkable Format) file for 64-bit architectures. Each entry
 * describes a segment in the ELF file, including its type, attributes, and
 * memory layout. This information is used by the operating system or loader
 * to map segments into memory and prepare them for execution.
 */
struct elf64_prog_entry {
	uint32_t p_type;   /**< Type of segment. */
	uint32_t p_flags;  /**< Segment attributes. */
	uint64_t p_offset; /**< Offset in file. */
	uint64_t p_vaddr;  /**< Virtual address in memory. */
	uint64_t p_paddr;  /**< Physical address in memory. */
	uint64_t p_filesz; /**< Size of segment in file. */
	uint64_t p_memsz;  /**< Size of segment in memory. */
	uint64_t p_align;  /**< Alignment of segment. */
};

/**
 * @brief ELF64 Section Header Entry Structure
 *
 * This structure represents an entry in the section header table of an ELF
 * (Executable and Linkable Format) file for 64-bit architectures. Each entry
 * describes a section in the ELF file, including its name, type, attributes,
 * and memory layout. This information is used by the operating system or loader
 * to interpret and manage sections within the ELF file.
 */
struct elf64_sec_entry {
	uint32_t sh_name;      /**< Section name index. */
	uint32_t sh_type;      /**< Section type. */
	uint64_t sh_flags;     /**< Section attributes. */
	uint64_t sh_addr;      /**< Virtual address in memory. */
	uint64_t sh_offset;    /**< Offset in file. */
	uint64_t sh_size;      /**< Size of section. */
	uint32_t sh_link;      /**< Link to other section. */
	uint32_t sh_info;      /**< Miscellaneous information. */
	uint64_t sh_addralign; /**< Address alignment boundary. */
	uint64_t sh_entsize;   /**< Size of entries, if section has a table. */
};

/**
 * @brief ELF32 Header Structure
 *
 * This structure represents the header of an ELF (Executable and Linkable Format)
 * file for 32-bit architectures. It contains essential information about the ELF
 * file, including its identification, type, machine, entry point, and various
 * offsets and sizes. This information is used by the operating system or loader
 * to interpret and execute the ELF file correctly.
 */
struct elf32_hdr {
	uint8_t e_ident[EI_NIDENT]; /**< ELF identification. */
	uint16_t e_type;            /**< Object file type. */
	uint16_t e_machine;         /**< Machine type. */
	uint32_t e_version;         /**< Object file version. */
	uint32_t e_entry;           /**< Entry point address. */
	uint32_t e_phoff;           /**< Program header offset. */
	uint32_t e_shoff;           /**< Section header offset. */
	uint32_t e_flags;           /**< Processor-specific flags. */
	uint16_t e_ehsize;          /**< ELF header size. */
	uint16_t e_phentsize;       /**< Size of program header entry. */
	uint16_t e_phnum;           /**< Number of program header entries. */
	uint16_t e_shentsize;       /**< Size of section header entry. */
	uint16_t e_shnum;           /**< Number of section header entries. */
	uint16_t e_shstrndx;        /**< Section name string table index. */
};

/**
 * @brief ELF32 Program Header Entry Structure
 *
 * This structure represents an entry in the program header table of an ELF
 * (Executable and Linkable Format) file for 32-bit architectures. Each entry
 * describes a segment in the ELF file, including its type, attributes, and
 * memory layout. This information is used by the operating system or loader
 * to map segments into memory and prepare them for execution.
 */
struct elf32_prog_entry {
	uint32_t p_type;   /**< Type of segment. */
	uint32_t p_offset; /**< Offset in file. */
	uint32_t p_vaddr;  /**< Virtual address in memory. */
	uint32_t p_paddr;  /**< Physical address in memory. */
	uint32_t p_filesz; /**< Size of segment in file. */
	uint32_t p_memsz;  /**< Size of segment in memory. */
	uint32_t p_flags;  /**< Segment attributes. */
	uint32_t p_align;  /**< Alignment of segment. */
};

/**
 * @brief ELF32 Section Header Entry Structure
 *
 * This structure represents an entry in the section header table of an ELF
 * (Executable and Linkable Format) file for 32-bit architectures. Each entry
 * describes a section in the ELF file, including its name, type, attributes,
 * and memory layout. This information is used by the operating system or loader
 * to interpret and manage sections within the ELF file.
 */
struct elf32_sec_entry {
	uint32_t sh_name;      /**< Section name index. */
	uint32_t sh_type;      /**< Section type. */
	uint32_t sh_flags;     /**< Section attributes. */
	uint32_t sh_addr;      /**< Virtual address in memory. */
	uint32_t sh_offset;    /**< Offset in file. */
	uint32_t sh_size;      /**< Size of section. */
	uint32_t sh_link;      /**< Link to other section. */
	uint32_t sh_info;      /**< Miscellaneous information. */
	uint32_t sh_addralign; /**< Address alignment boundary. */
	uint32_t sh_entsize;   /**< Size of entries, if section has a table. */
};
#endif /* !ELF_H */
/**
 * @}
 */
