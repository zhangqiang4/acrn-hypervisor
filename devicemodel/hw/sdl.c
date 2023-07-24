/*
 * Copyright (C) 2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Virtual Display sdl implementation
 *
 */

#include <SDL.h>
#include <SDL_syswm.h>
#include <egl.h>
#include <eglext.h>
#include <gl2.h>
#include <gl2ext.h>
#include <pixman.h>
#include "vdisplay.h"
#include "log.h"
#include <stdio.h>


static unsigned char default_raw_argb[VDPY_DEFAULT_WIDTH * VDPY_DEFAULT_HEIGHT * 4];

struct egl_display_ops {

	PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
	PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
	PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
};

struct sdl_cmd_param {
	int pscreen_id;
	bool is_fullscreen;
	int org_x;
	int org_y;
	int guest_width;
	int guest_height;
};

struct vscreen {
	struct display_info info;
	int pscreen_id;
	SDL_Rect pscreen_rect;
	bool is_fullscreen;
	int org_x;
	int org_y;
	int width;
	int height;
	int guest_width;
	int guest_height;
	struct surface surf;
	struct cursor cur;
	uint64_t modifier;
	SDL_Texture *surf_tex;
	SDL_Texture *cur_tex;
	SDL_Texture *bogus_tex;
	int surf_updates;
	int cur_updates;
	SDL_Window *win;
	SDL_Renderer *renderer;
	pixman_image_t *img;
	EGLImage egl_img;
	/* Record the update_time that is activated from guest_vm */
};

static struct sdl_info {
	/* add the below two fields for calling eglAPI directly */
	bool egl_dmabuf_supported;
	SDL_GLContext eglContext;
	EGLDisplay eglDisplay;
	struct egl_display_ops gl_ops;
	struct vscreen **vscrs;
	int num;
	int index;
	struct sdl_cmd_param cmd_param[VSCREEN_MAX_NUM];

} sdl = {
	.num = 0,
	.index = 0,
};

static void
sdl_gl_display_init(void)
{
	struct egl_display_ops *gl_ops = &sdl.gl_ops;
	struct vscreen *vscr;
	int i;

	/* obtain the eglDisplay/eglContext */
	sdl.eglDisplay = eglGetCurrentDisplay();
	sdl.eglContext = SDL_GL_GetCurrentContext();

	/* Try to use the eglGetProcaddress to obtain callback API for
	 * eglCreateImageKHR/eglDestroyImageKHR
	 * glEGLImageTargetTexture2DOES
	 */
	gl_ops->eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)
				eglGetProcAddress("eglCreateImageKHR");
	gl_ops->eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)
				eglGetProcAddress("eglDestroyImageKHR");
	gl_ops->glEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)
				eglGetProcAddress("glEGLImageTargetTexture2DOES");

	for (i = 0; i < sdl.num; i++) {
		vscr = sdl.vscrs[i];
		vscr->egl_img = EGL_NO_IMAGE_KHR;
	}

	if ((gl_ops->eglCreateImageKHR == NULL) ||
		(gl_ops->eglDestroyImageKHR == NULL) ||
		(gl_ops->glEGLImageTargetTexture2DOES == NULL)) {
		pr_info("DMABuf is not supported.\n");
		sdl.egl_dmabuf_supported = false;
	} else
		sdl.egl_dmabuf_supported = true;

	return;
}

static void sdl_gl_prepare_draw(struct vscreen *vscr)
{
	SDL_Rect bogus_rect;

	if (vscr == NULL)
		return;

	bogus_rect.x = 0;
	bogus_rect.y = 0;
	bogus_rect.w = 32;
	bogus_rect.h = 32;
	/* The limitation in libSDL causes that ACRN can't display framebuffer
	 * correctly on one window when using multi SDL_context to displaying
	 * the framebuffers under multi-display scenario.
	 * The small texture is added to workaround the display issue caused by
	 * libSDL limitation.
	 * Todo: Keep monitoring the libSDL to check whether the limitation is
	 * fixed.
	 */
	SDL_RenderClear(vscr->renderer);
	SDL_RenderCopy(vscr->renderer, vscr->bogus_tex, NULL, &bogus_rect);
	return;
}
static void
sdl_surface_set(void *backend, struct surface *surf)
{
	pixman_image_t *src_img;
	int format;
	int access, i;
	struct vscreen *vscr = (struct vscreen *)backend;

	if (surf == NULL ) {
		vscr->surf.width = 0;
		vscr->surf.height = 0;
		/* Need to use the default 640x480 for the SDL_Texture */
		src_img = pixman_image_create_bits(PIXMAN_a8r8g8b8,
			VDPY_MIN_WIDTH, VDPY_MIN_HEIGHT,
			(uint32_t *)default_raw_argb,
			VDPY_MIN_WIDTH * 4);
		if (src_img == NULL) {
			pr_err("failed to create pixman_image\n");
			return;
		}
		vscr->guest_width = VDPY_MIN_WIDTH;
		vscr->guest_height = VDPY_MIN_HEIGHT;
	} else if (surf->surf_type == SURFACE_PIXMAN) {
		src_img = pixman_image_create_bits(surf->surf_format,
			surf->width, surf->height, surf->pixel,
			surf->stride[0]);
		if (src_img == NULL) {
			pr_err("failed to create pixman_image\n");
			return;
		}
		vscr->surf = *surf;
		vscr->guest_width = surf->width;
		vscr->guest_height = surf->height;
	} else if (surf->surf_type == SURFACE_DMABUF) {
		src_img = NULL;
		vscr->surf = *surf;
		vscr->guest_width = surf->width;
		vscr->guest_height = surf->height;
	} else {
		/* Unsupported type */
		return;
	}

	if (vscr->surf_tex) {
		SDL_DestroyTexture(vscr->surf_tex);
	}
	if (surf && (surf->surf_type == SURFACE_DMABUF)) {
		access = SDL_TEXTUREACCESS_STATIC;
		format = SDL_PIXELFORMAT_EXTERNAL_OES;
	} else {
		access = SDL_TEXTUREACCESS_STREAMING;
		format = SDL_PIXELFORMAT_ARGB8888;
		switch (pixman_image_get_format(src_img)) {
		case PIXMAN_a8r8g8b8:
		case PIXMAN_x8r8g8b8:
			format = SDL_PIXELFORMAT_ARGB8888;
			break;
		case PIXMAN_a8b8g8r8:
		case PIXMAN_x8b8g8r8:
			format = SDL_PIXELFORMAT_ABGR8888;
			break;
		case PIXMAN_r8g8b8a8:
			format = SDL_PIXELFORMAT_RGBA8888;
		case PIXMAN_r8g8b8x8:
			format = SDL_PIXELFORMAT_RGBX8888;
			break;
		case PIXMAN_b8g8r8a8:
		case PIXMAN_b8g8r8x8:
			format = SDL_PIXELFORMAT_BGRA8888;
			break;
		default:
			pr_err("Unsupported format. %x\n",
					pixman_image_get_format(src_img));
		}
	}
	vscr->surf_tex = SDL_CreateTexture(vscr->renderer,
			format, access,
			vscr->guest_width, vscr->guest_height);

	if (vscr->surf_tex == NULL) {
		pr_err("Failed to create SDL_texture for surface.\n");
	}

	/* For the surf_switch, it will be updated in surface_update */
	if (!surf) {
		SDL_UpdateTexture(vscr->surf_tex, NULL,
				  pixman_image_get_data(src_img),
				  pixman_image_get_stride(src_img));
		sdl_gl_prepare_draw(vscr);
		SDL_RenderCopy(vscr->renderer, vscr->surf_tex, NULL, NULL);
		SDL_RenderPresent(vscr->renderer);
	} else if (surf->surf_type == SURFACE_DMABUF) {
		EGLImageKHR egl_img = EGL_NO_IMAGE_KHR;
		EGLint attrs[64];
		struct egl_display_ops *gl_ops;

		gl_ops = &sdl.gl_ops;
		i = 0;
		attrs[i++] = EGL_WIDTH;
		attrs[i++] = surf->width;
		attrs[i++] = EGL_HEIGHT;
		attrs[i++] = surf->height;
		attrs[i++] = EGL_LINUX_DRM_FOURCC_EXT;
		attrs[i++] = surf->dma_info.surf_fourcc;
		attrs[i++] = EGL_DMA_BUF_PLANE0_FD_EXT;
		attrs[i++] = surf->dma_info.dmabuf_fd;
		attrs[i++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
		attrs[i++] = surf->stride[0];
		attrs[i++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
		attrs[i++] = surf->dma_info.dmabuf_offset;

               if (vscr->modifier) {
                       attrs[i++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
                       attrs[i++] = vscr->modifier & 0xffffffff;
                       attrs[i++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
                       attrs[i++] = (vscr->modifier & 0xffffffff00000000) >> 32;
               }

		attrs[i++] = EGL_NONE;

		egl_img = gl_ops->eglCreateImageKHR(sdl.eglDisplay,
				EGL_NO_CONTEXT,
				EGL_LINUX_DMA_BUF_EXT,
				NULL, attrs);
		if (egl_img == EGL_NO_IMAGE_KHR) {
			pr_err("Failed in eglCreateImageKHR.\n");
			return;
		}

		SDL_GL_BindTexture(vscr->surf_tex, NULL, NULL);
		gl_ops->glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, egl_img);
		if (vscr->egl_img != EGL_NO_IMAGE_KHR)
			gl_ops->eglDestroyImageKHR(sdl.eglDisplay,
					vscr->egl_img);

		/* In theory the created egl_img can be released after it is bound
		 * to texture.
		 * Now it is released next time so that it is controlled correctly
		 */
		vscr->egl_img = egl_img;
	}

	if (vscr->img)
		pixman_image_unref(vscr->img);

	if (surf == NULL) {
		SDL_SetWindowTitle(vscr->win,
				"Not activate display yet!");
	} else {
		SDL_SetWindowTitle(vscr->win,
				"ACRN Virtual Monitor");
	}
	/* Replace the cur_img with the created_img */
	vscr->img = src_img;
}

static void
vdpy_cursor_position_transformation(struct vscreen *vscr, SDL_Rect *rect)
{
	rect->x = (vscr->cur.x * vscr->width) / vscr->guest_width;
	rect->y = (vscr->cur.y * vscr->height) / vscr->guest_height;
	rect->w = (vscr->cur.width * vscr->width) / vscr->guest_width;
	rect->h = (vscr->cur.height * vscr->height) / vscr->guest_height;
}


static void
sdl_surface_update(void *backend, struct surface *surf)
{
	SDL_Rect cursor_rect;
	struct vscreen *vscr = (struct vscreen *)backend;

	if (surf->surf_type == SURFACE_PIXMAN)
		SDL_UpdateTexture(vscr->surf_tex, NULL,
			  surf->pixel,
			  surf->stride[0]);

	sdl_gl_prepare_draw(vscr);
	SDL_RenderCopy(vscr->renderer, vscr->surf_tex, NULL, NULL);

	/* This should be handled after rendering the surface_texture.
	 * Otherwise it will be hidden
	 */
	if (vscr->cur_tex) {
		vdpy_cursor_position_transformation(vscr, &cursor_rect);
		SDL_RenderCopy(vscr->renderer, vscr->cur_tex,
				NULL, &cursor_rect);
	}

	SDL_RenderPresent(vscr->renderer);

	/* update the rendering time */
//	clock_gettime(CLOCK_MONOTONIC, &vscr->last_time);
}

static void
sdl_cursor_define(void *backend, struct cursor *cur)
{
	struct vscreen *vscr = (struct vscreen *)backend;

	if (cur->data == NULL)
		return;

	if (vscr->cur_tex)
		SDL_DestroyTexture(vscr->cur_tex);

	vscr->cur_tex = SDL_CreateTexture(
			vscr->renderer,
			SDL_PIXELFORMAT_ARGB8888,
			SDL_TEXTUREACCESS_STREAMING,
			cur->width, cur->height);
	if (vscr->cur_tex == NULL) {
		pr_err("Failed to create sdl_cursor surface for %p.\n", cur);
		return;
	}

	SDL_SetTextureBlendMode(vscr->cur_tex, SDL_BLENDMODE_BLEND);
	vscr->cur = *cur;
	SDL_UpdateTexture(vscr->cur_tex, NULL, cur->data, cur->width * 4);
}

static void
sdl_cursor_move(void *backend, uint32_t x, uint32_t y)
{
	struct vscreen *vscr = (struct vscreen *)backend;

	/* Only move the position of the cursor. The cursor_texture
	 * will be handled in surface_update
	 */
	vscr->cur.x = x;
	vscr->cur.y = y;
}

static int
sdl_create_vscreen_window(struct vscreen *vscr)
{
	uint32_t win_flags;

	win_flags = SDL_WINDOW_OPENGL |
		SDL_WINDOW_ALWAYS_ON_TOP |
		SDL_WINDOW_SHOWN;
	if (vscr->is_fullscreen) {
		win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
		vscr->org_x = vscr->pscreen_rect.x;
		vscr->org_y = vscr->pscreen_rect.y;
		vscr->width = vscr->pscreen_rect.w;
		vscr->height = vscr->pscreen_rect.h;
	} else {
		vscr->width = vscr->guest_width;
		vscr->height = vscr->guest_height;
	}
	vscr->win = NULL;
	vscr->renderer = NULL;
	vscr->img = NULL;
	// Zoom to width and height of pscreen is fullscreen enabled
	vscr->win = SDL_CreateWindow("ACRN_DM",
				vscr->org_x, vscr->org_y,
				vscr->width, vscr->height,
				win_flags);
	if (vscr->win == NULL) {
		pr_err("Failed to Create SDL_Window\n");
		return -1;
	}
	pr_info("SDL display bind to screen %d: [%d,%d,%d,%d].\n", vscr->pscreen_id,
			vscr->org_x, vscr->org_y, vscr->width, vscr->height);

	vscr->renderer = SDL_CreateRenderer(vscr->win, -1, 0);
	if (vscr->renderer == NULL) {
		pr_err("Failed to Create GL_Renderer \n");
		return -1;
	}
	vscr->bogus_tex = SDL_CreateTexture(vscr->renderer,
				SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC,
				32, 32);
	if (vscr->bogus_tex == NULL) {
		pr_err("%s: Failed to create SDL_Texture\n", __func__);
		return -1;
	}
	SDL_SetTextureColorMod(vscr->bogus_tex, 0x80, 0x80, 0x80);


	return 0;
}

static void sdl_release_res()
{
	struct vscreen *vscr;
	int i;

	if(!sdl.num)
		return;
	for (i = 0; i < sdl.num; i++) {
		vscr = sdl.vscrs[i];
		if (vscr->img) {
			pixman_image_unref(vscr->img);
			vscr->img = NULL;
		}
		/* Continue to thread cleanup */
		if (vscr->surf_tex) {
			SDL_DestroyTexture(vscr->surf_tex);
			vscr->surf_tex = NULL;
		}
		if (vscr->cur_tex) {
			SDL_DestroyTexture(vscr->cur_tex);
			vscr->cur_tex = NULL;
		}

		if (sdl.egl_dmabuf_supported && (vscr->egl_img != EGL_NO_IMAGE_KHR))
			sdl.gl_ops.eglDestroyImageKHR(sdl.eglDisplay,
						vscr->egl_img);
		if (vscr->bogus_tex) {
			SDL_DestroyTexture(vscr->bogus_tex);
			vscr->bogus_tex = NULL;
		}
		if (vscr->renderer) {
			SDL_DestroyRenderer(vscr->renderer);
			vscr->renderer = NULL;
		}
		if (vscr->win) {
			SDL_DestroyWindow(vscr->win);
			vscr->win = NULL;
		}

	}
	/* This is used to workaround the TLS issue of libEGL + libGLdispatch
	 * after unloading library.
	 */
	eglReleaseThread();
}

void
vdpy_calibrate_vscreen_geometry(struct vscreen *vscr)
{
	if (vscr->guest_width && vscr->guest_height) {
		/* clip the region between (640x480) and (3840x2160) */
		if (vscr->guest_width < VDPY_MIN_WIDTH)
			vscr->guest_width = VDPY_MIN_WIDTH;
		if (vscr->guest_width > VDPY_MAX_WIDTH)
			vscr->guest_width = VDPY_MAX_WIDTH;
		if (vscr->guest_height < VDPY_MIN_HEIGHT)
			vscr->guest_height = VDPY_MIN_HEIGHT;
		if (vscr->guest_height > VDPY_MAX_HEIGHT)
			vscr->guest_height = VDPY_MAX_HEIGHT;
	} else {
		/* the default window(1920x1080) is created with undefined pos
		 * when no geometry info is passed
		 */
		vscr->org_x = 0xFFFF;
		vscr->org_y = 0xFFFF;
		vscr->guest_width = VDPY_DEFAULT_WIDTH;
		vscr->guest_height = VDPY_DEFAULT_HEIGHT;
	}
}

static void set_sdl_param(struct vscreen *vscr, struct sdl_cmd_param *param)
{
	vscr->pscreen_id = param->pscreen_id;
	vscr->is_fullscreen = param->is_fullscreen;
	vscr->org_x = param->org_x;
	vscr->org_y = param->org_y;
	vscr->guest_width = param->guest_width;
	vscr->guest_height = param->guest_height;
}

int
sdl_init()
{
	SDL_SysWMinfo info;
	int num_pscreen;
	struct vscreen *vscr;
	struct vscreen **vscr_set;
	int i;

	if(!sdl.num)
		return 0;
	setenv("SDL_VIDEO_X11_FORCE_EGL", "1", 1);
	setenv("SDL_OPENGL_ES_DRIVER", "1", 1);
	setenv("SDL_RENDER_DRIVER", "opengles2", 1);
	setenv("SDL_RENDER_SCALE_QUALITY", "linear", 1);

	if (SDL_Init(SDL_INIT_VIDEO)) {
		pr_err("Failed to init SDL2 system\n");
		return -1;
	}

	num_pscreen = SDL_GetNumVideoDisplays();

	vscr_set= calloc(sdl.num, sizeof(struct vscreen *));
	if(!vscr_set) {
		SDL_Quit();
		return -1;
	}
	sdl.vscrs = vscr_set;

	for (i = 0; i < sdl.num; i++) {
		vscr= calloc(1, sizeof(struct vscreen));
		if(!vscr)
			goto error;
		sdl.vscrs[i] = vscr;

		set_sdl_param(vscr, &sdl.cmd_param[i]);
		if (vscr->pscreen_id >= num_pscreen) {
			pr_err("Monitor id %d is out of avalble range [0~%d].\n",
					vscr->pscreen_id, num_pscreen);
			goto error;
		}

		SDL_GetDisplayBounds(vscr->pscreen_id, &vscr->pscreen_rect);

		if (vscr->pscreen_rect.w < VDPY_MIN_WIDTH ||
				vscr->pscreen_rect.h < VDPY_MIN_HEIGHT) {
			pr_err("Too small resolutions. Please check the "
					" graphics system\n");
			goto error;
		}

		if (vscr->is_fullscreen) {
			vscr->guest_width = vscr->pscreen_rect.w;
			vscr->guest_height = vscr->pscreen_rect.h;
		}
	}

	SDL_SetHint(SDL_HINT_GRAB_KEYBOARD, "1");
	memset(&info, 0, sizeof(info));
	SDL_VERSION(&info.version);

	/* Set the GL_parameter for Window/Renderer */
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
			    SDL_GL_CONTEXT_PROFILE_ES);
	/* GLES2.0 is used */
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

	/* GL target surface selects A8/R8/G8/B8 */
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

	return 0;
error:
	int j;
	for(j=0; j<i; j++) {
		free(sdl.vscrs[i]);
	}
	free(sdl.vscrs);
	SDL_Quit();
	return -1;
}

static void sdl_cursor_refresh(void *backend)
{
	SDL_Rect cursor_rect;
	struct vscreen *vscr = (struct vscreen *)backend;

	/* Skip it if no surface needs to be rendered */
	if (vscr->surf_tex == NULL)
		return;

	sdl_gl_prepare_draw(vscr);
	SDL_RenderCopy(vscr->renderer, vscr->surf_tex, NULL, NULL);

	/* This should be handled after rendering the surface_texture.
	 * Otherwise it will be hidden
	 */
	if (vscr->cur_tex) {
		vdpy_cursor_position_transformation(vscr, &cursor_rect);
		SDL_RenderCopy(vscr->renderer, vscr->cur_tex,
				NULL, &cursor_rect);
	}

	SDL_RenderPresent(vscr->renderer);
}

static void sdl_display_info(void *backend, struct display_info *display)
{
	struct vscreen *vscr = (struct vscreen *)backend;
	display->xoff = vscr->info.xoff;
	display->yoff = vscr->info.yoff;
	display->width = vscr->info.width;
	display->height = vscr->info.height;
}

static void sdl_set_modifier(void *backend, int64_t modifier)
{
	struct vscreen *vscr = (struct vscreen *)backend;
	vscr->modifier = modifier;
}

static void sdl_set_scaling(void *backend, int x1, int y1, int x2, int y2)
{
	struct vscreen *vscr = (struct vscreen *)backend;

	vscr->surf.dst_x = x1;
	vscr->surf.dst_y = y1;
	vscr->surf.dst_width = x2;
	vscr->surf.dst_height = y2;
}

struct screen_backend_ops sdl_vscreen_ops = {
	.vdpy_surface_set = sdl_surface_set,
	.vdpy_surface_set_vga = sdl_surface_set,
	.vdpy_surface_update = sdl_surface_update,
	.vdpy_surface_update_vga = sdl_surface_update,
	.vdpy_cursor_refresh = sdl_cursor_refresh,
	.vdpy_display_info = sdl_display_info,
	.vdpy_cursor_move = sdl_cursor_move,
	.vdpy_cursor_define = sdl_cursor_define,
	.vdpy_set_modifier = sdl_set_modifier,
	.vdpy_set_scaling = sdl_set_scaling,
};

static void sdl_deinit()
{
	int i;
	if (!sdl.num)
		return;

	for (i = 0; i < sdl.num; i++) {
		free(sdl.vscrs[i]);
	}
	free(sdl.vscrs);
	SDL_Quit();
}

static int sdl_parse_cmd(char *tmp)
{
	int snum;
	struct sdl_cmd_param *cmd_param;
	cmd_param = &sdl.cmd_param[sdl.num];

	if (strcasestr(tmp, "geometry=fullscreen")) {
		snum = sscanf(tmp, "geometry=fullscreen:%d", &cmd_param->pscreen_id);
		if (snum != 1) {
			cmd_param->pscreen_id = 0;
			return -1;
		}
		sdl.num++;
		cmd_param->org_x = 0;
		cmd_param->org_y = 0;
		cmd_param->guest_width = VDPY_MAX_WIDTH;
		cmd_param->guest_height = VDPY_MAX_HEIGHT;
		cmd_param->is_fullscreen = true;

	} else {
		snum = sscanf(tmp, "geometry=%dx%d+%d+%d",
				&cmd_param->guest_width, &cmd_param->guest_height,
				&cmd_param->org_x, &cmd_param->org_y);
		if (snum != 4) {
			pr_err("incorrect geometry option. Should be"
						" WxH+x+y\n");
			return -1;
		}
		cmd_param->is_fullscreen = false;
		cmd_param->pscreen_id = 0;
		sdl.num++;
	}
	return 0;
}

static void sdl_init_screen(void **backend, struct screen_backend_ops **ops)
{
	*ops = &sdl_vscreen_ops;
	if (sdl.index >= sdl.num) {
		pr_err("invalid sdl screen config \r\n");
		return;
	}

	*backend = sdl.vscrs[sdl.index];
	sdl.index++;
}

static int sdl_init_thread()
{
	struct vscreen *vscr;
	int i;

	if(!sdl.num)
		return 0;

	for (i = 0; i < sdl.num; i++) {
		vscr = sdl.vscrs[i];
		vdpy_calibrate_vscreen_geometry(vscr);
		if (sdl_create_vscreen_window(vscr)) {
			pr_err("thread start error \r\n");
			return 1;
		}
		vscr->info.xoff = vscr->org_x;
		vscr->info.yoff = vscr->org_y;
		vscr->info.width = vscr->guest_width;
		vscr->info.height = vscr->guest_height;
	}
	sdl_gl_display_init();
	return 0;
}

struct vdpy_backend sdl_backend = {
	.name = "sdl",
	.init = sdl_init,
	.deinit = sdl_deinit,
	.parse_cmd = sdl_parse_cmd,
	.init_screen = sdl_init_screen,
	.init_thread = sdl_init_thread,
	.deinit_thread = sdl_release_res,
};
DEFINE_BACKEND_TYPE(sdl_backend);
