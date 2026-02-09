#include "vmmapi.h"
#include "xhciregs.h"
#include <bits/types/struct_itimerspec.h>
#include <linux/limits.h>
#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/queue.h>
#include <unistd.h>

#define pr_prefix "usb: xhci: "
#include "log.h"
#include "xhci.h"
#include "types.h"
#include "dm.h"
#include "usb/core.h"
#include "usb/pmapper.h"
#include "pci_core.h"

/*
 * Modeling Rules:
 * - All register accesses are handled as quick as possible in main vcpu thread, thus in
 *   non-blocking/lock-less manner. This is also in accordance with design of xHCI spec.
 * - Asynchronous tasks such as resets/stops and TRB handling of command and transfer rings are
 *   handled in asynchronous work queues and mevents.
 * - Transfer submission and completion event handlers shall be classified as timing critical
 *   or non-critical. Although ring transfer can be implemented in different mevent contexts TODO,
 *   the completion side, libusb event handler thread can't do this. TBD directly use usbfs API.
 *
 */

/* --------------------- events ------------------- */

/*
 * Driver may change
 * - ERSTBA when the interrupter is not used,
 * - ERSTSZ at any time,
 * - ERSTE (base and size) owned by driver (i.e. full filled with pending events, and not pointed
 *   to by ERDP).
 * So don't cache ERSTSZ and ERSTE. Stop secondary event ring when updating ERSTBA and start it
 * if HC is running.
 */

static inline struct erst *map_erst(struct xhci_ring *ering)
{
	struct event_ring_priv *priv = xhci_ring_priv(ering);
	struct pci_xhci *xhci = ering->xhci;
	struct vmctx *vmctx = xhci->pdev->vmctx;
	uint64_t table_base = priv->intr.erstba;
	uint32_t max_segs = 1 << xhci->caps.hcsparams2.erst_max;
	uint32_t max_table_size = ALIGN_UP(max_segs * sizeof(struct erst), XHCI_PAGE_SIZE_4K);
	if (!table_base || !priv->intr.erstsz) {
		pr_err("Event Ring Segment Table invalid!\n");
		return NULL;
	}

	/* map all possible segments, because ERSTSZ can change at runtime */
	return dm_vm_map_gpa(vmctx, table_base, max_table_size);
}

static inline void map_erste(struct vmctx *vmctx, struct seg *seg, struct event_ring_state *state,
		uint32_t index)
{
	seg->index = index;
	seg->base = state->erst_base[index].seg_base << XHCI_ERST_SEG_BASE_SHIFT;
	seg->trbs = state->erst_base[index].seg_size;
	if (!seg->base || !seg->trbs) {
		pr_err("event ring encounters an invalid segment!\n");
		// TODO trigger a xHC system error ?
	}
	seg->base_trb = dm_vm_map_gpa(vmctx, seg->base, seg->trbs * sizeof(union xhci_trb));
}

/*
 * Advance enqueue pointer and return whether event ring is going to be full (only 1 TRB left
 * which is for full event). Refer to Spec Chapter 4.9.4 Figure 4-12: Event Ring State Machine.
 */
static bool event_ring_enq_advance(struct xhci_ring *ering)
{
	struct event_ring_priv *priv = xhci_ring_priv(ering);
	struct event_ring_state *state = &priv->state;
	struct interrupter *intr = &priv->intr;

	state->enq_trb++;
	state->enq_trb_gpa += sizeof(union xhci_trb);
	state->left_trbs--;

	if (!state->left_trbs) {
		state->cur = state->next;
		if (state->cur.index == 0)
			state->pcs = !state->pcs;
		state->enq_trb = state->cur.base_trb;
		state->enq_trb_gpa = state->cur.base;
		state->left_trbs = state->cur.trbs;
		pr_dbg("event ring jump to segment %d, base %lx, trbs %d\n",
				state->cur.index, state->cur.base, state->cur.trbs);
		/* Don't prefetch next ERSTE here. Driver may change it before moving ERDP there */
	}

	uint64_t erdp = intr->erdp & XHCI_RTS_INT_ERDP_MASK;
	if (state->left_trbs > 1) {
		return state->enq_trb_gpa + sizeof(union xhci_trb) == erdp;
	} else {
		/* Prefetch next ERSTE to check full condition. */
		uint32_t next_index = state->next.index + 1;
		/* Use >= here because event ring segments may shrink (ERSTSZ decreased) */
		if (next_index >= intr->erstsz)
			next_index = 0;
		struct vmctx *vmctx = ering->xhci->pdev->vmctx;
		map_erste(vmctx, &state->next, state, next_index);

		return state->next.base == erdp;
	}
}

static bool xhci_event_ring_handle_trb(struct xhci_ring *ering)
{
	struct event_ring_priv *priv = xhci_ring_priv(ering);
	struct pci_xhci *xhci = ering->xhci;
	struct usb_hcd *hcd = usb_get_hcd_from_priv(xhci);
	volatile struct event_ring_state *state = &priv->state;
	bool gen_int = false;
	bool progress = false;

	pthread_mutex_lock(&priv->incoming_lock);
	STAILQ_HEAD(evt_list, event_list) temp;
	temp.stqh_first = priv->incoming.stqh_first;
	temp.stqh_last = priv->incoming.stqh_last;
	STAILQ_INIT(&priv->incoming);
	pthread_mutex_unlock(&priv->incoming_lock);

	xhci_ring_lock(ering);
	while (!STAILQ_EMPTY(&temp) && !state->is_full) {
		struct event_list *e = STAILQ_FIRST(&temp);
		STAILQ_REMOVE_HEAD(&temp, list);

		e->trb.event_xfer.c = state->pcs;
		*state->enq_trb = e->trb;
		progress = true;
		xhci_dump_trb(ering, state->enq_trb, state->enq_trb_gpa, "raise");
		if (event_ring_enq_advance(ering)) {
			pr_warn("event ring is going to be full! Add a event ring full event!\n");
			state->is_full = 1;
			/* TODO
			 * insert temp list back to incoming list
			 * stop command ring and transfer rings.
			 * Don't halt HC.
			 */

			union xhci_trb evt_ering_full;
			memset(&evt_ering_full, 0, sizeof(union xhci_trb));
			evt_ering_full.event_hc.type = TRB_TYPE_EVENT_HC;
			evt_ering_full.event_hc.cc = COMPLETION_CODE_EVENT_RING_FULL;
			evt_ering_full.event_hc.c = state->pcs;
			*state->enq_trb = evt_ering_full;
			xhci_dump_trb(ering, state->enq_trb, state->enq_trb_gpa, "raise");

			event_ring_enq_advance(ering);
		}
		gen_int = gen_int || e->gen_int || state->is_full;
		free(e);
	}

	/* TODO interrupt modulation */

	if (gen_int) {
		atomic_fetch_or(&priv->intr.iman, XHCI_RTS_INT_IMAN_IP);
		atomic_fetch_or(&priv->intr.erdp, XHCI_RTS_INT_ERDP_EHB);
	}
	xhci_ring_unlock(ering);

	if (gen_int) {
		usb_hcd_lock(hcd);	// necessary?
		atomic_fetch_or(&xhci->usbsts, XHCI_OP_USBSTS_EINT);
		if ((priv->intr.iman & XHCI_RTS_INT_IMAN_IE) &&
				(xhci->usbcmd & XHCI_OP_USBCMD_INTE)) {
			usb_hcd_unlock(hcd);
			if (pci_msi_enabled(xhci->pdev))
				pci_generate_msi(xhci->pdev, 0);
			else
				pci_lintr_assert(xhci->pdev);
		} else {
			usb_hcd_unlock(hcd);
		}
	}

	return progress;
}

static int xhci_event_ring_start(struct xhci_ring *ering)
{
	struct event_ring_priv *priv = xhci_ring_priv(ering);
	struct pci_xhci *xhci = ering->xhci;
	struct vmctx *vmctx = xhci->pdev->vmctx;
	struct event_ring_state *state = &priv->state;

	struct erst *erst = map_erst(ering);
	if (!erst) {
		return -1;
	}

	/* look up the ERSTE containing ERDP */
	int seg = 0;
	bool found = false;
	uint64_t erdp = priv->intr.erdp & XHCI_RTS_INT_ERDP_MASK;	// ERDP as EREP
	for (int i = 0; i < priv->intr.erstsz; i++) {
		struct erst *erste = &erst[i];
		struct range erdpr = { erdp, sizeof(union xhci_trb) };
		struct range erster = { erste->seg_base << XHCI_ERST_SEG_BASE_SHIFT,
			erste->seg_size * sizeof(union xhci_trb) };
		if (range_in(erdpr, erster)) {
			seg = i;
			found = true;
			pr_dbg("found valid erdp %x as enqueue pointer\n", erdp);
			break;
		}
	}
	state->erst_base = erst;
	map_erste(vmctx, &state->cur, state, seg);

	state->enq_trb_gpa = found ? erdp : state->cur.base;
	uint32_t trb_off = (state->enq_trb_gpa - state->cur.base) / sizeof(union xhci_trb);
	state->enq_trb = state->cur.base_trb + trb_off;
	state->left_trbs = state->cur.trbs - trb_off;

	if (state->left_trbs == 1) {
		/* Prefetch next ERSTE */
		uint32_t next_index = seg + 1;
		/* Use >= here because event ring segments may shrink (ERSTSZ decreased) */
		if (next_index >= priv->intr.erstsz)
			next_index = 0;
		map_erste(vmctx, &state->next, state, next_index);
	}

	/* PCS may have been imported by CRS operation. */

	return 0;
}

static void xhci_event_ring_stop(struct xhci_ring *ering)
{
	// unmap current and next ERSTE, and ERST
}

static int xhci_event_ring_init(struct xhci_ring *ering, void *data)
{
	struct event_ring_priv *priv = xhci_ring_priv(ering);
	STAILQ_INIT(&priv->incoming);
	pthread_mutex_init(&priv->incoming_lock, NULL);
	return 0;
}

static void xhci_event_ring_reset(struct xhci_ring *ering)
{
	struct event_ring_priv *priv = xhci_ring_priv(ering);

	memset(&priv->intr, 0, sizeof(struct interrupter));
	memset(&priv->state, 0, sizeof(struct event_ring_state));
	memset(&priv->saved, 0, sizeof(struct event_ring_state));

	/* TBD Should these internal state retain across start/stop ?
	 * If so, just initialize it once here. Otherwise, also do it in xhci_event_ring_stop().
	 * Because after suspend and before restart event ring, driver could import PCS by CRS
	 * operation, we can't initialize these two internal variable in xhci_event_ring_start().
	 */
	priv->state.pcs = 1;
	priv->state.is_full = 0;

	/* drop stale events */
	pthread_mutex_lock(&priv->incoming_lock);
	while (!STAILQ_EMPTY(&priv->incoming)) {
		struct event_list *e = STAILQ_FIRST(&priv->incoming);
		STAILQ_REMOVE_HEAD(&priv->incoming, list);
		free(e);
	}
	pthread_mutex_unlock(&priv->incoming_lock);
}

static struct xhci_ring_ops xhci_event_ring_ops = {
	.priv_size = sizeof(struct event_ring_priv),
	.init = xhci_event_ring_init,
	.start = xhci_event_ring_start,
	.stop = xhci_event_ring_stop,
	.reset = xhci_event_ring_reset,
	.handle_trb = xhci_event_ring_handle_trb,
};

static void xhci_enqueue_event(struct pci_xhci *xhci, uint32_t intr, union xhci_trb *trb, bool gen_int)
{
	struct xhci_ring *ering = xhci->event_primary;
	struct event_ring_priv *priv = xhci_ring_priv(ering);
	struct event_list *e = calloc(1, sizeof(struct event_list));
	e->trb = *trb;
	xhci_dump_trb(ering, &e->trb, 0, "enqueue");
	e->gen_int = gen_int;
	pthread_mutex_lock(&priv->incoming_lock);
	STAILQ_INSERT_TAIL(&priv->incoming, e, list);
	pthread_mutex_unlock(&priv->incoming_lock);
	xhci_ring_notify(ering);
}

/*
 * This is required because internal state like PCS can't be restored from registers. And the
 * register options for resume and initialization are the same.
 */
void ering_save_state(struct xhci_ring *ering)
{
	struct event_ring_priv *priv = xhci_ring_priv(ering);
	priv->saved = priv->state;
}

void ering_restore_state(struct xhci_ring *ering)
{
	struct event_ring_priv *priv = xhci_ring_priv(ering);
	priv->state = priv->saved;
}

void ering_regs_write(struct xhci_ring *ering, uint32_t offset, int width, uint64_t value)
{
	struct event_ring_priv *priv = xhci_ring_priv(ering);
	struct interrupter *intr = &priv->intr;
	struct pci_xhci *xhci = ering->xhci;

	pr_dbg("write%c intr%d reg %x: %x\n", width2char(width), priv->intr_id, offset, value);

	switch (offset) {
	case XHCI_RTS_INT_IMAN:
	{
		unsigned old, new;
		do {
			new = old = intr->iman;
			if (value & XHCI_RTS_INT_IMAN_IP)
				new &= ~XHCI_RTS_INT_IMAN_IP;
			if (value & XHCI_RTS_INT_IMAN_IE)
				new |= XHCI_RTS_INT_IMAN_IE;
		} while (!atomic_compare_exchange_strong(&intr->iman, &old, new));
		break;
	}
	case XHCI_RTS_INT_IMOD:
		intr->imod = value;
		break;
	case XHCI_RTS_INT_ERSTSZ:
	{
		/* enlarge/shrink event ring when ring is running */
		uint32_t size = value & XHCI_RTS_INT_ERSTSZ_SIZE_MASK;
		if (size >= (1 << xhci->caps.hcsparams2.erst_max)) {
			pr_err("ERST size is too big!\n");
			// cap it. TBD report error?
			size = 1 << xhci->caps.hcsparams2.erst_max;
		}
		intr->erstsz = size;

		/* write 0 to disable an event ring */
		if (!size) {
			if (ering == xhci->event_primary) {
				/* primary event ring can't be disabled */
				// The action is undefined. TBD throw system error?
				pr_err("guest is trying to disable primary event ring! Ignored.\n");
			} else {
				xhci_ring_stop(ering, true);
				return;
			}
		}
		break;
	}
	case XHCI_RTS_INT_ERSTBA:
	{
		if ((priv->intr_id == 0) && (xhci->usbcmd & XHCI_OP_USBCMD_RS)) {
			pr_warn("can't change ERSTBA of primary event ring when xHC is running\n");
			break;
		}
		intr->erstba = value;

		xhci_ring_stop(ering, true);
		if (xhci->usbcmd & XHCI_OP_USBCMD_RS)
			xhci_ring_start(ering);
		break;
	}
	case XHCI_RTS_INT_ERSTBA + 4:
		break;
	case XHCI_RTS_INT_ERDP:
	{
		intr->erdp = value & ~XHCI_RTS_INT_ERDP_EHB;

		if (!ering->running)
			break;

		if (atomic_exchange(&priv->state.is_full, 0))
			xhci_ring_notify(ering);
		break;
	}
	case XHCI_RTS_INT_ERDP + 4:
		break;
	default:
		break;
	}
}

/* --------------------- handle transfers ------------------- */

/* called when enabling transfer ring or cross a new segment */
static void transfer_ring_map_next_seg(struct xhci_ring *xring)
{
	struct transfer_ring_priv *priv = xhci_ring_priv(xring);
	struct vmctx *vmctx = priv->xhci->pdev->vmctx;
	uint32_t map_len = XHCI_PAGE_SIZE_4K - (priv->deq_ptr_gpa % XHCI_PAGE_SIZE_4K);
	/* unmap xring->last_map_base */
	priv->last_map_base = dm_vm_map_gpa(vmctx, priv->deq_ptr_gpa, map_len);
	priv->deq_trb = priv->last_map_base;
}

static union xhci_trb *xhci_get_next_trb(struct pci_xhci *xhci, union xhci_trb *trb,
							uint64_t *trb_gpa)
{
	if (trb->common.type == TRB_TYPE_LINK) {
		*trb_gpa = trb->link_trb.ring_segment;
		struct vmctx *vmctx = xhci->pdev->vmctx;
		return dm_vm_map_gpa(vmctx, *trb_gpa, 8);
	} else {
		*trb_gpa += sizeof(union xhci_trb);
		return trb + 1;
	}
}

//static void xhci_ep_halt(struct pci_xhci *xhci, struct devctx_desc *dev_desc, uint8_t ep_dci);

/* TRB walk state for one transfer */
struct walk_state {
	uint64_t len;
	uint8_t slot;
	uint8_t ep_dci;
	struct pci_xhci *xhci;
	bool jump_to_next_td;
	bool stopping;
	enum usb_transfer_result result;
	enum completion_code default_cc;
	//enum completion_code event_data;
	struct usb_transfer *xfer;
	struct transfer_ring_priv *ring_priv;
	union xhci_trb *prev_trb;	// Event Data TRB need look back to determine cc.
	struct ep_ctx *ep_ctx;
};

static bool resolve_one_ctrl_trb(union xhci_trb *trb, uint64_t trb_gpa, struct walk_state *state)
{
	union xhci_trb evt_xfer;

	switch (trb->common.type) {
	case TRB_TYPE_EVENT_DATA:
		pr_err("skipped resolving Event Data TRB for control transfer!\n");
		break;
	case TRB_TYPE_SETUP_STAGE:
		state->len -= trb->xfer_ctrl_setup.len;
		if (trb->xfer_ctrl_setup.ioc) {
			evt_xfer = make_xfer_event(trb_gpa, 0, state->slot, 
					state->ep_dci, 0, COMPLETION_CODE_SUCCESS);
			// don't generate interrupt even through we should according to spec.
			// There must be a following status TRB to generate interrupt.
			xhci_enqueue_event(state->xhci, 0, &evt_xfer, false);
		}
		break;
	case TRB_TYPE_NORMAL:
	case TRB_TYPE_DATA_STAGE:
	{
		if (state->jump_to_next_td)	// still need to walk link/status_stage TRBs
			break;

		/* len/isp/ioc locations are the same for normal/ctrl_data/isoc TRBs */
		bool sp = state->len < trb->xfer_normal.len;
		uint32_t trb_residual = sp ? (trb->xfer_normal.len - state->len) : 0;
		state->len = sp ? 0 : (state->len - trb->xfer_normal.len);
		if (trb->xfer_normal.ioc ||
			(sp && (trb->xfer_normal.isp || state->result != USB_XFER_RES_OK))) {
			//bool gen_int = (state->result != USB_XFER_RES_OK) || !trb->xfer_normal.bei;
			enum completion_code cc;
			if (trb_residual == 0)
				cc = COMPLETION_CODE_SUCCESS;
			else if (state->result == USB_XFER_RES_OK)
				cc = COMPLETION_CODE_SHORT_PACKET;
			else {
				cc = COMPLETION_CODE_TRANSACTION_ERROR;
				state->jump_to_next_td = true;
			}
			evt_xfer = make_xfer_event(trb_gpa, trb_residual, state->slot,
					state->ep_dci, 0, cc);
			xhci_enqueue_event(state->xhci, 0, &evt_xfer, false);
		}
		break;
	}
	case TRB_TYPE_STATUS_STAGE:
	{
		enum completion_code cc = COMPLETION_CODE_SUCCESS;
		if (state->result == USB_XFER_RES_STALL) {
			cc = COMPLETION_CODE_STALL_ERROR;
			state->ep_ctx->state = XHCI_EP_STATE_HALTED;
			state->ep_ctx->tr_deq_ptr = trb_gpa >> XHCI_TR_DEQ_PTR_SHIFT;
			state->ep_ctx->dcs = trb->common.c;
		} else if (state->result != USB_XFER_RES_OK) {
			cc = COMPLETION_CODE_TRANSACTION_ERROR;
		}
		evt_xfer = make_xfer_event(trb_gpa, 0, state->slot,
				state->ep_dci, 0, cc);
		xhci_enqueue_event(state->xhci, 0, &evt_xfer, true);
		break;
	}
	default:
		pr_warn("Control transfer done, impossible TRB type: %d?!\n",
							trb->common.type);
	}

	return true;
}

static bool resolve_one_data_trb(union xhci_trb *trb, uint64_t trb_gpa, struct walk_state *state)
{
	union xhci_trb evt_xfer;

	switch (trb->common.type) {
	case TRB_TYPE_EVENT_DATA:
	{
		/* Event Data TRB can follow a ctrl-data/normal/isoc TRB. This event
		 * will inherit completion code of the transfer?
		 */
		if (trb->event_data_trb.ioc) {
			//TBD
			evt_xfer = make_xfer_event(trb->event_data_trb.event_data,
					state->xfer->actual_len, state->slot,
					state->ep_dci, 1, state->default_cc);
			xhci_enqueue_event(state->xhci, 0, &evt_xfer,
					!trb->event_data_trb.bei);
		}
		break;
	}
	case TRB_TYPE_NORMAL:
	case TRB_TYPE_ISOCH:
	{
		/* len/isp/ioc locations are the same for normal/ctrl_data/isoc TRBs */
		bool sp = state->len < trb->xfer_normal.len;
		uint32_t trb_residual = sp ? (trb->xfer_normal.len - state->len) : 0;
		state->len = sp ? 0 : (state->len - trb->xfer_normal.len);
		if (!sp && trb->xfer_normal.ioc) {
			evt_xfer = make_xfer_event(trb_gpa, 0, state->slot,
					state->ep_dci, 0, COMPLETION_CODE_SUCCESS);
			xhci_enqueue_event(state->xhci, 0, &evt_xfer,
					!trb->event_data_trb.bei);
			break;
		}
		if (sp) {
			if (state->result != USB_XFER_RES_OK || state->stopping) {
				enum completion_code cc =COMPLETION_CODE_SUCCESS;
				switch (state->result) {
				case USB_XFER_RES_NODEV:
				case USB_XFER_RES_ERR:
					cc = COMPLETION_CODE_TRANSACTION_ERROR;
					break;
				case USB_XFER_RES_STALL:
					cc = COMPLETION_CODE_STALL_ERROR;
					// this blocks doorbell until reset_endpoint command
					state->ep_ctx->state = XHCI_EP_STATE_HALTED;
					break;
				case USB_XFER_RES_OK:
					// short packet result is OK by design
					state->ring_priv->stopped_trb = trb;
					cc = COMPLETION_CODE_STOPPED_SHORT_PACKET;
					break;
				}
				if (state->ring_priv->stream_desc) {
					assert(state->ring_priv->stream > 0);
					struct stream_ctx *stream_ctx = state->ring_priv->stream_desc->stream_ctx;
					stream_ctx->tr_deq_ptr = trb_gpa >> XHCI_TR_DEQ_PTR_SHIFT;
					stream_ctx->dcs = trb->common.c;
					//stream_ctx->stopped_edtla = ? // if sec=1
					// TBD how to stall other streams ?
				} else {
					state->ep_ctx->tr_deq_ptr = trb_gpa >> XHCI_TR_DEQ_PTR_SHIFT;
					state->ep_ctx->dcs = trb->common.c;
				}
				evt_xfer = make_xfer_event(trb_gpa, trb_residual,
						state->slot, state->ep_dci, 0, cc);
				xhci_enqueue_event(state->xhci, 0, &evt_xfer, true);

				return false;
			}

			/* successful short transfer */
			if (trb->xfer_normal.isp || trb->xfer_normal.ioc) {
				evt_xfer = make_xfer_event(trb_gpa, trb_residual,
						state->slot, state->ep_dci, 0,
						COMPLETION_CODE_SHORT_PACKET);
				xhci_enqueue_event(state->xhci, 0, &evt_xfer,
						!trb->xfer_normal.bei);
				// TBD skip left TRBs if ISP met ?
				return false;
			}
		}
		break;
	}
	default:
		pr_warn("Transfer done for an impossible TRB type: %d?!\n", trb->common.type);
	}

	return true;
}

static void xhci_xfer_done(struct usb_transfer *xfer)
{
	struct xhci_usb_transfer_priv *xfer_priv = xfer->hc_priv;
	struct xhci_ring *xring = xfer_priv->xring;
	struct transfer_ring_priv *ring_priv = xhci_ring_priv(xring);
	struct epctx_desc *ep_desc = ring_priv->epctx_desc;
	//struct devctx_desc *dev_desc = ep_desc->devctx_desc;
	struct pci_xhci *xhci = ring_priv->xhci;

	pthread_mutex_lock(&ring_priv->outstanding_lock);
	STAILQ_REMOVE(&ring_priv->outstanding, xfer_priv, xhci_usb_transfer_priv, list);
	pthread_mutex_unlock(&ring_priv->outstanding_lock);

	uint8_t slot = ring_priv->slot;
	uint8_t ep_dci = ring_priv->ep_dci;
	uint16_t stream = ring_priv->stream;
	union xhci_trb evt_xfer;

	if (xring->destroying || ep_desc->halting)
		goto free_xfer;

	pr_dbg("xfer complete [%d-%d-%d] start_trb: %lx, last_trb: %lx\n",
			slot, ep_dci, stream, xfer_priv->first_deq_ptr, xfer_priv->last_deq_ptr);

	/* walk and resolve TRBs */
	union xhci_trb *trb = NULL;
	uint64_t trb_gpa = xfer_priv->first_deq_ptr;
	struct walk_state state;
	state.len = xfer->actual_len;
	state.slot = slot;
	state.ep_dci = ep_dci;
	state.xhci = xhci;
	state.jump_to_next_td = false;
	state.stopping = xring->stopping;
	state.result = xfer->result;
	state.xfer = xfer;
	state.ring_priv = ring_priv;
	state.ep_ctx = ep_desc->epctx;
	state.default_cc = COMPLETION_CODE_SUCCESS;
	if (state.result != USB_XFER_RES_OK)
		state.default_cc = COMPLETION_CODE_TRANSACTION_ERROR;
	else if (xfer->actual_len < usb_transfer_total_len(xfer))
		state.default_cc = COMPLETION_CODE_SHORT_PACKET;

	do {
		state.prev_trb = trb;
		trb = trb ? xhci_get_next_trb(xhci, trb, &trb_gpa) : xfer_priv->first_trb;

		xhci_dump_trb(xring, trb, trb_gpa, "resolve");

		if (trb->common.type == TRB_TYPE_LINK) {
			if (trb->link_trb.ioc) {
				pr_dbg("IOC on link TRB\n");
				evt_xfer = make_xfer_event(trb_gpa, 0, slot,
						ep_dci, 0, COMPLETION_CODE_SUCCESS);
				xhci_enqueue_event(xhci, 0, &evt_xfer, true);
			}
			continue;
		}

		bool cont;
		if (xfer->type == USB_XFER_TYPE_CTRL) {
			cont = resolve_one_ctrl_trb(trb, trb_gpa, &state);
		} else
			cont = resolve_one_data_trb(trb, trb_gpa, &state);
		if(!cont)
			break;
	} while (trb != xfer_priv->last_trb);

#if 0
	if (xfer->result == USB_XFER_RES_STALL)
		xhci_ep_halt(xhci, dev_desc, ep_dci);
#endif

free_xfer:
	usb_transfer_free(xfer);
	xhci_ring_put(xring);
}

static bool handle_one_xfer_trb(struct xhci_ring *xring, union xhci_trb *trb, uint64_t trb_gpa)
{
	struct transfer_ring_priv *ring_priv = xhci_ring_priv(xring);
	struct pci_xhci *xhci = ring_priv->xhci;
	struct usb_hcd *hcd = usb_get_hcd_from_priv(xhci);
	struct vmctx *vmctx = xhci->pdev->vmctx;
	//union xhci_trb *trb = ring_priv->deq_trb;
	bool progress = false;

	uint8_t slot = ring_priv->slot;
	uint8_t ep_dci = ring_priv->ep_dci;

	struct devctx_desc *dev_desc = &xhci->ctx_descs[slot];
	struct ep_ctx *epctx = &dev_desc->ctxs->ctxs[ep_dci].ep_ctx;
	struct usb_device *dev = dev_desc->device;
	enum usb_transfer_type xfer_type = ep_type_to_xfer_type(epctx->type);

	struct xhci_usb_transfer_priv *xfer_priv;
	if (!ring_priv->cur_xfer) {
		ring_priv->cur_xfer = usb_transfer_alloc(hcd, dev, xfer_type, xhci_ep_dci_to_epid(ep_dci),
				ring_priv->stream, xhci_xfer_done);
		if (!ring_priv->cur_xfer) {
			pr_err("no memory for USB transfer!\n");
			//TODO stall the transfer
			return false;
		}
		ring_priv->cur_xfer->mps = ring_priv->mps;
		xfer_priv = ring_priv->cur_xfer->hc_priv;
		xfer_priv->xring = xhci_ring_get(xring);
		xfer_priv->xfer = ring_priv->cur_xfer;
		xfer_priv->start_rcs = ring_priv->rcs;
		xfer_priv->first_deq_ptr = trb_gpa;
		xfer_priv->first_trb = trb;
		xfer_priv->last_deq_ptr = 0;
		xfer_priv->last_trb = NULL;
		xfer_priv->canceled = false;
		xfer_priv->skip_td = false;
		xfer_priv->target_frame = 0;
	} else {
		xfer_priv = ring_priv->cur_xfer->hc_priv;
	}

	switch (trb->common.type) {
	case TRB_TYPE_NORMAL:
	{
		uint64_t gpa = trb->xfer_normal.buffer;
		uint32_t len = trb->xfer_normal.len;
		void *buf = dm_vm_map_gpa(vmctx, gpa, len);

		if (xfer_type == USB_XFER_TYPE_CTRL) {
			if (ring_priv->next_ctrl_stage != CTRL_XFER_STAGE_DATA_NORMAL)
				goto gen_err_event;
			usb_transfer_add_one_buf(ring_priv->cur_xfer, buf, len);
			if (trb->xfer_normal.ch)
				ring_priv->next_ctrl_stage = CTRL_XFER_STAGE_DATA_NORMAL;
			else
				ring_priv->next_ctrl_stage = CTRL_XFER_STAGE_STATUS;
			/* send transfer in status stage TD */
			break;
		}

		usb_transfer_add_one_buf(ring_priv->cur_xfer, buf, len);
		if (!trb->xfer_normal.ch) {	/* last TRB of a TD */
			xfer_priv->last_trb = trb;
			xfer_priv->last_deq_ptr = trb_gpa;
			pthread_mutex_lock(&ring_priv->outstanding_lock);
			STAILQ_INSERT_TAIL(&ring_priv->outstanding, xfer_priv, list);
			pthread_mutex_unlock(&ring_priv->outstanding_lock);
			dev->backend->transfer(dev, ring_priv->cur_xfer);

			ring_priv->cur_xfer = NULL;
			progress = true;
		}
		break;
	}
	case TRB_TYPE_SETUP_STAGE:
	{
		if ((xfer_type != USB_XFER_TYPE_CTRL) ||
				(ring_priv->next_ctrl_stage != CTRL_XFER_STAGE_SETUP) ||
				!trb->xfer_ctrl_setup.idt ||
				(trb->xfer_ctrl_setup.len != 8)) {
			goto gen_err_event;
		}
		/* control setup packet is part of a control transfer, though it's a TD */
		usb_transfer_add_one_buf(ring_priv->cur_xfer, trb, 8);
		if (trb->xfer_ctrl_setup.wLength) // or check trt ?
			ring_priv->next_ctrl_stage = CTRL_XFER_STAGE_DATA;
		else
			ring_priv->next_ctrl_stage = CTRL_XFER_STAGE_STATUS;
		break;
	}
	case TRB_TYPE_DATA_STAGE:
	{
		if ((xfer_type != USB_XFER_TYPE_CTRL) ||
				(ring_priv->next_ctrl_stage != CTRL_XFER_STAGE_DATA))
			goto gen_err_event; // reset priv->cur_ctrl_stage?

		/* TODO filter out set_address command and return error_trb code */
		void *buf;
		uint32_t len = trb->xfer_ctrl_data.len;
		if (trb->xfer_ctrl_data.idt) {
			buf = trb;
		} else {
			uint64_t gpa = trb->xfer_ctrl_data.buffer;
			buf = dm_vm_map_gpa(vmctx, gpa, len);
		}
		usb_transfer_add_one_buf(ring_priv->cur_xfer, buf, len);
		if (trb->xfer_ctrl_data.ch)
			ring_priv->next_ctrl_stage = CTRL_XFER_STAGE_DATA_NORMAL;
		else
			ring_priv->next_ctrl_stage = CTRL_XFER_STAGE_STATUS;
		break;
	}
	case TRB_TYPE_STATUS_STAGE:
	{
		if ((xfer_type != USB_XFER_TYPE_CTRL) ||
				(ring_priv->next_ctrl_stage != CTRL_XFER_STAGE_STATUS))
			goto gen_err_event; // need reset priv->cur_ctrl_stage?
		xfer_priv->last_trb = trb;
		xfer_priv->last_deq_ptr = trb_gpa;
		pthread_mutex_lock(&ring_priv->outstanding_lock);
		STAILQ_INSERT_TAIL(&ring_priv->outstanding, xfer_priv, list);
		pthread_mutex_unlock(&ring_priv->outstanding_lock);
		dev->backend->transfer(dev, ring_priv->cur_xfer);

		ring_priv->cur_xfer = NULL;
		progress = true;
		ring_priv->next_ctrl_stage = CTRL_XFER_STAGE_SETUP;
		break;
	}
	case TRB_TYPE_ISOCH:
	{
		uint64_t gpa = trb->xfer_normal.buffer;
		uint32_t len = trb->xfer_normal.len;
		void *buf = dm_vm_map_gpa(vmctx, gpa, len);

		if (xfer_type != USB_XFER_TYPE_ISOC)
			goto gen_err_event;
		/* must be first TRB of current transfer in composition */
		if (trb != xfer_priv->first_trb)
			pr_warn("ISOC TRB is not the first of a TD!\n");

		if (!trb->xfer_isoc.asap) {
			xfer_priv->target_frame = trb->xfer_isoc.frame_id;
		}

		usb_transfer_add_one_buf(ring_priv->cur_xfer, buf, len);
		if (!trb->xfer_normal.ch) {	/* last TRB of a TD */
			xfer_priv->last_trb = trb;
			xfer_priv->last_deq_ptr = trb_gpa;
			// TODO check target_frame here and in normal TRB. Send
			// COMPLETION_CODE_MISSED_SERVICE if xfer missed.
			pthread_mutex_lock(&ring_priv->outstanding_lock);
			STAILQ_INSERT_TAIL(&ring_priv->outstanding, xfer_priv, list);
			pthread_mutex_unlock(&ring_priv->outstanding_lock);
			dev->backend->transfer(dev, ring_priv->cur_xfer);

			ring_priv->cur_xfer = NULL;
			progress = true;
		}
		break;
	}
	case TRB_TYPE_EVENT_DATA:
		if (trb->event_data_trb.ioc) {
			/* force a transfer for the event data even we haven't prefetch a whole
			 * TD. */
			if (trb->event_data_trb.ch) {
				pr_info("met an intermediate Event Data TRB!\n");
			}
			xfer_priv->last_trb = trb;
			xfer_priv->last_deq_ptr = trb_gpa;
			pthread_mutex_lock(&ring_priv->outstanding_lock);
			STAILQ_INSERT_TAIL(&ring_priv->outstanding, xfer_priv, list);
			pthread_mutex_unlock(&ring_priv->outstanding_lock);
			dev->backend->transfer(dev, ring_priv->cur_xfer);
			progress = true;

			ring_priv->cur_xfer = NULL;
		}
		break;
	default:
gen_err_event:
	{
		// TBD what if we are composting a transfer ?
		pr_warn("invalid TRB ?: %08x %08x %08x %08x\n",
				trb->dword0, trb->dword1, trb->dword2, trb->dword3);
		union xhci_trb evt_xfer = make_xfer_event(trb_gpa,
				0, slot, ep_dci, 0, COMPLETION_CODE_TRB_ERROR);
		xhci_enqueue_event(xhci, 0, &evt_xfer, true);
		break;
	}
	}
	return progress;
}

/* 
 * For better performance and easiness to save ring state on stop endpoint command, this handler
 * prefetches TRBs until end of TD or an intermediate Event Data TRB. TRBs prefetched constructs
 * a transfer passed to USB device backend.
 *
 * On transfer completion, walk TRBs list and complete them. Possible situations:
 * - Transfer succeeded.
 *   For each TRBs with IOC set (including link TRB), generate a transfer event with successful
 *   completion code, the TRB pointer should match the one with IOC bit.
 * - Transfer succeeded with short packet.
 *   Find the TRB which is not fully transferred. If ISP set, generate a transfer event with short
 *   packet completion code and advances to the last TRB. If IOC set, generate a transfer event.
 * Update current rcs, 
 *
 * Actually it's easier if prefetching a whole TD for a transfer. I don't choose this because:
 * - A TD may span the whole transfer ring? I'm not sure. If this case is allowed, we can never
 *   prefetch a TD if we don't consume existing TRBs, and the handler will hang.
 * - For big TD with data length > 16MB, driver may insert intermediate Event Data TRBs with IOC,
 *   to get notified when the transfer length reaches the bounds. Driver may assume HC will
 *   schedule the physical transfer when consume an Event Data TRB and thus it may fill the whole
 *   TD slowly. In this case, we shouldn't wait driver to fill a whole TD because it may incur a
 *   big latency for the packets at the front of the TD.
 */
static bool transfer_ring_handle_trbs(struct xhci_ring *xring)
{
	volatile struct transfer_ring_priv *ring_priv = xhci_ring_priv(xring);
	bool progress = false;

	while (ring_priv->deq_trb->common.c == ring_priv->rcs && (!xring->stopping ||
		(ring_priv->deq_trb->common.type == TRB_TYPE_EVENT_DATA) /* consume it before stop! */))
	{
		uint64_t trb_gpa = ring_priv->deq_ptr_gpa;
		union xhci_trb *trb = ring_priv->deq_trb;

		xhci_dump_trb(xring, trb, trb_gpa, "prefetch");

		/* A Link TRB can't be the last one in a TD (refer to $4.11.5). So we can handle it
		 * here without interfering state of the current transfer */
		if (ring_priv->deq_trb->common.type == TRB_TYPE_LINK) {
			if (trb->link_trb.tc)
				ring_priv->rcs = !ring_priv->rcs;
			ring_priv->deq_ptr_gpa = trb->link_trb.ring_segment;
			transfer_ring_map_next_seg(xring); /* deq_trb updated */
			continue;
		} else if (ring_priv->deq_trb->common.type == TRB_TYPE_NOOP_XFER) {
			// TBD can NOOP be chained into a TD?
			//if (trb->xfer_noop.ioc) {
			//	union xhci_trb evt_xfer = make_xfer_event(trb_gpa,
			//		0, slot, ep_dci, 0, COMPLETION_CODE_SUCCESS);
			//	xhci_enqueue_event(xhci, 0, &evt_xfer, true);
			//	progress = true;
			//}
		} else {
			progress |= handle_one_xfer_trb(xring, trb, trb_gpa);
		}

		ring_priv->deq_trb++;
		ring_priv->deq_ptr_gpa += sizeof(union xhci_trb);
	}
	return progress;
}

static int transfer_ring_start(struct xhci_ring *cring)
{
	transfer_ring_map_next_seg(cring);
	return 0;
}

// ring_priv->cur_xfer == NULL doesn't mean in TD boundary. Since Event Data TRB could split a TD
// into multiple transfers
// Assume no intermediate Event Data TRB in TD
static void transfer_ring_stop(struct xhci_ring *xring)
{
	struct transfer_ring_priv *ring_priv = xhci_ring_priv(xring);
	struct epctx_desc *ep_desc = ring_priv->epctx_desc;
	struct devctx_desc *dev_desc = ep_desc->devctx_desc;
	struct pci_xhci *xhci = ring_priv->xhci;
	struct usb_device *dev = dev_desc->device;
	struct xhci_usb_transfer_priv *xfer_priv;

	if (ring_priv->cur_xfer) {
		/* discard prefetched incomplete transfer */
		xfer_priv = ring_priv->cur_xfer->hc_priv;
		usb_transfer_free(ring_priv->cur_xfer);
		ring_priv->cur_xfer = NULL;
		ring_priv->rcs = xfer_priv->start_rcs;
		ring_priv->deq_ptr_gpa = xfer_priv->first_deq_ptr;
		ring_priv->deq_trb = xfer_priv->first_trb;
		ring_priv->next_ctrl_stage = CTRL_XFER_STAGE_SETUP;
	}

	WRITE_ONCE(&ring_priv->stopped_trb, NULL);

	pthread_mutex_lock(&ring_priv->outstanding_lock);
	STAILQ_FOREACH(xfer_priv, &ring_priv->outstanding, list) {
		struct usb_transfer *xfer = xfer_priv->xfer;
		/* mark xfer canceled so that it will be resolved in xfer_done callback. */
		xfer_priv->canceled = true;
		pr_dbg("cancel xfer [%d-%d-%d] start_trb: %lx, last_trb: %lx\n",
			ring_priv->slot, ring_priv->ep_dci, ring_priv->stream,
			xfer_priv->first_deq_ptr, xfer_priv->last_deq_ptr);
		dev->backend->cancel_transfer(dev, xfer);
	}
	pthread_mutex_unlock(&ring_priv->outstanding_lock);

	/* wait all outstanding transfers finished */
	while (1) {
		pthread_mutex_lock(&ring_priv->outstanding_lock);
		if (STAILQ_EMPTY(&ring_priv->outstanding)) {
			pthread_mutex_unlock(&ring_priv->outstanding_lock);
			break;
		}
		pthread_mutex_unlock(&ring_priv->outstanding_lock);
		usleep(5000);
	}

	union xhci_trb *stopped_trb = READ_ONCE(&ring_priv->stopped_trb);
#if 0
	if (stopped_trb) {
		ring_priv->deq_ptr_gpa = READ_ONCE(&ring_priv->stopped_trb_gpa);
		ring_priv->rcs = stopped_trb->common.c;
	}
#endif

	/* graceful stop.
	 * No outstanding transfers actually canceled (may be canceled but finished).
	 */
	if (!xring->destroying && !stopped_trb) {
		if (ring_priv->stream_desc) {
			assert(ring_priv->stream > 0);
			struct stream_ctx *stream_ctx = ring_priv->stream_desc->stream_ctx;
			stream_ctx->tr_deq_ptr = ring_priv->deq_ptr_gpa >> XHCI_TR_DEQ_PTR_SHIFT;
			stream_ctx->dcs = ring_priv->rcs;
			//stream_ctx->stopped_edtla = ? // if sec=1
		} else {
			struct ep_ctx *ep_ctx = ep_desc->epctx;
			ep_ctx->tr_deq_ptr = ring_priv->deq_ptr_gpa >> XHCI_TR_DEQ_PTR_SHIFT;
			ep_ctx->dcs = ring_priv->rcs;
		}

		union xhci_trb evt = make_xfer_event(ring_priv->deq_ptr_gpa, 0,
				ring_priv->slot, ring_priv->ep_dci, 0,
				COMPLETION_CODE_STOPPED_LEN_INVALID);
		xhci_enqueue_event(xhci, 0, &evt, true);
		pr_dbg("stopped in between TD boundaries!\n");
	}
}

static void transfer_ring_reset(struct xhci_ring *xring)
{
	struct transfer_ring_priv *priv = xhci_ring_priv(xring);
	priv->rcs = 0;
	priv->deq_ptr_gpa = 0;
	priv->deq_trb = priv->last_map_base = NULL;
}

static int transfer_ring_init(struct xhci_ring *xring, void *d)
{
	struct transfer_ring_init_data *data = d;
	struct transfer_ring_priv *priv = xhci_ring_priv(xring);
	struct stream_desc *stream_desc = data->is_stream ? data->stream_desc : NULL;
	struct epctx_desc *ep_desc = stream_desc ? stream_desc->ep_desc : data->ep_desc;
	struct ep_ctx *ep_ctx = ep_desc->epctx;

	priv->epctx_desc = ep_desc;
	priv->stream_desc = stream_desc;
	priv->xhci = priv->epctx_desc->devctx_desc->xhci;
	priv->slot = priv->epctx_desc->devctx_desc->slot;
	priv->ep_dci = priv->epctx_desc->dci;
	priv->stream = stream_desc ? stream_desc->stream : 0;

	if (priv->stream) {
		priv->deq_ptr_gpa = stream_desc->stream_ctx->tr_deq_ptr << XHCI_TR_DEQ_PTR_SHIFT;
		priv->rcs = stream_desc->stream_ctx->dcs;
	} else {
		priv->deq_ptr_gpa = ep_ctx->tr_deq_ptr << XHCI_TR_DEQ_PTR_SHIFT;
		priv->rcs = ep_ctx->dcs;
	}

	priv->mps = ep_ctx->mps;

	pthread_mutex_init(&priv->outstanding_lock, NULL);
	STAILQ_INIT(&priv->outstanding);
	priv->next_ctrl_stage = CTRL_XFER_STAGE_SETUP;
	return 0;
}

struct xhci_ring_ops transfer_ring_ops = {
	.priv_size = sizeof(struct transfer_ring_priv),
	.init = transfer_ring_init,
	.reset = transfer_ring_reset,
	.start = transfer_ring_start,
	.stop = transfer_ring_stop,
	//.notify = transfer_ring_notify,
	.handle_trb = transfer_ring_handle_trbs,
};

/* --------------------- device slots ------------------- */

/* ep_ctx must have been updated. This function should just create the ring and setup tr_deq_ptr
 * and dcs. */
static int xhci_ep_enable(struct epctx_desc *ep_desc, struct ep_ctx *ep_ctx)
{
	struct devctx_desc *dev_desc = ep_desc->devctx_desc;
	uint8_t slot = ep_desc->devctx_desc->slot;
	uint8_t ep_dci = ep_desc->dci;

	assert(dev_desc->enabled);
	assert(ep_dci >= XHCI_DCI_EP0_CTX && ep_dci < XHCI_DCI_MAX);

	if (ep_desc->enabled)
		return 0;

	ep_desc->epctx = &dev_desc->ctxs->ctxs[ep_dci].ep_ctx;
	memcpy(ep_desc->epctx, ep_ctx, sizeof(struct ep_ctx));

	pr_dbg("try to enable slot %d endpoint(dci) %d\n", slot, ep_dci);
	char name[32];

	if (XHCI_EP_TYPE_IS_BULK(ep_ctx->type) && ep_ctx->mpstream) {
		ep_desc->streams = 1 << (ep_ctx->mpstream + 1);
		ep_desc->stream_descs = calloc(ep_desc->streams, sizeof(struct stream_desc));
		if (!ep_desc->stream_descs) {
			pr_err("failed to allocate stream contexts\n");
			goto exit;
		}
		struct usb_device *dev = dev_desc->device;
		uint8_t ep_id = xhci_ep_dci_to_epid(ep_dci);
		if (dev->backend->alloc_streams(dev, ep_desc->streams - 1, &ep_id, 1)) {
			pr_err("failed to allocate streams\n");
			free(ep_desc->stream_descs);
			goto exit;
		}

		struct vmctx *vmctx = dev_desc->xhci->pdev->vmctx;
		uint64_t stream_array_gpa = ep_ctx->tr_deq_ptr << XHCI_TR_DEQ_PTR_SHIFT;
		uint32_t size = ep_desc->streams * sizeof(struct stream_ctx);
		ep_desc->stream_ctxs = dm_vm_map_gpa(vmctx, stream_array_gpa, size);
		int i;
		for (i = 1; i < ep_desc->streams; i++) {
			struct stream_desc *stream_desc = &ep_desc->stream_descs[i];
			stream_desc->stream = i;
			stream_desc->ep_desc = ep_desc;
			stream_desc->stream_ctx = &ep_desc->stream_ctxs[i];
			if (stream_desc->stream_ctx->sct != XHCI_SCT_PRIMARY_ONLY) {
				pr_err("no secondary stream array support!\n");
			}
			snprintf(name, sizeof(name), "xring-%d-%d-%d", slot, ep_dci, i);
			struct transfer_ring_init_data data = { true, .stream_desc = stream_desc };
			stream_desc->xring = xhci_ring_create(dev_desc->xhci, &transfer_ring_ops, name, 0, &data);
			if (!stream_desc->xring) {
				pr_err("failed to create transfer ring %s\n", name);
				break;
			}
			stream_desc->enabled = true;
		}
		if (i != ep_desc->streams) {
			for (int j = 1; j < i; j++) {
				xhci_ring_destroy(ep_desc->stream_descs[j].xring, true);
			}
			dev->backend->free_streams(dev, &ep_id, 1);
			free(ep_desc->stream_descs);
			goto exit;
		}
	} else {
		ep_desc->streams = 0;
		uint16_t interval = 0;
		if (XHCI_EP_TYPE_IS_INT(ep_ctx->type) || XHCI_EP_TYPE_IS_ISOC(ep_ctx->type))
			interval = 1 << ep_ctx->interval;
		snprintf(name, sizeof(name), "xring-%d-%d-%d", slot, ep_dci, 0);
		struct transfer_ring_init_data data = { false, .ep_desc = ep_desc };
		ep_desc->xring = xhci_ring_create(dev_desc->xhci, &transfer_ring_ops, name, interval, &data);
		if (!ep_desc->xring) {
			pr_err("failed to create transfer ring\n");
			goto exit;
		}
	}

	ep_desc->enabled = true;
	ep_desc->epctx->state = XHCI_EP_STATE_RUNNING;
	return 0;
exit:
	memset(ep_desc->epctx, 0, sizeof(struct ep_ctx));
	return -1;
}

static void xhci_ep_disable(struct epctx_desc *ep_desc)
{
	if (!ep_desc->enabled)
		return;

	struct devctx_desc *dev_desc = ep_desc->devctx_desc;
	struct ep_ctx *ep_ctx = ep_desc->epctx;
	uint8_t slot = dev_desc->slot;
	uint8_t ep_dci = ep_desc->dci;

	pr_dbg("try to disable slot %d endpoint(dci) %d\n", slot, ep_dci);

	if (ep_ctx->state == XHCI_EP_STATE_DISABLED)
		pr_info("endpoint(dci) %d was already disabled\n", ep_dci);

	if (!ep_desc->streams) {
		xhci_ring_destroy(ep_desc->xring, true);
	} else {
		for (int i = 1; i < ep_desc->streams; i++) {
			xhci_ring_destroy(ep_desc->stream_descs[i].xring, true);
		}
		struct usb_device *dev = dev_desc->device;
		uint8_t ep_id = xhci_ep_dci_to_epid(ep_dci);
		dev->backend->free_streams(dev, &ep_id, 1);
		free(ep_desc->stream_descs);
	}
	ep_ctx->state = XHCI_EP_STATE_DISABLED;
	ep_desc->enabled = false;
}

#if 0
static void xhci_ep_halt(struct pci_xhci *xhci, struct devctx_desc *dev_desc, uint8_t ep_dci)
{
	struct epctx_desc *ep_desc = &dev_desc->ep_descs[ep_dci];
	struct ep_ctx *ep_ctx = ep_desc->epctx;

	assert(ep_desc->enabled);

	pr_dbg("try to halt slot %d endpoint(dci) %d\n", (dev_desc - xhci->ctx_descs), ep_dci);
	if (ep_ctx->state == XHCI_EP_STATE_HALTED) {
		pr_info("endpoint(dci) %d was already halted\n", ep_dci);
		return;
	}

	ep_ctx->state = XHCI_EP_STATE_HALTED;
	work_add(&ep_desc->halt_work);
}
#endif

static void xhci_ep_halt_work(struct work *work)
{
	struct epctx_desc *ep_desc = container_of(work, struct epctx_desc, halt_work);
	struct devctx_desc *dev_desc = ep_desc->devctx_desc;
	struct ep_ctx *ep_ctx = ep_desc->epctx;

	/* Another devctx operator besides command ring, doorbell and xfer done handlers */
	pthread_rwlock_wrlock(&dev_desc->rwlock);	// rdlock is ok?
	if (!dev_desc->enabled || !ep_desc->enabled) {
		pthread_rwlock_unlock(&dev_desc->rwlock);
		return;
	}

	if (!ep_desc->streams)
		xhci_ring_stop(ep_desc->xring, true);
	else {
		for (int i = 1; i < ep_desc->streams; i++) {
			if (ep_desc->stream_descs[i].enabled)
				xhci_ring_stop(ep_desc->stream_descs[i].xring, true);
		}
	}

	pthread_rwlock_unlock(&dev_desc->rwlock);
	ep_ctx->state = XHCI_EP_STATE_HALTED;
}

static int xhci_init_slots(struct pci_xhci *xhci)
{
	xhci->ctx_descs = calloc(xhci->configs.maxslots + 1, sizeof(struct devctx_desc));
	if (!xhci->ctx_descs) {
		pr_err("no memory for xhci slots\n");
		return -1;
	}

	for (int i = 0; i < xhci->configs.maxslots + 1; i++) {
		pthread_rwlock_init(&xhci->ctx_descs[i].rwlock, NULL);
		xhci->ctx_descs[i].xhci = xhci;
		xhci->ctx_descs[i].slot = i;
		for (int j = 0; j < XHCI_DCI_MAX; j++) {
			xhci->ctx_descs[i].ep_descs[j].dci = j;
			xhci->ctx_descs[i].ep_descs[j].devctx_desc = &xhci->ctx_descs[i];
			work_init(&xhci->ctx_descs[i].ep_descs[j].halt_work, xhci_ep_halt_work);
		}
	}

	return 0;
}

static void xhci_deinit_slots(struct pci_xhci *xhci)
{
	for (int i = 1; i <= xhci->configs.maxslots; i++) {
		struct devctx_desc *devctx = &xhci->ctx_descs[i];
		if (devctx->selected && devctx->device) {
			pr_dbg("slot %d still have device, guest may not disable this slot!\n", i);
		}
	}
	free(xhci->ctx_descs);
}

static void xhci_select_slots(struct pci_xhci *xhci)
{
	struct devctx_desc *devctx;
	int i = 1;
	for (; i < (xhci->config & XHCI_OP_CONFIG_MAXSLOTS_MASK); i++) {
		devctx = &xhci->ctx_descs[i];
		if (!devctx->selected) {
			devctx->selected = true;
			devctx->enabled = false;
			devctx->device = NULL;
			devctx->ctxs = NULL;
		}
	}

	for (; i < xhci->configs.maxslots; i++) {
		devctx = &xhci->ctx_descs[i];
		if (devctx->enabled) {
			pr_warn("guest try to unselect enabled device context!\n");
		}
		devctx->selected = false;
	}
}

static bool xhci_slots_all_disabled(struct pci_xhci *xhci)
{
	for (int i = 1; xhci->ctx_descs[i].selected; i++) {
		if (xhci->ctx_descs[i].enabled)
			return false;
	}
	return true;
}

static struct devctx_desc *xhci_slot_enable(struct pci_xhci *xhci)
{
	// slot state has only one writer, the command ring
	if (xhci_slots_all_disabled(xhci)) {
		struct vmctx *vmctx = xhci->pdev->vmctx;
		uint32_t size = sizeof(uint64_t) * (xhci->configs.maxslots + 1);
		xhci->dcbaap = dm_vm_map_gpa(vmctx, xhci->dcbaap_gpa, size);
	}

	for (int i = 1; xhci->ctx_descs[i].selected; i++) {
		struct devctx_desc *dev_desc = &xhci->ctx_descs[i];
		pthread_rwlock_wrlock(&dev_desc->rwlock);
		if (!dev_desc->enabled) {
			dev_desc->enabled = true;
			/* the pointer in DCBAA isn't valid until next slot command */
			dev_desc->ctxs = NULL;
			dev_desc->sctx = NULL;
			pthread_rwlock_unlock(&dev_desc->rwlock);
			return dev_desc;
		}
		pthread_rwlock_unlock(&dev_desc->rwlock);
	}
	return NULL;
}

static int xhci_slot_disable(struct pci_xhci *xhci, uint8_t slot)
{
	struct devctx_desc *dev_desc = &xhci->ctx_descs[slot];
	pthread_rwlock_wrlock(&dev_desc->rwlock);
	if (!dev_desc->enabled) {
		pthread_rwlock_unlock(&dev_desc->rwlock);
		pr_warn("slot %d was already disabled\n", slot);
		return 0;
	}

	/* slot enabled and contexts mapped (by address_device command) */
	if (dev_desc->ctxs) {
		for (int ep_dci = XHCI_DCI_EP0_CTX; ep_dci < XHCI_DCI_MAX; ep_dci++) {
			xhci_ep_disable(&dev_desc->ep_descs[ep_dci]);
		}
		dev_desc->sctx->state = XHCI_SLOT_STATE_DISABLED;
		dev_desc->sctx = NULL;
		dev_desc->ctxs = NULL;	/* mark of unmap */
		
		struct usb_hcd *hcd = usb_get_hcd_from_priv(xhci);
		struct usb_device *dev = dev_desc->device;
		if (dev) {
			hcd->devices[dev->addr] = NULL;
			dev->addr = 0;
			dev_desc->device = NULL;
			usb_device_put(dev);
		}
	}
	dev_desc->enabled = false;
	pthread_rwlock_unlock(&dev_desc->rwlock);

	if (xhci_slots_all_disabled(xhci)) {
		xhci->dcbaap = NULL;	/* mark of unmap, catch bad accesses before next map */
	}

	return 0;
}

static void xhci_slot_reset_all(struct pci_xhci *xhci)
{
	for (int i = 1; xhci->ctx_descs[i].selected; i++) {
		if (xhci->ctx_descs[i].enabled) {
			xhci_slot_disable(xhci, i);
		}
		xhci->ctx_descs[i].selected = false;
	}
}

static void xhci_slot_update_max_ctxs(struct output_ctxs *octxs)
{
	uint8_t max_ctx_dci = 1;
	for (int i = XHCI_DCI_EP0_CTX; i < XHCI_DCI_MAX; i++) {
		if (octxs->ctxs[i].ep_ctx.state != XHCI_EP_STATE_DISABLED)
			max_ctx_dci = i;
	}
	octxs->ctxs[XHCI_DCI_SLOT_CTX].slot_ctx.ctxs = max_ctx_dci;
}

#if 0
/* always called on address device command? */
static void xhci_slot_map_ctxs(struct pci_xhci *xhci, uint8_t slot)
{
	struct devctx_desc *dev_desc = &xhci->ctx_descs[slot];
	if (dev_desc->ctxs)
		return;

	/* First command for an enabled slot. Device Context ownership is passed to the HC */
	struct vmctx *vmctx = xhci->pdev->vmctx;
	uint64_t ctxs_gpa = xhci->dcbaap[slot];
	dev_desc->ctxs = dm_vm_map_gpa(vmctx, ctxs_gpa, XHCI_PAGE_SIZE_4K);
	pr_dbg("map dcbaap[%d]: %lx\n", slot, ctxs_gpa);

	/* Driver should have cleared it. Just make sure it's cleared */
	memset(dev_desc->ctxs, 0, sizeof(struct output_ctxs));
}
#endif


/* --------------------- handle commands ------------------- */

static void handle_command_enable_slot(struct pci_xhci *xhci, union xhci_trb *trb, uint64_t trb_gpa)
{
	uint8_t slot;
	enum completion_code cc;

	struct devctx_desc *new = xhci_slot_enable(xhci);
	if (new) {
		slot = new - xhci->ctx_descs;
		cc = COMPLETION_CODE_SUCCESS;
	} else {
		slot = 0;
		cc = COMPLETION_CODE_NO_SLOT;
	}

	union xhci_trb evt_cmd = make_cmd_event(trb_gpa, slot, cc);
	xhci_enqueue_event(xhci, 0, &evt_cmd, true);
}

static void handle_command_disable_slot(struct pci_xhci *xhci, union xhci_trb *trb, uint64_t trb_gpa)
{
	uint8_t slot = trb->cmd_disable_slot.slot_id;
	enum completion_code cc;

	int ret = xhci_slot_disable(xhci, slot);
	if (!ret)
		cc = COMPLETION_CODE_SUCCESS;
	else
		cc = COMPLETION_CODE_SLOT_DISABLED;

	union xhci_trb evt_cmd = make_cmd_event(trb_gpa, slot, cc);
	xhci_enqueue_event(xhci, 0, &evt_cmd, true);
}

static void handle_command_address_device(struct pci_xhci *xhci, union xhci_trb *trb, uint64_t trb_gpa)
{
	struct usb_hcd *hcd = usb_get_hcd_from_priv(xhci);
	struct vmctx *vmctx = xhci->pdev->vmctx;
	uint64_t ictxs_gpa = trb->cmd_address_device.input_ctx_ptr;
	uint8_t slot = trb->cmd_address_device.slot_id;
	struct devctx_desc *dev_desc = &xhci->ctx_descs[slot];
	struct epctx_desc *ep_desc = &dev_desc->ep_descs[XHCI_DCI_EP0_CTX];
	bool bsr = trb->cmd_address_device.bsr;
	enum completion_code cc = COMPLETION_CODE_SUCCESS;
	union xhci_trb evt_cmd;

	struct input_ctxs *ictxs = dm_vm_map_gpa(vmctx, ictxs_gpa, sizeof(*ictxs));
	xhci_dump_ictxs(ictxs, "ictxs");

	pthread_rwlock_wrlock(&dev_desc->rwlock);
	if (!dev_desc->enabled) {
		cc = COMPLETION_CODE_SLOT_DISABLED;
		goto exit_unlock;
	} else if (dev_desc->sctx && dev_desc->sctx->state != XHCI_SLOT_STATE_DEFAULT) {
		/* already addressed */
		cc = COMPLETION_CODE_CTX_STATE_ERROR;
		goto exit_unlock;
	}

	if (!dev_desc->sctx) {
		pr_dbg("map dcbaap[%d]: %lx\n", slot, xhci->dcbaap[slot]);
		dev_desc->ctxs = dm_vm_map_gpa(vmctx, xhci->dcbaap[slot], XHCI_PAGE_SIZE_4K);
		dev_desc->sctx = &dev_desc->ctxs->ctxs[XHCI_DCI_SLOT_CTX].slot_ctx;
	}
	//xhci_slot_map_ctxs(xhci, slot);
	xhci_dump_octxs(dev_desc->ctxs, "before");

	/* handle input contexts and update output contexts */
	// TODO validate ictxs, otherwise COMPLETION_CODE_PARAM_ERROR

	if (!dev_desc->device) {
		// Consider slot_ctx.route after introducing vHUB
		uint8_t port_idx = ictxs->ctxs[XHCI_DCI_SLOT_CTX].slot_ctx.rhport - 1;
		struct xhci_port *xport = &xhci->ports[port_idx];
		struct usb_hub *hub = xport->port->hub;

		pthread_mutex_lock(&hub->lock);
		dev_desc->device = usb_device_get(xport->port->device);
		pthread_mutex_unlock(&hub->lock);
		if (!dev_desc->device) {
			cc = COMPLETION_CODE_TRANSACTION_ERROR;
			goto exit_unlock;
		}
	}

	if (bsr) {
		memcpy(dev_desc->sctx, &ictxs->ctxs[XHCI_DCI_SLOT_CTX].slot_ctx, sizeof(struct slot_ctx));
		dev_desc->sctx->state = XHCI_SLOT_STATE_DEFAULT;
		dev_desc->sctx->addr = 0;
	} else {
		/* no hcd lock. All address related commands are handled by command ring. */
		uint8_t newaddr = usb_hcd_alloc_dev_addr(hcd, dev_desc->device);
		if (!newaddr) {
			pr_err("no device address available ?\n");
			dev_desc->sctx->addr = 0;
			cc = COMPLETION_CODE_RESOURCE_ERROR;
			goto exit_unlock;
		}
		memcpy(dev_desc->sctx, &ictxs->ctxs[XHCI_DCI_SLOT_CTX].slot_ctx, sizeof(struct slot_ctx));
		dev_desc->sctx->state = XHCI_SLOT_STATE_ADDRESSED;
		dev_desc->sctx->addr = newaddr;
	}

	/* Spec doesn't say exactly what if control endpoint initialization fails? */
	if (xhci_ep_enable(ep_desc, &ictxs->ctxs[XHCI_DCI_EP0_CTX].ep_ctx)) {
		pr_err("failed to enabled control endpoint!\n");
		cc = COMPLETION_CODE_RESOURCE_ERROR;
	}

exit_unlock:
	pthread_rwlock_unlock(&dev_desc->rwlock);

	xhci_dump_octxs(dev_desc->ctxs, "after");

	evt_cmd = make_cmd_event(trb_gpa, slot, cc);
	xhci_enqueue_event(xhci, 0, &evt_cmd, true);

	// unmap input ctxs
}

static void handle_command_evaluate_contex(struct pci_xhci *xhci, union xhci_trb *trb, uint64_t trb_gpa)
{
	struct vmctx *vmctx = xhci->pdev->vmctx;
	uint64_t ictxs_gpa = trb->cmd_eval_context.input_ctx_ptr;
	uint8_t slot = trb->cmd_eval_context.slot_id;
	struct devctx_desc *dev_desc = &xhci->ctx_descs[slot];
	enum completion_code cc = COMPLETION_CODE_SUCCESS;
	union xhci_trb evt_cmd;

	struct input_ctxs *ictxs = dm_vm_map_gpa(vmctx, ictxs_gpa, XHCI_PAGE_SIZE_4K);
	xhci_dump_ictxs(ictxs, "ictxs");

	pthread_rwlock_wrlock(&dev_desc->rwlock);
	struct slot_ctx *slot_ctx = dev_desc->sctx;
	struct output_ctxs *octxs = dev_desc->ctxs;
	if (!dev_desc->enabled) {
		cc = COMPLETION_CODE_SLOT_DISABLED;
		goto exit_unlock;
	} else if (!slot_ctx || slot_ctx->state == XHCI_SLOT_STATE_ENABLED) {
		/* should be in default/addressed/configured state */
		cc = COMPLETION_CODE_CTX_STATE_ERROR;
		goto exit_unlock;
	}

	/* handle input contexts and update output contexts */
	struct input_ctrl_ctx *ictrl = &ictxs->ctrl_ctx;
	if (ictrl->add_ctx & BIT(XHCI_DCI_SLOT_CTX)) {
		/* not all fields should be copied. Refer to $4.5.2 and $6.2.2.3 */
		struct slot_ctx *isctx = &ictxs->ctxs[XHCI_DCI_SLOT_CTX].slot_ctx;
		slot_ctx->max_exit_latency = isctx->max_exit_latency;
		slot_ctx->intr = isctx->intr; // TODO validate
	}
	if (ictrl->add_ctx & BIT(XHCI_DCI_EP0_CTX)) {
		/* not all fields should be copied. $6.2.3.3 */
		struct ep_ctx *oectx = &octxs->ctxs[XHCI_DCI_EP0_CTX].ep_ctx;
		struct ep_ctx *iectx = &ictxs->ctxs[XHCI_DCI_EP0_CTX].ep_ctx;
		oectx->mps= iectx->mps;
	}

exit_unlock:
	pthread_rwlock_unlock(&dev_desc->rwlock);

	evt_cmd = make_cmd_event(trb_gpa, slot, cc);
	xhci_enqueue_event(xhci, 0, &evt_cmd, true);
}

/* refer to spec $4.6.6 */
static void handle_command_config_endpoint(struct pci_xhci *xhci, union xhci_trb *trb, uint64_t trb_gpa)
{
	struct vmctx *vmctx = xhci->pdev->vmctx;
	uint64_t ictxs_gpa = trb->cmd_config_endpoint.input_ctx_ptr;
	uint8_t slot = trb->cmd_config_endpoint.slot_id;
	bool deconfig = trb->cmd_config_endpoint.deconfig;
	struct devctx_desc *dev_desc = &xhci->ctx_descs[slot];
	enum completion_code cc = COMPLETION_CODE_SUCCESS;
	union xhci_trb evt_cmd;

	/* handle input contexts and update output contexts */
	struct input_ctxs *ictxs = dm_vm_map_gpa(vmctx, ictxs_gpa, XHCI_PAGE_SIZE_4K);
	xhci_dump_ictxs(ictxs, "ictxs");

	pthread_rwlock_wrlock(&dev_desc->rwlock);
	struct output_ctxs *octxs = dev_desc->ctxs;
	struct slot_ctx *slot_ctx = dev_desc->sctx;
	if (!dev_desc->enabled) {
		cc = COMPLETION_CODE_SLOT_DISABLED;
		goto exit_unlock;
	} else if (!slot_ctx) {
		cc = COMPLETION_CODE_CTX_STATE_ERROR;
		goto exit_unlock;
	}

	if (slot_ctx->state == XHCI_SLOT_STATE_CONFIGURED) {
		if (deconfig) {
			for (int ep_dci = XHCI_DCI_EPX_CTX(1, 0); ep_dci < XHCI_DCI_MAX; ep_dci++) {
				xhci_ep_disable(&dev_desc->ep_descs[ep_dci]);

				// TODO subtract resources required
				// TODO subtract bandwidth required for periodic endpoint
			}
			slot_ctx->state = XHCI_SLOT_STATE_ADDRESSED;
		}
	}

	if (!deconfig && (slot_ctx->state == XHCI_SLOT_STATE_CONFIGURED ||
			slot_ctx->state == XHCI_SLOT_STATE_ADDRESSED))
	{
		// validate ictxs, otherwise COMPLETION_CODE_PARAM_ERROR
		for (int ep_dci = XHCI_DCI_EPX_CTX(1, 0); ep_dci < XHCI_DCI_MAX; ep_dci++) {
			if (!(ictxs->ctrl_ctx.add_ctx & BIT(ep_dci)))
				continue;;
			if (!ictxs->ctxs[ep_dci].ep_ctx.tr_deq_ptr) {
				// TODO so many checks
				pr_info("input ep(dci) %d context invalid: tr_deq_ptr: %lx",
						ep_dci, ictxs->ctxs[ep_dci].ep_ctx.tr_deq_ptr);
				cc = COMPLETION_CODE_PARAM_ERROR;
				goto exit_unlock;
			}
		}

		// TODO resource/bandwidth allocation for int and isoc endpoints
		for (int ep_dci = XHCI_DCI_EPX_CTX(1, 0); ep_dci < XHCI_DCI_MAX; ep_dci++) {
			if (ictxs->ctrl_ctx.add_ctx & BIT(ep_dci)) {
				// TODO account required resource
				if (XHCI_EP_TYPE_IS_INT(ictxs->ctxs[ep_dci].ep_ctx.type) ||
					XHCI_EP_TYPE_IS_ISOC(ictxs->ctxs[ep_dci].ep_ctx.type)) {
					// account required bandwidth
				}
			}
		}

		if (1 /* TODO check resource is sufficient */) {
			if (1 /* TODO check bandwidth is sufficient */) {
				/* TODO subtract the bandwidth required */
				bool is_cfged = false;
				for (int ep_dci = XHCI_DCI_EPX_CTX(1, 0); ep_dci < XHCI_DCI_MAX; ep_dci++) {
					struct epctx_desc *ep_desc = &dev_desc->ep_descs[ep_dci];
					if (ictxs->ctrl_ctx.drop_ctx & BIT(ep_dci))
						xhci_ep_disable(ep_desc);
					if (ictxs->ctrl_ctx.add_ctx & BIT(ep_dci)) {
						/* TODO ictxs->ctxs[i].ep_ctx.state may be
						 * stopped/running, we should differentiate
						 * enable(stopped) and enabled(running) */
						if (xhci_ep_enable(ep_desc, &ictxs->ctxs[ep_dci].ep_ctx)) {
							pr_err("failed to enable endpoint(dci) %d\n", ep_dci);
							cc = COMPLETION_CODE_RESOURCE_ERROR;
							break;
						}
						is_cfged = true;
					}
				}

				if (cc == COMPLETION_CODE_RESOURCE_ERROR) {
					for (int ep_dci = XHCI_DCI_EPX_CTX(1, 0); ep_dci < XHCI_DCI_MAX; ep_dci++) {
						struct epctx_desc *ep_desc = &dev_desc->ep_descs[ep_dci];
						if (ictxs->ctrl_ctx.add_ctx & BIT(ep_dci)) {
							xhci_ep_disable(ep_desc);
						}
					}
				} else {
					if (is_cfged)
						slot_ctx->state = XHCI_SLOT_STATE_CONFIGURED;
					else
						slot_ctx->state = XHCI_SLOT_STATE_ADDRESSED;
					cc = COMPLETION_CODE_SUCCESS;
				}
			} else {
				cc = COMPLETION_CODE_BANDWIDTH_ERROR;
			}
		} else {
			cc = COMPLETION_CODE_RESOURCE_ERROR;
		}

	} else {
		cc = COMPLETION_CODE_CTX_STATE_ERROR;
	}

	xhci_slot_update_max_ctxs(octxs);
	xhci_dump_octxs(octxs, "after");

exit_unlock:
	pthread_rwlock_unlock(&dev_desc->rwlock);

	evt_cmd = make_cmd_event(trb_gpa, slot, cc);
	xhci_enqueue_event(xhci, 0, &evt_cmd, true);
}

/* refer to spec $4.6.9 */
static void handle_command_stop_endpoint(struct pci_xhci *xhci, union xhci_trb *trb, uint64_t trb_gpa)
{
	uint8_t slot = trb->cmd_stop_endpoint.slot;
	uint8_t ep_dci = trb->cmd_stop_endpoint.endpoint;
	//bool suspend = trb->cmd_stop_endpoint.suspend;
	struct devctx_desc *dev_desc = &xhci->ctx_descs[slot];
	struct epctx_desc *ep_desc = &dev_desc->ep_descs[ep_dci];
	enum completion_code cc = COMPLETION_CODE_SUCCESS;
	union xhci_trb evt_cmd;

	pthread_rwlock_wrlock(&dev_desc->rwlock);
	struct slot_ctx *slot_ctx = dev_desc->sctx;
	struct ep_ctx *ep_ctx = ep_desc->epctx;
	if (!dev_desc->enabled) {
		cc = COMPLETION_CODE_SLOT_DISABLED;
		goto exit_unlock;
	} else if (!slot_ctx || slot_ctx->state == XHCI_SLOT_STATE_ENABLED) {
		/* should be in default/addressed/configured state */
		cc = COMPLETION_CODE_CTX_STATE_ERROR;
		goto exit_unlock;
	} else if (!ep_desc->enabled || ep_ctx->state != XHCI_EP_STATE_RUNNING) {
		cc = COMPLETION_CODE_CTX_STATE_ERROR;
		goto exit_unlock;
	}

	if (!ep_desc->streams) {
		struct xhci_ring *xring = ep_desc->xring;
		xhci_ring_stop(xring, true);
	} else {
		for (int i = 1; i < ep_desc->streams; i++) {
			struct xhci_ring *xring = ep_desc->stream_descs[i].xring;
			xhci_ring_stop(xring, true);
		}
	}
	ep_ctx->state = XHCI_EP_STATE_STOPPED;
exit_unlock:
	pthread_rwlock_unlock(&dev_desc->rwlock);

	evt_cmd = make_cmd_event(trb_gpa, slot, cc);
	xhci_enqueue_event(xhci, 0, &evt_cmd, true);
}

static void handle_command_reset_endpoint(struct pci_xhci *xhci, union xhci_trb *trb,
							uint64_t trb_gpa)
{
	uint8_t slot = trb->cmd_reset_endpoint.slot;
	uint8_t ep_dci = trb->cmd_reset_endpoint.endpoint;
	//bool tsp = trb->cmd_reset_endpoint.tsp;
	struct devctx_desc *dev_desc = &xhci->ctx_descs[slot];
	struct epctx_desc *ep_desc = &dev_desc->ep_descs[ep_dci];
	enum completion_code cc = COMPLETION_CODE_SUCCESS;
	union xhci_trb evt_cmd;

	pthread_rwlock_wrlock(&dev_desc->rwlock);
	struct slot_ctx *slot_ctx = dev_desc->sctx;
	struct ep_ctx *ep_ctx = ep_desc->epctx;
	if (!dev_desc->enabled) {
		cc = COMPLETION_CODE_SLOT_DISABLED;
	} else if (!slot_ctx || slot_ctx->state == XHCI_SLOT_STATE_ENABLED) {
		/* should be in default/addressed/configured state */
		cc = COMPLETION_CODE_CTX_STATE_ERROR;
	} else if (!ep_desc->enabled || ep_ctx->state != XHCI_EP_STATE_HALTED) {
		cc = COMPLETION_CODE_CTX_STATE_ERROR;
	} else {
		cc = COMPLETION_CODE_SUCCESS;
		ep_ctx->state = XHCI_EP_STATE_STOPPED;
	}
	pthread_rwlock_unlock(&dev_desc->rwlock);

	evt_cmd = make_cmd_event(trb_gpa, slot, cc);
	xhci_enqueue_event(xhci, 0, &evt_cmd, true);
}

static void handle_command_set_tr_deq_ptr(struct pci_xhci *xhci, union xhci_trb *trb,
							uint64_t trb_gpa)
{
	uint8_t slot = trb->cmd_set_tr_dequeue_ptr.slot;
	uint8_t ep_dci = trb->cmd_set_tr_dequeue_ptr.endpoint;
	uint16_t stream = trb->cmd_set_tr_dequeue_ptr.stream;
	uint64_t deq_ptr_gpa = trb->cmd_set_tr_dequeue_ptr.tr_deq_ptr << XHCI_TR_DEQ_PTR_SHIFT;
	bool ccs = trb->cmd_set_tr_dequeue_ptr.ccs;
	//uint8_t sctx_type = trb->cmd_set_tr_dequeue_ptr.sctx_type;

	struct devctx_desc *dev_desc = &xhci->ctx_descs[slot];
	struct epctx_desc *ep_desc = &dev_desc->ep_descs[ep_dci];
	enum completion_code cc = COMPLETION_CODE_SUCCESS;
	union xhci_trb evt_cmd;

	pthread_rwlock_wrlock(&dev_desc->rwlock);
	struct slot_ctx *slot_ctx = dev_desc->sctx;
	struct ep_ctx *ep_ctx = ep_desc->epctx;
	if (!dev_desc->enabled) {
		cc = COMPLETION_CODE_SLOT_DISABLED;
		goto exit_unlock;
	} else if (!slot_ctx || slot_ctx->state == XHCI_SLOT_STATE_ENABLED) {
		/* should be in default/addressed/configured state */
		cc = COMPLETION_CODE_CTX_STATE_ERROR;
		goto exit_unlock;
	} else if (!ep_desc->enabled || (ep_ctx->state != XHCI_EP_STATE_STOPPED &&
				ep_ctx->state != XHCI_EP_STATE_ERROR)) {
		cc = COMPLETION_CODE_CTX_STATE_ERROR;
		goto exit_unlock;
	}

	if ((ep_desc->streams && (!stream || stream >= ep_desc->streams)) ||
			(!ep_desc->streams && stream)) {
		cc = COMPLETION_CODE_TRB_ERROR;
		goto exit_unlock;
	} else if (stream && ep_desc->streams) {
		struct stream_desc *stream_desc = &ep_desc->stream_descs[stream];
		struct stream_ctx *sctx = stream_desc->stream_ctx;
		struct xhci_ring *xring = stream_desc->xring;
		struct transfer_ring_priv *priv = xhci_ring_priv(xring);
		sctx->tr_deq_ptr = trb->cmd_set_tr_dequeue_ptr.tr_deq_ptr;
		sctx->dcs = ccs;
		priv->deq_ptr_gpa = deq_ptr_gpa;
		priv->rcs = ccs;
	} else {
		struct xhci_ring *xring = ep_desc->xring;
		struct transfer_ring_priv *priv = xhci_ring_priv(xring);
		ep_ctx->tr_deq_ptr = trb->cmd_set_tr_dequeue_ptr.tr_deq_ptr;
		ep_ctx->dcs = ccs;
		priv->deq_ptr_gpa = deq_ptr_gpa;
		priv->rcs = ccs;
	}

exit_unlock:
	pthread_rwlock_unlock(&dev_desc->rwlock);

	evt_cmd = make_cmd_event(trb_gpa, slot, cc);
	xhci_enqueue_event(xhci, 0, &evt_cmd, true);
}

static void handle_command_reset_device(struct pci_xhci *xhci, union xhci_trb *trb,
							uint64_t trb_gpa)
{
	uint8_t slot = trb->cmd_reset_device.slot;
	struct devctx_desc *dev_desc = &xhci->ctx_descs[slot];
	enum completion_code cc = COMPLETION_CODE_SUCCESS;
	union xhci_trb evt_cmd;

	pthread_rwlock_wrlock(&dev_desc->rwlock);
	struct slot_ctx *slot_ctx = dev_desc->sctx;
	if (!dev_desc->enabled) {
		cc = COMPLETION_CODE_SLOT_DISABLED;
		goto exit_unlock;
	} else if (!slot_ctx || slot_ctx->state == XHCI_SLOT_STATE_ENABLED) {
		/* should be in default/addressed/configured state */
		cc = COMPLETION_CODE_CTX_STATE_ERROR;
		goto exit_unlock;
	}

	for (int ep_dci = XHCI_DCI_EPX_CTX(1, 0); ep_dci < XHCI_DCI_MAX; ep_dci++) {
		xhci_ep_disable(&dev_desc->ep_descs[ep_dci]);
	}
	slot_ctx->addr = 0;
	slot_ctx->ctxs = 1;
	slot_ctx->state = XHCI_SLOT_STATE_DEFAULT;

exit_unlock:
	pthread_rwlock_unlock(&dev_desc->rwlock);

	evt_cmd = make_cmd_event(trb_gpa, slot, cc);
	xhci_enqueue_event(xhci, 0, &evt_cmd, true);
}

static void handle_command_noop(struct pci_xhci *xhci, union xhci_trb *trb, uint64_t trb_gpa)
{
	union xhci_trb evt_cmd = make_cmd_event(trb_gpa, 0, COMPLETION_CODE_SUCCESS);
	xhci_enqueue_event(xhci, 0, &evt_cmd, true);
}

static void handle_command_unsupported(struct pci_xhci *xhci, union xhci_trb *trb, uint64_t trb_gpa)
{
	union xhci_trb evt_cmd = make_cmd_event(trb_gpa, 0, COMPLETION_CODE_TRB_ERROR);
	pr_warn("unsupported command %d\n", trb->cmd_noop.type);
	xhci_enqueue_event(xhci, 0, &evt_cmd, true);
}

/* called when enabling transfer ring or cross a new segment */
static void command_ring_map_next_seg(struct xhci_ring *cring)
{
	struct command_ring_priv *priv = xhci_ring_priv(cring);
	struct vmctx *vmctx = cring->xhci->pdev->vmctx;
	uint32_t map_len = XHCI_PAGE_SIZE_4K - (priv->deq_ptr_gpa % XHCI_PAGE_SIZE_4K);
	/* unmap xring->last_map_base */
	priv->last_map_base = dm_vm_map_gpa(vmctx, priv->deq_ptr_gpa, map_len);
	priv->deq_trb = priv->last_map_base;
}

static bool command_ring_handle_trbs(struct xhci_ring *cring)
{
	volatile struct command_ring_priv *ring_priv = xhci_ring_priv(cring);
	struct pci_xhci *xhci = cring->xhci;
	bool progress = false;

	/* ring lock not required because only ring handler is the user of ring metadata
	 * (except cring->stopping) when ring is running.
	 */

	while (ring_priv->deq_trb->common.c == ring_priv->rcs && !cring->stopping) {
		union xhci_trb *trb = ring_priv->deq_trb;
		uint64_t trb_gpa = ring_priv->deq_ptr_gpa;

		xhci_dump_trb(cring, trb, trb_gpa, "handle");

		if (ring_priv->deq_trb->common.type == TRB_TYPE_LINK) {
			uint64_t trb_gpa = ring_priv->deq_ptr_gpa;
			union xhci_trb *trb = ring_priv->deq_trb;

			if (trb->link_trb.tc)
				ring_priv->rcs = !ring_priv->rcs;
			ring_priv->deq_ptr_gpa = trb->link_trb.ring_segment;
			command_ring_map_next_seg(cring); /* deq_trb updated */

			if (trb->link_trb.ioc) {
				union xhci_trb evt = {0};
				evt.event_cmd.type = TRB_TYPE_EVENT_CMD_DONE;
				evt.event_cmd.trb_ptr = trb_gpa;
				evt.event_cmd.cc = COMPLETION_CODE_SUCCESS;
				xhci_enqueue_event(xhci, 0, &evt, true);
			}
			continue;
		}

		switch (trb->common.type) {
		case TRB_TYPE_ENABLE_SLOT:
			handle_command_enable_slot(xhci, trb, trb_gpa);
			break;
		case TRB_TYPE_DISABLE_SLOT:
			handle_command_disable_slot(xhci, trb, trb_gpa);
			break;
		case TRB_TYPE_ADDRESS_DEVICE:
			handle_command_address_device(xhci, trb, trb_gpa);
			break;
		case TRB_TYPE_CONFIG_ENDPOINT:
			handle_command_config_endpoint(xhci, trb, trb_gpa);
			break;
		case TRB_TYPE_EVAL_CONTEXT:
			handle_command_evaluate_contex(xhci, trb, trb_gpa);
			break;
		case TRB_TYPE_RESET_ENDPOINT:
			handle_command_reset_endpoint(xhci, trb, trb_gpa);
			break;
		case TRB_TYPE_STOP_ENDPOINT:
			handle_command_stop_endpoint(xhci, trb, trb_gpa);
			break;
		case TRB_TYPE_SET_TR_DEQUEUE_PTR:
			handle_command_set_tr_deq_ptr(xhci, trb, trb_gpa);
			break;
		case TRB_TYPE_RESET_DEVICE:
			handle_command_reset_device(xhci, trb, trb_gpa);
			break;
		case TRB_TYPE_FORCE_HEADER:
			handle_command_noop(xhci, trb, trb_gpa);
			break;
		case TRB_TYPE_NOOP_CMD:
			handle_command_noop(xhci, trb, trb_gpa);
			break;

		case TRB_TYPE_GET_PORT_BW:
		case TRB_TYPE_NEGO_BW: /* depends on hccparams1.bnc */
		case TRB_TYPE_SET_LATENCY_TOLERANCE: /* dpeends on hccparams1.ltc */
		case TRB_TYPE_FORCE_EVENT:	/* depends on SRIOV support */
		case TRB_TYPE_GET_EXT_PROP:
		case TRB_TYPE_SET_EXT_PROP:
		default:
			handle_command_unsupported(xhci, trb, trb_gpa);
			break;
		}

		progress = true;
		ring_priv->deq_trb++;
		ring_priv->deq_ptr_gpa += sizeof(union xhci_trb);
	}
	return progress;
}

static int command_ring_start(struct xhci_ring *cring)
{
	command_ring_map_next_seg(cring);
	return 0;
}

static void command_ring_stop(struct xhci_ring *cring)
{
	struct command_ring_priv *priv = xhci_ring_priv(cring);
	struct pci_xhci *xhci = cring->xhci;
	union xhci_trb evt_cmd = {0};

	evt_cmd.event_cmd.type = TRB_TYPE_EVENT_CMD_DONE;
	evt_cmd.event_cmd.trb_ptr = priv->deq_ptr_gpa;
	evt_cmd.event_cmd.cc = COMPLETION_CODE_CMD_RING_STOPPED;
	xhci_enqueue_event(xhci, 0, &evt_cmd, true);
	// unmap priv->last_map_base
}

static void command_ring_reset(struct xhci_ring *cring)
{
	struct command_ring_priv *priv = xhci_ring_priv(cring);
	priv->rcs = 0;
	priv->deq_ptr_gpa = 0;
	priv->deq_trb = priv->last_map_base = NULL;
}

static int command_ring_init(struct xhci_ring *cring, void *data)
{
	return 0;
}

struct xhci_ring_ops command_ring_ops = {
	.priv_size = sizeof(struct command_ring_priv),
	.init = command_ring_init,
	.reset = command_ring_reset,
	.start = command_ring_start,
	.stop = command_ring_stop,
	.handle_trb = command_ring_handle_trbs,
};


/* --------------------- ports ------------------- */

/* Generate a Port State Changed Event if some event flag was set */
static void xhci_port_gen_psce(struct pci_xhci *xhci, struct xhci_port *xport)
{
	uint8_t port = (xport - xhci->ports) + 1;
	pr_info("event port status changed, port %d\n", port);

	if (xport->regs.sc & XHCI_PORTSC_EVENT_MASK) {
		/* PSCE is gated if HC Halted */
		if (!(xhci->usbsts & XHCI_OP_USBSTS_HCH)) {
			union xhci_trb psce = {0};
			psce.event_port_changed.type = TRB_TYPE_EVENT_PORT_STATUS_CHANGE;
			psce.event_port_changed.port = port;
			psce.event_port_changed.cc = COMPLETION_CODE_SUCCESS;
			xhci_enqueue_event(xhci, 0, &psce, true);
		} else {
			pr_info("PSCE blocked since HC is Halted!\n");
			xport->psceg_pending = true;
		}
	}
}

/* Called on new device connected, with hub lock */
static void xhci_port_connected(struct pci_xhci *xhci, struct xhci_port *xport)
{
	struct usb_device *dev = xport->port->device;
	enum usb_device_speed speed = dev->backend->speed(dev);
	unsigned xhci_psi = usb_speed_to_xhci_psi(speed);
	unsigned old, new;
	bool gen_psce;

	do {
		new = old = xport->regs.sc;
		gen_psce = false;
		if (!(old & XHCI_OP_PORT_SC_PP)) {
			pr_dbg("but port power not enabled yet!\n");
			return;
		}

		new = (new & ~XHCI_OP_PORT_SC_SPEED_MASK) | xhci_psi;
		new |= XHCI_OP_PORT_SC_CCS | XHCI_OP_PORT_SC_CSC;
		if (!(old & XHCI_OP_PORT_SC_CSC))
			gen_psce = true;

		if (xport->port->hub->usb2) {
			/* Disconnected -> Disabled */
			new = (new & ~XHCI_OP_PORT_SC_PLS_MASK) | XHCI_PLS_POLLING;
		} else {
			/* Disconnected -> Enabled */
			new = (new & ~XHCI_OP_PORT_SC_PLS_MASK) | XHCI_PLS_U0;
			new |= XHCI_OP_PORT_SC_PED;	/* no port reset required for USB3 port */
		}
	} while (!atomic_compare_exchange_strong(&xport->regs.sc, &old, new));

	if (gen_psce)
		xhci_port_gen_psce(xhci, xport);
}

/* Called on new device disconnected. Device Slot will be disabled by driver */
static void xhci_port_disconnected(struct pci_xhci *xhci, struct xhci_port *xport)
{
	unsigned old, new;
	bool gen_psce;

	do {
		new = old = xport->regs.sc;
		gen_psce = false;
		if (!(old & XHCI_OP_PORT_SC_PP)) {
			pr_dbg("but port power not enabled yet!\n");
			return;
		}

		new = (new & ~XHCI_OP_PORT_SC_SPEED_MASK) | XHCI_PSI_UNDEFINED;
		new |= XHCI_OP_PORT_SC_CSC;
		new &= ~(XHCI_OP_PORT_SC_CCS | XHCI_OP_PORT_SC_PED);
		if (!(old & XHCI_OP_PORT_SC_CSC))
			gen_psce = true;

		new = (new & ~XHCI_OP_PORT_SC_PLS_MASK) | XHCI_PLS_RXDETECT;
	} while (!atomic_compare_exchange_strong(&xport->regs.sc, &old, new));

	if (gen_psce)
		xhci_port_gen_psce(xhci, xport);
}

/* reset device and port status */
static void xhci_port_reset_func(struct work *work)
{
	struct xhci_port *xport = container_of(work, struct xhci_port, reset_work);
	struct pci_xhci *xhci = xport->xhci;
	uint8_t port = (xport - xhci->ports) + 1;

	//struct usb_hcd *hcd = usb_get_hcd_from_priv(xhci);
	struct usb_hub *rh = xport->port->hub;
	pthread_mutex_lock(&rh->lock);
	struct usb_device *dev = xport->port->device;
	bool gen_psce;
	unsigned old, new;

	do {
		gen_psce = false;
		new = old = xport->regs.sc;
		new &= ~(XHCI_OP_PORT_SC_PLS_MASK | XHCI_OP_PORT_SC_PED | XHCI_OP_PORT_SC_SPEED_MASK);
		if (dev && !dev->backend->reset(dev)) {
			/* TBD just use port->usb2 flag to set speed ? */
			enum usb_device_speed speed = dev->backend->speed(dev);
			if (usb_device_speed_is_usb2(speed) != xport->port->hub->usb2) {
				pr_err("BUG! Device in speed-incompatible port %d ?", port);
			}
			new |= XHCI_PLS_U0;
			new |= XHCI_OP_PORT_SC_PED;
			new |= usb_speed_to_xhci_psi(speed);
		} else {
			new &= ~XHCI_OP_PORT_SC_CCS;
			new |= XHCI_PLS_RXDETECT;
			new |= XHCI_PSI_UNDEFINED;
			// TBD how to handle reset failed device?
		}

		new &= ~XHCI_OP_PORT_SC_PR;
		if (old & XHCI_OP_PORT_SC_WPR) {
			new &= ~XHCI_OP_PORT_SC_WPR;
			if (!(old & XHCI_OP_PORT_SC_WRC)) {
				new |= XHCI_OP_PORT_SC_WRC;
				gen_psce = true;
			}
		}
		if (!(old & XHCI_OP_PORT_SC_PRC))
			gen_psce = true;
		new |= XHCI_OP_PORT_SC_PRC;
	} while(!atomic_compare_exchange_strong(&xport->regs.sc, &old, new));

	if (gen_psce)
		xhci_port_gen_psce(xhci, xport);

	pthread_mutex_unlock(&rh->lock);
}

static int xhci_init_ports(struct pci_xhci *xhci)
{
	struct usb_hcd *hcd = usb_get_hcd_from_priv(xhci);
	xhci->ports = calloc(xhci->configs.ports, sizeof(struct xhci_port));
	if (!xhci->ports) {
		pr_err("no memory for xhci ports!\n");
		return -1;
	}

	for (int i = 0; i < xhci->configs.usb2_ports; i++) {
		xhci->ports[i].xhci = xhci;
		work_init(&xhci->ports[i].reset_work, xhci_port_reset_func);
		xhci->ports[i].port = &hcd->hub2.ports[i];
		hcd->hub2.ports[i].priv = &xhci->ports[i];
	}

	for (int i = xhci->configs.usb2_ports; i < xhci->configs.ports; i++) {
		xhci->ports[i].xhci = xhci;
		work_init(&xhci->ports[i].reset_work, xhci_port_reset_func);
		xhci->ports[i].port = &hcd->hub3.ports[i - xhci->configs.usb2_ports];
		hcd->hub3.ports[i - xhci->configs.usb2_ports].priv = &xhci->ports[i];
	}

	return 0;
}

static void xhci_deinit_ports(struct pci_xhci *xhci)
{
	for (int i = 0; i < xhci->configs.ports; i++) {
		if (xhci->ports[i].port->device) {
			pr_warn("port %d still has device connected!\n", i);
		}
	}
	free(xhci->ports);
}

static void xhci_port_regs_write(struct pci_xhci *xhci, struct xhci_port *xport,
				uint32_t offset, int width, uint64_t value)
{
	switch (offset) {
	case XHCI_OP_PORT_SC:
	{
		unsigned old, new;
		int port = (xport - xhci->ports) + 1;
		bool reset, check_connect;

		do {
			reset = check_connect = false;
			new = old = xport->regs.sc;
			new &= ~(value & (XHCI_OP_PORT_SC_PP | XHCI_PORTSC_EVENT_MASK | XHCI_PORTSC_WAKE_MASK));
			new |= value & XHCI_PORTSC_WAKE_MASK;
			new |= value & XHCI_OP_PORT_SC_PP;
			if (!(old & XHCI_OP_PORT_SC_PP) && (new & XHCI_OP_PORT_SC_PP)) {
				xport->regs.sc = XHCI_OP_PORT_SC_PP | XHCI_PLS_RXDETECT;
				xport->regs.pmsc = 0;
				xport->regs.li = 0;
				xport->regs.hlpmc = 0;
				check_connect = true;
				continue;
			} else if ((old & XHCI_OP_PORT_SC_PP) && !(new & XHCI_OP_PORT_SC_PP)) {
				unsigned cleared_bits = XHCI_OP_PORT_SC_CCS |
					XHCI_OP_PORT_SC_PED |XHCI_OP_PORT_SC_PR |
					XHCI_OP_PORT_SC_PP | XHCI_OP_PORT_SC_SPEED_MASK |
					XHCI_OP_PORT_SC_PIC_MASK | XHCI_OP_PORT_SC_CAS;
				new &= ~cleared_bits;
				// don't trigger disconnect PSCE on positive port port disable?
				continue;
			} else if (!(old & XHCI_OP_PORT_SC_PP)) {
				pr_warn("can't set portsc when port %d power is disabled!\n", port);
				continue;
			}

			/* with hcd lock hold. Warm reset is deeper but slower than hot reset.
			 * Hot reset implementation can fall back to warm reset */
			if (!(old & XHCI_PORTSC_RESET_MASK) && (value & XHCI_PORTSC_RESET_MASK)) {
				new |= value & XHCI_PORTSC_RESET_MASK;
				new &= ~XHCI_OP_PORT_SC_PED;
				reset = true;
				continue;
			}

			if (value & XHCI_OP_PORT_SC_PED) {
				if (xport->port->hub->usb2) {
					if (old & XHCI_OP_PORT_SC_PED) {
						/* keep connected */
						new &= ~ XHCI_OP_PORT_SC_PED;
						new &= ~XHCI_OP_PORT_SC_PLS_MASK;
						new |= XHCI_PLS_POLLING;
					}
				} else {
					/* usb3 port can be disable from any state except power-off */
					new &= ~ (XHCI_OP_PORT_SC_PED | XHCI_OP_PORT_SC_CCS);
					new &= ~XHCI_OP_PORT_SC_PLS_MASK;
					new |= XHCI_PLS_DISABLED;
				}
				continue;
			}

			if (value & XHCI_OP_PORT_SC_LWS) {
				switch (value & XHCI_OP_PORT_SC_PLS_MASK) {
				case XHCI_PLS_U0:
				case XHCI_PLS_U1:
				case XHCI_PLS_U2:
					/* USB3 auto enter U1/U2 after defined timeout ?*/
				case XHCI_PLS_U3:
				case XHCI_PLS_RESUME:
					// TODO Device Suspend Resume ?
					break;
				case XHCI_PLS_RXDETECT:
					if (!xport->port->hub->usb2 && ((old & XHCI_OP_PORT_SC_PLS_MASK)
								== XHCI_PLS_DISABLED))
					{
						new = (new & ~XHCI_OP_PORT_SC_PLS_MASK) | XHCI_PLS_RXDETECT;
						check_connect = true;
					}
					break;
				default:
					break;
				}
			}
		} while (!atomic_compare_exchange_strong(&xport->regs.sc, &old, new));

		if (reset)
			work_add(&xport->reset_work);
		if (check_connect) {
			struct usb_hub *hub = xport->port->hub;
			pthread_mutex_lock(&hub->lock);
			if (xport->port->device) {
				/* auto connect */
				xhci_port_connected(xhci, xport);
			}
			pthread_mutex_unlock(&hub->lock);
		}

		break;
	}
	case XHCI_OP_PORT_PMSC:
		xport->regs.pmsc = value;
		break;
	case XHCI_OP_PORT_LI:
		xport->regs.li = value;
		break;
	case XHCI_OP_PORT_HLPMC:
		xport->regs.hlpmc = value;
		break;
	}
}

/* --------------------- controller ------------------- */

static void xhci_save_state(struct pci_xhci *xhci)
{
	ering_save_state(xhci->event_primary);
}

static void xhci_restore_state(struct pci_xhci *xhci)
{
	ering_restore_state(xhci->event_primary);
}

static void xhci_reset_func(struct work *work)
{
	struct pci_xhci *xhci = container_of(work, struct pci_xhci, reset_work);
	struct usb_hcd *hcd = usb_get_hcd_from_priv(xhci);

	xhci_ring_reset(xhci->cmd_ring, true);
	/* 
	 * Command ring stopped and can't be enabled until CNR = 0, no race.
	 * Reset slots may generate some events when disabling existing devices.
	 */
	xhci_slot_reset_all(xhci);	
	xhci_ring_reset(xhci->event_primary, true);

	// TODO skip port reset for light HC reset (not supported yet)
	pthread_mutex_lock(&hcd->hub2.lock);
	for (int i = 0; i < hcd->hub2.port_count; i++) {
		struct usb_port *port = &hcd->hub2.ports[i];
		struct xhci_port *xport = port->priv;
		xport->regs.sc = XHCI_OP_PORT_SC_PP | XHCI_PLS_RXDETECT;
		xport->regs.pmsc = 0;
		xport->regs.li = 0;
		xport->regs.hlpmc = 0;
		if (port->device) {
			xhci_port_connected(xhci, xport);
		}
	}
	pthread_mutex_unlock(&hcd->hub2.lock);
	pthread_mutex_lock(&hcd->hub3.lock);
	for (int i = 0; i < hcd->hub3.port_count; i++) {
		struct usb_port *port = &hcd->hub3.ports[i];
		struct xhci_port *xport = port->priv;
		xport->regs.sc = XHCI_OP_PORT_SC_PP | XHCI_PLS_RXDETECT;
		xport->regs.pmsc = 0;
		xport->regs.li = 0;
		xport->regs.hlpmc = 0;
		if (port->device) {
			xhci_port_connected(xhci, xport);
		}
	}
	pthread_mutex_unlock(&hcd->hub3.lock);

	usb_hcd_lock(hcd);
	xhci->dcbaap_gpa = 0;
	xhci->dcbaap = NULL;
	xhci->dnctrl = 0;
	xhci->config = 0;
	//xhci->mfindex = 0;
	xhci->usbsts = XHCI_OP_USBSTS_HCH;	// clear CNR
	xhci->usbcmd = 0;	// clear USBCMD.RESET at last
	usb_hcd_notify_all(hcd);
	usb_hcd_unlock(hcd);
	pr_dbg("xhci reset done!\n");
}

static void xhci_initiate_reset(struct pci_xhci *xhci, enum xhci_reset type, bool wait)
{
	struct usb_hcd *hcd = usb_get_hcd_from_priv(xhci);
	pr_dbg("xHCI %s reset initiated\n", type == POWER_ON_RESET ? "power on" : "hc");

	if (type == LIGHT_RESET) {
		pr_err("light HC reset is not supported yet! Just trigger a HC reset.\n");
	}

	atomic_fetch_or(&xhci->usbcmd, XHCI_OP_USBCMD_HCRST);
	atomic_fetch_or(&xhci->usbsts, XHCI_OP_USBSTS_CNR);	/* in resetting */

	work_add(&xhci->reset_work);

	if (wait) {
		usb_hcd_lock(hcd);
		while(xhci->usbcmd & XHCI_OP_USBCMD_HCRST) {
			usb_hcd_wait(hcd);
		}
		usb_hcd_unlock(hcd);
	}
}

static void xhci_stop_func(struct work *work)
{
	struct pci_xhci *xhci = container_of(work, struct pci_xhci, stop_work);
	struct usb_hcd *hcd = usb_get_hcd_from_priv(xhci);

	xhci_ring_stop(xhci->cmd_ring, true);
	xhci_ring_stop(xhci->event_primary, true);
	/* TODO initiate stopping transfer rings */

	usb_hcd_lock(hcd);
	atomic_fetch_or(&xhci->usbsts, XHCI_OP_USBSTS_HCH);
	usb_hcd_notify_all(hcd);
	usb_hcd_unlock(hcd);
	pr_dbg("HC stopped!\n");
}

/* must be called with hcd lock hold */
static void xhci_initiate_stop(struct pci_xhci *xhci, bool wait)
{
	struct usb_hcd *hcd = usb_get_hcd_from_priv(xhci);

	pr_dbg("HC stop initiated!\n");

	if (xhci->usbsts & XHCI_OP_USBSTS_HCH)
		return;

	xhci->stopping = true;

	work_add(&xhci->stop_work);

	if (wait) {
		while(!(xhci->usbsts & XHCI_OP_USBSTS_HCH)) {
			usb_hcd_wait(hcd);
		}
	}
}

static void xhci_start(struct pci_xhci *xhci)
{
	if (xhci_ring_start(xhci->event_primary)) {
		pr_err("failed to start primary event ring!\n");
		atomic_fetch_or(&xhci->usbcmd, ~XHCI_OP_USBCMD_RS);
		return;
	}

	/* send gated PSCE */
	for (int i = 0; i < xhci->configs.ports; i++) {
		struct xhci_port *xport = &xhci->ports[i];
		if (xport->psceg_pending) {
			xport->psceg_pending = false;
#if 0 /* TBD */
			union xhci_trb psce;
			psce.event_port_changed.type = TRB_TYPE_EVENT_PORT_STATUS_CHANGE;
			psce.event_port_changed.port = i + 1;
			psce.event_port_changed.cc = COMPLETION_CODE_SUCCESS;
			xhci_enqueue_event(xhci, 0, &psce, true);
			pr_dbg("send PSCE for port %d\n", i+1);
#endif
		}
	}

	atomic_fetch_and(&xhci->usbsts, ~XHCI_OP_USBSTS_HCH);
	pr_dbg("HC started!\n");
}

static int xhci_init_caps(struct pci_xhci *xhci)
{
	struct xhci_hw_configs *configs = &xhci->configs;
	struct xhci_caps *caps = &xhci->caps;
	uint64_t pgsz = sysconf(_SC_PAGESIZE);

	xhci->opregs.len = XHCI_OP_PORT_START + sizeof(struct xhci_port_regs) * configs->ports;
	xhci->rtregs.len = XHCI_RTS_INT_START + sizeof(struct interrupter) * configs->maxintrs;
	/* one extra doorbell (0) for command ring */
	xhci->dbregs.len = sizeof(union doorbell) * (configs->maxslots + 1);
	xhci->opregs.off = ACRN_XHCI_CAPLEN;
	xhci->rtregs.off = ALIGN_UP(xhci->opregs.off + xhci->opregs.len, pgsz);
	xhci->dbregs.off = ALIGN_UP(xhci->rtregs.off + xhci->rtregs.len, pgsz);
	caps->hccparams1.xecp = ALIGN_UP(xhci->dbregs.off + xhci->dbregs.len, pgsz) / 4;
	uint32_t extcaps_end = xhci_add_extcaps(xhci,
		ALIGN_UP(xhci->dbregs.off + xhci->dbregs.len, pgsz));
	xhci->bar_len = fnp2(extcaps_end);
	pr_dbg("%s: dboff %x@%x, rtsoff %x@%x, extcap@%x, barsize %x\n",
			hcd_name(usb_get_hcd_from_priv(xhci)),
			xhci->dbregs.len, xhci->dbregs.off,
			xhci->rtregs.len, xhci->rtregs.off,
			xhci->caps.hccparams1.xecp * 4, xhci->bar_len);

	caps->header.caplength = ACRN_XHCI_CAPLEN;
	caps->header.hciversion = xhci->configs.xhci_version;
	caps->hcsparams1.maxslots = xhci->configs.maxslots;
	caps->hcsparams1.maxintrs = xhci->configs.maxintrs;
	caps->hcsparams1.maxports = xhci->configs.ports;
	caps->hcsparams2.ist = ACRN_XHCI_IST;
	caps->hcsparams2.erst_max = xhci->configs.maxerst;
	caps->hcsparams3.dword = 0;
	caps->hccparams1.nss = 1;	/* No Secondary Stream Context Array */
	caps->hccparams1.spc = 0;	/* Stopped - Short Packet completion code support */
	caps->hccparams1.max_psa_size = xhci->configs.maxpsa;
	caps->doorbell_offset = xhci->dbregs.off;
	caps->runtime_regs_offset = xhci->rtregs.off;
	//caps->hccparams2.lec = 1; TBD hccparams2 depends on xhci 1.1?

	return 0;
}

static int xhci_caps_read(struct pci_xhci *xhci, uint32_t offset, int width, uint64_t *val)
{
	/* byte/word/dword read allowed */
	if (offset + width <= sizeof(struct xhci_caps)) {
		*val = *(uint64_t *)((char *)&xhci->caps + offset);
		*val <<= (8 - width) * 8;
		*val >>= (8 - width) * 8;
		pr_dbg("read%c cap reg %x: %x\n", width2char(width), offset, *val);
		return 0;
	}
	return -1;
}

static uint64_t xhci_opregs_read(struct pci_xhci *xhci, uint32_t offset, int width)
{
	uint64_t value = 0;

	if (width != 4) {
		// TBD allow other widths? qword shall be allowed if adding 64-bit support
		pr_warn("invalid width %d to read operational registers\n", width);
		return 0;
	}

	switch (offset) {
	case XHCI_OP_USBCMD:
		value = xhci->usbcmd;
		break;
	case XHCI_OP_USBSTS:
		value = xhci->usbsts;
		break;
	case XHCI_OP_PAGESZ:
	{
		value = XHCI_PAGE_SIZE_4K;
		break;
	}
	case XHCI_OP_DNCTRL:
		value = xhci->dnctrl;
		break;
	case XHCI_OP_CRCR:
	{
		/* Bits other that CRR are always 0 on read */
		value = xhci->cmd_ring->running ? XHCI_OP_CRCR_CRR : 0;
		break;
	}
	case XHCI_OP_CRCR + 4:
		value = 0;
		break;
	case XHCI_OP_DCBAAP:
	{
		value = xhci->dcbaap_gpa & XHCI_OP_DCBAAP_MASK;
		break;
	}
	case XHCI_OP_DCBAAP + 4:
		value = 0;
		break;
	case XHCI_OP_CONFIG:
		value = xhci->config;
		break;
	default:
		if (offset >= XHCI_OP_PORT_START && (offset + width <= XHCI_OP_PORT_START +
				sizeof(struct xhci_port_regs) * xhci->configs.ports)) {
			int port = (offset - XHCI_OP_PORT_START) / sizeof(struct xhci_port_regs);
			struct xhci_port *xport = &xhci->ports[port];
			switch (offset % sizeof(struct xhci_port_regs)) {
			case XHCI_OP_PORT_SC:
				value = xport->regs.sc;
				break;
			case XHCI_OP_PORT_PMSC:
				value = xport->regs.pmsc;
				break;
			case XHCI_OP_PORT_LI:
				value = xport->regs.li;
				break;
			case XHCI_OP_PORT_HLPMC:
				value = xport->regs.hlpmc;
				break;
			default:
				pr_warn("non word-aligned port register access: %d@%x\n", width, offset);
			}
			break;
		} else {
			pr_warn("read%c hole %x in operation register space?\n", width2char(width), offset);
		}
	}

	pr_dbg("read%c op reg %x: %x\n", width2char(width), offset, value);
	return value;
}

static void xhci_op_regs_write(struct pci_xhci *xhci, uint32_t offset, int width, uint64_t value)
{
	if (width != 4) {
		// TODO allow 64-bit
		pr_warn("invalid width %d to write operational registers\n", width);
		return;
	}
	pr_dbg("write%c op reg %x: %x\n", width2char(width), offset, value);

	if ((xhci->usbsts & XHCI_OP_USBSTS_CNR) && offset != XHCI_OP_USBSTS) {
		pr_dbg("controller not ready, ignored");
		return;
	}

	switch (offset) {
	case XHCI_OP_USBCMD:
	{
		/* Warn for understood but unsupported features */
		if (value & XHCI_OP_USBCMD_CME) {
			pr_warn("can't enable cme when hccparams2.cmc = 0\n");
			value &= ~XHCI_OP_USBCMD_CME;
		}
		if (value & XHCI_OP_USBCMD_ETE) {
			pr_warn("can't enable etc when hccparams2.etc = 0\n");
			value &= ~XHCI_OP_USBCMD_ETE;
		}
		if (value & XHCI_OP_USBCMD_TSCEN) {
			pr_warn("can't enable tsc_en when hccparams2.etc_tsc = 0\n");
			value &= ~XHCI_OP_USBCMD_TSCEN;
		}
		if (value & XHCI_OP_USBCMD_VTIOE) {
			pr_warn("can't enable vtio when hccparams2.vtc = 0\n");
			value &= ~XHCI_OP_USBCMD_VTIOE;
		}
		if (value & XHCI_OP_USBCMD_LHCRST) {
			pr_warn("light reset not supported yet!\n");
			value &= ~XHCI_OP_USBCMD_LHCRST;
		}

		if (xhci->usbsts & XHCI_OP_USBSTS_HCH) {
			uint32_t old = xhci->usbcmd;
			/* register update done */
			xhci->usbcmd = value & ~(XHCI_OP_USBCMD_CSS | XHCI_OP_USBCMD_CRS);

			/* handle quick actions synchronously. */
			if (value & XHCI_OP_USBCMD_CSS)
				xhci_save_state(xhci);
			if (value & XHCI_OP_USBCMD_CRS)
				xhci_restore_state(xhci);
			if (value & XHCI_OP_USBCMD_RS)
				xhci_start(xhci);
			if (!(old & XHCI_OP_USBCMD_HCRST) &&
					(value & XHCI_OP_USBCMD_HCRST))
				xhci_initiate_reset(xhci, HC_RESET, false);
		} else {
			xhci->usbcmd = value & ~(XHCI_OP_USBCMD_CSS | XHCI_OP_USBCMD_CRS | XHCI_OP_USBCMD_HCRST);
			if (!(value & XHCI_OP_USBCMD_RS))
				xhci_initiate_stop(xhci, false);
		}
		break;
	}
	case XHCI_OP_USBSTS:
		atomic_fetch_and(&xhci->usbsts, ~(value & XHCI_USBSTS_EVT_MASK));
		break;
	case XHCI_OP_PAGESZ:	/* RO */
		break;
	case XHCI_OP_DNCTRL:
		/* GAP. No way to generate Device Notification Event yet */
		xhci->dnctrl = value;
		break;
	case XHCI_OP_CRCR:
	{
		struct xhci_ring *cring = xhci->cmd_ring;
		struct command_ring_priv *priv = xhci_ring_priv(cring);
		pthread_mutex_lock(&cring->lock);
		if (cring->running) {
			if (value & XHCI_OP_CRCR_CA) {
				xhci_ring_stop_locked(xhci->cmd_ring, true);
			} else if (value & XHCI_OP_CRCR_CS) {
				xhci_ring_stop_locked(xhci->cmd_ring, true);
			}
		} else {
			/* changeable when command ring is stopped */
			priv->rcs = !!(value & XHCI_OP_CRCR_RCS);
			priv->deq_ptr_gpa = value & XHCI_OP_CRCR_PTR_MASK;
		}
		pthread_mutex_unlock(&cring->lock);
		break;
	}
	case XHCI_OP_CRCR + 4:
		/* 64-bit not supported yet */
		break;
	case XHCI_OP_DCBAAP:
		/* could change only when no device context enabled */
		/* mapped on first device context enabling */
		if (xhci->dcbaap) {
			pr_warn("can't set DCBAAP since it was already mapped!\n");
		}
		xhci->dcbaap_gpa = (uint32_t)(value & ~0x3F);
		break;
	case XHCI_OP_DCBAAP + 4:
		/* 64-bit not supported yet */
		break;
	case XHCI_OP_CONFIG:	/* this can be changed even when HC is running */
	{
		unsigned oldconfig = xhci->config;
		xhci->config = value;
		if ((xhci->config & XHCI_OP_CONFIG_MAXSLOTS_MASK) !=
				(oldconfig & XHCI_OP_CONFIG_MAXSLOTS_MASK))
			xhci_select_slots(xhci);
		/* u3e, cie are rsvdp since no u3c, cic support */
		break;
	}
	default:
		if (offset >= XHCI_OP_PORT_START &&
				offset + width <= XHCI_OP_PORT_START +
				sizeof(struct xhci_port_regs) * xhci->configs.ports) {
			int port = (offset - XHCI_OP_PORT_START) / sizeof(struct xhci_port_regs);
			struct xhci_port *xport = &xhci->ports[port];
			xhci_port_regs_write(xhci, xport,
					offset % sizeof(struct xhci_port_regs), width, value);
			break;
		} else {
			pr_warn("write%c hole %x in operation register space?\n",
					width2char(width), offset);
		}
	}
}

static uint64_t xhci_rt_regs_read(struct pci_xhci *xhci, uint32_t offset, int width)
{
	uint64_t value = 0;

	if (offset == XHCI_RTS_MFINDEX) {
		xhci->mfindex = xhci_get_mfindex(xhci);
		value = xhci->mfindex;
		pr_dbg("read%c mfindex %x\n", width2char(width), value);
	} else if (offset >= XHCI_RTS_INT_START) {
		uint32_t new_offset = offset - XHCI_RTS_INT_START;
		int idx = new_offset / sizeof(struct interrupter);
		if (idx == 0) {
			/* for now, only one interrupter is supported */
			struct xhci_ring *primary = xhci->event_primary;
			struct event_ring_priv *priv = xhci_ring_priv(primary);
			struct interrupter *intr = &priv->intr;
			value = *(uint64_t *)((char *)intr + new_offset % sizeof(struct interrupter));
			value <<= (8 - width) * 8;
			value >>= (8 - width) * 8;
		}
		pr_dbg("read%c intr%d reg %x: %x\n", width2char(width), idx, new_offset, value);
	} else {
		pr_warn("read%c hole %x in runtime register space?\n", width2char(width), offset);
	}

	return value;
}

static void xhci_rt_regs_write(struct pci_xhci *xhci, uint32_t offset, int width, uint64_t value)
{
	if (offset >= XHCI_RTS_INT_START) {
		uint32_t new_offset = offset - XHCI_RTS_INT_START;
		int idx = new_offset / sizeof(struct interrupter);
		if (idx == 0) {
			struct xhci_ring *primary = xhci->event_primary;
			ering_regs_write(primary, new_offset % sizeof(struct interrupter), width, value);
			return;
		}
	}
	pr_warn("rt register write ignored: %x -> %d@%x\n", value, width, offset);
}

static void xhci_db_regs_write(struct pci_xhci *xhci, uint32_t offset, int width, uint64_t value)
{
	if ((width != 4) || (offset % 4)) {
		pr_warn("invalid offset %x or width %d for doorbell write\n", offset, width);
		return;
	}

	uint32_t slot = offset / 4;
	union doorbell new;
	new.dword = value;
	pr_dbg("doorbell %d, target: %d, stream: %d\n", slot, new.target, new.stream);

	if ((xhci->usbsts & (XHCI_OP_USBSTS_CNR | XHCI_OP_USBSTS_HCH)) ||
			!(xhci->usbcmd & XHCI_OP_USBCMD_RS)) {
		pr_warn("ignore doorbell because xHC is busy or stopped or halted\n");
		return;
	}

	if (slot == 0) {
		if (xhci_ring_start(xhci->cmd_ring)) // can also used to notify
			pr_err("failed to start command ring\n");
	} else {
		uint8_t ep_dci = new.target;
		uint16_t stream = new.stream;
		if (ep_dci < XHCI_DCI_EP0_CTX || ep_dci >= XHCI_DCI_MAX) {
			pr_err("invalid doorbell target: %d\n", ep_dci);
			return;
		}

		struct devctx_desc *dev_desc = &xhci->ctx_descs[slot];
		struct epctx_desc *ep_desc = &dev_desc->ep_descs[ep_dci];
		if ((ep_desc->streams && (!stream || stream >= ep_desc->streams)) ||
				(!ep_desc->streams && stream)) {
			pr_err("invalid stream: %d\n", stream);
			return;
		}

		pthread_rwlock_rdlock(&dev_desc->rwlock);
		struct slot_ctx *slot_ctx = dev_desc->sctx;
		struct ep_ctx *ep_ctx = ep_desc->epctx;

		if (!dev_desc->enabled || !slot_ctx ||
				(slot_ctx->state == XHCI_SLOT_STATE_DISABLED)) {
			pr_info("doorbell ignored because of slot %d not enabled\n", slot);
			goto slot_db_done;
		}

		if (!ep_desc->enabled || !(ep_ctx->state == XHCI_EP_STATE_STOPPED ||
				ep_ctx->state == XHCI_EP_STATE_RUNNING)) {
			pr_info("doorbell ignored because of invalid endpoint state: %d\n",
					ep_ctx->state);
			goto slot_db_done;
		}
		ep_ctx->state = XHCI_EP_STATE_RUNNING;	// move to ring->ops->start ?

		if (ep_desc->streams){
			/* start and notify */
			if (xhci_ring_start(ep_desc->stream_descs[stream].xring))
				pr_err("failed to start ep(dci) %d, stream %d, xring\n", ep_dci, stream);
		} else {
			if (xhci_ring_start(ep_desc->xring))
				pr_err("failed to start xring for ep(dci) %d\n", ep_dci);
		}
slot_db_done:
		pthread_rwlock_unlock(&dev_desc->rwlock);
		return;
	}
}

static uint64_t pci_xhci_read(struct vmctx *ctx, int vcpu, struct pci_vdev *dev,
	      int baridx, uint64_t offset, int width)
{
	uint64_t value;
	struct pci_xhci *xhci = dev->arg;
	struct range op = { offset, width };

	if (offset + width <= xhci->caps.header.caplength) {
		if (!xhci_caps_read(xhci, offset, width, &value))
			return value;

		// allow extended capabilities in cap region
	}

	if (range_in(op, xhci->opregs)) {
		value = xhci_opregs_read(xhci, offset - xhci->opregs.off, width);
	} else if (range_in(op, xhci->rtregs)) {
		value = xhci_rt_regs_read(xhci, offset - xhci->rtregs.off, width);
	} else if (range_in(op, xhci->dbregs)) {
		/* read doorbell to ensure previous doorbell write completed */
		value = 0;
	} else if (xhci_extcap_read(xhci, offset, width, &value)) {
		value = 0;
		pr_warn("read%c hole %x in xHCI register space?\n", width2char(width), offset);
	}

	return value;
}

/* 
 * This should be accessed from different threads. Although current ACRM-DM only support one vCPU
 * thread to handle all IO requests.
 */
static void pci_xhci_write(struct vmctx *ctx, int vcpu, struct pci_vdev *dev,
	       int baridx, uint64_t offset, int width, uint64_t value)
{
	struct pci_xhci *xhci = dev->arg;
	struct range op = { offset, width };

	if (range_in(op, xhci->opregs)) {
		xhci_op_regs_write(xhci, offset - xhci->opregs.off, width, value);
	} else if (range_in(op, xhci->rtregs)) {
		xhci_rt_regs_write(xhci, offset - xhci->rtregs.off, width, value);
	} else if (range_in(op, xhci->dbregs)) {
		xhci_db_regs_write(xhci, offset - xhci->dbregs.off, width, value);
	} else if (xhci_extcap_write(xhci, offset, width, value)) {
		pr_warn("write%c hole %x in xHCI register space?\n", width2char(width), offset);
	}
}

static int pci_xhci_parse_opts(struct xhci_hw_configs *configs, char *opts)
{
	char *seg;

	configs->model = PCI_XHCI_ACRN;
	configs->xhci_version = ACRN_XHCI_VERSION;
	configs->page_size_mask = sysconf(_SC_PAGESIZE);
	configs->usb3_ports = ACRN_XHCI_USB3_PORTS;
	configs->usb2_ports = ACRN_XHCI_USB2_PORTS;
	configs->ports = configs->usb3_ports + configs->usb2_ports;
	configs->maxslots = ACRN_XHCI_DEVICE_SLOTS;
	configs->maxintrs = ACRN_XHCI_INTERRUPTERS;
	configs->maxerst = ACRN_XHCI_ERST_MAX;
	configs->maxpsa = ACRN_XHCI_PSA_MAX;
	memset(configs->device_opts, 0, sizeof(configs->device_opts));
	configs->device_num = 0;

	while ((seg = strsep(&opts, ",:"))) {
		if (!strcmp(seg, "cap=apl")) {
			configs->model = PCI_XHCI_APL;
		} else if (!strncmp(seg, "drd=", 4)) {
			const char *opt = seg + 4;
			uint8_t bus;
			struct usb_devpath path;
			if (usb_parse_bus_devpath_str(opt, &bus, &path) ||
					(path.port[0] == 0 || path.port[1] != 0)) {
				pr_err("invalid drd option: '%s'\n", seg);
				return -1;
			}
			configs->drd_bus = bus;
			configs->drd_port = path.port[0];
		} else {
			configs->device_opts[configs->device_num++] = seg;
		}
	}

	return 0;
}

/* called from pmapper/backend threads, with hub lock hold */
static void xhci_on_device_add(struct usb_hub *hub, uint8_t port_idx)
{
	struct pci_xhci *xhci = usb_hcd_get_priv(hub->hcd);
	port_idx += hub->usb2 ? 0 : xhci->configs.usb2_ports;
	struct xhci_port *xport= &xhci->ports[port_idx];

	pr_info("new device connected to %s port %d\n", hcd_name(hub->hcd), port_idx + 1);

	xhci_port_connected(xhci, xport);
}

/* called from pmapper/backend threads, with hub lock hold */
static void xhci_on_device_del(struct usb_hub *hub, uint8_t port_idx)
{
	struct pci_xhci *xhci = usb_hcd_get_priv(hub->hcd);
	port_idx += hub->usb2 ? 0 : xhci->configs.usb2_ports;
	struct xhci_port *xport= &xhci->ports[port_idx];

	pr_info("device disconnected from %s port %d\n", hcd_name(hub->hcd), port_idx + 1);

	xhci_port_disconnected(xhci, xport);
}

static int pci_xhci_init(struct vmctx *ctx, struct pci_vdev *dev, char *opts)
{
	struct xhci_hw_configs configs;
	pci_xhci_parse_opts(&configs, opts);

	char name[NAME_MAX];
	snprintf(name, NAME_MAX, "%02x:%02x.%01x", dev->bus, dev->slot, dev->func);
	struct usb_hcd *hcd = usb_hcd_alloc(sizeof(struct pci_xhci), configs.usb2_ports, configs.usb3_ports, name);
	if (!hcd) {
		return -1;
	}

	hcd->xfer_priv_size = sizeof(struct xhci_usb_transfer_priv);
	hcd->hub2.on_device_add = hcd->hub3.on_device_add = xhci_on_device_add;
	hcd->hub2.on_device_del = hcd->hub3.on_device_del = xhci_on_device_del;

	struct pci_xhci *xhci = usb_hcd_get_priv(hcd);
	xhci->pdev = dev;
	dev->arg = xhci;
	xhci->configs = configs;

	if (xhci->configs.model == PCI_XHCI_APL) {
		xhci->pid = PCI_INTEL_APL_XHCI_PID;
		xhci->vid = PCI_INTEL_APL_XHCI_VID;
	} else {
		xhci->pid = PCI_ACRN_XHCI_PID;
		xhci->vid = PCI_ACRN_XHCI_VID;
	}

	if (xhci_init_extcaps(xhci)) {
		pr_err("failed to initialize extended capabilities\n");
		goto free_hcd;
	}

	if (xhci_init_caps(xhci)) {
		pr_err("failed to initialize static capabilities\n");
		goto destroy_extcaps;
	}

	if (xhci_init_ports(xhci)) {
		pr_err("failed to initialize ports\n");
		goto destroy_extcaps;
	}

	// postpone until driver set config.maxslots ?
	if (xhci_init_slots(xhci)) {
		pr_err("failed to initialize device slots\n");
		goto deinit_ports;
	}

	xhci->event_primary = xhci_ring_create(xhci, &xhci_event_ring_ops, "xhci-evt", 0, NULL);
	xhci->cmd_ring = xhci_ring_create(xhci, &command_ring_ops, "xhci-cmd", 0, NULL);
	if (!xhci->event_primary || !xhci->cmd_ring) {
		pr_err("failed to create basic rings\n");
		goto deinit_slots;
	}

	work_init(&xhci->reset_work, xhci_reset_func);
	work_init(&xhci->stop_work, xhci_stop_func);

	xhci_initiate_reset(xhci, POWER_ON_RESET, true);

	if (clock_gettime(CLOCK_MONOTONIC, &xhci->init_time)) {
		pr_err("failed to get clock time\n");
		goto deinit_rings;
	}
	CIRCLEQ_INIT(&xhci->sched_rings);
	pthread_mutex_init(&xhci->sched_lock, NULL);
	xhci->sched.clockid = CLOCK_MONOTONIC;
	if (acrn_timer_init(&xhci->sched, sched_timer_handler, xhci)) {
		pr_err("failed to create sched timer\n");
		goto deinit_timer;
	}

	pci_set_cfgdata16(dev, PCIR_DEVICE, xhci->pid);
	pci_set_cfgdata16(dev, PCIR_VENDOR, xhci->vid);
	pci_set_cfgdata8(dev, PCIR_CLASS, PCIC_SERIALBUS);
	pci_set_cfgdata8(dev, PCIR_SUBCLASS, PCIS_SERIALBUS_USB);
	pci_set_cfgdata8(dev, PCIR_PROGIF, PCIP_SERIALBUS_USB_XHCI);
	pci_set_cfgdata8(dev, PCI_USBREV, PCI_USB_REV_3_0);

	pci_emul_add_msicap(dev, 1);

	pci_emul_alloc_bar(dev, 0, PCIBAR_MEM32, xhci->bar_len);

	pci_lintr_request(dev);

	if (usb_hcd_register(hcd)) {
		pr_err("failed to register xhci hcd!\n");
		goto deinit_timer;
	}

	for (int i = 0; i < xhci->configs.device_num; i++) {
		const char *opt = xhci->configs.device_opts[i];
		uint8_t bus;
		struct usb_devpath path;
		if (usb_parse_bus_devpath_str(opt, &bus, &path)) {
			pr_err("invalid device option: '%s'\n", opt);
			goto unregister;
		}

		usb_pmapper_add(hcd, i+1, bus, path);
	}

	return 0;

unregister:
	usb_hcd_unregister(hcd);
deinit_timer:
	acrn_timer_deinit(&xhci->sched);
deinit_rings:
	xhci_ring_destroy(xhci->cmd_ring, true);
	xhci_ring_destroy(xhci->event_primary, true);
deinit_slots:
	xhci_deinit_slots(xhci);
deinit_ports:
	xhci_deinit_ports(xhci);
destroy_extcaps:
	xhci_destroy_extcaps(xhci);
free_hcd:
	usb_hcd_destroy(hcd);
	return -1;
}

static void pci_xhci_deinit(struct vmctx *ctx, struct pci_vdev *dev, char *opts)
{
	struct pci_xhci *xhci = dev->arg;
	struct usb_hcd *hcd = usb_get_hcd_from_priv(xhci);

	usb_pmapper_del(hcd);
	xhci_slot_reset_all(xhci);
	usb_hcd_unregister(hcd);
	xhci_ring_destroy(xhci->cmd_ring, true);
	xhci_ring_destroy(xhci->event_primary, true);
	xhci_deinit_slots(xhci);
	xhci_deinit_ports(xhci);
	xhci_destroy_extcaps(xhci);
	usb_hcd_destroy(hcd);
}

struct pci_vdev_ops pci_ops_xhci = {
	.class_name	= "xhci",
	.vdev_init	= pci_xhci_init,
	.vdev_deinit	= pci_xhci_deinit,
	.vdev_barwrite	= pci_xhci_write,
	.vdev_barread	= pci_xhci_read,
};
DEFINE_PCI_DEVTYPE(pci_ops_xhci);
