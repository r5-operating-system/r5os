/*
 * Copyright 2008, François Revol, <revol@free.fr>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "LoginWindow.h"

#include "LoginView.h"

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <Catalog.h>
// SPDX-SnippetEnd
#include <LayoutBuilder.h>


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Login: Sign in window"
// SPDX-SnippetEnd


LoginWindow::LoginWindow(BRect frame)
	:
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	BWindow(frame, B_TRANSLATE("Sign in"), B_TITLED_WINDOW_LOOK, B_MODAL_ALL_WINDOW_FEEL,
// SPDX-SnippetEnd

		B_NOT_CLOSABLE | B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE
			| B_AUTO_UPDATE_SIZE_LIMITS | B_ASYNCHRONOUS_CONTROLS,
		B_ALL_WORKSPACES)
{
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(new LoginView());

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	ResizeToPreferred();
// SPDX-SnippetEnd
	CenterOnScreen();
	SetPulseRate(1000000LL);
}


LoginWindow::~LoginWindow()
{
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
void
LoginWindow::ScreenChanged(BRect frame, color_space mode)
{
	BWindow::ScreenChanged(frame, mode);
	CenterOnScreen();
}


void
LoginWindow::DispatchMessage(BMessage* message, BHandler* target)
{
	uint32 what = message->what;
	BWindow::DispatchMessage(message, target);
	// Accounts step 5: BPasswordControl has no modification message, so the
	// first-start screen looks at its fields after every key, as "Add
	// account" does.
	if (what == B_KEY_DOWN || what == B_PASTE || what == B_CUT
		|| what == B_UNDO || what == B_INPUT_METHOD_EVENT) {
		LoginView* view = dynamic_cast<LoginView*>(FindView("LoginView"));
		if (view != NULL)
			view->FieldsChanged();
	}
}
// SPDX-SnippetEnd


void
LoginWindow::PrepareForSignIn()
{
	LoginView* view = dynamic_cast<LoginView*>(FindView("LoginView"));
	if (view != NULL)
		view->PrepareForSignIn();
}
