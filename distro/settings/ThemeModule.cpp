/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


/*!	The Theme page under Personalization (docs/design/THEMING.md, mockup A1,
	A2 and A3). A theme is a saved combination of parts; this page shows the
	themes as cards, the mode and the accent, and every part on its own.
	A change is applied at once through libr5ui's set_theme(), which sets each
	part the way Appearance and Desktop and Deskbar do. Revert puts back the
	theme from when the page was opened, Defaults chooses R5, light.
*/


#include "ThemeModule.h"

#include <algorithm>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include <Alert.h>
#include <AppFileInfo.h>
#include <Box.h>
#include <Button.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <DecorInfo.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Path.h>
#include <PathFinder.h>
#include <PopUpMenu.h>
#include <ScrollBar.h>
#include <Region.h>
#include <SpaceLayoutItem.h>
#include <String.h>
#include <StringList.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>
#include <Window.h>

#include <R5Icons.h>
#include <Themes.h>
#include <WindowEffects.h>

#include "SettingsFont.h"
#include "SettingsManager.h"
#include "SettingsModule.h"


using namespace BAppearanceKit;


static const char* kModuleId = "theme";

static const uint32 kMsgPickTheme = 'thPk';
static const uint32 kMsgSaveTheme = 'thSv';
static const uint32 kMsgSaveThemeDone = 'thSd';
static const uint32 kMsgDeleteTheme = 'thDl';
static const uint32 kMsgMode = 'thMd';
static const uint32 kMsgAccent = 'thAc';
static const uint32 kMsgParts = 'thPt';
static const uint32 kMsgWindows = 'thWn';
static const uint32 kMsgWindowsAbout = 'thWa';
static const uint32 kMsgControls = 'thCt';
static const uint32 kMsgControlsAbout = 'thCa';
static const uint32 kMsgScrollBars = 'thSb';
static const uint32 kMsgArrows = 'thAr';
static const uint32 kMsgScrollWidth = 'thSw';
static const uint32 kMsgDeskbarStyle = 'thDs';
static const uint32 kMsgDeskbarLayout = 'thDy';
static const uint32 kMsgDeskbarCheck = 'thDc';
static const uint32 kMsgEffects = 'thEf';
static const uint32 kMsgStrength = 'thSt';

static const rgb_color kWhite = { 255, 255, 255, 255 };
static const rgb_color kInk = { 15, 23, 42, 255 };


// #pragma mark - colours and sizes


static float
scale()
{
	return std::max(be_plain_font->Size(), 9.0f) / 12.0f;
}


static float
px(float pixels)
{
	return roundf(pixels * scale());
}


static rgb_color
blend(const rgb_color& from, const rgb_color& to, float amount)
{
	rgb_color color;
	color.red = static_cast<uint8>(roundf(from.red
		+ (to.red - from.red) * amount));
	color.green = static_cast<uint8>(roundf(from.green
		+ (to.green - from.green) * amount));
	color.blue = static_cast<uint8>(roundf(from.blue
		+ (to.blue - from.blue) * amount));
	color.alpha = 255;
	return color;
}


static rgb_color
hex(uint32 value)
{
	rgb_color color = { static_cast<uint8>(value >> 16),
		static_cast<uint8>(value >> 8), static_cast<uint8>(value), 255 };
	return color;
}


static bool
dark()
{
	return ui_color(B_PANEL_BACKGROUND_COLOR).IsDark();
}


//! The thin line (#d5dae2 light, #3a404c dark).
static rgb_color
line_color()
{
	rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	return dark() ? blend(panel, kWhite, 0.13f) : blend(panel, kInk, 0.11f);
}


//! The second text colour (#5b6472 light, #a3abb8 dark).
static rgb_color
text2_color()
{
	return blend(ui_color(B_PANEL_BACKGROUND_COLOR),
		ui_color(B_PANEL_TEXT_COLOR), dark() ? 0.68f : 0.64f);
}


//! The well of a segmented control (#eef1f5 on white, #1b1f26 on dark).
static rgb_color
well_color()
{
	rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	return dark() ? blend(panel, hex(0x000000), 0.15f)
		: blend(panel, kInk, 0.035f);
}


//! The chosen segment (#ffffff light, #353b47 dark).
static rgb_color
segment_on_color()
{
	rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	return dark() ? blend(panel, kWhite, 0.12f) : kWhite;
}


static void
fill_round(BView* view, BRect rect, float radius, rgb_color color)
{
	view->SetHighColor(color);
	view->FillRoundRect(BRect(rect.left, rect.top, rect.right + 1,
		rect.bottom + 1), radius, radius);
}


//! A 1 px line round \a rect, on its outer pixels, as a CSS border.
static void
stroke_round(BView* view, BRect rect, float radius, rgb_color color)
{
	view->PushState();
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetHighColor(color);
	view->SetPenSize(1);
	view->StrokeRoundRect(rect, radius, radius);
	view->PopState();
}


static void
draw_text(BView* view, const char* text, BPoint where, const BFont* font,
	rgb_color color)
{
	view->PushState();
	view->SetFont(font);
	view->SetHighColor(color);
	view->SetDrawingMode(B_OP_OVER);
	view->DrawString(text, where);
	view->PopState();
}


/*!	A label in the second text colour, worked out when it draws, so it
	follows a change to dark (the mockup's row labels and notes).
*/
class QuietLabel : public BStringView {
public:
	QuietLabel(const char* name, const char* text)
		:
		BStringView(name, text)
	{
	}

	virtual void Draw(BRect updateRect)
	{
		SetHighColor(text2_color());
		BStringView::Draw(updateRect);
	}
};


static BString
decorator_label(const BString& name)
{
	BString label(name);
	label.RemoveLast("Decorator").Trim();
	return label;
}


static BString
control_look_label(const char* leaf)
{
	if (leaf == NULL || leaf[0] == '\0')
		return BString("Haiku");
	BString label(leaf);
	label.RemoveLast("ControlLook").Trim();
	if (label.StartsWith("R5") && label.Length() > 2 && isupper(label[2]))
		label.Insert(" ", 2);
	return label;
}


static bool
is_classic(const theme_info& info)
{
	return strcmp(info.base, "Classic (Haiku)") == 0;
}


// #pragma mark - SegmentedControl


/*!	Two or three choices side by side in one well (the mockup's Mode and
	Arrows). Haiku has no such control; it is a BControl like BCheckBox, its
	value is the chosen segment, and it sends its message on a change.
*/
class SegmentedControl : public BControl {
public:
								SegmentedControl(const char* name,
									BMessage* message, float height,
									float padding);

			void				AddSegment(const char* label, int32 glyph = 0);
			void				SetSegmentEnabled(int32 index, bool enabled);

	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	void				KeyDown(const char* bytes, int32 numBytes);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();

private:
			float				_SegmentWidth(int32 index) const;
			BRect				_SegmentFrame(int32 index) const;
			void				_Choose(int32 index);

			BStringList			fLabels;
			int32				fGlyphs[4];
			bool				fEnabled[4];
			float				fHeight;
			float				fPadding;
};


SegmentedControl::SegmentedControl(const char* name, BMessage* message,
	float height, float padding)
	:
	BControl(name, NULL, message, B_WILL_DRAW | B_NAVIGABLE),
	fHeight(height),
	fPadding(padding)
{
	for (int32 i = 0; i < 4; i++) {
		fGlyphs[i] = 0;
		fEnabled[i] = true;
	}
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
}


void
SegmentedControl::AddSegment(const char* label, int32 glyph)
{
	int32 index = fLabels.CountStrings();
	if (index >= 4)
		return;
	fLabels.Add(label);
	fGlyphs[index] = glyph;
	InvalidateLayout();
}


void
SegmentedControl::SetSegmentEnabled(int32 index, bool enabled)
{
	if (index < 0 || index >= 4 || fEnabled[index] == enabled)
		return;
	fEnabled[index] = enabled;
	Invalidate();
}


float
SegmentedControl::_SegmentWidth(int32 index) const
{
	const BFont* bold = r5_settings_bold_font();
	float width = px(fPadding) * 2
		+ ceilf(bold->StringWidth(fLabels.StringAt(index).String()));
	if (fGlyphs[index] != 0)
		width += px(14) + px(6);
	return width;
}


BRect
SegmentedControl::_SegmentFrame(int32 index) const
{
	float x = px(3);
	for (int32 i = 0; i < index; i++)
		x += _SegmentWidth(i);
	return BRect(x, px(3), x + _SegmentWidth(index) - 1,
		px(3) + px(fHeight) - 1);
}


BSize
SegmentedControl::PreferredSize()
{
	float width = px(3) * 2;
	for (int32 i = 0; i < fLabels.CountStrings(); i++)
		width += _SegmentWidth(i);
	return BSize(width - 1, px(fHeight) + px(3) * 2 - 1);
}


BSize
SegmentedControl::MinSize()
{
	return PreferredSize();
}


BSize
SegmentedControl::MaxSize()
{
	return PreferredSize();
}


void
SegmentedControl::Draw(BRect updateRect)
{
	BRect bounds = Bounds();
	SetDrawingMode(B_OP_ALPHA);
	fill_round(this, bounds, px(9), well_color());
	stroke_round(this, bounds, px(9), line_color());

	const BFont* plain = r5_settings_plain_font();
	const BFont* bold = r5_settings_bold_font();
	font_height height;
	bold->GetHeight(&height);

	for (int32 i = 0; i < fLabels.CountStrings(); i++) {
		BRect frame = _SegmentFrame(i);
		bool on = Value() == i;
		rgb_color text = on ? ui_color(B_PANEL_TEXT_COLOR) : text2_color();
		if (!IsEnabled() || !fEnabled[i])
			text = blend(well_color(), text, 0.45f);
		if (on) {
			// the chosen one: a lighter face with a soft shadow under it
			rgb_color shadow = hex(0x000000);
			shadow.alpha = dark() ? 60 : 30;
			SetHighColor(shadow);
			FillRoundRect(BRect(frame.left, frame.top + 1, frame.right + 1,
				frame.bottom + 2), px(7), px(7));
			fill_round(this, frame, px(7), segment_on_color());
		}
		if (IsFocus() && on && Window()->IsActive())
			stroke_round(this, frame, px(7), ui_color(B_CONTROL_HIGHLIGHT_COLOR));

		float x = frame.left + px(fPadding);
		float baseline = floorf((frame.top + frame.bottom + 1
			+ ceilf(height.ascent) - ceilf(height.descent)) / 2);
		if (fGlyphs[i] != 0) {
			float side = px(14);
			BRect glyph(x, floorf((frame.top + frame.bottom + 1 - side) / 2),
				x + side, floorf((frame.top + frame.bottom + 1 - side) / 2)
					+ side);
			r5_glyph_draw(this, (r5_glyph)fGlyphs[i], glyph, text);
			x += side + px(6);
		}
		draw_text(this, fLabels.StringAt(i).String(), BPoint(x, baseline),
			on ? bold : plain, text);
	}
}


void
SegmentedControl::_Choose(int32 index)
{
	if (index < 0 || index >= fLabels.CountStrings() || !fEnabled[index]
		|| index == Value())
		return;
	SetValue(index);
	Invoke();
}


void
SegmentedControl::MouseDown(BPoint where)
{
	if (!IsEnabled())
		return;
	for (int32 i = 0; i < fLabels.CountStrings(); i++) {
		if (_SegmentFrame(i).Contains(where)) {
			_Choose(i);
			return;
		}
	}
}


void
SegmentedControl::KeyDown(const char* bytes, int32 numBytes)
{
	if (numBytes == 1 && bytes[0] == B_LEFT_ARROW) {
		_Choose(Value() - 1);
		return;
	}
	if (numBytes == 1 && bytes[0] == B_RIGHT_ARROW) {
		_Choose(Value() + 1);
		return;
	}
	BControl::KeyDown(bytes, numBytes);
}


// #pragma mark - AccentControl


//! The accent swatches (20 px discs, 8 apart, a ring round the chosen one).
class AccentControl : public BControl {
public:
								AccentControl(BMessage* message);

			void				SetMode(int32 mode);

	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	void				KeyDown(const char* bytes, int32 numBytes);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();

private:
			BRect				_Swatch(int32 index) const;
			void				_Choose(int32 index);

			int32				fMode;
};


AccentControl::AccentControl(BMessage* message)
	:
	BControl("accent", NULL, message, B_WILL_DRAW | B_NAVIGABLE),
	fMode(B_THEME_LIGHT)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
	SetValue(-1);
}


void
AccentControl::SetMode(int32 mode)
{
	if (fMode == mode)
		return;
	fMode = mode;
	Invalidate();
}


BRect
AccentControl::_Swatch(int32 index) const
{
	float side = px(20);
	float x = px(4) + index * (side + px(8));
	return BRect(x, px(4), x + side - 1, px(4) + side - 1);
}


BSize
AccentControl::PreferredSize()
{
	BRect last = _Swatch(count_theme_accents() - 1);
	return BSize(last.right + px(4), last.bottom + px(4));
}


BSize
AccentControl::MinSize()
{
	return PreferredSize();
}


BSize
AccentControl::MaxSize()
{
	return PreferredSize();
}


void
AccentControl::Draw(BRect updateRect)
{
	SetDrawingMode(B_OP_ALPHA);
	rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	for (int32 i = 0; i < count_theme_accents(); i++) {
		BRect swatch = _Swatch(i);
		rgb_color color = theme_accent_at(i, fMode);
		if (!IsEnabled())
			color = blend(panel, color, 0.4f);
		if (Value() == i) {
			// box-shadow: 0 0 0 2px card, 0 0 0 4px accent
			BRect ring(swatch);
			ring.InsetBy(-px(4), -px(4));
			SetHighColor(color);
			FillEllipse(BRect(ring.left, ring.top, ring.right + 1,
				ring.bottom + 1));
			ring.InsetBy(px(2), px(2));
			SetHighColor(panel);
			FillEllipse(BRect(ring.left, ring.top, ring.right + 1,
				ring.bottom + 1));
		}
		SetHighColor(color);
		FillEllipse(BRect(swatch.left, swatch.top, swatch.right + 1,
			swatch.bottom + 1));
		if (IsFocus() && Window()->IsActive()
			&& (Value() == i || (Value() < 0 && i == 0))) {
			// the keyboard mark: a ring in the navigation colour
			BRect ring(swatch);
			ring.InsetBy(-px(4) - 1, -px(4) - 1);
			SetHighColor(ui_color(B_KEYBOARD_NAVIGATION_COLOR));
			StrokeEllipse(BRect(ring.left, ring.top, ring.right + 1,
				ring.bottom + 1));
		}
	}
}


void
AccentControl::_Choose(int32 index)
{
	if (index < 0 || index >= count_theme_accents() || index == Value())
		return;
	SetValue(index);
	Invoke();
}


void
AccentControl::MouseDown(BPoint where)
{
	if (!IsEnabled())
		return;
	for (int32 i = 0; i < count_theme_accents(); i++) {
		BRect hit(_Swatch(i));
		hit.InsetBy(-px(3), -px(3));
		if (hit.Contains(where)) {
			_Choose(i);
			return;
		}
	}
}


void
AccentControl::KeyDown(const char* bytes, int32 numBytes)
{
	if (numBytes == 1 && bytes[0] == B_LEFT_ARROW) {
		_Choose(Value() <= 0 ? 0 : Value() - 1);
		return;
	}
	if (numBytes == 1 && bytes[0] == B_RIGHT_ARROW) {
		_Choose(Value() + 1);
		return;
	}
	BControl::KeyDown(bytes, numBytes);
}


// #pragma mark - ThemeCards


/*!	The theme cards (A1): a small picture of a window and the Deskbar in
	each theme, its name and where it comes from, and the "Save as theme…"
	card at the end. Five to a row.
*/
class ThemeCards : public BView {
public:
								ThemeCards();

			void				SetThemes(const std::vector<theme_info>& themes,
									const char* selected, bool changed,
									int32 mode);
			void				SetTarget(BHandler* target);

	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	bool				HasHeightForWidth();
	virtual	void				GetHeightForWidth(float width, float* min,
									float* max, float* preferred);
	virtual	BSize				MinSize();
	virtual	void				FrameResized(float width, float height);

private:
			float				_CardWidth(float width) const;
			float				_CardHeight() const;
			BRect				_Card(int32 index) const;
			void				_DrawCard(int32 index, BRect frame);
			void				_DrawThumb(const theme_info& theme,
									BRect thumb);
			void				_DrawSaveCard(BRect frame);

			std::vector<theme_info> fThemes;
			BString				fSelected;
			bool				fChanged;
			int32				fMode;
			BMessenger			fTarget;
};


static const int32 kColumns = 5;


ThemeCards::ThemeCards()
	:
	BView("theme cards", B_WILL_DRAW | B_FRAME_EVENTS),
	fChanged(false),
	fMode(B_THEME_LIGHT)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
}


void
ThemeCards::SetTarget(BHandler* target)
{
	fTarget = BMessenger(target);
}


void
ThemeCards::SetThemes(const std::vector<theme_info>& themes,
	const char* selected, bool changed, int32 mode)
{
	fThemes = themes;
	fSelected = selected;
	fChanged = changed;
	fMode = mode;
	InvalidateLayout();
	Invalidate();
}


float
ThemeCards::_CardWidth(float width) const
{
	return floorf((width + 1 - px(12) * (kColumns - 1)) / kColumns);
}


float
ThemeCards::_CardHeight() const
{
	font_height bold;
	r5_settings_bold_font()->GetHeight(&bold);
	font_height plain;
	r5_settings_plain_font()->GetHeight(&plain);
	return px(6) + px(78) + px(7) + ceilf(bold.ascent + bold.descent)
		+ ceilf(plain.ascent + plain.descent) + px(6) + 2;
}


BRect
ThemeCards::_Card(int32 index) const
{
	float width = _CardWidth(Bounds().Width());
	int32 column = index % kColumns;
	int32 row = index / kColumns;
	// one pixel kept free round each card for the chosen ring
	float x = column * (width + px(12));
	float y = row * (_CardHeight() + px(12)) + 1;
	return BRect(x + 1, y, x + width - 2, y + _CardHeight() - 1);
}


bool
ThemeCards::HasHeightForWidth()
{
	return true;
}


void
ThemeCards::GetHeightForWidth(float width, float* min, float* max,
	float* preferred)
{
	int32 count = (int32)fThemes.size() + 1;
	int32 rows = (count + kColumns - 1) / kColumns;
	float height = rows * _CardHeight() + (rows - 1) * px(12) + px(4) - 1;
	if (min != NULL)
		*min = height;
	if (max != NULL)
		*max = height;
	if (preferred != NULL)
		*preferred = height;
}


BSize
ThemeCards::MinSize()
{
	float height;
	GetHeightForWidth(px(600), &height, NULL, NULL);
	return BSize(px(500), height);
}


void
ThemeCards::FrameResized(float width, float height)
{
	Invalidate();
}


void
ThemeCards::Draw(BRect updateRect)
{
	SetDrawingMode(B_OP_ALPHA);
	for (int32 i = 0; i < (int32)fThemes.size(); i++)
		_DrawCard(i, _Card(i));
	_DrawSaveCard(_Card((int32)fThemes.size()));
}


void
ThemeCards::_DrawThumb(const theme_info& theme, BRect thumb)
{
	bool classic = is_classic(theme);
	bool darkTheme = !classic && theme.mode == B_THEME_DARK;
	bool modern = strcmp(theme.decorator, "R5ModernDecorator") == 0;
	bool modernControls = strcmp(theme.control_look,
		"R5ModernControlLook") == 0;
	float r = modern ? px(6) : 0;

	// the mockup's colours for each theme
	rgb_color desk = classic ? hex(0x336698)
		: darkTheme ? hex(0x1e2a3f) : hex(0xbcd2f3);
	rgb_color window = classic ? hex(0xd8d8d8)
		: darkTheme ? (modernControls ? hex(0x262b34) : hex(0x2a2f39))
		: modernControls ? kWhite : hex(0xededed);
	rgb_color tab = classic ? hex(0xffd800)
		: darkTheme ? hex(0xe8b923) : hex(0xffd21f);
	bool classicBar = theme.deskbar_style == B_THEME_DESKBAR_CLASSIC;
	rgb_color bar = classicBar ? hex(0xd8d8d8)
		: darkTheme ? hex(0x1c212b) : hex(0xeff3f9);
	if (theme.accent.alpha != 0 && !classic) {
		// your own theme: the desktop takes a tint of its accent
		desk = blend(desk, theme.accent, darkTheme ? 0.18f : 0.12f);
	}

	PushState();
	BRegion clip;
	clip.Set(BRect(thumb.left, thumb.top, thumb.right + 1, thumb.bottom + 1));
	ConstrainClippingRegion(&clip);
	fill_round(this, thumb, px(6), desk);

	// the Deskbar: right 8, top 8, 28 by 40
	BRect deskbar(thumb.right - px(8) - px(28) + 1, thumb.top + px(8),
		thumb.right - px(8), thumb.top + px(8) + px(40) - 1);
	rgb_color shadow = hex(0x000000);
	shadow.alpha = 46;
	SetHighColor(shadow);
	// round only when the theme's Deskbar is (external review 2.7)
	float deskbarRadius = !classicBar && theme.deskbar_rounded ? px(6) : 0;
	FillRoundRect(deskbar.OffsetByCopy(0, 2), deskbarRadius, deskbarRadius);
	fill_round(this, deskbar, deskbarRadius, bar);

	// the window: left 12, top 22, 70 by 44, and its tab of 40 by 10
	BRect win(thumb.left + px(12), thumb.top + px(22),
		thumb.left + px(12) + px(70) - 1, thumb.top + px(22) + px(44) - 1);
	shadow.alpha = 64;
	SetHighColor(shadow);
	FillRoundRect(BRect(win.left, win.top + 3, win.right + 1,
		win.bottom + 4), r, r);
	BRect tabRect(win.left, thumb.top + px(14), win.left + px(40) - 1,
		thumb.top + px(14) + px(10) - 1);
	BRect tabShape(tabRect.left, tabRect.top, tabRect.right + 1,
		tabRect.bottom + 1 + r);
	SetHighColor(tab);
	FillRoundRect(tabShape, r, r);
	fill_round(this, win, r, window);
	PopState();
}


void
ThemeCards::_DrawCard(int32 index, BRect frame)
{
	const theme_info& theme = fThemes[index];
	bool selected = fSelected == theme.name;
	rgb_color accent = ui_color(B_CONTROL_HIGHLIGHT_COLOR);

	if (selected) {
		BRect ring(frame);
		ring.InsetBy(-1, -1);
		stroke_round(this, ring, px(10) + 1, accent);
		stroke_round(this, frame, px(10), accent);
	} else
		stroke_round(this, frame, px(10), line_color());

	BRect thumb(frame);
	thumb.InsetBy(px(6), px(6));
	thumb.bottom = thumb.top + px(78) - 1;
	_DrawThumb(theme, thumb);

	const BFont* bold = r5_settings_bold_font();
	const BFont* plain = r5_settings_plain_font();
	font_height boldHeight;
	bold->GetHeight(&boldHeight);
	font_height plainHeight;
	plain->GetHeight(&plainHeight);

	BString name(theme.name);
	if (selected && fChanged)
		name << " (changed)";
	bold->TruncateString(&name, B_TRUNCATE_END, thumb.Width());
	float y = thumb.bottom + 1 + px(7) + ceilf(boldHeight.ascent);
	draw_text(this, name.String(), BPoint(thumb.left, y), bold,
		ui_color(B_PANEL_TEXT_COLOR));

	BString sub;
	if (!is_built_in_theme(theme.name))
		sub = "Your theme";
	else if (strcmp(theme.name, "Classic (Haiku)") == 0)
		sub = "Built in · light only";
	else
		sub = "Built in";
	BFont small(plain);
	small.SetSize(plain->Size() * 11.5f / 12.5f);
	small.TruncateString(&sub, B_TRUNCATE_END, thumb.Width());
	y += ceilf(boldHeight.descent) + ceilf(plainHeight.ascent);
	draw_text(this, sub.String(), BPoint(thumb.left, y), &small, text2_color());
}


//! The dashed card: "Save as theme…", "Keep these choices".
void
ThemeCards::_DrawSaveCard(BRect frame)
{
	rgb_color line = ui_color(B_CONTROL_BORDER_COLOR);
	if (dark())
		line = blend(ui_color(B_PANEL_BACKGROUND_COLOR), kWhite, 0.22f);
	float r = px(10);

	// a dashed line, 4 on and 3 off, on the straight sides; the corners
	// solid
	PushState();
	BRegion corners;
	corners.Include(BRect(frame.left, frame.top, frame.left + r,
		frame.top + r));
	corners.Include(BRect(frame.right - r, frame.top, frame.right,
		frame.top + r));
	corners.Include(BRect(frame.left, frame.bottom - r, frame.left + r,
		frame.bottom));
	corners.Include(BRect(frame.right - r, frame.bottom - r, frame.right,
		frame.bottom));
	for (float x = frame.left + r + 1; x < frame.right - r; x += 7) {
		corners.Include(BRect(x, frame.top, std::min(x + 3, frame.right - r),
			frame.top));
		corners.Include(BRect(x, frame.bottom, std::min(x + 3,
			frame.right - r), frame.bottom));
	}
	for (float y = frame.top + r + 1; y < frame.bottom - r; y += 7) {
		corners.Include(BRect(frame.left, y, frame.left, std::min(y + 3,
			frame.bottom - r)));
		corners.Include(BRect(frame.right, y, frame.right, std::min(y + 3,
			frame.bottom - r)));
	}
	ConstrainClippingRegion(&corners);
	stroke_round(this, frame, r, line);
	PopState();

	BRect thumb(frame);
	thumb.InsetBy(px(6), px(6));
	thumb.bottom = thumb.top + px(78) - 1;
	fill_round(this, thumb, px(6), well_color());

	float side = px(26);
	BRect circle(floorf((thumb.left + thumb.right + 1 - side) / 2),
		floorf((thumb.top + thumb.bottom + 1 - side) / 2), 0, 0);
	circle.right = circle.left + side - 1;
	circle.bottom = circle.top + side - 1;
	PushState();
	SetPenSize(px(2));
	SetHighColor(text2_color());
	BRect ring(circle);
	ring.InsetBy(px(1), px(1));
	StrokeEllipse(BRect(ring.left, ring.top, ring.right + 1, ring.bottom + 1));
	// the plus inside the ring (owner's decision on review point 1.7): two
	// bars of 10, round ends, the ring's colour
	SetDrawingMode(B_OP_ALPHA);
	SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);
	BPoint center((circle.left + circle.right + 1) / 2,
		(circle.top + circle.bottom + 1) / 2);
	float arm = px(10) / 2;
	StrokeLine(BPoint(center.x - arm, center.y),
		BPoint(center.x + arm, center.y));
	StrokeLine(BPoint(center.x, center.y - arm),
		BPoint(center.x, center.y + arm));
	PopState();

	const BFont* bold = r5_settings_bold_font();
	const BFont* plain = r5_settings_plain_font();
	font_height boldHeight;
	bold->GetHeight(&boldHeight);
	font_height plainHeight;
	plain->GetHeight(&plainHeight);
	float y = thumb.bottom + 1 + px(7) + ceilf(boldHeight.ascent);
	draw_text(this, "Save as theme…", BPoint(thumb.left, y), bold,
		ui_color(B_PANEL_TEXT_COLOR));
	BFont small(plain);
	small.SetSize(plain->Size() * 11.5f / 12.5f);
	y += ceilf(boldHeight.descent) + ceilf(plainHeight.ascent);
	draw_text(this, "Keep these choices", BPoint(thumb.left, y), &small,
		text2_color());
}


void
ThemeCards::MouseDown(BPoint where)
{
	for (int32 i = 0; i <= (int32)fThemes.size(); i++) {
		if (!_Card(i).Contains(where))
			continue;
		if (i == (int32)fThemes.size()) {
			fTarget.SendMessage(kMsgSaveTheme);
			return;
		}
		BMessage message(kMsgPickTheme);
		message.AddString("name", fThemes[i].name);
		fTarget.SendMessage(&message);
		return;
	}
}


// #pragma mark - PartsLabel, ScrollBarPreview


//! The label of the Parts box: a chevron that folds the box, and "Parts".
class PartsLabel : public BView {
public:
								PartsLabel();

			void				SetOpen(bool open);
			bool				IsOpen() const { return fOpen; }
			void				SetTarget(BHandler* target);

	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();

private:
			bool				fOpen;
			BMessenger			fTarget;
};


PartsLabel::PartsLabel()
	:
	BView("parts label", B_WILL_DRAW),
	fOpen(true)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
}


void
PartsLabel::SetTarget(BHandler* target)
{
	fTarget = BMessenger(target);
}


void
PartsLabel::SetOpen(bool open)
{
	fOpen = open;
	Invalidate();
}


BSize
PartsLabel::PreferredSize()
{
	const BFont* bold = r5_settings_bold_font();
	font_height height;
	bold->GetHeight(&height);
	return BSize(px(6) + px(11) + px(6) + ceilf(bold->StringWidth("Parts"))
		+ px(6), ceilf(height.ascent + height.descent) + 2);
}


BSize
PartsLabel::MinSize()
{
	return PreferredSize();
}


BSize
PartsLabel::MaxSize()
{
	return PreferredSize();
}


void
PartsLabel::Draw(BRect updateRect)
{
	const BFont* bold = r5_settings_bold_font();
	font_height height;
	bold->GetHeight(&height);
	BRect bounds = Bounds();
	float baseline = floorf((bounds.Height() + 1 + ceilf(height.ascent)
		- ceilf(height.descent)) / 2);

	rgb_color text = ui_color(B_PANEL_TEXT_COLOR);
	float side = px(11);
	float x = px(6);
	float middle = floorf((bounds.Height() + 1) / 2) + 0.5f;
	PushState();
	SetDrawingMode(B_OP_ALPHA);
	SetHighColor(text);
	SetPenSize(std::max(1.5f, px(11) * 3 / 24));
	SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);
	float q = side / 4;
	if (fOpen) {
		StrokeLine(BPoint(x + q * 0.5f, middle - q * 0.75f),
			BPoint(x + side / 2, middle + q * 0.75f));
		StrokeLine(BPoint(x + side / 2, middle + q * 0.75f),
			BPoint(x + side - q * 0.5f, middle - q * 0.75f));
	} else {
		StrokeLine(BPoint(x + q, middle - q * 1.5f),
			BPoint(x + q * 2.5f, middle));
		StrokeLine(BPoint(x + q * 2.5f, middle),
			BPoint(x + q, middle + q * 1.5f));
	}
	PopState();
	draw_text(this, "Parts", BPoint(x + side + px(6), baseline), bold, text);
}


void
PartsLabel::MouseDown(BPoint where)
{
	fTarget.SendMessage(kMsgParts);
}


//! The live scroll bar preview of the Scrollbars row (150 by 14).
class ScrollBarPreview : public BView {
public:
								ScrollBarPreview();

	virtual	void				Draw(BRect updateRect);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();
};


ScrollBarPreview::ScrollBarPreview()
	:
	BView("scroll bar preview", B_WILL_DRAW)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
}


BSize
ScrollBarPreview::PreferredSize()
{
	// the look's width (Thin, Haiku width, Wide): the window lays out again
	// when the look or its width changes (external review 1.12)
	float height = be_control_look != NULL
		? be_control_look->GetScrollBarWidth(B_HORIZONTAL)
		: B_H_SCROLL_BAR_HEIGHT;
	return BSize(px(150) - 1, height);
}


BSize
ScrollBarPreview::MinSize()
{
	return PreferredSize();
}


BSize
ScrollBarPreview::MaxSize()
{
	return PreferredSize();
}


void
ScrollBarPreview::Draw(BRect updateRect)
{
	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);
	BRect bounds = Bounds();
	uint32 flags = BControlLook::B_PARTIALLY_ACTIVATED;
	be_control_look->DrawScrollBarBorder(this, bounds, updateRect, base, flags,
		B_HORIZONTAL);
	BRect inside(bounds);
	inside.InsetBy(1, 1);
	BRect thumb(inside.left + px(34), inside.top, inside.left + px(34) + px(64),
		inside.bottom);
	BRect before(inside.left, inside.top, thumb.left - 1, inside.bottom);
	BRect after(thumb.right + 1, inside.top, inside.right, inside.bottom);
	be_control_look->DrawScrollBarBackground(this, before, after, updateRect,
		base, flags, B_HORIZONTAL);
	be_control_look->DrawScrollBarThumb(this, thumb, updateRect,
		ui_color(B_SCROLL_BAR_THUMB_COLOR), flags, B_HORIZONTAL, BControlLook::B_KNOB_NONE);
}


// #pragma mark - SaveThemeWindow


/*!	"Save as theme" (A3): a name, which parts the theme keeps, Cancel and
	Save. It sends kMsgSaveThemeDone with "name" and "parts" to the page.
*/
class SaveThemeWindow : public BWindow {
public:
								SaveThemeWindow(BWindow* parent,
									BHandler* target, const theme_info& theme,
									const char* windowsLabel,
									const char* controlsLabel);

	virtual	void				MessageReceived(BMessage* message);

private:
			BTextControl*		fName;
			BCheckBox*			fParts[6];
			BButton*			fSave;
			BMessenger			fTarget;
};


static const uint32 kMsgSaveNameChanged = 'svNc';
static const uint32 kMsgSaveConfirm = 'svOk';
static const uint32 kPartBits[6] = {
	B_THEME_WINDOWS, B_THEME_CONTROLS, B_THEME_SCROLL_BARS, B_THEME_DESKBAR,
	B_THEME_COLORS, B_THEME_EFFECTS
};


//! The views of a dialog on the document colour, as mockup A3.
static void
set_page_colors(BView* view)
{
	if (view == NULL)
		return;
	if (dynamic_cast<BTextView*>(view) == NULL) {
		view->SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
		view->SetLowUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	}
	for (int32 i = 0; i < view->CountChildren(); i++)
		set_page_colors(view->ChildAt(i));
}


SaveThemeWindow::SaveThemeWindow(BWindow* parent, BHandler* target,
	const theme_info& theme, const char* windowsLabel,
	const char* controlsLabel)
	:
	BWindow(BRect(0, 0, px(440) - 1, px(320) - 1), "Save as theme",
		B_TITLED_WINDOW_LOOK, B_MODAL_SUBSET_WINDOW_FEEL,
		B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE
			| B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fTarget(target)
{
	if (parent != NULL)
		AddToSubset(parent);

	BStringView* nameLabel = new BStringView("name label", "Name");
	nameLabel->SetFont(r5_settings_bold_font());
	BString suggestion(is_built_in_theme(theme.name) ? "" : theme.name);
	fName = new BTextControl("name", NULL, suggestion.String(),
		new BMessage(kMsgSaveNameChanged));
	fName->SetModificationMessage(new BMessage(kMsgSaveNameChanged));

	BStringView* keepLabel = new BStringView("keep", "Keep in this theme");
	keepLabel->SetFont(r5_settings_bold_font());

	BString labels[6];
	labels[0] << "Windows · " << windowsLabel;
	labels[1] << "Controls · " << controlsLabel;
	labels[2] << "Scrollbars · "
		<< (theme.double_arrows ? "double arrows" : "single arrows");
	labels[3] << "Deskbar · "
		<< (theme.deskbar_style == B_THEME_DESKBAR_CLASSIC
			? "Classic (Haiku)" : "Default (R5)")
		<< ", " << (theme.deskbar_layout == B_THEME_DESKBAR_COMPACT ? "Compact"
			: theme.deskbar_layout == B_THEME_DESKBAR_HORIZONTAL ? "Horizontal"
			: "Vertical");
	int32 accent = -1;
	for (int32 i = 0; i < count_theme_accents(); i++) {
		rgb_color color = theme_accent_at(i, theme.mode);
		if (theme.accent.alpha != 0 && color.red == theme.accent.red
			&& color.green == theme.accent.green
			&& color.blue == theme.accent.blue)
			accent = i;
	}
	BString accentName(accent >= 0 ? theme_accent_name(accent)
		: theme.accent.alpha != 0 ? "own accent" : "Blue");
	accentName.ToLower();
	labels[4] << "Mode and colors · "
		<< (theme.mode == B_THEME_DARK ? "Dark" : "Light") << ", "
		<< accentName;
	labels[5] = "Effects";

	for (int32 i = 0; i < 6; i++) {
		fParts[i] = new BCheckBox(labels[i].String(), labels[i].String(),
			new BMessage(kMsgSaveNameChanged));
		fParts[i]->SetValue(i < 5 ? B_CONTROL_ON : B_CONTROL_OFF);
	}

	BStringView* note = new QuietLabel("note",
		"Saved in your own settings. The built-in themes stay as they are.");
	BFont small(r5_settings_plain_font());
	small.SetSize(small.Size() * 12.0f / 12.5f);
	note->SetFont(&small);

	BButton* cancel = new BButton("Cancel", new BMessage(B_QUIT_REQUESTED));
	fSave = new BButton("Save", new BMessage(kMsgSaveConfirm));

	float spacing = px(14);
	BLayoutBuilder::Group<>(this, B_VERTICAL, spacing)
		.SetInsets(px(22), px(20), px(22), px(20))
		.AddGroup(B_VERTICAL, px(8))
			.Add(nameLabel)
			.Add(fName)
		.End()
		.Add(keepLabel)
		.AddGrid(px(14), px(9))
			.Add(fParts[0], 0, 0)
			.Add(fParts[1], 1, 0)
			.Add(fParts[2], 0, 1)
			.Add(fParts[3], 1, 1)
			.Add(fParts[4], 0, 2)
			.Add(fParts[5], 1, 2)
		.End()
		.Add(note)
		.AddGlue()
		.AddGroup(B_HORIZONTAL, px(8))
			.AddGlue()
			.Add(cancel)
			.Add(fSave)
		.End();

	SetDefaultButton(fSave);
	fSave->SetEnabled(suggestion.Length() > 0);
	// the white page of mockup A3 (the document colour; external review
	// 1.13): every view of the dialog, as they adopted the panel colour
	// when they were attached
	set_page_colors(ChildAt(0));
	r5_settings_apply_plain(ChildAt(0));
	nameLabel->SetFont(r5_settings_bold_font());
	keepLabel->SetFont(r5_settings_bold_font());
	note->SetFont(&small);
	fName->MakeFocus(true);

	if (parent != NULL)
		CenterIn(parent->Frame());
}


void
SaveThemeWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSaveNameChanged:
		{
			BString name(fName->Text());
			name.Trim();
			// a name, and at least one part (external review 2.3)
			bool parts = false;
			for (int32 i = 0; i < 6; i++)
				parts = parts || fParts[i]->Value() == B_CONTROL_ON;
			fSave->SetEnabled(name.Length() > 0 && parts);
			break;
		}

		case kMsgSaveConfirm:
		{
			BString name(fName->Text());
			name.Trim();
			uint32 parts = 0;
			for (int32 i = 0; i < 6; i++) {
				if (fParts[i]->Value() == B_CONTROL_ON)
					parts |= kPartBits[i];
			}
			if (name.Length() == 0 || parts == 0)
				break;
			if (is_built_in_theme(name.String())) {
				BAlert* alert = new BAlert("Save as theme",
					"This is the name of a built-in theme. The built-in themes "
					"stay as they are; choose another name.", "OK");
				alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
				alert->Go(NULL);
				break;
			}
			bool control = false;
			for (int32 i = 0; i < name.Length(); i++) {
				if (static_cast<uint8>(name[i]) < 0x20)
					control = true;
			}
			if (control || name == "." || name == ".."
				|| name.FindFirst('/') >= 0 || name.FindFirst('"') >= 0
				|| name.FindFirst('\\') >= 0) {
				BAlert* alert = new BAlert("Save as theme",
					"A theme name cannot contain / \\ or \".", "OK");
				alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
				alert->Go(NULL);
				break;
			}
			BMessage done(kMsgSaveThemeDone);
			done.AddString("name", name);
			done.AddUInt32("parts", parts);
			fTarget.SendMessage(&done);
			Quit();
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


// #pragma mark - ThemePage


class ThemeModule;


class ThemePage : public BGroupView {
public:
								ThemePage(ThemeModule* module);
	virtual						~ThemePage();

	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);

			void				Revert();
			void				Defaults();
			bool				Modified() const;
			bool				CanDefaults() const;

private:
			void				_BuildWindowsMenu();
			void				_BuildControlsMenu();
			void				_Update();
			void				_Apply(const theme_info& info, uint32 parts);
			void				_ApplyPart(uint32 part);
			void				_ShowAbout(bool windows);
			BString				_WindowsLabel(const char* shortcut);
			bool				_IsChanged() const;
			void				_ReadEffects();

			ThemeModule*		fModule;
			theme_info			fOpened;
			BMessage			fOpenedColors;
			theme_info			fCurrent;

			BButton*			fSaveButton;
			BButton*			fDeleteButton;
			ThemeCards*			fCards;
			SegmentedControl*	fMode;
			AccentControl*		fAccent;
			PartsLabel*			fPartsLabel;
			BBox*				fPartsBox;
			BView*				fPartsGrid;

			BPopUpMenu*			fWindowsMenu;
			BMenuField*			fWindows;
			BPopUpMenu*			fControlsMenu;
			BMenuField*			fControls;
			BPopUpMenu*			fScrollMenu;
			BMenuField*			fScroll;
			SegmentedControl*	fArrows;
			BPopUpMenu*			fWidthMenu;
			BMenuField*			fWidth;
			ScrollBarPreview*	fPreview;
			BPopUpMenu*			fStyleMenu;
			BMenuField*			fStyle;
			BPopUpMenu*			fLayoutMenu;
			BMenuField*			fLayout;
			BCheckBox*			fRounded;
			BCheckBox*			fFloating;
			BCheckBox*			fShadow;
			BCheckBox*			fGlass;
			BCheckBox*			fAnimations;
			BPopUpMenu*			fStrengthMenu;
			BMenuField*			fStrength;
};


class ThemeModule : public SettingsModule {
public:
								ThemeModule();

	virtual	const char*			Identifier() const { return kModuleId; }
	virtual	const char*			Name() const { return "Theme"; }
	virtual	const char*			Category() const
									{ return "personalization"; }
	virtual	const char*			Description() const
		{ return "Choose how windows, controls and the Deskbar look"; }
	virtual	const char*			Keywords() const
		{ return "theme dark light mode accent colors look controls "
			"decorator windows scrollbars deskbar effects"; }
	virtual	BView*				CreateView(SettingsContext* context,
									BRect frame);
	virtual	status_t			LoadSettings(SettingsContext* context);
	virtual	status_t			SaveSettings(SettingsContext* context);
	virtual	status_t			RevertSettings(SettingsContext* context);
	virtual	bool				IsModified() const;
	virtual	bool				CanApplyDefaults() const;
	virtual	status_t			ApplyDefaults(SettingsContext* context);

			void				ClearPage(ThemePage* page);
			void				Touch();

private:
			ThemePage*			fPage;
			SettingsContext*	fContext;
};


static BMenuItem*
add_item(BPopUpMenu* menu, const char* label, uint32 what,
	const char* field = NULL, const char* value = NULL, int32 number = -1)
{
	BMessage* message = new BMessage(what);
	if (field != NULL && value != NULL)
		message->AddString(field, value);
	if (number >= 0)
		message->AddInt32("value", number);
	BMenuItem* item = new BMenuItem(label, message);
	menu->AddItem(item);
	return item;
}


static BStringView*
row_label(const char* text)
{
	BStringView* label = new QuietLabel(text, text);
	label->SetAlignment(B_ALIGN_RIGHT);
	label->SetExplicitAlignment(BAlignment(B_ALIGN_RIGHT,
		B_ALIGN_VERTICAL_CENTER));
	return label;
}


ThemePage::ThemePage(ThemeModule* module)
	:
	BGroupView("theme page", B_VERTICAL, 0),
	fModule(module)
{
	get_theme(&fCurrent);
	fOpened = fCurrent;
	// the colours as they are now, also ones set in Appearance > Colors:
	// Revert puts them back (every colour a theme sets)
	BMessage names;
	get_theme_colors(&fCurrent, &names);
	char* name;
	type_code type;
	for (int32 i = 0; names.GetInfo(B_RGB_32_BIT_TYPE, i, &name, &type)
			== B_OK; i++) {
		color_which which = which_ui_color(name);
		if (which != B_NO_COLOR)
			fOpenedColors.AddColor(name, ui_color(which));
	}

	BStringView* title = new BStringView("theme title", "Theme");
	fSaveButton = new BButton("Save as theme…", new BMessage(kMsgSaveTheme));
	fDeleteButton = new BButton("Delete", new BMessage(kMsgDeleteTheme));

	fCards = new ThemeCards();

	BStringView* modeLabel = new BStringView("mode label", "Mode");
	fMode = new SegmentedControl("mode", new BMessage(kMsgMode), 26, 12);
	fMode->AddSegment("Light", R5_GLYPH_SUN);
	fMode->AddSegment("Dark", R5_GLYPH_MOON);
	BStringView* accentLabel = new BStringView("accent label", "Accent");
	fAccent = new AccentControl(new BMessage(kMsgAccent));

	// Parts
	fWindowsMenu = new BPopUpMenu("windows");
	_BuildWindowsMenu();
	fWindows = new BMenuField("windows", NULL, fWindowsMenu);
	BButton* windowsAbout = new BButton("windows about", "About",
		new BMessage(kMsgWindowsAbout));

	fControlsMenu = new BPopUpMenu("controls");
	_BuildControlsMenu();
	fControls = new BMenuField("controls", NULL, fControlsMenu);
	BButton* controlsAbout = new BButton("controls about", "About",
		new BMessage(kMsgControlsAbout));

	fScrollMenu = new BPopUpMenu("scroll bars");
	add_item(fScrollMenu, "R5 Modern", kMsgScrollBars, NULL, NULL, 1);
	add_item(fScrollMenu, "Haiku", kMsgScrollBars, NULL, NULL, 0);
	fScroll = new BMenuField("scroll bars", NULL, fScrollMenu);
	fArrows = new SegmentedControl("arrows", new BMessage(kMsgArrows), 22, 10);
	fArrows->AddSegment("Single");
	fArrows->AddSegment("Double");
	// the width a theme gives its scroll bars (Haiku's stays the default)
	fWidthMenu = new BPopUpMenu("scroll bar width");
	add_item(fWidthMenu, "Thin", kMsgScrollWidth, NULL, NULL,
		B_THEME_SCROLL_BAR_THIN);
	add_item(fWidthMenu, "Haiku width", kMsgScrollWidth, NULL, NULL, 0);
	add_item(fWidthMenu, "Wide", kMsgScrollWidth, NULL, NULL,
		B_THEME_SCROLL_BAR_WIDE);
	fWidth = new BMenuField("scroll bar width", NULL, fWidthMenu);
	fPreview = new ScrollBarPreview();

	fStyleMenu = new BPopUpMenu("deskbar style");
	add_item(fStyleMenu, "Default (R5)", kMsgDeskbarStyle, NULL, NULL,
		B_THEME_DESKBAR_R5);
	add_item(fStyleMenu, "Classic (Haiku)", kMsgDeskbarStyle, NULL, NULL,
		B_THEME_DESKBAR_CLASSIC);
	fStyle = new BMenuField("deskbar style", NULL, fStyleMenu);
	fLayoutMenu = new BPopUpMenu("deskbar layout");
	add_item(fLayoutMenu, "Vertical", kMsgDeskbarLayout, NULL, NULL,
		B_THEME_DESKBAR_VERTICAL);
	add_item(fLayoutMenu, "Compact", kMsgDeskbarLayout, NULL, NULL,
		B_THEME_DESKBAR_COMPACT);
	add_item(fLayoutMenu, "Horizontal", kMsgDeskbarLayout, NULL, NULL,
		B_THEME_DESKBAR_HORIZONTAL);
	fLayout = new BMenuField("deskbar layout", NULL, fLayoutMenu);
	fRounded = new BCheckBox("rounded", "Rounded",
		new BMessage(kMsgDeskbarCheck));
	fFloating = new BCheckBox("floating", "Away from the screen edges",
		new BMessage(kMsgDeskbarCheck));

	fShadow = new BCheckBox("shadow", "Shadow", new BMessage(kMsgEffects));
	fGlass = new BCheckBox("glass", "Frosted glass", new BMessage(kMsgEffects));
	fAnimations = new BCheckBox("animations", "Animations",
		new BMessage(kMsgEffects));
	BStringView* strengthLabel = new QuietLabel("strength label", "Strength");
	fStrengthMenu = new BPopUpMenu("strength");
	add_item(fStrengthMenu, "Quiet", kMsgStrength, NULL, NULL,
		B_WINDOW_EFFECTS_QUIET);
	add_item(fStrengthMenu, "Balanced", kMsgStrength, NULL, NULL,
		B_WINDOW_EFFECTS_BALANCED);
	add_item(fStrengthMenu, "Glassy", kMsgStrength, NULL, NULL,
		B_WINDOW_EFFECTS_GLASSY);
	fStrength = new BMenuField("strength", NULL, fStrengthMenu);

	// the mockup's widths: menus of 220, 140, 110 and 104 at 12 pt
	fWindows->SetExplicitSize(BSize(px(220), B_SIZE_UNSET));
	fControls->SetExplicitSize(BSize(px(220), B_SIZE_UNSET));
	fScroll->SetExplicitSize(BSize(px(140), B_SIZE_UNSET));
	fStyle->SetExplicitSize(BSize(px(140), B_SIZE_UNSET));
	fLayout->SetExplicitSize(BSize(px(110), B_SIZE_UNSET));
	fStrength->SetExplicitSize(BSize(px(104), B_SIZE_UNSET));
	fWidth->SetExplicitSize(BSize(px(118), B_SIZE_UNSET));

	fPartsGrid = new BView("parts grid", 0);
	fPartsGrid->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	BLayoutBuilder::Grid<>(fPartsGrid, px(12), px(10))
		.SetInsets(px(14), px(6), px(14), px(14))
		.Add(row_label("Windows"), 0, 0)
		.AddGroup(B_HORIZONTAL, px(8), 1, 0)
			.Add(fWindows)
			.Add(windowsAbout)
			.AddGlue()
		.End()
		.Add(row_label("Controls"), 0, 1)
		.AddGroup(B_HORIZONTAL, px(8), 1, 1)
			.Add(fControls)
			.Add(controlsAbout)
			.AddGlue()
		.End()
		.Add(row_label("Scrollbars"), 0, 2)
		.AddGroup(B_HORIZONTAL, px(12), 1, 2)
			.Add(fScroll)
			.Add(fArrows)
			.Add(fWidth)
			.Add(fPreview)
			.AddGlue()
		.End()
		.Add(row_label("Deskbar"), 0, 3)
		.AddGroup(B_HORIZONTAL, px(12), 1, 3)
			.Add(fStyle)
			.Add(fLayout)
			.Add(fRounded)
			.Add(fFloating)
			.AddGlue()
		.End()
		.Add(row_label("Effects"), 0, 4)
		.AddGroup(B_HORIZONTAL, px(14), 1, 4)
			.Add(fShadow)
			.Add(fGlass)
			.Add(fAnimations)
			.Add(strengthLabel)
			.Add(fStrength)
			.AddGlue()
		.End();
	BGridLayout* grid = dynamic_cast<BGridLayout*>(fPartsGrid->GetLayout());
	if (grid != NULL) {
		grid->SetMinColumnWidth(0, px(92));
		grid->SetMaxColumnWidth(0, px(92));
		grid->SetColumnWeight(1, 1.0f);
	}

	fPartsLabel = new PartsLabel();
	fPartsBox = new BBox("parts");
	fPartsBox->SetLabel(fPartsLabel);
	fPartsBox->AddChild(fPartsGrid);

	// room under the Parts box for its bottom line and corners (external
	// review 1.13)
	BLayoutBuilder::Group<>(this, B_VERTICAL, px(14))
		.SetInsets(0, 0, 0, px(14))
		.AddGroup(B_HORIZONTAL, px(10))
			.Add(title)
			.AddGlue()
			.Add(fSaveButton)
			.Add(fDeleteButton)
		.End()
		.Add(fCards)
		.AddGroup(B_HORIZONTAL, px(22))
			.AddGroup(B_HORIZONTAL, px(10))
				.Add(modeLabel)
				.Add(fMode)
			.End()
			.AddGroup(B_HORIZONTAL, px(10))
				.Add(accentLabel)
				.Add(fAccent)
			.End()
			.AddGlue()
		.End()
		.Add(fPartsBox)
		.AddGlue();

	r5_settings_apply_plain(this);
	title->SetFont(r5_settings_bold_font());
	BFont semibold(r5_settings_bold_font());
	modeLabel->SetFont(&semibold);
	modeLabel->SetExplicitSize(BSize(px(52), B_SIZE_UNSET));
	accentLabel->SetFont(&semibold);
}


ThemePage::~ThemePage()
{
	if (fModule != NULL)
		fModule->ClearPage(this);
}


void
ThemePage::AttachedToWindow()
{
	BGroupView::AttachedToWindow();

	fSaveButton->SetTarget(this);
	fDeleteButton->SetTarget(this);
	fCards->SetTarget(this);
	fMode->SetTarget(this);
	fAccent->SetTarget(this);
	fPartsLabel->SetTarget(this);
	fWindowsMenu->SetTargetForItems(this);
	fControlsMenu->SetTargetForItems(this);
	fScrollMenu->SetTargetForItems(this);
	fArrows->SetTarget(this);
	fWidthMenu->SetTargetForItems(this);
	fStyleMenu->SetTargetForItems(this);
	fLayoutMenu->SetTargetForItems(this);
	fRounded->SetTarget(this);
	fFloating->SetTarget(this);
	fShadow->SetTarget(this);
	fGlass->SetTarget(this);
	fAnimations->SetTarget(this);
	fStrengthMenu->SetTargetForItems(this);
	for (int32 i = 0; i < fPartsGrid->CountChildren(); i++) {
		BButton* button = dynamic_cast<BButton*>(fPartsGrid->ChildAt(i));
		if (button != NULL)
			button->SetTarget(this);
	}
	// the About buttons sit in groups of the grid
	BView* about = FindView("windows about");
	if (BButton* button = dynamic_cast<BButton*>(about))
		button->SetTarget(this);
	about = FindView("controls about");
	if (BButton* button = dynamic_cast<BButton*>(about))
		button->SetTarget(this);

	_Update();
}


void
ThemePage::_BuildWindowsMenu()
{
	BPrivate::DecorInfoUtility decorators;
	BStringList names;
	BStringList shortcuts;
	for (int32 i = 0; i < decorators.CountDecorators(); i++) {
		BPrivate::DecorInfo* decor = decorators.DecoratorAt(i);
		if (decor == NULL || decor->IsDefault())
			continue;
		BString label = decorator_label(decor->Name());
		int32 index = 0;
		while (index < names.CountStrings()
			&& names.StringAt(index).ICompare(label) <= 0)
			index++;
		names.Add(label, index);
		shortcuts.Add(decor->ShortcutName(), index);
	}
	for (int32 i = 0; i < names.CountStrings(); i++) {
		add_item(fWindowsMenu, names.StringAt(i).String(), kMsgWindows,
			"decor", shortcuts.StringAt(i).String());
	}
	// Haiku's built-in one last, as Appearance lists it
	add_item(fWindowsMenu, "Classic (Haiku)", kMsgWindows, "decor", "");
}


void
ThemePage::_BuildControlsMenu()
{
	add_item(fControlsMenu, "Haiku", kMsgControls, "look", "");

	BStringList paths;
	BPathFinder finder;
	if (finder.FindPaths(B_FIND_PATH_ADD_ONS_DIRECTORY, "control_look",
			paths) != B_OK)
		return;
	BStringList leaves;
	for (int32 i = 0; i < paths.CountStrings(); i++) {
		BDirectory directory(paths.StringAt(i).String());
		BEntry entry;
		while (directory.GetNextEntry(&entry) == B_OK) {
			char name[B_FILE_NAME_LENGTH];
			if (entry.GetName(name) == B_OK && !leaves.HasString(name))
				leaves.Add(name);
		}
	}
	leaves.Sort();
	// R5 Modern first, then the others by name
	if (leaves.HasString("R5ModernControlLook")) {
		add_item(fControlsMenu, "R5 Modern", kMsgControls, "look",
			"R5ModernControlLook");
	}
	for (int32 i = 0; i < leaves.CountStrings(); i++) {
		if (leaves.StringAt(i) == "R5ModernControlLook")
			continue;
		add_item(fControlsMenu,
			control_look_label(leaves.StringAt(i).String()).String(),
			kMsgControls, "look", leaves.StringAt(i).String());
	}
}


BString
ThemePage::_WindowsLabel(const char* shortcut)
{
	for (int32 i = 0; i < fWindowsMenu->CountItems(); i++) {
		BMenuItem* item = fWindowsMenu->ItemAt(i);
		const char* decor;
		if (item->Message()->FindString("decor", &decor) == B_OK
			&& strcasecmp(decor, shortcut) == 0)
			return BString(item->Label());
	}
	return BString(shortcut);
}


static void
mark_string(BPopUpMenu* menu, const char* field, const char* value)
{
	for (int32 i = 0; i < menu->CountItems(); i++) {
		BMenuItem* item = menu->ItemAt(i);
		const char* itemValue;
		if (item->Message()->FindString(field, &itemValue) == B_OK
			&& strcasecmp(itemValue, value) == 0) {
			item->SetMarked(true);
			return;
		}
	}
}


static void
mark_number(BPopUpMenu* menu, int32 value)
{
	for (int32 i = 0; i < menu->CountItems(); i++) {
		BMenuItem* item = menu->ItemAt(i);
		if (item->Message()->GetInt32("value", -1) == value) {
			item->SetMarked(true);
			return;
		}
	}
}


bool
ThemePage::_IsChanged() const
{
	theme_info base;
	if (find_theme(fCurrent.base, &base) != B_OK)
		return false;
	if (is_built_in_theme(base.name) && !is_classic(base))
		base.mode = fCurrent.mode;
	return !theme_matches(&fCurrent, &base);
}


void
ThemePage::_ReadEffects()
{
	fCurrent.effects = window_effects();
	fCurrent.effect_strength = window_effect_strength();
}


void
ThemePage::_Update()
{
	_ReadEffects();

	std::vector<theme_info> themes;
	for (int32 i = 0; i < count_themes(); i++) {
		theme_info info;
		if (get_theme_at(i, &info) != B_OK)
			continue;
		// a built-in card shows the mode in use
		if (is_built_in_theme(info.name) && !is_classic(info))
			info.mode = fCurrent.mode;
		themes.push_back(info);
	}
	fCards->SetThemes(themes, fCurrent.base, _IsChanged(), fCurrent.mode);

	bool classic = is_classic(fCurrent);
	fMode->SetValue(fCurrent.mode == B_THEME_DARK ? 1 : 0);
	fMode->SetSegmentEnabled(1, !classic);

	int32 accent = -1;
	bool modern = strcmp(fCurrent.control_look, "R5ModernControlLook") == 0;
	if (fCurrent.accent.alpha == 0) {
		if (fCurrent.mode == B_THEME_DARK || modern)
			accent = 0;
	} else {
		for (int32 i = 0; i < count_theme_accents(); i++) {
			rgb_color color = theme_accent_at(i, fCurrent.mode);
			if (color.red == fCurrent.accent.red
				&& color.green == fCurrent.accent.green
				&& color.blue == fCurrent.accent.blue)
				accent = i;
		}
	}
	fAccent->SetMode(fCurrent.mode);
	fAccent->SetValue(accent);
	fAccent->SetEnabled(!classic);

	mark_string(fWindowsMenu, "decor", fCurrent.decorator);
	mark_string(fControlsMenu, "look", fCurrent.control_look);
	mark_number(fScrollMenu, modern && fCurrent.modern_scroll_bars ? 1 : 0);
	fScroll->SetEnabled(modern);
	fArrows->SetValue(fCurrent.double_arrows ? 1 : 0);
	int32 width = static_cast<int32>(roundf(fCurrent.scroll_bar_width));
	if (width != B_THEME_SCROLL_BAR_THIN && width != B_THEME_SCROLL_BAR_WIDE)
		width = 0;
	mark_number(fWidthMenu, modern ? width : 0);
	fWidth->SetEnabled(modern);
	fPreview->Invalidate();

	mark_number(fStyleMenu, fCurrent.deskbar_style);
	mark_number(fLayoutMenu, fCurrent.deskbar_layout);
	bool r5Deskbar = fCurrent.deskbar_style == B_THEME_DESKBAR_R5;
	fLayoutMenu->ItemAt(1)->SetEnabled(r5Deskbar);
	fRounded->SetValue(fCurrent.deskbar_rounded);
	fRounded->SetEnabled(r5Deskbar);
	fFloating->SetValue(fCurrent.deskbar_floating);
	fFloating->SetEnabled(r5Deskbar);

	fShadow->SetValue((fCurrent.effects & B_WINDOW_SHADOW) != 0);
	fGlass->SetValue((fCurrent.effects & B_WINDOW_FROSTED_GLASS) != 0);
	fAnimations->SetValue((fCurrent.effects & B_WINDOW_ANIMATIONS) != 0);
	mark_number(fStrengthMenu, fCurrent.effect_strength);

	theme_info saved;
	fDeleteButton->SetEnabled(!is_built_in_theme(fCurrent.base)
		&& find_theme(fCurrent.base, &saved) == B_OK);
}


void
ThemePage::_Apply(const theme_info& info, uint32 parts)
{
	theme_info next = info;
	status_t status = set_theme(&next, parts);
	if (status != B_OK) {
		BString text("Not every part of the theme could be set: ");
		text << strerror(status);
		BAlert* alert = new BAlert("Theme", text.String(), "OK");
		alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
		alert->Go(NULL);
		get_theme(&fCurrent);
	} else {
		// app_server sets the colours a moment later, so the mode is not
		// read back now; B_COLORS_UPDATED brings the page up to date
		fCurrent = next;
		if (is_classic(fCurrent))
			fCurrent.mode = B_THEME_LIGHT;
	}
	_Update();
	if (fModule != NULL)
		fModule->Touch();
}


//! One part changed on the page: the theme keeps its name and base.
void
ThemePage::_ApplyPart(uint32 part)
{
	_Apply(fCurrent, part);
}


void
ThemePage::_ShowAbout(bool windows)
{
	BString text;
	if (windows) {
		BPrivate::DecorInfoUtility decorators;
		BPrivate::DecorInfo* decor = decorators.FindDecorator(
			fCurrent.decorator[0] != '\0' ? BString(fCurrent.decorator)
				: BString("Default"));
		if (decor == NULL)
			return;
		text << _WindowsLabel(fCurrent.decorator) << "\n\n";
		if (decor->Authors().Length() > 0)
			text << "Authors: " << decor->Authors() << "\n";
		if (decor->LicenseName().Length() > 0)
			text << "License: " << decor->LicenseName() << "\n";
		if (decor->ShortDescription().Length() > 0)
			text << "\n" << decor->ShortDescription();
	} else {
		text << control_look_label(fCurrent.control_look) << "\n\n";
		if (fCurrent.control_look[0] == '\0')
			text << "The controls of Haiku, built into the system.";
		else {
			BPathFinder finder;
			BPath path;
			BString subPath("control_look/");
			subPath << fCurrent.control_look;
			if (finder.FindPath(B_FIND_PATH_ADD_ONS_DIRECTORY, subPath.String(),
					B_FIND_PATH_EXISTING_ONLY, path) == B_OK) {
				BFile file(path.Path(), B_READ_ONLY);
				BAppFileInfo info(&file);
				version_info version;
				if (info.InitCheck() == B_OK
					&& info.GetVersionInfo(&version, B_APP_VERSION_KIND)
						== B_OK) {
					text << version.long_info;
				}
			}
		}
	}
	BAlert* alert = new BAlert("About", text.String(), "OK");
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go(NULL);
}


void
ThemePage::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgPickTheme:
		{
			const char* name;
			theme_info info;
			if (message->FindString("name", &name) != B_OK
				|| find_theme(name, &info) != B_OK)
				break;
			uint32 parts = info.parts;
			if (is_built_in_theme(info.name)) {
				// a built-in theme in the mode in use (Classic (Haiku) is
				// light only)
				info.mode = is_classic(info) ? B_THEME_LIGHT : fCurrent.mode;
			}
			_Apply(info, parts);
			break;
		}

		case kMsgMode:
		{
			int32 mode = fMode->Value() == 1 ? B_THEME_DARK : B_THEME_LIGHT;
			if (mode == fCurrent.mode)
				break;
			// the accent follows to its colour in the other mode
			for (int32 i = 0; i < count_theme_accents(); i++) {
				rgb_color color = theme_accent_at(i, fCurrent.mode);
				if (fCurrent.accent.alpha != 0
					&& color.red == fCurrent.accent.red
					&& color.green == fCurrent.accent.green
					&& color.blue == fCurrent.accent.blue) {
					fCurrent.accent = theme_accent_at(i, mode);
					break;
				}
			}
			fCurrent.mode = mode;
			_ApplyPart(B_THEME_COLORS);
			break;
		}

		case kMsgAccent:
		{
			int32 index = fAccent->Value();
			if (index < 0)
				break;
			fCurrent.accent = theme_accent_at(index, fCurrent.mode);
			_ApplyPart(B_THEME_COLORS);
			break;
		}

		case kMsgParts:
		{
			bool open = !fPartsLabel->IsOpen();
			fPartsLabel->SetOpen(open);
			if (open)
				fPartsGrid->Show();
			else
				fPartsGrid->Hide();
			break;
		}

		case kMsgWindows:
		{
			const char* decor;
			if (message->FindString("decor", &decor) != B_OK)
				break;
			strlcpy(fCurrent.decorator, decor, sizeof(fCurrent.decorator));
			_ApplyPart(B_THEME_WINDOWS);
			break;
		}

		case kMsgControls:
		{
			const char* look;
			if (message->FindString("look", &look) != B_OK)
				break;
			strlcpy(fCurrent.control_look, look,
				sizeof(fCurrent.control_look));
			// the R5 Modern controls come with their scroll bars
			fCurrent.modern_scroll_bars
				= strcmp(look, "R5ModernControlLook") == 0;
			_ApplyPart(B_THEME_CONTROLS | B_THEME_SCROLL_BARS);
			break;
		}

		case kMsgScrollBars:
			fCurrent.modern_scroll_bars = message->GetInt32("value", 0) == 1;
			_ApplyPart(B_THEME_SCROLL_BARS);
			break;

		case kMsgArrows:
			fCurrent.double_arrows = fArrows->Value() == 1;
			_ApplyPart(B_THEME_SCROLL_BARS);
			break;

		case kMsgScrollWidth:
			fCurrent.scroll_bar_width = message->GetInt32("value", 0);
			_ApplyPart(B_THEME_SCROLL_BARS);
			break;

		case kMsgDeskbarStyle:
			fCurrent.deskbar_style = message->GetInt32("value",
				B_THEME_DESKBAR_R5);
			if (fCurrent.deskbar_style == B_THEME_DESKBAR_CLASSIC
				&& fCurrent.deskbar_layout == B_THEME_DESKBAR_COMPACT)
				fCurrent.deskbar_layout = B_THEME_DESKBAR_VERTICAL;
			_ApplyPart(B_THEME_DESKBAR);
			break;

		case kMsgDeskbarLayout:
			fCurrent.deskbar_layout = message->GetInt32("value",
				B_THEME_DESKBAR_VERTICAL);
			_ApplyPart(B_THEME_DESKBAR);
			break;

		case kMsgDeskbarCheck:
			fCurrent.deskbar_rounded = fRounded->Value() == B_CONTROL_ON;
			fCurrent.deskbar_floating = fFloating->Value() == B_CONTROL_ON;
			_ApplyPart(B_THEME_DESKBAR);
			break;

		case kMsgEffects:
		case kMsgStrength:
		{
			uint32 effects = 0;
			if (fShadow->Value() == B_CONTROL_ON)
				effects |= B_WINDOW_SHADOW;
			if (fGlass->Value() == B_CONTROL_ON)
				effects |= B_WINDOW_FROSTED_GLASS;
			if (fAnimations->Value() == B_CONTROL_ON)
				effects |= B_WINDOW_ANIMATIONS;
			fCurrent.effects = effects;
			if (message->what == kMsgStrength) {
				fCurrent.effect_strength = message->GetInt32("value",
					B_WINDOW_EFFECTS_BALANCED);
			}
			_ApplyPart(B_THEME_EFFECTS);
			break;
		}

		case kMsgWindowsAbout:
			_ShowAbout(true);
			break;

		case kMsgControlsAbout:
			_ShowAbout(false);
			break;

		case kMsgSaveTheme:
		{
			BString windows = _WindowsLabel(fCurrent.decorator);
			BString controls = control_look_label(fCurrent.control_look);
			SaveThemeWindow* window = new SaveThemeWindow(Window(), this,
				fCurrent, windows.String(), controls.String());
			window->Show();
			break;
		}

		case kMsgSaveThemeDone:
		{
			const char* name;
			uint32 parts;
			if (message->FindString("name", &name) != B_OK
				|| message->FindUInt32("parts", &parts) != B_OK)
				break;
			theme_info info = fCurrent;
			strlcpy(info.name, name, sizeof(info.name));
			status_t status = save_theme(&info, parts);
			if (status != B_OK) {
				BString text("The theme could not be saved: ");
				text << strerror(status);
				(new BAlert("Save as theme", text.String(), "OK"))->Go(NULL);
				break;
			}
			// the new theme is the one in use now, unchanged
			strlcpy(info.base, name, sizeof(info.base));
			_Apply(info, 0);
			break;
		}

		case kMsgDeleteTheme:
		{
			if (is_built_in_theme(fCurrent.base))
				break;
			BString text;
			text << "Delete the theme \"" << fCurrent.base << "\"? What you "
				"see now stays as it is.";
			BAlert* alert = new BAlert("Delete", text.String(), "Cancel",
				"Delete", NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetShortcut(0, B_ESCAPE);
			if (alert->Go() != 1)
				break;
			if (remove_theme(fCurrent.base) == B_OK) {
				// what is on screen stays; it is a changed R5 now
				theme_info info = fCurrent;
				strlcpy(info.name, "R5", sizeof(info.name));
				strlcpy(info.base, "R5", sizeof(info.base));
				_Apply(info, 0);
			}
			break;
		}

		case B_COLORS_UPDATED:
			// the theme changed elsewhere (another program, the test tool,
			// Appearance); the page shows what is in use
			get_theme(&fCurrent);
			_Update();
			BGroupView::MessageReceived(message);
			break;

		default:
			BGroupView::MessageReceived(message);
			break;
	}
}


void
ThemePage::Revert()
{
	_Apply(fOpened, B_THEME_ALL_PARTS);
	set_ui_colors(&fOpenedColors);
}


void
ThemePage::Defaults()
{
	// R5, light, with the factory effects (all on, Balanced)
	theme_info info;
	find_theme("R5", &info);
	info.mode = B_THEME_LIGHT;
	info.parts |= B_THEME_EFFECTS;
	_Apply(info, info.parts);
}


bool
ThemePage::Modified() const
{
	theme_info opened = fOpened;
	opened.parts = B_THEME_ALL_PARTS;
	return !theme_matches(&fCurrent, &opened)
		|| strcmp(fCurrent.base, fOpened.base) != 0;
}


bool
ThemePage::CanDefaults() const
{
	theme_info info;
	find_theme("R5", &info);
	info.mode = B_THEME_LIGHT;
	info.parts |= B_THEME_EFFECTS;
	return strcmp(fCurrent.base, "R5") != 0
		|| !theme_matches(&fCurrent, &info);
}


// #pragma mark - ThemeModule


ThemeModule::ThemeModule()
	:
	fPage(NULL),
	fContext(NULL)
{
}


BView*
ThemeModule::CreateView(SettingsContext* context, BRect frame)
{
	(void)frame;
	fContext = context;
	fPage = new ThemePage(this);
	return fPage;
}


void
ThemeModule::ClearPage(ThemePage* page)
{
	if (fPage == page)
		fPage = NULL;
}


void
ThemeModule::Touch()
{
	if (fContext != NULL)
		fContext->NotifyChanged(Identifier());
}


status_t
ThemeModule::LoadSettings(SettingsContext* context)
{
	fContext = context;
	return B_OK;
}


status_t
ThemeModule::SaveSettings(SettingsContext* context)
{
	// every change was applied (and the theme kept by libr5ui) at once
	(void)context;
	return B_OK;
}


status_t
ThemeModule::RevertSettings(SettingsContext* context)
{
	(void)context;
	if (fPage != NULL)
		fPage->Revert();
	return B_OK;
}


bool
ThemeModule::IsModified() const
{
	return fPage != NULL && fPage->Modified();
}


bool
ThemeModule::CanApplyDefaults() const
{
	return fPage != NULL && fPage->CanDefaults();
}


status_t
ThemeModule::ApplyDefaults(SettingsContext* context)
{
	(void)context;
	if (fPage != NULL)
		fPage->Defaults();
	return B_OK;
}


void
register_theme_page(SettingsManager& manager)
{
	manager.Add(new ThemeModule());
}
