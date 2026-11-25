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
	if (node->up != NULL && !node->up->root)
		parent = inode->Parent();
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
		fill_node(grand->inode, &grand->node, NULL);
	}
	fill_node(parent->inode, &parent->node, grand->held ? &grand->node : NULL);
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
	held_parent parent;
	held_parent grand;
	status_t status = hold_parent(volume, directory, &parent, &grand);
	exfat_node dir;
	fill_node(directory, &dir, parent.held ? &parent.node : NULL);
	exfat_node found;
	if (status == B_OK)
		status = exfat_lookup(volume->Store(), volume->Disk(), &dir, name,
			&found);
	release_parent(volume, &grand);
	release_parent(volume, &parent);
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
		if (cookie == NULL)
			return B_NO_MEMORY;
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
		fill_node(grand.inode, &grand.node, NULL);
	}
	exfat_node dir;
	fill_node(directory, &dir, grand.held ? &grand.node : NULL);
	exfat_time time = current_time();
	exfat_node created;
	MutexLocker allocation(volume->AllocLock());
	status_t status = exfat_create(volume->Store(), volume->Disk(),
		grand.held ? &grand.node : NULL, &dir, name, asDirectory, attributes,
		&time, &created);
	if (status == B_OK)
		adopt(directory, &dir);
	allocation.Unlock();
	if (status != B_OK) {
		release_parent(volume, &grand);
		return status;
	}
	status = publish_new(volume, directory, &created, openMode, _cookie, _id);
	if (status == B_OK) {
		entry_cache_add(volume->ID(), directory->ID(), name, *_id);
		notify_entry_created(volume->ID(), directory->ID(), name, *_id);
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
		fill_node(grand.inode, &grand.node, NULL);
	}
	exfat_node dir;
	fill_node(directory, &dir, grand.held ? &grand.node : NULL);
	exfat_node found;
	status_t status = exfat_lookup(volume->Store(), volume->Disk(), &dir, name,
		&found);
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
	MutexLocker allocation(volume->AllocLock());
	status = exfat_remove_entry(volume->Store(), volume->Disk(),
		grand.held ? &grand.node : NULL, &dir, name, &found);
	allocation.Unlock();
	release_parent(volume, &grand);
	if (status != B_OK) {
		put_vnode(volume->FSVolume(), id);
		return status;
	}
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
	if (inode->IsDeleted() && inode->ID() != 1) {
		MutexLocker allocation(volume->AllocLock());
		exfat_free_allocation(volume->Store(), volume->Disk(),
			inode->StartCluster(), (uint64)inode->Size(), inode->StreamFlags());
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

	exfat_node fromNode;
	exfat_node toNode;
	fill_node(fromDir, &fromNode, NULL);
	fill_node(toDir, &toNode, NULL);
	exfat_node source;
	status_t status = exfat_lookup(volume->Store(), volume->Disk(), &fromNode,
		fromName, &source);
	if (status != B_OK)
		return unlock_pair(first, second, status);
	if (source.is_dir) {
		ino_t sourceID = volume->GetIno(source.entry_cluster, source.entry_index,
			fromDir->ID());
		status = same_tree(volume, toDir->ID(), sourceID);
		if (status != B_OK)
			return unlock_pair(first, second, status);
	}

	exfat_node dest;
	status = exfat_lookup(volume->Store(), volume->Disk(), &toNode, toName,
		&dest);
	if (status == B_OK) {
		ino_t sourceID = volume->GetIno(source.entry_cluster, source.entry_index,
			fromDir->ID());
		ino_t destID = volume->GetIno(dest.entry_cluster, dest.entry_index,
			toDir->ID());
		if (sourceID != destID) {
			if (source.is_dir != dest.is_dir) {
				status = dest.is_dir ? B_IS_A_DIRECTORY : B_NOT_A_DIRECTORY;
				return unlock_pair(first, second, status);
			}
			status = unlink_name(volume, toDir, toName, dest.is_dir);
			if (status != B_OK)
				return unlock_pair(first, second, status);
		}
	} else if (status != B_ENTRY_NOT_FOUND)
		return unlock_pair(first, second, status);

	exfat_time time = current_time();
	exfat_node moved;
	MutexLocker allocation(volume->AllocLock());
	status = exfat_rename(volume->Store(), volume->Disk(), NULL, &fromNode,
		fromName, NULL, &toNode, toName, &time, &moved);
	if (status == B_OK) {
		adopt(fromDir, &fromNode);
		if (fromDir != toDir)
			adopt(toDir, &toNode);
		ino_t id = volume->GetIno(source.entry_cluster, source.entry_index,
			fromDir->ID());
		volume->MoveNode(id, moved.entry_cluster, moved.entry_index, toDir->ID());
		Inode* live = NULL;
		if (get_vnode(volume->FSVolume(), id, (void**)&live) == B_OK) {
			live->AdoptStream(moved.first_cluster, moved.data_length,
				moved.valid_length, moved.flags, moved.attributes,
				moved.secondary_count, moved.entry_cluster, moved.entry_index);
			live->SetParent(toDir->ID());
			put_vnode(volume->FSVolume(), id);
		}
	}
	allocation.Unlock();
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
	held_parent parent;
	held_parent grand;
	status_t status = hold_parent(volume, inode, &parent, &grand);
	if (status != B_OK)
		return status;
	exfat_node node;
	fill_node(inode, &node, parent.held ? &parent.node : NULL);
	exfat_time time = current_time();
	MutexLocker allocation(volume->AllocLock());
	status = exfat_set_size(volume->Store(), volume->Disk(),
		parent.held ? &parent.node : NULL, &node, (uint64)size, (uint64)size,
		&time, true);
	if (status == B_OK) {
		adopt(inode, &node);
		if (parent.held)
			adopt(parent.inode, &parent.node);
	}
	allocation.Unlock();
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
	rw_lock_write_lock(inode->Lock());
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
		exfat_time time = current_time();
		if ((mask & B_STAT_MODIFICATION_TIME) != 0
			|| (mask & B_STAT_CHANGE_TIME) != 0) {
			time.unix_seconds = stat->st_mtim.tv_sec;
			time.nanoseconds = (uint32)stat->st_mtim.tv_nsec;
		}
		MutexLocker allocation(volume->AllocLock());
		if (status == B_OK && (mask & B_STAT_MODE) != 0) {
			uint16 attributes = node.attributes;
			if ((stat->st_mode & S_IWUSR) == 0)
				attributes |= 0x01;
			else
				attributes = (uint16)(attributes & ~0x01);
			status = exfat_set_attributes(volume->Store(), volume->Disk(),
				parent.held ? &parent.node : NULL, &node, attributes);
		}
		uint32 which = 0;
		if ((mask & (B_STAT_MODIFICATION_TIME | B_STAT_CHANGE_TIME)) != 0)
			which |= EXFAT_TIME_MODIFY;
		if ((mask & B_STAT_ACCESS_TIME) != 0)
			which |= EXFAT_TIME_ACCESS;
		if ((mask & B_STAT_CREATION_TIME) != 0) {
			which |= EXFAT_TIME_CREATE;
			time.unix_seconds = stat->st_crtim.tv_sec;
			time.nanoseconds = (uint32)stat->st_crtim.tv_nsec;
		}
		if (status == B_OK && which != 0)
			status = exfat_set_times(volume->Store(), volume->Disk(),
				parent.held ? &parent.node : NULL, &node, which, &time);
		if (status == B_OK)
			adopt(inode, &node);
		allocation.Unlock();
		release_parent(volume, &grand);
		release_parent(volume, &parent);
		if (status == B_OK)
			notify_stat_changed(volume->ID(), inode->Parent(), inode->ID(),
				mask);
	}
	rw_lock_write_unlock(inode->Lock());
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
	if ((cookie->open_mode & O_APPEND) != 0)
		pos = inode->Size();

	off_t end = pos + (off_t)*length;
	off_t oldData = inode->Size();
	off_t oldValid = inode->ValidSize();
	rw_lock_write_lock(inode->Lock());
	status_t status = B_OK;
	if (end > oldData) {
		held_parent parent;
		held_parent grand;
		status = hold_parent(volume, inode, &parent, &grand);
		exfat_node node;
		fill_node(inode, &node, parent.held ? &parent.node : NULL);
		if (status == B_OK) {
			MutexLocker allocation(volume->AllocLock());
			status = exfat_set_size(volume->Store(), volume->Disk(),
				parent.held ? &parent.node : NULL, &node, (uint64)end,
				(uint64)oldValid, NULL, false);
			if (status == B_OK) {
				adopt(inode, &node);
				if (parent.held)
					adopt(parent.inode, &parent.node);
			}
		}
		release_parent(volume, &grand);
		release_parent(volume, &parent);
	}
	rw_lock_write_unlock(inode->Lock());
	if (status != B_OK)
		return status;

	if (pos > oldValid) {
		size_t gap = (size_t)(pos - oldValid);
		uint8 zeros[4096];
		memset(zeros, 0, sizeof(zeros));
		off_t at = oldValid;
		while (gap > 0) {
			size_t piece = gap > sizeof(zeros) ? sizeof(zeros) : gap;
			status = file_cache_write(inode->FileCache(), at, zeros, &piece);
			if (status != B_OK || piece == 0)
				break;
			at += (off_t)piece;
			gap -= piece;
		}
	}
	size_t written = *length;
	if (status == B_OK && written > 0)
		status = file_cache_write(inode->FileCache(), pos, buffer, &written);
	if (status != B_OK || (written == 0 && *length > 0)) {
		if (end > oldData)
			exfat_set_file_size(inode, oldData);
		*length = 0;
		return status != B_OK ? status : B_IO_ERROR;
	}

	off_t newValid = oldValid;
	if (pos <= oldValid && pos + (off_t)written > oldValid)
		newValid = pos + (off_t)written;
	else if (pos > oldValid)
		newValid = pos + (off_t)written;
	if (newValid != oldValid || end > oldData) {
		held_parent parent;
		held_parent grand;
		status_t stamp = hold_parent(volume, inode, &parent, &grand);
		exfat_node node;
		fill_node(inode, &node, parent.held ? &parent.node : NULL);
		exfat_time time = current_time();
		if (stamp == B_OK) {
			MutexLocker allocation(volume->AllocLock());
			stamp = exfat_set_size(volume->Store(), volume->Disk(),
				parent.held ? &parent.node : NULL, &node,
				(uint64)inode->Size(), (uint64)newValid, &time, true);
			if (stamp == B_OK)
				adopt(inode, &node);
		}
		release_parent(volume, &grand);
		release_parent(volume, &parent);
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
