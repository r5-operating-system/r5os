/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * The add-on settings_printers: the Printers launcher page (LegacyPages.cpp).
 */


#include "LegacyPages.h"
#include "SettingsModule.h"


extern "C" _EXPORT int32
settings_api_version()
{
	return kSettingsApiVersion;
}


extern "C" _EXPORT SettingsModule*
instantiate_settings_module()
{
	return create_legacy_module("printers");
}
