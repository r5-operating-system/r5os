/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#include "Inode.h"
#include "exfat_update.h"

#include <file_systems/fs_ops_support.h>


extern fs_vnode_ops gExfatVnodeOps;


static exfat_time
current_time()
{
	exfat_time time;
	memset(&time, 0, sizeof(time));
	time.unix_seconds = real_time_clock();
	int32 seconds = get_timezone_offset();
	if (seconds % (15 * 60) == 0) {
		time.offset_minutes = (int16)(seconds / 60);
		time.offset_valid = true;
	}
	return time;
}


static void
fill_node(Inode* inode, exfat_node* node, exfat_node* up)
{
	memset(node, 0, sizeof(*node));
	node->entry_cluster = inode->ID() == 1 ? 0 : (uint32)inode->Cluster();
	node->entry_index = inode->ID() == 1 ? 0 : inode->Offset();
	node->first_cluster = inode->StartCluster();
	node->data_length = (uint64)inode->Size();
	node->valid_length = (uint64)inode->ValidSize();
	node->attributes = inode->Attributes();
	node->name_hash = inode->NameHash();
	node->flags = inode->StreamFlags();
	node->secondary_count = inode->ChunkCount();
	node->root = inode->ID() == 1;
	node->is_dir = inode->IsDirectory();
	node->up = up;
}


static void
adopt(Inode* inode, const exfat_node* node)
{
	ino_t parent = inode->Parent();
	inode->AdoptStream(node->first_cluster, node->data_length,
		node->valid_length, node->flags, node->attributes,
		node->secondary_count, node->entry_cluster, node->entry_index);
	if (inode->ID() != 1) {
		inode->GetVolume()->MoveNode(inode->ID(), node->entry_cluster,
			node->entry_index, parent);
	}
}


struct held_parent {
	Inode*		inode;
	exfat_node	node;
	bool		held;
};


static void
release_parent(Volume* volume, held_parent* held)
{
	if (held->held && held->inode != NULL)
		put_vnode(volume->FSVolume(), held->inode->ID());
	held->held = false;
	held->inode = NULL;
}


static status_t
hold_parent(Volume* volume, Inode* child, held_parent* parent,
	held_parent* grand)
{
	memset(parent, 0, sizeof(*parent));
	memset(grand, 0, sizeof(*grand));
	if (child->ID() == 1)
		return B_OK;
	status_t status = get_vnode(volume->FSVolume(), child->Parent(),
		(void**)&parent->inode);
	if (status != B_OK)
		return status;
	parent->held = true;
	if (parent->inode->ID() != 1) {
		status = get_vnode(volume->FSVolume(), parent->inode->Parent(),
			(void**)&grand->inode);
		if (status != B_OK) {
			release_parent(volume, parent);
			return status;
		}
		grand->held = true;
	}
	return B_OK;
}


status_t
exfat_lookup_id(Volume* volume, Inode* directory, const char* name, ino_t* _id)
{
	if (strcmp(name, ".") == 0) {
		*_id = directory->ID();
		return B_OK;
	}
	if (strcmp(name, "..") == 0) {
		*_id = directory->ID() == 1 ? directory->ID() : directory->Parent();
		return B_OK;
	}
	exfat_node dir;
	fill_node(directory, &dir, NULL);
	exfat_node found;
	status_t status = exfat_lookup(volume->Store(), volume->Disk(), &dir, name,
		&found);
	if (status != B_OK)
		return status;
	*_id = volume->GetIno(found.entry_cluster, found.entry_index,
		directory->ID());
	return B_OK;
}


static status_t
publish_new(Volume* volume, Inode* directory, const exfat_node* created,
	int openMode, void** _cookie, ino_t* _id)
{
	ino_t id = volume->GetIno(created->entry_cluster, created->entry_index,
		directory->ID());
	Inode* inode = new(std::nothrow) Inode(volume, id);
	if (inode == NULL)
		return B_NO_MEMORY;
	if (inode->InitCheck() != B_OK) {
		status_t status = inode->InitCheck();
		delete inode;
		return status;
	}
	status_t status = publish_vnode(volume->FSVolume(), id, inode,
		&gExfatVnodeOps, inode->Mode(), 0);
	if (status != B_OK) {
		delete inode;
		return status;
	}
	if (_cookie != NULL) {
		file_cookie* cookie = new(std::nothrow) file_cookie;
		if (cookie == NULL) {
			put_vnode(volume->FSVolume(), id);
			return B_NO_MEMORY;
		}
		cookie->open_mode = openMode & EXFAT_OPEN_MODE_USER_MASK;
		cookie->last_size = inode->Size();
		cookie->last_notification = system_time();
		*_cookie = cookie;
	}
	if (_id != NULL)
		*_id = id;
	return B_OK;
}


static status_t
create_entry(Volume* volume, Inode* directory, const char* name,
	bool asDirectory, uint16 attributes, int openMode, void** _cookie,
	ino_t* _id)
{
	if (volume->IsReadOnly())
		return B_READ_ONLY_DEVICE;
	if (strchr(name, '/') != NULL)
		return B_BAD_VALUE;

	held_parent grand;
	memset(&grand, 0, sizeof(grand));
	if (directory->ID() != 1) {
		status_t status = get_vnode(volume->FSVolume(), directory->Parent(),
			(void**)&grand.inode);
		if (status != B_OK)
			return status;
		grand.held = true;
	}
	exfat_node dir;
	exfat_time time = current_time();
	exfat_node created;
	MutexLocker allocation(volume->AllocLock());
	if (grand.held)
		fill_node(grand.inode, &grand.node, NULL);
	fill_node(directory, &dir, grand.held ? &grand.node : NULL);
	status_t status = exfat_create(volume->Store(), volume->Disk(),
		grand.held ? &grand.node : NULL, &dir, name, asDirectory, attributes,
		&time, &created);
	if (status == B_OK) {
		adopt(directory, &dir);
		if (grand.held)
			adopt(grand.inode, &grand.node);
	}
	allocation.Unlock();
	if (status != B_OK) {
		release_parent(volume, &grand);
		return status;
	}
	status = publish_new(volume, directory, &created, openMode, _cookie, _id);
	if (status == B_OK) {
		entry_cache_add(volume->ID(), directory->ID(), name, *_id);
		notify_entry_created(volume->ID(), directory->ID(), name, *_id);
	} else {
		MutexLocker again(volume->AllocLock());
		exfat_deactivate(volume->Store(), volume->Disk(), &dir,
			created.entry_index, (uint32)created.secondary_count + 1);
		if (created.first_cluster >= 2) {
			exfat_free_allocation(volume->Store(), volume->Disk(),
				created.first_cluster, created.data_length, created.flags);
		}
	}
	release_parent(volume, &grand);
	return status;
}


status_t
exfat_create_file(fs_volume* _volume, fs_vnode* _directory, const char* name,
	int openMode, int perms, void** _cookie, ino_t* _newID)
{
	Volume* volume = (Volume*)_volume->private_volume;
	Inode* directory = (Inode*)_directory->private_node;
	if (!directory->IsDirectory())
		return B_NOT_A_DIRECTORY;
	uint16 attributes = 0;
	if ((perms & S_IWUSR) == 0)
		attributes = 0x01;
	rw_lock_write_lock(directory->Lock());
	status_t status = create_entry(volume, directory, name, false, attributes,
		openMode, _cookie, _newID);
	rw_lock_write_unlock(directory->Lock());
	return status;
}


status_t
exfat_create_dir(fs_volume* _volume, fs_vnode* _directory, const char* name,
	int perms)
{
	Volume* volume = (Volume*)_volume->private_volume;
	Inode* directory = (Inode*)_directory->private_node;
	(void)perms;
	rw_lock_write_lock(directory->Lock());
	ino_t id = 0;
	status_t status = create_entry(volume, directory, name, true, 0, 0, NULL,
		&id);
	rw_lock_write_unlock(directory->Lock());
	if (status == B_OK)
		put_vnode(volume->FSVolume(), id);
	return status;
}


static status_t
unlink_name(Volume* volume, Inode* directory, const char* name, bool directoryOnly)
{
	if (volume->IsReadOnly())
		return B_READ_ONLY_DEVICE;
	if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
		return B_NOT_ALLOWED;

	held_parent grand;
	memset(&grand, 0, sizeof(grand));
	if (directory->ID() != 1) {
		status_t status = get_vnode(volume->FSVolume(), directory->Parent(),
			(void**)&grand.inode);
		if (status != B_OK)
			return status;
		grand.held = true;
	}
	exfat_node dir;
	exfat_node found;
	MutexLocker allocation(volume->AllocLock());
	if (grand.held)
		fill_node(grand.inode, &grand.node, NULL);
	fill_node(directory, &dir, grand.held ? &grand.node : NULL);
	status_t status = exfat_lookup(volume->Store(), volume->Disk(), &dir, name,
		&found);
	allocation.Unlock();
	if (status != B_OK) {
		release_parent(volume, &grand);
		return status;
	}
	if (directoryOnly && !found.is_dir) {
		release_parent(volume, &grand);
		return B_NOT_A_DIRECTORY;
	}
	if (!directoryOnly && found.is_dir) {
		release_parent(volume, &grand);
		return B_IS_A_DIRECTORY;
	}
	ino_t id = volume->GetIno(found.entry_cluster, found.entry_index,
		directory->ID());
	Inode* inode = NULL;
	status = get_vnode(volume->FSVolume(), id, (void**)&inode);
	if (status != B_OK) {
		release_parent(volume, &grand);
		return status;
	}
	allocation.Lock();
	if (grand.held)
		fill_node(grand.inode, &grand.node, NULL);
	fill_node(directory, &dir, grand.held ? &grand.node : NULL);
	status = exfat_remove_entry(volume->Store(), volume->Disk(),
		grand.held ? &grand.node : NULL, &dir, name, &found);
	if (status == B_OK)
		adopt(directory, &dir);
	allocation.Unlock();
	release_parent(volume, &grand);
	if (status != B_OK) {
		put_vnode(volume->FSVolume(), id);
		return status;
	}
	volume->ForgetIno(id);
	inode->SetDeleted();
	entry_cache_remove(volume->ID(), directory->ID(), name);
	notify_entry_removed(volume->ID(), directory->ID(), name, id);
	remove_vnode(volume->FSVolume(), id);
	put_vnode(volume->FSVolume(), id);
	return B_OK;
}


status_t
exfat_unlink(fs_volume* _volume, fs_vnode* _directory, const char* name)
{
	Volume* volume = (Volume*)_volume->private_volume;
	Inode* directory = (Inode*)_directory->private_node;
	rw_lock_write_lock(directory->Lock());
	status_t status = unlink_name(volume, directory, name, false);
	rw_lock_write_unlock(directory->Lock());
	return status;
}


status_t
exfat_remove_dir(fs_volume* _volume, fs_vnode* _directory, const char* name)
{
	Volume* volume = (Volume*)_volume->private_volume;
	Inode* directory = (Inode*)_directory->private_node;
	rw_lock_write_lock(directory->Lock());
	status_t status = unlink_name(volume, directory, name, true);
	rw_lock_write_unlock(directory->Lock());
	return status;
}


status_t
exfat_remove_vnode(fs_volume* _volume, fs_vnode* _node, bool /*reenter*/)
{
	Volume* volume = (Volume*)_volume->private_volume;
	Inode* inode = (Inode*)_node->private_node;
	volume->ForgetIno(inode->ID());
	if (inode->IsDeleted() && inode->ID() != 1) {
		MutexLocker allocation(volume->AllocLock());
		status_t freed = exfat_free_allocation(volume->Store(), volume->Disk(),
			inode->StartCluster(), (uint64)inode->Size(), inode->StreamFlags());
		if (freed == B_BAD_DATA)
			volume->MarkReadOnly();
	}
	delete inode;
	return B_OK;
}


static status_t
unlock_pair(Inode* first, Inode* second, status_t status)
{
	if (first != second)
		rw_lock_write_unlock(second->Lock());
	rw_lock_write_unlock(first->Lock());
	return status;
}


static status_t
same_tree(Volume* volume, ino_t parent, ino_t child)
{
	ino_t cursor = parent;
	for (int guard = 0; guard < 256 && cursor > 1; guard++) {
		if (cursor == child)
			return B_BAD_VALUE;
		Inode* inode = NULL;
		status_t status = get_vnode(volume->FSVolume(), cursor, (void**)&inode);
		if (status != B_OK)
			return status;
		ino_t next = inode->Parent();
		put_vnode(volume->FSVolume(), cursor);
		cursor = next;
	}
	if (cursor > 1)
		return B_BAD_VALUE;
	return B_OK;
}


status_t
exfat_rename_vnode(fs_volume* _volume, fs_vnode* _fromDir, const char* fromName,
	fs_vnode* _toDir, const char* toName)
{
	Volume* volume = (Volume*)_volume->private_volume;
	Inode* fromDir = (Inode*)_fromDir->private_node;
	Inode* toDir = (Inode*)_toDir->private_node;
	if (volume->IsReadOnly())
		return B_READ_ONLY_DEVICE;
	if (strcmp(fromName, ".") == 0 || strcmp(fromName, "..") == 0
		|| strcmp(toName, ".") == 0 || strcmp(toName, "..") == 0) {
		return B_NOT_ALLOWED;
	}

	Inode* first = fromDir->ID() <= toDir->ID() ? fromDir : toDir;
	Inode* second = first == fromDir ? toDir : fromDir;
	rw_lock_write_lock(first->Lock());
	if (first != second)
		rw_lock_write_lock(second->Lock());

	held_parent fromGrand;
	held_parent toGrand;
	memset(&fromGrand, 0, sizeof(fromGrand));
	memset(&toGrand, 0, sizeof(toGrand));
	if (fromDir->ID() != 1) {
		status_t held = get_vnode(volume->FSVolume(), fromDir->Parent(),
			(void**)&fromGrand.inode);
		if (held != B_OK)
			return unlock_pair(first, second, held);
		fromGrand.held = true;
	}
	if (toDir != fromDir && toDir->ID() != 1) {
		status_t held = get_vnode(volume->FSVolume(), toDir->Parent(),
			(void**)&toGrand.inode);
		if (held != B_OK) {
			release_parent(volume, &fromGrand);
			return unlock_pair(first, second, held);
		}
		toGrand.held = true;
	}
	exfat_node fromNode;
	exfat_node toNode;
	exfat_node source;
	exfat_node dest;
	bool replace = false;
	ino_t sourceID = 0;
	ino_t destID = 0;
	MutexLocker allocation(volume->AllocLock());
	if (fromGrand.held)
		fill_node(fromGrand.inode, &fromGrand.node, NULL);
	if (toGrand.held)
		fill_node(toGrand.inode, &toGrand.node, NULL);
	fill_node(fromDir, &fromNode, fromGrand.held ? &fromGrand.node : NULL);
	fill_node(toDir, &toNode, toDir == fromDir
		? (fromGrand.held ? &fromGrand.node : NULL)
		: (toGrand.held ? &toGrand.node : NULL));
	status_t status = exfat_lookup(volume->Store(), volume->Disk(), &fromNode,
		fromName, &source);
	if (status == B_OK && source.is_dir) {
		sourceID = volume->GetIno(source.entry_cluster, source.entry_index,
			fromDir->ID());
		allocation.Unlock();
		status = same_tree(volume, toDir->ID(), sourceID);
		allocation.Lock();
	}
	if (status == B_OK) {
		sourceID = volume->GetIno(source.entry_cluster, source.entry_index,
			fromDir->ID());
		status = exfat_lookup(volume->Store(), volume->Disk(), &toNode, toName,
			&dest);
		if (status == B_ENTRY_NOT_FOUND)
			status = B_OK;
		else if (status == B_OK) {
			destID = volume->GetIno(dest.entry_cluster, dest.entry_index,
				toDir->ID());
			if (destID != sourceID) {
				if (source.is_dir != dest.is_dir)
					status = dest.is_dir ? B_IS_A_DIRECTORY : B_NOT_A_DIRECTORY;
				else
					replace = true;
			}
		}
	}
	exfat_time time = current_time();
	exfat_node moved;
	if (status == B_OK) {
		status = exfat_rename(volume->Store(), volume->Disk(),
			fromGrand.held ? &fromGrand.node : NULL, &fromNode, fromName,
			toDir == fromDir ? (fromGrand.held ? &fromGrand.node : NULL)
				: (toGrand.held ? &toGrand.node : NULL),
			&toNode, toName, &time, &moved);
	}
	if (status == B_OK && replace) {
		status = exfat_deactivate(volume->Store(), volume->Disk(), &toNode,
			dest.entry_index, (uint32)dest.secondary_count + 1);
	}
	if (status == B_OK) {
		adopt(fromDir, &fromNode);
		if (fromDir != toDir)
			adopt(toDir, &toNode);
		if (fromGrand.held)
			adopt(fromGrand.inode, &fromGrand.node);
		if (toGrand.held)
			adopt(toGrand.inode, &toGrand.node);
		volume->MoveNode(sourceID, moved.entry_cluster, moved.entry_index,
			toDir->ID());
		Inode* live = NULL;
		if (get_vnode(volume->FSVolume(), sourceID, (void**)&live) == B_OK) {
			live->AdoptStream(moved.first_cluster, moved.data_length,
				moved.valid_length, moved.flags, moved.attributes,
				moved.secondary_count, moved.entry_cluster, moved.entry_index);
			live->SetParent(toDir->ID());
			put_vnode(volume->FSVolume(), sourceID);
		}
		if (replace)
			volume->ForgetIno(destID);
	}
	allocation.Unlock();
	if (status == B_OK && replace) {
		Inode* gone = NULL;
		if (get_vnode(volume->FSVolume(), destID, (void**)&gone) == B_OK) {
			gone->SetDeleted();
			remove_vnode(volume->FSVolume(), destID);
			put_vnode(volume->FSVolume(), destID);
		}
	}
	release_parent(volume, &fromGrand);
	release_parent(volume, &toGrand);
	if (status == B_OK) {
		entry_cache_remove(volume->ID(), fromDir->ID(), fromName);
		notify_entry_moved(volume->ID(), fromDir->ID(), fromName, toDir->ID(),
			toName, volume->GetIno(moved.entry_cluster, moved.entry_index,
				toDir->ID()));
		entry_cache_add(volume->ID(), toDir->ID(), toName,
			volume->GetIno(moved.entry_cluster, moved.entry_index, toDir->ID()));
	}
	return unlock_pair(first, second, status);
}


status_t
exfat_set_file_size(Inode* inode, off_t size)
{
	Volume* volume = inode->GetVolume();
	if (volume->IsReadOnly())
		return B_READ_ONLY_DEVICE;
	if (inode->IsDirectory())
		return B_IS_A_DIRECTORY;
	inode->WriteLock();
	held_parent parent;
	held_parent grand;
	status_t status = hold_parent(volume, inode, &parent, &grand);
	if (status != B_OK) {
		inode->WriteUnlock();
		return status;
	}
	off_t oldSize = inode->Size();
	off_t oldValid = inode->ValidSize();
	uint64 valid = (uint64)size;
	if (size > oldSize)
		valid = (uint64)oldValid;
	if (size < oldSize && inode->FileCache() != NULL) {
		file_cache_set_size(inode->FileCache(), size);
		file_map_set_size(inode->Map(), size);
	}
	exfat_node node;
	exfat_time time = current_time();
	MutexLocker allocation(volume->AllocLock());
	if (parent.held)
		fill_node(parent.inode, &parent.node, NULL);
	if (grand.held)
		fill_node(grand.inode, &grand.node, NULL);
	fill_node(inode, &node, parent.held ? &parent.node : NULL);
	status = exfat_set_size(volume->Store(), volume->Disk(),
		parent.held ? &parent.node : NULL, &node, (uint64)size, valid,
		&time, true);
	if (status == B_BAD_DATA)
		volume->MarkReadOnly();
	if (status != B_OK && size < oldSize && inode->FileCache() != NULL) {
		file_cache_set_size(inode->FileCache(), oldSize);
		file_map_set_size(inode->Map(), oldSize);
	}
	if (status == B_OK) {
		adopt(inode, &node);
		if (parent.held)
			adopt(parent.inode, &parent.node);
		if (grand.held)
			adopt(grand.inode, &grand.node);
	}
	allocation.Unlock();
	inode->WriteUnlock();
	release_parent(volume, &grand);
	release_parent(volume, &parent);
	if (status == B_OK)
		notify_stat_changed(volume->ID(), inode->Parent(), inode->ID(),
			B_STAT_SIZE | B_STAT_MODIFICATION_TIME);
	return status;
}


status_t
exfat_write_stat(fs_volume* _volume, fs_vnode* _node, const struct stat* stat,
	uint32 mask)
{
	Volume* volume = (Volume*)_volume->private_volume;
	Inode* inode = (Inode*)_node->private_node;
	if (volume->IsReadOnly())
		return B_READ_ONLY_DEVICE;
	if ((mask & (B_STAT_SIZE | B_STAT_MODE | B_STAT_MODIFICATION_TIME
			| B_STAT_CREATION_TIME | B_STAT_ACCESS_TIME
			| B_STAT_CHANGE_TIME)) == 0) {
		return B_OK;
	}
	inode->WriteLock();
	status_t status = B_OK;
	if ((mask & B_STAT_SIZE) != 0)
		status = exfat_set_file_size(inode, stat->st_size);
	if (status == B_OK && (mask & (B_STAT_MODE | B_STAT_MODIFICATION_TIME
			| B_STAT_CREATION_TIME | B_STAT_ACCESS_TIME
			| B_STAT_CHANGE_TIME)) != 0) {
		held_parent parent;
		held_parent grand;
		status = hold_parent(volume, inode, &parent, &grand);
		exfat_node node;
		fill_node(inode, &node, parent.held ? &parent.node : NULL);
		MutexLocker allocation(volume->AllocLock());
		if (parent.held)
			fill_node(parent.inode, &parent.node, NULL);
		if (grand.held)
			fill_node(grand.inode, &grand.node, NULL);
		fill_node(inode, &node, parent.held ? &parent.node : NULL);
		if (status == B_OK && (mask & B_STAT_MODE) != 0) {
			uint16 attributes = node.attributes;
			if ((stat->st_mode & S_IWUSR) == 0)
				attributes |= 0x01;
			else
				attributes = (uint16)(attributes & ~0x01);
			status = exfat_set_attributes(volume->Store(), volume->Disk(),
				parent.held ? &parent.node : NULL, &node, attributes);
		}
		exfat_time accessTime = current_time();
		exfat_time modifyTime = accessTime;
		exfat_time createTime = accessTime;
		accessTime.unix_seconds = stat->st_atim.tv_sec;
		accessTime.nanoseconds = (uint32)stat->st_atim.tv_nsec;
		modifyTime.unix_seconds = stat->st_mtim.tv_sec;
		modifyTime.nanoseconds = (uint32)stat->st_mtim.tv_nsec;
		createTime.unix_seconds = stat->st_crtim.tv_sec;
		createTime.nanoseconds = (uint32)stat->st_crtim.tv_nsec;
		if (status == B_OK && (mask & B_STAT_MODIFICATION_TIME) != 0)
			status = exfat_set_times(volume->Store(), volume->Disk(),
				parent.held ? &parent.node : NULL, &node, EXFAT_TIME_MODIFY,
				&modifyTime);
		if (status == B_OK && (mask & B_STAT_ACCESS_TIME) != 0)
			status = exfat_set_times(volume->Store(), volume->Disk(),
				parent.held ? &parent.node : NULL, &node, EXFAT_TIME_ACCESS,
				&accessTime);
		if (status == B_OK && (mask & B_STAT_CREATION_TIME) != 0)
			status = exfat_set_times(volume->Store(), volume->Disk(),
				parent.held ? &parent.node : NULL, &node, EXFAT_TIME_CREATE,
				&createTime);
		if (status == B_OK) {
			adopt(inode, &node);
			if (parent.held)
				adopt(parent.inode, &parent.node);
			if (grand.held)
				adopt(grand.inode, &grand.node);
		}
		allocation.Unlock();
		release_parent(volume, &grand);
		release_parent(volume, &parent);
		if (status == B_OK)
			notify_stat_changed(volume->ID(), inode->Parent(), inode->ID(),
				mask);
	}
	inode->WriteUnlock();
	return status;
}


status_t
exfat_write(fs_volume* _volume, fs_vnode* _node, void* _cookie, off_t pos,
	const void* buffer, size_t* length)
{
	Volume* volume = (Volume*)_volume->private_volume;
	Inode* inode = (Inode*)_node->private_node;
	file_cookie* cookie = (file_cookie*)_cookie;
	if (volume->IsReadOnly())
		return B_READ_ONLY_DEVICE;
	if (inode->IsDirectory())
		return B_IS_A_DIRECTORY;
	if (inode->FileCache() == NULL)
		return B_BAD_VALUE;
	if ((inode->Attributes() & 0x01) != 0)
		return B_NOT_ALLOWED;
	if (pos < 0)
		return B_BAD_VALUE;
	inode->WriteLock();
	if ((cookie->open_mode & O_APPEND) != 0)
		pos = inode->Size();
	if (*length > 0 && pos > OFF_MAX - (off_t)*length) {
		inode->WriteUnlock();
		return B_DEVICE_FULL;
	}
	off_t end = pos + (off_t)*length;
	uint64 limit = (uint64)volume->Disk()->cluster_count * volume->ClusterSize();
	if (end < 0 || (uint64)end > limit) {
		inode->WriteUnlock();
		return B_DEVICE_FULL;
	}
	off_t oldData = inode->Size();
	off_t oldValid = inode->ValidSize();
	status_t status = B_OK;
	static const uint8 kZeroPage[4096] = {};
	if (end > oldData) {
		held_parent parent;
		held_parent grand;
		status = hold_parent(volume, inode, &parent, &grand);
		if (status == B_OK) {
			MutexLocker allocation(volume->AllocLock());
			if (parent.held)
				fill_node(parent.inode, &parent.node, NULL);
			if (grand.held)
				fill_node(grand.inode, &grand.node, NULL);
			exfat_node node;
			fill_node(inode, &node, parent.held ? &parent.node : NULL);
			status = exfat_set_size(volume->Store(), volume->Disk(),
				parent.held ? &parent.node : NULL, &node, (uint64)end,
				(uint64)oldValid, NULL, false);
			if (status == B_OK) {
				adopt(inode, &node);
				if (parent.held)
					adopt(parent.inode, &parent.node);
				if (grand.held)
					adopt(grand.inode, &grand.node);
			}
		}
		release_parent(volume, &grand);
		release_parent(volume, &parent);
	}
	if (status == B_OK && pos > inode->ValidSize()) {
		size_t gap = (size_t)(pos - inode->ValidSize());
		off_t at = inode->ValidSize();
		while (gap > 0) {
			size_t piece = gap > sizeof(kZeroPage) ? sizeof(kZeroPage) : gap;
			status = file_cache_write(inode->FileCache(), at, kZeroPage, &piece);
			if (status != B_OK || piece == 0)
				break;
			at += (off_t)piece;
			gap -= piece;
		}
	}
	size_t written = *length;
	if (status == B_OK && written > 0)
		status = file_cache_write(inode->FileCache(), pos, buffer, &written);
	if (status == B_OK)
		status = file_cache_sync(inode->FileCache());
	off_t newValid = oldValid;
	if (status == B_OK) {
		if (pos <= oldValid && pos + (off_t)written > oldValid)
			newValid = pos + (off_t)written;
		else if (pos > oldValid)
			newValid = pos + (off_t)written;
		inode->SetValidLength((uint64)newValid);
	}
	if (status == B_OK && (newValid != oldValid || end > oldData)) {
		held_parent parent;
		held_parent grand;
		status = hold_parent(volume, inode, &parent, &grand);
		if (status == B_OK) {
			MutexLocker allocation(volume->AllocLock());
			if (parent.held)
				fill_node(parent.inode, &parent.node, NULL);
			if (grand.held)
				fill_node(grand.inode, &grand.node, NULL);
			exfat_node node;
			fill_node(inode, &node, parent.held ? &parent.node : NULL);
			exfat_time time = current_time();
			status = exfat_set_size(volume->Store(), volume->Disk(),
				parent.held ? &parent.node : NULL, &node,
				(uint64)inode->Size(), (uint64)newValid, &time, true);
			if (status == B_OK) {
				adopt(inode, &node);
				if (parent.held)
					adopt(parent.inode, &parent.node);
				if (grand.held)
					adopt(grand.inode, &grand.node);
			}
		}
		release_parent(volume, &grand);
		release_parent(volume, &parent);
	}
	inode->WriteUnlock();
	if (status != B_OK) {
		*length = 0;
		return status;
	}
	*length = written;
	notify_stat_changed(volume->ID(), inode->Parent(), inode->ID(),
		B_STAT_SIZE | B_STAT_MODIFICATION_TIME);
	return B_OK;
}


status_t
exfat_write_pages(fs_volume* _volume, fs_vnode* _node, void*,
	off_t pos, const iovec* vecs, size_t count, size_t* _numBytes)
{
	Volume* volume = (Volume*)_volume->private_volume;
	Inode* inode = (Inode*)_node->private_node;
	if (volume->IsReadOnly())
		return B_READ_ONLY_DEVICE;
	if (inode->Map() == NULL)
		return B_BAD_VALUE;

	inode->ReadLock();
	uint32 vecIndex = 0;
	size_t vecOffset = 0;
	size_t bytesLeft = *_numBytes;
	status_t status = B_OK;
	while (bytesLeft > 0) {
		file_io_vec fileVecs[8];
		size_t fileVecCount = 8;
		status = file_map_translate(inode->Map(), pos, bytesLeft, fileVecs,
			&fileVecCount, 0);
		if (status != B_OK && status != B_BUFFER_OVERFLOW)
			break;
		bool overflow = status == B_BUFFER_OVERFLOW;
		size_t bytes = bytesLeft;
		status = write_file_io_vec_pages(volume->Device(), fileVecs,
			fileVecCount, vecs, count, &vecIndex, &vecOffset, &bytes);
		if (status != B_OK || !overflow)
			break;
		pos += (off_t)bytes;
		bytesLeft -= bytes;
	}
	inode->ReadUnlock();
	return status;
}


status_t
exfat_fsync(fs_volume* _volume, fs_vnode* _node, bool /*dataOnly*/)
{
	Volume* volume = (Volume*)_volume->private_volume;
	Inode* inode = (Inode*)_node->private_node;
	status_t status = B_OK;
	if (inode->FileCache() != NULL)
		status = file_cache_sync(inode->FileCache());
	if (status == B_OK)
		status = block_cache_sync(volume->BlockCache());
	return status;
}


status_t
exfat_sync_volume(fs_volume* _volume)
{
	Volume* volume = (Volume*)_volume->private_volume;
	return volume->SyncDisk();
}


void
exfat_zero_past_valid(Inode* inode, off_t pos, const iovec* vecs, size_t count,
	size_t bytes)
{
	off_t valid = inode->ValidSize();
	size_t skip = 0;
	if (pos < valid) {
		if (pos + (off_t)bytes <= valid)
			return;
		skip = (size_t)(valid - pos);
	}
	size_t left = bytes - skip;
	size_t index = 0;
	size_t offset = 0;
	while (index < count && skip > 0) {
		size_t room = vecs[index].iov_len - offset;
		if (skip < room) {
			offset += skip;
			skip = 0;
			break;
		}
		skip -= room;
		offset = 0;
		index++;
	}
	while (index < count && left > 0) {
		size_t room = vecs[index].iov_len - offset;
		size_t piece = room < left ? room : left;
		memset((uint8*)vecs[index].iov_base + offset, 0, piece);
		left -= piece;
		offset = 0;
		index++;
	}
}
