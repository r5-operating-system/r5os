/*
 * Copyright 2011, Haiku Inc. All rights reserved.
 * This file may be used under the terms of the MIT License.
 *
 * Authors:
 *		Jérôme Duval
 */


#include "DataStream.h"

#include "Volume.h"
#include "exfat_update.h"


//#define TRACE_EXFAT
#ifdef TRACE_EXFAT
#	define TRACE(x...) dprintf("\33[34mexfat:\33[0m " x)
#else
#	define TRACE(x...) ;
#endif
#define ERROR(x...)	dprintf("\33[34mexfat:\33[0m " x)


DataStream::DataStream(Volume* volume, Inode* inode, off_t size)
	:
	kBlockSize(volume->BlockSize()),
	kClusterSize(volume->ClusterSize()),
	fVolume(volume),
	fInode(inode),
	fSize(size)
{
	fNumBlocks = size == 0 ? 0 : ((size - 1) / kBlockSize) + 1;
}


DataStream::~DataStream()
{
}


status_t
DataStream::FindBlock(off_t pos, off_t& physical, off_t *_length)
{
	if (pos >= fSize) {
		TRACE("FindBlock: offset larger than size\n");
		return B_ENTRY_NOT_FOUND;
	}
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	uint64 physicalByte = 0;
	uint64 run = 0;
	status_t status = exfat_map(fVolume->Store(), fVolume->Disk(),
		fInode->StartCluster(), (uint64)fInode->Size(), fInode->StreamFlags(),
		(uint64)pos, &physicalByte, &run);
	if (status != B_OK)
		return status;
	physical = (off_t)physicalByte;
	if (_length != NULL)
		*_length = (off_t)min_c(run, (uint64)(fSize - pos));
	// SPDX-SnippetEnd
	TRACE("inode %" B_PRIdINO ": pos %" B_PRIdOFF ", %" B_PRIdOFF "\n",
		fInode->ID(), pos, physical);
	return B_OK;
}

