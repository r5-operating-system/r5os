/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * The add-on settings_keyboard: the Keyboard launcher page (LegacyPages.cpp).
 */


#include "LegacyPages.h"
#include "SettingsModule.h"

#include <Catalog.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Settings: Keyboard"


extern "C" _EXPORT int32
settings_api_version()
{
	return kSettingsApiVersion;
}


extern "C" _EXPORT SettingsModule*
instantiate_settings_module()
{
	return create_legacy_module("keyboard", B_TRANSLATE("Keyboard"), "devices",
		B_TRANSLATE("Keymap and the typing repeat rate."),
		B_TRANSLATE_COMMENT("keymap layout keys", "Search words, separated by "
			"spaces. Keep the English words and add words in your language."),
		"application/x-vnd.Haiku-Keymap");
}
