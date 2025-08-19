/*
 * Copyright (C) 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <types.h>
#include <pci.h>
#include <uart16550.h>
#include <shell.h>
#include <timer.h>
#include <ticks.h>
#include <vuart.h>
#include <logmsg.h>
#include <acrn_hv_defs.h>
#include <asm/guest/vm.h>
#include <console.h>
#include <boot.h>
#include <dbg_cmd.h>
#include <asm/lib/spinlock.h>

struct hv_timer console_timer;

/* With a typical physical UART at baud rate 115200, the data transfer
 * rate is approximately 115200 bits/s (115.2 bits/ms). And typically
 * UART communicates with 8 data bits, 1 start bit and 1 stop bit per
 * character, this amounts to about 11.52 characters per ms.
 *
 * Since we have the console timer firing every 40ms, we should limit
 * the maximum characters written to UART within each tick so that the
 * writing operation does not block other threads (especially vcpu threads).
 *
 * To trade-off between interference to vcpu thread and UART usability,
 * here we limit the physical uart to print 110 characters (10ms print time)
 * during each tick.
 */
#define CONSOLE_TICK_MAX_TRANSFER_CHARS 110

#define CONSOLE_KICK_TIMER_TIMEOUT  40UL /* timeout is 40ms*/
/* Switching key combinations for shell and uart console */
#define GUEST_CONSOLE_ESCAPE_KEY	0x0 /* the "break", put twice to send "break" to guest */
#define GUEST_CONSOLE_TO_HV_SWITCH_KEY  'e' /* escape + e to switch back to hv console */
uint16_t console_vmid = CONFIG_CONSOLE_DEFAULT_VM;

/* if use INIT to kick pcpu only, if not notification IPI still is used for sharing CPU */
static bool use_init_ipi = false;

uint16_t console_loglevel = CONFIG_CONSOLE_LOGLEVEL_DEFAULT;
static spinlock_t console_log_lock;

bool is_using_init_ipi(void)
{
	return use_init_ipi;
}

static void parse_hvdbg_cmdline(void)
{
	const char *start = NULL;
	const char *end = NULL;
	struct acrn_boot_info *abi = get_acrn_boot_info();

	start = abi->cmdline;

	while ((*start) != '\0') {
		while ((*start) == ' ')
			start++;
		if ((*start) != '\0') {
			end = start + 1;
			while ((*end != ' ') && ((*end) != '\0'))
				end++;

			if (!handle_dbg_cmd(start, (int32_t)(end - start))) {
				/* if not handled by handle_dbg_cmd, it can be handled further */
				if (strncmp(start, "USE_INIT_IPI", (size_t)(end - start)) == 0) {
					use_init_ipi = true;
				}
			}
			start = end;
		}
	}

}

void console_init(void)
{
	/*Parse cmdline to get UART setting*/
	parse_hvdbg_cmdline();

	/*
	 * Enable UART as early as possible.
	 * Then we could use printf for debugging on early boot stage.
	 */
	uart16550_init(true);

	spinlock_init(&console_log_lock);
}

void console_putc(const char *ch)
{
	(void)uart16550_puts(ch, 1U);
}


size_t console_write(const char *s, size_t len)
{
	return  uart16550_puts(s, len);
}

char console_getc(void)
{
	return uart16550_getc();
}

/*
 * @post return != NULL
 */
struct acrn_vuart *vm_console_vuart(struct acrn_vm *vm)
{
	return &vm->vuart[0];
}

/**
 * @pre vu != NULL
 * @pre vu->active == true
 */
static void vuart_console_rx_chars(struct acrn_vuart *vu)
{
	char ch = -1;
	bool recv = false;

	while (1) {
		/* Get data from physical uart */
		ch = uart16550_getc();
		if (ch == -1)
			break;

		if (vu->escaping) {
			vu->escaping = false;
			switch (ch) {
				case GUEST_CONSOLE_ESCAPE_KEY:
					vuart_putchar(vu, ch);
					vu->lsr |= LSR_BI;
					recv = true;
					break;
				case GUEST_CONSOLE_TO_HV_SWITCH_KEY:
					/* Switch the console */
					console_vmid = ACRN_INVALID_VMID;
					printf("\r\n\r\n ---Entering ACRN SHELL---\r\n");
					/* following inputs are for hv, don't handle in this loop */
					goto exit;
				default:
					printf("Unknown escaping key: '%c'\r\n", ch);
					break;
			}
		} else {
			if (ch == GUEST_CONSOLE_ESCAPE_KEY) {
				vu->escaping = true;
			} else {
				vuart_putchar(vu, ch);
				recv = true;
			}
		}
	}

exit:
	if (recv) {
		vuart_toggle_intr(vu);
	}
}

/**
 * @brief Send out vUART tx to console
 *
 * This function reads vUART tx chars and print on console.
 * The output is throttled as it may block CPU when there is a flood of message
 * from vUART.
 * When the 16550 UART tx fifo has no headroom for the incoming chars, this function
 * will return true to indicate that the output is throttled. Then the caller
 * can wait for a certain time for the tx headroom to be available again.
 * The char that is fetched from vUART but not transferred will be pended to the next round.
 * Polling is not needed when we output number of chars within the headroom value.
 *
 * @return A bool value to indicate whether output is throttled.
 *
 * @pre vu != NULL
 */
static bool vuart_console_tx_chars(struct acrn_vuart *vu)
{
	bool is_throttled = false;
	static char c = -1;

	/* When c == -1 Get a new char.
	   Otherwise it should be the pending char of last round. Try to transfer it first. */
	if (c == -1) {
		c = vuart_getchar(vu);
	}

	while(c != -1) {
		/* If there is no headroom in uart tx fifo, pend it till next round.
		   '\n' will be expanded to '\r\n', it needs headroom for 2. */
		if ((c == '\n' && uart16550_get_tx_headroom() < 2) ||
			 uart16550_get_tx_headroom() < 1) {
			is_throttled = true;
			break;
		}
		printf("%c", c);

		c = vuart_getchar(vu);
	}

	return is_throttled;
}

static struct acrn_vuart *vuart_console_active(void)
{
	struct acrn_vm *vm = NULL;
	struct acrn_vuart *vu = NULL;

	if (console_vmid < CONFIG_MAX_VM_NUM) {
		vm = get_vm_from_vmid(console_vmid);
		if (!is_paused_vm(vm) && !is_poweroff_vm(vm)) {
			vu = vm_console_vuart(vm);
		} else if (CONFIG_CONSOLE_DEFAULT_VM == ACRN_INVALID_VMID) {
			/*
			 * Default console vm is not set in config, and it is invalid,
			 * switch back to HV-Shell
			 */
			console_vmid = ACRN_INVALID_VMID;
		}
	}

	return ((vu != NULL) && vu->active) ? vu : NULL;
}

/**
 * @brief Console timer callback.
 *
 * vUART console tx and rx chars are processed here.
 * Tx chars may be throttled as message flood may block CPU.
 * When tx is throttled, next tick interval is reduced to the time when the debug UART
 * is expected to send out all chars in its fifo.
 *
 * When we send less chars than tx fifo depth each time, the debug UART can avoid polling.
 *
 * @param data unused for timer callback.
 *
 * @return None.
 */
static void console_timer_callback(__unused void *data)
{
	struct acrn_vuart *vu;
	bool is_tx_throttled;
	uint64_t period;

	/* Kick HV-Shell and Uart-Console tasks */
	vu = vuart_console_active();
	if (vu != NULL) {
		/* serial Console Rx operation */
		vuart_console_rx_chars(vu);
		/* serial Console Tx operation */
		is_tx_throttled = vuart_console_tx_chars(vu);

		/* Update timer period, so that we can fill UART tx fifo again
		   right after chars in it been sent out. */
		period = is_tx_throttled ?
			uart16550_get_tx_load_interval() : (TICKS_PER_MS * CONSOLE_KICK_TIMER_TIMEOUT);
		if (period != console_timer.period_in_cycle) {
			update_timer(&console_timer, console_timer.timeout, period);
		}
	} else {
		shell_kick();
	}
}

void console_setup_timer(void)
{
	uint64_t period_in_cycle, fire_tsc;

	period_in_cycle = TICKS_PER_MS * CONSOLE_KICK_TIMER_TIMEOUT;
	fire_tsc = cpu_ticks() + period_in_cycle;
	initialize_timer(&console_timer,
			console_timer_callback, NULL,
			fire_tsc, period_in_cycle);

	/* Start an periodic timer */
	if (add_timer(&console_timer) != 0) {
		pr_err("Failed to add console kick timer");
	}
}

/* When lapic-pt is enabled for a vcpu working on the pcpu hosting
 * console timer, we utilize vm-exits to drive the console.
 *
 * Note that currently this approach will result in a laggy shell when
 * the number of VM-exits/second is low (which is mostly true when lapic-pt is
 * enabled).
 */
void console_vmexit_callback(struct acrn_vcpu *vcpu)
{
	static uint64_t prev_tsc = 0;
	uint64_t tsc;

	if ((pcpuid_from_vcpu(vcpu) == VUART_TIMER_CPU) && (is_lapic_pt_configured(vcpu->vm))) {
		tsc = cpu_ticks();
		if (tsc - prev_tsc > (TICKS_PER_MS * CONSOLE_KICK_TIMER_TIMEOUT)) {
			console_timer_callback(NULL);
			prev_tsc = tsc;
		}
	}
}

void suspend_console(void)
{
	if (VUART_TIMER_CPU == BSP_CPU_ID) {
		del_timer(&console_timer);
	}
}

void resume_console(void)
{
	if (VUART_TIMER_CPU == BSP_CPU_ID) {
		console_setup_timer();
	}
}

bool console_need_log(uint32_t severity)
{
	return (severity <= console_loglevel);
}

void console_log(char *buffer)
{
	uint64_t rflags;

	spinlock_irqsave_obtain(&console_log_lock ,&rflags);

	/* Send buffer to stdout */
	printf("%s\n\r", buffer);

	spinlock_irqrestore_release(&console_log_lock, rflags);
}
