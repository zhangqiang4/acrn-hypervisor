#ifndef DRM_PRIVATE_H
#define DRM_PRIVATE_H

#include <xf86drm.h>
#include <xf86drmMode.h>

#define U642VOID(x) ((void *)(unsigned long)(x))
#define VOID2U64(x) ((uint64_t)(unsigned long)(x))

#define DRM_EVENT_VBLANK_FLIP 0x0f

typedef struct ____drmEventContext {

	/* This struct is versioned so we can add more pointers if we
	 * add more events. */
	int version;

	void (*vblank_flip_handler)(int fd,
				    unsigned int sequence,
				    unsigned int tv_sec,
				    unsigned int tv_usec,
				    unsigned int flip_sequence,
				    void *user_data);

	void (*vblank_handler)(int fd,
			       unsigned int sequence,
			       unsigned int tv_sec,
			       unsigned int tv_usec,
			       void *user_data);

	void (*page_flip_handler)(int fd,
				  unsigned int sequence,
				  unsigned int tv_sec,
				  unsigned int tv_usec,
				  void *user_data);

	void (*page_flip_handler2)(int fd,
				   unsigned int sequence,
				   unsigned int tv_sec,
				   unsigned int tv_usec,
				   unsigned int crtc_id,
				   void *user_data);

	void (*sequence_handler)(int fd,
				 uint64_t sequence,
				 uint64_t ns,
				 uint64_t user_data);
} ___drmEventContext, *___drmEventContextPtr;

struct drm_event_vblank_flip {
	struct drm_event base;
	__u64 user_data;
	__u32 tv_sec;
	__u32 tv_usec;
	__u32 sequence;
	__u32 crtc_id; /* 0 on older kernels that do not support this */
	/* Will be the last sequence that flip happened */
	__u64 flip_sequence;
};

int ___drmHandleEvent(int fd, ___drmEventContextPtr evctx);

#endif
