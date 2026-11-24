/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef ONLINE_STORAGE_WINDOW_H
#define ONLINE_STORAGE_WINDOW_H

#include <Window.h>


class BButton;
class BFilePanel;
class BMenuField;
class BStringView;
class BTextControl;


class OnlineStorageWindow : public BWindow {
public:
								OnlineStorageWindow();
	virtual						~OnlineStorageWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			void				_SignIn(const char* provider);
			void				_Sync(bool allowLargeDelete = false);
			void				_SetState(const char* text);
			bool				_StopRequested() const;

			BButton*			fOneDriveButton;
			BButton*			fDropboxButton;
			BMenuField*			fService;
			BTextControl*		fCloudFolder;
			BTextControl*		fLocalFolder;
			BStringView*		fState;
			BButton*			fSync;
			BFilePanel*			fPanel;
			thread_id			fWorker;
			volatile bool		fStop;

			friend status_t		sign_in_thread(void* data);
			friend status_t		sync_thread(void* data);
};


#endif
