/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * The add-on settings_mouse: the Mouse launcher page (LegacyPages.cpp).
 */


#include "LegacyPages.h"
#include "SettingsModule.h"

#include <Catalog.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Settings: Mouse"


extern "C" _EXPORT int32
settings_api_version()
{
	return kSettingsApiVersion;
}


extern "C" _EXPORT SettingsModule*
instantiate_settings_module()
{
	return create_legacy_module("mouse", B_TRANSLATE("Mouse"), "devices",
		B_TRANSLATE("Mouse speed, focus, and the scroll wheel."),
		B_TRANSLATE_COMMENT("input pointer focus", "Search words, separated by "
			"spaces. Keep the English words and add words in your language."),
		"application/x-vnd.Haiku-Input");
}
