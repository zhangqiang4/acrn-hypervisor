/*
 * Copyright (C) 2024 Intel Corporation
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#ifndef __VIRTIO_VACKENDS_H__
#define __VIRTIO_VACKENDS_H__

#include <pthread.h>
#include "pci_core.h"
#include "vmmapi.h"

struct virtio_vq_info;
struct virtio_ops;
struct virtio_base;
struct mem_range;
struct pci_vdev;
enum pcibar_type;
struct inout_port;
struct monitor_vm_ops;

struct virtio_be_ops {
    /* mem */
    bool (*find_memfd_region)(struct vmctx *ctx, vm_paddr_t gpa,
                struct vm_mem_region *ret_region);
    bool (*get_mem_region)(struct vmctx *ctx, vm_paddr_t gpa,
                struct vm_mmap_mem_region *ret_region);
    bool (*allow_dmabuf)(struct vmctx *ctx);
    void *(*map_gpa)(struct vmctx *ctx, vm_paddr_t gaddr, size_t len);
    int (*register_mem)(struct mem_range *memp);
    int (*register_mem_fallback)(struct mem_range *memp);

    /* pci */
    int (*alloc_bar)(struct pci_vdev *pdi, int idx,
			   enum pcibar_type type, uint64_t size);
    int (*add_capability)(struct pci_vdev *dev, u_char *capdata, int caplen);
    struct pci_vdev* (*get_vdev_info)(int slot);

    /* virtio */
    void (*notify_fe)(struct virtio_base *vb, struct virtio_vq_info *vq);
    void (*config_changed)(struct virtio_base *vb);
    void (*iothread)(void *arg);
    void (*linkup)(struct virtio_base *base, struct virtio_ops *vops,
		   void *pci_virtio_dev, struct pci_vdev *dev,
		   struct virtio_vq_info *queues,
		   int backend_type);
    int (*intr_init)(struct virtio_base *base, int barnum, int use_msix);
    void (*set_iothread)(struct virtio_base *base, bool is_register);
    void (*reset_dev)(struct virtio_base *base);
    void (*set_io_bar)(struct virtio_base *base, int barnum);
    int (*set_modern_pio_bar)(struct virtio_base *base, int barnum);
    int (*set_modern_bar)(struct virtio_base *base, bool use_notify_pio);
    uint64_t (*pci_read)(struct vmctx *ctx, int vcpu, struct pci_vdev *dev,
		    int baridx, uint64_t offset, int size);
    void (*pci_write)(struct vmctx *ctx, int vcpu, struct pci_vdev *dev,
		    int baridx, uint64_t offset, int size, uint64_t value);
    int (*register_ioeventfd)(struct virtio_base *base, int idx,
            bool is_register, int fd);

    int (*register_inout)(struct inout_port *iop);
    int (*unregister_inout)(struct inout_port *iop);
    int (*ioeventfd)(struct vmctx *ctx, struct acrn_ioeventfd *args);
    int (*irqfd)(struct vmctx *ctx, struct acrn_irqfd *args);
    int (*monitor_register_vm_ops)(struct monitor_vm_ops *mops, void *arg,
            const char *name);
};

extern bool only_be;
extern struct virtio_be_ops *vb_ops;
/* mem */
#define vm_find_memfd_region(ctx, gpa, ret_region)    \
    vb_ops->find_memfd_region(ctx, gpa, ret_region)
#define vm_get_mem_region(ctx, gpa, ret_region) \
    vb_ops->get_mem_region(ctx, gpa, ret_region)
#define vm_allow_dmabuf(ctx)    \
    vb_ops->allow_dmabuf(ctx)
#define vm_map_gpa(ctx, gaddr, len) \
    vb_ops->map_gpa(ctx, gaddr, len)
#define register_mem(memp)  \
    vb_ops->register_mem(memp)
#define register_mem_fallback(memp) \
    vb_ops->register_mem_fallback(memp)
/* pci */
#define pci_emul_alloc_bar(pdi, idx, type, size)    \
    vb_ops->alloc_bar(pdi, idx, type, size)
#define pci_emul_add_capability(dev, capdata, caplen)   \
    vb_ops->add_capability(dev, capdata, caplen)
#define pci_get_vdev_info(slot) \
    vb_ops->get_vdev_info(slot)

/* virtio */
#define virtio_intr_init(base, barnum, use_msix)  \
    vb_ops->intr_init(base, barnum, use_msix)
#define virtio_set_iothread(base, is_register) \
    vb_ops->set_iothread(base, is_register)
#define virtio_set_modern_pio_bar(base, barnum) \
    vb_ops->set_modern_pio_bar(base, barnum)
#define virtio_register_ioeventfd(base, idx, is_register, fd)   \
    vb_ops->register_ioeventfd(base, idx, is_register, fd)

/* io */
#define register_inout(iop)     vb_ops->register_inout(iop)
#define unregister_inout(iop)   vb_ops->unregister_inout(iop)
#define vm_ioeventfd(ctx, args) vb_ops->ioeventfd(ctx, args)
#define vm_irqfd(ctx, args)     vb_ops->irqfd(ctx, args)
#define monitor_register_vm_ops(mops, args, name)   \
    vb_ops->monitor_register_vm_ops(mops, args, name)

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
uint64_t virtio_pci_read(struct vmctx *ctx, int vcpu, struct pci_vdev *dev,
	int baridx, uint64_t offset, int size);

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
void virtio_pci_write(struct vmctx *ctx, int vcpu, struct pci_vdev *dev,
	int baridx, uint64_t offset, int size, uint64_t value);

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
		   int backend_type);

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
void virtio_reset_dev(struct virtio_base *base);

/**
 * @brief Set I/O BAR (usually 0) to map PCI config registers.
 *
 * @param base Pointer to struct virtio_base.
 * @param barnum Which BAR[0..5] to use.
 *
 * @return None
 */
void virtio_set_io_bar(struct virtio_base *base, int barnum);

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
int virtio_set_modern_bar(struct virtio_base *base, bool use_notify_pio);

/**
 * @brief Deliver an config changed interrupt to guest.
 *
 * MSI-X or a generic MSI interrupt with config changed event.
 *
 * @param vb Pointer to struct virtio_base.
 */
void virtio_config_changed(struct virtio_base *vb);

/**
 * @brief Deliver an interrupt to guest on the given virtqueue.
 *
 * The interrupt could be MSI-X or a generic MSI interrupt.
 *
 * @param vb Pointer to struct virtio_base.
 * @param vq Pointer to struct virtio_vq_info.
 */
void vq_interrupt(struct virtio_base *vb, struct virtio_vq_info *vq);

#endif  /* __VIRTIO_VACKENDS_H__ */
