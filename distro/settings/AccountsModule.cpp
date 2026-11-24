/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Users and Accounts, a page under Accounts (step 3 of the accounts work,
 * generated/GEMINI-ACCOUNTS-SECURITY-PROMPT.md, "The mockups"; issue #66).
 * Left: the accounts, with Add, Disable or Enable, and Delete. Right: the
 * card of the chosen account. Every change goes to Login through its admin
 * port, as the separate Users & Accounts program does, and is applied at
 * once. Login decides what a sender may see and change: an administrator,
 * or a team with a grant, gets every account; anyone else only the own
 * card. So the page does not require an administrator as a whole (as
 * Power and Bluetooth): it reloads on kMsgRefreshPage when the lock
 * changes. The "Guest and sharing" box belongs under the list (step 4).
 */


#include "AccountsModule.h"

#include "AccountAdmin.h"
#include "AccountDialogs.h"
#include "SettingsFont.h"
#include "SettingsManager.h"
#include "SettingsMessages.h"
#include "SettingsModule.h"

#include <Alert.h>
#include <Bitmap.h>
#include <Button.h>
#include <Card.h>
#include <Entry.h>
#include <FilePanel.h>
#include <GridLayout.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <R5UI.h>
#include <ScrollView.h>
#include <String.h>
#include <StringView.h>
#include <UserListItem.h>

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>


using BAppearanceKit::BCard;
using BAppearanceKit::BUserListItem;


static const uint32 kMsgSelect = 'acSl';
static const uint32 kMsgAdd = 'acAc';
static const uint32 kMsgDisable = 'acDs';
static const uint32 kMsgDelete = 'acDl';
static const uint32 kMsgName = 'acNm';
static const uint32 kMsgEmail = 'acEm';
static const uint32 kMsgType = 'acTp';
static const uint32 kMsgPassword = 'acPs';
static const uint32 kMsgChoose = 'acCh';

static const char* kDot = " \xc2\xb7 ";


static float
px(float value)
{
	return floorf(value * account_unit() + 0.5f);
}


static void
show_error(const char* text)
{
	BAlert* alert = new BAlert("Users and Accounts", text, "OK", NULL, NULL,
		B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go(NULL);
}


static BFont
sized(const BFont* face, float mockupSize)
{
	BFont font(face);
	font.SetSize(floorf(face->Size() * mockupSize / 12.0f * 2 + 0.5f) / 2);
	return font;
}


static BStringView*
caption(const char* name, const char* text)
{
	// The small capitals over the list and the card. The font is set in
	// AttachedToWindow(), after the window gave the page its plain font.
	BStringView* view = new BStringView(name, text);
	view->SetHighColor(mix_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		ui_color(B_PANEL_TEXT_COLOR), 140));
	return view;
}


struct AccountInfo {
	BString		name;
	BString		real;
	BString		home;
	uid_t		uid;
	bool		admin;
	bool		disabled;
	bool		first;

	AccountInfo()
		:
		uid((uid_t)-1),
		admin(false),
		disabled(false),
		first(false)
	{
	}

	const char* Shown() const
	{
		return real.Length() > 0 ? real.String() : name.String();
	}

	bool IsSelf() const
	{
		return uid == getuid();
	}
};


// #pragma mark - AccountItem


// BUserListItem keeps the name and the role; this row draws them as the
// mockup does: "@username · you", "· disabled" and the row faded, and the
// Admin or Standard pill at the right.
class AccountItem : public BUserListItem {
public:
	AccountItem(const AccountInfo& info, const BBitmap* photo)
		:
		BUserListItem(info.name.String(), info.Shown(),
			info.admin ? "Administrator" : "Standard"),
		fInfo(info),
		fPhoto(photo != NULL ? new BBitmap(photo) : NULL)
	{
	}

	~AccountItem()
	{
		delete fPhoto;
	}

	const AccountInfo& Info() const
	{
		return fInfo;
	}

	virtual void Update(BView* owner, const BFont* font)
	{
		BUserListItem::Update(owner, font);
		SetHeight(px(52));
	}

	virtual void DrawItem(BView* owner, BRect frame, bool complete)
	{
		(void)complete;
		rgb_color document = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
		rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
		rgb_color text = ui_color(B_DOCUMENT_TEXT_COLOR);
		rgb_color accent = ui_color(B_STATUS_BAR_COLOR);
		rgb_color background = owner->LowColor();

		owner->PushState();
		if (IsSelected()) {
			background = mix_color(document, accent, 28);
			owner->SetHighColor(background);
			owner->FillRect(frame);
			BRect bar = frame;
			bar.right = bar.left + px(3) - 1;
			owner->SetHighColor(accent);
			owner->FillRect(bar);
		} else {
			owner->SetHighColor(background);
			owner->FillRect(frame);
		}
		owner->SetHighColor(mix_color(background, panel, 80));
		owner->StrokeLine(frame.LeftBottom(), frame.RightBottom());

		float side = px(32);
		float padding = px(12);
		BRect circle(0, 0, side - 1, side - 1);
		circle.OffsetTo(frame.left + padding,
			floorf(frame.top + (frame.Height() - side) / 2));
		account_draw_picture(owner, circle, fPhoto, fInfo.admin);
		if (fInfo.disabled) {
			rgb_color veil = background;
			veil.alpha = 140;
			owner->SetDrawingMode(B_OP_ALPHA);
			owner->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
			owner->SetHighColor(veil);
			owner->FillEllipse(circle);
			owner->SetDrawingMode(B_OP_OVER);
		}

		// The pill at the right.
		BFont pillFont = sized(r5_settings_bold_font(), 10.0f);
		const char* pill = fInfo.admin ? "Admin" : "Standard";
		font_height pillHeight;
		pillFont.GetHeight(&pillHeight);
		float pillWidth = pillFont.StringWidth(pill) + px(16);
		float pillTall = ceilf(pillHeight.ascent + pillHeight.descent) + px(6);
		BRect pillRect(frame.right - padding - pillWidth,
			floorf(frame.top + (frame.Height() - pillTall) / 2),
			frame.right - padding,
			floorf(frame.top + (frame.Height() - pillTall) / 2) + pillTall);
		rgb_color pillFill = fInfo.admin ? mix_color(document, accent, 40)
			: mix_color(panel, document, 60);
		rgb_color pillText = fInfo.admin ? accent
			: mix_color(text, document, 110);
		if (fInfo.disabled) {
			pillFill = mix_color(pillFill, background, 128);
			pillText = mix_color(pillText, background, 128);
		}
		owner->SetHighColor(pillFill);
		owner->FillRoundRect(pillRect, pillTall / 2, pillTall / 2);
		owner->SetFont(&pillFont);
		owner->SetHighColor(pillText);
		owner->SetLowColor(pillFill);
		owner->DrawString(pill, BPoint(pillRect.left + px(8),
			floorf(pillRect.top + (pillTall + pillHeight.ascent
				- pillHeight.descent) / 2)));

		// The name and the line under it.
		float left = circle.right + px(10);
		float width = pillRect.left - px(8) - left;
		if (width > 8) {
			BFont bold(r5_settings_bold_font());
			BFont small = sized(r5_settings_plain_font(), 11.0f);
			font_height boldHeight;
			font_height smallHeight;
			bold.GetHeight(&boldHeight);
			small.GetHeight(&smallHeight);
			float gap = px(2);
			float block = boldHeight.ascent + boldHeight.descent + gap
				+ smallHeight.ascent + smallHeight.descent;
			float y = floorf(frame.top + (frame.Height() - block) / 2
				+ boldHeight.ascent);

			rgb_color nameColor = text;
			rgb_color subColor = mix_color(text, document, 120);
			if (fInfo.disabled) {
				nameColor = mix_color(nameColor, background, 128);
				subColor = mix_color(subColor, background, 128);
			}
			BString name(fInfo.Shown());
			bold.TruncateString(&name, B_TRUNCATE_END, width);
			owner->SetLowColor(background);
			owner->SetFont(&bold);
			owner->SetHighColor(nameColor);
			owner->DrawString(name.String(), BPoint(left, y));

			BString sub("@");
			sub << fInfo.name;
			if (fInfo.IsSelf())
				sub << kDot << "you";
			if (fInfo.disabled)
				sub << kDot << "disabled";
			small.TruncateString(&sub, B_TRUNCATE_END, width);
			owner->SetFont(&small);
			owner->SetHighColor(subColor);
			owner->DrawString(sub.String(), BPoint(left, y
				+ boldHeight.descent + gap + smallHeight.ascent));
		}
		owner->PopState();
	}

private:
	AccountInfo	fInfo;
	BBitmap*	fPhoto;
};


// #pragma mark - AccountPicture


class AccountPicture : public BView {
public:
	AccountPicture(float side)
		:
		BView("picture", B_WILL_DRAW),
		fPhoto(NULL),
		fAdmin(false)
	{
		SetExplicitSize(BSize(side - 1, side - 1));
	}

	~AccountPicture()
	{
		delete fPhoto;
	}

	virtual void AttachedToWindow()
	{
		BView::AttachedToWindow();
		AdoptParentColors();
	}

	void SetPicture(const BBitmap* photo, bool admin)
	{
		if (photo != fPhoto) {
			delete fPhoto;
			fPhoto = photo != NULL ? new BBitmap(photo) : NULL;
		}
		fAdmin = admin;
		Invalidate();
	}

	const BBitmap* Picture() const
	{
		return fPhoto;
	}

	virtual void Draw(BRect updateRect)
	{
		(void)updateRect;
		account_draw_picture(this, Bounds(), fPhoto, fAdmin);
	}

private:
	BBitmap*	fPhoto;
	bool		fAdmin;
};


// #pragma mark - AccountsModule


class AccountsView;


class AccountsModule : public SettingsModule {
public:
								AccountsModule();

	virtual	const char*			Identifier() const { return "accounts"; }
	virtual	const char*			Name() const { return "Users and Accounts"; }
	virtual	const char*			Category() const { return "accounts"; }
	virtual	const char*			Description() const
									{ return "People on this computer."; }
	virtual	const char*			Keywords() const
									{ return "user users account accounts "
										"password administrator standard "
										"picture email sign in"; }
	virtual	bool				RequiresAdmin() const { return false; }
	virtual	BView*				CreateView(SettingsContext* context,
									BRect frame);
	virtual	status_t			LoadSettings(SettingsContext* context);
	virtual	status_t			SaveSettings(SettingsContext* context);
	virtual	status_t			RevertSettings(SettingsContext* context);
	virtual	bool				IsModified() const;

			SettingsContext*	Context() const { return fContext; }
			// The values of an account from when the page was opened, kept
			// the first time its card is shown.
			void				Remember(const char* name,
									const BMessage& values);
			void				Touched(const char* name);
			void				ClearView(AccountsView* view);

private:
			AccountsView*		fView;
			SettingsContext*	fContext;
			BMessage			fOpened;
			BMessage			fTouched;
};


// #pragma mark - AccountsView


class AccountsView : public BGroupView {
public:
								AccountsView(AccountsModule* module);
	virtual						~AccountsView();

	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);

			void				Reload(const char* select = NULL);

private:
			bool				_Managing() const;
			AccountItem*		_Selected() const;
			void				_ShowSelected();
			void				_ShowAccount(const AccountInfo& info);
			void				_SyncEnabled();
			void				_SetName(BMessage* message);
			void				_SetEmail(BMessage* message);
			void				_SetType(BMessage* message);
			void				_SetPicture(BMessage* message);
			void				_DisableOrEnable();
			void				_Delete();
			void				_Password();
			bool				_LastAdmin(const AccountInfo& info) const;

			AccountsModule*		fModule;
			BGroupView*			fLeft;
			BStringView*		fListCaption;
			bool				fLeftShown;
			BListView*			fList;
			BButton*			fAdd;
			BButton*			fDisable;
			BButton*			fDelete;
			BStringView*		fCaption;
			BCard*				fCard;
			AccountPicture*		fPicture;
			BStringView*		fHeadName;
			BStringView*		fHeadLine;
			BButton*			fChoose;
			AccountTextField*	fName;
			AccountTextField*	fEmail;
			BMenuField*			fType;
			BMenuItem*			fTypeAdmin;
			BMenuItem*			fTypeStandard;
			BButton*			fPassword;
			BFilePanel*			fPanel;
			AccountInfo			fShown;
			bool				fHasShown;
			int32				fAdmins;
			bool				fManaging;
};


AccountsView::AccountsView(AccountsModule* module)
	:
	BGroupView("accounts page", B_HORIZONTAL, px(24)),
	fModule(module),
	fLeftShown(true),
	fPanel(NULL),
	fHasShown(false),
	fAdmins(0),
	fManaging(false)
{
	// Left: the accounts.
	fList = new BListView("accounts");
	fList->SetSelectionMessage(new BMessage(kMsgSelect));
	BScrollView* scroll = new BScrollView("accounts scroll", fList, 0, false,
		true);
	scroll->SetExplicitMinSize(BSize(B_SIZE_UNSET, px(52) * 4 + 4));
	fAdd = new BButton("add", "Add account…", new BMessage(kMsgAdd));
	fDisable = new BButton("disable", "Disable account",
		new BMessage(kMsgDisable));
	fDelete = new BButton("delete", "Delete account…",
		new BMessage(kMsgDelete));

	fListCaption = caption("accounts caption", "ACCOUNTS");
	fLeft = new BGroupView(B_VERTICAL, px(8));
	BLayoutBuilder::Group<>(fLeft)
		.Add(fListCaption)
		.Add(scroll)
		.AddGroup(B_HORIZONTAL, px(6))
			.Add(fAdd)
			.Add(fDisable)
			.Add(fDelete)
		.End()
		// Step 4 adds the "GUEST AND SHARING" caption and its card here.
		.AddGlue();
	fLeft->SetExplicitPreferredSize(BSize(px(300), B_SIZE_UNSET));

	// Right: the card of the chosen account.
	fCaption = caption("card caption", " ");
	fPicture = new AccountPicture(px(64));
	fHeadName = new BStringView("head name", "");
	fHeadLine = new BStringView("head line", "");
	fHeadLine->SetHighColor(mix_color(ui_color(B_DOCUMENT_BACKGROUND_COLOR),
		ui_color(B_DOCUMENT_TEXT_COLOR), 140));
	fHeadLine->SetExplicitMinSize(BSize(px(120), B_SIZE_UNSET));
	fChoose = new BButton("choose", "Choose picture…",
		new BMessage(kMsgChoose));

	fName = new AccountTextField("name", new BMessage(kMsgName));
	fEmail = new AccountTextField("email", new BMessage(kMsgEmail));
	fEmail->SetPlaceholder("name@example.com");
	BPopUpMenu* menu = new BPopUpMenu("type");
	BMessage* adminMessage = new BMessage(kMsgType);
	adminMessage->AddBool("admin", true);
	fTypeAdmin = new BMenuItem("Administrator", adminMessage);
	BMessage* standardMessage = new BMessage(kMsgType);
	standardMessage->AddBool("admin", false);
	fTypeStandard = new BMenuItem("Standard", standardMessage);
	menu->AddItem(fTypeAdmin);
	menu->AddItem(fTypeStandard);
	fType = new BMenuField("type field", NULL, menu);
	fPassword = new BButton("password", "Change password…",
		new BMessage(kMsgPassword));

	AccountWrapLabel* note = new AccountWrapLabel("card note",
		"Changes are saved as you make them. The last administrator cannot "
		"become a standard account.");
	note->SetTextColor(mix_color(ui_color(B_DOCUMENT_BACKGROUND_COLOR),
		ui_color(B_DOCUMENT_TEXT_COLOR), 140));

	BStringView* nameLabel = new BStringView("name label", "Name");
	BStringView* emailLabel = new BStringView("email label", "Email");
	BStringView* typeLabel = new BStringView("type label", "Account type");
	BStringView* passwordLabel = new BStringView("password label",
		"Password");

	fCard = new BCard("account card", BAppearanceKit::B_CARD_DOCUMENT);
	BGroupLayout* cardLayout = (BGroupLayout*)fCard->GetLayout();
	cardLayout->SetSpacing(px(14));
	BLayoutBuilder::Group<>(cardLayout)
		.SetInsets(px(18), px(16), px(18), px(16))
		.AddGroup(B_HORIZONTAL, px(14))
			.Add(fPicture)
			.AddGroup(B_VERTICAL, px(3))
				.AddGlue()
				.Add(fHeadName)
				.Add(fHeadLine)
				.AddGlue()
			.End()
			.AddGlue()
			.Add(fChoose)
		.End()
		.AddGrid(px(14), px(8))
			.Add(nameLabel, 0, 0)
			.Add(fName, 1, 0)
			.Add(emailLabel, 0, 1)
			.Add(fEmail, 1, 1)
			.Add(typeLabel, 0, 2)
			.Add(fType, 1, 2)
			.Add(passwordLabel, 0, 3)
			.AddGroup(B_HORIZONTAL, 0, 1, 3)
				.Add(fPassword)
				.AddGlue()
			.End()
			.SetColumnWeight(1, 1.0f)
		.End()
		.Add(note);

	BCard* info = new BCard("standard info", BAppearanceKit::B_CARD_PANEL);
	AccountWrapLabel* infoText = new AccountWrapLabel("standard info text",
		"A standard account sees only its own card (name, email, picture, "
		"password). The account list, account types and the guest wait for "
		"an administrator's unlock.");
	infoText->SetTextColor(mix_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		ui_color(B_PANEL_TEXT_COLOR), 160));
	BLayoutBuilder::Group<>((BGroupLayout*)info->GetLayout())
		.SetInsets(px(14), px(10), px(14), px(10))
		.Add(infoText);

	BGroupView* right = new BGroupView(B_VERTICAL, px(8));
	BLayoutBuilder::Group<>(right)
		.Add(fCaption)
		.Add(fCard)
		.Add(info)
		.AddGlue();

	BLayoutBuilder::Group<>(this)
		.SetInsets(0, px(4), 0, 0)
		.Add(fLeft, 0.0f)
		.Add(right, 1.0f);
}


AccountsView::~AccountsView()
{
	delete fPanel;
	if (fModule != NULL)
		fModule->ClearView(this);
}


void
AccountsView::AttachedToWindow()
{
	BGroupView::AttachedToWindow();
	// The window gave every view the plain Settings font before the page
	// was attached; the captions and the heading get theirs here.
	BFont captionFont = sized(r5_settings_bold_font(), 10.5f);
	fListCaption->SetFont(&captionFont);
	fCaption->SetFont(&captionFont);
	BFont headFont = sized(r5_settings_bold_font(), 15.0f);
	fHeadName->SetFont(&headFont);
	BFont lineFont = sized(r5_settings_plain_font(), 11.0f);
	fHeadLine->SetFont(&lineFont);
	fList->SetTarget(this);
	fAdd->SetTarget(this);
	fDisable->SetTarget(this);
	fDelete->SetTarget(this);
	fChoose->SetTarget(this);
	fName->SetTarget(this);
	fEmail->SetTarget(this);
	fType->Menu()->SetTargetForItems(this);
	fPassword->SetTarget(this);
	Reload();
}


bool
AccountsView::_Managing() const
{
	SettingsContext* context = fModule != NULL ? fModule->Context() : NULL;
	return context != NULL && context->AdminChangeAllowed();
}


AccountItem*
AccountsView::_Selected() const
{
	return dynamic_cast<AccountItem*>(fList->ItemAt(fList->CurrentSelection()));
}


bool
AccountsView::_LastAdmin(const AccountInfo& info) const
{
	return info.admin && !info.disabled && fAdmins <= 1;
}


void
AccountsView::Reload(const char* select)
{
	BString keep;
	if (select != NULL)
		keep = select;
	else if (_Selected() != NULL)
		keep = _Selected()->Info().name;
	else if (fHasShown)
		keep = fShown.name;

	fManaging = _Managing();
	if (fManaging != fLeftShown) {
		if (fManaging)
			fLeft->Show();
		else
			fLeft->Hide();
		fLeftShown = fManaging;
	}

	for (int32 i = fList->CountItems() - 1; i >= 0; i--)
		delete fList->RemoveItem(i);

	BMessage request(kMsgAdminListAccounts);
	BMessage reply;
	BString error;
	if (account_ask_login(request, reply, error) != B_OK) {
		fHasShown = false;
		BString text("Accounts could not be read: ");
		text << error;
		fCaption->SetText(text.String());
		_SyncEnabled();
		return;
	}
	fAdmins = reply.GetInt32("admins", 0);

	int32 select_index = -1;
	int32 selfIndex = -1;
	const char* name = NULL;
	for (int32 i = 0; reply.FindString("name", i, &name) == B_OK; i++) {
		AccountInfo info;
		info.name = name;
		info.real = reply.GetString("real", i, "");
		info.home = reply.GetString("home", i, "");
		info.uid = (uid_t)reply.GetInt32("uid", i, -1);
		info.admin = reply.GetBool("admin", i, false);
		info.disabled = reply.GetBool("disabled", i, false);
		info.first = reply.GetBool("first", i, false);

		BBitmap* photo = NULL;
		BMessage profileRequest(kMsgAdminGetProfile);
		profileRequest.AddString("name", name);
		BMessage profile;
		BString profileError;
		if (account_ask_login(profileRequest, profile, profileError)
				== B_OK) {
			const void* data = NULL;
			ssize_t size = 0;
			if (profile.FindData("picture", B_RAW_TYPE, &data, &size) == B_OK)
				photo = account_picture_from_blob(data, size);
		}
		fList->AddItem(new AccountItem(info, photo));
		delete photo;
		if (keep.Length() > 0 && keep == info.name)
			select_index = fList->CountItems() - 1;
		if (info.IsSelf())
			selfIndex = fList->CountItems() - 1;
	}

	if (select_index < 0)
		select_index = selfIndex;
	if (select_index < 0 && fList->CountItems() > 0)
		select_index = 0;
	if (select_index >= 0) {
		fList->Select(select_index);
		fList->ScrollToSelection();
		_ShowSelected();
	} else {
		fHasShown = false;
		fCaption->SetText(" ");
		_SyncEnabled();
	}
}


void
AccountsView::_ShowSelected()
{
	AccountItem* item = _Selected();
	if (item != NULL)
		_ShowAccount(item->Info());
	else
		_SyncEnabled();
}


void
AccountsView::_ShowAccount(const AccountInfo& info)
{
	fShown = info;
	fHasShown = true;

	BString upper(info.Shown());
	upper.ToUpper();
	fCaption->SetText(upper.String());
	fHeadName->SetText(info.Shown());
	BString line("@");
	line << info.name << kDot << (info.admin ? "Administrator" : "Standard");
	if (info.disabled)
		line << kDot << "disabled";
	if (info.home.Length() > 0)
		line << kDot << "home " << info.home;
	fHeadLine->SetText(line.String());

	fName->SetText(info.real.String());
	fName->Message()->RemoveName("name");
	fName->Message()->AddString("name", info.name.String());
	fEmail->SetText("");
	fEmail->Message()->RemoveName("name");
	fEmail->Message()->AddString("name", info.name.String());
	(info.admin ? fTypeAdmin : fTypeStandard)->SetMarked(true);

	BBitmap* photo = NULL;
	BMessage request(kMsgAdminGetProfile);
	request.AddString("name", info.name.String());
	BMessage reply;
	BString error;
	BMessage values;
	values.AddString("real", info.real.String());
	values.AddBool("admin", info.admin);
	values.AddBool("disabled", info.disabled);
	if (account_ask_login(request, reply, error) == B_OK) {
		const char* email = NULL;
		if (reply.FindString("email", &email) == B_OK && email != NULL) {
			fEmail->SetText(email);
			values.AddString("email", email);
		}
		const void* data = NULL;
		ssize_t size = 0;
		if (reply.FindData("picture", B_RAW_TYPE, &data, &size) == B_OK) {
			photo = account_picture_from_blob(data, size);
			values.AddData("picture", B_RAW_TYPE, data, size);
		}
		if (fModule != NULL)
			fModule->Remember(info.name.String(), values);
	}
	fPicture->SetPicture(photo, info.admin);
	delete photo;
	_SyncEnabled();
}


void
AccountsView::_SyncEnabled()
{
	bool shown = fHasShown;
	bool self = shown && fShown.IsSelf();
	bool edit = shown && (self || fManaging);
	fName->SetEnabled(edit);
	fEmail->SetEnabled(edit);
	fChoose->SetEnabled(edit);
	fPassword->SetEnabled(edit);
	fType->SetEnabled(shown && fManaging && !fShown.first
		&& !(fShown.admin && _LastAdmin(fShown)));

	fAdd->SetEnabled(fManaging);
	bool other = shown && fManaging && !fShown.first && !self;
	fDisable->SetLabel(shown && fShown.disabled ? "Enable account"
		: "Disable account");
	fDisable->SetEnabled(other && (fShown.disabled || !_LastAdmin(fShown)));
	fDelete->SetEnabled(other && !_LastAdmin(fShown));
}


void
AccountsView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgRefreshPage:
			// The lock opened or closed.
			Reload();
			break;
		case kMsgSelect:
			_ShowSelected();
			break;
		case kMsgAdd:
			if (fManaging)
				account_open_add_window(Window(), BMessenger(this));
			break;
		case kMsgAccountAdded:
			Reload(message->GetString("name", NULL));
			break;
		case kMsgAccountPasswordChanged:
			break;
		case kMsgDisable:
			_DisableOrEnable();
			break;
		case kMsgDelete:
			_Delete();
			break;
		case kMsgName:
			_SetName(message);
			break;
		case kMsgEmail:
			_SetEmail(message);
			break;
		case kMsgType:
			_SetType(message);
			break;
		case kMsgPassword:
			_Password();
			break;
		case kMsgChoose:
			if (message->HasRef("refs")) {
				_SetPicture(message);
				break;
			}
			if (!fHasShown)
				break;
			if (fPanel == NULL) {
				fPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this),
					NULL, B_FILE_NODE, false, NULL);
			}
			{
				BMessage pick(kMsgChoose);
				pick.AddString("name", fShown.name.String());
				fPanel->SetMessage(&pick);
			}
			fPanel->Show();
			break;
		default:
			BGroupView::MessageReceived(message);
			break;
	}
}


void
AccountsView::_SetName(BMessage* message)
{
	const char* name = message->GetString("name", NULL);
	if (name == NULL || !fHasShown || fShown.name != name)
		return;
	BString real(fName->Text());
	if (real == fShown.real)
		return;
	BMessage request(kMsgAdminSetProfile);
	request.AddString("name", name);
	request.AddString("real", real.String());
	BMessage reply;
	BString error;
	if (account_ask_login(request, reply, error) != B_OK) {
		show_error(error.Length() > 0 ? error.String()
			: "The name could not be saved.");
		fName->SetText(fShown.real.String());
		return;
	}
	fModule->Touched(name);
	Reload(name);
}


void
AccountsView::_SetEmail(BMessage* message)
{
	const char* name = message->GetString("name", NULL);
	if (name == NULL || !fHasShown || fShown.name != name)
		return;
	BMessage request(kMsgAdminSetProfile);
	request.AddString("name", name);
	request.AddString("email", fEmail->Text());
	BMessage reply;
	BString error;
	if (account_ask_login(request, reply, error) != B_OK) {
		show_error(error.Length() > 0 ? error.String()
			: "The email address could not be saved.");
		return;
	}
	fModule->Touched(name);
}


void
AccountsView::_SetType(BMessage* message)
{
	if (!fHasShown)
		return;
	bool admin = message->GetBool("admin", false);
	if (admin == fShown.admin)
		return;
	BString name(fShown.name);
	BMessage request(kMsgAdminSetAccountType);
	request.AddString("name", name.String());
	request.AddBool("admin", admin);
	BMessage reply;
	BString error;
	if (account_ask_login(request, reply, error) != B_OK) {
		show_error(error.Length() > 0 ? error.String()
			: "The account type could not be changed.");
		(fShown.admin ? fTypeAdmin : fTypeStandard)->SetMarked(true);
		return;
	}
	fModule->Touched(name.String());
	Reload(name.String());
}


void
AccountsView::_SetPicture(BMessage* message)
{
	const char* name = message->GetString("name", NULL);
	entry_ref ref;
	if (name == NULL || message->FindRef("refs", &ref) != B_OK)
		return;
	BString error;
	BBitmap* picture = account_load_picture(ref, error);
	if (picture == NULL) {
		show_error(error.String());
		return;
	}
	BMessage request(kMsgAdminSetProfile);
	request.AddString("name", name);
	status_t status = account_add_picture_blob(picture, request);
	delete picture;
	BMessage reply;
	if (status == B_OK)
		status = account_ask_login(request, reply, error);
	if (status != B_OK) {
		show_error(error.Length() > 0 ? error.String()
			: "The picture could not be saved.");
		return;
	}
	fModule->Touched(name);
	Reload(name);
}


void
AccountsView::_DisableOrEnable()
{
	if (!fHasShown || !fManaging)
		return;
	BString name(fShown.name);
	BMessage request(kMsgAdminSetDisabled);
	request.AddString("name", name.String());
	request.AddBool("disable", !fShown.disabled);
	BMessage reply;
	BString error;
	if (account_ask_login(request, reply, error) != B_OK) {
		show_error(error.Length() > 0 ? error.String()
			: "The account could not be changed.");
	} else
		fModule->Touched(name.String());
	Reload(name.String());
}


void
AccountsView::_Delete()
{
	if (!fHasShown || !fManaging || fShown.first || fShown.IsSelf())
		return;
	BString name(fShown.name);
	BString text;
	text.SetToFormat("Delete the account of %s?\n\nKeep files leaves the "
		"home folder %s on the disk. Delete files removes it, with "
		"everything in it.", fShown.Shown(), fShown.home.String());
	BAlert* alert = new BAlert("Delete account", text.String(), "Cancel",
		"Keep files", "Delete files", B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	int32 choice = alert->Go();
	if (choice <= 0)
		return;
	BMessage request(kMsgAdminDelete);
	request.AddString("name", name.String());
	request.AddBool("delete_files", choice == 2);
	BMessage reply;
	BString error;
	if (account_ask_login(request, reply, error) != B_OK) {
		show_error(error.Length() > 0 ? error.String()
			: "The account could not be deleted.");
	}
	Reload();
}


void
AccountsView::_Password()
{
	if (!fHasShown)
		return;
	bool self = fShown.IsSelf();
	if (!self && !fManaging)
		return;
	account_open_password_window(Window(), BMessenger(this),
		fShown.name.String(), fShown.real.String(), fShown.admin, self,
		fPicture->Picture());
}


// #pragma mark - AccountsModule


AccountsModule::AccountsModule()
	:
	fView(NULL),
	fContext(NULL)
{
}


BView*
AccountsModule::CreateView(SettingsContext* context, BRect frame)
{
	(void)frame;
	fContext = context;
	fView = new AccountsView(this);
	return fView;
}


void
AccountsModule::ClearView(AccountsView* view)
{
	if (fView == view)
		fView = NULL;
}


status_t
AccountsModule::LoadSettings(SettingsContext* context)
{
	fContext = context;
	fOpened.MakeEmpty();
	fTouched.MakeEmpty();
	if (fView != NULL && fView->Window() != NULL)
		fView->Reload();
	return B_OK;
}


status_t
AccountsModule::SaveSettings(SettingsContext* context)
{
	// Every change is sent to Login when it is made.
	(void)context;
	return B_OK;
}


void
AccountsModule::Remember(const char* name, const BMessage& values)
{
	if (name == NULL || fOpened.HasMessage(name))
		return;
	fOpened.AddMessage(name, &values);
}


void
AccountsModule::Touched(const char* name)
{
	if (name != NULL && !fTouched.HasBool(name))
		fTouched.AddBool(name, true);
	if (fContext != NULL)
		fContext->NotifyChanged(Identifier());
}


bool
AccountsModule::IsModified() const
{
	return fTouched.CountNames(B_ANY_TYPE) > 0;
}


status_t
AccountsModule::RevertSettings(SettingsContext* context)
{
	fContext = context;
	bool managing = context != NULL && context->AdminChangeAllowed();

	// The current type and state, to send only what changed.
	BMessage listRequest(kMsgAdminListAccounts);
	BMessage list;
	BString error;
	account_ask_login(listRequest, list, error);

	BString failed;
	char* name = NULL;
	type_code type;
	for (int32 i = 0; fTouched.GetInfo(B_BOOL_TYPE, i, &name, &type)
			== B_OK; i++) {
		BMessage values;
		if (fOpened.FindMessage(name, &values) != B_OK)
			continue;
		int32 index = -1;
		const char* listed = NULL;
		for (int32 j = 0; list.FindString("name", j, &listed) == B_OK; j++) {
			if (strcmp(listed, name) == 0) {
				index = j;
				break;
			}
		}
		if (index < 0)
			continue;
		bool self = (uid_t)list.GetInt32("uid", index, -1) == getuid();
		if (!self && !managing)
			continue;

		BMessage profile(kMsgAdminSetProfile);
		profile.AddString("name", name);
		profile.AddString("real", values.GetString("real", ""));
		profile.AddString("email", values.GetString("email", ""));
		const void* data = NULL;
		ssize_t size = 0;
		if (values.FindData("picture", B_RAW_TYPE, &data, &size) == B_OK)
			profile.AddData("picture", B_RAW_TYPE, data, size);
		else
			profile.AddBool("clear_picture", true);
		BMessage reply;
		BString text;
		if (account_ask_login(profile, reply, text) != B_OK)
			failed << "\n" << name << ": " << text;

		if (!managing)
			continue;
		bool admin = values.GetBool("admin", false);
		if (admin != list.GetBool("admin", index, false)) {
			BMessage request(kMsgAdminSetAccountType);
			request.AddString("name", name);
			request.AddBool("admin", admin);
			if (account_ask_login(request, reply, text) != B_OK)
				failed << "\n" << name << ": " << text;
		}
		bool disabled = values.GetBool("disabled", false);
		if (disabled != list.GetBool("disabled", index, false)) {
			BMessage request(kMsgAdminSetDisabled);
			request.AddString("name", name);
			request.AddBool("disable", disabled);
			if (account_ask_login(request, reply, text) != B_OK)
				failed << "\n" << name << ": " << text;
		}
	}
	fTouched.MakeEmpty();
	if (fView != NULL)
		fView->Reload();
	if (failed.Length() > 0) {
		BString message("Not everything could be put back:");
		message << failed;
		show_error(message.String());
	}
	return B_OK;
}


void
register_accounts_page(SettingsManager& manager)
{
	manager.Add(new AccountsModule());
}
