/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * The add-on settings_sound: the Sound launcher page (LegacyPages.cpp).
 */


#include "LegacyPages.h"
#include "SettingsModule.h"

#include <Catalog.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Settings: Sound"


extern "C" _EXPORT int32
settings_api_version()
{
	return kSettingsApiVersion;
}


extern "C" _EXPORT SettingsModule*
instantiate_settings_module()
{
	return create_legacy_module("sound", B_TRANSLATE("Sound"), "system",
		B_TRANSLATE("Event sounds and the output device."),
		B_TRANSLATE_COMMENT("audio volume media", "Search words, separated by "
			"spaces. Keep the English words and add words in your language."),
		"application/x-vnd.Haiku-Sounds");
}
