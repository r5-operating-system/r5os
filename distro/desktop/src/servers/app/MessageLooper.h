/*
 * Copyright 2005-2016, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */
#ifndef MESSAGE_LOOPER_H
#define MESSAGE_LOOPER_H


#include <PortLink.h>
#include <Locker.h>
#include <OS.h>


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
/*!	Link receiver that records the kernel-verified sender of the port
	message it read last (get_port_message_info_etc), and whether the link
	message just returned is the first one in that port message. Sender
	tracking is off until SetTrackSender(true); then the receiving thread
	must be the only reader of the port.
*/
class SenderLinkReceiver : public BPrivate::LinkReceiver {
public:
								SenderLinkReceiver(port_id port = -1);

			void				SetTrackSender(bool track)
									{ fTrackSender = track; }
			uid_t				SenderUid() const { return fSenderUid; }
			team_id				SenderTeam() const { return fSenderTeam; }
			bool				TakeNewPortMessage();

protected:
	virtual	status_t			ReadFromPort(bigtime_t timeout);

private:
			bool				fTrackSender;
			bool				fNewPortMessage;
			uid_t				fSenderUid;
			team_id				fSenderTeam;
};


class SenderTrackingLink : public BPrivate::PortLink {
public:
								SenderTrackingLink();

			SenderLinkReceiver&	TrackingReceiver()
									{ return *fTrackingReceiver; }

private:
			SenderLinkReceiver*	fTrackingReceiver;
};
// SPDX-SnippetEnd


class MessageLooper : public BLocker {
public:
								MessageLooper(const char* name);
	virtual						~MessageLooper();

	virtual	status_t			Run();
	virtual	void				Quit();

			status_t			PostMessage(int32 code,
									bigtime_t timeout = B_INFINITE_TIMEOUT);

			thread_id			Thread() const { return fThread; }
			bool				IsQuitting() const { return fQuitting; }
			sem_id				DeathSemaphore() const
									{ return fDeathSemaphore; }

	virtual	port_id				MessagePort() const = 0;

	static	status_t			WaitForQuit(sem_id semaphore,
									bigtime_t timeout = B_INFINITE_TIMEOUT);

private:
	virtual	void				_PrepareQuit();
	virtual	void				_GetLooperName(char* name, size_t length);
	virtual	void				_DispatchMessage(int32 code,
									BPrivate::LinkReceiver& link);
	virtual	void				_MessageLooper();

protected:
	static	int32				_message_thread(void*_looper);

protected:
			const char*			fName;
			thread_id			fThread;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			SenderTrackingLink	fLink;
// SPDX-SnippetEnd
			bool				fQuitting;
			sem_id				fDeathSemaphore;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			// Set by the default _MessageLooper() for each link message:
			// the kernel-verified sender of the port message it came from,
			// and whether it is the first link message in that port message.
			uid_t				fLastSenderUid;
			team_id				fLastSenderTeam;
			bool				fLastSenderFirst;
// SPDX-SnippetEnd
};


static const int32 kMsgQuitLooper = 'quit';


#endif	/* MESSAGE_LOOPER_H */
