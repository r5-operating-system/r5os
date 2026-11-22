/*
 * Copyright 2013, Jérôme Duval, korli@users.berlios.de.
 * Copyright 2014, Rene Gollent, rene@gollent.com.
 * Copyright 2005, Nathan Whitehorn.
 *
 * Distributed under the terms of the MIT License.
 */

/*
 * R5OS modifications in this file are MIT, like the original work. They are
 * wrapped in SPDX-Snippet markers. The copyright and license notice above still
 * cover the original work. The original is air/OS's copy (commit
 * a4ddc48124a9) of Haiku's src/servers/power.
 */


#include "ac_monitor.h"
#include "lid_monitor.h"
#include "power_button_monitor.h"
#include "power_policy.h"
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// air/OS's thermal_trip_monitor.h: the thermal trips are step 3 of
// docs/POWER.md.
// SPDX-SnippetEnd

#include <Server.h>

#include <PathMonitor.h>

#include <map>

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <new>
#include <stdio.h>
#include <syslog.h>

#include <MessageRunner.h>
#include <Path.h>
#include <PowerRoster.h>
#include <R5Admin.h>

#include <PowerProtocol.h>


static const uint32 kMsgTick = 'r5pt';
// SPDX-SnippetEnd


class PowerManagementDaemon : public BServer {
public:
								PowerManagementDaemon();
	virtual 					~PowerManagementDaemon();

	virtual	void				MessageReceived(BMessage* message);

private:
			void				_EventLoop();
	static	status_t			_EventLooper(void *arg);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	static	void				_AdminRequest(BMessage* request,
									const r5_admin_sender& sender,
									BMessage* reply, void* cookie);
			void				_ScheduleTick();

			port_id				fAdminPort;
			BMessageRunner*		fTickRunner;
			bigtime_t			fTickInterval;
// SPDX-SnippetEnd

			thread_id			fEventThread;
			PowerPolicy			fPolicy;
			PowerMonitor*		fPowerMonitors[4];
			uint32				fMonitorCount;

			bool				fQuitRequested;
};


int
main(void)
{
	new PowerManagementDaemon();
	be_app->Run();
	delete be_app;
	return 0;
}


PowerManagementDaemon::PowerManagementDaemon()
	:
	BServer("application/x-vnd.Haiku-powermanagement", false, NULL),
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	fAdminPort(-1),
	fTickRunner(NULL),
	fTickInterval(0),
// SPDX-SnippetEnd
	fPolicy(this),
	fMonitorCount(0),
	fQuitRequested(false)
{
	PowerMonitor* powerButtonMonitor = new PowerButtonMonitor(&fPolicy);
	if (powerButtonMonitor->FDs().size() > 0)
		fPowerMonitors[fMonitorCount++] = powerButtonMonitor;
	else
		delete powerButtonMonitor;

	PowerMonitor* lidMonitor = new LidMonitor(&fPolicy);
	if (lidMonitor->FDs().size() > 0)
		fPowerMonitors[fMonitorCount++] = lidMonitor;
	else
		delete lidMonitor;

	PowerMonitor* acMonitor = new ACMonitor(&fPolicy);
	if (acMonitor->FDs().size() > 0)
		fPowerMonitors[fMonitorCount++] = acMonitor;
	else
		delete acMonitor;

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// The first profile, also without an AC monitor
	fPolicy.Start();

	// Someone may edit the file by hand; the Power page goes through the
	// admin port below.
	BPath settingsPath;
	if (PowerPolicy::SettingsPath(settingsPath) == B_OK) {
		BPrivate::BPathMonitor::StartWatching(settingsPath.Path(),
			B_WATCH_STAT | B_WATCH_FILES_ONLY, BMessenger(this));
	}

	// Changes to the machine settings are admin requests: the kernel names
	// the sender (libr5admin.so, docs/LIBR5.md "Admin requests").
	if (r5_admin_port_start("power admin", &_AdminRequest, this,
			&fAdminPort) != B_OK) {
		syslog(LOG_ERR, "power_daemon: no admin port\n");
		fAdminPort = -1;
	}

	_ScheduleTick();
// SPDX-SnippetEnd

	fEventThread = spawn_thread(_EventLooper, "_power_daemon_event_loop_",
		B_NORMAL_PRIORITY, this);
	if (fEventThread < B_OK)
		return;
	if (resume_thread(fEventThread) < B_OK) {
		kill_thread(fEventThread);
		fEventThread = -1;
		return;
	}
}


PowerManagementDaemon::~PowerManagementDaemon()
{
	fQuitRequested = true;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	delete fTickRunner;
	if (fAdminPort >= 0)
		r5_admin_port_stop(fAdminPort);
// SPDX-SnippetEnd
	for (uint32 i = 0; i < fMonitorCount; i++)
		delete fPowerMonitors[i];

	status_t status;
	wait_for_thread(fEventThread, &status);
}


void
PowerManagementDaemon::MessageReceived(BMessage* message)
{
	if (message->what == B_PATH_MONITOR) {
		fPolicy.LoadSettings();
		return;
	}
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	if (r5_admin_port_reply(message, fAdminPort))
		return;

	switch (message->what) {
		case kMsgTick:
			fPolicy.Tick();
			_ScheduleTick();
			return;

		case kMsgPowerGetSettings:
		{
			// Reading needs no rights.
			BMessage reply;
			fPolicy.GetSettings(reply);
			reply.what = kMsgPowerGetSettings;
			message->SendReply(&reply);
			return;
		}

		case kMsgPowerWatch:
		case kMsgPowerUnwatch:
		{
			BMessenger target;
			if (message->FindMessenger("target", &target) != B_OK)
				return;
			if (message->what == kMsgPowerWatch)
				fPolicy.AddWatcher(target);
			else
				fPolicy.RemoveWatcher(target);
			return;
		}
	}
// SPDX-SnippetEnd

	BServer::MessageReceived(message);
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
/*!	On the admin port's own thread. Only an administrator, uid 0, or a team
	with a grant changes the machine settings; the kernel named the sender.
	Never by BMessage::ReturnAddress() (docs/LIBR5.md, "Admin requests").
*/
/*static*/ void
PowerManagementDaemon::_AdminRequest(BMessage* request,
	const r5_admin_sender& sender, BMessage* reply, void* cookie)
{
	PowerManagementDaemon* self = (PowerManagementDaemon*)cookie;
	status_t status;
	if (request->what != kMsgPowerSetSettings)
		status = B_BAD_VALUE;
	else if (!r5_admin_sender_allowed(sender)) {
		syslog(LOG_NOTICE, "power_daemon: settings change by uid %d "
			"refused\n", (int)sender.uid);
		status = B_PERMISSION_DENIED;
	} else
		status = self->fPolicy.SetSettings(*request);

	reply->AddInt32("status", status);
}


void
PowerManagementDaemon::_ScheduleTick()
{
	bigtime_t interval = fPolicy.TickInterval();
	if (fTickRunner != NULL && interval == fTickInterval)
		return;

	delete fTickRunner;
	BMessage tick(kMsgTick);
	fTickRunner = new(std::nothrow) BMessageRunner(BMessenger(this), &tick,
		interval);
	fTickInterval = interval;
}
// SPDX-SnippetEnd


status_t
PowerManagementDaemon::_EventLooper(void* arg)
{
	PowerManagementDaemon* self = (PowerManagementDaemon*)arg;
	self->_EventLoop();
	return B_OK;
}


void
PowerManagementDaemon::_EventLoop()
{
	if (fMonitorCount == 0)
		return;

	std::map<int, PowerMonitor*> descriptorMap;

	size_t fdCount = 0;
	for (uint32 i = 0; i < fMonitorCount; i++)
		fdCount += fPowerMonitors[i]->FDs().size();

	object_wait_info info[fdCount];

	uint32 index = 0;
	for (uint32 i = 0; i < fMonitorCount; i++) {
		const std::set<int>& fds = fPowerMonitors[i]->FDs();
		for (std::set<int>::iterator it = fds.begin(); it != fds.end(); ++it) {
			info[index].object = *it;
			info[index].type = B_OBJECT_TYPE_FD;
			info[index].events = B_EVENT_READ;
			descriptorMap[*it] = fPowerMonitors[i];
			++index;
		}
	}
	while (!fQuitRequested) {
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		// air/OS waits with a timeout for its clamshell poll; R5's
		// timer is the tick on the looper.
		if (wait_for_objects(info, fdCount) < B_OK)
			continue;
// SPDX-SnippetEnd

		// handle events and reset events
		for (uint32 i = 0; i < fdCount; i++) {
			if (info[i].events & B_EVENT_READ)
				descriptorMap[info[i].object]->HandleEvent(info[i].object);
			else
				info[i].events = B_EVENT_READ;
		}
	}
}
