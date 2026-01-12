/*
 * Copyright (C) 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#ifndef __USB_CORE_H__
#define __USB_CORE_H__

#include "glib.h"
#include <bits/pthreadtypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/queue.h>
#include <sys/uio.h>
#include <linux/usb/ch9.h>

struct usb_hcd;
struct usb_device;
struct usb_transfer;

#define USB_MAX_TIERS 7
struct usb_devpath {
	/* aligned with libusb */
	uint8_t port[USB_MAX_TIERS];	/* Downstream port number in each level of hub.
					   0 terminated. port[0] is root hub port number.
					   At most first 6 numbers are non-zero since the
					   root hub itself occupied the first tier. */
};

bool usb_devpath_eq(struct usb_devpath a, struct usb_devpath b);
bool usb_devpath_has_prefix(struct usb_devpath path, struct usb_devpath prefix);
/* returned string needs to be freed with free(). */
char *usb_devpath_str(struct usb_devpath path);
char *usb_bus_devpath_str(uint8_t bus, struct usb_devpath path);
int usb_parse_bus_devpath_str(const char *str, uint8_t *bus, struct usb_devpath *path);

/* a downstream port of root hub or external hub */
struct usb_port {
	uint8_t index;			/* port index counting from 0 */
	struct usb_port *parent;	/* upper tier downstream port */
	struct usb_hub *hub;
	struct usb_device *device;	/* connected device. Protected by hub lock */
	void *priv;			/* implementation specific data */
};

/* root hub or external hub */
struct usb_hub {
	pthread_mutex_t lock;		/* manage port states */
	struct usb_port *ports;		/* downstream ports */
	uint8_t port_count;
	union {
		struct usb_device *dev;	/* usb device of external hub */
		struct usb_hcd *hcd;	/* host hcd */
	};
	bool is_roothub;
	bool usb2;
	char *name;

	/* hotplug notifiers */
	void (*on_device_add)(struct usb_hub *hub, uint8_t port_idx);
	void (*on_device_del)(struct usb_hub *hub, uint8_t port_idx);
};

int usb_hub_attach_device(struct usb_hub *hub, uint8_t port_idx, struct usb_device *dev);
struct usb_device *usb_hub_detach_device(struct usb_hub *hub, uint8_t port_idx);
void usb_hub_remove_device(struct usb_hub *hub, uint8_t port_idx);

struct usb_hcd {
	char *name;
	STAILQ_ENTRY(usb_hcd) list;
	pthread_mutex_t lock;
	pthread_cond_t cond;

	struct usb_hub hub2;
	struct usb_hub hub3;

	/* hardware address on bus, not the device address in linux */
	uint8_t next_dev_addr;
	struct usb_device *devices[128];

	/* TBD transfer scheduling ? */

	/* TBD bandwidth management? for better performance for periodic endpoints
	 * Available BW is defined from device's perspective. They involve different computation
	 * algorithms for physical devices and emulated devices */

	/* private data */
	uint32_t xfer_priv_size;
};

static inline const char *hcd_name(struct usb_hcd *hcd)
{
	return hcd->name;
}
static inline void usb_hcd_lock(struct usb_hcd *hcd)
{
	pthread_mutex_lock(&hcd->lock);
}
static inline void usb_hcd_unlock(struct usb_hcd *hcd)
{
	pthread_mutex_unlock(&hcd->lock);
}
static inline void usb_hcd_wait(struct usb_hcd *hcd)
{
	pthread_cond_wait(&hcd->cond, &hcd->lock);
}
static inline void usb_hcd_notify(struct usb_hcd *hcd)
{
	pthread_cond_signal(&hcd->cond);
}
static inline void usb_hcd_notify_all(struct usb_hcd *hcd)
{
	pthread_cond_broadcast(&hcd->cond);
}

uint8_t usb_hcd_alloc_dev_addr(struct usb_hcd *hcd, struct usb_device *dev);
struct usb_hcd *usb_hcd_alloc(size_t priv_size, uint8_t u2_port, uint8_t u3_port, const char *name);
int usb_hcd_destroy(struct usb_hcd *hcd);
int usb_hcd_register(struct usb_hcd *hcd);
int usb_hcd_unregister(struct usb_hcd *hcd);
struct usb_hcd *find_usb_hcd_by_name(const char *name);
static inline void *usb_hcd_get_priv(struct usb_hcd *hcd)
{
	return (void *)(hcd + 1);
}
static inline struct usb_hcd *usb_get_hcd_from_priv(void *priv)
{
	return (struct usb_hcd *)priv - 1;
}

static inline bool usb_device_speed_is_usb2(enum usb_device_speed speed)
{
	return (speed == USB_SPEED_LOW) || (speed == USB_SPEED_FULL) || (speed == USB_SPEED_HIGH);
}


struct usb_device_backend {
	const char *name;
	size_t dev_priv_size;
	size_t xfer_priv_size;
	bool (*match)(const char *opts); /* check if the options are for this kind of device */
	int (*init)(struct usb_device *dev, const char *opts);
	void (*deinit)(struct usb_device *dev);
	int (*reset)(struct usb_device *dev); /* may cause reconnect if failed */
	enum usb_device_speed (*speed)(struct usb_device *dev); /* for port state reporting */

	//void (*control_request)(struct usb_device *dev, struct usb_control_setup setup, struct usb_transfer *xfer);
	/* Asynchronous or not. But must be finished in order for one endpoint/stream */
	void (*transfer)(struct usb_device *dev, struct usb_transfer *xfer);
	void (*cancel_transfer)(struct usb_device *dev, struct usb_transfer *xfer);

	int (*alloc_streams)(struct usb_device *dev, uint32_t num_streams, uint8_t *eps, uint8_t num_eps);
	void (*free_streams)(struct usb_device *dev, uint8_t *eps, uint8_t num_eps);
};

// currently implemented device backends. TODO use a new data set DEFINE_USB_DEVTYPE.
extern struct usb_device_backend usb_backend_host;

struct usb_device {
	uint8_t addr;		/* from SET_ADDRESS request, not the device address in linux */
	struct usb_port *port;	/* upstream facing port the device is connected to */
	struct usb_hub *hub;	/* for external hub device */

	// HCD may reference USB device after device being forcefully disconnected
	gatomicrefcount ref;
	const char *id;
	STAILQ_ENTRY(usb_device) list;

	//struct usb_device_descriptor dev_desc;
	//struct usb_config *configs;

	struct usb_device_backend *backend;

	void *be_priv;		/* backend private data */
	//void *hcd_priv;		/* HCD private data */
};

struct usb_device *usb_device_create(struct usb_device_backend *backend, const char *opts);
void usb_device_destroy(struct usb_device *dev);
struct usb_device *usb_device_get(struct usb_device *dev);
struct usb_device *usb_device_get_from_id(const char *id);
void usb_device_put(struct usb_device *dev);
//struct usb_hcd *usb_device_get_hcd(struct usb_device *dev);

#if 0
int usb_hcd_connect_device(struct usb_hcd *hcd, struct usb_device *dev);
int usb_hcd_disconnect_device(struct usb_device *dev);
struct usb_device *usb_add_device(struct usb_hcd *hcd, struct usb_device_backend *backend, const char *opts);
void usb_del_device(struct usb_device *dev);
#endif




enum usb_transfer_type {
	USB_XFER_TYPE_CTRL,
	USB_XFER_TYPE_BULK,
	USB_XFER_TYPE_ISOC,
	USB_XFER_TYPE_INT,
};
enum usb_transfer_result {
	USB_XFER_RES_OK,
	USB_XFER_RES_ERR,
	USB_XFER_RES_NODEV,
	USB_XFER_RES_STALL,
};

struct usb_transfer {
	uint8_t ep;	/* endpoint 0 is bidirectional, bit 7 set for IN */
	uint16_t stream;
	uint16_t mps;	/* Max Packet Size, for isochronous transfers */
	enum usb_transfer_type type;

	struct usb_device *dev;
	struct iovec *vecs;	/* for control transfers, the first iovec is control setup,
				   followings are for data stage. Even they may be contiguous */
	uint32_t vec_capacity;
	uint32_t vec_count;

	uint32_t actual_len;	/* backend should take control setup header into account */
	enum usb_transfer_result result;

	void (*done)(struct usb_transfer *xfer);

	/* All the private data and usb_transfer structure should be allocated together */
	void *hc_priv;	/* HC private data */
	void *dev_priv; /* Device Backend private data. */
};

/* TBD maybe a local mempool is required */
struct usb_transfer *usb_transfer_alloc(struct usb_hcd *hcd, struct usb_device *dev,
		enum usb_transfer_type type,
		uint8_t ep, uint16_t stream, void (*done)(struct usb_transfer *xfer));
int usb_transfer_add_one_buf(struct usb_transfer *xfer, void *buf, uint32_t len);
void usb_transfer_free(struct usb_transfer *xfer);
static inline uint32_t usb_transfer_total_len(struct usb_transfer *xfer)
{
	uint32_t sum = 0;
	/* for control transfer first iovec is setup which shouldn't be accounted. */
	//int i = xfer->type == USB_XFER_TYPE_CTRL ? 1 : 0;
	for (int i = 0; i < xfer->vec_count; i++)
		sum += xfer->vecs[i].iov_len;
	return sum;
}

int usb_core_init(void);
void usb_core_deinit(void);

#endif
