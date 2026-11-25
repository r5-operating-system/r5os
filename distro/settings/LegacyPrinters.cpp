/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * The add-on settings_printers: the Printers launcher page (LegacyPages.cpp).
 */


#include "LegacyPages.h"
#include "SettingsModule.h"

#include <Catalog.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Settings: Printers"


extern "C" _EXPORT int32
settings_api_version()
{
	return kSettingsApiVersion;
}


extern "C" _EXPORT SettingsModule*
instantiate_settings_module()
{
	return create_legacy_module("printers", B_TRANSLATE("Printers"), "devices",
		B_TRANSLATE("Printers and the print queue."),
		B_TRANSLATE_COMMENT("print printer", "Search words, separated by "
			"spaces. Keep the English words and add words in your language."),
		"application/x-vnd.Be-PRNT");
}
