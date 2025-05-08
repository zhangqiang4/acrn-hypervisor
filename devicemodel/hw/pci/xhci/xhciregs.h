#ifndef __XHCIREGS_H__
#define __XHCIREGS_H__

#include <stdatomic.h>
#include <stdint.h>
#include <glib.h>
#include "types.h"

/*
 * xHCI defined Register Attributes:
 * RO - Read Only
 * RW - Read-Write
 * RW1C - Write 1 to clear status
 * RW1S - Write 1 to set status
 * ROS - Sticky RO. Registers that consume AUX Power preserves across resets.
 * RWS - Sticky Rw.
 * RW1CS - Sticky RW1C.
 * Rsvd - Reserved for future RO implementations. BE returns 0 when read and ignores writes.
 * RsvdO - Reserved and Opaque. BE may use it for any purpose. FE can not use it.
 * RsvdP - Reserved for future RW implementations. FE will use Read-Modify-Write sequence.
 * RsvdZ - Reserved for future RW1C implementations. FE will use 0 for writes.
 */

/* 
 * xHCI capability registers - RO and Constant.
 * This range start from offset 0 with length set in cap_header.caplength
 */
#define XHCI_CAP_HEADER		0x0
union cap_header {
	atomic_uint dword;
	struct {
		uint8_t caplength;
		uint8_t _rsvd0;
		uint16_t hciversion; /* BCD encoding, 1.0.0 is 0x0100 */
	};
} __packed;
G_STATIC_ASSERT(sizeof(union cap_header) == 4);

#define XHCI_CAP_HCSPARAMS1	0x4
union hcsparams1 {
	atomic_uint dword;
	struct {
		uint32_t maxslots : 8;
		uint32_t maxintrs : 11;
		uint32_t _rsvd1 : 5;
		uint32_t maxports : 8;
	};
} __packed;
G_STATIC_ASSERT(sizeof(union hcsparams1) == 4);

#define XHCI_CAP_HCSPARAMS2	0x8
union hcsparams2 {
	atomic_uint dword;
	struct {
		/* Isochronous Scheduling Threshold. Required transfers enqueue advancements.
		 * Highest bit select the unit: frame or micro-frame, following bits is the
		 * (micro)frame count.
		 * TODO take virtualization overhead into account */
		uint32_t ist : 4;
		uint32_t erst_max : 4;	/* Event Ring Segment Table Max entries, power of 2 */
		uint32_t _rsvd2 : 13;

		/* 
		 * scratch buffers required for xHCI internal use. DCBAA slot 0 is the buffers
		 * address array.
		 */
		uint32_t max_scratchpads_hi : 5;
		/* Scratchpad Restore. Valid if max_scratchpads > 0.
		 * A value of 1 indicates xHC uses scratchpad buffers to save state when executing
		 * Save and Restore State operations. So the integrity of scratchpad buffers need
		 * to be * maintained across power cycles. Drivers should save and restore them.
		 */
		uint32_t spr: 1;
		uint32_t max_scratchpads_lo : 5;
	};
} __packed;
G_STATIC_ASSERT(sizeof(union hcsparams2) == 4);

#define XHCI_CAP_HCSPARAMS3	0xC
union hcsparams3 {
	atomic_uint dword;
	struct {
		/* worst latency for port link state from U1/U2 to U0.
		 * TODO do we need to virtualise this for power savings ?
		 */
		uint32_t u1_exit_latency : 8;
		uint32_t _rsvd3 : 8;
		uint32_t u2_exit_latency : 16;
	};
} __packed;
G_STATIC_ASSERT(sizeof(union hcsparams3) == 4);

#define XHCI_CAP_HCCPARAMS1	0x10
union hccparams1 {
	atomic_uint dword;
	struct {
		/* optional capabilities */
		uint32_t ac64 : 1;	/* 64-bit addressing capability */
		uint32_t bnc : 1;	/* bandwidth negotiation capability */
		uint32_t csz : 1;	/* Context Size. 0: 32byte context; 1: 64 byte context */
		uint32_t ppc : 1;	/* Port Power Control. Control physical port power */
		uint32_t pind : 1;	/* Port Indicators */
		uint32_t lhrc : 1;	/* Light HC Reset Capability */
		uint32_t ltc : 1;	/* Latency Tolerance Messaging Capability */
		uint32_t nss : 1;	/* No Secondary SID Support */
		uint32_t pae : 1;	/* Parse All Event Data */
		uint32_t spc : 1;	/* Stopped Short Packet Capability */
		uint32_t sec : 1;	/* Stopped EDTLA Capability */
		uint32_t cfc : 1;	/* Contiguous Frame ID Capability */
		uint32_t max_psa_size : 4; /* Max Primary Stream Array Size, 2^(x+1) */
/* XHCI extended capability ID's */
#define	XHCI_EXTCAP_USB_LEGACY		0x0001
#define	XHCI_EXTCAP_PROTOCOLS		0x0002
#define	XHCI_EXTCAP_POWER_MGMT		0x0003
#define	XHCI_EXTCAP_VIRTUALIZATION	0x0004
#define	XHCI_EXTCAP_MSG_IRQ		0x0005
#define	XHCI_EXTCAP_USB_LOCAL_MEM	0x0006
#define	XHCI_EXTCAP_DRD_INTEL		0x00C0
		uint32_t xecp : 16;	/* xHCI Extended Capability Pointer */
	};
} __packed;
G_STATIC_ASSERT(sizeof(union hccparams1) == 4);

#define XHCI_CAP_DBOFF		0x14	/* doorbell array offset, dword aligned */
#define XHCI_CAP_RTSOFF		0x18	/* runtime register space offset, 32-byte aligned */

#define XHCI_CAP_HCCPARAMS2	0x1C
union hccparams2 {
	atomic_uint dword;
	struct {
		uint32_t u3c : 1;	/* Port suspend completion event notification support.
					   Allow guest to handle port suspend asynchronously.
					   Introduced since 1.0, required since 1.1 */
		uint32_t cmc : 1;	/* support Max Exit Latency Too Large error for
					   config endpoint command */
		uint32_t fsc : 1;	/* Force Save Context Capability.
					   Value 1 indicates Save State operation shall save any
					   cached slot, endpoint, stream and other context
					   information to memory.
					   Introduced since 1.0, required since 1.1 */
		uint32_t ctc : 1;	/* Compliance Transition Capability */
		uint32_t lec : 1;	/* Large ESIT Payload Capability */
		uint32_t cic : 1;	/* Configuartion Information Capability */
		uint32_t etc : 1;	/* Extended TBC Capability */
		uint32_t etc_tsc: 1;	/* Extended TBC TRB Status Capability */
		uint32_t gsc : 1;	/* Get/Set Extended Property Capability */
		uint32_t vtc : 1;	/* Virtualization Based Trusted I/O Capability */
		uint32_t _rsvd4 : 22;
	};
} __packed;
G_STATIC_ASSERT(sizeof(union hccparams2) == 4);

/*
 * operational registers.
 * Right following capability registers. Length is determined by max supported ports.
 */
#define XHCI_OP_USBCMD		0x0
#define XHCI_OP_USBCMD_RS	BIT(0)	/* Run/Stop. Also showing current state */
#define XHCI_OP_USBCMD_HCRST	BIT(1)	/* HC Reset. Only allow to reset a halted HC */
#define XHCI_OP_USBCMD_INTE	BIT(2)	/* Interrupter Enable */
#define XHCI_OP_USBCMD_HSEE	BIT(3)	/* Host System Error Enable */
#define XHCI_OP_USBCMD_LHCRST	BIT(7)	/* Light HC Reset, depends on hccparams1.lhrc */
#define XHCI_OP_USBCMD_CSS	BIT(8)	/* Controller Save State */
#define XHCI_OP_USBCMD_CRS	BIT(9)	/* Controller Restore State */
#define XHCI_OP_USBCMD_EWE	BIT(10)	/* Enable Wrap Event */
#define XHCI_OP_USBCMD_EU3S	BIT(11)	/* Enable U3 MFINDEX Stop */
#define XHCI_OP_USBCMD_CME	BIT(13) /* CEM (Config Error MaxLatencyTooLarge) Enable. Dep. hccparams2.cmc. */
#define XHCI_OP_USBCMD_ETE	BIT(14)	/* Extended TBC Enable. Depends on hccparams2.etc. */
#define XHCI_OP_USBCMD_TSCEN	BIT(15)	/* Extended TBC TRB Status Enable. Depends on hccparams2.etc_tsc. */
#define XHCI_OP_USBCMD_VTIOE	BIT(16)	/* VTIO Enable. Depends hccparams2.vtc. */

#define XHCI_OP_USBSTS		0x4
#define XHCI_OP_USBSTS_HCH	BIT(0)	/* Halted, due to rs = 0 or HC internal error */
#define XHCI_OP_USBSTS_HSE	BIT(2)	/* Host System Error. RW1C */
#define XHCI_OP_USBSTS_EINT	BIT(3)	/* Event Interrupt. RW1C. Set when IP changes from 0 to 1
					   of any Interrupter */
#define XHCI_OP_USBSTS_PCD	BIT(4)	/* Port Change Detect. RW1C. */
#define XHCI_OP_USBSTS_SSS	BIT(8)	/* Save State Status. Set when state saving is ongoing */
#define XHCI_OP_USBSTS_RSS	BIT(9)	/* Restore State Status. Set when state restoration is ongoing */
#define XHCI_OP_USBSTS_SRE	BIT(10)	/* Save/Restore Error. RW1C. Error occurred in state save/restore */
#define XHCI_OP_USBSTS_CNR	BIT(11)	/* Controller Not Ready. Set in reset process to inform
					   driver that the db and op registers are not writable */
#define XHCI_OP_USBSTS_HCE	BIT(12)	/* Host Controller Error. Show HC internal error */
#define XHCI_USBSTS_EVT_MASK	(XHCI_OP_USBSTS_EINT | XHCI_OP_USBSTS_PCD | XHCI_OP_USBSTS_SRE)

#define XHCI_OP_PAGESZ		0x8
#define XHCI_PAGE_SIZE_4K	0x1
#define XHCI_PAGE_SIZE_16K	0x4
#define XHCI_PAGE_SIZE_64K	0x10
#define XHCI_PAGE_SIZE_128M	0x8000

#define XHCI_OP_DNCTRL		0x14
#define XHCI_OP_DNCTRL_MASK	0xFFFF	/* 16 possible notification types */

#define XHCI_OP_CRCR		0x18	/* command ring control register */
/* Ring Cycle State. It's the value of xHC Consumer Cycle State (CCS) flag for the TRB referenced
 * by the Command Ring Pointer.  Reading this flag always returns 0. */
#define XHCI_OP_CRCR_RCS	BIT(0)
#define XHCI_OP_CRCR_CS		BIT(1)	/* Command Stop. RW1S.  Return 0 on read. */
#define XHCI_OP_CRCR_CA		BIT(2)	/* Command Abort. RW1S.  Return 0 on read. */
#define XHCI_OP_CRCR_CRR	BIT(3)	/* Command Ring Running. RO. */
/* Command Ring Pointer. High order bits of the initial value of the 64-bit command ring dequeue
 * pointer. Return 0 on read. */
#define XHCI_OP_CRCR_PTR_SHIFT	6
#define XHCI_OP_CRCR_PTR_MASK	(~0x3FULL)

#define XHCI_OP_DCBAAP		0x30	/* Device Context Base Address Pointer */
#define XHCI_OP_DCBAAP_MASK	(~0x3FULL)	/* low 6 bits are rsvdz */

#define XHCI_OP_CONFIG		0x38
/* specifies the max number of enabled Device Slots. Disabled Device Slots shall not respond to
 * Doorbell Register references. */
#define XHCI_OP_CONFIG_MAXSLOTS_MASK	0xFF
/* U3 Entry Enable. Depends on hccparams2.u3c. When set, xHC shall assert PLC when a Root Hub port
 * transition to U3 State. */
#define XHCI_OP_CONFIG_U3E		BIT(8)
/* Configuration Information Enable. Depends on hccparams2.cic. When set, driver shall initialize
 * the Configuration Value, Interface Number, and Alternate Setting fields in the Input Control
 * Context when it's associated with a Configure Endpoint Command. When cleared, the extended
 * Input Control Context fields are not supported. */
#define XHCI_OP_CONFIG_CIE		BIT(9)

#define XHCI_OP_PORT_START	0x400
/* following is per-port registers in operational register space */
#define XHCI_OP_PORT_SC		0x0	/* In Aux Power well. Not Affected by lhcrst. */
#define XHCI_OP_PORT_SC_CCS	BIT(0)	/* Current Connect Status. ROS. 0 if pp is 0 */
#define XHCI_OP_PORT_SC_PED	BIT(1)	/* Port Enabled/Disabled. RW1CS. Software can only write 1
					   to this bit and pr = 0, to disable the port.
					   This bit is set by xHC when PLS is
					   Polling state and following conditions are met:
					   For USB2 ports: Software set pr to 1 
					   For USB3 ports: xHC will automatically transition to
					   enabled state if link training succeeds. */
#define XHCI_OP_PORT_SC_OCA	BIT(3)	/* Over-current Active. RO. */
#define XHCI_OP_PORT_SC_PR	BIT(4)	/* Power Reset. RW1S. Write 1 to trigger bus reset (Hot
					   reset). It will keep set until reset done.
					   For USB2 ports, Software shall write 1 to transition the
					   port from Polling state to the Enabled state */
#define XHCI_OP_PORT_SC_PLS_SHIFT 5
#define XHCI_OP_PORT_SC_PLS_MASK 0x1E0	/* Show current Link status. Some values are allowed to
					   write to initiate state transition (with lws) */
#define XHCI_PLS_U0		0	/* Writable to transition from Ux to U0 */
#define XHCI_PLS_U1		0x20
#define XHCI_PLS_U2		0x40	/* Writable for USB2 ports. USB3 has automatic U1/U2 */
#define XHCI_PLS_U3		0x60	/* Writable to Suspended */
#define XHCI_PLS_DISABLED	0x80
#define XHCI_PLS_RXDETECT	0xA0	/* Writable for USB3 ports to transition from Disabled
					   state to RxDetect state */
#define XHCI_PLS_INACTIVE	0xC0
#define XHCI_PLS_POLLING	0xE0
#define XHCI_PLS_RECOVERY	0x100
#define XHCI_PLS_HOTRESET	0x120
#define XHCI_PLS_COMPLIANCE	0x140	/* Writable for USB3 ports */
#define XHCI_PLS_TEST		0x160
#define XHCI_PLS_RESUME		0x1E0	/* Writable for USB2 ports from U3 */
#define XHCI_OP_PORT_SC_PP	BIT(9)	/* Port Power. RW. Logic power control. If hccparams1.ppc
					   is 1, this bit controls port physical power. */
#define XHCI_OP_PORT_SC_SPEED_SHIFT 10
#define XHCI_OP_PORT_SC_SPEED_MASK 0x3C00 /* Spped of connected device. See Protocol Speed ID (PSI)
					   for encodings. */
#define XHCI_PSI_UNDEFINED	0
#define XHCI_PSI_FS		0x400
#define XHCI_PSI_LS		0x800
#define XHCI_PSI_HS		0xC00
#define XHCI_PSI_SS_GEN1X1	0x1000
#define XHCI_PSI_SS_GEN2X1	0x1400
#define XHCI_PSI_SS_GEN1X2	0x1800
#define XHCI_PSI_SS_GEN2X2	0x1C00
#define XHCI_OP_PORT_SC_PIC_SHIFT 14
#define XHCI_OP_PORT_SC_PIC_MASK 0xC000	/* Control Port Indicator. RWS. Depends hccparams1.pind */
#define XHCI_OP_PORT_SC_LWS	BIT(16)	/* Port Link State Write Strobe */
/* event bits, RW1CS */
#define XHCI_OP_PORT_SC_CSC	BIT(17)	/* Connect Status Change */
#define XHCI_OP_PORT_SC_PEC	BIT(18) /* Port Enabled/Disabled Change. USB2 only. Only set when 1
					   to 0 transition caused by Port_Error */
#define XHCI_OP_PORT_SC_WRC	BIT(19)	/* Warm Port Reset Change (Done). USB3 only. */
#define XHCI_OP_PORT_SC_OCC	BIT(20)	/* Over-current Change */
#define XHCI_OP_PORT_SC_PRC	BIT(21)	/* Port Reset Change (Done). For both warm and hot */
#define XHCI_OP_PORT_SC_PLC	BIT(22)	/* Port Link State Change. */
#define XHCI_OP_PORT_SC_CEC	BIT(23) /* Port Config Error Change. USB3 only. RsvdZ for USB2.
					   Port failed to configure its link partner. */
#define XHCI_PORTSC_EVENT_MASK	0x00FE0000U
#define XHCI_OP_PORT_SC_CAS	BIT(24) /* Cold Attach Status. USB3 only. Far-end Terminations
					   were detected in the disconnected state. */
#define XHCI_OP_PORT_SC_WCE	BIT(25)	/* Wake on Connect Enable. RWS. */
#define XHCI_OP_PORT_SC_WDE	BIT(26)	/* Wake on Disconnect Enable. RWS. */
#define XHCI_OP_PORT_SC_WOE	BIT(27)	/* Wake on Over-current Enable. RWS. */
#define XHCI_PORTSC_WAKE_MASK	(XHCI_OP_PORT_SC_WCE | XHCI_OP_PORT_SC_WDE | XHCI_OP_PORT_SC_WOE)
#define XHCI_OP_PORT_SC_DR	BIT(30)	/* Device Removable */
#define XHCI_OP_PORT_SC_WPR	BIT(31)	/* Warm Port Reset. RW1S. USB3. Deeper than hot reset */
#define XHCI_PORTSC_RESET_MASK	(XHCI_OP_PORT_SC_PR | XHCI_OP_PORT_SC_WPR)

#define XHCI_OP_PORT_PMSC		0x4
union portpmsc {			/* In Aux Power well. Not Affected by lhcrst. */
	atomic_uint dword;
	struct {
		uint32_t u1t : 8;	/* U1 Timeout */
		uint32_t u2t : 8;	/* U2 Timeout */
		uint32_t fla : 1;	/* Force Link PM Accept. */
		uint32_t _rsvdp : 15;
	} usb3;
	struct {
		uint32_t l1s : 3;	/* L1 Status */
		uint32_t rwe : 1;	/* Remote Wake Enable. RW */
		uint32_t besl : 4;	/* Best Effort Service Latency. RW */
		uint32_t l1d : 8;	/* L1 Device Slot. Software sets the Device Slot ID of the
					   device connected to this port. xHC uses it to generate
					   the LPM Token packet. */
		uint32_t hle : 1;	/* Hardware LPM Enable. Depends on HLC */
		uint32_t _rsvdp0 : 11;
		uint32_t test: 4;	/* Port Test Control */
	} usb2;
} __packed;
G_STATIC_ASSERT(sizeof(union portpmsc) == 4);

#define XHCI_OP_PORT_LI			0x8
union portli {
	atomic_uint dword;
	struct {
		uint32_t lec : 16;	/* Link Error Count */
		uint32_t rlc : 4;	/* Rx Lane Count */
		uint32_t tlc : 4;	/* Tx Lane Count */
		uint32_t _rsvdp : 8;
	} usb3;
	/* rsvdp for usb2 */
} __packed;
G_STATIC_ASSERT(sizeof(union portli) == 4);

#define XHCI_OP_PORT_HLPMC		0xC
union porthlpmc {
	atomic_uint dword;
	struct {			/* Depends on LSECC */
		uint32_t lsec : 16;	/* Link Soft Error Count */
		uint32_t _rsvdp : 16;
	} usb3;
	struct {			/* Depends on HLC */
		uint32_t hirdm : 2;	/* Host Initiated Resume Duration Mode */
		uint32_t l1t : 8;	/* L1 Timeout */
		uint32_t besld : 4;	/* Best Effort Service Latency Deep */
		uint32_t _rsvdp : 18;
	} usb2;
} __packed;
G_STATIC_ASSERT(sizeof(union porthlpmc) == 4);

/*
 * Runtime registers - frequently accessed at runtime.
 */

/* When usbcmd.eu3s is 0, the xHC may stop the MFINDEX counting action if all
 * Root Hub ports are in the Disconnected, Disabled, Training, or Powered-off state.
 * When usbcmd.eu3s is 1, the xHC may stop the MFINDEX counting action if all
 * Root Hub ports are in the U3, Disconnected, Disabled, or Powered-off state.
 */
#define XHCI_RTS_MFINDEX	0x0
#define XHCI_RTS_MFINDEX_MASK	0x3FFF

#define XHCI_RTS_INT_START	0x20
#define XHCI_RTS_INT_IMAN	0x0
#define XHCI_RTS_INT_IMAN_IP	BIT(0)
#define XHCI_RTS_INT_IMAN_IE	BIT(1)

#define XHCI_RTS_INT_IMOD	0x4				/* interrupt throttling */
/* Interrupt Moderation Interval. Time to count, in 250ns increments */
#define XHCI_RTS_INT_IMOD_I_SHIFT	0
#define XHCI_RTS_INT_IMOD_I_MASK	0xFF
/* Interrupt Moderation Counter. */
#define XHCI_RTS_INT_IMOD_C_SHIFT	16
#define XHCI_RTS_INT_IMOD_C_MASK	0xFF00

#define XHCI_RTS_INT_ERSTSZ	0x8
/* # of ERST entries pointed by ERSTBA. Write 0 to disable this Event Ring. (Can't do this for the
 * Primary Event Ring (Ring 0)) */
#define XHCI_RTS_INT_ERSTSZ_SIZE_MASK	0xFF
/* high word is rsvdp */

#define XHCI_RTS_INT_ERSTBA	0x10
/* low 6 bits are rsvdp and whole qword is effective ERSTBA */

#define XHCI_RTS_INT_ERDP	0x18
/* Dequeue ERST Segment Index */
#define XHCI_RTS_INT_ERDP_DESI_MASK	0x7
/* Event Handler Busy. Set when IP bit set, and cleared by software when the Dequeue Pointer
 * register is written */
#define XHCI_ERDP_DESI_MAX (XHCI_RTS_INT_ERDP_DESI_MASK + 1)
#define XHCI_RTS_INT_ERDP_EHB		BIT(3)
#define XHCI_RTS_INT_ERDP_MASK		(~0xFULL)	/* Event Ring Dequeue Pinter. */

/* Event Ring Segment Table entry in memory */
struct erst {
	uint64_t _rsvdz0 : 6;
#define XHCI_ERST_SEG_BASE_SHIFT	6
	uint64_t seg_base : 58;
	uint32_t seg_size : 16;		/* in trbs */
	uint32_t _rsvdz1 : 16;
	uint32_t _rsvdz2;
} __packed;

struct interrupter {
	atomic_uint iman;
	atomic_uint imod;
	atomic_uint erstsz;
	uint32_t _rsvdp;
	atomic_ulong erstba;
	atomic_ulong erdp;
} __packed;
G_STATIC_ASSERT(sizeof(struct interrupter) == 0x20);

/*
 * Doorbell registers
 */

/* each for a ring for guest to produce data, i.e. command ring and transfer rings.
 * Doorbell 0 is for HC and Doorbell 1 ~ Device Slots Number is for Device Contexts
 */
union doorbell {
	atomic_uint dword;
	struct {
#define DB_TARGET_CMD		0		/* for doorbell 0 only */
#define DB_EP0_ENQUEUED		1		/* for bidirectional ep0 */
#define DB_EPX_OUT_ENQUEUED(ep)	((ep) * 2)	/* for unidirectional ep1-15 out */
#define DB_EPX_IN_ENQUEUED(ep)	((ep) * 2 + 1)	/* for unidirectional ep1-15 in */
#define DB_TARGET_MAX		32
		uint32_t target : 8;
		uint32_t _rsvdz : 8;
		uint32_t stream : 16;
	};
} __packed;

/*
 * Contexts - in-memory data structures
 */

struct slot_ctx {
	uint32_t route : 20;	/* 4-bit encodes a downstream port number. 5 tiers without root port number */
	uint32_t speed : 4;	/* deprecated ? */
	uint32_t _rsvdz0: 1;
	uint32_t mtt : 1;
	uint32_t hub : 1;
	uint32_t ctxs : 5;	/* # of following endpoint contexts, 1-31 */
	uint32_t max_exit_latency : 16;		/* time to wake up all links along the path */
	uint32_t rhport : 8;	/* root hub port number in the port path, numbered from 1 */
	uint32_t ports : 8;	/* for hub, hub downstream ports */
	uint32_t parent_hub_slot : 8;	/* 0 for dev under rh. */
	uint32_t parent_port : 8;	/* 0 for dev under rh. */
	uint32_t ttt : 2;	/* TT Think Time. 0 */
	uint32_t _rsvdz1 : 4;
	uint32_t intr : 10;	/* Interrupter to receive BW Request/Device Notification/Ring Underrun/Overrun */
	uint32_t addr : 8;	/* Device Address */
	uint32_t _rsvdz2 : 19;
#define XHCI_SLOT_STATE_DISABLED	0
#define XHCI_SLOT_STATE_ENABLED		0
#define XHCI_SLOT_STATE_DEFAULT		1
#define XHCI_SLOT_STATE_ADDRESSED	2
#define XHCI_SLOT_STATE_CONFIGURED	3
	uint32_t state : 5;	/* Output only */
	uint32_t _rsvdo[4];
} __packed;
G_STATIC_ASSERT(sizeof(struct slot_ctx) == 0x20);

struct ep_ctx {
#define XHCI_EP_STATE_DISABLED		0
#define XHCI_EP_STATE_RUNNING		1
#define XHCI_EP_STATE_HALTED		2
#define XHCI_EP_STATE_STOPPED		3
#define XHCI_EP_STATE_ERROR		4
	uint32_t state : 3;	/* Output only */
	uint32_t _rsvdz0 : 5;
	uint32_t mult : 2;	/* If LEC=0, this indicates the max bursts - 1 within an Interval.
				   If LEC=1, this is RsvdZ and Mult is calculated as
				   Roundup(Max ESIT Payload / Max Packet Size / (Max Burst Size + 1)) - 1*/
	uint32_t mpstream: 5;	/* Max Primary Streams. Effective value: power(2, mpstream+1) for 1-15 */
	uint32_t lsa : 1;	/* Linear Stream Array */
	uint32_t interval : 8;	/* in micro-frames, this is power of 2 */
	uint32_t esit_hi : 8;	/* Max ESIT (Endpoint Service Time Interval) Payload. For periodic
				   endpoints. It's the total bytes can transfer in an Interval */
	/* dword 1 */
	uint32_t _rsvdz1 : 1;
	uint32_t err_cnt : 2;	/* Actually max retry count. 0 means no limit */
#define XHCI_EP_TYPE_ISOC_OUT	1
#define XHCI_EP_TYPE_BULK_OUT	2
#define XHCI_EP_TYPE_INT_OUT	3
#define XHCI_EP_TYPE_CTRL	4
#define XHCI_EP_TYPE_ISOC_IN	5
#define XHCI_EP_TYPE_BULK_IN	6
#define XHCI_EP_TYPE_INT_IN	7
#define XHCI_EP_TYPE_IS_CTRL(x) ((x) == 4)
#define XHCI_EP_TYPE_IS_BULK(x) ((x) == 2 || (x) == 6)
#define XHCI_EP_TYPE_IS_INT(x) ((x) == 3 || (x) == 7)
#define XHCI_EP_TYPE_IS_ISOC(x) ((x) == 1 || (x) == 5)
	uint32_t type : 3;	/* Endpoint type. */
	uint32_t _rsvdz2 : 1;
	uint32_t hid : 1;	/* Host Initiate Disable. */
	uint32_t mbs : 8;	/* Max Burst Size - 1 (transactions in a scheduling opportunity) */
	uint32_t mps : 16;	/* Max Packet Size */
	/* dword 2 and 3 */
	uint64_t dcs : 1;	/* Dequeue Cycle State. CCS flag for TR Dequeue Pointer.
				   0 if Max Primary Streams > 0 */
	uint64_t _rsvdz3 : 3;
#define XHCI_TR_DEQ_PTR_SHIFT	4
	uint64_t tr_deq_ptr : 60;/* TR Dequeue Pointer, to a Transfer Ring if MaxPStreams=0 or a
				   Stream Context Array. As input, this is next to use. As output,
				   this is current using when halted or stopped */
	uint32_t avg_trb_len : 16;
	uint32_t esit_lo : 16;

	uint32_t _rsvdo[3];
} __packed;
G_STATIC_ASSERT(sizeof(struct ep_ctx) == 0x20);

/* Device Context Index for slot context and endpoint contexts */
#define XHCI_DCI_SLOT_CTX		0
#define XHCI_DCI_EP0_CTX		1
#define XHCI_DCI_EPX_CTX(ep, dir_in)	(2 * (ep) + (dir_in))
#define XHCI_DCI_MAX			32
struct output_ctxs {
	union {
		struct slot_ctx slot_ctx;
		struct ep_ctx ep_ctx;
	} ctxs[XHCI_DCI_MAX];
	//struct slot_ctx slot_ctx;
	//struct ep_ctx ep_ctxs[31];
} __packed;
G_STATIC_ASSERT(sizeof(struct output_ctxs) == 0x400);

struct input_ctrl_ctx {
	uint32_t drop_ctx : 32; /* Each bit identifies which endpoint context shall be disabled */
	uint32_t add_ctx : 32;  /* Each bit identifies which Device Context shall be evaluated
				   and/or enabled by a command. */
	uint32_t _rsvdz0[5];
	uint32_t config : 8;	/* These 3 fields should be set for a Configure Endpoint Command
				   when hccparams2.CIC=1 and config.CIE=1 */
	uint32_t interface : 8;
	uint32_t alt_set : 8;
	uint32_t _rsvdz1 : 8;
} __packed;

struct input_ctxs {
	struct input_ctrl_ctx ctrl_ctx;
	union {
		struct slot_ctx slot_ctx;	/* for index 0 */
		struct ep_ctx ep_ctx;		/* for index 1-31 */
	} ctxs[32];
	//struct slot_ctx slot_ctx;
	//struct ep_ctx ep_ctxs[31];
} __packed;
G_STATIC_ASSERT(sizeof(struct input_ctxs) == 0x420);

struct stream_ctx {
	uint64_t dcs : 1;	/* Dequeue Cycle State. i.e. CCS */
#define XHCI_SCT_SECONDARY		0
#define XHCI_SCT_PRIMARY_ONLY		1
#define XHCI_SCT_PRIMARY_WITH_8_SSA	2
#define XHCI_SCT_PRIMARY_WITH_16_SSA	3
#define XHCI_SCT_PRIMARY_WITH_32_SSA	4
#define XHCI_SCT_PRIMARY_WITH_64_SSA	5
#define XHCI_SCT_PRIMARY_WITH_128_SSA	6
#define XHCI_SCT_PRIMARY_WITH_256_SSA	7
	uint64_t sct : 3;	/* Stream Context Type. Only support XHCI_SCT_PRIMARY_ONLY since
				   hccparams1.nss = 1 */
	uint64_t tr_deq_ptr : 60;

	uint64_t stopped_edtla : 24;	/* Depends on hccparams1.sec */
	uint64_t _rsvdo : 40;
} __packed;

struct port_bw_ctx {
	/* TBD */
};

/*
 * TRB definitions
 */

enum trb_type {
	TRB_TYPE_RESERVED,
	/* transfer ring trbs */
	TRB_TYPE_NORMAL,
	TRB_TYPE_SETUP_STAGE,
	TRB_TYPE_DATA_STAGE,
	TRB_TYPE_STATUS_STAGE,
	TRB_TYPE_ISOCH,
	TRB_TYPE_LINK,		/* can also be used in command ring */
	TRB_TYPE_EVENT_DATA,
	TRB_TYPE_NOOP_XFER,
	/* command ring trbs */
	TRB_TYPE_ENABLE_SLOT,
	TRB_TYPE_DISABLE_SLOT,
	TRB_TYPE_ADDRESS_DEVICE,
	TRB_TYPE_CONFIG_ENDPOINT,
	TRB_TYPE_EVAL_CONTEXT,
	TRB_TYPE_RESET_ENDPOINT,
	TRB_TYPE_STOP_ENDPOINT,
	TRB_TYPE_SET_TR_DEQUEUE_PTR,
	TRB_TYPE_RESET_DEVICE,
	TRB_TYPE_FORCE_EVENT,
	TRB_TYPE_NEGO_BW,
	TRB_TYPE_SET_LATENCY_TOLERANCE,
	TRB_TYPE_GET_PORT_BW,
	TRB_TYPE_FORCE_HEADER,
	TRB_TYPE_NOOP_CMD,
	TRB_TYPE_GET_EXT_PROP,
	TRB_TYPE_SET_EXT_PROP,
	/* event ring trbs */
	TRB_TYPE_EVENT_XFER = 32,
	TRB_TYPE_EVENT_CMD_DONE,
	TRB_TYPE_EVENT_PORT_STATUS_CHANGE,
	TRB_TYPE_EVENT_BW_REQUEST,
	TRB_TYPE_EVENT_DOORBELL,
	TRB_TYPE_EVENT_HC,
	TRB_TYPE_EVENT_DEVICE,
	TRB_TYPE_EVENT_MFINDEX_WRAP,
	TRB_TYPE_VENDOR_START = 48,	/* 48~63 for vendor defined trb types */
	TRB_TYPE_VENDOR_END = 63,
	TRB_TYPE_MAX
};

enum completion_code {
	COMPLETION_CODE_INVALID = 0,
	COMPLETION_CODE_SUCCESS,
	COMPLETION_CODE_BUFFER_ERROR,
	COMPLETION_CODE_BABBLE_ERROR,
	COMPLETION_CODE_TRANSACTION_ERROR,
	COMPLETION_CODE_TRB_ERROR,
	COMPLETION_CODE_STALL_ERROR,
	COMPLETION_CODE_RESOURCE_ERROR,
	COMPLETION_CODE_BANDWIDTH_ERROR,
	COMPLETION_CODE_NO_SLOT,
	COMPLETION_CODE_INVALID_STREAM_TYPE = 10,
	COMPLETION_CODE_SLOT_DISABLED,
	COMPLETION_CODE_ENDPOINT_DISABLED,
	COMPLETION_CODE_SHORT_PACKET,
	COMPLETION_CODE_RING_UNDERRUN,
	COMPLETION_CODE_RING_OVERRUN,
	COMPLETION_CODE_VF_EVENT_RING_FULL,
	COMPLETION_CODE_PARAM_ERROR,
	COMPLETION_CODE_BANDWIDTH_OVERRUN,
	COMPLETION_CODE_CTX_STATE_ERROR,
	COMPLETION_CODE_NO_PING_RESPONSE = 20,
	COMPLETION_CODE_EVENT_RING_FULL,
	COMPLETION_CODE_INCOMPATIBLE_DEVICE,
	COMPLETION_CODE_MISSED_SERVICE,		/* when an isoc TD missed target ESIT */
	COMPLETION_CODE_CMD_RING_STOPPED,
	COMPLETION_CODE_CMD_ABORTED,
	COMPLETION_CODE_STOPPED,
	COMPLETION_CODE_STOPPED_LEN_INVALID,
	COMPLETION_CODE_STOPPED_SHORT_PACKET,
	COMPLETION_CODE_MAX_EXIT_LATENCY_TOO_LARGE = 29,
	COMPLETION_CODE_ISOCH_BUF_OVERRUN = 31,
	COMPLETION_CODE_EVENT_LOST_ERROR,
	COMPLETION_CODE_UNDEFINED_ERROR,
	COMPLETION_CODE_INVALID_STREAM_ID,
	COMPLETION_CODE_SECONDARY_BW_ERROR,
	COMPLETION_CODE_SPLIT_TRANSACTION_ERROR = 36,
	COMPLETION_CODE_VERDOR_ERROR_START = 192,
	COMPLETION_CODE_VERDOR_ERROR_END = 223,
	COMPLETION_CODE_VENDOR_INFO_START = 224,
	COMPLETION_CODE_VENDOR_INFO_END = 255
};

union xhci_trb {
	struct {
		atomic_uint dword0;
		atomic_uint dword1;
		atomic_uint dword2;
		atomic_uint dword3;
	};
	struct {
		uint64_t param;
		uint32_t status;
		uint32_t c : 1;
		uint32_t ent : 1;
		uint32_t rsvd : 8;
		uint32_t type : 6;
		uint32_t control : 16;
	} common;			/* template trb. Use common since template is a keyword */
	struct {
		/* 0x0 */
		uint64_t buffer;	/* 64-bit address of the TRB data area */
		/* 0x8 */
		uint32_t len : 17;	/* 0 ~ 64K */
		uint32_t tdsize : 5;	/* Transfer Descriptor size. Include all chained TRBs */
		uint32_t intr : 10;	/* Interrupter Target. 0 ~ MaxIntrs-1 */
		/* 0xC */
		uint32_t c : 1;		/* cycle bit. To mark the Enqueue Pointer of the ring */
		uint32_t ent : 1;	/* Evaluate Next TRB. Let xHC fetch and evaluate the next
					   TRB before saving the endpoint state */
		uint32_t isp : 1;	/* Interrupt-on Short Packet. Short packet means size of a
					   IN packet is less than the amount specified in trb.len.
					   If isp is set and a short packet is detected, a Transfer
					   Event TRB shall be generated with its Completion Code set
					   to Short Packet. The length field in the Transfer Event
					   TRB reflects the residual number of bytes. Note this is
					   not an error.
					   If both isp and ioc are set to 1, and a Short Packet is
					   detected, only one Transfer Event TRB is generated. */
		uint32_t ns : 1;	/* No Snoop */
		uint32_t ch : 1;	/* Chain bit */
		uint32_t ioc : 1;	/* Interrupt On Completion. Actually, Event On Completion.
					   An interrupt is generated only if !bei */
		uint32_t idt : 1;	/* Immediate Data. If set, the buffer field is the data to
					   send, not a pointer, the len field contains a value
					   between 0 and 8 to indicate the bytes to send. */
		uint32_t _rsvdz0 : 2;
		uint32_t bei : 1;	/* Block Event Interrupt. If both bei and ioc are set to 1, 
					   the Transfer Event generated by IOC shall not assert an
					   interrupt */
		uint32_t type : 6;
		uint32_t _rsvdz1 : 16;
	} xfer_normal;
	struct {
		/* 0x0. First 8 bytes as the setup transaction data are defined in USB spec. */
		uint64_t bmRequestType : 8;
		uint64_t bRequest : 8;
		uint64_t wValue : 16;
		uint64_t wIndex : 16;
		uint64_t wLength : 16;	/* this is the data stage length */
		/* 0x8 */
		uint32_t len : 17;	/* trb len. always 8, for control setup stage */
		uint32_t _rsvdz0 : 5;
		uint32_t intr : 10;
		/* 0xC */
		uint32_t c : 1;
		uint32_t _rsvdz1 : 4;
		uint32_t ioc : 1;
		uint32_t idt : 1;
		uint32_t _rsvdz2 : 3;
		uint32_t type : 6;
#define SETUP_URB_XFER_NO_DATA	0
#define SETUP_URB_XFER_RSVD	1
#define SETUP_URB_XFER_OUT_DATA 2
#define SETUP_URB_XFER_IN_DATA	3
		uint32_t trt : 2;	/* Transfer Type */
		uint32_t _rsvdz3 : 14;
	} xfer_ctrl_setup;
	struct {
		/* 0x0 */
		uint64_t buffer;
		/* 0x8 */
		uint32_t len : 17;
		uint32_t tdsize : 5;
		uint32_t intr : 10;
		/* 0xC */
		uint32_t c : 1;
		uint32_t ent : 1;
		uint32_t isp : 1;
		uint32_t ns : 1;
		uint32_t ch : 1;
		uint32_t ioc : 1;
		uint32_t idt : 1;
		uint32_t _rsvdz0 : 3;
		uint32_t type : 6;
		uint32_t dir : 1;	/* Direction of the data stage transfer. 0 is OUT
					   (Write Data), 1 is IN (Read Data) */
		uint32_t _rsvdz1 : 15;
	} xfer_ctrl_data;
	struct {
		/* 0x0 */
		uint64_t _rsvdz0;
		/* 0x8 */
		uint32_t _rsvdz1 : 22;
		uint32_t intr : 10;
		/* 0xC */
		uint32_t c : 1;
		uint32_t ent : 1;
		uint32_t _rsvdz2 : 2;
		uint32_t ch : 1;
		uint32_t _rsvdz3 : 4;
		uint32_t type : 6;
		uint32_t dir : 1;
		uint32_t _rsvdz4 : 15;
	} xfer_ctrl_status;		/* The Transfer Event generated by this TRB shall reflect
					   the status state response from the USB device. */
	struct {
		/* 0x0 */
		uint64_t buffer;
		/* 0x8 */
		uint32_t len : 17;
		uint32_t tdsz_tbc : 5;	/* If usbcmd.ete is 0, this field defines the TD Size.
					   If usbcmd.ete is 1, this field defines the Transfer
					   Burst Count (TBC), which identifies the number of
					   burst - 1. All bursts except the last shall transfer
					   MaxBurstSize packets. The last burst shall transfer
					   TLBPC + 1 packets. */
		uint32_t intr : 10;
		/* 0xC */
		uint32_t c : 1;
		uint32_t ent : 1;
		uint32_t isp : 1;
		uint32_t ns : 1;
		uint32_t ch : 1;	/* This isoc trb marks the start of an isoc TD. It can be
					   followed by a number of normal trbs */
		uint32_t ioc : 1;
		uint32_t idt : 1;
		uint32_t tbc_trbsts : 2;/* If usbcmd.etc is 0, this field defines the number of
					   burst - 1. All bursts except the last shall transfer
					   MaxBurstSize packets. The last burst shall transfer
					   TLBPC + 1 packets. TODO */
		uint32_t bei : 1;
		uint32_t type : 6;
		uint32_t tlbpc : 4;	/* Transfer Last Burst Packet Count. This field is the
					   number of packet -1 that shall be in the last burst of
					   this isoc TD */
#define TARGET_FRAME_MASK	0x7FFU
		uint32_t frame_id : 11;	/* target frame as the start of the ISOC Interval */
		uint32_t asap : 1;	/* Start as soon as possible. Ignore frame_id */
	} xfer_isoc;
	struct {
		/* 0x0 */
		uint64_t _rsvdz0;
		/* 0x8 */
		uint32_t _rsvdz1 : 22;
		uint32_t intr : 10;
		/* 0xC */
		uint32_t c : 1;
		uint32_t ent : 1;
		uint32_t _rsvdz2 : 2;
		uint32_t ch : 1;
		uint32_t ioc : 1;
		uint32_t _rsvdz3 : 4;
		uint32_t type : 6;
		uint32_t _rsvdz4 : 16;
	} xfer_noop;
	/* event trbs */
	struct {
		/* 0x0 */
		uint64_t trb_ptr;	/* the xfer trb that generated this event when ed == 0,
					   or event data when ed == 1 */
		/* 0x8 */
		uint32_t trb_len : 24;	/* residual bytes not transferred */
		uint32_t cc : 8;	/* Completion Code */
		/* 0xC */
		uint32_t c : 1;
		uint32_t _rsvdz0 : 1;
		uint32_t ed : 1;	/* Event Data. When set, this event was generated by an
					   Event Data TRB and the trb_ptr field contains a 64-bit
					   value provided by the Event Data TRB.
					   If cleared, the trb_ptr field contains a pointer to the
					   TRB that generated this event. */
		uint32_t _rsvdz1 : 7;
		uint32_t type : 6;
		uint32_t endpoint : 5;	/* The index in the Device Context to select the Endpoint
					   Context associated with this event.
					   This is DCI of endpoint, not endpoint index !!! */
		uint32_t _rsvdz2 : 3;
		uint32_t slot : 8;	/* The index in the DCBAA to select the Device Context */
	} event_xfer;			/* Event TRB generated by a previous Transfer TRB */
	struct {
		/* 0x0 */
		uint64_t trb_ptr;	/* command TRB that generated this event */
		/* 0x8 */
		uint32_t param : 24;	/* From some commands. Otherwise it's treated as rsvdz */
		uint32_t cc : 8;
		/* 0xC */
		uint32_t c : 1;
		uint32_t _rsvdz0 : 9;
		uint32_t type : 6;
		uint32_t vf : 8;	/* for SRIOV */
		uint32_t slot : 8;	/* for enable-slot command */
	} event_cmd;			/* Event TRB generated by a previous Command TRB */
	struct {
		/* 0x0 */
		uint32_t _rsvdz0 : 24;
		uint32_t port : 8;
		/* 0x4 */
		uint32_t _rsvdz1 : 32;
		/* 0x8 */
		uint32_t _rsvdz2 : 24;
		uint32_t cc : 8;
		/* 0xC */
		uint32_t c : 1;
		uint32_t _rsvdz3 : 9;
		uint32_t type : 6;
		uint32_t _rsvdz4 : 16;
	} event_port_changed;
	struct {
		/* 0x0 */
		uint64_t _rsvdz0;
		/* 0x8 */
		uint32_t _rsvdz1 : 24;
		uint32_t cc : 8;
		/* 0xC */
		uint32_t c : 1;
		uint32_t _rsvdz2 : 9;
		uint32_t type : 6;
		uint32_t _rsvdz3 : 8;
		uint32_t slot : 8;	/* the slot should evaluate its bandwidth requirements */
	} event_bw_requuest;
	struct {
		/* 0x0 */
		uint32_t reason : 5;
		uint32_t _rsvdz0 : 27;
		/* 0x4 */
		uint32_t _rsvdz1 : 24;
		uint32_t cc : 8;
		/* 0xC */
		uint32_t c : 1;
		uint32_t _rsvdz2 : 9;
		uint32_t type : 6;
		uint32_t vf : 8;
		uint32_t slot : 8;
	} event_doorbell;		/* For SRIOV. Interrupter 0 receives all db events */
	struct {
		/* 0x0 */
		uint64_t _rsvdz0;
		/* 0x8 */
		uint32_t _rsvdz1 : 24;
		uint32_t cc : 8;
		/* 0xC */
		uint32_t c : 1;
		uint32_t _rsvdz2 : 9;
		uint32_t type : 6;
		uint32_t _rsvdz3 : 16;
	} event_hc;			/* state change or error reporting. See completion code */
	struct {
		/* 0x0 */
		uint32_t _rsvdz0 : 4;
		uint32_t notification_type : 4;
		uint32_t notification_data_lo : 24;
		/* 0x4 */
		uint32_t notification_data_hi : 32;
		uint32_t _rsvdz1 : 24;
		uint32_t cc : 8;
		/* 0xC */
		uint32_t c : 1;
		uint32_t _rsvdz2 : 9;
		uint32_t type : 6;
		uint32_t _rsvdz3 : 8;
		uint32_t slot : 8;	/* the device that generated the event */
	} event_device;			/* device notification TODO */
	struct {
		/* 0x0 */
		uint64_t _rsvdz0;	
		/* 0x8 */
		uint32_t _rsvdz1 : 24;
		uint32_t cc : 8;	/* always success */
		/* 0xC */
		uint32_t c : 1;
		uint32_t _rsvdz2 : 9;
		uint32_t type : 6;
		uint32_t _rsvdz3 : 16;
	} event_mfindex_wrap;
	/* command trbs */
	struct {
		uint32_t _rsvdz0[3];
		/* 0xC */
		uint32_t c : 1;
		uint32_t _rsvdz1 : 9;
		uint32_t type : 6;
		uint32_t _rsvdz2 : 16;
	} cmd_noop;
	struct {
		uint32_t _rsvdz0[3];
		/* 0xC */
		uint32_t c : 1;
		uint32_t _rsvdz1 : 9;
		uint32_t type : 6;
		uint32_t slot_type : 5;	/* always 0 ? TODO */
		uint32_t _rsvdz2 : 11;
	} cmd_enable_slot;
	struct {
		uint32_t _rsvdz0[3];
		/* 0xC */
		uint32_t c : 1;
		uint32_t _rsvdz1 : 9;
		uint32_t type : 6;
		uint32_t _rsvdz2 : 8;
		uint32_t slot_id: 8;
	} cmd_disable_slot;
	struct {
		uint64_t input_ctx_ptr;
		uint32_t _rsvdz0;
		/* 0xC */
		uint32_t c : 1;
		uint32_t _rsvdz1 : 8;
		uint32_t bsr : 1;	/* Block Set Address Request. Don't generate a
					   USB_SET_ADDRESS request. Only for
					   TRB_TYPE_ADDRESS_DEVICE */
		uint32_t type : 6;
		uint32_t _rsvdz2 : 8;
		uint32_t slot_id : 8;
	} cmd_address_device, cmd_eval_context;
	struct {
		uint64_t input_ctx_ptr;
		uint32_t _rsvdz0;
		/* 0xC */
		uint32_t c : 1;
		uint32_t _rsvdz1 : 8;
		uint32_t deconfig : 1;	/* de-configure, input_ctx ignored */
		uint32_t type : 6;
		uint32_t _rsvdz2 : 8;
		uint32_t slot_id : 8;
	} cmd_config_endpoint;
	struct {
		uint32_t _rsvdz0[3];
		/* 0xC */
		uint32_t c : 1;
		uint32_t _rsvdz1 : 8;
		uint32_t tsp : 1;	/* Transfer State Preserve */
		uint32_t type : 6;
		uint32_t endpoint : 5;
		uint32_t _rsvdz2 : 3;
		uint32_t slot : 8;
	} cmd_reset_endpoint;
	struct {
		uint32_t _rsvdz0[3];
		uint32_t c : 1;	/* 0xC */
		uint32_t _rsvdz1 : 9;
		uint32_t type : 6;
		uint32_t endpoint : 5;
		uint32_t _rsvdz2 : 2;
		uint32_t suspend : 1;
		uint32_t slot : 8;
	} cmd_stop_endpoint;
	struct {
		/* 0x0 */
		uint64_t ccs : 1;	/* ccs */
		uint64_t sctx_type : 3; /* stream context type */
		uint64_t tr_deq_ptr : 60;
		/* 0x8 */
		uint32_t _rsvdz0 : 16;
		uint32_t stream : 16;
		/* 0xC */
		uint32_t c : 1;
		uint32_t _rsvdz1 : 9;
		uint32_t type : 6;
		uint32_t endpoint : 5;
		uint32_t _rsvdz2 : 3;
		uint32_t slot : 8;
	} cmd_set_tr_dequeue_ptr;
	struct {
		uint32_t _rsvdz0[3];
		uint32_t c : 1;	/* 0xC */
		uint32_t _rsvdz1 : 9;
		uint32_t type : 6;
		uint32_t _rsvdz2 : 8;
		uint32_t slot : 8;
	} cmd_reset_device;
	struct {
		uint32_t _rsvdz0[4];
	} cmd_force_event, cmd_nego_bandwidth, cmd_set_ltv;
	struct {
		uint64_t port_bw_ctx_ptr;
		uint32_t _rsvdz0;
		uint32_t c : 1;	/* 0xC */
		uint32_t _rsvdz1 : 9;
		uint32_t type : 6;
		uint32_t dev_speed : 4;
		uint32_t _rsvdz2 : 4;
		uint32_t hub_slot : 8;
	} cmd_get_port_bandwidth;
	struct {
		uint32_t _rsvdz0[4];
	} cmd_force_header, cmd_get_ext_prop, cmd_set_ext_prop;
	/* other trbs */
	struct link_trb {
		uint64_t ring_segment;
		uint32_t _rsvdz0 : 22;	/* 0x8 */
		uint32_t intr : 10;
		uint32_t c : 1;	/* 0xC */
		uint32_t tc : 1;	/* toggle cycle */
		uint32_t _rsvdz1 : 2;
		uint32_t ch : 1;
		uint32_t ioc : 1;
		uint32_t _rsvdz2 : 4;
		uint32_t type : 6;
		uint32_t _rsvdz3 : 16;
	} link_trb;
	struct {
		uint64_t event_data;
		/* 0x8 */
		uint32_t _rsvdz0 : 22;
		uint32_t intr : 10;
		/* 0xC */
		uint32_t c : 1;
		uint32_t ent : 1;
		uint32_t _rsvdz1 : 2;
		uint32_t ch : 1;
		uint32_t ioc : 1;
		uint32_t _rsvdz2 : 3;
		uint32_t bei : 1;
		uint32_t type : 6;
		uint32_t _rsvdz3 : 16;
	} event_data_trb;
};

G_STATIC_ASSERT(sizeof(union xhci_trb) == 0x10);

static inline union xhci_trb make_cmd_event(uint64_t trb_gpa, uint8_t slot, enum completion_code cc)
{
	union xhci_trb evt = {0};
	evt.event_cmd.type = TRB_TYPE_EVENT_CMD_DONE;
	evt.event_cmd.trb_ptr = trb_gpa;
	evt.event_cmd.slot = slot;
	evt.event_cmd.cc = cc;
	return evt;
}

static inline union xhci_trb make_xfer_event(uint64_t trb_gpa, uint32_t trb_len,
		uint8_t slot, uint8_t ep_dci, uint8_t ed, enum completion_code cc)
{
	union xhci_trb evt = {0};
	evt.event_xfer.type = TRB_TYPE_EVENT_XFER;
	evt.event_xfer.trb_ptr = trb_gpa;
	evt.event_xfer.trb_len = trb_len;
	evt.event_xfer.slot = slot;
	evt.event_xfer.endpoint = ep_dci;
	evt.event_xfer.cc = cc;
	evt.event_xfer.ed = ed;
	return evt;
}

#endif
