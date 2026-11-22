/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*
 * R5OS modifications in this file are MIT, like the original work. They are
 * wrapped in SPDX-Snippet markers. The copyright and license notice above still
 * cover the original work. The original is air/OS's power_policy.h (commit
 * a4ddc48124a9).
 */
#ifndef _POWER_POLICY_H
#define _POWER_POLICY_H


#include <Locker.h>
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <Message.h>
#include <Messenger.h>
#include <vector>
// SPDX-SnippetEnd
#include <SupportDefs.h>


class BPath;
class BServer;


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
/*!	What the power daemon does when the power source changes, the lid is
	closed, the power button is pressed or nobody used the computer for a
	while, from the machine's settings file (docs/POWER.md, section 3):
	"power" in B_SYSTEM_SETTINGS_DIRECTORY. Only this service writes it,
	after an admin request (libr5admin.so) from an administrator.

	air/OS's sleep, its deep sleep, its wired-network handling and its
	app_server display layout are not here: sleep is step 4 of
	docs/POWER.md, and R5's app_server has no display layout call.
	Dimming on battery is step 3.

	Every public call takes the policy's lock itself.
*/
class PowerPolicy {
public:
								PowerPolicy(BServer* server);

			void				Start();
				// after the monitors: the first profile when no AC
				// monitor told the power source
			void				LoadSettings();
			status_t			SetSettings(const BMessage& settings);
			void				GetSettings(BMessage& settings);

			void				SetOnAC(bool onAC);
			void				SetLidOpen(bool open);
			void				PowerButtonPressed();
			void				Tick();
				// from the service's timer: the display-off timeout, and
				// the power source once more (an event can be missed)
			bigtime_t			TickInterval();

			void				AddWatcher(const BMessenger& target);
			void				RemoveWatcher(const BMessenger& target);

	static	status_t			SettingsPath(BPath& path);

private:
			enum lid_action {
				kLidNothing,
				kLidDisplayOff,
				kLidSleep
			};

			void				_ApplyProfile();
			bool				_InitGUI();
			bool				_HasExternalDisplay();
			void				_SetDisplay(bool on);
			void				_Broadcast(int32 change, int32 profile = -1);
			bool				_SleepWorks();
			status_t			_Write(const BMessage& settings);

			BServer*			fServer;
			BLocker				fLock;
			BMessage			fSettings;
			bool				fOnAC;
			bool				fKnowSource;
			bool				fLidClosed;
			bool				fKnowLid;
			int32				fProfile;
				// the one last applied, -1 none yet
			bool				fDisplayOffByTimer;
			bool				fDisplayOffByLid;
			std::vector<BMessenger>	fWatchers;
};
// SPDX-SnippetEnd


#endif // _POWER_POLICY_H
