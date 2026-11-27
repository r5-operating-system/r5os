/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


#include "AomiWindow.h"

#include "AomiDownloads.h"
#include "AomiMessages.h"
#include "AomiSettings.h"
#include "AomiPages.h"
#include "ServoContentView.h"
#include "WebContentView.h"
#include "r5_pebble.inc"

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <Clipboard.h>
#include <Entry.h>
#include <File.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <GroupLayout.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <NetworkNotifications.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <Screen.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>

#include <R5Icons.h>
#include <Themes.h>
#include <WindowEffects.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Aomi"


static float
ui_scale()
{
	float size = be_plain_font != NULL ? be_plain_font->Size() : 12.0f;
	if (size < 8.0f)
		size = 12.0f;
	return size / 12.0f;
}


static inline float
sc(float px)
{
	return floorf(px * ui_scale() + 0.5f);
}


static inline float tabs_h() { return sc(34); }
static inline float nav_h() { return sc(42); }
static inline float marks_h() { return sc(26); }


struct Palette {
	rgb_color	panel;
	rgb_color	text;
	rgb_color	sub;
	rgb_color	field;
	rgb_color	fieldText;
	rgb_color	fline;
	rgb_color	ctl;
	rgb_color	cline;
	rgb_color	hair;
	rgb_color	accent;
	bool		dark;
};


static rgb_color
rgb(int r, int g, int b)
{
	rgb_color c;
	c.red = (uint8)r;
	c.green = (uint8)g;
	c.blue = (uint8)b;
	c.alpha = 255;
	return c;
}


static rgb_color
mix(rgb_color a, rgb_color b, float t)
{
	rgb_color c;
	c.red = (uint8)(a.red + (b.red - a.red) * t);
	c.green = (uint8)(a.green + (b.green - a.green) * t);
	c.blue = (uint8)(a.blue + (b.blue - a.blue) * t);
	c.alpha = 255;
	return c;
}


// The chrome's colours come from the theme (ui_color()), so every R5
// theme, light and dark, and a changed accent follow without Aomi code
// (docs/design/AOMI-UI.md, "Glass with the fallback"). Only the new tab
// page and the private label keep colours of their own.
static Palette
palette()
{
	Palette p;
	p.panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	p.text = ui_color(B_PANEL_TEXT_COLOR);
	p.dark = p.panel.IsDark();
	p.sub = mix(p.text, p.panel, 0.42f);
	p.field = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
	p.fieldText = ui_color(B_DOCUMENT_TEXT_COLOR);
	p.fline = mix(p.panel, p.text, 0.26f);
	p.ctl = ui_color(B_CONTROL_BACKGROUND_COLOR);
	p.cline = mix(p.panel, p.text, 0.24f);
	p.hair = mix(p.panel, p.text, 0.13f);
	p.accent = ui_color(B_STATUS_BAR_COLOR);
	return p;
}


// True while the compositor shows frosted glass behind glass views: then
// the toolbar leaves its background to the glass. Asked when the window is
// shown and when the effects change, not on every draw (each ask is a
// message to app_server).
static bool sGlass = false;


static void
update_glass()
{
	sGlass = BAppearanceKit::window_effects_active()
		&& (BAppearanceKit::window_effects()
			& BAppearanceKit::B_WINDOW_FROSTED_GLASS) != 0;
}


class TabProxy : public BHandler {
public:
	TabProxy(AomiWindow* window, int32 index)
		:
		BHandler("aomi tab"),
		fWindow(window),
		fIndex(index)
	{
	}

	void SetIndex(int32 index) { fIndex = index; }

	virtual void MessageReceived(BMessage* message)
	{
		int32 index = fWindow->TabIndexForProxy(this);
		if (index < 0)
			return;
		if (message->what == AOMI_DIALOG || message->what == AOMI_NEW_WINDOW)
			fWindow->HandleEngine(message, index);
		else {
			BMessage copy(*message);
			copy.AddPointer("proxy", this);
			copy.AddInt32("tab", index);
			fWindow->PostMessage(&copy);
		}
	}

private:
	AomiWindow*	fWindow;
	int32		fIndex;
};


class AddressField : public BTextView {
public:
	AddressField()
		:
		BTextView("address")
	{
		MakeResizable(false);
		SetWordWrap(false);
		SetInsets(0, 0, 0, 0);
		BFont font(be_plain_font);
		font.SetSize(13.5f * ui_scale());
		SetFontAndColor(&font);
	}

	virtual void KeyDown(const char* bytes, int32 numBytes)
	{
		if (numBytes == 1 && (bytes[0] == B_ENTER || bytes[0] == B_RETURN)) {
			Window()->PostMessage(MSG_GO);
			return;
		}
		if (numBytes == 1 && bytes[0] == B_ESCAPE) {
			Window()->PostMessage('esc!');
			return;
		}
		BTextView::KeyDown(bytes, numBytes);
	}
};


class ChromeView : public BView {
public:
	ChromeView(AomiWindow* window)
		:
		// B_FRAME_EVENTS: FrameResized() places the address field. Without
		// it the field kept its size from AttachedToWindow(), before the
		// layout gave the view a width: negative, so typed text was never
		// shown (stick 27 Nov 2026). The same holds for NewTabView.
		BView("chrome", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE
			| B_FRAME_EVENTS),
		fWindow(window),
		fField(new AddressField()),
		fFocus(false)
	{
		SetViewColor(palette().panel);
		AddChild(fField);
		ApplyColors();
	}

	// The address field is a document field: theme colours, so typed text
	// stays readable in dark mode too.
	void ApplyColors()
	{
		Palette p = palette();
		SetViewColor(p.panel);
		BFont font(be_plain_font);
		font.SetSize(13.5f * ui_scale());
		fField->SetViewColor(p.field);
		fField->SetLowColor(p.field);
		fField->SetFontAndColor(&font, B_FONT_ALL, &p.fieldText);
		fField->Invalidate();
		Invalidate();
	}

	float Height() const
	{
		return tabs_h() + nav_h() + (fWindow->Prefs().showBookmarks ? marks_h() : 0);
	}

	void ApplyFontScale()
	{
		ApplyColors();
		_Place();
		Invalidate();
	}

	virtual void AttachedToWindow()
	{
		BView::AttachedToWindow();
		fField->MakeFocus(false);
		_Place();
	}

	virtual void FrameResized(float, float)
	{
		_Place();
		Invalidate();
	}

	virtual void Draw(BRect)
	{
		Palette p = palette();
		float s = ui_scale();
		float kTabs = tabs_h();
		float kNav = nav_h();
		float kMarks = marks_h();

		SetLowColor(p.panel);
		if (sGlass) {
			// app_server filled the background with the panel colour at
			// alpha 0; the blur shows through it. Composite drawing keeps
			// the alpha of what is drawn on top (docs/LIBR5.md, "Window
			// effects"). Without the GPU the panel is opaque.
			SetDrawingMode(B_OP_ALPHA);
			SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
		} else {
			SetDrawingMode(B_OP_COPY);
			FillRect(Bounds(), B_SOLID_LOW);
		}
		BRect hair(Bounds());
		SetHighColor(p.hair);
		StrokeLine(hair.LeftBottom(), hair.RightBottom());

		BFont font(be_plain_font);
		font.SetSize(12.5f * s);
		SetFont(&font);
		font_height tfh;
		font.GetHeight(&tfh);
		float x = sc(8);
		float tabH = sc(27);
		float tabW = sc(209);
		float tabStep = sc(214);
		float tabRad = sc(7);
		float y = kTabs - sc(28);
		int32 count = fWindow->CountTabs();
		int32 active = fWindow->Active();
		// The tab strip shares the width; from 32 tabs on the rest are
		// reached with Command-1 to 9 and the window menu.
		if (count > 32)
			count = 32;
		if (count > 0 && x + count * tabStep + tabH > Bounds().right) {
			tabStep = floorf((Bounds().right - x - tabH - sc(8)) / count);
			if (tabStep < sc(60))
				tabStep = sc(60);
			tabW = tabStep - sc(5);
		}
		fTabCount = count;
		for (int32 i = 0; i < count; i++) {
			BRect tab(x, y, x + tabW, y + tabH);
			if (i == active) {
				tab.bottom += 1;
				SetHighColor(p.field);
				FillRoundRect(tab, tabRad, tabRad);
				SetHighColor(p.hair);
				StrokeRoundRect(tab, tabRad, tabRad);
				SetHighColor(p.text);
				font.SetFace(B_BOLD_FACE);
			} else {
				SetHighColor(p.sub);
				font.SetFace(B_REGULAR_FACE);
			}
			SetFont(&font);
			BString title(fWindow->TabTitle(i));
			if (title.Length() == 0)
				title = B_TRANSLATE("New tab");
			float room = tabW - sc(40);
			font.TruncateString(&title, B_TRUNCATE_END, room);
			float base = tab.top + (tab.Height() + tfh.ascent - tfh.descent) / 2.0f;
			DrawString(title.String(), BPoint(tab.left + sc(10), base));
			BRect close(tab.right - sc(22), tab.top + sc(6),
				tab.right - sc(6), tab.top + sc(22));
			r5_glyph_draw(this, R5_GLYPH_CROSS, close, p.sub);
			fClose[i] = close;
			fTab[i] = tab;
			x += tabStep;
		}
		fPlus = BRect(x, y, x + tabH, y + tabH);
		r5_glyph_draw(this, R5_GLYPH_PLUS, fPlus.InsetByCopy(sc(4), sc(4)), p.sub);

		float navY = kTabs;
		SetHighColor(p.hair);
		StrokeLine(BPoint(0, navY), BPoint(Bounds().right, navY));
		float by = navY + sc(6);
		float btnSize = sc(29);
		float btnStep = sc(36);
		x = sc(8);
		fBack = _Button(x, by, R5_GLYPH_BACK, fWindow->CanBack(), p);
		x += btnStep;
		fForward = _Button(x, by, R5_GLYPH_FORWARD, fWindow->CanForward(), p);
		x += btnStep;
		r5_glyph which = fWindow->Loading() ? R5_GLYPH_CROSS : R5_GLYPH_RELOAD;
		fReload = _Button(x, by, which, true, p);
		x += btnStep;
		if (fWindow->Prefs().showHome) {
			fHome = _Button(x, by, R5_GLYPH_HOME, true, p);
			x += btnStep;
		} else {
			fHome = BRect();
		}
		float right = Bounds().right - sc(8);
		fMenu = BRect(right - btnSize, by, right, by + btnSize);
		_Icon(fMenu, R5_GLYPH_MENU, p.text, p);
		right -= btnStep;
		fDownloads = BRect(right - btnSize, by, right, by + btnSize);
		bool running = aomi_download_count_running() > 0;
		_Icon(fDownloads, R5_GLYPH_DOWNLOAD, running ? p.accent : p.text, p);
		if (running) {
			// A file is still coming in: a dot in the accent colour.
			float d = sc(7);
			BRect dot(fDownloads.right - d - sc(1), fDownloads.top + sc(1),
				fDownloads.right - sc(1), fDownloads.top + sc(1) + d);
			SetHighColor(p.accent);
			FillEllipse(dot);
		}
		right -= btnStep;
		if (fWindow->Private()) {
			BFont small(be_plain_font);
			small.SetSize(12.0f * s);
			small.SetFace(B_BOLD_FACE);
			SetFont(&small);
			font_height sfh;
			small.GetHeight(&sfh);
			float w = small.StringWidth(B_TRANSLATE("Private")) + sc(28);
			fPill = BRect(right - w, by + sc(3), right, by + btnSize - sc(3));
			SetHighColor(p.dark ? rgb(60, 40, 90) : rgb(237, 231, 250));
			FillRoundRect(fPill, sc(12), sc(12));
			r5_glyph_draw(this, R5_GLYPH_MASK,
				BRect(fPill.left + sc(6), fPill.top + sc(2),
					fPill.left + sc(20), fPill.bottom - sc(2)),
				p.dark ? rgb(196, 181, 253) : rgb(124, 58, 237));
			SetHighColor(p.dark ? rgb(196, 181, 253) : rgb(124, 58, 237));
			float pbase = fPill.top + (fPill.Height() + sfh.ascent - sfh.descent) / 2.0f;
			DrawString(B_TRANSLATE("Private"), BPoint(fPill.left + sc(22), pbase));
			right = fPill.left - sc(6);
		} else {
			fPill = BRect();
		}
		fURL = BRect(x, by, right, by + btnSize);
		SetHighColor(p.field);
		FillRoundRect(fURL, tabRad, tabRad);
		SetHighColor(fFocus ? p.accent : p.fline);
		StrokeRoundRect(fURL, tabRad, tabRad);
		BString address(fWindow->Address());
		bool blank = address.Length() == 0 || address.StartsWith("aomi:");
		if (!blank && address.StartsWith("http")) {
			rgb_color lock = fWindow->Security() == AOMI_SECURITY_SECURE
				? rgb(27, 127, 69) : p.sub;
			if (fWindow->Security() == AOMI_SECURITY_CERTIFICATE)
				lock = rgb(180, 40, 40);
			fLock = BRect(fURL.left + sc(6), fURL.top + sc(6),
				fURL.left + sc(22), fURL.bottom - sc(6));
			r5_glyph_draw(this, R5_GLYPH_LOCK, fLock, lock);
		} else {
			fLock = BRect(fURL.left + sc(6), fURL.top + sc(5),
				fURL.left + sc(24), fURL.bottom - sc(5));
			r5_glyph_draw(this, R5_GLYPH_SEARCH, fLock, p.sub);
		}
		fStar = BRect(fURL.right - sc(24), fURL.top + sc(6),
			fURL.right - sc(6), fURL.bottom - sc(6));
		r5_glyph_draw(this, R5_GLYPH_STAR, fStar,
			fWindow->Bookmarked() ? p.accent : p.sub);
		if (!fFocus) {
			BFont text(be_plain_font);
			text.SetSize(13.5f * s);
			SetFont(&text);
			font_height ufh;
			text.GetHeight(&ufh);
			float ubase = fURL.top + (fURL.Height() + ufh.ascent - ufh.descent) / 2.0f;
			BPoint pen(fURL.left + sc(30), ubase);
			if (blank) {
				SetHighColor(p.sub);
				DrawString(B_TRANSLATE("Search or type an address"), pen);
			} else {
				int32 scheme = address.FindFirst("://");
				int32 host = scheme >= 0 ? scheme + 3 : 0;
				int32 slash = address.FindFirst('/', host);
				BString bold(address);
				BString rest;
				if (slash > host) {
					bold.Truncate(slash);
					rest.SetTo(address.String() + slash);
				}
				text.SetFace(B_BOLD_FACE);
				SetFont(&text);
				SetHighColor(p.text);
				float maxRight = fStar.left - sc(6);
				if (pen.x < maxRight) {
					BString drawBold(bold);
					text.TruncateString(&drawBold, B_TRUNCATE_END,
						maxRight - pen.x);
					DrawString(drawBold.String(), pen);
					pen.x += text.StringWidth(drawBold.String());
				}
				if (rest.Length() > 0 && pen.x < maxRight) {
					text.SetFace(B_REGULAR_FACE);
					SetFont(&text);
					SetHighColor(p.sub);
					BString drawRest(rest);
					text.TruncateString(&drawRest, B_TRUNCATE_END,
						maxRight - pen.x);
					DrawString(drawRest.String(), pen);
				}
			}
		}
		if (fWindow->Loading()) {
			float width = fURL.Width() * fWindow->TabProgress(active);
			if (width < sc(40))
				width = Bounds().Width() * 0.2f;
			SetHighColor(p.accent);
			FillRect(BRect(0, navY + kNav - 2, width, navY + kNav - 1));
		}
		fBarCount = 0;
		if (fWindow->Prefs().showBookmarks) {
			float by2 = kTabs + kNav;
			SetHighColor(p.hair);
			StrokeLine(BPoint(0, by2 + kMarks - 1),
				BPoint(Bounds().right, by2 + kMarks - 1));
			// The "Bookmark bar" folder, by name; Tracker sorts and renames.
			BFont small(be_plain_font);
			small.SetSize(12.0f * s);
			SetFont(&small);
			font_height bfh;
			small.GetHeight(&bfh);
			float bx = sc(10);
			float base = by2 + (kMarks + bfh.ascent - bfh.descent) / 2.0f - 1;
			if (fBarTitles.empty()) {
				SetHighColor(p.sub);
				DrawString(B_TRANSLATE("Bookmarks in the folder "
					"\"Bookmark bar\" are shown here."), BPoint(bx, base));
			}
			for (size_t i = 0; i < fBarTitles.size() && i < 32; i++) {
				BString title(fBarTitles[i]);
				small.TruncateString(&title, B_TRUNCATE_END, sc(160));
				float w = small.StringWidth(title.String()) + sc(16);
				if (bx + w > Bounds().right - sc(8))
					break;
				fBar[i] = BRect(bx, by2 + sc(3), bx + w, by2 + kMarks - sc(4));
				SetHighColor(p.text);
				DrawString(title.String(), BPoint(bx + sc(8), base));
				bx += w + sc(4);
				fBarCount = (int32)i + 1;
			}
		}
	}

	virtual void MouseDown(BPoint where)
	{
		int32 buttons = 0;
		Window()->CurrentMessage()->FindInt32("buttons", &buttons);
		for (int32 i = 0; i < fBarCount; i++) {
			if (fBar[i].Contains(where)) {
				BMessage open(MSG_OPEN_BOOKMARK);
				open.AddString("url", fBarUrls[i]);
				Window()->PostMessage(&open);
				return;
			}
		}
		int32 count = fTabCount;
		for (int32 i = 0; i < count; i++) {
			if (fClose[i].Contains(where)) {
				BMessage close(MSG_CLOSE_TAB);
				close.AddInt32("tab", i);
				Window()->PostMessage(&close);
				return;
			}
			if (fTab[i].Contains(where)) {
				if ((buttons & B_TERTIARY_MOUSE_BUTTON) != 0) {
					BMessage close(MSG_CLOSE_TAB);
					close.AddInt32("tab", i);
					Window()->PostMessage(&close);
				} else {
					BMessage select(MSG_SELECT_TAB);
					select.AddInt32("tab", i);
					Window()->PostMessage(&select);
				}
				return;
			}
		}
		if (fPlus.Contains(where)) {
			Window()->PostMessage(MSG_NEW_TAB);
			return;
		}
		if (fBack.Contains(where) && fWindow->CanBack()) {
			Window()->PostMessage(MSG_BACK);
			return;
		}
		if (fForward.Contains(where) && fWindow->CanForward()) {
			Window()->PostMessage(MSG_FORWARD);
			return;
		}
		if (fReload.Contains(where)) {
			Window()->PostMessage(MSG_RELOAD);
			return;
		}
		if (fHome.IsValid() && fHome.Contains(where)) {
			Window()->PostMessage(MSG_HOME);
			return;
		}
		if (fDownloads.Contains(where)) {
			Window()->PostMessage(MSG_DOWNLOADS);
			return;
		}
		if (fMenu.Contains(where)) {
			Window()->PostMessage(MSG_MENU);
			return;
		}
		if (fLock.Contains(where)) {
			Window()->PostMessage(MSG_LOCK);
			return;
		}
		if (fStar.Contains(where)) {
			Window()->PostMessage(MSG_STAR);
			return;
		}
		if (fURL.Contains(where))
			FocusAddress();
	}

	void ShowAddress(const char* text, bool focus)
	{
		fFocus = focus;
		if (text != NULL)
			fField->SetText(text);
		if (focus) {
			// On the field as it is drawn now (fURL comes from the last
			// Draw()), shown, then focused.
			_Place();
			_ShowField(true);
			fField->MakeFocus(true);
			fField->Invalidate();
		} else {
			fField->MakeFocus(false);
			_ShowField(false);
		}
		Invalidate();
	}

	const char* FieldText() const { return fField->Text(); }

	// Command-L: the address, selected, ready to be typed over.
	void FocusAddress()
	{
		BString address(fWindow->Address());
		if (address.StartsWith("aomi:"))
			address = "";
		ShowAddress(address.String(), true);
		fField->SelectAll();
	}

	void ReloadBar()
	{
		aomi_bookmark_bar(&fBarTitles, &fBarUrls);
		Invalidate();
	}

	BRect DownloadsRect() const { return fDownloads; }
	BRect MenuRect() const { return fMenu; }
	BRect LockRect() const { return fLock; }

private:
	BRect _Button(float x, float y, r5_glyph glyph, bool on, const Palette& p)
	{
		float bSize = sc(29);
		float rad = sc(7);
		float inset = sc(5);
		BRect rect(x, y, x + bSize, y + bSize);
		SetHighColor(p.ctl);
		FillRoundRect(rect, rad, rad);
		SetHighColor(p.cline);
		StrokeRoundRect(rect, rad, rad);
		rgb_color color = on ? p.text : mix(p.text, p.panel, 0.62f);
		r5_glyph_draw(this, glyph, rect.InsetByCopy(inset, inset), color);
		return rect;
	}

	void _Icon(BRect rect, r5_glyph glyph, rgb_color color, const Palette& p)
	{
		(void)p;
		float inset = sc(5);
		r5_glyph_draw(this, glyph, rect.InsetByCopy(inset, inset), color);
	}

	// BView::Hide() and Show() nest: _Place() runs on every resize, so
	// hiding an already hidden field made one Show() leave it hidden. It
	// then took the keys without drawing them (stick 27 Nov 2026).
	void _ShowField(bool show)
	{
		if (show == fField->IsHidden(fField)) {
			if (show)
				fField->Show();
			else
				fField->Hide();
		}
	}

	void _Place()
	{
		float kTabs = tabs_h();
		float bSize = sc(29);
		if (!fURL.IsValid())
			fURL = BRect(sc(120), kTabs + sc(6), Bounds().right - sc(80),
				kTabs + sc(6) + bSize);
		fField->MoveTo(fURL.left + sc(28), fURL.top + sc(4));
		fField->ResizeTo(fURL.Width() - sc(56), sc(20));
		// Placed by hand, not by a layout: without word wrap,
		// BTextView::FrameResized() only moves its text rect, which stayed
		// 0 x 0 from the layout constructor, so typed text was kept but
		// never drawn (stick 27 Nov 2026).
		fField->SetTextRect(fField->Bounds());
		if (!fFocus)
			_ShowField(false);
		SetExplicitMinSize(BSize(sc(400), Height()));
		SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, Height()));
	}

	AomiWindow*		fWindow;
	AddressField*	fField;
	bool			fFocus;
	int32			fTabCount = 0;
	BRect			fTab[32];
	BRect			fClose[32];
	BRect			fPlus;
	BRect			fBack;
	BRect			fForward;
	BRect			fReload;
	BRect			fHome;
	BRect			fURL;
	BRect			fLock;
	BRect			fStar;
	BRect			fDownloads;
	BRect			fMenu;
	BRect			fPill;
	std::vector<BString> fBarTitles;
	std::vector<BString> fBarUrls;
	BRect			fBar[32];
	int32			fBarCount = 0;
};


struct ShortcutTile {
	BString		badge;
	BString		label;
	BString		url;
	rgb_color	color;
};


// The tiles of the new tab page: the "Bookmark bar" folder (its first six,
// by name), or a few real sites while that folder is empty.
static void
shortcut_tiles(std::vector<ShortcutTile>* tiles)
{
	static const struct {
		const char*	label;
		const char*	url;
		uint8		r, g, b;
	} kDefaults[] = {
		{ "R5", "https://r5-operating-system.github.io/", 0, 132, 255 },
		{ "Haiku", "https://www.haiku-os.org/", 124, 58, 237 },
		{ "Servo", "https://servo.org/", 28, 34, 44 },
		{ "Wikipedia", "https://www.wikipedia.org/", 24, 148, 66 },
		{ "DuckDuckGo", "https://duckduckgo.com/", 222, 88, 51 },
	};
	static const uint8 kColors[][3] = {
		{ 0, 132, 255 }, { 124, 58, 237 }, { 24, 148, 66 }, { 222, 88, 51 },
		{ 14, 116, 144 }, { 190, 24, 93 }
	};
	tiles->clear();
	std::vector<BString> titles;
	std::vector<BString> urls;
	aomi_bookmark_bar(&titles, &urls);
	for (size_t i = 0; i < titles.size() && tiles->size() < 6; i++) {
		ShortcutTile tile;
		tile.label = titles[i];
		tile.url = urls[i];
		tile.color = rgb(kColors[i % 6][0], kColors[i % 6][1],
			kColors[i % 6][2]);
		tiles->push_back(tile);
	}
	if (tiles->empty()) {
		for (size_t i = 0; i < sizeof(kDefaults) / sizeof(kDefaults[0]); i++) {
			ShortcutTile tile;
			tile.label = kDefaults[i].label;
			tile.url = kDefaults[i].url;
			tile.color = rgb(kDefaults[i].r, kDefaults[i].g, kDefaults[i].b);
			tiles->push_back(tile);
		}
	}
	for (size_t i = 0; i < tiles->size(); i++) {
		ShortcutTile& tile = (*tiles)[i];
		// The badge: the first letter (a whole UTF-8 character), or "R5".
		if (tile.label == "R5")
			tile.badge = "R5";
		else
			tile.label.CopyCharsInto(tile.badge, 0, 1);
		tile.badge.ToUpper();
	}
}


class NewTabSearchInput : public BTextView {
public:
	NewTabSearchInput()
		:
		BTextView("ntp input")
	{
		MakeResizable(false);
		SetWordWrap(false);
		SetInsets(0, 0, 0, 0);
		BFont font(be_plain_font);
		font.SetSize(14.5f * ui_scale());
		SetFontAndColor(&font);
	}

	virtual void KeyDown(const char* bytes, int32 numBytes)
	{
		if (numBytes == 1 && (bytes[0] == B_ENTER || bytes[0] == B_RETURN)) {
			Window()->PostMessage(MSG_NTP_GO);
			return;
		}
		BTextView::KeyDown(bytes, numBytes);
	}
};


class NewTabView : public BView {
public:
	NewTabView(bool privat)
		:
		BView("new tab", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE
			| B_FRAME_EVENTS),
		fPrivate(privat),
		fPebble(NULL),
		fPebbleSize(0),
		fInput(new NewTabSearchInput())
	{
		SetViewColor(B_TRANSPARENT_COLOR);
		AddChild(fInput);
		_EnsurePebble();
		shortcut_tiles(&fShortcuts);
	}

	void ReloadTiles()
	{
		shortcut_tiles(&fShortcuts);
		Invalidate();
	}

	virtual ~NewTabView()
	{
		delete fPebble;
	}

	void ApplyFontScale()
	{
		_EnsurePebble();
		_Place();
		Invalidate();
	}

	virtual void AttachedToWindow()
	{
		BView::AttachedToWindow();
		_Place();
	}

	virtual void FrameResized(float, float)
	{
		_Place();
		Invalidate();
	}

	virtual void Draw(BRect)
	{
		Palette p = palette();
		float s = ui_scale();
		_EnsurePebble();
		BRect b = Bounds();
		for (int32 y = 0; y <= (int32)b.Height(); y += 2) {
			float t = b.Height() > 0 ? y / b.Height() : 0;
			rgb_color c;
			if (fPrivate)
				c = mix(rgb(25, 39, 86), rgb(15, 42, 74), t);
			else if (p.dark)
				c = mix(rgb(18, 50, 87), rgb(15, 42, 74), t);
			else
				c = mix(rgb(234, 244, 255), rgb(255, 255, 255), t);
			SetHighColor(c);
			FillRect(BRect(0, y, b.right, y + 1));
		}

		float cx = b.Width() / 2.0f;
		float pSz = (float)fPebbleSize;
		float logoTop = fPrivate ? sc(120) : sc(90);
		if (b.Height() > sc(620))
			logoTop = fPrivate ? sc(156) : sc(126);
		if (fPebble != NULL) {
			SetDrawingMode(B_OP_ALPHA);
			SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
			DrawBitmap(fPebble, BPoint(floorf(cx - pSz / 2.0f), logoTop));
			SetDrawingMode(B_OP_COPY);
		}

		BFont font(be_plain_font);
		font.SetSize(25.0f * s);
		font.SetFace(B_BOLD_FACE);
		SetFont(&font);
		SetHighColor(fPrivate || p.dark ? rgb(255, 255, 255) : rgb(15, 42, 74));
		const char* title = fPrivate ? B_TRANSLATE("Private window") : "Aomi";
		float tw = font.StringWidth(title);
		float titleY = logoTop + pSz + sc(34);
		DrawString(title, BPoint(cx - tw / 2, titleY));

		// Rounded search field matching mockup
		BRect pill = _SearchRect();
		float pillRad = pill.Height() / 2.0f;
		rgb_color fieldBg = (p.dark && !fPrivate) ? rgb(26, 32, 42) : rgb(255, 255, 255);
		rgb_color fieldFg = (p.dark && !fPrivate) ? rgb(230, 233, 239) : rgb(20, 25, 35);
		SetHighColor(fieldBg);
		FillRoundRect(pill, pillRad, pillRad);
		SetHighColor(p.accent);
		StrokeRoundRect(pill, pillRad, pillRad);
		float gSz = sc(16);
		float gTop = pill.top + (pill.Height() - gSz) / 2.0f;
		r5_glyph_draw(this, R5_GLYPH_SEARCH,
			BRect(pill.left + sc(16), gTop, pill.left + sc(16) + gSz, gTop + gSz),
			p.sub);

		fInput->SetViewColor(fieldBg);
		fInput->SetLowColor(fieldBg);
		BFont ifont(be_plain_font);
		ifont.SetSize(14.5f * s);
		fInput->SetFontAndColor(&ifont, B_FONT_ALL, &fieldFg);

		if (fInput->TextLength() == 0) {
			BFont ph(be_plain_font);
			ph.SetSize(14.5f * s);
			SetFont(&ph);
			font_height pfh;
			ph.GetHeight(&pfh);
			float pbase = pill.top + (pill.Height() + pfh.ascent - pfh.descent) / 2.0f;
			SetHighColor(p.sub);
			const char* placeholder = fPrivate
				? B_TRANSLATE("Search privately or type an address")
				: B_TRANSLATE("Search the web or type an address");
			DrawString(placeholder, BPoint(pill.left + sc(42), pbase));
		}

		if (fPrivate) {
			font.SetSize(13.5f * s);
			font.SetFace(B_REGULAR_FACE);
			SetFont(&font);
			SetHighColor(rgb(200, 214, 235));
			const char* note1 = B_TRANSLATE(
				"Aomi forgets the history, cookies and site data of this window when you close it.");
			float nw1 = font.StringWidth(note1);
			DrawString(note1, BPoint(cx - nw1 / 2, pill.bottom + sc(26)));
			const char* note2 = B_TRANSLATE("Downloads and new bookmarks stay.");
			float nw2 = font.StringWidth(note2);
			DrawString(note2, BPoint(cx - nw2 / 2, pill.bottom + sc(46)));
		} else {
			// 6 shortcut tiles matching mockup; nothing stray at bottom
			float cardW = sc(104);
			float cardH = sc(92);
			float gap = sc(14);
			int32 count = (int32)fShortcuts.size();
			float totalW = count * cardW + (count - 1) * gap;
			if (count > 0 && totalW > b.Width() - sc(32)) {
				cardW = floorf((b.Width() - sc(32) - (count - 1) * gap)
					/ count);
				totalW = count * cardW + (count - 1) * gap;
			}
			float startX = cx - totalW / 2;
			float topY = pill.bottom + sc(32);
			for (int32 i = 0; i < count; i++) {
				const ShortcutTile& shortcut = fShortcuts[i];
				BRect tile(startX + i * (cardW + gap), topY,
					startX + i * (cardW + gap) + cardW - 1, topY + cardH - 1);
				fTiles[i] = tile;
				SetHighColor(p.dark ? rgb(26, 36, 54) : rgb(255, 255, 255));
				FillRoundRect(tile, sc(12), sc(12));
				SetHighColor(p.dark ? rgb(53, 66, 90) : rgb(216, 224, 236));
				StrokeRoundRect(tile, sc(12), sc(12));

				float bSize = sc(34);
				BRect badge(tile.left + (cardW - bSize) / 2, tile.top + sc(16),
					tile.left + (cardW + bSize) / 2 - 1, tile.top + sc(16) + bSize - 1);
				SetHighColor(shortcut.color);
				FillRoundRect(badge, sc(8), sc(8));

				BFont bf(be_bold_font);
				bf.SetSize(13.5f * s);
				SetFont(&bf);
				font_height bfh;
				bf.GetHeight(&bfh);
				SetHighColor(255, 255, 255);
				float bw = bf.StringWidth(shortcut.badge.String());
				DrawString(shortcut.badge.String(),
					BPoint(badge.left + (bSize - bw) / 2,
						badge.top + (bSize + bfh.ascent - bfh.descent) / 2.0f));

				BFont lf(be_plain_font);
				lf.SetSize(12.5f * s);
				SetFont(&lf);
				SetHighColor(p.dark ? rgb(230, 233, 239) : rgb(20, 25, 35));
				BString name(shortcut.label);
				lf.TruncateString(&name, B_TRUNCATE_END, cardW - sc(12));
				float lw = lf.StringWidth(name.String());
				DrawString(name.String(),
					BPoint(tile.left + (cardW - lw) / 2, tile.bottom - sc(14)));
			}
		}
	}

	virtual void MouseDown(BPoint where)
	{
		if (!fPrivate) {
			for (int32 i = 0; i < (int32)fShortcuts.size() && i < 6; i++) {
				if (fTiles[i].Contains(where)) {
					BMessage msg(MSG_OPEN_BOOKMARK);
					msg.AddString("url", fShortcuts[i].url);
					Window()->PostMessage(&msg);
					return;
				}
			}
		}
		if (_SearchRect().Contains(where)) {
			fInput->MakeFocus(true);
			Invalidate();
		}
	}

	const char* Text() const { return fInput->Text(); }

private:
	void _EnsurePebble()
	{
		int32 want = (int32)sc(72);
		if (want < 32)
			want = 32;
		if (fPebble != NULL && fPebbleSize == want)
			return;
		delete fPebble;
		fPebble = r5_vector_icon_bitmap(kR5Pebble, sizeof(kR5Pebble), want);
		fPebbleSize = want;
	}

	BRect _SearchRect() const
	{
		float w = sc(560);
		if (w > Bounds().Width() - sc(48))
			w = Bounds().Width() - sc(48);
		float logoTop = fPrivate ? sc(120) : sc(90);
		if (Bounds().Height() > sc(620))
			logoTop = fPrivate ? sc(156) : sc(126);
		float top = logoTop + sc(72) + sc(58);
		float left = (Bounds().Width() - w) / 2.0f;
		return BRect(left, top, left + w, top + sc(43));
	}

	void _Place()
	{
		BRect pill = _SearchRect();
		fInput->MoveTo(pill.left + sc(40), pill.top + sc(11));
		fInput->ResizeTo(pill.Width() - sc(56), sc(22));
		fInput->SetTextRect(fInput->Bounds());
	}

	bool				fPrivate;
	BBitmap*			fPebble;
	int32				fPebbleSize;
	NewTabSearchInput*	fInput;
	BRect				fTiles[6];
	std::vector<ShortcutTile> fShortcuts;
};


// A small panel under a toolbar button (the site information behind the
// lock): a bordered window with round corners and the panel colour, sized
// to what it holds, closed by a click anywhere else. Opaque: its labels
// paint the panel colour.
class FloatPanel : public BWindow {
public:
	FloatPanel(BPoint where)
		:
		BWindow(BRect(where, where + BPoint(40, 20)), "",
			B_BORDERED_WINDOW_LOOK, B_FLOATING_APP_WINDOW_FEEL,
			B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE
				| B_AUTO_UPDATE_SIZE_LIMITS | B_WILL_ACCEPT_FIRST_CLICK
				| B_CLOSE_ON_ESCAPE)
	{
		fView = new BGroupView(B_VERTICAL, sc(4));
		fView->GroupLayout()->SetInsets(sc(14), sc(12), sc(14), sc(12));
		SetLayout(new BGroupLayout(B_VERTICAL, 0));
		GetLayout()->AddView(fView);
		BAppearanceKit::set_window_corner_radius(this, sc(10));
	}

	virtual void WindowActivated(bool active)
	{
		if (!active && !IsHidden())
			PostMessage(B_QUIT_REQUESTED);
	}

	BGroupView* Panel() const { return fView; }

	// After the content is in: as large as it needs, the top left at the
	// point it was opened for, inside the screen.
	void Open()
	{
		ResizeToPreferred();
		BRect screen = BScreen(this).Frame();
		BRect frame = Frame();
		if (frame.right > screen.right - 4)
			MoveBy(screen.right - 4 - frame.right, 0);
		Show();
	}

private:
	BGroupView*	fView;
};


class PageHost : public BView {
public:
	PageHost()
		:
		BView("pages", B_WILL_DRAW | B_FRAME_EVENTS)
	{
		SetViewColor(255, 255, 255);
	}

	void Fit()
	{
		FrameResized(Bounds().Width(), Bounds().Height());
	}

	virtual void FrameResized(float width, float height)
	{
		BView::FrameResized(width, height);
		for (int32 i = 0; i < CountChildren(); i++) {
			BView* child = ChildAt(i);
			if (child->Name() != NULL && strcmp(child->Name(), "find bar") == 0) {
				float fh = sc(32);
				child->MoveTo(0, height - fh);
				child->ResizeTo(width, fh);
			} else {
				child->MoveTo(0, 0);
				child->ResizeTo(width, height);
			}
		}
	}
};


AomiWindow::AomiWindow(BWebSession* session, bool privat)
	:
	BWindow(_Frame(), privat ? B_TRANSLATE("Private – Aomi") : "Aomi",
		B_TITLED_WINDOW, 0),
	fSession(session),
	fPrivate(privat),
	fPrefs(aomi_prefs()),
	fMenu(NULL),
	fChrome(NULL),
	fPages(NULL),
	fNewTab(NULL),
	fFindBar(NULL),
	fFind(NULL),
	fActive(-1),
	fZoom(1.0f),
	fSave(NULL),
	fRetry(NULL),
	fRetryPoll(NULL),
	fDownloadPanel(NULL)
{
	fZoom = fPrefs.zoom / 100.0f;
	BAppearanceKit::set_window_glass(this, true);
	SetLayout(new BGroupLayout(B_VERTICAL, 0));
	_BuildMenu();
	fChrome = new ChromeView(this);
	fPages = new PageHost();
	fPages->SetViewColor(255, 255, 255);
	fNewTab = new NewTabView(privat);
	GetLayout()->AddView(fMenu);
	GetLayout()->AddView(fChrome);
	GetLayout()->AddView(fPages);
	fPages->AddChild(fNewTab);
	if (!fPrefs.showMenuBar && GetLayout()->ItemAt(0) != NULL)
		GetLayout()->ItemAt(0)->SetVisible(false);
	fFind = new BTextControl("find", B_TRANSLATE("Find:"), "",
		new BMessage(MSG_FIND_NEXT));
	// A bar with a layout of its own: PageHost gives it the width and the
	// height, the layout places the field and the buttons.
	BGroupView* findBar = new BGroupView("find bar", B_HORIZONTAL,
		B_USE_SMALL_SPACING);
	findBar->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	BButton* previous = new BButton("previous", B_TRANSLATE("Previous"),
		new BMessage(MSG_FIND_PREVIOUS));
	BButton* next = new BButton("next", B_TRANSLATE("Next"),
		new BMessage(MSG_FIND_NEXT));
	BButton* close = new BButton("close", B_TRANSLATE("Close"),
		new BMessage(MSG_FIND_CLOSE));
	BLayoutBuilder::Group<>(findBar)
		.SetInsets(B_USE_SMALL_SPACING, 2, B_USE_SMALL_SPACING, 2)
		.Add(fFind, 1)
		.Add(previous)
		.Add(next)
		.Add(close);
	fFindBar = findBar;
	fPages->AddChild(fFindBar);
	fFindBar->Hide();
	static_cast<ChromeView*>(fChrome)->ReloadBar();
	if (privat || fPrefs.start != 2) {
		const char* url = "aomi:newtab";
		if (!privat && fPrefs.start == 1 && fPrefs.home.Length() > 0)
			url = fPrefs.home.String();
		_AddTab(url, true, NULL);
	} else {
		// Last session: the pages that were open when Aomi last closed.
		std::vector<BString> urls;
		aomi_session_load(&urls);
		for (size_t i = 0; i < urls.size() && i < 32; i++)
			_AddTab(urls[i].String(), i == 0, NULL);
		if (urls.empty())
			_AddTab("aomi:newtab", true, NULL);
	}
}


AomiWindow::~AomiWindow()
{
	stop_watching_network(BMessenger(this));
	stop_watching(BMessenger(this));
	delete fRetry;
	delete fRetryPoll;
	delete fDownloadPanel;
	delete fSave;
	delete fSession;
	for (uint32 i = 0; i < fTabs.size(); i++)
		delete fTabs[i].icon;
}


BRect
AomiWindow::_Frame()
{
	BRect screen = BScreen().Frame();
	float width = sc(1252);
	float height = sc(760);
	float marginX = sc(14);
	float topY = sc(32);
	if (screen.Width() < width + marginX * 2)
		width = screen.Width() - marginX * 2;
	if (screen.Height() < height + topY + 12)
		height = screen.Height() - topY - 12;
	return BRect(marginX, topY, marginX + width - 1, topY + height - 1);
}


void
AomiWindow::Show()
{
	BWindow::Show();
	PostMessage('shwn');
}


bool
AomiWindow::_IsLastBrowserWindow() const
{
	for (int32 i = 0; i < be_app->CountWindows(); i++) {
		AomiWindow* other = dynamic_cast<AomiWindow*>(be_app->WindowAt(i));
		if (other != NULL && other != this)
			return false;
	}
	return true;
}


void
AomiWindow::GetSession(std::vector<BString>* urls) const
{
	for (size_t i = 0; i < fTabs.size(); i++) {
		const BString& url = fTabs[i].url;
		if (url.StartsWith("http://") || url.StartsWith("https://"))
			urls->push_back(url);
	}
}


static bool sQuitting = false;


/*static*/ void
AomiWindow::SetQuitting()
{
	sQuitting = true;
}


/*static*/ bool
AomiWindow::Quitting()
{
	return sQuitting;
}


/*static*/ bool
AomiWindow::AskStopDownloads(int32 running)
{
	BString text;
	text.SetToFormat(B_TRANSLATE("%d downloads are still running. "
		"Closing Aomi stops them."), (int)running);
	if (running == 1) {
		text = B_TRANSLATE("A download is still running. Closing Aomi "
			"stops it.");
	}
	BAlert* alert = new BAlert(B_TRANSLATE("Close Aomi"), text.String(),
		B_TRANSLATE("Keep Aomi open"), B_TRANSLATE("Close"), NULL,
		B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	return alert->Go() == 1;
}


bool
AomiWindow::QuitRequested()
{
	// The last browser window closes the program, and with it the Settings
	// and Downloads windows; they alone do not keep Aomi running.
	if (sQuitting || !_IsLastBrowserWindow())
		return true;
	int32 running = aomi_download_count_running();
	if (running > 0 && !AskStopDownloads(running))
		return false;
	if (!fPrivate) {
		std::vector<BString> urls;
		GetSession(&urls);
		aomi_session_save(urls);
	}
	sQuitting = true;
	aomi_download_stop_all();
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
AomiWindow::_BuildMenu()
{
	fMenu = new BMenuBar("menu");
	BMenu* window = new BMenu(B_TRANSLATE("Window"));
	window->AddItem(new BMenuItem(B_TRANSLATE("New tab"),
		new BMessage(MSG_NEW_TAB), 'T'));
	window->AddItem(new BMenuItem(B_TRANSLATE("New window"),
		new BMessage(MSG_NEW_WINDOW), 'N'));
	window->AddItem(new BMenuItem(B_TRANSLATE("New private window"),
		new BMessage(MSG_NEW_PRIVATE), 'N', B_COMMAND_KEY | B_SHIFT_KEY));
	window->AddItem(new BMenuItem(B_TRANSLATE("Open location"),
		new BMessage(MSG_FOCUS_ADDRESS), 'L'));
	window->AddSeparatorItem();
	window->AddItem(new BMenuItem(B_TRANSLATE("Settings…"),
		new BMessage(MSG_SETTINGS), ','));
	window->AddItem(new BMenuItem(B_TRANSLATE("Downloads"),
		new BMessage(MSG_DOWNLOADS), 'D'));
	window->AddSeparatorItem();
	window->AddItem(new BMenuItem(B_TRANSLATE("Close tab"),
		new BMessage(MSG_CLOSE_TAB), 'W'));
	window->AddSeparatorItem();
	BMenuItem* quit = new BMenuItem(B_TRANSLATE("Quit"),
		new BMessage(B_QUIT_REQUESTED), 'Q');
	quit->SetTarget(be_app);
	window->AddItem(quit);
	fMenu->AddItem(window);
	BMenu* edit = new BMenu(B_TRANSLATE("Edit"));
	edit->AddItem(new BMenuItem(B_TRANSLATE("Find in page…"),
		new BMessage(MSG_FIND), 'F'));
	fMenu->AddItem(edit);
	BMenu* view = new BMenu(B_TRANSLATE("View"));
	view->AddItem(new BMenuItem(B_TRANSLATE("Reload"),
		new BMessage(MSG_RELOAD), 'R'));
	view->AddItem(new BMenuItem(B_TRANSLATE("Zoom in"),
		new BMessage(MSG_ZOOM_IN), '+'));
	view->AddItem(new BMenuItem(B_TRANSLATE("Zoom out"),
		new BMessage(MSG_ZOOM_OUT), '-'));
	view->AddItem(new BMenuItem(B_TRANSLATE("Actual size"),
		new BMessage(MSG_ZOOM_RESET), '0'));
	view->AddSeparatorItem();
	view->AddItem(new BMenuItem(B_TRANSLATE("Show bookmarks bar"),
		new BMessage(MSG_BOOKMARKS_BAR)));
	view->AddItem(new BMenuItem(B_TRANSLATE("View source"),
		new BMessage(MSG_SOURCE), 'U'));
	fMenu->AddItem(view);
	BMenu* history = new BMenu(B_TRANSLATE("History"));
	fMenu->AddItem(history);
	BMenu* bookmarks = new BMenu(B_TRANSLATE("Bookmarks"));
	bookmarks->AddItem(new BMenuItem(B_TRANSLATE("Bookmark this page"),
		new BMessage(MSG_BOOKMARK_PAGE), 'B'));
	bookmarks->AddItem(new BMenuItem(B_TRANSLATE("Manage bookmarks"),
		new BMessage(MSG_MANAGE_BOOKMARKS)));
	bookmarks->AddSeparatorItem();
	fMenu->AddItem(bookmarks);
	BMenu* help = new BMenu(B_TRANSLATE("Help"));
	help->AddItem(new BMenuItem(B_TRANSLATE("About Aomi"),
		new BMessage(MSG_ABOUT)));
	fMenu->AddItem(help);
}


void
AomiWindow::MenusBeginning()
{
	BMenu* history = fMenu->FindItem(B_TRANSLATE("History")) != NULL
		? fMenu->FindItem(B_TRANSLATE("History"))->Submenu() : NULL;
	if (history != NULL) {
		history->RemoveItems(0, history->CountItems(), true);
		aomi_fill_history(history, MSG_OPEN_BOOKMARK);
	}
	BMenuItem* mark = fMenu->FindItem(B_TRANSLATE("Bookmarks"));
	BMenu* bookmarks = mark != NULL ? mark->Submenu() : NULL;
	if (bookmarks != NULL) {
		while (bookmarks->CountItems() > 3)
			delete bookmarks->RemoveItem((int32)3);
		aomi_fill_bookmarks(bookmarks, MSG_OPEN_BOOKMARK);
	}
}


void
AomiWindow::WindowActivated(bool active)
{
	BWindow::WindowActivated(active);
	// Back from Tracker: the "Bookmark bar" folder may have changed.
	if (active)
		PostMessage(MSG_BOOKMARKS_CHANGED);
}


const char*
AomiWindow::TabTitle(int32 index) const
{
	if (index < 0 || index >= (int32)fTabs.size())
		return "";
	return fTabs[index].title.String();
}


bool
AomiWindow::TabLoading(int32 index) const
{
	if (index < 0 || index >= (int32)fTabs.size())
		return false;
	return fTabs[index].loading;
}


float
AomiWindow::TabProgress(int32 index) const
{
	if (index < 0 || index >= (int32)fTabs.size())
		return 0;
	return fTabs[index].progress;
}


bool
AomiWindow::CanBack() const
{
	return fActive >= 0 && fTabs[fActive].canBack;
}


bool
AomiWindow::CanForward() const
{
	return fActive >= 0 && fTabs[fActive].canForward;
}


bool
AomiWindow::Loading() const
{
	return fActive >= 0 && fTabs[fActive].loading;
}


const char*
AomiWindow::Address() const
{
	if (fActive < 0)
		return "";
	return fTabs[fActive].url.String();
}


int32
AomiWindow::Security() const
{
	if (fActive < 0)
		return AOMI_SECURITY_UNKNOWN;
	return fTabs[fActive].security;
}


bool
AomiWindow::Bookmarked() const
{
	if (fActive < 0)
		return false;
	return fTabs[fActive].bookmarked;
}


void
AomiWindow::ReloadPrefs()
{
	fPrefs = aomi_prefs();
	if (GetLayout()->ItemAt(0) != NULL)
		GetLayout()->ItemAt(0)->SetVisible(fPrefs.showMenuBar);
	fChrome->Invalidate();
	fChrome->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED,
		static_cast<ChromeView*>(fChrome)->Height()));
	fChrome->SetExplicitMinSize(BSize(400,
		static_cast<ChromeView*>(fChrome)->Height()));
}


void
AomiWindow::_AddTab(const char* url, bool select, BWebContentView* adopt)
{
	Tab tab;
	tab.icon = NULL;
	tab.canBack = false;
	tab.canForward = false;
	tab.loading = false;
	tab.progress = 0;
	tab.security = AOMI_SECURITY_UNKNOWN;
	tab.bookmarked = false;
	tab.failed = false;
	tab.zoomApplied = false;
	tab.title = fPrivate
		? B_TRANSLATE("Private – New tab") : B_TRANSLATE("New tab");
	if (adopt != NULL) {
		// A page opened this tab; Servo reports its address soon. Until then
		// it is not the new tab page.
		tab.view = adopt;
		tab.url = "about:blank";
	} else {
		tab.view = create_aomi_content_view("Servo", "page", fSession);
	}
	int32 index = (int32)fTabs.size();
	TabProxy* proxy = new TabProxy(this, index);
	tab.proxy = proxy;
	AddHandler(proxy);
	if (tab.view != NULL) {
		tab.view->SetListener(BMessenger(proxy));
		// Hidden before it is attached: the tabs of a window share one Servo
		// painter, and a view attached while shown starts its engine visible
		// and would draw into the current tab's frame.
		tab.view->Hide();
		fPages->AddChild(tab.view);
	}
	fTabs.push_back(tab);
	static_cast<PageHost*>(fPages)->Fit();
	if (url != NULL && url[0] != '\0' && adopt == NULL)
		_LoadOn(index, url);
	_ApplyLook(index);
	{
		BString line;
		line.SetToFormat("tabs add %d%s", (int)index,
			adopt != NULL ? " (opened by a page)" : "");
		_Log(line.String());
	}
	if (select)
		_Select(index);
	fChrome->Invalidate();
}


void
AomiWindow::_LoadOn(int32 index, const char* url)
{
	if (index < 0 || index >= (int32)fTabs.size() || fTabs[index].view == NULL)
		return;
	if (url == NULL || url[0] == '\0')
		return;
	if (strcmp(url, "aomi:copy-technical") == 0) {
		PostMessage(MSG_COPY_TECHNICAL);
		return;
	}
	fTabs[index].url = url;
	fTabs[index].protocol = "";
	if (strcmp(url, "aomi:newtab") == 0) {
		fTabs[index].loading = false;
		fTabs[index].title = fPrivate
			? B_TRANSLATE("Private – New tab") : B_TRANSLATE("New tab");
		if (index == fActive)
			_Select(fActive);
		fChrome->Invalidate();
		return;
	}
	if (strcmp(url, "aomi:about") == 0) {
		fTabs[index].loading = false;
		fTabs[index].title = B_TRANSLATE("About Aomi");
		BString html = aomi_about_html(palette().dark);
		fTabs[index].view->LoadHTML(html.String(), "aomi:about");
		if (index == fActive)
			_Select(fActive);
		fChrome->Invalidate();
		return;
	}
	if (strcmp(url, "aomi:technical") == 0 || strcmp(url, "aomi:version") == 0
		|| strcmp(url, "aomi:gpu") == 0) {
		fTabs[index].url = "aomi:technical";
		fTabs[index].loading = false;
		fTabs[index].title = B_TRANSLATE("Technical info");
		BFile jf("/tmp/aomi-technical.json",
			B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);
		if (jf.InitCheck() == B_OK) {
			BString j = aomi_technical_json();
			jf.Write(j.String(), j.Length());
		}
		BString html = aomi_technical_html(palette().dark);
		fTabs[index].view->LoadHTML(html.String(), "aomi:technical");
		if (index == fActive)
			_Select(fActive);
		fChrome->Invalidate();
		return;
	}
	if (strcmp(url, "aomi:technical.json") == 0) {
		fTabs[index].loading = false;
		fTabs[index].title = "technical.json";
		BString j = aomi_technical_json();
		BFile jf("/tmp/aomi-technical.json",
			B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);
		if (jf.InitCheck() == B_OK)
			jf.Write(j.String(), j.Length());
		BString html("<!DOCTYPE html><html><head><meta charset='utf-8'>"
			"<title>technical.json</title><style>body{margin:16px;font:13px/1.5 monospace;background:");
		html << (palette().dark ? "#15181e;color:#e6e9ef" : "#f6f8fb;color:#141923")
			<< ";}pre{white-space:pre-wrap;}</style></head><body><pre>";
		BString esc(j);
		esc.ReplaceAll("&", "&amp;");
		esc.ReplaceAll("<", "&lt;");
		esc.ReplaceAll(">", "&gt;");
		html << esc << "</pre></body></html>";
		fTabs[index].view->LoadHTML(html.String(), "aomi:technical.json");
		if (index == fActive)
			_Select(fActive);
		fChrome->Invalidate();
		return;
	}
	fTabs[index].loading = true;
	aomi_log_add(BString("load ").Append(url).String());
	fTabs[index].view->LoadURL(url);
	if (index == fActive)
		_Select(fActive);
}


int32
AomiWindow::TabIndexForProxy(const BHandler* proxy) const
{
	if (proxy == NULL)
		return -1;
	for (int32 i = 0; i < (int32)fTabs.size(); i++) {
		if (fTabs[i].proxy == proxy)
			return i;
	}
	return -1;
}


void
AomiWindow::_Select(int32 index)
{
	if (index < 0 || index >= (int32)fTabs.size())
		return;
	fActive = index;
	{
		BString line;
		line.SetToFormat("tabs select %d of %d", (int)index, (int)fTabs.size());
		_Log(line.String());
	}
	bool ntp = fTabs[index].url.Length() == 0
		|| fTabs[index].url == "aomi:newtab";
	// Hide the other tabs first, then show this one: never two visible tabs
	// in the window's shared Servo painter.
	for (int32 i = 0; i < (int32)fTabs.size(); i++) {
		if (fTabs[i].view == NULL || (i == index && !ntp))
			continue;
		// IsHidden(view): the view's own state; IsHidden() also counts a
		// window that is not shown yet.
		if (!fTabs[i].view->IsHidden(fTabs[i].view))
			fTabs[i].view->Hide();
	}
	if (!ntp && fTabs[index].view != NULL
		&& fTabs[index].view->IsHidden(fTabs[index].view))
		fTabs[index].view->Show();
	if (ntp) {
		if (fNewTab->IsHidden())
			fNewTab->Show();
	} else {
		if (!fNewTab->IsHidden())
			fNewTab->Hide();
	}
	fHover = fTabs[index].hover;
	_Title();
	static_cast<ChromeView*>(fChrome)->ShowAddress(NULL, false);
	fPages->Invalidate();
}


void
AomiWindow::_Close(int32 index)
{
	if (index < 0 || index >= (int32)fTabs.size())
		return;
	{
		BString line;
		line.SetToFormat("tabs close %d of %d (active %d)", (int)index,
			(int)fTabs.size(), (int)fActive);
		_Log(line.String());
	}
	if (fTabs.size() == 1) {
		PostMessage(B_QUIT_REQUESTED);
		return;
	}
	Tab tab = fTabs[index];
	if (tab.view != NULL) {
		fPages->RemoveChild(tab.view);
		delete tab.view;
	}
	if (tab.proxy != NULL) {
		RemoveHandler(tab.proxy);
		delete tab.proxy;
	}
	delete tab.icon;
	fTabs.erase(fTabs.begin() + index);
	for (int32 i = 0; i < (int32)fTabs.size(); i++)
		static_cast<TabProxy*>(fTabs[i].proxy)->SetIndex(i);
	if (fActive >= (int32)fTabs.size())
		fActive = (int32)fTabs.size() - 1;
	_Select(fActive);
}


void
AomiWindow::_Title()
{
	if (fActive < 0 || fActive >= (int32)fTabs.size())
		return;
	BString title = fTabs[fActive].title;
	if (fPrivate) {
		if (title.Length() == 0 || title == B_TRANSLATE("Private – New tab"))
			title = B_TRANSLATE("Private – Aomi");
		else
			title << " – " << B_TRANSLATE("Private – Aomi");
	} else if (title.Length() == 0) {
		title = "Aomi";
	} else {
		title << " – Aomi";
	}
	SetTitle(title.String());
}


void
AomiWindow::OpenURL(const char* url, bool newTab)
{
	if (newTab)
		_AddTab(url, true, NULL);
	else if (fActive >= 0)
		_LoadOn(fActive, url);
}


void
AomiWindow::_Log(const char* text)
{
	const char* path = getenv("AOMI_LOG");
	if (path == NULL || text == NULL)
		return;
	FILE* file = fopen(path, "a");
	if (file == NULL)
		return;
	fprintf(file, "%s\n", text);
	fclose(file);
}


void
AomiWindow::HandleEngine(BMessage* message, int32 tab)
{
	if (message->what == AOMI_NEW_WINDOW) {
		BWebContentView* view = NULL;
		message->FindPointer("view", reinterpret_cast<void**>(&view));
		// Get*(): Haiku's Find*() sets the value to 0 when the field is
		// missing, so a default before it does not survive.
		bool primary = message->GetBool("primary", true);
		BString url;
		message->FindString("url", &url);
		if (view != NULL)
			_AddTab(NULL, primary, view);
		else if (url.Length() > 0)
			_AddTab(url.String(), primary, NULL);
		return;
	}
	if (message->what != AOMI_DIALOG)
		return;
	// The view asked the user and answered the page itself; the window only
	// notes what the page asked for.
	int32 kind = 0;
	message->FindInt32("kind", &kind);
	BString line;
	line.SetToFormat("tab %d page request %d", (int)tab, (int)kind);
	aomi_log_add(line.String());
}


void
AomiWindow::MessageReceived(BMessage* message)
{
	int32 tab = fActive;
	void* proxy = NULL;
	if (message->FindPointer("proxy", &proxy) == B_OK) {
		tab = TabIndexForProxy(static_cast<const BHandler*>(proxy));
		if (tab < 0)
			return;
	} else {
		tab = message->GetInt32("tab", fActive);
	}
	bool validTab = tab >= 0 && tab < (int32)fTabs.size();
	switch (message->what) {
		case AOMI_LOAD_STARTED:
			if (validTab) {
				fTabs[tab].failed = false;
				fTabs[tab].loading = true;
				fTabs[tab].progress = 0.1f;
				BString url;
				if (message->FindString("url", &url) == B_OK && url.Length() > 0
					&& !url.StartsWith("data:")) {
					fTabs[tab].url = url;
				}
			}
			fChrome->Invalidate();
			break;
		case AOMI_LOAD_COMMITTED:
		case AOMI_URL_CHANGED:
		{
			BString url;
			if (validTab && message->FindString("url", &url) == B_OK) {
				// Another page: until it names itself, the tab shows its
				// address, not the title of the page before.
				if (url != fTabs[tab].url && !url.StartsWith("data:")
					&& !fTabs[tab].url.StartsWith("aomi:")) {
					fTabs[tab].title = url;
					if (tab == fActive)
						_Title();
				}
				if (!url.StartsWith("data:"))
					fTabs[tab].url = url;
				fTabs[tab].bookmarked = aomi_bookmark_has(url.String());
				if (!fPrivate)
					aomi_history_add(url.String());
				if (!fTabs[tab].zoomApplied && fTabs[tab].view != NULL
					&& fZoom != 1.0f
					&& fTabs[tab].view->SetZoom(fZoom, false) == B_OK) {
					fTabs[tab].zoomApplied = true;
				}
				BString protocol;
				status_t proto = fTabs[tab].view != NULL
					? fTabs[tab].view->Protocol(&protocol) : B_NOT_SUPPORTED;
				BString line("committed ");
				line << url << " protocol ";
				if (proto == B_OK)
					line << protocol;
				else
					line << "unsupported";
				_Log(line.String());
			}
			if (tab == fActive)
				_Select(fActive);
			fChrome->Invalidate();
			break;
		}
		case AOMI_LOAD_FINISHED:
			if (validTab) {
				fTabs[tab].loading = false;
				fTabs[tab].progress = 1;
			}
			fChrome->Invalidate();
			break;
		case AOMI_LOAD_FAILED:
			if (validTab) {
				fTabs[tab].loading = false;
				fTabs[tab].failed = true;
			}
			fChrome->Invalidate();
			break;
		case AOMI_PROGRESS:
		{
			float progress = 0;
			if (validTab && message->FindFloat("progress", &progress) == B_OK)
				fTabs[tab].progress = progress;
			fChrome->Invalidate();
			break;
		}
		case AOMI_TITLE_CHANGED:
		{
			BString title;
			if (validTab && !fTabs[tab].url.StartsWith("aomi:")
				&& message->FindString("title", &title) == B_OK
				&& title.Length() > 0) {
				// Servo's own page for a load that failed (its resource
				// neterror.html): the tab is loaded again once the network
				// is back.
				if (title == "Error loading page") {
					fTabs[tab].failed = true;
					title = B_TRANSLATE("Page could not be loaded");
					// Not every network reports that it came back (a
					// driver without link events, an address that did not
					// change): try again every 15 s for five minutes, as
					// other browsers do on their error page.
					bigtime_t interval = 0;
					int32 left = 0;
					if (fRetryPoll != NULL
						&& (fRetryPoll->GetInfo(&interval, &left) != B_OK
							|| left <= 0)) {
						delete fRetryPoll;
						fRetryPoll = NULL;
					}
					if (fRetryPoll == NULL) {
						BMessage retry(MSG_NETWORK_RETRY);
						fRetryPoll = new BMessageRunner(BMessenger(this),
							&retry, 15000000, 20);
					}
				}
				fTabs[tab].title = title;
				BString line;
				line.SetToFormat("title tab=%d url=%s title=%s", (int)tab,
					fTabs[tab].url.String(), title.String());
				_Log(line.String());
				if (tab == fActive)
					_Title();
			}
			fChrome->Invalidate();
			break;
		}
		case AOMI_NAVIGATION:
			if (validTab) {
				message->FindBool("canBack", &fTabs[tab].canBack);
				message->FindBool("canForward", &fTabs[tab].canForward);
				message->FindBool("loading", &fTabs[tab].loading);
			}
			fChrome->Invalidate();
			break;
		case AOMI_HOVER_LINK:
		{
			BString url;
			message->FindString("url", &url);
			if (validTab)
				fTabs[tab].hover = url;
			if (tab == fActive) {
				fHover = url;
				fPages->Invalidate();
			}
			break;
		}
		case AOMI_SECURITY:
			if (validTab) {
				message->FindInt32("state", &fTabs[tab].security);
				message->FindString("text", &fTabs[tab].securityText);
			}
			fChrome->Invalidate();
			break;
		case AOMI_FAVICON:
		{
			BMessage icon;
			if (validTab && message->FindMessage("icon", &icon) == B_OK) {
				delete fTabs[tab].icon;
				fTabs[tab].icon = new BBitmap(&icon);
			}
			fChrome->Invalidate();
			break;
		}
		case AOMI_PROTOCOL:
		{
			BString proto;
			if (validTab && message->FindString("protocol", &proto) == B_OK
				&& proto.Length() > 0) {
				fTabs[tab].protocol = proto;
				BString line("protocol tab=");
				line << tab << " " << proto;
				_Log(line.String());
			}
			fChrome->Invalidate();
			break;
		}
		case AOMI_DOWNLOAD_ADDED:
		case AOMI_DOWNLOAD_PROGRESS:
		case AOMI_DOWNLOAD_FINISHED:
		case AOMI_DOWNLOAD_FAILED:
			// The download button shows a dot while a file comes in.
			fChrome->Invalidate();
			break;
		case AOMI_EXTERNAL_URL:
		{
			BString url;
			if (message->FindString("url", &url) == B_OK) {
				aomi_log_add(BString("external ").Append(url).String());
				_OpenExternal(url.String());
			}
			break;
		}
		case AOMI_DOWNLOAD_WANTED:
		{
			BString url;
			BString shown;
			message->FindString("text", &shown);
			if (message->FindString("url", &url) == B_OK)
				_Download(url.String(), shown.String());
			break;
		}
		case B_NODE_MONITOR:
		case B_NETWORK_MONITOR:
			aomi_log_add(message->what == B_NODE_MONITOR
				? "network settings changed" : "network changed");
			// An interface or a link changed (Wi-Fi joined, cable in). The
			// DHCP client writes the name servers a moment later, so the
			// pages that failed load again three times, 3 s apart.
			delete fRetry;
			{
				BMessage retry(MSG_NETWORK_RETRY);
				fRetry = new BMessageRunner(BMessenger(this), &retry, 3000000,
					3);
			}
			break;
		case MSG_NETWORK_RETRY:
			_RetryFailed();
			break;
		case MSG_FOCUS_ADDRESS:
			static_cast<ChromeView*>(fChrome)->FocusAddress();
			break;
		case MSG_DOWNLOAD_SAVE:
		{
			entry_ref dir;
			BString name;
			BString url;
			if (message->FindRef("directory", &dir) == B_OK
				&& message->FindString("name", &name) == B_OK
				&& message->FindString("url", &url) == B_OK) {
				BPath path(&dir);
				path.Append(name.String());
				if (aomi_start_download_to(url.String(), path.Path()) == B_OK)
					OpenDownloads();
			}
			break;
		}
		case MSG_FIND_PREVIOUS:
			if (fActive >= 0 && fTabs[fActive].view != NULL)
				fTabs[fActive].view->Find(fFind->Text(), false, false);
			break;
		case MSG_FIND_CLOSE:
			if (!fFindBar->IsHidden(fFindBar))
				fFindBar->Hide();
			if (fActive >= 0 && fTabs[fActive].view != NULL)
				fTabs[fActive].view->MakeFocus(true);
			break;
		case BAppearanceKit::B_WINDOW_EFFECTS_CHANGED:
			update_glass();
			fChrome->Invalidate();
			break;
		case AOMI_CLOSE_WANTED:
			// A page called window.close(): that tab closes, not the window
			// (the window closes with its last tab).
			if (validTab)
				_Close(tab);
			break;

		case MSG_GO:
		{
			BString url = aomi_smart_url(
				static_cast<ChromeView*>(fChrome)->FieldText(), fPrefs.search);
			if (url.Length() == 0) {
				static_cast<ChromeView*>(fChrome)->ShowAddress(
					fTabs[fActive].url.String(), true);
				break;
			}
			_LoadOn(fActive, url.String());
			static_cast<ChromeView*>(fChrome)->ShowAddress(NULL, false);
			break;
		}
		case 'esc!':
			static_cast<ChromeView*>(fChrome)->ShowAddress(NULL, false);
			if (fActive >= 0 && fTabs[fActive].loading)
				fTabs[fActive].view->StopLoading();
			break;
		case MSG_NTP_GO:
		{
			NewTabView* page = static_cast<NewTabView*>(fNewTab);
			BString url = aomi_smart_url(page->Text(), fPrefs.search);
			if (url.Length() > 0)
				_LoadOn(fActive, url.String());
			break;
		}
		case MSG_BACK:
			if (fActive >= 0 && fTabs[fActive].view != NULL)
				fTabs[fActive].view->GoBack();
			break;
		case MSG_FORWARD:
			if (fActive >= 0 && fTabs[fActive].view != NULL)
				fTabs[fActive].view->GoForward();
			break;
		case MSG_RELOAD:
			if (fActive >= 0 && fTabs[fActive].view != NULL) {
				if (fTabs[fActive].loading)
					fTabs[fActive].view->StopLoading();
				else
					fTabs[fActive].view->Reload(false);
			}
			break;
		case MSG_HOME:
			_LoadOn(fActive, fPrefs.home.String());
			break;
		case MSG_NEW_TAB:
			_AddTab("aomi:newtab", true, NULL);
			break;
		case MSG_SELECT_TAB:
		{
			int32 index = 0;
			if (message->FindInt32("tab", &index) == B_OK)
				_Select(index);
			break;
		}
		case MSG_CLOSE_TAB:
		{
			// No "tab" (Alt+W, the menu): the active tab. FindInt32() would
			// have set 0 for a missing field and always closed the first tab.
			_Close(message->GetInt32("tab", fActive));
			break;
		}
		case MSG_NEW_WINDOW:
		case MSG_NEW_PRIVATE:
			be_app->PostMessage(message);
			break;
		case MSG_STAR:
		case MSG_BOOKMARK_PAGE:
			if (fActive >= 0 && fTabs[fActive].url.StartsWith("http")) {
				aomi_bookmark_add(fTabs[fActive].url.String(),
					fTabs[fActive].title.String());
				fTabs[fActive].bookmarked = true;
				static_cast<ChromeView*>(fChrome)->ReloadBar();
			}
			break;
		case MSG_MANAGE_BOOKMARKS:
			aomi_open_bookmarks();
			break;
		case MSG_BOOKMARKS_CHANGED:
			static_cast<ChromeView*>(fChrome)->ReloadBar();
			static_cast<NewTabView*>(fNewTab)->ReloadTiles();
			break;
		case MSG_OPEN_BOOKMARK:
		{
			BString url;
			if (message->FindString("url", &url) == B_OK)
				_LoadOn(fActive, url.String());
			break;
		}
		case MSG_LOCK:
			_OpenLock();
			break;
		case MSG_DOWNLOADS:
			OpenDownloads();
			break;
		case MSG_MENU:
			OpenMenu();
			break;
		case MSG_MENU_BAR:
		case MSG_BOOKMARKS_BAR:
		{
			// The saved settings with this one value turned over, then
			// every window is told: the other browser windows, and the
			// settings window, whose next save would write the old value
			// back otherwise.
			AomiPrefs prefs = aomi_prefs();
			if (message->what == MSG_MENU_BAR)
				prefs.showMenuBar = !fPrefs.showMenuBar;
			else
				prefs.showBookmarks = !fPrefs.showBookmarks;
			aomi_prefs_save(prefs);
			for (int32 i = 0; i < be_app->CountWindows(); i++) {
				BWindow* window = be_app->WindowAt(i);
				if (window != NULL)
					window->PostMessage(MSG_SETTINGS_CHANGED);
			}
			break;
		}
		case MSG_GET_ADDRESS:
		{
			BMessage reply(B_REPLY);
			reply.AddString("url", Address());
			message->SendReply(&reply);
			break;
		}
		case MSG_SETTINGS:
			OpenSettings(message->GetString("page", NULL));
			break;
		case MSG_SETTINGS_CHANGED:
		{
			int32 oldZoom = fPrefs.zoom;
			ReloadPrefs();
			// A new zoom in Aomi settings applies at once, to every tab.
			if (fPrefs.zoom != oldZoom) {
				fZoom = fPrefs.zoom / 100.0f;
				for (int32 i = 0; i < (int32)fTabs.size(); i++) {
					if (fTabs[i].view != NULL) {
						fTabs[i].zoomApplied
							= fTabs[i].view->SetZoom(fZoom, false) == B_OK;
					}
				}
			}
			for (int32 i = 0; i < (int32)fTabs.size(); i++)
				_ApplyLook(i);
			break;
		}
		case MSG_FIND:
			if (fFindBar->IsHidden(fFindBar))
				fFindBar->Show();
			static_cast<PageHost*>(fPages)->Fit();
			fFind->MakeFocus(true);
			if (fFind->TextView() != NULL)
				fFind->TextView()->SelectAll();
			break;
		case MSG_FIND_NEXT:
			if (fActive >= 0 && fTabs[fActive].view != NULL)
				fTabs[fActive].view->Find(fFind->Text(), true, false);
			break;
		case MSG_ZOOM_IN:
			fZoom *= 1.2f;
			if (fActive >= 0 && fTabs[fActive].view != NULL)
				fTabs[fActive].view->SetZoom(fZoom, false);
			break;
		case MSG_ZOOM_OUT:
			fZoom /= 1.2f;
			if (fActive >= 0 && fTabs[fActive].view != NULL)
				fTabs[fActive].view->SetZoom(fZoom, false);
			break;
		case MSG_ZOOM_RESET:
			fZoom = 1;
			if (fActive >= 0 && fTabs[fActive].view != NULL)
				fTabs[fActive].view->SetZoom(1, false);
			break;
		case MSG_SOURCE:
			if (fActive >= 0 && fTabs[fActive].view != NULL)
				fTabs[fActive].view->ViewSource();
			break;
		case MSG_PRINT:
			if (fActive >= 0 && fTabs[fActive].view != NULL
				&& fTabs[fActive].view->Print() == B_NOT_SUPPORTED) {
				BAlert* alert = new BAlert("Aomi",
					B_TRANSLATE("This engine cannot print."),
					B_TRANSLATE("OK"));
				alert->Go(NULL);
			}
			break;
		case MSG_SAVE:
			if (fSave == NULL) {
				fSave = new BFilePanel(B_SAVE_PANEL, new BMessenger(this),
					NULL, 0, false, new BMessage(MSG_SAVE_FILE));
			}
			fSave->Show();
			break;
		case MSG_SAVE_FILE:
		{
			entry_ref dir;
			BString name;
			if (message->FindRef("directory", &dir) == B_OK
				&& message->FindString("name", &name) == B_OK
				&& fActive >= 0 && fTabs[fActive].view != NULL) {
				BPath path(&dir);
				path.Append(name.String());
				fTabs[fActive].view->SavePage(path.Path());
			}
			break;
		}
		case MSG_OPEN_FOLDER:
		{
			entry_ref ref;
			if (get_ref_for_path(fPrefs.downloadPath.String(), &ref) == B_OK)
				be_roster->Launch(&ref);
			break;
		}
		case 'shwn':
			update_glass();
			// Wi-Fi joined, a cable plugged in: pages that failed without
			// network load again (B_NETWORK_MONITOR).
			start_watching_network(B_WATCH_NETWORK_INTERFACE_CHANGES
				| B_WATCH_NETWORK_LINK_CHANGES, BMessenger(this));
			{
				// The name servers arriving is the moment pages can load:
				// the DHCP client writes resolv.conf (a new file, renamed
				// into place). Not every driver reports its link (virtio in
				// QEMU, 27 Nov 2026), so this folder is watched as well.
				BPath settings;
				node_ref folder;
				if (find_directory(B_SYSTEM_SETTINGS_DIRECTORY, &settings)
						== B_OK
					&& settings.Append("network") == B_OK
					&& BEntry(settings.Path()).GetNodeRef(&folder) == B_OK)
					watch_node(&folder, B_WATCH_DIRECTORY, BMessenger(this));
			}
			BAppearanceKit::set_view_glass(fChrome, true);
			if (fMenu != NULL)
				BAppearanceKit::set_view_glass(fMenu, true);
			// Tabs started loading when they were added; a view starts its
			// engine as soon as it is in the window, so nothing loads again.
			if (fActive >= 0)
				_Select(fActive);

			break;
		case MSG_ABOUT:
			_AddTab("aomi:about", true, NULL);
			break;
		case MSG_TECHNICAL:
			_AddTab("aomi:technical", true, NULL);
			break;
		case MSG_COPY_TECHNICAL:
		{
			BString text = aomi_technical_text();
			if (be_clipboard->Lock()) {
				be_clipboard->Clear();
				BMessage* clip = be_clipboard->Data();
				if (clip != NULL) {
					clip->AddData("text/plain", B_MIME_TYPE, text.String(),
						text.Length());
					be_clipboard->Commit();
				}
				be_clipboard->Unlock();
			}
			aomi_log_add("Copied technical information to clipboard");
			break;
		}
		case MSG_OPEN_URL:
		{
			BString url;
			bool newTab = message->GetBool("newTab", true);

			if (message->FindString("url", &url) == B_OK && url.Length() > 0)
				OpenURL(url.String(), newTab);
			break;
		}
		case MSG_DOWNLOAD_URL:
		{
			BString url;
			if (message->FindString("url", &url) == B_OK)
				_Download(url.String(), url.String());
			break;
		}
		case MSG_SHOW_ALL_DOWNLOADS:
			OpenDownloads();
			break;
		case B_FONTS_UPDATED:
			static_cast<ChromeView*>(fChrome)->ApplyFontScale();
			static_cast<NewTabView*>(fNewTab)->ApplyFontScale();
			for (int32 i = 0; i < (int32)fTabs.size(); i++)
				_ApplyLook(i);
			if (GetLayout() != NULL)
				GetLayout()->InvalidateLayout();
			BWindow::MessageReceived(message);
			break;
		case B_COLORS_UPDATED:
			static_cast<ChromeView*>(fChrome)->ApplyColors();
			fNewTab->Invalidate();
			for (int32 i = 0; i < (int32)fTabs.size(); i++)
				_ApplyLook(i);
			if (fActive >= 0 && fActive < (int32)fTabs.size()) {
				if (fTabs[fActive].url == "aomi:about"
					|| fTabs[fActive].url == "aomi:technical"
					|| fTabs[fActive].url == "aomi:technical.json") {
					_LoadOn(fActive, fTabs[fActive].url.String());
				}
			}
			BWindow::MessageReceived(message);
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
AomiWindow::_OpenLock()
{
	if (fActive < 0)
		return;
	ChromeView* chrome = static_cast<ChromeView*>(fChrome);
	BRect rect = chrome->LockRect();
	chrome->ConvertToScreen(&rect);
	FloatPanel* panel = new FloatPanel(BPoint(rect.left, rect.bottom + 6));
	BGroupView* view = panel->Panel();
	Palette p = palette();
	// The site's name, not the whole address.
	BString host(fTabs[fActive].url);
	int32 scheme = host.FindFirst("://");
	if (scheme >= 0)
		host.Remove(0, scheme + 3);
	int32 slash = host.FindFirst('/');
	if (slash >= 0)
		host.Truncate(slash);
	BStringView* title = new BStringView("host", host.String());
	title->SetFont(be_bold_font);
	BString line(B_TRANSLATE("Connection: "));
	BString protocol(fTabs[fActive].protocol);
	status_t status = protocol.Length() > 0 ? B_OK : B_NOT_SUPPORTED;
	if (status != B_OK && fTabs[fActive].view != NULL)
		status = fTabs[fActive].view->Protocol(&protocol);
	if (status == B_OK && protocol == "h2")
		line << B_TRANSLATE("HTTP/2");
	else if (status == B_OK && protocol == "h3")
		line << B_TRANSLATE("HTTP/3");
	else if (status == B_OK && protocol == "http/1.1")
		line << B_TRANSLATE("HTTP/1.1");
	else
		line << B_TRANSLATE("protocol not reported");
	if (fTabs[fActive].security == AOMI_SECURITY_SECURE)
		line << B_TRANSLATE(", secure");
	else if (fTabs[fActive].security == AOMI_SECURITY_NOT_SECURE)
		line << B_TRANSLATE(", not secure");
	else if (fTabs[fActive].security == AOMI_SECURITY_CERTIFICATE)
		line << B_TRANSLATE(", certificate error");
	BStringView* detail = new BStringView("conn", line.String());
	view->GroupLayout()->AddView(title);
	view->GroupLayout()->AddView(detail);
	title->SetHighColor(p.text);
	detail->SetHighColor(p.sub);
	panel->Open();
}


void
AomiWindow::OpenDownloads()
{
	// A window of its own, like every other R5 window: it can be moved,
	// kept open beside the page and closed with its close box.
	AomiDownloadsWindow::ShowWindow(fPrefs.downloadPath.String());
}


void
AomiWindow::_Download(const char* url, const char* shown)
{
	if (url == NULL || url[0] == '\0')
		return;
	if (fPrefs.askDownload) {
		// "Ask where to save each file": a save panel with the name from
		// the address.
		if (fDownloadPanel == NULL) {
			fDownloadPanel = new BFilePanel(B_SAVE_PANEL, new BMessenger(this),
				NULL, 0, false, NULL);
		}
		BMessage message(MSG_DOWNLOAD_SAVE);
		message.AddString("url", url);
		fDownloadPanel->SetMessage(&message);
		// The same name a download without the panel gets: decoded ("%20"
		// is a space) and cleaned of slashes and control characters.
		fDownloadPanel->SetSaveText(aomi_download_name(url).String());
		entry_ref dir;
		if (get_ref_for_path(fPrefs.downloadPath.String(), &dir) == B_OK)
			fDownloadPanel->SetPanelDirectory(&dir);
		fDownloadPanel->Show();
		return;
	}
	if (aomi_start_download(url, fPrefs.downloadPath.String(),
			shown != NULL && shown[0] != '\0' ? shown : NULL) == B_OK) {
		OpenDownloads();
	}
}


void
AomiWindow::_OpenExternal(const char* url)
{
	// mailto: and tel: belong to another program.
	status_t status = aomi_open_external(url);
	if (status == B_OK)
		return;
	BString scheme(url);
	int32 colon = scheme.FindFirst(':');
	if (colon > 0)
		scheme.Truncate(colon);
	BString text;
	if (status == B_NOT_ALLOWED) {
		text.SetToFormat(B_TRANSLATE("Aomi does not open \"%s\" links from "
			"a web page."), scheme.String());
	} else {
		text.SetToFormat(B_TRANSLATE("No program on this computer opens "
			"\"%s\" links."), scheme.String());
	}
	BAlert* alert = new BAlert("Aomi", text.String(), B_TRANSLATE("OK"));
	alert->Go(NULL);
}


void
AomiWindow::_ApplyLook(int32 index)
{
	if (index < 0 || index >= (int32)fTabs.size()
		|| fTabs[index].view == NULL)
		return;
	// Pages follow R5's light or dark mode unless the settings say
	// otherwise, and the system font size (12 points is 1:1).
	bool dark = fPrefs.pageColors == 2
		|| (fPrefs.pageColors == 0 && palette().dark);
	fTabs[index].view->SetColorScheme(dark);
	float scale = ui_scale();
	if (scale < 0.5f)
		scale = 0.5f;
	if (scale > 4.0f)
		scale = 4.0f;
	fTabs[index].view->SetScale(scale);
}


void
AomiWindow::_RetryFailed()
{
	bool anyFailed = false;
	for (size_t i = 0; i < fTabs.size(); i++)
		anyFailed |= fTabs[i].failed;
	if (!anyFailed) {
		delete fRetryPoll;
		fRetryPoll = NULL;
		return;
	}
	for (int32 i = 0; i < (int32)fTabs.size(); i++) {
		Tab& tab = fTabs[i];
		// A tab with the engine's error page counts as failed even when the
		// engine still reports it as loading.
		if (!tab.failed || tab.view == NULL)
			continue;
		if (!tab.url.StartsWith("http://") && !tab.url.StartsWith("https://"))
			continue;
		BString line;
		line.SetToFormat("network back: tab %d loads %s again", (int)i,
			tab.url.String());
		aomi_log_add(line.String());
		tab.loading = true;
		tab.view->LoadURL(tab.url.String());
	}
	fChrome->Invalidate();
}


class ZoomMenuItem : public BMenuItem {
public:
	ZoomMenuItem(AomiWindow* window)
		:
		BMenuItem(B_TRANSLATE("Zoom"), new BMessage(MSG_ZOOM_RESET)),
		fWindow(window)
	{
	}

	virtual void GetContentSize(float* width, float* height)
	{
		BMenuItem::GetContentSize(width, height);
		if (width != NULL && *width < sc(220))
			*width = sc(220);
		if (height != NULL && *height < sc(26))
			*height = sc(26);
	}

	virtual void DrawContent()
	{
		BMenu* menu = Menu();
		if (menu == NULL)
			return;
		BRect f = Frame();
		Palette p = palette();
		BFont font;
		menu->GetFont(&font);
		font_height fh;
		font.GetHeight(&fh);
		float baseline = f.top + (f.Height() + fh.ascent - fh.descent) / 2.0f;

		menu->SetHighColor(p.text);
		menu->DrawString(B_TRANSLATE("Zoom"), BPoint(f.left + sc(12), baseline));

		float right = f.right - sc(10);
		float h = sc(22);
		float top = f.top + (f.Height() - h) / 2.0f;
		fPlusRect = BRect(right - sc(24), top, right, top + h);
		right = fPlusRect.left - sc(4);
		fResetRect = BRect(right - sc(52), top, right, top + h);
		right = fResetRect.left - sc(4);
		fMinusRect = BRect(right - sc(24), top, right, top + h);

		auto drawPill = [&](BRect r, const char* label) {
			menu->SetHighColor(p.ctl);
			menu->FillRoundRect(r, sc(6), sc(6));
			menu->SetHighColor(p.cline);
			menu->StrokeRoundRect(r, sc(6), sc(6));
			menu->SetHighColor(p.text);
			float lw = font.StringWidth(label);
			menu->DrawString(label, BPoint(r.left + (r.Width() - lw) / 2.0f,
				r.top + (r.Height() + fh.ascent - fh.descent) / 2.0f));
		};
		drawPill(fMinusRect, "−");
		BString pct;
		pct.SetToFormat("%d %%", (int)(fWindow->PageZoom() * 100.0f + 0.5f));
		drawPill(fResetRect, pct.String());
		drawPill(fPlusRect, "+");
	}

	virtual status_t Invoke(BMessage* message = NULL)
	{
		(void)message;
		BMenu* menu = Menu();
		uint32 cmd = MSG_ZOOM_RESET;
		if (menu != NULL) {
			BPoint where;
			uint32 buttons = 0;
			menu->GetMouse(&where, &buttons, false);
			if (fMinusRect.Contains(where))
				cmd = MSG_ZOOM_OUT;
			else if (fPlusRect.Contains(where))
				cmd = MSG_ZOOM_IN;
		}
		return fWindow->PostMessage(cmd);
	}

private:
	AomiWindow*	fWindow;
	BRect		fMinusRect;
	BRect		fResetRect;
	BRect		fPlusRect;
};


void
AomiWindow::OpenMenu()
{
	BPopUpMenu* menu = new BPopUpMenu("aomi");
	menu->AddItem(new BMenuItem(B_TRANSLATE("New tab"),
		new BMessage(MSG_NEW_TAB), 'T'));
	menu->AddItem(new BMenuItem(B_TRANSLATE("New window"),
		new BMessage(MSG_NEW_WINDOW), 'N'));
	menu->AddItem(new BMenuItem(B_TRANSLATE("New private window"),
		new BMessage(MSG_NEW_PRIVATE), 'N', B_COMMAND_KEY | B_SHIFT_KEY));
	menu->AddSeparatorItem();
	BMenu* history = new BMenu(B_TRANSLATE("History"));
	aomi_fill_history(history, MSG_OPEN_BOOKMARK);
	menu->AddItem(history);
	BMenu* bookmarks = new BMenu(B_TRANSLATE("Bookmarks"));
	bookmarks->AddItem(new BMenuItem(B_TRANSLATE("Bookmark this page"),
		new BMessage(MSG_BOOKMARK_PAGE), 'B'));
	bookmarks->AddItem(new BMenuItem(B_TRANSLATE("Manage bookmarks"),
		new BMessage(MSG_MANAGE_BOOKMARKS)));
	BMenuItem* bar = new BMenuItem(B_TRANSLATE("Show bookmarks bar"),
		new BMessage(MSG_BOOKMARKS_BAR));
	bar->SetMarked(fPrefs.showBookmarks);
	bookmarks->AddItem(bar);
	bookmarks->AddSeparatorItem();
	aomi_fill_bookmarks(bookmarks, MSG_OPEN_BOOKMARK);
	menu->AddItem(bookmarks);
	menu->AddItem(new BMenuItem(B_TRANSLATE("Downloads"),
		new BMessage(MSG_DOWNLOADS), 'D'));
	menu->AddSeparatorItem();
	menu->AddItem(new ZoomMenuItem(this));
	menu->AddItem(new BMenuItem(B_TRANSLATE("Find in page…"),
		new BMessage(MSG_FIND), 'F'));
	menu->AddItem(new BMenuItem(B_TRANSLATE("Save page as…"),
		new BMessage(MSG_SAVE), 'S'));
	BMenuItem* print = new BMenuItem(B_TRANSLATE("Print…"),
		new BMessage(MSG_PRINT), 'P');
	print->SetEnabled(false);
	menu->AddItem(print);
	menu->AddItem(new BMenuItem(B_TRANSLATE("View source"),
		new BMessage(MSG_SOURCE), 'U'));
	menu->AddSeparatorItem();
	BMenuItem* menuBar = new BMenuItem(B_TRANSLATE("Show menu bar"),
		new BMessage(MSG_MENU_BAR));
	menuBar->SetMarked(fPrefs.showMenuBar);
	menu->AddItem(menuBar);
	menu->AddItem(new BMenuItem(B_TRANSLATE("Settings…"),
		new BMessage(MSG_SETTINGS), ',', B_COMMAND_KEY));
	menu->AddItem(new BMenuItem(B_TRANSLATE("About Aomi"),
		new BMessage(MSG_ABOUT)));
	menu->AddItem(new BMenuItem(B_TRANSLATE("Technical info"),
		new BMessage(MSG_TECHNICAL)));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem(B_TRANSLATE("Quit"),
		new BMessage(B_QUIT_REQUESTED), 'Q'));
	menu->SetTargetForItems(this);
	if (BMenuItem* quitItem = menu->FindItem(B_QUIT_REQUESTED))
		quitItem->SetTarget(be_app);
	history->SetTargetForItems(this);
	bookmarks->SetTargetForItems(this);
	ChromeView* chrome = static_cast<ChromeView*>(fChrome);
	BRect rect = chrome->MenuRect();
	chrome->ConvertToScreen(&rect);
	menu->Go(BPoint(rect.left, rect.bottom + 4), true, true, true);
	delete menu;
}


void
AomiWindow::OpenSettings(const char* page)
{
	AomiSettingsWindow::ShowWindow(page);
}


void
AomiWindow::_Load(const char* url)
{
	if (fActive >= 0)
		_LoadOn(fActive, url);
}
