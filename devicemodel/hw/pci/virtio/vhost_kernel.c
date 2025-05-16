/*
 * Copyright (C) 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include <sys/ioctl.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

#include "pci_core.h"
#include "vhost.h"
#include "vmmapi.h"

static int vhost_kernel_debug;
#define LOG_TAG "vhost_kernel: "
#define DPRINTF(fmt, args...) \
	do { if (vhost_kernel_debug) pr_dbg(LOG_TAG fmt, ##args); } while (0)
#define WPRINTF(fmt, args...) pr_err(LOG_TAG fmt, ##args)

inline
int vhost_kernel_ioctl(struct vhost_dev *vdev,
		       unsigned long request,
		       void *arg)
{
	int rc;

	rc = ioctl(vdev->fd, request, arg);
	if (rc < 0)
		WPRINTF("ioctl failed, fd = %d, request = 0x%lx, rc = %d, errno = %d\n",
			vdev->fd, request, rc, errno);
	return rc;
}

static int
vhost_k_init(struct vhost_dev *vdev, struct virtio_base *base,
		  int fd, int vq_idx, uint32_t busyloop_timeout)
{
	vdev->base = base;
	vdev->fd = fd;
	vdev->vq_idx = vq_idx;
	vdev->busyloop_timeout = busyloop_timeout;
	return 0;
}

static int
vhost_k_deinit(struct vhost_dev *vdev)
{
	vdev->base = NULL;
	vdev->vq_idx = 0;
	vdev->busyloop_timeout = 0;
	if (vdev->fd > 0) {
		close(vdev->fd);
		vdev->fd = -1;
	}
	return 0;
}

static int
__vhost_k_set_mem_table(struct vhost_dev *vdev,
			   struct vhost_memory *mem)
{
	return vhost_kernel_ioctl(vdev, VHOST_SET_MEM_TABLE, mem);
}

static int
vhost_k_set_mem_table(struct vhost_dev *vdev)
{
	struct vmctx *ctx;
	struct vhost_memory *vhost_mem;
	struct vm_mmap_mem_region *vm_mem_regs;
	int nregions, i, rc = 0;

	ctx = vdev->base->dev->vmctx;

	vm_mem_regs = calloc(1, sizeof(*vm_mem_regs) * MAX_VM_MEM_REGION);
	vhost_mem = calloc(1, sizeof(struct vhost_memory) +
		sizeof(struct vhost_memory_region) * MAX_VM_MEM_REGION);
	if (!vhost_mem || !vm_mem_regs) {
		WPRINTF("out of memory\n");
		rc = -1;
		goto end;
	}

	nregions = vm_get_mem_regions(ctx, MAX_VM_MEM_REGION, vm_mem_regs);
	if(nregions <= 0) {
		WPRINTF("set_mem_table failed. Unable to retrieve memory region information.\n");
		rc = -1;
		goto end;
	}

	for(i = 0; i < nregions; i++) {
		vhost_mem->regions[i].guest_phys_addr = vm_mem_regs[i].gpa_start;
		vhost_mem->regions[i].memory_size = vm_mem_regs[i].gpa_end - vm_mem_regs[i].gpa_start;
		vhost_mem->regions[i].userspace_addr =  (uint64_t)vm_mem_regs[i].hva_base;
		DPRINTF("[%d][0x%llx -> 0x%llx, 0x%llx]\n",
			i,
			vhost_mem->regions[i].guest_phys_addr,
			vhost_mem->regions[i].userspace_addr,
			vhost_mem->regions[i].memory_size);
	}

	vhost_mem->nregions = nregions;
	vhost_mem->padding = 0;
	rc = __vhost_k_set_mem_table(vdev, vhost_mem);

end:
	free(vhost_mem);
	free(vm_mem_regs);
	return rc;
}

static int
vhost_k_set_vring_addr(struct vhost_dev *vdev,
			    struct vhost_vring_addr *addr)
{
	return vhost_kernel_ioctl(vdev, VHOST_SET_VRING_ADDR, addr);
}

static int
vhost_k_set_vring_num(struct vhost_dev *vdev,
			   struct vhost_vring_state *ring)
{
	return vhost_kernel_ioctl(vdev, VHOST_SET_VRING_NUM, ring);
}

static int
vhost_k_set_vring_base(struct vhost_dev *vdev,
			    struct vhost_vring_state *ring)
{
	return vhost_kernel_ioctl(vdev, VHOST_SET_VRING_BASE, ring);
}

static int
vhost_k_get_vring_base(struct vhost_dev *vdev,
			    struct vhost_vring_state *ring)
{
	return vhost_kernel_ioctl(vdev, VHOST_GET_VRING_BASE, ring);
}

static int
vhost_k_set_vring_kick(struct vhost_dev *vdev,
			    struct vhost_vring_file *file)
{
	return vhost_kernel_ioctl(vdev, VHOST_SET_VRING_KICK, file);
}

static int
vhost_k_set_vring_call(struct vhost_dev *vdev,
			    struct vhost_vring_file *file)
{
	return vhost_kernel_ioctl(vdev, VHOST_SET_VRING_CALL, file);
}

static int
vhost_k_set_vring_busyloop_timeout(struct vhost_dev *vdev,
					struct vhost_vring_state *s)
{
#ifdef VHOST_SET_VRING_BUSYLOOP_TIMEOUT
	return vhost_kernel_ioctl(vdev, VHOST_SET_VRING_BUSYLOOP_TIMEOUT, s);
#else
	return 0;
#endif
}

static int
vhost_k_set_features(struct vhost_dev *vdev,
			  uint64_t features)
{
	return vhost_kernel_ioctl(vdev, VHOST_SET_FEATURES, &features);
}

static int
vhost_k_get_features(struct vhost_dev *vdev,
			  uint64_t *features)
{
	return vhost_kernel_ioctl(vdev, VHOST_GET_FEATURES, features);
}

static int
vhost_k_set_owner(struct vhost_dev *vdev)
{
	return vhost_kernel_ioctl(vdev, VHOST_SET_OWNER, NULL);
}

const struct vhost_dev_ops vhost_kernel_ops = {
	.vhost_init = vhost_k_init,
	.vhost_deinit = vhost_k_deinit,
	.vhost_set_vring_busyloop_timeout = vhost_k_set_vring_busyloop_timeout,
	.vhost_set_mem_table = vhost_k_set_mem_table,
	.vhost_set_vring_addr = vhost_k_set_vring_addr,
	.vhost_set_vring_num = vhost_k_set_vring_num,
	.vhost_set_vring_base = vhost_k_set_vring_base,
	.vhost_get_vring_base = vhost_k_get_vring_base,
	.vhost_set_vring_kick = vhost_k_set_vring_kick,
	.vhost_set_vring_call = vhost_k_set_vring_call,
	.vhost_set_features = vhost_k_set_features,
	.vhost_get_features = vhost_k_get_features,
	.vhost_set_owner = vhost_k_set_owner,
	.vhost_reset_device = NULL
};
