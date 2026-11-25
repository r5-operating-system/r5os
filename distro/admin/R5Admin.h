/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * libr5admin.so: requests that change the machine, with a sender the
 * kernel names (docs/LIBR5.md, "Admin requests").
 *
 * A BMessage's ReturnAddress() is the reply messenger the sender chose, so
 * its team proves nothing: any program could name the team of Login or of
 * another uid 0 process there. A service that decides by the sender (an
 * administrator, uid 0, a team with a grant) reads such requests from its
 * own port instead. The kernel gives the uid, group and team of every
 * message on a port (get_port_message_info_etc()), and nobody can change
 * those.
 *
 * Service: r5_admin_port_start() once; answer R5_ADMIN_PORT_REQUEST on the
 * normal looper with r5_admin_port_reply(); decide in the handler by the
 * r5_admin_sender it gets. Client: r5_admin_send() instead of
 * BMessenger::SendMessage().
 *
 * A function that shipped does not change. New ones go at the end.
 */
#ifndef R5_ADMIN_H
#define R5_ADMIN_H


#include <Message.h>
#include <Messenger.h>
#include <OS.h>

#include <sys/types.h>


// Sent to a service's normal messenger: the answer has int32 "port", the
// service's admin port.
const uint32 R5_ADMIN_PORT_REQUEST = 'r5AP';


// Who sent a request, as the kernel saw it.
struct r5_admin_sender {
	uid_t		uid;
	gid_t		gid;
	team_id		team;
};


// Called on the port's own thread, one request at a time. The handler
// locks what it touches (for most services: be_app). "reply" goes back to
// the sender when it waits for one.
typedef void (*r5_admin_handler)(BMessage* request,
	const r5_admin_sender& sender, BMessage* reply, void* cookie);


// Service side. Creates the port and its reading thread.
status_t	r5_admin_port_start(const char* name, r5_admin_handler handler,
				void* cookie, port_id* _port);
// Deletes the port and waits for its thread.
void		r5_admin_port_stop(port_id port);
// In the service's MessageReceived(): true when the message was
// R5_ADMIN_PORT_REQUEST, which it then answered with "port".
bool		r5_admin_port_reply(BMessage* message, port_id port);

// Client side. Asks "service" for its admin port, checks that the port
// belongs to that service's team, sends "request" there and waits up to
// "timeout" for the reply.
status_t	r5_admin_send(const BMessenger& service, BMessage* request,
				BMessage* reply, bigtime_t timeout = 5000000);

// Service side, added 22 Nov 2026 for the power service: true when the
// sender may change a machine-wide setting: uid 0, a member of group
// "admin", or a team Login gave a grant (the administrator lock opened by a
// standard account, /boot/system/settings/r5-machine-unlock). The same rule
// as bluetooth_server's radio switch and Login's own check.
bool		r5_admin_sender_allowed(const r5_admin_sender& sender);

// Added 24 Nov 2026 for Deskbar: true when "uid" is the guest account that
// R5 made itself: login name "guest", home /boot/home/users/guest, and the
// uid recorded in /boot/system/settings/r5-guest-uid (owned by uid 0, not
// writable by group or others). An older account that is only called
// "guest" is not the guest. The same rule as Login's wipe and lock.
bool		r5_admin_is_system_guest(uid_t uid);

// Added 25 Nov 2026 (issue #4639 and the review after it). Marks a message
// as made in this team: it gets a field with a random token that only this
// team knows (128 bits from /dev/urandom, made once per team). Use it for a
// message that one part of a service posts to another part of the same
// service (a button of its own window, a BMessageRunner it made), where
// !IsSourceRemote() used to be the check: the reply address is whatever the
// sender chose, also this team's own messenger. B_OK, or the error of
// reading /dev/urandom (then r5_admin_is_local() is false for every
// message).
status_t	r5_admin_mark_local(BMessage* message);
// True only when "message" carries this team's token
// (r5_admin_mark_local()). A message from another team never does.
bool		r5_admin_is_local(const BMessage* message);

// Added 25 Nov 2026 (issue #4643), for a uid 0 program that changes files
// in a person's home. Opens "relative" under the directory "base" one part
// at a time, never following a symbolic link (also not the last part of
// "base"): a link fails with B_LINK_LIMIT (ELOOP). With "createMode" not 0,
// missing directories on the way are made with that mode, and so is the
// last part when "flags" has O_DIRECTORY. "flags" is passed to the last
// open (O_DIRECTORY, O_RDONLY, O_WRONLY | O_CREAT | O_EXCL, ...);
// O_NOFOLLOW, O_NONBLOCK and O_CLOEXEC are always added. A NULL or empty
// "relative" opens "base". Returns the file descriptor, or -1 with errno
// set.
int			r5_admin_open_nofollow(const char* base, const char* relative,
				int flags, mode_t createMode = 0, mode_t fileMode = 0600);
// Sets the owner and the mode of the open "fd" (fchown(), fchmod()), so
// the change cannot follow a link. A regular file with more than one hard
// link is refused (B_NOT_ALLOWED), so a link to a system file is never
// handed over. uid (uid_t)-1 keeps the owner; mode (mode_t)-1 keeps the
// mode.
status_t	r5_admin_set_owner(int fd, uid_t uid, gid_t gid, mode_t mode);


#endif	// R5_ADMIN_H
