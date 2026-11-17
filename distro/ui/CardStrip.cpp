/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */



#include "CardStrip.h"

#include <Bitmap.h>
#include <Button.h>
#include <Font.h>
#include <InterfaceDefs.h>
#include <LayoutUtils.h>
#include <Message.h>
#include <String.h>
#include <Window.h>

#include <algorithm>
#include <math.h>
#include <new>


namespace BAppearanceKit {


static const char* kCardStripClassName = "BAppearanceKit::BCardStrip";
static const uint32 kMsgEmptyAction = 'csEA';


struct BCardStrip::Data {
	BString		emptyTitle;
	BString		emptySubtitle;
	BButton*	button;
	BMessage*	emptyMessage;

	Data()
		:
		button(NULL),
		emptyMessage(NULL)
	{
	}

	~Data()
	{
		delete emptyMessage;
	}
};


static rgb_color
quiet_text()
{
	return mix_color(ui_color(B_DOCUMENT_BACKGROUND_COLOR),
		ui_color(B_DOCUMENT_TEXT_COLOR), 140);
}


static rgb_color
card_border()
{
	return tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DARKEN_2_TINT);
}


static BFont
small_font()
{
	BFont font(be_plain_font);
	font.SetSize(be_plain_font->Size() * 0.88f);
	return font;
}


BCardStrip::BCardStrip(const char* name, BMessage* selectionMessage)
	:
	BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE | B_FRAME_EVENTS
		| B_NAVIGABLE),
	BInvoker(selectionMessage, NULL),
	fSelected(-1),
	fScroll(0),
	fData(NULL)
{
	_Init();
}


BCardStrip::BCardStrip(BMessage* archive)
	:
	BView(archive),
	fSelected(-1),
	fScroll(0),
	fData(NULL)
{
	_Init();
	if (archive == NULL)
		return;
	BMessage message;
	if (archive->FindMessage("_msg", &message) == B_OK)
		SetMessage(new BMessage(message));
	BMessage itemArchive;
	for (int32 i = 0; archive->FindMessage("r5:item", i, &itemArchive) == B_OK;
			i++) {
		BArchivable* object = BCardStripItem::Instantiate(&itemArchive);
		BCardStripItem* item = dynamic_cast<BCardStripItem*>(object);
		if (item == NULL || !AddItem(item))
			delete object;
	}
	const char* title = NULL;
	const char* subtitle = NULL;
	archive->FindString("r5:empty_title", &title);
	archive->FindString("r5:empty_subtitle", &subtitle);
	SetEmptyText(title, subtitle);
	const char* label;
	if (archive->FindString("r5:empty_label", &label) == B_OK
		&& archive->FindMessage("r5:empty_msg", &message) == B_OK)
		SetEmptyAction(label, new BMessage(message));
	int32 selected;
	if (archive->FindInt32("r5:selected", &selected) == B_OK)
		Select(selected);
}


BCardStrip::~BCardStrip()
{
	MakeEmpty();
	delete fData;
}


BArchivable*
BCardStrip::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, kCardStripClassName))
		return NULL;
	return new(std::nothrow) BCardStrip(archive);
}


status_t
BCardStrip::Archive(BMessage* archive, bool deep) const
{
	// The button of the empty card is built again from r5:empty_label.
	BButton* button = fData != NULL ? fData->button : NULL;
	if (button != NULL)
		const_cast<BCardStrip*>(this)->RemoveChild(button);
	status_t status = BView::Archive(archive, deep);
	if (button != NULL)
		const_cast<BCardStrip*>(this)->AddChild(button);
	if (status != B_OK)
		return status;
	if (archive->ReplaceString("class", kCardStripClassName) != B_OK)
		status = archive->AddString("class", kCardStripClassName);
	if (status == B_OK && Message() != NULL)
		status = archive->AddMessage("_msg", Message());
	for (int32 i = 0; status == B_OK && deep && i < CountItems(); i++) {
		BMessage item;
		status = ItemAt(i)->Archive(&item, true);
		if (status == B_OK)
			status = archive->AddMessage("r5:item", &item);
	}
	if (status == B_OK)
		status = archive->AddInt32("r5:selected", fSelected);
	if (status == B_OK && fData != NULL) {
		status = archive->AddString("r5:empty_title", fData->emptyTitle);
		if (status == B_OK) {
			status = archive->AddString("r5:empty_subtitle",
				fData->emptySubtitle);
		}
		if (status == B_OK && fData->button != NULL
			&& fData->emptyMessage != NULL) {
			status = archive->AddString("r5:empty_label",
				fData->button->Label());
			if (status == B_OK) {
				status = archive->AddMessage("r5:empty_msg",
					fData->emptyMessage);
			}
		}
	}
	return status;
}


bool
BCardStrip::AddItem(BCardStripItem* item)
{
	return AddItem(item, CountItems());
}


bool
BCardStrip::AddItem(BCardStripItem* item, int32 index)
{
	if (item == NULL || item->fOwner != NULL || !fItems.AddItem(item, index))
		return false;
	item->fOwner = this;
	if (fSelected >= index)
		fSelected++;
	_LayoutButton();
	InvalidateLayout();
	Invalidate();
	return true;
}


BCardStripItem*
BCardStrip::RemoveItem(int32 index)
{
	BCardStripItem* item = (BCardStripItem*)fItems.RemoveItem(index);
	if (item == NULL)
		return NULL;
	item->fOwner = NULL;
	if (fSelected == index)
		fSelected = -1;
	else if (fSelected > index)
		fSelected--;
	_ScrollTo(fScroll);
	_LayoutButton();
	InvalidateLayout();
	Invalidate();
	return item;
}


void
BCardStrip::MakeEmpty()
{
	for (int32 i = 0; i < fItems.CountItems(); i++) {
		BCardStripItem* item = (BCardStripItem*)fItems.ItemAt(i);
		item->fOwner = NULL;
		delete item;
	}
	fItems.MakeEmpty();
	fSelected = -1;
	fScroll = 0;
	_LayoutButton();
	InvalidateLayout();
	Invalidate();
}


int32
BCardStrip::CountItems() const
{
	return fItems.CountItems();
}


BCardStripItem*
BCardStrip::ItemAt(int32 index) const
{
	return (BCardStripItem*)fItems.ItemAt(index);
}


int32
BCardStrip::IndexOf(const BCardStripItem* item) const
{
	return fItems.IndexOf((void*)item);
}


void
BCardStrip::InvalidateItem(int32 index)
{
	if (index < 0 || index >= CountItems())
		return;
	Invalidate(_CardFrame(index).InsetByCopy(-2, -2));
}


void
BCardStrip::Select(int32 index)
{
	if (index < -1 || index >= CountItems())
		index = -1;
	if (index == fSelected)
		return;
	fSelected = index;
	ScrollToSelection();
	Invalidate();
}


int32
BCardStrip::CurrentSelection() const
{
	return fSelected;
}


void
BCardStrip::ScrollToSelection()
{
	if (fSelected < 0)
		return;
	float left = fSelected * (_CardWidth() + _Gap());
	float visible = Bounds().Width() - 2 * _Padding();
	float offset = fScroll;
	if (left < offset)
		offset = left;
	else if (left + _CardWidth() > offset + visible)
		offset = left + _CardWidth() - visible;
	_ScrollTo(offset);
}


BSize
BCardStrip::PictureSize() const
{
	float u = _Unit();
	return BSize(roundf(56 * u), roundf(38 * u));
}


void
BCardStrip::SetEmptyText(const char* title, const char* subtitle)
{
	if (fData == NULL)
		return;
	fData->emptyTitle = title;
	fData->emptySubtitle = subtitle;
	if (CountItems() == 0)
		Invalidate();
}


void
BCardStrip::SetEmptyAction(const char* label, BMessage* message)
{
	if (fData == NULL) {
		delete message;
		return;
	}
	delete fData->emptyMessage;
	fData->emptyMessage = NULL;
	if (fData->button != NULL) {
		fData->button->RemoveSelf();
		delete fData->button;
		fData->button = NULL;
	}
	if (label == NULL) {
		delete message;
		InvalidateLayout();
		Invalidate();
		return;
	}
	fData->emptyMessage = message;
	fData->button = new(std::nothrow) BButton(BRect(0, 0, 10, 10),
		"empty action", label, new BMessage(kMsgEmptyAction),
		B_FOLLOW_NONE);
	if (fData->button != NULL) {
		fData->button->ResizeToPreferred();
		AddChild(fData->button);
		if (Window() != NULL)
			fData->button->SetTarget(this);
	}
	_LayoutButton();
	InvalidateLayout();
	Invalidate();
}


void
BCardStrip::AttachedToWindow()
{
	BView::AttachedToWindow();
	SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	SetLowUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	if (!Messenger().IsValid())
		SetTarget(Window());
	if (fData != NULL && fData->button != NULL)
		fData->button->SetTarget(this);
	_LayoutButton();
}


void
BCardStrip::Draw(BRect updateRect)
{
	(void)updateRect;
	BRect bounds = Bounds();
	SetHighColor(ui_color(B_DOCUMENT_BACKGROUND_COLOR));
	FillRect(bounds);
	SetHighColor(card_border());
	StrokeRect(bounds);

	if (CountItems() == 0) {
		_DrawEmpty(_Inner());
		return;
	}

	PushState();
	ClipToRect(bounds.InsetByCopy(1, 1));
	for (int32 i = 0; i < CountItems(); i++) {
		BRect frame = _CardFrame(i);
		if (frame.right < bounds.left || frame.left > bounds.right)
			continue;
		_DrawCard(ItemAt(i), frame, i == fSelected);
	}

	// An end that hides cards: an arrow that scrolls there.
	float u = _Unit();
	float zone = roundf(16 * u);
	BRect inner = bounds.InsetByCopy(1, 1);
	for (int32 side = 0; side < 2; side++) {
		bool show = side == 0 ? fScroll > 0.5f : fScroll < _MaxScroll() - 0.5f;
		if (!show)
			continue;
		BRect end = inner;
		if (side == 0)
			end.right = end.left + zone;
		else
			end.left = end.right - zone;
		SetHighColor(ui_color(B_DOCUMENT_BACKGROUND_COLOR));
		FillRect(end);
		SetHighColor(ui_color(B_DOCUMENT_TEXT_COLOR));
		SetPenSize(std::max(1.0f, 1.5f * u));
		float cx = (end.left + end.right) / 2;
		float cy = (end.top + end.bottom) / 2;
		float arm = 4 * u;
		float dir = side == 0 ? 1 : -1;
		StrokeLine(BPoint(cx + dir * arm / 2, cy - arm),
			BPoint(cx - dir * arm / 2, cy));
		StrokeLine(BPoint(cx - dir * arm / 2, cy),
			BPoint(cx + dir * arm / 2, cy + arm));
		SetPenSize(1);
	}
	PopState();
}


void
BCardStrip::FrameResized(float width, float height)
{
	BView::FrameResized(width, height);
	_ScrollTo(fScroll);
	ScrollToSelection();
	_LayoutButton();
	Invalidate();
}


void
BCardStrip::KeyDown(const char* bytes, int32 numBytes)
{
	if (numBytes == 1 && CountItems() > 0) {
		int32 index = fSelected;
		switch (bytes[0]) {
			case B_LEFT_ARROW:
				index = std::max((int32)0, fSelected - 1);
				break;
			case B_RIGHT_ARROW:
				index = std::min(CountItems() - 1, fSelected + 1);
				break;
			case B_HOME:
				index = 0;
				break;
			case B_END:
				index = CountItems() - 1;
				break;
			default:
				BView::KeyDown(bytes, numBytes);
				return;
		}
		_SelectByUser(index);
		return;
	}
	BView::KeyDown(bytes, numBytes);
}


void
BCardStrip::MakeFocus(bool focus)
{
	if (focus == IsFocus())
		return;
	BView::MakeFocus(focus);
	Invalidate();
}


void
BCardStrip::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgEmptyAction:
			if (fData != NULL && fData->emptyMessage != NULL) {
				BMessage copy(*fData->emptyMessage);
				Invoke(&copy);
			}
			break;

		case B_MOUSE_WHEEL_CHANGED:
		{
			float dx = 0;
			float dy = 0;
			message->FindFloat("be:wheel_delta_x", &dx);
			message->FindFloat("be:wheel_delta_y", &dy);
			float delta = dx != 0 ? dx : dy;
			if (delta != 0 && _MaxScroll() > 0)
				_ScrollTo(fScroll + delta * (_CardWidth() + _Gap()) / 2);
			else
				BView::MessageReceived(message);
			break;
		}

		case B_COLORS_UPDATED:
			Invalidate();
			BView::MessageReceived(message);
			break;

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
BCardStrip::MouseDown(BPoint where)
{
	if (CountItems() == 0) {
		BView::MouseDown(where);
		return;
	}
	MakeFocus(true);
	float zone = roundf(16 * _Unit());
	BRect bounds = Bounds();
	if (fScroll > 0.5f && where.x < bounds.left + 1 + zone) {
		_ScrollTo(fScroll - (_CardWidth() + _Gap()));
		return;
	}
	if (fScroll < _MaxScroll() - 0.5f && where.x > bounds.right - 1 - zone) {
		_ScrollTo(fScroll + _CardWidth() + _Gap());
		return;
	}
	for (int32 i = 0; i < CountItems(); i++) {
		if (_CardFrame(i).Contains(where)) {
			_SelectByUser(i);
			return;
		}
	}
}


BSize
BCardStrip::MinSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMinSize(),
		BSize(_CardWidth() + 2 * _Padding(),
			_CardHeight() + 2 * _Padding()));
}


BSize
BCardStrip::MaxSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMaxSize(),
		BSize(B_SIZE_UNLIMITED, _CardHeight() + 2 * _Padding()));
}


BSize
BCardStrip::PreferredSize()
{
	float width = std::max(_CardWidth(), _ContentWidth()) + 2 * _Padding();
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(),
		BSize(width, _CardHeight() + 2 * _Padding()));
}


void
BCardStrip::_Init()
{
	fData = new(std::nothrow) Data;
	for (int32 index = 0; index < 4; index++)
		_reserved[index] = 0;
	SetViewUIColor(B_DOCUMENT_BACKGROUND_COLOR);
	SetLowUIColor(B_DOCUMENT_BACKGROUND_COLOR);
}


float
BCardStrip::_Unit() const
{
	return be_plain_font->Size() / 13.0f;
}


float
BCardStrip::_Padding() const
{
	return roundf(8 * _Unit());
}


float
BCardStrip::_CardWidth() const
{
	return roundf(182 * _Unit());
}


float
BCardStrip::_Gap() const
{
	return roundf(8 * _Unit());
}


float
BCardStrip::_CardHeight() const
{
	float u = _Unit();
	font_height bold;
	be_bold_font->GetHeight(&bold);
	font_height small;
	small_font().GetHeight(&small);
	float text = ceilf(bold.ascent + bold.descent) + roundf(3 * u)
		+ ceilf(small.ascent + small.descent);
	float height = std::max(PictureSize().height, text) + 2 * roundf(5 * u);
	// The empty card holds its button.
	if (fData != NULL && fData->button != NULL) {
		float buttonWidth;
		float buttonHeight;
		fData->button->GetPreferredSize(&buttonWidth, &buttonHeight);
		height = std::max(height, buttonHeight + 2 * roundf(6 * u));
	}
	return height;
}


float
BCardStrip::_ContentWidth() const
{
	int32 count = CountItems();
	if (count == 0)
		return 0;
	return count * _CardWidth() + (count - 1) * _Gap();
}


float
BCardStrip::_MaxScroll() const
{
	return std::max(0.0f,
		_ContentWidth() - (Bounds().Width() - 2 * _Padding()));
}


BRect
BCardStrip::_CardFrame(int32 index) const
{
	BRect bounds = Bounds();
	float left = roundf(bounds.left + _Padding()
		+ index * (_CardWidth() + _Gap()) - fScroll);
	float top = bounds.top + _Padding();
	return BRect(left, top, left + _CardWidth() - 1,
		top + _CardHeight() - 1);
}


BRect
BCardStrip::_Inner() const
{
	return Bounds().InsetByCopy(_Padding(), _Padding());
}


void
BCardStrip::_ScrollTo(float offset)
{
	offset = std::max(0.0f, std::min(offset, _MaxScroll()));
	if (offset == fScroll)
		return;
	fScroll = offset;
	Invalidate();
}


void
BCardStrip::_SelectByUser(int32 index)
{
	if (index < 0 || index >= CountItems())
		return;
	bool changed = index != fSelected;
	fSelected = index;
	ScrollToSelection();
	Invalidate();
	if (changed && Message() != NULL) {
		BMessage message(*Message());
		message.AddInt32("index", index);
		Invoke(&message);
	}
}


void
BCardStrip::_LayoutButton()
{
	if (fData == NULL || fData->button == NULL)
		return;
	BButton* button = fData->button;
	bool show = CountItems() == 0;
	if (show && button->IsHidden(this))
		button->Show();
	else if (!show && !button->IsHidden(this))
		button->Hide();
	button->ResizeToPreferred();
	BRect inner = _Inner();
	BRect frame = button->Frame();
	button->MoveTo(roundf(inner.right - roundf(14 * _Unit()) - frame.Width()),
		roundf((inner.top + inner.bottom - frame.Height()) / 2));
}


void
BCardStrip::_DrawCard(BCardStripItem* item, BRect frame, bool selected)
{
	float u = _Unit();
	float radius = be_plain_font->Size() * 0.25f;
	rgb_color base = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
	rgb_color accent = ui_color(B_STATUS_BAR_COLOR);
	if (selected)
		base = mix_color(base, accent, 26);
	SetHighColor(base);
	FillRoundRect(frame, radius, radius);
	if (selected) {
		SetHighColor(IsFocus() ? ui_color(B_KEYBOARD_NAVIGATION_COLOR)
			: accent);
		SetPenSize(2);
		StrokeRoundRect(frame.InsetByCopy(0.5f, 0.5f), radius, radius);
		SetPenSize(1);
	} else {
		SetHighColor(card_border());
		StrokeRoundRect(frame, radius, radius);
	}

	float pad = roundf(6 * u);
	BSize pictureSize = PictureSize();
	BRect picture;
	picture.left = frame.left + pad;
	picture.right = picture.left + pictureSize.width;
	picture.top = roundf((frame.top + frame.bottom - pictureSize.height) / 2);
	picture.bottom = picture.top + pictureSize.height;
	const BBitmap* bitmap = item->Picture();
	if (bitmap != NULL) {
		// Kept in shape, not made larger, centred in the picture area.
		BRect source = bitmap->Bounds();
		float scale = std::min(
			picture.Width() / std::max(1.0f, source.Width()),
			picture.Height() / std::max(1.0f, source.Height()));
		scale = std::min(scale, 1.0f);
		float width = source.Width() * scale;
		float height = source.Height() * scale;
		BRect dest(0, 0, width, height);
		dest.OffsetTo(roundf(picture.left + (picture.Width() - width) / 2),
			roundf(picture.top + (picture.Height() - height) / 2));
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		DrawBitmap(bitmap, source, dest, B_FILTER_BITMAP_BILINEAR);
		SetDrawingMode(B_OP_COPY);
	}

	float textLeft = picture.right + roundf(10 * u);
	float textWidth = frame.right - pad - textLeft;
	font_height bold;
	be_bold_font->GetHeight(&bold);
	BFont small = small_font();
	font_height smallHeight;
	small.GetHeight(&smallHeight);
	float textHeight = ceilf(bold.ascent + bold.descent) + roundf(3 * u)
		+ ceilf(smallHeight.ascent + smallHeight.descent);
	float y = roundf((frame.top + frame.bottom - textHeight) / 2)
		+ ceilf(bold.ascent);
	SetLowColor(base);
	SetFont(be_bold_font);
	SetHighColor(ui_color(B_DOCUMENT_TEXT_COLOR));
	BString title(item->Title());
	be_bold_font->TruncateString(&title, B_TRUNCATE_END, textWidth);
	DrawString(title.String(), BPoint(textLeft, y));
	y += ceilf(bold.descent) + roundf(3 * u) + ceilf(smallHeight.ascent);
	SetFont(&small);
	SetHighColor(quiet_text());
	BString subtitle(item->Subtitle());
	small.TruncateString(&subtitle, B_TRUNCATE_END, textWidth);
	DrawString(subtitle.String(), BPoint(textLeft, y));
	SetLowColor(ui_color(B_DOCUMENT_BACKGROUND_COLOR));
	SetFont(be_plain_font);
}


void
BCardStrip::_DrawEmpty(BRect frame)
{
	float u = _Unit();
	// A dashed outline: the place where a card comes.
	SetHighColor(card_border());
	float dash = roundf(6 * u);
	float space = roundf(4 * u);
	for (float x = frame.left; x < frame.right; x += dash + space) {
		float end = std::min(frame.right, x + dash);
		StrokeLine(BPoint(x, frame.top), BPoint(end, frame.top));
		StrokeLine(BPoint(x, frame.bottom), BPoint(end, frame.bottom));
	}
	for (float y = frame.top; y < frame.bottom; y += dash + space) {
		float end = std::min(frame.bottom, y + dash);
		StrokeLine(BPoint(frame.left, y), BPoint(frame.left, end));
		StrokeLine(BPoint(frame.right, y), BPoint(frame.right, end));
	}
	if (fData == NULL)
		return;

	float left = frame.left + roundf(16 * u);
	float right = frame.right - roundf(16 * u);
	if (fData->button != NULL && !fData->button->IsHidden(this))
		right = fData->button->Frame().left - roundf(12 * u);
	float width = std::max(0.0f, right - left);
	font_height bold;
	be_bold_font->GetHeight(&bold);
	BFont small = small_font();
	font_height smallHeight;
	small.GetHeight(&smallHeight);
	bool two = fData->emptySubtitle.Length() > 0;
	float textHeight = ceilf(bold.ascent + bold.descent);
	if (two) {
		textHeight += roundf(3 * u)
			+ ceilf(smallHeight.ascent + smallHeight.descent);
	}
	float y = roundf((frame.top + frame.bottom - textHeight) / 2)
		+ ceilf(bold.ascent);
	SetFont(be_bold_font);
	SetHighColor(ui_color(B_DOCUMENT_TEXT_COLOR));
	BString title(fData->emptyTitle);
	be_bold_font->TruncateString(&title, B_TRUNCATE_END, width);
	DrawString(title.String(), BPoint(left, y));
	if (two) {
		y += ceilf(bold.descent) + roundf(3 * u) + ceilf(smallHeight.ascent);
		SetFont(&small);
		SetHighColor(quiet_text());
		BString subtitle(fData->emptySubtitle);
		small.TruncateString(&subtitle, B_TRUNCATE_END, width);
		DrawString(subtitle.String(), BPoint(left, y));
	}
	SetFont(be_plain_font);
}


void BCardStrip::_ReservedCardStrip1() {}
void BCardStrip::_ReservedCardStrip2() {}
void BCardStrip::_ReservedCardStrip3() {}
void BCardStrip::_ReservedCardStrip4() {}


}	// namespace BAppearanceKit
