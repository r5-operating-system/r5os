/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef R5_SETTINGS_LEGACY_H
#define R5_SETTINGS_LEGACY_H


class SettingsModule;


// A launcher page: an Open button for a preference program that stays a
// window of its own. Each launcher is an add-on (LegacySound.cpp and the
// others), which passes its own translated name, description and keywords.
SettingsModule* create_legacy_module(const char* identifier, const char* name,
	const char* category, const char* description, const char* keywords,
	const char* signature);


#endif
