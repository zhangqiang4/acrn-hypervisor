/*
 * Copyright (C) 2026 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/usb/ch9.h>
#include <linux/usbdevice_fs.h>

#define pr_prefix "usb: host: "
#include "usb/core.h"
#include "log.h"
#include "mevent.h"

// TODO endian check

struct usb_interface {
	struct usb_interface_descriptor **descs;
	int altsettings;
};

struct usb_config {
	struct usb_config_descriptor *desc;
	struct usb_interface *ifs;
	unsigned if_num;
};

struct usb_descs {
	void *raw_descriptors;
	size_t descriptors_len;
	struct usb_device_descriptor *device_desc;
	struct usb_config *configs;
	unsigned config_num;
};

struct usb_host_dev_data {
	struct usb_device *dev;
	int fd;
	struct mevent *reap_event;

	struct usb_descs descs;
	uint8_t old_config;				/* old configuration before claiming */
	struct usb_config *cur_usb_config;

	pthread_mutex_t lock;	// lock for outstanding xfer list. TODO use atomic list
	CIRCLEQ_HEAD(, usb_host_xfer_data) outstanding_xfers; // Multi Producer One Consumer

	enum usb_device_speed speed;
	//struct usb_devpath path;
	uint8_t bus;
	uint8_t addr;
};

struct usb_host_xfer_data {
	void *buf;
	uint32_t len;
	bool is_in;
	struct usbdevfs_urb *urb;
	CIRCLEQ_ENTRY(usb_host_xfer_data) list;
};


static enum usb_device_speed usb_speed_host(struct usb_device *dev)
{
	struct usb_host_dev_data *pdata = dev->be_priv;
	return pdata->speed;
}

static int usb_host_get_config(struct usb_host_dev_data *pdata)
{
	uint8_t config;
	struct usbdevfs_ctrltransfer ctrl = {
		.bRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
		.bRequest = USB_REQ_GET_CONFIGURATION,
		.wValue = 0,
		.wLength = 1,
		.timeout = 1000,	// 1s
		.data = &config,
	};
	if (ioctl(pdata->fd, USBDEVFS_CONTROL, &ctrl) < 0)
		return -1;

	return config;
}

static void usb_host_claim_config(struct usb_host_dev_data *pdata)
{
	struct usb_config *config = pdata->cur_usb_config;
	if (config) {
		for (int i = 0; i < config->if_num; i++) {
			const struct usb_interface_descriptor *if_desc = config->ifs[i].descs[0];
			unsigned int ifno = if_desc->bInterfaceNumber;

#if 0
			/* disconnect kernel driver if present */
			struct usbdevfs_getdriver getdrv;
			getdrv.interface = ifno;
			int r = ioctl(pdata->fd, USBDEVFS_GETDRIVER, &getdrv);
			if (r != 0 || strcmp(getdrv.driver, "usbfs")) {
				struct usbdevfs_ioctl disconn_ioctl = {
					.ioctl_code = USBDEVFS_DISCONNECT,
					.ifno = ifno,
					.data = NULL,
				};
				pr_dbg("disconnect %d:%d from driver %s\n",
						pdata->bus, pdata->addr, getdrv.driver);
				ioctl(pdata->fd, USBDEVFS_IOCTL, &disconn_ioctl);
			}
#endif

			struct usbdevfs_disconnect_claim dc = {
				.driver = "usbfs",
				.flags = USBDEVFS_DISCONNECT_CLAIM_EXCEPT_DRIVER,
				.interface = ifno,
			};
			
			/* disconnect kernel driver and claim this interface */
			if (ioctl(pdata->fd, USBDEVFS_DISCONNECT_CLAIM, &dc))
				pr_err("%d:%d config %d: failed to claim interface %d\n",
						pdata->bus, pdata->addr,
						config->desc->bConfigurationValue, ifno);
			else
				pr_dbg("%d:%d config %d: claimed interface %d\n",
					pdata->bus, pdata->addr,
					config->desc->bConfigurationValue, ifno);
		}
	} else {
		pr_dbg("skip claiming interfaces as device is not configured!\n");
	}
}

static void usb_host_release_config(struct usb_host_dev_data *pdata)
{
	struct usb_config *config = pdata->cur_usb_config;
	if (config) {
		for (int i = 0; i < config->if_num; i++) {
			const struct usb_interface_descriptor *if_desc = config->ifs[i].descs[0];
			unsigned int ifno = if_desc->bInterfaceNumber;
			if (ioctl(pdata->fd, USBDEVFS_RELEASEINTERFACE, &ifno))
				pr_err("%d:%d config %d: failed to release interface %d\n",
						pdata->bus, pdata->addr,
						config->desc->bConfigurationValue, ifno);
			else
				pr_dbg("%d:%d config %d: released interface %d\n",
					pdata->bus, pdata->addr,
					config->desc->bConfigurationValue, ifno);
		}
	} else {
		pr_dbg("skip releasing interfaces as device is not configured!\n");
	}
}

static int usb_host_set_config(struct usb_host_dev_data *pdata, unsigned int config)
{
	ioctl(pdata->fd, USBDEVFS_SETCONFIGURATION, &config);

	int new_config = usb_host_get_config(pdata);
	if (new_config < 0)
		return -1;

	pdata->cur_usb_config = NULL;
	for (int i = 0; i < pdata->descs.config_num; i++) {
		struct usb_config *usb_config = &pdata->descs.configs[i];
		if (new_config == usb_config->desc->bConfigurationValue)
			pdata->cur_usb_config = usb_config;
		// may be not configured.
	}
	return 0;
}

static void usb_host_restore_config(struct usb_host_dev_data *pdata, int config)
{
	usb_host_release_config(pdata);

	usb_host_set_config(pdata, config);
}


/* Handle transfers without submitting urb. */
static int usb_host_direct_xfer(struct usb_device *dev, struct usb_transfer *xfer)
{
	struct usb_host_dev_data *pdata = dev->be_priv;

	if (xfer->type != USB_XFER_TYPE_CTRL)
		return -1;

	struct usb_ctrlrequest *setup = xfer->vecs[0].iov_base;

#define REQ_W(req_type, req)	(req << 8 | req_type)
	switch (REQ_W(setup->bRequestType, setup->bRequest)) {
	case REQ_W(USB_TYPE_STANDARD| USB_RECIP_DEVICE, USB_REQ_SET_ADDRESS):
		pr_err("SET_ADDRESS request shall be filtered by xHCI!\n");
		dev->addr = setup->wValue;
		xfer->result = USB_XFER_RES_OK;
		break;
	case REQ_W(USB_TYPE_STANDARD| USB_RECIP_DEVICE, USB_REQ_SET_CONFIGURATION):
		pr_dbg("device %d:%d set configure %d\n", pdata->bus, pdata->addr, setup->wValue);
		usb_host_release_config(pdata);
		if (usb_host_set_config(pdata, setup->wValue) == 0) {
			usb_host_claim_config(pdata);
		} else {
			pr_err("failed to set config %d!\n", setup->wValue);
			xfer->result = USB_XFER_RES_ERR;
		}
		break;
	case REQ_W(USB_TYPE_STANDARD | USB_RECIP_INTERFACE, USB_REQ_SET_INTERFACE):
	{
		uint8_t intf = setup->wIndex;
		uint8_t alt = setup->wValue;
		pr_dbg("device %d:%d interface %d alt setting changed to %d\n",
				pdata->bus, pdata->addr, intf, alt);
		mock_delay(2000);
		struct usbdevfs_setinterface setif = {
			.interface = intf,
			.altsetting = alt,
		};
		if (ioctl(pdata->fd, USBDEVFS_SETINTERFACE, &setif) < 0) {
			pr_err("device %d:%d failed to set interface %d to alt setting %d!\n",
					pdata->bus, pdata->addr, intf, alt);
			xfer->result = USB_XFER_RES_ERR;
		} else {
			xfer->result = USB_XFER_RES_OK;
		}
		break;
	}
	case REQ_W(USB_TYPE_STANDARD | USB_RECIP_ENDPOINT, USB_REQ_CLEAR_FEATURE):
		if (setup->wValue == 0) {
			pr_dbg("device %d:%d endpoint %d clear halt\n",
					pdata->bus, pdata->addr, setup->wIndex);
			unsigned int ep = setup->wIndex;
			if (ioctl(pdata->fd, USBDEVFS_CLEAR_HALT, &ep) < 0) {
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


static void usb_transfer_host(struct usb_device *dev, struct usb_transfer *xfer)
{
	struct usb_host_dev_data *pdata = dev->be_priv;
	struct usb_host_xfer_data *xfer_priv = xfer->dev_priv;
	uint32_t total_len = usb_transfer_total_len(xfer);

	pr_dbg("handle xfer for %d:%d, ep: 0x%x, stream: %d, type: %d, vec_count: %d, len: %d\n",
			pdata->bus, pdata->addr, xfer->ep, xfer->stream, xfer->type,
			xfer->vec_count, total_len);

	if (!usb_host_direct_xfer(dev, xfer))
		return;

	void *buf = mmap(NULL, total_len, PROT_READ | PROT_WRITE, MAP_SHARED, pdata->fd, 0);
	if (buf == MAP_FAILED) {
		pr_err("failed to mmap transfer buffer!\n");
		goto exit_xfer_done;
	}
	struct usbdevfs_urb *urb = calloc(1, sizeof(*urb) + sizeof(struct usbdevfs_iso_packet_desc));
	if (!urb) {
		pr_err("failed to allocate urb!\n");
		goto exit_unmap;
	}

	xfer_priv->buf = buf;
	xfer_priv->len = total_len;
	xfer_priv->urb = urb;

	if (xfer->type == USB_XFER_TYPE_CTRL) {
		struct usb_ctrlrequest *setup = xfer->vecs[0].iov_base;
		xfer_priv->is_in = setup->bRequestType & USB_ENDPOINT_DIR_MASK;
		memcpy(buf, setup, sizeof(struct usb_ctrlrequest));	// required for both in and out
	} else {
		xfer_priv->is_in = xfer->ep & USB_ENDPOINT_DIR_MASK;
	}

	int cur_len = 0;
	int index = 0;
	if (!xfer_priv->is_in) {
		for (int i = index; i < xfer->vec_count; i++) {
			memcpy(buf + cur_len, xfer->vecs[i].iov_base, xfer->vecs[i].iov_len);
			cur_len += xfer->vecs[i].iov_len;
		}
	}

	urb->usercontext = xfer;
	urb->endpoint = xfer->ep;
	urb->stream_id = xfer->stream;
	urb->buffer = buf;
	urb->buffer_length = total_len;

	switch (xfer->type) {
	case USB_XFER_TYPE_CTRL:
		urb->type = USBDEVFS_URB_TYPE_CONTROL;
		urb->endpoint = 0;
		urb->stream_id = 0;
		break;
	case USB_XFER_TYPE_INT:
		urb->type = USBDEVFS_URB_TYPE_INTERRUPT;
		urb->stream_id = 0;
		break;
	case USB_XFER_TYPE_BULK:
		urb->type = USBDEVFS_URB_TYPE_BULK;
		break;
	case USB_XFER_TYPE_ISOC:
		urb->iso_frame_desc[0].length = total_len;
		urb->type = USBDEVFS_URB_TYPE_ISO;
		urb->flags = USBDEVFS_URB_ISO_ASAP;
		urb->number_of_packets = 1;
		break;
	}

	int r = ioctl(pdata->fd, USBDEVFS_SUBMITURB, urb);
	if (r < 0) {
		pr_err("failed to submit urb!\n");
		goto exit_free_urb;
	}
	pthread_mutex_lock(&pdata->lock);
	CIRCLEQ_INSERT_TAIL(&pdata->outstanding_xfers, xfer_priv, list);
	pthread_mutex_unlock(&pdata->lock);

	return;

exit_free_urb:
	free(urb);
exit_unmap:
	munmap(buf, total_len);
exit_xfer_done:
	xfer->result = USB_XFER_RES_ERR;
	xfer->actual_len = 0;
	xfer->done(xfer);
}

static void usbfs_reap_one(struct usb_host_dev_data *pdata, struct usb_transfer *xfer)
{
	struct usb_host_xfer_data *xfer_priv = xfer->dev_priv;
	struct usbdevfs_urb *urb = xfer_priv->urb;
	int status = urb->status;
	int actual_length = urb->actual_length;

	if (xfer->type == USB_XFER_TYPE_ISOC) {
		actual_length = urb->iso_frame_desc[0].actual_length;
		status = urb->iso_frame_desc[0].status;
	}

	pr_dbg("transfer done, ep: 0x%x, stream: %d, status: %d, actual length: %d\n",
			xfer->ep, xfer->stream, status, actual_length);

	if (status) {
		pr_info("transfer failed, type: %d, status: %d, len: %d, actual len: %d\n",
				urb->type, urb->status, urb->buffer_length, actual_length);
	}
	
	switch (status) {
	case 0:
	case -ENOENT: /* cancelled */
	case -ECONNRESET:
		xfer->result = USB_XFER_RES_OK;
		break;
	case -ENODEV:
	case -ESHUTDOWN:
		xfer->result = USB_XFER_RES_NODEV;
		break;
	case -EPIPE:
		xfer->result = USB_XFER_RES_STALL;
		break;
	case -EOVERFLOW:	// add this error type to core?
	case -ETIME:
	case -EPROTO:
	case -EILSEQ:
	case -ECOMM:
	case -ENOSR:
	case -EXDEV:
	default:
		xfer->result = USB_XFER_RES_ERR;
		break;
	}

	xfer->actual_len = actual_length;
	if (xfer->type == USB_XFER_TYPE_CTRL) {
		xfer->actual_len = actual_length + 8;

		/* 
		 * For GET_DESCRIPTOR request for config descriptor, hide the "remote wakeup" attribute
		 * to avoid guest triggers auto-suspend as there is no way for acrn-dm to capture
		 * the host device wakeup signal.
		 */
		struct usbdevfs_ctrltransfer *setup = urb->buffer;
		unsigned char *data = urb->buffer + 8;
		if (setup->bRequestType == (USB_ENDPOINT_DIR_MASK | USB_RECIP_DEVICE | USB_TYPE_STANDARD)  &&
				setup->bRequest == USB_REQ_GET_DESCRIPTOR &&
				(setup->wValue >> 8) == USB_DT_CONFIG) {
			struct usb_config_descriptor *config = (struct usb_config_descriptor *)data;
			if (actual_length > offsetof(struct usb_config_descriptor, bmAttributes))
				config->bmAttributes &= ~USB_CONFIG_ATT_WAKEUP;
		}
	} 

	/* fill receive buffer */
	void *buf = xfer_priv->buf;
	uint32_t left_len = xfer->actual_len;
	if (xfer_priv->is_in) {
		int copied_len = 0;
		for (int i = 0; i < xfer->vec_count; i++) {
			int tmp_len = MIN(xfer->vecs[i].iov_len, left_len);
			memcpy(xfer->vecs[i].iov_base,  buf + copied_len, tmp_len);
			left_len -= tmp_len;
			copied_len += tmp_len;
			if (!left_len)
				break;
		}
	}
	munmap(xfer_priv->buf, xfer_priv->len);
	free(xfer_priv->urb);
	xfer->done(xfer);	/* xfer_priv freed as part of xfer ! */
}

static void usbfs_reap_device_xfers(int fd, enum ev_type ev_type, void *param)
{
	int r;
	struct usb_host_dev_data *pdata = param;
	struct usbdevfs_urb *urb;

	while (!(r = ioctl(pdata->fd, USBDEVFS_REAPURBNDELAY, &urb))) {
		struct usb_transfer *xfer = urb->usercontext;
		usbfs_reap_one(pdata, xfer);
	}

	if (errno == ENODEV) {
		// device disconnected. hotplugger will eject and destroy the virtual usb device.
	} else if (errno != EAGAIN) {
		pr_err("reap failed, errno %d\n", errno);
	}
}

static void usb_cancel_transfer_host(struct usb_device *dev, struct usb_transfer *xfer)
{
	struct usb_host_dev_data *pdata = dev->be_priv;
	struct usb_host_xfer_data *xfer_priv = xfer->dev_priv;

	ioctl(pdata->fd, USBDEVFS_DISCARDURB, xfer_priv->urb);
}

static int usb_alloc_streams_host(struct usb_device *dev, uint32_t num_streams,
				uint8_t *eps, uint8_t num_eps)
{
	struct usb_host_dev_data *pdata = dev->be_priv;

	struct usbdevfs_streams *streams = malloc(sizeof(*streams) + num_eps);
	if (!streams)
		return -1;
	streams->num_streams = num_streams;
	streams->num_eps = num_eps;
	memcpy(streams->eps, eps, num_eps);

	int r = ioctl(pdata->fd, USBDEVFS_ALLOC_STREAMS, streams);

	free(streams);

	return r;
}

static void usb_free_streams_host(struct usb_device *dev, uint8_t *eps, uint8_t num_eps)
{
	struct usb_host_dev_data *pdata = dev->be_priv;

	struct usbdevfs_streams *streams = malloc(sizeof(*streams) + num_eps);
	if (!streams)
		return;
	streams->num_streams = 0;
	streams->num_eps = num_eps;
	memcpy(streams->eps, eps, num_eps);

	ioctl(pdata->fd, USBDEVFS_FREE_STREAMS, streams);

	free(streams);
}

static bool usb_match_host(const char *opts)
{
	return !strncmp(opts, "usb-host", 8);
}

static int usb_reset_host(struct usb_device *dev)
{
	struct usb_host_dev_data *pdata = dev->be_priv;

	if (ioctl(pdata->fd, USBDEVFS_RESET)) {
		pr_err("failed to reset %d:%d\n", pdata->bus, pdata->addr);
		return -1;
	}
	pr_dbg("reset %d:%d\n", pdata->bus, pdata->addr);

	int config = usb_host_get_config(pdata);
	if (config < 0) {
		pr_err("failed to get current configuration!\n");
		return -1;
	}

	for (int i = 0; i < pdata->descs.config_num; i++) {
		struct usb_config *usb_config = &pdata->descs.configs[i];
		if (config == usb_config->desc->bConfigurationValue)
			pdata->cur_usb_config = usb_config;
		// may be not configured.
	}

	usb_host_claim_config(pdata);

	return 0;
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

static int parse_interface(struct usb_interface *ifp, void *ptr, unsigned remaining)
{
	int parsed = 0;
	int interface_num = -1;

	while (remaining >= sizeof(struct usb_interface_descriptor)) {
		ifp->descs = reallocarray(ifp->descs, ifp->altsettings + 1,
				sizeof(struct usb_interface_descriptor*));
		if (!ifp->descs) {
			pr_err("no memory for interface descriptors!\n");
			return -1;
		}

		struct usb_interface_descriptor *if_desc = ptr;
		if (if_desc->bDescriptorType != USB_DT_INTERFACE) {
			pr_err("unexpected descriptor type %d!\n", if_desc->bDescriptorType);
			goto free_descs;
		} else if (if_desc->bLength < sizeof(struct usb_descriptor_header)) {
			pr_err("invalid interface descriptor bLength!\n");
			goto free_descs;
		} else if (if_desc->bLength > remaining) {
			pr_warn("short interface descriptor\n");
			parsed += remaining;
			break;
		} else if (if_desc->bNumEndpoints > 32) {
			pr_err("too many endpoints!\n");
			goto free_descs;
		}

		ptr += sizeof(struct usb_interface_descriptor);
		parsed += sizeof(struct usb_interface_descriptor);
		remaining -= sizeof(struct usb_interface_descriptor);
		
		while (remaining >= sizeof(struct usb_descriptor_header)) {
			const struct usb_descriptor_header *header = ptr;
			if (header->bLength < sizeof(struct usb_descriptor_header)) {
				pr_err("invalid descriptor bLength!\n");
				goto free_descs;
			} else if (header->bLength > remaining) {
				pr_warn("short descriptor\n");
				parsed += remaining;
				goto done;
			}

			if (header->bDescriptorType == USB_DT_INTERFACE ||
					header->bDescriptorType == USB_DT_CONFIG ||
					header->bDescriptorType == USB_DT_DEVICE)
				break;

			ptr += header->bLength;
			parsed += header->bLength;
			remaining -= header->bLength;
		}

		ifp->descs[ifp->altsettings++] = if_desc;
		if (interface_num == -1)
			interface_num = if_desc->bInterfaceNumber;

		if_desc = ptr;
		if (remaining < sizeof(struct usb_interface_descriptor) ||
				if_desc->bDescriptorType != USB_DT_INTERFACE ||
				if_desc->bInterfaceNumber != interface_num)
			break;
	}
done:
	return parsed;
free_descs:
	free(ifp->descs);
	return -1;
}

static void free_interface(struct usb_interface *ifp)
{
	free(ifp->descs);
}

static int parse_config(struct usb_config *config, void *ptr, unsigned remaining)
{
	int parsed = 0;
	if (remaining < sizeof(struct usb_config_descriptor)) {
		pr_err("too short config descriptor!\n");
		return -1;
	}

	config->desc = ptr;

	if (config->desc->bDescriptorType != USB_DT_CONFIG) {
		pr_err("invalid config descriptor bDescriptorType\n");
		return -1;
	} else if (config->desc->bLength < USB_DT_CONFIG_SIZE) {
		pr_err("invalid config descriptor bLength\n");
		return -1;
	}
	uint16_t total_len = le16toh(config->desc->wTotalLength);
	if (total_len < USB_DT_CONFIG_SIZE) {
		pr_err("invalid config descriptor wTotalLength\n");
		return -1;
	}
	if (remaining < total_len) {
		pr_err("incomplete config descriptor\n");
		return -1;
	}
	if (config->desc->bConfigurationValue == 0) {
		pr_warn("device has invalid configuration 0\n");
	}
	if (config->desc->bNumInterfaces > 32) {
		pr_err("too many interfaces!\n");
		return -1;
	}

	ptr += sizeof(struct usb_config_descriptor);
	parsed += sizeof(struct usb_config_descriptor);
	remaining -= sizeof(struct usb_config_descriptor);

	config->if_num = config->desc->bNumInterfaces;
	config->ifs = calloc(config->desc->bNumInterfaces, sizeof(struct usb_interface));
	if (!config->ifs) {
		pr_err("no memory for interfaces!\n");
		return -1;
	}

	for (int i = 0; i < config->desc->bNumInterfaces; i ++) {
		/* skip unknown descriptors */
		while (remaining >= sizeof(struct usb_descriptor_header)) {
			const struct usb_descriptor_header *header = ptr;
			if (header->bLength < sizeof(struct usb_descriptor_header)) {
				pr_err("invalid descriptor bLength!\n");
				goto free_ifs;
			} else if (header->bLength > remaining) {
				pr_warn("short descriptor\n");
				parsed += remaining;
				remaining = 0;
				goto done;
			}

			if (header->bDescriptorType == USB_DT_INTERFACE ||
					header->bDescriptorType == USB_DT_ENDPOINT ||
					header->bDescriptorType == USB_DT_CONFIG ||
					header->bDescriptorType == USB_DT_DEVICE)
				break;

			ptr += header->bLength;
			parsed += header->bLength;
			remaining -= header->bLength;
		}

		int if_parsed = parse_interface(&config->ifs[i], ptr, remaining);
		if (if_parsed < 0) {
			// free parsed interfaces
			for (int j = i - 1; j > 0; j--) {
				free_interface(&config->ifs[j]);
			}
			goto free_ifs;
		} else if (if_parsed == 0) {
			config->if_num = i;
			break;
		}

		ptr += if_parsed;
		parsed += if_parsed;
		remaining -= if_parsed;
	}
done:
	return parsed;

free_ifs:
	free(config->ifs);
	return -1;
}

static void free_config(struct usb_config *config)
{
	for (int i = 0; i < config->if_num; i++) {
		free_interface(&config->ifs[i]);
	}

	free(config->ifs);
}

static int usb_parse_descriptors(struct usb_host_dev_data *pdata) {
	int alloc_len = 0;
	do {
		const unsigned batch_size = 256;
		alloc_len += batch_size;
		pdata->descs.raw_descriptors = realloc(pdata->descs.raw_descriptors, alloc_len);
		if (!pdata->descs.raw_descriptors) {
			pr_err("no memory for descriptors!\n");
			return -1;
		}
		uint8_t *read_ptr = pdata->descs.raw_descriptors + pdata->descs.descriptors_len;
		memset(read_ptr, 0, batch_size);
		int read_num = read(pdata->fd, read_ptr, batch_size);
		if (read_num < 0) {
			pr_err("failed to read descriptors!\n");
			goto free_descs;
		}
		pdata->descs.descriptors_len += read_num;
	} while (alloc_len == pdata->descs.descriptors_len);

	if (pdata->descs.descriptors_len < sizeof(struct usb_device_descriptor)) {
		pr_err("too short device descriptor!\n");
		goto free_descs;
	}
	pdata->descs.device_desc = pdata->descs.raw_descriptors;
	void *ptr = pdata->descs.raw_descriptors + sizeof(struct usb_device_descriptor);
	unsigned remaining = pdata->descs.descriptors_len - sizeof(struct usb_device_descriptor);

	pdata->descs.configs = calloc(pdata->descs.device_desc->bNumConfigurations, sizeof(struct usb_config));
	if (!pdata->descs.configs) {
		pr_err("no memory for configs!\n");
		goto free_descs;
	}
	pdata->descs.config_num = pdata->descs.device_desc->bNumConfigurations;

	for (int i = 0; i < pdata->descs.device_desc->bNumConfigurations; i++) {
		struct usb_config *config = &pdata->descs.configs[i];
		int parsed = parse_config(config, ptr, remaining);
		if (parsed < 0) {
			// free parsed configs
			for (int j = i - 1; j > 0; j--)
				free_config(&pdata->descs.configs[j]);
			goto free_configs;
		} else if (parsed == 0) {
			pdata->descs.config_num = i;
			break;
		}

		ptr += parsed;
		remaining -= parsed;
	}

	return 0;

free_configs:
	free(pdata->descs.configs);
free_descs:
	free(pdata->descs.raw_descriptors);
	return -1;

}

static void usb_free_descriptors(struct usb_host_dev_data *pdata)
{
	for (int i = 0; i < pdata->descs.config_num; i++) {
		struct usb_config *config = &pdata->descs.configs[i];
		free_config(config);
	}
	free(pdata->descs.configs);
	free(pdata->descs.raw_descriptors);

}

static int usb_init_host(struct usb_device *dev, const char *opts)
{
	struct usb_host_dev_data *pdata = calloc(sizeof(struct usb_host_dev_data), 1);

	if (usb_host_parse_data(opts, pdata)) {
		goto err_free;
	}
	CIRCLEQ_INIT(&pdata->outstanding_xfers);
	pthread_mutex_init(&pdata->lock, NULL);

	char path[32];
	snprintf(path, sizeof(path), "/dev/bus/usb/%03d/%03d", pdata->bus, pdata->addr);
	pdata->fd = open(path, O_RDWR);
	if (pdata->fd < 0) {
		pr_err("failed to open %s\n", path);
		goto err_free;
	}
	pdata->reap_event = mevent_add(pdata->fd, EVF_WRITE, usbfs_reap_device_xfers, pdata, NULL, NULL);
	if (!pdata->reap_event) {
		pr_err("failed to create usb reaper mevent!\n");
		goto err_close;
	}

	/* TODO check presence of USBDEVFS_CAP_BULK_SCATTER_GATHER. */

	if (usb_parse_descriptors(pdata))
		goto err_mevent_del;

	int config = usb_host_get_config(pdata);
	if (config < 0) {
		pr_err("failed to get current configuration!\n");
		goto free_descs;
	}
	pdata->old_config = config;
	pr_dbg("usb %d:%d old config is %d\n", pdata->bus, pdata->addr, config);

	for (int i = 0; i < pdata->descs.config_num; i++) {
		struct usb_config *usb_config = &pdata->descs.configs[i];
		if (config == usb_config->desc->bConfigurationValue)
			pdata->cur_usb_config = usb_config;
		// may be not configured.
	}

	int speed = ioctl(pdata->fd, USBDEVFS_GET_SPEED);
	if (speed < 0) {
		pr_err("failed to get speed\n");
		goto free_descs;
	}
	pdata->speed = speed;

	usb_host_claim_config(pdata);

	dev->be_priv = pdata;

	return 0;

free_descs:
	usb_free_descriptors(pdata);
err_mevent_del:
	mevent_delete(pdata->reap_event);
err_close:
	close(pdata->fd);
err_free:
	free(pdata);
	return -1;
}

static void usb_deinit_host(struct usb_device *dev)
{
	struct usb_host_dev_data *pdata = dev->be_priv;

	usb_host_restore_config(pdata, pdata->old_config);
	usb_free_descriptors(pdata);
	mevent_delete(pdata->reap_event);

	close(pdata->fd);
	dev->be_priv = NULL;
	free(pdata);
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
