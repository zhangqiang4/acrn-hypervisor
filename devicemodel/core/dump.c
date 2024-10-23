/*
 * Copyright (C) 2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <sys/stat.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <blkid/blkid.h>
#include <time.h>
#include "log.h"
#include "dm.h"
#include "acrn_mngr.h"
#include "sw_load.h"
#include "vmmapi.h"
#include "crashdump.h"

static void *crash_dump_buf;
static void *crash_dump_buf_aligned;
char dump_log_path[STR_LEN];
static enum dm_dump_mode dump_mode = DM_DUMP_OFF;
extern char *vmname;

#define DUMP_SZ_1K 1024UL
#define DUMP_SZ_1M (DUMP_SZ_1K * 1024UL)
#define DUMP_SZ_1G (DUMP_SZ_1M * 1024UL)
#define FILE_NAME_LENGTH 1024

int dump_set_params(enum dm_dump_mode mode)
{
	dump_mode = mode;

	return 0;
}

int acrn_parse_dump_log_path(char *arg)
{
	size_t len = strnlen(arg, STR_LEN);

	if (len < STR_LEN) {
		strncpy(dump_log_path, arg, len + 1);
#ifdef DM_DEBUG
		dump_mode = DM_DUMP_ON_PANIC;
#endif
		return 0;
	} else {
		return -1;
	}
}

bool get_dev_by_uuid(char **dev_path)
{
	int ret;
	const char *uuid = NULL;
	const char *type = NULL;
	const char *devname = NULL;
	blkid_cache cache = NULL;
	blkid_dev_iterate dev_iter;
	blkid_tag_iterate tag_iter;
	blkid_dev dev;
	bool is_find = false;

	ret = blkid_get_cache(&cache, NULL);
	if (ret < 0) {
		pr_info("blkid_get_cache\n");
		goto ret;
	}

	ret = blkid_probe_all(cache);
	if (ret < 0) {
		pr_info("blkid_probe_all\n");
		goto ret;
	}

	dev_iter = blkid_dev_iterate_begin(cache);
	while (blkid_dev_next(dev_iter, &dev) == 0) {
		dev = blkid_verify(cache, dev);
		if (!dev) {
			pr_info("blkid_verify\n");
			continue;
		}
		tag_iter = blkid_tag_iterate_begin(dev);
		while (blkid_tag_next(tag_iter, &type, &uuid) == 0) {
			if (strcmp(uuid, DUMP_PART_UUID) == 0) {
				devname = blkid_dev_devname(dev);
				*dev_path =  devname ? strdup(devname) : NULL;
				blkid_tag_iterate_end(tag_iter);
				blkid_dev_iterate_end(dev_iter);
				is_find = true;
				goto ret;
			}
		}
		blkid_tag_iterate_end(tag_iter);
	}
	blkid_dev_iterate_end(dev_iter);

ret:
	blkid_put_cache(cache);
	return is_find;
}

void save_log(void)
{
	int fd;
	int written_size;
	int file_path_len = 0;

	char file_name[FILE_NAME_LENGTH];
	struct timespec times = { 0, 0 };
	struct stat st;
	struct tm *lt;
	time_t tt;

	time(&tt);
	lt = localtime(&tt);
	clock_gettime(CLOCK_MONOTONIC, &times);

	file_path_len = snprintf(file_name, FILE_NAME_LENGTH - 1,
		 "%s//%s-dump-%4d-%02d-%02d-%02d:%02d:%02d.log", dump_log_path,
		 vmname, lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
		 lt->tm_hour, lt->tm_min, lt->tm_sec);

	if ((file_path_len < 0) || (file_path_len > FILE_NAME_LENGTH) ||
		stat(dump_log_path, &st)) {
		pr_info("Dump finished but failed to log %s, "
			"please check '--dump_log <file_path>' arg in acrn-dm \n",
			dump_log_path);
		return;
	}

	fd = open(file_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		pr_info("Dump finished but failed to open %s, "
			"missing '--dump_log <file_path>' arg in acrn-dm?\n",
			file_name);
		return;
	}

	written_size = write(fd, vmname, strlen(vmname));
	if (written_size != strlen(vmname)) {
		pr_info("Write file truncated, expected 0x%lx, actual 0x%lx\n",
			strlen(vmname), written_size);
	}

	pr_info("Saving dump log to %s\n", file_name);
	close(fd);
}

/*
 * write() syscall can transfer at most 0x7ffff000 bytes.
 * Any transfer that exceeds this size needs a split.
 */
size_t write_helper(int fd, void *buf, size_t size)
{
	size_t written_size;
	int count = size / DUMP_SZ_1G;
	int mod = size % DUMP_SZ_1G;

	for (int i = 0; i < count; i++) {
		written_size = write(fd, buf + i * DUMP_SZ_1G, DUMP_SZ_1G);
		if (written_size != DUMP_SZ_1G) {
			pr_info("Write file truncated, expected 0x%lx, actual 0x%lx\n",
				DUMP_SZ_1G, written_size);
			return -1;
		}
	}

	if (mod) {
		written_size = write(fd, buf + count * DUMP_SZ_1G, mod);
		if (written_size != mod) {
			pr_info("Write file truncated, expected 0x%lx, actual 0x%lx\n",
				mod, written_size);
			return -1;
		}
	}

	return size;
}

void dump_guest_memory(struct vmctx *ctx)
{
	int fd;
	char *vmcoreinfo;
	void *host_addr;
	size_t written_size;
	struct shm_vm *shm_vm;
	dump_hdr_t hdr;
	char *dump_path = NULL;

	if (dump_mode == DM_DUMP_OFF) {
		pr_info("Dump condition set to 'off'. Skipping dump\n");
		return;
	}

	shm_vm = (struct shm_vm *)crash_dump_buf_aligned;
	vmcoreinfo = (char *)(shm_vm->vmcoreinfo);
	/* normal boot, no need to dump unless 'dump_mode' is DM_DUMP_ON_REBOOT */
	if (dump_mode == DM_DUMP_ON_PANIC &&
	    shm_vm->boot_reason == BOOT_REASON_NORMAL_BOOT) {
		shm_vm->boot_reason = BOOT_REASON_DEFAULT_SET;
		return;
	}
	shm_vm->boot_reason = BOOT_REASON_DEFAULT_SET;
	pr_info("vmcoreinfo:\n%s\n", vmcoreinfo);

	if (!get_dev_by_uuid(&dump_path) || dump_path == NULL) {
		pr_info("Failed to get raw partition for dump\n");
		return;
	}
	pr_info("Saving raw dump to %s\n", dump_path);
	fd = open(dump_path, O_WRONLY | O_CREAT | O_TRUNC, 0660);
	free(dump_path);
	if (fd < 0) {
		pr_info("Failed to open dump partition\n");
		return;
	}

	memset(&hdr, 0, sizeof(dump_hdr_t));
	strcpy((char *)hdr.magic, DUMP_MAGIC);

	hdr.dump_hdr_ver = DUMP_HEAD_VERSION;
	hdr.owner = DUMP_GUES;
	hdr.region_num = 0;
	if (ctx->lowmem > 0) {
		hdr.dump_ram_region[hdr.region_num].start = 0;
		hdr.dump_ram_region[hdr.region_num].map_sz = ctx->lowmem;
		hdr.region_num++;
	}
	if (ctx->highmem > 0) {
		hdr.dump_ram_region[hdr.region_num].start =
			ctx->highmem_gpa_base;
		hdr.dump_ram_region[hdr.region_num].map_sz = ctx->highmem;
		hdr.region_num++;
	}

	// Write Dump Header first.
	written_size = write(fd, &hdr, sizeof(hdr));
	if (written_size != sizeof(hdr)) {
		pr_info("Write file truncated, expected 0x%lx, actual 0x%lx\n",
			sizeof(hdr), written_size);
		close(fd);
		return;
	}

	// Write Shared Memory.
	written_size = write(fd, shm_vm, sizeof(struct shm_vm));
	if (written_size != sizeof(struct shm_vm)) {
		pr_info("Write file truncated, expected 0x%lx, actual 0x%lx\n",
			sizeof(struct shm_vm), written_size);
		close(fd);
		return;
	}

	//VM shared memory should occupy 16KB, reserved 1M in partition
	lseek(fd, sizeof(hdr) + RESERVED_MEM_SIZE, SEEK_SET);

	for (int i = 0; i < hdr.region_num; i++) {
		host_addr = paddr_guest2host(ctx, hdr.dump_ram_region[i].start,
					     hdr.dump_ram_region[i].map_sz);
		if (host_addr == NULL) {
			break;
		}
		written_size = write_helper(fd, host_addr,
					    hdr.dump_ram_region[i].map_sz);
		if (written_size != hdr.dump_ram_region[i].map_sz) {
			pr_info("Write file truncated, expected 0x%lx, actual 0x%lx\n",
				hdr.dump_ram_region[i].map_sz, written_size);
			break;
		}
	}

	save_log();
	close(fd);
}

int init_dump_shmem(struct vmctx *ctx)
{
	int ret = -1;
	struct acrn_vm_memmap memmap;
	void *buf;
	void *buf_aligned;
	struct shm_vm *shm_vm;

	/* Make sure there is enough buffer for alignmemt. */
	buf = calloc(1, DUMP_E820_SECTION_SZ * 2);
	if (!buf) {
		pr_err("Failed to allocate shared memory for crash dump\n");
		return ret;
	}
	buf_aligned = (void *)ALIGN_UP((size_t)buf, DUMP_E820_SECTION_SZ);
	bzero(&memmap, sizeof(struct acrn_vm_memmap));
	memmap.type = ACRN_MEMMAP_RAM;
	memmap.len = DUMP_E820_SECTION_SZ;
	memmap.user_vm_pa = DUMP_E820_ENTRY_BASE;
	memmap.vma_base = (uint64_t)buf_aligned;
	memmap.attr = ACRN_MEM_ACCESS_RWX;

	ret = ioctl(ctx->fd, ACRN_IOCTL_SET_MEMSEG, &memmap);
	if (ret) {
		pr_err("mapping EPT for crash dump shmem returned an error: %s\n",
		       errormsg(errno));
		free(buf);
		return ret;
	}

	crash_dump_buf = buf;
	crash_dump_buf_aligned = buf_aligned;

	shm_vm = (struct shm_vm *)crash_dump_buf_aligned;
	shm_vm->shm_header.shm_hdr_version = SHM_HEAD_VERSION;
	shm_vm->shm_header.dump_ctl = DUMP_FULL;
	// TODO
	// shm_vm->shm_header.type = ;
	/* shm_vm->guest_version & shm_vm->vmcoreinfo will be filled by guest */
	strcpy(shm_vm->guest_name, ctx->name);
	shm_vm->boot_reason = BOOT_REASON_DEFAULT_SET;

	return ret;
}

void deinit_dump_shmem(void)
{
	free(crash_dump_buf);
}
