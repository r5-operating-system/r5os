/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * Relicensed from GPL-3.0-or-later to MIT by the rights holder in November 2026.
 * Lock for machine settings, part of libr5ui.so. See AdminLock.h.
 */


#include "AdminLock.h"

#include "AdminLockProtocol.h"

#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <Control.h>
#include <LayoutBuilder.h>
#include <MessageFilter.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <ScrollBar.h>
#include <String.h>
#include <StringFormat.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>
#include <Window.h>

#include <R5Admin.h>

#include <string.h>
#include <unistd.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "libr5ui: Admin lock"


using BAppearanceKit::BAdminLockBar;


static const uint32 kMsgPressUnlock = 'r5ap';
static const uint32 kMsgRefreshLock = 'r5ar';
static const uint32 kMsgUnlockOk = 'r5ao';
static const uint32 kMsgWatchLock = 'r5aw';


static bool
is_under(BView* view, BView* ancestor)
{
	for (BView* item = view; item != NULL; item = item->Parent()) {
		if (item == ancestor)
			return true;
	}
	return false;
}


static bool
tree_has(BView* view, BView* needle)
{
	if (view == NULL || needle == NULL)
		return false;
	if (view == needle)
		return true;
	int32 count = view->CountChildren();
	for (int32 i = 0; i < count; i++) {
		if (tree_has(view->ChildAt(i), needle))
			return true;
	}
	return false;
}


class LockInputFilter : public BMessageFilter {
public:
	LockInputFilter(AdminLockBar* bar)
		:
		BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE),
		fBar(bar)
	{
	}

	virtual filter_result
	Filter(BMessage* message, BHandler** target)
	{
		if (message == NULL || target == NULL)
			return B_DISPATCH_MESSAGE;
		uint32 what = message->what;
		if (what != B_MOUSE_DOWN && what != B_MOUSE_UP && what != B_KEY_DOWN
			&& what != B_KEY_UP)
			return B_DISPATCH_MESSAGE;
		if (fBar->AcceptsInput(*target))
			return B_DISPATCH_MESSAGE;
		return B_SKIP_MESSAGE;
	}

private:
	AdminLockBar*	fBar;
};


static status_t
ask_login(BMessage& request, BMessage& reply, BString& error)
{
	BMessenger login(kAdminLockLoginSignature);
	if (!login.IsValid()) {
		error = B_TRANSLATE("Sign in must be running.");
		return B_ERROR;
	}

	// Through Login's admin port: there the kernel names this team as the
	// sender, so an unlock grant is this program's and nobody else's.
	status_t status = r5_admin_send(login, &request, &reply);
	if (status != B_OK) {
		error = strerror(status);
		return status;
	}

	int32 code = B_OK;
	reply.FindInt32("error", &code);
	const char* text = NULL;
	reply.FindString("text", &text);
	if (code != B_OK) {
		error = text != NULL ? text : strerror(code);
		return code;
	}
	return B_OK;
}


bool
r5_admin_change_allowed()
{
	bool administrator = false;
	bool unlocked = false;
	if (r5_admin_query(administrator, unlocked) != B_OK)
		return false;
	return unlocked;
}


status_t
r5_admin_query(bool& administrator, bool& unlocked)
{
	administrator = false;
	unlocked = false;
	if (geteuid() == 0) {
		administrator = true;
		unlocked = true;
		return B_OK;
	}

	BMessage request(kMsgAdminQuery);
	BMessage reply;
	BString error;
	status_t status = ask_login(request, reply, error);
	if (status != B_OK)
		return status;

	reply.FindBool("admin", &administrator);
	reply.FindBool("unlocked", &unlocked);
	return B_OK;
}


status_t
r5_admin_unlock(const char* name, const char* password, BString& error)
{
	BMessage request(kMsgAdminUnlock);
	request.AddString("name", name != NULL ? name : "");
	request.AddString("password", password != NULL ? password : "");
	BMessage reply;
	return ask_login(request, reply, error);
}


status_t
r5_admin_relock()
{
	BMessage request(kMsgAdminRelock);
	BMessage reply;
	BString error;
	return ask_login(request, reply, error);
}


status_t
r5_admin_set_clock(unsigned long seconds)
{
	BMessage request(kMsgAdminSetClock);
	request.AddInt64("seconds", (int64)seconds);
	BMessage reply;
	BString error;
	return ask_login(request, reply, error);
}


status_t
r5_admin_set_timezone(int32 offset, const char* name)
{
	BMessage request(kMsgAdminSetTimezone);
	request.AddInt32("offset", offset);
	request.AddString("name", name != NULL ? name : "");
	BMessage reply;
	BString error;
	return ask_login(request, reply, error);
}


status_t
r5_admin_set_clock_gmt(bool gmt)
{
	BMessage request(kMsgAdminSetClockGmt);
	request.AddBool("gmt", gmt);
	BMessage reply;
	BString error;
	return ask_login(request, reply, error);
}


status_t
r5_admin_set_driver_blocked(const char* driver, bool blocked, BString& error)
{
	BMessage request(kMsgAdminSetDriverBlocked);
	request.AddString("driver", driver != NULL ? driver : "");
	request.AddBool("blocked", blocked);
	BMessage reply;
	return ask_login(request, reply, error);
}


status_t
r5_admin_set_driver_setting(const char* fileName, const char* const* keys,
	int32 keyCount, const char* lines, bool replaceFile, BString& error)
{
	BMessage request(kMsgAdminSetDriverSetting);
	request.AddString("file", fileName != NULL ? fileName : "");
	for (int32 i = 0; keys != NULL && i < keyCount; i++)
		request.AddString("key", keys[i] != NULL ? keys[i] : "");
	request.AddString("lines", lines != NULL ? lines : "");
	request.AddBool("replace", replaceFile);
	BMessage reply;
	return ask_login(request, reply, error);
}


class UnlockWindow : public BWindow {
public:
	UnlockWindow(BMessenger target)
		:
		BWindow(BRect(0, 0, 320, 140), B_TRANSLATE("Unlock"), B_TITLED_WINDOW,
			B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS
				| B_CLOSE_ON_ESCAPE),
		fTarget(target),
		fName(NULL),
		fPassword(NULL)
	{
		fName = new BTextControl("name", B_TRANSLATE("Administrator"), "user",
			NULL);
		fPassword = new BTextControl("password", B_TRANSLATE("Password"), "",
			NULL);
		fPassword->TextView()->HideTyping(true);

		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(new BStringView("hint",
				B_TRANSLATE("This changes the computer, not only your account.")))
			.Add(fName)
			.Add(fPassword)
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(new BButton("cancel", B_TRANSLATE("Cancel"),
					new BMessage(B_QUIT_REQUESTED)))
				.Add(new BButton("unlock", B_TRANSLATE("Unlock"),
					new BMessage(kMsgUnlockOk)))
			.End();

		fPassword->MakeFocus(true);
		CenterOnScreen();
	}

	virtual void
	MessageReceived(BMessage* message)
	{
		if (message->what != kMsgUnlockOk) {
			BWindow::MessageReceived(message);
			return;
		}

		BString error;
		status_t status = r5_admin_unlock(fName->Text(), fPassword->Text(),
			error);
		fPassword->SetText("");
		if (status != B_OK) {
			BAlert* alert = new BAlert("Unlock",
				error.Length() > 0 ? error.String() : B_TRANSLATE("Could not unlock."),
				B_TRANSLATE("OK"));
			alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
			alert->Go(NULL);
			return;
		}

		fTarget.SendMessage(kMsgRefreshLock);
		BMessage changed(kMsgAdminLockChanged);
		changed.AddBool("unlocked", true);
		r5_admin_mark_local(&changed);
		fTarget.SendMessage(&changed);
		PostMessage(B_QUIT_REQUESTED);
	}

private:
	BMessenger		fTarget;
	BTextControl*	fName;
	BTextControl*	fPassword;
};


void
r5_admin_prompt_unlock(BMessenger target)
{
	(new UnlockWindow(target))->Show();
}


BAdminLockBar::BAdminLockBar()
	:
	BGroupView(B_HORIZONTAL, B_USE_SMALL_SPACING),
	fLabel(NULL),
	fButton(NULL),
	fRunner(NULL),
	fWatch(NULL),
	fFilter(NULL),
	fAdmin(false),
	fUnlocked(false),
	fSyncing(false)
{
	fLabel = new BStringView("admin-lock", B_TRANSLATE("Locked"));
	fButton = new BButton("admin-unlock", B_TRANSLATE("Unlock..."),
		new BMessage(kMsgPressUnlock));
	fFilter = new LockInputFilter(this);
	BLayoutBuilder::Group<>(this)
		.Add(fLabel)
		.Add(fButton);
}


BAdminLockBar::~BAdminLockBar()
{
	delete fRunner;
	delete fWatch;
	if (Window() != NULL && fFilter != NULL)
		Window()->RemoveCommonFilter(fFilter);
	delete fFilter;
}


void
BAdminLockBar::AttachedToWindow()
{
	BGroupView::AttachedToWindow();
	fButton->SetTarget(this);
	if (fFilter != NULL)
		Window()->AddCommonFilter(fFilter);
	_Reload();
	fRunner = new BMessageRunner(BMessenger(this),
		new BMessage(kMsgRefreshLock), 10000000);
	fWatch = new BMessageRunner(BMessenger(this),
		new BMessage(kMsgWatchLock), 200000);
}


void
BAdminLockBar::DetachedFromWindow()
{
	if (fFilter != NULL)
		Window()->RemoveCommonFilter(fFilter);
	BGroupView::DetachedFromWindow();
}


bool
BAdminLockBar::AcceptsInput(BHandler* target) const
{
	if (fAdmin || fUnlocked)
		return true;

	BView* view = dynamic_cast<BView*>(target);
	if (view == NULL || is_under(view, const_cast<AdminLockBar*>(this)))
		return true;
	if (_IsExempt(view))
		return true;

	for (BView* item = view; item != NULL; item = item->Parent()) {
		if (dynamic_cast<BScrollBar*>(item) != NULL)
			return true;
		if (dynamic_cast<BControl*>(item) != NULL)
			return false;
	}
	return false;
}


void
BAdminLockBar::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgPressUnlock:
			if (fUnlocked)
				r5_admin_relock();
			else
				(new UnlockWindow(BMessenger(this)))->Show();
			_Reload();
			break;
		case kMsgRefreshLock:
			_Reload();
			break;
		case kMsgWatchLock:
			_SyncControls();
			break;
		default:
			BGroupView::MessageReceived(message);
			break;
	}
}


void
BAdminLockBar::SetExempt(BView* view, bool exempt)
{
	if (view == NULL)
		return;
	if (exempt) {
		if (!fExempt.HasItem(view))
			fExempt.AddItem(view);
	} else
		fExempt.RemoveItem(view);
	if (Window() != NULL)
		_SyncControls();
}



bool
BAdminLockBar::_IsExempt(BView* view) const
{
	for (BView* item = view; item != NULL; item = item->Parent()) {
		if (fExempt.HasItem(item))
			return true;
	}
	return false;
}


void
BAdminLockBar::_Reload()
{
	bool was = fUnlocked;
	bool wasAdmin = fAdmin;
	BMessage request(kMsgAdminQuery);
	BMessage reply;
	BString error;
	fAdmin = false;
	fUnlocked = false;
	if (ask_login(request, reply, error) == B_OK) {
		reply.FindBool("admin", &fAdmin);
		reply.FindBool("unlocked", &fUnlocked);
	}

	if (fAdmin) {
		fLabel->SetText(B_TRANSLATE("Administrator"));
		fButton->Hide();
	} else if (fUnlocked) {
		fLabel->SetText(B_TRANSLATE("Unlocked for a few minutes"));
		fButton->SetLabel(B_TRANSLATE("Lock"));
		fButton->Show();
	} else {
		fLabel->SetText(B_TRANSLATE("Locked"));
		fButton->SetLabel(B_TRANSLATE("Unlock..."));
		fButton->Show();
	}

	_SyncControls();
	if ((was != fUnlocked || wasAdmin != fAdmin) && Window() != NULL)
		Window()->PostMessage(kMsgAdminLockChanged);
}


void
BAdminLockBar::_Prune()
{
	BWindow* window = Window();
	for (int32 i = fForced.CountItems() - 1; i >= 0; i--) {
		BView* control = (BView*)fForced.ItemAt(i);
		bool live = false;
		if (window != NULL) {
			int32 count = window->CountChildren();
			for (int32 child = 0; child < count; child++) {
				if (tree_has(window->ChildAt(child), control)) {
					live = true;
					break;
				}
			}
		}
		if (!live)
			fForced.RemoveItem(i);
	}
}


void
BAdminLockBar::_DisableTree(BView* view)
{
	if (view == NULL || is_under(view, this) || _IsExempt(view))
		return;

	BControl* control = dynamic_cast<BControl*>(view);
	if (control != NULL && dynamic_cast<BScrollBar*>(control) == NULL
		&& control->IsEnabled() && !fForced.HasItem(control)) {
		control->SetEnabled(false);
		fForced.AddItem(control);
	}

	int32 count = view->CountChildren();
	for (int32 i = 0; i < count; i++)
		_DisableTree(view->ChildAt(i));
}


void
BAdminLockBar::_SyncControls()
{
	if (fSyncing || Window() == NULL)
		return;

	fSyncing = true;
	_Prune();
	if (fAdmin || fUnlocked) {
		for (int32 i = 0; i < fForced.CountItems(); i++) {
			BControl* control = (BControl*)fForced.ItemAt(i);
			control->SetEnabled(true);
		}
		fForced.MakeEmpty();
	} else {
		for (int32 i = fForced.CountItems() - 1; i >= 0; i--) {
			BControl* control = (BControl*)fForced.ItemAt(i);
			if (!_IsExempt(control))
				continue;
			control->SetEnabled(true);
			fForced.RemoveItem(i);
		}
		int32 count = Window()->CountChildren();
		for (int32 i = 0; i < count; i++)
			_DisableTree(Window()->ChildAt(i));
	}
	fSyncing = false;
}


namespace {

class ShutdownWarnWindow : public BWindow {
public:
	ShutdownWarnWindow(bool isReboot, const BMessage& sessions,
		BMessenger target, uint32 proceedWhat, bool requireUnlock)
		:
		BWindow(BRect(0, 0, 420, 220),
			isReboot ? B_TRANSLATE("Restart") : B_TRANSLATE("Shut down"),
			B_TITLED_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
			B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_NOT_CLOSABLE
				| B_AUTO_UPDATE_SIZE_LIMITS),
		fReboot(isReboot),
		fTarget(target),
		fProceedWhat(proceedWhat),
		fUnlocked(false),
		fUnlockButton(NULL),
		fProceedButton(NULL)
	{
		BString body;
		if (fReboot) {
			body = B_TRANSLATE("Other people are signed in. Restarting will "
				"close their apps and lose any unsaved work:\n\n");
		} else {
			body = B_TRANSLATE("Other people are signed in. Shutting down will "
				"close their apps and lose any unsaved work:\n\n");
		}
		int32 count = 0;
		type_code type;
		sessions.GetInfo("login", &type, &count);
		for (int32 i = 0; i < count; i++) {
			const char* disp = sessions.GetString("display", i, "");
			int32 apps = sessions.GetInt32("apps", i, 0);
			BString line;
			if (apps > 0) {
				BStringFormat fmt(B_TRANSLATE(
					"{0, plural, "
					"one{\xe2\x80\xa2 %name% (# open app)} "
					"other{\xe2\x80\xa2 %name% (# open apps)}}"));
				fmt.Format(line, apps);
				line.ReplaceFirst("%name%", disp);
			} else {
				line.SetToFormat("\xe2\x80\xa2 %s", disp);
			}
			body << line << "\n";
		}

		BTextView* text = new BTextView("body");
		text->SetText(body.String());
		text->MakeEditable(false);
		text->MakeSelectable(false);
		text->SetStylable(false);
		text->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		text->SetHighUIColor(B_PANEL_TEXT_COLOR);
		text->SetExplicitMinSize(BSize(360, 90));

		BMessage* unlockMsg = new BMessage('unlk');
		r5_admin_mark_local(unlockMsg);
		BMessage* proceedMsg = new BMessage('go__');
		r5_admin_mark_local(proceedMsg);

		BButton* cancel = new BButton("cancel", B_TRANSLATE("Cancel"),
			new BMessage(B_QUIT_REQUESTED));
		fUnlockButton = new BButton("unlock",
			B_TRANSLATE("Unlock" B_UTF8_ELLIPSIS), unlockMsg);
		fProceedButton = new BButton("proceed",
			fReboot ? B_TRANSLATE("Restart anyway")
				: B_TRANSLATE("Shut down anyway"),
			proceedMsg);

		if (!requireUnlock) {
			bool admin = false;
			bool unlocked = false;
			if (r5_admin_query(admin, unlocked) == B_OK && unlocked)
				fUnlocked = true;
		}
		fProceedButton->SetEnabled(fUnlocked);
		if (fUnlocked)
			fUnlockButton->Hide();

		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
			.SetInsets(B_USE_WINDOW_SPACING)
			.Add(text)
			.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
				.Add(fUnlockButton)
				.AddGlue()
				.Add(cancel)
				.Add(fProceedButton)
			.End();
		CenterOnScreen();
	}

	virtual void MessageReceived(BMessage* msg)
	{
		switch (msg->what) {
			case 'unlk':
			{
				if (!r5_admin_is_local(msg))
					break;
				// Modal like this warning: a plain titled window opened behind
				// it (B_MODAL_APP_WINDOW_FEEL stays above the app's normal
				// windows), so Unlock... showed nothing (QEMU, 25 Nov 2026).
				// A modal subset window does not work here: AddToSubset()
				// takes only normal windows, and with an empty subset the
				// Unlock window stayed hidden.
				UnlockWindow* unlock = new UnlockWindow(BMessenger(this));
				unlock->SetFeel(B_MODAL_APP_WINDOW_FEEL);
				unlock->Show();
				break;
			}
			case kMsgAdminLockChanged:
				if (!r5_admin_is_local(msg))
					break;
				if (msg->GetBool("unlocked", false)) {
					fUnlocked = true;
					fProceedButton->SetEnabled(true);
					if (!fUnlockButton->IsHidden())
						fUnlockButton->Hide();
				}
				break;
			case 'go__':
				if (!r5_admin_is_local(msg) || !fUnlocked)
					break;
				{
					BMessage go(fProceedWhat);
					go.AddBool("confirmed", true);
					r5_admin_mark_local(&go);
					fTarget.SendMessage(&go);
				}
				PostMessage(B_QUIT_REQUESTED);
				break;
			default:
				BWindow::MessageReceived(msg);
				break;
		}
	}

private:
	bool		fReboot;
	BMessenger	fTarget;
	uint32		fProceedWhat;
	bool		fUnlocked;
	BButton*	fUnlockButton;
	BButton*	fProceedButton;
};

}	// namespace


void
r5_show_shutdown_warning(bool reboot, const BMessage& sessions,
	BMessenger target, uint32 proceedWhat, bool requireUnlock)
{
	(new ShutdownWarnWindow(reboot, sessions, target, proceedWhat,
		requireUnlock))->Show();
}
