/*
 * Copyright 2012, Alex Smith, alex@alex-smith.me.uk.
 * Copyright 2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */

/*
 * R5OS modifications in this file are MIT, like the original work. They are
 * wrapped in SPDX-Snippet markers. The copyright and license notice above still
 * cover the original work.
 */
#ifndef KERNEL_ARCH_X86_PAGING_64BIT_X86_VM_TRANSLATION_MAP_64BIT_H
#define KERNEL_ARCH_X86_PAGING_64BIT_X86_VM_TRANSLATION_MAP_64BIT_H


#include "paging/X86VMTranslationMap.h"


struct X86PagingStructures64Bit;


struct X86VMTranslationMap64Bit : X86VMTranslationMap {
								X86VMTranslationMap64Bit(bool la57);
	virtual						~X86VMTranslationMap64Bit();

			status_t			Init(bool kernel);

	virtual	size_t				MaxPagesNeededToMap(addr_t start,
									addr_t end) const;

	virtual	status_t			Map(addr_t virtualAddress,
									phys_addr_t physicalAddress,
									uint32 attributes, uint32 memoryType,
									vm_page_reservation* reservation);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	virtual	status_t			MapLarge(addr_t virtualAddress,
									phys_addr_t physicalAddress,
									uint32 attributes, uint32 memoryType,
									vm_page_reservation* reservation);
// SPDX-SnippetEnd
	virtual	status_t			Unmap(addr_t start, addr_t end);

	virtual	status_t			UnmapPage(VMArea* area, addr_t address,
									bool updatePageQueue,
									bool deletingAddressSpace, uint32* _flags);
	virtual	void				UnmapPages(VMArea* area, addr_t base,
									size_t size, bool updatePageQueue,
									bool deletingAddressSpace);

	virtual	status_t			Query(addr_t virtualAddress,
									phys_addr_t* _physicalAddress,
									uint32* _flags);
	virtual	status_t			QueryInterrupt(addr_t virtualAddress,
									phys_addr_t* _physicalAddress,
									uint32* _flags);

	virtual	status_t			Protect(addr_t base, addr_t top,
									uint32 attributes, uint32 memoryType);

	virtual	status_t			ClearFlags(addr_t virtualAddress,
									uint32 flags);

	virtual	bool				ClearAccessedAndModified(
									VMArea* area, addr_t address,
									bool unmapIfUnaccessed,
									bool& _modified);

	virtual	bool				DebugGetReverseMappingInfo(
									phys_addr_t physicalAddress,
									ReverseMappingInfoCallback& callback);

	virtual	X86PagingStructures* PagingStructures() const;
	inline	X86PagingStructures64Bit* PagingStructures64Bit() const
									{ return fPagingStructures; }

private:
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			void				_PushStandbyPageTable(
									phys_addr_t physicalPageTable);
			phys_addr_t			_PopStandbyPageTable();
			void				_FreeStandbyPageTable();
			uint64*				_SplitLargePage(uint64* pde,
									addr_t pdeVirtualBase,
									const char* reason);
// SPDX-SnippetEnd

			X86PagingStructures64Bit* fPagingStructures;
			bool				fLA57;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			phys_addr_t			fStandbyPageTableHead;
// SPDX-SnippetEnd
};


#endif	// KERNEL_ARCH_X86_PAGING_64BIT_X86_VM_TRANSLATION_MAP_64BIT_H
