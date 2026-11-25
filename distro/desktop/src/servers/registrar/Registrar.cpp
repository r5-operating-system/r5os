/*
 * Copyright 2001-2015, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Ingo Weinhold, ingo_weinhold@gmx.de
 */

#include "Registrar.h"

#include <stdio.h>
#include <string.h>

#include <exception>

#include <Application.h>
#include <Clipboard.h>
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <LaunchRoster.h>
#include <MessagePrivate.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
// SPDX-SnippetEnd
#include <Message.h>
#include <MessengerPrivate.h>
#include <OS.h>
#include <RegistrarDefs.h>
#include <RosterPrivate.h>
#include <system_info.h>

#include "AuthenticationManager.h"
#include "ClipboardHandler.h"
#include "Debug.h"
#include "EventQueue.h"
#include "MessageDeliverer.h"
#include "MessageEvent.h"
#include "MessageRunnerManager.h"
#include "MessagingService.h"
#include "MIMEManager.h"
#include "PackageWatchingManager.h"
#include "ShutdownProcess.h"
#include "TRoster.h"


/*!
	\class Registrar
	\brief The application class of the registrar.

	Glues the registrar services together and dispatches the roster messages.
*/

using std::nothrow;
using namespace BPrivate;

//! Name of the event queue.
static const char *kEventQueueName = "timer_thread";


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
static port_id
create_inner_registrar_port()
{
	return create_port(B_LOOPER_PORT_DEFAULT_CAPACITY, "registrar_inner");
}
// SPDX-SnippetEnd


/*!	\brief Creates the registrar application class.
	\param error Passed to the BServer constructor for returning an
		   error code.
*/
Registrar::Registrar(status_t* _error)
	:
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	BServer(B_REGISTRAR_SIGNATURE, "registrar_inner",
		create_inner_registrar_port(), false, _error),
// SPDX-SnippetEnd
	fRoster(NULL),
	fClipboardHandler(NULL),
	fMIMEManager(NULL),
	fEventQueue(NULL),
	fMessageRunnerManager(NULL),
	fShutdownProcess(NULL),
	fAuthenticationManager(NULL),
	fPackageWatchingManager(NULL),
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	fPublicPort(-1),
	fPortThread(-1)
// SPDX-SnippetEnd
{
	FUNCTION_START();

	set_thread_priority(find_thread(NULL), B_NORMAL_PRIORITY + 1);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	{
		bool seeded = false;
		int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
		if (fd >= 0) {
			ssize_t got = read(fd, fProxySecret, sizeof(fProxySecret));
			close(fd);
			seeded = (got == (ssize_t)sizeof(fProxySecret));
		}
		if (!seeded) {
			bigtime_t t = system_time();
			uint64 mix = (uint64)t ^ ((uint64)find_thread(NULL) << 32)
				^ (uint64)(uintptr_t)this;
			for (size_t i = 0; i < sizeof(fProxySecret); i++) {
				mix ^= mix >> 12;
				mix ^= mix << 25;
				mix ^= mix >> 27;
				fProxySecret[i] = (uint8)((mix * 0x2545F4914F6CDD1DULL) >> 56);
			}
		}
	}
	fPublicPort = BLaunchRoster().GetPort(B_REGISTRAR_SIGNATURE, NULL);
	if (fPublicPort < 0) {
		fPublicPort = create_port(B_LOOPER_PORT_DEFAULT_CAPACITY,
			B_REGISTRAR_PORT_NAME);
	}
	if (fPublicPort >= 0) {
		fPortThread = spawn_thread(&_PortProxyThread, "registrar port proxy",
			B_NORMAL_PRIORITY + 1, this);
		if (fPortThread >= 0)
			resume_thread(fPortThread);
	}
// SPDX-SnippetEnd
}


/*!	\brief Frees all resources associated with the registrar.

	All registrar services, that haven't been shut down earlier, are
	terminated.
*/
Registrar::~Registrar()
{
	FUNCTION_START();
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	if (fPublicPort >= 0) {
		delete_port(fPublicPort);
		fPublicPort = -1;
	}
	if (fPortThread >= 0) {
		status_t threadStatus;
		wait_for_thread(fPortThread, &threadStatus);
		fPortThread = -1;
	}
// SPDX-SnippetEnd
	Lock();
	fEventQueue->Die();
	delete fAuthenticationManager;
	delete fPackageWatchingManager;
	delete fMessageRunnerManager;
	delete fEventQueue;
	fMIMEManager->Lock();
	fMIMEManager->Quit();
	RemoveHandler(fClipboardHandler);
	delete fClipboardHandler;
	delete fRoster;
	// Invalidate the global be_roster, so that the BApplication destructor
	// won't dead-lock when sending a message to itself.
	BRoster::Private().SetTo(BMessenger(), BMessenger());
	FUNCTION_END();
}


/*!	\brief Overrides the super class version to dispatch roster specific
		   messages.
	\param message The message to be handled
*/
void
Registrar::MessageReceived(BMessage *message)
{
	try {
		_MessageReceived(message);
	} catch (std::exception& exception) {
		char buffer[1024];
		snprintf(buffer, sizeof(buffer),
			"Registrar::MessageReceived() caught exception: %s",
			exception.what());
		debugger(buffer);
	} catch (...) {
		debugger("Registrar::MessageReceived() caught unknown exception");
	}
}


/*!	\brief Overrides the super class version to initialize the registrar
		   services.
*/
void
Registrar::ReadyToRun()
{
	FUNCTION_START();

	// create message deliverer
	status_t error = MessageDeliverer::CreateDefault();
	if (error != B_OK) {
		FATAL("Registrar::ReadyToRun(): Failed to create the message "
			"deliverer: %s\n", strerror(error));
	}

	// create event queue
	fEventQueue = new EventQueue(kEventQueueName);

	// create authentication manager
	fAuthenticationManager = new AuthenticationManager;
	fAuthenticationManager->Init();

	// create roster
	fRoster = new TRoster;
	fRoster->Init();

	// create clipboard handler
	fClipboardHandler = new ClipboardHandler;
	AddHandler(fClipboardHandler);

	// create MIME manager
	fMIMEManager = new MIMEManager;
	fMIMEManager->Run();

	// create message runner manager
	fMessageRunnerManager = new MessageRunnerManager(fEventQueue);

	// init the global be_roster
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// Route registrar's own internal be_roster calls through fPublicPort so
	// they receive a verified uid 0 stamp from the proxy thread.
	BMessenger rosterMessenger(be_app_messenger);
	if (fPublicPort >= 0) {
		BMessenger::Private mp(rosterMessenger);
		mp.SetTo(be_app->Team(), fPublicPort, mp.Token());
	}
	BRoster::Private().SetTo(rosterMessenger, BMessenger(NULL, fMIMEManager));
// SPDX-SnippetEnd

	// create the messaging service
	error = MessagingService::CreateDefault();
	if (error != B_OK) {
		ERROR("Registrar::ReadyToRun(): Failed to init messaging service "
			"(that's by design when running under R5): %s\n", strerror(error));
	}

	// create the package watching manager
	fPackageWatchingManager = new PackageWatchingManager;

	// Sanity check roster after team deletion
	BMessenger target(this);
	BMessenger::Private messengerPrivate(target);

	port_id port = messengerPrivate.Port();
	int32 token = messengerPrivate.Token();
	__start_watching_system(-1, B_WATCH_SYSTEM_TEAM_DELETION, port, token);
	fRoster->CheckSanity();
		// Clean up any teams that exited before we started watching

	FUNCTION_END();
}


/*!	\brief Overrides the super class version to avoid termination of the
		   registrar until the system shutdown.
*/
bool
Registrar::QuitRequested()
{
	FUNCTION_START();
	// The final registrar must not quit. At least not that easily. ;-)
	return BApplication::QuitRequested();
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
/*static*/ bool
Registrar::VerifiedSenderUID(const BMessage* message, uid_t* _uid)
{
	Registrar* app = Registrar::App();
	if (app == NULL || message == NULL || _uid == NULL)
		return false;

	const void* secret = NULL;
	ssize_t secretSize = 0;
	if (message->FindData("_r5_proxy_secret", B_RAW_TYPE, &secret, &secretSize)
			!= B_OK
		|| secret == NULL
		|| secretSize != (ssize_t)sizeof(app->fProxySecret)) {
		return false;
	}
	const uint8* got = (const uint8*)secret;
	uint8 diff = 0;
	for (size_t i = 0; i < sizeof(app->fProxySecret); i++)
		diff |= got[i] ^ app->fProxySecret[i];
	if (diff != 0)
		return false;

	int32 uid = -1;
	if (message->FindInt32("_r5_sender_uid", &uid) != B_OK || uid < 0)
		return false;
	*_uid = (uid_t)uid;
	return true;
}


/*static*/ status_t
Registrar::_PortProxyThread(void* data)
{
	((Registrar*)data)->_PortProxyLoop();
	return B_OK;
}


void
Registrar::_PortProxyLoop()
{
	port_id innerPort = BMessenger::Private(be_app_messenger).Port();
	team_id selfTeam = be_app->Team();
	while (true) {
		port_message_info info;
		status_t status;
		do {
			status = get_port_message_info_etc(fPublicPort, &info, 0, 0);
		} while (status == B_INTERRUPTED);
		if (status != B_OK)
			break;

		char* buffer = info.size > 0 ? (char*)malloc(info.size) : NULL;
		if (info.size > 0 && buffer == NULL) {
			// Drain the message so we do not spin on it, and unblock any
			// synchronous sender with B_NO_REPLY when possible.
			char discard[256];
			int32 dropCode = 0;
			ssize_t dropped = read_port_etc(fPublicPort, &dropCode, discard,
				sizeof(discard), B_RELATIVE_TIMEOUT, 0);
			if (dropped > 0 && dropped <= (ssize_t)sizeof(discard)) {
				BMessage droppedMsg;
				if (droppedMsg.Unflatten(discard) == B_OK
					&& droppedMsg.IsSourceWaiting()) {
					droppedMsg.SendReply(B_NO_REPLY);
				}
			}
			continue;
		}

		int32 code = 0;
		ssize_t bytesRead = read_port_etc(fPublicPort, &code, buffer,
			info.size, B_RELATIVE_TIMEOUT, 0);
		if (bytesRead < 0) {
			free(buffer);
			if (bytesRead == B_BAD_PORT_ID)
				break;
			continue;
		}

		if (bytesRead > 0 && buffer != NULL) {
			BMessage msg;
			if (msg.Unflatten(buffer) == B_OK) {
				free(buffer);
				buffer = NULL;

				msg.RemoveName("_r5_sender_uid");
				msg.RemoveName("_r5_sender_team");
				msg.RemoveName("_r5_proxy_secret");
				status_t stampErr = msg.AddInt32("_r5_sender_uid",
					(int32)info.sender);
				if (stampErr == B_OK) {
					stampErr = msg.AddInt32("_r5_sender_team",
						(int32)info.sender_team);
				}
				if (stampErr == B_OK) {
					stampErr = msg.AddData("_r5_proxy_secret", B_RAW_TYPE,
						fProxySecret, sizeof(fProxySecret));
				}

				if (stampErr == B_OK) {
					// Forward through BMessage::Private::SendMessage so large
					// messages (> 40 KB / > 256 KB) use direct enqueue or
					// MESSAGE_FLAG_PASS_BY_AREA and keep reply metadata intact.
					BMessage::Private msgPriv(msg);
					bool replyRequired = msg.IsSourceWaiting();
					BMessenger replyTo;
					status_t sendErr = msgPriv.SendMessage(innerPort, selfTeam,
						msgPriv.GetTarget(), B_INFINITE_TIMEOUT, replyRequired,
						replyTo);
					if (sendErr != B_OK && msg.IsSourceWaiting())
						msg.SendReply(B_NO_REPLY);
				} else if (msg.IsSourceWaiting()) {
					msg.SendReply(B_NO_REPLY);
				}
				continue;
			}
		}

		status_t writeErr;
		do {
			writeErr = write_port_etc(innerPort, code, buffer, bytesRead,
				B_RELATIVE_TIMEOUT, 5000000);
		} while (writeErr == B_INTERRUPTED);
		(void)writeErr;
		free(buffer);
	}
}
// SPDX-SnippetEnd


/*!	\brief Returns the registrar's event queue.
	\return The registrar's event queue.
*/
EventQueue*
Registrar::GetEventQueue() const
{
	return fEventQueue;
}


/*!	\brief Returns the Registrar application object.
	\return The Registrar application object.
*/
Registrar*
Registrar::App()
{
	return dynamic_cast<Registrar*>(be_app);
}


void
Registrar::_MessageReceived(BMessage *message)
{
	switch (message->what) {
		// general requests
		case B_REG_GET_MIME_MESSENGER:
		{
			PRINT("B_REG_GET_MIME_MESSENGER\n");
			BMessenger messenger(NULL, fMIMEManager);
			BMessage reply(B_REG_SUCCESS);
			reply.AddMessenger("messenger", messenger);
			message->SendReply(&reply);
			break;
		}

		case B_REG_GET_CLIPBOARD_MESSENGER:
		{
			PRINT("B_REG_GET_CLIPBOARD_MESSENGER\n");
			BMessenger messenger(fClipboardHandler);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			if (fPublicPort >= 0) {
				BMessenger::Private mp(messenger);
				mp.SetTo(be_app->Team(), fPublicPort, mp.Token());
			}
// SPDX-SnippetEnd
			BMessage reply(B_REG_SUCCESS);
			reply.AddMessenger("messenger", messenger);
			message->SendReply(&reply);
			break;
		}

		// shutdown process
		case B_REG_SHUT_DOWN:
		{
			PRINT("B_REG_SHUT_DOWN\n");

			_HandleShutDown(message);
			break;
		}
		case B_REG_IS_SHUT_DOWN_IN_PROGRESS:
		{
			PRINT("B_REG_IS_SHUT_DOWN_IN_PROGRESS\n");

			_HandleIsShutDownInProgress(message);
			break;
		}
		case B_REG_TEAM_DEBUGGER_ALERT:
		{
			if (fShutdownProcess != NULL)
				fShutdownProcess->PostMessage(message);
			break;
		}

		// roster requests
		case B_REG_ADD_APP:
			fRoster->HandleAddApplication(message);
			break;
		case B_REG_COMPLETE_REGISTRATION:
			fRoster->HandleCompleteRegistration(message);
			break;
		case B_REG_IS_APP_REGISTERED:
			fRoster->HandleIsAppRegistered(message);
			break;
		case B_REG_REMOVE_PRE_REGISTERED_APP:
			fRoster->HandleRemovePreRegApp(message);
			break;
		case B_REG_REMOVE_APP:
			fRoster->HandleRemoveApp(message);
			break;
		case B_REG_SET_THREAD_AND_TEAM:
			fRoster->HandleSetThreadAndTeam(message);
			break;
		case B_REG_SET_SIGNATURE:
			fRoster->HandleSetSignature(message);
			break;
		case B_REG_GET_APP_INFO:
			fRoster->HandleGetAppInfo(message);
			break;
		case B_REG_GET_APP_LIST:
			fRoster->HandleGetAppList(message);
			break;
		case B_REG_UPDATE_ACTIVE_APP:
			fRoster->HandleUpdateActiveApp(message);
			break;
		case B_REG_BROADCAST:
			fRoster->HandleBroadcast(message);
			break;
		case B_REG_START_WATCHING:
			fRoster->HandleStartWatching(message);
			break;
		case B_REG_STOP_WATCHING:
			fRoster->HandleStopWatching(message);
			break;
		case B_REG_GET_RECENT_DOCUMENTS:
			fRoster->HandleGetRecentDocuments(message);
			break;
		case B_REG_GET_RECENT_FOLDERS:
			fRoster->HandleGetRecentFolders(message);
			break;
		case B_REG_GET_RECENT_APPS:
			fRoster->HandleGetRecentApps(message);
			break;
		case B_REG_ADD_TO_RECENT_DOCUMENTS:
			fRoster->HandleAddToRecentDocuments(message);
			break;
		case B_REG_ADD_TO_RECENT_FOLDERS:
			fRoster->HandleAddToRecentFolders(message);
			break;
		case B_REG_ADD_TO_RECENT_APPS:
			fRoster->HandleAddToRecentApps(message);
			break;
		case B_REG_CLEAR_RECENT_DOCUMENTS:
			fRoster->ClearRecentDocuments();
			break;
		case B_REG_CLEAR_RECENT_FOLDERS:
			fRoster->ClearRecentFolders();
			break;
		case B_REG_CLEAR_RECENT_APPS:
			fRoster->ClearRecentApps();
			break;
		case B_REG_LOAD_RECENT_LISTS:
			fRoster->HandleLoadRecentLists(message);
			break;
		case B_REG_SAVE_RECENT_LISTS:
			fRoster->HandleSaveRecentLists(message);
			break;

		// message runner requests
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		case B_REG_REGISTER_MESSAGE_RUNNER:
		{
			uid_t senderUid = 0;
			if (!Registrar::VerifiedSenderUID(message, &senderUid)) {
				BMessage reply(B_REG_ERROR);
				reply.AddInt32("error", B_PERMISSION_DENIED);
				message->SendReply(&reply);
				break;
			}
			BMessenger target;
			if (senderUid != 0
				&& message->FindMessenger("target", &target) == B_OK) {
				port_id targetPort = BMessenger::Private(target).Port();
				port_id innerPort = BMessenger::Private(be_app_messenger).Port();
				if (target.Team() == Team()
					|| targetPort == fPublicPort
					|| targetPort == innerPort) {
					BMessage reply(B_REG_ERROR);
					reply.AddInt32("error", B_PERMISSION_DENIED);
					message->SendReply(&reply);
					break;
				}
			}
			fMessageRunnerManager->HandleRegisterRunner(message);
			break;
		}
// SPDX-SnippetEnd
		case B_REG_UNREGISTER_MESSAGE_RUNNER:
			fMessageRunnerManager->HandleUnregisterRunner(message);
			break;
		case B_REG_SET_MESSAGE_RUNNER_PARAMS:
			fMessageRunnerManager->HandleSetRunnerParams(message);
			break;
		case B_REG_GET_MESSAGE_RUNNER_INFO:
			fMessageRunnerManager->HandleGetRunnerInfo(message);
			break;

		// package watching requests
		case B_REG_PACKAGE_START_WATCHING:
		case B_REG_PACKAGE_STOP_WATCHING:
			fPackageWatchingManager->HandleStartStopWatching(message);
			break;
		case B_PACKAGE_UPDATE:
			fPackageWatchingManager->NotifyWatchers(message);
			break;

		// internal messages
		case B_SYSTEM_OBJECT_UPDATE:
		{
			team_id team = (team_id)message->GetInt32("team", -1);
			if (team >= 0 && message->GetInt32("opcode", 0) == B_TEAM_DELETED)
				fRoster->HandleRemoveApp(message);
			break;
		}
		case B_REG_SHUTDOWN_FINISHED:
			if (fShutdownProcess) {
				fShutdownProcess->PostMessage(B_QUIT_REQUESTED,
					fShutdownProcess);
				fShutdownProcess = NULL;
			}
			break;

		case kMsgAppServerStarted:
		{
			fRoster->HandleAppServerStarted(message);

			// Don't pass this message on to our BApplication, as that may deadlock.
			break;
		}

		default:
			BApplication::MessageReceived(message);
			break;
	}
}


/*!	\brief Handle a shut down request message.
	\param request The request to be handled.
*/
void
Registrar::_HandleShutDown(BMessage *request)
{
	status_t error = B_OK;

	// check, whether we're already shutting down
	if (fShutdownProcess)
		error = B_SHUTTING_DOWN;

	bool needsReply = true;
	if (error == B_OK) {
		// create a ShutdownProcess
		fShutdownProcess = new(nothrow) ShutdownProcess(fRoster, fEventQueue);
		if (fShutdownProcess) {
			error = fShutdownProcess->Init(request);
			if (error == B_OK) {
				DetachCurrentMessage();
				fShutdownProcess->Run();
				needsReply = false;
			} else {
				delete fShutdownProcess;
				fShutdownProcess = NULL;
			}
		} else
			error = B_NO_MEMORY;
	}

	if (needsReply)
		ShutdownProcess::SendReply(request, error);
}


/*!	\brief Handle a is shut down in progress request message.
	\param request The request to be handled.
*/
void
Registrar::_HandleIsShutDownInProgress(BMessage *request)
{
	BMessage reply(B_REG_SUCCESS);
	reply.AddBool("in-progress", fShutdownProcess != NULL);
	request->SendReply(&reply);
}


//	#pragma mark -


/*!	\brief Creates and runs the registrar application.

	The main thread is renamed.

	\return 0.
*/
int
main()
{
	FUNCTION_START();

	// Create the global be_clipboard manually -- it will not work, since it
	// wants to talk to the registrar in its constructor, but it doesn't have
	// to and we would otherwise deadlock when initializing our GUI in the
	// app thread.
	be_clipboard = new BClipboard(NULL);

	// create and run the registrar application
	status_t error;
	Registrar *app = new Registrar(&error);
	if (error != B_OK) {
		fprintf(stderr, "REG: Failed to create the BApplication: %s\n",
			strerror(error));
		return 1;
	}

	// rename the main thread
	rename_thread(find_thread(NULL), "roster");

	PRINT("app->Run()...\n");

	try {
		app->Run();
	} catch (std::exception& exception) {
		char buffer[1024];
		snprintf(buffer, sizeof(buffer),
			"registrar main() caught exception: %s", exception.what());
		debugger(buffer);
	} catch (...) {
		debugger("registrar main() caught unknown exception");
	}

	PRINT("delete app...\n");
	delete app;

	FUNCTION_END();
	return 0;
}

