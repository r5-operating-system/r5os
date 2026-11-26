/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


#include "AomiWindow.h"

#include "AomiMessages.h"
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
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <Screen.h>
#include <StringView.h>
#include <TextControl.h>
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


static Palette
palette()
{
	rgb_color bg = ui_color(B_PANEL_BACKGROUND_COLOR);
	Palette p;
	p.dark = bg.red + bg.green + bg.blue < 380;
	if (!p.dark) {
		p.panel = rgb(238, 241, 245);
		p.text = rgb(20, 20, 20);
		p.sub = rgb(91, 98, 112);
		p.field = rgb(255, 255, 255);
		p.fline = rgb(184, 192, 204);
		p.ctl = rgb(247, 248, 250);
		p.cline = rgb(180, 188, 200);
		p.hair = rgb(211, 216, 223);
		p.accent = rgb(0, 132, 255);
	} else {
		p.panel = rgb(31, 35, 43);
		p.text = rgb(230, 233, 239);
		p.sub = rgb(163, 171, 184);
		p.field = rgb(21, 24, 30);
		p.fline = rgb(69, 76, 89);
		p.ctl = rgb(42, 47, 57);
		p.cline = rgb(69, 76, 89);
		p.hair = rgb(53, 59, 70);
		p.accent = rgb(0, 132, 255);
	}
	return p;
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
		BView("chrome", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
		fWindow(window),
		fField(new AddressField()),
		fFocus(false)
	{
		SetViewColor(palette().panel);
		AddChild(fField);
		fField->SetViewColor(palette().field);
	}

	float Height() const
	{
		return tabs_h() + nav_h() + (fWindow->Prefs().showBookmarks ? marks_h() : 0);
	}

	void ApplyFontScale()
	{
		BFont font(be_plain_font);
		font.SetSize(13.5f * ui_scale());
		fField->SetFontAndColor(&font);
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

		SetViewColor(p.panel);
		SetLowColor(p.panel);
		FillRect(Bounds(), B_SOLID_LOW);
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
		_Icon(fDownloads, R5_GLYPH_DOWNLOAD, p.text, p);
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
		if (fWindow->Prefs().showBookmarks) {
			float by2 = kTabs + kNav;
			SetHighColor(p.hair);
			StrokeLine(BPoint(0, by2 + kMarks - 1),
				BPoint(Bounds().right, by2 + kMarks - 1));
		}
	}

	virtual void MouseDown(BPoint where)
	{
		int32 buttons = 0;
		Window()->CurrentMessage()->FindInt32("buttons", &buttons);
		int32 count = fWindow->CountTabs();
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
		if (fURL.Contains(where)) {
			fFocus = true;
			fField->Show();
			BString address(fWindow->Address());
			if (address.StartsWith("aomi:"))
				address = "";
			fField->SetText(address.String());
			fField->SelectAll();
			fField->MakeFocus(true);
			Invalidate();
		}
	}

	void ShowAddress(const char* text, bool focus)
	{
		fFocus = focus;
		if (text != NULL)
			fField->SetText(text);
		if (focus) {
			fField->Show();
			fField->MakeFocus(true);
		} else {
			fField->MakeFocus(false);
			fField->Hide();
		}
		Invalidate();
	}

	const char* FieldText() const { return fField->Text(); }

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

	void _Place()
	{
		float kTabs = tabs_h();
		float bSize = sc(29);
		if (!fURL.IsValid())
			fURL = BRect(sc(120), kTabs + sc(6), Bounds().right - sc(80),
				kTabs + sc(6) + bSize);
		fField->MoveTo(fURL.left + sc(28), fURL.top + sc(4));
		fField->ResizeTo(fURL.Width() - sc(56), sc(20));
		if (!fFocus)
			fField->Hide();
		SetExplicitMinSize(BSize(sc(400), Height()));
		SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, Height()));
	}

	AomiWindow*		fWindow;
	AddressField*	fField;
	bool			fFocus;
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
};


struct ShortcutTile {
	const char* badge;
	const char* label;
	const char* url;
	uint8 r, g, b;
};

static const ShortcutTile kShortcutTiles[] = {
	{ "R5", "R5", "https://r5-operating-system.github.io/", 0, 132, 255 },
	{ "S", "Servo", "https://servo.org/", 28, 34, 44 },
	{ "N", "Northline", "https://example.com/", 176, 74, 16 },
	{ "W", "Wikipedia", "https://en.wikipedia.org/wiki/Haiku_(operating_system)", 24, 148, 66 },
	{ "H", "HaikuPorts", "https://www.haiku-os.org/", 124, 58, 237 },
	{ "G", "GitHub", "https://github.com/login", 22, 118, 86 },
};
static const int32 kShortcutCount = 6;


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
		BView("new tab", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
		fPrivate(privat),
		fPebble(NULL),
		fPebbleSize(0),
		fInput(new NewTabSearchInput())
	{
		SetViewColor(B_TRANSPARENT_COLOR);
		AddChild(fInput);
		_EnsurePebble();
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
			float totalW = kShortcutCount * cardW + (kShortcutCount - 1) * gap;
			if (totalW > b.Width() - sc(32)) {
				cardW = floorf((b.Width() - sc(32) - (kShortcutCount - 1) * gap)
					/ kShortcutCount);
				totalW = kShortcutCount * cardW + (kShortcutCount - 1) * gap;
			}
			float startX = cx - totalW / 2;
			float topY = pill.bottom + sc(32);
			for (int32 i = 0; i < kShortcutCount; i++) {
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
				SetHighColor(rgb(kShortcutTiles[i].r, kShortcutTiles[i].g,
					kShortcutTiles[i].b));
				FillRoundRect(badge, sc(8), sc(8));

				BFont bf(be_bold_font);
				bf.SetSize(13.5f * s);
				SetFont(&bf);
				font_height bfh;
				bf.GetHeight(&bfh);
				SetHighColor(255, 255, 255);
				float bw = bf.StringWidth(kShortcutTiles[i].badge);
				DrawString(kShortcutTiles[i].badge,
					BPoint(badge.left + (bSize - bw) / 2,
						badge.top + (bSize + bfh.ascent - bfh.descent) / 2.0f));

				BFont lf(be_plain_font);
				lf.SetSize(12.5f * s);
				SetFont(&lf);
				SetHighColor(p.dark ? rgb(230, 233, 239) : rgb(20, 25, 35));
				float lw = lf.StringWidth(kShortcutTiles[i].label);
				DrawString(kShortcutTiles[i].label,
					BPoint(tile.left + (cardW - lw) / 2, tile.bottom - sc(14)));
			}
		}
	}

	virtual void MouseDown(BPoint where)
	{
		if (!fPrivate) {
			for (int32 i = 0; i < kShortcutCount; i++) {
				if (fTiles[i].Contains(where)) {
					BMessage msg(MSG_OPEN_BOOKMARK);
					msg.AddString("url", kShortcutTiles[i].url);
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
	}

	bool				fPrivate;
	BBitmap*			fPebble;
	int32				fPebbleSize;
	NewTabSearchInput*	fInput;
	BRect				fTiles[kShortcutCount];
};


class FloatPanel : public BWindow {
public:
	FloatPanel(BRect frame, const char* title)
		:
		BWindow(frame, title, B_NO_BORDER_WINDOW_LOOK,
			B_FLOATING_ALL_WINDOW_FEEL,
			B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE
				| B_AUTO_UPDATE_SIZE_LIMITS | B_WILL_ACCEPT_FIRST_CLICK)
	{
		BAppearanceKit::set_window_glass(this, true);
		Palette p = palette();
		fView = new BView(Bounds(), "panel", B_FOLLOW_ALL, B_WILL_DRAW);
		fView->SetViewColor(p.panel);
		AddChild(fView);
	}

	virtual void WindowActivated(bool active)
	{
		if (!active && !IsHidden())
			PostMessage(B_QUIT_REQUESTED);
	}

	BView* Panel() const { return fView; }

private:
	BView*	fView;
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
	fSave(NULL)
{
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
	fFindBar = new BView("find bar", 0);
	fFindBar->SetViewColor(palette().panel);
	fFindBar->AddChild(fFind);
	fPages->AddChild(fFindBar);
	fFindBar->Hide();
	const char* url = "aomi:newtab";
	if (!privat && fPrefs.start == 1)
		url = fPrefs.home.String();
	_AddTab(url, true, NULL);
}


AomiWindow::~AomiWindow()
{
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
AomiWindow::QuitRequested()
{
	if (be_app->CountWindows() <= 1)
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
	window->AddSeparatorItem();
	window->AddItem(new BMenuItem(B_TRANSLATE("Downloads"),
		new BMessage(MSG_DOWNLOADS), 'D'));
	window->AddSeparatorItem();
	window->AddItem(new BMenuItem(B_TRANSLATE("Close tab"),
		new BMessage(MSG_CLOSE_TAB), 'W'));
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
		fPages->AddChild(tab.view);
		tab.view->Hide();
	}
	fTabs.push_back(tab);
	static_cast<PageHost*>(fPages)->Fit();
	if (url != NULL && url[0] != '\0' && adopt == NULL)
		_LoadOn(index, url);
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
	bool ntp = fTabs[index].url.Length() == 0
		|| fTabs[index].url == "aomi:newtab";
	for (int32 i = 0; i < (int32)fTabs.size(); i++) {
		if (fTabs[i].view == NULL)
			continue;
		if (i == index && !ntp) {
			if (fTabs[i].view->IsHidden())
				fTabs[i].view->Show();
		} else {
			if (!fTabs[i].view->IsHidden())
				fTabs[i].view->Hide();
		}
	}
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
		bool primary = true;
		message->FindBool("primary", &primary);
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
		message->FindInt32("tab", &tab);
	}
	bool validTab = tab >= 0 && tab < (int32)fTabs.size();
	switch (message->what) {
		case AOMI_LOAD_STARTED:
			if (validTab) {
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
				if (!url.StartsWith("data:"))
					fTabs[tab].url = url;
				fTabs[tab].bookmarked = aomi_bookmark_has(url.String());
				if (!fPrivate)
					aomi_history_add(url.String());
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
			if (validTab)
				fTabs[tab].loading = false;
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
		{
			DlEntry entry;
			entry.id = 0;
			entry.done = false;
			entry.failed = false;
			message->FindInt32("id", &entry.id);
			message->FindString("name", &entry.name);
			fDownloads.push_back(entry);
			break;
		}
		case AOMI_DOWNLOAD_PROGRESS:
			break;
		case AOMI_DOWNLOAD_FINISHED:
		{
			int32 id = 0;
			BString path;
			message->FindInt32("id", &id);
			message->FindString("path", &path);
			for (size_t i = 0; i < fDownloads.size(); i++) {
				if (fDownloads[i].id == id) {
					fDownloads[i].done = true;
					fDownloads[i].path = path;
					break;
				}
			}
			break;
		}
		case AOMI_DOWNLOAD_FAILED:
		{
			int32 id = 0;
			message->FindInt32("id", &id);
			for (size_t i = 0; i < fDownloads.size(); i++) {
				if (fDownloads[i].id == id) {
					fDownloads[i].done = true;
					fDownloads[i].failed = true;
					break;
				}
			}
			break;
		}
		case AOMI_CLOSE_WANTED:
			PostMessage(B_QUIT_REQUESTED);
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
			int32 index = fActive;
			message->FindInt32("tab", &index);
			_Close(index);
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
				fChrome->Invalidate();
			}
			break;
		case MSG_MANAGE_BOOKMARKS:
			aomi_open_bookmarks();
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
			fPrefs.showMenuBar = !fPrefs.showMenuBar;
			aomi_prefs_save(fPrefs);
			ReloadPrefs();
			break;
		case MSG_BOOKMARKS_BAR:
			fPrefs.showBookmarks = !fPrefs.showBookmarks;
			aomi_prefs_save(fPrefs);
			ReloadPrefs();
			break;
		case MSG_SETTINGS:
			OpenSettings();
			break;
		case MSG_SETTINGS_CHANGED:
			ReloadPrefs();
			break;
		case MSG_FIND:
			if (fFindBar->IsHidden())
				fFindBar->Show();
			fFind->MakeFocus(true);
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
			BAppearanceKit::set_view_glass(fChrome, true);
			if (fMenu != NULL)
				BAppearanceKit::set_view_glass(fMenu, true);
			for (uint32 i = 0; i < fTabs.size(); i++) {
				if (fTabs[i].view == NULL)
					continue;
				if (fTabs[i].url.StartsWith("aomi:") || fTabs[i].url.Length() == 0)
					continue;
				fTabs[i].view->LoadURL(fTabs[i].url.String());
			}
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
			bool newTab = true;
			message->FindBool("newTab", &newTab);
			if (message->FindString("url", &url) == B_OK && url.Length() > 0)
				OpenURL(url.String(), newTab);
			break;
		}
		case MSG_DOWNLOAD_URL:
		{
			BString url;
			if (message->FindString("url", &url) == B_OK)
				aomi_start_download(url.String(), fPrefs.downloadPath.String());
			break;
		}
		case B_FONTS_UPDATED:
			static_cast<ChromeView*>(fChrome)->ApplyFontScale();
			static_cast<NewTabView*>(fNewTab)->ApplyFontScale();
			if (GetLayout() != NULL)
				GetLayout()->InvalidateLayout();
			BWindow::MessageReceived(message);
			break;
		case B_COLORS_UPDATED:
			fChrome->Invalidate();
			fNewTab->Invalidate();
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
	BRect frame(rect.left, rect.bottom + 6, rect.left + 280, rect.bottom + 90);
	FloatPanel* panel = new FloatPanel(frame, "");
	BView* view = panel->Panel();
	Palette p = palette();
	BString host(fTabs[fActive].url);
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
	view->SetLayout(new BGroupLayout(B_VERTICAL, 4));
	view->AddChild(title);
	view->AddChild(detail);
	title->SetHighColor(p.text);
	detail->SetHighColor(p.sub);
	panel->Show();
	(void)p;
}


void
AomiWindow::OpenDownloads()
{
	ChromeView* chrome = static_cast<ChromeView*>(fChrome);
	BRect rect = chrome->DownloadsRect();
	chrome->ConvertToScreen(&rect);
	BRect frame(rect.right - 360, rect.bottom + 6, rect.right, rect.bottom + 220);
	FloatPanel* panel = new FloatPanel(frame, "");
	BView* view = panel->Panel();
	view->SetLayout(new BGroupLayout(B_VERTICAL, 4));
	BStringView* title = new BStringView("dl", B_TRANSLATE("Downloads"));
	title->SetFont(be_bold_font);
	view->AddChild(title);
	if (fDownloads.empty()) {
		view->AddChild(new BStringView("empty",
			B_TRANSLATE("Files you download are listed here.")));
	} else {
		for (int32 i = (int32)fDownloads.size() - 1; i >= 0 && i >= (int32)fDownloads.size() - 5; i--) {
			BString row(fDownloads[i].name);
			if (fDownloads[i].failed)
				row << " – Failed";
			else if (fDownloads[i].done)
				row << " – Done";
			else
				row << " – Downloading…";
			view->AddChild(new BStringView("dlrow", row.String()));
		}
	}
	BButton* all = new BButton("all", B_TRANSLATE("Show all downloads"),
		new BMessage(MSG_SHOW_ALL_DOWNLOADS));
	all->SetTarget(this);
	view->AddChild(all);
	BButton* folder = new BButton("folder", B_TRANSLATE("Open folder"),
		new BMessage(MSG_OPEN_FOLDER));
	folder->SetTarget(this);
	view->AddChild(folder);
	panel->Show();
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
	bookmarks->AddItem(new BMenuItem(B_TRANSLATE("Show bookmarks bar"),
		new BMessage(MSG_BOOKMARKS_BAR)));
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
	menu->AddItem(new BMenuItem(B_TRANSLATE("Show menu bar"),
		new BMessage(MSG_MENU_BAR)));
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
	history->SetTargetForItems(this);
	bookmarks->SetTargetForItems(this);
	ChromeView* chrome = static_cast<ChromeView*>(fChrome);
	BRect rect = chrome->MenuRect();
	chrome->ConvertToScreen(&rect);
	menu->Go(BPoint(rect.left, rect.bottom + 4), true, true, true);
	delete menu;
}


void
AomiWindow::OpenSettings()
{
	(new AomiSettingsWindow())->Show();
}


void
AomiWindow::_Load(const char* url)
{
	if (fActive >= 0)
		_LoadOn(fActive, url);
}


static const uint32 kPrefChanged = 'pchg';
static const uint32 kPrefRevert = 'prev';
static const uint32 kPrefDefaults = 'pdef';
static const uint32 kPrefStart = 'pstt';
static const uint32 kPrefEngine = 'peng';


AomiSettingsWindow::AomiSettingsWindow()
	:
	BWindow(BRect(360, 120, 919, 700), B_TRANSLATE("Aomi settings"),
		B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS)
{
	BAppearanceKit::set_window_glass(this, true);
	fOpened = fPrefs = aomi_prefs();
	BPopUpMenu* starts = new BPopUpMenu(B_TRANSLATE("New tab page"));
	starts->SetRadioMode(true);
	starts->AddItem(new BMenuItem(B_TRANSLATE("New tab page"),
		new BMessage(kPrefStart)));
	starts->AddItem(new BMenuItem(B_TRANSLATE("Home page"),
		new BMessage(kPrefStart)));
	starts->AddItem(new BMenuItem(B_TRANSLATE("Last session"),
		new BMessage(kPrefStart)));
	starts->ItemAt(fPrefs.start >= 0 && fPrefs.start <= 2
		? fPrefs.start : 0)->SetMarked(true);
	fStart = new BMenuField("start", NULL, starts);
	fHome = new BTextControl("home", NULL, fPrefs.home.String(),
		new BMessage(kPrefChanged));
	BPopUpMenu* engines = new BPopUpMenu("DuckDuckGo");
	engines->SetRadioMode(true);
	engines->AddItem(new BMenuItem("DuckDuckGo", new BMessage(kPrefEngine)));
	engines->AddItem(new BMenuItem("Google", new BMessage(kPrefEngine)));
	engines->AddItem(new BMenuItem("Wikipedia", new BMessage(kPrefEngine)));
	fEngine = new BMenuField("engine", NULL, engines);
	fSearch = new BTextControl("search", NULL, fPrefs.search.String(),
		new BMessage(kPrefChanged));
	fShowHome = new BCheckBox("home button", B_TRANSLATE("Show home button"),
		new BMessage(kPrefChanged));
	fShowHome->SetValue(fPrefs.showHome);
	fShowBar = new BCheckBox("bar", B_TRANSLATE("Show bookmarks bar"),
		new BMessage(kPrefChanged));
	fShowBar->SetValue(fPrefs.showBookmarks);
	fDownloads = new BTextControl("downloads", NULL,
		fPrefs.downloadPath.String(), new BMessage(kPrefChanged));

	BStringView* dlHead = new BStringView("dlh", B_TRANSLATE("Downloads"));
	dlHead->SetFont(be_bold_font);
	BStringView* privHead = new BStringView("prvh", B_TRANSLATE("Private windows"));
	privHead->SetFont(be_bold_font);
	BStringView* privNote = new BStringView("prvn", B_TRANSLATE(
		"A private window forgets its history, cookies and site data when you close it."));
	privNote->SetHighColor(palette().sub);

	auto makeLabel = [](const char* text) {
		BStringView* view = new BStringView("", text);
		view->SetAlignment(B_ALIGN_RIGHT);
		return view;
	};

	BLayoutBuilder::Group<>(this, B_VERTICAL, 10)
		.SetInsets(18, 16, 18, 14)
		.AddGrid(12, 8)
			.Add(makeLabel(B_TRANSLATE("Start with:")), 0, 0)
			.Add(fStart, 1, 0)
			.Add(makeLabel(B_TRANSLATE("Home page:")), 0, 1)
			.Add(fHome, 1, 1)
			.Add(makeLabel(B_TRANSLATE("Search engine:")), 0, 2)
			.Add(fEngine, 1, 2)
			.Add(makeLabel(B_TRANSLATE("Search address:")), 0, 3)
			.Add(fSearch, 1, 3)
			.Add(fShowHome, 1, 4)
			.Add(fShowBar, 1, 5)
		.End()
		.AddStrut(4)
		.Add(dlHead)
		.AddGrid(12, 8)
			.Add(makeLabel(B_TRANSLATE("Save downloads to:")), 0, 0)
			.AddGroup(B_HORIZONTAL, 6, 1, 0)
				.Add(fDownloads, 1)
				.Add(new BButton("choose", B_TRANSLATE("Choose…"),
					new BMessage(MSG_OPEN_FOLDER)))
			.End()
		.End()
		.AddStrut(4)
		.Add(privHead)
		.Add(privNote)
		.AddGlue()
		.AddGroup(B_HORIZONTAL, 8)
			.AddGlue()
			.Add(new BButton("revert", B_TRANSLATE("Revert"),
				new BMessage(kPrefRevert)))
			.Add(new BButton("defaults", B_TRANSLATE("Defaults"),
				new BMessage(kPrefDefaults)))
		.End();
}


void
AomiSettingsWindow::_Save()
{
	fPrefs.home = fHome->Text();
	fPrefs.search = fSearch->Text();
	fPrefs.showHome = fShowHome->Value() != 0;
	fPrefs.showBookmarks = fShowBar->Value() != 0;
	fPrefs.downloadPath = fDownloads->Text();
	BMenu* menu = fStart->Menu();
	fPrefs.start = 0;
	if (menu->ItemAt(1) != NULL && menu->ItemAt(1)->IsMarked())
		fPrefs.start = 1;
	if (menu->ItemAt(2) != NULL && menu->ItemAt(2)->IsMarked())
		fPrefs.start = 2;
	aomi_prefs_save(fPrefs);
	for (int32 i = 0; i < be_app->CountWindows(); i++)
		be_app->WindowAt(i)->PostMessage(MSG_SETTINGS_CHANGED);
}


void
AomiSettingsWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kPrefChanged:
			_Save();
			break;
		case kPrefStart:
			_Save();
			break;
		case kPrefEngine:
		{
			BMenuItem* item = fEngine->Menu()->FindMarked();
			if (item != NULL && strcmp(item->Label(), "Google") == 0)
				fSearch->SetText("https://www.google.com/search?q=%s");
			else if (item != NULL && strcmp(item->Label(), "Wikipedia") == 0)
				fSearch->SetText("https://en.wikipedia.org/w/index.php?search=%s");
			else
				fSearch->SetText("https://duckduckgo.com/?q=%s");
			_Save();
			break;
		}
		case kPrefRevert:
			fPrefs = fOpened;
			fHome->SetText(fPrefs.home.String());
			fSearch->SetText(fPrefs.search.String());
			fDownloads->SetText(fPrefs.downloadPath.String());
			fShowHome->SetValue(fPrefs.showHome);
			fShowBar->SetValue(fPrefs.showBookmarks);
			_Save();
			break;
		case kPrefDefaults:
			aomi_prefs_defaults(&fPrefs);
			fHome->SetText(fPrefs.home.String());
			fSearch->SetText(fPrefs.search.String());
			fDownloads->SetText(fPrefs.downloadPath.String());
			fShowHome->SetValue(fPrefs.showHome);
			fShowBar->SetValue(fPrefs.showBookmarks);
			_Save();
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}
