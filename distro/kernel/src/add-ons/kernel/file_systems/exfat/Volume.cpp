/*
 * Copyright 2008-2010, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2011-2019, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2014 Haiku, Inc. All rights reserved.
 *
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *		Jérôme Duval, korli@users.berlios.de
 *		John Scipione, jscipione@gmail.com
 */


//! Superblock, mounting, etc.


#include "Volume.h"
#include "CachedBlock.h"
#include "DeviceOpener.h"
#include "Inode.h"
#include "Utility.h"


//#define TRACE_EXFAT
#ifdef TRACE_EXFAT
#	define TRACE(x...) dprintf("\33[34mexfat:\33[0m " x)
#else
#	define TRACE(x...) ;
#endif
#	define ERROR(x...) dprintf("\33[34mexfat:\33[0m " x)


//	#pragma mark - LabelVisitor


class LabelVisitor : public EntryVisitor {
public:
								LabelVisitor(Volume* volume);
			bool				VisitLabel(struct exfat_entry*);
private:
			Volume*				fVolume;
};


LabelVisitor::LabelVisitor(Volume* volume)
	:
	fVolume(volume)
{
}


bool
LabelVisitor::VisitLabel(struct exfat_entry* entry)
{
	TRACE("LabelVisitor::VisitLabel()\n");
	char name[B_FILE_NAME_LENGTH];
	status_t result = get_volume_name(entry, name, sizeof(name));
	if (result != B_OK)
		return false;

	fVolume->SetName(name);
	return true;
}


//	#pragma mark - exfat_super_block::IsValid()


bool
exfat_super_block::IsMagicValid()
{
	return strncmp(filesystem, EXFAT_SUPER_BLOCK_MAGIC, sizeof(filesystem)) == 0;
}


bool
exfat_super_block::IsValid()
{
	if (!IsMagicValid())
		return false;
	if (signature != 0xaa55)
		return false;
	if (jump_boot[0] != 0xeb || jump_boot[1] != 0x76 || jump_boot[2] != 0x90)
		return false;
	if (version_minor != 0 || version_major != 1)
		return false;
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	if (block_shift < 9 || block_shift > 12)
		return false;
	if (blocks_per_cluster_shift > (uint8)(25 - block_shift))
		return false;
	uint32 clusters = ClusterCount();
	if (clusters < 1 || clusters > 0xfffffff5u)
		return false;
	if (RootDirCluster() < 2 || (uint64)RootDirCluster() > (uint64)clusters + 1)
		return false;
	if (FirstFatBlock() >= FirstDataBlock())
		return false;
	if ((uint64)FirstFatBlock() + FatLength() > FirstDataBlock())
		return false;
	if ((uint64)FirstDataBlock()
			+ ((uint64)clusters << blocks_per_cluster_shift) > NumBlocks())
		return false;
	// SPDX-SnippetEnd

	return true;
}


//	#pragma mark - Volume


Volume::Volume(fs_volume* volume)
	:
	fFSVolume(volume),
	fFlags(0),
	fRootNode(NULL),
	fNextId(1)
{
	mutex_init(&fLock, "exfat volume");
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	mutex_init(&fAllocLock, "exfat allocation");
	memset(&fStore, 0, sizeof(fStore));
	memset(&fDisk, 0, sizeof(fDisk));
	// SPDX-SnippetEnd
	fInodesClusterTree = new InodesClusterTree;
	fInodesInoTree = new InodesInoTree;
	memset(fName, 0, sizeof(fName));
}


Volume::~Volume()
{
	TRACE("Volume destructor.\n");
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	while (struct node* node = fNodeTree.FindMin()) {
		fNodeTree.Remove(node);
		fInoTree.Remove(node);
		delete node;
	}
	// SPDX-SnippetEnd
	delete fInodesClusterTree;
	delete fInodesInoTree;
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	exfat_disk_close(&fDisk);
	mutex_destroy(&fAllocLock);
	mutex_destroy(&fLock);
	// SPDX-SnippetEnd
}


bool
Volume::IsValidSuperBlock()
{
	return fSuperBlock.IsValid();
}


const char*
Volume::Name() const
{
	return fName;
}


status_t
Volume::Mount(const char* deviceName, uint32 flags)
{
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// Read-write unless the device, the mount, or a failed check says otherwise.
	// SPDX-SnippetEnd

	if ((flags & B_MOUNT_READ_ONLY) != 0) {
		TRACE("Volume::Mount(): Read only\n");
	} else {
		TRACE("Volume::Mount(): Read write\n");
	}

	DeviceOpener opener(deviceName, (flags & B_MOUNT_READ_ONLY) != 0
		? O_RDONLY : O_RDWR);
	fDevice = opener.Device();
	if (fDevice < B_OK) {
		ERROR("Volume::Mount(): couldn't open device\n");
		return fDevice;
	}

	if (opener.IsReadOnly())
		fFlags |= VOLUME_READ_ONLY;

	// read the superblock
	status_t status = Identify(fDevice, &fSuperBlock);
	if (status != B_OK) {
		ERROR("Volume::Mount(): Identify() failed\n");
		return status;
	}

	fBlockSize = 1 << fSuperBlock.BlockShift();
	TRACE("block size %" B_PRIu32 "\n", fBlockSize);
	fEntriesPerBlock = (fBlockSize / sizeof(struct exfat_entry));

	// check that the device is large enough to hold the partition
	off_t deviceSize;
	status = opener.GetSize(&deviceSize);
	if (status != B_OK)
		return status;

	off_t partitionSize = (off_t)fSuperBlock.NumBlocks()
		<< fSuperBlock.BlockShift();
	if (deviceSize < partitionSize)
		return B_BAD_VALUE;

	fBlockCache = opener.InitCache(fSuperBlock.NumBlocks(), fBlockSize);
	if (fBlockCache == NULL)
		return B_ERROR;

	TRACE("Volume::Mount(): Initialized block cache: %p\n", fBlockCache);

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	status = FinishMount();
	if (status != B_OK)
		return status;
	// SPDX-SnippetEnd

	ino_t rootIno;
	// ready
	{
		Inode rootNode(this, fSuperBlock.RootDirCluster(), 0);
		rootIno = rootNode.ID();
	}

	status = get_vnode(fFSVolume, rootIno, (void**)&fRootNode);
	if (status != B_OK) {
		ERROR("could not create root node: get_vnode() failed!\n");
		return status;
	}

	TRACE("Volume::Mount(): Found root node: %" B_PRIdINO " (%s)\n",
		fRootNode->ID(), strerror(fRootNode->InitCheck()));

	// all went fine
	opener.Keep();

	DirectoryIterator iterator(fRootNode);
	LabelVisitor visitor(this);
	iterator.Iterate(visitor);

	if (fName[0] == '\0')
		get_default_volume_name(partitionSize, fName, sizeof(fName));

	return B_OK;
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
static bool is_media_gone(status_t status);
// SPDX-SnippetEnd


status_t
Volume::Unmount()
{
	TRACE("Volume::Unmount()\n");

	TRACE("Volume::Unmount(): Putting root node\n");
	put_vnode(fFSVolume, RootNode()->ID());
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	status_t syncStatus = SyncDisk(true);
	if (syncStatus != B_OK && is_media_gone(syncStatus))
		MarkReadOnly();
	// SPDX-SnippetEnd
	TRACE("Volume::Unmount(): Deleting the block cache\n");
	block_cache_delete(fBlockCache, !IsReadOnly());
	TRACE("Volume::Unmount(): Closing device\n");
	close(fDevice);

	TRACE("Volume::Unmount(): Done\n");
	return B_OK;
}


status_t
Volume::LoadSuperBlock()
{
	CachedBlock cached(this);
	const uint8* block = cached.SetTo(EXFAT_SUPER_BLOCK_OFFSET / fBlockSize);

	if (block == NULL)
		return B_IO_ERROR;

	memcpy(&fSuperBlock, block + EXFAT_SUPER_BLOCK_OFFSET % fBlockSize,
		sizeof(fSuperBlock));

	return B_OK;
}


status_t
Volume::ClusterToBlock(cluster_t cluster, fsblock_t &block)
{
	if ((cluster - EXFAT_FIRST_DATA_CLUSTER) >= SuperBlock().ClusterCount()
		|| cluster < EXFAT_FIRST_DATA_CLUSTER) {
		return B_BAD_VALUE;
	}
	block = ((fsblock_t)(cluster - EXFAT_FIRST_DATA_CLUSTER)
		<< SuperBlock().BlocksPerClusterShift())
		+ SuperBlock().FirstDataBlock();
	TRACE("Volume::ClusterToBlock() cluster %" B_PRIu32 " %u %" B_PRIu32 ": %"
		B_PRIu64 ", %" B_PRIu32 "\n", cluster,
		SuperBlock().BlocksPerClusterShift(), SuperBlock().FirstDataBlock(),
		block, SuperBlock().FirstFatBlock());
	return B_OK;
}


cluster_t
Volume::NextCluster(cluster_t _cluster)
{
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	if (_cluster < EXFAT_FIRST_DATA_CLUSTER
		|| (_cluster - EXFAT_FIRST_DATA_CLUSTER) >= SuperBlock().ClusterCount())
		return 0;
	// SPDX-SnippetEnd
	uint32 clusterPerBlock = fBlockSize / sizeof(cluster_t);
	CachedBlock block(this);
	fsblock_t blockNum = SuperBlock().FirstFatBlock()
		+ _cluster / clusterPerBlock;
	cluster_t *cluster = (cluster_t *)block.SetTo(blockNum);
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	if (cluster == NULL)
		return 0;
	// SPDX-SnippetEnd
	cluster += _cluster % clusterPerBlock;
	TRACE("Volume::NextCluster() cluster %" B_PRIu32 " next %" B_PRIu32 "\n",
		_cluster, B_LENDIAN_TO_HOST_INT32(*cluster));
	return B_LENDIAN_TO_HOST_INT32(*cluster);
}


Inode*
Volume::FindInode(ino_t id)
{
	return fInodesInoTree->Lookup(id);
}


Inode*
Volume::FindInode(cluster_t cluster)
{
	return fInodesClusterTree->Lookup(cluster);
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
ino_t
Volume::GetIno(cluster_t cluster, uint32 offset, ino_t parent,
	uint32 dirCluster, uint64 dirLength, uint8 dirFlags, bool dirRoot)
{
	struct node_key key;
	key.cluster = cluster;
	key.offset = offset;

	MutexLocker locker(fLock);
	struct node* node = fNodeTree.Lookup(key);
	if (node != NULL) {
		if (dirLength != 0 || dirRoot || dirCluster >= 2) {
			node->dir_cluster = dirCluster;
			node->dir_length = dirLength;
			node->dir_flags = dirFlags;
			node->dir_root = dirRoot;
		}
		TRACE("Volume::GetIno() cached cluster %" B_PRIu32 " offset %" B_PRIu32
			" ino %" B_PRIdINO "\n", cluster, offset, node->ino);
		return node->ino;
	}
	node = new(std::nothrow) struct node();
	if (node == NULL)
		return 0;
	node->key = key;
	node->ino = _NextID();
	node->parent = parent;
	node->dir_cluster = dirCluster;
	node->dir_length = dirLength;
	node->dir_flags = dirFlags;
	node->dir_root = dirRoot;
	if (!fNodeTree.Insert(node)) {
		struct node* existing = fNodeTree.Lookup(key);
		delete node;
		return existing != NULL ? existing->ino : 0;
	}
	if (!fInoTree.Insert(node)) {
		fNodeTree.Remove(node);
		delete node;
		return 0;
	}
	TRACE("Volume::GetIno() new cluster %" B_PRIu32 " offset %" B_PRIu32
		" ino %" B_PRIdINO "\n", cluster, offset, node->ino);
	return node->ino;
}


bool
Volume::GetNode(ino_t ino, struct node_key& key, ino_t& parent,
	uint32& dirCluster, uint64& dirLength, uint8& dirFlags, bool& dirRoot)
{
	MutexLocker locker(fLock);
	struct node* node = fInoTree.Lookup(ino);
	if (node == NULL)
		return false;
	key = node->key;
	parent = node->parent;
	dirCluster = node->dir_cluster;
	dirLength = node->dir_length;
	dirFlags = node->dir_flags;
	dirRoot = node->dir_root;
	return true;
}


void
Volume::SetDir(ino_t ino, uint32 dirCluster, uint64 dirLength, uint8 dirFlags,
	bool dirRoot)
{
	MutexLocker locker(fLock);
	struct node* node = fInoTree.Lookup(ino);
	if (node == NULL)
		return;
	node->dir_cluster = dirCluster;
	node->dir_length = dirLength;
	node->dir_flags = dirFlags;
	node->dir_root = dirRoot;
}
// SPDX-SnippetEnd


//	#pragma mark - Disk scanning and initialization


/*static*/ status_t
Volume::Identify(int fd, exfat_super_block* superBlock)
{
	if (read_pos(fd, EXFAT_SUPER_BLOCK_OFFSET, superBlock,
			sizeof(exfat_super_block)) != sizeof(exfat_super_block))
		return B_IO_ERROR;

	if (!superBlock->IsMagicValid())
		return B_BAD_VALUE;

	if (!superBlock->IsValid()) {
		ERROR("invalid superblock!\n");
		return B_BAD_VALUE;
	}

	return B_OK;
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT

static status_t
exfat_store_read(void* cookie, uint64 block, void* buffer, uint32 blocks)
{
	Volume* volume = (Volume*)cookie;
	uint8* out = (uint8*)buffer;
	for (uint32 index = 0; index < blocks; index++) {
		const void* data = block_cache_get(volume->BlockCache(),
			(off_t)(block + index));
		if (data == NULL)
			return B_IO_ERROR;
		memcpy(out, data, volume->BlockSize());
		block_cache_put(volume->BlockCache(), (off_t)(block + index));
		out += volume->BlockSize();
	}
	return B_OK;
}


static status_t
exfat_store_write(void* cookie, uint64 block, const void* buffer, uint32 blocks)
{
	Volume* volume = (Volume*)cookie;
	const uint8* in = (const uint8*)buffer;
	for (uint32 index = 0; index < blocks; index++) {
		void* data = block_cache_get_writable(volume->BlockCache(),
			(off_t)(block + index), -1);
		if (data == NULL)
			return B_IO_ERROR;
		memcpy(data, in, volume->BlockSize());
		block_cache_set_dirty(volume->BlockCache(), (off_t)(block + index),
			true, -1);
		block_cache_put(volume->BlockCache(), (off_t)(block + index));
		in += volume->BlockSize();
	}
	return B_OK;
}


static status_t
exfat_store_flush(void* cookie, uint64 block, uint32 blocks)
{
	Volume* volume = (Volume*)cookie;
	if (blocks == 0)
		return block_cache_sync(volume->BlockCache());
	return block_cache_sync_etc(volume->BlockCache(), (off_t)block, blocks);
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
status_t
Volume::FlushDrive()
{
	// ioctl() here is the libroot wrapper: a failure is -1 and the status
	// is in errno. fd_ioctl() also turns B_DEV_INVALID_IOCTL into ENOTTY
	// before that return, so the driver code itself never arrives.
	int result = ioctl(fDevice, B_FLUSH_DRIVE_CACHE);
	if (result == 0)
		return B_OK;
	status_t error = result == -1 ? errno : (status_t)result;
	if (error == B_DEV_INVALID_IOCTL || error == ENOTTY
		|| error == B_UNSUPPORTED || error == B_NOT_SUPPORTED
		|| error == B_BAD_VALUE)
		return B_OK;
	return error != 0 ? error : B_ERROR;
}


static status_t
exfat_store_drive_flush(void* cookie)
{
	return ((Volume*)cookie)->FlushDrive();
}
// SPDX-SnippetEnd


status_t
Volume::FinishMount()
{
	fStore.cookie = this;
	fStore.read = exfat_store_read;
	fStore.write = exfat_store_write;
	fStore.flush = exfat_store_flush;
	fStore.drive_flush = exfat_store_drive_flush;

	uint8* boot = (uint8*)malloc(BlockSize());
	if (boot == NULL)
		return B_NO_MEMORY;
	status_t status = exfat_store_read(this, 0, boot, 1);
	if (status == B_OK)
		status = exfat_disk_open(&fStore, boot, &fDisk);
	free(boot);
	if (status != B_OK)
		return status;
	if (!fDisk.can_write)
		fFlags |= VOLUME_READ_ONLY;
	if (fDisk.read_only_reason[0] != '\0')
		dprintf("exfat: %s, mounting read-only\n", fDisk.read_only_reason);
	return B_OK;
}


extern status_t exfat_commit_inode(Inode* inode, bool flushDrive);

static bool
is_media_gone(status_t status)
{
	return status == B_DEV_NO_MEDIA || status == B_DEV_NOT_READY
		|| status == ENODEV || status == B_FILE_ERROR
		|| status == B_IO_ERROR || status == B_DEV_MEDIA_CHANGED
		|| status == B_DEV_WRITE_ERROR || status == B_DEV_READ_ERROR;
}


void
Volume::RegisterInode(Inode* inode)
{
	if (inode == NULL || inode->ID() == 0)
		return;
	MutexLocker locker(fLock);
	if (fInodesInoTree->Lookup(inode->ID()) == NULL)
		fInodesInoTree->Insert(inode);
}


void
Volume::UnregisterInode(Inode* inode)
{
	if (inode == NULL || inode->ID() == 0)
		return;
	MutexLocker locker(fLock);
	if (fInodesInoTree->Lookup(inode->ID()) == inode)
		fInodesInoTree->Remove(inode);
}


status_t
Volume::SyncOpenInodes()
{
	status_t result = B_OK;
	for (;;) {
		ino_t batch[32];
		size_t count = 0;
		{
			MutexLocker locker(fLock);
			InodesInoTree::Iterator it = fInodesInoTree->GetIterator();
			while (Inode* inode = it.Next()) {
				if (inode->NeedsCommit()) {
					batch[count++] = inode->ID();
					if (count == sizeof(batch) / sizeof(batch[0]))
						break;
				}
			}
		}
		if (count == 0)
			break;
		for (size_t i = 0; i < count; i++) {
			Inode* inode = NULL;
			if (get_vnode(fFSVolume, batch[i], (void**)&inode) != B_OK)
				continue;
			status_t st = exfat_commit_inode(inode, false);
			put_vnode(fFSVolume, batch[i]);
			if (st != B_OK) {
				if (is_media_gone(st)) {
					MarkReadOnly();
					return st;
				}
				if (result == B_OK)
					result = st;
			}
		}
		if (result != B_OK)
			break;
	}
	return result;
}


status_t
Volume::SyncDisk(bool unmounting)
{
	if (IsReadOnly())
		return B_OK;
	if (!unmounting) {
		status_t openStatus = SyncOpenInodes();
		if (openStatus != B_OK)
			return openStatus;
	}
	MutexLocker allocation(fAllocLock);
	if (IsReadOnly() || !fDisk.dirty)
		return B_OK;
	status_t status = exfat_sync(&fStore, &fDisk);
	if (is_media_gone(status)) {
		dprintf("exfat: media removed during sync (%s), marking read-only\n",
			strerror(status));
		MarkReadOnly();
		return status;
	}
	if (status != B_OK)
		dprintf("exfat: sync failed, volume stays dirty\n");
	return status;
}


status_t
Volume::MoveNode(ino_t ino, cluster_t cluster, uint32 offset, ino_t parent)
{
	MutexLocker locker(fLock);
	struct node* node = fInoTree.Lookup(ino);
	if (node == NULL)
		return B_ENTRY_NOT_FOUND;
	struct node_key oldKey = node->key;
	ino_t oldParent = node->parent;
	fNodeTree.Remove(node);
	node->key.cluster = cluster;
	node->key.offset = offset;
	node->parent = parent;
	struct node* occupying = fNodeTree.Lookup(node->key);
	if (occupying != NULL && occupying != node) {
		fInoTree.Remove(occupying);
		fNodeTree.Remove(occupying);
		delete occupying;
	}
	if (!fNodeTree.Insert(node)) {
		node->key = oldKey;
		node->parent = oldParent;
		if (!fNodeTree.Insert(node)) {
			fInoTree.Remove(node);
			delete node;
		}
		return B_ERROR;
	}
	return B_OK;
}


void
Volume::ForgetIno(ino_t ino)
{
	MutexLocker locker(fLock);
	struct node* node = fInoTree.Lookup(ino);
	if (node == NULL)
		return;
	fNodeTree.Remove(node);
	fInoTree.Remove(node);
	delete node;
}


void
Volume::MarkReadOnly()
{
	atomic_or(&fFlags, VOLUME_READ_ONLY);
	fDisk.can_write = false;
}


uint64
Volume::ChainBytes(cluster_t start)
{
	uint64 bytes = 0;
	cluster_t cluster = start;
	uint32 guard = SuperBlock().ClusterCount() + 2;
	while (guard > 0) {
		guard--;
		if (cluster < 2 || cluster >= 0xfffffff8)
			break;
		bytes += ClusterSize();
		cluster_t next = NextCluster(cluster);
		if (next < 2 || next >= 0xfffffff8 || next == cluster)
			break;
		cluster = next;
	}
	return bytes;
}

// SPDX-SnippetEnd
