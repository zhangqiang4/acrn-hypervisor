/*
 * Copyright (C) 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include "glib.h"
#include <sys/eventfd.h>
#include <pthread.h>

#define pr_prefix "usb: xhci: ring: "
#include "log.h"
#include "xhci.h"
#include "timer.h"
#include "mevent.h"

void xhci_ring_lock(struct xhci_ring *ring)
{
	pthread_mutex_lock(&ring->lock);
}

void xhci_ring_unlock(struct xhci_ring *ring)
{
	pthread_mutex_unlock(&ring->lock);
}

struct xhci_ring *xhci_ring_get(struct xhci_ring *xring)
{
	if (xring)
		g_atomic_ref_count_inc(&xring->ref);
	return xring;
}

static void xhci_ring_release(struct xhci_ring *xring)
{
	if (xring->ops->deinit)
		xring->ops->deinit(xring);

	free(xring->name);
	free(xring);
}

void xhci_ring_put(struct xhci_ring *xring)
{
	if (g_atomic_ref_count_dec(&xring->ref)) {
		xhci_ring_release(xring);
	}
}

void xhci_ring_stop(struct xhci_ring *xring, bool wait)
{
	pthread_mutex_lock(&xring->lock);
	xhci_ring_stop_locked(xring, wait);
	pthread_mutex_unlock(&xring->lock);
}

static void xhci_ring_handler_trb(int fd, enum ev_type ev_type, void *param)
{
	struct xhci_ring *ring = param;
	uint64_t dummy;
	if (read(ring->kick_fd, &dummy, sizeof(dummy)) < 0)
		pr_err("failed to read eventfd\n");
	ring->ops->handle_trb(ring);
}

struct xhci_ring *xhci_ring_create(struct pci_xhci *xhci, struct xhci_ring_ops *ops,
		const char *name, uint16_t interval, void *data)
{
	if (!ops || !ops->handle_trb) {
		pr_err("Invalid ring ops\n");
		return NULL;
	}

	struct xhci_ring *xring = calloc(1, sizeof(struct xhci_ring) + ops->priv_size);
	if (!xring)
		return NULL;

	pthread_mutex_init(&xring->lock, NULL);

	g_atomic_ref_count_init(&xring->ref);
	xring->xhci = xhci;
	xring->ops = ops;
	xring->name = strdup(name);

	if (interval) {
		xring->periodic = true;
		xring->interval = interval;
	}
	{
		xring->kick_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
		if (xring->kick_fd < 0) {
			pr_err("failed to create eventfd for xring %s\n", name);
			goto exit_free;
		}
		xring->kick_event = mevent_add(xring->kick_fd, EVF_READ_ET, xhci_ring_handler_trb,
				xring, NULL, NULL);
		if (!xring->kick_event)
			goto exit_close;
	}

	if (ops->init && ops->init(xring, data)) {
		pr_err("BUG: failed to init xhci ring!\n");
		goto exit_del_mevent;
	}

	return xring;

exit_del_mevent:
	mevent_delete(xring->kick_event);
exit_close:
	close(xring->kick_fd);
exit_free:
	pthread_mutex_destroy(&xring->lock);
	free(xring->name);
	free(xring);
	return NULL;
}

int xhci_ring_start(struct xhci_ring *xring)
{
	pthread_mutex_lock(&xring->lock);
	if (!xring->running) {
		if (xring->ops->start && xring->ops->start(xring)) {
			pthread_mutex_unlock(&xring->lock);
			return -1;
		}

		if (!xring->periodic)
			mevent_enable(xring->kick_event);

		xring->running = true;
	}
	pthread_mutex_unlock(&xring->lock);

	xhci_ring_notify(xring);

	return 0;
}

void xhci_ring_stop_locked(struct xhci_ring *xring, bool wait)
{
	if (xring->running) {
		xring->stopping = true;

		if (xring->periodic) {
			struct pci_xhci *xhci = xring->xhci;
			struct xhci_ring *r, *first;
			pthread_mutex_lock(&xhci->sched_lock);
			first = CIRCLEQ_FIRST(&xhci->sched_rings);
			CIRCLEQ_FOREACH(r, &xhci->sched_rings, list) {
				if (r == xring) {
					CIRCLEQ_REMOVE(&xhci->sched_rings, r, list);
					pr_dbg("drop ring %s from scheduling list\n", xring->name);
				}
			}
			if (xring == first) {
				struct itimerspec new = xhci_sched_get_next(xhci);
				pr_dbg("itimerspec value %ld,%ld, it %ld,%ld\n",
						new.it_value.tv_sec, new.it_value.tv_nsec,
						new.it_interval.tv_sec, new.it_interval.tv_nsec);
				acrn_timer_settime_abs(&xhci->sched, &new);
			}
			pthread_mutex_unlock(&xhci->sched_lock);
		} else {
			mevent_disable(xring->kick_event);
		}

		// TBD execute stop callback from ring handler, not stop initiator
		if (xring->ops->stop)
			xring->ops->stop(xring);
		xring->stopping = false;
		xring->running = false;		// in case ->stop did not do it
	}
}

void xhci_ring_destroy(struct xhci_ring *xring, bool wait)
{
	pthread_mutex_lock(&xring->lock);
	xring->destroying = true;
	if (xring->running) {
		xhci_ring_stop_locked(xring, wait);
	}
	pthread_mutex_unlock(&xring->lock);

	mevent_delete(xring->kick_event);
	close(xring->kick_fd);
	pthread_mutex_destroy(&xring->lock);
	xring->destroying = false;

	xhci_ring_put(xring);
}

void xhci_ring_notify(struct xhci_ring *xring)
{
	if (!xring->running)
		return;

	if (xring->periodic) {
		struct pci_xhci *xhci = xring->xhci;
		pthread_mutex_lock(&xhci->sched_lock);
		struct xhci_ring *r;
		CIRCLEQ_FOREACH(r, &xhci->sched_rings, list) {
			if (r == xring) {
				pthread_mutex_unlock(&xhci->sched_lock);
				return;
			}
			// Newcomer will be the last one among rings with the same interval.
			// Don't use ">=" here (as the first one with the same interval) !!
			// Because above check existence wouldn't reach forever for nodes not the
			// head after re-notify!
			if (r->interval > xring->interval)
				break;
		}
		if (r != (const void *)(&xhci->sched_rings))
			CIRCLEQ_INSERT_BEFORE(&xhci->sched_rings, r, xring, list);
		else
			CIRCLEQ_INSERT_TAIL(&xhci->sched_rings, xring, list);
		pr_dbg("insert ring %s with interval %x\n", xring->name, xring->interval);
		struct itimerspec new = xhci_sched_get_next(xhci);
		pr_dbg("itimerspec value %ld,%ld, it %ld,%ld\n",
				new.it_value.tv_sec, new.it_value.tv_nsec,
				new.it_interval.tv_sec, new.it_interval.tv_nsec);
		acrn_timer_settime_abs(&xhci->sched, &new);
		pthread_mutex_unlock(&xhci->sched_lock);
	} else {
		uint64_t dummy = 1;
		if (write(xring->kick_fd, &dummy, sizeof(dummy)) < 0) {
			pr_err("failed to write to eventfd\n");
		}
	}
}

void xhci_ring_reset(struct xhci_ring *xring, bool wait)
{
	pthread_mutex_lock(&xring->lock);

	if (xring->running) {
		xhci_ring_stop_locked(xring, wait);
		xring->running = false;
	}
	xring->ops->reset(xring);
	pthread_mutex_unlock(&xring->lock);
}

void sched_timer_handler(void *data, uint64_t nexp)
{
	struct pci_xhci *xhci = data;
	uint32_t mfindex = xhci_get_mfindex(xhci);
	pr_dbg("timer handler, mfindex %x\n", mfindex);

	struct xhci_ring *r, *first;
	pthread_mutex_lock(&xhci->sched_lock);
	first = CIRCLEQ_FIRST(&xhci->sched_rings);
	CIRCLEQ_FOREACH(r, &xhci->sched_rings, list) {
		if (mfindex - r->last_mfindex >= r->interval) {
			r->last_mfindex = mfindex;
			if (!r->ops->handle_trb(r)) {
				CIRCLEQ_REMOVE(&xhci->sched_rings, r, list);
				pr_dbg("drop ring %s from scheduling list\n", r->name);
			}
		}
	}

	if (first != CIRCLEQ_FIRST(&xhci->sched_rings)) {
		struct itimerspec new = xhci_sched_get_next(xhci);
		pr_dbg("itimerspec value %ld,%ld, it %ld,%ld\n",
				new.it_value.tv_sec, new.it_value.tv_nsec,
				new.it_interval.tv_sec, new.it_interval.tv_nsec);
		acrn_timer_settime_abs(&xhci->sched, &new);
	}
	pthread_mutex_unlock(&xhci->sched_lock);

#if 0
	LIST_HEAD(,transfer_ring_priv) tmp;
	LIST_INIT(&tmp);
	struct transfer_ring_priv *r, *t;
	pthread_mutex_lock(&xhci->sched_lock);
	LIST_FOREACH(r, &xhci->sched_rings, list) {
		if (mfindex - r->last_mfindex >= r->interval) {
			LIST_INSERT_HEAD(&tmp, r, tmp_list);
			r->last_mfindex = mfindex;
		}
	}
	pthread_mutex_unlock(&xhci->sched_lock);

	list_foreach_safe(r, &tmp, tmp_list, t) {
		struct xhci_ring *xring = (struct xhci_ring *)r - 1;
		if (xring->ops->handle_trb(xring)) {
			LIST_REMOVE(r, tmp_list);
		}
	}

	/* Some rings are underrun/overrun. Drop them from periodic scheduling list */
	if (!LIST_EMPTY(&tmp)) {
		pthread_mutex_lock(&xhci->sched_lock);
		while (!LIST_EMPTY(&tmp)) {
			r = LIST_FIRST(&tmp);
			LIST_REMOVE(r, tmp_list);
			LIST_REMOVE(r, list);
			struct xhci_ring *xring = (struct xhci_ring *)r - 1;
			pr_dbg("drop ring %s from scheduling list\n", xring->name);
		}
		struct itimerspec new = xhci_sched_get_next(xhci);
		pr_dbg("itimerspec value %ld,%ld, it %ld,%ld\n",
				new.it_value.tv_sec, new.it_value.tv_nsec,
				new.it_interval.tv_sec, new.it_interval.tv_nsec);
		acrn_timer_settime_abs(&xhci->sched, &new);
		pthread_mutex_unlock(&xhci->sched_lock);
	}
#endif
}
