/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * R5OS account helper. Login (uid 0) migrates the first home off root
 * and applies Users & Accounts changes for an administrator sender.
 */


#include "AccountAdmin.h"

#include "HomeLock.h"
#include "KeyStoreCrypto.h"
#include "KeyStoreProtocol.h"

#include <fs_attr.h>
#include <Message.h>
#include <Node.h>
#include <OS.h>
#include <Path.h>
#include <R5Admin.h>
#include <R5Security.h>
#include <Roster.h>
#include <String.h>
#include <StringList.h>

#include <RegistrarDefs.h>
#include <syscalls.h>
#include <user_group.h>
#include <util/KMessage.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <shadow.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>


const char* kR5LoginSignature = "application/x-vnd.Haiku-Login";
const char* kAdminGroupName = "admin";
const char* kGuestAccountName = "guest";
const char* kGuestHomePath = "/boot/home/users/guest";

static const char* kHome = "/boot/home";
static const char* kUsersRoot = "/boot/home/users";
static const char* kSessionPath = "/boot/system/settings/r5-desktop-session";
static const char* kGuestEnabledPath = "/boot/system/settings/r5-guest-enabled";
// The uid of the guest account that ensure_guest_account() made. Only that
// account is the guest; an older person who happens to be called "guest" is
// not, and is never locked or wiped.
static const char* kGuestUidPath = "/boot/system/settings/r5-guest-uid";
static const char* kSystemRootName = "root";


static bool
group_has_member(const char* groupName, const char* user)
{
	struct group* group = getgrnam(groupName);
	if (group == NULL || group->gr_mem == NULL || user == NULL)
		return false;
	for (int32 i = 0; group->gr_mem[i] != NULL; i++) {
		if (strcmp(group->gr_mem[i], user) == 0)
			return true;
	}
	return false;
}


static bool
read_guest_uid(uid_t& uid)
{
	struct stat st;
	if (lstat(kGuestUidPath, &st) != 0 || !S_ISREG(st.st_mode)
		|| st.st_uid != 0 || (st.st_mode & (S_IWGRP | S_IWOTH)) != 0)
		return false;
	FILE* file = fopen(kGuestUidPath, "r");
	if (file == NULL)
		return false;
	long value = -1;
	bool ok = fscanf(file, "%ld", &value) == 1;
	fclose(file);
	if (!ok || value < (long)kMinPersonUid)
		return false;
	uid = (uid_t)value;
	return true;
}


static bool
write_guest_uid(uid_t uid)
{
	FILE* file = fopen(kGuestUidPath, "w");
	if (file == NULL)
		return false;
	bool ok = fprintf(file, "%d\n", (int)uid) > 0;
	if (fclose(file) != 0)
		ok = false;
	chown(kGuestUidPath, 0, 0);
	chmod(kGuestUidPath, 0644);
	return ok;
}


static bool
is_system_guest(const struct passwd* pwd)
{
	uid_t guestUid;
	return pwd != NULL && pwd->pw_name != NULL
		&& strcmp(pwd->pw_name, kGuestAccountName) == 0
		&& pwd->pw_dir != NULL && strcmp(pwd->pw_dir, kGuestHomePath) == 0
		&& read_guest_uid(guestUid) && pwd->pw_uid == guestUid;
}


bool
is_guest_account(const char* name)
{
	if (name == NULL || strcmp(name, kGuestAccountName) != 0)
		return false;
	// getpwnam_r(): a caller may still hold a getpwnam() result.
	struct passwd entry;
	struct passwd* pwd = NULL;
	char buffer[1024];
	if (getpwnam_r(name, &entry, buffer, sizeof(buffer), &pwd) != 0
		|| pwd == NULL)
		return false;
	return is_system_guest(pwd);
}


bool
guest_session_active()
{
	uid_t guestUid;
	if (!read_guest_uid(guestUid))
		return false;
	int32 cookie = 0;
	team_info info;
	while (get_next_team_info(&cookie, &info) == B_OK) {
		if (info.uid == guestUid && info.team > 0)
			return true;
	}
	return false;
}


bool
is_listed_account(const struct passwd* pwd)
{
	if (pwd == NULL || pwd->pw_name == NULL || pwd->pw_name[0] == '\0')
		return false;
	if (strcmp(pwd->pw_name, "sshd") == 0
		|| strcmp(pwd->pw_name, "ftp") == 0
		|| strcmp(pwd->pw_name, "nobody") == 0
		|| strcmp(pwd->pw_name, "root") == 0
		|| is_system_guest(pwd))
		return false;

	const char* home = pwd->pw_dir;
	if (home == NULL || home[0] == '\0')
		return false;
	if (strcmp(home, kHome) == 0)
		return true;
	if (strncmp(home, "/boot/home/", 11) == 0)
		return true;
	return false;
}


bool
is_admin_user(const struct passwd* pwd)
{
	if (pwd == NULL || pwd->pw_name == NULL)
		return false;

	uid_t uid = pwd->pw_uid;
	gid_t gid = pwd->pw_gid;
	BString name(pwd->pw_name);
	BString home(pwd->pw_dir != NULL ? pwd->pw_dir : "");
	if (uid == 0 || gid == 0)
		return true;
	if (home == kHome)
		return true;
	return group_has_member("wheel", name.String())
		|| group_has_member(kAdminGroupName, name.String());
}


bool
is_disabled_user(const char* name)
{
	struct spwd* shadow = getspnam(name);
	if (shadow == NULL || shadow->sp_pwdp == NULL)
		return false;
	return shadow->sp_pwdp[0] == '!';
}


bool
account_password_matches(const struct passwd* pwd, const char* plain)
{
	if (pwd == NULL)
		return false;

	if (plain == NULL)
		plain = "";

	const char* required = pwd->pw_passwd;
	if (required != NULL && strcmp(required, "x") == 0) {
		struct spwd* shadow = getspnam(pwd->pw_name);
		if (shadow == NULL)
			return false;
		required = shadow->sp_pwdp;
	}

	// No password is set. Only an empty field matches.
	if (required == NULL || required[0] == '\0')
		return plain[0] == '\0';

	// A locked account refuses every field. An account with a password
	// refuses an empty field before the hash is checked.
	if (required[0] == '!' || plain[0] == '\0')
		return false;

	// Accounts step 5: a hash made by R5 is Argon2id through the Security
	// Kit. An older scrypt ("$s$") or DES hash still goes through crypt().
	if (is_kit_password_hash(required)) {
		BString hash(required);
			// getspnam() keeps its result in a static buffer.
		bool matches = false;
		return BSecurityKit::BPasswordHash::Verify(plain, hash.String(),
				&matches) == B_OK && matches;
	}

	char* encrypted = crypt(plain, required);
	return encrypted != NULL && strcmp(encrypted, required) == 0;
}


bool
is_kit_password_hash(const char* hash)
{
	static const char kPrefix[] = "$argon2id$";
	return hash != NULL && strncmp(hash, kPrefix, sizeof(kPrefix) - 1) == 0;
}


status_t
make_password_hash(const char* password, BString& outHash)
{
	outHash.Truncate(0);
	if (password == NULL || password[0] == '\0')
		return B_BAD_VALUE;
	return BSecurityKit::BPasswordHash::Hash(password, outHash,
		BSecurityKit::BPasswordHash::DefaultParameters());
}


bool
is_first_home_admin(const struct passwd* pwd)
{
	return pwd != NULL && pwd->pw_dir != NULL
		&& strcmp(pwd->pw_dir, kHome) == 0;
}


bool
current_user_is_admin()
{
	if (geteuid() == 0)
		return true;
	return is_admin_user(getpwuid(getuid()));
}


static bool
valid_username(const char* name)
{
	if (name == NULL || name[0] == '\0' || strlen(name) > 31)
		return false;
	if (!islower((unsigned char)name[0]) && name[0] != '_')
		return false;
	// The guest account's name is reserved.
	if (strcmp(name, kGuestAccountName) == 0)
		return false;
	for (const char* p = name; *p != '\0'; p++) {
		if (!islower((unsigned char)*p) && !isdigit((unsigned char)*p)
			&& *p != '_' && *p != '-')
			return false;
	}
	return true;
}


static status_t
run_argv(char** argv, BString& errorText)
{
	pid_t pid = fork();
	if (pid < 0) {
		errorText = strerror(errno);
		return errno;
	}
	if (pid == 0) {
		int nullFd = open("/dev/null", O_RDWR);
		if (nullFd >= 0) {
			dup2(nullFd, STDIN_FILENO);
			dup2(nullFd, STDOUT_FILENO);
			dup2(nullFd, STDERR_FILENO);
			if (nullFd > 2)
				close(nullFd);
		}
		execv(argv[0], argv);
		_exit(127);
	}

	int status = 0;
	if (waitpid(pid, &status, 0) < 0) {
		errorText = strerror(errno);
		return errno;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		errorText.SetToFormat("%s failed", argv[0]);
		return B_ERROR;
	}
	return B_OK;
}


static status_t
set_shadow_password(const char* user, const char* encrypted)
{
	struct passwd* pwd = getpwnam(user);
	if (pwd == NULL)
		return ENOENT;

	BPrivate::KMessage message(BPrivate::B_REG_UPDATE_USER);
	if (message.AddInt32("uid", pwd->pw_uid) != B_OK
		|| message.AddInt32("last changed", (int32)time(NULL)) != B_OK
		|| message.AddString("password", "x") != B_OK
		|| message.AddString("shadow password", encrypted) != B_OK)
		return B_NO_MEMORY;

	BPrivate::KMessage reply;
	return BPrivate::send_authentication_request_to_registrar(message, reply);
}


// Accounts step 5. The R5 image (the live medium, "Try R5") carries this
// file; the Installer does not copy it (distro/rest/src/apps/installer/
// WorkerThread.cpp), so an installed R5 does not have it.
static const char* kLiveMediumPath = "/boot/system/settings/r5-live-medium";


bool
is_live_medium()
{
	struct stat st;
	return lstat(kLiveMediumPath, &st) == 0 && S_ISREG(st.st_mode)
		&& st.st_uid == 0;
}


bool
account_has_no_password(const struct passwd* pwd)
{
	if (pwd == NULL || pwd->pw_name == NULL)
		return false;
	const char* required = pwd->pw_passwd;
	if (required != NULL && strcmp(required, "x") == 0) {
		struct spwd* shadow = getspnam(pwd->pw_name);
		if (shadow == NULL)
			return false;
		required = shadow->sp_pwdp;
	}
	return required == NULL || required[0] == '\0';
}


bool
needs_first_password(const char* login)
{
	if (login == NULL || is_guest_account(login) || is_live_medium())
		return false;
	struct passwd* pwd = getpwnam(login);
	return pwd != NULL && is_listed_account(pwd)
		&& account_has_no_password(pwd);
}


status_t
set_first_password(const char* login, const char* realName,
	const char* password, BString& error)
{
	if (geteuid() != 0) {
		error = "Only the sign-in helper can set this password.";
		return B_PERMISSION_DENIED;
	}
	if (!needs_first_password(login)) {
		error = "This account already has a password.";
		return B_NOT_ALLOWED;
	}
	if (password == NULL || password[0] == '\0') {
		error = "Please enter a password.";
		return B_BAD_VALUE;
	}
	if (realName != NULL && (strchr(realName, ':') != NULL
			|| strchr(realName, '\n') != NULL
			|| strchr(realName, '\r') != NULL)) {
		error = "The name cannot contain a colon or a new line.";
		return B_BAD_VALUE;
	}
	if (realName != NULL && strlen(realName) > 80) {
		error = "That name is too long.";
		return B_BAD_VALUE;
	}

	struct passwd* pwd = getpwnam(login);
	if (pwd == NULL) {
		error = "That account does not exist.";
		return ENOENT;
	}
	uid_t uid = pwd->pw_uid;

	BString hash;
	if (make_password_hash(password, hash) != B_OK) {
		error = "Could not encrypt the password.";
		return B_ERROR;
	}
	status_t status = set_shadow_password(login, hash.String());
	if (status != B_OK) {
		error = "The password could not be saved.";
		return status;
	}

	if (realName != NULL && realName[0] != '\0') {
		BPrivate::KMessage message(BPrivate::B_REG_UPDATE_USER);
		if (message.AddInt32("uid", uid) == B_OK
			&& message.AddString("real name", realName) == B_OK) {
			BPrivate::KMessage reply;
			if (BPrivate::send_authentication_request_to_registrar(message,
					reply) != B_OK) {
				// The password is set; only the name is missing.
				syslog(LOG_WARNING, "Login: the name of %s was not saved",
					login);
			}
		}
	}
	return B_OK;
}


static status_t
ensure_admin_group(BString& errorText)
{
	if (getgrnam(kAdminGroupName) != NULL)
		return B_OK;
	char* argv[] = {
		const_cast<char*>("/bin/groupadd"),
		const_cast<char*>(kAdminGroupName),
		NULL
	};
	return run_argv(argv, errorText);
}


static status_t
add_to_group(const char* groupName, const char* user, BString& errorText)
{
	if (groupName == NULL || user == NULL)
		return B_BAD_VALUE;
	if (getgrnam(groupName) == NULL)
		return B_NAME_NOT_FOUND;
	if (group_has_member(groupName, user))
		return B_OK;
	char* argv[] = {
		const_cast<char*>("/bin/groupmod"),
		const_cast<char*>("-A"),
		const_cast<char*>(user),
		const_cast<char*>(groupName),
		NULL
	};
	return run_argv(argv, errorText);
}


static status_t
remove_from_group(const char* groupName, const char* user, BString& errorText)
{
	if (groupName == NULL || user == NULL)
		return B_BAD_VALUE;
	if (getgrnam(groupName) == NULL || !group_has_member(groupName, user))
		return B_OK;
	char* argv[] = {
		const_cast<char*>("/bin/groupmod"),
		const_cast<char*>("-R"),
		const_cast<char*>(user),
		const_cast<char*>(groupName),
		NULL
	};
	return run_argv(argv, errorText);
}


static void ensure_desktop_links(const char* home);
static void ensure_private_trash(const char* home, uid_t uid, gid_t gid);
static void ensure_user_mime_db(const char* home, uid_t uid, gid_t gid);


// Haiku's image puts config/settings/first_login in the first home. Login
// runs /boot/system/boot/first-login for a home that has it, and the
// PostInstallScript removes it again.
static void
ensure_first_login_marker(const char* home, uid_t uid, gid_t gid)
{
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// Issue #4643: no part of the path may be a link.
	int fd = r5_admin_open_nofollow(home, "config/settings/first_login",
		O_WRONLY | O_CREAT | O_EXCL, 0, 0600);
	if (fd < 0)
		return;
	r5_admin_set_owner(fd, uid, gid, 0600);
	close(fd);
	// SPDX-SnippetEnd
}


// The R5 wallpaper, as the image sets it on the first home's Desktop
// (distro/rest/src/data/directory_attrs/home-desktop.rdef). A Desktop
// without this attribute showed Haiku's plain blue. A background the
// account chose itself stays.
static void
ensure_default_wallpaper(const char* home)
{
	static const char* kAttribute = "be:bgndimginfo";
	static const char* kWallpaper
		= "/boot/system/data/artwork/r5-wallpaper.jpg";

	if (access(kWallpaper, R_OK) != 0)
		return;

	BString path;
	path.SetToFormat("%s/Desktop", home);
	int fd = open(path.String(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW
		| O_CLOEXEC);
	if (fd < 0)
		return;
	attr_info info;
	if (fs_stat_attr(fd, kAttribute, &info) == 0) {
		close(fd);
		return;
	}

	BMessage background;
	background.AddString("be:bgndimginfopath", kWallpaper);
	background.AddInt32("be:bgndimginfomode", 2);
	background.AddInt32("be:bgndimginfoworkspaces", -1);
	background.AddPoint("be:bgndimginfooffset", BPoint(0, 0));
	background.AddBool("be:bgndimginfoerasetext", false);
	background.AddInt32("be:bgndimginfoset", 0);

	ssize_t size = background.FlattenedSize();
	char* buffer = size > 0 ? (char*)malloc(size) : NULL;
	if (buffer != NULL && background.Flatten(buffer, size) == B_OK)
		fs_write_attr(fd, kAttribute, B_MESSAGE_TYPE, 0, buffer, size);
	free(buffer);
	close(fd);
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// Issue #4643: a directory in a person's home, owned by that person. Login
// runs as uid 0, and the person can put a link anywhere in their own home:
// no part of the path is followed (libr5admin.so). A missing directory is
// made with "mode"; its parents must already exist.
static status_t
own_home_dir(const char* home, const char* relative, uid_t uid, gid_t gid,
	mode_t mode)
{
	int fd = r5_admin_open_nofollow(home, relative, O_RDONLY | O_DIRECTORY,
		mode);
	if (fd < 0)
		return errno;
	status_t status = r5_admin_set_owner(fd, uid, gid, mode);
	close(fd);
	return status;
}


static status_t
create_home(const char* home, uid_t uid, gid_t gid)
{
	mkdir(kUsersRoot, 0755);
	if (mkdir(home, 0700) != 0 && errno != EEXIST)
		return errno;
	// Also the home itself: an old home that was kept can be a link.
	status_t status = own_home_dir(home, NULL, uid, gid, 0700);
	if (status != B_OK)
		return status;

	static const char* kFolders[] = {
		"Desktop", "Documents", "Pictures", "Videos", "Downloads", "Music",
		"config", "config/settings", NULL
	};
	for (int32 i = 0; kFolders[i] != NULL; i++)
		own_home_dir(home, kFolders[i], uid, gid, 0700);

	int existing = r5_admin_open_nofollow(home, "config/settings/deskbar",
		O_RDONLY | O_DIRECTORY);
	bool newDeskbar = existing < 0 && errno == ENOENT;
	if (existing >= 0)
		close(existing);
	own_home_dir(home, "config/settings/deskbar", uid, gid, 0700);

	// What the image gives the first home (5625U, stick 81): the first-login
	// marker, so Login runs default_deskbar_items.sh once (the battery), and
	// the R5 wallpaper. A new and a wiped guest home get both again.
	if (newDeskbar)
		ensure_first_login_marker(home, uid, gid);
	ensure_default_wallpaper(home);

	ensure_desktop_links(home);
	ensure_private_trash(home, uid, gid);
	ensure_user_mime_db(home, uid, gid);
	lock_private_homes();
	return B_OK;
}
// SPDX-SnippetEnd


int32
admin_count()
{
	int32 count = 0;
	setpwent();
	for (;;) {
		struct passwd* pwd = getpwent();
		if (pwd == NULL)
			break;
		if (!is_listed_account(pwd) || !is_admin_user(pwd))
			continue;
		if (!is_disabled_user(pwd->pw_name))
			count++;
	}
	endpwent();
	return count;
}


// The sender the kernel named for the request being handled (Login's admin
// port, libr5admin.so); unset for a request on the normal messenger.
static bool sSenderVerified = false;
static uid_t sSenderUid = (uid_t)-1;
static team_id sSenderTeam = -1;


void
set_admin_sender(bool verified, uid_t uid, team_id team)
{
	sSenderVerified = verified;
	sSenderUid = verified ? uid : (uid_t)-1;
	sSenderTeam = verified ? team : -1;
}


// The sender's team: from the kernel, or (normal messenger, only for the
// query) the reply address.
static team_id
sender_team(BMessage* request)
{
	if (sSenderVerified)
		return sSenderTeam;
	return request != NULL ? request->ReturnAddress().Team() : -1;
}


static bool
sender_is_admin(BMessage* request)
{
	if (request == NULL)
		return false;
	if (sSenderVerified) {
		if (sSenderUid == 0)
			return true;
		return is_admin_user(getpwuid(sSenderUid));
	}
	if (geteuid() == 0 && !request->IsSourceRemote())
		return true;

	team_id team = request->ReturnAddress().Team();
	team_info info;
	if (get_team_info(team, &info) != B_OK)
		return false;
	if (info.uid == 0)
		return true;
	return is_admin_user(getpwuid(info.uid));
}


static uid_t
first_free_person_uid()
{
	uid_t uid = kMinPersonUid;
	while (getpwuid(uid) != NULL)
		uid++;
	return uid;
}


static gid_t
users_gid()
{
	struct group* group = getgrnam("users");
	if (group != NULL)
		return group->gr_gid;
	return 100;
}


static status_t
add_unix_user(const char* name, uid_t uid, gid_t gid, const char* home,
	const char* shell, const char* realName, const char* shadow)
{
	BPrivate::KMessage message(BPrivate::B_REG_UPDATE_USER);
	if (message.AddInt32("uid", uid) != B_OK
		|| message.AddInt32("gid", gid) != B_OK
		|| message.AddString("name", name) != B_OK
		|| message.AddString("password", "x") != B_OK
		|| message.AddString("home", home != NULL ? home : "") != B_OK
		|| message.AddString("shell",
			shell != NULL && shell[0] != '\0' ? shell : "/bin/sh") != B_OK
		|| message.AddString("real name",
			realName != NULL ? realName : "") != B_OK
		|| message.AddString("shadow password",
			shadow != NULL ? shadow : "") != B_OK
		|| message.AddInt32("last changed", (int32)time(NULL)) != B_OK
		|| message.AddInt32("min", -1) != B_OK
		|| message.AddInt32("max", -1) != B_OK
		|| message.AddInt32("warn", 7) != B_OK
		|| message.AddInt32("inactive", -1) != B_OK
		|| message.AddInt32("expiration", 99999) != B_OK
		|| message.AddInt32("flags", 0) != B_OK
		|| message.AddBool("add user", true) != B_OK)
		return B_NO_MEMORY;

	BPrivate::KMessage reply;
	return BPrivate::send_authentication_request_to_registrar(message, reply);
}


static status_t
ensure_system_root()
{
	mkdir("/boot/system/var", 0755);
	mkdir("/boot/system/var/root", 0700);

	struct passwd* root = getpwuid(0);
	if (root == NULL) {
		if (getpwnam(kSystemRootName) != NULL)
			return B_NAME_IN_USE;
		return add_unix_user(kSystemRootName, 0, 0, "/boot/system/var/root",
			"/bin/sh", "System", "!");
	}
	if (root->pw_name == NULL
		|| strcmp(root->pw_name, kSystemRootName) != 0)
		return B_OK;

	char name[64];
	if (snprintf(name, sizeof(name), "%s", root->pw_name) >= (int)sizeof(name))
		return B_NAME_TOO_LONG;
	BString home(root->pw_dir != NULL ? root->pw_dir : "");
	bool disabled = is_disabled_user(name);
	if (home == "/boot/system/var/root" && disabled)
		return B_OK;

	// A test disk can leave root with the person's home. Sign in would
	// then list it. Keep the system account locked and out of /boot/home.
	BPrivate::KMessage message(BPrivate::B_REG_UPDATE_USER);
	if (message.AddInt32("uid", 0) != B_OK
		|| message.AddString("name", name) != B_OK
		|| message.AddString("home", "/boot/system/var/root") != B_OK
		|| message.AddString("password", "x") != B_OK
		|| message.AddString("shadow password", "!") != B_OK
		|| message.AddInt32("last changed", (int32)time(NULL)) != B_OK)
		return B_NO_MEMORY;

	BPrivate::KMessage reply;
	return BPrivate::send_authentication_request_to_registrar(message, reply);
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// Issue #4644: the walk that gives the first administrator /boot/home runs
// once, on its own thread (LoginApp), and Sign in shows how far it is.
// /boot/home/users stays uid 0: the other homes are in it, and whoever owns
// it can rename them. The walk does not call getpwnam() (one static
// buffer, also used by Login's own thread).
static const char* kHomeOwnerPath = "/boot/system/settings/r5-home-owner";


struct home_walk {
	uid_t				uid;
	gid_t				gid;
	dev_t				device;
	int64				count;
	bigtime_t			lastReport;
	home_walk_progress	progress;
	void*				cookie;
};


static void
own_tree(const char* path, home_walk& walk)
{
	struct stat st;
	if (lstat(path, &st) != 0)
		return;
	if (st.st_dev != walk.device)
		return;
	if (S_ISLNK(st.st_mode))
		return;
	if (strcmp(path, kUsersRoot) == 0)
		return;
	if (strcmp(path, kSharedFolderPath) == 0) {
		// Own /boot/home/Shared itself by the first administrator so the
		// sticky bit lets them delete any file, but never chown files inside.
		if (S_ISDIR(st.st_mode)) {
			int fd = r5_admin_open_nofollow(path, NULL,
				O_RDONLY | O_DIRECTORY);
			if (fd >= 0) {
				r5_admin_set_owner(fd, walk.uid, walk.gid, 01777);
				close(fd);
			}
		}
		return;
	}

	if (S_ISDIR(st.st_mode)) {
		DIR* dir = opendir(path);
		if (dir != NULL) {
			while (struct dirent* entry = readdir(dir)) {
				if (strcmp(entry->d_name, ".") == 0
					|| strcmp(entry->d_name, "..") == 0)
					continue;
				char child[PATH_MAX];
				if (snprintf(child, sizeof(child), "%s/%s", path,
						entry->d_name) >= (int)sizeof(child))
					continue;
				own_tree(child, walk);
			}
			closedir(dir);
		}
	}

	// A folder after what is in it: /boot/home itself last, so a walk that
	// stopped half way leaves /boot/home with uid 0.
	if (st.st_uid != walk.uid || st.st_gid != walk.gid)
		lchown(path, walk.uid, walk.gid);
	walk.count++;
	if (walk.progress != NULL) {
		bigtime_t now = system_time();
		if (now - walk.lastReport >= 200000) {
			walk.lastReport = now;
			walk.progress(walk.count, walk.cookie);
		}
	}
}


static bool
read_home_owner(uid_t& uid)
{
	struct stat st;
	if (lstat(kHomeOwnerPath, &st) != 0 || !S_ISREG(st.st_mode)
		|| st.st_uid != 0 || (st.st_mode & (S_IWGRP | S_IWOTH)) != 0)
		return false;
	FILE* file = fopen(kHomeOwnerPath, "r");
	if (file == NULL)
		return false;
	long value = -1;
	bool ok = fscanf(file, "%ld", &value) == 1;
	fclose(file);
	if (!ok || value < 0)
		return false;
	uid = (uid_t)value;
	return true;
}


static status_t
write_home_owner(uid_t uid)
{
	BString temporary;
	temporary.SetToFormat("%s.new", kHomeOwnerPath);
	unlink(temporary.String());
	int fd = open(temporary.String(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
		0644);
	if (fd < 0)
		return errno;
	char line[32];
	int length = snprintf(line, sizeof(line), "%ld\n", (long)uid);
	bool ok = write(fd, line, length) == length && fsync(fd) == 0;
	close(fd);
	if (!ok || rename(temporary.String(), kHomeOwnerPath) != 0) {
		status_t status = errno != 0 ? errno : B_IO_ERROR;
		unlink(temporary.String());
		return status;
	}
	return B_OK;
}
// SPDX-SnippetEnd


static void
ensure_desktop_link(const char* home, const char* name, const char* target)
{
	BString path;
	path.SetToFormat("%s/Desktop/%s", home, name);
	struct stat st;
	if (lstat(path.String(), &st) == 0)
		return;
	symlink(target, path.String());
}


static void
ensure_desktop_links(const char* home)
{
	if (home == NULL)
		return;
	ensure_desktop_link(home, "Home", home);
	ensure_desktop_link(home, "Shared", kSharedFolderPath);
	if (strcmp(home, kHome) != 0) {
		BString homeShared;
		homeShared.SetToFormat("%s/Shared", home);
		struct stat st;
		if (lstat(homeShared.String(), &st) != 0)
			symlink(kSharedFolderPath, homeShared.String());
	}
}


// Trash follows the account home ($h/trash). The image only had /boot/trash,
// which was mode 0700 for the administrator, so a standard account got no icon.
static void
ensure_private_trash(const char* home, uid_t uid, gid_t gid)
{
	if (home == NULL)
		return;

	BString dest;
	dest.SetToFormat("%s/trash", home);
	struct stat st;
	if (lstat(dest.String(), &st) != 0) {
		if (strcmp(home, kHome) == 0 && lstat("/boot/trash", &st) == 0
			&& S_ISDIR(st.st_mode)) {
			if (rename("/boot/trash", dest.String()) != 0)
				mkdir(dest.String(), 0700);
		} else
			mkdir(dest.String(), 0700);
	}
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// Issue #4643: a link at $h/trash is left alone.
	own_home_dir(home, "trash", uid, gid, 0700);
	// SPDX-SnippetEnd
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// Issue #4643: the copy of the MIME database goes into the person's own
// config/settings as uid 0. Everything on the person's side is opened from
// the directory above it, never through a link, and a file is only made
// new (O_EXCL), never written through an existing name.
static void
copy_attributes_to(const char* srcPath, int dstFd)
{
	BNode src(srcPath);
	if (src.InitCheck() != B_OK)
		return;
	src.RewindAttrs();
	char name[B_ATTR_NAME_LENGTH];
	while (src.GetNextAttrName(name) == B_OK) {
		attr_info info;
		if (src.GetAttrInfo(name, &info) != B_OK || info.size < 0
			|| info.size > 1024 * 1024)
			continue;
		if (info.size == 0) {
			fs_write_attr(dstFd, name, info.type, 0, NULL, 0);
			continue;
		}
		void* buffer = malloc((size_t)info.size);
		if (buffer == NULL)
			return;
		ssize_t bytes = src.ReadAttr(name, info.type, 0, buffer, info.size);
		if (bytes > 0)
			fs_write_attr(dstFd, name, info.type, 0, buffer, bytes);
		free(buffer);
	}
}


static void
copy_mime_entry_at(const char* srcPath, int dstDir, const char* name,
	uid_t uid, gid_t gid, int32 depth)
{
	struct stat st;
	if (depth > 16 || lstat(srcPath, &st) != 0)
		return;

	if (S_ISDIR(st.st_mode)) {
		mkdirat(dstDir, name, 0755);
		int fd = openat(dstDir, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW
			| O_CLOEXEC);
		if (fd < 0)
			return;
		r5_admin_set_owner(fd, uid, gid, 0755);
		copy_attributes_to(srcPath, fd);

		DIR* dir = opendir(srcPath);
		if (dir != NULL) {
			while (struct dirent* entry = readdir(dir)) {
				if (strcmp(entry->d_name, ".") == 0
					|| strcmp(entry->d_name, "..") == 0)
					continue;
				BString childSrc;
				childSrc.SetToFormat("%s/%s", srcPath, entry->d_name);
				copy_mime_entry_at(childSrc.String(), fd, entry->d_name, uid,
					gid, depth + 1);
			}
			closedir(dir);
		}
		close(fd);
		return;
	}
	if (!S_ISREG(st.st_mode))
		return;

	int in = open(srcPath, O_RDONLY | O_CLOEXEC);
	if (in < 0)
		return;
	int out = openat(dstDir, name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW
		| O_CLOEXEC, 0644);
	if (out < 0) {
		close(in);
		return;
	}

	char buffer[4096];
	ssize_t n;
	while ((n = read(in, buffer, sizeof(buffer))) > 0) {
		if (write(out, buffer, n) != n)
			break;
	}
	close(in);
	r5_admin_set_owner(out, uid, gid, 0644);
	copy_attributes_to(srcPath, out);
	close(out);
}


static void
ensure_user_mime_db(const char* home, uid_t uid, gid_t gid)
{
	if (home == NULL)
		return;

	static const char* kSystemMime = "/boot/system/data/mime_db";
	struct stat st;
	if (lstat(kSystemMime, &st) != 0 || !S_ISDIR(st.st_mode))
		return;

	int mimeFd = r5_admin_open_nofollow(home, "config/settings/mime_db",
		O_RDONLY | O_DIRECTORY, 0700);
	if (mimeFd < 0)
		return;
	r5_admin_set_owner(mimeFd, uid, gid, 0700);

	int appFd = openat(mimeFd, "application", O_RDONLY | O_DIRECTORY
		| O_NOFOLLOW | O_CLOEXEC);
	if (appFd < 0 && errno == ENOENT) {
		copy_attributes_to(kSystemMime, mimeFd);
		DIR* dir = opendir(kSystemMime);
		if (dir != NULL) {
			while (struct dirent* entry = readdir(dir)) {
				if (strcmp(entry->d_name, ".") == 0
					|| strcmp(entry->d_name, "..") == 0)
					continue;
				BString childSrc;
				childSrc.SetToFormat("%s/%s", kSystemMime, entry->d_name);
				copy_mime_entry_at(childSrc.String(), mimeFd, entry->d_name,
					uid, gid, 1);
			}
			closedir(dir);
		}
	}
	if (appFd >= 0)
		close(appFd);

	// Tracker reads the user mime_db first. The leaf menu draws merged
	// folders as application/x-vnd.haiku-virtual-directory (blue). Ordinary
	// folders use application/x-vnd.be-directory (yellow). Keep both on the
	// yellow system icon, including when this database already existed.
	mkdirat(mimeFd, "application", 0755);
	appFd = openat(mimeFd, "application", O_RDONLY | O_DIRECTORY | O_NOFOLLOW
		| O_CLOEXEC);
	close(mimeFd);
	if (appFd < 0)
		return;
	r5_admin_set_owner(appFd, uid, gid, 0755);
	static const char* kYellowTypes[] = {
		"x-vnd.be-directory",
		"x-vnd.haiku-virtual-directory",
		NULL
	};
	for (int32 i = 0; kYellowTypes[i] != NULL; i++) {
		BString srcType;
		srcType.SetToFormat("%s/application/%s", kSystemMime,
			kYellowTypes[i]);
		unlinkat(appFd, kYellowTypes[i], 0);
		copy_mime_entry_at(srcType.String(), appFd, kYellowTypes[i], uid, gid,
			2);
	}
	close(appFd);
}
// SPDX-SnippetEnd


static void
adopt_trash(uid_t uid, gid_t gid)
{
	struct stat st;
	if (lstat("/boot/trash", &st) != 0 || !S_ISDIR(st.st_mode))
		return;
	lchown("/boot/trash", uid, gid);
	chmod("/boot/trash", 0700);
}


static void
ensure_person_dirs(uid_t uid, gid_t gid)
{
	static const char* kRelative[] = {
		"Desktop",
		"Documents",
		"Downloads",
		"Music",
		"Pictures",
		"Videos",
		"mail",
		"config",
		"config/settings",
		"config/settings/deskbar",
		NULL
	};
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// Issue #4643: no part of the path is followed.
	for (int32 i = 0; kRelative[i] != NULL; i++)
		own_home_dir(kHome, kRelative[i], uid, gid, 0700);
	// SPDX-SnippetEnd
}


static status_t
find_first_home_admin(BString& name, uid_t& uid, gid_t& gid)
{
	struct passwd* named = getpwnam("user");
	if (named != NULL && is_first_home_admin(named)) {
		name = named->pw_name;
		uid = named->pw_uid;
		gid = named->pw_gid;
		return B_OK;
	}

	setpwent();
	for (;;) {
		struct passwd* pwd = getpwent();
		if (pwd == NULL)
			break;
		if (!is_first_home_admin(pwd) || !is_listed_account(pwd))
			continue;
		name = pwd->pw_name;
		uid = pwd->pw_uid;
		gid = pwd->pw_gid;
		endpwent();
		return B_OK;
	}
	endpwent();
	return B_NAME_NOT_FOUND;
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// Issue #4640: moves the account to another uid in one registrar request
// ("new uid", distro/desktop/src/servers/registrar/AuthenticationManager.cpp).
// The account, its password and its name stay; it is never deleted.
static status_t
move_unix_user(const char* name, uid_t uid, gid_t gid)
{
	BPrivate::KMessage message(BPrivate::B_REG_UPDATE_USER);
	if (message.AddString("name", name) != B_OK
		|| message.AddInt32("new uid", uid) != B_OK
		|| message.AddInt32("gid", gid) != B_OK)
		return B_NO_MEMORY;
	BPrivate::KMessage reply;
	status_t status = BPrivate::send_authentication_request_to_registrar(
		message, reply);
	if (status == B_OK)
		status = reply.What();
	return status;
}


// Issue #4641: the groups of the first administrator, by name, so they can
// be set before the uid changes. "admin" is required; "users" and "wheel"
// only where the group exists. "added" gets the groups this call added.
static status_t
set_first_admin_groups(const char* name, BStringList& added)
{
	BString error;
	status_t status = ensure_admin_group(error);
	if (status != B_OK) {
		syslog(LOG_ERR, "Login: no group %s: %s", kAdminGroupName,
			error.String());
		return status;
	}
	static const char* kGroups[] = { kAdminGroupName, "users", "wheel", NULL };
	for (int32 i = 0; kGroups[i] != NULL; i++) {
		if (getgrnam(kGroups[i]) == NULL) {
			syslog(LOG_WARNING, "Login: there is no group %s", kGroups[i]);
			continue;
		}
		if (group_has_member(kGroups[i], name))
			continue;
		status = add_to_group(kGroups[i], name, error);
		if (status == B_OK && !group_has_member(kGroups[i], name))
			status = B_ERROR;
		if (status != B_OK) {
			syslog(LOG_ERR, "Login: %s not put in group %s: %s", name,
				kGroups[i], error.String());
			return status;
		}
		added.Add(kGroups[i]);
	}
	return B_OK;
}


static void
remove_added_groups(const char* name, const BStringList& added)
{
	for (int32 i = 0; i < added.CountStrings(); i++) {
		BString error;
		if (remove_from_group(added.StringAt(i).String(), name, error)
				!= B_OK) {
			syslog(LOG_ERR, "Login: %s still in group %s: %s", name,
				added.StringAt(i).String(), error.String());
		}
	}
}


status_t
migrate_admin_off_root()
{
	if (geteuid() != 0)
		return B_PERMISSION_DENIED;

	BString name;
	uid_t uid = 0;
	gid_t gid = 0;
	status_t status = find_first_home_admin(name, uid, gid);
	if (status != B_OK)
		return status;
	if (uid != 0) {
		if (ensure_system_root() != B_OK)
			syslog(LOG_WARNING, "Login: the locked root account is missing");
		// A disk from before issue #4641 can have the administrator
		// outside group admin. Put it back; the sign-in goes on.
		if (!group_has_member(kAdminGroupName, name.String())) {
			BStringList added;
			set_first_admin_groups(name.String(), added);
		}
		// /boot/home itself is given by own_first_home() (LoginApp, off
		// the message loop, first_home_walk_needed()).
		ensure_person_dirs(uid, gid);
		adopt_trash(uid, gid);
		return B_OK;
	}

	struct stat homeStat;
	if (lstat(kHome, &homeStat) != 0 || !S_ISDIR(homeStat.st_mode))
		return B_NOT_A_DIRECTORY;

	// First the groups: a failure leaves the account as it was.
	BStringList added;
	status = set_first_admin_groups(name.String(), added);
	if (status != B_OK) {
		remove_added_groups(name.String(), added);
		return status;
	}

	uid_t newUid = first_free_person_uid();
	gid_t newGid = users_gid();
	status = move_unix_user(name.String(), newUid, newGid);
	if (status != B_OK) {
		syslog(LOG_ERR, "Login: %s not moved to uid %d: %s", name.String(),
			(int)newUid, strerror(status));
		remove_added_groups(name.String(), added);
		return status;
	}

	endpwent();
	struct passwd* pwd = getpwnam(name.String());
	if (pwd == NULL || pwd->pw_uid != newUid) {
		syslog(LOG_ERR, "Login: %s is not at uid %d after the move",
			name.String(), (int)newUid);
		return B_ERROR;
	}

	// The account is already off uid 0. Without the locked root the next
	// sign-in tries again (above).
	status = ensure_system_root();
	if (status != B_OK) {
		syslog(LOG_WARNING, "Login: the locked root account was not made: %s",
			strerror(status));
	}

	ensure_person_dirs(newUid, newGid);
	adopt_trash(newUid, newGid);
	lock_private_homes();
	ensure_shared_folder();
	return B_OK;
}


bool
first_home_walk_needed(uid_t& uid, gid_t& gid)
{
	if (geteuid() != 0)
		return false;
	BString name;
	if (find_first_home_admin(name, uid, gid) != B_OK || uid == 0)
		return false;
	uid_t owner = (uid_t)-1;
	if (read_home_owner(owner) && owner == uid)
		return false;
	// A disk from before the marker: the old walk gave /boot/home itself
	// first, and only when it still had uid 0.
	struct stat homeStat;
	if (!read_home_owner(owner) && lstat(kHome, &homeStat) == 0
		&& S_ISDIR(homeStat.st_mode) && homeStat.st_uid == uid) {
		write_home_owner(uid);
		return false;
	}
	return true;
}


status_t
own_first_home(uid_t uid, gid_t gid, home_walk_progress progress,
	void* cookie)
{
	if (geteuid() != 0)
		return B_PERMISSION_DENIED;
	if (uid == 0)
		return B_BAD_VALUE;
	struct stat homeStat;
	if (lstat(kHome, &homeStat) != 0 || !S_ISDIR(homeStat.st_mode))
		return B_NOT_A_DIRECTORY;

	home_walk walk;
	walk.uid = uid;
	walk.gid = gid;
	walk.device = homeStat.st_dev;
	walk.count = 0;
	walk.lastReport = system_time();
	walk.progress = progress;
	walk.cookie = cookie;
	own_tree(kHome, walk);
	if (progress != NULL)
		progress(walk.count, cookie);

	if (lstat(kHome, &homeStat) != 0 || homeStat.st_uid != uid)
		return B_PERMISSION_DENIED;
	status_t status = write_home_owner(uid);
	if (status != B_OK)
		syslog(LOG_WARNING, "Login: home owner marker not written");
	return B_OK;
}
// SPDX-SnippetEnd


static bool
is_session_home(const struct passwd* pwd)
{
	if (pwd == NULL || pwd->pw_dir == NULL || pwd->pw_name == NULL)
		return false;
	if (strcmp(pwd->pw_dir, kHome) == 0)
		return true;

	BString expected;
	expected.SetToFormat("%s/%s", kUsersRoot, pwd->pw_name);
	return expected == pwd->pw_dir;
}


static void own_shared_folder(uid_t uid, gid_t gid);


status_t
write_desktop_session(const char* login)
{
	if (geteuid() != 0)
		return B_PERMISSION_DENIED;
	if (login == NULL || login[0] == '\0')
		return B_BAD_VALUE;
	bool guestAllowed = is_guest_account(login) && is_guest_enabled();
	if (!guestAllowed && is_disabled_user(login))
		return B_NOT_ALLOWED;

	struct passwd* pwd = getpwnam(login);
	if (pwd == NULL || strcmp(pwd->pw_name, login) != 0)
		return ENOENT;
	bool allowedAccount = is_listed_account(pwd) || guestAllowed;
	if (!allowedAccount || !is_session_home(pwd))
		return B_NOT_ALLOWED;
	if (pwd->pw_uid == 0)
		return B_NOT_ALLOWED;

	// getpwnam() uses one static buffer. Copy before create_home().
	char name[64];
	if (snprintf(name, sizeof(name), "%s", pwd->pw_name) >= (int)sizeof(name))
		return B_NAME_TOO_LONG;
	if (strchr(name, '\n') != NULL || strchr(name, '\r') != NULL)
		return B_BAD_VALUE;
	uid_t uid = pwd->pw_uid;
	gid_t gid = pwd->pw_gid;
	BString home(pwd->pw_dir);
	bool firstHome = home == kHome;

	// Do not run ensure_person_dirs() here: that always rewrites /boot/home
	// and would give a standard account the administrator's folders.
	if (firstHome)
		ensure_person_dirs(uid, gid);
	else {
		status_t status = create_home(home.String(), uid, gid);
		if (status != B_OK)
			return status;
	}
	ensure_desktop_links(home.String());
	ensure_private_trash(home.String(), uid, gid);
	ensure_user_mime_db(home.String(), uid, gid);

	FILE* file = fopen(kSessionPath, "w");
	if (file == NULL)
		return errno;
	if (fprintf(file, "%s\n", name) < 0) {
		fclose(file);
		unlink(kSessionPath);
		return EIO;
	}
	if (fclose(file) != 0)
		return errno;
	if (chmod(kSessionPath, 0600) != 0 || chown(kSessionPath, 0, 0) != 0)
		return errno;

	lock_private_homes();
	ensure_shared_folder();

	struct passwd entry;
	struct passwd* signedIn = NULL;
	char buffer[1024];
	if (!guestAllowed
		&& getpwuid_r(uid, &entry, buffer, sizeof(buffer), &signedIn) == 0
		&& signedIn != NULL && strcmp(signedIn->pw_name, name) == 0
		&& is_admin_user(signedIn)) {
		own_shared_folder(uid, gid);
	}
	return B_OK;
}


bool
is_guest_enabled()
{
	// Review fix (24 Nov 2026): the same owner and mode check as the uid
	// file. Only a root-owned file that others cannot write turns guest on.
	struct stat st;
	if (lstat(kGuestEnabledPath, &st) != 0 || !S_ISREG(st.st_mode)
		|| st.st_uid != 0 || (st.st_mode & (S_IWGRP | S_IWOTH)) != 0)
		return false;
	FILE* file = fopen(kGuestEnabledPath, "r");
	if (file == NULL)
		return false;
	char buf[16];
	bool enabled = false;
	if (fgets(buf, sizeof(buf), file) != NULL)
		enabled = (buf[0] == '1');
	fclose(file);
	return enabled;
}


// 24 Nov 2026: the sticky bit on Shared lets only uid 0, the owner of a file
// and the owner of Shared delete there; group admin does not count. One
// graphical session runs at a time, so the administrator who signs in becomes
// the owner of Shared (the folder only, never what is in it) and can delete
// any file there. A standard account or the guest leaves the owner as it is.
static void
own_shared_folder(uid_t uid, gid_t gid)
{
	int fd = open(kSharedFolderPath, O_RDONLY | O_DIRECTORY | O_NOFOLLOW
		| O_CLOEXEC);
	if (fd < 0)
		return;
	struct stat st;
	if (fstat(fd, &st) == 0 && S_ISDIR(st.st_mode)) {
		fchown(fd, uid, gid);
		fchmod(fd, 01777);
	}
	close(fd);
}


status_t
ensure_shared_folder()
{
	if (geteuid() != 0)
		return B_PERMISSION_DENIED;

	struct stat st;
	if (lstat(kSharedFolderPath, &st) != 0) {
		if (mkdir(kSharedFolderPath, 01777) != 0 && errno != EEXIST)
			return errno;
		if (lstat(kSharedFolderPath, &st) != 0)
			return errno;
	}
	if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode))
		return B_NOT_A_DIRECTORY;

	int fd = open(kSharedFolderPath, O_RDONLY | O_DIRECTORY | O_NOFOLLOW
		| O_CLOEXEC);
	if (fd < 0)
		return errno;
	struct stat fstatBuf;
	if (fstat(fd, &fstatBuf) != 0 || !S_ISDIR(fstatBuf.st_mode)) {
		close(fd);
		return B_NOT_A_DIRECTORY;
	}

	uid_t ownerUid = 0;
	gid_t ownerGid = 0;
	setpwent();
	for (;;) {
		struct passwd* pwd = getpwent();
		if (pwd == NULL)
			break;
		if (is_first_home_admin(pwd) && pwd->pw_uid >= kMinPersonUid) {
			ownerUid = pwd->pw_uid;
			ownerGid = pwd->pw_gid;
			break;
		}
	}
	endpwent();

	fchown(fd, ownerUid, ownerGid);
	fchmod(fd, 01777);
	close(fd);

	struct stat desktopStat;
	if (lstat("/boot/home/Desktop", &desktopStat) == 0
		&& S_ISDIR(desktopStat.st_mode)) {
		ensure_desktop_link(kHome, "Shared", kSharedFolderPath);
	}
	return B_OK;
}


status_t
ensure_guest_account(BString& error)
{
	if (geteuid() != 0) {
		error = "Guest account setup requires the sign-in helper.";
		return B_PERMISSION_DENIED;
	}

	struct passwd* pwd = getpwnam(kGuestAccountName);
	if (pwd != NULL) {
		if (!is_system_guest(pwd)) {
			// A person made before the name was reserved. Never lock or wipe
			// that account.
			error = "An account named \"guest\" already exists. Rename it "
				"first.";
			return B_NOT_ALLOWED;
		}
		if (pwd->pw_uid < kMinPersonUid || pwd->pw_uid == 0) {
			error = "The existing guest account has an unexpected setup.";
			return B_NOT_ALLOWED;
		}
		// Never allow guest in admin or wheel, and keep shadow locked so only
		// LoginApp::ValidateLogin() (when enabled) can start a guest session.
		BString detail;
		remove_from_group(kAdminGroupName, kGuestAccountName, detail);
		remove_from_group("wheel", kGuestAccountName, detail);
		if (!is_disabled_user(kGuestAccountName))
			set_shadow_password(kGuestAccountName, "!");
		return B_OK;
	}

	uid_t uid = (getpwuid(kGuestUid) == NULL) ? kGuestUid
		: first_free_person_uid();
	gid_t gid = users_gid();
	// Lock the shadow entry ("!") so su/login/sshd cannot authenticate as
	// guest without a password; LoginApp::ValidateLogin() gates guest sign-in.
	status_t status = add_unix_user(kGuestAccountName, uid, gid,
		kGuestHomePath, "/bin/sh", "Guest", "!");
	if (status != B_OK) {
		error = "Could not create the guest account.";
		return status;
	}
	endpwent();
	if (!write_guest_uid(uid)) {
		error = "Could not save the guest account.";
		return B_ERROR;
	}
	return B_OK;
}


static void
kill_guest_teams(uid_t guestUid)
{
	if (guestUid < kMinPersonUid || guestUid == 0)
		return;
	bool any = false;
	int32 cookie = 0;
	team_info info;
	while (get_next_team_info(&cookie, &info) == B_OK) {
		if (info.uid == guestUid && info.team > 0) {
			send_signal(info.team, SIGTERM);
			any = true;
		}
	}
	if (!any)
		return;
	snooze(500000);
	cookie = 0;
	while (get_next_team_info(&cookie, &info) == B_OK) {
		if (info.uid == guestUid && info.team > 0)
			send_signal(info.team, SIGKILL);
	}
	snooze(200000);
}


static status_t
remove_dir_contents_nofollow(int dirFd, dev_t rootDev)
{
	int dupFd = dup(dirFd);
	if (dupFd < 0)
		return errno;
	DIR* dir = fdopendir(dupFd);
	if (dir == NULL) {
		int err = errno;
		close(dupFd);
		return err;
	}

	status_t result = B_OK;
	while (struct dirent* entry = readdir(dir)) {
		if (strcmp(entry->d_name, ".") == 0
			|| strcmp(entry->d_name, "..") == 0)
			continue;

		struct stat st;
		if (fstatat(dirFd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
			if (errno != ENOENT && result == B_OK)
				result = errno;
			continue;
		}
		// Never follow symlinks or cross mount points out of the guest home.
		if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
			if (st.st_dev != rootDev)
				continue;
			int subFd = openat(dirFd, entry->d_name,
				O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
			if (subFd < 0) {
				if (errno != ENOENT && result == B_OK)
					result = errno;
				continue;
			}
			struct stat subStat;
			if (fstat(subFd, &subStat) != 0 || subStat.st_dev != rootDev) {
				close(subFd);
				if (result == B_OK)
					result = B_NOT_ALLOWED;
				continue;
			}
			status_t subStatus = remove_dir_contents_nofollow(subFd, rootDev);
			close(subFd);
			if (subStatus != B_OK && result == B_OK)
				result = subStatus;
			if (unlinkat(dirFd, entry->d_name, AT_REMOVEDIR) != 0
				&& errno != ENOENT && result == B_OK) {
				result = errno;
			}
		} else {
			if (unlinkat(dirFd, entry->d_name, 0) != 0
				&& errno != ENOENT && result == B_OK) {
				result = errno;
			}
		}
	}
	closedir(dir);
	return result;
}


status_t
wipe_guest_home()
{
	if (geteuid() != 0)
		return B_PERMISSION_DENIED;

	struct passwd* pwd = getpwnam(kGuestAccountName);
	if (pwd == NULL)
		return B_OK;
	if (!is_system_guest(pwd)) {
		// No guest made by R5, or a person called "guest": nothing to wipe.
		return B_OK;
	}
	if (pwd->pw_uid < kMinPersonUid || pwd->pw_uid == 0
		|| pwd->pw_dir == NULL
		|| strcmp(pwd->pw_dir, kGuestHomePath) != 0) {
		syslog(LOG_ERR, "Login: refusing to wipe guest home (invalid passwd)");
		return B_NOT_ALLOWED;
	}

	uid_t guestUid = pwd->pw_uid;
	gid_t guestGid = pwd->pw_gid;

	// Stop any leftover background processes owned by guest before wiping.
	kill_guest_teams(guestUid);

	struct stat st;
	if (lstat(kGuestHomePath, &st) == 0) {
		if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)) {
			syslog(LOG_ERR,
				"Login: refusing to wipe %s (not a real directory)",
				kGuestHomePath);
			return B_NOT_ALLOWED;
		}
		if (st.st_uid != guestUid && st.st_uid != 0) {
			syslog(LOG_ERR,
				"Login: refusing to wipe %s (unexpected owner uid %d)",
				kGuestHomePath, (int)st.st_uid);
			return B_NOT_ALLOWED;
		}
		int fd = open(kGuestHomePath,
			O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		if (fd < 0)
			return errno;
		struct stat fstatBuf;
		if (fstat(fd, &fstatBuf) != 0 || fstatBuf.st_ino != st.st_ino
			|| fstatBuf.st_dev != st.st_dev) {
			close(fd);
			return B_NOT_ALLOWED;
		}
		status_t wipeStatus = remove_dir_contents_nofollow(fd,
			fstatBuf.st_dev);
		close(fd);
		if (wipeStatus != B_OK) {
			syslog(LOG_ERR, "Login: guest wipe failed: %s",
				strerror(wipeStatus));
			return wipeStatus;
		}
		if (rmdir(kGuestHomePath) != 0 && errno != ENOENT) {
			status_t err = errno;
			syslog(LOG_ERR, "Login: rmdir %s failed: %s", kGuestHomePath,
				strerror(err));
			return err;
		}
	}

	return create_home(kGuestHomePath, guestUid, guestGid);
}


static status_t
set_guest_enabled(bool enabled, BString& error)
{
	if (geteuid() != 0) {
		error = "Guest setting requires the sign-in helper.";
		return B_PERMISSION_DENIED;
	}
	if (guest_session_active()) {
		// Turning the guest off while a guest session runs would leave it
		// running; turning it on would wipe a live home.
		error = "Log out Guest first.";
		return B_BUSY;
	}
	if (enabled) {
		status_t status = ensure_guest_account(error);
		if (status != B_OK)
			return status;
	}
	FILE* file = fopen(kGuestEnabledPath, "w");
	if (file == NULL) {
		error = "Could not save the guest setting.";
		return errno;
	}
	if (fprintf(file, "%d\n", enabled ? 1 : 0) < 0) {
		fclose(file);
		error = "Could not save the guest setting.";
		return EIO;
	}
	if (fclose(file) != 0) {
		error = "Could not save the guest setting.";
		return errno;
	}
	chmod(kGuestEnabledPath, 0644);
	chown(kGuestEnabledPath, 0, 0);
	if (enabled) {
		status_t status = wipe_guest_home();
		if (status != B_OK) {
			error = "The guest's old files could not be removed.";
			return status;
		}
	}
	return B_OK;
}


status_t
clear_desktop_session()
{
	if (geteuid() != 0)
		return B_PERMISSION_DENIED;

	unlink(kSessionPath);
	unlink("/tmp/r5-desktop-session");
	return B_OK;
}


static status_t
create_account(const char* name, const char* real, const char* password,
	bool admin, BString& error)
{
	if (password == NULL || password[0] == '\0') {
		error = "Type a password.";
		return B_BAD_VALUE;
	}
	if (!valid_username(name)) {
		error = "Use a lowercase username of letters, digits, underscore "
			"or hyphen.";
		return B_BAD_VALUE;
	}
	if (getpwnam(name) != NULL) {
		error = "That username already exists.";
		return B_NAME_IN_USE;
	}

	BString home;
	home.SetToFormat("%s/%s", kUsersRoot, name);
	char* argv[] = {
		const_cast<char*>("/bin/useradd"),
		const_cast<char*>("-d"),
		const_cast<char*>(home.String()),
		const_cast<char*>("-n"),
		const_cast<char*>(real != NULL ? real : ""),
		const_cast<char*>("-s"),
		const_cast<char*>("/bin/sh"),
		const_cast<char*>(name),
		NULL
	};
	status_t status = run_argv(argv, error);
	if (status != B_OK)
		return status;

	struct passwd* pwd = getpwnam(name);
	if (pwd == NULL) {
		error = "The account was not created.";
		return B_ERROR;
	}
	if (pwd->pw_uid < kMinPersonUid) {
		char* delArgv[] = {
			const_cast<char*>("/bin/userdel"),
			const_cast<char*>(name),
			NULL
		};
		run_argv(delArgv, error);
		error = "The new account did not get a user id of 1000 or higher.";
		return B_ERROR;
	}

	create_home(home.String(), pwd->pw_uid, pwd->pw_gid);

	BString encrypted;
	if (make_password_hash(password, encrypted) != B_OK) {
		error = "Could not encrypt the password.";
		return B_ERROR;
	}
	if (set_shadow_password(name, encrypted.String()) != B_OK) {
		error = "The account exists, but the password was not set.";
		return B_ERROR;
	}

	if (admin && add_to_group(kAdminGroupName, name, error) != B_OK) {
		ensure_admin_group(error);
		if (add_to_group(kAdminGroupName, name, error) != B_OK) {
			BString text;
			text.SetToFormat("The account exists, but it is not an "
				"administrator: %s", error.String());
			error = text;
			return B_ERROR;
		}
	}

	struct passwd* created = getpwnam(name);
	if (created != NULL && created->pw_dir != NULL
		&& created->pw_dir[0] != '\0') {
		status_t ksStatus = BPrivate::R5KeyStoreCrypto::CreateAccountKeyStore(
			created->pw_dir, created->pw_uid, created->pw_gid, password);
		if (ksStatus != B_OK) {
			syslog(LOG_ERR, "AccountAdmin: CreateAccountKeyStore failed: %s",
				strerror(ksStatus));
		}
	}

	return B_OK;
}


static status_t
set_disabled(const char* name, bool disable, BString& error)
{
	struct passwd* pwd = getpwnam(name);
	if (pwd == NULL || !is_listed_account(pwd)) {
		error = "That account was not found.";
		return ENOENT;
	}
	if (is_first_home_admin(pwd)) {
		error = "The original administrator account cannot be disabled.";
		return B_NOT_ALLOWED;
	}

	if (!disable) {
		struct spwd* shadow = getspnam(name);
		const char* current = "";
		if (shadow != NULL && shadow->sp_pwdp != NULL)
			current = shadow->sp_pwdp;
		if (current[0] == '!')
			current++;
		if (set_shadow_password(name, current) != B_OK) {
			error = "Could not enable the account.";
			return B_ERROR;
		}
		return B_OK;
	}

	if (is_admin_user(pwd) && admin_count() <= 1) {
		error = "There must be at least one administrator.";
		return B_NOT_ALLOWED;
	}

	BString locked("!");
	struct spwd* shadow = getspnam(name);
	if (shadow != NULL && shadow->sp_pwdp != NULL
		&& shadow->sp_pwdp[0] != '!')
		locked << shadow->sp_pwdp;
	if (set_shadow_password(name, locked.String()) != B_OK) {
		error = "Could not disable the account.";
		return B_ERROR;
	}
	return B_OK;
}


status_t
signed_in_name(BString& name)
{
	name = "";
	FILE* file = fopen(kSessionPath, "r");
	if (file == NULL)
		return B_ENTRY_NOT_FOUND;
	char line[64];
	if (fgets(line, sizeof(line), file) == NULL) {
		fclose(file);
		return B_ENTRY_NOT_FOUND;
	}
	fclose(file);
	size_t length = strlen(line);
	while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'
			|| line[length - 1] == ' '))
		line[--length] = '\0';
	if (line[0] == '\0')
		return B_ENTRY_NOT_FOUND;
	name = line;
	return B_OK;
}


static bool
is_signed_in_name(const char* name)
{
	BString session;
	if (name == NULL || signed_in_name(session) != B_OK)
		return false;
	return session == name;
}


static status_t
delete_account(const char* name, bool deleteFiles, BString& error)
{
	struct passwd* pwd = getpwnam(name);
	if (pwd == NULL || !is_listed_account(pwd)) {
		error = "That account was not found.";
		return ENOENT;
	}
	if (is_first_home_admin(pwd)) {
		error = "The original administrator account cannot be deleted.";
		return B_NOT_ALLOWED;
	}
	// Login is uid 0. The person at the screen is the session file, not getuid().
	if (is_signed_in_name(name)) {
		error = "You cannot delete the account that is signed in.";
		return B_NOT_ALLOWED;
	}
	if (is_admin_user(pwd) && !is_disabled_user(name) && admin_count() <= 1) {
		error = "There must be at least one administrator.";
		return B_NOT_ALLOWED;
	}

	BString home;
	if (pwd->pw_dir != NULL)
		home = pwd->pw_dir;

	char* argv[] = {
		const_cast<char*>("/bin/userdel"),
		const_cast<char*>(name),
		NULL
	};
	if (run_argv(argv, error) != B_OK)
		return B_ERROR;

	if (deleteFiles && home.FindFirst("/boot/home/users/") == 0) {
		char* rmArgv[] = {
			const_cast<char*>("/bin/rm"),
			const_cast<char*>("-rf"),
			const_cast<char*>(home.String()),
			NULL
		};
		if (run_argv(rmArgv, error) != B_OK) {
			error = "The account was removed, but its files could not be "
				"deleted.";
			return B_ERROR;
		}
	}
	return B_OK;
}


struct admin_grant {
	team_id	team;
	time_t	until;
};

static const int32 kMaxAdminGrants = 8;
static admin_grant sGrants[kMaxAdminGrants];
static const char* kGrantPath = "/boot/system/settings/r5-machine-unlock";
static const char* kNetworkSettings = "/boot/system/settings/network";
static const char* kMachinePrinters = "/boot/system/settings/printers";
static const char* kUserPrinters = "/boot/home/config/settings/printers";
static const char* kSwapSettings =
	"/boot/system/settings/kernel/drivers/virtual_memory";
static const char* kUserSwapSettings =
	"/boot/home/config/settings/kernel/drivers/virtual_memory";
static const char* kRepoSettings = "/boot/system/settings/Repositories_settings";
static const char* kUserRepoSettings =
	"/boot/home/config/settings/Repositories_settings";
static const char* kPackageRepos = "/boot/system/settings/package-repositories";

static void
forget_expired_grants()
{
	time_t now = time(NULL);
	for (int32 i = 0; i < kMaxAdminGrants; i++) {
		if (sGrants[i].team > 0 && sGrants[i].until <= now)
			sGrants[i].team = -1;
	}
}


static bool
team_has_grant(team_id team)
{
	forget_expired_grants();
	if (team <= 0)
		return false;
	time_t now = time(NULL);
	for (int32 i = 0; i < kMaxAdminGrants; i++) {
		if (sGrants[i].team == team && sGrants[i].until > now)
			return true;
	}
	return false;
}


static bool
any_grant()
{
	forget_expired_grants();
	for (int32 i = 0; i < kMaxAdminGrants; i++) {
		if (sGrants[i].team > 0)
			return true;
	}
	return false;
}


static void
remember_grant(team_id team)
{
	forget_expired_grants();
	time_t until = time(NULL) + kAdminGrantSeconds;
	for (int32 i = 0; i < kMaxAdminGrants; i++) {
		if (sGrants[i].team == team) {
			sGrants[i].until = until;
			return;
		}
	}
	for (int32 i = 0; i < kMaxAdminGrants; i++) {
		if (sGrants[i].team <= 0) {
			sGrants[i].team = team;
			sGrants[i].until = until;
			return;
		}
	}
}


static void
forget_grant(team_id team)
{
	for (int32 i = 0; i < kMaxAdminGrants; i++) {
		if (sGrants[i].team == team)
			sGrants[i].team = -1;
	}
}


static bool
sender_may_change(BMessage* request)
{
	// Only a request the kernel named a sender for changes anything.
	if (!sSenderVerified || request == NULL)
		return false;
	if (sender_is_admin(request))
		return true;
	return team_has_grant(sSenderTeam);
}


static uid_t
first_grant_uid()
{
	forget_expired_grants();
	team_info info;
	for (int32 i = 0; i < kMaxAdminGrants; i++) {
		if (sGrants[i].team <= 0)
			continue;
		if (get_team_info(sGrants[i].team, &info) == B_OK && info.uid != 0)
			return info.uid;
	}
	return 0;
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// During a grant the granted team owns these folders and can put a link
// in them; nothing here follows one (the review after issue #4643).
static void
chmod_settings_tree_fd(int dirFd, mode_t dirMode, mode_t fileMode,
	uid_t uid, gid_t gid, int32 depth)
{
	r5_admin_set_owner(dirFd, uid, gid, dirMode);
	if (depth > 16)
		return;
	int listFd = dup(dirFd);
	if (listFd < 0)
		return;
	DIR* folder = fdopendir(listFd);
	if (folder == NULL) {
		close(listFd);
		return;
	}
	while (struct dirent* entry = readdir(folder)) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;
		int fd = openat(dirFd, entry->d_name, O_RDONLY | O_NOFOLLOW
			| O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue;
		struct stat st;
		if (fstat(fd, &st) == 0) {
			if (S_ISDIR(st.st_mode)) {
				chmod_settings_tree_fd(fd, dirMode, fileMode, uid, gid,
					depth + 1);
			} else if (S_ISREG(st.st_mode))
				r5_admin_set_owner(fd, uid, gid, fileMode);
		}
		close(fd);
	}
	closedir(folder);
}


static void
chmod_settings_tree(const char* dir, mode_t dirMode, mode_t fileMode,
	uid_t uid, gid_t gid)
{
	if (dir == NULL)
		return;
	int fd = r5_admin_open_nofollow(dir, NULL, O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		return;
	chmod_settings_tree_fd(fd, dirMode, fileMode, uid, gid, 0);
	close(fd);
}
// SPDX-SnippetEnd


static void
write_grant_file()
{
	FILE* file = fopen(kGrantPath, "w");
	if (file == NULL)
		return;
	forget_expired_grants();
	for (int32 i = 0; i < kMaxAdminGrants; i++) {
		if (sGrants[i].team <= 0)
			continue;
		fprintf(file, "team %ld until %lld\n", (long)sGrants[i].team,
			(long long)sGrants[i].until);
	}
	fclose(file);
	chmod(kGrantPath, 0644);
}


void refresh_machine_settings_access();


void
clear_admin_grants()
{
	for (int32 i = 0; i < kMaxAdminGrants; i++)
		sGrants[i].team = -1;
	refresh_machine_settings_access();
}


static bool
directory_has_entries(const char* path)
{
	DIR* folder = opendir(path);
	if (folder == NULL)
		return false;
	bool any = false;
	while (struct dirent* entry = readdir(folder)) {
		if (strcmp(entry->d_name, ".") != 0
			&& strcmp(entry->d_name, "..") != 0) {
			any = true;
			break;
		}
	}
	closedir(folder);
	return any;
}


static void
copy_tree_if_empty(const char* from, const char* to)
{
	if (directory_has_entries(to) || !directory_has_entries(from))
		return;
	DIR* folder = opendir(from);
	if (folder == NULL)
		return;
	while (struct dirent* entry = readdir(folder)) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;
		BString source;
		BString dest;
		source.SetToFormat("%s/%s", from, entry->d_name);
		dest.SetToFormat("%s/%s", to, entry->d_name);
		BString ignored;
		char* argv[] = {
			const_cast<char*>("/bin/cp"),
			const_cast<char*>("-R"),
			const_cast<char*>(source.String()),
			const_cast<char*>(dest.String()),
			NULL
		};
		run_argv(argv, ignored);
	}
	closedir(folder);
}


static void
copy_file_if_missing(const char* from, const char* to)
{
	if (access(to, F_OK) == 0 || access(from, F_OK) != 0)
		return;
	FILE* in = fopen(from, "r");
	if (in == NULL)
		return;
	FILE* out = fopen(to, "w");
	if (out == NULL) {
		fclose(in);
		return;
	}
	char buffer[4096];
	size_t got = 0;
	while ((got = fread(buffer, 1, sizeof(buffer), in)) > 0) {
		if (fwrite(buffer, 1, got, out) != got)
			break;
	}
	fclose(out);
	fclose(in);
}


static void
share_file(const char* path, mode_t mode, uid_t uid, gid_t gid)
{
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// A link put here during a grant is not followed.
	int fd = r5_admin_open_nofollow(path, NULL, O_RDONLY);
	if (fd < 0)
		return;
	r5_admin_set_owner(fd, uid, gid, mode);
	close(fd);
	// SPDX-SnippetEnd
}


void
refresh_machine_settings_access()
{
	BString error;
	ensure_admin_group(error);
	struct group* admin = getgrnam(kAdminGroupName);
	gid_t gid = admin != NULL ? admin->gr_gid : 0;
	bool open = any_grant();
	uid_t owner = open ? first_grant_uid() : 0;
	// Owner is the granted Settings team. Group admin still writes. Never
	// 0777/0666: any local uid used to be able to change machine settings
	// for the five-minute grant.
	mode_t dirMode = (open && owner != 0) ? 0770 : 0775;
	mode_t fileMode = (open && owner != 0) ? 0660 : 0664;

	mkdir(kNetworkSettings, 0755);
	chmod_settings_tree(kNetworkSettings, dirMode, fileMode, owner, gid);

	mkdir(kMachinePrinters, 0755);
	copy_tree_if_empty(kUserPrinters, kMachinePrinters);
	chmod_settings_tree(kMachinePrinters, dirMode, fileMode, owner, gid);

	mkdir("/boot/system/settings/kernel", 0755);
	mkdir("/boot/system/settings/kernel/drivers", 0755);
	chown("/boot/system/settings/kernel", 0, gid);
	chown("/boot/system/settings/kernel/drivers", 0, gid);
	chmod("/boot/system/settings/kernel", 0755);
	chmod("/boot/system/settings/kernel/drivers", 0755);
	copy_file_if_missing(kUserSwapSettings, kSwapSettings);
	if (access(kSwapSettings, F_OK) != 0) {
		FILE* created = fopen(kSwapSettings, "w");
		if (created != NULL)
			fclose(created);
	}
	share_file(kSwapSettings, fileMode, owner, gid);

	copy_file_if_missing(kUserRepoSettings, kRepoSettings);
	share_file(kRepoSettings, fileMode, owner, gid);

	mkdir(kPackageRepos, 0755);
	chmod_settings_tree(kPackageRepos, dirMode, fileMode, owner, gid);

	write_grant_file();
}


static bool
sender_uid(BMessage* request, uid_t& uid)
{
	if (request == NULL || !sSenderVerified)
		return false;
	uid = sSenderUid;
	return true;
}


static bool
sender_may_edit(BMessage* request, const struct passwd* pwd)
{
	if (pwd == NULL)
		return false;
	if (sender_may_change(request))
		return true;
	uid_t uid = (uid_t)-1;
	return sender_uid(request, uid) && uid == pwd->pw_uid;
}


static BString
account_settings_dir(const char* home)
{
	BString path;
	if (home != NULL)
		path.SetToFormat("%s/config/settings/r5-account", home);
	return path;
}


static status_t
ensure_account_dir(const struct passwd* pwd, BString& dir)
{
	if (pwd == NULL || pwd->pw_dir == NULL)
		return B_BAD_VALUE;
	BString settings;
	settings.SetToFormat("%s/config/settings", pwd->pw_dir);
	mkdir(settings.String(), 0700);
	chown(settings.String(), pwd->pw_uid, pwd->pw_gid);
	dir = account_settings_dir(pwd->pw_dir);
	if (mkdir(dir.String(), 0700) != 0 && errno != EEXIST)
		return errno;
	chmod(dir.String(), 0700);
	if (chown(dir.String(), pwd->pw_uid, pwd->pw_gid) != 0)
		return errno;
	return B_OK;
}


static status_t
write_owned_file(const char* path, const void* data, size_t size,
	uid_t uid, gid_t gid)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0)
		return errno;
	const char* bytes = (const char*)data;
	size_t done = 0;
	while (done < size) {
		ssize_t wrote = write(fd, bytes + done, size - done);
		if (wrote < 0) {
			int error = errno;
			close(fd);
			return error;
		}
		done += wrote;
	}
	close(fd);
	chmod(path, 0600);
	chown(path, uid, gid);
	return B_OK;
}


static void
read_profile_files(const struct passwd* pwd, BMessage& reply)
{
	if (pwd == NULL || pwd->pw_dir == NULL)
		return;
	BString dir = account_settings_dir(pwd->pw_dir);
	BString emailPath;
	emailPath.SetToFormat("%s/email", dir.String());
	FILE* file = fopen(emailPath.String(), "r");
	if (file != NULL) {
		char buffer[256];
		if (fgets(buffer, sizeof(buffer), file) != NULL) {
			size_t length = strlen(buffer);
			while (length > 0 && (buffer[length - 1] == '\n'
				|| buffer[length - 1] == '\r'))
				buffer[--length] = '\0';
			reply.AddString("email", buffer);
		}
		fclose(file);
	}

	BString picturePath;
	picturePath.SetToFormat("%s/picture", dir.String());
	int fd = open(picturePath.String(), O_RDONLY);
	if (fd < 0)
		return;
	struct stat st;
	if (fstat(fd, &st) != 0 || st.st_size < 12 || st.st_size > 512 * 1024) {
		close(fd);
		return;
	}
	char* bytes = new char[st.st_size];
	ssize_t got = read(fd, bytes, st.st_size);
	close(fd);
	if (got == st.st_size)
		reply.AddData("picture", B_RAW_TYPE, bytes, got);
	delete[] bytes;
}


static status_t
rewrap_keystore_for_account(const BString& home, uid_t uid, gid_t gid,
	const BSecurityKit::BSecureBuffer& oldKek,
	const BSecurityKit::BSecureBuffer& newKek, const uint8 newSalt[16],
	const BSecurityKit::BPasswordHash::Parameters& newParams)
{
	static const char* kKeyStoreSig
		= "application/x-vnd.Haiku-keystore_server";
	team_id ksTeam = be_roster->TeamFor(kKeyStoreSig);
	team_info tInfo;
	app_info appInfo;
	if (ksTeam >= 0 && get_team_info(ksTeam, &tInfo) == B_OK
		&& tInfo.uid == uid && tInfo.uid != 0
		&& be_roster->GetRunningAppInfo(ksTeam, &appInfo) == B_OK) {
		BPath imgPath(&appInfo.ref);
		if (imgPath.InitCheck() == B_OK
			&& (strcmp(imgPath.Path(),
				"/boot/system/servers/keystore_server") == 0
			|| strcmp(imgPath.Path(),
				"/system/servers/keystore_server") == 0)) {
			BMessage rewrap(BPrivate::kMsgR5KeyStoreRewrap);
			rewrap.AddData(BPrivate::kR5KSFieldOldKek, B_RAW_TYPE,
				oldKek.Data(), oldKek.Size());
			rewrap.AddData(BPrivate::kR5KSFieldNewKek, B_RAW_TYPE,
				newKek.Data(), newKek.Size());
			rewrap.AddData(BPrivate::kR5KSFieldSalt, B_RAW_TYPE,
				newSalt, 16);
			rewrap.AddUInt32(BPrivate::kR5KSFieldMemKiB, newParams.memoryKiB);
			rewrap.AddUInt32(BPrivate::kR5KSFieldPasses, newParams.passes);
			rewrap.AddUInt32(BPrivate::kR5KSFieldLanes, newParams.lanes);

			BMessage ksReply;
			status_t sendStatus = r5_admin_send(BMessenger(NULL, ksTeam),
				&rewrap, &ksReply);
			const void* raw = NULL;
			ssize_t rawSize = 0;
			if (rewrap.FindData(BPrivate::kR5KSFieldOldKek, B_RAW_TYPE,
					&raw, &rawSize) == B_OK && raw != NULL && rawSize > 0) {
				BSecurityKit::BSecureBuffer::WipeMemory(
					const_cast<void*>(raw), static_cast<size_t>(rawSize));
			}
			if (rewrap.FindData(BPrivate::kR5KSFieldNewKek, B_RAW_TYPE,
					&raw, &rawSize) == B_OK && raw != NULL && rawSize > 0) {
				BSecurityKit::BSecureBuffer::WipeMemory(
					const_cast<void*>(raw), static_cast<size_t>(rawSize));
			}
			rewrap.MakeEmpty();
			if (sendStatus == B_OK) {
				status_t ksStatus = ksReply.GetInt32(
					BPrivate::kR5KSFieldStatus, B_ERROR);
				if (ksStatus == B_OK)
					return B_OK;
			}
		}
	}

	return BPrivate::R5KeyStoreCrypto::RewrapAccountPasswordProtector(
		home.String(), uid, gid, oldKek, newKek, newSalt, newParams);
}


static status_t
update_profile(BMessage* request, BString& error)
{
	const char* name = NULL;
	request->FindString("name", &name);
	struct passwd* pwd = name != NULL ? getpwnam(name) : NULL;
	if (pwd == NULL || name == NULL || strcmp(pwd->pw_name, name) != 0
		|| !is_listed_account(pwd)) {
		error = "That account was not found.";
		return ENOENT;
	}
	if (!sender_may_edit(request, pwd)) {
		error = "Unlock this with an administrator password.";
		return B_PERMISSION_DENIED;
	}

	char account[64];
	snprintf(account, sizeof(account), "%s", pwd->pw_name);
	uid_t uid = pwd->pw_uid;
	gid_t gid = pwd->pw_gid;
	BString home(pwd->pw_dir != NULL ? pwd->pw_dir : "");
	bool self = false;
	uid_t sender = (uid_t)-1;
	if (sender_uid(request, sender))
		self = sender == uid;

	const char* real = NULL;
	if (request->FindString("real", &real) == B_OK && real != NULL) {
		if (strchr(real, ':') != NULL || strchr(real, '\n') != NULL
			|| strchr(real, '\r') != NULL) {
			error = "The name cannot contain a colon or a new line.";
			return B_BAD_VALUE;
		}
		if (strlen(real) > 80) {
			error = "That name is too long.";
			return B_BAD_VALUE;
		}
		BPrivate::KMessage message(BPrivate::B_REG_UPDATE_USER);
		if (message.AddInt32("uid", uid) != B_OK
			|| message.AddString("real name", real) != B_OK)
			return B_NO_MEMORY;
		BPrivate::KMessage registrarReply;
		status_t status = BPrivate::send_authentication_request_to_registrar(
			message, registrarReply);
		if (status != B_OK) {
			error = "The name could not be saved.";
			return status;
		}
	}

	const char* password = request->GetString("password", NULL);
	if (password != NULL && password[0] != '\0') {
		pwd = getpwnam(account);
		const char* current = request->GetString("current", "");
		if (self && !account_password_matches(pwd, current)) {
			error = "That password is not right.";
			return B_PERMISSION_DENIED;
		}
		BString hash;
		if (make_password_hash(password, hash) != B_OK) {
			error = "Could not encrypt the password.";
			return B_ERROR;
		}

		// For own password change, rewrap the key store protector BEFORE
		// updating /etc/shadow so a rewrap failure never leaves the sign-in
		// password out of sync with the key store.
		bool rewrapped = false;
		BPrivate::R5PasswordProtectorParams oldParams;
		BSecurityKit::BSecureBuffer oldKek;
		BSecurityKit::BSecureBuffer newKek;
		if (self && home.Length() > 0) {
			status_t protStatus = BPrivate::R5KeyStoreCrypto::
				ReadAccountPasswordProtectorParams(home.String(), uid,
					oldParams);
			if (protStatus == B_OK) {
				uint8 newSalt[16];
				BSecurityKit::BPasswordHash::Parameters newParams
					= BSecurityKit::BPasswordHash::DefaultParameters();
				status_t rewrapStatus = B_ERROR;
				if (BSecurityKit::BRandom::Fill(newSalt, sizeof(newSalt))
						== B_OK
					&& BSecurityKit::BPasswordHash::DeriveKey(current,
						strlen(current), oldParams.salt,
						sizeof(oldParams.salt), oldParams.kdf, oldKek,
						BSecurityKit::BSealedData::kKeySize) == B_OK
					&& BSecurityKit::BPasswordHash::DeriveKey(password,
						strlen(password), newSalt, sizeof(newSalt),
						newParams, newKek,
						BSecurityKit::BSealedData::kKeySize) == B_OK) {
					rewrapStatus = rewrap_keystore_for_account(home, uid, gid,
						oldKek, newKek, newSalt, newParams);
				}
				if (rewrapStatus != B_OK) {
					oldKek.Clear();
					newKek.Clear();
					syslog(LOG_ERR,
						"AccountAdmin: keystore rewrap failed: %s",
						strerror(rewrapStatus));
					error = "Your password was not changed because the key "
						"store could not be updated.";
					return rewrapStatus;
				}
				rewrapped = true;
			}
		}

		if (set_shadow_password(account, hash.String()) != B_OK) {
			error = "The password could not be saved.";
			if (rewrapped) {
				status_t rollback = rewrap_keystore_for_account(home, uid,
					gid, newKek, oldKek, oldParams.salt, oldParams.kdf);
				if (rollback != B_OK) {
					// The key store now opens with the new password, which
					// was not applied. At the next sign-in it is moved aside
					// and the "old password" question needs the new one.
					syslog(LOG_ERR, "AccountAdmin: keystore rollback after a "
						"failed password change failed: %s",
						strerror(rollback));
					error = "The password could not be saved. If you are "
						"later asked for your old password for saved "
						"passwords, enter the new password you just typed.";
				}
			}
			oldKek.Clear();
			newKek.Clear();
			return B_ERROR;
		}
		oldKek.Clear();
		newKek.Clear();

		if (!self && home.Length() > 0) {
			// Administrator password reset: preserve existing keystore in the
			// next free .old / .old2 .. .old16 slot and create a new empty
			// encrypted keystore with the new password.
			status_t resetStatus = BPrivate::R5KeyStoreCrypto::
				ResetAccountKeyStore(home.String(), uid, gid, password);
			if (resetStatus != B_OK) {
				syslog(LOG_ERR,
					"AccountAdmin: ResetAccountKeyStore failed: %s",
					strerror(resetStatus));
			}
		}
	}

	pwd = getpwnam(account);
	if (pwd == NULL) {
		error = "That account was not found.";
		return ENOENT;
	}
	home.SetTo(pwd->pw_dir != NULL ? pwd->pw_dir : "");
	uid = pwd->pw_uid;
	gid = pwd->pw_gid;

	const char* email = NULL;
	bool hasEmail = request->FindString("email", &email) == B_OK;
	const void* picture = NULL;
	ssize_t pictureSize = 0;
	bool hasPicture = request->FindData("picture", B_RAW_TYPE, &picture,
		&pictureSize) == B_OK;
	bool clearPicture = request->GetBool("clear_picture", false);
	if (!hasEmail && !hasPicture && !clearPicture)
		return B_OK;

	BString dir;
	status_t status = ensure_account_dir(pwd, dir);
	if (status != B_OK) {
		error = "The account folder could not be updated.";
		return status;
	}
	if (hasEmail) {
		if (email == NULL)
			email = "";
		if (strlen(email) > 200 || strchr(email, '\n') != NULL) {
			error = "That email address cannot be saved.";
			return B_BAD_VALUE;
		}
		BString path;
		path.SetToFormat("%s/email", dir.String());
		status = write_owned_file(path.String(), email, strlen(email),
			uid, gid);
		if (status != B_OK) {
			error = "The email address could not be saved.";
			return status;
		}
	}
	BString picturePath;
	picturePath.SetToFormat("%s/picture", dir.String());
	if (clearPicture)
		unlink(picturePath.String());
	else if (hasPicture) {
		if (pictureSize < 12 || pictureSize > 512 * 1024) {
			error = "That picture is too large.";
			return B_BAD_VALUE;
		}
		status = write_owned_file(picturePath.String(), picture, pictureSize,
			uid, gid);
		if (status != B_OK) {
			error = "The picture could not be saved.";
			return status;
		}
	}
	return B_OK;
}


// Users and Accounts in Settings reads the accounts here: Login runs as
// uid 0, so it can read the shadow entry that says whether an account is
// disabled, which the signed-in account cannot.
static void
list_accounts(BMessage* request, BMessage& reply)
{
	bool all = sender_may_change(request);
	uid_t self = (uid_t)-1;
	bool known = sender_uid(request, self);
	setpwent();
	for (;;) {
		struct passwd* pwd = getpwent();
		if (pwd == NULL)
			break;
		if (!is_listed_account(pwd))
			continue;
		if (!all && (!known || pwd->pw_uid != self))
			continue;
		reply.AddString("name", pwd->pw_name);
		reply.AddString("real", pwd->pw_gecos != NULL ? pwd->pw_gecos : "");
		reply.AddString("home", pwd->pw_dir != NULL ? pwd->pw_dir : "");
		reply.AddInt32("uid", (int32)pwd->pw_uid);
		reply.AddBool("admin", is_admin_user(pwd));
		reply.AddBool("disabled", is_disabled_user(pwd->pw_name));
		reply.AddBool("first", is_first_home_admin(pwd));
	}
	endpwent();
	if (all)
		reply.AddInt32("admins", admin_count());
}


static status_t
set_account_type(const char* name, bool admin, BString& error)
{
	struct passwd* pwd = name != NULL ? getpwnam(name) : NULL;
	if (pwd == NULL || strcmp(pwd->pw_name, name) != 0
		|| !is_listed_account(pwd)) {
		error = "That account was not found.";
		return ENOENT;
	}
	char account[64];
	snprintf(account, sizeof(account), "%s", pwd->pw_name);
	bool wasAdmin = is_admin_user(pwd);
	BString detail;
	if (admin) {
		if (wasAdmin)
			return B_OK;
		if (add_to_group(kAdminGroupName, account, detail) != B_OK) {
			ensure_admin_group(detail);
			if (add_to_group(kAdminGroupName, account, detail) != B_OK) {
				error = "The account could not become an administrator.";
				return B_ERROR;
			}
		}
		return B_OK;
	}

	if (!wasAdmin)
		return B_OK;
	if (is_first_home_admin(pwd)) {
		error = "The first administrator stays an administrator.";
		return B_NOT_ALLOWED;
	}
	if (pwd->pw_uid == 0 || pwd->pw_gid == 0) {
		error = "This account is an administrator through its system group.";
		return B_NOT_ALLOWED;
	}
	if (!is_disabled_user(account) && admin_count() <= 1) {
		error = "The last administrator cannot become a standard account.";
		return B_NOT_ALLOWED;
	}
	if (remove_from_group(kAdminGroupName, account, detail) != B_OK
		|| remove_from_group("wheel", account, detail) != B_OK) {
		error = "The account type could not be changed.";
		return B_ERROR;
	}
	pwd = getpwnam(account);
	if (pwd != NULL && is_admin_user(pwd)) {
		error = "The account type could not be changed.";
		return B_ERROR;
	}
	return B_OK;
}


static void
deny_locked(BMessage& reply)
{
	reply.AddString("text", "Unlock this with an administrator password.");
	reply.AddInt32("error", B_PERMISSION_DENIED);
}


status_t
handle_admin_message(BMessage* request, BMessage& reply)
{
	reply.MakeEmpty();
	if (request == NULL)
		return B_BAD_VALUE;
	if (geteuid() != 0) {
		reply.AddString("text", "Account changes require the sign-in helper.");
		reply.AddInt32("error", B_PERMISSION_DENIED);
		return B_PERMISSION_DENIED;
	}

	team_id sender = sender_team(request);
	// On the normal messenger only the query: everything else comes
	// through Login's admin port (libr5admin.so), where the kernel names
	// the sender.
	if (!sSenderVerified && request->what != kMsgAdminQuery) {
		reply.AddString("text", "This request must come through "
			"r5_admin_send().");
		reply.AddInt32("error", B_NOT_ALLOWED);
		return B_NOT_ALLOWED;
	}
	BString error;
	status_t status = B_OK;
	switch (request->what) {
		case kMsgAdminQuery:
		{
			bool admin = sender_is_admin(request);
			reply.AddBool("admin", admin);
			reply.AddBool("unlocked", admin || team_has_grant(sender));
			reply.AddInt32("error", B_OK);
			refresh_machine_settings_access();
			return B_OK;
		}
		case kMsgAdminUnlock:
		{
			const char* name = NULL;
			request->FindString("name", &name);
			const char* password = request->GetString("password", "");
			struct passwd* pwd = name != NULL ? getpwnam(name) : NULL;
			if (pwd == NULL || name == NULL || strcmp(pwd->pw_name, name) != 0)
				status = ENOENT;
			else if (!is_listed_account(pwd) || !is_admin_user(pwd))
				status = B_NOT_ALLOWED;
			else if (is_disabled_user(name))
				status = B_NOT_ALLOWED;
			else if (!account_password_matches(pwd, password))
				status = B_PERMISSION_DENIED;
			else {
				remember_grant(sender);
				refresh_machine_settings_access();
				reply.AddBool("unlocked", true);
				reply.AddInt32("error", B_OK);
				return B_OK;
			}
			if (status == B_PERMISSION_DENIED)
				error = "That password is not right.";
			else if (status == ENOENT)
				error = "That account was not found.";
			else
				error = "That account cannot unlock settings.";
			break;
		}
		case kMsgAdminRelock:
			forget_grant(sender);
			refresh_machine_settings_access();
			reply.AddBool("unlocked", sender_is_admin(request));
			reply.AddInt32("error", B_OK);
			return B_OK;

		case kMsgAdminGetProfile:
		{
			const char* name = NULL;
			request->FindString("name", &name);
			struct passwd* pwd = name != NULL ? getpwnam(name) : NULL;
			if (pwd == NULL || !is_listed_account(pwd)
				|| !sender_may_edit(request, pwd)) {
				reply.AddString("text", "That account was not found.");
				reply.AddInt32("error", B_PERMISSION_DENIED);
				return B_PERMISSION_DENIED;
			}
			read_profile_files(pwd, reply);
			reply.AddInt32("error", B_OK);
			return B_OK;
		}
		case kMsgAdminListAccounts:
			list_accounts(request, reply);
			reply.AddInt32("error", B_OK);
			return B_OK;
		case kMsgAdminGetGuestEnabled:
			reply.AddBool("enabled", is_guest_enabled());
			reply.AddInt32("error", B_OK);
			return B_OK;
		case kMsgAdminSetProfile:
			status = update_profile(request, error);
			if (status != B_OK) {
				if (error.Length() > 0)
					reply.AddString("text", error.String());
				reply.AddInt32("error", status);
			} else
				reply.AddInt32("error", B_OK);
			return status;

		case kMsgAdminSetClock:
		case kMsgAdminSetTimezone:
		case kMsgAdminSetClockGmt:
			if (!sender_may_change(request)) {
				deny_locked(reply);
				return B_PERMISSION_DENIED;
			}
			if (request->what == kMsgAdminSetClock) {
				int64 seconds = 0;
				if (request->FindInt64("seconds", &seconds) != B_OK)
					status = B_BAD_VALUE;
				else {
					status = _kern_set_real_time_clock(
						(bigtime_t)seconds * 1000000);
				}
			} else if (request->what == kMsgAdminSetTimezone) {
				int32 offset = 0;
				const char* name = NULL;
				request->FindInt32("offset", &offset);
				request->FindString("name", &name);
				if (name == NULL)
					name = "";
				status = _kern_set_timezone(offset, name, strlen(name));
			} else {
				status = _kern_set_real_time_clock_is_gmt(
					request->GetBool("gmt", false));
			}
			if (status != B_OK)
				error = "The clock could not be changed.";
			break;

		default:
			break;
	}

	if (request->what == kMsgAdminUnlock) {
		if (error.Length() > 0)
			reply.AddString("text", error.String());
		reply.AddInt32("error", status);
		return status;
	}
	if (request->what == kMsgAdminSetClock || request->what == kMsgAdminSetTimezone
		|| request->what == kMsgAdminSetClockGmt) {
		if (status != B_OK) {
			if (error.Length() > 0)
				reply.AddString("text", error.String());
			reply.AddInt32("error", status);
		} else
			reply.AddInt32("error", B_OK);
		return status;
	}

	if (!sender_may_change(request)) {
		deny_locked(reply);
		return B_PERMISSION_DENIED;
	}

	status = B_BAD_VALUE;
	switch (request->what) {
		case kMsgAdminAdd:
		{
			const char* name = NULL;
			request->FindString("name", &name);
			status = create_account(name, request->GetString("real"),
				request->GetString("password"),
				request->GetBool("admin", false), error);
			break;
		}
		case kMsgAdminDelete:
		{
			const char* name = NULL;
			request->FindString("name", &name);
			status = delete_account(name, request->GetBool("delete_files",
				false), error);
			break;
		}
		case kMsgAdminSetDisabled:
		{
			const char* name = NULL;
			request->FindString("name", &name);
			status = set_disabled(name, request->GetBool("disable", true),
				error);
			break;
		}
		case kMsgAdminSetAccountType:
		{
			const char* name = NULL;
			request->FindString("name", &name);
			status = set_account_type(name, request->GetBool("admin", false),
				error);
			break;
		}
		case kMsgAdminSetGuestEnabled:
		{
			status = set_guest_enabled(request->GetBool("enabled", false),
				error);
			break;
		}
		default:
			error = "Unknown account request.";
			status = B_BAD_VALUE;
			break;
	}

	if (status != B_OK && error.Length() > 0)
		reply.AddString("text", error.String());
	reply.AddInt32("error", status == B_OK ? B_OK : status);
	return status;
}


bool
admin_sender_may_change(BMessage* request)
{
	return sender_may_change(request);
}
