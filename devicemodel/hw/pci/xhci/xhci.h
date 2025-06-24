#ifndef __XHCI_H__
#define __XHCI_H__

#include <bits/types/struct_itimerspec.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/queue.h>
#include <stdbool.h>
#include <stdint.h>
#include "xhciregs.h"
#include "usb/core.h"
#include "work.h"
#include "timer.h"
#include "mevent.h"

//#define XHCI_RING_THREADED

/* ACRN xHCI iDs */
#define PCI_ACRN_XHCI_VID	0x1234
#define PCI_ACRN_XHCI_PID	0x1234

/* Intel xHCI IDs */
#define PCI_INTEL_APL_XHCI_VID	0x8086
#define PCI_INTEL_APL_XHCI_PID	0x5aa8

#define	PCI_USBREV		0x60	/* USB protocol revision */
#define	PCI_USB_REV_3_0		0x30	/* USB 3.0 */

#define ACRN_XHCI_USB3_PORTS	15
#define ACRN_XHCI_USB2_PORTS	15
#define ACRN_XHCI_DEVICE_SLOTS	64
#define ACRN_XHCI_INTERRUPTERS	1	/* Even linux uses only one event ring */
#define ACRN_XHCI_CAPLEN	64	/* Spec Table 5-1: Capability Registers up to 256 bytes */
#define ACRN_XHCI_VERSION	0x0100	/* BCD number for xHCI version that we implement */
#define ACRN_XHCI_IST		0x4	/* TODO profile backend URB submission overhead */
#define ACRN_XHCI_ERST_MAX	3	/* 2^3 event ring segment table entries */
#define ACRN_XHCI_PSA_MAX	3	/* 2^(3+1) streams */
//#define ACRN_XHCI_EXTCAP_OFFSET 0x8000	/* Place extended capabilities here for APL? */

static inline unsigned usb_speed_to_xhci_psi(enum usb_device_speed speed)
{
	unsigned map[] = {
		[USB_SPEED_UNKNOWN] = XHCI_PSI_UNDEFINED,
		[USB_SPEED_LOW] = XHCI_PSI_LS,
		[USB_SPEED_FULL] = XHCI_PSI_FS,
		[USB_SPEED_HIGH] = XHCI_PSI_HS,
		[USB_SPEED_SUPER] = XHCI_PSI_SS_GEN1X1,
		[USB_SPEED_SUPER_PLUS] = XHCI_PSI_SS_GEN2X1,
	};
	return map[speed];
}

enum xhci_reset {
	POWER_ON_RESET,
	HC_RESET,
	LIGHT_RESET
};

struct xhci_hw_configs {
	enum model {
		PCI_XHCI_ACRN,
		PCI_XHCI_APL,
	} model;
	uint16_t drd_bus, drd_port;	/* physical location of DAP port which is exposed to guest
					   via DRD capability */
	uint16_t xhci_version;
	uint32_t page_size_mask;
	uint8_t usb3_ports;
	uint8_t usb2_ports;
	uint8_t ports;
	uint8_t maxslots;	/* 1~255 */
	uint16_t maxintrs;	/* Max Interrupters, 1~1024. Should match MSI-X Vectors */
	uint8_t maxerst;	/* Max ERST Entries/Segments, power of 2, 0~15 */
	uint8_t maxpsa;		/* Max Primary Stream Array Size. 0-15. 0 means no stream support.
				   Effective size is power(2, maxpsa + 1) */

	char *device_opts[128];
	uint8_t device_num;
};

static inline char width2char(uint8_t width)
{
	char map[] = { '?', 'b', 'w', '?', 'l', '?', '?', '?', 'q' };
	return map[width];
};

struct range {
	uint64_t off;	/* start */
	uint64_t len;
};

static inline bool range_in(struct range r1, struct range r2)
{
	return (r1.off >= r2.off) && ((r1.off + r1.len) <= (r2.off + r2.len));
}

struct xhci_caps {
	union cap_header header;	/* 0x00 */
	union hcsparams1 hcsparams1;	/* 0x04 */
	union hcsparams2 hcsparams2;	/* 0x08 */
	union hcsparams3 hcsparams3;	/* 0x0C */
	union hccparams1 hccparams1;	/* 0x10 */
	uint32_t doorbell_offset;	/* 0x14 */
	uint32_t runtime_regs_offset;	/* 0x18 */
	union hccparams2 hccparams2;	/* 0x1C */
} __packed;
G_STATIC_ASSERT(sizeof(struct xhci_caps) == 0x20);

struct xhci_extcap;
struct pci_xhci;
struct xhci_extcap_desc {
	uint8_t id;
	uint16_t priv_size;	/* private data size */
	const char *name;

	int (*init)(struct xhci_extcap *cap, const void *cfg);
	void (*deinit)(struct xhci_extcap *cap);
	/* offset in the cap */
	void (*write)(struct xhci_extcap *cap, uint16_t offset, size_t width, uint64_t data);
	uint64_t (*read)(struct xhci_extcap *cap, uint16_t offset, size_t width);
};
struct xhci_extcap {
	STAILQ_ENTRY(xhci_extcap) list;
	struct xhci_extcap_desc *desc;
	uint16_t caplen;	/* include the common id and next pointer */
	uint16_t offset;
	uint8_t next;
	uint8_t priv[];
};

int xhci_init_extcaps(struct pci_xhci *xhci);
void xhci_destroy_extcaps(struct pci_xhci *xhci);
uint32_t xhci_add_extcaps(struct pci_xhci *xhci, uint32_t offset);
int xhci_extcap_read(struct pci_xhci *xhci, uint32_t offset, int width, uint64_t *value);
int xhci_extcap_write(struct pci_xhci *xhci, uint32_t offset, int width, uint64_t value);

struct xhci_ring {
	pthread_mutex_t lock;
	gatomicrefcount ref;

	struct mevent *kick_event;
	int kick_fd;

	bool periodic;
	uint16_t interval;		/* in micro-frame, must be power of 2, for periodic rings */
	uint16_t last_mfindex;		/* mfindex of last poll */
	CIRCLEQ_ENTRY(xhci_ring) list;	/* on periodic scheduling list */
	LIST_ENTRY(xhci_ring) tmp_list;

	/* flags for the condition variable. Equivalent to Or-ed multiple conditions */
	volatile bool notified;
	volatile bool running;
	volatile bool stopping;
	volatile bool destroying;
	volatile bool resetting;

	struct xhci_ring_ops *ops;
	struct pci_xhci *xhci;
	char *name;
};

struct xhci_ring_ops {
	unsigned priv_size;
	int (*init)(struct xhci_ring *ring, void *data); // called w/o ring lock.
	void (*deinit)(struct xhci_ring *ring);// called when ref reaches 0 w/o ring lock
	void (*reset)(struct xhci_ring *ring);
	int (*start)(struct xhci_ring *ring);
	void (*stop)(struct xhci_ring *ring);
	void (*notify)(struct xhci_ring *ring);
	bool (*handle_trb)(struct xhci_ring *ring); // called w/o ring lock, grab explicitly if needed.
};

static inline void *xhci_ring_priv(struct xhci_ring *ring)
{
	return ring + 1;
}

static inline struct xhci_ring *xhci_ring_from_priv(void *priv)
{
	return (struct xhci_ring *)priv - 1;
}

/* data is used to fill ring_priv */
struct xhci_ring *xhci_ring_create(struct pci_xhci *xhci, struct xhci_ring_ops *ops,
		const char *name, uint16_t interval, void *data);
struct xhci_ring *xhci_ring_get(struct xhci_ring *ring);
void xhci_ring_put(struct xhci_ring *ring);
void xhci_ring_destroy(struct xhci_ring *ring, bool wait);
void xhci_ring_lock(struct xhci_ring *ring);
void xhci_ring_unlock(struct xhci_ring *ring);
int xhci_ring_start(struct xhci_ring *ring); /* notify if already started */
void xhci_ring_stop(struct xhci_ring *ring, bool wait);
void xhci_ring_stop_locked(struct xhci_ring *ring, bool wait);
void xhci_ring_notify(struct xhci_ring *ring);
//void xhci_ring_notify_locked(struct xhci_ring *ring);
void xhci_ring_reset(struct xhci_ring *ring, bool wait);
//void xhci_ring_reset(struct xhci_ring *ring);

struct event_list {
	STAILQ_ENTRY(event_list) list;
	union xhci_trb trb;
	bool gen_int;
};

struct event_ring_priv {
	uint32_t intr_id;
	struct interrupter intr;		/* raw registers */

	STAILQ_HEAD(evt_list, event_list) incoming;	/* keep in order */
	pthread_mutex_t incoming_lock;		// TODO optimize this with atomic op.

	struct event_ring_state {
		struct erst *erst_base;

		struct seg {
			uint32_t index;
			uint32_t trbs;
			uint64_t base;
			union xhci_trb *base_trb;
		} cur, next;

		uint32_t left_trbs;

		uint64_t enq_trb_gpa;
		union xhci_trb *enq_trb;	/* next to enqueue */

		atomic_uint is_full;
		bool pcs;	/* producer cycle state. Toggles when wrap erst to first segment */
	} state, saved;
};

enum ctrl_xfer_stage {
	CTRL_XFER_STAGE_SETUP = 0,
	CTRL_XFER_STAGE_DATA,
	CTRL_XFER_STAGE_DATA_NORMAL,
	CTRL_XFER_STAGE_STATUS,
};

struct xhci_usb_transfer_priv {
	struct xhci_ring *xring;
	struct usb_transfer *xfer;
	STAILQ_ENTRY(xhci_usb_transfer_priv) list;	/* on outstanding transfer list */

	uint64_t first_deq_ptr;		/* first trb for this transfer.*/
	union xhci_trb *first_trb;
	uint64_t last_deq_ptr;		/* first_trb and last_trb may not be in the same segment.
					   They are used to resolve the TRBs when transfer done */
	union xhci_trb *last_trb;

	uint16_t target_frame;
	bool skip_td;
	bool canceled;
	bool start_rcs;			/* rcs for the first TRB, valid in compositing transfer.
					   On stop endpoint command, discard current transfer and
					   restore rcs and deq_trb/gpa with these members. */
	//bool ring_stop_after_this;
};

struct transfer_ring_init_data {
	bool is_stream;
	union {
		struct epctx_desc *ep_desc;
		struct stream_desc *stream_desc;
	};
};

/* for transfer rings */
struct transfer_ring_priv {
	struct pci_xhci *xhci;
	struct epctx_desc *epctx_desc;
	struct stream_desc *stream_desc;
	uint8_t slot;
	uint8_t ep_dci;
	uint16_t stream;
	uint16_t mps;			/* Max Packet Size */
	//uint32_t edtla;		/* for current finishing TD */
	//uint32_t tdsize;		/* for current TD */
	//bool td_stopped;		/* a TD is stopped caused by ep stop command */

	struct usb_transfer *cur_xfer;	/* current in compositing transfer, not submitted yet */
	STAILQ_HEAD(, xhci_usb_transfer_priv) outstanding;
	pthread_mutex_t outstanding_lock;

	enum ctrl_xfer_stage next_ctrl_stage;

	/* set by config_endpoint or set_tr_deq_ptr command */
	bool rcs;			/* Consumer Cycle State, i.e. bit flag for valid TRB */
	uint64_t stopped_trb_gpa;	/* last completed TRB */
	union xhci_trb *stopped_trb;
	uint64_t deq_ptr_gpa;		/* next to fetch */
	union xhci_trb *deq_trb;	/* mapped on ring start */
	union xhci_trb *last_map_base;
};
extern struct xhci_ring_ops transfer_ring_ops;

/* for command ring */
struct command_ring_priv {
	bool rcs;		/* Consumer Cycle State, i.e. bit flag for valid TRB */
	uint64_t deq_ptr_gpa;	/* Set by writing CRCR */
	union xhci_trb *deq_trb;
	union xhci_trb *last_map_base;
};

struct xhci_port_regs {
	atomic_uint sc;
	atomic_uint pmsc;
	atomic_uint li;
	atomic_uint hlpmc;
} __packed;
G_STATIC_ASSERT(sizeof(struct xhci_port_regs) == 0x10);

struct xhci_port {
	struct pci_xhci *xhci;
	struct usb_port *port;
	struct work reset_work;
	bool psceg;		/* port status change event generation flag. Or-ed from xxc */
	bool psceg_pending;	/* event pending because of HC halted/Event Ring full */

	struct xhci_port_regs regs;

};

static inline enum usb_transfer_type ep_type_to_xfer_type(uint8_t ep_type)
{
	enum usb_transfer_type map[8] = {
		[0] = USB_XFER_TYPE_CTRL, /* ep_type 0 is invalid */
		[XHCI_EP_TYPE_ISOC_OUT] = USB_XFER_TYPE_ISOC,
		[XHCI_EP_TYPE_BULK_OUT] = USB_XFER_TYPE_BULK,
		[XHCI_EP_TYPE_INT_OUT] = USB_XFER_TYPE_INT,
		[XHCI_EP_TYPE_CTRL] = USB_XFER_TYPE_CTRL,
		[XHCI_EP_TYPE_ISOC_IN] = USB_XFER_TYPE_ISOC,
		[XHCI_EP_TYPE_BULK_IN] = USB_XFER_TYPE_BULK,
		[XHCI_EP_TYPE_INT_IN] = USB_XFER_TYPE_INT
	};
	return map[ep_type]; /* assume input is valid */
}

#if 0
/* xhci epidx. 0: ctrl ep (bidirectional), 1: ep1 out, 2 ep1 in, etc */
static inline uint8_t xhci_ep_idx_to_epid(uint8_t epidx)
{
	return (((epidx + 1) & 0x1) << 7) | ((epidx + 1) / 2);

}
#endif

/* input: 1-31 */
static inline uint8_t xhci_ep_dci_to_epid(uint8_t ep_dci)
{
	return ((ep_dci & 0x1) << 7) | (ep_dci / 2);
}
static inline uint8_t epid_to_xhci_ep_dci(uint8_t epid)
{
	if (!(epid & 0xF))
		return 1;
	else
		return (epid & 0xF) * 2 + !!(epid & 0x80);
}

struct stream_desc {
	struct epctx_desc *ep_desc;
	struct stream_ctx *stream_ctx;
	struct xhci_ring *xring;
	bool enabled;
	uint16_t stream;
};

struct epctx_desc {
	struct devctx_desc *devctx_desc;
	struct ep_ctx *epctx;
	struct stream_desc *stream_descs;
	struct stream_ctx *stream_ctxs;	/* stream context array */
	struct xhci_ring *xring;	/* for single transfer ring */
	uint8_t dci;
	bool enabled;
	int streams;
	bool halting;
	struct work halt_work;		/* stop may be triggered in xfer_done, which can't get ring
					 * lock (deadlock if outstanding xfers triggering a halt
					 * after stop initiated) */
};

struct devctx_desc {
	pthread_rwlock_t rwlock;	/* following fields can be accessed from vcpu thread and
					   command ring handler simultaneously */
	bool selected;			/* selected/deselected when setting config.maxslots */
	bool enabled;			/* enabled by command TRB_TYPE_ENABLE_SLOT */

	struct slot_ctx *sctx;
	struct output_ctxs *ctxs;	/* the output context from guest, mapped on first slot
					   related command (address device, any exceptions?) */
	struct epctx_desc ep_descs[XHCI_DCI_MAX]; /* indexed by endpoint DCI (Device Context Index).
					   So zero is not used because DCI 0 is for slot context.
					   We use DCI to reference an endpoint to align with TRB
					   parameters. */
	struct usb_device *device;	/* device connected, set on ADDRESS_DEVICE command,
					   cleared on DISABLE_SLOT command. */
	struct pci_xhci *xhci;
	uint32_t slot;
};


struct pci_xhci {
	struct pci_vdev *pdev;
	struct xhci_hw_configs configs;
	STAILQ_HEAD(, xhci_extcap) extcaps;

	uint16_t pid, vid;
	// pci_cfg_regs some capabilities there should be emulated? TBD

	struct xhci_caps caps;
	struct range opregs, rtregs, dbregs;
	uint64_t bar_len;

	bool stopping;			/* in progress of usbcmd.rs = 0 */
	atomic_uint usbcmd;		/* usbcmd.hcrst = 1 means reset WIP */
	atomic_uint usbsts;
	atomic_uint dnctrl;
	atomic_uint config;

	struct xhci_ring *cmd_ring;
	struct xhci_ring *event_primary;

	struct work reset_work;
	struct work stop_work;

	struct xhci_port *ports;	/* downstream ports */

	/* device slot management */
	struct devctx_desc *ctx_descs;	/* context 0: scratchpad, 1-maxslots: devices */
	uint64_t dcbaap_gpa;
	uint64_t *dcbaap;		/* mapped when enabling first slot,
					   unmapped when disabling last slot */

	struct timespec init_time;	/* for micro-frame counting */
	uint64_t mfindex;		/* micro-frame counter */
	CIRCLEQ_HEAD(, xhci_ring) sched_rings;	/* in interval increasing order */
	pthread_mutex_t sched_lock;
	struct acrn_timer sched;
};

static inline uint32_t xhci_get_mfindex(struct pci_xhci *xhci)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	uint64_t mf_diff = (t.tv_sec - xhci->init_time.tv_sec) * 8000 + 
			(t.tv_nsec - xhci->init_time.tv_nsec) / 125000;
	return mf_diff & XHCI_RTS_MFINDEX_MASK;
}

static inline void xhci_get_time_for_it(struct pci_xhci *xhci, uint32_t interval, struct timespec *ret)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	uint64_t mf_diff = (t.tv_sec - xhci->init_time.tv_sec) * 8000 + 
			(t.tv_nsec - xhci->init_time.tv_nsec) / 125000;
	mf_diff = ALIGN_UP(mf_diff + 1, interval);
	uint64_t new = xhci->init_time.tv_sec * 1000000000 + xhci->init_time.tv_nsec + mf_diff * 125000;
	ret->tv_sec = new / 1000000000;
	ret->tv_nsec = new % 1000000000;
}

static inline struct itimerspec xhci_sched_get_next(struct pci_xhci *xhci)
{
	struct itimerspec ret = {0};

	if (!CIRCLEQ_EMPTY(&xhci->sched_rings)) {
		struct xhci_ring *r = CIRCLEQ_FIRST(&xhci->sched_rings);
		ret.it_interval.tv_sec = 0;	/* polling interval > 1s ? Impossible */
		ret.it_interval.tv_nsec = 125000 * r->interval;
		xhci_get_time_for_it(xhci, r->interval, &ret.it_value);
	}
	return ret;
}

void sched_timer_handler(void *data, uint64_t nexp);

#define XHCI_DUMP_STRUCTS
#ifdef XHCI_DUMP_STRUCTS
void xhci_dump_trb(struct xhci_ring *ring, const union xhci_trb *trb, uint64_t trb_gpa,
				const char *comment);
void xhci_dump_octxs(struct output_ctxs *octxs, const char *comment);
void xhci_dump_ictxs(struct input_ctxs *ictxs, const char *comment);
#else
static inline void xhci_dump_trb(struct xhci_ring *xring, const union xhci_trb *trb,
		uint64_t trb_gpa, const char *comment)
{}
static inline void xhci_dump_octxs(struct output_ctxs *octxs, const char *comment) {}
static inline void xhci_dump_ictxs(struct input_ctxs *ictxs, const char *comment) {}
#endif

#define READ_ONCE(p) (*(volatile typeof(*p) *)(p))
#define WRITE_ONCE(p, v) (*(volatile typeof(*p) *)(p) = (v))

#endif
