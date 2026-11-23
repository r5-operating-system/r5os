/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


/*!	Themes (docs/design/THEMING.md). A theme is a small settings file with
	the names of the parts and the colours; applying one sets every part the
	way its own page does (Appearance, Desktop and Deskbar), so nothing here
	draws. The file format is driver settings text, readable and short
	enough to fit in a mail:

		name "Evening"
		base "R5 Modern"
		windows "R5ModernDecorator"
		controls "R5ModernControlLook"
		scroll_bars modern
		arrows single
		deskbar_style r5
		deskbar_layout vertical
		deskbar_rounded true
		deskbar_floating true
		mode dark
		accent #0084ff
		shadow true
		glass true
		animations false
		strength balanced

	A key that is not there is a part the theme does not hold.
*/


#include "Themes.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Application.h>
#include <DecorInfo.h>
#include <DefaultColors.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <InterfaceDefs.h>
#include <InterfacePrivate.h>
#include <Message.h>
#include <Messenger.h>
#include <OS.h>
#include <Path.h>
#include <PathFinder.h>
#include <Roster.h>
#include <String.h>
#include <StringList.h>
#include <Window.h>
#include <driver_settings.h>

#include "WindowEffects.h"


namespace BAppearanceKit {


static const char* kThemesDirectory = "r5/themes";
static const char* kCurrentTheme = "r5/theme";
static const char* kDeskbarSignature = "application/x-vnd.Be-TSKB";
static const uint32 kSetDeskbarStyle = 'R5ds';
	// Deskbar's private code (DeskbarStyle.h kMsgSetStyle), the same one
	// the Desktop and Deskbar page sends; absolute values

static const char* kR5Modern = "R5 Modern";
static const char* kR5 = "R5";
static const char* kClassic = "Classic (Haiku)";

static const char* kR5ModernDecorator = "R5ModernDecorator";
static const char* kR5Decorator = "R5Decorator";
static const char* kR5ModernControlLook = "R5ModernControlLook";


static rgb_color
hex(uint32 value)
{
	rgb_color color;
	color.red = (value >> 16) & 0xff;
	color.green = (value >> 8) & 0xff;
	color.blue = value & 0xff;
	color.alpha = 255;
	return color;
}


// #pragma mark - accents


struct accent {
	const char*	name;
	uint32		light;
	uint32		dark;
};

// mockup A1 (the Accent swatches) and A2 (on dark each a little deeper, under IsLight() (the dark Blue is darker than the light one))
// The dark accents stay under Haiku's rgb_color::IsLight() line: a
// Brightness() (the HSP formula, sqrt(.299 r2 + .587 g2 + .114 b2)) of 127
// or less, so code that picks the text by it puts white text on them
// (external review 1.3). Each is the mockup's colour scaled to 124; the
// mockup's dark blue #2f95ff is 145.
static const accent kAccents[] = {
	{ "Blue",		0x0084ff, 0x287fda },
	{ "Teal",		0x0f9488, 0x12958a },
	{ "Purple",		0x7c4ddb, 0x855cd5 },
	{ "Pink",		0xd6457a, 0xbc4b75 },
	{ "Orange",		0xe07b1a, 0xae6721 },
	{ "Graphite",	0x6b7280, 0x767c88 }
};
static const int32 kAccentCount = sizeof(kAccents) / sizeof(kAccents[0]);


int32
count_theme_accents()
{
	return kAccentCount;
}


rgb_color
theme_accent_at(int32 index, int32 mode)
{
	if (index < 0 || index >= kAccentCount)
		index = 0;
	return hex(mode == B_THEME_DARK ? kAccents[index].dark
		: kAccents[index].light);
}


const char*
theme_accent_name(int32 index)
{
	if (index < 0 || index >= kAccentCount)
		return NULL;
	return kAccents[index].name;
}


// #pragma mark - colour sets


// Every colour a theme sets, so that a change from one set to another
// leaves nothing of the first. The desktop colour is the Wallpaper page's.
static const color_which kThemeColors[] = {
	B_PANEL_BACKGROUND_COLOR,
	B_PANEL_TEXT_COLOR,
	B_DOCUMENT_BACKGROUND_COLOR,
	B_DOCUMENT_TEXT_COLOR,
	B_CONTROL_BACKGROUND_COLOR,
	B_CONTROL_TEXT_COLOR,
	B_CONTROL_BORDER_COLOR,
	B_CONTROL_HIGHLIGHT_COLOR,
	B_CONTROL_MARK_COLOR,
	B_NAVIGATION_BASE_COLOR,
	B_MENU_BACKGROUND_COLOR,
	B_MENU_SELECTED_BACKGROUND_COLOR,
	B_MENU_ITEM_TEXT_COLOR,
	B_MENU_SELECTED_ITEM_TEXT_COLOR,
	B_MENU_SELECTED_BORDER_COLOR,
	B_LIST_BACKGROUND_COLOR,
	B_LIST_SELECTED_BACKGROUND_COLOR,
	B_LIST_ITEM_TEXT_COLOR,
	B_LIST_SELECTED_ITEM_TEXT_COLOR,
	B_SCROLL_BAR_THUMB_COLOR,
	B_WINDOW_TAB_COLOR,
	B_WINDOW_TEXT_COLOR,
	B_WINDOW_INACTIVE_TAB_COLOR,
	B_WINDOW_INACTIVE_TEXT_COLOR,
	B_WINDOW_BORDER_COLOR,
	B_WINDOW_INACTIVE_BORDER_COLOR,
	B_TOOL_TIP_BACKGROUND_COLOR,
	B_TOOL_TIP_TEXT_COLOR,
	B_STATUS_BAR_COLOR,
	B_LINK_TEXT_COLOR,
	B_LINK_HOVER_COLOR,
	B_LINK_VISITED_COLOR,
	B_LINK_ACTIVE_COLOR
};
static const int32 kThemeColorCount
	= sizeof(kThemeColors) / sizeof(kThemeColors[0]);


struct color_value {
	color_which	which;
	uint32		value;
};

// THEMING-CONTRACT / mockup A2, B1, B2: R5 Modern dark and R5 dark
static const color_value kDarkColors[] = {
	{ B_PANEL_BACKGROUND_COLOR,			0x1f232b },
	{ B_PANEL_TEXT_COLOR,				0xe6e9ef },
	{ B_DOCUMENT_BACKGROUND_COLOR,		0x15181e },
	{ B_DOCUMENT_TEXT_COLOR,			0xe6e9ef },
	{ B_CONTROL_BACKGROUND_COLOR,		0x2a2f39 },
	{ B_CONTROL_TEXT_COLOR,				0xe6e9ef },
	{ B_CONTROL_BORDER_COLOR,			0x454c59 },
	{ B_MENU_BACKGROUND_COLOR,			0x262b34 },
	{ B_MENU_ITEM_TEXT_COLOR,			0xe6e9ef },
	{ B_MENU_SELECTED_ITEM_TEXT_COLOR,	0xffffff },
	{ B_LIST_BACKGROUND_COLOR,			0x1b1f26 },
	{ B_LIST_ITEM_TEXT_COLOR,			0xe6e9ef },
	{ B_LIST_SELECTED_ITEM_TEXT_COLOR,	0xffffff },
	{ B_SCROLL_BAR_THUMB_COLOR,			0x4f5664 },
	{ B_WINDOW_TAB_COLOR,				0xe8b923 },
	{ B_WINDOW_TEXT_COLOR,				0x1a1a1a },
	{ B_WINDOW_INACTIVE_TAB_COLOR,		0x2e333d },
	{ B_WINDOW_INACTIVE_TEXT_COLOR,		0xa3abb8 },
	{ B_WINDOW_BORDER_COLOR,			0x4a515e },
	{ B_WINDOW_INACTIVE_BORDER_COLOR,	0x3a404c },
	{ B_TOOL_TIP_BACKGROUND_COLOR,		0x2a2f39 },
	{ B_TOOL_TIP_TEXT_COLOR,			0xe6e9ef }
};

// mockup A1: R5 Modern light; the rest is Haiku's factory set
static const color_value kModernLightColors[] = {
	{ B_PANEL_BACKGROUND_COLOR,			0xeef1f5 },
	{ B_PANEL_TEXT_COLOR,				0x141414 },
	{ B_DOCUMENT_BACKGROUND_COLOR,		0xffffff },
	{ B_DOCUMENT_TEXT_COLOR,			0x141414 },
	{ B_CONTROL_BACKGROUND_COLOR,		0xf7f8fa },
	{ B_CONTROL_TEXT_COLOR,				0x141414 },
	{ B_CONTROL_BORDER_COLOR,			0xb4bcc8 },
	{ B_MENU_BACKGROUND_COLOR,			0xf7f8fa },
		// menu mockup M1, M6: the opaque menu, the glass tint
	{ B_MENU_ITEM_TEXT_COLOR,			0x141414 },
	{ B_MENU_SELECTED_ITEM_TEXT_COLOR,	0xffffff },
	{ B_LIST_BACKGROUND_COLOR,			0xffffff },
	{ B_LIST_ITEM_TEXT_COLOR,			0x141414 },
	{ B_LIST_SELECTED_ITEM_TEXT_COLOR,	0xffffff },
	{ B_SCROLL_BAR_THUMB_COLOR,			0xb6bec9 }
};


static bool
modern_controls(const theme_info* info)
{
	return strcmp(info->control_look, kR5ModernControlLook) == 0;
}


status_t
get_theme_colors(const theme_info* info, BMessage* colors)
{
	if (info == NULL || colors == NULL)
		return B_BAD_VALUE;

	colors->MakeEmpty();

	bool dark = info->mode == B_THEME_DARK;
	bool modern = modern_controls(info);

	// start from Haiku's factory set (R5 and Classic (Haiku), light)
	rgb_color values[kThemeColorCount];
	for (int32 i = 0; i < kThemeColorCount; i++)
		values[i] = BPrivate::GetSystemColor(kThemeColors[i], false);

	const color_value* set = NULL;
	int32 setCount = 0;
	if (dark) {
		set = kDarkColors;
		setCount = sizeof(kDarkColors) / sizeof(kDarkColors[0]);
		// Haiku's own dark links (they read on a dark panel)
		for (int32 i = 0; i < kThemeColorCount; i++) {
			color_which which = kThemeColors[i];
			if (which == B_LINK_TEXT_COLOR || which == B_LINK_HOVER_COLOR
				|| which == B_LINK_VISITED_COLOR
				|| which == B_LINK_ACTIVE_COLOR) {
				values[i] = BPrivate::GetSystemColor(which, true);
			}
		}
	} else if (modern) {
		set = kModernLightColors;
		setCount = sizeof(kModernLightColors) / sizeof(kModernLightColors[0]);
	}

	for (int32 i = 0; i < setCount; i++) {
		for (int32 j = 0; j < kThemeColorCount; j++) {
			if (kThemeColors[j] == set[i].which)
				values[j] = hex(set[i].value);
		}
	}

	// The accent. R5 and Classic (Haiku) in light keep Haiku's grey
	// selection with black text; the R5 Modern and dark sets select with
	// the accent and white text.
	bool hasAccent = info->accent.alpha != 0;
	rgb_color accentColor = info->accent;
	accentColor.alpha = 255;
	if (!hasAccent && (dark || modern)) {
		accentColor = theme_accent_at(0, info->mode);
		hasAccent = true;
	}
	if (hasAccent) {
		for (int32 i = 0; i < kThemeColorCount; i++) {
			switch (kThemeColors[i]) {
				case B_CONTROL_HIGHLIGHT_COLOR:
				case B_CONTROL_MARK_COLOR:
				case B_NAVIGATION_BASE_COLOR:
				case B_STATUS_BAR_COLOR:
					values[i] = accentColor;
					break;
				case B_MENU_SELECTED_BACKGROUND_COLOR:
				case B_LIST_SELECTED_BACKGROUND_COLOR:
				case B_MENU_SELECTED_BORDER_COLOR:
					if (dark || modern)
						values[i] = accentColor;
					break;
				default:
					break;
			}
		}
	}

	for (int32 i = 0; i < kThemeColorCount; i++)
		colors->AddColor(ui_color_name(kThemeColors[i]), values[i]);
	return B_OK;
}


// #pragma mark - built-in themes


static void
init_theme(theme_info* info, const char* name)
{
	*info = theme_info();
	strlcpy(info->name, name, sizeof(info->name));
	strlcpy(info->base, name, sizeof(info->base));
	info->deskbar_style = B_THEME_DESKBAR_R5;
	info->deskbar_layout = B_THEME_DESKBAR_VERTICAL;
	info->deskbar_rounded = true;
	info->deskbar_floating = true;
	info->mode = B_THEME_LIGHT;
	info->effects = B_WINDOW_SHADOW | B_WINDOW_FROSTED_GLASS
		| B_WINDOW_ANIMATIONS;
	info->effect_strength = B_WINDOW_EFFECTS_BALANCED;
	// a built-in theme leaves the effects as they are
	info->parts = B_THEME_ALL_PARTS & ~B_THEME_EFFECTS;
}


static const int32 kBuiltInCount = 3;


static void
built_in_theme(int32 index, theme_info* info)
{
	switch (index) {
		case 0:
			init_theme(info, kR5Modern);
			strlcpy(info->decorator, kR5ModernDecorator,
				sizeof(info->decorator));
			strlcpy(info->control_look, kR5ModernControlLook,
				sizeof(info->control_look));
			info->modern_scroll_bars = true;
			break;
		case 1:
			init_theme(info, kR5);
			strlcpy(info->decorator, kR5Decorator, sizeof(info->decorator));
			break;
		default:
			init_theme(info, kClassic);
			info->deskbar_style = B_THEME_DESKBAR_CLASSIC;
			info->deskbar_rounded = false;
			info->deskbar_floating = false;
			break;
	}
}


bool
is_built_in_theme(const char* name)
{
	if (name == NULL)
		return false;
	return strcasecmp(name, kR5Modern) == 0 || strcasecmp(name, kR5) == 0
		|| strcasecmp(name, kClassic) == 0;
}


// #pragma mark - files


static status_t
settings_path(const char* leaf, BPath& path)
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK)
		return status;
	return path.Append(leaf);
}


static bool
valid_name(const char* name)
{
	if (name == NULL || name[0] == '\0' || strlen(name) >= B_FILE_NAME_LENGTH)
		return false;
	if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
		return false;
	for (const char* c = name; *c != '\0'; c++) {
		if (*c == '/' || *c == '"' || *c == '\\'
			|| static_cast<uint8>(*c) < 0x20)
			return false;
	}
	return true;
}


/*!	An add-on's file name from a theme file: a plain name in the add-ons
	folders (no path), so a theme from somewhere else cannot point a program
	at another file.
*/
static bool
valid_leaf(const char* leaf)
{
	if (leaf == NULL || strlen(leaf) >= B_FILE_NAME_LENGTH)
		return false;
	if (strcmp(leaf, ".") == 0 || strcmp(leaf, "..") == 0)
		return false;
	for (const char* c = leaf; *c != '\0'; c++) {
		if (*c == '/' || *c == '"' || *c == '\\'
			|| static_cast<uint8>(*c) < 0x20)
			return false;
	}
	return true;
}


static bool
parse_accent(const char* value, rgb_color* color)
{
	if (value == NULL || value[0] != '#' || strlen(value) != 7)
		return false;
	for (int i = 1; i < 7; i++) {
		if (!isxdigit(static_cast<unsigned char>(value[i])))
			return false;
	}
	*color = hex(strtoul(value + 1, NULL, 16));
	return true;
}


static const char*
layout_name(int32 layout)
{
	switch (layout) {
		case B_THEME_DESKBAR_COMPACT:
			return "compact";
		case B_THEME_DESKBAR_HORIZONTAL:
			return "horizontal";
		default:
			return "vertical";
	}
}


static const char*
strength_name(int32 strength)
{
	switch (strength) {
		case B_WINDOW_EFFECTS_QUIET:
			return "quiet";
		case B_WINDOW_EFFECTS_GLASSY:
			return "glassy";
		default:
			return "balanced";
	}
}


static status_t
write_theme(const char* path, const theme_info* info, uint32 parts)
{
	BString text("# R5 theme (docs/LIBR5.md, \"Theme files\")\n");
	text << "format " << static_cast<int32>(B_THEME_FORMAT_VERSION) << "\n";
	text << "name \"" << info->name << "\"\n";
	if (info->base[0] != '\0')
		text << "base \"" << info->base << "\"\n";
	if ((parts & B_THEME_WINDOWS) != 0)
		text << "windows \"" << info->decorator << "\"\n";
	if ((parts & B_THEME_CONTROLS) != 0)
		text << "controls \"" << info->control_look << "\"\n";
	if ((parts & B_THEME_SCROLL_BARS) != 0) {
		text << "scroll_bars " << (info->modern_scroll_bars ? "modern" : "haiku")
			<< "\n";
		text << "arrows " << (info->double_arrows ? "double" : "single")
			<< "\n";
		if (info->scroll_bar_width > 0) {
			text << "scroll_bar_width "
				<< static_cast<int32>(roundf(info->scroll_bar_width)) << "\n";
		}
	}
	if ((parts & B_THEME_DESKBAR) != 0) {
		text << "deskbar_style "
			<< (info->deskbar_style == B_THEME_DESKBAR_CLASSIC
				? "classic" : "r5") << "\n";
		text << "deskbar_layout " << layout_name(info->deskbar_layout) << "\n";
		text << "deskbar_rounded " << (info->deskbar_rounded ? "true" : "false")
			<< "\n";
		text << "deskbar_floating "
			<< (info->deskbar_floating ? "true" : "false") << "\n";
	}
	if ((parts & B_THEME_COLORS) != 0) {
		text << "mode " << (info->mode == B_THEME_DARK ? "dark" : "light")
			<< "\n";
		if (info->accent.alpha != 0) {
			char accent[16];
			snprintf(accent, sizeof(accent), "#%02x%02x%02x",
				info->accent.red, info->accent.green, info->accent.blue);
			// Quote #rrggbb: driver_settings treats an unquoted # as a comment.
			text << "accent \"" << accent << "\"\n";
		} else
			text << "accent default\n";
	}
	if ((parts & B_THEME_EFFECTS) != 0) {
		text << "shadow "
			<< ((info->effects & B_WINDOW_SHADOW) != 0 ? "true" : "false")
			<< "\n";
		text << "glass "
			<< ((info->effects & B_WINDOW_FROSTED_GLASS) != 0
				? "true" : "false") << "\n";
		text << "animations "
			<< ((info->effects & B_WINDOW_ANIMATIONS) != 0 ? "true" : "false")
			<< "\n";
		text << "strength " << strength_name(info->effect_strength) << "\n";
	}

	BFile file(path, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status_t status = file.InitCheck();
	if (status != B_OK)
		return status;
	ssize_t written = file.Write(text.String(), text.Length());
	if (written < 0)
		return static_cast<status_t>(written);
	return written == text.Length() ? B_OK : B_IO_ERROR;
}


static status_t
read_theme(const char* path, theme_info* info)
{
	void* handle = load_driver_settings(path);
	if (handle == NULL)
		return B_ENTRY_NOT_FOUND;

	// what a file does not hold stays as R5 has it by default
	built_in_theme(1, info);
	info->parts = 0;
	info->base[0] = '\0';

	const char* value = get_driver_parameter(handle, "name", NULL, NULL);
	if (value == NULL || !valid_name(value)) {
		unload_driver_settings(handle);
		return B_BAD_DATA;
	}
	strlcpy(info->name, value, sizeof(info->name));

	value = get_driver_parameter(handle, "base", NULL, NULL);
	if (value != NULL && valid_name(value))
		strlcpy(info->base, value, sizeof(info->base));

	value = get_driver_parameter(handle, "windows", NULL, "");
	if (value != NULL && valid_leaf(value)) {
		strlcpy(info->decorator, value, sizeof(info->decorator));
		info->parts |= B_THEME_WINDOWS;
	}
	value = get_driver_parameter(handle, "controls", NULL, "");
	if (value != NULL && valid_leaf(value)) {
		strlcpy(info->control_look, value, sizeof(info->control_look));
		info->parts |= B_THEME_CONTROLS;
	}
	value = get_driver_parameter(handle, "scroll_bars", NULL, NULL);
	if (value != NULL) {
		info->modern_scroll_bars = strcmp(value, "modern") == 0;
		value = get_driver_parameter(handle, "arrows", "single", "single");
		info->double_arrows = strcmp(value, "double") == 0;
		value = get_driver_parameter(handle, "scroll_bar_width", NULL, NULL);
		info->scroll_bar_width = 0;
		if (value != NULL) {
			int width = atoi(value);
			if (width >= 8 && width <= 32)
				info->scroll_bar_width = width;
		}
		info->parts |= B_THEME_SCROLL_BARS;
	}
	value = get_driver_parameter(handle, "deskbar_style", NULL, NULL);
	if (value != NULL) {
		info->deskbar_style = strcmp(value, "classic") == 0
			? B_THEME_DESKBAR_CLASSIC : B_THEME_DESKBAR_R5;
		value = get_driver_parameter(handle, "deskbar_layout", "vertical",
			"vertical");
		info->deskbar_layout = strcmp(value, "compact") == 0
			? B_THEME_DESKBAR_COMPACT : strcmp(value, "horizontal") == 0
				? B_THEME_DESKBAR_HORIZONTAL : B_THEME_DESKBAR_VERTICAL;
		info->deskbar_rounded = get_driver_boolean_parameter(handle,
			"deskbar_rounded", true, true);
		info->deskbar_floating = get_driver_boolean_parameter(handle,
			"deskbar_floating", true, true);
		info->parts |= B_THEME_DESKBAR;
	}
	value = get_driver_parameter(handle, "mode", NULL, NULL);
	if (value != NULL) {
		info->mode = strcmp(value, "dark") == 0 ? B_THEME_DARK : B_THEME_LIGHT;
		value = get_driver_parameter(handle, "accent", "default", "default");
		info->accent.alpha = 0;
		parse_accent(value, &info->accent);
		info->parts |= B_THEME_COLORS;
	}
	value = get_driver_parameter(handle, "strength", NULL, NULL);
	if (value != NULL) {
		info->effect_strength = strcmp(value, "quiet") == 0
			? B_WINDOW_EFFECTS_QUIET : strcmp(value, "glassy") == 0
				? B_WINDOW_EFFECTS_GLASSY : B_WINDOW_EFFECTS_BALANCED;
		info->effects = 0;
		if (get_driver_boolean_parameter(handle, "shadow", true, true))
			info->effects |= B_WINDOW_SHADOW;
		if (get_driver_boolean_parameter(handle, "glass", true, true))
			info->effects |= B_WINDOW_FROSTED_GLASS;
		if (get_driver_boolean_parameter(handle, "animations", true, true))
			info->effects |= B_WINDOW_ANIMATIONS;
		info->parts |= B_THEME_EFFECTS;
	}

	unload_driver_settings(handle);
	return B_OK;
}


static status_t
make_settings_directory(const char* leaf)
{
	BPath path;
	status_t status = settings_path(leaf, path);
	if (status != B_OK)
		return status;
	return create_directory(path.Path(), 0755);
}


// #pragma mark - the system now


static void
read_deskbar_settings(theme_info* info)
{
	BPath path;
	if (settings_path("deskbar/settings", path) != B_OK)
		return;
	BFile file(path.Path(), B_READ_ONLY);
	BMessage settings;
	if (file.InitCheck() != B_OK || settings.Unflatten(&file) != B_OK)
		return;

	// as the Desktop and Deskbar page reads it: a file without the keys is
	// Default (R5)
	bool vertical = settings.GetBool("vertical", true);
	info->deskbar_style = settings.GetInt32("style", B_THEME_DESKBAR_R5)
			== B_THEME_DESKBAR_CLASSIC
		? B_THEME_DESKBAR_CLASSIC : B_THEME_DESKBAR_R5;
	info->deskbar_layout = settings.GetInt32("layout", vertical
		? B_THEME_DESKBAR_VERTICAL : B_THEME_DESKBAR_HORIZONTAL);
	if (info->deskbar_layout < B_THEME_DESKBAR_VERTICAL
		|| info->deskbar_layout > B_THEME_DESKBAR_HORIZONTAL)
		info->deskbar_layout = B_THEME_DESKBAR_VERTICAL;
	info->deskbar_rounded = settings.GetBool("rounded", true);
	info->deskbar_floating = settings.GetBool("floating", true);
}


//! What the system shows now, as a theme, for a system no theme was set on.
static void
system_theme(theme_info* info)
{
	built_in_theme(1, info);
	info->parts = B_THEME_ALL_PARTS;

	BPrivate::DecorInfoUtility decorators;
	BPrivate::DecorInfo* decorator = decorators.CurrentDecorator();
	if (decorator != NULL) {
		if (decorator->IsDefault())
			info->decorator[0] = '\0';
		else {
			strlcpy(info->decorator, decorator->ShortcutName().String(),
				sizeof(info->decorator));
		}
	}

	BString lookPath;
	info->control_look[0] = '\0';
	if (BPrivate::get_control_look(lookPath) && lookPath.Length() > 0) {
		BPath path(lookPath.String());
		if (path.Leaf() != NULL)
			strlcpy(info->control_look, path.Leaf(), sizeof(info->control_look));
	}
	info->modern_scroll_bars = modern_controls(info);

	scroll_bar_info scrollBars;
	if (get_scroll_bar_info(&scrollBars) == B_OK)
		info->double_arrows = scrollBars.double_arrows;

	read_deskbar_settings(info);

	info->mode = ui_color(B_PANEL_BACKGROUND_COLOR).IsDark()
		? B_THEME_DARK : B_THEME_LIGHT;
	info->accent.alpha = 0;
	info->effects = window_effects();
	info->effect_strength = window_effect_strength();

	// the name of the built-in theme it shows, if it shows one
	for (int32 i = 0; i < kBuiltInCount; i++) {
		theme_info builtIn;
		built_in_theme(i, &builtIn);
		builtIn.mode = info->mode;
		if (theme_matches(info, &builtIn)) {
			strlcpy(info->name, builtIn.name, sizeof(info->name));
			strlcpy(info->base, builtIn.name, sizeof(info->base));
			return;
		}
	}
}


bool
theme_matches(const theme_info* theme, const theme_info* base)
{
	if (theme == NULL || base == NULL)
		return false;

	uint32 parts = base->parts;
	if ((parts & B_THEME_WINDOWS) != 0
		&& strcasecmp(theme->decorator, base->decorator) != 0)
		return false;
	if ((parts & B_THEME_CONTROLS) != 0
		&& strcmp(theme->control_look, base->control_look) != 0)
		return false;
	if ((parts & B_THEME_SCROLL_BARS) != 0
		&& (theme->modern_scroll_bars != base->modern_scroll_bars
			|| theme->double_arrows != base->double_arrows
			|| roundf(theme->scroll_bar_width)
				!= roundf(base->scroll_bar_width)))
		return false;
	if ((parts & B_THEME_DESKBAR) != 0
		&& (theme->deskbar_style != base->deskbar_style
			|| theme->deskbar_layout != base->deskbar_layout
			|| theme->deskbar_rounded != base->deskbar_rounded
			|| theme->deskbar_floating != base->deskbar_floating))
		return false;
	if ((parts & B_THEME_COLORS) != 0) {
		if (theme->mode != base->mode)
			return false;
		bool a = theme->accent.alpha != 0;
		bool b = base->accent.alpha != 0;
		if (a != b || (a && (theme->accent.red != base->accent.red
				|| theme->accent.green != base->accent.green
				|| theme->accent.blue != base->accent.blue)))
			return false;
	}
	if ((parts & B_THEME_EFFECTS) != 0
		&& (theme->effects != base->effects
			|| theme->effect_strength != base->effect_strength))
		return false;
	return true;
}


status_t
get_theme(theme_info* info)
{
	if (info == NULL)
		return B_BAD_VALUE;

	// What every part shows now: Appearance, Desktop and Deskbar and the
	// Colors tab change the parts too, so the system is the truth for
	// them. The file adds what the system does not know: the theme's name
	// and base, its accent and the R5 Modern scroll bar style.
	system_theme(info);

	BPath path;
	theme_info kept;
	if (settings_path(kCurrentTheme, path) != B_OK
		|| read_theme(path.Path(), &kept) != B_OK)
		return B_OK;

	strlcpy(info->name, kept.name, sizeof(info->name));
	strlcpy(info->base, kept.base, sizeof(info->base));

	// Deskbar writes its own file a moment after it took a style; until
	// then the theme file is the newer word on the Deskbar part. Desktop and
	// Deskbar changes go to Deskbar's file, which is then the newer one.
	BPath deskbarPath;
	time_t themeTime = 0;
	time_t deskbarTime = 0;
	BEntry(path.Path()).GetModificationTime(&themeTime);
	if (settings_path("deskbar/settings", deskbarPath) == B_OK)
		BEntry(deskbarPath.Path()).GetModificationTime(&deskbarTime);
	if ((kept.parts & B_THEME_DESKBAR) != 0 && themeTime >= deskbarTime) {
		info->deskbar_style = kept.deskbar_style;
		info->deskbar_layout = kept.deskbar_layout;
		info->deskbar_rounded = kept.deskbar_rounded;
		info->deskbar_floating = kept.deskbar_floating;
	}
	if ((kept.parts & B_THEME_COLORS) != 0 && kept.mode == info->mode)
		info->accent = kept.accent;
	if ((kept.parts & B_THEME_SCROLL_BARS) != 0
		&& strcmp(kept.control_look, info->control_look) == 0) {
		info->modern_scroll_bars = kept.modern_scroll_bars;
		info->scroll_bar_width = kept.scroll_bar_width;
	}
	return B_OK;
}


// #pragma mark - applying


static status_t
apply_decorator(const char* name)
{
	if (!valid_leaf(name))
		return B_BAD_VALUE;
	BPrivate::DecorInfoUtility decorators;
	BPrivate::DecorInfo* decorator = decorators.FindDecorator(
		name[0] != '\0' ? BString(name) : BString("Default"));
	if (decorator == NULL)
		return B_ENTRY_NOT_FOUND;
	if (decorator == decorators.CurrentDecorator())
		return B_OK;
	return decorators.SetDecorator(decorator);
}


static status_t
apply_control_look(const char* leaf, bool* changed)
{
	if (!valid_leaf(leaf))
		return B_BAD_VALUE;
	BString path;
	if (leaf[0] != '\0') {
		BPath found;
		BPathFinder finder;
		BString subPath("control_look/");
		subPath << leaf;
		status_t status = finder.FindPath(B_FIND_PATH_ADD_ONS_DIRECTORY,
			subPath.String(), B_FIND_PATH_EXISTING_ONLY, found);
		if (status != B_OK)
			return status;
		path = found.Path();
	}

	BString current;
	BPrivate::get_control_look(current);
	if (current == path)
		return B_OK;
	*changed = true;
	return BPrivate::set_control_look(path);
}


static status_t
apply_deskbar(const theme_info* info)
{
	BMessenger deskbar(kDeskbarSignature);
	if (!deskbar.IsValid()) {
		// Deskbar is not running: nothing to set, and nothing kept
		return B_NAME_NOT_FOUND;
	}
	BMessage message(kSetDeskbarStyle);
	message.AddInt32("style", info->deskbar_style);
	message.AddInt32("layout", info->deskbar_layout);
	message.AddBool("rounded", info->deskbar_rounded);
	message.AddBool("floating", info->deskbar_floating);
	return deskbar.SendMessage(&message);
}


status_t
set_theme(const theme_info* info, uint32 parts)
{
	if (info == NULL || !valid_name(info->name))
		return B_BAD_VALUE;

	theme_info now;
	get_theme(&now);

	bool lookChanged = false;
	status_t result = B_OK;
	// a part that could not be set is not kept as the one in use
	uint32 applied = parts;

	if ((parts & B_THEME_WINDOWS) != 0) {
		status_t status = apply_decorator(info->decorator);
		if (status != B_OK) {
			applied &= ~B_THEME_WINDOWS;
			if (result == B_OK)
				result = status;
		}
	}
	if ((parts & B_THEME_CONTROLS) != 0) {
		status_t status = apply_control_look(info->control_look, &lookChanged);
		if (status != B_OK) {
			applied &= ~B_THEME_CONTROLS;
			if (result == B_OK)
				result = status;
		}
	}
	if ((parts & B_THEME_SCROLL_BARS) != 0) {
		scroll_bar_info scrollBars;
		status_t status = get_scroll_bar_info(&scrollBars);
		if (status == B_OK
			&& scrollBars.double_arrows != info->double_arrows) {
			scrollBars.double_arrows = info->double_arrows;
			status = set_scroll_bar_info(&scrollBars);
		}
		// the first error counts, as for the other parts (external review
		// 2.5)
		if (status != B_OK) {
			applied &= ~B_THEME_SCROLL_BARS;
			if (result == B_OK)
				result = status;
		}
		if (now.modern_scroll_bars != info->modern_scroll_bars
			|| roundf(now.scroll_bar_width)
				!= roundf(info->scroll_bar_width))
			lookChanged = true;
	}
	if ((parts & B_THEME_DESKBAR) != 0) {
		status_t status = apply_deskbar(info);
		if (status != B_OK) {
			applied &= ~B_THEME_DESKBAR;
			if (status != B_NAME_NOT_FOUND && result == B_OK)
				result = status;
		}
	}
	if ((parts & B_THEME_EFFECTS) != 0) {
		status_t status = set_window_effects(info->effects);
		if (status == B_OK) {
			status = set_window_effect_strength(
				static_cast<enum window_effect_strength>(
					info->effect_strength));
		}
		if (status != B_OK && result == B_OK)
			result = status;
	}

	// The theme in use, before the broadcast: the R5 Modern controls read
	// their scroll bar style from it when a program loads them.
	theme_info kept = now;
	strlcpy(kept.name, info->name, sizeof(kept.name));
	strlcpy(kept.base, info->base, sizeof(kept.base));
	if ((applied & B_THEME_WINDOWS) != 0)
		strlcpy(kept.decorator, info->decorator, sizeof(kept.decorator));
	if ((applied & B_THEME_CONTROLS) != 0)
		strlcpy(kept.control_look, info->control_look, sizeof(kept.control_look));
	if ((applied & B_THEME_SCROLL_BARS) != 0) {
		kept.modern_scroll_bars = info->modern_scroll_bars;
		kept.double_arrows = info->double_arrows;
		kept.scroll_bar_width = info->scroll_bar_width;
	}
	if ((applied & B_THEME_DESKBAR) != 0) {
		kept.deskbar_style = info->deskbar_style;
		kept.deskbar_layout = info->deskbar_layout;
		kept.deskbar_rounded = info->deskbar_rounded;
		kept.deskbar_floating = info->deskbar_floating;
	}
	if ((parts & B_THEME_COLORS) != 0) {
		kept.mode = info->mode;
		kept.accent = info->accent;
	}
	// Classic (Haiku) is light only, exactly as Haiku
	if (strcmp(kept.base, kClassic) == 0)
		kept.mode = B_THEME_LIGHT;

	if (make_settings_directory("r5") == B_OK) {
		BPath path;
		if (settings_path(kCurrentTheme, path) == B_OK) {
			write_theme(path.Path(), &kept,
				B_THEME_ALL_PARTS & ~B_THEME_EFFECTS);
		}
	}

	// The colours follow the mode, the accent and the controls (R5 Modern
	// light has its own set). The mode and the accent set them. A change of
	// the controls alone sets them only while the colours are still the
	// set of the theme before, so colours of the user's own (Appearance >
	// Colors) stay.
	bool setColors = (parts & B_THEME_COLORS) != 0;
	if (!setColors && (applied & B_THEME_CONTROLS) != 0) {
		BMessage before;
		get_theme_colors(&now, &before);
		setColors = true;
		char* name;
		type_code type;
		rgb_color color;
		for (int32 i = 0; before.GetInfo(B_RGB_32_BIT_TYPE, i, &name, &type)
				== B_OK; i++) {
			color_which which = which_ui_color(name);
			if (which == B_NO_COLOR || before.FindColor(name, &color) != B_OK)
				continue;
			rgb_color current = ui_color(which);
			if (current.red != color.red || current.green != color.green
				|| current.blue != color.blue) {
				setColors = false;
				break;
			}
		}
	}
	if (setColors) {
		BMessage colors;
		if (get_theme_colors(&kept, &colors) == B_OK)
			set_ui_colors(&colors);
	}

	if (lookChanged) {
		// live switching: every window of every program loads the control
		// look again (Window.cpp, r5_reload_control_look()). Post directly
		// to our own windows too so the caller (Settings) switches live even
		// if BRoster::Broadcast skips or delays the calling team.
		BMessage message(B_COLORS_UPDATED);
		message.AddInt64("r5:control_look", system_time());
		if (be_roster != NULL)
			be_roster->Broadcast(&message);
		if (be_app != NULL) {
			int32 count = be_app->CountWindows();
			for (int32 i = 0; i < count; i++) {
				BWindow* window = be_app->WindowAt(i);
				if (window != NULL)
					window->PostMessage(&message);
			}
		}
	}

	if (be_roster != NULL) {
		// the Theme Kit's notification, for programs and pages that show
		// the theme (docs/LIBR5.md, "Themes")
		BMessage changed(B_THEME_CHANGED);
		changed.AddString("name", kept.name);
		changed.AddString("base", kept.base);
		changed.AddUInt32("parts", parts);
		changed.AddInt32("mode", kept.mode);
		be_roster->Broadcast(&changed);
	}

	return result;
}


// #pragma mark - saved themes


static void
saved_theme_names(BStringList& names)
{
	names.MakeEmpty();
	BPath path;
	if (settings_path(kThemesDirectory, path) != B_OK)
		return;
	BDirectory directory(path.Path());
	if (directory.InitCheck() != B_OK)
		return;

	BEntry entry;
	while (directory.GetNextEntry(&entry) == B_OK) {
		BPath file;
		theme_info info;
		if (entry.GetPath(&file) != B_OK
			|| read_theme(file.Path(), &info) != B_OK
			|| is_built_in_theme(info.name))
			continue;
		names.Add(info.name);
	}
	names.Sort(true);
}


int32
count_themes()
{
	BStringList names;
	saved_theme_names(names);
	return kBuiltInCount + names.CountStrings();
}


static status_t
theme_file_path(const char* name, BPath& path)
{
	if (!valid_name(name))
		return B_BAD_VALUE;
	status_t status = settings_path(kThemesDirectory, path);
	if (status != B_OK)
		return status;
	return path.Append(name);
}


status_t
get_theme_at(int32 index, theme_info* info)
{
	if (info == NULL || index < 0)
		return B_BAD_VALUE;
	if (index < kBuiltInCount) {
		built_in_theme(index, info);
		return B_OK;
	}

	BStringList names;
	saved_theme_names(names);
	if (index - kBuiltInCount >= names.CountStrings())
		return B_BAD_INDEX;
	return find_theme(names.StringAt(index - kBuiltInCount).String(), info);
}


status_t
find_theme(const char* name, theme_info* info)
{
	if (name == NULL || info == NULL)
		return B_BAD_VALUE;
	for (int32 i = 0; i < kBuiltInCount; i++) {
		built_in_theme(i, info);
		if (strcasecmp(info->name, name) == 0)
			return B_OK;
	}

	// a file is named after its theme; a file copied in by hand may have
	// another file name, so look at the names inside the files too
	BPath path;
	if (theme_file_path(name, path) == B_OK
		&& read_theme(path.Path(), info) == B_OK
		&& strcasecmp(info->name, name) == 0)
		return B_OK;

	if (settings_path(kThemesDirectory, path) != B_OK)
		return B_ENTRY_NOT_FOUND;
	BDirectory directory(path.Path());
	BEntry entry;
	while (directory.GetNextEntry(&entry) == B_OK) {
		BPath file;
		if (entry.GetPath(&file) == B_OK
			&& read_theme(file.Path(), info) == B_OK
			&& strcasecmp(info->name, name) == 0)
			return B_OK;
	}
	return B_ENTRY_NOT_FOUND;
}


status_t
save_theme(const theme_info* info, uint32 parts)
{
	if (info == NULL || !valid_name(info->name))
		return B_BAD_VALUE;
	if (is_built_in_theme(info->name))
		return B_NOT_ALLOWED;
	if ((parts & B_THEME_ALL_PARTS) == 0)
		return B_BAD_VALUE;

	status_t status = make_settings_directory(kThemesDirectory);
	if (status != B_OK)
		return status;

	BPath path;
	status = theme_file_path(info->name, path);
	if (status != B_OK)
		return status;

	theme_info saved = *info;
	// a saved theme is its own base
	strlcpy(saved.base, info->name, sizeof(saved.base));
	return write_theme(path.Path(), &saved, parts & B_THEME_ALL_PARTS);
}


status_t
remove_theme(const char* name)
{
	if (is_built_in_theme(name))
		return B_NOT_ALLOWED;
	BPath path;
	status_t status = theme_file_path(name, path);
	if (status != B_OK)
		return status;
	BEntry entry(path.Path());
	theme_info inside;
	if (entry.Exists() && read_theme(path.Path(), &inside) == B_OK
		&& strcasecmp(inside.name, name) == 0) {
		return entry.Remove();
	}

	// a file copied in by hand may have another file name: the name inside
	// it counts, as for find_theme() (external review 2.4)
	if (settings_path(kThemesDirectory, path) != B_OK)
		return B_ENTRY_NOT_FOUND;
	BDirectory directory(path.Path());
	while (directory.GetNextEntry(&entry) == B_OK) {
		BPath file;
		if (entry.GetPath(&file) == B_OK
			&& read_theme(file.Path(), &inside) == B_OK
			&& strcasecmp(inside.name, name) == 0)
			return entry.Remove();
	}
	return B_ENTRY_NOT_FOUND;
}


}	// namespace BAppearanceKit
