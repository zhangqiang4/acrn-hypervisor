#include <unistd.h>
#include "drm-private.h"

int ___drmHandleEvent(int fd, ___drmEventContextPtr evctx)
{
	char buffer[1024];
	int len, i;
	struct drm_event *e;
	struct drm_event_vblank *vblank;
	struct drm_event_vblank_flip *vblank_flip;
	struct drm_event_crtc_sequence *seq;
	void *user_data;

	/* The DRM read semantics guarantees that we always get only
	 * complete events. */

	len = read(fd, buffer, sizeof buffer);
	if (len == 0)
		return 0;
	if (len < (int)sizeof *e)
		return -1;

	i = 0;
	while (i < len) {
		e = (struct drm_event *)(buffer + i);
		switch (e->type) {
		case DRM_EVENT_VBLANK_FLIP:
			vblank_flip = (struct drm_event_vblank_flip *) e;
			evctx->vblank_flip_handler(fd,
						   vblank_flip->sequence,
						   vblank_flip->tv_sec,
						   vblank_flip->tv_usec,
						   vblank_flip->flip_sequence,
						   U642VOID (vblank_flip->user_data));
			break;
		case DRM_EVENT_VBLANK:
			if (evctx->version < 1 ||
			    evctx->vblank_handler == NULL)
				break;
			vblank = (struct drm_event_vblank *) e;
			evctx->vblank_handler(fd,
					      vblank->sequence,
					      vblank->tv_sec,
					      vblank->tv_usec,
					      U642VOID (vblank->user_data));
			break;
		case DRM_EVENT_FLIP_COMPLETE:
			vblank = (struct drm_event_vblank *) e;
			user_data = U642VOID (vblank->user_data);

			if (evctx->version >= 3 && evctx->page_flip_handler2)
				evctx->page_flip_handler2(fd,
							 vblank->sequence,
							 vblank->tv_sec,
							 vblank->tv_usec,
							 vblank->crtc_id,
							 user_data);
			else if (evctx->version >= 2 && evctx->page_flip_handler)
				evctx->page_flip_handler(fd,
							 vblank->sequence,
							 vblank->tv_sec,
							 vblank->tv_usec,
							 user_data);
			break;
		case DRM_EVENT_CRTC_SEQUENCE:
			seq = (struct drm_event_crtc_sequence *) e;
			if (evctx->version >= 4 && evctx->sequence_handler)
				evctx->sequence_handler(fd,
							seq->sequence,
							seq->time_ns,
							seq->user_data);
			break;
		default:
			break;
		}
		i += e->length;
	}

	return 0;
}
