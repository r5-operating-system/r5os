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



#include "AccountAdmin.h"
#include "AccountDialogs.h"
#include "SettingsFont.h"
#include "SettingsMessages.h"
#include "SettingsModule.h"

#include <AdminLock.h>
#include <Alert.h>
#include <Bitmap.h>
#include <Button.h>
#include <Card.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <Entry.h>
#include <FilePanel.h>
#include <GridLayout.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <R5IconView.h>
#include <R5Icons.h>
#include <R5UI.h>
#include <ScrollView.h>
#include <SeparatorView.h>
#include <String.h>
#include <StringView.h>
#include <UserListItem.h>

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Settings: Users and Accounts"


using BAppearanceKit::BCard;
using BAppearanceKit::BIconView;
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
static const uint32 kMsgRemovePicture = 'acRp';
static const uint32 kMsgGuestToggle = 'acGt';
static const uint32 kMsgOpenShared = 'acSh';

static const char* kDot = " \xc2\xb7 ";


static float
px(float value)
{
	return floorf(value * account_unit() + 0.5f);
}


static void
show_error(const char* text)
{
	BAlert* alert = new BAlert(B_TRANSLATE("Users and Accounts"), text,
		B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
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
			info.admin ? B_TRANSLATE("Administrator") : B_TRANSLATE("Standard")),
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
		const char* pill = fInfo.admin ? B_TRANSLATE("Admin")
			: B_TRANSLATE("Standard");
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
				sub << kDot << B_TRANSLATE("you");
			if (fInfo.disabled)
				sub << kDot << B_TRANSLATE("disabled");
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
	virtual	const char*			Name() const { return B_TRANSLATE("Users and Accounts"); }
	virtual	const char*			Category() const { return "accounts"; }
	virtual	const char*			Description() const
									{ return B_TRANSLATE("People on this computer."); }
	virtual	const char*			Keywords() const
									{ return B_TRANSLATE_COMMENT(
										"user users account accounts "
										"password administrator standard "
										"picture email sign in guest shared",
										"Search words, separated by spaces. Keep the "
										"English words and add words in your language."); }
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
			void				RememberGuest(bool enabled);
			void				GuestTouched();
			void				ClearView(AccountsView* view);

private:
			AccountsView*		fView;
			SettingsContext*	fContext;
			BMessage			fOpened;
			BMessage			fTouched;
			bool				fHasOpenedGuest;
			bool				fOpenedGuest;
			bool				fGuestTouched;
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
			bool				_SaveName();
			void				_SaveEmail();
			void				_SetType(BMessage* message);
			void				_SetPicture(BMessage* message);
			void				_RemovePicture();
			void				_DisableOrEnable();
			void				_Delete();
			void				_Password();
			void				_ToggleGuest();
			void				_OpenShared();
			bool				_LastAdmin(const AccountInfo& info) const;

			AccountsModule*		fModule;
			BGroupView*			fLeft;
			BStringView*		fListCaption;
			BStringView*		fGuestCaption;
			BCheckBox*			fGuestSwitch;
			AccountWrapLabel*	fGuestHint;
			BStringView*		fSharedTitle;
			BStringView*		fSharedPath;
			BButton*			fSharedOpen;
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
			BButton*			fRemovePicture;
			AccountTextField*	fName;
			AccountTextField*	fEmail;
			BMenuField*			fType;
			BMenuItem*			fTypeAdmin;
			BMenuItem*			fTypeStandard;
			BButton*			fPassword;
			BFilePanel*			fPanel;
			AccountInfo			fShown;
			BString				fShownEmail;
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
	scroll->SetExplicitMinSize(BSize(B_SIZE_UNSET, px(52) * 3 + 4));
	fAdd = new BButton("add", B_TRANSLATE("Add account…"),
		new BMessage(kMsgAdd));
	fDisable = new BButton("disable", B_TRANSLATE("Disable account"),
		new BMessage(kMsgDisable));
	fDelete = new BButton("delete", B_TRANSLATE("Delete account…"),
		new BMessage(kMsgDelete));

	fGuestCaption = caption("guest caption", B_TRANSLATE("GUEST AND SHARING"));
	fGuestSwitch = new BCheckBox("guest switch",
		B_TRANSLATE("Allow guests to sign in to this computer"),
		new BMessage(kMsgGuestToggle));
	fGuestSwitch->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	fGuestHint = new AccountWrapLabel("guest hint",
		B_TRANSLATE("No password. Everything is deleted at log out."));
	fGuestHint->SetTextColor(mix_color(ui_color(B_DOCUMENT_BACKGROUND_COLOR),
		ui_color(B_DOCUMENT_TEXT_COLOR), 140));

	BBitmap* folderBitmap = r5_mime_icon_bitmap(
		"application/x-vnd.Be-directory", px(24));
	BIconView* folderIcon = new BIconView("shared icon", folderBitmap);
	delete folderBitmap;
	fSharedTitle = new BStringView("shared title",
		B_TRANSLATE("Shared folder"));
	fSharedPath = new BStringView("shared path", kSharedFolderPath);
	fSharedPath->SetHighColor(mix_color(ui_color(B_DOCUMENT_BACKGROUND_COLOR),
		ui_color(B_DOCUMENT_TEXT_COLOR), 140));
	fSharedOpen = new BButton("shared open", B_TRANSLATE("Open"),
		new BMessage(kMsgOpenShared));

	BCard* guestCard = new BCard("guest card", BAppearanceKit::B_CARD_DOCUMENT);
	BGroupLayout* guestLayout = (BGroupLayout*)guestCard->GetLayout();
	guestLayout->SetSpacing(px(6));
	BLayoutBuilder::Group<>(guestLayout)
		.SetInsets(px(12), px(8), px(12), px(8))
		.AddGroup(B_VERTICAL, px(2))
			.Add(fGuestSwitch)
			.Add(fGuestHint)
		.End()
		.Add(new BSeparatorView(B_HORIZONTAL))
		.AddGroup(B_HORIZONTAL, px(8))
			.Add(folderIcon)
			.AddGroup(B_VERTICAL, 0)
				.Add(fSharedTitle)
				.Add(fSharedPath)
			.End()
			.AddGlue()
			.Add(fSharedOpen)
		.End();

	fListCaption = caption("accounts caption", B_TRANSLATE("ACCOUNTS"));
	fLeft = new BGroupView(B_VERTICAL, px(6));
	BLayoutBuilder::Group<>(fLeft)
		.Add(fListCaption)
		.Add(scroll)
		.AddGroup(B_HORIZONTAL, px(6))
			.Add(fAdd)
			.Add(fDisable)
			.Add(fDelete)
		.End()
		.AddStrut(px(4))
		.Add(fGuestCaption)
		.Add(guestCard)
		.AddGlue();
	fLeft->SetExplicitPreferredSize(BSize(px(300), B_SIZE_UNSET));

	// Right: the card of the chosen account.
	fCaption = caption("card caption", " ");
	fPicture = new AccountPicture(px(64));
	fHeadName = new BStringView("head name", "");
	fHeadLine = new BStringView("head line", "");
	fHeadLine->SetHighColor(mix_color(ui_color(B_DOCUMENT_BACKGROUND_COLOR),
		ui_color(B_DOCUMENT_TEXT_COLOR), 140));
	fChoose = new BButton("choose", B_TRANSLATE("Choose picture…"),
		new BMessage(kMsgChoose));
	fRemovePicture = new BButton("remove picture", B_TRANSLATE("Remove picture"),
		new BMessage(kMsgRemovePicture));

	fName = new AccountTextField("name", new BMessage(kMsgName));
	fEmail = new AccountTextField("email", new BMessage(kMsgEmail));
	fEmail->SetPlaceholder(B_TRANSLATE("name@example.com"));
	fName->SetCompact(true);
	fEmail->SetCompact(true);
	BPopUpMenu* menu = new BPopUpMenu("type");
	BMessage* adminMessage = new BMessage(kMsgType);
	adminMessage->AddBool("admin", true);
	fTypeAdmin = new BMenuItem(B_TRANSLATE("Administrator"), adminMessage);
	BMessage* standardMessage = new BMessage(kMsgType);
	standardMessage->AddBool("admin", false);
	fTypeStandard = new BMenuItem(B_TRANSLATE("Standard"), standardMessage);
	menu->AddItem(fTypeAdmin);
	menu->AddItem(fTypeStandard);
	fType = new BMenuField("type field", NULL, menu);
	fPassword = new BButton("password", B_TRANSLATE("Change password…"),
		new BMessage(kMsgPassword));

	AccountWrapLabel* note = new AccountWrapLabel("card note",
		B_TRANSLATE("Changes are saved as you make them. The last administrator "
		"cannot become a standard account."));
	note->SetTextColor(mix_color(ui_color(B_DOCUMENT_BACKGROUND_COLOR),
		ui_color(B_DOCUMENT_TEXT_COLOR), 140));

	BStringView* nameLabel = new BStringView("name label",
		B_TRANSLATE("Name"));
	BStringView* emailLabel = new BStringView("email label",
		B_TRANSLATE("Email"));
	BStringView* typeLabel = new BStringView("type label",
		B_TRANSLATE("Account type"));
	BStringView* passwordLabel = new BStringView("password label",
		B_TRANSLATE("Password"));

	fPicture->SetExplicitAlignment(
		BAlignment(B_ALIGN_LEFT, B_ALIGN_VERTICAL_CENTER));

	fCard = new BCard("account card", BAppearanceKit::B_CARD_DOCUMENT);
	BGroupLayout* cardLayout = (BGroupLayout*)fCard->GetLayout();
	cardLayout->SetSpacing(px(10));
	BLayoutBuilder::Group<>(cardLayout)
		.SetInsets(px(18), px(14), px(18), px(14))
		.AddGrid(px(14), px(6))
			.Add(fPicture, 0, 0)
			.AddGroup(B_VERTICAL, px(3), 1, 0)
				.AddGlue()
				.Add(fHeadName)
				.Add(fHeadLine)
				.AddStrut(px(2))
				.AddGroup(B_HORIZONTAL)
					.Add(fChoose)
					.Add(fRemovePicture)
					.AddGlue()
				.End()
				.AddStrut(px(4))
				.AddGlue()
			.End()
			.Add(nameLabel, 0, 1)
			.Add(fName, 1, 1)
			.Add(emailLabel, 0, 2)
			.Add(fEmail, 1, 2)
			.Add(typeLabel, 0, 3)
			.Add(fType, 1, 3)
			.Add(passwordLabel, 0, 4)
			.AddGroup(B_HORIZONTAL, 0, 1, 4)
				.Add(fPassword)
				.AddGlue()
			.End()
			.SetColumnWeight(1, 1.0f)
		.End()
		.Add(note);

	BCard* info = new BCard("standard info", BAppearanceKit::B_CARD_PANEL);
	AccountWrapLabel* infoText = new AccountWrapLabel("standard info text",
		B_TRANSLATE("A standard account sees only its own card (name, email, "
		"picture, password). The account list, account types and the guest "
		"wait for an administrator's unlock."));
	infoText->SetTextColor(mix_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		ui_color(B_PANEL_TEXT_COLOR), 160));
	BLayoutBuilder::Group<>((BGroupLayout*)info->GetLayout())
		.SetInsets(px(14), px(8), px(14), px(8))
		.Add(infoText);

	BGroupView* right = new BGroupView(B_VERTICAL, px(6));
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
	fGuestCaption->SetFont(&captionFont);
	fCaption->SetFont(&captionFont);
	BFont headFont = sized(r5_settings_bold_font(), 15.0f);
	fHeadName->SetFont(&headFont);
	BFont lineFont = sized(r5_settings_plain_font(), 11.0f);
	fHeadLine->SetFont(&lineFont);
	fSharedPath->SetFont(&lineFont);
	BFont sharedBold = sized(r5_settings_bold_font(), 12.5f);
	fSharedTitle->SetFont(&sharedBold);
	fList->SetTarget(this);
	fAdd->SetTarget(this);
	fDisable->SetTarget(this);
	fDelete->SetTarget(this);
	fGuestSwitch->SetTarget(this);
	fSharedOpen->SetTarget(this);
	fChoose->SetTarget(this);
	fRemovePicture->SetTarget(this);
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

	BMessage guestReq(kMsgAdminGetGuestEnabled);
	BMessage guestReply;
	BString guestErr;
	bool guestEnabled = false;
	if (account_ask_login(guestReq, guestReply, guestErr) == B_OK) {
		guestEnabled = guestReply.GetBool("enabled", false);
		if (fModule != NULL)
			fModule->RememberGuest(guestEnabled);
	}
	fGuestSwitch->SetValue(guestEnabled ? B_CONTROL_ON : B_CONTROL_OFF);

	BMessage request(kMsgAdminListAccounts);
	BMessage reply;
	BString error;
	if (account_ask_login(request, reply, error) != B_OK) {
		fHasShown = false;
		BString text(B_TRANSLATE("Accounts could not be read: %error%"));
		text.ReplaceFirst("%error%", error.String());
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
	// A Name or Email typed for the account on the card is saved before
	// another one is shown: its message may come after the selection.
	bool renamed = false;
	if (fHasShown && (item == NULL || item->Info().name != fShown.name)) {
		renamed = _SaveName();
		_SaveEmail();
	}
	if (renamed && item != NULL) {
		Reload(item->Info().name.String());
		return;
	}
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
	line << info.name << kDot << (info.admin ? B_TRANSLATE("Administrator")
		: B_TRANSLATE("Standard"));
	if (info.disabled)
		line << kDot << B_TRANSLATE("disabled");
	if (info.home.Length() > 0) {
		BString home(B_TRANSLATE("home %path%"));
		home.ReplaceFirst("%path%", info.home.String());
		line << kDot << home;
	}
	fHeadLine->SetText(line.String());

	fName->SetText(info.real.String());
	fName->Message()->RemoveName("name");
	fName->Message()->AddString("name", info.name.String());
	fEmail->SetText("");
	fShownEmail = "";
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
			fShownEmail = email;
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
	fRemovePicture->SetEnabled(edit && fPicture->Picture() != NULL);
	fPassword->SetEnabled(edit);
	fType->SetEnabled(shown && fManaging && !fShown.first
		&& !(fShown.admin && _LastAdmin(fShown)));

	fAdd->SetEnabled(fManaging);
	fGuestSwitch->SetEnabled(fManaging);
	bool other = shown && fManaging && !fShown.first && !self;
	fDisable->SetLabel(shown && fShown.disabled ? B_TRANSLATE("Enable account")
		: B_TRANSLATE("Disable account"));
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
		case B_COLORS_UPDATED:
		{
			rgb_color muted = mix_color(ui_color(B_DOCUMENT_BACKGROUND_COLOR),
				ui_color(B_DOCUMENT_TEXT_COLOR), 140);
			fHeadLine->SetHighColor(muted);
			fGuestHint->SetTextColor(muted);
			fSharedPath->SetHighColor(muted);
			fGuestSwitch->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
			BGroupView::MessageReceived(message);
			break;
		}
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
			if (fHasShown && fShown.name == message->GetString("name", "")
				&& _SaveName())
				Reload(fShown.name.String());
			break;
		case kMsgEmail:
			if (fHasShown && fShown.name == message->GetString("name", ""))
				_SaveEmail();
			break;
		case kMsgType:
			_SetType(message);
			break;
		case kMsgPassword:
			_Password();
			break;
		case kMsgGuestToggle:
			_ToggleGuest();
			break;
		case kMsgOpenShared:
			_OpenShared();
			break;
		case kMsgRemovePicture:
			_RemovePicture();
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


// Saves a changed Name of the account on the card. True when it changed.
bool
AccountsView::_SaveName()
{
	if (!fHasShown)
		return false;
	BString real(fName->Text());
	if (real == fShown.real)
		return false;
	BString name(fShown.name);
	BMessage request(kMsgAdminSetProfile);
	request.AddString("name", name.String());
	request.AddString("real", real.String());
	BMessage reply;
	BString error;
	if (account_ask_login(request, reply, error) != B_OK) {
		show_error(error.Length() > 0 ? error.String()
			: B_TRANSLATE("The name could not be saved."));
		fName->SetText(fShown.real.String());
		return false;
	}
	fShown.real = real;
	if (fModule != NULL)
		fModule->Touched(name.String());
	return true;
}


void
AccountsView::_SaveEmail()
{
	if (!fHasShown)
		return;
	BString email(fEmail->Text());
	if (email == fShownEmail)
		return;
	BString name(fShown.name);
	BMessage request(kMsgAdminSetProfile);
	request.AddString("name", name.String());
	request.AddString("email", email.String());
	BMessage reply;
	BString error;
	if (account_ask_login(request, reply, error) != B_OK) {
		show_error(error.Length() > 0 ? error.String()
			: B_TRANSLATE("The email address could not be saved."));
		fEmail->SetText(fShownEmail.String());
		return;
	}
	fShownEmail = email;
	if (fModule != NULL)
		fModule->Touched(name.String());
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
			: B_TRANSLATE("The account type could not be changed."));
		(fShown.admin ? fTypeAdmin : fTypeStandard)->SetMarked(true);
		return;
	}
	if (fModule != NULL)
		fModule->Touched(name.String());
	// Your own type may have changed: the window asks Login about the
	// lock again.
	if (Window() != NULL)
		Window()->PostMessage(kMsgAdminLockChanged);
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
			: B_TRANSLATE("The picture could not be saved."));
		return;
	}
	if (fModule != NULL)
		fModule->Touched(name);
	Reload(name);
}


/*!	Back to the initials, as "Remove picture" in the Users program
	(issue #4615). Login removes the stored picture.
*/
void
AccountsView::_RemovePicture()
{
	if (!fHasShown || fPicture->Picture() == NULL)
		return;
	bool self = fShown.IsSelf();
	if (!self && !fManaging)
		return;
	BString name(fShown.name);
	BMessage request(kMsgAdminSetProfile);
	request.AddString("name", name.String());
	request.AddBool("clear_picture", true);
	BMessage reply;
	BString error;
	if (account_ask_login(request, reply, error) != B_OK) {
		show_error(error.Length() > 0 ? error.String()
			: B_TRANSLATE("The picture could not be removed."));
		return;
	}
	if (fModule != NULL)
		fModule->Touched(name.String());
	Reload(name.String());
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
			: B_TRANSLATE("The account could not be changed."));
	} else if (fModule != NULL)
		fModule->Touched(name.String());
	Reload(name.String());
}


void
AccountsView::_Delete()
{
	if (!fHasShown || !fManaging || fShown.first || fShown.IsSelf())
		return;
	BString name(fShown.name);
	BString text(B_TRANSLATE("Delete the account of %name%?\n\nKeep files "
		"leaves the home folder %home% on the disk. Delete files removes it, "
		"with everything in it."));
	text.ReplaceFirst("%name%", fShown.Shown());
	text.ReplaceFirst("%home%", fShown.home.String());
	BAlert* alert = new BAlert(B_TRANSLATE("Delete account"), text.String(),
		B_TRANSLATE("Cancel"), B_TRANSLATE("Keep files"),
		B_TRANSLATE("Delete files"), B_WIDTH_AS_USUAL, B_WARNING_ALERT);
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
			: B_TRANSLATE("The account could not be deleted."));
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


void
AccountsView::_ToggleGuest()
{
	if (!fManaging)
		return;
	bool enabled = fGuestSwitch->Value() == B_CONTROL_ON;
	BMessage request(kMsgAdminSetGuestEnabled);
	request.AddBool("enabled", enabled);
	BMessage reply;
	BString error;
	if (account_ask_login(request, reply, error) != B_OK) {
		show_error(error.Length() > 0 ? error.String()
			: B_TRANSLATE("The guest setting could not be changed."));
		fGuestSwitch->SetValue(enabled ? B_CONTROL_OFF : B_CONTROL_ON);
		return;
	}
	if (fModule != NULL)
		fModule->GuestTouched();
}


void
AccountsView::_OpenShared()
{
	entry_ref ref;
	if (get_ref_for_path(kSharedFolderPath, &ref) != B_OK) {
		show_error(B_TRANSLATE("The Shared folder could not be found."));
		return;
	}
	BMessage open(B_REFS_RECEIVED);
	open.AddRef("refs", &ref);
	BMessenger tracker("application/x-vnd.Be-TRAK");
	if (tracker.SendMessage(&open) != B_OK)
		show_error(B_TRANSLATE("Tracker could not open the Shared folder."));
}


// #pragma mark - AccountsModule


AccountsModule::AccountsModule()
	:
	fView(NULL),
	fContext(NULL),
	fHasOpenedGuest(false),
	fOpenedGuest(false),
	fGuestTouched(false)
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
	fHasOpenedGuest = false;
	fGuestTouched = false;
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


void
AccountsModule::RememberGuest(bool enabled)
{
	if (fHasOpenedGuest)
		return;
	fHasOpenedGuest = true;
	fOpenedGuest = enabled;
}


void
AccountsModule::GuestTouched()
{
	fGuestTouched = true;
	if (fContext != NULL)
		fContext->NotifyChanged(Identifier());
}


bool
AccountsModule::IsModified() const
{
	return fTouched.CountNames(B_ANY_TYPE) > 0 || fGuestTouched;
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
	bool adminTypeChanged = false;
	char* name = NULL;
	type_code type;

	// Pass 1: restore profiles, and re-enable / re-promote accounts before
	// any demotion or disable so we never hit "last administrator".
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
		bool wasAdmin = values.GetBool("admin", false);
		if (wasAdmin && !list.GetBool("admin", index, false)) {
			BMessage request(kMsgAdminSetAccountType);
			request.AddString("name", name);
			request.AddBool("admin", true);
			if (account_ask_login(request, reply, text) != B_OK)
				failed << "\n" << name << ": " << text;
			else
				adminTypeChanged = true;
		}
		bool wasDisabled = values.GetBool("disabled", false);
		if (!wasDisabled && list.GetBool("disabled", index, false)) {
			BMessage request(kMsgAdminSetDisabled);
			request.AddString("name", name);
			request.AddBool("disable", false);
			if (account_ask_login(request, reply, text) != B_OK)
				failed << "\n" << name << ": " << text;
		}
	}

	// Pass 2: demotions and disables, non-self first so demoting yourself
	// cannot block reverting another account.
	if (managing) {
		for (int32 pass = 0; pass < 2; pass++) {
			bool wantSelf = (pass == 1);
			for (int32 i = 0; fTouched.GetInfo(B_BOOL_TYPE, i, &name, &type)
					== B_OK; i++) {
				BMessage values;
				if (fOpened.FindMessage(name, &values) != B_OK)
					continue;
				int32 index = -1;
				const char* listed = NULL;
				for (int32 j = 0; list.FindString("name", j, &listed) == B_OK;
						j++) {
					if (strcmp(listed, name) == 0) {
						index = j;
						break;
					}
				}
				if (index < 0)
					continue;
				bool self = (uid_t)list.GetInt32("uid", index, -1) == getuid();
				if (self != wantSelf)
					continue;

				BMessage reply;
				BString text;
				bool wasAdmin = values.GetBool("admin", false);
				if (!wasAdmin && list.GetBool("admin", index, false)) {
					BMessage request(kMsgAdminSetAccountType);
					request.AddString("name", name);
					request.AddBool("admin", false);
					if (account_ask_login(request, reply, text) != B_OK)
						failed << "\n" << name << ": " << text;
					else
						adminTypeChanged = true;
				}
				bool wasDisabled = values.GetBool("disabled", false);
				if (wasDisabled && !list.GetBool("disabled", index, false)) {
					BMessage request(kMsgAdminSetDisabled);
					request.AddString("name", name);
					request.AddBool("disable", true);
					if (account_ask_login(request, reply, text) != B_OK)
						failed << "\n" << name << ": " << text;
				}
			}
		}
	}

	if (managing && fGuestTouched && fHasOpenedGuest) {
		BMessage request(kMsgAdminSetGuestEnabled);
		request.AddBool("enabled", fOpenedGuest);
		BMessage reply;
		BString text;
		if (account_ask_login(request, reply, text) != B_OK) {
			BString guest(B_TRANSLATE("Guest: %error%"));
			guest.ReplaceFirst("%error%", text.String());
			failed << "\n" << guest;
		}
	}

	fTouched.MakeEmpty();
	fGuestTouched = false;
	if (adminTypeChanged && fView != NULL && fView->Window() != NULL)
		fView->Window()->PostMessage(kMsgAdminLockChanged);
	if (fView != NULL)
		fView->Reload();
	if (failed.Length() > 0) {
		BString message(B_TRANSLATE("Not everything could be put back:"));
		message << failed;
		show_error(message.String());
	}
	return B_OK;
}


// The add-on settings_accounts (distro/settings/ModuleAPI.md): every
// Settings page is an add-on of its own.
extern "C" _EXPORT int32
settings_api_version()
{
	return kSettingsApiVersion;
}


extern "C" _EXPORT SettingsModule*
instantiate_settings_module()
{
	return new AccountsModule();
}
