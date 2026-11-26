/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


#include "WebContentView.h"

#include <Application.h>
#include <GroupLayout.h>
#include <Window.h>


static const char* kEngine = "WebKit";


class AomiApp : public BApplication {
public:
								AomiApp()
									:
									BApplication("application/x-vnd.R5-Aomi")
								{
								}

	virtual	void				ReadyToRun()
								{
									aomi_engine_start(kEngine);
									BWebSession* session = new BWebSession(false);
									BWindow* window = new BWindow(
										BRect(80, 60, 1100, 760), "Aomi",
										B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS);
									window->SetLayout(new BGroupLayout(
										B_VERTICAL));
									BWebContentView* view
										= create_aomi_content_view(kEngine,
											"page", session);
									if (view != NULL) {
										view->SetListener(BMessenger(window));
										window->AddChild(view);
										view->LoadURL("about:blank");
									}
									window->Show();
								}
};


int
main()
{
	AomiApp app;
	app.Run();
	return 0;
}
