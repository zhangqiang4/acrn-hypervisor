/*
 * Copyright (C) 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <errno.h>
#include <asm/lib/bits.h>
#include "shell_priv.h"
#include <asm/irq.h>
#include <console.h>
#include <asm/per_cpu.h>
#include <asm/vmx.h>
#include <asm/cpuid.h>
#include <asm/ioapic.h>
#include <ptdev.h>
#include <asm/guest/vm.h>
#include <sprintf.h>
#include <logmsg.h>
#include <version.h>
#include <shell.h>
#include <asm/guest/vmcs.h>
#include <asm/host_pm.h>
#include <dump.h>
#include <asm/tsc.h>
#include <asm/guest/guest_pm.h>

#define TEMP_STR_SIZE		60U
#define MAX_STR_SIZE		256U
#define SHELL_PROMPT_STR	"ACRN:\\>"

#define SHELL_LOG_BUF_SIZE		(PAGE_SIZE * MAX_PCPU_NUM)
static char shell_log_buf[SHELL_LOG_BUF_SIZE];

/* Input Line Other - Switch to the "other" input line (there are only two
 * input lines total).
 */

static int32_t shell_cmd_help(__unused int32_t argc, __unused char **argv);
static int32_t shell_version(__unused int32_t argc, __unused char **argv);
static int32_t shell_list_vm(__unused int32_t argc, __unused char **argv);
static int32_t shell_list_vcpu(__unused int32_t argc, __unused char **argv);
static int32_t shell_vcpu_dumpreg(int32_t argc, char **argv);
static int32_t shell_dump_host_mem(int32_t argc, char **argv);
static int32_t shell_dump_guest_mem(int32_t argc, char **argv);
static int32_t shell_to_vm_console(int32_t argc, char **argv);
static int32_t shell_show_cpu_int(__unused int32_t argc, __unused char **argv);
static int32_t shell_show_ptdev_info(__unused int32_t argc, __unused char **argv);
static int32_t shell_show_vioapic_info(int32_t argc, char **argv);
static int32_t shell_show_ioapic_info(__unused int32_t argc, __unused char **argv);
static int32_t shell_loglevel(int32_t argc, char **argv);
static int32_t shell_cpuid(int32_t argc, char **argv);
static int32_t shell_reboot(int32_t argc, char **argv);
#ifdef CONFIG_HV_COREDUMP_ENABLED
static int32_t shell_coredump(int32_t argc, char **argv);
#endif
static int32_t shell_rdmsr(int32_t argc, char **argv);
static int32_t shell_wrmsr(int32_t argc, char **argv);
static int32_t shell_show_vmexit_profile(__unused int argc, __unused char **argv);
static int32_t shell_dump_vm_console_out(int32_t argc, char **argv);
static int32_t shell_vm_resume(int32_t argc, char **argv);
static int32_t shell_dump_pci(int32_t argc, char **argv);
static int32_t shell_lspci(int32_t argc, char **argv);

static struct shell_cmd shell_cmds[] = {
	{
		.str		= SHELL_CMD_HELP,
		.cmd_param	= SHELL_CMD_HELP_PARAM,
		.help_str	= SHELL_CMD_HELP_HELP,
		.fcn		= shell_cmd_help,
	},
	{
		.str		= SHELL_CMD_VERSION,
		.cmd_param	= SHELL_CMD_VERSION_PARAM,
		.help_str	= SHELL_CMD_VERSION_HELP,
		.fcn		= shell_version,
	},
	{
		.str		= SHELL_CMD_VM_LIST,
		.cmd_param	= SHELL_CMD_VM_LIST_PARAM,
		.help_str	= SHELL_CMD_VM_LIST_HELP,
		.fcn		= shell_list_vm,
	},
	{
		.str		= SHELL_CMD_VCPU_LIST,
		.cmd_param	= SHELL_CMD_VCPU_LIST_PARAM,
		.help_str	= SHELL_CMD_VCPU_LIST_HELP,
		.fcn		= shell_list_vcpu,
	},
	{
		.str		= SHELL_CMD_VCPU_DUMPREG,
		.cmd_param	= SHELL_CMD_VCPU_DUMPREG_PARAM,
		.help_str	= SHELL_CMD_VCPU_DUMPREG_HELP,
		.fcn		= shell_vcpu_dumpreg,
	},
	{
		.str		= SHELL_CMD_DUMP_HOST_MEM,
		.cmd_param	= SHELL_CMD_DUMP_HOST_MEM_PARAM,
		.help_str	= SHELL_CMD_DUMP_HOST_MEM_HELP,
		.fcn		= shell_dump_host_mem,
	},
	{
		.str		= SHELL_CMD_DUMP_GUEST_MEM,
		.cmd_param	= SHELL_CMD_DUMP_GUEST_MEM_PARAM,
		.help_str	= SHELL_CMD_DUMP_GUEST_MEM_HELP,
		.fcn		= shell_dump_guest_mem,
	},
	{
		.str		= SHELL_CMD_VM_CONSOLE,
		.cmd_param	= SHELL_CMD_VM_CONSOLE_PARAM,
		.help_str	= SHELL_CMD_VM_CONSOLE_HELP,
		.fcn		= shell_to_vm_console,
	},
	{
		.str		= SHELL_CMD_INTERRUPT,
		.cmd_param	= SHELL_CMD_INTERRUPT_PARAM,
		.help_str	= SHELL_CMD_INTERRUPT_HELP,
		.fcn		= shell_show_cpu_int,
	},
	{
		.str		= SHELL_CMD_PTDEV,
		.cmd_param	= SHELL_CMD_PTDEV_PARAM,
		.help_str	= SHELL_CMD_PTDEV_HELP,
		.fcn		= shell_show_ptdev_info,
	},
	{
		.str		= SHELL_CMD_VIOAPIC,
		.cmd_param	= SHELL_CMD_VIOAPIC_PARAM,
		.help_str	= SHELL_CMD_VIOAPIC_HELP,
		.fcn		= shell_show_vioapic_info,
	},
	{
		.str		= SHELL_CMD_IOAPIC,
		.cmd_param	= SHELL_CMD_IOAPIC_PARAM,
		.help_str	= SHELL_CMD_IOAPIC_HELP,
		.fcn		= shell_show_ioapic_info,
	},
	{
		.str		= SHELL_CMD_LOG_LVL,
		.cmd_param	= SHELL_CMD_LOG_LVL_PARAM,
		.help_str	= SHELL_CMD_LOG_LVL_HELP,
		.fcn		= shell_loglevel,
	},
	{
		.str		= SHELL_CMD_CPUID,
		.cmd_param	= SHELL_CMD_CPUID_PARAM,
		.help_str	= SHELL_CMD_CPUID_HELP,
		.fcn		= shell_cpuid,
	},
	{
		.str		= SHELL_CMD_REBOOT,
		.cmd_param	= SHELL_CMD_REBOOT_PARAM,
		.help_str	= SHELL_CMD_REBOOT_HELP,
		.fcn		= shell_reboot,
	},
#ifdef CONFIG_HV_COREDUMP_ENABLED
	{
		.str		= SHELL_CMD_COREDUMP,
		.cmd_param	= SHELL_CMD_COREDUMP_PARAM,
		.help_str	= SHELL_CMD_COREDUMP_HELP,
		.fcn		= shell_coredump,
	},
#endif
	{
		.str		= SHELL_CMD_RDMSR,
		.cmd_param	= SHELL_CMD_RDMSR_PARAM,
		.help_str	= SHELL_CMD_RDMSR_HELP,
		.fcn		= shell_rdmsr,
	},
	{
		.str		= SHELL_CMD_WRMSR,
		.cmd_param	= SHELL_CMD_WRMSR_PARAM,
		.help_str	= SHELL_CMD_WRMSR_HELP,
		.fcn		= shell_wrmsr,
	},
	{
		.str		= SHELL_CMD_VMEXIT,
		.cmd_param	= SHELL_CMD_VMEXIT_PARAM,
		.help_str	= SHELL_CMD_VMEXIT_HELP,
		.fcn		= shell_show_vmexit_profile,
	},
	{
		.str		= SHELL_CMD_DUMP_VCON_OUT,
		.cmd_param	= SHELL_CMD_DUMP_VCON_OUT_PARAM,
		.help_str	= SHELL_CMD_DUMP_VCON_OUT_HELP,
		.fcn		= shell_dump_vm_console_out,
	},
	{
		.str		= SHELL_CMD_VM_RESUME,
		.cmd_param	= SHELL_CMD_VM_RESUME_PARAM,
		.help_str	= SHELL_CMD_VM_RESUME_HELP,
		.fcn		= shell_vm_resume,
	},
	{
		.str		= SHELL_CMD_DUMP_PCI,
		.cmd_param	= SHELL_CMD_DUMP_PCI_PARAM,
		.help_str	= SHELL_CMD_DUMP_PCI_HELP,
		.fcn		= shell_dump_pci,
	},
	{
		.str		= SHELL_CMD_LSPCI,
		.cmd_param	= SHELL_CMD_LSPCI_PARAM,
		.help_str	= SHELL_CMD_LSPCI_HELP,
		.fcn		= shell_lspci,
	},
};

/* for function key: up/down/right/left/home/end and delete key */
enum function_key {
	KEY_NONE,

	KEY_DELETE = 0x5B33,
	KEY_UP = 0x5B41,
	KEY_DOWN = 0x5B42,
	KEY_RIGHT = 0x5B43,
	KEY_LEFT = 0x5B44,
	KEY_END = 0x5B46,
	KEY_HOME = 0x5B48,
};

/* version information */
extern const char version_info[];

extern uint16_t mem_loglevel;
extern uint16_t console_loglevel;
extern uint16_t npk_loglevel;

extern uint64_t irq_alloc_bitmap[IRQ_ALLOC_BITMAP_SIZE];

static struct shell hv_shell;
static struct shell *p_shell = &hv_shell;

static int32_t string_to_argv(char *argv_str, void *p_argv_mem,
		__unused uint32_t argv_mem_size,
		uint32_t *p_argc, char ***p_argv)
{
	uint32_t argc;
	char **argv;
	char *p_ch;

	/* Setup initial argument values. */
	argc = 0U;
	argv = NULL;

	/* Ensure there are arguments to be processed. */
	if (argv_str == NULL) {
		*p_argc = argc;
		*p_argv = argv;
		return -EINVAL;
	}

	/* Process the argument string (there is at least one element). */
	argv = (char **)p_argv_mem;
	p_ch = argv_str;

	/* Remove all spaces at the beginning of cmd*/
	while (*p_ch == ' ') {
		p_ch++;
	}

	while (*p_ch != 0) {
		/* Add argument (string) pointer to the vector. */
		argv[argc] = p_ch;

		/* Move past the vector entry argument string (in the
		 * argument string).
		 */
		while ((*p_ch != ' ') && (*p_ch != ',') && (*p_ch != 0)) {
			p_ch++;
		}

		/* Count the argument just processed. */
		argc++;

		/* Check for the end of the argument string. */
		if (*p_ch != 0) {
			/* Terminate the vector entry argument string
			 * and move to the next.
			 */
			*p_ch = 0;
			/* Remove all space in middile of cmdline */
			p_ch++;
			while (*p_ch == ' ') {
				p_ch++;
			}
		}
	}

	/* Update return parameters */
	*p_argc = argc;
	*p_argv = argv;

	return 0;
}

static struct shell_cmd *shell_find_cmd(const char *cmd_str)
{
	uint32_t i;
	struct shell_cmd *p_cmd = NULL;

	for (i = 0U; i < p_shell->cmd_count; i++) {
		p_cmd = &p_shell->cmds[i];
		if (strcmp(p_cmd->str, cmd_str) == 0) {
			return p_cmd;
		}
	}
	return NULL;
}

static char shell_getc(void)
{
	return console_getc();
}

static void shell_puts(const char *string_ptr)
{
	/* Output the string */
	(void)console_write(string_ptr, strnlen_s(string_ptr,
				SHELL_STRING_MAX_LEN));
}

static uint16_t sanitize_vmid(uint16_t vmid)
{
	uint16_t sanitized_vmid = vmid;
	char temp_str[TEMP_STR_SIZE];

	if (vmid >= CONFIG_MAX_VM_NUM) {
		snprintf(temp_str, TEMP_STR_SIZE,
			"VM ID given exceeds the MAX_VM_NUM(%u), using 0 instead\r\n",
			CONFIG_MAX_VM_NUM);
		shell_puts(temp_str);
		sanitized_vmid = 0U;
	}

	return sanitized_vmid;
}

static void clear_input_line(uint32_t len)
{
	while (len > 0) {
		len--;
		shell_puts("\b");
		shell_puts(" \b");
	}
}

static void set_cursor_pos(uint32_t left_offset)
{
	while (left_offset > 0) {
		left_offset--;
		shell_puts("\b");
	}
}

static void handle_delete_key(void)
{
	if (p_shell->cursor_offset < p_shell->input_line_len) {

		uint32_t delta = p_shell->input_line_len - p_shell->cursor_offset - 1;

		/* Send a space + backspace sequence to delete character */
		shell_puts(" \b");

		/* display the left input chars and remove former last one */
		shell_puts(p_shell->buffered_line[p_shell->input_line_active] + p_shell->cursor_offset + 1);
		shell_puts(" \b");

		set_cursor_pos(delta);

		memcpy_erms(p_shell->buffered_line[p_shell->input_line_active] + p_shell->cursor_offset,
			p_shell->buffered_line[p_shell->input_line_active] + p_shell->cursor_offset + 1, delta);

		/* Null terminate the last character to erase it */
		p_shell->buffered_line[p_shell->input_line_active][p_shell->input_line_len - 1] = 0;

		/* Reduce the length of the string by one */
		p_shell->input_line_len--;
	}
}

static void handle_updown_key(enum function_key key_value)
{
	int32_t to_select, current_select = p_shell->to_select_index;

	/* update current_select and p_shell->to_select_index as up/down key */
	if (key_value == KEY_UP) {
		/* if the ring buffer not full, just decrease one until to 0; if full, need handle overflow case */
		to_select = p_shell->to_select_index - 1;
		if (to_select < 0) {
			to_select += MAX_BUFFERED_CMDS;
		}

		if (p_shell->buffered_line[to_select][0] != '\0') {
			current_select = to_select;
		}

	} else {
		/* if down key and current is active line, not need update */
		if (p_shell->to_select_index != p_shell->input_line_active) {
			current_select = (p_shell->to_select_index + 1) % MAX_BUFFERED_CMDS;
		}
	}

	/* go up/down until first buffered cmd or current input line: user will know it is end to select */
	if (current_select != p_shell->input_line_active) {
		p_shell->to_select_index = current_select;
	}

	if (strcmp(p_shell->buffered_line[current_select], p_shell->buffered_line[p_shell->input_line_active]) != 0) {
		/* reset cursor pos and clear current input line first, then output selected cmd */
		if (p_shell->cursor_offset < p_shell->input_line_len) {
			shell_puts(p_shell->buffered_line[p_shell->input_line_active] + p_shell->cursor_offset);
		}

		clear_input_line(p_shell->input_line_len);
		shell_puts(p_shell->buffered_line[current_select]);

		size_t len = strnlen_s(p_shell->buffered_line[current_select], SHELL_CMD_MAX_LEN);

		memcpy_s(p_shell->buffered_line[p_shell->input_line_active], SHELL_CMD_MAX_LEN,
			p_shell->buffered_line[current_select], len + 1);
		p_shell->input_line_len = len;
		p_shell->cursor_offset = len;
	}
}

static void shell_handle_special_char(char ch)
{
	enum function_key key_value = KEY_NONE;

	switch (ch) {
	/* original function key value: ESC + key (2/3 bytes), so consume the next 2/3 characters */
	case 0x1b:
		key_value = (shell_getc() << 8) | shell_getc();
		if (key_value == KEY_DELETE) {
			(void)shell_getc(); /* delete key has one more byte */
		}

		switch (key_value) {
		case KEY_DELETE:
			handle_delete_key();
			break;
		case KEY_UP:
		case KEY_DOWN:
			handle_updown_key(key_value);
			break;
		case KEY_RIGHT:
			if (p_shell->cursor_offset < p_shell->input_line_len) {
				shell_puts(p_shell->buffered_line[p_shell->input_line_active] + p_shell->cursor_offset);
				p_shell->cursor_offset++;
				set_cursor_pos(p_shell->input_line_len - p_shell->cursor_offset);
			}
			break;
		case KEY_LEFT:
			if (p_shell->cursor_offset > 0) {
				p_shell->cursor_offset--;
				shell_puts("\b");
			}
			break;
		case KEY_END:
			if (p_shell->cursor_offset < p_shell->input_line_len) {
				shell_puts(p_shell->buffered_line[p_shell->input_line_active] + p_shell->cursor_offset);
				p_shell->cursor_offset = p_shell->input_line_len;
			}
			break;
		case KEY_HOME:
			if (p_shell->cursor_offset > 0) {
				set_cursor_pos(p_shell->cursor_offset);
				p_shell->cursor_offset = 0;
			}
			break;
		default:
			break;
		}

		break;
	default:
		/*
		 * Only the Escape character is treated as special character.
		 * All the other characters have been handled properly in
		 * shell_input_line, so they will not be handled in this API.
		 * Gracefully return if prior case clauses have not been met.
		 */
		break;
	}
}

static void handle_backspace_key(void)
{
	/* Ensure length is not 0 */
	if (p_shell->cursor_offset > 0U) {
		/* Echo backspace */
		shell_puts("\b");
		/* Send a space + backspace sequence to delete character */
		shell_puts(" \b");

		if (p_shell->cursor_offset < p_shell->input_line_len) {
			uint32_t delta = p_shell->input_line_len - p_shell->cursor_offset;

			/* display the left input-chars and remove the former last one */
			shell_puts(p_shell->buffered_line[p_shell->input_line_active] + p_shell->cursor_offset);
			shell_puts(" \b");

			set_cursor_pos(delta);
			memcpy_erms(p_shell->buffered_line[p_shell->input_line_active] + p_shell->cursor_offset - 1,
				p_shell->buffered_line[p_shell->input_line_active] + p_shell->cursor_offset, delta);
		}

		/* Null terminate the last character to erase it */
		p_shell->buffered_line[p_shell->input_line_active][p_shell->input_line_len - 1] = 0;

		/* Reduce the length of the string by one */
		p_shell->input_line_len--;
		p_shell->cursor_offset--;
	}
}

static void handle_input_char(char ch)
{
	uint32_t delta = p_shell->input_line_len - p_shell->cursor_offset;

	/* move the input from cursor offset back first */
	if (delta > 0) {
		memcpy_erms_backwards(p_shell->buffered_line[p_shell->input_line_active] + p_shell->input_line_len,
			p_shell->buffered_line[p_shell->input_line_active] + p_shell->input_line_len - 1, delta);
	}

	p_shell->buffered_line[p_shell->input_line_active][p_shell->cursor_offset] = ch;

	/* Echo back the input */
	shell_puts(p_shell->buffered_line[p_shell->input_line_active] + p_shell->cursor_offset);
	set_cursor_pos(delta);

	/* Move to next character in string */
	p_shell->input_line_len++;
	p_shell->cursor_offset++;
}

static bool shell_input_line(void)
{
	bool done = false;
	char ch;

	ch = shell_getc();

	/* Check character */
	switch (ch) {
	/* Backspace */
	case '\b':
		handle_backspace_key();
		break;

	/* Carriage-return */
	case '\r':
		/* Echo carriage return / line feed */
		shell_puts("\r\n");

		/* Set flag showing line input done */
		done = true;

		/* Reset command length for next command processing */
		p_shell->input_line_len = 0U;
		p_shell->cursor_offset = 0U;
		break;

	/* Line feed */
	case '\n':
		/* Do nothing */
		break;

	/* All other characters */
	default:
		/* Ensure data doesn't exceed full terminal width */
		if (p_shell->input_line_len < SHELL_CMD_MAX_LEN) {
			/* See if a "standard" prINTable ASCII character received */
			if ((ch >= 32) && (ch <= 126)) {
				handle_input_char(ch);
			} else {
				/* call special character handler */
				shell_handle_special_char(ch);
			}
		} else {
			/* Echo carriage return / line feed */
			shell_puts("\r\n");

			/* Set flag showing line input done */
			done = true;

			/* Reset command length for next command processing */
			p_shell->input_line_len = 0U;
			p_shell->cursor_offset = 0U;
		}
		break;
	}


	return done;
}

static int32_t shell_process_cmd(const char *p_input_line)
{
	int32_t status = -EINVAL;
	struct shell_cmd *p_cmd;
	char cmd_argv_str[SHELL_CMD_MAX_LEN + 1U];
	int32_t cmd_argv_mem[sizeof(char *) * ((SHELL_CMD_MAX_LEN + 1U) >> 1U)];
	int32_t cmd_argc;
	char **cmd_argv;

	/* Copy the input line INTo an argument string to become part of the
	 * argument vector.
	 */
	(void)strncpy_s(&cmd_argv_str[0], SHELL_CMD_MAX_LEN + 1U, p_input_line, SHELL_CMD_MAX_LEN);
	cmd_argv_str[SHELL_CMD_MAX_LEN] = 0;

	/* Build the argv vector from the string. The first argument in the
	 * resulting vector will be the command string itself.
	 */

	/* NOTE: This process is destructive to the argument string! */

	(void) string_to_argv(&cmd_argv_str[0],
			(void *) &cmd_argv_mem[0],
			sizeof(cmd_argv_mem), (void *)&cmd_argc, &cmd_argv);

	/* Determine if there is a command to process. */
	if (cmd_argc != 0) {
		/* See if command is in cmds supported */
		p_cmd = shell_find_cmd(cmd_argv[0]);
		if (p_cmd == NULL) {
			shell_puts("\r\nError: Invalid command.\r\n");
			return -EINVAL;
		}

		status = p_cmd->fcn(cmd_argc, &cmd_argv[0]);
		if (status == -EINVAL) {
			snprintf(shell_log_buf, SHELL_LOG_BUF_SIZE, "\r\nError: Invalid parameters. Please refer: %s\r\n", p_cmd->cmd_param);
			shell_puts(shell_log_buf);
		} else if (status == -ENOMEM) {
			shell_puts("\r\nBuffer size is not enough! Please check!\r\n");
		} else if (status != 0) {
			shell_puts("\r\nCommand launch failed.\r\n");
		} else {
			/* No other state currently, do nothing */
		}
	}

	return status;
}

static int32_t shell_process(void)
{
	int32_t status, former_index;
	char *p_input_line;

	/* Process current command (using active input line). */
	p_input_line = p_shell->buffered_line[p_shell->input_line_active];

	former_index = (p_shell->input_line_active + MAX_BUFFERED_CMDS - 1) % MAX_BUFFERED_CMDS;

	/* just buffer current cmd if current is not empty and not same with last buffered one */
	if ((strnlen_s(p_input_line, SHELL_CMD_MAX_LEN) > 0) &&
		(strcmp(p_input_line, p_shell->buffered_line[former_index]) != 0)) {
		p_shell->input_line_active = (p_shell->input_line_active + 1) % MAX_BUFFERED_CMDS;
	}

	p_shell->to_select_index = p_shell->input_line_active;

	/* Process command */
	status = shell_process_cmd(p_input_line);

	/* Now that the command is processed, zero fill the input buffer */
	(void)memset(p_shell->buffered_line[p_shell->input_line_active], 0, SHELL_CMD_MAX_LEN + 1U);

	/* Process command and return result to caller */
	return status;
}


void shell_kick(void)
{
	static bool is_cmd_cmplt = true;

	/* At any given instance, UART may be owned by the HV
	 * OR by the guest that has enabled the vUart.
	 * Show HV shell prompt ONLY when HV owns the
	 * serial port.
	 */
	/* Prompt the user for a selection. */
	if (is_cmd_cmplt) {
		shell_puts(SHELL_PROMPT_STR);
	}

	/* Get user's input */
	is_cmd_cmplt = shell_input_line();

	/* If user has pressed the ENTER then process
	 * the command
	 */
	if (is_cmd_cmplt) {
		/* Process current input line. */
		(void)shell_process();
	}
}


void shell_init(void)
{
	p_shell->cmds = shell_cmds;
	p_shell->cmd_count = ARRAY_SIZE(shell_cmds);

	p_shell->to_select_index = 0;

	/* Zero fill the input buffer */
	(void)memset(p_shell->buffered_line[p_shell->input_line_active], 0U, SHELL_CMD_MAX_LEN + 1U);
}

#define SHELL_ROWS	30
#define MAX_OUTPUT_LEN  80
static int32_t shell_cmd_help(__unused int32_t argc, __unused char **argv)
{
	struct shell_cmd *p_cmd = NULL;

	char str[MAX_STR_SIZE];
	char* help_str;
	/* Print title */
	shell_puts("\r\nRegistered Commands:\r\n\r\n");

	pr_dbg("shell: Number of registered commands = %u in %s\n",
		p_shell->cmd_count, __func__);

	/* Proceed based on the number of registered commands. */
	if (p_shell->cmd_count == 0U) {
		/* No registered commands */
		shell_puts("NONE\r\n");
	} else {
		int32_t i = 0;
		uint32_t j;

		for (j = 0U; j < p_shell->cmd_count; j++) {
			p_cmd = &p_shell->cmds[j];

			/* Check if we've filled the screen with info */
			/* i + 1 used to avoid 0%SHELL_ROWS=0 */
			if (((i + 1) % SHELL_ROWS) == 0) {
				/* Pause before we continue on to the next
				 * page.
				 */

				/* Print message to the user. */
				shell_puts("<*** Hit any key to continue ***>");

				/* Wait for a character from user (NOT USED) */
				(void)shell_getc();

				/* Print a new line after the key is hit. */
				shell_puts("\r\n");
			}

			i++;
			if (p_cmd->cmd_param == NULL)
				p_cmd->cmd_param = " ";
			(void)memset(str, ' ', sizeof(str));
			/* Output the command & parameter string */
			snprintf(str, MAX_OUTPUT_LEN, " %-15s%-64s",
					p_cmd->str, p_cmd->cmd_param);
			shell_puts(str);
			shell_puts("\r\n");

			help_str = p_cmd->help_str;
			while (strnlen_s(help_str, MAX_OUTPUT_LEN > 0)) {
				(void)memset(str, ' ', sizeof(str));
				if (strnlen_s(help_str, MAX_OUTPUT_LEN) > 65) {
					snprintf(str, MAX_OUTPUT_LEN, "               %-s", help_str);
					shell_puts(str);
					shell_puts("\r\n");
					help_str = help_str + 65;
				} else {
					snprintf(str, MAX_OUTPUT_LEN, "               %-s", help_str);
					shell_puts(str);
					shell_puts("\r\n");
					break;
				}
			}
		}
	}

	shell_puts("\r\n");

	return 0;
}

static int32_t shell_version(__unused int32_t argc, __unused char **argv) {
	shell_puts(version_info);
	shell_puts("\r\n");
	return 0;
}

static int32_t shell_list_vm(__unused int32_t argc, __unused char **argv)
{
	char temp_str[MAX_STR_SIZE];
	struct acrn_vm *vm;
	struct acrn_vm_config *vm_config;
	uint16_t vm_id;
	char state[32];

	shell_puts("\r\nVM_ID VM_NAME                          VM_STATE"
		   "\r\n===== ================================ ========\r\n");

	for (vm_id = 0U; vm_id < CONFIG_MAX_VM_NUM; vm_id++) {
		vm = get_vm_from_vmid(vm_id);
		switch (vm->state) {
		case VM_CREATED:
			(void)strncpy_s(state, 32U, "Created", 32U);
			break;
		case VM_RUNNING:
			(void)strncpy_s(state, 32U, "Running", 32U);
			break;
		case VM_PAUSED:
			(void)strncpy_s(state, 32U, "Paused", 32U);
			break;
		case VM_POWERED_OFF:
			(void)strncpy_s(state, 32U, "Off", 32U);
			break;
		default:
			(void)strncpy_s(state, 32U, "Unknown", 32U);
			break;
		}
		vm_config = get_vm_config(vm_id);
		if (!is_poweroff_vm(vm)) {
			snprintf(temp_str, MAX_STR_SIZE, "  %-3d %-32s %-8s\r\n",
				vm_id, vm_config->name, state);

			/* Output information for this task */
			shell_puts(temp_str);
		}
	}

	return 0;
}

static int32_t shell_list_vcpu(__unused int32_t argc, __unused char **argv)
{
	char temp_str[MAX_STR_SIZE];
	struct acrn_vm *vm;
	struct acrn_vcpu *vcpu;
	char vcpu_state_str[32], thread_state_str[32];
	uint16_t i;
	uint16_t idx;

	shell_puts("\r\nVM ID    PCPU ID    VCPU ID    VCPU ROLE    VCPU STATE    THREAD STATE"
		"\r\n=====    =======    =======    =========    ==========    ==========\r\n");

	for (idx = 0U; idx < CONFIG_MAX_VM_NUM; idx++) {
		vm = get_vm_from_vmid(idx);
		if (is_poweroff_vm(vm)) {
			continue;
		}
		foreach_vcpu(i, vm, vcpu) {
			switch (vcpu->state) {
			case VCPU_INIT:
				(void)strncpy_s(vcpu_state_str, 32U, "Init", 32U);
				break;
			case VCPU_RUNNING:
				(void)strncpy_s(vcpu_state_str, 32U, "Running", 32U);
				break;
			case VCPU_ZOMBIE:
				(void)strncpy_s(vcpu_state_str, 32U, "Zombie", 32U);
				break;
			default:
				(void)strncpy_s(vcpu_state_str, 32U, "Unknown", 32U);
				break;
			}

			switch (vcpu->thread_obj.status) {
			case THREAD_STS_RUNNING:
				(void)strncpy_s(thread_state_str, 32U, "RUNNING", 32U);
				break;
			case THREAD_STS_RUNNABLE:
				(void)strncpy_s(thread_state_str, 32U, "RUNNABLE", 32U);
				break;
			case THREAD_STS_BLOCKED:
				(void)strncpy_s(thread_state_str, 32U, "BLOCKED", 32U);
				break;
			default:
				(void)strncpy_s(thread_state_str, 32U, "UNKNOWN", 32U);
				break;
			}
			/* Create output string consisting of VM name
			 * and VM id
			 */
			snprintf(temp_str, MAX_STR_SIZE,
					"  %-9d %-10d %-7hu %-12s %-16s %-16s\r\n",
					vm->vm_id,
					pcpuid_from_vcpu(vcpu),
					vcpu->vcpu_id,
					is_vcpu_bsp(vcpu) ?
					"PRIMARY" : "SECONDARY",
					vcpu_state_str, thread_state_str);
			/* Output information for this task */
			shell_puts(temp_str);
		}
	}

	return 0;
}

#define DUMPREG_SP_SIZE	32
/* the input 'data' must != NULL and indicate a vcpu structure pointer */
static void dump_vcpu_reg(void *data)
{
	int32_t status;
	uint64_t i, fault_addr, tmp[DUMPREG_SP_SIZE];
	uint32_t err_code = 0;
	struct vcpu_dump *dump = data;
	struct acrn_vcpu *vcpu = dump->vcpu;
	char *str = dump->str;
	size_t len, size = dump->str_max;
	uint16_t pcpu_id = get_pcpu_id();
	struct acrn_vcpu *curr = get_running_vcpu(pcpu_id);

	/* switch vmcs */
	load_vmcs(vcpu);

	len = snprintf(str, size,
		"=  VM ID %d ==== CPU ID %hu========================\r\n"
		"=  RIP=0x%016lx  RSP=0x%016lx RFLAGS=0x%016lx\r\n"
		"=  CR0=0x%016lx  CR2=0x%016lx\r\n"
		"=  CR3=0x%016lx  CR4=0x%016lx\r\n"
		"=  RAX=0x%016lx  RBX=0x%016lx RCX=0x%016lx\r\n"
		"=  RDX=0x%016lx  RDI=0x%016lx RSI=0x%016lx\r\n"
		"=  RBP=0x%016lx  R8=0x%016lx R9=0x%016lx\r\n"
		"=  R10=0x%016lx  R11=0x%016lx R12=0x%016lx\r\n"
		"=  R13=0x%016lx  R14=0x%016lx  R15=0x%016lx\r\n",
		vcpu->vm->vm_id, vcpu->vcpu_id,
		vcpu_get_rip(vcpu),
		vcpu_get_rsp(vcpu),
		vcpu_get_rflags(vcpu),
		vcpu_get_cr0(vcpu), vcpu_get_cr2(vcpu),
		exec_vmread(VMX_GUEST_CR3), vcpu_get_cr4(vcpu),
		vcpu_get_gpreg(vcpu, CPU_REG_RAX),
		vcpu_get_gpreg(vcpu, CPU_REG_RBX),
		vcpu_get_gpreg(vcpu, CPU_REG_RCX),
		vcpu_get_gpreg(vcpu, CPU_REG_RDX),
		vcpu_get_gpreg(vcpu, CPU_REG_RDI),
		vcpu_get_gpreg(vcpu, CPU_REG_RSI),
		vcpu_get_gpreg(vcpu, CPU_REG_RBP),
		vcpu_get_gpreg(vcpu, CPU_REG_R8),
		vcpu_get_gpreg(vcpu, CPU_REG_R9),
		vcpu_get_gpreg(vcpu, CPU_REG_R10),
		vcpu_get_gpreg(vcpu, CPU_REG_R11),
		vcpu_get_gpreg(vcpu, CPU_REG_R12),
		vcpu_get_gpreg(vcpu, CPU_REG_R13),
		vcpu_get_gpreg(vcpu, CPU_REG_R14),
		vcpu_get_gpreg(vcpu, CPU_REG_R15));
	if (len >= size) {
		goto overflow;
	}
	size -= len;
	str += len;

	/* dump sp */
	status = copy_from_gva(vcpu, tmp, vcpu_get_gpreg(vcpu, CPU_REG_RSP),
			DUMPREG_SP_SIZE*sizeof(uint64_t), &err_code,
			&fault_addr);
	if (status < 0) {
		/* copy_from_gva fail */
		len = snprintf(str, size, "Cannot handle user gva yet!\r\n");
		if (len >= size) {
			goto overflow;
		}
		size -= len;
		str += len;
	} else {
		len = snprintf(str, size, "\r\nDump RSP for vm %hu, from gva 0x%016lx\r\n",
			vcpu->vm->vm_id, vcpu_get_gpreg(vcpu, CPU_REG_RSP));
		if (len >= size) {
			goto overflow;
		}
		size -= len;
		str += len;

		for (i = 0UL; i < 8UL; i++) {
			len = snprintf(str, size, "=  0x%016lx  0x%016lx 0x%016lx  0x%016lx\r\n",
					tmp[i*4UL], tmp[(i*4UL)+1UL], tmp[(i*4UL)+2UL], tmp[(i*4UL)+3UL]);
			if (len >= size) {
				goto overflow;
			}
			size -= len;
			str += len;
		}
	}
	if (curr != NULL) {
		load_vmcs(curr);
	}
	return;

overflow:
	printf("buffer size could not be enough! please check!\n");
}

static int32_t shell_vcpu_dumpreg(int32_t argc, char **argv)
{
	int32_t status = 0;
	uint16_t vm_id;
	uint16_t vcpu_id, pcpu_id;
	struct acrn_vm *vm;
	struct acrn_vcpu *vcpu;
	uint64_t mask = 0UL;
	struct vcpu_dump dump;

	/* User input invalidation */
	if (argc != 3) {
		shell_puts("Please enter cmd with <vm_id, vcpu_id>\r\n");
		status = -EINVAL;
		goto out;
	}

	status = strtol_deci(argv[1]);
	if (status < 0) {
		goto out;
	}
	vm_id = sanitize_vmid((uint16_t)status);
	vcpu_id = (uint16_t)strtol_deci(argv[2]);

	vm = get_vm_from_vmid(vm_id);
	if (is_poweroff_vm(vm)) {
		shell_puts("No vm found in the input <vm_id, vcpu_id>\r\n");
		status = -EINVAL;
		goto out;
	}

	if (vcpu_id >= vm->hw.created_vcpus) {
		shell_puts("vcpu id is out of range\r\n");
		status = -EINVAL;
		goto out;
	}

	vcpu = vcpu_from_vid(vm, vcpu_id);
	if (vcpu->state == VCPU_OFFLINE) {
		shell_puts("vcpu is offline\r\n");
		status = -EINVAL;
		goto out;
	}

	pcpu_id = pcpuid_from_vcpu(vcpu);
	dump.vcpu = vcpu;
	dump.str = shell_log_buf;
	dump.str_max = SHELL_LOG_BUF_SIZE;
	bitmap_set_nolock(pcpu_id, &mask);
	smp_call_function(mask, dump_vcpu_reg, &dump);
	shell_puts(shell_log_buf);
	status = 0;

out:
	return status;
}

static inline bool is_valid_addr(uint64_t addr, uint32_t length) {
	uint64_t high_min, high_max;
	uint64_t end = addr + (((length & 0x1fU) == 0U) ? ((length >> 5U)) : ((length >> 5U) + 1U)) * 32U - 1U;
	bool ret = false;

	if (addr >= MMIO64_START && end < MMIO64_END) {
		ret = true;
	} else if (end < MEM_4G) {
		ret = true;
	} else {
		get_high64_ram(&high_min, &high_max);
		ret = (high_min == MEM_4G) ? (end < high_max) : (addr >= high_min && end < high_max);
	}

	return ret;
}

static int32_t shell_dump_host_mem(int32_t argc, char **argv)
{
	uint64_t *hva;
	int32_t ret = 0;
	uint32_t i, length, loop_cnt;
	uint64_t high_min, high_max;
	char temp_str[MAX_STR_SIZE];

	/* User input invalidation */
	if (argc == 2) {
		if (strcmp(argv[1], "-h") == 0) {
			get_high64_ram(&high_min, &high_max);
			shell_puts("Available address range:\r\n");
			if (high_min > MEM_4G) {
				snprintf(temp_str, MAX_STR_SIZE, "0x%016lx - 0x%016lx\r\n", 0, MEM_4G);
				shell_puts(temp_str);
				snprintf(temp_str, MAX_STR_SIZE, "0x%016lx - 0x%016lx\r\n", high_min, high_max);
				shell_puts(temp_str);
			} else {
				snprintf(temp_str, MAX_STR_SIZE, "0x%016lx - 0x%016lx\r\n", 0, high_max);
				shell_puts(temp_str);
			}
			snprintf(temp_str, MAX_STR_SIZE, "0x%016lx - 0x%016lx\r\n", MMIO64_START, MMIO64_END);
			shell_puts(temp_str);
		} else {
			ret = -EINVAL;
		}
	} else if (argc == 3) {
		hva = (uint64_t *)strtoul_hex(argv[1]);
		length = (uint32_t)strtol_deci(argv[2]);

		snprintf(temp_str, MAX_STR_SIZE, "Dump physical memory addr: 0x%016lx, length %d:\r\n", hva, length);
		shell_puts(temp_str);
	
		if (is_valid_addr((uint64_t)hva, length) == false) {
			shell_puts("Invalid address region\r\n");
			ret = -EINVAL;
			goto out;
		}

		if ((uint64_t)hva % 4 != 0) {
			shell_puts("The address needs 4-byte alignment\r\n");
			ret = -EINVAL;
			goto out;
		}		
		/* Change the length to a multiple of 32 if the length is not */
		loop_cnt = ((length & 0x1fU) == 0U) ? ((length >> 5U)) : ((length >> 5U) + 1U);
		stac();
		for (i = 0U; i < loop_cnt; i++) {
			snprintf(temp_str, MAX_STR_SIZE, "HVA(0x%llx): 0x%016lx  0x%016lx  0x%016lx  0x%016lx\r\n",
				hva, *hva, *(hva + 1UL), *(hva + 2UL), *(hva + 3UL));
			hva += 4UL;
			shell_puts(temp_str);
		}
		clac();
		ret = 0;
	} else {
		ret = -EINVAL;
	}
out:
	return ret;
}

static void dump_guest_mem(void *data)
{
	uint64_t i, fault_addr;
	uint32_t err_code = 0U;
	uint64_t loop_cnt;
	uint64_t buf[4U];
	char temp_str[MAX_STR_SIZE];
	struct guest_mem_dump *dump = (struct guest_mem_dump *)data;
	uint64_t length = dump->len;
	uint64_t ga = dump->ga;
	bool is_gva = dump->is_gva;
	struct acrn_vcpu *vcpu = dump->vcpu;
	uint16_t pcpu_id = get_pcpu_id();
	struct acrn_vcpu *curr = get_running_vcpu(pcpu_id);

	load_vmcs(vcpu);

	/* Change the length to a multiple of 32 if the length is not */
	loop_cnt = ((length & 0x1fUL) == 0UL) ? ((length >> 5UL)) : ((length >> 5UL) + 1UL);

	for (i = 0UL; i < loop_cnt; i++) {
		if (is_gva) {
			if (copy_from_gva(vcpu, buf, ga, 32U, &err_code, &fault_addr) == 0U) {
				snprintf(temp_str, MAX_STR_SIZE, "GVA(0x%llx):  0x%016lx  0x%016lx  0x%016lx  0x%016lx\r\n",
					ga, buf[0U], buf[1U], buf[2U], buf[3U]);
			} else {
				printf("copy_from_gva error! err_code=0x%x fault_addr=0x%llx\r\n", err_code, fault_addr);
				break;
			}
		} else {
			if (copy_from_gpa(vcpu->vm, buf, ga, 32U) == 0U) {
				snprintf(temp_str, MAX_STR_SIZE, "GPA(0x%llx):  0x%016lx  0x%016lx  0x%016lx  0x%016lx\r\n",
					ga, buf[0U], buf[1], buf[2U], buf[3U]);
			} else {
				printf("copy_from_gpa error!\r\n");
				break;
			}
		}
		shell_puts(temp_str);
		ga += 32UL;
	}
	if (curr != NULL) {
		load_vmcs(curr);
	}
}

static int32_t shell_dump_guest_mem(int32_t argc, char **argv)
{
	uint16_t vm_id, pcpu_id, addr_pos;
	int32_t ret = 0U;
	bool is_gva;
	uint64_t ga;
	uint64_t length;
	uint64_t mask = 0UL;
	struct acrn_vm *vm;
	struct acrn_vcpu *vcpu = NULL;
	struct guest_mem_dump dump;

	/* User input invalidation */
	if (argc == 4U) {
		is_gva = true;
		addr_pos = 2U;
	} else if ((argc == 5U) && (strcmp(argv[2], "-p") == 0U)) {
		is_gva = false;
		addr_pos = 3U;
	} else {
		ret = -EINVAL;
	}

	if (ret == 0U) {
		vm_id = sanitize_vmid((uint16_t)strtol_deci(argv[1U]));
		ga = strtoul_hex(argv[addr_pos]);
		length = (uint64_t)strtol_deci(argv[addr_pos + 1U]);

		vm = get_vm_from_vmid(vm_id);
		vcpu = vcpu_from_vid(vm, BSP_CPU_ID);

		dump.vcpu = vcpu;
		dump.is_gva = is_gva;
		dump.ga = ga;
		dump.len = length;

		pcpu_id = pcpuid_from_vcpu(vcpu);
		bitmap_set_nolock(pcpu_id, &mask);
		smp_call_function(mask, dump_guest_mem, &dump);
		ret = 0U;
	}

	return ret;
}

static int32_t shell_to_vm_console(int32_t argc, char **argv)
{
	char temp_str[TEMP_STR_SIZE];
	uint16_t vm_id = 0U;

	struct acrn_vm *vm;
	struct acrn_vuart *vu;

	if (argc == 2) {
		vm_id = sanitize_vmid((uint16_t)strtol_deci(argv[1]));
	}

	/* Get the virtual device node */
	vm = get_vm_from_vmid(vm_id);
	if (is_poweroff_vm(vm)) {
		shell_puts("VM is not valid \n");
		return -EINVAL;
	}
	vu = vm_console_vuart(vm);
	if (!vu->active) {
		shell_puts("vuart console is not active \n");
		return 0;
	}
	console_vmid = vm_id;
	/* Output that switching to Service VM shell */
	snprintf(temp_str, TEMP_STR_SIZE, "\r\n----- Entering VM %d Shell -----\r\n", vm_id);

	shell_puts(temp_str);

	return 0;
}

/**
 * @brief Get the interrupt statistics
 *
 * It's for debug only.
 *
 * @param[in]	str_max	The max size of the string containing interrupt info
 * @param[inout]	str_arg	Pointer to the output interrupt info
 */
static int get_cpu_interrupt_info(char *str_arg, size_t str_max)
{
	char *str = str_arg;
	uint16_t pcpu_id;
	uint32_t irq, vector;
	size_t len, size = str_max;
	uint16_t pcpu_nums = get_pcpu_nums();
	int ret = -ENOMEM;

	len = snprintf(str, size, "\r\nIRQ\tVECTOR");
	if (len >= size) {
		goto overflow;
	}
	size -= len;
	str += len;

	for (pcpu_id = 0U; pcpu_id < pcpu_nums; pcpu_id++) {
		len = snprintf(str, size, "\tCPU%d", pcpu_id);
		if (len >= size) {
			goto overflow;
		}
		size -= len;
		str += len;
	}

	for (irq = 0U; irq < NR_IRQS; irq++) {
		vector = irq_to_vector(irq);
		if (bitmap_test((uint16_t)(irq & 0x3FU),
			irq_alloc_bitmap + (irq >> 6U))
			&& (vector != VECTOR_INVALID)) {
			len = snprintf(str, size, "\r\n%d\t0x%X", irq, vector);
			if (len >= size) {
				goto overflow;
			}
			size -= len;
			str += len;

			for (pcpu_id = 0U; pcpu_id < pcpu_nums; pcpu_id++) {
				len = snprintf(str, size, "\t%d", per_cpu(irq_count, pcpu_id)[irq]);
				if (len >= size) {
					goto overflow;
				}
				size -= len;
				str += len;
			}
		}
	}
	snprintf(str, size, "\r\n");

	ret = 0;

overflow:
	return ret;
}

static int32_t shell_show_cpu_int(__unused int32_t argc, __unused char **argv)
{
	int ret = 0;

	ret = get_cpu_interrupt_info(shell_log_buf, SHELL_LOG_BUF_SIZE);
	if(!ret) {
		shell_puts(shell_log_buf);
	}

	return ret;
}

static void get_entry_info(const struct ptirq_remapping_info *entry, char *type,
		uint32_t *irq, uint32_t *vector, uint64_t *dest, bool *lvl_tm,
		uint32_t *pgsi, uint32_t *vgsi, union pci_bdf *bdf, union pci_bdf *vbdf)
{
	if (is_entry_active(entry)) {
		if (entry->intr_type == PTDEV_INTR_MSI) {
			(void)strncpy_s(type, 16U, "MSI", 16U);
			*dest = entry->pmsi.addr.bits.dest_field;
			if (entry->pmsi.data.bits.trigger_mode == MSI_DATA_TRGRMODE_LEVEL) {
				*lvl_tm = true;
			} else {
				*lvl_tm = false;
			}
			*pgsi = INVALID_INTERRUPT_PIN;
			*vgsi = INVALID_INTERRUPT_PIN;
			bdf->value = entry->phys_sid.msi_id.bdf;
			vbdf->value = entry->virt_sid.msi_id.bdf;
		} else {
			uint32_t phys_irq = entry->allocated_pirq;
			union ioapic_rte rte;

			(void)strncpy_s(type, 16U, "IOAPIC", 16U);
			ioapic_get_rte(phys_irq, &rte);
			*dest = rte.bits.dest_field;
			if (rte.bits.trigger_mode == IOAPIC_RTE_TRGRMODE_LEVEL) {
				*lvl_tm = true;
			} else {
				*lvl_tm = false;
			}
			*pgsi = entry->phys_sid.intx_id.gsi;
			*vgsi = entry->virt_sid.intx_id.gsi;
			bdf->value = 0U;
			vbdf->value = 0U;
		}
		*irq = entry->allocated_pirq;
		*vector = irq_to_vector(entry->allocated_pirq);
	} else {
		(void)strncpy_s(type, 16U, "NONE", 16U);
		*irq = IRQ_INVALID;
		*vector = 0U;
		*dest = 0UL;
		*lvl_tm = 0;
		*pgsi = ~0U;
		*vgsi = ~0U;
		bdf->value = 0U;
		vbdf->value = 0U;
	}
}

static int get_ptdev_info(char *str_arg, size_t str_max)
{
	char *str = str_arg;
	struct ptirq_remapping_info *entry;
	uint16_t idx;
	size_t len, size = str_max;
	uint32_t irq, vector;
	char type[16];
	uint64_t dest;
	bool lvl_tm;
	uint32_t pgsi, vgsi;
	union pci_bdf bdf, vbdf;
	int ret = -ENOMEM;

	len = snprintf(str, size, "\r\nVM\tTYPE\tIRQ\tVEC\tDEST\tTM\tGSI\tVGSI\tBDF\tVBDF");
	if (len >= size) {
		goto overflow;
	}
	size -= len;
	str += len;

	for (idx = 0U; idx < CONFIG_MAX_PT_IRQ_ENTRIES; idx++) {
		entry = &ptirq_entries[idx];
		if (is_entry_active(entry)) {
			get_entry_info(entry, type, &irq, &vector, &dest, &lvl_tm, &pgsi, &vgsi,
					&bdf, &vbdf);
			len = snprintf(str, size, "\r\n%d\t%s\t%d\t0x%X\t0x%X",
					entry->vm->vm_id, type, irq, vector, dest);
			if (len >= size) {
				goto overflow;
			}
			size -= len;
			str += len;

			len = snprintf(str, size, "\t%s\t%hhu\t%hhu\t%x:%x.%x\t%x:%x.%x",
					is_entry_active(entry) ? (lvl_tm ? "level" : "edge") : "none",
					pgsi, vgsi, bdf.bits.b, bdf.bits.d, bdf.bits.f,
					vbdf.bits.b, vbdf.bits.d, vbdf.bits.f);
			if (len >= size) {
				goto overflow;
			}
			size -= len;
			str += len;
		}
	}

	snprintf(str, size, "\r\n");

	ret = 0;

overflow:
	return ret;
}

static int32_t shell_show_ptdev_info(__unused int32_t argc, __unused char **argv)
{
	int ret = 0;

	ret = get_ptdev_info(shell_log_buf, SHELL_LOG_BUF_SIZE);
	if (!ret) {
		shell_puts(shell_log_buf);
	}

	return ret;
}

static int get_vioapic_info(char *str_arg, size_t str_max, uint16_t vmid)
{
	char *str = str_arg;
	size_t len, size = str_max;
	union ioapic_rte rte;
	uint32_t delmode, vector, dest;
	bool level, phys, remote_irr, mask;
	struct acrn_vm *vm = get_vm_from_vmid(vmid);
	uint32_t gsi, gsi_count;
	int ret = -ENOMEM;

	if (is_poweroff_vm(vm)) {
		len = snprintf(str, size, "\r\nvm is not exist for vmid %hu", vmid);
		if (len >= size) {
			goto overflow;
		}
		size -= len;
		str += len;
		goto END;
	}

	len = snprintf(str, size, "\r\nPIN\tVEC\tDM\tDEST\tTM\tDELM\tIRR\tMASK");
	if (len >= size) {
		goto overflow;
	}
	size -= len;
	str += len;

	gsi_count = get_vm_gsicount(vm);
	rte.full = 0UL;
	for (gsi = 0U; gsi < gsi_count; gsi++) {
		if (is_service_vm(vm) && (!is_gsi_valid(gsi))) {
			continue;
		}
		vioapic_get_rte(vm, gsi, &rte);
		mask = (rte.bits.intr_mask == IOAPIC_RTE_MASK_SET);
		remote_irr = (rte.bits.remote_irr == IOAPIC_RTE_REM_IRR);
		phys = (rte.bits.dest_mode == IOAPIC_RTE_DESTMODE_PHY);
		delmode = rte.bits.delivery_mode;
		level = (rte.bits.trigger_mode == IOAPIC_RTE_TRGRMODE_LEVEL);
		vector = rte.bits.vector;
		dest = rte.bits.dest_field;

		len = snprintf(str, size, "\r\n%hhu\t0x%X\t%s\t0x%X\t%s\t%u\t%d\t%d",
				gsi, vector, phys ? "phys" : "logic", dest, level ? "level" : "edge",
				delmode >> 8U, remote_irr, mask);
		if (len >= size) {
			goto overflow;
		}
		size -= len;
		str += len;
	}
END:
	snprintf(str, size, "\r\n");

	ret = 0;

overflow:
	return ret;
}

static int32_t shell_show_vioapic_info(int32_t argc, char **argv)
{
	uint16_t vmid;
	int32_t ret;

	/* User input invalidation */
	if (argc != 2) {
		return -EINVAL;
	}
	ret = strtol_deci(argv[1]);
	if (ret >= 0) {
		vmid = sanitize_vmid((uint16_t) ret);
		ret = get_vioapic_info(shell_log_buf, SHELL_LOG_BUF_SIZE, vmid);
		if (!ret) {
			shell_puts(shell_log_buf);
		}
		return ret;
	}

	return -EINVAL;
}

/**
 * @brief Get information of ioapic
 *
 * It's for debug only.
 *
 * @param[in]	str_max_len	The max size of the string containing
 *				interrupt info
 * @param[inout]	str_arg	Pointer to the output information
 */
static int32_t get_ioapic_info(char *str_arg, size_t str_max_len)
{
	char *str = str_arg;
	uint32_t gsi;
	size_t len, size = str_max_len;
	uint32_t ioapic_nr_gsi = 0U;
	int ret = -ENOMEM;

	len = snprintf(str, size, "\r\nIRQ\tPIN\tRTE.HI32\tRTE.LO32\tVEC\tDST\tDM\tTM\tDELM\tIRR\tMASK");
	if (len >= size) {
		goto overflow;
	}
	size -= len;
	str += len;

	ioapic_nr_gsi = get_max_nr_gsi ();
	for (gsi = 0U; gsi < ioapic_nr_gsi; gsi++) {
		void *addr;
		uint32_t pin;
		union ioapic_rte rte;

		if (!is_gsi_valid(gsi)) {
			continue;
		}
		addr = gsi_to_ioapic_base(gsi);
		pin = gsi_to_ioapic_pin(gsi);

		ioapic_get_rte_entry(addr, pin, &rte);

		len = snprintf(str, size, "\r\n%03d\t%03hhu\t0x%08X\t0x%08X\t", gsi, pin, rte.u.hi_32, rte.u.lo_32);
		if (len >= size) {
			goto overflow;
		}
		size -= len;
		str += len;

		len = snprintf(str, size, "0x%02X\t0x%02X\t%s\t%s\t%u\t%d\t%d",
			rte.bits.vector, rte.bits.dest_field,
			(rte.bits.dest_mode == IOAPIC_RTE_DESTMODE_LOGICAL)? "logic" : "phys",
			(rte.bits.trigger_mode == IOAPIC_RTE_TRGRMODE_LEVEL)? "level" : "edge",
			rte.bits.delivery_mode, rte.bits.remote_irr,
			rte.bits.intr_mask);
		if (len >= size) {
			goto overflow;
		}
		size -= len;
		str += len;
	}

	snprintf(str, size, "\r\n");

	ret = 0;

overflow:
	return ret;
}

static int32_t shell_show_ioapic_info(__unused int32_t argc, __unused char **argv)
{
	int32_t err = 0;

	err = get_ioapic_info(shell_log_buf, SHELL_LOG_BUF_SIZE);
	if (!err) {
		shell_puts(shell_log_buf);
	}

	return err;
}

static int32_t shell_loglevel(int32_t argc, char **argv)
{
	char str[MAX_STR_SIZE] = {0};

	switch (argc) {
	case 4:
		npk_loglevel = (uint16_t)strtol_deci(argv[3]);
		/* falls through */
	case 3:
		mem_loglevel = (uint16_t)strtol_deci(argv[2]);
		/* falls through */
	case 2:
		console_loglevel = (uint16_t)strtol_deci(argv[1]);
		break;
	case 1:
		snprintf(str, MAX_STR_SIZE, "console_loglevel: %u, "
			"mem_loglevel: %u, npk_loglevel: %u\r\n",
			console_loglevel, mem_loglevel, npk_loglevel);
		shell_puts(str);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int32_t shell_cpuid(int32_t argc, char **argv)
{
	char str[MAX_STR_SIZE] = {0};
	uint32_t leaf, subleaf = 0;
	uint32_t eax, ebx, ecx, edx;

	if (argc == 2) {
		leaf = (uint32_t)strtoul_hex(argv[1]);
	} else if (argc == 3) {
		leaf = (uint32_t)strtoul_hex(argv[1]);
		subleaf = (uint32_t)strtoul_hex(argv[2]);
	} else {
		shell_puts("Please enter correct cmd with "
			"cpuid <leaf> [subleaf]\r\n");
		return -EINVAL;
	}

	cpuid_subleaf(leaf, subleaf, &eax, &ebx, &ecx, &edx);
	snprintf(str, MAX_STR_SIZE,
		"cpuid leaf: 0x%x, subleaf: 0x%x, 0x%x:0x%x:0x%x:0x%x\r\n",
		leaf, subleaf, eax, ebx, ecx, edx);

	shell_puts(str);

	return 0;
}

static int32_t shell_reboot(__unused int32_t argc, __unused char **argv)
{
	reset_host(false);
	return 0;
}

#ifdef CONFIG_HV_COREDUMP_ENABLED
static int32_t shell_coredump(__unused int32_t argc, __unused char **argv)
{
	trigger_coredump(true);
	return 0;
}
#endif

static int32_t shell_rdmsr(int32_t argc, char **argv)
{
	uint16_t pcpu_id = 0;
	int32_t ret = 0;
	uint32_t msr_index = 0;
	uint64_t val = 0;
	char str[MAX_STR_SIZE] = {0};

	pcpu_id = get_pcpu_id();

	switch (argc) {
	case 3:
		/* rdrmsr -p<PCPU_ID> <MSR_INDEX>*/
		 if ((argv[1][0] == '-') && (argv[1][1] == 'p')) {
			pcpu_id = (uint16_t)strtol_deci(&(argv[1][2]));
			msr_index = (uint32_t)strtoul_hex(argv[2]);
		} else {
			ret = -EINVAL;
		}
		break;
	case 2:
		/* rdmsr <MSR_INDEX> */
		msr_index = (uint32_t)strtoul_hex(argv[1]);
		break;
	default:
		ret = -EINVAL;
	}

	if (ret == 0) {
		if (pcpu_id < get_pcpu_nums()) {
			val = msr_read_pcpu(msr_index, pcpu_id);
			snprintf(str, MAX_STR_SIZE, "rdmsr(0x%x):0x%lx\n", msr_index, val);
			shell_puts(str);
		} else {
			shell_puts("pcpu id is out of range!\n");
		}
	}

	return ret;
}

static int32_t shell_wrmsr(int32_t argc, char **argv)
{
	uint16_t pcpu_id = 0;
	int32_t ret = 0;
	uint32_t msr_index = 0;
	uint64_t val = 0;

	pcpu_id = get_pcpu_id();

	switch (argc) {
	case 4:
		/* wrmsr -p<PCPU_ID> <MSR_INDEX> <VALUE>*/
		 if ((argv[1][0] == '-') && (argv[1][1] == 'p')) {
			pcpu_id = (uint16_t)strtol_deci(&(argv[1][2]));
			msr_index = (uint32_t)strtoul_hex(argv[2]);
			val = strtoul_hex(argv[3]);
		} else {
			ret = -EINVAL;
		}
		break;
	case 3:
		/* wrmsr <MSR_INDEX> <VALUE>*/
		msr_index = (uint32_t)strtoul_hex(argv[1]);
		val = strtoul_hex(argv[2]);
		break;
	default:
		ret = -EINVAL;
	}

	if (ret == 0) {
		if (pcpu_id < get_pcpu_nums()) {
			msr_write_pcpu(msr_index, val, pcpu_id);
		} else {
			shell_puts("pcpu id is out of range!\n");
		}
	}

	return ret;
}
static const char *level_info[MAX_VMEXIT_LEVEL] = {
	"   0us -   2us",
	"   2us -   4us",
	"   4us -   8us",
	"   8us -  16us",
	"  16us -  32us",
	"  32us -  64us",
	"  64us - 128us",
	" 128us - 256us",
	" 256us - 512us",
	" 512us -1024us",
	"1024us -2048us",
	"2048us -4096us",
	"4096us -8192us",
	"8192us -  more",
};

/* if target_vm_id valid, just check this vm or check all VMs; if count is 0, just ignore it */
static bool check_vmexit_count_per_reason(uint16_t target_vm_id, uint16_t reason, uint16_t level)
{
	bool has_vmexit = false;
	uint16_t vm_id, vcpu_id;
	struct acrn_vm *vm;
	struct acrn_vcpu *vcpu;

	if (target_vm_id != CONFIG_MAX_VM_NUM) {
		vm = get_vm_from_vmid(target_vm_id);
		foreach_vcpu(vcpu_id, vm, vcpu) {
			if (vcpu->vmexit_cnt[reason][level] != 0) {
				has_vmexit = true;
				break;
			}
		}
	} else {
		for (vm_id = 0U; vm_id < CONFIG_MAX_VM_NUM; vm_id++) {
			vm = get_vm_from_vmid(vm_id);
			if (is_poweroff_vm(vm))
				continue;

			foreach_vcpu(vcpu_id, vm, vcpu) {
				if (vcpu->vmexit_cnt[reason][level] != 0) {
					has_vmexit = true;
					break;
				}
			}

			if (has_vmexit)
				break;
		}
	}

	return has_vmexit;
}

/* if target_vm_id valid, just output this vm or output all VMs */
static bool output_id_per_vm_vcpu(char **str_arg, size_t *size_max, uint16_t target_vm_id)
{
	char *str = *str_arg;
	size_t len, size = *size_max;
	uint16_t vm_id, vcpu_id;
	struct acrn_vm *vm;
	struct acrn_vcpu *vcpu;

	if (target_vm_id != CONFIG_MAX_VM_NUM) {
		vm = get_vm_from_vmid(target_vm_id);
		foreach_vcpu(vcpu_id, vm, vcpu) {
			len = snprintf(str, size, "   VM%u/vCPU%u", target_vm_id, vcpu_id);
			if (len >= size) {
				return false;
			}
			size -= len;
			str += len;
		}
	} else {
		for (vm_id = 0U; vm_id < CONFIG_MAX_VM_NUM; vm_id++) {
			vm = get_vm_from_vmid(vm_id);
			if (is_poweroff_vm(vm))
				continue;

			foreach_vcpu(vcpu_id, vm, vcpu) {
				len = snprintf(str, size, "   VM%u/vCPU%u", vm_id, vcpu_id);
				if (len >= size) {
					return false;
				}
				size -= len;
				str += len;
			}
		}
	}

	*str_arg = str;
	*size_max = size;
	return true;
}

static bool output_vmexit_info_per_vcpu(char **str_arg, size_t *size_max, uint16_t target_vm_id,
	uint16_t reason, uint16_t level)
{
	char *str = *str_arg;
	size_t len, size = *size_max;
	uint16_t vm_id, vcpu_id;
	struct acrn_vm *vm;
	struct acrn_vcpu *vcpu;

	if (target_vm_id != CONFIG_MAX_VM_NUM) {
		vm = get_vm_from_vmid(target_vm_id);

		foreach_vcpu(vcpu_id, vm, vcpu) {
			 /* if level is MAX, output the max time */
			if (level == MAX_VMEXIT_LEVEL) {
				len = snprintf(str, size, "%12lld", vcpu->vmexit_time[reason]);
			} else {
				len = snprintf(str, size, "%12lld", vcpu->vmexit_cnt[reason][level]);
			}

			if (len >= size) {
				return false;
			}

			size -= len;
			str += len;
		}
	} else {
		for (vm_id = 0U; vm_id < CONFIG_MAX_VM_NUM; vm_id++) {
			vm = get_vm_from_vmid(vm_id);
			if (is_poweroff_vm(vm))
				continue;

			foreach_vcpu(vcpu_id, vm, vcpu) {
				/* if level is MAX, output the max time */
				if (level == MAX_VMEXIT_LEVEL) {
					len = snprintf(str, size, "%12lld", vcpu->vmexit_time[reason]);
				} else {
					len = snprintf(str, size, "%12lld", vcpu->vmexit_cnt[reason][level]);
				}

				if (len >= size) {
					return false;
				}

				size -= len;
				str += len;
			}
		}
	}

	*str_arg = str;
	*size_max = size;
	return true;
}

static int get_vmexit_details_per_vcpu(char *str_arg, size_t str_max, uint16_t target_vm_id)
{
	char *str = str_arg;
	uint16_t reason, level;
	size_t len, size = str_max;
	int ret = -ENOMEM;

	len = snprintf(str, size, "\r\nNow=%lldus, for detailed latency of each vmexit on %s VM/vCPU:",
		ticks_to_us(rdtsc()), (target_vm_id == CONFIG_MAX_VM_NUM) ? "each" : "one");
	if (len >= size) {
		goto overflow;
	}
	size -= len;
	str += len;

	for (reason = 0U; reason < NR_VMX_EXIT_REASONS; reason++) {

		if (!check_vmexit_count_per_reason(target_vm_id, reason, TOTAL_ARRAY_LEVEL - 1))
			continue;

		len = snprintf(str, size, "\r\n\r\n   VMEXIT/0x%02x", reason);
		if (len >= size) {
			goto overflow;
		}
		size -= len;
		str += len;

		if (!output_id_per_vm_vcpu(&str, &size, target_vm_id)) {
			goto overflow;
		}

		for (level = 0; level < MAX_VMEXIT_LEVEL; level++) {

			if (!check_vmexit_count_per_reason(target_vm_id, reason, level)) {
				continue;
			}

			len = snprintf(str, size, "\r\n%s", level_info[level]);
			if (len >= size) {
				goto overflow;
			}
			size -= len;
			str += len;

			if (!output_vmexit_info_per_vcpu(&str, &size, target_vm_id, reason, level)) {
				goto overflow;
			}
		}

		len = snprintf(str, size, "\r\n  Max Lat(us):");
		if (len >= size) {
			goto overflow;
		}
		size -= len;
		str += len;

		if (!output_vmexit_info_per_vcpu(&str, &size, target_vm_id, reason, MAX_VMEXIT_LEVEL)) {
			goto overflow;
		}
	}

	snprintf(str, size, "\r\n");

	ret = 0;

overflow:
	return ret;
}

static void clear_vmexit_info_buffer(void)
{
	uint16_t vm_id, vcpu_id;
	struct acrn_vm *vm;
	struct acrn_vcpu *vcpu;

	for (vm_id = 0U; vm_id < CONFIG_MAX_VM_NUM; vm_id++) {
		vm = get_vm_from_vmid(vm_id);
		if (!is_poweroff_vm(vm)) {
			foreach_vcpu(vcpu_id, vm, vcpu) {
				memset(vcpu->vmexit_cnt, 0, sizeof(vcpu->vmexit_cnt));
				memset(vcpu->vmexit_time, 0, sizeof(vcpu->vmexit_time));
			}
		}
	}
}

/* used to control vmexit sample function */
static bool sample_vmexit_enabled = false;

static int shell_show_vmexit_profile(__unused int argc, __unused char **argv)
{
	uint16_t vm_id = CONFIG_MAX_VM_NUM;
	bool to_continue = true;
	int ret = 0;

	if (argc == 2) {
		if (strcmp(argv[1], "clear") == 0) {
			clear_vmexit_info_buffer();
		} else if (strcmp(argv[1], "enable") == 0) {
			shell_puts("\tenable vmexit sample!\n");
			sample_vmexit_enabled = true;
			to_continue = false;
		} else if (strcmp(argv[1], "disable") == 0) {
			shell_puts("\tdisable vmexit sample!\n");
			sample_vmexit_enabled = false;
			to_continue = false;
		} else {
			vm_id = (uint16_t)strtol_deci(argv[1]);
			if ((vm_id >= CONFIG_MAX_VM_NUM) || is_poweroff_vm(get_vm_from_vmid(vm_id))) {
				shell_puts("\tThis vm_id not existed or active! please check!\n");
				to_continue = false;
			}
		}
	}

	if (to_continue && !sample_vmexit_enabled) {
		shell_puts("\tvmexit sample is disabled!\n");
		to_continue = false;
	}

	if (to_continue) {
		ret = get_vmexit_details_per_vcpu(shell_log_buf, SHELL_LOG_BUF_SIZE, vm_id);
		if (!ret) {
			shell_puts(shell_log_buf);
		}
	}

	return ret;
}


static int32_t shell_dump_vm_console_out(int32_t argc, char **argv)
{
	char temp_str[TEMP_STR_SIZE];
	uint16_t vm_id = 0U;
	uint32_t len = 0U;
	struct acrn_vm *vm;
	struct acrn_vuart *vu;

	if (argc == 2) {
		vm_id = sanitize_vmid((uint16_t)strtol_deci(argv[1]));
	}

	/* Get the virtual device node */
	vm = get_vm_from_vmid(vm_id);
	if (is_poweroff_vm(vm)) {
		shell_puts("VM is not valid \n");
		return -EINVAL;
	}
	vu = vm_console_vuart(vm);
	if (!vu->active) {
		shell_puts("vuart console is not active \n");
		return 0;
	}

	snprintf(temp_str, TEMP_STR_SIZE, "\r\n----- Dump VM %d Console Tx Buffer -----\r\n", vm_id);
	shell_puts(temp_str);

	memset(shell_log_buf, 0, SHELL_LOG_BUF_SIZE);
	if (vu->txfifo.buf[vu->txfifo.windex] != '\0') {
		len = vu->txfifo.size - vu->txfifo.windex;
		memcpy_s(shell_log_buf, SHELL_LOG_BUF_SIZE, vu->txfifo.buf + vu->txfifo.windex, len);
	}
	ASSERT(len < SHELL_LOG_BUF_SIZE, "The buffer size of shell log is too small!");
	if (vu->txfifo.windex > 0) {
		memcpy_s(shell_log_buf + len, SHELL_LOG_BUF_SIZE - len, vu->txfifo.buf, vu->txfifo.windex);
	}
	shell_puts(shell_log_buf);
	return 0;
}

/* to sample vmexit tsc data */
void sample_vmexit_end(struct acrn_vcpu *vcpu)
{
	uint32_t basic_exit_reason = vcpu->arch.exit_reason & 0xFFFFUL;

	if (!sample_vmexit_enabled)
		return;

	if (vcpu->vmexit_begin != 0UL) {
		uint64_t delta = rdtsc() - vcpu->vmexit_begin;
		uint32_t us = (uint32_t)ticks_to_us(delta);
		uint16_t fls = (uint16_t)(fls32(us) + 1); /* to avoid us = 0 case, then fls=0xFFFF */
		uint16_t index = 0;

		if (fls >= MAX_VMEXIT_LEVEL) {
			index = MAX_VMEXIT_LEVEL - 1;
		} else if (fls > 0) { /* if fls == 0, it means the us = 0 */
			index = fls - 1;
		}

		vcpu->vmexit_cnt[basic_exit_reason][index]++;

		if (us > vcpu->vmexit_time[basic_exit_reason]) {
			vcpu->vmexit_time[basic_exit_reason] = us;
		}
	}
}

void sample_vmexit_begin(struct acrn_vcpu *vcpu)
{
	uint32_t basic_exit_reason = vcpu->arch.exit_reason & 0xFFFFUL;

	if (!sample_vmexit_enabled)
		return;

	vcpu->vmexit_begin = rdtsc();
	vcpu->vmexit_cnt[basic_exit_reason][TOTAL_ARRAY_LEVEL - 1]++;
}

static int32_t shell_vm_resume(int32_t argc, char **argv)
{
	uint16_t vm_id = 0U;
	struct acrn_vm *vm;
	int ret = 0;

	if (argc != 2) {
		shell_puts("usage: " SHELL_CMD_VM_RESUME " " SHELL_CMD_VM_RESUME_PARAM "\n");
		return -EINVAL;
	}

	vm_id = sanitize_vmid((uint16_t)strtol_deci(argv[1]));
	vm = get_vm_from_vmid(vm_id);

	ret = s3_resume_vm(vm);

	return ret;
}

static int32_t pci_get_dev_cfg(char *str_arg, size_t str_max, union pci_bdf bdf, uint32_t offset, uint32_t cfg_len)
{
	uint32_t i;
	char *str = str_arg;
	size_t len, size = str_max;
	uint16_t vendor, device;
	char temp_str[TEMP_STR_SIZE];
	int ret = -ENOMEM;

	if (!pci_is_valid_access(offset, 4)) {
		snprintf(temp_str, TEMP_STR_SIZE, "Offset 0x%x does not align with 4 bytes, error\n", offset);
		shell_puts(temp_str);
		ret = -EINVAL;
		goto end;
	}

	if (cfg_len % 4) {
		snprintf(temp_str, TEMP_STR_SIZE, "Length %d is not multiple of 4 bytes, error\n", cfg_len);
		shell_puts(temp_str);
		ret = -EINVAL;
		goto end;
	}

	vendor = pci_pdev_read_cfg(bdf, PCIR_VENDOR, 2U);
	if (!is_pci_vendor_valid(vendor)) {
		snprintf(temp_str, TEMP_STR_SIZE, "Wrong BDF(%02x:%02x.%x) which VENDOR ID is 0x%x!\n", bdf.bits.b, bdf.bits.d, bdf.bits.f, vendor);
		shell_puts(temp_str);
		ret = -ENODEV;
		goto end;
	}

	device = pci_pdev_read_cfg(bdf, PCIR_DEVICE, 2U);

	len = snprintf(str, size, "%02x:%02x.%x \t0x%x:0x%x\n",
			bdf.bits.b, bdf.bits.d, bdf.bits.f, vendor, device);
	if (len >= size) {
		goto end;
	}
	size -= len;
	str += len;

	for (i = 0; i < cfg_len; i += 4) {
		len = snprintf(str, size, "%08x ", pci_pdev_read_cfg(bdf, offset + i, 4));
		if (len >= size) {
			goto end;
		}
		size -= len;
		str += len;

		if (((i + 4) % 16) == 0) {
			len = snprintf(str, size, "\n");
			if (len >= size) {
				goto end;
			}
			size -= len;
			str += len;
		}
	}

	len = snprintf(str, size, "\r\n");
	if (len >= size) {
		goto end;
	}
	size -= len;
	str += len;

	ret = 0;

end:
	return ret;
}

static int32_t vpci_get_dev_cfg(struct acrn_vm *vm, union pci_bdf bdf, uint32_t offset, uint32_t cfg_len,
				 char *str_arg, size_t str_max)
{
	int32_t ret = -ENOMEM;
	struct acrn_vpci *vpci = &(vm->vpci);
	char *str = str_arg;
	size_t len, size = str_max;
	uint32_t i;
	uint32_t val = ~0U;
	char temp_str[TEMP_STR_SIZE];

	if (!pci_is_valid_access(offset, 4)) {
		snprintf(temp_str, TEMP_STR_SIZE, "Offset 0x%x does not align with 4 bytes, error\n", offset);
		shell_puts(temp_str);
		ret = -EINVAL;
		goto end;
	}

	if (cfg_len % 4) {
		snprintf(temp_str, TEMP_STR_SIZE, "Length %d is not multiple of 4 bytes, error\n", cfg_len);
		shell_puts(temp_str);
		ret = -EINVAL;
		goto end;
	}

	len = snprintf(str, size, "%02x:%02x.%x\n", bdf.bits.b, bdf.bits.d, bdf.bits.f);
	if (len >= size) {
		goto end;
	}
	size -= len;
	str += len;

	for (i = 0; i < cfg_len; i += 4) {
		ret = vpci_read_cfg(vpci, bdf, offset + i, 4, &val);
		if (ret == -ENODEV) {
			snprintf(temp_str, TEMP_STR_SIZE, "%02x:%02x.%x is an acrn-dm virtual PCI device.\n",
					bdf.bits.b, bdf.bits.d, bdf.bits.f);
			shell_puts(temp_str);
			goto end;
		} else if (ret) {
			snprintf(temp_str, TEMP_STR_SIZE, "Fail to read dev %02x:%02x.%x, offset 0x%x, len %d\n",
					bdf.bits.b, bdf.bits.d, bdf.bits.f, offset, cfg_len);
			shell_puts(temp_str);
			goto end;
		}

		len = snprintf(str, size, "%08x ", val);
		if (len >= size) {
			goto end;
		}
		size -= len;
		str += len;

		if (((i + 4) % 16) == 0) {
			len = snprintf(str, size, "\n");
			if (len >= size) {
				goto end;
			}
			size -= len;
			str += len;
		}
	}

	len = snprintf(str, size, "\r\n");
	if (len >= size) {
		goto end;
	}
	size -= len;
	str += len;

	ret = 0;

end:
	return ret;
}

static int parse_bdf(char *bdf_str, union pci_bdf *bdf)
{
	int bus = 0, dev = 0, func = 0;
	char *colon_pos;
	char *dot_pos;
	char bus_str[8] = {0}, device_str[8] = {0}, function_str[5] = {0};
	uint32_t bus_len;
	uint32_t dev_len;
	int ret = 0;

	if (strnlen_s(bdf_str, 128) > 7) {
		shell_puts("BDF string is too long! BDF format should be XX:YY.Z, XX:Y.Z or X:YY.Z.\n");
		ret = -EINVAL;
		goto end;
	}

	colon_pos = strchr(bdf_str, ':');
	dot_pos = strchr(bdf_str, '.');
	if (!colon_pos || !dot_pos || colon_pos > dot_pos) {
		shell_puts("Fail to parse BDF! BDF format should be XX:YY.Z, XX:Y.Z or X:YY.Z.\n");
		ret = -EINVAL;
		goto end;
	}
	bus_len = (uint32_t)(colon_pos - bdf_str);
	dev_len = (uint32_t)(dot_pos - colon_pos - 1);
	strncpy_s(bus_str, bus_len + 1, bdf_str, bus_len);
	bus_str[bus_len] = '\0';
	strncpy_s(device_str, dev_len + 1, colon_pos + 1, dev_len);
	device_str[dev_len] = '\0';
	strncpy_s(function_str, 2, dot_pos + 1, 2);

	bus = strtoul_hex(bus_str);
	dev = strtoul_hex(device_str);
	func = strtoul_hex(function_str);

	bdf->bits.b = bus;
	bdf->bits.d = dev;
	bdf->bits.f = func;

end:
	return ret;
}

static int32_t shell_dump_pci(int32_t argc, char **argv)
{
	uint16_t vm_id = 0;
	int ret = 0;
	uint32_t offset, length;
	union pci_bdf bdf;
	char bdf_str[10] = {0};
	struct acrn_vm *vm;

	switch (argc) {
	case 4:
		strncpy_s(bdf_str, 10, argv[1], 8);
		ret = parse_bdf(bdf_str, &bdf);
		if (ret) {
			goto end;
		}

		offset = strtoul_hex(argv[2]);
		length = (uint64_t)strtol_deci(argv[3]);

		shell_puts("\r\n----- Dump physical devices PCI CFG -----\r\n");
		ret = pci_get_dev_cfg(shell_log_buf, SHELL_LOG_BUF_SIZE, bdf, offset, length);
		if (!ret) {
			shell_puts(shell_log_buf);
		}
		break;

	case 6:
		if ((argv[1][0] == '-') && (argv[1][1] == 'v') && (argv[1][2] == 'm')
		    && (argv[1][3] == '_') && (argv[1][4] == 'i') && (argv[1][5] == 'd')) {
			vm_id = (uint16_t)strtol_deci(argv[2]);

			strncpy_s(bdf_str, 10, argv[3], 8);
			ret = parse_bdf(bdf_str, &bdf);
			if (ret) {
				goto end;
			}

			offset = strtoul_hex(argv[4]);
			length = (uint64_t)strtol_deci(argv[5]);

			snprintf(shell_log_buf, SHELL_LOG_BUF_SIZE, "\r\n----- Dump VM %d device (%02x:%02x.%x) PCI CFG: offset 0x%x, len %d -----\r\n",
				 vm_id, bdf.bits.b, bdf.bits.d, bdf.bits.f, offset, length);
			shell_puts(shell_log_buf);

			vm = get_vm_from_vmid(vm_id);
			ret = vpci_get_dev_cfg(vm, bdf, offset, length, shell_log_buf, SHELL_LOG_BUF_SIZE);
			if (!ret) {
				shell_puts(shell_log_buf);
			}
		} else {
			ret = -EINVAL;
		}
		break;
	default:
		shell_puts("\r\n----- Parameter error for dump_pci -----\r\n");
		ret = -EINVAL;
	}

end:
	return ret;
}

static int32_t vpci_get_vm_devs_bdf(struct acrn_vm *vm, char *str_arg, size_t str_max)
{
	uint16_t idx;
	struct pci_vdev *vdev;
	struct acrn_vpci *vpci = &(vm->vpci);
	char *str = str_arg;
	size_t len, size = str_max;
	int ret = -ENOMEM;

	for (idx = 0U; idx < CONFIG_MAX_PCI_DEV_NUM; idx++) {
		if (!bitmap_test((idx & 0x3FU), &vpci->vdev_bitmaps[idx >> 6U])) {
			continue;
		}

		vdev = &vpci->pci_vdevs[idx];
		if (vdev->user == NULL) {
			continue;
		}

		if (vdev->pdev) {
			len = snprintf(str, size, "%02x:%02x.%x -> %02x:%02x.%x\n",
					vdev->bdf.bits.b, vdev->bdf.bits.d, vdev->bdf.bits.f,
					vdev->pdev->bdf.bits.b, vdev->pdev->bdf.bits.d, vdev->pdev->bdf.bits.f);
		} else {
			len = snprintf(str, size, "%02x:%02x.%x -> NULL\n",
					vdev->bdf.bits.b, vdev->bdf.bits.d, vdev->bdf.bits.f);
		}
		if (len >= size) {
			goto end;
		}
		size -= len;
		str += len;
	}

	len = snprintf(str, size, "\r\n");
	if (len >= size) {
		goto end;
	}
	size -= len;
	str += len;

	ret = 0;

end:
	return ret;
}

static int32_t shell_lspci(int32_t argc, char **argv)
{
	uint16_t vm_id = 0;
	int ret = 0;
	struct acrn_vm *vm;

	switch (argc) {
	case 1:
		shell_puts("\r\n----- List physical devices BDF -----\r\n");
		ret = pci_get_hw_bdf(shell_log_buf, SHELL_LOG_BUF_SIZE);
		if (!ret) {
			shell_puts(shell_log_buf);
		}
		break;
	case 3:
		if ((argv[1][0] == '-') && (argv[1][1] == 'v') && (argv[1][2] == 'm')
		    && (argv[1][3] == '_') && (argv[1][4] == 'i') && (argv[1][5] == 'd')) {
			vm_id = (uint16_t)strtol_deci(argv[2]);
			snprintf(shell_log_buf, SHELL_LOG_BUF_SIZE, "\r\n----- List VM %d devices BDF (virt bdf -> phys bdf) -----\r\n", vm_id);
			shell_puts(shell_log_buf);
			vm = get_vm_from_vmid(vm_id);
			ret = vpci_get_vm_devs_bdf(vm, shell_log_buf, SHELL_LOG_BUF_SIZE);
			if (!ret) {
				shell_puts(shell_log_buf);
			}
		} else {
			ret = -EINVAL;
		}
		break;
	default:
		shell_puts("\r\n----- Parameter error for lspci -----\r\n");
		ret = -EINVAL;
	}

	return ret;
}
