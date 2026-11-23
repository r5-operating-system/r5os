/*
 * Copyright 2010, Stephan Aßmus <superstippi@gmx.de>.
 * Copyright 2020, Panagiotis "Ivory" Vasilopoulos <git@n0toose.net>
 * All rights reserved. Distributed under the terms of the MIT License.
 */
/*
 * R5OS modifications in this file are MIT, like the original work. They are
 * wrapped in SPDX-Snippet markers. The copyright and license notice above still
 * cover the original work.
 */
#ifndef BOOT_PROMPT_WINDOW_H
#define BOOT_PROMPT_WINDOW_H


#include <Window.h>


enum {
	MSG_LANGUAGE_SELECTED	= 'lngs',
	MSG_KEYMAP_SELECTED	= 'kmps',
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	MSG_THEME_SELECTED	= 'thms',
// SPDX-SnippetEnd
};


class BButton;
class BLanguage;
class BListView;
class BMenuItem;
class BMenuField;
class BStringView;
class BTextView;


class BootPromptWindow : public BWindow {
public:
								BootPromptWindow();

			bool				QuitRequested();
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_InitCatalog(bool saveSettings);
			void				_UpdateStrings();
			void				_UpdateKeymapsMenu();
			void				_PopulateLanguages();
			void				_PopulateKeymaps();
			void				_ActivateKeymap(const BMessage* message) const;
			status_t			_GetCurrentKeymapRef(entry_ref& ref) const;
			BMenuItem*			_KeymapItemForLanguage(
									BLanguage& language) const;

private:
			BTextView*			fInfoTextView;
			BStringView*		fLanguagesLabelView;
			BStringView*		fKeymapsMenuLabel;
			BListView*			fLanguagesListView;
			BMenuField*			fKeymapsMenuField;
			BMenuItem*			fDefaultKeymapItem;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			BMenuField*			fThemeMenuField;
// SPDX-SnippetEnd
			BButton*			fDesktopButton;
			BButton*			fInstallerButton;
};


#endif // BOOT_PROMPT_WINDOW_H
