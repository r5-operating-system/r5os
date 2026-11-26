/*
 * Copyright 2011, Jérôme Duval, korli@users.berlios.de.
 * Copyright 2008, Axel Dörfler, axeld@pinc-software.de.
 * This file may be used under the terms of the MIT License.
 */
#ifndef INODE_H
#define INODE_H


#include "DirectoryIterator.h"
#include "exfat.h"
#include "Volume.h"
#include "system_dependencies.h"


//#define TRACE_EXFAT
#ifdef TRACE_EXFAT
#	define TRACEI(x...) dprintf("\33[34mexfat:\33[0m " x)
#else
#	define TRACEI(x...) ;
#endif


struct InodesTreeDefinition;


class Inode : EntryVisitor {
public:
						Inode(Volume* volume, cluster_t cluster,
							uint32 offset);
						Inode(Volume* volume, ino_t ino);
						~Inode();

			status_t	InitCheck();

			ino_t		ID() const { return fID; }
			ino_t		Parent() const { return fParent; }
			cluster_t	Cluster() const { return fCluster; }
			uint32		Offset() const { return fOffset; }
			cluster_t	StartCluster() const
							{ return fFileInfoEntry.file_info.StartCluster(); }
			bool		IsContiguous() const
							{ return fFileInfoEntry.file_info.IsContiguous(); }
			cluster_t	NextCluster(cluster_t cluster) const;

			rw_lock*	Lock() { return &fLock; }
			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			void		WriteLock();
			void		WriteUnlock();
			void		MetaLock();
			void		MetaUnlock();
			void		ReadLock();
			void		ReadUnlock();

			status_t	UpdateNodeFromDisk();
			void		EnsureCache();
			// SPDX-SnippetEnd

			bool		IsDirectory() const
							{ return S_ISDIR(Mode()); }
			bool		IsFile() const
							{ return S_ISREG(Mode()); }
			bool		IsSymLink() const
							{ return S_ISLNK(Mode()); }
			status_t	CheckPermissions(int accessMode) const;

			mode_t		Mode() const;
			off_t		Size() const { return fFileInfoEntry.file_info.Size(); }
			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			off_t		ValidSize() const
							{ return B_LENDIAN_TO_HOST_INT64(
								fFileInfoEntry.file_info.size1); }
			uint8		StreamFlags() const
							{ return fFileInfoEntry.file_info.flag; }
			uint16		Attributes() const
							{ return fFileEntry.file.Attribs(); }
			uint8		ChunkCount() const
							{ return fFileEntry.file.chunkCount; }
			uint16		NameHash() const
							{ return B_LENDIAN_TO_HOST_INT16(
								fFileInfoEntry.file_info.name_hash); }
			void		SetValidLength(uint64 valid) {
							fFileInfoEntry.file_info.size1
								= B_HOST_TO_LENDIAN_INT64(valid);
						}
			uint64		CommittedValid() const { return fCommittedValid; }
			void		SetCommittedValid(uint64 valid)
							{ fCommittedValid = valid; }
			void		ClearChainHint();
			bool		RecallChain(uint64 index, cluster_t& cluster,
							uint64& from);
			void		RememberChain(uint64 index, cluster_t cluster);
			bool		IsDeleted() const { return fDeleted; }
			void		SetDeleted() { fDeleted = true; }
			void		SetParent(ino_t parent) { fParent = parent; }
			uint32		LastCluster() const { return fLastCluster; }
			uint32		LastIndex() const { return fLastIndex; }
			void		SetLastCluster(uint32 cluster, uint32 index)
							{ fLastCluster = cluster; fLastIndex = index; }
			bool		IsDataDirty() const { return fDataDirty; }
			void		SetDataDirty(bool dirty) { fDataDirty = dirty; }
			bool		NeedsCommit() const
							{ return IsFile() && !IsDeleted()
								&& (fDataDirty
									|| (uint64)ValidSize() != fCommittedValid); }
			void		ApplyCacheSize(off_t invalidateFrom = -1);
			void		AdoptStream(uint32 first, uint64 dataLength,
							uint64 validLength, uint8 flags, uint16 attributes,
							uint8 chunks, cluster_t entryCluster,
							uint32 entryOffset, uint16 nameHash,
							bool keepValid = false,
							const uint8* primary = NULL);
			// SPDX-SnippetEnd
			uid_t		UserID() const { return 0;/*fNode.UserID();*/ }
			gid_t		GroupID() const { return 0;/*fNode.GroupID();*/ }
			void		GetChangeTime(struct timespec &timespec) const
							{ GetModificationTime(timespec); }
			void		GetModificationTime(struct timespec &timespec) const
							{ _GetTimespec(fFileEntry.file.ModificationDate(),
								fFileEntry.file.ModificationTime(),
								fFileEntry.file.modification_time_low,
								fFileEntry.file.reserved2[1], timespec); }
			void		GetCreationTime(struct timespec &timespec) const
							{ _GetTimespec(fFileEntry.file.CreationDate(),
								fFileEntry.file.CreationTime(),
								fFileEntry.file.creation_time_low,
								fFileEntry.file.reserved2[0], timespec); }
			void		GetAccessTime(struct timespec &timespec) const
							{ _GetTimespec(fFileEntry.file.AccessDate(),
								fFileEntry.file.AccessTime(), 0,
								fFileEntry.file.reserved2[2], timespec); }

			Volume*		GetVolume() const { return fVolume; }

			status_t	FindBlock(off_t logical, off_t& physical,
							off_t *_length = NULL, off_t maxRun = 0);
			status_t	ReadAt(off_t pos, uint8 *buffer, size_t *length);
			status_t	FillGapWithZeros(off_t start, off_t end);

			void*		FileCache() const { return fCache; }
			void*		Map() const { return fMap; }

			bool		VisitFile(struct exfat_entry*);
			bool		VisitFileInfo(struct exfat_entry*);
	
private:
			friend struct InodesInoTreeDefinition;
			friend struct InodesClusterTreeDefinition;
 
						Inode(Volume* volume);
						Inode(const Inode&);
						Inode &operator=(const Inode&);
							// no implementation

			void		_GetTimespec(uint16 date, uint16 time, uint8 tenMs,
							uint8 utcOffset, struct timespec &timespec) const;
			void		_Init();

			rw_lock		fLock;
			::Volume*	fVolume;
			ino_t		fID;
			ino_t		fParent;
			cluster_t 	fCluster;
			uint32		fOffset;
			uint32		fFlags;
			void*		fCache;
			void*		fMap;
			status_t	fInitStatus;
			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			bool		fDeleted;
			bool		fDataDirty;
			mutex		fWriterLock;
			int32		fWriter;
			int32		fWriteDepth;
			uint64		fCommittedValid;
			int64		fChainHint;
			uint32		fLastCluster;
			uint32		fLastIndex;
			uint32		fDirCluster;
			uint64		fDirLength;
			uint8		fDirFlags;
			bool		fDirRoot;
			// SPDX-SnippetEnd

			SplayTreeLink<Inode> fInoTreeLink;
			Inode*				fInoTreeNext;
			SplayTreeLink<Inode> fClusterTreeLink;
			Inode*				fClusterTreeNext;

			struct exfat_entry	fFileEntry;
			struct exfat_entry	fFileInfoEntry;
};


// The Vnode class provides a convenience layer upon get_vnode(), so that
// you don't have to call put_vnode() anymore, which may make code more
// readable in some cases

class Vnode {
public:
	Vnode(Volume* volume, ino_t id)
		:
		fInode(NULL)
	{
		SetTo(volume, id);
	}

	Vnode()
		:
		fStatus(B_NO_INIT),
		fInode(NULL)
	{
	}

	~Vnode()
	{
		Unset();
	}

	status_t InitCheck()
	{
		return fStatus;
	}

	void Unset()
	{
		if (fInode != NULL) {
			put_vnode(fInode->GetVolume()->FSVolume(), fInode->ID());
			fInode = NULL;
			fStatus = B_NO_INIT;
		}
	}

	status_t SetTo(Volume* volume, ino_t id)
	{
		Unset();

		return fStatus = get_vnode(volume->FSVolume(), id, (void**)&fInode);
	}

	status_t Get(Inode** _inode)
	{
		*_inode = fInode;
		return fStatus;
	}

	void Keep()
	{
		TRACEI("Vnode::Keep()\n");
		fInode = NULL;
	}

private:
	status_t	fStatus;
	Inode*		fInode;
};


struct InodesInoTreeDefinition {
	typedef ino_t KeyType;
	typedef	Inode NodeType;

	static KeyType GetKey(const NodeType* node)
	{
		return node->ID();
	}

	static SplayTreeLink<NodeType>* GetLink(NodeType* node)
	{
		return &node->fInoTreeLink;
	}

	static int Compare(KeyType key, const NodeType* node)
	{
		return key == node->ID() ? 0
			: (key < node->ID() ? -1 : 1);
	}

	static NodeType** GetListLink(NodeType* node)
	{
		return &node->fInoTreeNext;
	}
};

typedef IteratableSplayTree<InodesInoTreeDefinition> InodesInoTree;

struct InodesClusterTreeDefinition {
	typedef cluster_t KeyType;
	typedef	Inode NodeType;

	static KeyType GetKey(const NodeType* node)
	{
		return node->Cluster();
	}

	static SplayTreeLink<NodeType>* GetLink(NodeType* node)
	{
		return &node->fClusterTreeLink;
	}

	static int Compare(KeyType key, const NodeType* node)
	{
		return key == node->Cluster() ? 0
			: (key < node->Cluster() ? -1 : 1);
	}

	static NodeType** GetListLink(NodeType* node)
	{
		return &node->fClusterTreeNext;
	}
};

typedef IteratableSplayTree<InodesClusterTreeDefinition> InodesClusterTree;

#endif	// INODE_H
