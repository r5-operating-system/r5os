/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Host test of the fence-page fast path in libdrm-r5 (r5_translate.c)
 * against a fake kernel whose syncobj rules are copied from
 * r5_gpu_core.cpp (gpu_job_blocks, gpu_list_ready, gpu_list_submitted,
 * gpu_sync_wait_list, gpu_wait_value, QUERY) and whose block writer is
 * gpu_fence_publish.
 *
 *  1. differential: random kernel states and random waits, queries and
 *     WAIT_CS; the answer with the fence block must equal the answer of
 *     the kernel call, and the share answered without an ioctl is counted
 *  2. seqlock: a writer thread changes a slot while readers query it; a
 *     torn pair is a failure
 *  3. a block that is closed and handed to another file (new generation),
 *     and a kernel without R5_GPU_FENCE_PAGE
 *  4. speed: calls per second with and without the block (host only)
 *
 * Build and run: scripts/test-r5-fence-page.sh (WSL).
 */
#include "r5_gpu.h"
#include "r5_translate.h"

#include <drm.h>
#include <amdgpu_drm.h>

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d %s\n", \
	__FILE__, __LINE__, #x); exit(1); } } while (0)

extern unsigned long r5_host_fence_fast;
extern int r5_host_gpu_debug;
extern int r5_host_gpu_trace;


/* #pragma mark - fake kernel (r5_gpu_core.cpp rules) */


enum { kMaxSync = 128, kMaxJob = 64, kMaxList = 8 };

typedef struct {
	int			live;
	uint64_t	point;
	uint64_t	submitted;
} fake_sync;

typedef struct {
	int			live;
	uint64_t	sequence;
	uint32_t	count;
	int			sync[kMaxList];	/* index, -1 for none */
	uint64_t	points[kMaxList];
} fake_job;

static fake_sync gSync[kMaxSync];
static fake_job gJob[kMaxJob];
static uint64_t gSeen;			/* the ring's fence (gpu_hw_seen) */
static uint64_t gRetired;		/* client->retired */
static int gGone;				/* gpu_gone(client) */
static int gPageSupported = 1;
static int gPageBusy;
static uint32_t gGeneration = 1;
static unsigned long gIoctlWaits;	/* wait, query and WAIT_CS ioctls */
static unsigned long gIoctlPage;

static _Alignas(4096) unsigned char gArea[4 * R5_GPU_FENCE_BLOCK_BYTES];
static r5_gpu_fence_page *const gPage = (r5_gpu_fence_page *)gArea;


static int
k_signalled(uint64_t value, uint64_t submitted, uint64_t target, int binary)
{
	if (binary)
		return submitted > 0 && value >= submitted;
	return value >= target;
}


static int
k_job_blocks(int sync, uint64_t target, int binary)
{
	for (uint32_t i = 0; i < kMaxJob; i++) {
		fake_job *job = &gJob[i];
		if (!job->live || job->sequence <= gSeen)
			continue;
		for (uint32_t n = 0; n < job->count; n++) {
			if (job->sync[n] != sync)
				continue;
			if (binary)
				return 1;
			if (job->points[n] >= target && gSync[sync].point < target)
				return 1;
		}
	}
	return 0;
}


static int
k_list_ready(int sync, uint64_t target, uint32_t flags)
{
	int binary = (flags & R5_GPU_WAIT_BINARY) != 0;
	int available = (flags & R5_GPU_WAIT_AVAILABLE) != 0;
	if (!available && k_job_blocks(sync, target, binary))
		return 0;
	uint64_t value = available ? gSync[sync].submitted : gSync[sync].point;
	return k_signalled(value, gSync[sync].submitted, target, binary);
}


static int
k_list_submitted(int sync, uint64_t target, uint32_t flags)
{
	if ((flags & R5_GPU_WAIT_BINARY) != 0)
		return gSync[sync].submitted > 0;
	return gSync[sync].submitted >= target;
}


static int
k_sync_index(uint32_t handle)
{
	if (handle == 0 || handle > kMaxSync || !gSync[handle - 1].live)
		return -1;
	return (int)handle - 1;
}


/* gpu_sync_wait_list. A wait that would block reports a timeout: the
 * fake kernel never sleeps, which is the outcome at the deadline. */
static int
k_wait_list(r5_gpu_syncobj_wait *in)
{
	uint32_t handles[kMaxSync];
	uint64_t points[kMaxSync];
	int syncs[kMaxSync];
	if (in->count == 0 || in->count > kMaxSync)
		return -EINVAL;
	memset(points, 0, sizeof(points));
	memcpy(handles, (void *)(uintptr_t)in->handles, 4 * in->count);
	if (in->points != 0)
		memcpy(points, (void *)(uintptr_t)in->points, 8 * in->count);
	for (uint32_t i = 0; i < in->count; i++) {
		syncs[i] = k_sync_index(handles[i]);
		if (syncs[i] < 0)
			return -EINVAL;
	}
	if ((in->flags & R5_GPU_WAIT_FOR_SUBMIT) == 0) {
		for (uint32_t i = 0; i < in->count; i++) {
			if (!k_list_submitted(syncs[i], points[i], in->flags))
				return -EINVAL;
		}
	}
	int first = -1;
	uint32_t met = 0;
	for (uint32_t i = 0; i < in->count; i++) {
		if (!k_list_ready(syncs[i], points[i], in->flags))
			continue;
		met++;
		if (first < 0)
			first = (int)i;
		if ((in->flags & R5_GPU_WAIT_ALL) == 0)
			break;
	}
	if ((in->flags & R5_GPU_WAIT_ALL) != 0 && met == in->count) {
		in->first_signaled = 0;
		return 0;
	}
	if ((in->flags & R5_GPU_WAIT_ALL) == 0 && first >= 0) {
		in->first_signaled = (uint32_t)first;
		return 0;
	}
	if (gGone)
		return -ECANCELED;
	return -ETIMEDOUT;
}


/* gpu_fence_publish */
static void
k_publish(void)
{
	uint32_t pending[kMaxSync / 32];
	memset(pending, 0, sizeof(pending));
	for (uint32_t i = 0; i < kMaxJob; i++) {
		if (!gJob[i].live)
			continue;
		for (uint32_t n = 0; n < gJob[i].count; n++) {
			int s = gJob[i].sync[n];
			if (s >= 0 && gSync[s].live)
				pending[s / 32] |= 1u << (s % 32);
		}
	}
	__atomic_store_n(&gPage->seq, gPage->seq + 1, __ATOMIC_RELAXED);
	__atomic_thread_fence(__ATOMIC_RELEASE);
	gPage->magic = R5_GPU_FENCE_MAGIC;
	gPage->version = R5_GPU_FENCE_VERSION;
	gPage->slot_count = R5_GPU_FENCE_SLOTS;
	gPage->generation = gGeneration;
	gPage->flags = gGone ? R5_GPU_FENCE_GONE : 0;
	gPage->retired = gRetired;
	for (uint32_t i = 0; i < kMaxSync; i++) {
		r5_gpu_fence_slot *slot = &gPage->slot[i];
		memset(slot, 0, sizeof(*slot));
		if (!gSync[i].live)
			continue;
		slot->point = gSync[i].point;
		slot->submitted = gSync[i].submitted;
		slot->flags = R5_GPU_FENCE_SLOT_LIVE;
		if ((pending[i / 32] & (1u << (i % 32))) != 0)
			slot->flags |= R5_GPU_FENCE_SLOT_PENDING;
	}
	__atomic_thread_fence(__ATOMIC_RELEASE);
	__atomic_store_n(&gPage->seq, gPage->seq + 1, __ATOMIC_RELAXED);
}


void *
r5_area_address(int area)
{
	return area == 7 ? gArea : NULL;
}


int
r5_area_destroy(int area)
{
	(void)area;
	return 0;
}


int
r5_host_ioctl(int fd, unsigned long op, void *buf, size_t bytes)
{
	(void)fd;
	(void)bytes;
	if (op == R5_GPU_FENCE_PAGE) {
		r5_gpu_fence_page_map *map = buf;
		gIoctlPage++;
		if (!gPageSupported)
			return -ENOSYS;
		if (gPageBusy)
			return -EBUSY;
		map->area = 7;
		map->offset = 0;
		map->generation = gGeneration;
		return 0;
	}
	if (op == R5_GPU_SYNCOBJ_WAIT_LIST) {
		gIoctlWaits++;
		return k_wait_list(buf);
	}
	if (op == R5_GPU_FENCE_WAIT) {
		r5_gpu_fence *fence = buf;
		gIoctlWaits++;
		/* gpu_wait_value with value NULL */
		if (gGone)
			return -ECANCELED;
		if (gSeen >= fence->sequence)
			return 0;
		return -ETIMEDOUT;
	}
	if (op == R5_GPU_SYNCOBJ_QUERY) {
		r5_gpu_syncobj *sync = buf;
		int s = k_sync_index(sync->handle);
		gIoctlWaits++;
		if (s < 0)
			return -EINVAL;
		sync->point = gSync[s].point;
		return 0;
	}
	if (op == R5_GPU_SYNCOBJ_BATCH) {
		r5_gpu_syncobj_batch *batch = buf;
		uint32_t *handles = (uint32_t *)(uintptr_t)batch->handles;
		uint64_t *points = (uint64_t *)(uintptr_t)batch->points;
		gIoctlWaits++;
		if (batch->op != R5_GPU_SYNCOBJ_OP_QUERY)
			return -ENOSYS;
		for (uint32_t i = 0; i < batch->count; i++) {
			if (k_sync_index(handles[i]) < 0)
				return -EINVAL;
		}
		for (uint32_t i = 0; i < batch->count; i++)
			points[i] = gSync[handles[i] - 1].point;
		return 0;
	}
	if (op == R5_GPU_SYNCOBJ_CREATE)
		return -ENOSYS;
	return -ENOSYS;
}


/* #pragma mark - random states */


static uint64_t gRandom = 0x9e3779b97f4a7c15ull;

static uint64_t
rnd(uint64_t limit)
{
	gRandom ^= gRandom << 13;
	gRandom ^= gRandom >> 7;
	gRandom ^= gRandom << 17;
	return limit == 0 ? 0 : gRandom % limit;
}


/* Small numbers, so that equal and neighbouring values happen often. */
static void
random_state(void)
{
	memset(gSync, 0, sizeof(gSync));
	memset(gJob, 0, sizeof(gJob));
	gSeen = 5 + rnd(6);
	gGone = rnd(20) == 0;
	for (int i = 0; i < 8; i++) {
		gSync[i].live = rnd(8) != 0;
		gSync[i].submitted = rnd(4) == 0 ? 0 : rnd(14);
		gSync[i].point = rnd(3) == 0 ? gSync[i].submitted : rnd(14);
	}
	uint64_t retired = 0;
	int jobs = (int)rnd(5);
	for (int j = 0; j < jobs; j++) {
		fake_job *job = &gJob[j];
		job->live = 1;
		job->sequence = 3 + rnd(12);
		job->count = 1 + (uint32_t)rnd(3);
		for (uint32_t n = 0; n < job->count; n++) {
			job->sync[n] = rnd(6) == 0 ? -1 : (int)rnd(8);
			job->points[n] = rnd(3) == 0 ? job->sequence : rnd(14);
		}
	}
	/* retired is at most the ring's fence (r5_gpu_eop) */
	retired = gSeen > 0 ? rnd(gSeen + 1) : 0;
	gRetired = retired;
	k_publish();
}


typedef struct {
	int			ret;
	int			err;
	uint32_t	first;
	uint64_t	points[8];
	uint32_t	status;
} answer;


static answer
do_wait(int binary, uint32_t *handles, uint64_t *points, uint32_t count,
	uint32_t flags, int64_t timeout)
{
	answer a;
	memset(&a, 0, sizeof(a));
	a.first = 0xdead;
	if (binary) {
		struct drm_syncobj_wait wait;
		memset(&wait, 0, sizeof(wait));
		wait.handles = (uintptr_t)handles;
		wait.count_handles = count;
		wait.flags = flags;
		wait.timeout_nsec = timeout;
		wait.first_signaled = 0xdead;
		errno = 0;
		a.ret = r5_drm_ioctl(3, DRM_IOCTL_SYNCOBJ_WAIT, &wait, sizeof(wait));
		a.err = a.ret != 0 ? errno : 0;
		a.first = wait.first_signaled;
	} else {
		struct drm_syncobj_timeline_wait wait;
		memset(&wait, 0, sizeof(wait));
		wait.handles = (uintptr_t)handles;
		wait.points = (uintptr_t)points;
		wait.count_handles = count;
		wait.flags = flags;
		wait.timeout_nsec = timeout;
		wait.first_signaled = 0xdead;
		errno = 0;
		a.ret = r5_drm_ioctl(3, DRM_IOCTL_SYNCOBJ_TIMELINE_WAIT, &wait,
			sizeof(wait));
		a.err = a.ret != 0 ? errno : 0;
		a.first = wait.first_signaled;
	}
	return a;
}


static answer
do_query(uint32_t *handles, uint32_t count)
{
	answer a;
	struct drm_syncobj_timeline_array list;
	memset(&a, 0, sizeof(a));
	memset(&list, 0, sizeof(list));
	list.handles = (uintptr_t)handles;
	list.points = (uintptr_t)a.points;
	list.count_handles = count;
	errno = 0;
	a.ret = r5_drm_ioctl(3, DRM_IOCTL_SYNCOBJ_QUERY, &list, sizeof(list));
	a.err = a.ret != 0 ? errno : 0;
	return a;
}


static answer
do_wait_cs(uint64_t sequence, uint64_t timeout)
{
	answer a;
	union drm_amdgpu_wait_cs wait;
	memset(&a, 0, sizeof(a));
	memset(&wait, 0, sizeof(wait));
	wait.in.handle = sequence;
	wait.in.timeout = timeout;
	errno = 0;
	a.ret = r5_drm_ioctl(3, DRM_IOCTL_AMDGPU_WAIT_CS, &wait, sizeof(wait));
	a.err = a.ret != 0 ? errno : 0;
	a.status = (uint32_t)wait.out.status;
	return a;
}


static int
same(const answer *x, const answer *y, int compareFirst)
{
	if (x->ret != y->ret || x->err != y->err || x->status != y->status)
		return 0;
	if (x->ret == 0 && compareFirst && x->first != y->first)
		return 0;
	return memcmp(x->points, y->points, sizeof(x->points)) == 0;
}


static void
test_differential(int rounds)
{
	unsigned long fast = 0;
	unsigned long total = 0;
	unsigned long fastWaits = 0;
	unsigned long waits = 0;
	for (int round = 0; round < rounds; round++) {
		random_state();
		uint32_t handles[8];
		uint64_t points[8];
		uint32_t count = 1 + (uint32_t)rnd(4);
		for (uint32_t i = 0; i < count; i++) {
			handles[i] = rnd(12) == 0 ? 9 + (uint32_t)rnd(3)
				: 1 + (uint32_t)rnd(8);
			points[i] = rnd(14);
		}
		int kind = (int)rnd(4);
		uint32_t flags = 0;
		if (rnd(2))
			flags |= DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL;
		if (rnd(5) == 0)
			flags |= DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE;
		if (rnd(3) == 0)
			flags |= DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT;
		/* 0, an absolute time already past, or one far ahead */
		int64_t timeout = rnd(3) == 0 ? 0 : rnd(2) == 0 ? 1000
			: INT64_MAX / 2;

		unsigned long before = r5_host_fence_fast;
		answer withPage;
		answer kernel;
		int compareFirst = 1;
		if (kind == 0 || kind == 1) {
			withPage = do_wait(kind == 0, handles, points, count, flags,
				timeout);
			gPage->flags |= R5_GPU_FENCE_GONE;
			kernel = do_wait(kind == 0, handles, points, count, flags,
				timeout);
			waits++;
		} else if (kind == 2) {
			withPage = do_query(handles, count);
			gPage->flags |= R5_GPU_FENCE_GONE;
			kernel = do_query(handles, count);
			compareFirst = 0;
		} else {
			uint64_t sequence = rnd(16);
			withPage = do_wait_cs(sequence, rnd(2) ? 0 : INT64_MAX / 2);
			gPage->flags |= R5_GPU_FENCE_GONE;
			kernel = do_wait_cs(sequence, rnd(2) ? 0 : INT64_MAX / 2);
			compareFirst = 0;
		}
		/* the second call must have gone to the kernel */
		CHECK(r5_host_fence_fast - before <= 1);
		int wasFast = r5_host_fence_fast - before == 1;
		if (!same(&withPage, &kernel, compareFirst)) {
			printf("FAIL round %d kind %d count %u flags %#x "
				"timeout %lld: page ret %d err %d first %u status %u, "
				"kernel ret %d err %d first %u status %u\n", round, kind,
				count, flags, (long long)timeout, withPage.ret, withPage.err,
				withPage.first, withPage.status, kernel.ret, kernel.err,
				kernel.first, kernel.status);
			for (uint32_t i = 0; i < count; i++) {
				uint32_t h = handles[i];
				if (h >= 1 && h <= 8)
					printf("  handle %u point %llu submitted %llu "
						"live %d target %llu\n", h,
						(unsigned long long)gSync[h - 1].point,
						(unsigned long long)gSync[h - 1].submitted,
						gSync[h - 1].live, (unsigned long long)points[i]);
			}
			exit(1);
		}
		total++;
		if (wasFast) {
			fast++;
			if (kind <= 1)
				fastWaits++;
		}
	}
	printf("ok differential %lu calls, %lu answered from the block "
		"(%lu of %lu waits)\n", total, fast, fastWaits, waits);
}


/* #pragma mark - seqlock */


static volatile int gStop;

static void *
writer(void *data)
{
	(void)data;
	uint64_t value = 1;
	while (!gStop) {
		r5_gpu_fence_slot *slot = &gPage->slot[0];
		__atomic_store_n(&gPage->seq, gPage->seq + 1, __ATOMIC_RELAXED);
		__atomic_thread_fence(__ATOMIC_RELEASE);
		/* point and submitted in two stores: a reader that mixes two
		 * versions sees them differ */
		*(volatile uint64_t *)&slot->point = value;
		*(volatile uint64_t *)&slot->submitted = value;
		__atomic_thread_fence(__ATOMIC_RELEASE);
		__atomic_store_n(&gPage->seq, gPage->seq + 1, __ATOMIC_RELAXED);
		value++;
	}
	return NULL;
}


static unsigned long gTorn;
static unsigned long gReads;

static void *
reader(void *data)
{
	(void)data;
	uint32_t handle = 1;
	for (int i = 0; i < 2000000; i++) {
		struct drm_syncobj_wait wait;
		memset(&wait, 0, sizeof(wait));
		wait.handles = (uintptr_t)&handle;
		wait.count_handles = 1;
		wait.timeout_nsec = 0;
		/* binary: READY only when point == submitted (PENDING clear) */
		int ret = r5_drm_ioctl(3, DRM_IOCTL_SYNCOBJ_WAIT, &wait, sizeof(wait));
		__atomic_add_fetch(&gReads, 1, __ATOMIC_RELAXED);
		if (ret != 0 && errno == ETIME)
			__atomic_add_fetch(&gTorn, 1, __ATOMIC_RELAXED);
	}
	return NULL;
}


static void
test_seqlock(void)
{
	memset(gSync, 0, sizeof(gSync));
	memset(gJob, 0, sizeof(gJob));
	gGone = 0;
	gSync[0].live = 1;
	gSync[0].point = 1;
	gSync[0].submitted = 1;
	k_publish();
	unsigned long ioctlsBefore = gIoctlWaits;
	pthread_t w;
	pthread_t r[3];
	gStop = 0;
	pthread_create(&w, NULL, writer, NULL);
	for (int i = 0; i < 3; i++)
		pthread_create(&r[i], NULL, reader, NULL);
	for (int i = 0; i < 3; i++)
		pthread_join(r[i], NULL);
	gStop = 1;
	pthread_join(w, NULL);
	/* A torn read (point from one write, submitted from the next) would
	 * be point < submitted: NOT_READY, ETIME without an ioctl. With the
	 * seqlock it is a retry, or the kernel call (which sees point ==
	 * submitted here). */
	CHECK(gTorn == 0);
	printf("ok seqlock %lu reads under a writer, 0 torn, %lu went to the "
		"kernel after %d tries\n", gReads, gIoctlWaits - ioctlsBefore,
		4);
}


/* #pragma mark - generation, closed, unsupported */


static void
test_generation(void)
{
	uint32_t handle = 1;
	answer a;
	memset(gSync, 0, sizeof(gSync));
	memset(gJob, 0, sizeof(gJob));
	gGone = 0;
	gSync[0].live = 1;
	gSync[0].point = 4;
	gSync[0].submitted = 4;
	k_publish();
	unsigned long pageCalls = gIoctlPage;
	unsigned long fast = r5_host_fence_fast;
	a = do_query(&handle, 1);
	CHECK(a.ret == 0 && a.points[0] == 4);
	CHECK(r5_host_fence_fast == fast + 1);

	/* The file is closed and the block goes to another file. */
	__atomic_store_n(&gPage->seq, gPage->seq + 1, __ATOMIC_RELAXED);
	gPage->flags = R5_GPU_FENCE_GONE | R5_GPU_FENCE_CLOSED;
	gGeneration++;
	gPage->generation = gGeneration;
	__atomic_store_n(&gPage->seq, gPage->seq + 1, __ATOMIC_RELAXED);
	gSync[0].point = 9;
	gSync[0].submitted = 9;
	a = do_query(&handle, 1);
	/* this call went to the kernel, and asks for the block next time */
	CHECK(a.ret == 0 && a.points[0] == 9);
	k_publish();
	fast = r5_host_fence_fast;
	a = do_query(&handle, 1);
	CHECK(a.ret == 0 && a.points[0] == 9);
	CHECK(r5_host_fence_fast == fast + 1);
	CHECK(gIoctlPage > pageCalls);

	/* DRM_IOCTL_VERSION makes the next use ask for the block again */
	pageCalls = gIoctlPage;
	{
		struct drm_version version;
		char name[64];
		char date[64];
		char desc[64];
		memset(&version, 0, sizeof(version));
		version.name = name;
		version.name_len = sizeof(name);
		version.date = date;
		version.date_len = sizeof(date);
		version.desc = desc;
		version.desc_len = sizeof(desc);
		r5_drm_ioctl(3, DRM_IOCTL_VERSION, &version, sizeof(version));
	}
	a = do_query(&handle, 1);
	CHECK(a.ret == 0 && a.points[0] == 9);
	CHECK(gIoctlPage == pageCalls + 1);

	/* A kernel without the call: every answer from the kernel. */
	gPageSupported = 0;
	{
		struct drm_version version;
		char name[64], date[64], desc[64];
		memset(&version, 0, sizeof(version));
		version.name = name;
		version.name_len = sizeof(name);
		version.date = date;
		version.date_len = sizeof(date);
		version.desc = desc;
		version.desc_len = sizeof(desc);
		r5_drm_ioctl(3, DRM_IOCTL_VERSION, &version, sizeof(version));
	}
	fast = r5_host_fence_fast;
	unsigned long waits = gIoctlWaits;
	for (int i = 0; i < 10; i++) {
		a = do_query(&handle, 1);
		CHECK(a.ret == 0 && a.points[0] == 9);
	}
	CHECK(r5_host_fence_fast == fast);
	CHECK(gIoctlWaits == waits + 10);
	gPageSupported = 1;
	printf("ok generation: closed block and new owner re-asked, VERSION "
		"re-asks, a kernel without the call answers every query\n");
}


/* #pragma mark - speed */


static double
now_s(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}


static void
test_speed(void)
{
	uint32_t handle = 1;
	memset(gSync, 0, sizeof(gSync));
	memset(gJob, 0, sizeof(gJob));
	gGone = 0;
	gSync[0].live = 1;
	gSync[0].point = 3;
	gSync[0].submitted = 3;
	k_publish();
	const int n = 2000000;
	double start = now_s();
	for (int i = 0; i < n; i++) {
		struct drm_syncobj_wait wait;
		memset(&wait, 0, sizeof(wait));
		wait.handles = (uintptr_t)&handle;
		wait.count_handles = 1;
		CHECK(r5_drm_ioctl(3, DRM_IOCTL_SYNCOBJ_WAIT, &wait,
			sizeof(wait)) == 0);
	}
	double fast = (now_s() - start) / n * 1e9;
	printf("ok speed host: signalled binary wait from the block %.0f ns "
		"(the fake kernel has no system call to compare with)\n", fast);
}


int
main(void)
{
	test_differential(400000);
	test_seqlock();
	test_speed();
	test_generation();
	printf("ok fence page\n");
	return 0;
}
