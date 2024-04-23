/*
 * Copyright (C) 2024 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include <errno.h>
#include <error.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "shmem.h"

#define MAX_VECTORS 2

#define IVSHMEM_BAR0_SIZE  256

struct uio_irq_data {
	int fd;
	int vector;
};
#define UIO_IRQ_DATA _IOW('u', 100, struct uio_irq_data)

struct ivshm_regs {
	uint32_t int_mask;
	uint32_t int_status;
	uint32_t ivpos;
	uint32_t doorbell;
};

static int shmem_open(const char *devpath, struct shmem_info *info, int evt_fds[], int nr_ent_fds)
{
	int i;
	int uio_fd, bar0_fd, bar2_fd;
	char *uio_devname;
	char sysfs_path[64];
	struct uio_irq_data irq_data;
	struct ivshm_regs *regs;
	struct stat stat;

	uio_fd = open(devpath, O_RDWR);
	if (uio_fd < 0)
		error(1, errno, "cannot open %s", devpath);

	uio_devname = strstr(devpath, "/uio");
	snprintf(sysfs_path, sizeof(sysfs_path), "/sys/class/uio%s/device/resource0", uio_devname);

	bar0_fd = open(sysfs_path, O_RDWR);
	if (bar0_fd < 0)
		error(1, errno, "cannot open %s", sysfs_path);
	snprintf(sysfs_path, sizeof(sysfs_path),
		 "/sys/class/uio%s/device/resource2_wc",
		 uio_devname);
	bar2_fd = open(sysfs_path, O_RDWR);
	if (bar2_fd < 0)
		error(1, errno, "cannot open %s", sysfs_path);

	info->mmio_base = mmap(NULL, IVSHMEM_BAR0_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, bar0_fd, 0);
	if (info->mmio_base == MAP_FAILED)
		error(1, errno, "mmap of registers failed");

	if (fstat(bar2_fd, &stat) < 0)
		error(1, errno, "Cannot get file stats of fd %d", bar2_fd);

	info->mem_size = stat.st_size;
	info->mem_base = mmap(NULL, info->mem_size, PROT_READ | PROT_WRITE, MAP_SHARED, bar2_fd, 0);
	if (info->mem_base == MAP_FAILED)
		error(1, errno, "mmap of shared memory failed");

	info->nr_vecs = min(MAX_VECTORS, nr_ent_fds);
	for (i = 0; i < info->nr_vecs; i++) {
		irq_data.fd = evt_fds[i];
		irq_data.vector = i;
		if (ioctl(uio_fd, UIO_IRQ_DATA, &irq_data) < 0)
			error(1, errno, "cannot bind uio IRQ %d", i);
	}

	regs = (struct ivshm_regs *)info->mmio_base;
	info->this_id = mmio_read32(&regs->ivpos);
	info->peer_id = -1;

	close(bar0_fd);
	close(bar2_fd);
	close(uio_fd);

	info->ops = &uio_shmem_ops;

	return 0;
}

static void shmem_close(struct shmem_info *info)
{
	if (info->mmio_base) {
		munmap(info->mmio_base, IVSHMEM_BAR0_SIZE);
		info->mmio_base = NULL;
	}

	if (info->mem_base) {
		munmap(info->mem_base, info->mem_size);
		info->mem_base = NULL;
		info->mem_size = 0;
	}
}

static void shmem_notify_peer(struct shmem_info *info, int vector)
{
	struct ivshm_regs *regs = (struct ivshm_regs *)info->mmio_base;

	mmio_write32(&regs->doorbell, (info->peer_id << 16) | vector);
}

struct shmem_ops uio_shmem_ops = {
	.name = "uio-ivshmem",

	.open = shmem_open,
	.close = shmem_close,
	.notify_peer = shmem_notify_peer,
};
