/*
 * Copyright (C) 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#define pr_prefix "virtio-gpio: "

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <ctype.h>
#include <linux/types.h>
#include <linux/gpio.h>
#include <stddef.h>

#include "types.h"
#include "log.h"
#include "acpi.h"
#include "dm.h"
#include "pci_core.h"
#include "mevent.h"
#include "virtio.h"
#include "gpio_dm.h"

/*
 *  GPIO virtualization architecture
 *
 *                +--------------------------+
 *                |ACRN DM                   |
 *                |  +--------------------+  |
 *                |  |                    |  |  virtqueue
 *                |  |   GPIO mediator    |<-+-----------+
 *                |  |                    |  |           |
 *                |  +-+-----+--------+---+  |           |
 *   User space   +----|-----|--------|------+           |
 *           +---------+     |        |                  |
 *           v               v        v                  |
 *   +----------------+   +-----+   +----------------+   | +---------------+
 *  -+ /dev/gpiochip0 +---+ ... +---+ /dev/gpiochipN +-----+ User VM       +-
 *   +                +   +     +   +                +   | +/dev/gpiochip0 +
 *   +------------+---+   +--+--+   +-------------+--+   | +------+--------+
 *   Kernel space |          +--------------+     |      |        |
 *                +--------------------+    |     |      |        |
 *                                     v    v     v      |        v
 *   +---------------------+    +---------------------+  |  +--------------+
 *   |                     |    |                     |  |  |User VM Virtio|
 *   |  pinctrl subsystem  |<---+  gpiolib subsystem  |  +->+GPIO Driver   |
 *   |                     |    |                     |     |              |
 *   +--------+------------+    +----------+----------+     +--------------+
 *            |   +------------------------+
 *            |   |
 *  ----------|---|----------------------------------------------------------
 *   Hardware |   |
 *            v   v
 *   +------------------+
 *   |                  |
 *   | GPIO controllers |
 *   |                  |
 *   +------------------+
 */

/*
 *  GPIO IRQ virtualization architecture
 *
 *               Service VM                                     User VM
 *  +-------------------------------+
 *  |      virtio GPIO mediator     |
 *  | +-------------------------+   |
 *  | |     GPIO IRQ chip       |   | request 
 *  | | +-------------------+   |   | virtqueue
 *  | | |Enable, Disable    +<--|---|-----------+
 *  | | +-------------------+   |   |           |
 *  | |                         |   | event     |
 *  | | +-------------------+   |   | virtqueue |
 *  | | | Gen(Mask) & Unmask+---|---|--------+  |
 *  | | +-------------------+   |   |        |  |
 *  | +-------------------------+   |        |  |
 *  |   ^    ^    ^    ^            |        |  |
 *  +---|----|----|----|------------+        |  |
 * -----|----|----|----|---------------------+--+-------------------------------
 *    +-+----+----+----+-+                   |  | +------------+  +------------+
 *    | gpiolib framework|                   |  | |IRQ consumer|  |IRQ consumer|
 *    +------------------+                   |  | +------------+  +------------+
 *                                           |  | +----------------------------+
 *                                           |  | |   User VM gpiolib framework|
 *                                           |  | +----------------------------+
 *                                           |  | +----------------------+
 *                                           |  +-+   User VM virtio GPIO|
 *                                           +--->|   IRQ chip           |
 *                                                +----------------------+
 */

/* Virtio GPIO supports maximum number of virtual gpio */
#define VIRTIO_GPIO_MAX_LINES	64

/* Virtio GPIO capabilities */
#define VIRTIO_GPIO_F_IRQ	0
#define VIRTIO_GPIO_S_HOSTCAPS	((1UL << VIRTIO_F_VERSION_1) | \
				(1UL << VIRTIO_GPIO_F_IRQ))

#define VIRTIO_GPIO_RINGSZ	64

enum {
	VIRTIO_GPIO_VQ_REQUEST = 0,
	VIRTIO_GPIO_VQ_EVENT = 1,
	VIRTIO_GPIO_VQ_MAX = 2,
};

/* GPIO message types */
#define VIRTIO_GPIO_MSG_GET_LINE_NAMES		0x0001
#define VIRTIO_GPIO_MSG_GET_DIRECTION		0x0002
#define VIRTIO_GPIO_MSG_SET_DIRECTION		0x0003
#define VIRTIO_GPIO_MSG_GET_VALUE		0x0004
#define VIRTIO_GPIO_MSG_SET_VALUE		0x0005
#define VIRTIO_GPIO_MSG_SET_IRQ_TYPE		0x0006

/* GPIO Direction types */
#define VIRTIO_GPIO_DIRECTION_NONE		0x00
#define VIRTIO_GPIO_DIRECTION_OUT		0x01
#define VIRTIO_GPIO_DIRECTION_IN		0x02

const char *direction_strings[] = {
	[VIRTIO_GPIO_DIRECTION_NONE] = "none",
	[VIRTIO_GPIO_DIRECTION_OUT] = "out",
	[VIRTIO_GPIO_DIRECTION_IN] = "in"
};

/* GPIO interrupt types */
#define VIRTIO_GPIO_IRQ_TYPE_NONE		0x00
#define VIRTIO_GPIO_IRQ_TYPE_EDGE_RISING	0x01
#define VIRTIO_GPIO_IRQ_TYPE_EDGE_FALLING	0x02
#define VIRTIO_GPIO_IRQ_TYPE_EDGE_BOTH		0x03
#define VIRTIO_GPIO_IRQ_TYPE_LEVEL_HIGH		0x04
#define VIRTIO_GPIO_IRQ_TYPE_LEVEL_LOW		0x08

struct virtio_gpio_config {
	uint16_t	ngpio;	/* number of gpios */
	uint8_t		padding[2];
	uint32_t	gpio_names_size;
} __attribute__((packed));

struct virtio_gpio_request {
	uint16_t	type;
	uint16_t	gpio;
	uint32_t	value;
} __attribute__((packed));

/* Possible values of the status field */
#define VIRTIO_GPIO_STATUS_OK		0x0
#define VIRTIO_GPIO_STATUS_ERR		0x1

struct virtio_gpio_response {
	uint8_t	status;
	uint8_t	value[];	/* request specific return value(s) */
} __attribute__((packed));

struct virtio_gpio_irq_request {
	uint16_t	gpio;
} __attribute__((packed));

/* Possible values of the interrupt status field */
#define VIRTIO_GPIO_IRQ_STATUS_INVALID	0x0
#define VIRTIO_GPIO_IRQ_STATUS_VALID	0x1

struct virtio_gpio_irq_response {
	uint8_t		status;
} __attribute__((packed));

struct gpio_line;
struct gpio_line_group;

/* 
 * Although methods for a mask of lines are more generic and efficient, we make a per-line
 * abstraction here since virtio-gpio request is per-line.
 * It's possible to define pin mask operation for backends and combine multiple virtio
 * gpio request to fit for it. But we don't bother to do this optimization.
 *
 * All methods return 0 on success, -1 on failure. (Maybe we will have generic error
 * definitions for all acrn device model backends).
 */
struct gpio_backend {
	const char *name;
	bool (*match)(struct gpio_backend *be, const char *domain);
	/* return lines count */
	int (*init)(struct gpio_line_group *group, char *domain, char *opts);
	void (*deinit)(struct gpio_line_group *group);

	int (*set_direction)(struct gpio_line *line, uint8_t direction);
	int (*get_direction)(struct gpio_line *line, uint8_t *direction);
	int (*set_value)(struct gpio_line *line, uint8_t value);
	int (*get_value)(struct gpio_line *line, uint8_t *value);
	int (*set_irq_mode)(struct gpio_line *line, uint32_t irq_mode);
};

SET_DECLARE(gpio_backend_set, struct gpio_backend);
#define DEFINE_GPIO_BACKEND(x)	DATA_SET(gpio_backend_set, x)

static struct gpio_backend *gpio_get_backend(const char *domain)
{
	struct gpio_backend **bepp, *bep;

	SET_FOREACH(bepp, gpio_backend_set) {
		bep = *bepp;
		if (bep->match(bep, domain)) {
			return bep;
		}
	}
	return NULL;
}

bool gpio_backend_match_by_name(struct gpio_backend *be, const char *domain)
{
	return !strcmp(be->name, domain);
}

struct gpio_line_state {
	uint8_t		direction;
	uint8_t		value;
	uint64_t	irq_mode;	/* interrupt trigger mode, including disabled */
};
struct gpio_line {
	char		name[GPIO_MAX_NAME_SIZE];
	uint16_t	offset;
	struct gpio_line_state state;

	bool		irq_pending;	/* set when interrupt sensed but masked */
	uint64_t	irq_count;	/* irq accounting */
	uint16_t	idx;		/* virtio descriptor chain to release */
	struct virtio_gpio_irq_response *rsp;	/* virtio eventq response, NULL means masked */

	STAILQ_ENTRY(gpio_line) list;
	struct gpio_line_group	*group;
};

struct gpio_line_group {
	char *name;
	STAILQ_HEAD(, gpio_line) lines;
	STAILQ_ENTRY(gpio_line_group) list;
	struct virtio_gpio	*gpio;
	struct gpio_backend	*backend;
	void *private;
};

struct virtio_gpio {
	struct virtio_base	base;
	pthread_mutex_t	mtx;
	struct virtio_gpio_config	config;
	struct virtio_vq_info	queues[VIRTIO_GPIO_VQ_MAX];

	char chipname[GPIO_MAX_NAME_SIZE];
	STAILQ_HEAD(, gpio_line_group) groups;
	struct gpio_line	**lines; /* indexed by virtual line offset */
	uint32_t		line_count;
	pthread_mutex_t		intr_mtx;	/* one lock for all lines */
};

static uint32_t gpio_get_line_names(struct virtio_gpio *gpio, uint8_t *buf, int in_len)
{
	int i;
	uint32_t len = 0, cur;

	for (i = 0; i < gpio->line_count; i++) {
		const char *name = gpio->lines[i]->name;
		cur = strnlen(name, GPIO_MAX_NAME_SIZE);

		/* save to input buffer if there is enough space */
		if (buf && cur + 1 <= in_len) {
			if (cur) {
				memcpy(buf + len, name, cur);
			}
			buf[len + cur] = '\0';
			in_len -= cur + 1;
		}
		len += cur + 1;
	}
	return len;
}

static int gpio_set_value(struct virtio_gpio *gpio, uint16_t offset, uint8_t value)
{
	struct gpio_line *line;
	int rc;

	pr_dbg("%s: set line %d value to %d\n", gpio->chipname, offset, value);
	line = gpio->lines[offset];
	if (line->state.direction != VIRTIO_GPIO_DIRECTION_OUT) {
		pr_dbg("%s: stage value for later direction out\n", gpio->chipname);
	} else if (line->group->backend->set_value) {
		rc = line->group->backend->set_value(line, value);
		if (rc) {
			pr_err("%s: failed to set line %d value to %d\n",
					gpio->chipname, offset, value);
			return rc;
		}
	}
	line->state.value = value;
	return 0;
}

static int gpio_get_value(struct virtio_gpio *gpio, uint16_t offset, uint8_t *value)
{
	struct gpio_line *line;
	int rc;

	line = gpio->lines[offset];
	if (line->group->backend->get_value) {
		rc = line->group->backend->get_value(line, &line->state.value);
		if (rc) {
			pr_err("%s: failed to get line %d value\n",
					gpio->chipname, offset);
			return rc;
		}
	}
	*value = line->state.value;
	pr_dbg("%s: line %d value is %d\n", gpio->chipname, offset, *value);

	return 0;
}

static int gpio_set_direction(struct virtio_gpio *gpio, uint16_t offset, uint8_t direction)
{
	struct gpio_line *line;
	int rc;

	pr_dbg("%s: set line %d direction to %s\n",
			gpio->chipname, offset, direction_strings[direction]);
	line = gpio->lines[offset];

	if (line->group->backend->set_direction) {
		rc = line->group->backend->set_direction(line, direction);
		if (rc) {
			pr_err("%s: failed to set line %d direction to %s\n",
					gpio->chipname, offset,
					direction_strings[direction]);
			return rc;
		}
	}
	line->state.direction = direction;
	return 0;
}

static int gpio_get_direction(struct virtio_gpio *gpio, uint16_t offset, uint8_t *direction)
{
	struct gpio_line *line;
	int rc = 0;

	line = gpio->lines[offset];
	if (line->group->backend->get_direction) {
		rc = line->group->backend->get_direction(line, &line->state.direction);
		if (rc) {
			pr_err("%s: failed to get line %d direction\n",
					gpio->chipname, offset);
			return rc;
		}
	}
	*direction = line->state.direction;
	pr_dbg("%s: line %d direction is %s\n",
			gpio->chipname, offset, direction_strings[*direction]);
	return rc;
}

static int gpio_set_irq_mode(struct virtio_gpio *gpio, uint16_t offset, uint32_t mode)
{
	struct gpio_line *line;
	int rc;

	line = gpio->lines[offset];
	if (mode == line->state.irq_mode) {
		pr_warn("%s: line %d is already in irqmode %d, request ignored!\n",
				gpio->chipname, offset, mode);
		return 0;
	} else if (line->state.irq_mode != VIRTIO_GPIO_IRQ_TYPE_NONE &&
			mode != VIRTIO_GPIO_IRQ_TYPE_NONE) {
		pr_warn("%s: changing line %d irq mode %d -> %d is not allowed, fail it\n",
				gpio->chipname, offset, line->state.irq_mode, mode);
		return -1;
	}

	pthread_mutex_lock(&gpio->intr_mtx);

	if (mode == VIRTIO_GPIO_IRQ_TYPE_NONE) {
		line->irq_pending = false;
		if (line->rsp) {
			/* consume stale buffer in eventq  */
			pr_dbg("%s: clean stale irq unmask request for line %d\n",
					gpio->chipname, offset);
			line->rsp->status = VIRTIO_GPIO_IRQ_STATUS_INVALID;
			vq_relchain(&gpio->queues[VIRTIO_GPIO_VQ_EVENT], line->idx, 1);
			line->rsp = NULL;
			vq_endchains(&gpio->queues[VIRTIO_GPIO_VQ_EVENT], 0);
		}
	}

	if (line->group->backend->set_irq_mode) {
		rc = line->group->backend->set_irq_mode(line, mode);
		if (rc) {
			pthread_mutex_unlock(&gpio->intr_mtx);
			pr_err("%s: failed to set line %d irq mode to 0x%x\n",
					gpio->chipname, offset, mode);
			return rc;
		}
	}
	line->state.irq_mode = mode;

	pthread_mutex_unlock(&gpio->intr_mtx);
	pr_dbg("%s: set line %d irq mode to 0x%x\n", gpio->chipname, offset, mode);

	return 0;
}

static int gpio_request_handler(struct virtio_gpio *gpio,
		struct virtio_gpio_request *req,
		struct virtio_gpio_response *rsp)
{
	int rc = 0;

	/* even VIRTIO_GPIO_MSG_GET_LINE_NAMES needs a valid req->gpio */
	if (req->gpio >= gpio->line_count) {
		pr_info("%s: ignore request for invalid line %u\n",
				gpio->chipname, req->gpio);
		rsp->status = VIRTIO_GPIO_STATUS_ERR;
		rsp->value[0] = 0;
		return 0;
	}

	switch (req->type) {
	case VIRTIO_GPIO_MSG_GET_LINE_NAMES:
		/* have checked sizeof(rsp->value) == gpio->config.gpio_names_size */
		gpio_get_line_names(gpio, rsp->value, gpio->config.gpio_names_size);
		break;
	case VIRTIO_GPIO_MSG_SET_VALUE:
		rc = gpio_set_value(gpio, req->gpio, req->value);
		rsp->value[0] = 0;
		break;
	case VIRTIO_GPIO_MSG_GET_VALUE:
		rc = gpio_get_value(gpio, req->gpio, &rsp->value[0]);
		break;
	case VIRTIO_GPIO_MSG_SET_DIRECTION:
		rc = gpio_set_direction(gpio, req->gpio, req->value);
		rsp->value[0] = 0;
		break;
	case VIRTIO_GPIO_MSG_GET_DIRECTION:
		rc = gpio_get_direction(gpio, req->gpio, &rsp->value[0]);
		break;
	case VIRTIO_GPIO_MSG_SET_IRQ_TYPE:
		rc = gpio_set_irq_mode(gpio, req->gpio, req->value);
		break;
	default:
		pr_err("%s: invalid gpio request: %d\n", gpio->chipname, req->type);
		rc = -1;
		break;
	}
	rsp->status = rc < 0 ? VIRTIO_GPIO_STATUS_ERR : VIRTIO_GPIO_STATUS_OK;

	/*
	 * TODO need further definition for errors. For now we only return guest an error
	 * response and take the request as successfully handled.
	 * But maybe it's better if we do more.
	 */

	return 0;
}

static void virtio_gpio_notify(void *vdev, struct virtio_vq_info *vq)
{
	struct iovec iov[2];
	struct virtio_gpio *gpio;
	struct virtio_gpio_request *req;
	struct virtio_gpio_response *rsp;
	uint16_t idx;
	int n, rc;

	gpio = (struct virtio_gpio *)vdev;
	while (vq_has_descs(vq)) {
		n = vq_getchain(vq, &idx, iov, 2, NULL);
		if (n != 2) {
			pr_err("invalid chain number %d\n", n);
			continue;
		}

		req = iov[0].iov_base;
		rsp = iov[1].iov_base;
		if (iov[0].iov_len != sizeof(*req)) {
			pr_err("invalid req size %d\n", iov[0].iov_len);
			rc = 0;
		} else if (((req->type == VIRTIO_GPIO_MSG_GET_LINE_NAMES) &&
			(iov[1].iov_len != gpio->config.gpio_names_size + 1)) ||
			((req->type != VIRTIO_GPIO_MSG_GET_LINE_NAMES) &&
			(iov[1].iov_len != 2))) {
			pr_err("ignore request with invalid rsp size %d\n", iov[1].iov_len);
			rc = 0;
		} else {
			rc = gpio_request_handler(gpio, req, rsp);
		}

		if (rc) {
			pr_err("failed to handle request: error %d\n", rc);
		}
		/*
		 * Release this chain and handle more
		 */
		vq_relchain(vq, idx, iov[1].iov_len);
		vq_endchains(vq, 0);
	}
}

static void virtio_irq_notify(void *vdev, struct virtio_vq_info *vq)
{
	struct iovec iov[2];
	struct virtio_gpio *gpio;
	struct gpio_line *line;
	struct virtio_gpio_irq_request *ireq;
	struct virtio_gpio_irq_response *irsp;
	uint16_t idx;
	int n;

	gpio = (struct virtio_gpio *)vdev;
	while (vq_has_descs(vq)) { /* why not loop here to consume all descs ? */
		n = vq_getchain(vq, &idx, iov, 2, NULL);
		if (n != 2) {
			pr_err("invalid irq chain %d\n", n);
			continue;
		}

		ireq = iov[0].iov_base;
		irsp = iov[1].iov_base;
		if (iov[0].iov_len != sizeof(*ireq) || iov[1].iov_len != sizeof(*irsp)) {
			pr_err("invalid event request or response size\n");
			continue;
		}

		if (ireq->gpio >= gpio->line_count) {
			pr_err("ignore invalid IRQ gpio %d\n", ireq->gpio);
			continue;
		}

		line = gpio->lines[ireq->gpio];

		pr_dbg("%s: unmask line %d\n", gpio->chipname, line->offset);
		pthread_mutex_lock(&gpio->intr_mtx);

		bool evtq_desc_used = false;
		if (line->state.irq_mode != VIRTIO_GPIO_IRQ_TYPE_NONE) {
			if (line->irq_pending) {
				irsp->status = VIRTIO_GPIO_IRQ_STATUS_VALID;
				vq_relchain(vq, idx, 1);
				evtq_desc_used = true;
				line->irq_pending = false;
				line->irq_count += 1;
				pr_dbg("%s: deliver interrupt for line %d: valid\n", gpio->chipname, line->offset);
			} else {
				if (line->rsp) {
					pr_warn("guest BUG! line %d was unmasked twice\n", line->offset);
					line->rsp->status = VIRTIO_GPIO_IRQ_STATUS_INVALID;
					vq_relchain(vq, line->idx, 1);
					evtq_desc_used = true;
					pr_dbg("%s: deliver interrupt for line %d: invalid\n", gpio->chipname, line->offset);
				}
				line->idx = idx;
				line->rsp = irsp;
				pr_dbg("%s: record event buffer for line %d\n", gpio->chipname, line->offset);
			}
		} else {
			irsp->status = VIRTIO_GPIO_IRQ_STATUS_INVALID;
			vq_relchain(vq, idx, 1);
			evtq_desc_used = true;
			pr_dbg("%s: deliver interrupt for line %d: invalid\n", gpio->chipname, line->offset);
		}

		if (evtq_desc_used) {
			vq_endchains(vq, 0);
		}
		pthread_mutex_unlock(&gpio->intr_mtx);
	}
}

/* API for backends to raise an irq */
void virtio_gpio_raise_irq(struct gpio_line* line)
{
	struct virtio_gpio *gpio = line->group->gpio;
	struct virtio_vq_info *vq = &gpio->queues[VIRTIO_GPIO_VQ_EVENT];

	bool evtq_desc_used = false;
	pthread_mutex_lock(&gpio->intr_mtx);
	if (!line->rsp) {
		line->irq_pending = true;
		pr_dbg("%s: interrupt for line %d is pending\n", gpio->chipname, line->offset);
	} else {
		line->rsp->status = VIRTIO_GPIO_IRQ_STATUS_VALID;
		vq_relchain(vq, line->idx, 1);
		evtq_desc_used = true;
		line->irq_pending = false;
		line->irq_count += 1;
		line->rsp = NULL;
		pr_dbg("%s: deliver interrupt for line %d: valid\n", gpio->chipname, line->offset);
	}

	if (evtq_desc_used) {
		vq_endchains(vq, 0);
	}
	pthread_mutex_unlock(&gpio->intr_mtx);
}

static void virtio_gpio_reset(void *vdev)
{
	struct virtio_gpio *gpio;

	gpio = vdev;

	pr_info("device reset requested!\n");
	virtio_reset_dev(&gpio->base);
}

static int
virtio_gpio_cfgread(void *vdev, int offset, int size, uint32_t *retval)
{
	struct virtio_gpio *gpio = vdev;
	void *ptr;
	int cfg_size;

	cfg_size = sizeof(struct virtio_gpio_config);
	if (offset < 0 || offset >= cfg_size) {
		pr_warn("read from invalid reg %d\n", offset);
		return -1;
	} else {
		ptr = (uint8_t *)&gpio->config + offset;
		memcpy(retval, ptr, size);
	}
	return 0;
}

static struct virtio_ops virtio_gpio_ops = {
	"virtio_gpio",			/* our name */
	VIRTIO_GPIO_VQ_MAX,		/* requestq and eventq */
	sizeof(struct virtio_gpio_config), 	/* config reg size */
	virtio_gpio_reset,		/* reset */
	NULL,				/* device-wide qnotify */
	virtio_gpio_cfgread,		/* read virtio config */
	NULL,				/* write virtio config */
	NULL,				/* apply negotiated features */
	NULL,				/* called on guest set status */
};

static void virtio_gpio_deinit_lines(struct virtio_gpio *gpio)
{
	struct gpio_line_group *group;

	while((group = STAILQ_FIRST(&gpio->groups))) {
		if (group->backend->deinit)
			group->backend->deinit(group);
		STAILQ_REMOVE(&gpio->groups, group, gpio_line_group, list);
		free(group->name);
		free(group);
	}

	if (gpio->lines)
		free(gpio->lines);
}

/* Actually this includes parameter parsing and line initialization */
static int virtio_gpio_parse_opts(struct virtio_gpio *gpio, char *opts)
{
	int rc = -1, l;
	char *b, *o, *tmp;
	int line_count = 0;
	struct gpio_line_group *group;
	struct gpio_line *line;

	/*
	 * -s <slot>,virtio-gpio,<gpio resources>
	 * <gpio resources> format
	 * <@domain0{<domain specific>}
	 * [@domain1{domain specific}]
	 * ...>
	 * <@domain0{}
	 * [@domain1{id[=vname]:id[=vname]:...}]
	 * [@domain2{id[=vname]:id[=vname]:...}]
	 * ...>
	 * Where:
	 * domain: can be gpiochip name, like "gpiochip0", or backend
	 * specific tag, e.g. socket file path.
	 * <domain specific> for physical gpiochip domain:
	 *	id[=vname]:id[=vname]:...
	 *	id: the physical gpio line offset or pin name.
	 *	vname: is the virtual pin name to be exposed to guest.
	 */
	b = o = strdup(opts);
	if (*o == '@')
		o += 1;
	while ((tmp = strsep(&o, "@")) != NULL) {
		char *domain, *dopts;

		domain = strsep(&tmp, "{");
		dopts = strsep(&tmp, "}");
		if (!dopts || !tmp) {
			pr_err("invalid argument: %s\n", domain);
			goto err;
		}

		struct gpio_backend *backend = gpio_get_backend(domain);
		if (!backend) {
			pr_err("unknown domain: %s\n", domain);
			goto err;
		}

		group = calloc(1, sizeof(*group));
		if (!group) {
			pr_err("calloc gpio_line_group failed\n");
			goto err;
		}
		group->name = strdup(domain);
		group->gpio = gpio;
		group->backend = backend;
		STAILQ_INIT(&group->lines);
		rc = backend->init(group, domain, dopts);
		if (rc) {
			free(group);
			goto err;
		}

		l = 0;
		STAILQ_FOREACH(line, &group->lines, list) {
			l += 1;
		}
		STAILQ_INSERT_TAIL(&gpio->groups, group, list);
		pr_dbg("add group: %s with %d lines\n", group->name, l);
		line_count += l;
	}

	gpio->lines = calloc(line_count, sizeof(struct gpio_line *));
	if (!gpio->lines)
		goto err;

	STAILQ_FOREACH(group, &gpio->groups, list) {
		STAILQ_FOREACH(line, &group->lines, list) {
			gpio->lines[gpio->line_count] = line;
			gpio->lines[gpio->line_count]->offset = gpio->line_count;
			gpio->lines[gpio->line_count]->group = group;
			gpio->line_count += 1;
		}
	}

	free(b);
	return 0;

err:
	virtio_gpio_deinit_lines(gpio);
	free(b);
	return rc;
}

static int virtio_gpio_init(struct vmctx *ctx, struct pci_vdev *dev, char *opts)
{
	struct virtio_gpio *gpio;
	pthread_mutexattr_t attr;
	int rc = -1;

	if (!opts) {
		pr_err("needs gpio information\n");
		return rc;
	}

	gpio = calloc(1, sizeof(struct virtio_gpio));
	if (!gpio) {
		pr_err("failed to calloc virtio_gpio\n");
		return rc;
	}
	snprintf(gpio->chipname, GPIO_MAX_NAME_SIZE, "gpio@%02x:%02x.%01x",
			dev->bus, dev->slot, dev->func);
	STAILQ_INIT(&gpio->groups);

	pthread_mutexattr_init(&attr);
	if ((rc = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE))) {
		pr_err("mutexattr_settype failed with error %d!\n", rc);
		goto err_free;
	}
	pthread_mutex_init(&gpio->mtx, &attr);
	pthread_mutex_init(&gpio->intr_mtx, NULL);

	rc = virtio_gpio_parse_opts(gpio, opts);
	if (rc) {
		pr_err("failed to initialize %s\n", gpio->chipname);
		goto err_mtx;
	}

	virtio_linkup(&gpio->base, &virtio_gpio_ops, gpio, dev, gpio->queues, BACKEND_VBSU);

	gpio->config.ngpio = gpio->line_count;
	gpio->config.gpio_names_size = gpio_get_line_names(gpio, NULL, 0);

	gpio->base.device_caps = VIRTIO_GPIO_S_HOSTCAPS;
	gpio->base.mtx = &gpio->mtx;
	gpio->queues[VIRTIO_GPIO_VQ_REQUEST].qsize = VIRTIO_GPIO_RINGSZ;
	gpio->queues[VIRTIO_GPIO_VQ_REQUEST].notify = virtio_gpio_notify;
	gpio->queues[VIRTIO_GPIO_VQ_EVENT].qsize = VIRTIO_GPIO_RINGSZ;
	gpio->queues[VIRTIO_GPIO_VQ_EVENT].notify = virtio_irq_notify;

	/* initialize config space */
	pci_set_cfgdata16(dev, PCIR_VENDOR, VIRTIO_VENDOR);
	pci_set_cfgdata16(dev, PCIR_DEVICE, VIRTIO_DEV_GPIO);
	pci_set_cfgdata16(dev, PCIR_REVID, 1);
	pci_set_cfgdata8(dev, PCIR_CLASS, PCIC_SIMPLECOMM);
	pci_set_cfgdata8(dev, PCIR_SUBCLASS, PCIS_SIMPLECOMM_OTHER);
	pci_set_cfgdata16(dev, PCIR_SUBVEND_0, VIRTIO_VENDOR);
	pci_set_cfgdata16(dev, PCIR_SUBDEV_0, VIRTIO_TYPE_GPIO);

	/* use BAR 1 to map MSI-X table and PBA, if we're using MSI-X */
	if ((rc = virtio_interrupt_init(&gpio->base, virtio_uses_msix()))) {
		pr_err("MSI interrupt init failed.\n");
		goto err_deinit;
	}
	if ((rc = virtio_set_modern_bar(&gpio->base, false))) {
		pr_err("set modern bar error\n");
		goto err_deinit;
	}
	return 0;

err_deinit:
	virtio_gpio_deinit_lines(gpio);
err_mtx:
	pthread_mutex_destroy(&gpio->intr_mtx);
	pthread_mutex_destroy(&gpio->mtx);
err_free:
	pthread_mutexattr_destroy(&attr);
	free(gpio);
	dev->arg = NULL;
	return rc;
}

static void virtio_gpio_deinit(struct vmctx *ctx, struct pci_vdev *dev, char *opts)
{
	struct virtio_gpio *gpio = (struct virtio_gpio *)dev->arg;

	if (gpio) {
		virtio_gpio_deinit_lines(gpio);
		pthread_mutex_destroy(&gpio->intr_mtx);
		pthread_mutex_destroy(&gpio->mtx);
		virtio_gpio_reset(gpio);
		free(gpio);
		dev->arg = NULL;
	}
}

struct pci_vdev_ops pci_ops_virtio_gpio = {
	.class_name	= "virtio-gpio",
	.vdev_init	= virtio_gpio_init,
	.vdev_deinit	= virtio_gpio_deinit,
	.vdev_barwrite	= virtio_pci_write,
	.vdev_barread	= virtio_pci_read,
};
DEFINE_PCI_DEVTYPE(pci_ops_virtio_gpio);

/* --------------------- gpio backends --------------------- */

#ifdef GPIO_MOCK
struct gpio_mock_line {
	struct gpio_line	line;
	//char			*name;
	uint8_t			input_value;
	LIST_ENTRY(gpio_mock_line) list;
};
static LIST_HEAD(, gpio_mock_line) mock_lines;

#undef pr_prefix
#define pr_prefix "virtio-gpio: mock: "

/* -s virtio-gpio,@mock{name1:name2:...} */
static int gpio_mock_init(struct gpio_line_group *group, char *domain, char *opts)
{
	char *vname;
	struct virtio_gpio *gpio = group->gpio;

	while((vname = strsep(&opts, ":"))) {
		if (*vname == '\0')
			continue;

		pr_dbg("%s: add line %s\n", gpio->chipname, vname);

		struct gpio_mock_line *mline = calloc(1, sizeof(*mline));
		if (!mline) {
			pr_err("%s: allocation failed\n", gpio->chipname);
			return -1;
		}
		strncpy(mline->line.name, vname, GPIO_MAX_NAME_SIZE-1);
		STAILQ_INSERT_TAIL(&group->lines, &mline->line, list);
		LIST_INSERT_HEAD(&mock_lines, mline, list);
	}

	return 0;
}

static void gpio_mock_deinit(struct gpio_line_group *group)
{
	struct gpio_mock_line *mline;
	struct gpio_line *line;

	while ((line = STAILQ_FIRST(&group->lines))) {
		mline = container_of(line, struct gpio_mock_line, line);
		STAILQ_REMOVE(&group->lines, line, gpio_line, list);
		LIST_REMOVE(mline, list);
		free(mline);
	}
}

static int gpio_mock_get_value(struct gpio_line *line, uint8_t *value)
{
	struct gpio_mock_line *mline = container_of(line, struct gpio_mock_line, line);
	*value = mline->input_value;
	return 0;
}

static struct gpio_backend gpio_mock = {
	.name = "mock",
	.match = gpio_backend_match_by_name,
	.init = gpio_mock_init,
	.deinit = gpio_mock_deinit,
	.get_value = gpio_mock_get_value,
};
DEFINE_GPIO_BACKEND(gpio_mock);

/* external API */
struct gpio_mock_line *gpio_mock_line_find(const char *name)
{
	struct gpio_mock_line *mline;

	LIST_FOREACH(mline, &mock_lines, list) {
		if (!strcmp(mline->line.name, name))
			return mline;
	}
	return NULL;
}

/* external API */
int gpio_mock_line_set_value(struct gpio_mock_line *mline, uint8_t value)
{
	struct gpio_line_state state = mline->line.state;
	/* Guest may be changing the direction right now */
	if (state.direction == VIRTIO_GPIO_DIRECTION_OUT)
		pr_warn("set mock line value for an output line, ignored!\n");

	if (mline->input_value == value)
		return 0;
	mline->input_value = value;

	if ((mline->input_value && (state.irq_mode &
		(VIRTIO_GPIO_IRQ_TYPE_EDGE_RISING | VIRTIO_GPIO_IRQ_TYPE_LEVEL_HIGH))) ||
		(!mline->input_value && (state.irq_mode &
		(VIRTIO_GPIO_IRQ_TYPE_EDGE_FALLING | VIRTIO_GPIO_IRQ_TYPE_LEVEL_LOW))))
		virtio_gpio_raise_irq(&mline->line);
	return 0;
}
#endif	/* GPIO_MOCK */
