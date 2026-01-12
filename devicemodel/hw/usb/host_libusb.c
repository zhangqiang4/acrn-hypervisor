/*
 * Copyright (C) 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <libusb.h>

#define pr_prefix "usb: host: "
#include "usb/core.h"
#include "log.h"

static libusb_context *lctx;
static uint32_t lctx_refs;
static pthread_mutex_t lctx_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t libusb_event_handler;
static bool libusb_event_handle_exit;

// TODO use multiple threads for different devices. This thread will be the bottleneck to execute
// plenty of transfer done callbacks.
static void *handle_libusb_event(void *arg)
{
	while (!libusb_event_handle_exit) {
		struct timeval t = {1, 0};
		libusb_handle_events_timeout(lctx, &t);
	}
	pr_info("libusb event handler exited!\n");
	return NULL;
}


struct usb_host_dev_data {
	struct usb_device *dev;
	//libusb_context *libusb_ctx;
	libusb_device *ldev;
	libusb_device_handle *handle;

	struct libusb_device_descriptor dev_desc;
	int old_config;				/* old configuration before claiming */

	//struct usb_devpath path;
	uint8_t bus;
	uint8_t addr;
};

struct usb_host_xfer_data {
	/* we can't put it here to let usb_core allocate it together with usb_transfer since
	 * libusb provides allocation API but no init API. Shit! */
	struct libusb_transfer *lxfer;
	void *libusb_buf;
	uint32_t len;
	bool is_in;
};

static void usb_host_claim_config(struct usb_host_dev_data *pdata)
{
	struct libusb_config_descriptor *active_config;
	const struct libusb_interface_descriptor *lif_desc;
	if (libusb_get_active_config_descriptor(pdata->ldev, &active_config) == LIBUSB_SUCCESS) {
		for (int i = 0; i < active_config->bNumInterfaces; i++) {
			lif_desc = active_config->interface[i].altsetting;
			libusb_detach_kernel_driver(pdata->handle, lif_desc->bInterfaceNumber);
			libusb_claim_interface(pdata->handle, lif_desc->bInterfaceNumber);
		}
		libusb_free_config_descriptor(active_config);
	}
}

static void usb_host_release_config(struct usb_host_dev_data *pdata)
{
	struct libusb_config_descriptor *active_config;
	const struct libusb_interface_descriptor *lif_desc;
	if (libusb_get_active_config_descriptor(pdata->ldev, &active_config) == LIBUSB_SUCCESS) {
		for (int i = 0; i < active_config->bNumInterfaces; i++) {
			lif_desc = active_config->interface[i].altsetting;
			libusb_release_interface(pdata->handle, lif_desc->bInterfaceNumber);
		}
		libusb_free_config_descriptor(active_config);
	}
}

static void usb_host_restore_config(struct usb_host_dev_data *pdata, int config)
{
	usb_host_release_config(pdata);

	libusb_set_configuration(pdata->handle, config);

	struct libusb_config_descriptor *active_config;
	const struct libusb_interface_descriptor *lif_desc;
	if (libusb_get_active_config_descriptor(pdata->ldev, &active_config) == LIBUSB_SUCCESS) {
		for (int i = 0; i < active_config->bNumInterfaces; i++) {
			lif_desc = active_config->interface[i].altsetting;
			libusb_attach_kernel_driver(pdata->handle, lif_desc->bInterfaceNumber);
		}
		libusb_free_config_descriptor(active_config);
	}
}

static void libusb_xfer_done(struct libusb_transfer *lxfer)
{
	struct usb_transfer *xfer = lxfer->user_data;
	struct usb_device *dev = xfer->dev;
	struct usb_host_xfer_data *xfer_data = xfer->dev_priv;
	struct usb_host_dev_data *dev_data = dev->be_priv;

	pr_dbg("libusb transfer done, ep: 0x%x, stream: %d, status: %d, actual length: %d\n",
			xfer->ep, xfer->stream, lxfer->status, lxfer->actual_length);

	if (lxfer->status != LIBUSB_TRANSFER_COMPLETED) {
		pr_info("libusb transfer failed, type: %d, status: %d, len: %d, actual len: %d\n",
				lxfer->type, lxfer->status, lxfer->length, lxfer->actual_length);
	}
	
	enum usb_transfer_result map[] = {
		[LIBUSB_TRANSFER_COMPLETED] = USB_XFER_RES_OK,
		[LIBUSB_TRANSFER_ERROR] = USB_XFER_RES_ERR,
		[LIBUSB_TRANSFER_STALL] = USB_XFER_RES_STALL,
		[LIBUSB_TRANSFER_TIMED_OUT] = USB_XFER_RES_OK,
		[LIBUSB_TRANSFER_CANCELLED] = USB_XFER_RES_OK, /* ? */
		[LIBUSB_TRANSFER_NO_DEVICE] = USB_XFER_RES_NODEV,
		[LIBUSB_TRANSFER_OVERFLOW] = USB_XFER_RES_ERR,
	};
	xfer->result = map[lxfer->status];

	if (xfer->type == USB_XFER_TYPE_CTRL) {
		xfer->actual_len =  lxfer->actual_length + 8;

		/* 
		 * For GET_DESCRIPTOR request for config descriptor, hide the "remote wakeup" attribute
		 * to avoid guest triggers auto-suspend as there is no way for acrn-dm to capture
		 * the host device wakeup signal.
		 */
		struct libusb_control_setup *lsetup = libusb_control_transfer_get_setup(lxfer);
		unsigned char *data = libusb_control_transfer_get_data(lxfer);
		if (lsetup->bmRequestType == LIBUSB_ENDPOINT_IN &&
				lsetup->bRequest == LIBUSB_REQUEST_GET_DESCRIPTOR &&
				(lsetup->wValue >> 8) == LIBUSB_DT_CONFIG) {
			struct libusb_config_descriptor *config = (struct libusb_config_descriptor *)data;
			if (lxfer->actual_length >
					offsetof(struct libusb_config_descriptor, bmAttributes))
				config->bmAttributes &= ~USB_CONFIG_ATT_WAKEUP;
		}
	} else if (xfer->type == USB_XFER_TYPE_ISOC) {
		xfer->actual_len = lxfer->iso_packet_desc[0].actual_length;
		xfer->result = map[lxfer->iso_packet_desc[0].status];
	} else {
		xfer->actual_len =  lxfer->actual_length;
	}
	
	/* fill receive buffer */
	void *libusb_buf = xfer_data->libusb_buf;
	uint32_t left_len = xfer->actual_len;
	if (xfer_data->is_in) {
		int copied_len = 0;
		for (int i = 0; i < xfer->vec_count; i++) {
			int tmp_len = MIN(xfer->vecs[i].iov_len, left_len);
			memcpy(xfer->vecs[i].iov_base, libusb_buf + copied_len, tmp_len);
			left_len -= tmp_len;
			copied_len += tmp_len;
			if (!left_len)
				break;
		}
	}
	libusb_dev_mem_free(dev_data->handle, libusb_buf, xfer_data->len);
	xfer->done(xfer);	/* xfer_data freed ! */
	libusb_free_transfer(lxfer);
}

/* Handle transfers without operate the host device. */
static int usb_host_direct_xfer(struct usb_device *dev, struct usb_transfer *xfer)
{
	struct usb_host_dev_data *pdata = dev->be_priv;

	if (xfer->type != USB_XFER_TYPE_CTRL)
		return -1;

	struct libusb_control_setup *setup = xfer->vecs[0].iov_base;

#define REQ_W(req_type, req)	(req << 8 | req_type)
	switch (REQ_W(setup->bmRequestType, setup->bRequest)) {
	case REQ_W(LIBUSB_REQUEST_TYPE_STANDARD | LIBUSB_RECIPIENT_DEVICE, LIBUSB_REQUEST_SET_ADDRESS):
		pr_err("SET_ADDRESS request shall be filtered by xHCI!\n");
		dev->addr = setup->wValue;
		xfer->result = USB_XFER_RES_OK;
		break;
	case REQ_W(LIBUSB_REQUEST_TYPE_STANDARD | LIBUSB_RECIPIENT_DEVICE, LIBUSB_REQUEST_SET_CONFIGURATION):
		pr_dbg("device %d:%d set configure %d\n", pdata->bus, pdata->addr, setup->wValue);
		usb_host_release_config(pdata);
		if (libusb_set_configuration(pdata->handle, setup->wValue) == LIBUSB_SUCCESS) {
			usb_host_claim_config(pdata);
		} else {
			pr_err("failed to set config %d!\n", setup->wValue);
			xfer->result = USB_XFER_RES_ERR;
		}
		break;
	case REQ_W(LIBUSB_REQUEST_TYPE_STANDARD | LIBUSB_RECIPIENT_INTERFACE, LIBUSB_REQUEST_SET_INTERFACE):
	{
		uint8_t intf = setup->wIndex;
		uint8_t alt = setup->wValue;
		pr_dbg("device %d:%d interface %d alt setting changed to %d\n",
				pdata->bus, pdata->addr, intf, alt);
		if (libusb_set_interface_alt_setting(pdata->handle, intf, alt) != LIBUSB_SUCCESS) {
			pr_err("device %d:%d failed to set interface %d to alt setting %d!\n",
					pdata->bus, pdata->addr, intf, alt);
			xfer->result = USB_XFER_RES_ERR;
		} else {
			xfer->result = USB_XFER_RES_OK;
		}
		break;
	}
	case REQ_W(LIBUSB_REQUEST_TYPE_STANDARD | LIBUSB_RECIPIENT_ENDPOINT, LIBUSB_REQUEST_CLEAR_FEATURE):
		if (setup->wValue == 0) {
			pr_dbg("device %d:%d endpoint %d clear halt\n",
					pdata->bus, pdata->addr, setup->wIndex);
			if (libusb_clear_halt(pdata->handle, setup->wIndex)) {
				pr_err("device %d:%d endpoint %d, failed to clean halt!\n",
						pdata->bus, pdata->addr, setup->wIndex);
				xfer->result = USB_XFER_RES_ERR;
			} else {
				xfer->result = USB_XFER_RES_OK;
			}
		}
		break;
	default:
		return -1;
	}
	xfer->actual_len = usb_transfer_total_len(xfer);
	xfer->done(xfer);	/* xfer_data freed ! */
	return 0;
}

/* TBD add udmabuf support for USB devio API ? */
static void usb_transfer_host(struct usb_device *dev, struct usb_transfer *xfer)
{
	struct usb_host_dev_data *pdata = dev->be_priv;
	struct usb_host_xfer_data *xfer_priv = xfer->dev_priv;
	uint32_t total_len = usb_transfer_total_len(xfer);
	int isoc_packets = (xfer->type == USB_XFER_TYPE_ISOC);

	pr_dbg("handle xfer for %d:%d, ep: 0x%x, stream: %d, type: %d, vec_count: %d, len: %d\n",
			pdata->bus, pdata->addr, xfer->ep, xfer->stream, xfer->type,
			xfer->vec_count, total_len);

	if (!usb_host_direct_xfer(dev, xfer))
		return;

	struct libusb_transfer *lxfer = libusb_alloc_transfer(isoc_packets);
	if (!lxfer) {
		pr_err("failed to libusb_alloc_transfer\n");
		goto exit_xfer_done;
	}

	/* allocation fails if device has gone */
	void *buf = libusb_dev_mem_alloc(pdata->handle, total_len);
	if (!buf) {
		pr_dbg("failed to allocated memory for new transfer!\n");
		goto exit_free_xfer;
	}

	xfer_priv->lxfer = lxfer;
	xfer_priv->libusb_buf = buf;
	xfer_priv->len = total_len;

	if (xfer->type == USB_XFER_TYPE_CTRL) {
		struct libusb_control_setup *setup = xfer->vecs[0].iov_base;
		xfer_priv->is_in = setup->bmRequestType & 0x80;
		memcpy(buf, setup, 8);	// required for both in and out
	} else {
		xfer_priv->is_in = xfer->ep & 0x80;
	}

	int cur_len = 0;
	int index = 0;
	if (!xfer_priv->is_in) {
		for (int i = index; i < xfer->vec_count; i++) {
			memcpy(buf + cur_len, xfer->vecs[i].iov_base, xfer->vecs[i].iov_len);
			cur_len += xfer->vecs[i].iov_len;
		}
	}

	switch (xfer->type) {
	case USB_XFER_TYPE_CTRL:
		/* 
		 * Sometimes spend 10s+ on broken device. We have to complete the transfer
		 * in 5s otherwise guest takes ep as failed and sends stop ep command.
		 * TODO track outstanding transfers and cancel them on stop ep command.
		 */
		libusb_fill_control_transfer(lxfer, pdata->handle, buf,
				libusb_xfer_done, xfer, 100);
		break;
	case USB_XFER_TYPE_INT:
		libusb_fill_interrupt_transfer(lxfer, pdata->handle, xfer->ep,
				buf, total_len, libusb_xfer_done, xfer, 0);
		break;
	case USB_XFER_TYPE_BULK:
		if (xfer->stream)
			libusb_fill_bulk_stream_transfer(lxfer, pdata->handle, xfer->ep,
					xfer->stream, buf, total_len, libusb_xfer_done, xfer, 0);
		else
			libusb_fill_bulk_transfer(lxfer, pdata->handle, xfer->ep,
					buf, total_len, libusb_xfer_done, xfer, 0);
		break;
	case USB_XFER_TYPE_ISOC:
		libusb_fill_iso_transfer(lxfer, pdata->handle, xfer->ep,
				buf, total_len, isoc_packets,
				libusb_xfer_done, xfer, 0);
		libusb_set_iso_packet_lengths(lxfer, total_len);
		break;
	}

	int ret;
	if ((ret = libusb_submit_transfer(lxfer)) != LIBUSB_SUCCESS) {
		pr_err("failed to submit transfer, %s\n", libusb_strerror(ret));
		goto exit_free_xfer;
	}
	return;

exit_free_xfer:
	libusb_free_transfer(lxfer);
exit_xfer_done:
	xfer->result = USB_XFER_RES_ERR;
	xfer->actual_len = 0;
	xfer->done(xfer);
	return;
}

static void usb_cancel_transfer_host(struct usb_device *dev, struct usb_transfer *xfer)
{
	struct usb_host_xfer_data *xfer_priv = xfer->dev_priv;
	struct libusb_transfer *lxfer = xfer_priv->lxfer;

	libusb_cancel_transfer(lxfer);
}

static int usb_alloc_streams_host(struct usb_device *dev, uint32_t num_streams,
				uint8_t *eps, uint8_t num_eps)
{
	struct usb_host_dev_data *pdata = dev->be_priv;

	if (libusb_alloc_streams(pdata->handle, num_streams, eps, num_eps) != num_streams) {
		libusb_free_streams(pdata->handle, eps, num_eps);
		return -1;
	}
	return 0;
}

static void usb_free_streams_host(struct usb_device *dev, uint8_t *eps, uint8_t num_eps)
{
	struct usb_host_dev_data *pdata = dev->be_priv;
	libusb_free_streams(pdata->handle, eps, num_eps);
}

static enum usb_device_speed usb_speed_host(struct usb_device *dev)
{
	struct usb_host_dev_data *pdata = dev->be_priv;

	enum libusb_speed lspeed = libusb_get_device_speed(pdata->ldev);
	int map[] = {
		[LIBUSB_SPEED_UNKNOWN] = USB_SPEED_UNKNOWN,
		[LIBUSB_SPEED_LOW] = USB_SPEED_LOW,
		[LIBUSB_SPEED_FULL] = USB_SPEED_FULL,
		[LIBUSB_SPEED_HIGH] = USB_SPEED_HIGH,
		[LIBUSB_SPEED_SUPER] = USB_SPEED_SUPER,
		[LIBUSB_SPEED_SUPER_PLUS] = USB_SPEED_SUPER_PLUS,
	};
	return map[lspeed];
}

static bool usb_match_host(const char *opts)
{
	return !strncmp(opts, "usb-host", 8);
}

static int usb_host_parse_data(const char *opts, struct usb_host_dev_data *pdata)
{
	char *new, *tmp;
	new = tmp = strdup(opts);

	for (char *kv = strsep(&tmp, ","); kv; kv = strsep(&tmp, ",")) {
		if (!strcmp(kv, "usb-host"))
			continue;

		char *v = kv;
		char *k = strsep(&v, "=");
		if (!v) {
			pr_err("invalid key value pair: %s\n", kv);
			goto err;
		}

		char *end;
		if (!strncmp(k, "hostbus", 7)) {
			pdata->bus = strtoul(v, &end, 0);
			if (*end) {
				pr_err("invalid bus: %s\n", v);
				goto err;
			}
		} else if (!strncmp(k, "hostaddr", 8)) {
			pdata->addr = strtoul(v, &end, 0);
			if (*end) {
				pr_err("invalid address: %s\n", v);
				goto err;
			}
		}
	}
	free(new);
	return 0;
err:
	free(new);
	return -1;
}

static int usb_init_host(struct usb_device *dev, const char *opts)
{
	int ret;
	struct usb_host_dev_data *pdata = malloc(sizeof(struct usb_host_dev_data));

	if (usb_host_parse_data(opts, pdata)) {
		goto err_free;
	}
	pthread_mutex_lock(&lctx_lock);
	if (!lctx) {
		if ((ret = libusb_init(&lctx)) != LIBUSB_SUCCESS) {
			pr_err("failed to get a libusb context: %s\n", libusb_strerror(ret));
			goto err_free;
		}
		libusb_event_handle_exit = false;
		lctx_refs = 1;
		pthread_create(&libusb_event_handler, NULL, handle_libusb_event, NULL);
		pthread_setname_np(libusb_event_handler, "libusb-handler");
	} else {
		lctx_refs ++;
	}
	pthread_mutex_unlock(&lctx_lock);
	pdata->ldev = NULL;
	libusb_device **list;
	if ((ret = libusb_get_device_list(lctx, &list)) < 0) {
		pr_err("failed to get usb device list: %s\n", libusb_strerror(ret));
		goto err_libusb_exit;
	}
	for (int i = 0; list[i]; i++) {
		uint8_t bus = libusb_get_bus_number(list[i]);
		uint8_t addr = libusb_get_device_address(list[i]);
		if (bus == pdata->bus && addr == pdata->addr) {
			pdata->ldev = list[i];
			break;
		}
	}
	if (!pdata->ldev) {
		pr_err("host device %d:%d not exist!\n", pdata->bus, pdata->addr);
		libusb_free_device_list(list, 1);
		goto err_libusb_exit;
	}
	if ((ret = libusb_open(pdata->ldev, &pdata->handle)) != LIBUSB_SUCCESS) {
		pr_err("failed to open host device %d:%d: %s\n",
				pdata->bus, pdata->addr, libusb_strerror(ret));
		libusb_free_device_list(list, 1);
		goto err_libusb_exit;
	}
	libusb_free_device_list(list, 1);

	if ((ret = libusb_get_device_descriptor(pdata->ldev, &pdata->dev_desc)) != LIBUSB_SUCCESS) {
		pr_err("failed to read descriptor of %d:%d: %s\n",
				pdata->bus, pdata->addr, libusb_strerror(ret));
		goto err_libusb_exit;
	}

	if (libusb_get_configuration(pdata->handle, &pdata->old_config) != LIBUSB_SUCCESS) {
		goto err_libusb_exit;
	}

	usb_host_claim_config(pdata);

	pdata->dev = dev;
	dev->be_priv = pdata;

	return 0;

err_libusb_exit:
	pthread_mutex_lock(&lctx_lock);
	if (!--lctx_refs) {
		libusb_event_handle_exit = true;
		pthread_join(libusb_event_handler, NULL);
		libusb_exit(lctx);
		lctx = NULL;
	}
	pthread_mutex_unlock(&lctx_lock);
err_free:
	free(pdata);
	return -1;
}

static void usb_deinit_host(struct usb_device *dev)
{
	struct usb_host_dev_data *pdata = dev->be_priv;

	usb_host_restore_config(pdata, pdata->old_config);
	libusb_close(pdata->handle);
	pthread_mutex_lock(&lctx_lock);
	if (!--lctx_refs) {
		libusb_event_handle_exit = true;
		pthread_join(libusb_event_handler, NULL);
		libusb_exit(lctx);
		lctx = NULL;
	}
	pthread_mutex_unlock(&lctx_lock);
	dev->be_priv = NULL;
	free(pdata);
}

static int usb_reset_host(struct usb_device *dev)
{
	struct usb_host_dev_data *pdata = dev->be_priv;
	int ret;

	pr_dbg("reset device %d:%d\n", pdata->bus, pdata->addr);
	if ((ret = libusb_reset_device(pdata->handle)) != LIBUSB_SUCCESS) {
		pr_err("failed to reset %d:%d, %s\n",
				pdata->bus, pdata->addr, libusb_strerror(ret));
		return -1;
	}
	return 0;
}

struct usb_device_backend usb_backend_host = {
	.name = "usb-host",
	.dev_priv_size = sizeof(struct usb_host_dev_data),
	.xfer_priv_size = sizeof(struct usb_host_xfer_data),
	.reset = usb_reset_host,
	.speed = usb_speed_host,
	.transfer = usb_transfer_host,
	.cancel_transfer = usb_cancel_transfer_host,
	.alloc_streams = usb_alloc_streams_host,
	.free_streams = usb_free_streams_host,
	.match = usb_match_host,
	.init = usb_init_host,
	.deinit = usb_deinit_host,
};
