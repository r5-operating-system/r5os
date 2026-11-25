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
	cluster_t cluster = fInode->StartCluster();
	if (cluster < 2)
		return B_BAD_DATA;
	uint64 clusterIndex = (uint64)pos / kClusterSize;
	uint64 within = (uint64)pos % kClusterSize;
	if (fInode->IsContiguous())
		cluster = (cluster_t)(cluster + clusterIndex);
	else {
		for (uint64 step = 0; step < clusterIndex; step++) {
			cluster_t next = fInode->NextCluster(cluster);
			if (next < 2)
				return B_BAD_DATA;
			cluster = next;
		}
	}
	exfat_disk* disk = fVolume->Disk();
	if (cluster < 2 || cluster > disk->cluster_count + 1)
		return B_BAD_DATA;
	uint32 blocksPerCluster = kClusterSize / kBlockSize;
	uint64 block = disk->data_block
		+ (uint64)(cluster - 2) * blocksPerCluster + within / kBlockSize;
	physical = (off_t)block;
	if (_length != NULL)
		*_length = (off_t)min_c(kClusterSize - within, (uint64)(fSize - pos));
	// SPDX-SnippetEnd
	TRACE("inode %" B_PRIdINO ": pos %" B_PRIdOFF ", %" B_PRIdOFF "\n",
		fInode->ID(), pos, physical);
	return B_OK;
}

