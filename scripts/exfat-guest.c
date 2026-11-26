/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Guest steps for the exFAT mount test. The shell mounts the card, this
 * program does the file operations, and each line goes to /dev/dprintf
 * so the host can read it on the serial port.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>


static void
say(const char* text)
{
	int fd = open("/dev/dprintf", O_WRONLY);
	if (fd < 0)
		return;
	write(fd, text, strlen(text));
	write(fd, "\n", 1);
	close(fd);
}


static void
step(const char* name, int ok)
{
	char line[128];
	if (ok)
		snprintf(line, sizeof(line), "exfat: %s ok", name);
	else
		snprintf(line, sizeof(line), "exfat: %s fail %d", name, errno);
	say(line);
}


static int
write_all(int fd, const void* buffer, size_t length)
{
	const char* bytes = (const char*)buffer;
	size_t done = 0;
	while (done < length) {
		ssize_t wrote = write(fd, bytes + done, length - done);
		if (wrote < 0)
			return -1;
		if (wrote == 0)
			return -1;
		done += (size_t)wrote;
	}
	return 0;
}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		say("exfat: usage fail");
		return 1;
	}
	if (chdir(argv[1]) != 0) {
		step("chdir", 0);
		return 1;
	}

	char hello[32];
	memset(hello, 0, sizeof(hello));
	struct stat helloStat;
	int fd = open("hello.txt", O_RDONLY);
	ssize_t got = fd < 0 ? -1 : read(fd, hello, sizeof(hello) - 1);
	if (fd >= 0)
		close(fd);
	step("read", stat("hello.txt", &helloStat) == 0 && helloStat.st_size == 12
		&& got == 12 && memcmp(hello, "hello exfat\n", 12) == 0);

	/* Seeded folder CritSec contains alpha.txt, an entry set with an unknown
	   0xC2 critical secondary ("bad"), and omega.txt. List it through the
	   real driver (readdir) and open both normal files by name. */
	{
		int sawAlpha = 0;
		int sawOmega = 0;
		int sawBad = 0;
		int dirOk = 0;
		DIR* dir = opendir("CritSec");
		if (dir != NULL) {
			errno = 0;
			struct dirent* ent;
			while ((ent = readdir(dir)) != NULL) {
				if (strcmp(ent->d_name, "alpha.txt") == 0)
					sawAlpha = 1;
				else if (strcmp(ent->d_name, "omega.txt") == 0)
					sawOmega = 1;
				else if (strcmp(ent->d_name, "bad") == 0)
					sawBad = 1;
			}
			if (errno == 0)
				dirOk = 1;
			closedir(dir);
		}
		char bufA[16];
		char bufO[16];
		memset(bufA, 0, sizeof(bufA));
		memset(bufO, 0, sizeof(bufO));
		int fdA = open("CritSec/alpha.txt", O_RDONLY);
		ssize_t gotA = fdA < 0 ? -1 : read(fdA, bufA, sizeof(bufA) - 1);
		if (fdA >= 0)
			close(fdA);
		int fdO = open("CritSec/omega.txt", O_RDONLY);
		ssize_t gotO = fdO < 0 ? -1 : read(fdO, bufO, sizeof(bufO) - 1);
		if (fdO >= 0)
			close(fdO);
		step("crit-secondary", dirOk && sawAlpha && sawOmega && !sawBad
			&& gotA == 6 && memcmp(bufA, "alpha\n", 6) == 0
			&& gotO == 6 && memcmp(bufO, "omega\n", 6) == 0);
	}

	char odd[1000];
	memset(odd, 'a', sizeof(odd));
	fd = open("odd.bin", O_RDWR | O_CREAT | O_TRUNC, 0644);
	int ok = fd >= 0 && write_all(fd, odd, sizeof(odd)) == 0;
	if (ok)
		ok = write_all(fd, odd, 1000) == 0;
	if (ok && lseek(fd, 0, SEEK_SET) == 0) {
		char back[64];
		ok = read(fd, back, sizeof(back)) == (ssize_t)sizeof(back)
			&& back[0] == 'a';
	}
	if (fd >= 0)
		close(fd);
	step("write", ok);

	struct stat st;
	ok = stat("odd.bin", &st) == 0 && st.st_size == 2000;
	step("append", ok);
	fd = open("odd.bin", O_RDWR);
	ok = fd >= 0 && ftruncate(fd, 1500) == 0;
	if (fd >= 0)
		close(fd);
	ok = ok && stat("odd.bin", &st) == 0 && st.st_size == 1500;
	step("truncate", ok);

	ok = rename("odd.bin", "even.bin") == 0 && access("even.bin", F_OK) == 0;
	step("rename", ok);

	ok = mkdir("Album", 0755) == 0;
	fd = ok ? open("Album/note.txt", O_RDWR | O_CREAT | O_TRUNC, 0644) : -1;
	ok = fd >= 0 && write_all(fd, "note", 4) == 0;
	if (fd >= 0)
		close(fd);
	ok = ok && rename("Album/note.txt", "Album/NOTE.TXT") == 0;
	fd = ok ? open("Album/NOTE.TXT", O_RDONLY) : -1;
	char note[8];
	got = fd < 0 ? -1 : read(fd, note, sizeof(note));
	if (fd >= 0)
		close(fd);
	step("case", ok && got == 4 && memcmp(note, "note", 4) == 0);

	fd = open("keep.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
	ok = fd >= 0 && write_all(fd, "K", 1) == 0;
	if (fd >= 0)
		close(fd);
	fd = open("drop.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
	ok = ok && fd >= 0 && write_all(fd, "D", 1) == 0;
	if (fd >= 0)
		close(fd);
	ok = ok && rename("keep.txt", "drop.txt") == 0;
	fd = ok ? open("drop.txt", O_RDONLY) : -1;
	char mark = 0;
	got = fd < 0 ? -1 : read(fd, &mark, 1);
	if (fd >= 0)
		close(fd);
	step("rename-over", ok && got == 1 && mark == 'K');

	ok = mkdir("Empty", 0755) == 0 && mkdir("Has", 0755) == 0;
	fd = ok ? open("Has/inside.txt", O_RDWR | O_CREAT | O_TRUNC, 0644) : -1;
	ok = fd >= 0 && write_all(fd, "in", 2) == 0;
	if (fd >= 0)
		close(fd);
	errno = 0;
	int replaced = rename("Empty", "Has");
	step("dir-full", replaced == -1 && errno == ENOTEMPTY);
	fd = open("Has/inside.txt", O_RDONLY);
	step("dir-child", fd >= 0);
	if (fd >= 0)
		close(fd);
	ok = rename("Has", "Empty") == 0 && access("Empty/inside.txt", F_OK) == 0;
	step("dir-empty", ok);

	ok = mkdir("Packed", 0755) == 0;
	struct stat dirStat;
	memset(&dirStat, 0, sizeof(dirStat));
	if (ok && stat("Packed", &dirStat) != 0)
		ok = 0;
	off_t length = dirStat.st_size;
	int growths = 0;
	int fit = 0;
	int made = 0;
	for (; ok && made < 4000 && growths < 2; made++) {
		char name[64];
		snprintf(name, sizeof(name), "Packed/p%04d", made);
		fd = open(name, O_RDWR | O_CREAT | O_TRUNC, 0644);
		if (fd < 0) {
			ok = 0;
			break;
		}
		close(fd);
		if (stat("Packed", &dirStat) == 0 && dirStat.st_size != length) {
			if (growths == 0)
				fit = made;
			growths++;
			length = dirStat.st_size;
		}
	}
	for (int extra = 0; ok && extra < fit - 1; extra++) {
		char name[64];
		snprintf(name, sizeof(name), "Packed/q%04d", extra);
		fd = open(name, O_RDWR | O_CREAT | O_TRUNC, 0644);
		if (fd < 0) {
			ok = 0;
			break;
		}
		close(fd);
	}
	struct stat packedBefore;
	memset(&packedBefore, 0, sizeof(packedBefore));
	char longName[64];
	memset(longName, 'n', 48);
	longName[48] = '\0';
	memcpy(longName, "Packed/", 7);
	ok = ok && growths >= 1 && stat("Packed", &packedBefore) == 0
		&& rename("Packed/p0000", longName) == 0;
	struct stat packedAfter;
	memset(&packedAfter, 0, sizeof(packedAfter));
	fd = ok ? open(longName, O_RDONLY) : -1;
	if (stat("Packed", &packedAfter) == 0) {
		char line[96];
		snprintf(line, sizeof(line), "exfat: full-dir size %lld %lld",
			(long long)packedBefore.st_size, (long long)packedAfter.st_size);
		say(line);
	}
	step("full-dir", ok && fd >= 0
		&& packedAfter.st_size > packedBefore.st_size);
	if (fd >= 0)
		close(fd);
	fd = ok ? open("Packed/p0001", O_RDONLY) : -1;
	step("full-neighbor", fd >= 0);
	if (fd >= 0)
		close(fd);

	fd = open("held", O_RDWR | O_CREAT | O_TRUNC, 0644);
	ok = fd >= 0 && write_all(fd, "old", 3) == 0;
	pid_t child = ok ? fork() : -1;
	if (child == 0) {
		sleep(2);
		ssize_t wrote = write(fd, "Z", 1);
		step("held-write", wrote == 1);
		_exit(wrote == 1 ? 0 : 1);
	}
	if (child > 0)
		sleep(1);
	ok = ok && child > 0 && unlink("held") == 0;
	int fresh = open("held", O_RDWR | O_CREAT | O_EXCL, 0644);
	ok = ok && fresh >= 0 && write_all(fresh, "new", 3) == 0;
	if (fresh >= 0)
		close(fresh);
	int childStatus = 1;
	if (child > 0)
		waitpid(child, &childStatus, 0);
	if (fd >= 0)
		close(fd);
	fd = open("held", O_RDONLY);
	char held[8];
	got = fd < 0 ? -1 : read(fd, held, sizeof(held));
	if (fd >= 0)
		close(fd);
	step("held", ok && childStatus == 0 && got == 3
		&& memcmp(held, "new", 3) == 0);

	ok = mkdir("Gone", 0755) == 0 && rmdir("Gone") == 0;
	step("rmdir", ok);

	ok = mkdir("Other", 0755) == 0;
	fd = ok ? open("cross.txt", O_RDWR | O_CREAT | O_TRUNC, 0644) : -1;
	ok = ok && fd >= 0 && write_all(fd, "XY", 2) == 0;
	if (fd >= 0)
		close(fd);
	ok = ok && rename("cross.txt", "Other/cross.txt") == 0;
	fd = ok ? open("Other/cross.txt", O_RDONLY) : -1;
	char cross[4];
	got = fd < 0 ? -1 : read(fd, cross, sizeof(cross));
	if (fd >= 0)
		close(fd);
	step("cross", ok && got == 2 && memcmp(cross, "XY", 2) == 0);

	fd = open("log.bin", O_RDWR | O_CREAT | O_TRUNC, 0644);
	char head[1000];
	char tail[1000];
	memset(head, 'A', sizeof(head));
	memset(tail, 'B', sizeof(tail));
	ok = fd >= 0 && write_all(fd, head, sizeof(head)) == 0;
	child = ok ? fork() : -1;
	if (child == 0) {
		sleep(1);
		ssize_t wrote = write(fd, tail, sizeof(tail));
		_exit(wrote == (ssize_t)sizeof(tail) ? 0 : 1);
	}
	if (child > 0)
		usleep(200000);
	ok = ok && child > 0 && rename("log.bin", "saved.bin") == 0;
	childStatus = 1;
	if (child > 0)
		waitpid(child, &childStatus, 0);
	char both[2000];
	got = -1;
	if (ok && fd >= 0 && lseek(fd, 0, SEEK_SET) == 0)
		got = read(fd, both, sizeof(both));
	if (fd >= 0)
		close(fd);
	ok = ok && childStatus == 0 && got == (ssize_t)sizeof(both)
		&& memcmp(both, head, sizeof(head)) == 0
		&& memcmp(both + sizeof(head), tail, sizeof(tail)) == 0;
	step("open-rename", ok);

	char* used = (char*)malloc(65536);
	ok = used != NULL;
	if (ok)
		memset(used, 'Z', 65536);
	fd = ok ? open("old.bin", O_RDWR | O_CREAT | O_TRUNC, 0644) : -1;
	ok = ok && fd >= 0 && write_all(fd, used, 65536) == 0;
	if (fd >= 0)
		close(fd);
	ok = ok && unlink("old.bin") == 0;
	fd = ok ? open("pre.bin", O_RDWR | O_CREAT | O_TRUNC, 0644) : -1;
	ok = ok && fd >= 0 && ftruncate(fd, 65536) == 0
		&& ftruncate(fd, 32768) == 0;
	if (fd >= 0)
		close(fd);
	fd = ok ? open("pre.bin", O_RDONLY) : -1;
	char* trimmed = (char*)malloc(32768);
	if (trimmed != NULL)
		memset(trimmed, 1, 32768);
	got = fd < 0 || trimmed == NULL ? -1 : read(fd, trimmed, 32768);
	if (fd >= 0)
		close(fd);
	ok = ok && trimmed != NULL && got == 32768;
	for (int i = 0; ok && i < 32768; i++) {
		if (trimmed[i] != 0)
			ok = 0;
	}
	free(used);
	free(trimmed);
	step("prealloc", ok);

	say("exfat: fill start");
	int holes = 0;
	for (; holes < 3000; holes++) {
		char name[64];
		snprintf(name, sizeof(name), "hole%04d", holes);
		fd = open(name, O_RDWR | O_CREAT | O_EXCL, 0644);
		if (fd < 0)
			break;
		char byte = 'h';
		if (write(fd, &byte, 1) != 1) {
			close(fd);
			unlink(name);
			break;
		}
		close(fd);
		if (holes > 0 && holes % 400 == 0) {
			char line[64];
			snprintf(line, sizeof(line), "exfat: fill %d", holes);
			say(line);
		}
	}
	int dropped = 0;
	for (int i = 1; i < holes; i += 2) {
		char name[64];
		snprintf(name, sizeof(name), "hole%04d", i);
		if (unlink(name) == 0)
			dropped++;
	}
	char* block = (char*)malloc(32768);
	char* back = (char*)malloc(32768);
	if (block != NULL)
		memset(block, 'F', 32768);
	fd = open("frag.bin", O_RDWR | O_CREAT | O_TRUNC, 0644);
	ok = fd >= 0 && dropped > 0 && block != NULL && back != NULL;
	for (int n = 0; ok && n < 3; n++)
		ok = write_all(fd, block, 32768) == 0;
	if (ok && lseek(fd, 0, SEEK_SET) == 0) {
		for (int n = 0; ok && n < 3; n++) {
			ok = read(fd, back, 32768) == 32768
				&& memcmp(back, block, 32768) == 0;
		}
	}
	if (fd >= 0)
		close(fd);
	free(block);
	free(back);
	step("fragment", ok);

	char pull[4196];
	memset(pull, 'P', sizeof(pull));
	fd = open("pull.bin", O_RDWR | O_CREAT | O_TRUNC, 0644);
	ok = fd >= 0 && write_all(fd, pull, sizeof(pull)) == 0;
	if (fd >= 0)
		close(fd);
	step("pull-close", ok);

	fd = open("sync.bin", O_RDWR | O_CREAT | O_TRUNC, 0644);
	char oldByte[1000];
	char newByte[1000];
	memset(oldByte, 'S', sizeof(oldByte));
	memset(newByte, 'T', sizeof(newByte));
	ok = fd >= 0 && write_all(fd, oldByte, sizeof(oldByte)) == 0;
	if (fd >= 0)
		close(fd);
	fd = ok ? open("sync.bin", O_RDWR) : -1;
	ok = ok && fd >= 0 && write_all(fd, newByte, sizeof(newByte)) == 0
		&& fsync(fd) == 0;
	step("fsync", ok);
	// Nothing is written after this. The host stops on the line, while
	// sync.bin stays open, so a later close or page write is not the flush.
	say("exfat: pull");
	if (access("/boot/home/exfat-unmount", F_OK) == 0)
		return ok ? 0 : 1;
	for (;;)
		sleep(60);
	return 0;
}
