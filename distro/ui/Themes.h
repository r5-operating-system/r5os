/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Themes (docs/design/THEMING.md, docs/LIBR5.md "Themes").
 *
 * A theme is a saved combination of parts that exist on their own: the
 * window decorator, the control look add-on, the scroll bar style and
 * arrows, the Deskbar style, the mode (light or dark) with an accent colour,
 * and the window effects. There is no drawing engine: applying a theme sets
 * each part the way Appearance and Desktop and Deskbar already do, and the
 * colours go through ui_color.
 */
#ifndef _R5_THEMES_H
#define _R5_THEMES_H


#include <GraphicsDefs.h>
#include <StorageDefs.h>
#include <SupportDefs.h>


class BMessage;

namespace BAppearanceKit {


enum theme_mode {
	B_THEME_LIGHT	= 0,
	B_THEME_DARK	= 1
};

// The parts of a theme, a bit set.
enum {
	B_THEME_WINDOWS		= 0x00000001,
	B_THEME_CONTROLS	= 0x00000002,
	B_THEME_SCROLL_BARS	= 0x00000004,
	B_THEME_DESKBAR		= 0x00000008,
	B_THEME_COLORS		= 0x00000010,
		// the mode and the accent
	B_THEME_EFFECTS		= 0x00000020,
	B_THEME_ALL_PARTS	= 0x0000003f
};

// Deskbar styles and layouts, the values of Desktop and Deskbar.
enum {
	B_THEME_DESKBAR_R5			= 0,
	B_THEME_DESKBAR_CLASSIC		= 1
};

enum {
	B_THEME_DESKBAR_VERTICAL	= 0,
	B_THEME_DESKBAR_COMPACT		= 1,
	B_THEME_DESKBAR_HORIZONTAL	= 2
};

struct theme_info {
	char		name[B_FILE_NAME_LENGTH];
	char		base[B_FILE_NAME_LENGTH];
		// the theme this one started from ("R5 Modern" for a changed
		// R5 Modern)
	uint32		parts;
		// which parts the theme holds (B_THEME_*)

	char		decorator[B_FILE_NAME_LENGTH];
		// the add-on's file name; "" is Haiku's built-in Classic (Haiku)
	char		control_look[B_FILE_NAME_LENGTH];
		// the add-on's file name; "" is Haiku's built-in controls
	bool		modern_scroll_bars;
		// the R5 Modern scroll bars (only with the R5 Modern controls)
	bool		double_arrows;

	int32		deskbar_style;
	int32		deskbar_layout;
	bool		deskbar_rounded;
	bool		deskbar_floating;

	int32		mode;
		// theme_mode
	rgb_color	accent;
		// alpha 0: the colour set's own accent

	uint32		effects;
		// window_effects() bits
	int32		effect_strength;
		// window_effect_strength

	uint32		_reserved[16];
};

// The theme in use now: what was applied last, or, before any theme was
// applied, what the system shows (as "R5").
status_t	get_theme(theme_info* info);

// Applies \a parts of \a info, at once, and keeps it as the theme in use.
// Running programs take the new controls without a restart.
status_t	set_theme(const theme_info* info,
				uint32 parts = B_THEME_ALL_PARTS);

// The built-in themes first (R5 Modern, R5, Classic (Haiku)), then the
// saved ones by name.
int32		count_themes();
status_t	get_theme_at(int32 index, theme_info* info);
status_t	find_theme(const char* name, theme_info* info);
bool		is_built_in_theme(const char* name);

// Saves \a parts of \a info under info->name in
// ~/config/settings/r5/themes/. B_NOT_ALLOWED for a built-in name.
status_t	save_theme(const theme_info* info, uint32 parts);
status_t	remove_theme(const char* name);

// Whether \a theme shows the same as \a base, part by part (the theme page
// shows "R5 Modern (changed)" when it does not).
bool		theme_matches(const theme_info* theme, const theme_info* base);

// The ui_color set a theme sets, as set_ui_colors() takes it.
status_t	get_theme_colors(const theme_info* info, BMessage* colors);

// The accent colours of the theme page, for \a mode.
int32		count_theme_accents();
rgb_color	theme_accent_at(int32 index, int32 mode);
const char*	theme_accent_name(int32 index);


}	// namespace BAppearanceKit


#endif	// _R5_THEMES_H
