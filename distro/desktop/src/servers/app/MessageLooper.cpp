/*
 * Copyright 2005-2016, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include "MessageLooper.h"

#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <Autolock.h>

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <FindDirectory.h>
#include <LinkRing.h>
#include <ServerProtocol.h>
#include <driver_settings.h>
#include <safemode_defs.h>
#include <syscalls.h>
#include <pthread.h>
#include <strings.h>
#include <sys/stat.h>
#include <syslog.h>
#if __has_include(<r5_os_version.h>)
#	include <r5_os_version.h>
#endif
// SPDX-SnippetEnd


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// Same value as kLinkCode in src/kits/app/link_message.h, which is private
// to libbe.
static const int32 kR5LinkCode = '_PTL';


SenderLinkReceiver::SenderLinkReceiver(port_id port)
	:
	BPrivate::LinkReceiver(port),
	fTrackSender(false),
	fNewPortMessage(false),
	fSenderUid((uid_t)-1),
	fSenderTeam(-1)
{
}


bool
SenderLinkReceiver::TakeNewPortMessage()
{
	bool isNew = fNewPortMessage;
	fNewPortMessage = false;
	return isNew;
}


/*!	LinkReceiver::GetNextMessage() calls this only when the receive buffer is
	empty. It reads the info of the port message at the head of the queue,
	then that same message (this thread is the only reader), so the recorded
	sender is the sender of every link message in the new buffer.
*/
status_t
SenderLinkReceiver::ReadFromPort(bigtime_t timeout)
{
	if (!fTrackSender)
		return BPrivate::LinkReceiver::ReadFromPort(timeout);

	ResetBuffer();
	fNewPortMessage = false;
	fSenderUid = (uid_t)-1;
	fSenderTeam = -1;

	while (true) {
		port_message_info info;
		status_t status;
		do {
			if (timeout == B_INFINITE_TIMEOUT) {
				status = get_port_message_info_etc(fReceivePort, &info, 0, 0);
			} else {
				status = get_port_message_info_etc(fReceivePort, &info,
					B_RELATIVE_TIMEOUT, timeout);
			}
		} while (status == B_INTERRUPTED);
		if (status != B_OK)
			return status;

		status = AdjustReplyBuffer(timeout);
		if (status != B_OK)
			return status;

		int32 code;
		ssize_t bytesRead;
		do {
			bytesRead = read_port_etc(fReceivePort, &code, fRecvBuffer,
				fRecvBufferSize, B_RELATIVE_TIMEOUT, 0);
		} while (bytesRead == B_INTERRUPTED);
		if (bytesRead == B_WOULD_BLOCK) {
			// Not expected with one reader: wait for the next message.
			continue;
		}
		if (bytesRead < B_OK)
			return (status_t)bytesRead;

		if (code != kR5LinkCode) {
			// Ignore it, as LinkReceiver does, and take the next message
			// with its own sender.
			continue;
		}

		fDataSize = bytesRead;
		fSenderUid = info.sender;
		fSenderTeam = info.sender_team;
		fNewPortMessage = true;
		return B_OK;
	}
}


SenderTrackingLink::SenderTrackingLink()
	:
	BPrivate::PortLink()
{
	// Replace the plain receiver that PortLink created. ~PortLink() deletes
	// fReceiver through its virtual destructor.
	fTrackingReceiver = new SenderLinkReceiver(fReceiver->Port());
	delete fReceiver;
	fReceiver = fTrackingReceiver;
}
// SPDX-SnippetEnd


MessageLooper::MessageLooper(const char* name)
	:
	BLocker(name),
	fName(strdup(name)),
	fThread(-1),
	fQuitting(false),
	fDeathSemaphore(-1)
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	,
	fLastSenderUid((uid_t)-1),
	fLastSenderTeam(-1),
	fLastSenderFirst(false)
// SPDX-SnippetEnd
{
}


MessageLooper::~MessageLooper()
{
	free((void*)fName);
}


status_t
MessageLooper::Run()
{
	BAutolock locker(this);

	fQuitting = false;

	char name[B_OS_NAME_LENGTH];
	_GetLooperName(name, sizeof(name));

	// Spawn our message-monitoring thread
	fThread = spawn_thread(_message_thread, name, B_DISPLAY_PRIORITY, this);
	if (fThread < B_OK) {
		fQuitting = true;
		return fThread;
	}

	if (resume_thread(fThread) != B_OK) {
		fQuitting = true;
		kill_thread(fThread);
		fThread = -1;
		return B_BAD_THREAD_ID;
	}

	return B_OK;
}


void
MessageLooper::Quit()
{
	fQuitting = true;
	_PrepareQuit();

	if (fThread < B_OK) {
		// thread has not been started yet
		delete this;
		return;
	}

	if (fThread == find_thread(NULL)) {
		// called from our message looper
		delete this;
		exit_thread(0);
	} else {
		// called from a different thread
		PostMessage(kMsgQuitLooper);
	}
}


/*!
	\brief Send a message to the looper without any attachments
	\param code ID code of the message to post
*/
status_t
MessageLooper::PostMessage(int32 code, bigtime_t timeout)
{
	BPrivate::LinkSender link(MessagePort());
	link.StartMessage(code);
	return link.Flush(timeout);
}


/*static*/
status_t
MessageLooper::WaitForQuit(sem_id semaphore, bigtime_t timeout)
{
	status_t status;
	do {
		status = acquire_sem_etc(semaphore, 1, B_RELATIVE_TIMEOUT, timeout);
	} while (status == B_INTERRUPTED);

	if (status == B_TIMED_OUT)
		return status;

	return B_OK;
}


void
MessageLooper::_PrepareQuit()
{
	// to be implemented by subclasses
}


void
MessageLooper::_GetLooperName(char* name, size_t length)
{
	if (fName != NULL)
		strlcpy(name, fName, length);
	else
		strlcpy(name, "unnamed looper", length);
}


void
MessageLooper::_DispatchMessage(int32 code, BPrivate::LinkReceiver &link)
{
}


void
MessageLooper::_MessageLooper()
{
	BPrivate::LinkReceiver& receiver = fLink.Receiver();
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// This thread is the only reader of the port. The receiver records the
	// kernel-verified sender each time it reads a new port message, so the
	// sender always belongs to the link message being handled.
	SenderLinkReceiver& tracking = fLink.TrackingReceiver();
	tracking.SetTrackSender(true);
// SPDX-SnippetEnd

	while (true) {
		int32 code;
		status_t status = receiver.GetNextMessage(code);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		// GetNextMessage() blocks until a message arrives; an idle port does
		// not end the loop.
		fLastSenderFirst = tracking.TakeNewPortMessage();
		fLastSenderUid = tracking.SenderUid();
		fLastSenderTeam = tracking.SenderTeam();
// SPDX-SnippetEnd
		if (status < B_OK) {
			// that shouldn't happen, it's our port
			char name[256];
			_GetLooperName(name, 256);
			printf("MessageLooper \"%s\": Someone deleted our message port %"
				B_PRId32 ", %s!\n", name, receiver.Port(), strerror(status));
			break;
		}

		Lock();

		if (code == kMsgQuitLooper)
			Quit();
		else
			_DispatchMessage(code, receiver);

		Unlock();
	}
}


/*!
	\brief Message-dispatching loop starter
*/
/*static*/
int32
MessageLooper::_message_thread(void* _looper)
{
	MessageLooper* looper = (MessageLooper*)_looper;

	looper->_MessageLooper();
	return 0;
}



// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
//
// The shared-memory link (LinkRing.h, docs/design/APP-SERVER-RING.md).


static bool
r5_safe_mode()
{
	char buffer[B_FILE_NAME_LENGTH];
	size_t size = sizeof(buffer);
	if (_kern_get_safemode_option(B_SAFEMODE_SAFE_MODE, buffer, &size)
			!= B_OK)
		return false;
	return strncasecmp(buffer, "true", size) == 0
		|| strncasecmp(buffer, "yes", size) == 0
		|| strncasecmp(buffer, "on", size) == 0
		|| strncasecmp(buffer, "enabled", size) == 0;
}


/*!	The Performance page writes "app_server_ring on" or "off" into the
	administrator's kernel/drivers/performance (the directory of the
	radeon_hd settings). Without the line the ring is on in a -dev build.
	Safe mode always turns it off. Read again when the file changes, so a
	change reaches applications started after it.
*/
/*static*/ bool
MessageLooper::LinkRingEnabled()
{
	static pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
	static int32 sSafeMode = -1;
	static bool sRead = false;
	static bool sEnabled = true;
	static struct timespec sModified;

	pthread_mutex_lock(&sLock);
	if (sSafeMode < 0) {
		sSafeMode = r5_safe_mode() ? 1 : 0;
		if (sSafeMode != 0)
			syslog(LOG_INFO, "app_server: link ring off: safe mode");
	}
	if (sSafeMode != 0) {
		pthread_mutex_unlock(&sLock);
		return false;
	}

	bool enabled = true;
#ifdef R5_OS_VERSION
	// Through a volatile pointer: GCC folds the constant and warns.
	const char* volatile version = R5_OS_VERSION;
	enabled = strstr(version, "-dev") != NULL;
#endif
	char path[B_PATH_NAME_LENGTH];
	struct stat st;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, false, path,
			sizeof(path)) == B_OK
		&& strlcat(path, "/kernel/drivers/performance", sizeof(path))
			< sizeof(path)
		&& stat(path, &st) == 0) {
		if (sRead && st.st_mtim.tv_sec == sModified.tv_sec
			&& st.st_mtim.tv_nsec == sModified.tv_nsec) {
			enabled = sEnabled;
		} else {
			FILE* file = fopen(path, "r");
			if (file != NULL) {
				char line[128];
				while (fgets(line, sizeof(line), file) != NULL) {
					if (strncmp(line, "app_server_ring ", 16) != 0)
						continue;
					const char* value = line + 16;
					if (strncmp(value, "off", 3) == 0
						|| strncmp(value, "false", 5) == 0)
						enabled = false;
					else if (strncmp(value, "on", 2) == 0
						|| strncmp(value, "true", 4) == 0)
						enabled = true;
				}
				fclose(file);
			}
			if (!sRead || enabled != sEnabled) {
				syslog(LOG_INFO, "app_server: link ring %s for new links "
					"(performance settings)", enabled ? "on" : "off");
			}
			sRead = true;
			sModified = st.st_mtim;
			sEnabled = enabled;
		}
	} else
		sRead = false;

	pthread_mutex_unlock(&sLock);
	return enabled;
}


void
MessageLooper::_SetUpLinkRing(BPrivate::LinkReceiver& link,
	team_id clientTeam)
{
	int32 version = 0;
	link.Read<int32>(&version);

	status_t status = B_OK;
	if (version != BPrivate::kLinkRingVersion)
		status = B_BAD_VALUE;
	else if (fLink.Sender().Ring() != NULL)
		status = B_BUSY;
	else if (!LinkRingEnabled())
		status = B_NOT_ALLOWED;

	// The ring goes to the team the kernel names as owner of the client's
	// reply port, and only when that is the team of this link.
	team_id team = -1;
	if (status == B_OK) {
		port_info info;
		if (get_port_info(fLink.SenderPort(), &info) != B_OK
			|| info.team != clientTeam)
			status = B_NOT_ALLOWED;
		else
			team = info.team;
	}

	BPrivate::LinkRing* ring = NULL;
	area_id clientArea = -1;
	uint32 toServerSize = 0;
	uint32 toClientSize = 0;
	if (status == B_OK) {
		ring = BPrivate::LinkRing::CreateServer(team, &clientArea,
			&toServerSize, &toClientSize);
		if (ring == NULL)
			status = B_NO_MEMORY;
	}

	// The answer still goes through the port; the ring starts after it.
	fLink.StartMessage(status);
	if (status == B_OK) {
		fLink.Attach<area_id>(clientArea);
		fLink.Attach<uint32>(toServerSize);
		fLink.Attach<uint32>(toClientSize);
		fLink.Attach<team_id>(getpid());
	}
	fLink.Flush();

	// ServerApp and ServerWindow run their own loops and do not track the
	// sender (SenderLinkReceiver); ring batches carry no port sender.
	if (ring != NULL) {
		BPrivate::link_ring_attach(fLink, ring);
		ring->ReleaseReference();
	}
}
// SPDX-SnippetEnd
