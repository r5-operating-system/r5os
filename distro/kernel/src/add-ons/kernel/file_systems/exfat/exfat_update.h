/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * On-disk updates for the exFAT driver. The layout and the checksums follow
 * the public exFAT File System Specification (Microsoft, 2019).
 */

#ifndef EXFAT_UPDATE_H
#define EXFAT_UPDATE_H

#ifdef EXFAT_HOST_TEST
#include <stddef.h>
#include <stdint.h>
typedef int status_t;
typedef int32_t int32;
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef int64_t int64;
typedef uint64_t uint64;
#define B_OK					0
#define B_NO_MEMORY				((int)(-2147483647 - 1))
#define B_IO_ERROR				((int)(-2147483647 - 1) + 1)
#define B_BAD_VALUE				((int)(-2147483647 - 1) + 5)
#define B_NOT_ALLOWED			((int)(-2147483647 - 1) + 15)
#define B_BAD_DATA				((int)(-2147483647 - 1) + 16)
#define B_FILE_EXISTS			((int)(-2147483647 - 1) + 0x6000 + 2)
#define B_ENTRY_NOT_FOUND		((int)(-2147483647 - 1) + 0x6000 + 3)
#define B_NAME_TOO_LONG			((int)(-2147483647 - 1) + 0x6000 + 4)
#define B_NOT_A_DIRECTORY		((int)(-2147483647 - 1) + 0x6000 + 5)
#define B_DIRECTORY_NOT_EMPTY	((int)(-2147483647 - 1) + 0x6000 + 6)
#define B_DEVICE_FULL			((int)(-2147483647 - 1) + 0x6000 + 7)
#define B_READ_ONLY_DEVICE		((int)(-2147483647 - 1) + 0x6000 + 8)
#define B_IS_A_DIRECTORY		((int)(-2147483647 - 1) + 0x6000 + 9)
#else
#include "system_dependencies.h"
#endif

/* A block device. flush(cookie, block, 0) writes every dirty block. */
struct exfat_store {
	void*		cookie;
	status_t	(*read)(void* cookie, uint64 block, void* buffer,
					uint32 blocks);
	status_t	(*write)(void* cookie, uint64 block, const void* buffer,
					uint32 blocks);
	status_t	(*flush)(void* cookie, uint64 block, uint32 blocks);
};

struct exfat_disk {
	uint32		block_size;
	uint32		blocks_per_cluster_shift;
	uint32		cluster_count;
	uint32		fat_block;
	uint32		data_block;
	uint32		root_cluster;
	uint16		volume_flags;

	uint16*		upcase;
	uint8*		bitmap;
	uint32		bitmap_bytes;
	uint32*		bitmap_clusters;
	uint32		bitmap_cluster_count;
	uint32		free_clusters;
	uint32		hint;

	bool		can_write;
	bool		dirty;
	char		read_only_reason[160];
};

/* Where a directory entry set lives, and the stream it names. */
struct exfat_node {
	uint32		entry_cluster;
	uint32		entry_index;
	uint32		first_cluster;
	uint64		data_length;
	uint64		valid_length;
	uint16		attributes;
	uint8		flags;
	uint8		secondary_count;
	bool		root;
	bool		is_dir;
	/* Directory that holds this entry. Not owned. NULL for the root. */
	struct exfat_node* up;
};

struct exfat_time {
	int64		unix_seconds;
	int32		nanoseconds;
	int32		offset_minutes;
	bool		offset_valid;
};

enum {
	EXFAT_TIME_MODIFY	= 1,
	EXFAT_TIME_ACCESS	= 2,
	EXFAT_TIME_CREATE	= 4
};

status_t	exfat_disk_open(exfat_store* store, const uint8* boot,
				exfat_disk* disk);
void		exfat_disk_close(exfat_disk* disk);

status_t	exfat_root(exfat_store* store, exfat_disk* disk, exfat_node* root);

status_t	exfat_lookup(exfat_store* store, exfat_disk* disk,
				const exfat_node* directory, const char* name,
				exfat_node* found);

status_t	exfat_create(exfat_store* store, exfat_disk* disk,
				exfat_node* parent, exfat_node* directory, const char* name,
				bool as_directory, uint16 attributes, const exfat_time* now,
				exfat_node* created);

status_t	exfat_remove_entry(exfat_store* store, exfat_disk* disk,
				exfat_node* parent, exfat_node* directory, const char* name,
				exfat_node* removed);

status_t	exfat_free_allocation(exfat_store* store, exfat_disk* disk,
				uint32 first_cluster, uint64 data_length, uint8 flags);

status_t	exfat_rename(exfat_store* store, exfat_disk* disk,
				exfat_node* from_parent, exfat_node* from_dir,
				const char* from_name, exfat_node* to_parent,
				exfat_node* to_dir, const char* to_name,
				const exfat_time* now, exfat_node* moved);

status_t	exfat_set_size(exfat_store* store, exfat_disk* disk,
				exfat_node* parent, exfat_node* node, uint64 data_length,
				uint64 valid_length, const exfat_time* now, bool stamp);

status_t	exfat_set_times(exfat_store* store, exfat_disk* disk,
				const exfat_node* parent, exfat_node* node, uint32 which,
				const exfat_time* time);

status_t	exfat_set_attributes(exfat_store* store, exfat_disk* disk,
				const exfat_node* parent, exfat_node* node, uint16 attributes);

status_t	exfat_map(exfat_store* store, exfat_disk* disk,
				uint32 first_cluster, uint64 data_length, uint8 flags,
				uint64 pos, uint64* physical, uint64* length);

status_t	exfat_read_bytes(exfat_store* store, exfat_disk* disk,
				const exfat_node* node, uint64 pos, void* buffer, uint64 length,
				uint64* transferred);

status_t	exfat_write_bytes(exfat_store* store, exfat_disk* disk,
				exfat_node* parent, exfat_node* node, uint64 pos,
				const void* buffer, uint64 length, const exfat_time* now,
				uint64* transferred);

status_t	exfat_sync(exfat_store* store, exfat_disk* disk);

uint16		exfat_upcase(const exfat_disk* disk, uint16 code);

#endif /* EXFAT_UPDATE_H */
