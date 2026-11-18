/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * One Wi-Fi password window. The status line changes in this window.
 */


#include <R5WiFi.h>

#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
#include <PopUpMenu.h>
#include <StringView.h>

#include <PasswordControl.h>
#include <WindowEffects.h>
#include <R5Icons.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "WiFiConnectWindow"


static const uint32 kConnect = 'wfcn';
static const uint32 kToggleOptions = 'wfop';
static const uint32 kSecurity = 'wfsc';

enum {
	kSecurityAutomatic = 0,
	kSecurityWpa2 = 1,
	kSecurityWpa3 = 2,
	kSecurityTransition = 3,
	kSecurityOpen = 4
};


class ConnectHeader : public BView {
public:
	ConnectHeader(const char* name, const char* subtitle, int32 bars)
		:
		BView("header", B_WILL_DRAW),
		fName(name),
		fSubtitle(subtitle),
		fBars(bars)
	{
		SetExplicitMinSize(BSize(360, 52));
		SetViewColor(B_TRANSPARENT_COLOR);
	}

	void Draw(BRect updateRect)
	{
		(void)updateRect;
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
		font_height height;
		be_bold_font->GetHeight(&height);
		float top = 8;
		BRect tile(8, top, 48, top + 40);
		int32 bars = fBars;
		if (bars < 0)
			bars = 0;
		if (bars > 4)
			bars = 4;
		SetHighUIColor(B_PANEL_TEXT_COLOR);
		r5_level_glyph_draw(this, R5_LEVEL_SIGNAL, tile.InsetByCopy(6, 8),
			bars, HighColor(), ui_color(B_PANEL_BACKGROUND_COLOR),
			R5_LEVEL_STYLE_NORMAL);
		SetFont(be_bold_font);
		SetHighUIColor(B_PANEL_TEXT_COLOR);
		DrawString(fName.String(), BPoint(58, top + height.ascent));
		SetFont(be_plain_font);
		rgb_color text = ui_color(B_PANEL_TEXT_COLOR);
		text.alpha = 180;
		SetHighColor(text);
		DrawString(fSubtitle.String(),
			BPoint(58, top + height.ascent + be_plain_font->Size() + 4));
	}

private:
	BString fName;
	BString fSubtitle;
	int32 fBars;
};


WiFiConnectWindow::WiFiConnectWindow(const BMessenger& controller,
	const char* name, const char* subtitle, uint32 authentication, int32 bars)
	:
	BWindow(BRect(0, 0, 460, 220), B_TRANSLATE("Connect to Wi-Fi"),
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE | B_NOT_RESIZABLE
			| B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE
			| B_ASYNCHRONOUS_CONTROLS),
	fController(controller),
	fClosedTarget(),
	fToken(0),
	fBars(bars),
	fName(name),
	fAuthentication(authentication),
	fBusy(false),
	fFinished(false),
	fOptionsOpen(false),
	fDone(-1),
	fResult(NULL)
{
	BAppearanceKit::set_window_glass(this, true);

	fPassword = new BAppearanceKit::BPasswordControl("password", NULL, "",
		NULL);
	fRemember = new BCheckBox("remember",
		B_TRANSLATE("Connect automatically"), NULL);
	fRemember->SetValue(B_CONTROL_ON);

	BPopUpMenu* security = new BPopUpMenu(B_TRANSLATE("Automatic"));
	const char* labels[] = {
		B_TRANSLATE("Automatic"),
		B_TRANSLATE("WPA2 Personal"),
		B_TRANSLATE("WPA3 Personal"),
		B_TRANSLATE("WPA2/WPA3"),
		B_TRANSLATE("Open")
	};
	for (int i = 0; i < 5; i++) {
		BMessage* message = new BMessage(kSecurity);
		message->AddInt32("choice", i);
		BMenuItem* item = new BMenuItem(labels[i], message);
		security->AddItem(item);
		if (i == 0)
			item->SetMarked(true);
	}
	fSecurity = new BMenuField("security", B_TRANSLATE("Security"), security);
	fSecurity->Hide();

	fStatus = new BStringView("status", "");
	fOther = new BButton("other", B_TRANSLATE("› Other options"),
		new BMessage(kToggleOptions));
	fConnect = new BButton("connect", B_TRANSLATE("Connect"),
		new BMessage(kConnect));
	BButton* cancel = new BButton("cancel", B_TRANSLATE("Cancel"),
		new BMessage(B_QUIT_REQUESTED));

	BStringView* passwordLabel = new BStringView("password label",
		B_TRANSLATE("Password"));
	passwordLabel->SetAlignment(B_ALIGN_RIGHT);
	passwordLabel->SetViewColor(B_TRANSPARENT_COLOR);

	ConnectHeader* header = new ConnectHeader(name,
		subtitle != NULL ? subtitle : "", bars);
	fStatus->SetViewColor(B_TRANSPARENT_COLOR);
	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(header)
		.AddGroup(B_HORIZONTAL)
			.Add(passwordLabel)
			.Add(fPassword)
		.End()
		.AddGroup(B_HORIZONTAL)
			.Add(new BStringView("pad", ""))
			.Add(fRemember)
		.End()
		.Add(fSecurity)
		.Add(fStatus)
		.AddGroup(B_HORIZONTAL)
			.Add(fOther)
			.AddGlue()
			.Add(cancel)
			.Add(fConnect)
		.End();
	SetDefaultButton(fConnect);
	fPassword->MakeFocus(true);
	fSecurity->Hide();
	BAppearanceKit::set_view_glass(header, true);
	BAppearanceKit::set_view_glass(passwordLabel, true);
	BAppearanceKit::set_view_glass(fStatus, true);
	CenterOnScreen();
}


void
WiFiConnectWindow::SetCompletion(sem_id done, status_t* result)
{
	fDone = done;
	fResult = result;
}


void
WiFiConnectWindow::SetController(const BMessenger& controller)
{
	fController = controller;
}


void
WiFiConnectWindow::SetClosedMessenger(const BMessenger& target, int32 token)
{
	fClosedTarget = target;
	fToken = token;
}


void
WiFiConnectWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kConnect:
			_Connect();
			break;
		case kToggleOptions:
			fOptionsOpen = !fOptionsOpen;
			if (fOptionsOpen)
				fSecurity->Show();
			else
				fSecurity->Hide();
			fOther->SetLabel(fOptionsOpen
				? B_TRANSLATE("⌄ Other options")
				: B_TRANSLATE("› Other options"));
			break;
		case kMsgWiFiJoinResult:
			ApplyResult(message->GetInt32("status", B_ERROR),
				message->GetString("name", ""),
				message->GetBool("key_failed", false));
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
WiFiConnectWindow::_Connect()
{
	if (fBusy || !fController.IsValid())
		return;
	const char* password = fPassword->Text();
	int32 choice = kSecurityAutomatic;
	BMenuItem* marked = fSecurity->Menu()->FindMarked();
	if (marked != NULL && marked->Message() != NULL)
		marked->Message()->FindInt32("choice", &choice);
	bool open = choice == kSecurityOpen
		|| (choice == kSecurityAutomatic
			&& fAuthentication == B_NETWORK_AUTHENTICATION_NONE);
	if (!open && (password == NULL || password[0] == '\0')) {
		fStatus->SetHighUIColor(B_PANEL_TEXT_COLOR);
		fStatus->SetText(B_TRANSLATE("Enter the password."));
		return;
	}

	uint32 authentication = fAuthentication;
	if (choice == kSecurityWpa2 || choice == kSecurityTransition)
		authentication = B_NETWORK_AUTHENTICATION_WPA2;
	else if (choice == kSecurityWpa3)
		authentication = B_NETWORK_AUTHENTICATION_WPA3;
	// WPA2/WPA3 stays WPA-PSK. A mixed network must not take the SAE path.
	else if (choice == kSecurityOpen)
		authentication = B_NETWORK_AUTHENTICATION_NONE;

	BMessage join(kMsgWiFiJoin);
	join.AddString("name", fName);
	if (!open && password != NULL)
		join.AddString("password", password);
	join.AddBool("remember", fRemember->Value() == B_CONTROL_ON);
	join.AddUInt32("authentication", authentication);
	fController.SendMessage(&join);
	join.MakeEmpty();

	BString line(B_TRANSLATE("Connecting to %name%" B_UTF8_ELLIPSIS
		" checking the password"));
	line.ReplaceAll("%name%", fName);
	fStatus->SetHighUIColor(B_PANEL_TEXT_COLOR);
	fStatus->SetText(line);
	_SetBusy(true);
}


void
WiFiConnectWindow::ApplyResult(status_t status, const char* name,
	bool keyFailed)
{
	if (name != NULL && name[0] != '\0' && fName != name)
		return;
	if (status == B_OK) {
		_Finish(B_OK, true);
		return;
	}
	if (status == B_CANCELED)
		return;
	// A join timeout is not a rejected password. The red line is only for
	// a result that says the key was refused. Nothing sends that yet.
	_ShowFailure(keyFailed);
}


void
WiFiConnectWindow::_ShowFailure(bool keyFailed)
{
	_SetBusy(false);
	BString line;
	if (keyFailed) {
		line = B_TRANSLATE("%name% didn't accept this password. "
			"Check it and try again.");
		fStatus->SetHighUIColor(B_FAILURE_COLOR);
		fConnect->SetLabel(B_TRANSLATE("Try again"));
	} else {
		line = B_TRANSLATE("Couldn't reach %name%. "
			"Your password wasn't checked. Move closer to the router "
			"or try again.");
		rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
		bool dark = (panel.red + panel.green + panel.blue) < 384;
		rgb_color warning;
		warning.red = dark ? 250 : 133;
		warning.green = dark ? 199 : 79;
		warning.blue = dark ? 117 : 11;
		warning.alpha = 255;
		fStatus->SetHighColor(warning);
		fConnect->SetLabel(B_TRANSLATE("Try again"));
	}
	line.ReplaceAll("%name%", fName);
	fStatus->SetText(line);
	fPassword->MakeFocus(true);
}


void
WiFiConnectWindow::_SetBusy(bool busy)
{
	fBusy = busy;
	fPassword->SetEnabled(!busy);
	fRemember->SetEnabled(!busy);
	fSecurity->SetEnabled(!busy);
	fOther->SetEnabled(!busy);
	fConnect->SetEnabled(!busy);
	if (busy)
		fConnect->SetLabel(B_TRANSLATE("Connecting" B_UTF8_ELLIPSIS));
}


bool
WiFiConnectWindow::QuitRequested()
{
	if (fFinished)
		return true;
	if (fBusy && fController.IsValid())
		fController.SendMessage(kMsgWiFiLeave);
	if (fClosedTarget.IsValid()) {
		BMessage closed(kMsgWiFiConnectClosed);
		closed.AddInt32("token", fToken);
		fClosedTarget.PostMessage(&closed);
	}
	_Finish(B_CANCELED, false);
	return true;
}


void
WiFiConnectWindow::_Finish(status_t status, bool quit)
{
	if (fFinished)
		return;
	fFinished = true;
	fPassword->SetText("");
	if (fResult != NULL)
		*fResult = status;
	if (fDone >= 0)
		release_sem(fDone);
	fDone = -1;
	if (quit)
		Quit();
}


status_t
r5_wifi_connect_dialog(const char* device, const char* name,
	const char* subtitle, uint32 authentication, int32 bars)
{
	if (name == NULL || name[0] == '\0')
		return B_BAD_VALUE;

	status_t result = B_CANCELED;
	sem_id done = create_sem(0, "wifi connect");
	if (done < 0)
		return done;

	WiFiConnectWindow* window = new WiFiConnectWindow(BMessenger(), name,
		subtitle, authentication, bars);
	WiFiController* controller = new WiFiController(BMessenger(window),
		"dialog", 0);
	window->SetController(BMessenger(controller));
	window->SetCompletion(done, &result);
	controller->Start();
	if (device != NULL && device[0] != '\0') {
		BMessage select(kMsgWiFiSelectDevice);
		select.AddString("device", device);
		BMessenger(controller).SendMessage(&select);
	}
	window->Show();
	while (acquire_sem(done) == B_INTERRUPTED)
		;
	delete_sem(done);
	controller->PostMessage(B_QUIT_REQUESTED);
	return result;
}
