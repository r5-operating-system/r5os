/*
 * Copyright 2010-2017, Haiku, Inc. All Rights Reserved.
 * Copyright 2008-2009, Pier Luigi Fiorini. All Rights Reserved.
 * Copyright 2004-2008, Michael Davidson. All Rights Reserved.
 * Copyright 2004-2007, Mikael Eiman. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _NOTIFICATION_WINDOW_H
#define _NOTIFICATION_WINDOW_H

#include <cmath>
#include <vector>
#include <map>

#include <AppFileInfo.h>
#include <Path.h>
#include <String.h>
#include <Window.h>

#include "NotificationView.h"


class AppGroupView;
class AppUsage;

struct property_info;

typedef std::map<BString, AppGroupView*> appview_t;
typedef std::map<BString, AppUsage*> appfilter_t;

const uint32 kRemoveGroupView = 'RGVi';


class NotificationWindow : public BWindow {
public:
									NotificationWindow();
	virtual							~NotificationWindow();

	virtual	bool					QuitRequested();
	virtual	void					MessageReceived(BMessage*);
	virtual	void					WorkspaceActivated(int32, bool);
	virtual	void					FrameResized(float width, float height);
	virtual	void					ScreenChanged(BRect frame, color_space mode);

			int32					Timeout();
			float					Width();

			void					_ShowHide();
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			port_id					AdminPort() const { return fAdminPort; }
			bool					IsSessionHidden() const
										{ return fSessionHidden; }
// SPDX-SnippetEnd

private:
	friend class AppGroupView;

			void					SetPosition();
			void					_LoadSettings(bool startMonitor = false);
			void					_LoadAppFilters(BMessage& settings);
			void					_LoadGeneralSettings(BMessage& settings);
			void					_LoadDisplaySettings(BMessage& settings);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			void					_DeliverNotification(BMessage* message,
										bool sendReply);
			void					_SetSessionHidden(bool hidden);
	static	void					_AdminRequest(BMessage* request,
										const struct r5_admin_sender& sender,
										BMessage* reply, void* cookie);
// SPDX-SnippetEnd

			appview_t				fAppViews;
			appfilter_t				fAppFilters;

			float					fWidth;
			int32					fIconSize;
			int32					fTimeout;
			uint32					fPosition;
			bool					fShouldRun;
			BPath					fCachePath;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			port_id					fAdminPort;
			bool					fSessionHidden;
			std::vector<BMessage>	fQueuedNotifications;
// SPDX-SnippetEnd
};

extern property_info main_prop_list[];

#endif	// _NOTIFICATION_WINDOW_H
