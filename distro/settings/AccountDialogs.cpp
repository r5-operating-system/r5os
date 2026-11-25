/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Users and Accounts in Settings: "Add account" and "Change password"
 * (generated/GEMINI-ACCOUNTS-SECURITY-PROMPT.md, "The mockups", 24 Nov
 * 2026), and the pieces the page shares with them. Every change goes to
 * Login through its admin port, as in the separate Users & Accounts
 * program. A password is never logged and never put in argv; the copies
 * this file makes are wiped after use.
 */


#include "AccountDialogs.h"

#include "AccountAdmin.h"

#include <Alert.h>
#include <Bitmap.h>
#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <Entry.h>
#include <FilePanel.h>
#include <Font.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <PasswordControl.h>
#include <R5Admin.h>
#include <R5Icons.h>
#include <R5UI.h>
#include <StringView.h>
#include <TextView.h>
#include <TranslationUtils.h>

#include <algorithm>
#include <ctype.h>
#include <math.h>
#include <new>
#include <pwd.h>
#include <stdio.h>
#include <string.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Settings: Account dialogs"


using BAppearanceKit::BPasswordControl;
using BAppearanceKit::BPasswordStrengthBar;


static const char* kLoginSignature = "application/x-vnd.Haiku-Login";
static const int32 kPictureSide = 128;
static const char* kDot = " \xc2\xb7 ";

static const uint32 kMsgEdited = 'acEd';
static const uint32 kMsgNameEdited = 'acNe';
static const uint32 kMsgUserEdited = 'acUe';
static const uint32 kMsgTypeChosen = 'acTy';
static const uint32 kMsgPickPicture = 'acPk';
static const uint32 kMsgSubmit = 'acOk';


// #pragma mark - shared helpers


float
account_unit()
{
	return be_plain_font->Size() / 12.0f;
}


static float
px(float value)
{
	return floorf(value * account_unit() + 0.5f);
}


static rgb_color
hint_color()
{
	return mix_color(ui_color(B_DOCUMENT_BACKGROUND_COLOR),
		ui_color(B_DOCUMENT_TEXT_COLOR), 140);
}


static rgb_color
panel_hint_color()
{
	return mix_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		ui_color(B_PANEL_TEXT_COLOR), 140);
}


static void
secure_zero(void* ptr, size_t length)
{
	volatile unsigned char* p = static_cast<volatile unsigned char*>(ptr);
	while (length-- > 0)
		*p++ = 0;
}


void
account_wipe(BString& text)
{
	int32 length = text.Length();
	if (length <= 0)
		return;
	char* buffer = text.LockBuffer(length);
	if (buffer != NULL)
		secure_zero(buffer, (size_t)length);
	text.UnlockBuffer(length);
	text.Truncate(0);
}


static void
wipe_field(BMessage& message, const char* field)
{
	const void* data = NULL;
	ssize_t size = 0;
	for (int32 i = 0; message.FindData(field, B_STRING_TYPE, i, &data, &size)
			== B_OK; i++) {
		if (data != NULL && size > 0)
			secure_zero(const_cast<void*>(data), (size_t)size);
	}
}


status_t
account_ask_login(BMessage& request, BMessage& reply, BString& error)
{
	BMessenger login(kLoginSignature);
	status_t status = B_OK;
	if (!login.IsValid()) {
		error = B_TRANSLATE("Sign in must be running to change accounts.");
		status = B_ERROR;
	} else {
		// Through Login's admin port, where the kernel names the sender.
		status = r5_admin_send(login, &request, &reply);
		if (status != B_OK)
			error = strerror(status);
	}
	wipe_field(request, "password");
	wipe_field(request, "current");
	if (status != B_OK)
		return status;

	int32 code = B_OK;
	if (reply.FindInt32("error", &code) != B_OK) {
		error = B_TRANSLATE("Sign in did not answer.");
		return B_ERROR;
	}
	if (code != B_OK) {
		const char* text = NULL;
		reply.FindString("text", &text);
		error = text != NULL ? text : strerror(code);
		return code;
	}
	return B_OK;
}


static void
show_error(const char* text)
{
	BAlert* alert = new BAlert(B_TRANSLATE("Users and Accounts"), text,
		B_TRANSLATE("OK"), NULL, NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go(NULL);
}


BBitmap*
account_picture_from_blob(const void* data, ssize_t size)
{
	// Login's format, the same as the separate Users & Accounts program:
	// width, height, bytes per row and colour space as int32, then the rows.
	if (data == NULL || size < (ssize_t)(sizeof(int32) * 4))
		return NULL;
	const uint8* bytes = (const uint8*)data;
	int32 width = 0;
	int32 height = 0;
	int32 bpr = 0;
	int32 space = 0;
	memcpy(&width, bytes, 4);
	memcpy(&height, bytes + 4, 4);
	memcpy(&bpr, bytes + 8, 4);
	memcpy(&space, bytes + 12, 4);
	if (width < 1 || height < 1 || width > 512 || height > 512)
		return NULL;
	if (space != B_RGBA32 && space != B_RGB32)
		return NULL;
	if (bpr < width * 4 || bpr > width * 4 + 64)
		return NULL;
	if (size < 16 + (ssize_t)bpr * height)
		return NULL;
	BBitmap* bitmap = new BBitmap(BRect(0, 0, width - 1, height - 1),
		(color_space)space);
	if (bitmap->InitCheck() != B_OK || bitmap->BytesPerRow() < width * 4) {
		delete bitmap;
		return NULL;
	}
	uint8* dest = (uint8*)bitmap->Bits();
	int32 copy = width * 4;
	for (int32 y = 0; y < height; y++)
		memcpy(dest + y * bitmap->BytesPerRow(), bytes + 16 + y * bpr, copy);
	if (space == B_RGB32) {
		for (int32 y = 0; y < height; y++) {
			uint8* row = dest + y * bitmap->BytesPerRow();
			for (int32 x = 0; x < width; x++)
				row[x * 4 + 3] = 255;
		}
	}
	return bitmap;
}


status_t
account_add_picture_blob(const BBitmap* picture, BMessage& message)
{
	if (picture == NULL || !picture->IsValid())
		return B_BAD_VALUE;
	int32 width = picture->Bounds().IntegerWidth() + 1;
	int32 height = picture->Bounds().IntegerHeight() + 1;
	int32 bpr = picture->BytesPerRow();
	int32 space = picture->ColorSpace();
	if (width < 1 || height < 1 || width > 512 || height > 512)
		return B_BAD_VALUE;
	if (space != B_RGBA32 && space != B_RGB32)
		return B_BAD_VALUE;
	if (bpr < width * 4 || bpr > width * 4 + 64)
		return B_BAD_VALUE;
	size_t pixels = (size_t)bpr * (size_t)height;
	size_t size = 16 + pixels;
	char* blob = new(std::nothrow) char[size];
	if (blob == NULL)
		return B_NO_MEMORY;
	memcpy(blob, &width, 4);
	memcpy(blob + 4, &height, 4);
	memcpy(blob + 8, &bpr, 4);
	memcpy(blob + 12, &space, 4);
	memcpy(blob + 16, picture->Bits(), pixels);
	status_t status = message.AddData("picture", B_RAW_TYPE, blob, size);
	delete[] blob;
	return status;
}


BBitmap*
account_load_picture(const entry_ref& ref, BString& error)
{
	BBitmap* loaded = BTranslationUtils::GetBitmap(&ref);
	if (loaded == NULL || !loaded->IsValid()) {
		delete loaded;
		error = B_TRANSLATE("That file is not a picture this computer can open.");
		return NULL;
	}

	// The middle square, scaled to the size Login keeps (well under its
	// 512 KiB limit, whatever the size of the photo).
	BRect source = loaded->Bounds();
	float side = std::min(source.Width(), source.Height());
	source.left = floorf(source.left + (source.Width() - side) / 2);
	source.top = floorf(source.top + (source.Height() - side) / 2);
	source.right = source.left + side;
	source.bottom = source.top + side;

	BRect target(0, 0, kPictureSide - 1, kPictureSide - 1);
	BBitmap* picture = new BBitmap(target, B_RGBA32, true);
	if (picture->InitCheck() != B_OK || !picture->Lock()) {
		delete picture;
		delete loaded;
		error = B_TRANSLATE("That picture could not be used.");
		return NULL;
	}
	BView* view = new BView(target, "scale", 0, 0);
	picture->AddChild(view);
	view->SetHighColor(255, 255, 255);
	view->FillRect(target);
	view->DrawBitmap(loaded, source, target, B_FILTER_BITMAP_BILINEAR);
	view->Sync();
	picture->RemoveChild(view);
	delete view;
	picture->Unlock();
	delete loaded;

	uint8* bits = (uint8*)picture->Bits();
	for (int32 y = 0; y < kPictureSide; y++) {
		uint8* row = bits + y * picture->BytesPerRow();
		for (int32 x = 0; x < kPictureSide; x++)
			row[x * 4 + 3] = 255;
	}
	return picture;
}


static BBitmap*
person_icon(float side)
{
	// As Sign in (distro/login/SignInLook.h).
	BBitmap* icon = r5_mime_icon_bitmap("application/x-vnd.Be-PEPL", side);
	if (icon == NULL)
		icon = r5_mime_icon_bitmap("application/x-person", side);
	return icon;
}


// The photo inside a circle: transparent outside it, with a soft edge.
static BBitmap*
round_picture(const BBitmap* photo, int32 side)
{
	if (photo == NULL || side < 4 || (photo->ColorSpace() != B_RGBA32
		&& photo->ColorSpace() != B_RGB32))
		return NULL;
	BBitmap* round = new BBitmap(BRect(0, 0, side - 1, side - 1), B_RGBA32);
	if (round->InitCheck() != B_OK) {
		delete round;
		return NULL;
	}
	int32 srcSide = photo->Bounds().IntegerWidth() + 1;
	int32 srcHeight = photo->Bounds().IntegerHeight() + 1;
	if (srcHeight < srcSide)
		srcSide = srcHeight;
	const uint8* src = (const uint8*)photo->Bits();
	int32 srcBpr = photo->BytesPerRow();
	uint8* bits = (uint8*)round->Bits();
	float center = side / 2.0f;
	for (int32 y = 0; y < side; y++) {
		uint8* row = bits + y * round->BytesPerRow();
		for (int32 x = 0; x < side; x++) {
			float dx = x + 0.5f - center;
			float dy = y + 0.5f - center;
			float edge = center - sqrtf(dx * dx + dy * dy);
			uint8* pixel = row + x * 4;
			if (edge <= 0) {
				memset(pixel, 0, 4);
				continue;
			}
			int32 sx = (int32)((x + 0.5f) * srcSide / side);
			int32 sy = (int32)((y + 0.5f) * srcSide / side);
			if (sx >= srcSide)
				sx = srcSide - 1;
			if (sy >= srcSide)
				sy = srcSide - 1;
			const uint8* sample = src + sy * srcBpr + sx * 4;
			pixel[0] = sample[0];
			pixel[1] = sample[1];
			pixel[2] = sample[2];
			pixel[3] = edge >= 1.0f ? 255 : (uint8)(255 * edge);
		}
	}
	return round;
}


void
account_draw_picture(BView* view, BRect circle, const BBitmap* photo,
	bool admin)
{
	if (view == NULL || !circle.IsValid())
		return;
	int32 side = circle.IntegerWidth() + 1;
	if (photo != NULL) {
		BBitmap* round = round_picture(photo, side);
		if (round != NULL) {
			view->PushState();
			view->SetDrawingMode(B_OP_ALPHA);
			view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
			view->DrawBitmap(round, circle.LeftTop());
			view->SetHighColor(mix_color(ui_color(B_PANEL_BACKGROUND_COLOR),
				ui_color(B_DOCUMENT_TEXT_COLOR), 60));
			view->SetDrawingMode(B_OP_OVER);
			view->StrokeEllipse(circle);
			view->PopState();
			delete round;
			return;
		}
	}
	BBitmap* icon = person_icon(r5_avatar_icon_side(side));
	r5_avatar_draw(view, circle, icon, admin);
	delete icon;
}


bool
account_valid_username(const char* name)
{
	// The rule Login checks (AccountAdmin.cpp, valid_username()).
	if (name == NULL || name[0] == '\0' || strlen(name) > 31)
		return false;
	if (!islower((unsigned char)name[0]) && name[0] != '_')
		return false;
	for (const char* p = name; *p != '\0'; p++) {
		if (!islower((unsigned char)*p) && !isdigit((unsigned char)*p)
			&& *p != '_' && *p != '-')
			return false;
	}
	return true;
}


BString
account_suggest_username(const char* name)
{
	// The first word of the name, lower case, letters and numbers only.
	BString base;
	if (name != NULL) {
		for (const char* c = name; *c != '\0'; c++) {
			unsigned char ch = (unsigned char)*c;
			if (isspace(ch)) {
				if (base.Length() > 0)
					break;
				continue;
			}
			if (isalpha(ch) && ch < 0x80)
				base << (char)tolower(ch);
			else if (isdigit(ch) && base.Length() > 0)
				base << (char)ch;
			if (base.Length() >= 24)
				break;
		}
	}
	if (base.Length() == 0)
		return base;
	BString suggestion(base);
	for (int32 i = 2; getpwnam(suggestion.String()) != NULL && i < 100; i++)
		suggestion.SetToFormat("%s%" B_PRId32, base.String(), i);
	return suggestion;
}


// #pragma mark - AccountWrapLabel


AccountWrapLabel::AccountWrapLabel(const char* name, const char* text,
	bool small)
	:
	BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE | B_FRAME_EVENTS),
	fText(text),
	fHasColor(false),
	fSmall(small),
	fBold(false)
{
	SetFlags(Flags() | B_TRANSPARENT_BACKGROUND);
}


void
AccountWrapLabel::SetText(const char* text)
{
	if (fText == text)
		return;
	fText = text;
	InvalidateLayout();
	Invalidate();
}


void
AccountWrapLabel::SetTextColor(rgb_color color)
{
	fColor = color;
	fHasColor = true;
	Invalidate();
}


void
AccountWrapLabel::SetBold(bool bold)
{
	fBold = bold;
	InvalidateLayout();
	Invalidate();
}


void
AccountWrapLabel::AttachedToWindow()
{
	BView::AttachedToWindow();
	AdoptParentColors();
}


BFont
AccountWrapLabel::_Font() const
{
	BFont font(fBold ? be_bold_font : be_plain_font);
	if (fSmall)
		font.SetSize(floorf(font.Size() * 11.0f / 12.0f * 2 + 0.5f) / 2);
	return font;
}


int32
AccountWrapLabel::_Lines(float width, BString* lines) const
{
	BFont font = _Font();
	int32 count = 0;
	BString line;
	BString word;
	const char* text = fText.String();
	for (const char* c = text;; c++) {
		if (*c == ' ' || *c == '\n' || *c == '\0') {
			BString candidate(line);
			if (candidate.Length() > 0 && word.Length() > 0)
				candidate << ' ';
			candidate << word;
			if (line.Length() > 0 && width > 0
				&& font.StringWidth(candidate.String()) > width) {
				if (lines != NULL && count < 32)
					lines[count] = line;
				count++;
				line = word;
			} else
				line = candidate;
			word = "";
			if (*c == '\n' || *c == '\0') {
				if (line.Length() > 0 || *c == '\n') {
					if (lines != NULL && count < 32)
						lines[count] = line;
					count++;
				}
				line = "";
			}
			if (*c == '\0' || count >= 32)
				break;
		} else
			word << *c;
	}
	return count;
}


void
AccountWrapLabel::Draw(BRect updateRect)
{
	(void)updateRect;
	BFont font = _Font();
	SetFont(&font);
	font_height height;
	font.GetHeight(&height);
	float lineHeight = ceilf(height.ascent + height.descent + height.leading);
	BString lines[32];
	int32 count = std::min(_Lines(Bounds().Width(), lines), (int32)32);
	SetDrawingMode(B_OP_OVER);
	SetHighColor(fHasColor ? fColor : HighColor());
	float y = ceilf(height.ascent);
	for (int32 i = 0; i < count; i++) {
		DrawString(lines[i].String(), BPoint(0, y));
		y += lineHeight;
	}
}


void
AccountWrapLabel::FrameResized(float width, float height)
{
	BView::FrameResized(width, height);
	Invalidate();
}


BSize
AccountWrapLabel::MinSize()
{
	BFont font = _Font();
	font_height height;
	font.GetHeight(&height);
	return BSize(font.StringWidth("The passwords don't match"),
		ceilf(height.ascent + height.descent + height.leading));
}


BSize
AccountWrapLabel::PreferredSize()
{
	BSize size = MinSize();
	float min;
	float max;
	float preferred;
	GetHeightForWidth(size.width, &min, &max, &preferred);
	size.height = preferred;
	return size;
}


bool
AccountWrapLabel::HasHeightForWidth()
{
	return true;
}


void
AccountWrapLabel::GetHeightForWidth(float width, float* min, float* max,
	float* preferred)
{
	BFont font = _Font();
	font_height height;
	font.GetHeight(&height);
	float lineHeight = ceilf(height.ascent + height.descent + height.leading);
	int32 count = _Lines(width, NULL);
	if (count < 1)
		count = 1;
	float total = count * lineHeight;
	if (min != NULL)
		*min = total;
	if (max != NULL)
		*max = total;
	if (preferred != NULL)
		*preferred = total;
}


// #pragma mark - AccountTextField


static BFont
field_font()
{
	BFont font(be_plain_font);
	font.SetSize(floorf(be_plain_font->Size() * 14.0f / 12.0f + 0.5f));
	return font;
}


static float
ring_width()
{
	return floorf(2.0f * account_unit() + 0.5f);
}


class FieldTextView : public BTextView {
public:
	FieldTextView(AccountTextField* field)
		:
		BTextView(BRect(0, 0, 100, 20), "text", BRect(0, 0, 100, 20),
			B_FOLLOW_NONE, B_WILL_DRAW | B_NAVIGABLE),
		fField(field)
	{
		SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
		SetLowUIColor(B_DOCUMENT_BACKGROUND_COLOR);
		rgb_color text = ui_color(B_DOCUMENT_TEXT_COLOR);
		BFont font = field_font();
		SetFontAndColor(&font, B_FONT_ALL, &text);
		SetWordWrap(false);
		MakeResizable(false);
		SetMaxBytes(200);
	}

	virtual void KeyDown(const char* bytes, int32 numBytes)
	{
		if (numBytes == 1 && bytes[0] == B_ENTER) {
			fField->EditingDone();
			if (Window() != NULL && Window()->DefaultButton() != NULL
				&& Window()->DefaultButton()->IsEnabled())
				Window()->DefaultButton()->Invoke();
			return;
		}
		if (numBytes == 1 && bytes[0] == B_TAB) {
			BView::KeyDown(bytes, numBytes);
			return;
		}
		BTextView::KeyDown(bytes, numBytes);
	}

	virtual void MakeFocus(bool focus = true)
	{
		bool had = IsFocus();
		BTextView::MakeFocus(focus);
		if (had && !focus)
			fField->EditingDone();
		if (focus && !had)
			SelectAll();
		fField->Invalidate();
	}

	virtual void Draw(BRect updateRect)
	{
		BTextView::Draw(updateRect);
		if (TextLength() > 0)
			return;
		const char* placeholder = fField->Placeholder();
		if (placeholder == NULL || placeholder[0] == '\0')
			return;
		BFont font = field_font();
		font_height height;
		font.GetHeight(&height);
		PushState();
		SetFont(&font);
		SetDrawingMode(B_OP_OVER);
		SetHighColor(hint_color());
		DrawString(placeholder, BPoint(TextRect().left + 1,
			TextRect().top + height.ascent));
		PopState();
	}

protected:
	virtual void InsertText(const char* text, int32 length, int32 offset,
		const text_run_array* runs)
	{
		// One line: a pasted line break becomes a space.
		BString clean;
		clean.SetTo(text, length);
		clean.ReplaceAll('\n', ' ');
		clean.ReplaceAll('\r', ' ');
		BTextView::InsertText(clean.String(), clean.Length(), offset, runs);
		fField->TextChanged();
		Invalidate();
	}

	virtual void DeleteText(int32 from, int32 to)
	{
		BTextView::DeleteText(from, to);
		fField->TextChanged();
		Invalidate();
	}

private:
	AccountTextField*	fField;
};


AccountTextField::AccountTextField(const char* name, BMessage* message)
	:
	BControl(name, NULL, message, B_WILL_DRAW | B_FRAME_EVENTS
		| B_FULL_UPDATE_ON_RESIZE),
	fTextView(NULL),
	fModification(NULL),
	fCompact(false)
{
	SetViewColor(B_TRANSPARENT_COLOR);
	fTextView = new FieldTextView(this);
	AddChild(fTextView);
}


AccountTextField::~AccountTextField()
{
	delete fModification;
}


const char*
AccountTextField::Text() const
{
	return fTextView->Text();
}


void
AccountTextField::SetText(const char* text)
{
	fLastSent = text != NULL ? text : "";
	fTextView->SetText(text != NULL ? text : "");
	Invalidate();
}


void
AccountTextField::SetPlaceholder(const char* text)
{
	fPlaceholder = text;
	fTextView->Invalidate();
}


void
AccountTextField::SetRightHint(const char* text)
{
	if (fRightHint == text)
		return;
	fRightHint = text;
	_LayoutText();
	Invalidate();
}


void
AccountTextField::SetModificationMessage(BMessage* message)
{
	delete fModification;
	fModification = message;
}


void
AccountTextField::SetCompact(bool compact)
{
	if (fCompact == compact)
		return;
	fCompact = compact;
	InvalidateLayout();
}


void
AccountTextField::TextChanged()
{
	if (fModification != NULL && Window() != NULL)
		Invoke(fModification);
}


void
AccountTextField::EditingDone()
{
	if (fLastSent == Text())
		return;
	fLastSent = Text();
	if (Message() != NULL)
		Invoke();
}


void
AccountTextField::AttachedToWindow()
{
	BControl::AttachedToWindow();
	_LayoutText();
}


void
AccountTextField::FrameResized(float width, float height)
{
	BControl::FrameResized(width, height);
	_LayoutText();
}


void
AccountTextField::_LayoutText()
{
	BRect bounds = Bounds();
	if (bounds.Width() <= 0 || bounds.Height() <= 0)
		return;
	float u = account_unit();
	float left = ring_width() + 10.0f * u;
	float right = ring_width() + 10.0f * u;
	if (fRightHint.Length() > 0) {
		BFont small(be_plain_font);
		small.SetSize(floorf(be_plain_font->Size() * 11.0f / 12.0f));
		float hint = small.StringWidth(fRightHint.String()) + 8.0f * u;
		if (hint > bounds.Width() / 2)
			hint = bounds.Width() / 2;
		right += hint;
	}
	BFont font = field_font();
	font_height height;
	font.GetHeight(&height);
	float textHeight = ceilf(height.ascent + height.descent + height.leading);
	float top = floorf((bounds.Height() - textHeight) / 2);
	float width = bounds.Width() - left - right;
	if (width < 10)
		width = 10;
	fTextView->MoveTo(left, top);
	fTextView->ResizeTo(width, textHeight);
	fTextView->SetTextRect(BRect(0, 0, width, textHeight));
}


void
AccountTextField::Draw(BRect updateRect)
{
	(void)updateRect;
	// The frame of BAppearanceKit::BPasswordControl (PasswordControl.cpp),
	// so a field and a password field beside it look the same.
	BRect bounds = Bounds();
	float u = account_unit();
	float radius = 4.0f * u;
	float ring = ring_width();
	BRect field = bounds.InsetByCopy(ring, ring);
	rgb_color parent = Parent() != NULL ? Parent()->ViewColor()
		: ui_color(B_PANEL_BACKGROUND_COLOR);
	if (parent == B_TRANSPARENT_COLOR)
		parent = ui_color(B_PANEL_BACKGROUND_COLOR);
	SetHighColor(parent);
	FillRect(bounds);

	rgb_color bg = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
	rgb_color accent = ui_color(B_STATUS_BAR_COLOR);
	rgb_color border = tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		B_DARKEN_3_TINT);
	bool focus = fTextView->IsFocus() && Window() != NULL
		&& Window()->IsActive();

	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);
	if (focus && IsEnabled()) {
		border = accent;
		rgb_color halo = accent;
		halo.alpha = 56;
		SetHighColor(halo);
		SetPenSize(ring);
		BRect haloRect = bounds.InsetByCopy(ring / 2, ring / 2);
		StrokeRoundRect(haloRect, radius + ring / 2, radius + ring / 2);
		SetPenSize(1.0f);
	}
	rgb_color fill = IsEnabled() ? bg
		: mix_color(bg, ui_color(B_PANEL_BACKGROUND_COLOR), 128);
	fill.alpha = 255;
	SetDrawingMode(B_OP_COPY);
	SetHighColor(fill);
	FillRoundRect(field, radius, radius);
	SetDrawingMode(B_OP_ALPHA);
	SetHighColor(0, 0, 0, 20);
	StrokeLine(BPoint(field.left + radius, field.top + 1),
		BPoint(field.right - radius, field.top + 1));
	SetHighColor(border);
	StrokeRoundRect(field, radius, radius);
	SetDrawingMode(B_OP_COPY);

	if (fRightHint.Length() > 0) {
		BFont small(be_plain_font);
		small.SetSize(floorf(be_plain_font->Size() * 11.0f / 12.0f));
		BString hint(fRightHint);
		small.TruncateString(&hint, B_TRUNCATE_MIDDLE, bounds.Width() / 2);
		font_height height;
		small.GetHeight(&height);
		SetFont(&small);
		SetLowColor(fill);
		SetHighColor(hint_color());
		float x = field.right - 10.0f * u - small.StringWidth(hint.String());
		float y = floorf(bounds.top + (bounds.Height() + height.ascent
			- height.descent) / 2);
		DrawString(hint.String(), BPoint(x, y));
	}
}


void
AccountTextField::MouseDown(BPoint where)
{
	(void)where;
	if (IsEnabled())
		fTextView->MakeFocus(true);
}


void
AccountTextField::MakeFocus(bool focus)
{
	fTextView->MakeFocus(focus);
}


void
AccountTextField::MessageReceived(BMessage* message)
{
	if (message->what == B_COLORS_UPDATED) {
		_ApplyColors();
		return;
	}
	BControl::MessageReceived(message);
}


void
AccountTextField::_ApplyColors()
{
	bool enabled = IsEnabled();
	rgb_color text = ui_color(B_DOCUMENT_TEXT_COLOR);
	if (!enabled)
		text = mix_color(text, ui_color(B_DOCUMENT_BACKGROUND_COLOR), 128);
	rgb_color bg = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
	if (!enabled)
		bg = mix_color(bg, ui_color(B_PANEL_BACKGROUND_COLOR), 128);
	fTextView->SetViewColor(bg);
	fTextView->SetLowColor(bg);
	BFont font = field_font();
	fTextView->SetFontAndColor(&font, B_FONT_ALL, &text);
	fTextView->Invalidate();
	Invalidate();
}


void
AccountTextField::SetEnabled(bool enabled)
{
	if (enabled == IsEnabled())
		return;
	BControl::SetEnabled(enabled);
	fTextView->MakeEditable(enabled);
	fTextView->MakeSelectable(enabled);
	_ApplyColors();
}


void
AccountTextField::WindowActivated(bool active)
{
	BControl::WindowActivated(active);
	Invalidate();
}


BSize
AccountTextField::MinSize()
{
	float u = account_unit();
	return BSize(140.0f * u, floorf((fCompact ? 30.0f : 38.0f) * u)
		+ 2 * ring_width() - 1);
}


BSize
AccountTextField::MaxSize()
{
	return BSize(B_SIZE_UNLIMITED, MinSize().height);
}


BSize
AccountTextField::PreferredSize()
{
	return BSize(240.0f * account_unit(), MinSize().height);
}


// #pragma mark - small views of the two windows


static BStringView*
field_label(const char* name, const char* text)
{
	BStringView* label = new BStringView(name, text);
	BFont font(be_bold_font);
	font.SetSize(floorf(be_plain_font->Size() * 12.0f / 12.0f));
	label->SetFont(&font);
	return label;
}


static BStringView*
small_label(const char* name, const char* text)
{
	BStringView* label = new BStringView(name, text);
	BFont font(be_plain_font);
	font.SetSize(floorf(be_plain_font->Size() * 11.0f / 12.0f * 2 + 0.5f) / 2);
	label->SetFont(&font);
	label->SetHighColor(panel_hint_color());
	return label;
}


// Two side-by-side choices (Standard, Administrator), each with a bold name
// and one line. Value 0 is Standard, 1 is Administrator.
class TypeChoice : public BControl {
public:
	TypeChoice(BMessage* message)
		:
		BControl("account type", NULL, message,
			B_WILL_DRAW | B_NAVIGABLE | B_FULL_UPDATE_ON_RESIZE)
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	}

	virtual void Draw(BRect updateRect)
	{
		(void)updateRect;
		static const char* kNames[] = { B_TRANSLATE_MARK("Standard"),
			B_TRANSLATE_MARK("Administrator") };
		static const char* kLines[] = {
			B_TRANSLATE_MARK("Uses apps, changes own settings"),
			B_TRANSLATE_MARK("Can also change the computer") };
		BRect bounds = Bounds();
		float gap = px(8);
		float width = (bounds.Width() + 1 - gap) / 2;
		float radius = px(6);
		rgb_color document = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
		rgb_color accent = ui_color(B_STATUS_BAR_COLOR);
		rgb_color text = ui_color(B_DOCUMENT_TEXT_COLOR);
		rgb_color border = tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
			B_DARKEN_2_TINT);
		BFont bold(be_bold_font);
		BFont small(be_plain_font);
		small.SetSize(floorf(be_plain_font->Size() * 11.0f / 12.0f * 2 + 0.5f)
			/ 2);
		font_height boldHeight;
		font_height smallHeight;
		bold.GetHeight(&boldHeight);
		small.GetHeight(&smallHeight);
		bool focus = IsFocus() && Window() != NULL && Window()->IsActive();

		for (int32 i = 0; i < 2; i++) {
			BRect part(bounds.left + i * (width + gap), bounds.top,
				bounds.left + i * (width + gap) + width - 1, bounds.bottom);
			bool chosen = Value() == i;
			rgb_color fill = chosen ? mix_color(document, accent, 28) : document;
			if (!IsEnabled())
				fill = mix_color(fill, ui_color(B_PANEL_BACKGROUND_COLOR), 128);
			SetHighColor(fill);
			FillRoundRect(part, radius, radius);
			SetHighColor(chosen ? accent : border);
			StrokeRoundRect(part, radius, radius);
			if (chosen) {
				StrokeRoundRect(part.InsetByCopy(1, 1), radius - 1,
					radius - 1);
			}
			if (chosen && focus) {
				rgb_color halo = accent;
				halo.alpha = 80;
				SetDrawingMode(B_OP_ALPHA);
				SetHighColor(halo);
				StrokeRoundRect(part.InsetByCopy(2, 2), radius - 2,
					radius - 2);
				SetDrawingMode(B_OP_COPY);
			}

			float left = part.left + px(12);
			float block = boldHeight.ascent + boldHeight.descent + px(3)
				+ smallHeight.ascent + smallHeight.descent;
			float y = floorf(part.top + (part.Height() - block) / 2
				+ boldHeight.ascent);
			SetLowColor(fill);
			SetFont(&bold);
			rgb_color nameColor = IsEnabled() ? text
				: mix_color(text, fill, 128);
			SetHighColor(nameColor);
			BString name(B_TRANSLATE_NOCOLLECT(kNames[i]));
			bold.TruncateString(&name, B_TRUNCATE_END, part.Width() - px(20));
			DrawString(name.String(), BPoint(left, y));
			SetFont(&small);
			SetHighColor(mix_color(fill, text, 140));
			BString line(B_TRANSLATE_NOCOLLECT(kLines[i]));
			small.TruncateString(&line, B_TRUNCATE_END, part.Width() - px(20));
			DrawString(line.String(), BPoint(left, y + boldHeight.descent
				+ px(3) + smallHeight.ascent));
		}
	}

	virtual void MouseDown(BPoint where)
	{
		if (!IsEnabled())
			return;
		MakeFocus(true);
		int32 value = where.x > Bounds().Width() / 2 ? 1 : 0;
		_Choose(value);
	}

	virtual void KeyDown(const char* bytes, int32 numBytes)
	{
		if (numBytes == 1 && (bytes[0] == B_LEFT_ARROW
				|| bytes[0] == B_RIGHT_ARROW)) {
			_Choose(bytes[0] == B_LEFT_ARROW ? 0 : 1);
			return;
		}
		if (numBytes == 1 && bytes[0] == B_SPACE) {
			_Choose(Value() == 0 ? 1 : 0);
			return;
		}
		BControl::KeyDown(bytes, numBytes);
	}

	virtual void MakeFocus(bool focus = true)
	{
		BControl::MakeFocus(focus);
		Invalidate();
	}

	virtual BSize MinSize()
	{
		BFont small(be_plain_font);
		small.SetSize(floorf(be_plain_font->Size() * 11.0f / 12.0f));
		float width = small.StringWidth(
			B_TRANSLATE("Uses apps, changes own settings"));
		return BSize(2 * (width + px(24)) + px(8), px(54));
	}

	virtual BSize MaxSize()
	{
		return BSize(B_SIZE_UNLIMITED, MinSize().height);
	}

	virtual BSize PreferredSize()
	{
		return MinSize();
	}

private:
	void _Choose(int32 value)
	{
		if (value == Value())
			return;
		SetValue(value);
		Invoke();
	}
};


// The yellow note: the window tab colour mixed into the document
// background, so it follows the theme.
class NoteView : public BGroupView {
public:
	NoteView(const char* text)
		:
		BGroupView("note", B_VERTICAL)
	{
		rgb_color fill = _Fill();
		SetViewColor(fill);
		SetLowColor(fill);
		fLabel = new AccountWrapLabel("note text", text, false);
		fLabel->SetTextColor(ui_color(B_DOCUMENT_TEXT_COLOR));
		BLayoutBuilder::Group<>(this)
			.SetInsets(px(12), px(10), px(12), px(10))
			.Add(fLabel);
		SetFlags(Flags() | B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE);
	}

	// The fill is a color of its own: a change of theme gives it, and the
	// text that took it, the new fill.
	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == B_COLORS_UPDATED) {
			rgb_color fill = _Fill();
			SetViewColor(fill);
			SetLowColor(fill);
			fLabel->SetViewColor(fill);
			fLabel->SetLowColor(fill);
			fLabel->SetTextColor(ui_color(B_DOCUMENT_TEXT_COLOR));
			fLabel->Invalidate();
			Invalidate();
		}
		BGroupView::MessageReceived(message);
	}

	virtual void Draw(BRect updateRect)
	{
		(void)updateRect;
		rgb_color fill = _Fill();
		BRect bounds = Bounds();
		SetHighColor(Parent() != NULL ? Parent()->ViewColor()
			: ui_color(B_PANEL_BACKGROUND_COLOR));
		FillRect(bounds);
		SetHighColor(fill);
		FillRoundRect(bounds, px(6), px(6));
		SetHighColor(mix_color(fill, ui_color(B_WINDOW_TAB_COLOR), 160));
		StrokeRoundRect(bounds, px(6), px(6));
	}

private:
	static rgb_color _Fill()
	{
		return mix_color(ui_color(B_DOCUMENT_BACKGROUND_COLOR),
			ui_color(B_WINDOW_TAB_COLOR), 70);
	}

	AccountWrapLabel*	fLabel;
};


// The round picture at the top of a window. With `pickable` it shows a
// small camera badge and sends its message on a click.
class PictureButton : public BControl {
public:
	PictureButton(float side, bool pickable, BMessage* message)
		:
		BControl("picture", NULL, message, B_WILL_DRAW),
		fPhoto(NULL),
		fAdmin(false),
		fPickable(pickable)
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		SetExplicitSize(BSize(side - 1, side - 1));
		if (pickable)
			SetToolTip(B_TRANSLATE("Choose picture…"));
	}

	~PictureButton()
	{
		delete fPhoto;
	}

	void SetPicture(const BBitmap* photo, bool admin)
	{
		delete fPhoto;
		fPhoto = photo != NULL ? new BBitmap(photo) : NULL;
		fAdmin = admin;
		Invalidate();
	}

	void SetAdmin(bool admin)
	{
		if (fAdmin == admin)
			return;
		fAdmin = admin;
		Invalidate();
	}

	const BBitmap* Picture() const
	{
		return fPhoto;
	}

	virtual void Draw(BRect updateRect)
	{
		(void)updateRect;
		BRect bounds = Bounds();
		account_draw_picture(this, bounds, fPhoto, fAdmin);
		if (!fPickable)
			return;
		float side = floorf(bounds.Width() * 0.36f);
		BRect badge(bounds.right - side, bounds.bottom - side, bounds.right,
			bounds.bottom);
		SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
		FillEllipse(badge.InsetByCopy(-1, -1));
		SetHighColor(ui_color(B_STATUS_BAR_COLOR));
		FillEllipse(badge);
		// A small camera: the body and the lens.
		rgb_color white = ui_color(B_STATUS_BAR_COLOR).IsLight()
			? make_color(0, 0, 0) : make_color(255, 255, 255);
		float w = badge.Width();
		BRect body(badge.left + w * 0.24f, badge.top + w * 0.36f,
			badge.right - w * 0.24f, badge.bottom - w * 0.28f);
		SetHighColor(white);
		FillRoundRect(body, w * 0.06f, w * 0.06f);
		FillRect(BRect(badge.left + w * 0.40f, badge.top + w * 0.28f,
			badge.left + w * 0.60f, body.top));
		float lens = w * 0.11f;
		BPoint center((body.left + body.right) / 2,
			(body.top + body.bottom) / 2);
		SetHighColor(ui_color(B_STATUS_BAR_COLOR));
		FillEllipse(center, lens, lens);
	}

	virtual void MouseDown(BPoint where)
	{
		(void)where;
		if (fPickable && IsEnabled())
			Invoke();
	}

private:
	BBitmap*	fPhoto;
	bool		fAdmin;
	bool		fPickable;
};


static void
set_match_line(BStringView* line, const char* first, const char* second)
{
	if (second == NULL || second[0] == '\0') {
		line->SetText(" ");
		return;
	}
	if (strcmp(first, second) == 0) {
		line->SetText(B_TRANSLATE("✓ The passwords match"));
		line->SetHighColor(ui_color(B_SUCCESS_COLOR));
	} else {
		line->SetText(B_TRANSLATE("× The passwords don't match yet"));
		line->SetHighColor(ui_color(B_FAILURE_COLOR));
	}
}


static void
clear_password(BPasswordControl* control)
{
	if (control == NULL)
		return;
	control->SetText("");
}


// #pragma mark - AddAccountWindow


class AddAccountWindow : public BWindow {
public:
								AddAccountWindow(BWindow* parent,
									const BMessenger& page);
	virtual						~AddAccountWindow();

	virtual	void				DispatchMessage(BMessage* message,
									BHandler* target);
	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			void				_Update();
			void				_Submit();

			BMessenger			fPage;
			PictureButton*		fPicture;
			AccountTextField*	fName;
			AccountTextField*	fUsername;
			BStringView*		fUserError;
			TypeChoice*			fType;
			BPasswordControl*	fPassword;
			BPasswordStrengthBar*	fStrength;
			BStringView*		fStrengthText;
			BPasswordControl*	fRepeat;
			BStringView*		fMatch;
			BButton*			fAdd;
			BFilePanel*			fPanel;
			BString				fSuggested;
			bool				fUserTyped;
};


AddAccountWindow::AddAccountWindow(BWindow* parent, const BMessenger& page)
	:
	BWindow(BRect(0, 0, px(480), px(560)), B_TRANSLATE("Add account"),
		B_TITLED_WINDOW_LOOK, B_MODAL_SUBSET_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_CLOSE_ON_ESCAPE),
	fPage(page),
	fPanel(NULL),
	fUserTyped(false)
{
	if (parent != NULL)
		AddToSubset(parent);

	fPicture = new PictureButton(px(64), true, new BMessage(kMsgPickPicture));
	fPicture->SetPicture(NULL, false);
	BStringView* heading = new BStringView("heading",
		B_TRANSLATE("Add an account"));
	BFont headingFont(be_bold_font);
	headingFont.SetSize(floorf(be_bold_font->Size() * 16.0f / 12.0f));
	heading->SetFont(&headingFont);
	AccountWrapLabel* intro = new AccountWrapLabel("intro",
		B_TRANSLATE("They get their own private home folder and settings."));
	intro->SetTextColor(panel_hint_color());

	fName = new AccountTextField("name", new BMessage(kMsgEdited));
	fName->SetModificationMessage(new BMessage(kMsgNameEdited));
	fUsername = new AccountTextField("username", new BMessage(kMsgEdited));
	fUsername->SetModificationMessage(new BMessage(kMsgUserEdited));
	BString homeHint(B_TRANSLATE("home: %path%"));
	homeHint.ReplaceFirst("%path%", "/boot/home/users/");
	fUsername->SetRightHint(homeHint.String());
	fUsername->TextView()->SetMaxBytes(31);
	fUserError = small_label("username error", " ");
	fUserError->SetHighColor(ui_color(B_FAILURE_COLOR));

	fType = new TypeChoice(new BMessage(kMsgTypeChosen));
	fType->SetValue(0);

	fPassword = new BPasswordControl("password", NULL);
	fStrength = new BPasswordStrengthBar("strength");
	fStrengthText = small_label("strength text", BPasswordStrengthBar::HintFor(0));
	fRepeat = new BPasswordControl("repeat", NULL);
	fMatch = small_label("match", " ");

	BButton* cancel = new BButton("cancel", B_TRANSLATE("Cancel"),
		new BMessage(B_QUIT_REQUESTED));
	fAdd = new BButton("add", B_TRANSLATE("Add account"),
		new BMessage(kMsgSubmit));
	fAdd->SetEnabled(false);

	BLayoutBuilder::Group<>(this, B_VERTICAL, px(6))
		.SetInsets(px(24), px(22), px(24), px(20))
		.AddGroup(B_HORIZONTAL, px(16))
			.Add(fPicture)
			.AddGroup(B_VERTICAL, px(4))
				.AddGlue()
				.Add(heading)
				.Add(intro)
				.AddGlue()
			.End()
		.End()
		.AddStrut(px(10))
		.Add(field_label("name label", B_TRANSLATE("Name")))
		.Add(fName)
		.AddStrut(px(6))
		.AddGroup(B_HORIZONTAL)
			.Add(field_label("username label", B_TRANSLATE("Username")))
			.AddGlue()
			.Add(small_label("username hint",
				B_TRANSLATE("Used to sign in; letters and numbers")))
		.End()
		.Add(fUsername)
		.Add(fUserError)
		.AddStrut(px(2))
		.Add(field_label("type label", B_TRANSLATE("Account type")))
		.Add(fType)
		.AddStrut(px(6))
		.Add(field_label("password label", B_TRANSLATE("Password")))
		.Add(fPassword)
		.Add(fStrength)
		.Add(fStrengthText)
		.AddStrut(px(6))
		.Add(field_label("repeat label", B_TRANSLATE("Repeat password")))
		.Add(fRepeat)
		.Add(fMatch)
		.AddStrut(px(10))
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancel)
			.Add(fAdd)
		.End();

	SetDefaultButton(fAdd);
	fName->MakeFocus(true);
	CenterOnScreen();
}


AddAccountWindow::~AddAccountWindow()
{
	delete fPanel;
}


bool
AddAccountWindow::QuitRequested()
{
	clear_password(fPassword);
	clear_password(fRepeat);
	return true;
}


void
AddAccountWindow::DispatchMessage(BMessage* message, BHandler* target)
{
	uint32 what = message->what;
	BWindow::DispatchMessage(message, target);
	// BPasswordControl has no modification message: look after every key.
	if (what == B_KEY_DOWN || what == B_PASTE || what == B_CUT
		|| what == B_UNDO || what == B_INPUT_METHOD_EVENT)
		_Update();
}


void
AddAccountWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgNameEdited:
			if (!fUserTyped) {
				// The modification message of SetText() comes later, so
				// kMsgUserEdited compares with the suggestion.
				fSuggested = account_suggest_username(fName->Text());
				fUsername->SetText(fSuggested.String());
			}
			_Update();
			break;
		case kMsgUserEdited:
			fUserTyped = fUsername->Text()[0] != '\0'
				&& fSuggested != fUsername->Text();
			_Update();
			break;
		case kMsgEdited:
		case kMsgTypeChosen:
			_Update();
			break;
		case kMsgPickPicture:
			if (message->HasRef("refs")) {
				entry_ref ref;
				message->FindRef("refs", &ref);
				BString error;
				BBitmap* picture = account_load_picture(ref, error);
				if (picture == NULL)
					show_error(error.String());
				else {
					fPicture->SetPicture(picture, fType->Value() == 1);
					delete picture;
				}
				break;
			}
			if (fPanel == NULL) {
				fPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this),
					NULL, B_FILE_NODE, false, new BMessage(kMsgPickPicture));
			}
			fPanel->Show();
			break;
		case kMsgSubmit:
			_Submit();
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
AddAccountWindow::_Update()
{
	const char* username = fUsername->Text();
	BString home(B_TRANSLATE("home: %path%"));
	BString path("/boot/home/users/");
	path << username;
	home.ReplaceFirst("%path%", path.String());
	fUsername->SetRightHint(home.String());

	bool nameOk = fName->Text()[0] != '\0'
		&& strchr(fName->Text(), ':') == NULL;
	bool userOk = account_valid_username(username);
	bool userFree = userOk && getpwnam(username) == NULL;
	if (username[0] == '\0')
		fUserError->SetText(" ");
	else if (!userOk) {
		fUserError->SetText(B_TRANSLATE("Start with a lowercase letter; then "
			"lowercase letters and numbers."));
	} else if (!userFree)
		fUserError->SetText(B_TRANSLATE("That username is already in use."));
	else
		fUserError->SetText(" ");

	BString password(fPassword->Text());
	BString repeat(fRepeat->Text());
	int32 strength = BPasswordStrengthBar::StrengthOf(password.String());
	fStrength->SetStrength(strength);
	fStrengthText->SetText(BPasswordStrengthBar::HintFor(strength));
	set_match_line(fMatch, password.String(), repeat.String());
	bool passwordOk = password.Length() > 0 && password == repeat;
	account_wipe(password);
	account_wipe(repeat);

	fPicture->SetAdmin(fType->Value() == 1);
	fAdd->SetEnabled(nameOk && userFree && passwordOk);
}


void
AddAccountWindow::_Submit()
{
	_Update();
	if (!fAdd->IsEnabled())
		return;

	BString name(fUsername->Text());
	BString password(fPassword->Text());
	BMessage request(kMsgAdminAdd);
	request.AddString("name", name.String());
	request.AddString("real", fName->Text());
	request.AddString("password", password.String());
	request.AddBool("admin", fType->Value() == 1);
	account_wipe(password);

	BMessage reply;
	BString error;
	if (account_ask_login(request, reply, error) != B_OK) {
		show_error(error.Length() > 0 ? error.String()
			: B_TRANSLATE("The account could not be added."));
		return;
	}

	if (fPicture->Picture() != NULL) {
		BMessage profile(kMsgAdminSetProfile);
		profile.AddString("name", name.String());
		if (account_add_picture_blob(fPicture->Picture(), profile) == B_OK) {
			BMessage profileReply;
			BString pictureError;
			if (account_ask_login(profile, profileReply, pictureError)
					!= B_OK) {
				BString text(B_TRANSLATE("The account was added, but its picture "
					"was not saved.\n%error%"));
				text.ReplaceFirst("%error%", pictureError.String());
				show_error(text.String());
			}
		}
	}

	BMessage added(kMsgAccountAdded);
	added.AddString("name", name.String());
	fPage.SendMessage(&added);
	PostMessage(B_QUIT_REQUESTED);
}


// #pragma mark - PasswordWindow


class PasswordWindow : public BWindow {
public:
								PasswordWindow(BWindow* parent,
									const BMessenger& page, const char* name,
									const char* realName, bool admin,
									bool self, const BBitmap* photo);

	virtual	void				DispatchMessage(BMessage* message,
									BHandler* target);
	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			void				_Update();
			void				_Submit();

			BMessenger			fPage;
			BString				fAccount;
			bool				fSelf;
			BPasswordControl*	fCurrent;
			BPasswordControl*	fNext;
			BPasswordStrengthBar*	fStrength;
			BStringView*		fStrengthText;
			BPasswordControl*	fRepeat;
			BStringView*		fMatch;
			BButton*			fChange;
};


PasswordWindow::PasswordWindow(BWindow* parent, const BMessenger& page,
	const char* name, const char* realName, bool admin, bool self,
	const BBitmap* photo)
	:
	BWindow(BRect(0, 0, px(440), px(400)), B_TRANSLATE("Change password"),
		B_TITLED_WINDOW_LOOK, B_MODAL_SUBSET_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_CLOSE_ON_ESCAPE),
	fPage(page),
	fAccount(name),
	fSelf(self),
	fCurrent(NULL),
	fStrength(NULL),
	fStrengthText(NULL)
{
	if (parent != NULL)
		AddToSubset(parent);

	BString shown(realName != NULL && realName[0] != '\0' ? realName : name);
	PictureButton* picture = new PictureButton(px(48), false, NULL);
	picture->SetPicture(photo, admin);

	BString title;
	BString subtitle;
	if (self) {
		title = B_TRANSLATE("Change your password");
		subtitle << shown << kDot << "@" << name;
	} else {
		title = B_TRANSLATE("New password for %name%");
		title.ReplaceFirst("%name%", shown.String());
		subtitle << "@" << name << kDot
			<< (admin ? B_TRANSLATE("Administrator") : B_TRANSLATE("Standard"));
	}
	BStringView* heading = new BStringView("heading", title.String());
	BFont headingFont(be_bold_font);
	headingFont.SetSize(floorf(be_bold_font->Size() * 15.0f / 12.0f));
	heading->SetFont(&headingFont);
	BStringView* sub = small_label("subtitle", subtitle.String());

	fNext = new BPasswordControl("new", NULL);
	fRepeat = new BPasswordControl("repeat", NULL);
	fMatch = small_label("match", " ");
	fChange = new BButton("change", B_TRANSLATE("Change password"),
		new BMessage(kMsgSubmit));
	fChange->SetEnabled(false);
	BButton* cancel = new BButton("cancel", B_TRANSLATE("Cancel"),
		new BMessage(B_QUIT_REQUESTED));

	BGroupLayout* layout = BLayoutBuilder::Group<>(this, B_VERTICAL, px(6))
		.SetInsets(px(24), px(22), px(24), px(20))
		.AddGroup(B_HORIZONTAL, px(14))
			.Add(picture)
			.AddGroup(B_VERTICAL, px(3))
				.AddGlue()
				.Add(heading)
				.Add(sub)
				.AddGlue()
			.End()
		.End()
		.AddStrut(px(10))
		.Layout();

	if (self) {
		fCurrent = new BPasswordControl("current", NULL);
		fStrength = new BPasswordStrengthBar("strength");
		fStrengthText = small_label("strength text",
			BPasswordStrengthBar::HintFor(0));
		BLayoutBuilder::Group<>(layout)
			.Add(field_label("current label", B_TRANSLATE("Current password")))
			.Add(fCurrent)
			.AddStrut(px(6))
			.Add(field_label("new label", B_TRANSLATE("New password")))
			.Add(fNext)
			.Add(fStrength)
			.Add(fStrengthText)
			.AddStrut(px(6));
	} else {
		BString note(B_TRANSLATE("%name%'s old password is not needed. Tell "
			"%name% the new one; it works from the next sign in. Their saved "
			"passwords stay locked until they enter their old password once; "
			"otherwise they start with an empty key store."));
		note.ReplaceAll("%name%", shown.String());
		BLayoutBuilder::Group<>(layout)
			.Add(new NoteView(note.String()))
			.AddStrut(px(8))
			.Add(field_label("new label", B_TRANSLATE("New password")))
			.Add(fNext)
			.AddStrut(px(6));
	}
	BLayoutBuilder::Group<>(layout)
		.Add(field_label("repeat label", B_TRANSLATE("Repeat new password")))
		.Add(fRepeat)
		.Add(fMatch)
		.AddStrut(px(10))
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancel)
			.Add(fChange)
		.End();

	SetDefaultButton(fChange);
	if (fCurrent != NULL)
		fCurrent->MakeFocus(true);
	else
		fNext->MakeFocus(true);
	CenterOnScreen();
}


bool
PasswordWindow::QuitRequested()
{
	clear_password(fCurrent);
	clear_password(fNext);
	clear_password(fRepeat);
	return true;
}


void
PasswordWindow::DispatchMessage(BMessage* message, BHandler* target)
{
	uint32 what = message->what;
	BWindow::DispatchMessage(message, target);
	if (what == B_KEY_DOWN || what == B_PASTE || what == B_CUT
		|| what == B_UNDO || what == B_INPUT_METHOD_EVENT)
		_Update();
}


void
PasswordWindow::MessageReceived(BMessage* message)
{
	if (message->what == kMsgSubmit) {
		_Submit();
		return;
	}
	BWindow::MessageReceived(message);
}


void
PasswordWindow::_Update()
{
	BString next(fNext->Text());
	BString repeat(fRepeat->Text());
	if (fStrength != NULL) {
		int32 strength = BPasswordStrengthBar::StrengthOf(next.String());
		fStrength->SetStrength(strength);
		fStrengthText->SetText(BPasswordStrengthBar::HintFor(strength));
	}
	set_match_line(fMatch, next.String(), repeat.String());
	fChange->SetEnabled(next.Length() > 0 && next == repeat);
	account_wipe(next);
	account_wipe(repeat);
}


void
PasswordWindow::_Submit()
{
	_Update();
	if (!fChange->IsEnabled())
		return;

	BString next(fNext->Text());
	BString current(fCurrent != NULL ? fCurrent->Text() : "");
	BMessage request(kMsgAdminSetProfile);
	request.AddString("name", fAccount.String());
	request.AddString("password", next.String());
	if (fSelf)
		request.AddString("current", current.String());
	account_wipe(next);
	account_wipe(current);

	// Changing your own password also rewraps the key store (Argon2id), so
	// this can take a moment.
	BMessage reply;
	BString error;
	if (account_ask_login(request, reply, error) != B_OK) {
		show_error(error.Length() > 0 ? error.String()
			: B_TRANSLATE("The password could not be changed."));
		if (fCurrent != NULL) {
			clear_password(fCurrent);
			fCurrent->MakeFocus(true);
		}
		return;
	}

	BMessage changed(kMsgAccountPasswordChanged);
	changed.AddString("name", fAccount.String());
	fPage.SendMessage(&changed);
	PostMessage(B_QUIT_REQUESTED);
}


// #pragma mark - public


void
account_open_add_window(BWindow* parent, const BMessenger& page)
{
	(new AddAccountWindow(parent, page))->Show();
}


void
account_open_password_window(BWindow* parent, const BMessenger& page,
	const char* name, const char* realName, bool admin, bool self,
	const BBitmap* photo)
{
	if (name == NULL || name[0] == '\0')
		return;
	(new PasswordWindow(parent, page, name, realName, admin, self, photo))
		->Show();
}
