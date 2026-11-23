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


#endif	// R5_ADMIN_H
