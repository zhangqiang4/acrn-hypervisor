/*
 * Copyright (C) 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>

#define pr_prefix "usb: "
#include "usb/core.h"
#include "log.h"

static pthread_mutex_t hcd_lock;
static STAILQ_HEAD(, usb_hcd) hcd_list;
static pthread_mutex_t device_lock;
static STAILQ_HEAD(, usb_device) device_list;


bool usb_devpath_eq(struct usb_devpath a, struct usb_devpath b)
{
	for(int depth = 0; depth < USB_MAX_TIERS; depth++) {
		if (a.port[depth] != b.port[depth])
			return false;
		if (a.port[depth] == 0)
			return true;
	}
	return true;
}

bool usb_devpath_has_prefix(struct usb_devpath path, struct usb_devpath prefix)
{
	for(int depth = 0; depth < USB_MAX_TIERS; depth++) {
		if (path.port[depth] == prefix.port[depth] && path.port[depth])
			continue;
		if (!prefix.port[depth])
			return true;
		else
			return false;
	}
	return true;
}

char *usb_devpath_str(struct usb_devpath path)
{
	char *buf = malloc(30); /* %02d.%02d.%02d.%02d.%02d.%02d\0 */
	int idx = 0;
	for (int i = 0; i < USB_MAX_TIERS && path.port[i]; i++) {
		idx += sprintf(buf+idx, i == 0 ? "%d": ".%d", path.port[i]);
	}
	buf[idx] = '\0';
	return buf;
}

char *usb_bus_devpath_str(uint8_t bus, struct usb_devpath path)
{
	char *buf = malloc(30); /* %03d-%02d.%02d.%02d.%02d.%02d.%02d\0 */
	int idx = 0;
	idx += sprintf(buf+idx, "%d-", bus);
	for (int i = 0; i < USB_MAX_TIERS && path.port[i]; i++) {
		idx += sprintf(buf+idx, i == 0 ? "%d": ".%d", path.port[i]);
	}
	buf[idx] = '\0';
	return buf;
}

int usb_parse_bus_devpath_str(const char *str, uint8_t *bus, struct usb_devpath *path)
{
	char *tmp = strdup(str);
	char *new = tmp;
	char *bus_str = strsep(&tmp, "-");

	char *end;
	*bus = strtoul(bus_str, &end, 0);
	if (*end) {
		goto err;
	}

	uint8_t idx = 0;
	for (char *port_str = strsep(&tmp, "."); port_str; port_str = strsep(&tmp, ".")) {
		if (idx == 6) {
			goto err;
		}
		path->port[idx++] = strtoul(port_str, &end, 0);
		if (*end) {
			goto err;
		}
	}
	path->port[idx] = 0;

	free(new);
	return 0;
err:
	*bus = 0;
	memset(path, 0, sizeof(struct usb_devpath));
	free(new);
	return -1;
}

int usb_hub_attach_device(struct usb_hub *hub, uint8_t port_idx, struct usb_device *dev)
{
	int ret = 0;

	if (port_idx >= hub->port_count) {
		pr_err("invalid port index %d\n", port_idx);
		return -1;
	}

	pthread_mutex_lock(&hub->lock);
	if (hub->ports[port_idx].device) {
		pr_err("a device has already occupied port index %d\n", port_idx);
		ret = -1;
		goto exit_unlock;
	}

	pthread_mutex_lock(&device_lock);
	if (dev->port) {
		pr_err("the device has already been connected to another port\n");
		ret = -1;
		pthread_mutex_unlock(&device_lock);
		goto exit_unlock;
	}
	hub->ports[port_idx].device = usb_device_get(dev);
	dev->port = &hub->ports[port_idx];
	pthread_mutex_unlock(&device_lock);

	hub->on_device_add(hub, port_idx);

exit_unlock:
	pthread_mutex_unlock(&hub->lock);
	return ret;
}

struct usb_device *usb_hub_detach_device(struct usb_hub *hub, uint8_t port_idx)
{
	if (port_idx >= hub->port_count) {
		pr_err("invalid port index %d\n", port_idx);
		return NULL;
	}

	pthread_mutex_lock(&hub->lock);
	struct usb_device *dev = hub->ports[port_idx].device;
	if (!dev) {
		pthread_mutex_unlock(&hub->lock);
		return NULL;
	}

	hub->on_device_del(hub, port_idx);

	pthread_mutex_lock(&device_lock);
	dev->port = NULL;
	hub->ports[port_idx].device = NULL;
	pthread_mutex_unlock(&device_lock);

	pthread_mutex_unlock(&hub->lock);

	usb_device_put(dev);

	return dev;
}

void usb_hub_remove_device(struct usb_hub *hub, uint8_t port_idx)
{
	struct usb_device *dev = usb_hub_detach_device(hub, port_idx);
	usb_device_destroy(dev);
}

#define USB_XFER_INITIAL_VECS		32

struct usb_transfer *usb_transfer_alloc(struct usb_hcd *hcd, struct usb_device *dev,
		enum usb_transfer_type type,
		uint8_t ep, uint16_t stream, void (*done)(struct usb_transfer *xfer))
{
	uint32_t size = ALIGN_UP(sizeof(struct usb_transfer) + hcd->xfer_priv_size +
			dev->backend->xfer_priv_size, sizeof(struct iovec));
	struct usb_transfer *xfer = calloc(1, size + USB_XFER_INITIAL_VECS * sizeof(struct iovec));
	xfer->dev = dev;
	xfer->hc_priv = xfer + 1;
	xfer->dev_priv = xfer->hc_priv + hcd->xfer_priv_size;
	xfer->vec_capacity = USB_XFER_INITIAL_VECS;
	xfer->vec_count = 0;
	xfer->vecs = (void *)xfer + size;
	xfer->ep = ep;
	xfer->stream = stream;
	xfer->type = type;
	xfer->done = done;
	return xfer;
}

int usb_transfer_add_one_buf(struct usb_transfer *xfer, void *buf, uint32_t len)
{
	if (xfer->vec_count == xfer->vec_capacity) {
		uint32_t new_cap = xfer->vec_capacity * 2;
		void *new_vecs;
		if (xfer->vec_capacity == USB_XFER_INITIAL_VECS) {
			new_vecs = malloc(sizeof(struct iovec) * new_cap);
			if (!new_vecs)
				return -1;

			memcpy(new_vecs, xfer->vecs, sizeof(struct iovec) * xfer->vec_capacity);
		} else {
			new_vecs = realloc(xfer->vecs, sizeof(struct iovec) * new_cap);
			if (!new_vecs)
				return -1;
		}
		xfer->vecs = new_vecs;
		xfer->vec_capacity = new_cap;
	}

	xfer->vecs[xfer->vec_count].iov_base = buf;
	xfer->vecs[xfer->vec_count].iov_len = len;
	xfer->vec_count++;

	return 0;
}

void usb_transfer_free(struct usb_transfer *xfer)
{
	if (xfer->vec_capacity == USB_XFER_INITIAL_VECS)
		free(xfer);
	else {
		free(xfer->vecs);
		free(xfer);
	}
}

uint8_t usb_hcd_alloc_dev_addr(struct usb_hcd *hcd, struct usb_device *dev)
{
	uint8_t start = hcd->next_dev_addr;
	uint8_t iter = start;
	uintptr_t old = (uintptr_t)NULL;
	atomic_uintptr_t target = (atomic_uintptr_t)hcd->devices[iter];
	while (!atomic_compare_exchange_strong(&target, &old, (uintptr_t)dev)) {
		iter ++;
		if (iter == 128)
			iter = 1;
		if (iter == start)
			return 0;
	}
	hcd->next_dev_addr = iter + 1;
	if (hcd->next_dev_addr == 128)
		hcd->next_dev_addr = 1;
	dev->addr = iter;
	return iter;
}

struct usb_hcd *usb_hcd_alloc(size_t priv_size,  uint8_t u2_port, uint8_t u3_port, const char *name)
{
	struct usb_hcd *hcd = calloc(1, sizeof(struct usb_hcd) + priv_size);
	if (!hcd)
		return 0;

	hcd->name = strdup(name);
	hcd->hub2.ports = calloc(u2_port, sizeof(struct usb_port));
	hcd->hub3.ports = calloc(u3_port, sizeof(struct usb_port));
	if (!hcd->hub2.ports || !hcd->hub3.ports) {
		free(hcd->hub2.ports);
		free(hcd->hub3.ports);
		free(hcd);
		return 0;
	}
	hcd->hub2.port_count = u2_port;
	hcd->hub2.is_roothub = true;
	hcd->hub2.hcd = hcd;
	hcd->hub2.usb2 = true;
	hcd->hub3.port_count = u3_port;
	hcd->hub3.is_roothub = true;
	hcd->hub3.hcd = hcd;
	hcd->hub3.usb2 = false;

	for (int i = 0; i < u2_port; i++) {
		hcd->hub2.ports[i].index = i;
		hcd->hub2.ports[i].parent = NULL;
		hcd->hub2.ports[i].hub = &hcd->hub2;
		hcd->hub2.ports[i].device = NULL;
	}
	for (int i = 0; i < u3_port; i++) {
		hcd->hub3.ports[i].index = i;
		hcd->hub3.ports[i].parent = NULL;
		hcd->hub3.ports[i].hub = &hcd->hub3;
		hcd->hub3.ports[i].device = NULL;
	}

	hcd->next_dev_addr = 1;

	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&hcd->lock, NULL);
	pthread_mutexattr_destroy(&attr);

	pthread_cond_init(&hcd->cond, NULL);

	return hcd;
}

int usb_hcd_destroy(struct usb_hcd *hcd)
{
	free(hcd->name);
	pthread_cond_destroy(&hcd->cond);
	pthread_mutex_destroy(&hcd->lock);
	free(hcd->hub2.ports);
	free(hcd->hub3.ports);
	free(hcd);
	return 0;
}

int usb_hcd_register(struct usb_hcd *hcd)
{
	if (!hcd->hub2.port_count && !hcd->hub3.port_count) {
		pr_err("can't register a hcd with 0 ports\n");
		return -1;
	}

	if (hcd->hub2.port_count && (!hcd->hub2.on_device_add || !hcd->hub2.on_device_del)) {
		pr_err("on_device_add and on_device_del are required for usb2 hub!\n");
		return -1;
	}

	if (hcd->hub3.port_count && (!hcd->hub3.on_device_add || !hcd->hub3.on_device_del)) {
		pr_err("on_device_add and on_device_del are required for usb3 hub!\n");
		return -1;
	}

	pthread_mutex_lock(&hcd_lock);
	STAILQ_INSERT_TAIL(&hcd_list, hcd, list);
	pthread_mutex_unlock(&hcd_lock);
	return 0;
}

int usb_hcd_unregister(struct usb_hcd *hcd)
{
	pthread_mutex_lock(&hcd_lock);
	STAILQ_REMOVE(&hcd_list, hcd, usb_hcd, list);
	pthread_mutex_unlock(&hcd_lock);
	return 0;
}

struct usb_hcd *find_usb_hcd_by_name(const char *name)
{
	struct usb_hcd *hcd;
	STAILQ_FOREACH (hcd, &hcd_list, list) {
		if (!strcmp(hcd->name, name))
			return hcd;
	}
	return NULL;
}

struct usb_device *usb_device_create(struct usb_device_backend *backend, const char *opts)
{
	struct usb_device *dev = calloc(1, sizeof(struct usb_device));
	if (!dev)
		return NULL;

	dev->backend = backend;
	g_atomic_ref_count_init(&dev->ref);
	// parse dev->id from opts

	if (backend->init(dev, opts)) {
		pr_err("failed to init new usb device\n");
		free(dev);
		return NULL;
	}
	pthread_mutex_lock(&device_lock);
	STAILQ_INSERT_TAIL(&device_list, dev, list);
	pthread_mutex_unlock(&device_lock);

	return dev;
}

static void usb_device_release(struct usb_device *dev)
{
	dev->backend->deinit(dev);
	free(dev);
}

void usb_device_destroy(struct usb_device *dev)
{
	pthread_mutex_lock(&device_lock);
	STAILQ_REMOVE(&device_list, dev, usb_device, list);
	pthread_mutex_unlock(&device_lock);
	usb_device_put(dev);
}

/* if dev is non NULL, caller must have hold a valid reference to it */
struct usb_device *usb_device_get(struct usb_device *dev)
{
	if (dev)
		g_atomic_ref_count_inc(&dev->ref);
	return dev;
}

struct usb_device *usb_device_get_from_id(const char *id)
{
	struct usb_device *dev = NULL;

	pthread_mutex_lock(&device_lock);
	STAILQ_FOREACH (dev, &device_list, list) {
		if (dev->id && !strcmp(id, dev->id))
			break;
	}
	dev = usb_device_get(dev);
	pthread_mutex_unlock(&device_lock);

	return dev;
}

void usb_device_put(struct usb_device *dev)
{
	if (g_atomic_ref_count_dec(&dev->ref)) {
		usb_device_release(dev);
	}
}

#if 0	/* bad API: dev does not always has a hcd and this need device lock */
// TODO refcount
struct usb_hcd *usb_device_get_hcd(struct usb_device *dev)
{
	struct usb_port *port = dev->port;
	while(port->parent) {
		port = port->parent;
	}
	g_assert(port->hub->is_roothub);
	return port->hub->hcd;
}
#endif

#if 0
int usb_hcd_connect_device(struct usb_hcd *hcd, struct usb_device *dev)
{
	int i;
	uint8_t port_idx;
	enum usb_device_speed speed = dev->backend->speed(dev);

	pthread_mutex_lock(&hcd->lock);
	for (i = 0; i < hcd->port_count; i++) {
		if (!hcd->ports[i].device &&
				(usb_device_speed_is_usb2(speed) == hcd->ports[i].usb2)) {
			port_idx = i;
			break;
		}
	}
	if (i >= hcd->port_count) {
		pthread_mutex_unlock(&hcd->lock);
		return -1;
	}

	pthread_mutex_lock(&device_lock);
	hcd->ports[port_idx].device = usb_device_get(dev);
	//hcd->devices[addr] = dev;
	dev->port = &hcd->ports[port_idx];
	pthread_mutex_unlock(&device_lock);

	if (hcd->on_device_add(hcd, port_idx)) {
		pr_err("%s: failed to add new device to port %d\n", hcd->name, port_idx + 1);
		pthread_mutex_lock(&device_lock);
		dev->port = NULL;
		hcd->ports[port_idx].device = NULL;
		usb_device_put(dev);
		//hcd->devices[addr] = NULL;
		pthread_mutex_unlock(&device_lock);
		pthread_mutex_unlock(&hcd->lock);
		return -1;
	}

	pthread_mutex_unlock(&hcd->lock);
	return 0;
}

int usb_hcd_disconnect_device(struct usb_device *dev)
{
	struct usb_hcd *hcd = usb_device_get_hcd(dev);
	uint8_t port = dev->port->index;	/* assume dev is connected to a root port */

	pthread_mutex_lock(&hcd->lock);

	if (hcd->on_device_del(hcd, port)) {
		pr_err("%s: failed to unplug device from port %d\n", hcd->name, port);
		pthread_mutex_unlock(&hcd->lock);
		return -1;
	}

	dev->port->device = NULL;
	dev->port = NULL;
	pthread_mutex_unlock(&hcd->lock);
	return 0;
}

// no lock needed now. only pmapper user
struct usb_device *usb_add_device(struct usb_hcd *hcd, struct usb_device_backend *backend,
		const char *opts)
{
	if (!backend->match(opts))
		return NULL;

	struct usb_device *dev = calloc(1, sizeof(struct usb_device));
	dev->backend = backend;
	g_atomic_ref_count_init(&dev->ref);

	if (backend->init(dev, opts)) {
		pr_err("failed to init new usb device\n");
		return NULL;
	}

	/* TODO check backend filled fields */

	if (usb_hcd_connect_device(hcd, dev)) {
		pr_err("failed to insert new usb device to hcd\n");
		usb_device_put(dev);
		return NULL;
	}

	return dev;
}

// no lock needed now. only pmapper user
void usb_del_device(struct usb_device *dev)
{
	if (dev->port) {
		usb_hcd_disconnect_device(dev);
	}
	usb_device_put(dev);
}
#endif

int usb_core_init(void)
{
	pthread_mutex_init(&hcd_lock, NULL);
	STAILQ_INIT(&hcd_list);
	pthread_mutex_init(&device_lock, NULL);
	STAILQ_INIT(&device_list);
	return 0;
}

void usb_core_deinit(void)
{
	pthread_mutex_destroy(&hcd_lock);
	pthread_mutex_destroy(&device_lock);
}
