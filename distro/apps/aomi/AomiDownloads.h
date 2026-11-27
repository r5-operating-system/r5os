/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Downloads: the files Aomi saves (a link to a file, "Save page as…",
 * --download) and the Downloads window that lists them. One list per
 * process. Every change is posted to be_app as AOMI_DOWNLOAD_ADDED,
 * _PROGRESS, _FINISHED or _FAILED (WebContentView.h), which AomiApp hands
 * to every window.
 */
#ifndef AOMI_DOWNLOADS_H
#define AOMI_DOWNLOADS_H


#include <String.h>
#include <Window.h>

#include <vector>


enum {
	AOMI_DOWNLOAD_RUNNING		= 0,
	AOMI_DOWNLOAD_DONE			= 1,
	AOMI_DOWNLOAD_FAILED_STATE	= 2,
	AOMI_DOWNLOAD_CANCELLED		= 3
};


struct AomiDownload {
	int32		id;
	BString		name;
	BString		path;
	// The address without a sign-in, for the window and the log.
	BString		shown;
	int64		current;
	int64		expected;
	int32		state;
	BString		error;
};


// Saves url in destDir (the Desktop when empty) under the name from the
// address, or the one the server gives, never over an existing file.
// shown: the address to show and log; NULL shows url.
status_t						aomi_start_download(const char* url,
									const char* destDir,
									const char* shown = NULL);
// Saves url as path (the user chose it in a save panel).
status_t						aomi_start_download_to(const char* url,
									const char* path);
status_t						aomi_download_cancel(int32 id);
status_t						aomi_download_retry(int32 id);
void							aomi_download_clear_finished();
void							aomi_download_list(
									std::vector<AomiDownload>* list);
bool							aomi_download_get(int32 id,
									AomiDownload* download);
int32							aomi_download_count_running();
// Stops every download that runs (the program quits).
void							aomi_download_stop_all();


class AomiDownloadsWindow : public BWindow {
public:
								AomiDownloadsWindow();
	virtual						~AomiDownloadsWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

	// The one Downloads window of this process, made on first use.
	static	void				ShowWindow(const char* folder);

private:
			void				_Rebuild();
			void				_Update(int32 id);

			class BGroupView*	fRows;
			class BView*		fEmpty;
			class BButton*		fClear;
			BString				fFolder;
};


#endif
