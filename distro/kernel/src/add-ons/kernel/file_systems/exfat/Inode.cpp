/*
 * Copyright 2011, Jérôme Duval, korli@users.berlios.de.
 * Copyright 2008-2014, Axel Dörfler, axeld@pinc-software.de.
 * This file may be used under the terms of the MIT License.
 */


#include "Inode.h"
#include "CachedBlock.h"
#include "DataStream.h"
#include "Utility.h"
#include "exfat_update.h"


#undef ASSERT
//#define TRACE_EXFAT
#ifdef TRACE_EXFAT
#	define TRACE(x...) dprintf("\33[34mexfat:\33[0m " x)
#	define ASSERT(x) { if (!(x)) kernel_debugger("exfat: assert failed: " #x "\n"); }
#else
#	define TRACE(x...) ;
#	define ASSERT(x) ;
#endif
#define ERROR(x...) dprintf("\33[34mexfat:\33[0m " x)


Inode::Inode(Volume* volume, cluster_t cluster, uint32 offset)
	:
	fVolume(volume),
	fID(volume->GetIno(cluster, offset, 0)),
	fCluster(cluster),
	fOffset(offset),
	fCache(NULL),
	fMap(NULL)
{
	TRACE("Inode::Inode(%" B_PRIu32 ", %" B_PRIu32 ") inode %" B_PRIdINO "\n",
		Cluster(), Offset(), ID());
	_Init();

	if (ID() == 1) {
		fFileEntry.file.SetAttribs(EXFAT_ENTRY_ATTRIB_SUBDIR);
		fFileEntry.file_info.SetStartCluster(Cluster());
		fFileInfoEntry.file_info.SetStartCluster(Cluster());
		fFileInfoEntry.file_info.SetFlag(0);
		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		uint64 bytes = fVolume->ChainBytes(Cluster());
		fFileInfoEntry.file_info.size1 = B_HOST_TO_LENDIAN_INT64(bytes);
		fFileInfoEntry.file_info.size2 = fFileInfoEntry.file_info.size1;
		fCommittedValid = bytes;
		// SPDX-SnippetEnd
	} else {
		fInitStatus = UpdateNodeFromDisk();
		if (fInitStatus == B_OK && !IsDirectory() && !IsSymLink()) {
			fCache = file_cache_create(fVolume->ID(), ID(), Size());
			fMap = file_map_create(fVolume->ID(), ID(), Size());
		}
	}
	TRACE("Inode::Inode(%" B_PRIdINO ") end\n", ID());
}


Inode::Inode(Volume* volume, ino_t ino)
	:
	fVolume(volume),
	fID(ino),
	fCluster(0),
	fOffset(0),
	fCache(NULL),
	fMap(NULL),
	fInitStatus(B_NO_INIT)
{
	_Init();
	struct node_key key;
	if (volume->GetNode(ino, key, fParent, fDirCluster, fDirLength, fDirFlags,
			fDirRoot)) {
		fCluster = key.cluster;
		fOffset = key.offset;
		fInitStatus = B_OK;
	}
	TRACE("Inode::Inode(%" B_PRIdINO ") cluster %" B_PRIu32 "\n", ID(),
		Cluster());

	if (fInitStatus == B_OK && ID() != 1) {
		fInitStatus = UpdateNodeFromDisk();
		if (!IsDirectory() && !IsSymLink()) {
			fCache = file_cache_create(fVolume->ID(), ID(), Size());
			fMap = file_map_create(fVolume->ID(), ID(), Size());
		}
	} else if (fInitStatus == B_OK && ID() == 1) {
		fFileEntry.file.SetAttribs(EXFAT_ENTRY_ATTRIB_SUBDIR);
		fFileInfoEntry.file_info.SetStartCluster(Cluster());
		fFileInfoEntry.file_info.SetFlag(0);
		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		uint64 bytes = fVolume->ChainBytes(Cluster());
		fFileInfoEntry.file_info.size1 = B_HOST_TO_LENDIAN_INT64(bytes);
		fFileInfoEntry.file_info.size2 = fFileInfoEntry.file_info.size1;
		fCommittedValid = bytes;
		// SPDX-SnippetEnd
	}
}


Inode::Inode(Volume* volume)
	:
	fVolume(volume),
	fID(0),
	fCache(NULL),
	fMap(NULL),
	fInitStatus(B_NO_INIT)
{
	_Init();
}


Inode::~Inode()
{
	TRACE("Inode destructor\n");
	file_cache_delete(FileCache());
	file_map_delete(Map());
	mutex_destroy(&fWriterLock);
	TRACE("Inode destructor: Done\n");
}


status_t
Inode::InitCheck()
{
	return fInitStatus;
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
status_t
Inode::UpdateNodeFromDisk()
{
	if (ID() == 1)
		return B_OK;
	exfat_node dir;
	memset(&dir, 0, sizeof(dir));
	dir.first_cluster = fDirCluster;
	dir.data_length = fDirLength;
	dir.valid_length = fDirLength;
	dir.flags = fDirFlags;
	dir.root = fDirRoot;
	dir.is_dir = true;
	if (!dir.root && (dir.first_cluster < 2 || dir.data_length == 0))
		return B_BAD_DATA;
	exfat_node found;
	status_t status = exfat_read_entry(fVolume->Store(), fVolume->Disk(), &dir,
		fOffset, &found);
	if (status != B_OK) {
		TRACE("exfat: load ino %" B_PRIdINO " index %" B_PRIu32
			" dir cluster %" B_PRIu32 ": %s\n", ID(), fOffset, fDirCluster,
			strerror(status));
		return status;
	}
	memcpy(&fFileEntry, found.primary, 32);
	fFileInfoEntry.file_info.SetStartCluster(found.first_cluster);
	fFileInfoEntry.file_info.SetFlag(found.flags);
	fFileInfoEntry.file_info.size1 = B_HOST_TO_LENDIAN_INT64(found.valid_length);
	fFileInfoEntry.file_info.size2 = B_HOST_TO_LENDIAN_INT64(found.data_length);
	fFileInfoEntry.file_info.name_hash = B_HOST_TO_LENDIAN_INT16(found.name_hash);
	fCommittedValid = found.valid_length;
	return B_OK;
}
// SPDX-SnippetEnd


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
void
Inode::WriteLock()
{
	thread_id self = find_thread(NULL);
	if (atomic_get(&fWriter) == self) {
		fWriteDepth++;
		return;
	}
	mutex_lock(&fWriterLock);
	atomic_set(&fWriter, self);
	fWriteDepth = 1;
}


void
Inode::WriteUnlock()
{
	if (fWriteDepth > 1) {
		fWriteDepth--;
		return;
	}
	fWriteDepth = 0;
	atomic_set(&fWriter, -1);
	mutex_unlock(&fWriterLock);
}


void
Inode::MetaLock()
{
	rw_lock_write_lock(&fLock);
}


void
Inode::MetaUnlock()
{
	rw_lock_write_unlock(&fLock);
}


void
Inode::ReadLock()
{
	rw_lock_read_lock(&fLock);
}


void
Inode::ReadUnlock()
{
	rw_lock_read_unlock(&fLock);
}


void
Inode::ClearChainHint()
{
	atomic_set64(&fChainHint, -1);
}


bool
Inode::RecallChain(uint64 index, cluster_t& cluster, uint64& from)
{
	if (index > 0xffffffff)
		return false;
	int64 hint = atomic_get64(&fChainHint);
	if (hint < 0)
		return false;
	uint64 hintIndex = (uint64)(uint32)(hint >> 32);
	cluster_t hintCluster = (cluster_t)(uint32)hint;
	if (hintIndex > index || hintCluster < 2)
		return false;
	cluster = hintCluster;
	from = hintIndex;
	return true;
}


void
Inode::RememberChain(uint64 index, cluster_t cluster)
{
	if (index > 0xffffffff)
		return;
	int64 packed = ((int64)(uint32)index << 32) | (uint32)cluster;
	atomic_set64(&fChainHint, packed);
}
// SPDX-SnippetEnd


cluster_t
Inode::NextCluster(cluster_t cluster) const
{
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	if (!IsContiguous())
		return GetVolume()->NextCluster(cluster);
	// SPDX-SnippetEnd
	return cluster + 1;
}


mode_t
Inode::Mode() const
{
	mode_t mode = S_IRUSR | S_IRGRP | S_IROTH;
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	if (!fVolume->IsReadOnly()
		&& (fFileEntry.file.Attribs() & 0x01) == 0)
		mode |= S_IWUSR | S_IWGRP | S_IWOTH;
	// SPDX-SnippetEnd
	if (fFileEntry.file.Attribs() & EXFAT_ENTRY_ATTRIB_SUBDIR)
		mode |= S_IFDIR | S_IXUSR | S_IXGRP | S_IXOTH;
	else
		mode |= S_IFREG;
	return mode;
}


status_t
Inode::CheckPermissions(int accessMode) const
{
	// you never have write access to a read-only volume
	if ((accessMode & W_OK) != 0 && fVolume->IsReadOnly())
		return B_READ_ONLY_DEVICE;

	return check_access_permissions(accessMode, Mode(), (gid_t)GroupID(),
		(uid_t)UserID());
}


status_t
Inode::FindBlock(off_t pos, off_t& physical, off_t *_length, off_t maxRun)
{
	DataStream stream(fVolume, this, Size());
	return stream.FindBlock(pos, physical, _length, maxRun);
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
void
Inode::EnsureCache()
{
	if (fCache != NULL || IsDirectory() || IsSymLink() || ID() == 1)
		return;
	fCache = file_cache_create(fVolume->ID(), ID(), Size());
	if (fMap == NULL)
		fMap = file_map_create(fVolume->ID(), ID(), Size());
}
// SPDX-SnippetEnd


status_t
Inode::ReadAt(off_t pos, uint8* buffer, size_t* _length)
{
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	if (FileCache() == NULL)
		return B_BAD_VALUE;
	// SPDX-SnippetEnd
	return file_cache_read(FileCache(), pos, buffer, _length);
}


bool
Inode::VisitFile(struct exfat_entry* entry)
{
	fFileEntry = *entry;
	return false;
}


bool
Inode::VisitFileInfo(struct exfat_entry* entry)
{
	fFileInfoEntry = *entry;
	return false;
}


void
Inode::_Init()
{
	memset(&fFileEntry, 0, sizeof(fFileEntry));
	memset(&fFileInfoEntry, 0, sizeof(fFileInfoEntry));
	rw_lock_init(&fLock, "exfat inode");
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	fDeleted = false;
	mutex_init(&fWriterLock, "exfat writer");
	atomic_set(&fWriter, -1);
	fWriteDepth = 0;
	fCommittedValid = 0;
	atomic_set64(&fChainHint, -1);
	fDirCluster = 0;
	fDirLength = 0;
	fDirFlags = 0;
	fDirRoot = false;
	// SPDX-SnippetEnd
}


// If divisible by 4, but not divisible by 100, but divisible by 400, it's a leap year
// 1996 is leap, 1900 is not, 2000 is, 2100 is not
#define IS_LEAP_YEAR(y) ((((y) % 4) == 0) && (((y) % 100) || ((((y)) % 400) == 0)))

/* returns leap days since 1970 */
static int leaps(int yr, int mon)
{
	// yr is 1970-based, mon 0-based
	int result = (yr+2)/4 - (yr + 70) / 100;
	if((yr+70) >= 100) result++; // correct for 2000
	if (IS_LEAP_YEAR(yr + 1970))
		if (mon < 2) result--;
	return result;
}

static int daze[] = { 0,0,31,59,90,120,151,181,212,243,273,304,334,0,0,0 };

void
Inode::_GetTimespec(uint16 date, uint16 time, struct timespec &timespec) const
{
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	int32 tzoffset = 0;
	// SPDX-SnippetEnd
#ifdef _KERNEL_MODE
	tzoffset = static_cast<int32>(get_timezone_offset());
#elif defined USER
	time_t localTime;
	time(&localTime);

	struct tm localTm;
	localtime_r(&localTime, &localTm);
	tzoffset = localTm.tm_gmtoff;
#endif

	time_t days = daze[(date >> 5) & 15] + ((date >> 9) + 10) * 365
		+ leaps((date >> 9) + 10, ((date >> 5) & 15) - 1) + (date & 31) -1;

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// get_timezone_offset() is seconds. The stamp is local time.
	timespec.tv_sec = ((days * 24 + (time >> 11)) * 60 + ((time >> 5) & 63))
		* 60 + 2 * (time & 31) - tzoffset;
	timespec.tv_nsec = 0;
	// SPDX-SnippetEnd
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
void
Inode::AdoptStream(uint32 first, uint64 dataLength, uint64 validLength,
	uint8 flags, uint16 attributes, uint8 chunks, cluster_t entryCluster,
	uint32 entryOffset, uint16 nameHash)
{
	fFileInfoEntry.file_info.SetStartCluster(first);
	fFileInfoEntry.file_info.SetFlag(flags);
	fFileInfoEntry.file_info.size1 = B_HOST_TO_LENDIAN_INT64(validLength);
	fFileInfoEntry.file_info.size2 = B_HOST_TO_LENDIAN_INT64(dataLength);
	fFileInfoEntry.file_info.name_hash = B_HOST_TO_LENDIAN_INT16(nameHash);
	fFileEntry.file.SetAttribs(attributes);
	fFileEntry.file.chunkCount = chunks;
	fCommittedValid = validLength;
	ClearChainHint();
	if (ID() != 1) {
		fCluster = entryCluster;
		fOffset = entryOffset;
	}
}


void
Inode::ApplyCacheSize(off_t invalidateFrom)
{
	if (FileCache() == NULL)
		return;
	off_t bytes = Size();
	file_cache_set_size(FileCache(), bytes);
	file_map_set_size(Map(), bytes);
	off_t from = 0;
	off_t span = bytes;
	if (invalidateFrom >= 0) {
		from = invalidateFrom < bytes ? invalidateFrom : bytes;
		off_t to = invalidateFrom > bytes ? invalidateFrom : bytes;
		span = to - from;
	}
	if (span > 0)
		file_map_invalidate(Map(), from, span);
}
// SPDX-SnippetEnd


