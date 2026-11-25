/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Host exercise for the exFAT writer. It speaks to a disk image through the
 * same exfat_update.cpp the kernel add-on uses. mkfs.exfat and fsck.exfat
 * are external tools; their source is not compiled in.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define EXFAT_HOST_TEST 1
#include "exfat_update.h"

struct image {
	int		fd;
	uint32	block_size;
	int		writes_left;
};

static status_t
image_read(void* cookie, uint64 block, void* buffer, uint32 blocks)
{
	image* disk = (image*)cookie;
	uint64 offset = block * disk->block_size;
	size_t length = (size_t)blocks * disk->block_size;
	uint8* out = (uint8*)buffer;
	while (length > 0) {
		ssize_t got = pread(disk->fd, out, length, (off_t)offset);
		if (got < 0)
			return B_IO_ERROR;
		if (got == 0)
			return B_IO_ERROR;
		out += got;
		offset += (uint64)got;
		length -= (size_t)got;
	}
	return B_OK;
}

static status_t
image_write(void* cookie, uint64 block, const void* buffer, uint32 blocks)
{
	image* disk = (image*)cookie;
	if (disk->writes_left == 0)
		return B_IO_ERROR;
	if (disk->writes_left > 0)
		disk->writes_left--;
	uint64 offset = block * disk->block_size;
	size_t length = (size_t)blocks * disk->block_size;
	const uint8* in = (const uint8*)buffer;
	while (length > 0) {
		ssize_t put = pwrite(disk->fd, in, length, (off_t)offset);
		if (put < 0)
			return B_IO_ERROR;
		if (put == 0)
			return B_IO_ERROR;
		in += put;
		offset += (uint64)put;
		length -= (size_t)put;
	}
	return B_OK;
}

static status_t
image_flush(void* cookie, uint64 block, uint32 blocks)
{
	(void)block;
	(void)blocks;
	image* disk = (image*)cookie;
	if (fsync(disk->fd) != 0)
		return B_IO_ERROR;
	return B_OK;
}

static exfat_time
now_time(void)
{
	exfat_time time;
	memset(&time, 0, sizeof(time));
	time.unix_seconds = 1764086880; /* 2026-11-25 16:08:00 UTC */
	time.offset_minutes = 60;
	time.offset_valid = true;
	return time;
}

static int
fail(const char* what, status_t status)
{
	fprintf(stderr, "fail: %s (%d)\n", what, status);
	return 1;
}

static int
run(const char* path, bool large)
{
	int fd = open(path, O_RDWR);
	if (fd < 0) {
		perror(path);
		return 1;
	}
	uint8 boot[512];
	if (pread(fd, boot, sizeof(boot), 0) != (ssize_t)sizeof(boot)) {
		perror("boot");
		close(fd);
		return 1;
	}
	uint8 shift = boot[108];
	if (shift < 9 || shift > 12) {
		fprintf(stderr, "unexpected sector shift %u\n", shift);
		close(fd);
		return 1;
	}
	image media;
	media.fd = fd;
	media.block_size = 1u << shift;
	media.writes_left = -1;
	exfat_store store;
	store.cookie = &media;
	store.read = image_read;
	store.write = image_write;
	store.flush = image_flush;
	exfat_disk disk;
	status_t status = exfat_disk_open(&store, boot, &disk);
	if (status != B_OK) {
		close(fd);
		return fail("open", status);
	}
	if (!disk.can_write) {
		fprintf(stderr, "not writable: %s\n", disk.read_only_reason);
		exfat_disk_close(&disk);
		close(fd);
		return 1;
	}
	exfat_node root;
	status = exfat_root(&store, &disk, &root);
	if (status != B_OK) {
		exfat_disk_close(&disk);
		close(fd);
		return fail("root", status);
	}
	exfat_time time = now_time();
	exfat_node file;
	status = exfat_create(&store, &disk, NULL, &root, "hello.txt", false, 0,
		&time, &file);
	if (status != B_OK)
		return fail("create", status);
	const char* text = "hello exfat";
	uint64 moved = 0;
	status = exfat_write_bytes(&store, &disk, &root, &file, 0, text,
		strlen(text), &time, &moved);
	if (status != B_OK || moved != strlen(text))
		return fail("write", status);
	char back[32];
	memset(back, 0, sizeof(back));
	status = exfat_read_bytes(&store, &disk, &file, 0, back, strlen(text),
		&moved);
	if (status != B_OK || memcmp(back, text, strlen(text)) != 0)
		return fail("read back", status);

	exfat_node renamed;
	status = exfat_rename(&store, &disk, NULL, &root, "hello.txt", NULL, &root,
		"Hello.TXT", &time, &renamed);
	if (status != B_OK)
		return fail("rename same dir", status);
	exfat_node found;
	status = exfat_lookup(&store, &disk, &root, "hello.txt", &found);
	if (status != B_OK)
		return fail("case lookup", status);

	exfat_node folder;
	status = exfat_create(&store, &disk, NULL, &root, "Album", true, 0, &time,
		&folder);
	if (status != B_OK)
		return fail("mkdir", status);
	folder.up = &root;
	exfat_node nested;
	status = exfat_create(&store, &disk, &root, &folder, "photo.bin", false, 0,
		&time, &nested);
	if (status != B_OK)
		return fail("create nested", status);
	uint8 block[4096];
	memset(block, 0x5a, sizeof(block));
	status = exfat_write_bytes(&store, &disk, &folder, &nested, 0, block,
		sizeof(block), &time, &moved);
	if (status != B_OK)
		return fail("write nested", status);
	exfat_node note;
	status = exfat_create(&store, &disk, &root, &folder, "note.txt", false, 0,
		&time, &note);
	if (status != B_OK)
		return fail("note", status);
	note.up = &folder;
	status = exfat_rename(&store, &disk, &root, &folder, "note.txt", &root,
		&folder, "NOTE.TXT", &time, &renamed);
	if (status != B_OK)
		return fail("case in subdir", status);
	status = exfat_lookup(&store, &disk, &folder, "NOTE.TXT", &found);
	if (status != B_OK)
		return fail("case lookup", status);
	status = exfat_lookup(&store, &disk, &root, "Album", &found);
	if (status != B_OK || !found.is_dir)
		return fail("parent intact", status);
	exfat_node drop;
	status = exfat_create(&store, &disk, &root, &folder, "drop.txt", false, 0,
		&time, &drop);
	if (status != B_OK)
		return fail("drop", status);
	exfat_node keep;
	status = exfat_create(&store, &disk, &root, &folder, "keep.txt", false, 0,
		&time, &keep);
	if (status != B_OK)
		return fail("keep", status);
	const char* mark = "K";
	status = exfat_write_bytes(&store, &disk, &folder, &keep, 0, mark, 1,
		&time, &moved);
	if (status != B_OK)
		return fail("mark", status);
	status = exfat_rename(&store, &disk, &root, &folder, "keep.txt", &root,
		&folder, "drop.txt", &time, &renamed);
	if (status != B_OK)
		return fail("rename over", status);
	status = exfat_deactivate(&store, &disk, &folder, drop.entry_index,
		(uint32)drop.secondary_count + 1);
	if (status != B_OK)
		return fail("deactivate drop", status);
	status = exfat_free_allocation(&store, &disk, drop.first_cluster,
		drop.data_length, drop.flags);
	if (status != B_OK)
		return fail("free drop", status);
	status = exfat_lookup(&store, &disk, &folder, "drop.txt", &found);
	if (status != B_OK)
		return fail("drop remains", status);
	status = exfat_read_bytes(&store, &disk, &found, 0, block, 1, &moved);
	if (status != B_OK || moved != 1 || block[0] != 'K')
		return fail("renamed bytes", status);
	uint64 physical = 0;
	uint64 run = 0;
	status = exfat_map(&store, &disk, found.first_cluster, found.data_length,
		found.flags, 0, &physical, &run);
	if (status != B_OK || physical < (uint64)disk.data_block * disk.block_size
		|| run == 0)
		return fail("map bytes", status);
	status = exfat_remove_entry(&store, &disk, &root, &folder, "NOTE.TXT",
		&found);
	if (status != B_OK)
		return fail("unlink note", status);
	status = exfat_free_allocation(&store, &disk, found.first_cluster,
		found.data_length, found.flags);
	if (status != B_OK)
		return fail("free note", status);
	status = exfat_remove_entry(&store, &disk, &root, &folder, "drop.txt",
		&found);
	if (status != B_OK)
		return fail("unlink drop", status);
	status = exfat_free_allocation(&store, &disk, found.first_cluster,
		found.data_length, found.flags);
	if (status != B_OK)
		return fail("free renamed", status);
	status = exfat_rename(&store, &disk, &root, &folder, "photo.bin", NULL,
		&root, "moved.bin", &time, &renamed);
	if (status != B_OK)
		return fail("rename across", status);
	status = exfat_set_size(&store, &disk, &root, &renamed, 100, 100, &time,
		true);
	if (status != B_OK)
		return fail("truncate", status);
	status = exfat_read_bytes(&store, &disk, &renamed, 0, block, 100, &moved);
	if (status != B_OK || moved != 100 || block[0] != 0x5a)
		return fail("truncated read", status);

	char longName[256];
	memset(longName, 'n', 255);
	longName[255] = '\0';
	exfat_node longFile;
	status = exfat_create(&store, &disk, NULL, &root, longName, false, 0,
		&time, &longFile);
	if (status != B_OK)
		return fail("long name", status);
	status = exfat_remove_entry(&store, &disk, NULL, &root, longName, &found);
	if (status != B_OK)
		return fail("unlink long", status);

	exfat_node child;
	status = exfat_create(&store, &disk, &root, &folder, "only", false, 0,
		&time, &child);
	if (status != B_OK)
		return fail("child", status);
	status = exfat_remove_entry(&store, &disk, &root, &folder, "only", &found);
	if (status != B_OK)
		return fail("unlink child", status);
	status = exfat_free_allocation(&store, &disk, found.first_cluster,
		found.data_length, found.flags);
	if (status != B_OK)
		return fail("free child", status);
	status = exfat_remove_entry(&store, &disk, NULL, &root, "Album", &found);
	if (status != B_OK)
		return fail("rmdir", status);
	status = exfat_free_allocation(&store, &disk, found.first_cluster,
		found.data_length, found.flags);
	if (status != B_OK)
		return fail("free dir", status);

	for (int index = 0; index < 300; index++) {
		char name[32];
		snprintf(name, sizeof(name), "f%03d.dat", index);
		exfat_node many;
		status = exfat_create(&store, &disk, NULL, &root, name, false, 0,
			&time, &many);
		if (status != B_OK)
			return fail(name, status);
	}

	exfat_node chain[12];
	exfat_node* parent = &root;
	for (int level = 0; level < 12; level++) {
		char name[32];
		snprintf(name, sizeof(name), "d%d", level);
		status = exfat_create(&store, &disk, parent->up, parent, name, true, 0,
			&time, &chain[level]);
		if (status != B_OK)
			return fail(name, status);
		chain[level].up = parent;
		parent = &chain[level];
	}

	if (large) {
		exfat_node big;
		status = exfat_create(&store, &disk, NULL, &root, "big.bin", false, 0,
			&time, &big);
		if (status != B_OK)
			return fail("big create", status);
		uint64 at = 4ull * 1024ull * 1024ull * 1024ull + 64;
		uint8 mark[16];
		memset(mark, 0x11, sizeof(mark));
		status = exfat_write_bytes(&store, &disk, &root, &big, at, mark,
			sizeof(mark), &time, &moved);
		if (status != B_OK)
			return fail("big write", status);
		if (big.data_length <= 4ull * 1024ull * 1024ull * 1024ull)
			return fail("big size", B_BAD_DATA);
	}

	/* Fill the rest of a small image. Stop when a one-cluster extend fails. */
	exfat_node filler;
	status = exfat_create(&store, &disk, NULL, &root, "fill.bin", false, 0,
		&time, &filler);
	if (status != B_OK)
		return fail("fill create", status);
	uint32 cluster = disk.block_size << disk.blocks_per_cluster_shift;
	uint8* chunk = (uint8*)malloc(cluster);
	if (chunk == NULL)
		return fail("chunk", B_NO_MEMORY);
	memset(chunk, 0x22, cluster);
	uint64 filled = 0;
	for (;;) {
		status = exfat_write_bytes(&store, &disk, &root, &filler, filled, chunk,
			cluster, &time, &moved);
		if (status == B_DEVICE_FULL)
			break;
		if (status != B_OK) {
			free(chunk);
			return fail("fill", status);
		}
		filled += cluster;
		if (large || filled > 8ull * 1024ull * 1024ull)
			break;
	}
	free(chunk);
	exfat_node rejected;
	status = exfat_create(&store, &disk, NULL, &root, "nospace", true, 0,
		&time, &rejected);
	if (disk.free_clusters == 0 && status != B_DEVICE_FULL && status != B_OK) {
		/* An empty directory needs a cluster. Anything but success or full
		   is a real error. A success means a hole in the directory itself. */
	}
	if (disk.free_clusters == 0 && status != B_DEVICE_FULL && status != B_OK)
		return fail("full create", status);

	status = exfat_sync(&store, &disk);
	if (status != B_OK)
		return fail("sync", status);
	exfat_disk_close(&disk);
	close(fd);
	printf("wrote %s\n", path);
	return 0;
}

static int
cut_power(const char* path)
{
	int fd = open(path, O_RDWR);
	if (fd < 0) {
		perror(path);
		return 1;
	}
	uint8 boot[512];
	if (pread(fd, boot, sizeof(boot), 0) != (ssize_t)sizeof(boot)) {
		close(fd);
		return fail("boot", B_IO_ERROR);
	}
	image media;
	media.fd = fd;
	media.block_size = 1u << boot[108];
	/* The first write marks the boot sector dirty. Later writes fail, which
	   is the power loss. */
	media.writes_left = 1;
	exfat_store store;
	store.cookie = &media;
	store.read = image_read;
	store.write = image_write;
	store.flush = image_flush;
	exfat_disk disk;
	status_t status = exfat_disk_open(&store, boot, &disk);
	if (status != B_OK || !disk.can_write) {
		close(fd);
		return fail("cut open", status);
	}
	exfat_node root;
	exfat_root(&store, &disk, &root);
	exfat_time time = now_time();
	exfat_node file;
	status = exfat_create(&store, &disk, NULL, &root, "cut.bin", false, 0,
		&time, &file);
	close(fd);
	if (status == B_OK)
		return fail("cut should have stopped", status);
	fd = open(path, O_RDWR);
	if (pread(fd, boot, sizeof(boot), 0) != (ssize_t)sizeof(boot)) {
		close(fd);
		return fail("reread boot", B_IO_ERROR);
	}
	media.fd = fd;
	media.writes_left = -1;
	store.cookie = &media;
	exfat_disk again;
	status = exfat_disk_open(&store, boot, &again);
	if (status != B_OK) {
		close(fd);
		return fail("reopen", status);
	}
	bool dirty = !again.can_write;
	fprintf(stderr, "after cut: %s\n", again.read_only_reason);
	exfat_disk_close(&again);
	close(fd);
	if (!dirty)
		return fail("expected read-only", B_BAD_DATA);
	printf("cut %s\n", path);
	return 0;
}

int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: exfat_host <image> [large|cut]\n");
		return 2;
	}
	if (argc > 2 && strcmp(argv[2], "cut") == 0)
		return cut_power(argv[1]);
	bool large = argc > 2 && strcmp(argv[2], "large") == 0;
	return run(argv[1], large);
}
