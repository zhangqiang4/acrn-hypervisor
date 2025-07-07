/*
 * Copyright (C) 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef SHELL_PRIV_H
#define SHELL_PRIV_H

#include <asm/lib/spinlock.h>

#define SHELL_CMD_MAX_LEN		100U
#define SHELL_STRING_MAX_LEN		(PAGE_SIZE << 2U)

extern int16_t console_vmid;

/* Shell Command Function */
typedef int32_t (*shell_cmd_fn_t)(int32_t argc, char **argv);

/* Shell Command */
struct shell_cmd {
	char *str;		/* Command string */
	char *cmd_param;	/* Command parameter string */
	char *help_str;		/* Help text associated with the command */
	shell_cmd_fn_t fcn;	/* Command call-back function */

};

#define MAX_BUFFERED_CMDS 8

/* Shell Control Block */
struct shell {
	/* a ring buffer to buffer former commands and use one as current active input */
	char buffered_line[MAX_BUFFERED_CMDS][SHELL_CMD_MAX_LEN + 1U];
	uint32_t input_line_len;	/* Length of current input line */
	int32_t input_line_active;	/* Active input line index */

	int32_t to_select_index; /* used for up/down key to select former cmds */
	uint32_t cursor_offset; /* cursor offset position from left input line */

	struct shell_cmd *cmds;	/* cmds supported */
	uint32_t cmd_count;		/* Count of cmds supported */
};

/* Shell Command list with parameters and help description */
#define SHELL_CMD_HELP			"help"
#define SHELL_CMD_HELP_PARAM		NULL
#define SHELL_CMD_HELP_HELP		"Display information about supported hypervisor shell commands"

#define SHELL_CMD_VERSION		"version"
#define SHELL_CMD_VERSION_PARAM		NULL
#define SHELL_CMD_VERSION_HELP		"Display the HV version information"

#define SHELL_CMD_VM_LIST		"vm_list"
#define SHELL_CMD_VM_LIST_PARAM		NULL
#define SHELL_CMD_VM_LIST_HELP		"List all VMs, displaying the VM UUID, ID, name and state"

#define SHELL_CMD_VCPU_LIST		"vcpu_list"
#define SHELL_CMD_VCPU_LIST_PARAM	NULL
#define SHELL_CMD_VCPU_LIST_HELP	"List all vCPUs in all VMs"

#define SHELL_CMD_VCPU_DUMPREG		"vcpu_dumpreg"
#define SHELL_CMD_VCPU_DUMPREG_PARAM	"<vm id, vcpu id>"
#define SHELL_CMD_VCPU_DUMPREG_HELP	"Dump registers for a specific vCPU"

#define SHELL_CMD_DUMP_HOST_MEM		"dump_host_mem"
#define SHELL_CMD_DUMP_HOST_MEM_PARAM	"[-h] <addr, length>"
#define SHELL_CMD_DUMP_HOST_MEM_HELP	"Dump host memory, starting at a given address(Hex) aligned to 4, and for" \
					" a given length (Dec in bytes).  'dump_host_mem -h' Show available address range."

#define SHELL_CMD_DUMP_GUEST_MEM	"dump_guest_mem"
#define SHELL_CMD_DUMP_GUEST_MEM_PARAM  "<vm_id, [-p], addr, length>"
#define SHELL_CMD_DUMP_GUEST_MEM_HELP   "Dump guest memory, vm id(Dec), starting at a given virtual/physical address"\
					"(Hex, default virtual address), and for a given length (Dec in bytes). "\
					"Use option -p to indicate a guest physical address."

#define SHELL_CMD_VM_CONSOLE		"vm_console"
#define SHELL_CMD_VM_CONSOLE_PARAM	"<vm id>"
#define SHELL_CMD_VM_CONSOLE_HELP	"Switch to the VM's console. Use 'BREAK + e' to return to the ACRN shell "\
					"console"

#define SHELL_CMD_INTERRUPT		"int"
#define SHELL_CMD_INTERRUPT_PARAM	NULL
#define SHELL_CMD_INTERRUPT_HELP	"List interrupt information per CPU"

#define SHELL_CMD_PTDEV			"pt"
#define SHELL_CMD_PTDEV_PARAM		NULL
#define SHELL_CMD_PTDEV_HELP		"Show pass-through device information"

#define SHELL_CMD_REBOOT		"reboot"
#define SHELL_CMD_REBOOT_PARAM		NULL
#define SHELL_CMD_REBOOT_HELP		"Trigger a system reboot (immediately)"

#define SHELL_CMD_COREDUMP		"coredump"
#define SHELL_CMD_COREDUMP_PARAM	NULL
#define SHELL_CMD_COREDUMP_HELP		"Trigger a system full coredump (immediately,need support of bootloader)"

#define SHELL_CMD_VM_RESUME		"vm_resume"
#define SHELL_CMD_VM_RESUME_PARAM	"<vm id>"
#define SHELL_CMD_VM_RESUME_HELP	"Resume VM from S3"

#define SHELL_CMD_IOAPIC		"dump_ioapic"
#define SHELL_CMD_IOAPIC_PARAM		NULL
#define SHELL_CMD_IOAPIC_HELP		"Show native IOAPIC information"

#define SHELL_CMD_DUMP_VCON_OUT		"dump_vm_console_out"
#define SHELL_CMD_DUMP_VCON_OUT_PARAM	"<vm_id>"
#define SHELL_CMD_DUMP_VCON_OUT_HELP	"Dump VM's console output buffer data as string, vm id(Dec)"

#define SHELL_CMD_VIOAPIC		"vioapic"
#define SHELL_CMD_VIOAPIC_PARAM		"<vm id>"
#define SHELL_CMD_VIOAPIC_HELP		"Show virtual IOAPIC (vIOAPIC) information for a specific VM"

#define SHELL_CMD_LOG_LVL		"loglevel"
#define SHELL_CMD_LOG_LVL_PARAM		"[<console_loglevel> [<mem_loglevel> [npk_loglevel]]]"
#define SHELL_CMD_LOG_LVL_HELP		"No argument: get the level of logging for the console, memory and npk. Set "\
					"the level by giving (up to) 3 parameters between 0 and 6 (verbose)"

#define SHELL_CMD_CPUID			"cpuid"
#define SHELL_CMD_CPUID_PARAM		"<leaf> [subleaf]"
#define SHELL_CMD_CPUID_HELP		"Display the CPUID leaf [subleaf], in hexadecimal"

#define SHELL_CMD_RDMSR			"rdmsr"
#define SHELL_CMD_RDMSR_PARAM		"[-p<pcpu_id>]	<msr_index>"
#define SHELL_CMD_RDMSR_HELP		"Read the MSR at msr_index (in hexadecimal) for CPU ID pcpu_id"

#define SHELL_CMD_WRMSR			"wrmsr"
#define SHELL_CMD_WRMSR_PARAM		"[-p<pcpu_id>]	<msr_index> <value>"
#define SHELL_CMD_WRMSR_HELP		"Write value (in hexadecimal) to the MSR at msr_index (in hexadecimal) for CPU"\
					" ID pcpu_id"

#define SHELL_CMD_VMEXIT		"vmexit"
#define SHELL_CMD_VMEXIT_PARAM		"[clear | enable | disable | vm_id]"
#define SHELL_CMD_VMEXIT_HELP		"profiling vmexit, use: vmexit [clear | enable | disable | vm_id] disabled by default"

#define SHELL_CMD_DUMP_PCI		"dump_pci"
#define SHELL_CMD_DUMP_PCI_PARAM	"[-vm_id <vm_id>] <bdf, offset, length>"
#define SHELL_CMD_DUMP_PCI_HELP		"Dump PCI Configuration Space of designated device (BDF), starting at a given offset(Hex), "\
					"and for a given length (Dec in bytes, multiple of 4 bytes). The offset must align with 4 bytes. Without '-vm_id' "\
					"to dump physical devices, -vm_id <vm_id> to dump VM's devices, vm_id(Dec)."

#define SHELL_CMD_LSPCI		"lspci"
#define SHELL_CMD_LSPCI_PARAM		"[-vm_id <vm_id>]"
#define SHELL_CMD_LSPCI_HELP		"List BDF of PCI devices, without paramemter to get physical devices, -vm_id <vm_id> to get "\
					"VM's devices, vm_id(Dec). The virtual device correpsonding physical device BDF will be shown too."

int64_t strtol_deci(const char *nptr);
#endif /* SHELL_PRIV_H */
