/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * Performance page. The switch for the shared-memory link between programs
 * and app_server (docs/design/APP-SERVER-RING.md). It writes
 * "app_server_ring on|off" into kernel/drivers/performance, the file
 * app_server reads when a program connects. Later speed switches belong on
 * this page too.
 */


#include "SettingsFont.h"
#include "SettingsMessages.h"
#include "SettingsModule.h"

#include <AdminLock.h>
#include <Box.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <String.h>
#include <StringView.h>
#include <TextView.h>

#include <math.h>
#include <string.h>

#if __has_include(<r5_os_version.h>)
#	include <r5_os_version.h>
#endif


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Settings: Performance"


static const uint32 kMsgRing = 'pfRg';
static const char* kFileName = "performance";
static const char* kRingKey = "app_server_ring";


// On in a -dev build (owner, 26 Nov 2026); app_server uses the same rule.
static bool
factory_ring()
{
#ifdef R5_OS_VERSION
	// Through a volatile pointer: GCC folds the constant and warns.
	const char* volatile version = R5_OS_VERSION;
	return strstr(version, "-dev") != NULL;
#else
	return true;
#endif
}


class PerformanceModule;


class PerformanceView : public BGroupView {
public:
								PerformanceView(PerformanceModule* module);
	virtual						~PerformanceView();
	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);

			void				ApplyState(bool ring, const char* notice);

private:
			PerformanceModule*	fModule;
			bool				fUpdating;
			BCheckBox*			fRing;
			BStringView*		fNotice;
};


class PerformanceModule : public SettingsModule {
public:
								PerformanceModule();

	virtual	const char*			Identifier() const { return "performance"; }
	virtual	const char*			Name() const
									{ return B_TRANSLATE("Performance"); }
	virtual	const char*			Category() const { return "system"; }
	virtual	const char*			Description() const;
	virtual	const char*			Keywords() const;
	virtual	bool				RequiresAdmin() const { return true; }
	virtual	BView*				CreateView(SettingsContext* context,
									BRect frame);
	virtual	status_t			LoadSettings(SettingsContext* context);
	virtual	status_t			SaveSettings(SettingsContext* context);
	virtual	status_t			RevertSettings(SettingsContext* context);
	virtual	bool				IsModified() const
									{ return fCurrent != fOpened; }
	virtual	bool				CanApplyDefaults() const
									{ return fCurrent != fFactory; }
	virtual	status_t			ApplyDefaults(SettingsContext* context);

			bool				Current() const { return fCurrent; }
			const char*			Notice() const { return fNotice.String(); }
			void				ClearView(PerformanceView* view);
			void				UserEdited(bool ring);

private:
			void				_Push();

			PerformanceView*	fView;
			SettingsContext*	fContext;
			bool				fCurrent;
			bool				fOpened;
			bool				fFactory;
			BString				fNotice;
};


PerformanceView::PerformanceView(PerformanceModule* module)
	:
	BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING),
	fModule(module),
	fUpdating(false)
{
	fRing = new BCheckBox("ring", B_TRANSLATE("Fast drawing link"),
		new BMessage(kMsgRing));
	fRing->SetFont(r5_settings_bold_font());

	BTextView* text = new BTextView("ringText");
	text->SetText(B_TRANSLATE("Programs hand their drawing to the screen "
		"through shared memory instead of one message at a time. That "
		"takes less work for every window. Programs started after a change "
		"use the new setting. Safe mode always uses messages."));
	text->MakeEditable(false);
	text->MakeSelectable(false);
	text->SetWordWrap(true);
	text->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	text->SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
	text->SetFontAndColor(r5_settings_plain_font(), B_FONT_ALL);
	// A BTextView does not ask for the height of its wrapped text in a
	// layout (PowerModule.cpp): room for four lines, no more, so the text
	// stays under the box title.
	font_height fontHeight;
	r5_settings_plain_font()->GetHeight(&fontHeight);
	float lines = 4 * ceilf(fontHeight.ascent + fontHeight.descent
		+ fontHeight.leading);
	text->SetExplicitMinSize(BSize(200, lines));
	text->SetExplicitPreferredSize(BSize(B_SIZE_UNSET, lines));
	text->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, lines));

	BBox* box = new BBox("drawing");
	box->SetLabel(fRing);
	BGroupView* inner = new BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING);
	BLayoutBuilder::Group<>(inner)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(text);
	box->AddChild(inner);

	fNotice = new BStringView("notice", "");
	fNotice->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fNotice->Hide();

	BLayoutBuilder::Group<>(this)
		.Add(fNotice)
		.Add(box)
		.AddGlue();

	if (fModule != NULL)
		ApplyState(fModule->Current(), fModule->Notice());
}


PerformanceView::~PerformanceView()
{
	if (fModule != NULL)
		fModule->ClearView(this);
}


void
PerformanceView::AttachedToWindow()
{
	BGroupView::AttachedToWindow();
	fRing->SetTarget(this);
}


void
PerformanceView::ApplyState(bool ring, const char* notice)
{
	fUpdating = true;
	fRing->SetValue(ring ? B_CONTROL_ON : B_CONTROL_OFF);
	fUpdating = false;

	fNotice->SetText(notice);
	if (notice[0] != '\0' && fNotice->IsHidden(fNotice))
		fNotice->Show();
	else if (notice[0] == '\0' && !fNotice->IsHidden(fNotice))
		fNotice->Hide();
}


void
PerformanceView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgRing:
			if (!fUpdating && fModule != NULL)
				fModule->UserEdited(fRing->Value() == B_CONTROL_ON);
			break;
		case kMsgRefreshPage:
			if (fModule != NULL)
				ApplyState(fModule->Current(), fModule->Notice());
			break;
		default:
			BGroupView::MessageReceived(message);
			break;
	}
}


PerformanceModule::PerformanceModule()
	:
	fView(NULL),
	fContext(NULL),
	fCurrent(factory_ring()),
	fOpened(fCurrent),
	fFactory(fCurrent)
{
}


const char*
PerformanceModule::Description() const
{
	return B_TRANSLATE("Speed options for the desktop");
}


const char*
PerformanceModule::Keywords() const
{
	return B_TRANSLATE_COMMENT("performance speed fast drawing shared memory "
		"app_server",
		"Search words, separated by spaces. Keep the English words and add "
		"words in your language.");
}


BView*
PerformanceModule::CreateView(SettingsContext* context, BRect frame)
{
	(void)frame;
	fContext = context;
	fView = new PerformanceView(this);
	return fView;
}


void
PerformanceModule::ClearView(PerformanceView* view)
{
	if (fView == view)
		fView = NULL;
}


void
PerformanceModule::_Push()
{
	if (fView != NULL)
		fView->ApplyState(fCurrent, fNotice.String());
}


void
PerformanceModule::UserEdited(bool ring)
{
	fCurrent = ring;
	_Push();
	if (fContext != NULL)
		fContext->NotifyChanged(Identifier());
}


status_t
PerformanceModule::LoadSettings(SettingsContext* context)
{
	fContext = context;
	fFactory = factory_ring();
	fCurrent = fFactory;
	BString value;
	if (r5_driver_settings_read(kFileName, kRingKey, value) == B_OK) {
		if (value == "off" || value == "false")
			fCurrent = false;
		else if (value == "on" || value == "true")
			fCurrent = true;
	}
	fOpened = fCurrent;
	_Push();
	return B_OK;
}


status_t
PerformanceModule::SaveSettings(SettingsContext* context)
{
	fContext = context;
	if (context == NULL || !context->AdminChangeAllowed())
		return B_PERMISSION_DENIED;

	const char* keys[] = { kRingKey };
	BString line;
	line << kRingKey << (fCurrent ? " on\n" : " off\n");
	BString error;
	status_t status;
	if (r5_driver_settings_writable()) {
		status = r5_driver_settings_write(kFileName, keys, 1, line.String(),
			false);
		if (status != B_OK)
			error = strerror(status);
	} else {
		status = r5_admin_set_driver_setting(kFileName, keys, 1,
			line.String(), false, error);
	}

	fNotice = status == B_OK ? "" : error.String();
	_Push();
	return status;
}


status_t
PerformanceModule::RevertSettings(SettingsContext* context)
{
	fCurrent = fOpened;
	_Push();
	return SaveSettings(context);
}


status_t
PerformanceModule::ApplyDefaults(SettingsContext* context)
{
	fCurrent = fFactory;
	_Push();
	return SaveSettings(context);
}


// The add-on settings_performance (distro/settings/ModuleAPI.md): every
// Settings page is an add-on of its own.
extern "C" _EXPORT int32
settings_api_version()
{
	return kSettingsApiVersion;
}


extern "C" _EXPORT SettingsModule*
instantiate_settings_module()
{
	return new PerformanceModule();
}
