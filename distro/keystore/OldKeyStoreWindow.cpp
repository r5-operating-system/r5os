/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Dialog shown at Sign in after an administrator password reset:
 * "Do you still know your old password?"
 */


#include "OldKeyStoreWindow.h"

#include "KeyStoreServer.h"

#include <Button.h>
#include <Font.h>
#include <LayoutBuilder.h>
#include <R5Security.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>

#include <string.h>


static const uint32 kMsgUnlockOld = 'r5UO';
static const uint32 kMsgSkipOld   = 'r5SO';


OldKeyStoreWindow::OldKeyStoreWindow(KeyStoreServer* server)
	:
	BWindow(BRect(0, 0, 420, 180), "Unlock old passwords",
		B_TITLED_WINDOW,
		B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_CLOSE_ON_ESCAPE),
	fServer(server),
	fPassword(NULL),
	fErrorView(NULL),
	fSkipButton(NULL),
	fUnlockButton(NULL)
{
	BStringView* heading = new BStringView("heading",
		"Do you still know your old password?");
	BFont bold(be_bold_font);
	bold.SetSize(be_plain_font->Size() * 1.15f);
	heading->SetFont(&bold);

	BStringView* subtitle = new BStringView("subtitle",
		"Your saved passwords (online accounts and apps) are locked "
		"with your old password.");

	fPassword = new BTextControl("old_password", "Old password:", "", NULL);
	fPassword->TextView()->HideTyping(true);

	fErrorView = new BStringView("error", "");
	fErrorView->SetHighColor(make_color(192, 48, 48));

	fSkipButton = new BButton("skip", "Skip", new BMessage(kMsgSkipOld));
	fUnlockButton = new BButton("unlock", "Unlock",
		new BMessage(kMsgUnlockOld));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(heading)
		.Add(subtitle)
		.AddStrut(B_USE_SMALL_SPACING)
		.Add(fPassword)
		.Add(fErrorView)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.AddGlue()
			.Add(fSkipButton)
			.Add(fUnlockButton)
		.End();

	SetDefaultButton(fUnlockButton);
	fPassword->MakeFocus(true);
	CenterOnScreen();
}


OldKeyStoreWindow::~OldKeyStoreWindow()
{
	if (fPassword != NULL)
		fPassword->SetText("");
}


void
OldKeyStoreWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgUnlockOld:
			_TryUnlock();
			break;

		case kMsgSkipOld:
			// Keep .old files intact so the user can still unlock later.
			PostMessage(B_QUIT_REQUESTED);
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
OldKeyStoreWindow::_TryUnlock()
{
	if (fServer == NULL || fPassword == NULL)
		return;

	const char* text = fPassword->Text();
	if (text == NULL || text[0] == '\0') {
		fErrorView->SetText("Enter your old password, or click Skip.");
		return;
	}

	size_t len = strlen(text);
	BSecurityKit::BSecureBuffer pwBuf;
	if (pwBuf.Allocate(len + 1) != B_OK) {
		fErrorView->SetText("Not enough memory.");
		return;
	}
	memcpy(pwBuf.Data(), text, len);
	pwBuf.Data()[len] = '\0';

	status_t status = fServer->RecoverOldKeyStore(
		reinterpret_cast<const char*>(pwBuf.Data()));
	pwBuf.Clear();

	if (status == B_OK) {
		fPassword->SetText("");
		PostMessage(B_QUIT_REQUESTED);
		return;
	}

	fPassword->SetText("");
	fPassword->MakeFocus(true);
	if (status == B_NOT_ALLOWED)
		fErrorView->SetText("That password did not unlock your old key store.");
	else
		fErrorView->SetText("Could not open your old key store.");
}
