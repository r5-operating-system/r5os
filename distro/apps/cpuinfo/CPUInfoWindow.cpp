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
#include <ScrollView.h>
#include <StringView.h>
#include <TextView.h>
#include <WindowEffects.h>


static const uint32 kMsgCopyReport = 'cpcp';


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
		textView->SetViewColor(B_TRANSPARENT_COLOR);
		rgb_color textColor = ui_color(B_PANEL_TEXT_COLOR);
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


static BAppearanceKit::BCard*
make_section_card(const char* name, const char* title)
{
	BAppearanceKit::BCard* card = new BAppearanceKit::BCard(name,
		BAppearanceKit::B_CARD_PANEL);
	BStringView* titleView = new BStringView("title", title);
	titleView->SetFont(be_bold_font);
	titleView->SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
	titleView->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_TOP));

	BLayoutBuilder::Group<>((BGroupLayout*)card->GetLayout())
		.Add(titleView);
	return card;
}


CPUInfoWindow::CPUInfoWindow()
	:
	BWindow(BRect(0, 0, 620, 560), "CPU Info", B_TITLED_WINDOW,
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
	if (icon != NULL)
		header->SetIcon(icon);

	BAppearanceKit::BCard* hwCard = make_section_card("hw", "CPU hardware");
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

	BAppearanceKit::BCard* r5Card = make_section_card("r5", "What R5 uses");
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
	cardsContainer->SetViewColor(B_TRANSPARENT_COLOR);
	BLayoutBuilder::Group<>(cardsContainer, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.Add(hwCard)
		.Add(r5Card)
		.AddGlue();

	BScrollView* scrollView = new BScrollView("scroll", cardsContainer, 0,
		false, true, B_NO_BORDER);
	scrollView->SetViewColor(B_TRANSPARENT_COLOR);

	BButton* copyButton = new BButton("copy", "Copy report",
		new BMessage(kMsgCopyReport));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(header)
		.Add(scrollView, 1.0f)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(copyButton)
		.End();

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
