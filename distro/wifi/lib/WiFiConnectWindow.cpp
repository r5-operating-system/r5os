/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * One Wi-Fi password window. The status line changes in this window.
 */


#include <R5WiFi.h>

#include <OS.h>
#include <string.h>

#include <Bitmap.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
#include <PopUpMenu.h>
#include <StringView.h>
#include <TextView.h>

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
		BView("header", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
		fName(name),
		fSubtitle(subtitle),
		fBars(bars < 0 ? 0 : bars > 4 ? 4 : bars),
		fTile(NULL)
	{
		SetExplicitMinSize(BSize(360, 52));
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	}

	~ConnectHeader()
	{
		delete fTile;
	}

	void AttachedToWindow()
	{
		BView::AttachedToWindow();
		BAppearanceKit::set_view_glass(this, true);
	}

	void MessageReceived(BMessage* message)
	{
		if (message->what == B_COLORS_UPDATED) {
			delete fTile;
			fTile = NULL;
			Invalidate();
		}
		BView::MessageReceived(message);
	}

	void Draw(BRect updateRect)
	{
		(void)updateRect;
		if (fTile == NULL) {
			fTile = new BBitmap(BRect(0, 0, 39, 39), B_RGBA32, true);
			if (fTile->IsValid() && fTile->Bits() != NULL)
				memset(fTile->Bits(), 0, fTile->BitsLength());
			BView* off = new BView(fTile->Bounds(), "tile", B_FOLLOW_NONE,
				B_WILL_DRAW);
			fTile->AddChild(off);
			if (fTile->Lock()) {
				off->SetViewColor(B_TRANSPARENT_COLOR);
				off->SetHighUIColor(B_PANEL_TEXT_COLOR);
				r5_level_glyph_draw(off, R5_LEVEL_SIGNAL,
					fTile->Bounds().InsetByCopy(6, 8), fBars,
					off->HighColor(),
					ui_color(B_PANEL_BACKGROUND_COLOR),
					R5_LEVEL_STYLE_NORMAL);
				off->Sync();
				fTile->Unlock();
			}
		}
		PushState();
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
		font_height boldHeight;
		be_bold_font->GetHeight(&boldHeight);
		font_height plainHeight;
		be_plain_font->GetHeight(&plainHeight);
		float top = 6;
		if (fTile != NULL)
			DrawBitmap(fTile, BPoint(4, top));
		SetFont(be_bold_font);
		SetHighUIColor(B_PANEL_TEXT_COLOR);
		float titleBaseline = top + boldHeight.ascent + 2;
		DrawString(fName.String(), BPoint(54, titleBaseline));
		SetFont(be_plain_font);
		rgb_color text = ui_color(B_PANEL_TEXT_COLOR);
		text.alpha = 180;
		SetHighColor(text);
		float subBaseline = titleBaseline + boldHeight.descent
			+ plainHeight.ascent + 4;
		DrawString(fSubtitle.String(), BPoint(54, subBaseline));
		PopState();
	}

private:
	BString fName;
	BString fSubtitle;
	int32 fBars;
	BBitmap* fTile;
};


static void
setup_glass_view(BView* view)
{
	if (view == NULL)
		return;
	view->SetFlags(view->Flags() | B_TRANSPARENT_BACKGROUND);
	view->SetViewColor(B_TRANSPARENT_COLOR);
	view->SetLowColor(blend_color(B_TRANSPARENT_COLOR,
		ui_color(B_PANEL_TEXT_COLOR), 192));
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
}


WiFiConnectWindow::WiFiConnectWindow(const BMessenger& controller,
	const char* name, const char* subtitle, uint32 authentication, int32 bars,
	const BMessenger& closedTarget, int32 token, sem_id done, status_t* result)
	:
	BWindow(BRect(0, 0, 460, 220), B_TRANSLATE("Connect to Wi-Fi"),
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE | B_NOT_RESIZABLE
			| B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE
			| B_ASYNCHRONOUS_CONTROLS),
	fController(controller),
	fClosedTarget(closedTarget),
	fToken(token),
	fBars(bars),
	fName(name),
	fAuthentication(authentication),
	fBusy(false),
	fFinished(false),
	fOptionsOpen(false),
	fDone(done),
	fResult(result)
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

	fStatus = new BTextView("status");
	fStatus->MakeEditable(false);
	fStatus->MakeSelectable(false);
	fStatus->SetWordWrap(true);
	fStatus->SetStylable(false);
	setup_glass_view(fStatus);
	fStatus->SetExplicitMinSize(BSize(360, be_plain_font->Size() * 2));
	fStatus->SetExplicitMaxSize(BSize(360, be_plain_font->Size() * 4));
	fOther = new BButton("other", B_TRANSLATE("› Other options"),
		new BMessage(kToggleOptions));
	fConnect = new BButton("connect", B_TRANSLATE("Connect"),
		new BMessage(kConnect));
	BButton* cancel = new BButton("cancel", B_TRANSLATE("Cancel"),
		new BMessage(B_QUIT_REQUESTED));

	BStringView* passwordLabel = new BStringView("password label",
		B_TRANSLATE("Password"));
	passwordLabel->SetAlignment(B_ALIGN_RIGHT);

	ConnectHeader* header = new ConnectHeader(name,
		subtitle != NULL ? subtitle : "", bars);
	fSecurity->Hide();
	fStatus->Hide();
	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(header)
		.AddGrid(B_USE_SMALL_SPACING, B_USE_SMALL_SPACING)
			.Add(passwordLabel, 0, 0)
			.Add(fPassword, 1, 0)
			.Add(fRemember, 1, 1)
			.Add(fSecurity, 0, 2, 2)
			.Add(fStatus, 0, 3, 2)
		.End()
		.AddGroup(B_HORIZONTAL)
			.Add(fOther)
			.AddGlue()
			.Add(cancel)
			.Add(fConnect)
		.End();

	setup_glass_view(passwordLabel);
	setup_glass_view(fRemember);
	setup_glass_view(fSecurity);
	setup_glass_view(fStatus);
	BAppearanceKit::set_view_glass(passwordLabel, true);
	BAppearanceKit::set_view_glass(fRemember, true);
	BAppearanceKit::set_view_glass(fSecurity, true);
	BAppearanceKit::set_view_glass(fStatus, true);

	SetDefaultButton(fConnect);
	fPassword->MakeFocus(true);
	CenterOnScreen();
}


void
WiFiConnectWindow::SetController(const BMessenger& controller)
{
	fController = controller;
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


static void
set_status(BTextView* view, const char* text, rgb_color color)
{
	if (text == NULL || text[0] == '\0') {
		view->SetText("");
		if (!view->IsHidden())
			view->Hide();
		return;
	}
	if (view->IsHidden())
		view->Show();
	view->SetText(text);
	BFont font(be_plain_font);
	view->SelectAll();
	view->SetFontAndColor(&font, B_FONT_ALL, &color);
	view->Select(0, 0);
	view->Invalidate();
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
		set_status(fStatus, B_TRANSLATE("Enter the password."),
			ui_color(B_PANEL_TEXT_COLOR));
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

	BString secret(password);
	BMessage join(kMsgWiFiJoin);
	join.AddString("name", fName);
	if (!open && secret.Length() > 0)
		join.AddString("password", secret);
	join.AddBool("remember", fRemember->Value() == B_CONTROL_ON);
	join.AddUInt32("authentication", authentication);
	fController.SendMessage(&join);
	if (secret.Length() > 0) {
		char wipe[128];
		int32 length = secret.Length();
		if (length > 127)
			length = 127;
		memset(wipe, 0, sizeof(wipe));
		join.ReplaceString("password", wipe);
		char* buffer = secret.LockBuffer(secret.Length());
		if (buffer != NULL)
			memset(buffer, 0, secret.Length());
		secret.UnlockBuffer(0);
		fPassword->SetText(wipe);
	}
	fPassword->SetText("");
	fPassword->SetInvalid(false);
	join.MakeEmpty();

	BString line(B_TRANSLATE("Connecting to %name%" B_UTF8_ELLIPSIS
		" checking the password"));
	line.ReplaceAll("%name%", fName);
	set_status(fStatus, line.String(), ui_color(B_PANEL_TEXT_COLOR));
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
	_ShowFailure(keyFailed);
}


void
WiFiConnectWindow::_ShowFailure(bool keyFailed)
{
	_SetBusy(false);
	BString line;
	rgb_color color = ui_color(B_PANEL_TEXT_COLOR);
	if (keyFailed) {
		line = B_TRANSLATE("%name% didn't accept this password. "
			"Check it and try again.");
		color = ui_color(B_FAILURE_COLOR);
		fPassword->SetInvalid(true);
	} else {
		line = B_TRANSLATE("Couldn't reach %name%. "
			"Your password wasn't checked. Move closer to the router "
			"or try again.");
		fPassword->SetInvalid(false);
	}
	fConnect->SetLabel(B_TRANSLATE("Try again"));
	line.ReplaceAll("%name%", fName);
	set_status(fStatus, line.String(), color);
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
		fClosedTarget.SendMessage(&closed);
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


const char*
r5_wifi_security_name(uint32 authentication)
{
	switch (authentication) {
		case B_NETWORK_AUTHENTICATION_WPA3:
			return B_TRANSLATE("WPA3 Personal");
		case B_NETWORK_AUTHENTICATION_WPA2:
			return B_TRANSLATE("WPA2 Personal");
		case B_NETWORK_AUTHENTICATION_WPA:
			return B_TRANSLATE("WPA Personal");
		case B_NETWORK_AUTHENTICATION_WEP:
			return B_TRANSLATE("WEP");
		case B_NETWORK_AUTHENTICATION_NONE:
			return B_TRANSLATE("Open");
		default:
			return B_TRANSLATE("Wi-Fi");
	}
}


void
r5_wifi_connect_subtitle(BString& subtitle, uint32 authentication,
	int32 bars, const char* routerStandard)
{
	if (bars < 0)
		bars = 0;
	if (bars > 4)
		bars = 4;
	const char* signal;
	switch (bars) {
		case 0:
			signal = B_TRANSLATE("No signal");
			break;
		case 1:
			signal = B_TRANSLATE("Weak signal");
			break;
		case 2:
			signal = B_TRANSLATE("Fair signal");
			break;
		case 3:
			signal = B_TRANSLATE("Good signal");
			break;
		default:
			signal = B_TRANSLATE("Strong signal");
			break;
	}
	subtitle = r5_wifi_security_name(authentication);
	subtitle << " · " << signal;
	if (routerStandard != NULL && routerStandard[0] != '\0')
		subtitle << " · " << routerStandard;
}


static int32 sConnectDialogOpen = 0;


status_t
r5_wifi_connect_dialog(const char* device, const char* name,
	const char* subtitle, uint32 authentication, int32 bars)
{
	if (name == NULL || name[0] == '\0')
		return B_BAD_VALUE;
	if (atomic_add(&sConnectDialogOpen, 1) != 0) {
		atomic_add(&sConnectDialogOpen, -1);
		return B_BUSY;
	}

	status_t result = B_CANCELED;
	sem_id done = create_sem(0, "wifi connect");
	if (done < 0) {
		atomic_add(&sConnectDialogOpen, -1);
		return done;
	}

	WiFiConnectWindow* window = new WiFiConnectWindow(BMessenger(), name,
		subtitle, authentication, bars, BMessenger(), 0, done, &result);
	WiFiController* controller = new WiFiController(BMessenger(window),
		"dialog", 0);
	window->SetController(BMessenger(controller));
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
	atomic_add(&sConnectDialogOpen, -1);
	return result;
}
