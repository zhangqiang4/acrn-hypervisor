/*
 * Copyright (C) 2022 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <getopt.h>
#include <assert.h>
#include <fcntl.h>
#include <time.h>
#include <libgen.h>
#include <immintrin.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/timex.h>
#include <sys/timerfd.h>
#include <sys/eventfd.h>
#include <sys/syscall.h>
#include <linux/limits.h>

#define PROFILING_ENABLED
#define STATISTICS_ENABLED

static int ivsh_tsync_debug = 0;
#define DPRINTF(params) do { if (ivsh_tsync_debug) printf params; } while (0)

#define _TSYNC_MAGIC	(0xcafecafe)
#define TSYNC_MAGIC	(((uint64_t)_TSYNC_MAGIC << 32) | (~_TSYNC_MAGIC))

struct uio_irq_data
{
	int fd;
	int vector;
};

#define UIO_IRQ_DATA _IOW('u', 100, struct uio_irq_data)

#define IVSH_MAX_IRQ_NUM	8
#define IVSH_MAX_PEER_NUM	8
#define IVSH_BAR0_SIZE		256
#define IVSH_REG_IVPOSITION	0x08
#define IVSH_REG_DOORBELL	0x0C

#define IVSH_NSEC_PER_SEC	1000000000L
#define IVSH_GETTIME_THRESHOLD	1000L
#define IVSH_GETTIME_RETRY_N	2

struct ivsh_dev_context
{
	bool is_master;
	bool opened;
	long uio_nr;

	int bar0_fd;
	uint32_t *p_reg;

	int bar2_fd;
	void *p_shmem;
	long shmem_size;

	int uio_dev_fd;
	int irq_event_fd;
};

struct ivsh_tsync_ctx
{
	struct ivsh_dev_context	dev_ctx;
	long			tsc_khz;
	long			interval_ms;
	long			threshold_cycles;
	bool			should_stop;
};

struct ivsh_tsync_time_info {
	uint64_t magic;
	uint64_t tsc_sequence;
	uint64_t tsc_timestamp;
	int64_t tv_sec;
	int64_t tv_nsec;
	uint64_t tsc_scale;
	uint64_t tsc_offset;
#ifdef STATISTICS_ENABLED
	uint64_t master_drop_n;
	uint64_t master_max_get_cycles;
	uint64_t slave_drop_n;
	uint64_t slave_max_get_cycles;
#endif
} __attribute__((__packed__));

static char *progname;
static int dest_vm = 1;

static inline uint64_t ivsh_rdtscp(void)
{
    uint32_t rax, rdx, aux;
    asm volatile ( "rdtscp" : "=a" (rax), "=d" (rdx), "=c" (aux) : : );
    return ((uint64_t)rdx << 32U) | rax;
}

static inline uint64_t u64_shl64_div_u64(uint64_t a, uint64_t divisor)
{
	uint64_t ret, tmp;

	asm volatile ("divq %2" :
		"=a" (ret), "=d" (tmp) :
		"rm" (divisor), "0" (0U), "1" (a));

	return ret;
}

static inline uint64_t u64_mul_u64_shr64(uint64_t a, uint64_t b)
{
	uint64_t ret, disc;

	asm volatile ("mulq %3" :
		"=d" (ret), "=a" (disc) :
		"a" (a), "r" (b));

	return ret;
}

static inline bool ivsh_check_magic(struct ivsh_dev_context *p_ivsh_dev_ctx)
{
	struct ivsh_tsync_time_info *p_sync_info;

	p_sync_info = (struct ivsh_tsync_time_info *)(p_ivsh_dev_ctx->p_shmem);
	return (p_sync_info->magic == TSYNC_MAGIC);
}

static void ivsh_setup_tsync_info(struct ivsh_tsync_ctx *p_tsync_ctx)
{
	struct ivsh_tsync_time_info *p_sync_info;
	uint64_t tsc_khz;
	uint64_t tsc_scale;

	p_sync_info = (struct ivsh_tsync_time_info *)(p_tsync_ctx->dev_ctx.p_shmem);
	tsc_khz = p_tsync_ctx->tsc_khz;

	/*
	 * The slave will calculate the delta time in 100ns from delta tsc
	 * by the following formula:
	 *
	 * delta_ns = (delta_tsc * tsc_scale) >> 64
	 *
	 * delta_ns =
	 *     delta_tsc / (tsc_khz * 1000) * 1000000000
	 *
	 * tsc_scale = (1000000U << 64U) / tsc_khz
	 */
	tsc_scale = u64_shl64_div_u64(1000000U, tsc_khz);

	memset(p_sync_info, 0, sizeof(*p_sync_info));

	p_sync_info->magic = TSYNC_MAGIC;
	p_sync_info->tsc_scale = tsc_scale;
}

static long ivsh_get_shmem_size(long uio_nr)
{
	char config_node[PATH_MAX] = {0};
	uint64_t shm_size;
	uint64_t tmp;
	int cfg_fd;
	ssize_t size;

	snprintf(config_node, sizeof(config_node),
		"/sys/class/uio/uio%ld/device/config", uio_nr);

	cfg_fd = open(config_node, O_RDWR);
	if (cfg_fd < 0) {
		printf("%s, open failed %s, error %s\n", __func__,
			config_node, strerror(errno));
		return -1;
	}

	size = pread(cfg_fd, &tmp, 8, 0x18);
	if (size != 8) {
		shm_size = 0;
		goto fail;
	}
	shm_size= ~0U;
	size = pwrite(cfg_fd ,&shm_size, 8, 0x18);
	if (size != 8) {
		shm_size = 0;
		goto fail;
	}
	size = pread(cfg_fd, &shm_size, 8, 0x18);
	if (size != 8) {
		shm_size = 0;
		goto fail;
	}
	size = pwrite(cfg_fd ,&tmp, 8, 0x18);
	if (size != 8) {
		shm_size = 0;
		goto fail;
	}
	shm_size &= (~0xfUL);
	shm_size = (shm_size & ~(shm_size - 1));

fail:
	close(cfg_fd);

	return (long)shm_size;
}

static inline void ivsh_ring_doorbell(struct ivsh_dev_context *p_ivsh_dev_ctx,
	uint16_t peer_id, uint16_t vector_id)
{
	//printf("0x%lx\t%s: peer_id = %d, vector_id = %d\n", ivsh_rdtscp(), __func__, peer_id, vector_id);
	p_ivsh_dev_ctx->p_reg[IVSH_REG_DOORBELL >> 2] = (peer_id << 16) | vector_id;
}

static int ivsh_init_dev(struct ivsh_dev_context *p_ivsh_dev_ctx,
	long uio_nr, bool is_master)
{
	if (p_ivsh_dev_ctx == NULL)
		return -1;

	memset(p_ivsh_dev_ctx, 0, sizeof(*p_ivsh_dev_ctx));

	p_ivsh_dev_ctx->is_master = is_master;
	p_ivsh_dev_ctx->uio_nr = uio_nr;
	p_ivsh_dev_ctx->bar0_fd = -1;
	p_ivsh_dev_ctx->bar2_fd = -1;
	p_ivsh_dev_ctx->uio_dev_fd = -1;
	p_ivsh_dev_ctx->irq_event_fd = -1;
	p_ivsh_dev_ctx->opened = false;

	return 0;
}

static void ivsh_close_dev(struct ivsh_dev_context *p_ivsh_dev_ctx)
{
	if (p_ivsh_dev_ctx == NULL)
		return;

	if (p_ivsh_dev_ctx->p_reg) {
		munmap(p_ivsh_dev_ctx->p_reg, IVSH_BAR0_SIZE);
		p_ivsh_dev_ctx->p_reg = NULL;
	}
	if (p_ivsh_dev_ctx->bar0_fd > 0){
		close(p_ivsh_dev_ctx->bar0_fd);
		p_ivsh_dev_ctx->bar0_fd = -1;
	}

	if (p_ivsh_dev_ctx->p_shmem) {
		munmap(p_ivsh_dev_ctx->p_shmem, p_ivsh_dev_ctx->shmem_size);
		p_ivsh_dev_ctx->p_shmem = NULL;
	}
	if (p_ivsh_dev_ctx->bar2_fd > 0){
		close(p_ivsh_dev_ctx->bar2_fd);
		p_ivsh_dev_ctx->bar2_fd = -1;
	}
	p_ivsh_dev_ctx->shmem_size = 0;

	if (p_ivsh_dev_ctx->irq_event_fd > 0) {
		close(p_ivsh_dev_ctx->irq_event_fd);
		p_ivsh_dev_ctx->irq_event_fd = -1;
	}

	if (p_ivsh_dev_ctx->uio_dev_fd > 0){
		close(p_ivsh_dev_ctx->uio_dev_fd);
		p_ivsh_dev_ctx->uio_dev_fd = -1;
	}

	p_ivsh_dev_ctx->opened = false;
}

static int ivsh_open_dev(struct ivsh_dev_context *p_ivsh_dev_ctx)
{
	char node_path[PATH_MAX] = {0};
	struct uio_irq_data irq_data;
	int evt_fd = -1;
	int ret = 0;

	if (p_ivsh_dev_ctx == NULL || p_ivsh_dev_ctx->uio_nr < 0)
		return -1;

	/* cannot open twice */
	if (p_ivsh_dev_ctx->opened)
		return -1;

	/* get shared memory size from config space */
	p_ivsh_dev_ctx->shmem_size = ivsh_get_shmem_size(p_ivsh_dev_ctx->uio_nr);
	if (p_ivsh_dev_ctx->shmem_size <= 0) {
		printf("ivsh_get_shmem_size failed\n");
		ret = -1;
		goto end;
	}
	if (p_ivsh_dev_ctx->shmem_size < sizeof(struct ivsh_tsync_time_info)) {
		printf("shmem_size 0x%lx is too small, need 0x%lx\n",
			p_ivsh_dev_ctx->shmem_size, sizeof(struct ivsh_tsync_time_info));
		ret = -1;
		goto end;
	}

	/* mmap reg mmio space from BAR0 */
	snprintf(node_path, sizeof(node_path),
		"/sys/class/uio/uio%ld/device/resource0", p_ivsh_dev_ctx->uio_nr);
	p_ivsh_dev_ctx->bar0_fd = open(node_path, O_RDWR);
	if (p_ivsh_dev_ctx->bar0_fd < 0) {
		printf("Open failed %s, error %s\n", node_path,
			strerror(errno));
		ret = -1;
		goto end;
	}

	p_ivsh_dev_ctx->p_reg = (uint32_t *)mmap(NULL, IVSH_BAR0_SIZE,
		PROT_READ|PROT_WRITE, MAP_SHARED,
		p_ivsh_dev_ctx->bar0_fd, 0);
	if (p_ivsh_dev_ctx->p_reg == MAP_FAILED) {
		printf("bar0_fd mmap failed\n");
		ret = -1;
		goto end;
	}

	/* mmap shared memory from BAR2, cache type doesn't matter on ACRN */
	snprintf(node_path, sizeof(node_path),
		"/sys/class/uio/uio%ld/device/resource2_wc", p_ivsh_dev_ctx->uio_nr);
	p_ivsh_dev_ctx->bar2_fd = open(node_path, O_RDWR);
	if (p_ivsh_dev_ctx->bar2_fd < 0) {
		printf("Open failed %s, error %s\n", node_path,
			strerror(errno));
		ret = -1;
		goto end;
	}

	p_ivsh_dev_ctx->p_shmem = mmap(NULL, p_ivsh_dev_ctx->shmem_size,
		PROT_READ|PROT_WRITE, MAP_SHARED,
		p_ivsh_dev_ctx->bar2_fd, 0);
	if (p_ivsh_dev_ctx->p_shmem == MAP_FAILED) {
		printf("bar2_fd mmap failed\n");
		ret = -1;
		goto end;
	}

	/* open /dev/uio%ld */
	snprintf(node_path, sizeof(node_path),
		"/dev/uio%ld", p_ivsh_dev_ctx->uio_nr);
	p_ivsh_dev_ctx->uio_dev_fd = open(node_path, O_RDWR);
	if (p_ivsh_dev_ctx->uio_dev_fd < 0) {
		printf("Open failed %s, error %s\n", node_path,
			strerror(errno));
		ret = -1;
		goto end;
	}

	if (!p_ivsh_dev_ctx->is_master) {
		/* create a eventfd for msix, we are using the first msix vector */
		evt_fd = eventfd(0, 0);
		if (evt_fd < 0) {
			printf("failed to create evt_fd\n");
			ret = -1;
			goto end;
		}
		p_ivsh_dev_ctx->irq_event_fd = evt_fd;

		/*
		 * set eventfds of msix to kernel driver by ioctl
		 * we are using the first msix vector
		 */
		irq_data.vector = 0;
		irq_data.fd = evt_fd;
		if (ioctl(p_ivsh_dev_ctx->uio_dev_fd, UIO_IRQ_DATA, &irq_data) < 0) {
			printf("ioctl(UIO_IRQ_DATA) failed\n");
			ret = -1;
			goto end;
		}
	}

	p_ivsh_dev_ctx->opened = true;

end:
	if (ret == -1)
		ivsh_close_dev(p_ivsh_dev_ctx);

	return ret;
}

static int ivsh_master_handler(struct ivsh_tsync_ctx *p_tsync_ctx)
{
	struct ivsh_tsync_time_info *p_sync_info;
	struct timespec ts_now;
	uint64_t tsc_start, tsc_now;
	int retry_n = IVSH_GETTIME_RETRY_N;
	int ret;

	p_sync_info = (struct ivsh_tsync_time_info *)(p_tsync_ctx->dev_ctx.p_shmem);

	while (retry_n > 0) {
		tsc_start = ivsh_rdtscp();
		ret = clock_gettime(CLOCK_REALTIME, &ts_now);
		tsc_now = ivsh_rdtscp();
		if (ret == -1) {
			printf("%s: clock_gettime failed, ret = %d, errno = %d\n",
				__func__, ret, errno);
			return -1;
		}
		if (tsc_now - tsc_start < p_tsync_ctx->threshold_cycles)
			break;
		retry_n--;
	}
	if (retry_n == 0) {
#ifdef STATISTICS_ENABLED
		p_sync_info->master_drop_n++;
		if (tsc_now - tsc_start > p_sync_info->master_max_get_cycles)
			p_sync_info->master_max_get_cycles = tsc_now - tsc_start;
#endif
		DPRINTF(("0x%lx\t%s: [seq: %08ld] clock_gettime takes too long: %ld, DROP!\n",
			ivsh_rdtscp(), __func__, p_sync_info->tsc_sequence,
			tsc_now - tsc_start));
		return 0 ;
	}

	if (p_sync_info->tsc_sequence & 1)
		p_sync_info->tsc_sequence++;
	p_sync_info->tsc_sequence++;

	__atomic_thread_fence(__ATOMIC_SEQ_CST);

	p_sync_info->tsc_timestamp = tsc_now;
	p_sync_info->tv_sec = ts_now.tv_sec;
	p_sync_info->tv_nsec = ts_now.tv_nsec;

	__atomic_thread_fence(__ATOMIC_SEQ_CST);

	p_sync_info->tsc_sequence++;

	__atomic_thread_fence(__ATOMIC_SEQ_CST);

	ivsh_ring_doorbell(&p_tsync_ctx->dev_ctx, dest_vm, 0);

	DPRINTF(("0x%lx\t%s: [seq: %08ld] tsc_master = 0x%lx@(0x%lx, 0x%lx)\n",
		ivsh_rdtscp(), __func__, p_sync_info->tsc_sequence,
		tsc_now, ts_now.tv_sec, ts_now.tv_nsec));

	return 0;
}

static void ivsh_master_loop(struct ivsh_tsync_ctx *p_tsync_ctx)
{
	struct itimerspec ts = {0};
	int tfd = -1, ret;
	uint64_t expired;
	ssize_t s;

	ivsh_setup_tsync_info(p_tsync_ctx);
	tfd = timerfd_create(CLOCK_MONOTONIC, 0);
	if (tfd < 0) {
		printf("%s: timerfd_create failed, errno = %d\n",
			__func__, errno);
		return;
	}

	ts.it_value.tv_sec = p_tsync_ctx->interval_ms / 1000;
	ts.it_value.tv_nsec = (p_tsync_ctx->interval_ms % 1000) * 1000000;
	ts.it_interval.tv_sec = p_tsync_ctx->interval_ms / 1000;
	ts.it_interval.tv_nsec = (p_tsync_ctx->interval_ms % 1000) * 1000000;
	ret = timerfd_settime(tfd, 0, &ts, NULL);
	if (ret < 0) {
		printf("%s: timerfd_settime failed, ret = %d, errno = %d\n",
			__func__, ret, errno);
		return;
	}

	while (!p_tsync_ctx->should_stop) {
		s = read(tfd, &expired, sizeof(expired));
		if (s != sizeof(uint64_t)) {
			printf("%s: read failed, s = %ld\n", __func__, s);
			continue;
		}
		if (expired > 0) {
			ivsh_master_handler(p_tsync_ctx);
		}
	}

	memset(&ts, 0, sizeof(ts));
	timerfd_settime(tfd, 0, &ts, NULL);
	close(tfd);
}

static int ivsh_slave_handler(struct ivsh_tsync_ctx *p_tsync_ctx)
{
	struct ivsh_tsync_time_info *p_sync_info;
	struct timespec ts_get;
	struct timex tx;
#ifdef PROFILING_ENABLED
	uint64_t tsc_compute_end, tsc_adj_start, tsc_adj_end;
#endif
#ifdef STATISTICS_ENABLED
	static uint64_t abs_delta_ns_max = 0UL;
	static bool max_is_negative = false;
	static uint64_t abs_delta_ns_min = ~0UL;
	static bool min_is_negative = false;
	static uint64_t abs_delta_ns_sum = 0UL;
	int64_t this_delta_ns = 0;
	bool this_delta_ns_is_negative = false;

	static uint64_t abs_step_ns_max = 0UL;
	static bool max_step_is_negative = false;
	static uint64_t abs_step_ns_min = ~0UL;
	static bool min_step_is_negative = false;
	static uint64_t abs_step_ns_sum = 0UL;

	static uint64_t sample_count = 0UL;
#endif
	uint64_t tsc_sequence_start, tsc_sequence_end;
	uint64_t tsc_master, tsc_scale, tsc_now, tsc_get_start;
	int64_t tv_sec_master, tv_nsec_master, delta_ns, step_ns;
	int retry_n = IVSH_GETTIME_RETRY_N;
	int sign = 1;
	int ret;

	if (!ivsh_check_magic(&p_tsync_ctx->dev_ctx)) {
		printf("%s: ivsh_check_magic failed\n", __func__);
		return -1;
	}

	p_sync_info = (struct ivsh_tsync_time_info *)(p_tsync_ctx->dev_ctx.p_shmem);
	do {
		tsc_sequence_start = p_sync_info->tsc_sequence;
		__atomic_thread_fence(__ATOMIC_SEQ_CST);
		tsc_master = p_sync_info->tsc_timestamp;
		tv_sec_master = p_sync_info->tv_sec;
		tv_nsec_master = p_sync_info->tv_nsec;
		tsc_scale = p_sync_info->tsc_scale;
		__atomic_thread_fence(__ATOMIC_SEQ_CST);
		tsc_sequence_end = p_sync_info->tsc_sequence;
	} while (tsc_sequence_start != tsc_sequence_end);

	while (retry_n > 0) {
		tsc_get_start = ivsh_rdtscp();
		ret = clock_gettime(CLOCK_REALTIME, &ts_get);
		tsc_now = ivsh_rdtscp();
		if (ret == -1) {
			printf("%s: clock_gettime failed, ret = %d, errno = %d\n",
				__func__, ret, errno);
			return -1;
		}
		if (tsc_now - tsc_get_start < p_tsync_ctx->threshold_cycles)
			break;
		retry_n--;
	}
	if (retry_n == 0) {
#ifdef STATISTICS_ENABLED
		p_sync_info->slave_drop_n++;
		if (tsc_now - tsc_get_start > p_sync_info->slave_max_get_cycles)
			p_sync_info->slave_max_get_cycles = tsc_now - tsc_get_start;
#endif
		DPRINTF(("0x%lx\t%s: [seq: %08ld] clock_gettime takes too long: %ld, DROP!\n",
			ivsh_rdtscp(), __func__, p_sync_info->tsc_sequence,
			tsc_now - tsc_get_start));
		return 0;
	}

	/* enter computing critical area */
	delta_ns = u64_mul_u64_shr64(tsc_now - tsc_master, tsc_scale);

	/*
	 * step_ns = tv_sec_master * IVSH_NSEC_PER_SEC + tv_nsec_master + delta_ns
	 *           - ts_self.tv_sec * IVSH_NSEC_PER_SEC
	 *           - ts_self.tv_nsec
	 */
	step_ns = (tv_sec_master - ts_get.tv_sec) * IVSH_NSEC_PER_SEC +
		tv_nsec_master + delta_ns - ts_get.tv_nsec;
	if (step_ns < 0) {
		sign = -1;
		step_ns = -step_ns;
	}
	memset(&tx, 0, sizeof(tx));
	tx.modes = ADJ_SETOFFSET | ADJ_NANO;
	tx.time.tv_sec  = sign * (step_ns / IVSH_NSEC_PER_SEC);
	tx.time.tv_usec = sign * (step_ns % IVSH_NSEC_PER_SEC);
	if (tx.time.tv_usec < 0) {
		tx.time.tv_sec -= 1;
		tx.time.tv_usec += IVSH_NSEC_PER_SEC;
	}
#ifdef PROFILING_ENABLED
	tsc_compute_end = tsc_adj_start = ivsh_rdtscp();
#endif

#if 1
	ret = clock_adjtime(CLOCK_REALTIME, &tx);
	if (ret == -1) {
		printf("%s: clock_adjtime failed, ret = %d, errno = %d\n",
			__func__, ret, errno);
		return -1;
	}
#endif
	/* leave computing critical area */

#ifdef PROFILING_ENABLED
	tsc_adj_end = ivsh_rdtscp();
#endif

	DPRINTF(("0x%lx\t%s: [seq: %08ld] tsc_master = 0x%lx@(0x%lx, 0x%lx), tsc_scale = 0x%lx\n",
		ivsh_rdtscp(), __func__, tsc_sequence_start,
		tsc_master, tv_sec_master, tv_nsec_master,
		tsc_scale));
#ifdef PROFILING_ENABLED
	DPRINTF(("0x%lx\t%s: [seq: %08ld] tsc_get = 0x%lx@(0x%lx, 0x%lx), (delta_tsc = 0x%lx, delta_ns = %ld)\n",
		ivsh_rdtscp(), __func__, tsc_sequence_start,
		tsc_get_start, ts_get.tv_sec, ts_get.tv_nsec,
		tsc_get_start - tsc_master, u64_mul_u64_shr64(tsc_get_start - tsc_master, tsc_scale)));
	DPRINTF(("0x%lx\t%s: [seq: %08ld] this_elapsed_ns = %ld, this_delta_ns = %ld\t\t\t\t<= this accuracy\n",
		ivsh_rdtscp(), __func__, tsc_sequence_start,
		(ts_get.tv_sec - tv_sec_master) * IVSH_NSEC_PER_SEC + (ts_get.tv_nsec - tv_nsec_master),
		(ts_get.tv_sec - tv_sec_master) * IVSH_NSEC_PER_SEC + (ts_get.tv_nsec - tv_nsec_master) -
			u64_mul_u64_shr64(tsc_get_start - tsc_master, tsc_scale)));
#endif
	DPRINTF(("0x%lx\t%s: [seq: %08ld] tsc_slave = 0x%lx@(delta_tsc = 0x%lx, delta_ns = %ld)\n",
		ivsh_rdtscp(), __func__, tsc_sequence_start,
		tsc_now, tsc_now - tsc_master, delta_ns));
	DPRINTF(("0x%lx\t%s: [seq: %08ld] step_ns = %ld, tx(0x%lx, 0x%lx)\t\t\t\t\t<= this jitter\n",
		ivsh_rdtscp(), __func__, tsc_sequence_start,
		sign * step_ns, tx.time.tv_sec, tx.time.tv_usec));

#ifdef PROFILING_ENABLED
	DPRINTF(("0x%lx\t%s: [seq: %08ld] compute = (0x%lx, %ldns), get = (0x%lx, %ldns), adj = (0x%lx, %ldns)\n",
		ivsh_rdtscp(), __func__, tsc_sequence_start,
		tsc_compute_end - tsc_now, u64_mul_u64_shr64(tsc_compute_end - tsc_now, tsc_scale),
		tsc_now - tsc_get_start, u64_mul_u64_shr64(tsc_now - tsc_get_start, tsc_scale),
		tsc_adj_end - tsc_adj_start, u64_mul_u64_shr64(tsc_adj_end - tsc_adj_start, tsc_scale)));
#endif

#ifdef STATISTICS_ENABLED
	sample_count++;
	/* ignore the first 10 samples */
	if (sample_count > 10) {
		/* jitter */
		if (step_ns > abs_step_ns_max) {
			abs_step_ns_max = step_ns;
			max_step_is_negative = (sign == -1);
		}
		if (step_ns < abs_step_ns_min) {
			abs_step_ns_min = step_ns;
			min_step_is_negative = (sign == -1);
		}
		abs_step_ns_sum += step_ns;

		DPRINTF(("0x%lx\t%s: [seq: %08ld] max_step_ns = %ld, min_step_ns = %ld, |avg_step_ns| = %ld @[%ld]\t\t\t<= jitter\n",
			ivsh_rdtscp(), __func__, tsc_sequence_start,
			(max_step_is_negative ? (-1 * abs_step_ns_max) : abs_step_ns_max),
			(min_step_is_negative ? (-1 * abs_step_ns_min) : abs_step_ns_min),
			abs_step_ns_sum / (sample_count - 10),
			sample_count - 10));

		/* accuracy */
		this_delta_ns = (ts_get.tv_sec - tv_sec_master) * IVSH_NSEC_PER_SEC +
			(ts_get.tv_nsec - tv_nsec_master) -
			u64_mul_u64_shr64(tsc_get_start - tsc_master, tsc_scale);
		if (this_delta_ns < 0) {
			this_delta_ns = -this_delta_ns;
			this_delta_ns_is_negative = true;
		}
		if (this_delta_ns > abs_delta_ns_max) {
			abs_delta_ns_max = this_delta_ns;
			max_is_negative = this_delta_ns_is_negative;
		}
		if (this_delta_ns < abs_delta_ns_min) {
			abs_delta_ns_min = this_delta_ns;
			min_is_negative = this_delta_ns_is_negative;
		}
		abs_delta_ns_sum += this_delta_ns;

		DPRINTF(("0x%lx\t%s: [seq: %08ld] max_delta_ns = %ld, min_delta_ns = %ld, |avg_delta_ns| = %ld @[%ld]\t\t<= accuracy\n",
			ivsh_rdtscp(), __func__, tsc_sequence_start,
			(max_is_negative ? (-1 * abs_delta_ns_max) : abs_delta_ns_max),
			(min_is_negative ? (-1 * abs_delta_ns_min) : abs_delta_ns_min),
			abs_delta_ns_sum / (sample_count - 10),
			sample_count - 10));

		DPRINTF(("0x%lx\t%s: [seq: %08ld] master(drop_n = %ld, max_get_cycles = %ld), slave(drop_n = %ld, max_get_cycles = %ld)\n",
			ivsh_rdtscp(), __func__, tsc_sequence_start,
			p_sync_info->master_drop_n, p_sync_info->master_max_get_cycles,
			p_sync_info->slave_drop_n, p_sync_info->slave_max_get_cycles));
	}
#endif

	DPRINTF(("\n"));

	return 0;
}

static void ivsh_slave_loop(struct ivsh_tsync_ctx *p_tsync_ctx)
{
	struct ivsh_dev_context *p_dev_ctx = &p_tsync_ctx->dev_ctx;
	int irq_event_fd = p_dev_ctx->irq_event_fd;
	uint64_t cnt;
	ssize_t ret;

	if (irq_event_fd < 0)
		return;

	while (!p_tsync_ctx->should_stop) {
		ret = read(irq_event_fd, &cnt, sizeof(cnt));
		if (ret != sizeof(uint64_t)) {
			printf("%s: read failed, ret = %ld\n", __func__, ret);
			continue;
		}
		if (cnt > 0) {
			ivsh_slave_handler(p_tsync_ctx);
		}
	}
}

static void usage(void)
{
	printf("Usage: %s [-m|-s] [options]\n", progname);
	printf("       %s [-h]\n", progname);
	printf("----------------------------------------------------\n");

	printf("Master or Slave:\n");
	printf("  -h\tshow this message and quit\n");
	printf("  -u\tuio number, default 0\n");
	printf("  -t\tthreshold of clock_gettime cycles, default %ld\n", IVSH_GETTIME_THRESHOLD);
	printf("  -d\tenable debug and statistics message\n");
	printf("----------------------------------------------------\n");

	printf("Master specific:\n");
	printf("  -f\ttsc frequency in kHZ\n");
	printf("  -i\tinterval to send time sync to slave in ms\n");
	printf("  -n\tdest vm number\n");
	printf("----------------------------------------------------\n");

	printf("Example:\n");
	printf("  %s -m -u 0 -i 100 -f 2100000 -t 800\n", progname);
	printf("  %s -s -u 0 -t 800\n", progname);
	printf("----------------------------------------------------\n");
}

static struct option const long_options[] =
{
	{"uiodev", required_argument, 0, 'u'},
	{"threshold", required_argument, 0, 't'},
	{"freq", required_argument, 0, 'f'},
	{"interval", required_argument, 0, 'i'},
	{"dest_vm", required_argument, 0, 'n'},
	{"master", no_argument, 0, 'm'},
	{"slave", no_argument, 0, 's'},
	{"debug", no_argument, 0, 'd'},
	{"help", no_argument, 0, 'h'},
	{NULL, 0, NULL, 0}
};

static char optstr[] = "u:t:f:i:n:msdh";

int main(int argc, char *argv[])
{
	struct ivsh_tsync_ctx tsync_ctx;
	struct ivsh_dev_context *p_dev_ctx = &tsync_ctx.dev_ctx;

	int c, option_idx = 0;
	char *stopstr;

	long uio_nr = 0;
	long tsc_khz = 0;
	long interval_ms = 100;
	long threshold_cycles = IVSH_GETTIME_THRESHOLD;
	int master_flag = 0, slave_flag = 0;
	bool is_master = false;
	int ret = 0;

	progname = basename(argv[0]);
	while ((c = getopt_long(argc, argv, optstr, long_options, &option_idx)) != -1) {
		switch (c) {
		case 'n':
			dest_vm = strtol(optarg, &stopstr, 10);
			if (errno != 0) {
				printf("-n: vm_no is not a number\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'u':
			uio_nr = strtol(optarg, &stopstr, 10);
			if (errno != 0) {
				printf("-u: uio_no is not a number\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 't':
			threshold_cycles = strtol(optarg, &stopstr, 10);
			if (errno != 0) {
				printf("-u: threshold_cycles is not a number\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'f':
			tsc_khz = strtol(optarg, &stopstr, 10);
			if (errno != 0) {
				printf("-f: tsc freq is not a number\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'i':
			interval_ms = strtol(optarg, &stopstr, 10);
			if (errno != 0) {
				printf("-i: tsc interval_ms is not a number\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'm':
			if (slave_flag) {
				printf("-m: cannot be both master and slave\n");
				exit(EXIT_FAILURE);
			}
			master_flag++;
			break;
		case 's':
			if (master_flag) {
				printf("-s: cannot be both master and slave\n");
				exit(EXIT_FAILURE);
			}
			slave_flag++;
			break;
		case 'd':
			ivsh_tsync_debug = 1;
			break;
		case 'h':
			usage();
			exit(EXIT_SUCCESS);
		default:
			usage();
			exit(EXIT_FAILURE);
		}
	}

	if (!master_flag && !slave_flag) {
		printf("must be either master or slave\n");
		usage();
		exit(EXIT_FAILURE);
	}

	if (master_flag)
		is_master = true;
	else
		is_master = false;

	if (!interval_ms) {
		interval_ms = 100;
	}

	if (is_master && !tsc_khz) {
		printf("tsc_khz must be specified for master\n");
		usage();
		exit(EXIT_FAILURE);
	}

	/* tsync_ctx */
	memset(&tsync_ctx, 0, sizeof(tsync_ctx));
	tsync_ctx.tsc_khz = tsc_khz;
	tsync_ctx.interval_ms = interval_ms;
	tsync_ctx.threshold_cycles = threshold_cycles;

	/* dev_ctx */
	ret = ivsh_init_dev(p_dev_ctx, uio_nr, is_master);
	if (ret < 0) {
		printf("ivsh_init_dev failed\n");
		return -1;
	}

	ret = ivsh_open_dev(p_dev_ctx);
	if (ret < 0) {
		printf("ivsh_open_dev failed\n");
		return -1;
	}

	if (is_master) {
		printf("Running in Master mode:\n");
		printf("uio_nr\t\t\t= %ld\n", tsync_ctx.dev_ctx.uio_nr);
		printf("threshold_cycles\t= %ld\n", tsync_ctx.threshold_cycles);
		printf("shmem\t\t\t= %p @ [0x%lx]\n", p_dev_ctx->p_shmem, p_dev_ctx->shmem_size);
		printf("tsc_khz\t\t\t= %ld\n", tsync_ctx.tsc_khz);
		printf("interval_ms\t\t= %ld\n", tsync_ctx.interval_ms);
		ivsh_master_loop(&tsync_ctx);
	} else {
		printf("Running in Slave mode:\n");
		printf("uio_nr\t\t\t= %ld\n", tsync_ctx.dev_ctx.uio_nr);
		printf("threshold_cycles\t= %ld\n", tsync_ctx.threshold_cycles);
		printf("shmem\t\t\t= %p @ [0x%lx]\n", p_dev_ctx->p_shmem, p_dev_ctx->shmem_size);
		ivsh_slave_loop(&tsync_ctx);
	}

	ivsh_close_dev(p_dev_ctx);

	return 0;
}
