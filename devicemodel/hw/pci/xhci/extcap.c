#include <fcntl.h>
#include <stdlib.h>
#include "xhci.h"

#define pr_prefix "usb: xhci: extcap: "
#include "log.h"

static int xhci_extcap_init(struct pci_xhci *xhci,
		struct xhci_extcap_desc *cap_desc, const void *cfg)
{
	struct xhci_extcap *cap = calloc(1, sizeof(struct xhci_extcap) + cap_desc->priv_size);
	if (!cap) {
		return -1;
	}
	cap->desc = cap_desc;
	if (cap->desc->init(cap, cfg)) {
		pr_err("failed to initialize an extcap\n");
		return -1;
	}

	if (!cap->caplen) {
		pr_err("invalid cap, length can't be 0!\n");
		return -1;
	}

	STAILQ_INSERT_TAIL(&xhci->extcaps, cap, list);
	return 0;
}

uint32_t xhci_add_extcaps(struct pci_xhci *xhci, uint32_t offset)
{
	struct xhci_extcap *cap, *last = NULL;
	STAILQ_FOREACH (cap, &xhci->extcaps, list) {
	//while ((cap = STAILQ_FIRST(&xhci->extcaps))) {
		cap->offset = offset;
		/* 0x10 bytes for padding */
		offset = ALIGN_UP(cap->offset + cap->caplen + 0x10, 0x10);
		cap->next = (offset - cap->offset) / 4;
		pr_dbg("an extcap %s was placed at offset 0x%x, next is 0x%x\n",
				cap->desc->name, cap->offset, cap->next);
		last = cap;
	}
	if (last)
		last->next = 0;
	return offset;
}

void xhci_destroy_extcaps(struct pci_xhci *xhci)
{
	struct xhci_extcap *cap;

	while ((cap = STAILQ_FIRST(&xhci->extcaps))) {
		if (cap->desc->deinit) {
			cap->desc->deinit(cap);
		}
		STAILQ_REMOVE_HEAD(&xhci->extcaps, list);
		free(cap);
	}
}

int xhci_extcap_read(struct pci_xhci *xhci, uint32_t offset, int width, uint64_t *value)
{
	struct xhci_extcap *cap;
	STAILQ_FOREACH (cap, &xhci->extcaps, list) {
		if (offset >= cap->offset && offset + width <= cap->offset + cap->caplen) {
			if (cap->desc->read)
				*value = cap->desc->read(cap, offset - cap->offset, width);
			else
				*value = 0;	/* all 0s or 1s ? */
			pr_dbg("read extcap %s@0x%x: %d@0x%x -> 0x%x\n", cap->desc->name,
					cap->offset, width, offset, *value);
			return 0;
		}
	}
	return -1;
}

int xhci_extcap_write(struct pci_xhci *xhci, uint32_t offset, int width, uint64_t value)
{
	struct xhci_extcap *cap;
	STAILQ_FOREACH (cap, &xhci->extcaps, list) {
		if (offset >= cap->offset && offset + width <= cap->offset + cap->caplen) {
			pr_dbg("write extcap %s@0x%x: 0x%x -> %d@0x%x\n", cap->desc->name,
					cap->offset, value, width, offset);
			if (cap->desc->write)
				cap->desc->write(cap, offset - cap->offset, width, value);
			return 0;
		}
	}
	return -1;
}

/* Intel APL xHCI DRD Configuration registers */
#define XHCI_DRD_MUX_CFG0               0x0068
#define XHCI_DRD_MUX_CFG1               0x006C
#define XHCI_DRD_CFG0_IDPIN             (1 << 20)
#define XHCI_DRD_CFG0_IDPIN_EN          (1 << 21)
#define XHCI_DRD_CFG0_VBUS_VALID        (1 << 24)
#define XHCI_DRD_CFG1_HOST_MODE         (1 << 29)

#define SW_DEV_NAME			"intel_xhci_usb_sw"

struct xhci_extcap_drd {
	uint16_t bus, port;	/* DAP port location. 0-0 if host role switch is legacy APL DRD */
	char *port_path;	/* port selection sysfs node */
	char *sw_path;	/* role switch sysfs node */
	uint32_t drdcfg0, drdcfg1;
};

struct xhci_extcap_drd_cfg {
	uint16_t bus, port;
};

static int xhci_drd_select_port(const char *path, int port)
{
	int fd = open(path, O_WRONLY);
	if (fd < 0) {
		pr_err("failed to open %s\n", path);
		return -1;
	}

	char port_str[8];
	snprintf(port_str, 7, "%d", port - 1);
	int rc = write(fd, port_str, strlen(port_str));
	close(fd);
	if (rc != strlen(port_str)) {
		pr_err("failed to write port selector\n");
		return -1;
	}

	return 0;
}

static int xhci_drd_select_role(const char *path, bool host)
{
	int fd = open(path, O_WRONLY);
	if (fd < 0) {
		pr_err("failed to open %s\n", path);
		return -1;
	}

	const char *role = host ? "host" : "device";
	int rc = write(fd, role, strlen(role));
	close(fd);
	if (rc != strlen(role)) {
		pr_err("failed to write role selector\n");
		return -1;
	}

	return 0;
}

static int xhci_extcap_drd_init(struct xhci_extcap *cap, const void *cfg)
{
	struct xhci_extcap_drd *drd = (struct xhci_extcap_drd *)(cap + 1);
	const struct xhci_extcap_drd_cfg *drd_cfg = cfg;

	cap->caplen = 0x70;
	drd->bus = drd_cfg->bus;
	drd->port = drd_cfg->port;

	if (drd->bus && drd->port) {
		char symlink[128], real_path[PATH_MAX];
		char *t, *sw_dev_name = NULL;
		ssize_t len;

		snprintf(symlink, sizeof(symlink), "/sys/bus/usb/devices/usb%d", drd->bus);
		// e.g. /sys/devices/pci0000:00/0000:00:14.0/usb3
		if (!realpath(symlink, real_path)) {
			pr_err("failed to get real path of %s\n", symlink);
			return -1;
		}

		t = strstr(real_path, "usb");
		if (!t) {
			pr_err("invalid sysfs symlink!\n");
			return -1;
		}
		*t = '\0';
		len = strlen(real_path);

		DIR *dir = opendir(real_path);
		if (!dir) {
			pr_err("failed to opendir %s\n", real_path);
			return -1;
		}
		struct dirent *entry;
		while ((entry = readdir(dir))) {
			if (strstr(entry->d_name, SW_DEV_NAME)) {
				sw_dev_name = strdup(entry->d_name);
				break;
			}
		}
		if (!sw_dev_name) {
			pr_err("%s not found under %s\n", SW_DEV_NAME, real_path);
			return -1;
		}

		snprintf(t, sizeof(real_path) - len - 1, "%s/port", sw_dev_name);
		drd->port_path = strdup(real_path);
		snprintf(t, sizeof(real_path) - len - 1, "%s/usb_role/%s-role-switch/role",
				sw_dev_name, sw_dev_name);
		drd->sw_path = strdup(real_path);

		free(sw_dev_name);
	} else {
		const char *sw_path = "/sys/class/usb_role/intel_xhci_usb_sw-role-switch/role";
		drd->port_path = NULL;
		drd->sw_path = strdup(sw_path);
	}
	pr_dbg("port path: %s\n", drd->port_path);
	pr_dbg("role switch path: %s\n", drd->sw_path);

        if (drd->port_path && xhci_drd_select_port(drd->port_path, drd->port) < 0)
		goto exit_free;
        if (xhci_drd_select_role(drd->sw_path, true) < 0)
		goto exit_free;
	drd->drdcfg0 = 0;
	drd->drdcfg1 = 0;

	return 0;

exit_free:
	free(drd->port_path);
	free(drd->sw_path);
	return -1;
}

static void xhci_extcap_drd_deinit(struct xhci_extcap *cap)
{
	struct xhci_extcap_drd *drd = (struct xhci_extcap_drd *)(cap + 1);

	free(drd->port_path);
	free(drd->sw_path);
}

static uint64_t xhci_extcap_drd_read(struct xhci_extcap *cap, uint16_t offset, size_t width)
{
	uint64_t value = 0;
	struct xhci_extcap_drd *drd = (struct xhci_extcap_drd *)(cap + 1);

	/* assume only dword accesses */
	if (width != 4) {
		pr_err("Guest accessed extcap in non 4-byte width!\n");
		return 0;
	}

	switch (offset) {
		case 0x0:
			value= cap->desc->id | (cap->next << 8);
			break;
		case XHCI_DRD_MUX_CFG0:
			value = drd->drdcfg0;
			break;
		case XHCI_DRD_MUX_CFG1:
			value = drd->drdcfg1;
			break;
		default:
			value = 0;
	}
	return value;
}

static void xhci_extcap_drd_write(struct xhci_extcap *cap, uint16_t offset,
				size_t width, uint64_t value)
{
	struct xhci_extcap_drd *drd = (struct xhci_extcap_drd *)(cap + 1);

	/* assume only dword accesses */
	if (width != 4) {
		pr_err("Guest accessed extcap in non 4-byte width!\n");
		return;
	}

	switch (offset) {
		case XHCI_DRD_MUX_CFG0:
		{
			uint32_t drdcfg1 = 0;
			bool host_mode;
			if (drd->drdcfg0 == value)
				break;
			if (!(value & XHCI_DRD_CFG0_IDPIN_EN))
				break;

			if ((value & XHCI_DRD_CFG0_IDPIN) == 0) {
				drdcfg1 |= XHCI_DRD_CFG1_HOST_MODE;
				host_mode = true;
			} else {
				drdcfg1 &= ~XHCI_DRD_CFG1_HOST_MODE;
				host_mode = false;
			}

			if (drd->port_path)
				xhci_drd_select_port(drd->port_path, drd->port);
			xhci_drd_select_role(drd->sw_path, host_mode);

			drd->drdcfg0 = value;
			drd->drdcfg1 = drdcfg1;
			break;
		}
		default:
			pr_warn("invalid write %lx to drd extcap %x\n", value, offset);
			break;
	}
}


static struct xhci_extcap_desc extcap_drd_desc = {
	.id = XHCI_EXTCAP_DRD_INTEL,
	.priv_size = sizeof(struct xhci_extcap_drd),
	.name = "drd",
	.init = xhci_extcap_drd_init,
	.deinit = xhci_extcap_drd_deinit,
	.read = xhci_extcap_drd_read,
	.write = xhci_extcap_drd_write,
};


struct xhci_extcap_proto {
	uint8_t port_offset;
	uint8_t port_count;
	uint32_t name;	/* always "USB " or 0x20425355 */
	uint8_t major;
	uint8_t minor;
};

struct xhci_extcap_proto_cfg {
	struct xhci_hw_configs *xhci_configs;
	bool usb3;
};

static int xhci_extcap_proto_init(struct xhci_extcap *cap, const void *cfg)
{
	struct xhci_extcap_proto *protocap = (struct xhci_extcap_proto *)(cap + 1);
	const struct xhci_extcap_proto_cfg *proto_cfg = cfg;
	cap->caplen = 0x10;	/* PSI (Port Speed ID) == 0 */
	if (proto_cfg->usb3) {
		protocap->major = 0x3;
		protocap->minor = 0x0;
		protocap->name = 0x20425355; /* "USB " */
		protocap->port_offset = proto_cfg->xhci_configs->usb2_ports + 1;
		protocap->port_count = proto_cfg->xhci_configs->usb3_ports;
	} else {
		protocap->major = 0x2;
		protocap->minor = 0x0;
		protocap->name = 0x20425355; /* "USB " */
		protocap->port_offset = 1;
		protocap->port_count = proto_cfg->xhci_configs->usb2_ports;
	}
	return 0;
}

static uint64_t xhci_extcap_proto_read(struct xhci_extcap *cap, uint16_t offset, size_t width)
{
	uint64_t value = 0;
	struct xhci_extcap_proto *protocap = (struct xhci_extcap_proto *)(cap + 1);

	/* assume only dword accesses */
	if (width != 4) {
		pr_err("Guest accessed extcap in non 4-byte width!\n");
		return 0;
	}

	switch (offset) {
		case 0x0:
			value= cap->desc->id | (cap->next << 8) | \
				(protocap->minor << 16) | (protocap->major << 24);
			break;
		case 0x4:
			value = protocap->name;
			break;
		case 0x8:
			value = protocap->port_offset | (protocap->port_count << 8);
			break;
		case 0xC:
		default:
			value = 0;
	}
	return value;
}

struct xhci_extcap_desc extcap_proto_desc = {
	.id = XHCI_EXTCAP_PROTOCOLS,
	.priv_size = sizeof(struct xhci_extcap_proto),
	.name = "proto",
	.init = xhci_extcap_proto_init,
	.read = xhci_extcap_proto_read,
	.write = NULL,
};

static int xhci_add_default_extcaps(struct pci_xhci *xhci)
{
	int ret;
	struct xhci_extcap_proto_cfg cfg = {
		.xhci_configs = &xhci->configs,
		.usb3 = false,
	};
	ret = xhci_extcap_init(xhci, &extcap_proto_desc, &cfg);
	if (ret)
		return -1;
	cfg.usb3 = true;
	ret = xhci_extcap_init(xhci, &extcap_proto_desc, &cfg);
	if (ret)
		return -1;

	return 0;
}

int xhci_init_extcaps(struct pci_xhci *xhci)
{
	STAILQ_INIT(&xhci->extcaps);

	struct xhci_extcap_drd_cfg drd_cfg = {
		.bus = xhci->configs.drd_bus,
		.port = xhci->configs.drd_port,
	};
	if (xhci->configs.model == PCI_XHCI_APL) {
		if (xhci_extcap_init(xhci, &extcap_drd_desc, &drd_cfg)) {
			pr_err("failed to add drd extcap\n");
			return -1;
		}
	}

	if (xhci_add_default_extcaps(xhci)) {
		pr_err("fail to add extcaps\n");
		return -1;
	}

	return 0;
}

