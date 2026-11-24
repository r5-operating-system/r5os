/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Users and Accounts in Settings: the "Add account" and "Change password"
 * windows, and the small pieces the page and those windows share.
 */
#ifndef R5_SETTINGS_ACCOUNT_DIALOGS_H
#define R5_SETTINGS_ACCOUNT_DIALOGS_H


#include <Control.h>
#include <Messenger.h>
#include <String.h>
#include <Window.h>


class BBitmap;
class BButton;
class BFilePanel;
class BStringView;
class BTextView;
struct entry_ref;

namespace BAppearanceKit {
	class BPasswordControl;
}


// Sent to the page by the windows below.
const uint32 kMsgAccountAdded = 'acAd';
	// "name": the new account
const uint32 kMsgAccountPasswordChanged = 'acPw';


// One mockup pixel: the mockups are drawn for a 12 point system font.
float			account_unit();

// Login's admin port (r5_admin_send(), libr5admin.so). B_OK, or the error
// with Login's text in `error`. Password fields in `request` are wiped
// after the send.
status_t		account_ask_login(BMessage& request, BMessage& reply,
					BString& error);
void			account_wipe(BString& text);

// The picture of an account, as Login stores it ("picture" in the profile).
BBitmap*		account_picture_from_blob(const void* data, ssize_t size);
status_t		account_add_picture_blob(const BBitmap* picture,
					BMessage& message);
// A file the translators can read, cut to a square and scaled to the size
// Login stores. NULL and a sentence in `error` when it is not a picture.
BBitmap*		account_load_picture(const entry_ref& ref, BString& error);

// The round picture of an account in `circle`: the photo when there is
// one, otherwise the person icon on r5_avatar_draw()'s plate.
void			account_draw_picture(BView* view, BRect circle,
					const BBitmap* photo, bool admin);

BString			account_suggest_username(const char* name);
bool			account_valid_username(const char* name);

// Text that wraps to the width the layout gives it.
class AccountWrapLabel : public BView {
public:
								AccountWrapLabel(const char* name,
									const char* text, bool small = true);

			void				SetText(const char* text);
			void				SetTextColor(rgb_color color);
			void				SetBold(bool bold);

	virtual	void				AttachedToWindow();
	virtual	void				Draw(BRect updateRect);
	virtual	void				FrameResized(float width, float height);
	virtual	BSize				MinSize();
	virtual	BSize				PreferredSize();
	virtual	bool				HasHeightForWidth();
	virtual	void				GetHeightForWidth(float width, float* min,
									float* max, float* preferred);

private:
			BFont				_Font() const;
			int32				_Lines(float width, BString* lines) const;

			BString				fText;
			rgb_color			fColor;
			bool				fHasColor;
			bool				fSmall;
			bool				fBold;
};


// A text field with an optional gray placeholder and a gray hint at its
// right edge, in the same frame as BAppearanceKit::BPasswordControl, so the
// fields of a window look alike. Its message is sent on Enter and when the
// focus leaves a changed field; the modification message on every change.
class AccountTextField : public BControl {
public:
								AccountTextField(const char* name,
									BMessage* message = NULL);
	virtual						~AccountTextField();

			const char*			Text() const;
			void				SetText(const char* text);
			BTextView*			TextView() const { return fTextView; }
			void				SetPlaceholder(const char* text);
			const char*			Placeholder() const
									{ return fPlaceholder.String(); }
			void				SetRightHint(const char* text);
			void				SetModificationMessage(BMessage* message);
			// A lower field (30 instead of 38 mockup pixels), for a form on a
			// Settings page beside menus and buttons.
			void				SetCompact(bool compact);
			void				TextChanged();
			void				EditingDone();

	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				FrameResized(float width, float height);
	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MakeFocus(bool focus = true);
	virtual	void				SetEnabled(bool enabled);
	virtual	void				WindowActivated(bool active);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();

private:
			void				_LayoutText();
			void				_ApplyColors();

			BTextView*			fTextView;
			BString				fPlaceholder;
			BString				fRightHint;
			BString				fLastSent;
			BMessage*			fModification;
			bool				fCompact;
};


// Opens the "Add account" window. The page gets kMsgAccountAdded.
void	account_open_add_window(BWindow* parent, const BMessenger& page);

// Opens "Change password". `self`: the signed-in account changes its own
// password (with the current one). Otherwise an administrator sets a new
// password for another account, with the note about saved passwords.
void	account_open_password_window(BWindow* parent, const BMessenger& page,
			const char* name, const char* realName, bool admin, bool self,
			const BBitmap* photo);


#endif	// R5_SETTINGS_ACCOUNT_DIALOGS_H
