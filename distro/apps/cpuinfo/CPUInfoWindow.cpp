/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#include "CPUInfoWindow.h"

#include <Application.h>
#include <Bitmap.h>
#include <Button.h>
#include <Card.h>
#include <Clipboard.h>
#include <ControlLook.h>
#include <Font.h>
#include <GridLayout.h>
#include <GridView.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <PageHeader.h>
#include <R5Icons.h>
#include <StringView.h>
#include <TextView.h>
#include <WindowEffects.h>

#include <string.h>


static const uint32 kMsgCopyReport = 'cpcp';


// A wrapping line reports one line of height until it has a width. After
// the layout has given it that width, the window is sized from the real
// height so the bottom card is not cut off.
static void
fit_wrapped(BView* view)
{
	BTextView* text = dynamic_cast<BTextView*>(view);
	if (text != NULL) {
		float width = text->Frame().Width();
		float left = 0.0f;
		float top = 0.0f;
		float right = 0.0f;
		float bottom = 0.0f;
		text->GetInsets(&left, &top, &right, &bottom);
		float textWidth = width - left - right;
		if (textWidth > 1.0f) {
			text->SetTextRect(BRect(0, 0, textWidth - 1.0f, 1000000.0f));
			int32 lines = text->CountLines();
			if (lines < 1)
				lines = 1;
			float height = text->TextHeight(0, lines - 1) + top + bottom;
			float line = text->LineHeight();
			if (height < line)
				height = line;
			text->SetExplicitMinSize(BSize(B_SIZE_UNSET, height));
			text->SetExplicitPreferredSize(BSize(B_SIZE_UNSET, height));
		}
	}
	int32 count = view->CountChildren();
	for (int32 i = 0; i < count; i++)
		fit_wrapped(view->ChildAt(i));
}


static void
apply_glass_children(BView* parent)
{
	for (int32 index = 0; index < parent->CountChildren(); index++) {
		BView* view = parent->ChildAt(index);
		if (view == NULL)
			continue;
		view->SetFlags(view->Flags() | B_TRANSPARENT_BACKGROUND);
		view->SetViewColor(B_TRANSPARENT_COLOR);
		view->SetLowColor(blend_color(B_TRANSPARENT_COLOR,
			view->HighColor(), 192));
		view->SetDrawingMode(B_OP_ALPHA);
		view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
		apply_glass_children(view);
	}
}


class GlassSectionCard : public BAppearanceKit::BCard {
public:
								GlassSectionCard(const char* name,
									const char* title);
	virtual	void				AttachedToWindow();
	virtual	void				AllAttached();
	virtual	void				Draw(BRect updateRect);
	virtual	void				MessageReceived(BMessage* message);

private:
			void				_UpdateColors();
			BStringView*		fTitleView;
};


GlassSectionCard::GlassSectionCard(const char* name, const char* title)
	:
	BCard(name, BAppearanceKit::B_CARD_PANEL),
	fTitleView(NULL)
{
	fTitleView = new BStringView("title", title);
	fTitleView->SetFont(be_bold_font);
	fTitleView->SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
	fTitleView->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_TOP));

	BLayoutBuilder::Group<>((BGroupLayout*)GetLayout())
		.Add(fTitleView);
}


void
GlassSectionCard::AttachedToWindow()
{
	BCard::AttachedToWindow();
	BAppearanceKit::set_view_glass(this, true);
}


void
GlassSectionCard::AllAttached()
{
	BCard::AllAttached();
	// BStringView::AttachedToWindow() copies the parent's view colour after
	// a parent AttachedToWindow() has run, which put an opaque bar behind
	// every label. AllAttached runs after that.
	_UpdateColors();
}


void
GlassSectionCard::Draw(BRect updateRect)
{
	(void)updateRect;
	// Match About R5's SysInfoView: half-transparent face over the frosted
	// glass in GPU mode; in CPU mode the panel background is erased first.
	BRect bounds = Bounds();
	float radius = be_plain_font->Size() * 0.25f;
	rgb_color face = tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		B_DARKEN_1_TINT);
	rgb_color border = tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		B_DARKEN_2_TINT);
	face.alpha = 110;
	border.alpha = 170;
	PushState();
	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
	SetHighColor(face);
	FillRoundRect(bounds, radius, radius);
	SetHighColor(border);
	StrokeRoundRect(bounds, radius, radius);
	PopState();
}


void
GlassSectionCard::MessageReceived(BMessage* message)
{
	if (message->what == B_COLORS_UPDATED) {
		_UpdateColors();
		Invalidate();
	}
	BCard::MessageReceived(message);
}


void
GlassSectionCard::_UpdateColors()
{
	rgb_color textColor = ui_color(B_PANEL_TEXT_COLOR);
	rgb_color labelColor = mix_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		textColor, 170);
	if (fTitleView != NULL)
		fTitleView->SetHighColor(textColor);

	for (int32 i = 0; i < CountChildren(); i++) {
		BView* child = ChildAt(i);
		if (child == NULL)
			continue;
		for (int32 j = 0; j < child->CountChildren(); j++) {
			BView* item = child->ChildAt(j);
			if (item == NULL)
				continue;
			if (strcmp(item->Name(), "label") == 0) {
				item->SetHighColor(labelColor);
			} else if (strcmp(item->Name(), "value") == 0) {
				item->SetHighColor(textColor);
				BTextView* tv = dynamic_cast<BTextView*>(item);
				if (tv != NULL)
					tv->SetFontAndColor(be_plain_font, B_FONT_ALL, &textColor);
			}
		}
	}
	apply_glass_children(this);
}


static void
add_row(BGridLayout* grid, int32 row, const char* label, const char* value,
	bool wrap = false)
{
	BStringView* labelView = new BStringView("label", label);
	labelView->SetHighColor(mix_color(ui_color(B_PANEL_BACKGROUND_COLOR),
		ui_color(B_PANEL_TEXT_COLOR), 170));
	labelView->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_TOP));

	grid->AddView(labelView, 0, row);

	if (wrap) {
		BTextView* textView = new BTextView("value");
		textView->SetText(value);
		textView->MakeEditable(false);
		textView->MakeSelectable(true);
		textView->SetWordWrap(true);
		textView->SetStylable(false);
		textView->SetFlags(textView->Flags() | B_TRANSPARENT_BACKGROUND);
		textView->SetViewColor(B_TRANSPARENT_COLOR);
		rgb_color textColor = ui_color(B_PANEL_TEXT_COLOR);
		textView->SetHighColor(textColor);
		textView->SetLowColor(blend_color(B_TRANSPARENT_COLOR,
			textColor, 192));
		textView->SetDrawingMode(B_OP_ALPHA);
		textView->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
		textView->SetFontAndColor(be_plain_font, B_FONT_ALL, &textColor);
		textView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
		grid->AddView(textView, 1, row);
	} else {
		BStringView* valueView = new BStringView("value", value);
		valueView->SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
		valueView->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_TOP));
		valueView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
		grid->AddView(valueView, 1, row);
	}
}


CPUInfoWindow::CPUInfoWindow()
	:
	BWindow(BRect(0, 0, 640, 720), "CPU Info", B_TITLED_WINDOW,
		B_AUTO_UPDATE_SIZE_LIMITS)
{
	CollectCpuReport(fReport);
	BAppearanceKit::set_window_glass(this, true);

	BAppearanceKit::BPageHeader* header = new BAppearanceKit::BPageHeader(
		"header");
	header->SetEyebrow("System");
	header->SetTitle("CPU Info");
	header->SetSubtitle("Processor hardware and R5 runtime paths");

	float iconSize = be_control_look != NULL
		? be_control_look->ComposeIconSize(40).Width() : 40.0f;
	BBitmap* icon = r5_mime_icon_bitmap(
		"application/x-vnd.Haiku-ActivityMonitor", iconSize);
	if (icon != NULL) {
		header->SetIcon(icon);
		delete icon;
	}

	GlassSectionCard* hwCard = new GlassSectionCard("hw", "Processor");
	BGridView* hwGrid = new BGridView(B_USE_DEFAULT_SPACING,
		B_USE_SMALL_SPACING);
	BGridLayout* hwLayout = hwGrid->GridLayout();
	hwLayout->SetColumnWeight(0, 0.0f);
	hwLayout->SetColumnWeight(1, 1.0f);
	add_row(hwLayout, 0, "Vendor", fReport.vendor.String());
	add_row(hwLayout, 1, "Brand", fReport.brand.String());
	add_row(hwLayout, 2, "Family / Model / Stepping",
		fReport.familyModelStepping.String());
	add_row(hwLayout, 3, "Cores / Threads", fReport.coresThreads.String());
	add_row(hwLayout, 4, "Caches", fReport.caches.String());
	add_row(hwLayout, 5, "x86-64 level", fReport.x86_64Level.String());
	add_row(hwLayout, 6, "Extra features", fReport.extraFeatures.String(), true);
	BLayoutBuilder::Group<>((BGroupLayout*)hwCard->GetLayout())
		.Add(hwGrid);

	GlassSectionCard* r5Card = new GlassSectionCard("r5", "Enabled features");
	BGridView* r5Grid = new BGridView(B_USE_DEFAULT_SPACING,
		B_USE_SMALL_SPACING);
	BGridLayout* r5Layout = r5Grid->GridLayout();
	r5Layout->SetColumnWeight(0, 0.0f);
	r5Layout->SetColumnWeight(1, 1.0f);
	add_row(r5Layout, 0, "Build baseline", fReport.buildBaseline.String());
	add_row(r5Layout, 1, "User-space paths", fReport.userSpacePaths.String());
	add_row(r5Layout, 2, "XCR0", fReport.xcr0.String());
	add_row(r5Layout, 3, "Kernel", fReport.kernelLine.String(), true);
	BLayoutBuilder::Group<>((BGroupLayout*)r5Card->GetLayout())
		.Add(r5Grid);

	BView* cardsContainer = new BView("cards", B_WILL_DRAW);
	cardsContainer->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	BLayoutBuilder::Group<>(cardsContainer, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.Add(hwCard)
		.Add(r5Card);

	BButton* copyButton = new BButton("copy", "Copy report",
		new BMessage(kMsgCopyReport));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(header)
		.Add(cardsContainer)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(copyButton)
		.End();

	BAppearanceKit::set_view_glass(cardsContainer, true);

	Layout(true);
	for (int32 i = 0; i < CountChildren(); i++)
		fit_wrapped(ChildAt(i));
	ResizeToPreferred();
	CenterOnScreen();
}


CPUInfoWindow::~CPUInfoWindow()
{
}


void
CPUInfoWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgCopyReport: {
			BString text = FormatCpuReport(fReport);
			BClipboard clipboard("system");
			if (clipboard.Lock()) {
				clipboard.Clear();
				BMessage* clip = clipboard.Data();
				if (clip != NULL) {
					clip->AddData("text/plain", B_MIME_TYPE, text.String(),
						text.Length());
				}
				clipboard.Commit();
				clipboard.Unlock();
			}
			break;
		}
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
CPUInfoWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}
