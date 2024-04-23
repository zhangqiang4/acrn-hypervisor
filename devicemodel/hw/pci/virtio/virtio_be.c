/*
 * Copyright (C) 2024 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include "virtio_be.h"
#include "virtio.h"
#include "mem.h"
#include "inout.h"
#include "monitor.h"

static struct virtio_be_ops vb_default_ops;
struct virtio_be_ops *vb_ops = &vb_default_ops;

/**
 * @brief Handle PCI configuration space reads.
 *
 * Handle virtio standard register reads, and dispatch other reads to
 * actual virtio device driver.
 *
 * @param ctx Pointer to struct vmctx representing VM context.
 * @param vcpu VCPU ID.
 * @param dev Pointer to struct pci_vdev which emulates a PCI device.
 * @param baridx Which BAR[0..5] to use.
 * @param offset Register offset in bytes within a BAR region.
 * @param size Access range in bytes.
 *
 * @return register value.
 */
uint64_t
virtio_pci_read(struct vmctx *ctx, int vcpu, struct pci_vdev *dev,
		int baridx, uint64_t offset, int size)
{
	return vb_ops->pci_read(ctx, vcpu, dev, baridx, offset, size);
}

/**
 * @brief Handle PCI configuration space writes.
 *
 * Handle virtio standard register writes, and dispatch other writes to
 * actual virtio device driver.
 *
 * @param ctx Pointer to struct vmctx representing VM context.
 * @param vcpu VCPU ID.
 * @param dev Pointer to struct pci_vdev which emulates a PCI device.
 * @param baridx Which BAR[0..5] to use.
 * @param offset Register offset in bytes within a BAR region.
 * @param size Access range in bytes.
 * @param value Data value to be written into register.
 *
 * @return None
 */
void
virtio_pci_write(struct vmctx *ctx, int vcpu, struct pci_vdev *dev,
		 int baridx, uint64_t offset, int size, uint64_t value)
{
	vb_ops->pci_write(ctx, vcpu, dev, baridx, offset, size, value);
}

/**
 * @brief Link a virtio_base to its constants, the virtio device,
 * and the PCI emulation.
 *
 * @param base Pointer to struct virtio_base.
 * @param vops Pointer to struct virtio_ops.
 * @param pci_virtio_dev Pointer to instance of certain virtio device.
 * @param dev Pointer to struct pci_vdev which emulates a PCI device.
 * @param queues Pointer to struct virtio_vq_info, normally an array.
 * @param backend_type can be VBSU, VBSK or VHOST
 *
 * @return None
 */
void virtio_linkup(struct virtio_base *base, struct virtio_ops *vops,
		   void *pci_virtio_dev, struct pci_vdev *dev,
		   struct virtio_vq_info *queues,
		   int backend_type)
{
	vb_ops->linkup(base, vops, pci_virtio_dev, dev, queues, backend_type);
}

/**
 * @brief Reset device (device-wide).
 *
 * This erases all queues, i.e., all the queues become invalid.
 * But we don't wipe out the internal pointers, by just clearing
 * the VQ_ALLOC flag.
 *
 * It resets negotiated features to "none".
 * If MSI-X is enabled, this also resets all the vectors to NO_VECTOR.
 *
 * @param base Pointer to struct virtio_base.
 *
 * @return None
 */
void virtio_reset_dev(struct virtio_base *base)
{
	vb_ops->reset_dev(base);
}

/**
 * @brief Set I/O BAR (usually 0) to map PCI config registers.
 *
 * @param base Pointer to struct virtio_base.
 * @param barnum Which BAR[0..5] to use.
 *
 * @return None
 */
void virtio_set_io_bar(struct virtio_base *base, int barnum)
{
	vb_ops->set_io_bar(base, barnum);
}

/**
 * @brief Set modern BAR (usually 4) to map PCI config registers.
 *
 * Set modern MMIO BAR (usually 4) to map virtio 1.0 capabilities and optional
 * set modern PIO BAR (usually 2) to map notify capability. This interface is
 * only valid for modern virtio.
 *
 * @param base Pointer to struct virtio_base.
 * @param use_notify_pio Whether use pio for notify capability.
 *
 * @return 0 on success and non-zero on fail.
 */
int virtio_set_modern_bar(struct virtio_base *base, bool use_notify_pio)
{
	return vb_ops->set_modern_bar(base, use_notify_pio);
}

/**
 * @brief Deliver an config changed interrupt to guest.
 *
 * MSI-X or a generic MSI interrupt with config changed event.
 *
 * @param vb Pointer to struct virtio_base.
 */
void virtio_config_changed(struct virtio_base *vb)
{
	vb_ops->config_changed(vb);
}

/**
 * @brief Deliver an interrupt to guest on the given virtqueue.
 *
 * The interrupt could be MSI-X or a generic MSI interrupt.
 *
 * @param vb Pointer to struct virtio_base.
 * @param vq Pointer to struct virtio_vq_info.
 */
void vq_interrupt(struct virtio_base *vb, struct virtio_vq_info *vq)
{
	vb_ops->notify_fe(vb, vq);
}

static struct virtio_be_ops vb_default_ops = {
	.find_memfd_region	= dm_vm_find_memfd_region,
	.get_mem_region		= dm_vm_get_mem_region,
	.allow_dmabuf		= dm_vm_allow_dmabuf,
	.map_gpa		= dm_vm_map_gpa,
	.register_mem		= dm_register_mem,
	.register_mem_fallback	= dm_register_mem_fallback,

	.alloc_bar     		= dm_pci_emul_alloc_bar,
	.add_capability 	= dm_pci_emul_add_capability,
	.get_vdev_info		= dm_pci_get_vdev_info,

	.notify_fe			= dm_vq_interrupt,
	.config_changed		= dm_virtio_config_changed,
	.iothread		= dm_virtio_iothread_handler,
	.linkup			= dm_virtio_linkup,
	.intr_init		= dm_virtio_intr_init,
	.set_iothread		= dm_virtio_set_iothread,
	.reset_dev		= dm_virtio_reset_dev,
	.set_io_bar		= dm_virtio_set_io_bar,
	.set_modern_pio_bar	= dm_virtio_set_modern_pio_bar,
	.set_modern_bar		= dm_virtio_set_modern_bar,
	.pci_read		= dm_virtio_pci_read,
	.pci_write		= dm_virtio_pci_write,
	.register_ioeventfd	= dm_virtio_register_ioeventfd,

	.register_inout		= dm_register_inout,
	.unregister_inout	= dm_unregister_inout,
	.ioeventfd			= dm_vm_ioeventfd,
	.irqfd				= dm_vm_irqfd,
	.monitor_register_vm_ops	= dm_monitor_register_vm_ops,
};
