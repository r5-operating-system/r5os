/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


#include "AomiWindow.h"

#include "AomiMessages.h"
#include "WebContentView.h"

#include <Application.h>
#include <String.h>

#include <Themes.h>

#include <stdio.h>
#include <string.h>
#include <vector>


static const char* kEngine = "WebKit";


class AomiApp : public BApplication {
public:
								AomiApp()
									:
									BApplication("application/x-vnd.R5-Aomi"),
									fPrivate(false),
									fDark(false),
									fMenu(false),
									fDownloads(false),
									fSettings(false)
								{
								}

	virtual	void				ArgvReceived(int32 argc, char** argv)
								{
									bool privat = false;
									bool menu = false;
									bool downloads = false;
									bool settings = false;
									std::vector<BString> urls;
									for (int32 i = 1; i < argc; i++) {
										if (strcmp(argv[i], "--private") == 0)
											privat = true;
										else if (strcmp(argv[i], "--dark") == 0)
											fDark = true;
										else if (strcmp(argv[i], "--menu") == 0)
											menu = true;
										else if (strcmp(argv[i], "--downloads") == 0)
											downloads = true;
										else if (strcmp(argv[i], "--settings") == 0)
											settings = true;
										else if (argv[i][0] != '-')
											urls.push_back(argv[i]);
									}
									if (fDark)
										_Dark();
									if (IsLaunching()) {
										fPrivate = privat;
										fMenu = menu;
										fDownloads = downloads;
										fSettings = settings;
										fUrls = urls;
										return;
									}
									AomiWindow* window = NULL;
									for (int32 i = 0; i < CountWindows(); i++) {
										window = dynamic_cast<AomiWindow*>(
											WindowAt(i));
										if (window != NULL)
											break;
									}
									if (window == NULL)
										return;
									if (!window->Lock())
										return;
									bool first = true;
									for (uint32 i = 0; i < urls.size(); i++) {
										window->OpenURL(urls[i].String(), true);
										first = false;
									}
									(void)first;
									if (menu)
										window->PostMessage(MSG_MENU);
									if (downloads)
										window->PostMessage(MSG_DOWNLOADS);
									if (settings)
										window->PostMessage(MSG_SETTINGS);
									if (privat)
										window->PostMessage(MSG_NEW_PRIVATE);
									window->Unlock();
								}

	virtual	void				ReadyToRun()
								{
									_Dark();
									aomi_engine_start(kEngine);
									AomiWindow* window = new AomiWindow(
										new BWebSession(fPrivate), fPrivate);
									window->Show();
									bool first = true;
									for (uint32 i = 0; i < fUrls.size(); i++) {
										window->OpenURL(fUrls[i].String(), !first);
										first = false;
									}
									if (fMenu)
										window->PostMessage(MSG_MENU);
									if (fDownloads)
										window->PostMessage(MSG_DOWNLOADS);
									if (fSettings)
										window->PostMessage(MSG_SETTINGS);
								}

	virtual	void				MessageReceived(BMessage* message)
								{
									switch (message->what) {
										case AOMI_DOWNLOAD_ADDED:
										case AOMI_DOWNLOAD_PROGRESS:
										case AOMI_DOWNLOAD_FINISHED:
										case AOMI_DOWNLOAD_FAILED:
											for (int32 i = 0; i < CountWindows(); i++) {
												AomiWindow* window
													= dynamic_cast<AomiWindow*>(
														WindowAt(i));
												if (window != NULL)
													window->PostMessage(message);
											}
											break;
										case MSG_NEW_WINDOW:
											(new AomiWindow(new BWebSession(false),
												false))->Show();
											break;
										case MSG_NEW_PRIVATE:
											(new AomiWindow(new BWebSession(true),
												true))->Show();
											break;
										default:
											BApplication::MessageReceived(message);
											break;
									}
								}

private:
			void				_Dark()
								{
									if (!fDark)
										return;
									BAppearanceKit::theme_info info;
									if (BAppearanceKit::get_theme(&info) == B_OK) {
										info.mode = BAppearanceKit::B_THEME_DARK;
										BAppearanceKit::set_theme(&info,
											BAppearanceKit::B_THEME_COLORS);
									}
								}

			bool				fPrivate;
			bool				fDark;
			bool				fMenu;
			bool				fDownloads;
			bool				fSettings;
			std::vector<BString> fUrls;
};


int
main()
{
	AomiApp app;
	app.Run();
	return 0;
}
