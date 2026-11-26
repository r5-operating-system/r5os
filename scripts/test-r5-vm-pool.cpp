/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Host test for per-file VRAM page-table page recycling (r5_vm_page_pool)
 * and deferred VRAM free overflow handling (r5_defer).
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "r5_defer.h"
#include "r5_gpu.h"
#include "r5_vm.h"
#include "r5_vmid.h"


static int
fail(const char* what)
{
	fprintf(stderr, "FAIL: %s\n", what);
	return 1;
}


struct pt_bump_ctx {
	r5_vm_page_pool*	pool;
	uint64_t			cursor;
	uint64_t			backing[64][512];
	uint32_t			allocs;
	uint32_t			failAfter;
};


static int
pt_alloc(void* cookie, uint64_t* physical, uint64_t** cpu)
{
	pt_bump_ctx* ctx = (pt_bump_ctx*)cookie;
	if (r5_vm_pool_take(ctx->pool, physical, cpu))
		return 1;
	if (ctx->failAfter != 0 && ctx->allocs >= ctx->failAfter)
		return 0;
	if (ctx->allocs >= 64)
		return 0;
	*physical = ctx->cursor;
	*cpu = ctx->backing[ctx->allocs];
	ctx->cursor += 4096ull;
	ctx->allocs++;
	return 1;
}


static int
test_vm_page_pool()
{
	pt_bump_ctx ctx;
	r5_vm_page poolBuf[64];
	r5_vm_page_pool pool = { poolBuf, 64, 0 };
	memset(&ctx, 0, sizeof(ctx));
	ctx.pool = &pool;
	ctx.cursor = 0x100000ull;

	uint64_t afterFirst = 0;
	for (int cycle = 0; cycle < 500; cycle++) {
		r5_vm vm;
		r5_vm_page pages[32];
		memset(&vm, 0, sizeof(vm));
		memset(pages, 0, sizeof(pages));
		vm.pages = pages;
		vm.capacity = 32;
		vm.alloc = pt_alloc;
		vm.cookie = &ctx;
		if (!r5_vm_new_page(&vm, 0))
			return fail("pt pool root");
		if (!r5_vm_map_page(&vm, R5_GPU_VA_OFFSET + 0x10000ull,
				0x200000ull | R5_GFX9_PTE_VALID))
			return fail("pt pool map low");
		if (!r5_vm_map_page(&vm, R5_GPU_HIGH_VA_OFFSET + 0x20000ull,
				0x300000ull | R5_GFX9_PTE_VALID))
			return fail("pt pool map high");
		if (cycle == 0)
			afterFirst = ctx.cursor;
		else if (ctx.cursor != afterFirst)
			return fail("pt pool cursor grew");
		uint32_t used = vm.used;
		uint32_t returned = r5_vm_release_pages(&vm, &pool);
		if (returned != used || vm.used != 0)
			return fail("pt pool release");
	}
	if (pool.count == 0 || ctx.cursor != afterFirst)
		return fail("pt pool level");

	/* Partial init failure rollback returns all allocated pages. */
	uint32_t beforeFail = pool.count;
	{
		r5_vm vm;
		r5_vm_page pages[32];
		memset(&vm, 0, sizeof(vm));
		memset(pages, 0, sizeof(pages));
		vm.pages = pages;
		vm.capacity = 32;
		vm.alloc = pt_alloc;
		vm.cookie = &ctx;
		if (!r5_vm_new_page(&vm, 0))
			return fail("pt fail root");
		if (!r5_vm_new_page(&vm, 0))
			return fail("pt fail child");
		r5_vm_release_pages(&vm, &pool);
		if (pool.count != beforeFail || vm.used != 0)
			return fail("pt fail rollback");
	}
	printf("ok vm page pool: 500 open/close cycles, cursor flat at 0x%llx (%u pages reused)\n",
		(unsigned long long)ctx.cursor, pool.count);
	return 0;
}


struct defer_overflow {
	r5_defer		entry;
	defer_overflow*	next;
};


static int
test_defer_overflow()
{
	const int kCap = 16;
	const int kTotal = 48;
	r5_defer table[kCap];
	memset(table, 0, sizeof(table));
	defer_overflow* overflow = NULL;

	for (int i = 0; i < kTotal; i++) {
		uint64_t page = 0x100000ull + (uint64_t)i * 4096ull;
		if (!r5_defer_add(table, kCap, page)) {
			defer_overflow* node = (defer_overflow*)malloc(sizeof(*node));
			if (node == NULL)
				return fail("defer oom");
			node->entry.page = page;
			node->entry.seq = 0;
			node->entry.state = 1;
			node->next = overflow;
			overflow = node;
		}
	}

	r5_defer_stamp(table, kCap, 42);
	for (defer_overflow* n = overflow; n != NULL; n = n->next)
		r5_defer_stamp(&n->entry, 1, 42);

	int reclaimed = 0;
	uint64_t page = 0;
	while (r5_defer_take(table, kCap, 42, &page))
		reclaimed++;
	defer_overflow** link = &overflow;
	while (*link != NULL) {
		defer_overflow* node = *link;
		if (r5_defer_take(&node->entry, 1, 42, &page)) {
			*link = node->next;
			free(node);
			reclaimed++;
		} else {
			link = &node->next;
		}
	}
	if (reclaimed != kTotal || overflow != NULL)
		return fail("defer overflow reclaim");
	printf("ok defer overflow: %d/%d pages reclaimed\n", reclaimed, kTotal);
	return 0;
}


static int
test_vm_defer_until_retire()
{
	pt_bump_ctx ctx;
	r5_vm_page poolBuf[64];
	r5_vm_page_pool pool = { poolBuf, 64, 0 };
	memset(&ctx, 0, sizeof(ctx));
	ctx.pool = &pool;
	ctx.cursor = 0x200000ull;

	r5_vmid_mgr mgr;
	r5_vmid_init_files(&mgr);
	int flush = 0;
	uint32_t vmid = r5_vmid_reserve(&mgr, 7, 10, 1, &flush);
	if (vmid < 2 || vmid > 15)
		return fail("vmid reserve");
	r5_vmid_arm(&mgr, 25);

	r5_vm vm;
	r5_vm_page pages[32];
	memset(&vm, 0, sizeof(vm));
	memset(pages, 0, sizeof(pages));
	vm.pages = pages;
	vm.capacity = 32;
	vm.alloc = pt_alloc;
	vm.cookie = &ctx;
	if (!r5_vm_new_page(&vm, 0))
		return fail("defer vm root");
	if (!r5_vm_map_page(&vm, R5_GPU_VA_OFFSET + 0x10000ull,
			0x400000ull | R5_GFX9_PTE_VALID))
		return fail("defer vm map");
	uint32_t used = vm.used;

	/* File closes at seen=20 while fence=25 is still in flight. */
	uint64_t busyFence = r5_vmid_last_fence(&mgr, 7);
	if (busyFence != 25 || busyFence <= 20)
		return fail("busy fence check");
	/* Must NOT return pages to pool yet. */
	if (pool.count != 0)
		return fail("pages returned early");

	/* Fence retires at seen=25: release VMID and return pages. */
	r5_vmid_release(&mgr, 7, 25);
	if (r5_vmid_last_fence(&mgr, 7) != 0)
		return fail("vmid not released after retire");
	uint32_t returned = r5_vm_release_pages(&vm, &pool);
	if (returned != used || pool.count != used)
		return fail("pages not returned after retire");

	printf("ok vm defer until retire: %u pages held until fence 25 retired\n",
		returned);
	return 0;
}


int
main()
{
	if (test_vm_page_pool() != 0)
		return 1;
	if (test_vm_defer_until_retire() != 0)
		return 1;
	if (test_defer_overflow() != 0)
		return 1;
	return 0;
}
