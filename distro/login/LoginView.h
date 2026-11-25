/*
 * Copyright 2008, François Revol, <revol@free.fr>. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _LOGINVIEW_H_
#define _LOGINVIEW_H_


#include <String.h>
#include <View.h>


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
class AvatarView;
class BadgeView;
class BBitmap;
class BButton;
class BGridView;
class BGroupView;
class BListView;
class BScrollView;
class BStringView;
class BTextControl;


namespace BAppearanceKit {
class BAccentButton;
class BCard;
class BIconView;
class BPasswordControl;
class BPasswordStrengthBar;
}


const uint32 kUserSelected = 'usel';
const uint32 kUserInvoked = 'uinv';
const uint32 kCardSelected = 'csel';


class LoginView : public BView {
public:
								LoginView();
	virtual						~LoginView();

	virtual	void				AttachedToWindow();
	virtual	void				AllAttached();
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				Pulse();
			void				PrepareForSignIn();
			void				FieldsChanged();

private:
			void				_FillUsers();
			void				_ShowSelected(const char* login,
									const char* displayName,
									const char* role);
			void				_SetFeedback(const char* text, bool error);
			void				_AttemptLogin();
			void				_EnableControls(bool enable);
			void				_ApplyColors();
			void				_ApplyGlyphs();
			void				_SetVisible(BView* view, bool visible);
			void				_UpdateSetup();
			void				_FocusField();

	BAppearanceKit::BCard*		fHeaderCard;
	BStringView*				fWelcomeView;
	BStringView*				fSubtitleView;

	BGroupView*					fUserSection;
	BStringView*				fSelectPersonLabel;
	BStringView*				fAccountCountLabel;
	BGridView*					fUserCardsGrid;
	BListView*					fUserList;
	BScrollView*				fUserScroll;

	BAppearanceKit::BCard*		fFormCard;
	BGroupView*					fSingleBanner;
	AvatarView*					fBannerAvatar;
	BStringView*				fBannerName;
	BStringView*				fBannerRole;
	BGroupView*					fActiveLine;
	BStringView*				fActiveCaption;
	BStringView*				fActiveName;
	BadgeView*					fActiveBadge;
	BStringView*				fPasswordLabel;
	BAppearanceKit::BPasswordControl* fPasswordControl;
	BGroupView*					fPasswordGroup;
	BView*						fGuestNote;
	// Accounts step 5: the first-start screen of an installed R5 (an
	// account without a password sets one), and the note of Try R5.
	BGroupView*					fSetupGroup;
	BTextControl*				fSetupName;
	BAppearanceKit::BPasswordControl* fSetupPassword;
	BAppearanceKit::BPasswordControl* fSetupRepeat;
	BAppearanceKit::BPasswordStrengthBar* fSetupStrength;
	BStringView*				fSetupHint;
	BView*						fLiveNote;
	bool						fSetupMode;
	bool						fLiveMode;
	bool						fBannerAdmin;
	BAppearanceKit::BAccentButton* fLoginButton;
	BStringView*				fInfoView;
	bool						fInfoIsError;

	BAppearanceKit::BIconView*	fClockIcon;
	BStringView*				fClockView;
	BButton*					fRebootButton;
	BButton*					fHaltButton;

	BBitmap*					fCardAvatar;
	BBitmap*					fListAvatar;
	BBitmap*					fBannerIcon;
	BString						fSelectedLogin;
};
// SPDX-SnippetEnd


#endif	// _LOGINVIEW_H_
