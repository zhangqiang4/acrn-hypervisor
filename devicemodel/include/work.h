#ifndef __WORK_H__
#define __WORK_H__

#include <sys/queue.h>

struct work {
	LIST_ENTRY(work) list;
	void (*func)(struct work *work);
};

void worker_init(void);
void worker_deinit(void);
void work_init(struct work *work, void (*func)(struct work*));
void work_add(struct work *work);

#endif
