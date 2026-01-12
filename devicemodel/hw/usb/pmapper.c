/*
 * Copyright (C) 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include <pthread.h>
#include <sys/queue.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <libusb.h>

#define pr_prefix "usb: pmapper: "
#include "usb/core.h"
#include "log.h"
#include "types.h"

#define KEEP_TREE_TOPOLOGY 0

struct pmapper_map {
	LIST_ENTRY(pmapper_map) list;
	struct usb_devpath path;
	struct usb_devpath vpath;
	struct usb_hub *rh;
};

/* Map all devices with one prefix to target virtual HCD */
struct pmapper {
	LIST_ENTRY(pmapper) list;
	bool ready_for_new_events;

	struct usb_hcd *hcd;
	uint8_t vport;			/* TODO used in keep-tree-topology map */
	struct usb_devpath prefix;	/* physical path to the port prefix */
	uint32_t bus;			/* bus of physical devices */
	LIST_HEAD(, pmapper_map) maps;	/* track devices in use */
};

static struct usb_device_backend *phy;
static LIST_HEAD(, pmapper) pmapper_list;
static pthread_mutex_t pmapper_lock;
static pthread_t pmapper_poller;
static bool pmapper_exit;
static struct libusb_context *libusb_ctx;
static libusb_hotplug_callback_handle device_arrive_handle, device_left_handle;

static struct pmapper *find_pmapper_from_bus_path(uint32_t bus, struct usb_devpath path)
{
	struct pmapper *pmapper;
	LIST_FOREACH(pmapper, &pmapper_list, list) {
		if (pmapper->bus == bus && usb_devpath_has_prefix(path, pmapper->prefix)) {
			pthread_mutex_unlock(&pmapper_lock);
			return pmapper;
		}
	}
	return NULL;
}

static struct pmapper_map *find_pmapper_map_from_path(struct pmapper *pmapper,
						struct usb_devpath path)
{
	struct pmapper_map *map;
	LIST_FOREACH(map, &pmapper->maps, list) {
		if (usb_devpath_eq(map->path, path)) {
			return map;
		}
	}
	return NULL;
}

static struct usb_devpath pmapper_map_vpath(struct pmapper *pmapper, struct usb_devpath path)
{
#if KEEP_TREE_TOPOLOGY
	return path;
#else
	bool used[16] = {0};
	int i;
	struct usb_devpath vpath = {0};
	struct pmapper *temp;
	LIST_FOREACH(temp, &pmapper_list, list) {
		if (temp->hcd == pmapper->hcd) {
			struct pmapper_map *map;
			LIST_FOREACH(map, &temp->maps, list) {
				used[map->vpath.port[0]] = true;
			}
		}
	}

	for (i = 1; i < 16; i++) {
		if (!used[i])
			break;
	}

	// assume same number of usb2 and usb3 ports, and usb2 and usb3 ports with the same index
	// constructs a pair (peer of each other).
	if (i <= pmapper->hcd->hub2.port_count)
		vpath.port[0] = i;
	return vpath;
#endif
}

static void pmapper_map_device(struct pmapper *pmapper, struct libusb_device *ldev, struct usb_devpath path)
{
	uint8_t bus = pmapper->bus;

	struct libusb_device_descriptor desc;
	int ret = libusb_get_device_descriptor(ldev, &desc);
	if (ret) {
		pr_warn("failed to get descriptor of %d-%s\n", bus, usb_devpath_str(path));
		return;
	}
	if (desc.bDeviceClass == LIBUSB_CLASS_HUB) {
		pr_dbg("ingore hub plugged at %d-%s\n", bus, usb_devpath_str(path));
		return;
	}
	enum libusb_speed lspeed = libusb_get_device_speed(ldev);
	bool is_usb2dev = (lspeed == LIBUSB_SPEED_LOW) || (lspeed == LIBUSB_SPEED_FULL) ||
		(lspeed == LIBUSB_SPEED_HIGH);
	struct usb_hub *rh = is_usb2dev ? &pmapper->hcd->hub2 : &pmapper->hcd->hub3;

	if (find_pmapper_map_from_path(pmapper, path)) {
		pr_info("map already exist\n");
		return;
	}

	struct usb_devpath vpath = pmapper_map_vpath(pmapper, path);
	if (!vpath.port[0]) {
		pr_err("no virtual port available\n");
		return;
	}

	struct pmapper_map *map = calloc(1, sizeof(struct pmapper_map));
	if (!map) {
		pr_err("no memory for new pmapper map!\n");
		return;
	}
	map->rh = rh;
	map->path = path;
	map->vpath = vpath;
	LIST_INSERT_HEAD(&pmapper->maps, map, list);

	char *ppath_str = usb_devpath_str(map->path);
	char *vpath_str = usb_devpath_str(map->vpath);
	pr_dbg("map %s to %s port path %s\n", ppath_str, hcd_name(pmapper->hcd), vpath_str);
	free(ppath_str);
	free(vpath_str);

	char opts[64] = {0};
	uint8_t addr = libusb_get_device_address(ldev);
	sprintf(opts, "usb-host,hostbus=%d,hostaddr=%d", bus, addr);
	struct usb_device *dev = usb_device_create(phy, opts);
	if (!dev) {
		pr_err("failed to create physical usb device to %s\n", hcd_name(pmapper->hcd));
		goto exit_unmap;
	}
#if KEEP_TREE_TOPOLOGY
	// find vhub per vpath
#else
	if (usb_hub_attach_device(rh, vpath.port[0] - 1, dev)) {
		pr_err("failed to connect new device with opts: %s\n", opts);
		goto exit_put_dev;
	}
#endif
	return;

exit_put_dev:
	usb_device_put(dev);
exit_unmap:
	LIST_REMOVE(map, list);
	free(map);

	return;
}

static void pmapper_del_map(struct pmapper *pmapper, struct pmapper_map *map)
{
#if KEEP_TREE_TOPOLOGY
#else
	usb_hub_remove_device(map->rh, map->vpath.port[0] - 1);
#endif
	LIST_REMOVE(map, list);
	free(map);
}

/* called from libusb internal event thread */
static int pmapper_device_arrived(struct libusb_context *ctx, struct libusb_device *ldev,
		libusb_hotplug_event event, void *data)
{
	uint8_t bus = libusb_get_bus_number(ldev);

	struct usb_devpath path;
	uint8_t depth = libusb_get_port_numbers(ldev, &path.port[0], sizeof(path.port));
	path.port[depth] = 0;

	char *path_str = usb_bus_devpath_str(bus, path);
	pr_dbg("new device: %s, depth=%d\n", path_str, depth);
	free(path_str);

	pthread_mutex_lock(&pmapper_lock);
	struct pmapper *pmapper = find_pmapper_from_bus_path(bus, path);
	if (!pmapper || !pmapper->ready_for_new_events) {
		pr_dbg("don't care.\n");
		pthread_mutex_unlock(&pmapper_lock);
		return 0;
	}

	/* duplicates will be ignored. We can't block libusb from detecting hotplug when scanning
	 * present device list. So duplicates may happen. */
	pmapper_map_device(pmapper, ldev, path);
	pthread_mutex_unlock(&pmapper_lock);

	return 0;
}

/* no concurrency */
static int pmapper_device_left(struct libusb_context *ctx, struct libusb_device *ldev,
		libusb_hotplug_event event, void *data)
{
	uint8_t bus = libusb_get_bus_number(ldev);

	struct usb_devpath path;
	uint8_t depth = libusb_get_port_numbers(ldev, &path.port[0], sizeof(path.port));
	path.port[depth] = 0;

	char *path_str = usb_bus_devpath_str(bus, path);
	pr_dbg("device left: %s, depth=%d\n", path_str, depth);
	free(path_str);

	pthread_mutex_lock(&pmapper_lock);
	struct pmapper *pmapper = find_pmapper_from_bus_path(bus, path);
	if (!pmapper /* || !pmapper->ready_for_new_events */) {
		pr_dbg("don't care.\n");
		goto exit_unlock;
	}

	struct pmapper_map *map = find_pmapper_map_from_path(pmapper, path);
	/* may race with pmapper_scan_for_new_commer, and no map was found.  */
	/* Besides, there is no map for hubs */
	if (!map) {
		pr_info("device had already left\n");
		goto exit_unlock;
	}

	pmapper_del_map(pmapper, map);

exit_unlock:
	pthread_mutex_unlock(&pmapper_lock);
	return 0;
}

int usb_pmapper_add(struct usb_hcd *hcd, uint8_t vport, uint8_t bus, struct usb_devpath prefix)
{
	if (!bus || !prefix.port[0] || !hcd) {
		pr_err("failed to add pmapper, invalid arguments\n");
		return -1;
	}

	struct pmapper *new = calloc(1, sizeof(struct pmapper));
	if (!new) {
		pr_err("failed to allocate new pmapper descriptor!\n");
		return -1;
	}
	LIST_INIT(&new->maps);
	new->hcd = hcd;
	new->bus = bus;
	new->vport = vport;
	new->prefix = prefix;
	/* pmapper poller thread will check present devices */
	new->ready_for_new_events = false;
	char *prefix_str = usb_bus_devpath_str(bus, prefix);
	pr_dbg("add path prefix %s for %s\n", prefix_str, hcd->name);
	free(prefix_str);

	pthread_mutex_lock(&pmapper_lock);
	LIST_INSERT_HEAD(&pmapper_list, new, list);
	pthread_mutex_unlock(&pmapper_lock);

	return 0;
}

void usb_pmapper_del(struct usb_hcd *hcd)
{
	struct pmapper *p, *t;
	pthread_mutex_lock(&pmapper_lock);
	list_foreach_safe(p, &pmapper_list, list, t) {
		if (p->hcd != hcd)
			continue;

		struct pmapper_map *m;
		while ((m = LIST_FIRST(&p->maps))) {
			pmapper_del_map(p, m);	/* disconnect mapped devices */
		}
		LIST_REMOVE(p, list);
		free(p);
	}
	pthread_mutex_unlock(&pmapper_lock);
}

static void pmapper_scan_for_new_commer(void)
{
	struct pmapper *pmapper;
	pthread_mutex_lock(&pmapper_lock);
	LIST_FOREACH(pmapper, &pmapper_list, list) {
		if (pmapper->ready_for_new_events) {
			continue;
		}
		pmapper->ready_for_new_events = true;

		pr_dbg("scan present devices for new pmapper\n");
		libusb_device **list;
		int ret;
		if ((ret = libusb_get_device_list(libusb_ctx, &list)) < 0) {
			pr_err("failed to get usb device list: %s\n", libusb_strerror(ret));
		}

		for (int i = 0; list[i]; i++) {
			uint8_t bus = libusb_get_bus_number(list[i]);
			struct usb_devpath path;
			uint8_t depth = libusb_get_port_numbers(list[i], &path.port[0], sizeof(path.port));
			path.port[depth] = 0;

			struct pmapper *iter = find_pmapper_from_bus_path(bus, path);
			if (pmapper == iter) {
				pmapper_map_device(pmapper, list[i], path);
			}
		}
		libusb_free_device_list(list, 1);
	}
	pthread_mutex_unlock(&pmapper_lock);
}

static void *pmapper_poll(void *arg)
{
	while (!pmapper_exit) {
		struct timeval t = {1, 0};
		pmapper_scan_for_new_commer();
		int ret = libusb_handle_events_timeout(libusb_ctx, &t);
		if (ret < 0)
			sleep(1);
	}
	pr_info("pmapper poller exited!\n");
	return NULL;
}

int usb_pmapper_init(void)
{
	int ret;

	pthread_mutex_init(&pmapper_lock, NULL);
	LIST_INIT(&pmapper_list);
	pmapper_exit = false;
	phy = &usb_backend_host;

	ret = libusb_init(&libusb_ctx);
	if (ret < 0) {
		pr_err("failed to initialize libusb: %d\n", ret);
		return -1;
	}
	ret = libusb_hotplug_register_callback(libusb_ctx,
			LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED, 0,
			LIBUSB_HOTPLUG_MATCH_ANY,
			LIBUSB_HOTPLUG_MATCH_ANY,
			LIBUSB_HOTPLUG_MATCH_ANY,
			pmapper_device_arrived, NULL, &device_arrive_handle);
	if (ret != LIBUSB_SUCCESS) {
		pr_err("failed to register libusb hot plug callback\n");
		ret = -1;
		goto err_libusb_deinit;
	}

	ret = libusb_hotplug_register_callback(libusb_ctx,
			LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT, 0,
			LIBUSB_HOTPLUG_MATCH_ANY,
			LIBUSB_HOTPLUG_MATCH_ANY,
			LIBUSB_HOTPLUG_MATCH_ANY,
			pmapper_device_left, NULL, &device_left_handle);
	if (ret != LIBUSB_SUCCESS) {
		pr_err("failed to register libusb hot remove callback\n");
		libusb_hotplug_deregister_callback(libusb_ctx, device_arrive_handle);
		ret = -1;
		goto err_libusb_deinit;
	}

	pthread_create(&pmapper_poller, NULL, pmapper_poll, NULL);
	pthread_setname_np(pmapper_poller, "usb-pmapper");

	return 0;

err_libusb_deinit:
	libusb_exit(libusb_ctx);
	pthread_mutex_destroy(&pmapper_lock);
	return ret;
}

void usb_pmapper_deinit(void)
{
	if (!LIST_EMPTY(&pmapper_list)) {
		pr_err("BUG: HC backends don't delete registered pmappers!\n");
	}

	pmapper_exit = true;
	/* no need to notify */
	pthread_join(pmapper_poller, NULL);

	libusb_hotplug_deregister_callback(libusb_ctx, device_arrive_handle);
	libusb_hotplug_deregister_callback(libusb_ctx, device_left_handle);

	libusb_exit(libusb_ctx);
	pthread_mutex_destroy(&pmapper_lock);
}
