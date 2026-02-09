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

struct xhci_extcap_drd {

};

static int xhci_extcap_drd_init(struct xhci_extcap *cap, const void *cfg)
{

	return 0;
}

static uint64_t xhci_extcap_drd_read(struct xhci_extcap *cap, uint16_t offset, size_t width)
{

	return 0;
}

static void xhci_extcap_drd_write(struct xhci_extcap *cap, uint16_t offset,
				size_t width, uint64_t data)
{

}


static struct xhci_extcap_desc extcap_drd_desc = {
	.id = XHCI_EXTCAP_DRD_INTEL,
	.priv_size = sizeof(struct xhci_extcap_drd),
	.name = "drd",
	.init = xhci_extcap_drd_init,
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
		pr_warn("Guest accessed extcap in non 4-byte width!\n");
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
	if (xhci->configs.model == PCI_XHCI_APL) {
		if (xhci_extcap_init(xhci, &extcap_drd_desc, NULL)) {
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

