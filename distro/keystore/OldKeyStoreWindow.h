/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Dialog shown at Sign in after an administrator password reset:
 * "Do you still know your old password?"
 */
#ifndef _R5_OLD_KEYSTORE_WINDOW_H
#define _R5_OLD_KEYSTORE_WINDOW_H


#include <Window.h>


class BButton;
class BStringView;
class BTextControl;
class KeyStoreServer;


class OldKeyStoreWindow : public BWindow {
public:
								OldKeyStoreWindow(KeyStoreServer* server);
	virtual						~OldKeyStoreWindow();

	virtual	void				MessageReceived(BMessage* message);

private:
			void				_TryUnlock();

			KeyStoreServer*		fServer;
			BTextControl*		fPassword;
			BStringView*		fErrorView;
			BButton*			fSkipButton;
			BButton*			fUnlockButton;
};


#endif	// _R5_OLD_KEYSTORE_WINDOW_H
