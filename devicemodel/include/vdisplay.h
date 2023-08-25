/*
 * Copyright (C) 2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Vistual Display for VMs
 *
 */
#ifndef _VDISPLAY_H_
#define _VDISPLAY_H_

#include <sys/queue.h>
#include <pixman.h>
#include "dm.h"
#include "backlight.h"

#define VDPY_MAX_NUM 4

typedef void (*bh_task_func)(void *data);

/* bh task is still pending */
#define ACRN_BH_PENDING (1 << 0)
/* bh task is done */
#define ACRN_BH_DONE	(1 << 1)
/* free vdpy_display_bh after executing bh_cb */
#define ACRN_BH_FREE    (1 << 2)

#define VDPY_MAX_WIDTH 3840
#define VDPY_MAX_HEIGHT 2160
#define VDPY_DEFAULT_WIDTH 1024
#define VDPY_DEFAULT_HEIGHT 768
#define VDPY_MIN_WIDTH 640
#define VDPY_MIN_HEIGHT 480
#define VSCREEN_MAX_NUM VDPY_MAX_NUM

struct vdpy_display_bh {
	TAILQ_ENTRY(vdpy_display_bh) link;
	bh_task_func task_cb;
	void *data;
	uint32_t bh_flag;
};

struct edid_info {
	char *vendor;
	char *name;
	char *sn;
	uint32_t prefx;
	uint32_t prefy;
	uint32_t maxx;
	uint32_t maxy;
	uint32_t refresh_rate;
};

struct display_info {
	/* geometry */
	int xoff;
	int yoff;
	uint32_t width;
	uint32_t height;
};

enum surface_type {
	SURFACE_PIXMAN = 1,
	SURFACE_DMABUF,
};

struct surface {
	enum surface_type surf_type;
	/* use pixman_format as the intermediate-format */
	pixman_format_code_t surf_format;
	uint32_t fb_width;
	uint32_t fb_height;
	uint32_t x;
	uint32_t y;
	uint32_t width;
	uint32_t height;
	uint32_t dst_x;
	uint32_t dst_y;
	uint32_t dst_width;
	uint32_t dst_height;
	uint32_t bpp;
	uint32_t depth;
	uint32_t stride[4];
	uint32_t offset[4];
	uint64_t modifier;
	void *pixel;
	struct  {
		int dmabuf_fd;
		uint32_t surf_fourcc;
		uint32_t dmabuf_offset;
	} dma_info;
};

struct cursor {
	enum surface_type surf_type;
	/* use pixman_format as the intermediate-format */
	pixman_format_code_t surf_format;
	uint32_t x;
	uint32_t y;
	uint32_t hot_x;
	uint32_t hot_y;
	uint32_t width;
	uint32_t height;
	void *data;
};

struct vdpy_if {
	int scanout_num;
	int pipe_num;
	int backlight_num;
};

struct screen_backend_ops {
	void (*vdpy_surface_set)(void *backend, struct surface *surf);
	void (*vdpy_surface_update)(void *backend, struct surface *surf);
	void (*vdpy_surface_set_vga)(void *backend, struct surface *surf);
	void (*vdpy_surface_update_vga)(void *backend, struct surface *surf);
	void (*vdpy_set_modifier)(void *backend, int64_t modifier);
	void (*vdpy_set_scaling)(void *backend, int plane_id, int x1, int y1, int x2, int y2);
	void (*vdpy_cursor_refresh)(void *backend);
	void (*vdpy_display_info)(void *backend, struct display_info *display);
	void (*vdpy_enable_vblank)(void *backend);
	void (*vdpy_vblank_init)(void *backend, void (*func)
		(void *data,unsigned int frame,int i), void *data);
	void (*vdpy_cursor_move)(void *backend, uint32_t x, uint32_t y);
	void (*vdpy_cursor_define)(void *backend, struct cursor *cur);
	void (*vdpy_get_plane_info)(void *backend, uint32_t *size, uint32_t *num, uint32_t *info);
	void (*vdpy_set_rotation)(void *backend, uint32_t plane_id, uint64_t rotation);
	void (*vdpy_get_plane_rotation)(void *backend, int plane_id, uint64_t *rotation, uint32_t *count);
	void (*vdpy_update_sprite)(void *backend, int plane_id, struct surface *surf);
	void (*vdpy_sprite_flush_sync)(void *backend);

};

struct vdpy_backend {
	char *name;
	int (*init)();
	void (*deinit)();
	int (*parse_cmd)(char *tmp);
	void (*init_screen)(void **backend, struct screen_backend_ops **ops);
	int (*init_thread)();
	void (*deinit_thread)();
	void (*create_res)(int dmabuf_fd);
	void (*destroy_res)(int dmabuf_fd);
	void (*mplane_fallback)();
	bool (*mplane_check)();
};

SET_DECLARE(vdpy_backend_set, struct vdpy_backend);
#define DEFINE_BACKEND_TYPE(x)	DATA_SET(vdpy_backend_set, x)

int vdpy_parse_cmd_option(const char *opts);
int gfx_ui_init();
int vdpy_init(struct vdpy_if *vdpy_if, void(*func)(void *data, unsigned int frame,int i), void *data);
void vdpy_get_display_info(int handle, int scanout_id, struct display_info *info);
void vdpy_surface_set(int handle, int scanout_id, struct surface *surf);
void vdpy_surface_set_vga(int handle, int scanout_id, struct surface *surf);
void vdpy_surface_update(int handle, int scanout_id, struct surface *surf);
void vdpy_surface_update_vga(int handle, int scanout_id, struct surface *surf);
void vdpy_enable_vblank(int scanout);
bool vdpy_submit_bh(int handle, struct vdpy_display_bh *bh);
void vdpy_get_edid(int handle, int scanout_id, uint8_t *edid, size_t size);
void vdpy_cursor_define(int handle, int scanout_id, struct cursor *cur);
void vdpy_cursor_move(int handle, int scanout_id, uint32_t x, uint32_t y);
void vdpy_set_modifier(int handle, uint64_t modifier, int scanout_id);
void vdpy_set_scaling(int handle,int scanout_id, int plane_id, int x1, int y1, int x2, int y2);
void vdpy_set_rotation(int handle,int scanout_id, int plane_id, uint64_t rotation);
int vdpy_backlight_update_status(int handle, uint32_t backlight_id, struct backlight_properties *props);
int vdpy_get_backlight(int handle, uint32_t backlight_id, int32_t *brightness);
int vdpy_get_backlight_info(int handle, uint32_t backlight_id, struct backlight_info *info);
void vdpy_destroy_res(int dmabuf_fd);
void vdpy_create_res(int dmabuf_fd);
int vdpy_deinit(int handle);
void vdpy_get_plane_info(int handle, int scanout_id, uint32_t *size, uint32_t *num, uint32_t *info);
void vdpy_get_plane_rotation(int handle, int scanout_id, int plane_id, uint64_t *rotation, uint32_t *count);
void vdpy_update_sprite(int handle, int scanout_id, int plane_id, struct surface *surf);
void vdpy_sprite_flush_sync(int handle, int scanout_id);
bool vdpy_mplane_check();
void vdpy_mplane_fallback();
void gfx_ui_deinit();

#endif /* _VDISPLAY_H_ */
