/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * exFAT updates from the public specification (Microsoft, 2019): allocation
 * bitmap, FAT chains, NoFatChain, directory-entry checksums, name hash,
 * valid data length and the dirty flag. This file does not contain code
 * from Linux fs/exfat, exfatprogs or exfat-fuse.
 */

#ifdef EXFAT_HOST_TEST
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#else
#include "system_dependencies.h"
#endif

#include "exfat_update.h"

#include <stdlib.h>
#include <string.h>

enum {
	kClusterEnd		= 0xffffffff,
	kClusterBad		= 0xfffffff7,
	kFlagAlloc		= 0x01,
	kFlagNoFat		= 0x02,
	kAttrReadOnly	= 0x01,
	kAttrDirectory	= 0x10,
	kAttrArchive	= 0x20,
	kTypeFile		= 0x85,
	kTypeStream		= 0xc0,
	kTypeName		= 0xc1,
	kTypeBitmap		= 0x81,
	kTypeUpcase		= 0x82,
	kTypeLabel		= 0x83,
	kNameMax		= 255,
	kDirMax			= 256 * 1024 * 1024,
	kBitmapMax		= 32 * 1024 * 1024
};

struct work {
	exfat_store*	store;
	exfat_disk*		disk;
	uint8*			block;
};

static uint32
cluster_size(const exfat_disk* disk)
{
	return disk->block_size << disk->blocks_per_cluster_shift;
}

static uint16 r16(const uint8* p)
{
	return (uint16)p[0] | ((uint16)p[1] << 8);
}

static uint32 r32(const uint8* p)
{
	return (uint32)p[0] | ((uint32)p[1] << 8) | ((uint32)p[2] << 16)
		| ((uint32)p[3] << 24);
}

static uint64 r64(const uint8* p)
{
	return (uint64)r32(p) | ((uint64)r32(p + 4) << 32);
}

static void w16(uint8* p, uint16 value)
{
	p[0] = (uint8)value;
	p[1] = (uint8)(value >> 8);
}

static void w32(uint8* p, uint32 value)
{
	p[0] = (uint8)value;
	p[1] = (uint8)(value >> 8);
	p[2] = (uint8)(value >> 16);
	p[3] = (uint8)(value >> 24);
}

static void w64(uint8* p, uint64 value)
{
	w32(p, (uint32)value);
	w32(p + 4, (uint32)(value >> 32));
}

static void
reason(exfat_disk* disk, const char* text)
{
	disk->can_write = false;
	if (disk->read_only_reason[0] == '\0') {
		size_t length = strlen(text);
		if (length > sizeof(disk->read_only_reason) - 1)
			length = sizeof(disk->read_only_reason) - 1;
		memcpy(disk->read_only_reason, text, length);
		disk->read_only_reason[length] = '\0';
	}
}

static status_t
work_open(exfat_store* store, exfat_disk* disk, work* out)
{
	out->store = store;
	out->disk = disk;
	out->block = (uint8*)malloc(disk->block_size);
	if (out->block == NULL)
		return B_NO_MEMORY;
	return B_OK;
}

static void
work_close(work* job)
{
	free(job->block);
	job->block = NULL;
}

static uint32
clusters_for(const exfat_disk* disk, uint64 length)
{
	uint32 size = cluster_size(disk);
	if (length == 0)
		return 0;
	return (uint32)((length + size - 1) / size);
}

static status_t
read_block(work* job, uint64 block)
{
	return job->store->read(job->store->cookie, block, job->block, 1);
}

static status_t
write_block(work* job, uint64 block)
{
	return job->store->write(job->store->cookie, block, job->block, 1);
}

static status_t
flush_blocks(work* job, uint64 block, uint32 count)
{
	return job->store->flush(job->store->cookie, block, count);
}

static uint64
cluster_block(const exfat_disk* disk, uint32 cluster)
{
	return disk->data_block
		+ ((uint64)(cluster - 2) << disk->blocks_per_cluster_shift);
}

static status_t
fat_get(work* job, uint32 cluster, uint32* value)
{
	if (cluster < 2 || cluster > job->disk->cluster_count + 1) {
		job->disk->can_write = false;
		return B_BAD_DATA;
	}
	uint32 per = job->disk->block_size / 4;
	uint64 block = job->disk->fat_block + (uint64)cluster / per;
	status_t status = read_block(job, block);
	if (status != B_OK)
		return status;
	*value = r32(job->block + (cluster % per) * 4);
	return B_OK;
}

static status_t
fat_set(work* job, uint32 cluster, uint32 value)
{
	if (cluster < 2 || cluster > job->disk->cluster_count + 1) {
		job->disk->can_write = false;
		return B_BAD_DATA;
	}
	uint32 per = job->disk->block_size / 4;
	uint64 block = job->disk->fat_block + (uint64)cluster / per;
	status_t status = read_block(job, block);
	if (status != B_OK)
		return status;
	w32(job->block + (cluster % per) * 4, value);
	return write_block(job, block);
}

static status_t
flush_fat(work* job, uint32 first, uint32 last)
{
	if (last < first)
		return B_BAD_DATA;
	uint32 per = job->disk->block_size / 4;
	uint64 start = job->disk->fat_block + (uint64)first / per;
	uint64 end = job->disk->fat_block + (uint64)last / per;
	return flush_blocks(job, start, (uint32)(end - start + 1));
}

static bool
cluster_ok(const exfat_disk* disk, uint32 cluster)
{
	return cluster >= 2 && cluster <= disk->cluster_count + 1;
}

static bool
bitmap_test(const exfat_disk* disk, uint32 cluster)
{
	if (!cluster_ok(disk, cluster))
		return true;
	uint32 bit = cluster - 2;
	if (bit / 8 >= disk->bitmap_bytes)
		return true;
	return (disk->bitmap[bit / 8] & (uint8)(1u << (bit % 8))) != 0;
}

static void
bitmap_assign(exfat_disk* disk, uint32 cluster, bool allocated)
{
	if (!cluster_ok(disk, cluster))
		return;
	uint32 bit = cluster - 2;
	if (bit / 8 >= disk->bitmap_bytes)
		return;
	uint8 mask = (uint8)(1u << (bit % 8));
	if (allocated)
		disk->bitmap[bit / 8] |= mask;
	else
		disk->bitmap[bit / 8] &= (uint8)~mask;
}

static status_t
bitmap_flush(work* job, uint32 firstCluster, uint32 count)
{
	exfat_disk* disk = job->disk;
	if (count == 0)
		return B_OK;
	if (!cluster_ok(disk, firstCluster) || disk->bitmap_bytes == 0)
		return B_BAD_DATA;
	uint32 size = cluster_size(disk);
	uint32 firstByte = (firstCluster - 2) / 8;
	if (firstByte >= disk->bitmap_bytes)
		return B_BAD_DATA;
	uint32 lastBit = (firstCluster - 2) + count - 1;
	uint32 lastByte = lastBit / 8;
	if (lastByte >= disk->bitmap_bytes)
		lastByte = disk->bitmap_bytes - 1;
	status_t status = B_OK;
	uint32 byte = firstByte;
	while (byte <= lastByte) {
		uint32 index = byte / size;
		if (index >= disk->bitmap_cluster_count)
			return B_BAD_DATA;
		uint32 within = byte % size;
		uint32 chunk = size - within;
		if (chunk > lastByte - byte + 1)
			chunk = lastByte - byte + 1;
		uint32 cluster = disk->bitmap_clusters[index];
		uint32 done = 0;
		while (done < chunk) {
			uint32 blockOffset = within + done;
			uint64 block = cluster_block(disk, cluster)
				+ blockOffset / disk->block_size;
			uint32 into = blockOffset % disk->block_size;
			uint32 piece = disk->block_size - into;
			if (piece > chunk - done)
				piece = chunk - done;
			status = read_block(job, block);
			if (status != B_OK)
				return status;
			memcpy(job->block + into, disk->bitmap + byte + done, piece);
			status = write_block(job, block);
			if (status != B_OK)
				return status;
			status = flush_blocks(job, block, 1);
			if (status != B_OK)
				return status;
			done += piece;
		}
		byte += chunk;
	}
	return B_OK;
}

static status_t
next_cluster(work* job, uint32 first, uint64 dataLength, uint8 flags,
	uint32 cluster, uint32 index, uint32* next)
{
	(void)first;
	uint32 count = clusters_for(job->disk, dataLength);
	if (index + 1 >= count)
		return B_ENTRY_NOT_FOUND;
	if ((flags & kFlagNoFat) != 0) {
		*next = cluster + 1;
		return B_OK;
	}
	uint32 value;
	status_t status = fat_get(job, cluster, &value);
	if (status != B_OK)
		return status;
	if (value < 2 || value > job->disk->cluster_count + 1)
		return B_BAD_DATA;
	*next = value;
	return B_OK;
}

static status_t
cluster_at(work* job, uint32 first, uint64 dataLength, uint8 flags,
	uint64 byte, uint32* cluster, uint32* within)
{
	uint32 size = cluster_size(job->disk);
	uint64 index = byte / size;
	uint32 current = first;
	for (uint64 step = 0; step < index; step++) {
		uint32 next;
		status_t status = next_cluster(job, first, dataLength, flags, current,
			(uint32)step, &next);
		if (status != B_OK)
			return status;
		current = next;
	}
	*cluster = current;
	*within = (uint32)(byte % size);
	return B_OK;
}

static status_t
stream_read(work* job, uint32 first, uint64 dataLength, uint8 flags,
	uint64 offset, uint8* buffer, uint32 length)
{
	if ((uint64)offset + length > dataLength)
		return B_BAD_VALUE;
	uint32 done = 0;
	while (done < length) {
		uint32 cluster;
		uint32 within;
		status_t status = cluster_at(job, first, dataLength, flags,
			offset + done, &cluster, &within);
		if (status != B_OK)
			return status;
		uint32 blockOffset = within;
		uint64 block = cluster_block(job->disk, cluster)
			+ blockOffset / job->disk->block_size;
		uint32 into = blockOffset % job->disk->block_size;
		uint32 piece = job->disk->block_size - into;
		if (piece > length - done)
			piece = length - done;
		status = read_block(job, block);
		if (status != B_OK)
			return status;
		memcpy(buffer + done, job->block + into, piece);
		done += piece;
	}
	return B_OK;
}

static status_t
stream_write(work* job, uint32 first, uint64 dataLength, uint8 flags,
	uint64 offset, const uint8* buffer, uint32 length, bool flush)
{
	if (first < 2 || (uint64)offset + length > dataLength)
		return B_BAD_VALUE;
	uint32 done = 0;
	while (done < length) {
		uint32 cluster;
		uint32 within;
		status_t status = cluster_at(job, first, dataLength, flags,
			offset + done, &cluster, &within);
		if (status != B_OK)
			return status;
		uint64 block = cluster_block(job->disk, cluster)
			+ within / job->disk->block_size;
		uint32 into = within % job->disk->block_size;
		uint32 piece = job->disk->block_size - into;
		if (piece > length - done)
			piece = length - done;
		status = read_block(job, block);
		if (status != B_OK)
			return status;
		if (buffer == NULL)
			memset(job->block + into, 0, piece);
		else
			memcpy(job->block + into, buffer + done, piece);
		status = write_block(job, block);
		if (status != B_OK)
			return status;
		if (flush) {
			status = flush_blocks(job, block, 1);
			if (status != B_OK)
				return status;
		}
		done += piece;
	}
	return B_OK;
}

static status_t
zero_cluster(work* job, uint32 cluster)
{
	uint32 blocks = 1u << job->disk->blocks_per_cluster_shift;
	uint64 start = cluster_block(job->disk, cluster);
	memset(job->block, 0, job->disk->block_size);
	for (uint32 index = 0; index < blocks; index++) {
		status_t status = write_block(job, start + index);
		if (status != B_OK)
			return status;
	}
	return flush_blocks(job, start, blocks);
}

static bool
bitmap_run_free(const exfat_disk* disk, uint32 start, uint32 count)
{
	if (count == 0 || start < 2)
		return false;
	if ((uint64)start + count - 1 > (uint64)disk->cluster_count + 1)
		return false;
	for (uint32 index = 0; index < count; index++) {
		if (bitmap_test(disk, start + index))
			return false;
	}
	return true;
}

static status_t
bitmap_take(work* job, uint32 start, uint32 count)
{
	for (uint32 index = 0; index < count; index++) {
		if (bitmap_test(job->disk, start + index))
			return B_BAD_DATA;
		bitmap_assign(job->disk, start + index, true);
		if (job->disk->free_clusters > 0)
			job->disk->free_clusters--;
	}
	return bitmap_flush(job, start, count);
}

static status_t
bitmap_give(work* job, uint32 start, uint32 count)
{
	for (uint32 index = 0; index < count; index++) {
		if (bitmap_test(job->disk, start + index)) {
			bitmap_assign(job->disk, start + index, false);
			job->disk->free_clusters++;
		}
	}
	return bitmap_flush(job, start, count);
}

static status_t
alloc_one(work* job, uint32* cluster)
{
	exfat_disk* disk = job->disk;
	if (disk->free_clusters == 0)
		return B_DEVICE_FULL;
	uint32 count = disk->cluster_count;
	uint32 hint = disk->hint < 2 ? 2 : disk->hint;
	for (uint32 step = 0; step < count; step++) {
		uint32 candidate = 2 + (uint32)(((uint64)(hint - 2) + step) % count);
		if (bitmap_test(disk, candidate))
			continue;
		uint32 fat;
		status_t status = fat_get(job, candidate, &fat);
		if (status != B_OK)
			return status;
		if (fat == kClusterBad) {
			bitmap_assign(disk, candidate, true);
			if (disk->free_clusters > 0)
				disk->free_clusters--;
			status = bitmap_flush(job, candidate, 1);
			if (status != B_OK)
				return status;
			continue;
		}
		status = bitmap_take(job, candidate, 1);
		if (status != B_OK)
			return status;
		disk->hint = candidate + 1;
		if (disk->hint > disk->cluster_count + 1)
			disk->hint = 2;
		*cluster = candidate;
		return B_OK;
	}
	return B_DEVICE_FULL;
}

static status_t
alloc_run(work* job, uint32 count, uint32 prefer, uint32* first)
{
	if (count == 0)
		return B_BAD_VALUE;
	if (job->disk->free_clusters < count)
		return B_DEVICE_FULL;
	if (bitmap_run_free(job->disk, prefer, count)) {
		status_t status = bitmap_take(job, prefer, count);
		if (status != B_OK)
			return status;
		*first = prefer;
		return B_OK;
	}
	uint32 last = job->disk->cluster_count + 1;
	uint32 start = 2;
	while (start <= last && start + count - 1 <= last) {
		if (bitmap_test(job->disk, start)) {
			start++;
			continue;
		}
		uint32 run = 0;
		uint32 begin = start;
		while (start <= last && !bitmap_test(job->disk, start)) {
			run++;
			start++;
			if (run == count) {
				status_t status = bitmap_take(job, begin, count);
				if (status != B_OK)
					return status;
				*first = begin;
				job->disk->hint = begin + count;
				return B_OK;
			}
		}
		if (start <= last)
			start++;
	}
	return B_ENTRY_NOT_FOUND;
}

static status_t
mark_dirty(work* job)
{
	exfat_disk* disk = job->disk;
	if (!disk->can_write)
		return B_READ_ONLY_DEVICE;
	if (disk->dirty)
		return B_OK;
	status_t status = read_block(job, 0);
	if (status != B_OK)
		return status;
	uint16 flags = r16(job->block + 106);
	flags = (uint16)(flags | 0x0002);
	w16(job->block + 106, flags);
	status = write_block(job, 0);
	if (status != B_OK)
		return status;
	status = flush_blocks(job, 0, 1);
	if (status != B_OK)
		return status;
	disk->volume_flags = flags;
	disk->dirty = true;
	return B_OK;
}

static uint16
entry_checksum(const uint8* bytes, uint32 entries)
{
	uint32 length = entries * 32;
	uint16 sum = 0;
	for (uint32 index = 0; index < length; index++) {
		if (index == 2 || index == 3)
			continue;
		sum = (uint16)(((sum & 1) ? 0x8000 : 0) + (sum >> 1) + bytes[index]);
	}
	return sum;
}

static uint16
name_hash(const uint16* name, uint8 units)
{
	uint16 hash = 0;
	for (uint8 index = 0; index < units; index++) {
		uint8 bytes[2];
		w16(bytes, name[index]);
		hash = (uint16)(((hash & 1) ? 0x8000 : 0) + (hash >> 1) + bytes[0]);
		hash = (uint16)(((hash & 1) ? 0x8000 : 0) + (hash >> 1) + bytes[1]);
	}
	return hash;
}

uint16
exfat_upcase(const exfat_disk* disk, uint16 code)
{
	if (disk->upcase != NULL)
		return disk->upcase[code];
	if (code >= 'a' && code <= 'z')
		return (uint16)(code - 32);
	return code;
}

static bool
name_equal(const exfat_disk* disk, const uint16* left, const uint16* right,
	uint8 units)
{
	for (uint8 index = 0; index < units; index++) {
		if (exfat_upcase(disk, left[index]) != exfat_upcase(disk, right[index]))
			return false;
	}
	return true;
}

/* Howard Hinnant, public domain (civil_from_days, 2013). */
static void
civil_from_days(int64 days, int* year, unsigned* month, unsigned* day)
{
	days += 719468;
	int64 era = (days >= 0 ? days : days - 146096) / 146097;
	unsigned doe = (unsigned)(days - era * 146097);
	unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int y = (int)yoe + (int)era * 400;
	unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	unsigned mp = (5 * doy + 2) / 153;
	unsigned d = doy - (153 * mp + 2) / 5 + 1;
	unsigned m = mp < 10 ? mp + 3 : mp - 9;
	y += (m <= 2);
	*year = y;
	*month = m;
	*day = d;
}

static void
pack_time(const exfat_time* time, uint8* timestamp, uint8* tenMs, uint8* utc)
{
	int64 local = time->unix_seconds;
	int32 offset = time->offset_minutes;
	bool valid = time->offset_valid;
	if (valid && (offset % 15) != 0) {
		local = time->unix_seconds;
		offset = 0;
		valid = true;
	} else if (valid)
		local += (int64)offset * 60;
	if (local < 0)
		local = 0;
	int64 days = local / 86400;
	int seconds = (int)(local % 86400);
	int year;
	unsigned month;
	unsigned day;
	civil_from_days(days, &year, &month, &day);
	if (year < 1980) {
		year = 1980;
		month = 1;
		day = 1;
		seconds = 0;
	} else if (year > 2107) {
		year = 2107;
		month = 12;
		day = 31;
		seconds = 86398;
	}
	int hour = seconds / 3600;
	int minute = (seconds / 60) % 60;
	int second = seconds % 60;
	uint16 clock = (uint16)(((hour & 31) << 11) | ((minute & 63) << 5)
		| ((second / 2) & 31));
	uint16 date = (uint16)(((year - 1980) << 9) | ((month & 15) << 5)
		| (day & 31));
	w16(timestamp, clock);
	w16(timestamp + 2, date);
	if (tenMs != NULL) {
		int extra = (second % 2) * 100 + time->nanoseconds / 10000000;
		if (extra > 199)
			extra = 199;
		if (extra < 0)
			extra = 0;
		*tenMs = (uint8)extra;
	}
	if (utc != NULL) {
		if (!valid)
			*utc = 0;
		else {
			int quarters = offset / 15;
			if (quarters > 63)
				quarters = 63;
			if (quarters < -64)
				quarters = -64;
			*utc = (uint8)((quarters & 0x7f) | 0x80);
		}
	}
}

static status_t
utf8_to_utf16(const char* text, uint16* name, uint8* units)
{
	if (text == NULL || text[0] == '\0')
		return B_BAD_VALUE;
	uint32 count = 0;
	const uint8* cursor = (const uint8*)text;
	while (*cursor != 0) {
		uint32 code;
		uint8 lead = *cursor++;
		if (lead < 0x80) {
			code = lead;
		} else if ((lead & 0xe0) == 0xc0) {
			if ((cursor[0] & 0xc0) != 0x80)
				return B_BAD_VALUE;
			code = ((uint32)(lead & 0x1f) << 6) | (cursor[0] & 0x3f);
			cursor += 1;
			if (code < 0x80)
				return B_BAD_VALUE;
		} else if ((lead & 0xf0) == 0xe0) {
			if ((cursor[0] & 0xc0) != 0x80 || (cursor[1] & 0xc0) != 0x80)
				return B_BAD_VALUE;
			code = ((uint32)(lead & 0x0f) << 12) | ((uint32)(cursor[0] & 0x3f) << 6)
				| (cursor[1] & 0x3f);
			cursor += 2;
			if (code < 0x800 || (code >= 0xd800 && code <= 0xdfff))
				return B_BAD_VALUE;
		} else if ((lead & 0xf8) == 0xf0) {
			if ((cursor[0] & 0xc0) != 0x80 || (cursor[1] & 0xc0) != 0x80
				|| (cursor[2] & 0xc0) != 0x80)
				return B_BAD_VALUE;
			code = ((uint32)(lead & 0x07) << 18) | ((uint32)(cursor[0] & 0x3f) << 12)
				| ((uint32)(cursor[1] & 0x3f) << 6) | (cursor[2] & 0x3f);
			cursor += 3;
			if (code < 0x10000 || code > 0x10ffff)
				return B_BAD_VALUE;
		} else
			return B_BAD_VALUE;
		if (code < 0x10000) {
			if (count + 1 > kNameMax)
				return B_NAME_TOO_LONG;
			name[count++] = (uint16)code;
		} else {
			if (count + 2 > kNameMax)
				return B_NAME_TOO_LONG;
			code -= 0x10000;
			name[count++] = (uint16)(0xd800 + (code >> 10));
			name[count++] = (uint16)(0xdc00 + (code & 0x3ff));
		}
	}
	if (count == 0)
		return B_BAD_VALUE;
	if (count == 1 && name[0] == '.')
		return B_BAD_VALUE;
	if (count == 2 && name[0] == '.' && name[1] == '.')
		return B_BAD_VALUE;
	for (uint32 index = 0; index < count; index++) {
		uint16 code = name[index];
		if (code < 0x20 || code == '"' || code == '*' || code == '/'
			|| code == ':' || code == '<' || code == '>' || code == '?'
			|| code == '\\' || code == '|')
			return B_BAD_VALUE;
	}
	*units = (uint8)count;
	return B_OK;
}

static status_t
dir_transfer(work* job, const exfat_node* directory, uint64 offset, uint8* data,
	const uint8* source, uint32 length, bool flush)
{
	if (directory->first_cluster < 2)
		return B_BAD_DATA;
	if (offset + length > directory->data_length)
		return B_BAD_VALUE;
	return source == NULL && data != NULL
		? stream_read(job, directory->first_cluster, directory->data_length,
			directory->flags, offset, data, length)
		: stream_write(job, directory->first_cluster, directory->data_length,
			directory->flags, offset, source, length, flush);
}

static bool
one_block(const exfat_disk* disk, uint64 offset, uint32 bytes)
{
	if (bytes == 0)
		return true;
	return (offset / disk->block_size)
		== ((offset + bytes - 1) / disk->block_size);
}

static status_t
read_set(work* job, const exfat_node* directory, uint32 index, uint8* bytes,
	uint32 entries)
{
	return dir_transfer(job, directory, (uint64)index * 32, bytes, NULL,
		entries * 32, false);
}

static status_t
write_set(work* job, const exfat_node* directory, uint32 index,
	const uint8* bytes, uint32 entries)
{
	uint64 offset = (uint64)index * 32;
	uint32 length = entries * 32;
	if (one_block(job->disk, offset, length))
		return dir_transfer(job, directory, offset, NULL, bytes, length, true);

	uint8* copy = (uint8*)malloc(length);
	if (copy == NULL)
		return B_NO_MEMORY;
	memcpy(copy, bytes, length);
	uint8 type = copy[0];
	copy[0] = (uint8)(type & 0x7f);
	if (copy[0] == 0)
		copy[0] = 0x05;
	status_t status = dir_transfer(job, directory, offset, NULL, copy, length,
		true);
	if (status == B_OK) {
		copy[0] = type;
		status = dir_transfer(job, directory, offset, NULL, copy, 1, true);
	}
	free(copy);
	return status;
}

static status_t
deactivate_set(work* job, const exfat_node* directory, uint32 index,
	uint32 entries)
{
	uint32 length = entries * 32;
	uint8* bytes = (uint8*)malloc(length);
	if (bytes == NULL)
		return B_NO_MEMORY;
	status_t status = read_set(job, directory, index, bytes, entries);
	if (status == B_OK) {
		for (uint32 entry = 0; entry < entries; entry++) {
			uint8 type = bytes[entry * 32];
			if (type >= 0x80)
				bytes[entry * 32] = (uint8)(type & 0x7f);
		}
		if (one_block(job->disk, (uint64)index * 32, length)) {
			status = dir_transfer(job, directory, (uint64)index * 32, NULL,
				bytes, length, true);
		} else {
			status = dir_transfer(job, directory, (uint64)index * 32, NULL,
				bytes, 1, true);
			for (uint32 entry = 1; status == B_OK && entry < entries; entry++) {
				status = dir_transfer(job, directory,
					(uint64)(index + entry) * 32, NULL, bytes + entry * 32, 1,
					true);
			}
		}
	}
	free(bytes);
	return status;
}

static uint32
set_entries_of(const uint8* primary)
{
	uint8 type = primary[0];
	if (type == 0 || type < 0x80)
		return 1;
	if (type == kTypeBitmap || type == kTypeUpcase || type == kTypeLabel)
		return 1;
	if ((type & 0x40) != 0)
		return 1;
	return (uint32)primary[1] + 1;
}

static status_t
grow_directory(work* job, exfat_node* parent, exfat_node* directory);

static status_t
shrink_allocation(work* job, exfat_node* parent, exfat_node* node,
	uint64 dataLength, uint64 validLength, const exfat_time* now, bool stamp);

static status_t
find_slot(work* job, exfat_node* parent, exfat_node* directory, uint32 need,
	uint32 avoidIndex, uint32 avoidCount, uint32* slot)
{
	uint32 capacity = (uint32)(directory->data_length / 32);
	uint32 index = 0;
	while (index < capacity) {
		uint8 primary[32];
		status_t status = read_set(job, directory, index, primary, 1);
		if (status != B_OK)
			return status;
		uint8 type = primary[0];
		if (type == 0) {
			if (capacity - index < need) {
				status = grow_directory(job, parent, directory);
				if (status != B_OK)
					return status;
				capacity = (uint32)(directory->data_length / 32);
				continue;
			}
			*slot = index;
			return B_OK;
		}
		uint32 entries = set_entries_of(primary);
		if (index + entries > capacity)
			return B_BAD_DATA;
		bool unused = type < 0x80;
		if (unused) {
			uint32 run = 0;
			uint32 start = index;
			while (index < capacity) {
				status = read_set(job, directory, index, primary, 1);
				if (status != B_OK)
					return status;
				if (primary[0] == 0 || primary[0] >= 0x80)
					break;
				bool overlaps = !(index + 1 <= avoidIndex
					|| index >= avoidIndex + avoidCount);
				if (!overlaps)
					run++;
				else
					run = 0;
				index++;
				if (run == need) {
					*slot = index - need;
					return B_OK;
				}
				(void)start;
			}
			continue;
		}
		index += entries;
	}
	status_t status = grow_directory(job, parent, directory);
	if (status != B_OK)
		return status;
	return find_slot(job, parent, directory, need, avoidIndex, avoidCount, slot);
}

static status_t
place_set(work* job, exfat_node* parent, exfat_node* directory, uint32* index,
	uint32 oldEntries, const uint8* bytes, uint32 entries, bool* relocated)
{
	*relocated = false;
	uint64 offset = (uint64)(*index) * 32;
	if (oldEntries == entries && *index != ~(uint32)0
		&& offset + entries * 32 <= directory->data_length) {
		uint32 block = job->disk->block_size;
		if (block != 0
			&& offset / block == (offset + entries * 32 - 1) / block) {
			return dir_transfer(job, directory, offset, NULL, bytes,
				entries * 32, true);
		}
		for (uint32 entry = entries; entry > 0; entry--) {
			status_t status = dir_transfer(job, directory,
				(uint64)(*index + entry - 1) * 32, NULL,
				bytes + (entry - 1) * 32, 32, true);
			if (status != B_OK)
				return status;
		}
		return B_OK;
	}
	uint32 avoidIndex = *index;
	uint32 avoidCount = oldEntries;
	if (*index == ~(uint32)0) {
		avoidIndex = 0;
		avoidCount = 0;
	}
	uint32 slot = 0;
	status_t status = find_slot(job, parent, directory, entries, avoidIndex,
		avoidCount, &slot);
	if (status != B_OK)
		return status;
	status = write_set(job, directory, slot, bytes, entries);
	if (status != B_OK)
		return status;
	if (*index != ~(uint32)0 && oldEntries != 0) {
		status = deactivate_set(job, directory, *index, oldEntries);
		if (status != B_OK)
			return status;
	}
	*index = slot;
	*relocated = true;
	return B_OK;
}

static status_t
load_chain(work* job, uint32 first, uint32** clusters, uint32* count)
{
	uint32* list = NULL;
	uint32 used = 0;
	uint32 capacity = 0;
	uint32 cluster = first;
	uint32 guard = 0;
	while (cluster >= 2 && cluster <= job->disk->cluster_count + 1
		&& guard++ <= job->disk->cluster_count) {
		if (used == capacity) {
			uint32 nextCap = capacity == 0 ? 8 : capacity * 2;
			uint32* grown = (uint32*)realloc(list, nextCap * sizeof(uint32));
			if (grown == NULL) {
				free(list);
				return B_NO_MEMORY;
			}
			list = grown;
			capacity = nextCap;
		}
		list[used++] = cluster;
		uint32 next;
		status_t status = fat_get(job, cluster, &next);
		if (status != B_OK) {
			free(list);
			return status;
		}
		if (next >= 0xfffffff8)
			break;
		if (next == kClusterBad) {
			free(list);
			return B_BAD_DATA;
		}
		cluster = next;
	}
	*clusters = list;
	*count = used;
	return B_OK;
}

static status_t
walk_root(work* job, uint32* bitmapCluster, uint64* bitmapLength,
	uint8* bitmapId, uint32* upcaseCluster, uint64* upcaseLength,
	uint32* upcaseChecksum)
{
	uint32* clusters = NULL;
	uint32 count = 0;
	status_t status = load_chain(job, job->disk->root_cluster, &clusters,
		&count);
	if (status != B_OK)
		return status;
	bool sawBitmap = false;
	bool sawUpcase = false;
	uint8 entry[32];
	for (uint32 index = 0; index < count && (!sawBitmap || !sawUpcase); index++) {
		uint32 bytes = cluster_size(job->disk);
		for (uint32 offset = 0; offset + 32 <= bytes
			&& (!sawBitmap || !sawUpcase); offset += 32) {
			status = stream_read(job, clusters[index], bytes, kFlagNoFat, offset,
				entry, 32);
			if (status != B_OK) {
				free(clusters);
				return status;
			}
			if (entry[0] == 0) {
				free(clusters);
				if (!sawBitmap || !sawUpcase)
					return B_BAD_DATA;
				return B_OK;
			}
			if (entry[0] == kTypeBitmap && !sawBitmap) {
				*bitmapId = (uint8)(entry[1] & 0x01);
				*bitmapCluster = r32(entry + 20);
				*bitmapLength = r64(entry + 24);
				sawBitmap = true;
			} else if (entry[0] == kTypeUpcase && !sawUpcase) {
				*upcaseChecksum = r32(entry + 4);
				*upcaseCluster = r32(entry + 20);
				*upcaseLength = r64(entry + 24);
				sawUpcase = true;
			}
		}
	}
	free(clusters);
	if (!sawBitmap || !sawUpcase)
		return B_BAD_DATA;
	return B_OK;
}

static status_t
read_fat_stream(work* job, uint32 first, uint64 length, uint8* buffer)
{
	uint32* clusters = NULL;
	uint32 count = 0;
	status_t status = load_chain(job, first, &clusters, &count);
	if (status != B_OK)
		return status;
	uint32 size = cluster_size(job->disk);
	uint64 done = 0;
	for (uint32 index = 0; index < count && done < length; index++) {
		uint32 chunk = size;
		if ((uint64)chunk > length - done)
			chunk = (uint32)(length - done);
		status = stream_read(job, clusters[index], chunk, kFlagNoFat, 0,
			buffer + done, chunk);
		if (status != B_OK)
			break;
		done += chunk;
	}
	free(clusters);
	if (status == B_OK && done < length)
		return B_BAD_DATA;
	return status;
}

static uint32
table_checksum(const uint8* table, uint64 length)
{
	uint32 sum = 0;
	for (uint64 index = 0; index < length; index++)
		sum = ((sum & 1) ? 0x80000000 : 0) + (sum >> 1) + table[index];
	return sum;
}

static status_t
expand_upcase(const uint8* raw, uint64 length, uint16* table)
{
	uint32 out = 0;
	uint64 cursor = 0;
	while (cursor + 1 < length && out < 65536) {
		uint16 value = r16(raw + cursor);
		cursor += 2;
		/* 0xFFFF starts an identity run. The mapping of U+FFFF is also
		   0xFFFF, so the last index is a real mapping, not a run. */
		if (value == 0xffff && out < 0xffff) {
			if (cursor + 1 >= length)
				return B_BAD_DATA;
			uint16 count = r16(raw + cursor);
			cursor += 2;
			if (count == 0 || (uint32)count > 65536 - out)
				return B_BAD_DATA;
			for (uint16 index = 0; index < count; index++)
				table[out + index] = (uint16)(out + index);
			out += count;
		} else
			table[out++] = value;
	}
	if (out != 65536)
		return B_BAD_DATA;
	for (uint16 code = 0; code < 128; code++) {
		uint16 expect = (code >= 'a' && code <= 'z') ? (uint16)(code - 32) : code;
		if (table[code] != expect)
			return B_BAD_DATA;
	}
	return B_OK;
}

status_t
exfat_disk_open(exfat_store* store, const uint8* boot, exfat_disk* disk)
{
	memset(disk, 0, sizeof(*disk));
	disk->hint = 2;
	if (boot[0] != 0xeb || boot[1] != 0x76 || boot[2] != 0x90
		|| memcmp(boot + 3, "EXFAT   ", 8) != 0) {
		return B_BAD_DATA;
	}
	disk->fat_block = r32(boot + 80);
	disk->data_block = r32(boot + 88);
	disk->cluster_count = r32(boot + 92);
	disk->root_cluster = r32(boot + 96);
	disk->volume_flags = r16(boot + 106);
	uint8 blockShift = boot[108];
	disk->blocks_per_cluster_shift = boot[109];
	uint8 fatCount = boot[110];
	if (blockShift < 9 || blockShift > 12 || disk->cluster_count < 1
		|| disk->root_cluster < 2) {
		return B_BAD_DATA;
	}
	disk->block_size = 1u << blockShift;
	if ((uint64)cluster_size(disk) > 32u * 1024u * 1024u)
		reason(disk, "cluster size is above 32 MiB");
	disk->can_write = disk->read_only_reason[0] == '\0';
	if ((disk->volume_flags & 0x0002) != 0)
		reason(disk, "volume was not unmounted cleanly");
	if ((disk->volume_flags & 0x0004) != 0)
		reason(disk, "media failure flag is set");
	if ((disk->volume_flags & 0x0008) != 0)
		reason(disk, "clear-to-zero flag is set");
	if (fatCount != 1)
		reason(disk, "TexFAT volumes stay read-only");
	if ((disk->volume_flags & 0x0001) != 0)
		reason(disk, "the second FAT is active");

	work job;
	status_t status = work_open(store, disk, &job);
	if (status != B_OK)
		return status;
	uint32 bitmapCluster = 0;
	uint64 bitmapLength = 0;
	uint8 bitmapId = 0;
	uint32 upcaseCluster = 0;
	uint64 upcaseLength = 0;
	uint32 upcaseChecksum = 0;
	status = walk_root(&job, &bitmapCluster, &bitmapLength, &bitmapId,
		&upcaseCluster, &upcaseLength, &upcaseChecksum);
	if (status != B_OK) {
		reason(disk, "bitmap or up-case table is missing");
		work_close(&job);
		return B_OK;
	}
	if (bitmapId != (disk->volume_flags & 0x0001))
		reason(disk, "allocation bitmap does not match the active FAT");
	uint32 bitmapBytes = (disk->cluster_count + 7) / 8;
	if (bitmapLength < bitmapBytes)
		reason(disk, "allocation bitmap is too small");
	else if (bitmapBytes > kBitmapMax)
		reason(disk, "allocation bitmap is too large to cache");
	else {
		disk->bitmap = (uint8*)malloc(bitmapBytes);
		if (disk->bitmap == NULL) {
			work_close(&job);
			return B_NO_MEMORY;
		}
		memset(disk->bitmap, 0, bitmapBytes);
		status = read_fat_stream(&job, bitmapCluster, bitmapBytes, disk->bitmap);
		if (status != B_OK) {
			reason(disk, "allocation bitmap could not be read");
		} else {
			disk->bitmap_bytes = bitmapBytes;
			status = load_chain(&job, bitmapCluster, &disk->bitmap_clusters,
				&disk->bitmap_cluster_count);
			if (status != B_OK)
				reason(disk, "allocation bitmap chain could not be read");
			disk->free_clusters = 0;
			for (uint32 bit = 0; bit < disk->cluster_count; bit++) {
				if ((disk->bitmap[bit / 8] & (uint8)(1u << (bit % 8))) == 0)
					disk->free_clusters++;
			}
		}
	}
	if (upcaseLength > 0 && upcaseLength <= 128 * 1024) {
		uint8* raw = (uint8*)malloc((size_t)upcaseLength);
		uint16* table = (uint16*)malloc(65536 * sizeof(uint16));
		if (raw == NULL || table == NULL) {
			free(raw);
			free(table);
			work_close(&job);
			return B_NO_MEMORY;
		}
		status = read_fat_stream(&job, upcaseCluster, upcaseLength, raw);
		if (status != B_OK || table_checksum(raw, upcaseLength) != upcaseChecksum
			|| expand_upcase(raw, upcaseLength, table) != B_OK) {
			reason(disk, "up-case table checksum does not match");
			free(table);
		} else
			disk->upcase = table;
		free(raw);
	} else
		reason(disk, "up-case table could not be read");
	if (disk->bitmap == NULL || disk->upcase == NULL)
		disk->can_write = false;
	work_close(&job);
	return B_OK;
}

void
exfat_disk_close(exfat_disk* disk)
{
	free(disk->upcase);
	free(disk->bitmap);
	free(disk->bitmap_clusters);
	disk->upcase = NULL;
	disk->bitmap = NULL;
	disk->bitmap_clusters = NULL;
}

static status_t
chain_bytes(work* job, uint32 first, uint64* bytes)
{
	uint32* clusters = NULL;
	uint32 count = 0;
	status_t status = load_chain(job, first, &clusters, &count);
	free(clusters);
	if (status != B_OK)
		return status;
	*bytes = (uint64)count * cluster_size(job->disk);
	return B_OK;
}

status_t
exfat_root(exfat_store* store, exfat_disk* disk, exfat_node* root)
{
	memset(root, 0, sizeof(*root));
	root->root = true;
	root->is_dir = true;
	root->first_cluster = disk->root_cluster;
	root->flags = 0;
	root->attributes = kAttrDirectory;
	work job;
	status_t status = work_open(store, disk, &job);
	if (status != B_OK)
		return status;
	status = chain_bytes(&job, disk->root_cluster, &root->data_length);
	root->valid_length = root->data_length;
	work_close(&job);
	return status;
}

static status_t
node_from_set(exfat_disk* disk, const uint8* bytes, uint32 index,
	uint32 entryCluster, exfat_node* node)
{
	if (bytes[0] != kTypeFile || bytes[32] != kTypeStream)
		return B_BAD_DATA;
	uint16 sum = entry_checksum(bytes, (uint32)bytes[1] + 1);
	if (sum != r16(bytes + 2))
		return B_BAD_DATA;
	memset(node, 0, sizeof(*node));
	memcpy(node->primary, bytes, 32);
	node->entry_index = index;
	node->entry_cluster = entryCluster;
	node->secondary_count = bytes[1];
	node->attributes = r16(bytes + 4);
	node->is_dir = (node->attributes & kAttrDirectory) != 0;
	node->flags = bytes[33];
	node->name_hash = r16(bytes + 36);
	node->valid_length = r64(bytes + 32 + 8);
	node->first_cluster = r32(bytes + 32 + 20);
	node->data_length = r64(bytes + 32 + 24);
	(void)disk;
	return B_OK;
}

static status_t
entry_cluster(work* job, const exfat_node* directory, uint32 index,
	uint32* cluster)
{
	uint32 within;
	return cluster_at(job, directory->first_cluster, directory->data_length,
		directory->flags, (uint64)index * 32, cluster, &within);
}

static status_t
lookup_units(work* job, const exfat_node* directory, const uint16* name,
	uint8 units, exfat_node* found)
{
	uint16 folded[kNameMax];
	for (uint8 unit = 0; unit < units; unit++)
		folded[unit] = exfat_upcase(job->disk, name[unit]);
	uint16 hash = name_hash(folded, units);
	uint32 capacity = (uint32)(directory->data_length / 32);
	uint32 index = 0;
	uint8 primary[32];
	while (index < capacity) {
		status_t status = read_set(job, directory, index, primary, 1);
		if (status != B_OK)
			return status;
		if (primary[0] == 0)
			return B_ENTRY_NOT_FOUND;
		uint32 entries = set_entries_of(primary);
		if (primary[0] == kTypeFile && entries >= 2 && index + entries <= capacity) {
			uint8* set = (uint8*)malloc(entries * 32);
			if (set == NULL)
				return B_NO_MEMORY;
			status = read_set(job, directory, index, set, entries);
			if (status == B_OK && set[32] == kTypeStream && set[35] == units
				&& r16(set + 36) == hash) {
				uint16 stored[kNameMax];
				uint32 got = 0;
				for (uint32 entry = 2; entry < entries && got < units; entry++) {
					if (set[entry * 32] != kTypeName)
						break;
					uint32 copy = units - got;
					if (copy > 15)
						copy = 15;
					for (uint32 unit = 0; unit < copy; unit++)
						stored[got++] = r16(set + entry * 32 + 2 + unit * 2);
				}
				if (got == units && name_equal(job->disk, stored, name, units)) {
					uint32 cluster;
					status = entry_cluster(job, directory, index, &cluster);
					if (status == B_OK)
						status = node_from_set(job->disk, set, index, cluster,
							found);
					free(set);
					return status;
				}
			}
			free(set);
		}
		if (index + entries <= index)
			return B_BAD_DATA;
		index += entries;
	}
	return B_ENTRY_NOT_FOUND;
}

status_t
exfat_lookup(exfat_store* store, exfat_disk* disk, const exfat_node* directory,
	const char* name, exfat_node* found)
{
	if (!directory->is_dir && !directory->root)
		return B_NOT_A_DIRECTORY;
	uint16 unitsName[kNameMax];
	uint8 units = 0;
	status_t status = utf8_to_utf16(name, unitsName, &units);
	if (status != B_OK)
		return status;
	work job;
	status = work_open(store, disk, &job);
	if (status != B_OK)
		return status;
	status = lookup_units(&job, directory, unitsName, units, found);
	if (status == B_OK)
		found->up = (exfat_node*)directory;
	work_close(&job);
	return status;
}

status_t
exfat_read_entry(exfat_store* store, exfat_disk* disk,
	const exfat_node* directory, uint32 index, exfat_node* found)
{
	if (!directory->is_dir && !directory->root)
		return B_NOT_A_DIRECTORY;
	work job;
	status_t status = work_open(store, disk, &job);
	if (status != B_OK)
		return status;
	uint8 primary[32];
	status = read_set(&job, directory, index, primary, 1);
	uint32 entries = 0;
	if (status == B_OK) {
		if (primary[0] != kTypeFile || primary[1] < 1 || primary[1] > 17)
			status = B_BAD_DATA;
		else
			entries = (uint32)primary[1] + 1;
	}
	uint8* bytes = NULL;
	if (status == B_OK) {
		bytes = (uint8*)malloc(entries * 32);
		if (bytes == NULL)
			status = B_NO_MEMORY;
	}
	if (status == B_OK)
		status = read_set(&job, directory, index, bytes, entries);
	uint32 cluster = 0;
	if (status == B_OK)
		status = entry_cluster(&job, directory, index, &cluster);
	if (status == B_OK)
		status = node_from_set(disk, bytes, index, cluster, found);
	free(bytes);
	work_close(&job);
	return status;
}

status_t
exfat_deactivate(exfat_store* store, exfat_disk* disk,
	const exfat_node* directory, uint32 index, uint32 entries)
{
	work job;
	status_t status = work_open(store, disk, &job);
	if (status != B_OK)
		return status;
	status = mark_dirty(&job);
	if (status == B_OK)
		status = deactivate_set(&job, directory, index, entries);
	work_close(&job);
	return status;
}

static status_t
fill_set(exfat_disk* disk, const exfat_node* node, const uint16* name,
	uint8 units, const exfat_time* now, uint8* bytes, uint32* entriesOut)
{
	uint32 nameEntries = (units + 14) / 15;
	uint32 entries = 2 + nameEntries;
	memset(bytes, 0, entries * 32);
	bytes[0] = kTypeFile;
	bytes[1] = (uint8)(entries - 1);
	w16(bytes + 4, node->attributes);
	if (now != NULL) {
		pack_time(now, bytes + 8, bytes + 20, bytes + 22);
		pack_time(now, bytes + 12, bytes + 21, bytes + 23);
		pack_time(now, bytes + 16, NULL, bytes + 24);
	}
	bytes[32] = kTypeStream;
	bytes[33] = node->flags;
	bytes[35] = units;
	uint16 folded[kNameMax];
	for (uint8 index = 0; index < units; index++)
		folded[index] = exfat_upcase(disk, name[index]);
	w16(bytes + 36, name_hash(folded, units));
	w64(bytes + 40, node->valid_length);
	w32(bytes + 52, node->first_cluster);
	w64(bytes + 56, node->data_length);
	uint32 placed = 0;
	for (uint32 entry = 0; entry < nameEntries; entry++) {
		uint8* nameEntry = bytes + (2 + entry) * 32;
		nameEntry[0] = kTypeName;
		for (uint32 unit = 0; unit < 15 && placed < units; unit++, placed++)
			w16(nameEntry + 2 + unit * 2, name[placed]);
	}
	w16(bytes + 2, entry_checksum(bytes, entries));
	*entriesOut = entries;
	return B_OK;
}

static status_t
remember_cluster(work* job, const exfat_node* directory, uint32 index,
	uint32* cluster)
{
	return entry_cluster(job, directory, index, cluster);
}

static status_t
publish_new(work* job, exfat_node* parent, exfat_node* directory,
	const uint8* bytes, uint32 entries, exfat_node* created)
{
	uint32 index = ~(uint32)0;
	bool relocated = false;
	status_t status = place_set(job, parent, directory, &index, 0, bytes,
		entries, &relocated);
	if (status != B_OK)
		return status;
	uint32 cluster = 0;
	status = remember_cluster(job, directory, index, &cluster);
	if (status != B_OK)
		return status;
	status = node_from_set(job->disk, bytes, index, cluster, created);
	return status;
}

static status_t
link_new_clusters(work* job, exfat_node* node, uint32 extra)
{
	uint32 oldCount = clusters_for(job->disk, node->data_length);
	if (extra == 0)
		return B_OK;
	if (oldCount == 0) {
		uint32 first = 0;
		status_t status = alloc_run(job, extra, job->disk->hint, &first);
		if (status == B_OK) {
			node->flags = (uint8)(kFlagAlloc | kFlagNoFat);
			node->first_cluster = first;
			return B_OK;
		}
		if (status != B_ENTRY_NOT_FOUND)
			return status;
		uint32* fresh = (uint32*)malloc(extra * sizeof(uint32));
		if (fresh == NULL)
			return B_NO_MEMORY;
		for (uint32 index = 0; index < extra; index++) {
			status = alloc_one(job, &fresh[index]);
			if (status != B_OK) {
				for (uint32 undo = 0; undo < index; undo++)
					bitmap_give(job, fresh[undo], 1);
				free(fresh);
				return status;
			}
		}
		for (uint32 index = 0; index + 1 < extra; index++) {
			status = fat_set(job, fresh[index], fresh[index + 1]);
			if (status != B_OK)
				break;
		}
		if (status == B_OK)
			status = fat_set(job, fresh[extra - 1], kClusterEnd);
		for (uint32 index = 0; status == B_OK && index < extra; index++)
			status = flush_fat(job, fresh[index], fresh[index]);
		if (status != B_OK) {
			for (uint32 index = 0; index < extra; index++)
				bitmap_give(job, fresh[index], 1);
			free(fresh);
			return status;
		}
		node->flags = kFlagAlloc;
		node->first_cluster = fresh[0];
		free(fresh);
		return B_OK;
	}
	uint32 last = node->first_cluster;
	if ((node->flags & kFlagNoFat) != 0)
		last = node->first_cluster + oldCount - 1;
	else {
		uint32 cluster = node->first_cluster;
		for (uint32 index = 0; index < oldCount - 1; index++) {
			uint32 next;
			status_t status = next_cluster(job, node->first_cluster,
				node->data_length, node->flags, cluster, index, &next);
			if (status != B_OK)
				return status;
			cluster = next;
		}
		last = cluster;
	}
	uint32 prefer = last + 1;
	if ((node->flags & kFlagNoFat) != 0 && bitmap_run_free(job->disk, prefer, extra)) {
		status_t status = bitmap_take(job, prefer, extra);
		if (status != B_OK)
			return status;
		return B_OK;
	}
	uint32* fresh = (uint32*)malloc(extra * sizeof(uint32));
	if (fresh == NULL)
		return B_NO_MEMORY;
	for (uint32 index = 0; index < extra; index++) {
		status_t status = alloc_one(job, &fresh[index]);
		if (status != B_OK) {
			for (uint32 undo = 0; undo < index; undo++)
				bitmap_give(job, fresh[undo], 1);
			free(fresh);
			return status;
		}
	}
	status_t status = B_OK;
	bool wasContiguous = (node->flags & kFlagNoFat) != 0;
	if (wasContiguous) {
		for (uint32 index = 0; index < oldCount - 1; index++) {
			status = fat_set(job, node->first_cluster + index,
				node->first_cluster + index + 1);
			if (status != B_OK)
				break;
		}
		if (status == B_OK && oldCount > 0)
			status = fat_set(job, last, fresh[0]);
		node->flags = kFlagAlloc;
	} else
		status = fat_set(job, last, fresh[0]);
	for (uint32 index = 0; status == B_OK && index < extra - 1; index++)
		status = fat_set(job, fresh[index], fresh[index + 1]);
	if (status == B_OK)
		status = fat_set(job, fresh[extra - 1], kClusterEnd);
	if (status == B_OK && wasContiguous && oldCount > 0) {
		status = flush_fat(job, node->first_cluster,
			node->first_cluster + oldCount - 1);
	}
	if (status == B_OK && last >= 2)
		status = flush_fat(job, last, last);
	for (uint32 index = 0; status == B_OK && index < extra; index++)
		status = flush_fat(job, fresh[index], fresh[index]);
	if (status != B_OK) {
		for (uint32 index = 0; index < extra; index++)
			bitmap_give(job, fresh[index], 1);
	}
	free(fresh);
	return status;
}

static status_t
write_node_entry(work* job, exfat_node* parent, exfat_node* node,
	const exfat_time* now, uint32 timeWhich, bool replaceName,
	const uint16* name, uint8 units);

static status_t
grow_directory(work* job, exfat_node* parent, exfat_node* directory)
{
	if (!directory->is_dir && !directory->root)
		return B_NOT_A_DIRECTORY;
	uint64 added = cluster_size(job->disk);
	if (directory->data_length + added > kDirMax)
		return B_DEVICE_FULL;
	uint64 oldLength = directory->data_length;
	uint32 oldFirst = directory->first_cluster;
	uint8 oldFlags = directory->flags;
	status_t status = mark_dirty(job);
	if (status != B_OK)
		return status;
	status = link_new_clusters(job, directory, 1);
	if (status != B_OK)
		return status;
	uint32 newest = directory->first_cluster;
	if ((directory->flags & kFlagNoFat) != 0) {
		newest = directory->first_cluster
			+ clusters_for(job->disk, oldLength + added) - 1;
	} else if (oldLength != 0) {
		uint32 cluster = directory->first_cluster;
		uint32 count = clusters_for(job->disk, oldLength) + 1;
		for (uint32 index = 0; index < count - 1; index++) {
			uint32 next;
			status = next_cluster(job, directory->first_cluster,
				oldLength + added, directory->flags, cluster, index, &next);
			if (status != B_OK)
				break;
			cluster = next;
		}
		if (status == B_OK)
			newest = cluster;
	}
	if (status == B_OK)
		status = zero_cluster(job, newest);
	if (status == B_OK) {
		directory->data_length = oldLength + added;
		directory->valid_length = directory->data_length;
		if (!directory->root) {
			if (parent == NULL)
				parent = directory->up;
			status = write_node_entry(job, parent, directory, NULL, 0, false,
				NULL, 0);
		}
	}
	if (status != B_OK) {
		uint32 give = newest;
		if (!(cluster_ok(job->disk, give) && give != oldFirst)
			&& (directory->flags & kFlagNoFat) == 0
			&& cluster_ok(job->disk, directory->first_cluster)) {
			uint32 cluster = directory->first_cluster;
			uint32 guard = clusters_for(job->disk, oldLength) + 2;
			for (uint32 index = 0; index < guard; index++) {
				uint32 next = 0;
				if (fat_get(job, cluster, &next) != B_OK
					|| !cluster_ok(job->disk, next))
					break;
				cluster = next;
				give = cluster;
			}
		}
		if (cluster_ok(job->disk, give) && give != oldFirst)
			bitmap_give(job, give, 1);
		directory->data_length = oldLength;
		directory->valid_length = oldLength;
		directory->first_cluster = oldFirst;
		directory->flags = oldFlags;
	}
	return status;
}

static status_t
write_node_entry(work* job, exfat_node* parent, exfat_node* node,
	const exfat_time* now, uint32 timeWhich, bool replaceName,
	const uint16* name, uint8 units)
{
	if (node->root)
		return B_OK;
	if (parent == NULL)
		parent = node->up;
	if (parent == NULL)
		return B_BAD_VALUE;
	uint32 entries = (uint32)node->secondary_count + 1;
	if (entries < 2)
		return B_BAD_DATA;
	uint8* bytes = (uint8*)malloc(entries * 32 + 32 * 17);
	if (bytes == NULL)
		return B_NO_MEMORY;
	status_t status = read_set(job, parent, node->entry_index, bytes, entries);
	if (status != B_OK) {
		free(bytes);
		return status;
	}
	if ((bytes[0] & 0x80) == 0 || (bytes[0] & 0x7f) != (kTypeFile & 0x7f)) {
		free(bytes);
		return B_BAD_VALUE;
	}
	if (entry_checksum(bytes, entries) != r16(bytes + 2)) {
		job->disk->can_write = false;
		free(bytes);
		return B_BAD_DATA;
	}
	if (node->check_cluster && r32(bytes + 52) != node->previous_cluster) {
		free(bytes);
		return B_BAD_VALUE;
	}
	if (!replaceName && node->name_hash != 0
		&& r16(bytes + 36) != node->name_hash) {
		free(bytes);
		return B_BAD_VALUE;
	}
	uint32 oldEntries = entries;
	if (replaceName) {
		uint8 saved[32];
		memcpy(saved, bytes, 32);
		status = fill_set(job->disk, node, name, units, NULL, bytes, &entries);
		if (status != B_OK) {
			free(bytes);
			return status;
		}
		memcpy(bytes + 8, saved + 8, 17);
		node->name_hash = r16(bytes + 36);
	} else {
		bytes[33] = node->flags;
		w64(bytes + 40, node->valid_length);
		w32(bytes + 52, node->first_cluster);
		w64(bytes + 56, node->data_length);
	}
	w16(bytes + 4, node->attributes);
	if ((timeWhich & EXFAT_TIME_CREATE) != 0 && now != NULL)
		pack_time(now, bytes + 8, bytes + 20, bytes + 22);
	if ((timeWhich & EXFAT_TIME_MODIFY) != 0 && now != NULL)
		pack_time(now, bytes + 12, bytes + 21, bytes + 23);
	if ((timeWhich & EXFAT_TIME_ACCESS) != 0 && now != NULL)
		pack_time(now, bytes + 16, NULL, bytes + 24);
	w16(bytes + 2, entry_checksum(bytes, entries));
	uint32 index = node->entry_index;
	bool relocated = false;
	status = place_set(job, parent->up, parent, &index, oldEntries, bytes,
		entries, &relocated);
	if (status == B_OK) {
		if (relocated) {
			uint32 cluster = 0;
			status = remember_cluster(job, parent, index, &cluster);
			node->entry_index = index;
			node->entry_cluster = cluster;
		}
		node->secondary_count = (uint8)(entries - 1);
	}
	free(bytes);
	return status;
}

status_t
exfat_set_size(exfat_store* store, exfat_disk* disk, exfat_node* parent,
	exfat_node* node, uint64 dataLength, uint64 validLength,
	const exfat_time* now, bool stamp)
{
	if (node->is_dir && dataLength > kDirMax)
		return B_DEVICE_FULL;
	if (dataLength > (uint64)disk->cluster_count * cluster_size(disk))
		return B_DEVICE_FULL;
	if (validLength > dataLength)
		validLength = dataLength;
	work job;
	status_t status = work_open(store, disk, &job);
	if (status != B_OK)
		return status;
	status = mark_dirty(&job);
	if (status != B_OK) {
		work_close(&job);
		return status;
	}
	uint32 oldCount = clusters_for(disk, node->data_length);
	uint32 newCount = clusters_for(disk, dataLength);
	if (newCount < oldCount) {
		status = shrink_allocation(&job, parent, node, dataLength, validLength,
			now, stamp);
		work_close(&job);
		return status;
	}
	uint32 oldFirst = node->first_cluster;
	uint8 oldFlags = node->flags;
	uint64 oldData = node->data_length;
	uint64 oldValid = node->valid_length;
	if (newCount > oldCount) {
		status = link_new_clusters(&job, node, newCount - oldCount);
		if (status != B_OK) {
			node->first_cluster = oldFirst;
			node->flags = oldFlags;
			work_close(&job);
			return status;
		}
	}
	node->data_length = dataLength;
	node->valid_length = validLength;
	if (!node->is_dir && dataLength != oldData)
		node->attributes = (uint16)(node->attributes | kAttrArchive);
	uint32 timeWhich = stamp ? (EXFAT_TIME_MODIFY | EXFAT_TIME_ACCESS) : 0;
	if (!node->root && !node->detached)
		status = write_node_entry(&job, parent, node, now, timeWhich, false,
			NULL, 0);
	if (status != B_OK) {
		if (newCount > oldCount && node->first_cluster >= 2) {
			uint32 cluster = node->first_cluster;
			for (uint32 index = 0; index < newCount; index++) {
				uint32 current = cluster;
				if (index + 1 < newCount) {
					uint32 next = 0;
					if ((node->flags & kFlagNoFat) != 0)
						next = node->first_cluster + index + 1;
					else if (fat_get(&job, cluster, &next) != B_OK)
						next = 0;
					cluster = next;
				}
				if (index >= oldCount && cluster_ok(disk, current))
					bitmap_give(&job, current, 1);
			}
		}
		node->data_length = oldData;
		node->valid_length = oldValid;
		node->first_cluster = oldFirst;
		node->flags = oldFlags;
	}
	work_close(&job);
	return status;
}

/* One flush per FAT or bitmap block, not one per cluster. */
extern "C" int
compare_cluster(const void* left, const void* right)
{
	uint32 a = *(const uint32*)left;
	uint32 b = *(const uint32*)right;
	if (a < b)
		return -1;
	if (a > b)
		return 1;
	return 0;
}


static status_t
flush_clusters(work* job, const uint32* list, uint32 count, bool fat)
{
	if (count == 0)
		return B_OK;
	uint32* order = (uint32*)malloc(count * sizeof(uint32));
	if (order == NULL)
		return B_NO_MEMORY;
	uint32 used = 0;
	for (uint32 index = 0; index < count; index++) {
		if (cluster_ok(job->disk, list[index]))
			order[used++] = list[index];
	}
	qsort(order, used, sizeof(uint32), compare_cluster);
	status_t status = B_OK;
	uint32 index = 0;
	uint32 per = job->disk->block_size / 4;
	uint32 bitsPer = cluster_size(job->disk) * 8;
	while (index < used && status == B_OK) {
		uint32 start = order[index];
		uint32 end = start;
		uint32 next = index + 1;
		if (fat) {
			uint32 block = per == 0 ? 0 : start / per;
			while (next < used && per != 0 && order[next] / per == block) {
				end = order[next];
				next++;
			}
			status = flush_fat(job, start, end);
		} else if (bitsPer != 0) {
			uint32 block = (start - 2) / bitsPer;
			while (next < used && (order[next] - 2) / bitsPer == block) {
				end = order[next];
				next++;
			}
			status = bitmap_flush(job, start, end - start + 1);
		}
		index = next;
	}
	free(order);
	return status;
}

status_t
exfat_free_allocation(exfat_store* store, exfat_disk* disk, uint32 first,
	uint64 dataLength, uint8 flags)
{
	uint32 count = clusters_for(disk, dataLength);
	if (count == 0 || first < 2)
		return B_OK;
	work job;
	status_t status = work_open(store, disk, &job);
	if (status != B_OK)
		return status;
	status = mark_dirty(&job);
	if (status != B_OK) {
		work_close(&job);
		return status;
	}
	if ((flags & kFlagNoFat) != 0) {
		if (!cluster_ok(disk, first) || count == 0
			|| !cluster_ok(disk, first + count - 1)) {
			disk->can_write = false;
			work_close(&job);
			return B_BAD_DATA;
		}
		status = bitmap_give(&job, first, count);
		work_close(&job);
		return status;
	}
	uint32* list = (uint32*)malloc(count * sizeof(uint32));
	if (list == NULL) {
		work_close(&job);
		return B_NO_MEMORY;
	}
	uint32 cluster = first;
	for (uint32 index = 0; index < count; index++) {
		list[index] = cluster;
		if (index + 1 == count)
			break;
		uint32 next;
		status = fat_get(&job, cluster, &next);
		if (status != B_OK || !cluster_ok(disk, next)) {
			disk->can_write = false;
			free(list);
			work_close(&job);
			return status != B_OK ? status : B_BAD_DATA;
		}
		cluster = next;
	}
	for (uint32 index = 0; index < count; index++) {
		status = fat_set(&job, list[index], 0);
		if (status != B_OK)
			break;
	}
	if (status == B_OK) {
		for (uint32 index = 0; index < count; index++) {
			if (bitmap_test(disk, list[index])) {
				bitmap_assign(disk, list[index], false);
				disk->free_clusters++;
			}
		}
		status = flush_clusters(&job, list, count, true);
		if (status == B_OK)
			status = flush_clusters(&job, list, count, false);
	}
	free(list);
	work_close(&job);
	return status;
}

/* Shrink without freeing the clusters that the shorter file still names.
   The directory entry is updated first, then the tail is unlinked. */
static status_t
shrink_allocation(work* job, exfat_node* parent, exfat_node* node,
	uint64 dataLength, uint64 validLength, const exfat_time* now, bool stamp)
{
	uint32 oldCount = clusters_for(job->disk, node->data_length);
	uint32 newCount = clusters_for(job->disk, dataLength);
	uint32* list = (uint32*)malloc(oldCount * sizeof(uint32));
	if (list == NULL && oldCount != 0)
		return B_NO_MEMORY;
	uint32 cluster = node->first_cluster;
	for (uint32 index = 0; index < oldCount; index++) {
		if (!cluster_ok(job->disk, cluster)) {
			job->disk->can_write = false;
			free(list);
			return B_BAD_DATA;
		}
		list[index] = cluster;
		if (index + 1 == oldCount)
			break;
		if ((node->flags & kFlagNoFat) != 0)
			cluster = node->first_cluster + index + 1;
		else {
			uint32 next;
			status_t status = fat_get(job, cluster, &next);
			if (status != B_OK || !cluster_ok(job->disk, next)) {
				job->disk->can_write = false;
				free(list);
				return status != B_OK ? status : B_BAD_DATA;
			}
			cluster = next;
		}
	}
	uint32 oldFirst = node->first_cluster;
	uint8 oldFlags = node->flags;
	uint64 oldData = node->data_length;
	node->data_length = dataLength;
	node->valid_length = validLength;
	if (newCount == 0) {
		node->first_cluster = 0;
		node->flags = kFlagAlloc;
	}
	status_t status = B_OK;
	if (!node->root && !node->detached) {
		status = write_node_entry(job, parent, node, now,
			stamp ? (EXFAT_TIME_MODIFY | EXFAT_TIME_ACCESS) : 0, false, NULL, 0);
	}
	if (status != B_OK) {
		node->first_cluster = oldFirst;
		node->flags = oldFlags;
		node->data_length = oldData;
		free(list);
		return status;
	}
	if ((oldFlags & kFlagNoFat) == 0 && newCount > 0) {
		status = fat_set(job, list[newCount - 1], kClusterEnd);
		if (status == B_OK)
			status = flush_fat(job, list[newCount - 1], list[newCount - 1]);
	}
	if (status == B_OK && newCount < oldCount) {
		for (uint32 index = newCount; index < oldCount; index++) {
			if ((oldFlags & kFlagNoFat) == 0) {
				status = fat_set(job, list[index], 0);
				if (status != B_OK)
					break;
			}
			if (bitmap_test(job->disk, list[index])) {
				bitmap_assign(job->disk, list[index], false);
				job->disk->free_clusters++;
			}
		}
		if (status == B_OK && (oldFlags & kFlagNoFat) == 0)
			status = flush_clusters(job, list + newCount, oldCount - newCount,
				true);
		if (status == B_OK)
			status = flush_clusters(job, list + newCount, oldCount - newCount,
				false);
	}
	free(list);
	return status;
}

status_t
exfat_create(exfat_store* store, exfat_disk* disk, exfat_node* parent,
	exfat_node* directory, const char* name, bool asDirectory,
	uint16 attributes, const exfat_time* now, exfat_node* created)
{
	if (!directory->is_dir && !directory->root)
		return B_NOT_A_DIRECTORY;
	uint16 unitsName[kNameMax];
	uint8 units = 0;
	status_t status = utf8_to_utf16(name, unitsName, &units);
	if (status != B_OK)
		return status;
	exfat_node existing;
	status = exfat_lookup(store, disk, directory, name, &existing);
	if (status == B_OK)
		return B_FILE_EXISTS;
	if (status != B_ENTRY_NOT_FOUND)
		return status;
	work job;
	status = work_open(store, disk, &job);
	if (status != B_OK)
		return status;
	status = mark_dirty(&job);
	if (status != B_OK) {
		work_close(&job);
		return status;
	}
	exfat_node node;
	memset(&node, 0, sizeof(node));
	node.is_dir = asDirectory;
	node.attributes = attributes;
	if (asDirectory)
		node.attributes = (uint16)(node.attributes | kAttrDirectory | kAttrArchive);
	else
		node.attributes = (uint16)(node.attributes | kAttrArchive);
	node.flags = kFlagAlloc;
	if (asDirectory) {
		status = link_new_clusters(&job, &node, 1);
		if (status != B_OK) {
			work_close(&job);
			return status;
		}
		status = zero_cluster(&job, node.first_cluster);
		if (status != B_OK) {
			exfat_free_allocation(store, disk, node.first_cluster,
				cluster_size(disk), node.flags);
			work_close(&job);
			return status;
		}
		node.data_length = cluster_size(disk);
		node.valid_length = node.data_length;
	}
	uint8* bytes = (uint8*)malloc((2 + (units + 14) / 15) * 32);
	if (bytes == NULL) {
		if (asDirectory) {
			exfat_free_allocation(store, disk, node.first_cluster,
				node.data_length, node.flags);
		}
		work_close(&job);
		return B_NO_MEMORY;
	}
	uint32 entries = 0;
	status = fill_set(disk, &node, unitsName, units, now, bytes, &entries);
	if (status == B_OK)
		status = publish_new(&job, parent, directory, bytes, entries, created);
	if (status == B_OK)
		created->up = directory;
	free(bytes);
	if (status != B_OK && asDirectory) {
		exfat_free_allocation(store, disk, node.first_cluster, node.data_length,
			node.flags);
	}
	work_close(&job);
	return status;
}

static status_t
directory_occupied(work* job, const exfat_node* directory)
{
	uint32 capacity = (uint32)(directory->data_length / 32);
	uint8 primary[32];
	for (uint32 index = 0; index < capacity; index++) {
		status_t status = read_set(job, directory, index, primary, 1);
		if (status != B_OK)
			return status;
		if (primary[0] == 0)
			return B_OK;
		if (primary[0] == kTypeFile)
			return B_DIRECTORY_NOT_EMPTY;
		uint32 skip = set_entries_of(primary);
		if (skip > 1)
			index += skip - 1;
	}
	return B_OK;
}

status_t
exfat_remove_entry(exfat_store* store, exfat_disk* disk, exfat_node* parent,
	exfat_node* directory, const char* name, exfat_node* removed)
{
	status_t status = exfat_lookup(store, disk, directory, name, removed);
	if (status != B_OK)
		return status;
	if (removed->is_dir) {
		work scan;
		status = work_open(store, disk, &scan);
		if (status != B_OK)
			return status;
		status = directory_occupied(&scan, removed);
		work_close(&scan);
		if (status != B_OK)
			return status;
	}
	work job;
	status = work_open(store, disk, &job);
	if (status != B_OK)
		return status;
	status = mark_dirty(&job);
	if (status == B_OK) {
		status = deactivate_set(&job, directory, removed->entry_index,
			(uint32)removed->secondary_count + 1);
	}
	(void)parent;
	work_close(&job);
	return status;
}

status_t
exfat_rename(exfat_store* store, exfat_disk* disk, exfat_node* fromParent,
	exfat_node* fromDir, const char* fromName, exfat_node* toParent,
	exfat_node* toDir, const char* toName, const exfat_time* now,
	exfat_node* moved)
{
	exfat_node source;
	status_t status = exfat_lookup(store, disk, fromDir, fromName, &source);
	if (status != B_OK)
		return status;
	uint16 unitsName[kNameMax];
	uint8 units = 0;
	status = utf8_to_utf16(toName, unitsName, &units);
	if (status != B_OK)
		return status;
	exfat_node dest;
	status = exfat_lookup(store, disk, toDir, toName, &dest);
	bool replacing = status == B_OK;
	if (status != B_OK && status != B_ENTRY_NOT_FOUND)
		return status;
	if (replacing && dest.entry_index == source.entry_index
		&& dest.entry_cluster == source.entry_cluster
		&& fromDir->first_cluster == toDir->first_cluster) {
		work job;
		status = work_open(store, disk, &job);
		if (status != B_OK)
			return status;
		status = mark_dirty(&job);
		if (status == B_OK) {
			fromDir->up = fromParent;
			source.up = fromDir;
			source.check_cluster = true;
			source.previous_cluster = source.first_cluster;
			status = write_node_entry(&job, fromDir, &source, now, 0, true,
				unitsName, units);
		}
		work_close(&job);
		if (status == B_OK)
			*moved = source;
		return status;
	}
	if (replacing) {
		if (dest.is_dir && !source.is_dir)
			return B_IS_A_DIRECTORY;
		if (!dest.is_dir && source.is_dir)
			return B_NOT_A_DIRECTORY;
		if (dest.is_dir) {
			work scan;
			status = work_open(store, disk, &scan);
			if (status != B_OK)
				return status;
			status = directory_occupied(&scan, &dest);
			work_close(&scan);
			if (status != B_OK)
				return status;
		}
		/* The caller deactivates dest after this publish, and frees its
		   clusters only when the vnode is gone. Doing it here would reuse
		   dest's slot for the new set and free clusters still in use. */
	}
	work job;
	status = work_open(store, disk, &job);
	if (status != B_OK)
		return status;
	status = mark_dirty(&job);
	if (status != B_OK) {
		work_close(&job);
		return status;
	}
	uint32 entries = 0;
	uint8* bytes = (uint8*)malloc((2 + (units + 14) / 15) * 32);
	if (bytes == NULL) {
		work_close(&job);
		return B_NO_MEMORY;
	}
	status = fill_set(disk, &source, unitsName, units, NULL, bytes, &entries);
	if (status == B_OK) {
		/* Keep the original timestamps from a freshly read set. fill_set
		   with a null time leaves zeros; reload by writing through
		   write_node_entry when the directory does not change and the
		   entry count matches. Cross-directory always publishes a copy. */
		uint8* old = (uint8*)malloc(((uint32)source.secondary_count + 1) * 32);
		if (old == NULL)
			status = B_NO_MEMORY;
		else {
			status = read_set(&job, fromDir, source.entry_index, old,
				(uint32)source.secondary_count + 1);
			if (status == B_OK) {
				memcpy(bytes + 8, old + 8, 17);
				w16(bytes + 2, entry_checksum(bytes, entries));
			}
			free(old);
		}
	}
	exfat_node published;
	if (status == B_OK)
		status = publish_new(&job, toParent, toDir, bytes, entries, &published);
	if (status == B_OK) {
		status = deactivate_set(&job, fromDir, source.entry_index,
			(uint32)source.secondary_count + 1);
		published.first_cluster = source.first_cluster;
		published.data_length = source.data_length;
		published.valid_length = source.valid_length;
		published.flags = source.flags;
		published.attributes = source.attributes;
		published.is_dir = source.is_dir;
		published.up = toDir;
		*moved = published;
	}
	free(bytes);
	(void)fromParent;
	(void)now;
	work_close(&job);
	return status;
}

status_t
exfat_set_times(exfat_store* store, exfat_disk* disk, const exfat_node* parent,
	exfat_node* node, uint32 which, const exfat_time* time)
{
	work job;
	status_t status = work_open(store, disk, &job);
	if (status != B_OK)
		return status;
	status = mark_dirty(&job);
	if (status == B_OK) {
		status = write_node_entry(&job, (exfat_node*)parent, node, time, which,
			false, NULL, 0);
	}
	work_close(&job);
	return status;
}

status_t
exfat_set_attributes(exfat_store* store, exfat_disk* disk,
	const exfat_node* parent, exfat_node* node, uint16 attributes)
{
	node->attributes = attributes;
	return exfat_set_times(store, disk, parent, node, 0, NULL);
}

status_t
exfat_map(exfat_store* store, exfat_disk* disk, uint32 first, uint64 dataLength,
	uint8 flags, uint64 pos, uint64* physical, uint64* length)
{
	if (pos >= dataLength || first < 2)
		return B_ENTRY_NOT_FOUND;
	work job;
	status_t status = work_open(store, disk, &job);
	if (status != B_OK)
		return status;
	uint32 cluster;
	uint32 within;
	status = cluster_at(&job, first, dataLength, flags, pos, &cluster, &within);
	if (status != B_OK) {
		work_close(&job);
		return status;
	}
	*physical = cluster_block(disk, cluster) * disk->block_size + within;
	uint64 run = (uint64)cluster_size(disk) - within;
	uint32 index = (uint32)(pos / cluster_size(disk));
	uint32 current = cluster;
	while (run < dataLength - pos) {
		uint32 next;
		status = next_cluster(&job, first, dataLength, flags, current, index,
			&next);
		if (status != B_OK || next != current + 1)
			break;
		current = next;
		index++;
		run += cluster_size(disk);
	}
	if (run > dataLength - pos)
		run = dataLength - pos;
	*length = run;
	work_close(&job);
	return B_OK;
}

status_t
exfat_read_bytes(exfat_store* store, exfat_disk* disk, const exfat_node* node,
	uint64 pos, void* buffer, uint64 length, uint64* transferred)
{
	*transferred = 0;
	if (pos >= node->data_length || length == 0)
		return B_OK;
	if (pos + length > node->data_length)
		length = node->data_length - pos;
	work job;
	status_t status = work_open(store, disk, &job);
	if (status != B_OK)
		return status;
	uint8* out = (uint8*)buffer;
	uint64 done = 0;
	while (done < length) {
		uint32 piece = (uint32)((length - done) > 64 * 1024
			? 64 * 1024 : (length - done));
		status = stream_read(&job, node->first_cluster, node->data_length,
			node->flags, pos + done, out + done, piece);
		if (status != B_OK)
			break;
		done += piece;
	}
	work_close(&job);
	if (status != B_OK)
		return status;
	if (pos + done > node->valid_length) {
		uint64 zeroFrom = pos >= node->valid_length ? 0
			: node->valid_length - pos;
		if (zeroFrom < done)
			memset(out + zeroFrom, 0, (size_t)(done - zeroFrom));
	}
	*transferred = done;
	return B_OK;
}

status_t
exfat_write_bytes(exfat_store* store, exfat_disk* disk, exfat_node* parent,
	exfat_node* node, uint64 pos, const void* buffer, uint64 length,
	const exfat_time* now, uint64* transferred)
{
	*transferred = 0;
	if (node->is_dir)
		return B_IS_A_DIRECTORY;
	if (length == 0)
		return B_OK;
	if (pos + length < pos)
		return B_BAD_VALUE;
	if (pos + length > (uint64)disk->cluster_count * cluster_size(disk))
		return B_DEVICE_FULL;
	uint64 end = pos + length;
	uint64 oldValid = node->valid_length;
	if (end > node->data_length) {
		status_t status = exfat_set_size(store, disk, parent, node, end,
			oldValid, now, false);
		if (status != B_OK)
			return status;
	}
	work job;
	status_t status = work_open(store, disk, &job);
	if (status != B_OK)
		return status;
	if (pos > oldValid) {
		uint64 gap = pos - oldValid;
		while (gap > 0) {
			uint32 piece = gap > 64 * 1024 ? 64 * 1024 : (uint32)gap;
			status = stream_write(&job, node->first_cluster, node->data_length,
				node->flags, oldValid + (pos - oldValid - gap), NULL, piece,
				false);
			if (status != B_OK)
				break;
			gap -= piece;
		}
	}
	if (status == B_OK) {
		const uint8* incoming = (const uint8*)buffer;
		uint64 left = length;
		uint64 at = pos;
		while (left > 0) {
			uint32 piece = left > 1024 * 1024 ? 1024 * 1024 : (uint32)left;
			status = stream_write(&job, node->first_cluster, node->data_length,
				node->flags, at, incoming, piece, false);
			if (status != B_OK)
				break;
			incoming += piece;
			at += piece;
			left -= piece;
		}
	}
	if (status == B_OK)
		status = flush_blocks(&job, 0, 0);
	work_close(&job);
	if (status != B_OK)
		return status;
	uint64 newValid = oldValid;
	if (pos <= oldValid) {
		if (end > oldValid)
			newValid = end;
	} else
		newValid = end;
	status = exfat_set_size(store, disk, parent, node, node->data_length,
		newValid, now, true);
	if (status == B_OK)
		*transferred = length;
	return status;
}

status_t
exfat_sync(exfat_store* store, exfat_disk* disk)
{
	work job;
	status_t status = work_open(store, disk, &job);
	if (status != B_OK)
		return status;
	status = flush_blocks(&job, 0, 0);
	if (status == B_OK && store->drive_flush != NULL)
		status = store->drive_flush(store->cookie);
	if (status != B_OK || !disk->dirty || !disk->can_write) {
		work_close(&job);
		return status;
	}
	status = read_block(&job, 0);
	if (status != B_OK) {
		work_close(&job);
		return status;
	}
	uint16 flags = (uint16)(disk->volume_flags & ~0x0002);
	w16(job.block + 106, flags);
	uint8 percent = 0xff;
	if (disk->cluster_count != 0) {
		uint64 used = disk->cluster_count - disk->free_clusters;
		percent = (uint8)((used * 100) / disk->cluster_count);
	}
	job.block[112] = percent;
	status = write_block(&job, 0);
	if (status == B_OK)
		status = flush_blocks(&job, 0, 1);
	if (status == B_OK) {
		disk->volume_flags = flags;
		disk->dirty = false;
	}
	work_close(&job);
	return status;
}
