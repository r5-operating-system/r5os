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

