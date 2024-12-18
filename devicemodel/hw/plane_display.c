/*
 * Copyright (C) 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <pixman.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "log.h"
#include "vdisplay.h"

#include "misc/drm-private.h"

#define PD_BACKEND_NAME "plane-display"

#define PD_MSG_MEMFD_MAGIC 0xaabb

#define PD_SOCKET_NAME "/tmp/plane-display-%s"

#define PD_PLANE_COUNT	3

/**
 * @brief Read Time Stamp Counter (TSC).
 *
 * @return TSC value
 */
static inline uint64_t rdtsc(void)
{
	uint32_t lo, hi;

	asm volatile("rdtsc" : "=a" (lo), "=d" (hi));
	return ((uint64_t)hi << 32U) | lo;
}

struct pd_param {
	int org_x;
	int org_y;
	int guest_width;
	int guest_height;
	char output_name[32];
};

struct pd_plane_buffer {
	uint32_t fb_width;
	uint32_t fb_height;
	uint32_t x;
	uint32_t y;
	uint32_t width;
	uint32_t height;
	uint32_t crtc_x;
	uint32_t crtc_y;
	uint32_t crtc_w;
	uint32_t crtc_h;
	uint64_t rotation;
	uint64_t modifier;
	uint32_t pixel_blend_mode;
	uint16_t alpha;
	int dmabuf_fd[4];
	int dmabuf_cnt;
	uint32_t surf_fourcc;
	uint32_t stride[4];
	uint32_t offset[4];
	bool is_set;
	bool is_set_blend;
};

enum pd_command {
	PD_COMMAND_UPDATE_LAYERS = 0x01,
	PD_COMMAND_FINISH = 0x02,
};

struct pd_msg_hdr {
	uint32_t command;
	uint32_t size;
	uint32_t fd_count;
	uint32_t padding;
	uint64_t frame_nr;
	uint64_t tsc;
};

struct pd_screen {
	struct pd_param *param;
	struct pd_plane_buffer plane[PD_PLANE_COUNT];
	int dmabuf_fd[12];
	int dma_cnt;
	int data_socket_fd;
	pthread_mutex_t socket_mutex;
	uint64_t frame_nr;

	uint64_t frame_to_send;
	int shared_memfd;
	void *shared_addr;
	bool connected;

	bool thread_enable;
	int fd;
	pthread_t vblank_thread;
	void *virtio_data;
	vblank_inject_func vblank_inject;
	int vblankq_id;
};

static struct pd_info {
	struct pd_screen **pd_screen_array;
	int num;
	int index;
	bool is_support_sprite;
	struct iovec sendbuf;
	struct pd_param window_param[VSCREEN_MAX_NUM];
} global_pd_info = {
	.num = 0,
	.index = 0,
};

static void
send_command(int socket, uint32_t command, void *data, int data_len,
	     int *fds, int n_fds, uint64_t frame_nr)
{
	struct msghdr msg = { 0 };
	struct iovec io[1];
	struct cmsghdr *cmsg;
	struct pd_msg_hdr header_buffer = {
		.command = command,
		.size = data != NULL ? data_len : 0,
		.frame_nr = frame_nr,
		.tsc = rdtsc(),
	};
	char buf[CMSG_SPACE(n_fds * sizeof(int))];
	int ret;

	ret = send(socket, &header_buffer, sizeof(header_buffer), 0);
	if (ret != sizeof(header_buffer)) {
		pr_err("Failed to send message: %s\n", strerror(errno));
		return;
	}

	if (data == NULL)
		return;

	memset(buf, 0, sizeof(buf));

	io[0].iov_base = data;
	io[0].iov_len = data_len;

	msg.msg_iov = io;
	msg.msg_iovlen = 1;

	msg.msg_control = buf;
	msg.msg_controllen = sizeof(buf);

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(n_fds * sizeof(int));

	memcpy(CMSG_DATA(cmsg), fds, n_fds * sizeof(int));

	if (sendmsg(socket, &msg, 0) < 0) {
		pr_err("Failed to send message: %s\n", strerror(errno));
	}
}

static void
pd_set_modifier(void *backend, int64_t modifier)
{
	struct pd_screen *pd = (struct pd_screen*)backend;

	pd->plane[0].modifier = modifier;
}

static void
pd_surface_set(void *backend, struct surface *surf)
{
	struct pd_screen *pd = (struct pd_screen*)backend;

	if (surf == NULL) {
		pd->plane[0].is_set = false;
		pd->plane[0].dmabuf_cnt = 0;
		return;
	}

	if (surf->surf_type != SURFACE_DMABUF) {
		pr_err("%s got invalid surf_type: 0x%x\n", __func__,
			surf->surf_type);
		return;
	}

	pd->plane[0].fb_width = surf->fb_width;
	pd->plane[0].fb_height = surf->fb_height;
	pd->plane[0].width = surf->width;
	pd->plane[0].height = surf->height;
	pd->plane[0].crtc_x = surf->dst_x;
	pd->plane[0].crtc_y = surf->dst_y;
	pd->plane[0].crtc_w = surf->dst_width;
	pd->plane[0].crtc_h = surf->dst_height;
	pd->plane[0].dmabuf_fd[0] = surf->dma_info.dmabuf_fd;
	pd->plane[0].dmabuf_cnt = 1;
	for (int i = 0; i < 4; i++) {
		pd->plane[0].stride[i] = surf->stride[i];
		pd->plane[0].offset[i] = surf->offset[i];
	}
	pd->plane[0].surf_fourcc = surf->dma_info.surf_fourcc;
	pd->plane[0].is_set = true;
}

static uint32_t
v2p_planeid(int vid)
{
	switch (vid) {
	case 0:
		return 0;
	case 1:
		return 5;
	default:
		return vid - 1;
	}
}

static void
pd_set_scaling(void *backend, int plane_id, int x1, int y1, int x2, int y2)
{
	struct pd_screen *pd = (struct pd_screen*)backend;

	plane_id = v2p_planeid(plane_id);

	if (plane_id >= PD_PLANE_COUNT) {
		pr_err("plane_id %d is out of range\n", plane_id);
		return;
	}

	pd->plane[plane_id].crtc_x = x1;
	pd->plane[plane_id].crtc_y = y1;
	pd->plane[plane_id].crtc_w = x2;
	pd->plane[plane_id].crtc_h = y2;
}

static void
pd_server_send_update_layers(struct pd_screen *pd)
{
	int i, j;

	pthread_mutex_lock(&pd->socket_mutex);
	pd->dma_cnt = 0;
	for (i = 0; i < PD_PLANE_COUNT; i++) {
		if (pd->plane[i].is_set) {
			for (j=0; j < pd->plane[i].dmabuf_cnt; j++) {
				pd->dmabuf_fd[pd->dma_cnt] =
					pd->plane[i].dmabuf_fd[j];
				pd->dma_cnt++;
			}
		}
	}

	send_command(pd->data_socket_fd, PD_COMMAND_UPDATE_LAYERS,
		     &pd->plane[0], sizeof(pd->plane[0]) * PD_PLANE_COUNT,
		     pd->dmabuf_fd, pd->dma_cnt,
		     pd->frame_nr++);

	for (i = 0; i < PD_PLANE_COUNT; i++) {
		if (pd->plane[i].is_set) {
			pd->plane[i].dmabuf_cnt = 0;
			pd->plane[i].is_set_blend = 0;
			pd->plane[i].is_set = 0;
		}
	}

	pthread_mutex_unlock(&pd->socket_mutex);
}

static void
pd_surface_update(void *backend, struct surface *surf)
{
	struct pd_screen *pd = (struct pd_screen*)backend;

	if (surf == NULL) {
		pr_err("%s got empty surf\n", __func__);
		return;
	}
	if (surf->surf_type != SURFACE_DMABUF) {
		pr_err("%s got invalid surf_type: 0x%x\n", __func__,
			surf->surf_type);
		return;
	}
	if (surf->dma_info.dmabuf_fd < 0) {
		pr_err("got invalid dmabuf_fd: %d\n", __func__,
			surf->dma_info.dmabuf_fd);
		return;
	}

	pd->plane[0].dmabuf_fd[0] = surf->dma_info.dmabuf_fd;
	if (!pd->plane[0].dmabuf_cnt)
		pd->plane[0].dmabuf_cnt = 1;
	pd->plane[0].is_set = 1;

	if (global_pd_info.is_support_sprite)
		return;

	pd_server_send_update_layers(pd);
}

static void
pd_display_info(void *backend, struct display_info *display)
{
	struct pd_screen *pd = (struct pd_screen*)backend;
	struct pd_param *param;
	for (int i = 0; i < global_pd_info.num; ++i) {
		param = &global_pd_info.window_param[i];
		if (global_pd_info.pd_screen_array[i] == pd) {
			display->xoff = param->org_x;
			display->yoff = param->org_y;
			display->width = param->guest_width;
			display->height = param->guest_height;
			return;
		}
	}
}

static void
pd_update_sprite(void *backend, int plane_id, struct surface *surf)
{
	struct pd_screen *pd = (struct pd_screen*)backend;
	int i;

	plane_id = v2p_planeid(plane_id);

	if (plane_id >= PD_PLANE_COUNT) {
		pr_err("plane_id %d is out of range\n", plane_id);
		return;
	}
	pd->plane[plane_id].dmabuf_fd[0] = surf->dma_info.dmabuf_fd;

	for (i = 0; i < surf->dma_info.dmabuf_planar_fd_cnt; i++)
		pd->plane[plane_id].dmabuf_fd[i + 1] =
			surf->dma_info.dmabuf_planar_fd[i];


	pd->plane[plane_id].dmabuf_cnt = surf->dma_info.dmabuf_planar_fd_cnt + 1;
	pd->plane[plane_id].fb_width = surf->fb_width;
	pd->plane[plane_id].fb_height = surf->fb_height;
	pd->plane[plane_id].x = surf->x;
	pd->plane[plane_id].y = surf->y;
	pd->plane[plane_id].width = surf->width;
	pd->plane[plane_id].height = surf->height;
	pd->plane[plane_id].modifier = surf->modifier;
	for (int i = 0; i < 4; i++) {
		pd->plane[plane_id].stride[i]= surf->stride[i];
		pd->plane[plane_id].offset[i]= surf->offset[i];
	}
	pd->plane[plane_id].surf_fourcc = surf->dma_info.surf_fourcc;
	pd->plane[plane_id].is_set = 1;
}

static void pd_sprite_flush_sync(void *backend)
{
	struct pd_screen *pd = (struct pd_screen*)backend;

	pd_server_send_update_layers(pd);
}

/* Format supported by i915.
 * TODO: Query kernel for these values. */
static uint32_t plane_info[60]={
	29,
	538982467,
	909199186,
	875713112,
	875709016,
	875713089,
	875708993,
	808669784,
	808665688,
	808669761,
	808665665,
	1211388504,
	1211384408,
	1211388481,
	1211384385,
	1448695129,
	1431918169,
	1498831189,
	1498765654,
	842094158,
	808530000,
	842084432,
	909193296,
	808530521,
	842084953,
	909193817,
	1448434008,
	808670808,
	909334104,
	942954072,
	29,
	538982467,
	909199186,
	875713112,
	875709016,
	875713089,
	875708993,
	808669784,
	808665688,
	808669761,
	808665665,
	1211388504,
	1211384408,
	1211388481,
	1211384385,
	1448695129,
	1431918169,
	1498831189,
	1498765654,
	842094158,
	808530000,
	842084432,
	909193296,
	808530521,
	842084953,
	909193817,
	1448434008,
	808670808,
	909334104,
	942954072,
};

static void
pd_get_plane_info(void *backend, uint32_t *size, uint32_t *num, uint32_t *info)
{
	int sprite_num;
	sprite_num = 2;

	memcpy(info, &plane_info, sizeof(plane_info));

	*num = sprite_num;
	*size = 60;
}

static void
pd_get_plane_rotation(void *backend, int plane_id,
		      uint64_t *rotation, uint32_t *count)
{
	*rotation = 1;
	*count = 1;
}

static void
pd_set_rotation(void *backend, int plane_id, uint64_t rotation)
{
	struct pd_screen *pd = (struct pd_screen*)backend;

	plane_id = v2p_planeid(plane_id);

	if (plane_id >= PD_PLANE_COUNT) {
		pr_err("plane_id %d is out of range\n", plane_id);
		return;
	}
	pd->plane[plane_id].rotation = rotation;
}

static void
pd_set_pixel_blend_mode(void *backend, int plane_id, uint32_t mode,
			uint16_t alpha)
{
	struct pd_screen *pd = (struct pd_screen*)backend;

	plane_id = v2p_planeid(plane_id);

	if (plane_id >= PD_PLANE_COUNT) {
		pr_err("plane_id %d is out of range\n", plane_id);
		return;
	}
	pd->plane[plane_id].pixel_blend_mode = mode;
	pd->plane[plane_id].alpha = alpha;
	pd->plane[plane_id].is_set_blend = 1;
}

static void
pd_set_planar(void *backend, int plane_id, uint32_t size, uint32_t *dmabuf)
{
	struct pd_screen *pd = (struct pd_screen*)backend;
	int i;

	plane_id = v2p_planeid(plane_id);

	if (plane_id >= PD_PLANE_COUNT) {
		pr_err("plane_id %d is out of range\n", plane_id);
		return;
	}
	pd->plane[plane_id].dmabuf_cnt = size+1;

	for (i = 0; i < size; i++) {
		pd->plane[plane_id].dmabuf_fd[i+1] = *(dmabuf+i);
	}
}

static void
vblank_flip_handler(int fd, unsigned int frame, unsigned int sec,
		    unsigned int usec, unsigned int flip_sequence,
		    void *data)
{
	struct pd_screen *pd = data;
	drmVBlank vbl;

	// Connection with weston not established, signal all vblank event
	// to make everyone proceed.
	if ((flip_sequence == 0) || (flip_sequence == frame)) {
		uint64_t *shared_frame_to_send = pd->shared_addr;
		// TODO: Add memory barrier here?
		pd->frame_to_send = *shared_frame_to_send;
	}
	pd->vblank_inject(pd->virtio_data, pd->frame_to_send, pd->vblankq_id);

	vbl.request.type = DRM_VBLANK_RELATIVE | DRM_VBLANK_EVENT | DRM_VBLANK_FLIP;
	vbl.request.sequence = 1;
	vbl.request.signal = (unsigned long)data;
	drmWaitVBlank(fd, &vbl);
}

static void
vblank_handler(int fd, unsigned int frame, unsigned int sec,
	       unsigned int usec, void *data)
{
	vblank_flip_handler(fd, frame, sec, usec, 0, data);
}

static void *
pd_generate_vblank(void *arg)
{
	___drmEventContext evctx;
	int ret, fd, max_fd;
	struct pd_screen *pd = (struct pd_screen *)arg;
	drmVBlank vbl;
	fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	if(fd < 0) {
		pr_err("failed to open drm device\n");
		return NULL;
	}

	pd->fd = fd;

	pd->vblankq_id = 2;

	// Use vblank_flip event, which is basically the same as vblank event
	// with flip sequence.
	vbl.request.type = DRM_VBLANK_RELATIVE | DRM_VBLANK_EVENT | DRM_VBLANK_FLIP;
	vbl.request.sequence = 1;
	vbl.request.signal = (unsigned long) pd;
	ret = drmWaitVBlank(pd->fd, &vbl);
	if (ret != 0) {
		pr_err("drmWaitVBlank failed: %s\n", strerror(errno));
		return NULL;
	}

	memset(&evctx, 0, sizeof evctx);
	evctx.version = DRM_EVENT_CONTEXT_VERSION;
	evctx.vblank_handler = vblank_handler;
	evctx.vblank_flip_handler = vblank_flip_handler;
	evctx.page_flip_handler = NULL;;
	max_fd = (pd->fd > pd->data_socket_fd ?
		pd->fd : pd->data_socket_fd) + 1;

	while (pd->thread_enable) {
		struct timeval timeout = { .tv_sec = 3, .tv_usec = 0 };
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(pd->fd, &fds);
		FD_SET(pd->data_socket_fd, &fds);
		ret = select(max_fd , &fds, NULL, NULL, &timeout);
		if (ret > 0) {
			if (FD_ISSET(pd->fd ,&fds)) {
				ret = ___drmHandleEvent(pd->fd, &evctx);
				if (ret != 0) {
					printf("drmHandleEvent failed: %i\n", ret);
				}
			} else if (FD_ISSET(pd->data_socket_fd, &fds)) {
				uint64_t buf[2];
				ret = recv(pd->data_socket_fd, buf, sizeof(buf), 0);
				if (ret != sizeof(buf)) {
					pr_err("failed to recv data from weston\n");
				}
				pd->connected = true;
			}
		}
	}
	return NULL;
}

static void
pd_enable_vblank(void *backend)
{
	struct pd_screen *pd = (struct pd_screen*)backend;
	struct sched_param params;
	int ret;

	pr_info("enable vblank\n");

	pd->thread_enable= true;
	pthread_create(&pd->vblank_thread, NULL, pd_generate_vblank,
			pd);
	params.sched_priority = sched_get_priority_max(SCHED_FIFO);
	ret = pthread_setschedparam(pd->vblank_thread,SCHED_FIFO,&params);
	if (ret)
		pr_err("failed to set vblank thread as top priority\n");
}


static void
pd_inject_register(void *backend, vblank_inject_func func, void *data)
{
	struct pd_screen *pd = (struct pd_screen*)backend;

	pd->vblank_inject = func;
	pd->virtio_data = data;
}

static struct screen_backend_ops pd_vscreen_ops = {
	.vdpy_surface_set = pd_surface_set,
	.vdpy_surface_update = pd_surface_update,
	.vdpy_set_modifier = pd_set_modifier,
	.vdpy_set_scaling = pd_set_scaling,
	.vdpy_display_info = pd_display_info,
	.vdpy_get_plane_info = pd_get_plane_info,
	.vdpy_get_plane_rotation = pd_get_plane_rotation,
	.vdpy_set_rotation = pd_set_rotation,
	.vdpy_set_pixel_blend_mode = pd_set_pixel_blend_mode,
	.vdpy_set_planar = pd_set_planar,
	.vdpy_update_sprite = pd_update_sprite,
	.vdpy_sprite_flush_sync = pd_sprite_flush_sync,
	.vdpy_enable_vblank = pd_enable_vblank,
	.vdpy_vblank_init = pd_inject_register,
};

static int
receive_memfd(int fd) {
	struct msghdr msg = {0};
	struct iovec io[2];
	struct cmsghdr *cmsg;
	int dummy[1] = { 0 };
	char buf[CMSG_SPACE(1 * sizeof(int))];
	int memfd;

	memset(buf, 0, sizeof(buf));

	io[0].iov_base = dummy;
	io[0].iov_len = sizeof(dummy);

	msg.msg_iov = io;
	msg.msg_iovlen = 1;

	msg.msg_control = buf;
	msg.msg_controllen = sizeof(buf);

	if (recvmsg(fd, &msg, 0) < 0) {
		pr_err("Failed to send message: %s\n", strerror(errno));
		return -1;
	}
	if (dummy[0] != PD_MSG_MEMFD_MAGIC) {
		pr_err("invalid memfd message\n");
	}
	cmsg = CMSG_FIRSTHDR(&msg);
	memfd = * (int *) CMSG_DATA(cmsg);
	return memfd;
}

static int
pd_server_memfd_init(struct pd_screen *pd)
{
	int ret;

	ret = receive_memfd(pd->data_socket_fd);
	if (ret < 0) {
		pr_err("failed to receive memfd\n");
		close(pd->data_socket_fd);
		return -1;
	}
	pd->shared_addr = mmap(NULL, sizeof(uint64_t),
			       PROT_READ | PROT_WRITE, MAP_SHARED,
			       ret, 0);
	if (pd->shared_addr == MAP_FAILED) {
		pr_err("failed to map shared memory\n");
		close(pd->data_socket_fd);
		close(ret);
		return -1;
	}
	pd->shared_memfd = ret;
	return 0;
}

static int
pd_server_connect(struct pd_screen *pd)
{
	struct sockaddr_un addr = { 0 };
	char socket_path[64];
	int ret;
	char *socket_name = strlen(pd->param->output_name) > 0 ?
		pd->param->output_name : "default";

	// TODO: Obtain port from argument
	snprintf(socket_path, sizeof(socket_path) - 1,
		 PD_SOCKET_NAME, socket_name);

	/* Create local socket */
	ret = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (ret == -1) {
		pr_err("%s failed to create socket: %s\n",
			__func__, strerror(errno));
		return ret;
	}
	pd->data_socket_fd = ret;

	/* Bind socket to socket name */
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

	pr_info("connect to socket %s...\n", socket_path);
	ret = connect(pd->data_socket_fd,
			(const struct sockaddr *) &addr, sizeof(addr));
	if (ret == -1) {
		pr_err("%s failed to connect to socket %s: %s\n",
			__func__, socket_path, strerror(errno));
		return ret;
	}
	return 0;
}

static int
pd_server_init(struct pd_screen *pd)
{
	int ret;

	ret = pd_server_connect(pd);
	if (ret == -1) {
		return ret;
	}

	ret = pd_server_memfd_init(pd);
	if (ret < 0)
		return ret;

	return 0;
}

static void
pd_server_destroy(struct pd_screen *pd)
{
	if (pd->shared_addr) {
		munmap(pd->shared_addr, sizeof(uint64_t));
		pd->shared_addr = NULL;
	}
	if (pd->shared_memfd >= 0) {
		close(pd->shared_memfd);
		pd->shared_memfd = -1;
	}
	if (pd->data_socket_fd >= 0) {
		close(pd->data_socket_fd);
		pd->data_socket_fd = -1;
	}
}

static int
pd_init()
{
	struct pd_screen *pd = NULL;
	struct pd_screen **pd_array = NULL;
	int ret = 0;
	int i, j;

	if (!global_pd_info.num) {
		return 0;
	}

	pd_array = calloc(global_pd_info.num, sizeof(struct pd_screen*));
	if (!pd_array) {
		pr_err("%s OOM!\n", __func__);
		return -1;
	}
	global_pd_info.pd_screen_array = pd_array;

	for (i = 0; i < global_pd_info.num; ++i) {
		pd = calloc(1, sizeof(struct pd_screen));
		if (!pd) {
			pr_err("%s OOM\n", __func__);
			goto error;
		}

		pd->param = &global_pd_info.window_param[i];
		pd->data_socket_fd = -1;
		pthread_mutex_init(&pd->socket_mutex, NULL);

		ret = pd_server_init(pd);
		if (ret < 0)
			goto error;

		global_pd_info.pd_screen_array[i] = pd;
	}

	return 0;

error:
	for (j = 0; j <= i; ++j) {
		pd = global_pd_info.pd_screen_array[j];
		if (pd) {
			global_pd_info.pd_screen_array[j] = NULL;
			pd_server_destroy(pd);
			free(pd);
		}
	}
	free(global_pd_info.pd_screen_array);
	return -1;
}

static void
pd_deinit_thread()
{
	struct pd_screen *pd;
	int i;

	for (i = 0; i < global_pd_info.num; i++) {
		pd = global_pd_info.pd_screen_array[i];
		if (pd->thread_enable) {
			pd->thread_enable= false;
			pthread_join(pd->vblank_thread, NULL);
		}
	}
}

static void
pd_deinit()
{
	struct pd_screen *pd = NULL;

	if (!global_pd_info.num || !global_pd_info.pd_screen_array) {
		return;
	}

	for (int i = 0; i < global_pd_info.num; ++i) {
		pd = global_pd_info.pd_screen_array[i];
		if (pd) {
			global_pd_info.pd_screen_array[i] = NULL;
			send_command(pd->data_socket_fd,
					PD_COMMAND_FINISH, NULL, 0,
					NULL, 0, pd->frame_nr++);
			pd_server_destroy(pd);
			free(pd);
		}
	}

	free(global_pd_info.pd_screen_array);
}

static void
pd_init_screen(void **backend, struct screen_backend_ops **ops)
{
	*ops = &pd_vscreen_ops;

	if (global_pd_info.index >= global_pd_info.num) {
		pr_err("Invalid screen config, index (%d) > num (%d)\n",
		       global_pd_info.index, global_pd_info.num);
		return;
	}

	*backend = global_pd_info.pd_screen_array[global_pd_info.index++];
}

static int
pd_parse_cmd(char *tmp)
{
	int snum;
	char *curr;
	struct pd_param *cmd_param =
		&global_pd_info.window_param[global_pd_info.num];

	snum = sscanf(tmp, PD_BACKEND_NAME"=%dx%d+%d+%d",
			&cmd_param->guest_width, &cmd_param->guest_height,
			&cmd_param->org_x, &cmd_param->org_y);
	if ((snum != 4) ||
		(cmd_param->org_x < 0) || (cmd_param->org_y < 0) ||
		(cmd_param->guest_width <= 0) || (cmd_param->guest_height <= 0)) {

		pr_err("Invalid parameter for backend %s, parameter=%s\n",
		       PD_BACKEND_NAME, tmp);
		cmd_param->guest_width = 0;
		cmd_param->guest_height = 0;
		cmd_param->org_x = 0;
		cmd_param->org_y = 0;
		return -1;
	}

	while ((curr = strsep(&tmp, ":")) != NULL) {
		if (strncmp(curr, "sprite", 6) == 0) {
			global_pd_info.is_support_sprite = 1;
			continue;
		}

		if (strncmp(curr, "port=", strlen("port=")) == 0) {
			const int start = strlen("port=");
			const int end = strlen(curr);

			if (end - start > sizeof(cmd_param->output_name) - 1) {
				pr_err("Parameter port is too long, len=%d\n",
					end - start);
				pr_err("fallback to default\n");
			} else {
				strncpy(cmd_param->output_name, curr + start,
					end - start);
			}
			continue;
		}
	}

	++global_pd_info.num;
	return 0;
}

static void pd_mplane_fallback()
{
	global_pd_info.is_support_sprite = 0;
	pr_err("fall back to non sprite mode\n");
}

static bool pd_mplane_check()
{
	return global_pd_info.is_support_sprite == 1;
}

struct vdpy_backend plane_display_backend = {
	.name = PD_BACKEND_NAME,
	.init = pd_init,
	.deinit_thread = pd_deinit_thread,
	.deinit = pd_deinit,
	.init_screen = pd_init_screen,
	.parse_cmd = pd_parse_cmd,
	.mplane_fallback = pd_mplane_fallback,
	.mplane_check = pd_mplane_check,
};
DEFINE_BACKEND_TYPE(plane_display_backend);
