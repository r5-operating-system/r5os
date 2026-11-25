/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * R5OS home modes. Other accounts must not read private files.
 *
 * /boot/home is 0755 because extra homes live in /boot/home/users/<name>
 * and every account's Shared is /boot/home/Shared. 0711 was not enough:
 * BEntry opens the parent folder for reading, and BFS refuses that without
 * the read bit, so Tracker showed Shared as a broken link for every other
 * account (5625U, stick 81, 25 Nov 2026). Other accounts can list the names
 * directly under /boot/home, not what is in them: private names under
 * /boot/home are 0700/0600.
 * After Sign in, the first account is a person uid, so these bits apply.
 */


#include "HomeLock.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <dirent.h>


static const char* kHome = "/boot/home";


// The administrator owns /boot/home and can put a link in it at any time.
// Every entry is opened from its folder without following a link, and the
// mode is set on what was opened (25 Nov 2026, the review after issue
// #4643). The old lstat() and then chmod() followed a link put in between.
static void
lock_entry_at(int dirFd, const char* name, mode_t dirMode, mode_t fileMode,
	dev_t homeDevice)
{
	int fd = openat(dirFd, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK
		| O_CLOEXEC);
	if (fd < 0)
		return;
	struct stat st;
	if (fstat(fd, &st) == 0 && st.st_dev == homeDevice
		&& (S_ISDIR(st.st_mode) || S_ISREG(st.st_mode))) {
		mode_t mode = S_ISDIR(st.st_mode) ? dirMode : fileMode;
		if ((st.st_mode & 07777) != mode)
			fchmod(fd, mode);
	}
	close(fd);
}


static DIR*
open_listing(int dirFd)
{
	int listFd = dup(dirFd);
	if (listFd < 0)
		return NULL;
	DIR* dir = fdopendir(listFd);
	if (dir == NULL)
		close(listFd);
	return dir;
}


status_t
lock_private_homes()
{
	int homeFd = open(kHome, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (homeFd < 0)
		return errno != 0 ? errno : B_ENTRY_NOT_FOUND;
	struct stat homeStat;
	if (fstat(homeFd, &homeStat) != 0) {
		close(homeFd);
		return errno;
	}

	mkdirat(homeFd, "users", 0755);
	fchmod(homeFd, 0755);

	DIR* dir = open_listing(homeFd);
	if (dir == NULL) {
		close(homeFd);
		return errno;
	}
	while (struct dirent* entry = readdir(dir)) {
		if (strcmp(entry->d_name, ".") == 0
			|| strcmp(entry->d_name, "..") == 0
			|| strcmp(entry->d_name, "users") == 0
			|| strcmp(entry->d_name, "Shared") == 0)
			continue;
		lock_entry_at(homeFd, entry->d_name, 0700, 0600, homeStat.st_dev);
	}
	closedir(dir);

	int sharedFd = openat(homeFd, "Shared", O_RDONLY | O_DIRECTORY
		| O_NOFOLLOW | O_CLOEXEC);
	if (sharedFd >= 0) {
		struct stat sharedStat;
		if (fstat(sharedFd, &sharedStat) == 0
			&& sharedStat.st_dev == homeStat.st_dev
			&& (sharedStat.st_mode & 07777) != 01777)
			fchmod(sharedFd, 01777);
		close(sharedFd);
	}

	// /boot/home/users belongs to uid 0: whoever owns it can rename the
	// other homes. The first walk of /boot/home gave it to the
	// administrator until 25 Nov 2026 (issue #4644); this gives it back.
	int usersFd = openat(homeFd, "users", O_RDONLY | O_DIRECTORY | O_NOFOLLOW
		| O_CLOEXEC);
	close(homeFd);
	if (usersFd < 0)
		return B_OK;
	struct stat usersStat;
	if (fstat(usersFd, &usersStat) != 0
		|| usersStat.st_dev != homeStat.st_dev) {
		close(usersFd);
		return B_OK;
	}
	if (geteuid() == 0 && (usersStat.st_uid != 0 || usersStat.st_gid != 0))
		fchown(usersFd, 0, 0);
	fchmod(usersFd, 0755);

	dir = open_listing(usersFd);
	if (dir != NULL) {
		while (struct dirent* entry = readdir(dir)) {
			if (entry->d_name[0] == '.')
				continue;
			lock_entry_at(usersFd, entry->d_name, 0700, 0700,
				homeStat.st_dev);
		}
		closedir(dir);
	}
	close(usersFd);
	return B_OK;
}
