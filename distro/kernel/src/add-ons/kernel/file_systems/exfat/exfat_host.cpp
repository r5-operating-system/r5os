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
	store.drive_flush = NULL;
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
	exfat_node longRead;
	status = exfat_read_entry(&store, &disk, &root, longFile.entry_index,
		&longRead);
	if (status != B_OK || longRead.secondary_count != 18)
		return fail("read 255-char entry", status);
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

	exfat_node has;
	status = exfat_create(&store, &disk, NULL, &root, "Has", true, 0, &time,
		&has);
	if (status != B_OK)
		return fail("has", status);
	has.up = &root;
	exfat_node inside;
	status = exfat_create(&store, &disk, &root, &has, "inside.txt", false, 0,
		&time, &inside);
	if (status != B_OK)
		return fail("inside", status);
	exfat_node emptyDir;
	status = exfat_create(&store, &disk, NULL, &root, "Empty", true, 0, &time,
		&emptyDir);
	if (status != B_OK)
		return fail("empty dir", status);
	emptyDir.up = &root;
	status = exfat_rename(&store, &disk, &root, &root, "Empty", NULL, &root,
		"Has", &time, &renamed);
	if (status != B_DIRECTORY_NOT_EMPTY)
		return fail("rename onto full dir", status);
	status = exfat_lookup(&store, &disk, &has, "inside.txt", &found);
	if (status != B_OK)
		return fail("inside remains", status);
	status = exfat_rename(&store, &disk, &root, &root, "Has", NULL, &root,
		"Empty", &time, &renamed);
	if (status != B_OK)
		return fail("rename onto empty dir", status);
	status = exfat_deactivate(&store, &disk, &root, emptyDir.entry_index,
		(uint32)emptyDir.secondary_count + 1);
	if (status != B_OK)
		return fail("deactivate empty", status);
	status = exfat_free_allocation(&store, &disk, emptyDir.first_cluster,
		emptyDir.data_length, emptyDir.flags);
	if (status != B_OK)
		return fail("free empty", status);

	exfat_node box;
	status = exfat_create(&store, &disk, NULL, &root, "Packed", true, 0,
		&time, &box);
	if (status != B_OK)
		return fail("packed", status);
	box.up = &root;
	uint64 packedLength = box.data_length;
	int growths = 0;
	int fit = 0;
	for (int made = 0; made < 8000 && growths < 2; made++) {
		char name[32];
		snprintf(name, sizeof(name), "p%04d", made);
		exfat_node one;
		status = exfat_create(&store, &disk, &root, &box, name, false, 0,
			&time, &one);
		if (status != B_OK)
			return fail("pack file", status);
		if (box.data_length != packedLength) {
			if (growths == 0)
				fit = made;
			growths++;
			packedLength = box.data_length;
		}
	}
	if (growths < 2 || fit < 1)
		return fail("pack grow", growths);
	for (int extra = 0; extra < fit - 1; extra++) {
		char name[32];
		snprintf(name, sizeof(name), "q%04d", extra);
		exfat_node one;
		status = exfat_create(&store, &disk, &root, &box, name, false, 0,
			&time, &one);
		if (status != B_OK)
			return fail("pack fill", status);
	}
	uint64 beforeRename = box.data_length;
	status = exfat_rename(&store, &disk, &root, &box, "p0000", &root, &box,
		"moved000", &time, &renamed);
	if (status != B_OK)
		return fail("rename full", status);
	if (box.data_length < beforeRename)
		return fail("folder shrank", status);
	status = exfat_lookup(&store, &disk, &box, "moved000", &found);
	if (status != B_OK)
		return fail("moved in full dir", status);
	status = exfat_lookup(&store, &disk, &box, "p0001", &found);
	if (status != B_OK)
		return fail("neighbor in full dir", status);

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
	store.drive_flush = NULL;
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

static int
seed(const char* path)
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
	store.drive_flush = NULL;
	exfat_disk disk;
	status_t status = exfat_disk_open(&store, boot, &disk);
	if (status != B_OK || !disk.can_write) {
		fprintf(stderr, "seed open %d %s\n", status, disk.read_only_reason);
		exfat_disk_close(&disk);
		close(fd);
		return 1;
	}
	exfat_node root;
	status = exfat_root(&store, &disk, &root);
	if (status != B_OK) {
		exfat_disk_close(&disk);
		close(fd);
		return fail("seed root", status);
	}
	exfat_time time = now_time();
	exfat_node file;
	status = exfat_create(&store, &disk, NULL, &root, "hello.txt", false, 0,
		&time, &file);
	const char* text = "hello exfat\n";
	uint64 moved = 0;
	if (status == B_OK) {
		status = exfat_write_bytes(&store, &disk, &root, &file, 0, text,
			strlen(text), &time, &moved);
	}
	if (status == B_OK)
		status = exfat_sync(&store, &disk);
	exfat_disk_close(&disk);
	close(fd);
	if (status != B_OK || moved != strlen(text))
		return fail("seed", status);
	printf("seed %s\n", path);
	return 0;
}

static int
expect_mark(exfat_store* store, exfat_disk* disk, exfat_node* root,
	const char* name, unsigned char mark, uint64 length)
{
	exfat_node found;
	status_t status = exfat_lookup(store, disk, root, name, &found);
	if (status != B_OK)
		return fail(name, status);
	printf("%s valid %llu data %llu flags 0x%x first %u\n", name,
		(unsigned long long)found.valid_length,
		(unsigned long long)found.data_length, found.flags,
		found.first_cluster);
	if (found.valid_length != length || found.data_length < length)
		return fail(name, B_BAD_DATA);
	uint8* buf = (uint8*)malloc((size_t)length);
	if (buf == NULL)
		return fail(name, B_NO_MEMORY);
	uint64 moved = 0;
	status = exfat_read_bytes(store, disk, &found, 0, buf, length, &moved);
	int bad = status != B_OK || moved != length;
	for (uint64 i = 0; !bad && i < length; i++) {
		if (buf[i] != mark)
			bad = 1;
	}
	free(buf);
	if (bad)
		return fail(name, status == B_OK ? B_BAD_DATA : status);
	if ((found.flags & 0x02) == 0)
		printf("%s is FAT-chained\n", name);
	else
		printf("%s is contiguous\n", name);
	return 0;
}

static int
pull(const char* path)
{
	int fd = open(path, O_RDONLY);
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
		close(fd);
		return fail("sector shift", B_BAD_DATA);
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
	store.drive_flush = NULL;
	exfat_disk disk;
	status_t status = exfat_disk_open(&store, boot, &disk);
	if (status != B_OK) {
		close(fd);
		return fail("pull open", status);
	}
	fprintf(stderr, "pull open: %s\n", disk.read_only_reason);
	exfat_node root;
	status = exfat_root(&store, &disk, &root);
	int result = 0;
	if (status != B_OK)
		result = fail("pull root", status);
	if (result == 0)
		result = expect_mark(&store, &disk, &root, "even.bin", 'a', 1500);
	if (result == 0)
		result = expect_mark(&store, &disk, &root, "sync.bin", 'T', 1000);
	if (result == 0)
		result = expect_mark(&store, &disk, &root, "pull.bin", 'P', 4196);
	if (result == 0)
		result = expect_mark(&store, &disk, &root, "frag.bin", 'F',
			3u * 32768u);
	exfat_disk_close(&disk);
	close(fd);
	if (result == 0)
		printf("pull %s\n", path);
	return result;
}

static int
test_bitmap_next_free_edge(void)
{
	/* Regression test for bitmap_next_free(): pass 1 limit in the middle of a
	   byte (cluster 7 = bit 5), bit 0 (cluster 2) free, bits 1-5 (clusters 3-7)
	   used, hint at cluster 8 (bits 6-7 used), plus one more free cluster (10)
	   so free_clusters == 2. Allocating count = 2 must not loop forever. */
	const uint32 blockSize = 512;
	const uint32 totalBlocks = 64;
	uint8* mem = (uint8*)calloc(totalBlocks, blockSize);
	if (mem == NULL)
		return fail("bitmap edge alloc", B_NO_MEMORY);

	struct mem_store {
		uint8*	data;
		uint32	block_size;
		uint32	blocks;
	} state = { mem, blockSize, totalBlocks };

	exfat_store store;
	memset(&store, 0, sizeof(store));
	store.cookie = &state;
	store.read = [](void* cookie, uint64 block, void* buf, uint32 blocks) -> status_t {
		mem_store* s = (mem_store*)cookie;
		if (block + blocks > s->blocks)
			return B_IO_ERROR;
		memcpy(buf, s->data + block * s->block_size, (size_t)blocks * s->block_size);
		return B_OK;
	};
	store.write = [](void* cookie, uint64 block, const void* buf, uint32 blocks) -> status_t {
		mem_store* s = (mem_store*)cookie;
		if (block + blocks > s->blocks)
			return B_IO_ERROR;
		memcpy(s->data + block * s->block_size, buf, (size_t)blocks * s->block_size);
		return B_OK;
	};
	store.flush = [](void*, uint64, uint32) -> status_t { return B_OK; };

	exfat_disk disk;
	memset(&disk, 0, sizeof(disk));
	disk.block_size = blockSize;
	disk.blocks_per_cluster_shift = 0; /* 1 block (512 B) per cluster */
	disk.cluster_count = 16; /* clusters 2..17 */
	disk.fat_block = 4;
	disk.data_block = 8;
	disk.root_cluster = 3;
	disk.can_write = true;
	disk.dirty = true;
	disk.bitmap_bytes = 2; /* 16 bits */
	disk.bitmap = (uint8*)calloc(1, disk.bitmap_bytes);
	disk.bitmap_clusters = (uint32*)calloc(1, sizeof(uint32));
	if (disk.bitmap == NULL || disk.bitmap_clusters == NULL) {
		exfat_disk_close(&disk);
		free(mem);
		return fail("bitmap edge disk", B_NO_MEMORY);
	}
	disk.bitmap_clusters[0] = 4;
	disk.bitmap_cluster_count = 1;
	/* Byte 0 (clusters 2..9): bit 0 (cluster 2) FREE, bits 1..7 (3..9) USED -> 0xFE */
	disk.bitmap[0] = 0xfe;
	/* Byte 1 (clusters 10..17): bit 0 (cluster 10) FREE, bits 1..7 (11..17) USED -> 0xFE */
	disk.bitmap[1] = 0xfe;
	disk.free_clusters = 2;
	disk.hint = 8; /* pass 0 scans 8..17, pass 1 scans 2..7 (limit=7, bit 5) */

	exfat_node node;
	memset(&node, 0, sizeof(node));
	node.detached = true; /* avoid directory write; exercises alloc_run + alloc_one */
	node.flags = 0x01;
	status_t status = exfat_set_size(&store, &disk, NULL, &node,
		2u * blockSize, 0, NULL, false);
	int result = 0;
	if (status != B_OK || disk.free_clusters != 0 || node.first_cluster < 2)
		result = fail("bitmap_next_free edge", status);
	exfat_disk_close(&disk);
	free(mem);
	return result;
}

int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: exfat_host <image> [large|cut|seed|pull]\n");
		return 2;
	}
	if (argc > 2 && strcmp(argv[2], "cut") == 0)
		return cut_power(argv[1]);
	if (argc > 2 && strcmp(argv[2], "seed") == 0)
		return seed(argv[1]);
	if (argc > 2 && strcmp(argv[2], "pull") == 0)
		return pull(argv[1]);
	if (test_bitmap_next_free_edge() != 0)
		return 1;
	bool large = argc > 2 && strcmp(argv[2], "large") == 0;
	return run(argv[1], large);
}
