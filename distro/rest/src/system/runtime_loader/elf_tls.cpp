/*
 * Copyright 2014, Paweł Dziepak, pdziepak@quarnos.org.
 * Distributed under the terms of the MIT License.
 */

#include "elf_tls.h"

#include <stdlib.h>
#include <string.h>

#include <OS.h>
#include <support/TLS.h>
#include <syscalls.h>

#include <tls.h>

// SPDX-SnippetBegin
// SPDX-License-Identifier: MIT
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
#include <locks.h>
// SPDX-SnippetEnd

#include <util/kernel_cpp.h>

#include "r5_tpoff.h"


class TLSBlock {
public:
	inline				TLSBlock();
	inline				TLSBlock(void* pointer);

	inline	status_t	Initialize(unsigned dso);

			void		Destroy();

			bool		IsInvalid() const	{ return fPointer == NULL; }

			void*		operator+(addr_t offset) const
							{ return (void*)((addr_t)fPointer + TLS_DTV_OFFSET + offset); }

private:
			void*		fPointer;
};

class Generation {
public:
	inline				Generation();

			unsigned	Counter() const	{ return fCounter; }
			unsigned	Size() const	{ return fSize; }

			void		SetCounter(unsigned counter)	{ fCounter = counter; }
			void		SetSize(unsigned size)	{ fSize = size; }

private:
			unsigned	fCounter;
			unsigned	fSize;
};

class DynamicThreadVector {
public:
	inline				DynamicThreadVector();

			void		DestroyAll();

	inline	TLSBlock&	operator[](unsigned dso);

private:
			bool		_DoesExist() const	{ return *fVector != NULL; }
			unsigned	_Size() const
							{ return _DoesExist()
									? fGeneration->Size() : 0; }

			unsigned	_Generation() const;

			status_t	_ResizeVector(unsigned minimumSize);

			TLSBlock**	fVector;
			Generation*	fGeneration;
			TLSBlock	fNullBlock;
};


TLSBlockTemplates*	TLSBlockTemplates::fInstance;


void
TLSBlockTemplate::SetBaseAddress(addr_t baseAddress)
{
	fAddress = (void*)((addr_t)fAddress + baseAddress);
	// SPDX-SnippetBegin
	// SPDX-License-Identifier: MIT
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
#if !defined(__x86_64__)
	// x86_64 marks the image after its relocation (arch_relocate.cpp, R5);
	// the other architectures have no such hook yet and keep the old point.
	fRelocated = true;
#endif
	// SPDX-SnippetEnd
}


TLSBlock
TLSBlockTemplate::CreateBlock()
{
	void* pointer = malloc(fMemorySize + TLS_DTV_OFFSET);
	if (pointer == NULL)
		return TLSBlock();
	memset(pointer, 0, TLS_DTV_OFFSET);
	memcpy((char*)pointer + TLS_DTV_OFFSET, fAddress, fFileSize);
	if (fMemorySize > fFileSize)
		memset((char*)pointer + TLS_DTV_OFFSET + fFileSize, 0, fMemorySize - fFileSize);
	return TLSBlock(pointer);
}


// SPDX-SnippetBegin
// SPDX-License-Identifier: MIT
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// Every new thread copies the static TLS images (Materialize(), from the
// thread entry in libroot) while another thread may load or unload a library
// (Register(), SetBaseAddress(), Unregister(), under the loader's own lock in
// elf.cpp). Without this lock a thread started during dlopen() read the
// template list while it grew, and copied a library's TLS image from its
// unrelocated file address: a segment violation in memcpy (QEMU, Aomi
// starting while libr5servo.so loads). The loader's lock is taken first and
// this one second, never the other way round.
static recursive_lock sTemplatesLock
	= RECURSIVE_LOCK_INITIALIZER("runtime loader tls templates");

// Placements for __tls_get_addr. Published once per load or unload and
// never freed: a thread may still be reading the previous one. The ready
// slot in the thread's TLS vector (tls_allocate, not __thread: this
// function is __tls_get_addr) remembers which snapshot that thread has
// already copied into its static image.
struct TlsSnap {
	uint32		count;
	uint8*		has;
	uint64*		placement;
};

static TlsSnap* sSnap = NULL;
static int32 sReadySlot = -1;


static TlsSnap*
load_snap()
{
	return __atomic_load_n(&sSnap, __ATOMIC_ACQUIRE);
}


static int32
ready_slot()
{
	int32 slot = __atomic_load_n(&sReadySlot, __ATOMIC_ACQUIRE);
	if (slot >= 0)
		return slot;

	int32 created = tls_allocate();
	if (created < 0)
		return -1;

	int32 expected = -1;
	if (!__atomic_compare_exchange_n(&sReadySlot, &expected, created,
			false, __ATOMIC_RELEASE, __ATOMIC_RELAXED))
		return expected;
	return created;
}


static bool
thread_ready(TlsSnap* snap)
{
	int32 slot = __atomic_load_n(&sReadySlot, __ATOMIC_ACQUIRE);
	if (slot < 0 || snap == NULL)
		return false;
	return tls_get(slot) == (void*)snap;
}


static void
mark_ready(TlsSnap* snap)
{
	if (snap == NULL)
		return;
	int32 slot = ready_slot();
	if (slot >= 0)
		tls_set(slot, snap);
}
// SPDX-SnippetEnd


TLSBlockTemplates&
TLSBlockTemplates::Get()
{
	if (fInstance == NULL)
		fInstance = new TLSBlockTemplates;
	return *fInstance;
}


unsigned
TLSBlockTemplates::Register(const TLSBlockTemplate& block)
{
	// SPDX-SnippetBegin
	// SPDX-License-Identifier: MIT
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	RecursiveLocker locker(sTemplatesLock);
	// SPDX-SnippetEnd
	unsigned dso;

	// SPDX-SnippetBegin
	// SPDX-License-Identifier: MIT
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// A new slot every time, never a freed one: Materialize() keeps per
	// thread how many slots it copied, in order. A reused low slot would be
	// skipped by threads that already passed it, and a thread that started
	// during the load would stop before libraries above it.
	dso = fTemplates.size();
	fTemplates.push_back(block);
	// SPDX-SnippetEnd

	fTemplates[dso].SetGeneration(fGeneration);
	// SPDX-SnippetBegin
	// SPDX-License-Identifier: MIT
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	_AssignStaticOffset(dso);
	_PublishSnap();
	// SPDX-SnippetEnd
	return dso;
}


void
TLSBlockTemplates::Unregister(unsigned dso)
{
	if (dso == unsigned(-1))
		return;

	// SPDX-SnippetBegin
	// SPDX-License-Identifier: MIT
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	RecursiveLocker locker(sTemplatesLock);
	// SPDX-SnippetEnd
	fGeneration++;
	// SPDX-SnippetBegin
	// SPDX-License-Identifier: MIT
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// The library's memory goes away. A thread that starts later must not
	// copy its TLS image (Materialize() skips a template with no static
	// offset).
	fTemplates[dso] = TLSBlockTemplate();
	fTemplates[dso].SetGeneration(fGeneration);
	_PublishSnap();
	// SPDX-SnippetEnd
	fFreeDSOs.push_back(dso);
}


void
TLSBlockTemplates::SetBaseAddress(unsigned dso, addr_t baseAddress)
{
	// SPDX-SnippetBegin
	// SPDX-License-Identifier: MIT
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	RecursiveLocker locker(sTemplatesLock);
	// SPDX-SnippetEnd
	if (dso != unsigned(-1))
		fTemplates[dso].SetBaseAddress(baseAddress);
}


unsigned
TLSBlockTemplates::GetGeneration(unsigned dso) const
{
	// SPDX-SnippetBegin
	// SPDX-License-Identifier: MIT
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	RecursiveLocker locker(sTemplatesLock);
	// SPDX-SnippetEnd
	if (dso == unsigned(-1))
		return fGeneration;
	return fTemplates[dso].Generation();
}


TLSBlock
TLSBlockTemplates::CreateBlock(unsigned dso)
{
	// SPDX-SnippetBegin
	// SPDX-License-Identifier: MIT
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	RecursiveLocker locker(sTemplatesLock);
	// SPDX-SnippetEnd
	return fTemplates[dso].CreateBlock();
}


TLSBlockTemplates::TLSBlockTemplates()
	:
	fGeneration(0),
	fStaticBaseReady(false),
	fStaticBase(0),
	fStaticNext(0)
{
}


TLSBlock::TLSBlock()
	:
	fPointer(NULL)
{
}


TLSBlock::TLSBlock(void* pointer)
	:
	fPointer(pointer)
{
}


status_t
TLSBlock::Initialize(unsigned dso)
{
	fPointer = TLSBlockTemplates::Get().CreateBlock(dso).fPointer;
	return fPointer != NULL ? B_OK : B_NO_MEMORY;
}


void
TLSBlock::Destroy()
{
	free(fPointer);
	fPointer = NULL;
}


Generation::Generation()
	:
	fCounter(0),
	fSize(0)
{
}


DynamicThreadVector::DynamicThreadVector()
	:
	fVector((TLSBlock**)tls_address(TLS_DYNAMIC_THREAD_VECTOR)),
	fGeneration(NULL)
{
	if (*fVector != NULL)
		fGeneration = (Generation*)*(void**)*fVector;
}


void
DynamicThreadVector::DestroyAll()
{
	for (unsigned i = 0; i < _Size(); i++) {
		TLSBlock& block = (*fVector)[i + 1];
		if (!block.IsInvalid())
			block.Destroy();
	}

	free(*fVector);
	*fVector = NULL;

	delete fGeneration;
}


TLSBlock&
DynamicThreadVector::operator[](unsigned dso)
{
	unsigned generation = TLSBlockTemplates::Get().GetGeneration(-1);
	if (_Generation() < generation) {
		// We need to destroy any blocks whose DSO generation has changed
		// to be greater than our own generation.
		for (unsigned dsoIndex = 0; dsoIndex < _Size(); dsoIndex++) {
			TLSBlock& block = (*fVector)[dsoIndex + 1];
			unsigned dsoGeneration
				= TLSBlockTemplates::Get().GetGeneration(dsoIndex);
			if (dsoGeneration > _Generation() && dsoGeneration <= generation)
				block.Destroy();
		}

		fGeneration->SetCounter(generation);
	}

	if (_Size() <= dso) {
		status_t result = _ResizeVector(dso + 1);
		if (result != B_OK)
			return fNullBlock;
	}

	TLSBlock& block = (*fVector)[dso + 1];
	if (block.IsInvalid()) {
		status_t result = block.Initialize(dso);
		if (result != B_OK)
			return fNullBlock;
	}

	return block;
}


unsigned
DynamicThreadVector::_Generation() const
{
	if (fGeneration != NULL)
		return fGeneration->Counter();
	return unsigned(-1);
}


status_t
DynamicThreadVector::_ResizeVector(unsigned minimumSize)
{
	static const unsigned kInitialSize = 4;
	unsigned size = std::max(minimumSize, kInitialSize);
	unsigned oldSize = _Size();
	if (size <= oldSize)
		return B_OK;

	void* newVector = realloc(*fVector, (size + 1) * sizeof(TLSBlock));
	if (newVector == NULL)
		return B_NO_MEMORY;

	*fVector = (TLSBlock*)newVector;
	memset((void*)(*fVector + oldSize + 1), 0, (size - oldSize) * sizeof(TLSBlock));
	if (fGeneration == NULL) {
		fGeneration = new Generation;
		if (fGeneration == NULL)
			return B_NO_MEMORY;
// SPDX-SnippetBegin
// SPDX-License-Identifier: MIT
// SPDX-SnippetCopyrightText: 2026 Witold Bołt
		// A new vector has no blocks yet, so none of them can be stale.
		// It starts at the templates' generation. At 0, unloading any TLS
		// image makes the next access destroy the blocks just created.
		fGeneration->SetCounter(TLSBlockTemplates::Get().GetGeneration(-1));
// SPDX-SnippetEnd
	}

	*(Generation**)*fVector = fGeneration;
	fGeneration->SetSize(size);

	return B_OK;
}


void*
get_tls_address(unsigned dso, addr_t offset)
{
	// SPDX-SnippetBegin
	// SPDX-License-Identifier: MIT
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// An executable writes an initial-exec TLS symbol at a fixed offset from
	// the thread pointer. A library reads that same symbol through
	// __tls_get_addr. The static image is the one the relocation names. A
	// template without a static offset keeps the dynamic block.
	//
	// Every loaded image is given a static offset, so this is the path for
	// ordinary general-dynamic TLS (Mesa, LLVM, Skia), not a rare case.
	// Once this thread has copied the published snapshot, the address is
	// the thread pointer plus that snapshot. No lock, and no walk of the
	// team's areas: both of those on every call kept the draw thread from
	// finishing a frame (26 Nov 2026).
	TlsSnap* snap = load_snap();
	if (snap != NULL && dso < snap->count && snap->has[dso]
			&& thread_ready(snap)) {
		addr_t threadPointer = (addr_t)tls_get(TLS_BASE_ADDRESS_SLOT);
		return (void*)(threadPointer + snap->placement[dso] + offset);
	}

	TLSBlockTemplates& templates = TLSBlockTemplates::Get();
	if (snap != NULL && dso < snap->count && snap->has[dso]) {
		void* ready = NULL;
		bool full = templates.Materialize(&ready);
		if (full && ready != NULL)
			mark_ready((TlsSnap*)ready);
		TlsSnap* use = (TlsSnap*)ready;
		if (use == NULL || dso >= use->count || !use->has[dso])
			use = load_snap();
		if (use != NULL && dso < use->count && use->has[dso]) {
			addr_t threadPointer = (addr_t)tls_get(TLS_BASE_ADDRESS_SLOT);
			return (void*)(threadPointer + use->placement[dso] + offset);
		}
	} else if (templates.HasStaticPlacement(dso)) {
		// The snapshot could not be published (out of memory). Same
		// address as before, without the per-call area walk: Materialize
		// maps once until the copy catches up.
		void* ready = NULL;
		templates.Materialize(&ready);
		if (ready != NULL)
			mark_ready((TlsSnap*)ready);
		addr_t threadPointer = (addr_t)tls_get(TLS_BASE_ADDRESS_SLOT);
		return (void*)(threadPointer + templates.StaticPlacement(dso)
			+ offset);
	}
	// SPDX-SnippetEnd

	DynamicThreadVector dynamicThreadVector;
	TLSBlock& block = dynamicThreadVector[dso];
	if (block.IsInvalid())
		return NULL;
	return block + offset;
}


void
destroy_thread_tls()
{
	DynamicThreadVector().DestroyAll();
}


// SPDX-SnippetBegin
// SPDX-License-Identifier: MIT
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors

void
TLSBlockTemplates::_AssignStaticOffset(unsigned dso)
{
	if (!fStaticBaseReady) {
		fStaticBase = _GapFromThreadPointer();
		// The first 16 bytes of the static image hold how many templates
		// this thread has already copied.
		fStaticNext = 16;
		fStaticBaseReady = true;
	}

	TLSBlockTemplate& stored = fTemplates[dso];
	size_t alignment = stored.Alignment();
	if (alignment < 1)
		alignment = 1;
	size_t offset = (fStaticNext + alignment - 1) & ~(alignment - 1);
	stored.SetStaticOffset(offset);
	fStaticNext = offset + stored.MemorySize();
}


void
TLSBlockTemplates::_PublishSnap()
{
	uint32 count = (uint32)fTemplates.size();
	TlsSnap* snap = (TlsSnap*)malloc(sizeof(TlsSnap));
	uint8* has = count > 0 ? (uint8*)malloc(count) : NULL;
	uint64* placement = count > 0
		? (uint64*)malloc(count * sizeof(uint64)) : NULL;
	if (snap == NULL || (count > 0 && (has == NULL || placement == NULL))) {
		free(snap);
		free(has);
		free(placement);
		return;
	}

	for (uint32 i = 0; i < count; i++) {
		has[i] = fTemplates[i].HasStaticOffset() ? 1 : 0;
		placement[i] = has[i]
			? fStaticBase + fTemplates[i].StaticOffset() : 0;
	}
	snap->count = count;
	snap->has = has;
	snap->placement = placement;
	__atomic_store_n(&sSnap, snap, __ATOMIC_RELEASE);
}


addr_t
TLSBlockTemplates::_GapFromThreadPointer() const
{
	addr_t threadPointer = (addr_t)tls_get(TLS_BASE_ADDRESS_SLOT);
	area_info info;
	ssize_t cookie = 0;
	while (_kern_get_next_area_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {
		addr_t start = (addr_t)info.address;
		addr_t end = start + info.size;
		if (threadPointer >= start && threadPointer < end) {
			addr_t gap = end - threadPointer;
			return (gap + 15) & ~(addr_t)15;
		}
	}

	// Past the 64 TLS slots when the stack area cannot be seen yet.
	return 4096;
}


bool
TLSBlockTemplates::_EnsureMapped(addr_t threadPointer, addr_t bytesFromTp)
{
	area_info info;
	ssize_t cookie = 0;
	while (_kern_get_next_area_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {
		addr_t start = (addr_t)info.address;
		addr_t end = start + info.size;
		if (threadPointer < start || threadPointer >= end)
			continue;

		addr_t need = threadPointer + bytesFromTp;
		if (need <= end)
			return true;

		size_t newSize = info.size + (need - end);
		newSize = (newSize + B_PAGE_SIZE - 1) & ~(B_PAGE_SIZE - 1);
		return _kern_resize_area(info.area, newSize) == B_OK;
	}
	return false;
}


bool
TLSBlockTemplates::HasStaticPlacement(unsigned dso) const
{
	RecursiveLocker locker(sTemplatesLock);
	return dso < fTemplates.size() && fTemplates[dso].HasStaticOffset();
}


uint64
TLSBlockTemplates::StaticPlacement(unsigned dso) const
{
	RecursiveLocker locker(sTemplatesLock);
	if (dso >= fTemplates.size() || !fTemplates[dso].HasStaticOffset())
		return fStaticBase;
	return fStaticBase + fTemplates[dso].StaticOffset();
}


bool
TLSBlockTemplates::Materialize(void** _readySnap)
{
	if (_readySnap != NULL)
		*_readySnap = NULL;

	TlsSnap* seen = load_snap();
	if (seen != NULL && thread_ready(seen)) {
		if (_readySnap != NULL)
			*_readySnap = seen;
		return true;
	}

	// Resize the stack area without the template lock. resize and the
	// area walk are syscalls; holding the lock across them made every
	// other thread's __tls_get_addr wait, including malloc.
	for (int attempt = 0; attempt < 4; attempt++) {
		addr_t threadPointer;
		addr_t bytes;
		{
			RecursiveLocker locker(sTemplatesLock);
			if (!fStaticBaseReady || fStaticNext <= 16) {
				if (_readySnap != NULL)
					*_readySnap = load_snap();
				return true;
			}
			threadPointer = (addr_t)tls_get(TLS_BASE_ADDRESS_SLOT);
			bytes = fStaticBase + fStaticNext;
		}

		if (!_EnsureMapped(threadPointer, bytes))
			return false;

		RecursiveLocker locker(sTemplatesLock);
		if (!fStaticBaseReady || fStaticNext <= 16) {
			if (_readySnap != NULL)
				*_readySnap = load_snap();
			return true;
		}
		if (fStaticBase + fStaticNext > bytes)
			continue;

		uint8* base = (uint8*)(threadPointer + fStaticBase);
		uint64 done = *(uint64*)base;
		unsigned count = fTemplates.size();
		for (unsigned i = 0; i < fTemplates.size(); i++) {
			if ((uint64)i < done)
				continue;
			TLSBlockTemplate& templ = fTemplates[i];
			if (!templ.HasStaticOffset())
				continue;
			if (!templ.IsRelocated()) {
				// Still being loaded: its image is copied by the next call on
				// this thread, from where it really is.
				count = i;
				break;
			}
			uint8* dest = base + templ.StaticOffset();
			if (templ.Address() != NULL && templ.FileSize() > 0)
				memcpy(dest, templ.Address(), templ.FileSize());
			if (templ.MemorySize() > templ.FileSize()) {
				memset(dest + templ.FileSize(), 0,
					templ.MemorySize() - templ.FileSize());
			}
		}
		*(uint64*)base = count;
		bool full = count == fTemplates.size();
		if (full && _readySnap != NULL)
			*_readySnap = load_snap();
		return full;
	}
	return false;
}


void
TLSBlockTemplates::SetRelocated(unsigned dso)
{
	RecursiveLocker locker(sTemplatesLock);
	if (dso < fTemplates.size())
		fTemplates[dso].SetRelocated();
}


void
tls_reinit_after_fork()
{
	// The child has only the forking thread; a lock another thread held is
	// free again. The copied static image is used again after the next
	// Materialize(), in case the parent forked mid-copy.
	recursive_lock_init(&sTemplatesLock, "runtime loader tls templates");
	int32 slot = __atomic_load_n(&sReadySlot, __ATOMIC_ACQUIRE);
	if (slot >= 0)
		tls_set(slot, NULL);
}


void
materialize_initial_exec_tls()
{
	void* ready = NULL;
	if (TLSBlockTemplates::Get().Materialize(&ready))
		mark_ready((TlsSnap*)ready);
}


uint64
r5_initial_exec_offset(unsigned dso, uint64 symbolOffset, int64 addend)
{
	uint64 placement = 0;
	if (dso != unsigned(-1))
		placement = TLSBlockTemplates::Get().StaticPlacement(dso);
	return r5_tpoff_offset(placement, symbolOffset, addend);
}

// SPDX-SnippetEnd

