/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * R5OS account helper. Login stays uid 0 and applies passwd changes.
 */
#ifndef R5_ACCOUNT_ADMIN_H
#define R5_ACCOUNT_ADMIN_H


#include <OS.h>
#include <SupportDefs.h>

#include <pwd.h>


class BMessage;
class BString;


extern const char* kR5LoginSignature;
extern const char* kAdminGroupName;
extern const char* kGuestAccountName;
extern const char* kGuestHomePath;

static const char* const kSharedFolderPath = "/boot/home/Shared";
static const uid_t kMinPersonUid = 1000;
static const uid_t kGuestUid = 1999;

const uint32 kMsgAdminAdd = 'r5aa';
const uint32 kMsgAdminDelete = 'r5ad';
const uint32 kMsgAdminSetDisabled = 'r5ax';
// The lock messages below have the same values in libr5ui's
// AdminLockProtocol.h. Change neither.
const uint32 kMsgAdminQuery = 'r5aq';
const uint32 kMsgAdminUnlock = 'r5au';
const uint32 kMsgAdminRelock = 'r5al';
const uint32 kMsgAdminSetClock = 'r5at';
const uint32 kMsgAdminSetTimezone = 'r5az';
const uint32 kMsgAdminSetClockGmt = 'r5ag';
const uint32 kMsgAdminGetProfile = 'r5ah';
const uint32 kMsgAdminSetProfile = 'r5af';
// Device Manager: block or unblock one packaged kernel driver. Fields
// "driver" (string, the driver file under /boot/system) and "blocked" (bool).
const uint32 kMsgAdminSetDriverBlocked = 'r5dv';
// Media: write a driver setting in kernel/drivers. Fields "file" (string),
// "key" (strings, the lines replaced), "lines" (string) and "replace" (bool).
// Login only accepts the files and keys in DriverSettingsAdmin.cpp.
const uint32 kMsgAdminSetDriverSetting = 'r5ds';
// Users and Accounts in Settings. The accounts, one entry per account in
// the repeated fields "name", "real" and "home" (strings), "uid" (int32),
// "admin", "disabled" and "first" (bools, "first" for the administrator
// whose home is /boot/home), plus "admins" (int32, the enabled
// administrators; only with every account). An administrator, or a team
// with a grant, gets every account; any other sender only its own.
const uint32 kMsgAdminListAccounts = 'r5ay';
// Make an account an administrator or a standard account. Fields "name"
// (string) and "admin" (bool). Needs an administrator. The first
// administrator and the last enabled administrator stay administrators.
const uint32 kMsgAdminSetAccountType = 'r5ak';
// Guest sign-in switch (off by default, stored machine-wide by Login).
// kMsgAdminGetGuestEnabled replies with bool "enabled".
// kMsgAdminSetGuestEnabled takes bool "enabled" and requires an administrator.
const uint32 kMsgAdminGetGuestEnabled = 'r5ge';
const uint32 kMsgAdminSetGuestEnabled = 'r5gs';

// How long an administrator password keeps one preference app unlocked.
static const int32 kAdminGrantSeconds = 5 * 60;


bool is_listed_account(const struct passwd* pwd);
bool is_guest_account(const char* name);
bool is_guest_enabled();
bool is_admin_user(const struct passwd* pwd);
bool is_disabled_user(const char* name);
bool is_first_home_admin(const struct passwd* pwd);
bool current_user_is_admin();
bool account_password_matches(const struct passwd* pwd, const char* plain);
// Accounts step 5: new sign-in hashes are Argon2id PHC strings made by the
// Security Kit (BPasswordHash); is_kit_password_hash() tells them apart from
// the older scrypt ("$s$") hashes that crypt() checks.
bool is_kit_password_hash(const char* hash);
status_t make_password_hash(const char* password, BString& outHash);
// Accounts step 5: the R5 image is the live medium ("Try R5"); an installed
// R5 is not. On an installed R5 an account without a password sets one
// before its first desktop (needs_first_password(), set_first_password()).
bool is_live_medium();
bool account_has_no_password(const struct passwd* pwd);
bool needs_first_password(const char* login);
status_t set_first_password(const char* login, const char* realName,
	const char* password, BString& error);
int32 admin_count();

status_t migrate_admin_off_root();
// Issue #4644: after migrate_admin_off_root(), true while /boot/home still
// has to be given to the first administrator ("uid", "gid"). The walk runs
// once: own_first_home() writes a marker when it is done. Not on Login's
// message loop; "progress" is called on the walking thread with the number
// of entries done.
typedef void (*home_walk_progress)(int64 count, void* cookie);
bool first_home_walk_needed(uid_t& uid, gid_t& gid);
status_t own_first_home(uid_t uid, gid_t gid, home_walk_progress progress,
	void* cookie);
status_t ensure_shared_folder();
status_t ensure_guest_account(BString& error);
status_t wipe_guest_home();
// True while a team of the guest account runs (a guest session).
bool guest_session_active();
status_t write_desktop_session(const char* login);
status_t clear_desktop_session();
status_t signed_in_name(BString& name);
void clear_admin_grants();
void refresh_machine_settings_access();
status_t handle_admin_message(BMessage* request, BMessage& reply);
// True when the sender is an administrator or holds a running grant.
bool admin_sender_may_change(BMessage* request);
// The sender of the request being handled, as the kernel named it
// (libr5admin.so, Login's admin port). Only while it is set do the
// requests that change something pass; a request on Login's normal
// messenger has a reply address its sender chose, which proves nothing.
void set_admin_sender(bool verified, uid_t uid, team_id team);


#endif
