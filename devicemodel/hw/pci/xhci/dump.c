#include "xhciregs.h"
#include <linux/limits.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <sys/queue.h>
#include <unistd.h>

#define pr_prefix "usb: xhci: "
#include "log.h"
#include "xhci.h"
#include "types.h"

void xhci_dump_trb(struct xhci_ring *ring, const union xhci_trb *trb, uint64_t trb_gpa, const char *comment)
{
	switch (trb->common.type) {
	/* transfer ring trbs */
	case TRB_TYPE_NORMAL:
		pr_dbg("%s: %s xfer normal 0x%lx, buf: 0x%lx, len: %d, tdsize: %d, intr: %d,"
				" ent: %d, isp: %d, ch: %d, ioc: %d, idt: %d, bei: %d\n",
				ring->name, comment, trb_gpa,
				trb->xfer_normal.buffer, trb->xfer_normal.len,
				trb->xfer_normal.tdsize, trb->xfer_normal.intr,
				trb->xfer_normal.ent, trb->xfer_normal.isp, trb->xfer_normal.ch,
				trb->xfer_normal.ioc, trb->xfer_normal.idt, trb->xfer_normal.bei);
		break;
	case TRB_TYPE_SETUP_STAGE:
		pr_dbg("%s: %s xfer setup 0x%lx, bmRequestType: 0x%x, bRequest: %d, wValue: %d,"
				" wIndex: %d, wLength: %d, trb_len: %d, intr: %d, ioc: %d,"
				" idt: %d, trt: %d\n", ring->name, comment, trb_gpa,
				trb->xfer_ctrl_setup.bmRequestType, trb->xfer_ctrl_setup.bRequest,
				trb->xfer_ctrl_setup.wValue, trb->xfer_ctrl_setup.wIndex,
				trb->xfer_ctrl_setup.wLength, trb->xfer_ctrl_setup.len,
				trb->xfer_ctrl_setup.intr, trb->xfer_ctrl_setup.ioc,
				trb->xfer_ctrl_setup.idt, trb->xfer_ctrl_setup.trt);
		break;
	case TRB_TYPE_DATA_STAGE:
		pr_dbg("%s: %s xfer data 0x%lx, gpa: 0x%lx, len: %d, tdsize: %d, ent: %d, isp: %d,"
				" ch: %d, ioc: %d, idt: %d, dir: %d\n",
				ring->name, comment, trb_gpa,
				trb->xfer_ctrl_data.buffer, trb->xfer_ctrl_data.len,
				trb->xfer_ctrl_data.tdsize, trb->xfer_ctrl_data.ent,
				trb->xfer_ctrl_data.isp, trb->xfer_ctrl_data.ch,
				trb->xfer_ctrl_data.ioc, trb->xfer_ctrl_data.idt,
				trb->xfer_ctrl_data.dir);
		break;
	case TRB_TYPE_STATUS_STAGE:
		pr_dbg("%s: %s xfer status 0x%lx, intr: %d, ent: %d, ch: %d, dir: %d\n",
				ring->name, comment, trb_gpa,
				trb->xfer_ctrl_status.intr, trb->xfer_ctrl_status.ent,
				trb->xfer_ctrl_status.ch, trb->xfer_ctrl_status.dir);
		break;
	case TRB_TYPE_ISOCH:
		pr_dbg("%s: %s xfer isoc 0x%lx, buf: 0x%lx, len: %d, tdsz_tbc: %d, intr: %d,"
				" ent: %d, isp: %d, ch: %d, ioc: %d, idt: %d, bei: %d, tlbpc: %d,"
				" frame: %d, asap: %d\n", ring->name, comment, trb_gpa,
				trb->xfer_isoc.buffer, trb->xfer_isoc.len, trb->xfer_isoc.tdsz_tbc,
				trb->xfer_isoc.intr, trb->xfer_isoc.ent, trb->xfer_isoc.isp,
				trb->xfer_isoc.ch, trb->xfer_isoc.ioc, trb->xfer_isoc.idt,
				trb->xfer_isoc.bei, trb->xfer_isoc.tlbpc, trb->xfer_isoc.frame_id,
				trb->xfer_isoc.asap);
		break;
	case TRB_TYPE_LINK:
		pr_dbg("%s: %s link 0x%lx, next_seg: 0x%lx, intr: %d, tc: %d, ch: %d, ioc: %d\n",
				ring->name, comment, trb_gpa, trb->link_trb.ring_segment,
				trb->link_trb.intr, trb->link_trb.tc, trb->link_trb.ch,
				trb->link_trb.ioc);
		break;
	case TRB_TYPE_EVENT_DATA:
		pr_dbg("%s: %s xfer event 0x%lx, data: 0x%lx, intr: %d, ent: %d, ch: %d, ioc: %d,"
				" bei: %d\n", ring->name, comment, trb_gpa,
				trb->event_data_trb.event_data, trb->event_data_trb.intr,
				trb->event_data_trb.ent, trb->event_data_trb.ch,
				trb->event_data_trb.ioc, trb->event_data_trb.bei);
		break;
	case TRB_TYPE_NOOP_XFER:
		pr_dbg("%s: %s xfer noop 0x%lx, ent: %d, ch: %d, ioc: %d\n",
				ring->name, comment, trb_gpa,
				trb->xfer_noop.ent, trb->xfer_noop.ch, trb->xfer_noop.ioc);
		break;
	/* command ring trbs */
	case TRB_TYPE_ENABLE_SLOT:
		pr_dbg("%s: %s cmd enable slot 0x%lx, slot type: %d\n",
				ring->name, comment, trb_gpa, trb->cmd_enable_slot.slot_type);
		break;
	case TRB_TYPE_DISABLE_SLOT:
		pr_dbg("%s: %s cmd disable slot 0x%lx, slot: %d\n",
				ring->name, comment, trb_gpa, trb->cmd_disable_slot.slot_id);
		break;
	case TRB_TYPE_ADDRESS_DEVICE:
		pr_dbg("%s: %s cmd address device 0x%lx, slot: %d, bsr: %d, inctxs_gpa: 0x%lx\n",
				ring->name, comment, trb_gpa,
				trb->cmd_address_device.slot_id, trb->cmd_address_device.bsr,
				trb->cmd_address_device.input_ctx_ptr);
		break;
	case TRB_TYPE_CONFIG_ENDPOINT:
		pr_dbg("%s: %s cmd config endpoint 0x%lx, input_ctx_gpa: 0x%lx, deconfig: %d,"
				" slot: %d\n", ring->name,
				comment, trb_gpa, trb->cmd_config_endpoint.input_ctx_ptr,
				trb->cmd_config_endpoint.deconfig,
				trb->cmd_config_endpoint.slot_id);
		break;
	case TRB_TYPE_EVAL_CONTEXT:
		pr_dbg("%s: %s cmd evaluate context 0x%lx, slot: %d, inctxs_gpa: 0x%lx\n",
				ring->name, comment, trb_gpa,
				trb->cmd_eval_context.slot_id, trb->cmd_eval_context.input_ctx_ptr);
		break;
	case TRB_TYPE_RESET_ENDPOINT:
		pr_dbg("%s: %s cmd reset endpoint 0x%lx, slot: %d, ep_dci: %d,"
				" xfer_state_preserve: %d\n", ring->name, comment, trb_gpa,
				trb->cmd_reset_endpoint.slot, trb->cmd_reset_endpoint.endpoint,
				trb->cmd_reset_endpoint.tsp);
		break;
	case TRB_TYPE_STOP_ENDPOINT:
		pr_dbg("%s: %s cmd stop endpoint 0x%lx, slot: %d, ep_dci: %d, suspend: %d\n",
				ring->name, comment, trb_gpa,
				trb->cmd_stop_endpoint.slot, trb->cmd_stop_endpoint.endpoint,
				trb->cmd_stop_endpoint.suspend);
		break;
	case TRB_TYPE_SET_TR_DEQUEUE_PTR:
		pr_dbg("%s: %s cmd set-tr-deq-ptr 0x%lx, slot: %d, ep_dci: %d, stream: %d,"
				" gpa: 0x%lx, ccs: %d, sctx_type: %d\n",
				ring->name, comment, trb_gpa, trb->cmd_set_tr_dequeue_ptr.slot,
				trb->cmd_set_tr_dequeue_ptr.endpoint,
				trb->cmd_set_tr_dequeue_ptr.stream,
				trb->cmd_set_tr_dequeue_ptr.tr_deq_ptr,
				trb->cmd_set_tr_dequeue_ptr.ccs,
				trb->cmd_set_tr_dequeue_ptr.sctx_type);
		break;
	case TRB_TYPE_RESET_DEVICE:
		pr_dbg("%s: %s cmd reset device 0x%lx, slot: %d\n",
				ring->name, comment, trb_gpa, trb->cmd_reset_device.slot);
		break;
	case TRB_TYPE_FORCE_HEADER:
	case TRB_TYPE_NOOP_CMD:
		break;
	/* event ring trbs */
	case TRB_TYPE_EVENT_XFER:
		pr_dbg("%s: %s event transfer done 0x%lx, trb_ptr: 0x%lx, trb_len: %d, cc: %d,"
				" ed: %d, slot: %d\n",
				ring->name, comment, trb_gpa,
				trb->event_xfer.trb_ptr, trb->event_xfer.trb_len,
				trb->event_xfer.cc, trb->event_xfer.ed,
				trb->event_xfer.slot);
		break;
	case TRB_TYPE_EVENT_CMD_DONE:
		pr_dbg("%s: %s event command done 0x%lx, trb_ptr: 0x%lx, param: 0x%x, cc: %d,"
				" slot: %d\n",
				ring->name, comment, trb_gpa,
				trb->event_cmd.trb_ptr, trb->event_cmd.param, trb->event_cmd.cc,
				trb->event_cmd.slot);
		break;
	case TRB_TYPE_EVENT_PORT_STATUS_CHANGE:
		pr_dbg("%s: %s event port status changed 0x%lx, port: %d, cc: %d\n",
				ring->name, comment, trb_gpa,
				trb->event_port_changed.port, trb->event_port_changed.cc);
		break;
	case TRB_TYPE_EVENT_BW_REQUEST:
		break;
	case TRB_TYPE_EVENT_DOORBELL:
		break;
	case TRB_TYPE_EVENT_HC:
		pr_dbg("%s: %s event HC 0x%lx, cc: %d\n",
				ring->name, comment, trb_gpa, trb->event_hc.cc);
		break;
	case TRB_TYPE_EVENT_DEVICE:
	case TRB_TYPE_EVENT_MFINDEX_WRAP:
		break;

	case TRB_TYPE_FORCE_EVENT:
	case TRB_TYPE_NEGO_BW:
	case TRB_TYPE_SET_LATENCY_TOLERANCE:
	case TRB_TYPE_GET_PORT_BW:
	case TRB_TYPE_GET_EXT_PROP:
	case TRB_TYPE_SET_EXT_PROP:
	default:
		pr_dbg("%s: %s unkonwn trb 0x%lx, type: %d\n",
				ring->name, comment, trb_gpa, trb->common.type);
		break;
	}
}

static void xhci_dump_slot_ctx(struct slot_ctx *sctx)
{
	pr_dbg("slot ctx, ctxs: %d, rhport: %d, route: 0x%05x, speed: %d, max_exit_latency: %d,"
			" state: %d, parent_hub_slot: %d, parent_port: %d, intr: %d, addr: %d,"
			" (hub: %d, mtt: %d, ports: %d, ttt: %d)\n",
			sctx->ctxs, sctx->rhport, sctx->route, sctx->speed, sctx->max_exit_latency,
			sctx->state, sctx->parent_hub_slot, sctx->parent_port, sctx->intr,
			sctx->addr, sctx->hub, sctx->mtt, sctx->ports, sctx->ttt);
}

static void xhci_dump_ep_ctx(uint8_t ep_dci, struct ep_ctx *ectx)
{
	pr_dbg("ep(dci) %d ctx, state: %d, mult: %d, mpstream: %d, lsa: %d, interval: %d, esit: %d,"
			" err_cnt: %d, type: %d, hid: %d, mbs: %d, mps: %d, dcs: %d, tr_deq_ptr:"
			" 0x%lx, avg_trb_len: %d\n", ep_dci,
			ectx->state, ectx->mult, ectx->mpstream, ectx->lsa, ectx->interval,
			(ectx->esit_hi << 16 | ectx->esit_lo), ectx->err_cnt, ectx->type,
			ectx->hid, ectx->mbs, ectx->mps, ectx->dcs,
			ectx->tr_deq_ptr << XHCI_TR_DEQ_PTR_SHIFT, ectx->avg_trb_len);
}

void xhci_dump_octxs(struct output_ctxs *octxs, const char *comment)
{
	if (comment)
		pr_dbg("%s:\n", comment);
	xhci_dump_slot_ctx(&octxs->ctxs[XHCI_DCI_SLOT_CTX].slot_ctx);
	uint32_t last_ep = octxs->ctxs[XHCI_DCI_SLOT_CTX].slot_ctx.ctxs;
	for (int i = XHCI_DCI_EP0_CTX; i <= last_ep; i++) {
		xhci_dump_ep_ctx(i, &octxs->ctxs[i].ep_ctx);
	}
}

void xhci_dump_ictxs(struct input_ctxs *ictxs, const char *comment)
{
	if (comment)
		pr_dbg("%s:\n", comment);
	uint32_t ep_mask = ictxs->ctrl_ctx.drop_ctx | ictxs->ctrl_ctx.add_ctx;
	pr_dbg("drop mask: 0x%08x, add mask: 0x%08x, cfg: %d, if: %d, alt: %d\n",
			ictxs->ctrl_ctx.drop_ctx, ictxs->ctrl_ctx.add_ctx, ictxs->ctrl_ctx.config,
			ictxs->ctrl_ctx.interface, ictxs->ctrl_ctx.alt_set);
	xhci_dump_slot_ctx(&ictxs->ctxs[XHCI_DCI_SLOT_CTX].slot_ctx);
	xhci_dump_ep_ctx(XHCI_DCI_EP0_CTX, &ictxs->ctxs[XHCI_DCI_EP0_CTX].ep_ctx);
	for (int i = XHCI_DCI_EPX_CTX(1, 0); i < XHCI_DCI_MAX; i++) {
		if (BIT(i) & ep_mask)
			xhci_dump_ep_ctx(i, &ictxs->ctxs[i].ep_ctx);
	}
}


