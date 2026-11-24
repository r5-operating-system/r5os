/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * libr5admin.so: see R5Admin.h.
 */


#include <R5Admin.h>

#include <grp.h>
#include <new>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>


static const int32 kRequestCode = 'r5AR';
static const int32 kReplyCode = 'r5AA';
static const char* kReplyPortField = "r5:reply port";
// Larger requests are refused: an admin request is a few fields.
static const ssize_t kMaxRequest = 64 * 1024;


namespace {


static void
wipe_memory(void* data, size_t size)
{
	volatile uint8* p = static_cast<volatile uint8*>(data);
	for (size_t i = 0; i < size; i++)
		p[i] = 0;
}


struct admin_port {
	port_id				port;
	thread_id			thread;
	r5_admin_handler	handler;
	void*				cookie;
	admin_port*			next;
};


admin_port* sPorts = NULL;
// Guards sPorts; start and stop are rare.
int32 sPortsLock = 0;


void
lock_ports()
{
	while (atomic_test_and_set(&sPortsLock, 1, 0) != 0)
		snooze(1000);
}


void
unlock_ports()
{
	atomic_set(&sPortsLock, 0);
}


void
answer(port_id replyPort, team_id senderTeam, BMessage& reply)
{
	// Only to a port of the team that sent the request: a request cannot
	// make the service write into someone else's port.
	port_info info;
	if (replyPort < 0 || get_port_info(replyPort, &info) != B_OK
		|| info.team != senderTeam)
		return;
	ssize_t size = reply.FlattenedSize();
	if (size <= 0)
		return;
	char* buffer = (char*)malloc(size);
	if (buffer == NULL)
		return;
	if (reply.Flatten(buffer, size) == B_OK) {
		write_port_etc(replyPort, kReplyCode, buffer, size,
			B_RELATIVE_TIMEOUT, 1000000);
	}
	free(buffer);
}


status_t
read_requests(void* data)
{
	admin_port* port = (admin_port*)data;
	while (true) {
		port_message_info info;
		status_t status = get_port_message_info_etc(port->port, &info, 0, 0);
		if (status == B_INTERRUPTED)
			continue;
		if (status != B_OK)
			break;

		int32 code;
		char* buffer = NULL;
		if (info.size > 0 && (ssize_t)info.size <= kMaxRequest)
			buffer = (char*)malloc(info.size);
		// Read even what is refused, or it would stay first in the port.
		char dummy;
		ssize_t read = read_port_etc(port->port, &code,
			buffer != NULL ? buffer : &dummy,
			buffer != NULL ? info.size : sizeof(dummy), B_RELATIVE_TIMEOUT, 0);
		if (read == B_BAD_PORT_ID)
			break;
		if (buffer == NULL || read != (ssize_t)info.size
			|| code != kRequestCode) {
			if (buffer != NULL) {
				wipe_memory(buffer, info.size);
				free(buffer);
			}
			continue;
		}

		BMessage request;
		status = request.Unflatten(buffer);
		// Requests may hold a password or derived key: wipe before freeing.
		wipe_memory(buffer, info.size);
		free(buffer);
		if (status != B_OK)
			continue;

		port_id replyPort = request.GetInt32(kReplyPortField, -1);
		request.RemoveName(kReplyPortField);

		r5_admin_sender sender;
		sender.uid = info.sender;
		sender.gid = info.sender_group;
		sender.team = info.sender_team;

		BMessage reply(B_REPLY);
		port->handler(&request, sender, &reply, port->cookie);
		answer(replyPort, sender.team, reply);
	}
	return B_OK;
}


} // namespace


status_t
r5_admin_port_start(const char* name, r5_admin_handler handler, void* cookie,
	port_id* _port)
{
	if (handler == NULL || _port == NULL)
		return B_BAD_VALUE;

	admin_port* port = new(std::nothrow) admin_port;
	if (port == NULL)
		return B_NO_MEMORY;
	port->handler = handler;
	port->cookie = cookie;
	port->port = create_port(16, name != NULL ? name : "r5 admin");
	if (port->port < 0) {
		status_t error = port->port;
		delete port;
		return error;
	}
	port->thread = spawn_thread(read_requests, "r5 admin requests",
		B_NORMAL_PRIORITY, port);
	if (port->thread < 0) {
		status_t error = port->thread;
		delete_port(port->port);
		delete port;
		return error;
	}

	lock_ports();
	port->next = sPorts;
	sPorts = port;
	unlock_ports();

	resume_thread(port->thread);
	*_port = port->port;
	return B_OK;
}


void
r5_admin_port_stop(port_id portID)
{
	lock_ports();
	admin_port** link = &sPorts;
	admin_port* port = NULL;
	while (*link != NULL) {
		if ((*link)->port == portID) {
			port = *link;
			*link = port->next;
			break;
		}
		link = &(*link)->next;
	}
	unlock_ports();
	if (port == NULL)
		return;

	delete_port(port->port);
	status_t result;
	wait_for_thread(port->thread, &result);
	delete port;
}


bool
r5_admin_port_reply(BMessage* message, port_id port)
{
	if (message == NULL || message->what != R5_ADMIN_PORT_REQUEST)
		return false;
	BMessage reply(B_REPLY);
	reply.AddInt32("port", port);
	message->SendReply(&reply);
	return true;
}


status_t
r5_admin_send(const BMessenger& service, BMessage* request, BMessage* reply,
	bigtime_t timeout)
{
	if (request == NULL || reply == NULL)
		return B_BAD_VALUE;
	if (!service.IsValid())
		return B_BAD_PORT_ID;

	// The service names its port, and the port must be its team's: a port
	// with the same name made by another program gets nothing.
	BMessage portRequest(R5_ADMIN_PORT_REQUEST);
	BMessage portReply;
	status_t status = service.SendMessage(&portRequest, &portReply, timeout,
		timeout);
	if (status != B_OK)
		return status;
	port_id port = portReply.GetInt32("port", -1);
	port_info info;
	if (port < 0 || get_port_info(port, &info) != B_OK
		|| info.team != service.Team())
		return B_BAD_PORT_ID;

	port_id replyPort = create_port(1, "r5 admin reply");
	if (replyPort < 0)
		return replyPort;

	BMessage copy(*request);
	copy.RemoveName(kReplyPortField);
	copy.AddInt32(kReplyPortField, replyPort);
	ssize_t size = copy.FlattenedSize();
	char* buffer = size > 0 && size <= kMaxRequest
		? (char*)malloc(size) : NULL;
	if (buffer == NULL) {
		delete_port(replyPort);
		return size > kMaxRequest ? B_BAD_VALUE : B_NO_MEMORY;
	}
	status = copy.Flatten(buffer, size);
	if (status == B_OK) {
		status = write_port_etc(port, kRequestCode, buffer, size,
			B_RELATIVE_TIMEOUT, timeout);
	}
	// The request may hold a password or derived key: wipe before freeing.
	wipe_memory(buffer, size);
	free(buffer);
	if (status != B_OK) {
		delete_port(replyPort);
		return status;
	}

	ssize_t replySize = port_buffer_size_etc(replyPort, B_RELATIVE_TIMEOUT,
		timeout);
	if (replySize < 0) {
		delete_port(replyPort);
		return replySize;
	}
	char* replyBuffer = (char*)malloc(replySize > 0 ? replySize : 1);
	if (replyBuffer == NULL) {
		delete_port(replyPort);
		return B_NO_MEMORY;
	}
	int32 code;
	ssize_t read = read_port_etc(replyPort, &code, replyBuffer, replySize,
		B_RELATIVE_TIMEOUT, 0);
	delete_port(replyPort);
	if (read < 0) {
		free(replyBuffer);
		return read;
	}
	status = code == kReplyCode ? reply->Unflatten(replyBuffer) : B_ERROR;
	free(replyBuffer);
	return status;
}


bool
r5_admin_sender_allowed(const r5_admin_sender& sender)
{
	// The kernel named this sender (admin port): uid, team and the grant
	// are its own, not a reply address it chose.
	if (sender.uid == 0)
		return true;

	struct passwd* account = getpwuid(sender.uid);
	struct group* admin = getgrnam("admin");
	if (account != NULL && account->pw_name != NULL && admin != NULL
		&& admin->gr_mem != NULL) {
		for (int32 i = 0; admin->gr_mem[i] != NULL; i++) {
			if (strcmp(admin->gr_mem[i], account->pw_name) == 0)
				return true;
		}
	}

	FILE* grants = fopen("/boot/system/settings/r5-machine-unlock", "r");
	if (grants == NULL)
		return false;
	bool granted = false;
	long grantTeam;
	long long until;
	time_t now = time(NULL);
	while (!granted
		&& fscanf(grants, " team %ld until %lld", &grantTeam, &until) == 2) {
		granted = grantTeam == (long)sender.team && until > (long long)now;
	}
	fclose(grants);
	return granted;
}


bool
r5_admin_is_system_guest(uid_t uid)
{
	static const char* kGuestUidPath = "/boot/system/settings/r5-guest-uid";

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
	if (!ok || value < 1000 || (uid_t)value != uid)
		return false;

	struct passwd entry;
	struct passwd* pwd = NULL;
	char buffer[1024];
	if (getpwuid_r(uid, &entry, buffer, sizeof(buffer), &pwd) != 0
		|| pwd == NULL || pwd->pw_name == NULL || pwd->pw_dir == NULL)
		return false;
	return strcmp(pwd->pw_name, "guest") == 0
		&& strcmp(pwd->pw_dir, "/boot/home/users/guest") == 0;
}
