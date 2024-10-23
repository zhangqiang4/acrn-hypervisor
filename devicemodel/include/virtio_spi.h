/*
 * Project Acrn
 * Acrn-dm: virtio-spi、
 *
 * Copyright (C) 2019-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#ifndef	__VIRTIO_SPI__
#define	__VIRTIO_SPI__

#include <stdint.h>

/* result code definitions */
#define VIRTIO_SPI_TRANS_OK	0
#define VIRTIO_SPI_PARAM_ERR	1
#define VIRTIO_SPI_TRANS_ERR	2

/* SPI device IRQ status */
#define VIRTIO_SPI_IRQ_STATUS_VALID	0
#define VIRTIO_SPI_IRQ_STATUS_INVALID	1

struct virtio_spi_transfer_req {
	struct virtio_spi_transfer_head *head;
	uint8_t *tx_buf;
	uint32_t tx_buf_size;
	uint8_t *rx_buf;
	uint32_t rx_buf_size;
};

enum vspidev_type {
	VSPIDEV_TYPE_NULL = 0,
	VSPIDEV_TYPE_LOOPBACK,
	VSPIDEV_TYPE_PHYSICAL,
	VSPIDEV_TYPE_TCP,
	VSPIDEV_TYPE_VMCU,
};

struct vspidev;

struct vspidev_be {
	enum vspidev_type type;
	char *name;
	int (*init)(struct vspidev *vspidev, char *opts);
	void (*deinit)(struct vspidev *vspidev);
	uint8_t (*transfer)(struct vspidev *vspidev, struct virtio_spi_transfer_req *req);
};

/*
 * SPI device attached a Virtio SPI controller
 */
struct vspidev {
	struct virtio_spi *vspi;
	int cs;			/* virtual cs */
	int type;
	struct vspidev_be *be;
	void *priv;

	/* TODO move to virtio spi, since this is vspi specific */
	bool irq_pending;
	bool irq_enabled;
	int evtq_idx;		/* the desc index to return to used ring */
	uint8_t *irq_status;	/* the status in the response descriptor */
};

void vspidev_inject_irq(struct vspidev *vspidev, uint8_t irq_status);

#endif
