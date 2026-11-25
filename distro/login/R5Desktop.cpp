/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * Drop to the account named in the session file and start the job as a
 * new team. Exec would keep the team name r5-desktop in Deskbar.
 * The helper then exits. Do not wrap launch_daemon jobs with this
 * binary: a service whose team exits is restarted forever.
 * launch_daemon and Login stay uid 0.
 */


#include "SessionLog.h"

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <image.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

#include <OS.h>
#include <R5Admin.h>


extern char** environ;


static const char* kSessionPath =
	"/boot/system/settings/r5-desktop-session";


static bool
read_session_name(char* name, size_t nameSize)
{
	FILE* file = fopen(kSessionPath, "r");
	if (file == NULL)
		return false;
	if (fgets(name, nameSize, file) == NULL) {
		fclose(file);
		return false;
	}
	fclose(file);

	size_t length = strlen(name);
	while (length > 0
		&& (name[length - 1] == '\n' || name[length - 1] == '\r'
			|| name[length - 1] == ' ')) {
		name[--length] = '\0';
	}
	return length > 0;
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// Issue #4643: this runs as uid 0, before the drop, in a home that the
// person owns. Every part of the path is opened without following a link
// (r5_admin_open_nofollow(), libr5admin.so), and the owner and mode are set
// on the open directory or file, so nothing outside the home changes owner.
static void
own_directory(const struct passwd* pwd, const char* relative)
{
	int fd = r5_admin_open_nofollow(pwd->pw_dir, relative,
		O_RDONLY | O_DIRECTORY, 0700);
	if (fd < 0) {
		r5_session_log("settings %s/%s: %s", pwd->pw_dir, relative,
			strerror(errno));
		return;
	}
	status_t status = r5_admin_set_owner(fd, pwd->pw_uid, pwd->pw_gid, 0700);
	if (status != B_OK) {
		r5_session_log("settings own %s/%s: %s", pwd->pw_dir, relative,
			strerror(status));
	}
	close(fd);
}


static void
ensure_user_cache(const struct passwd* pwd)
{
	if (pwd->pw_dir == NULL || pwd->pw_dir[0] == '\0')
		return;
	// The image creates this directory as root, mode 0755. Mesa then
	// cannot create mesa_shader_cache for the session user.
	own_directory(pwd, "config/cache");
}


static void
own_file_if_present(const struct passwd* pwd, const char* relative)
{
	int fd = r5_admin_open_nofollow(pwd->pw_dir, relative, O_RDONLY);
	if (fd < 0)
		return;
	struct stat st;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
		close(fd);
		return;
	}
	status_t status = r5_admin_set_owner(fd, pwd->pw_uid, pwd->pw_gid, 0644);
	if (status != B_OK) {
		r5_session_log("settings own %s/%s: %s", pwd->pw_dir, relative,
			strerror(status));
	}
	close(fd);
}
// SPDX-SnippetEnd


static void
ensure_user_settings(const struct passwd* pwd)
{
	if (pwd->pw_dir == NULL || pwd->pw_dir[0] == '\0')
		return;

	// config is the packagefs root. packagefs has no write_stat, so chown
	// and chmod on config itself fail. The image still creates
	// config/settings and config/cache as root, mode 0755. Those, and the
	// directories under settings, are the ones the session user must own.
	char configPath[B_PATH_NAME_LENGTH];
	int configWritten = snprintf(configPath, sizeof(configPath),
		"%s/config", pwd->pw_dir);
	if (configWritten > 0 && (size_t)configWritten < sizeof(configPath)) {
		int stamp = open("/tmp/r5-config-note", O_CREAT | O_EXCL | O_WRONLY,
			0644);
		if (stamp >= 0) {
			close(stamp);
			r5_session_log("config %s is the package root and is left alone",
				configPath);
		}
	}
	const char* suffixes[] = {
		"config/settings",
		"config/settings/system",
		"config/settings/kernel",
		"config/settings/kernel/drivers",
		"config/settings/r5",
		"config/settings/deskbar",
		NULL
	};
	for (int i = 0; suffixes[i] != NULL; i++)
		own_directory(pwd, suffixes[i]);
	const char* files[] = {
		"config/settings/r5/theme",
		"config/settings/deskbar/settings",
		NULL
	};
	for (int i = 0; files[i] != NULL; i++)
		own_file_if_present(pwd, files[i]);
}


int
main(int argc, char** argv)
{
	if (argc < 2 || argv[1] == NULL || argv[1][0] == '\0') {
		r5_session_log("missing command");
		syslog(LOG_ERR, "r5-desktop: missing command");
		return 1;
	}

	char name[64];
	if (!read_session_name(name, sizeof(name))) {
		r5_session_log("no session file for %s", argv[1]);
		syslog(LOG_ERR, "r5-desktop: no session file");
		return 1;
	}

	struct passwd* pwd = getpwnam(name);
	if (pwd == NULL || strcmp(pwd->pw_name, name) != 0 || pwd->pw_uid == 0) {
		r5_session_log("session user %s uid=%d not allowed", name,
			pwd != NULL ? (int)pwd->pw_uid : -1);
		syslog(LOG_ERR, "r5-desktop: session user %s is not allowed", name);
		return 1;
	}

	r5_session_log("session %s uid=%d gid=%d cmd=%s", name, (int)pwd->pw_uid,
		(int)pwd->pw_gid, argv[1]);

	if (pwd->pw_dir != NULL && pwd->pw_dir[0] != '\0')
		setenv("HOME", pwd->pw_dir, 1);
	setenv("USER", pwd->pw_name, 1);
	setenv("LOGNAME", pwd->pw_name, 1);
	if (pwd->pw_shell != NULL && pwd->pw_shell[0] != '\0')
		setenv("SHELL", pwd->pw_shell, 1);

	ensure_user_cache(pwd);
	ensure_user_settings(pwd);

	if (setresgid(pwd->pw_gid, pwd->pw_gid, pwd->pw_gid) != 0) {
		r5_session_log("setresgid: %s", strerror(errno));
		syslog(LOG_ERR, "r5-desktop: setresgid: %s", strerror(errno));
		return 1;
	}
	if (initgroups(pwd->pw_name, pwd->pw_gid) != 0) {
		r5_session_log("initgroups: %s", strerror(errno));
		syslog(LOG_ERR, "r5-desktop: initgroups: %s", strerror(errno));
		return 1;
	}
	if (setresuid(pwd->pw_uid, pwd->pw_uid, pwd->pw_uid) != 0
		|| geteuid() == 0) {
		r5_session_log("setresuid: %s euid=%d", strerror(errno),
			(int)geteuid());
		syslog(LOG_ERR, "r5-desktop: setresuid: %s", strerror(errno));
		return 1;
	}

	int reportTeamFd = -1;
	const char* reportFdEnv = getenv("R5_REPORT_TEAM_FD");
	if (reportFdEnv != NULL && reportFdEnv[0] != '\0') {
		char* end = NULL;
		long parsed = strtol(reportFdEnv, &end, 10);
		if (end != reportFdEnv && *end == '\0' && parsed >= 3 && parsed < 1024)
			reportTeamFd = static_cast<int>(parsed);
		unsetenv("R5_REPORT_TEAM_FD");
	}
	if (reportTeamFd >= 0)
		fcntl(reportTeamFd, F_SETFD, FD_CLOEXEC);

	r5_session_log("dropped uid=%d euid=%d, load_image %s", (int)getuid(),
		(int)geteuid(), argv[1]);

	thread_id thread = load_image(argc - 1, (const char**)(argv + 1),
		(const char**)environ);
	if (thread < B_OK) {
		if (reportTeamFd >= 0)
			close(reportTeamFd);
		r5_session_log("load_image %s: %s", argv[1], strerror(thread));
		syslog(LOG_ERR, "r5-desktop: load_image %s: %s", argv[1],
			strerror(thread));
		return 1;
	}
	if (reportTeamFd >= 0) {
		thread_info info;
		team_id launchedTeam = (get_thread_info(thread, &info) == B_OK)
			? info.team : -1;
		write(reportTeamFd, &launchedTeam, sizeof(launchedTeam));
		close(reportTeamFd);
	}
	resume_thread(thread);
	r5_session_log("started team thread %d for %s", (int)thread, argv[1]);
	return 0;
}
