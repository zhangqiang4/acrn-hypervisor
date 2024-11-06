#include <pthread.h>
#include <stdbool.h>
#include <sys/queue.h>
#include "work.h"
#define pr_prefix "work: "
#include "log.h"

static pthread_t worker;
static pthread_cond_t worker_cond;
static pthread_mutex_t worker_lock;
static bool worker_stop;

static LIST_HEAD(wlist, work) work_list;

static void *worker_thread_func(void *arg)
{
	pr_dbg("worker thread started!\n");
	while (1) {
		pthread_mutex_lock(&worker_lock);

		pthread_cond_wait(&worker_cond, &worker_lock);

		if (worker_stop) {
			pthread_mutex_unlock(&worker_lock);
			pr_dbg("worker thread exited!\n");
			return NULL;
		}
		LIST_HEAD(wlist, work) tmp;
		tmp.lh_first = work_list.lh_first;
		work_list.lh_first = NULL;
		pthread_mutex_unlock(&worker_lock);

		struct work *cur;
		LIST_FOREACH(cur, &tmp, list) {
			pr_dbg("handle a new work\n");
			cur->func(cur);
		}
	}
}

void worker_init(void)
{
	LIST_INIT(&work_list);
	worker_stop = false;
	pthread_mutex_init(&worker_lock, NULL);
	pthread_cond_init(&worker_cond, NULL);
	pthread_create(&worker, NULL, worker_thread_func, NULL);
	pthread_setname_np(worker, "dm-worker");
}

void worker_deinit(void)
{
	void *val;
	pthread_mutex_lock(&worker_lock);
	worker_stop = true;
	pthread_cond_broadcast(&worker_cond);
	pthread_mutex_unlock(&worker_lock);
	pthread_join(worker, &val);
	pthread_mutex_destroy(&worker_lock);
}

void work_init(struct work *work, void (*func)(struct work*))
{
	work->func = func;
}

void work_add(struct work *work)
{
	pthread_mutex_lock(&worker_lock);
	LIST_INSERT_HEAD(&work_list, work, list);
	pthread_cond_signal(&worker_cond);
	pthread_mutex_unlock(&worker_lock);
}


