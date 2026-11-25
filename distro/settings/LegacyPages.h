/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef R5_SETTINGS_LEGACY_H
#define R5_SETTINGS_LEGACY_H


class SettingsModule;


// The launcher page with this id ("sound", "keyboard", "mouse" or
// "printers"), or NULL. Each launcher is an add-on of its own
// (LegacySound.cpp and the others).
SettingsModule* create_legacy_module(const char* identifier);


#endif
