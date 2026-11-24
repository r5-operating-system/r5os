/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#include "OnlineStorageWindow.h"

#include "SelfTest.h"

#include <Application.h>

#include <stdio.h>
#include <string.h>


class OnlineStorageApp : public BApplication {
public:
								OnlineStorageApp()
									: BApplication("application/x-vnd.R5-OnlineStorage") {}
	virtual	void				ReadyToRun()
	{
		OnlineStorageWindow* window = new OnlineStorageWindow();
		window->Show();
	}
};


int
main(int argc, char** argv)
{
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--self-test") == 0)
			return RunOnlineStorageTests();
	}
	OnlineStorageApp app;
	app.Run();
	return 0;
}
