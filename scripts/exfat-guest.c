/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Guest steps for the exFAT mount test. The shell mounts the card, this
 * program does the file operations, and each line goes to /dev/dprintf
 * so the host can read it on the serial port.
 */

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
	for (int extra = 0; ok && extra < fit; extra++) {
		char name[64];
		snprintf(name, sizeof(name), "Packed/q%04d", extra);
		fd = open(name, O_RDWR | O_CREAT | O_TRUNC, 0644);
		if (fd < 0) {
			ok = 0;
			break;
		}
		close(fd);
	}
	ok = ok && growths >= 1
		&& rename("Packed/p0000", "Packed/moved000") == 0;
	fd = ok ? open("Packed/moved000", O_RDONLY) : -1;
	step("full-dir", ok && fd >= 0);
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
	say("exfat: ops done");
	return 0;
}
