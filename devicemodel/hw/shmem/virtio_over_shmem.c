/*
 * Copyright (C) 2024 Intel Corporation.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <error.h>
#include <errno.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <linux/virtio_pci.h>
#include <pthread.h>
#include <signal.h>
#include <getopt.h>

#include <pci_core.h>
#include <virtio.h>
#include <mevent.h>
#include <pm.h>
#include <vmmapi.h>

#include "log.h"
#include "vdisplay.h"
#include "virtio_over_shmem.h"
#include "shmem.h"
#include "virtio_be.h"

#define MAX_IRQS    8
#define	MAXBUSES	(PCI_BUSMAX + 1)
#define MAXSLOTS	(PCI_SLOTMAX + 1)
#define	MAXFUNCS	(PCI_FUNCMAX + 1)

extern bool gfx_ui;
static struct virtio_be_ops vos_op;

enum {
	CMD_OPT_BE,
	CMD_OPT_LOGGER_SETTING,
};

static struct shmem_ops *shmem_ops[] = {
	&uio_shmem_ops,
	&ivshm_ivshmem_ops,
	&ivshm_guest_shm_ops,
	NULL
};

static int vos_backend_init(struct virtio_backend_info *info);
static void vos_backend_deinit(struct virtio_backend_info *info);

static void be_usage(int code)
{
	fprintf(stderr,
		"Usage: acrn-dm --acrn_be -s <driver,device,emulate,configinfo> \n\n"
		"Options:\n"
		"-s | --subdevice <driver,device,emulate,configinfo> \n"
		"-h | --help          Print this message \n"
		"\n"
		"Available drivers: uio-ivshmem/ivshm-ivshmem/ivshm-guest-shm"
	);
	exit(code);
}

static char be_optstr[] = "s:h";

static const struct option
be_options[] = {
	{ "be",				no_argument,		0,		CMD_OPT_BE },
	{ "subdevice",		required_argument,	NULL,	's' },
	{ "help",			no_argument,		NULL,	'h' },
	{ "logger_setting",	required_argument,	0,		CMD_OPT_LOGGER_SETTING},
	{ 0, 0, 0, 0 }
};

static  int
shm_parse_sub_device(char *opt, struct virtio_backend_info *info)
{
	char *emul, *config, *str, *cp, *driver, *device;
	int error;
	bool found = false;

	error = -1;
	str = strdup(opt);
	if (!str) {
		pr_warn("%s: strdup returns NULL\n", __func__);
		return -1;
	}
	cp = str;

	driver = strsep(&cp, ",");
	if (cp == NULL) {
		pr_warn("%s: opt err %s\n", __func__, opt);
		goto done;
	}

	for (struct shmem_ops **ops = shmem_ops; *ops != NULL; ops ++) {
		if (strcmp(driver, (*ops)->name) == 0) {
			info->shmem_ops = *ops;
			found = true;
			break;
		}
	}
	if (!found) {
		pr_warn("Unknown driver: %s\n\n", driver);
		goto done;
	}

	device = strsep(&cp, ",");
	if (cp == NULL) {
		pr_warn("%s: opt err %s\n", __func__, opt);
		goto done;
	}
	pr_info("device %s, len %ld\n", device, strlen(device));
	info->shmem_devpath = calloc(1, strlen(device) + 1);
	if (info->shmem_devpath == NULL) {
		pr_warn("%s: alloc shmem_devpath fail %s\n", __func__, opt);
		goto done;
	}
	strcpy(info->shmem_devpath, device);

	if (cp) {
		emul = config = NULL;
		emul = strsep(&cp, ",");
		config = cp;
	} else {
		pr_warn("%s: opt err %s\n", __func__, opt);
		goto done;
	}

	if ((strcmp("pci-gvt", emul) == 0) || (strcmp("virtio-hdcp", emul) == 0)
			|| (strcmp("npk", emul) == 0) || (strcmp("virtio-coreu", emul) == 0)) {
		pr_warn("The \"%s\" parameter is obsolete and ignored\n", emul);
		goto done;
	}

	//todo
	if (strcmp("php-slot", emul) == 0) {
		error = 0;
		goto done;
	}

	if ((info->pci_vdev_ops = pci_emul_finddev(emul)) == NULL) {
		pr_warn("unknown device \"%s\"\n",
			emul);
		goto done;
	}
	pr_info("config: %s \n", config);
	error = 0;
	info->fi_funcs.fi_name = emul;
	/* saved fi param in case reboot */
	info->fi_funcs.fi_param_saved = config;

	if (strcmp("virtio-net", emul) == 0) {
		info->fi_funcs.fi_param_saved = cp;
	}
	if ((strcmp("virtio-gpu", emul) == 0)) {
		pr_info("%s: virtio-gpu device found, activating virtual display.\n",
				__func__);
		gfx_ui = true;
		vdpy_parse_cmd_option(config);
	}
done:
	if (error) {
		free(str);
		if (info->shmem_devpath)
			free(info->shmem_devpath);
	}

	return error;
}

int
acrn_be(int argc, char *argv[])
{
	int c, error, i;
	struct dm_backend *dm_be;
	int option_idx = 0;

	vmname = argv[0];
	dm_be = calloc(1, sizeof(struct dm_backend));
	if (dm_be == NULL) {
		pr_err("Failed to get dm_be\n");
		exit(1);
	}
	dm_be->be_cnt = 0;
	vb_ops = &vos_op;
	while ((c = getopt_long(argc, argv, be_optstr, be_options,
			&option_idx)) != -1) {
		switch (c) {
		case 's':
			if (dm_be->be_cnt >= MAX_BACKEND) {
				pr_warn("Too many backends(max %d)\n", MAX_BACKEND);
				exit(1);
			}
			dm_be->info[dm_be->be_cnt] = calloc(1, sizeof(struct virtio_backend_info));
			if (dm_be->info[dm_be->be_cnt] == NULL) {
				pr_warn("Failed to get dm BE info\n");
				exit(1);
			}

			if (shm_parse_sub_device(optarg, dm_be->info[dm_be->be_cnt]) != 0)
				exit(1);
			else {
				dm_be->be_cnt++;
				break;
			}
		case CMD_OPT_LOGGER_SETTING:
			if (init_logger_setting(optarg) != 0)
				pr_err("invalid logger setting params %s", optarg);
			break;
		case 'h':
			be_usage(0);
		default:
			be_usage(1);
		}
	}

	if (gfx_ui) {
		if(gfx_ui_init()) {
			pr_err("gfx ui initialize failed\n");
			exit(1);
		}
	}

	error = mevent_init();
	if (error) {
		pr_warn("Unable to initialize mevent (%d)\n", errno);
		exit(1);
	}

	for (i = 0; i < dm_be->be_cnt; i++) {
		if (dm_be->info[i]->hook_before_init)
			dm_be->info[i]->hook_before_init(dm_be->info[i]);

		error = vos_backend_init(dm_be->info[i]);
		if (error < 0) {
			pr_warn("Fail to open shmem ops (%d)\n", errno);
			exit(1);
		}
	}

	mevent_dispatch();
	for (i = 0; i < dm_be->be_cnt; i++) {
		vos_backend_deinit(dm_be->info[i]);
	}
	return 0;
}

int
vos_intr_init(struct virtio_base *base, int barnum, int use_msix)
{
	base->flags |= VIRTIO_USE_MSIX;
	VIRTIO_BASE_LOCK(base);
	virtio_reset_dev(base); /* set all vectors to NO_VECTOR */
	VIRTIO_BASE_UNLOCK(base);
	return 0;
}

static void vos_iothread_handler(void *arg)
{
	struct virtio_iothread *viothrd = arg;
	struct virtio_base *base = viothrd->base;
	int idx = viothrd->idx;
	struct virtio_vq_info *vq = &base->queues[idx];

	if (viothrd->iothread_run) {
		if (base->mtx)
			pthread_mutex_lock(base->mtx);
		(*viothrd->iothread_run)(base, vq);
		if (base->mtx)
			pthread_mutex_unlock(base->mtx);
	}
}

static void
vos_linkup(struct virtio_base *base, struct virtio_ops *vops,
	      void *pci_virtio_dev, struct pci_vdev *dev,
	      struct virtio_vq_info *queues,
	      int backend_type)
{
	int i;

	/* base and pci_virtio_dev addresses must match */
	if ((void *)base != pci_virtio_dev) {
		pr_err("virtio_base and pci_virtio_dev addresses don't match!\n");
		return;
	}
	base->vops = vops;
	base->dev = dev;
	dev->arg = base;
	base->backend_type = backend_type;

	base->queues = queues;
	for (i = 0; i < vops->nvq; i++) {
		queues[i].base = base;
		queues[i].num = i;
	}
}

static void
vos_set_iothread(struct virtio_base *base,
			  bool is_register)
{
	pr_err("function %s is not expected to be used\n", __func__);
	return;
}

static void
vos_reset_dev(struct virtio_base *base)
{
	struct virtio_vq_info *vq;
	int i, nvq;

	base->polling_in_progress = 0;

	nvq = base->vops->nvq;
	for (vq = base->queues, i = 0; i < nvq; vq++, i++) {
		vq->flags = 0;
		vq->last_avail = 0;
		vq->save_used = 0;
		vq->pfn = 0;
		vq->msix_idx = VIRTIO_MSI_NO_VECTOR;
		vq->gpa_desc[0] = 0;
		vq->gpa_desc[1] = 0;
		vq->gpa_avail[0] = 0;
		vq->gpa_avail[1] = 0;
		vq->gpa_used[0] = 0;
		vq->gpa_used[1] = 0;
		vq->enabled = 0;
	}
	base->negotiated_caps = 0;
	base->curq = 0;
	/* base->status = 0; -- redundant */
	if (base->isr)
		pci_lintr_deassert(base->dev);
	base->isr = 0;
	base->msix_cfg_idx = VIRTIO_MSI_NO_VECTOR;
	base->device_feature_select = 0;
	base->driver_feature_select = 0;
	base->config_generation = 0;
}

static void
vos_set_io_bar(struct virtio_base *base, int barnum)
{
	return;
}

static int
vos_set_modern_pio_bar(struct virtio_base *base, int barnum)
{
	return 0;
}

static int
vos_set_modern_bar(struct virtio_base *base, bool use_notify_pio)
{
	struct virtio_ops *vops;
	int rc = 0;

	vops = base->vops;

	if (!vops || (base->device_caps & (1UL << VIRTIO_F_VERSION_1)) == 0)
		return -1;

	return rc;
}

static uint64_t
vos_pci_read(struct vmctx *ctx, int vcpu, struct pci_vdev *dev,
		int baridx, uint64_t offset, int size)
{
	return size == 1 ? 0xff : size == 2 ? 0xffff : 0xffffffff;
}

static void
vos_pci_write(struct vmctx *ctx, int vcpu, struct pci_vdev *dev,
		 int baridx, uint64_t offset, int size, uint64_t value)
{
}

static int vos_register_ioeventfd(struct virtio_base *base, int idx, bool is_register, int fd)
{
	pr_err("function %s is not expected to be used for only BE\n", __func__);
	return -1;
}

int vos_register_inout(struct inout_port *iop)
{
	return 0;
}

static int 	vos_unregister_inout(struct inout_port *iop)
{
	return 0;
}

static int vos_ioeventfd(struct vmctx *ctx, struct acrn_ioeventfd *args)
{
	pr_err("function %s is not expected to be used for only BE\n", __func__);
	return -ENOTSUP;
}

static int vos_irqfd(struct vmctx *ctx, struct acrn_irqfd *args)
{
	pr_err("function %s is not expected to be used for only BE\n", __func__);
	return -ENOTSUP;
}

static int vos_monitor_register_vm_ops(struct monitor_vm_ops *mops, void *arg,
			    const char *name)
{
	return 0;
}

static void *
vos_paddr_guest2host(struct vmctx *ctx, uintptr_t gaddr, size_t len)
{
	struct shmem_info *info = (struct shmem_info *)ctx;

	return (gaddr < info->mem_size) ? (info->mem_base + gaddr) : NULL;
}

static int
vos_register_mem(struct mem_range *memp)
{
	return 0;
}

static int
vos_register_mem_fallback(struct mem_range *memp)
{
	return 0;
}

bool
vos_find_memfd_region(struct vmctx *ctx, vm_paddr_t gpa, struct vm_mem_region *ret_region)
{
	struct shmem_info *info = (struct shmem_info *)ctx;

	if (!ret_region)
		return false;

	if (info->mem_fd == 0)
		return false;

	if (gpa >= info->mem_size)
		return false;

	ret_region->fd = info->mem_fd;
	ret_region->fd_offset = gpa;

	return true;
}

bool vos_get_mem_region(struct vmctx *ctx, vm_paddr_t gpa,
                       struct vm_mmap_mem_region *ret_region)
{
       struct shmem_info *info = (struct shmem_info *)ctx;

       if (!ret_region)
               return false;

       if (info->mem_fd == 0)
               return false;

       if (gpa >= info->mem_size)
               return false;

       ret_region->fd = info->mem_fd;
       ret_region->fd_offset = gpa;
       ret_region->hva_base = info->mem_base;
       ret_region->gpa_start = 0;
       ret_region->gpa_end = info->mem_size;
       return true;
}

static void vos_notify_fe(struct virtio_base *vb, struct virtio_vq_info *vq)
{
	struct shmem_info *info = (struct shmem_info *)vb->dev->vmctx;
	struct virtio_backend_info *be_info = (struct virtio_backend_info *)info->be_info;

	be_info->virtio_header->queue_event = 1;
	__sync_synchronize();
	info->ops->notify_peer(info, vq->msix_idx);
}

static void vos_config_changed(struct virtio_base *vb)
{
	struct shmem_info *info = (struct shmem_info *)vb->dev->vmctx;
	struct virtio_backend_info *be_info = (struct virtio_backend_info *)info->be_info;

	if (!(vb->status & VIRTIO_CONFIG_S_DRIVER_OK))
		return;

	vb->config_generation++;
	be_info->virtio_header->queue_event = 1;
	__sync_synchronize();
	info->ops->notify_peer(info, vb->msix_cfg_idx);
}

static int
vos_emul_alloc_bar(struct pci_vdev *pdi, int idx, enum pcibar_type type,
		   uint64_t size)
{
	return 0;
}

static int
vos_add_capability(struct pci_vdev *dev, u_char *capdata, int caplen)
{
	return 0;
}

static struct pci_vdev*
vos_get_vdev_info(int slot)
{
	pr_err("function %s is not expected to be used for only BE\n", __func__);
	return NULL;
}

bool vos_allow_dmabuf(struct vmctx *ctx)
{
	struct shmem_info *info = (struct shmem_info *)ctx;

	return info->mem_fd > 0;
}

static void process_queue(struct pci_vdev *dev)
{
	struct virtio_base *base = dev->arg;
	struct virtio_ops *vops = base->vops;
	struct virtio_vq_info *vq;
	int i;

	/*
	 * Virtio-snd uses virtqueue 0 for control messages and 2/3 for tx/rx data. During playback starting there is an
	 * implicit requirement on the order of message handling: the (typically async) data messages in virtqueue 2
	 * (txq) must be processed before the PCM_START message in virtqueue 0 (controlq). Unfortunately that could be
	 * violated when multiple virtqueues share the same interrupt, and the interrupt handler walks virtqueue 0
	 * first.
	 *
	 * For now we work around that issue by walking through the queues in decremental order. Hopefully no other
	 * device has similar constraints on inter-virtqueue processing order.
	 */
        for (i = base->vops->nvq - 1; i >= 0; i--) {
		vq = &base->queues[i];
		if(!vq_ring_ready(vq))
			continue;

		if (vq->notify)
			(*vq->notify)((void *)base, vq);
		else if (vops->qnotify)
			(*vops->qnotify)((void *)base, vq);
		else
			pr_warn("%s: qnotify queue %d: missing vq/vops notify\r\n", vops->name, i);
	}
}

static void process_write_transaction(struct virtio_backend_info *info)
{
	void *new_value_p;
	uint64_t new_value;
	uint32_t offset;

	if (info->virtio_header->write_transaction == 0)
		return;

	new_value_p = (void *)info->virtio_header + info->virtio_header->write_offset;
	new_value =
		(info->virtio_header->write_size == 1) ? (*(uint8_t  *)new_value_p) :
		(info->virtio_header->write_size == 2) ? (*(uint16_t *)new_value_p) :
		(info->virtio_header->write_size == 4) ? (*(uint32_t *)new_value_p) :
		0xffffffff;

	if (info->virtio_header->write_offset >= offsetof(struct virtio_shmem_header, common_config) &&
	    info->virtio_header->write_offset < offsetof(struct virtio_shmem_header, config)) {
		offset = info->virtio_header->write_offset - offsetof(struct virtio_shmem_header, common_config);
		virtio_common_cfg_write(&info->pci_vdev, offset, info->virtio_header->write_size, new_value);

		/* Handle side effects */
		switch (offset) {
		case VIRTIO_PCI_COMMON_DFSELECT:
			/* Force VIRTIO_F_VERSION_1 and VIRTIO_F_ACCESS_PLATFORM to be 1. */
			info->virtio_header->common_config.device_feature =
				virtio_common_cfg_read(&info->pci_vdev, VIRTIO_PCI_COMMON_DF, 4) |
				((info->virtio_header->common_config.device_feature_select == 1) ?
				 ((1 << (VIRTIO_F_ACCESS_PLATFORM - 32)) | (1 << (VIRTIO_F_VERSION_1 - 32))) :
				 0);
			break;
		case VIRTIO_PCI_COMMON_GFSELECT:
			info->virtio_header->common_config.guest_feature = virtio_common_cfg_read(&info->pci_vdev, VIRTIO_PCI_COMMON_GF, 4);
			break;
		case VIRTIO_PCI_COMMON_Q_SELECT:
			info->virtio_header->common_config.queue_size = virtio_common_cfg_read(&info->pci_vdev, VIRTIO_PCI_COMMON_Q_SIZE, 2);
			info->virtio_header->common_config.queue_msix_vector = virtio_common_cfg_read(&info->pci_vdev, VIRTIO_PCI_COMMON_Q_MSIX, 2);
			info->virtio_header->common_config.queue_enable = virtio_common_cfg_read(&info->pci_vdev, VIRTIO_PCI_COMMON_Q_ENABLE, 2);
			info->virtio_header->common_config.queue_notify_off = virtio_common_cfg_read(&info->pci_vdev, VIRTIO_PCI_COMMON_Q_NOFF, 2);
			info->virtio_header->common_config.queue_desc_lo = virtio_common_cfg_read(&info->pci_vdev, VIRTIO_PCI_COMMON_Q_DESCLO, 4);
			info->virtio_header->common_config.queue_desc_hi = virtio_common_cfg_read(&info->pci_vdev, VIRTIO_PCI_COMMON_Q_DESCHI, 4);
			info->virtio_header->common_config.queue_avail_lo = virtio_common_cfg_read(&info->pci_vdev, VIRTIO_PCI_COMMON_Q_AVAILLO, 4);
			info->virtio_header->common_config.queue_avail_hi = virtio_common_cfg_read(&info->pci_vdev, VIRTIO_PCI_COMMON_Q_AVAILHI, 4);
			info->virtio_header->common_config.queue_used_lo = virtio_common_cfg_read(&info->pci_vdev, VIRTIO_PCI_COMMON_Q_USEDLO, 4);
			info->virtio_header->common_config.queue_used_hi = virtio_common_cfg_read(&info->pci_vdev, VIRTIO_PCI_COMMON_Q_USEDHI, 4);
			break;
		}
	} else if (info->virtio_header->write_offset >= offsetof(struct virtio_shmem_header, config)) {
		struct virtio_base *base = info->pci_vdev.arg;
		offset = info->virtio_header->write_offset - offsetof(struct virtio_shmem_header, config);
		base->vops->cfgwrite(&info->pci_vdev, offset, info->virtio_header->write_size, new_value);
	}

	__sync_synchronize();
	info->virtio_header->write_transaction = 0;
}

static void handle_requests(int fd, enum ev_type t, void *arg)
{
	eventfd_t val;
	struct virtio_backend_info *info = (struct virtio_backend_info *)arg;
	eventfd_read(fd, &val);

	if ((info->shmem_info.peer_id == -1) && (info->virtio_header->frontend_flags != 0)) {
		info->shmem_info.peer_id = info->virtio_header->frontend_id;
		pr_info("Frontend peer id: %d\n", info->shmem_info.peer_id);
	}

        process_write_transaction(info);
	if (info->virtio_header->common_config.device_status == 0xf)
		process_queue(&info->pci_vdev);
}

static struct virtio_be_ops vos_op = {
	.find_memfd_region	= vos_find_memfd_region,
	.get_mem_region		= vos_get_mem_region,
	.allow_dmabuf		= vos_allow_dmabuf,
	.map_gpa 		= vos_paddr_guest2host,
	.register_mem		= vos_register_mem,
	.register_mem_fallback  = vos_register_mem_fallback,

	.alloc_bar		= vos_emul_alloc_bar,
	.add_capability		= vos_add_capability,
	.get_vdev_info		= vos_get_vdev_info,

	.notify_fe		= vos_notify_fe,
	.config_changed		= vos_config_changed,
	.iothread			= vos_iothread_handler,
	.linkup				= vos_linkup,
	.intr_init			= vos_intr_init,
	.set_iothread			= vos_set_iothread,
	.reset_dev			= vos_reset_dev,
	.set_io_bar			= vos_set_io_bar,
	.set_modern_pio_bar		= vos_set_modern_pio_bar,
	.set_modern_bar			= vos_set_modern_bar,
	.pci_read			= vos_pci_read,
	.pci_write			= vos_pci_write,
	.register_ioeventfd		= vos_register_ioeventfd,

	.register_inout		= vos_register_inout,
	.unregister_inout	= vos_unregister_inout,
	.ioeventfd			= vos_ioeventfd,
	.irqfd				= vos_irqfd,
	.monitor_register_vm_ops	= vos_monitor_register_vm_ops,
};

static int vos_backend_init(struct virtio_backend_info *info)
{
	int ret = -1, i;
	struct virtio_base *base;

	ret = mevent_init();
	if (ret < 0)
		return ret;

	for (i = 0; i < MAX_IRQS; i++) {
		info->evt_fds[i] = eventfd(0, EFD_NONBLOCK);
		if (info->evt_fds[i] < 0) {
			ret = errno;
			goto close_evt_fds;
		}
	}

        ret = info->shmem_ops->open(info->shmem_devpath, &info->shmem_info, info->evt_fds, MAX_IRQS);
	if (ret < 0) {
		ret = errno;
		goto close_evt_fds;
	}
	info->shmem_info.be_info = info;

	pr_info("Shared memory size: 0x%lx\n", info->shmem_info.mem_size);
	pr_info("Number of interrupt vectors: %d\n", info->shmem_info.nr_vecs);
	pr_info("This ID: %d\n", info->shmem_info.this_id);

	for (i = 0; i < MAX_IRQS; i++) {
		if (i < info->shmem_info.nr_vecs) {
			info->mevents[i] = mevent_add(info->evt_fds[i], EVF_READ, handle_requests, info, NULL, NULL);
			if (info->mevents[i] == NULL)
				goto deregister_mevents;
		} else {
			close(info->evt_fds[i]);
			info->evt_fds[i] = 0;
		}
	}

	info->virtio_header = (struct virtio_shmem_header *)info->shmem_info.mem_base;
	memset(info->virtio_header, 0, sizeof(struct virtio_shmem_header));
	info->virtio_header->backend_status = (info->shmem_info.this_id << 16) | BACKEND_FLAG_PRESENT;
	info->virtio_header->revision = 1;

	//info->pci_vdev.vmctx = &info->shmem_info.ctx;
	info->pci_vdev.vmctx = (struct vmctx *)&info->shmem_info;
	info->pci_vdev.dev_ops = info->pci_vdev_ops;
	if (info->pci_vdev.dev_ops->vdev_init(info->pci_vdev.vmctx, &info->pci_vdev, info->fi_funcs.fi_param_saved)) {
		ret = -1;
		goto deregister_mevents;
	}

	info->virtio_header->device_id = pci_get_cfgdata16(&info->pci_vdev, PCIR_SUBDEV_0);
	info->virtio_header->vendor_id = pci_get_cfgdata16(&info->pci_vdev, PCIR_SUBVEND_0);

	base = info->pci_vdev.arg;
	info->virtio_header->size = sizeof(struct virtio_shmem_header) + base->vops->cfgsize;
	base->vops->cfgread(base, 0, base->vops->cfgsize, (void *)info->virtio_header->config);

	info->pci_vdev.msix.enabled = 1;

	return 0;

deregister_mevents:
	for (i = 0; i < MAX_IRQS; i++) {
		if (info->mevents[i])
			mevent_delete(info->mevents[i]);
	}
	info->shmem_ops->close(&info->shmem_info);

close_evt_fds:
	for (i = 0; i < MAX_IRQS; i++) {
		if (info->evt_fds[i])
			close(info->evt_fds[i]);
	}

	return ret;
}

static void vos_backend_deinit(struct virtio_backend_info *info)
{
	int i;

	for (i = 0; i < 2; i++) {
		mevent_delete(info->mevents[i]);
		close(info->evt_fds[i]);
	}
	info->shmem_info.ops->close(&info->shmem_info);
	mevent_deinit();
}

