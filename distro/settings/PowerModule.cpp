/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * The Power page (docs/POWER.md, section 2; docs/design/power-mockup/):
 * Battery and Sleep on the left, Performance and Wake on the right. The
 * machine settings go to the power service as an admin request through
 * libr5power.so; the page never writes the file. Battery and brightness
 * need no lock, so the page does not require an administrator as a whole
 * (as Bluetooth): the window sends kMsgRefreshPage when the lock changes,
 * and the page grays the locked groups itself.
 */



#include "SettingsFont.h"
#include "SettingsMessages.h"
#include "SettingsModule.h"

#include <Box.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <Entry.h>
#include <GridLayout.h>
#include <GroupView.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Screen.h>
#include <Slider.h>
#include <String.h>
#include <StringFormat.h>
#include <StringView.h>
#include <TextView.h>

#include <PowerRoster.h>
#include <R5Icons.h>
#include <syscalls.h>
#include <driver_settings.h>

#include <PowerProtocol.h>

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Settings: Power"


static const uint32 kMsgEdited = 'pwEd';
static const uint32 kMsgBrightness = 'pwBr';
static const char* kLidDevice = "/dev/power/acpi_lid/0";
static const char* kDot = " \xc2\xb7 ";


// #pragma mark - helpers


static bool
safe_mode()
{
	// as launch_daemon reads it
	char buffer[32];
	size_t size = sizeof(buffer);
	if (_kern_get_safemode_option(B_SAFEMODE_SAFE_MODE, buffer, &size)
			!= B_OK)
		return false;
	return !strncasecmp(buffer, "true", size)
		|| !strncasecmp(buffer, "yes", size)
		|| !strncasecmp(buffer, "on", size)
		|| !strncasecmp(buffer, "enabled", size);
}


static bool
same_settings(const BMessage& a, const BMessage& b)
{
	static const char* const kNumbers[] = { R5_POWER_PROFILE_BATTERY,
		R5_POWER_PROFILE_AC, R5_POWER_DISPLAY_BATTERY, R5_POWER_DISPLAY_AC,
		R5_POWER_SLEEP_BATTERY, R5_POWER_SLEEP_AC, NULL };
	static const char* const kStrings[] = { R5_POWER_LID_ACTION,
		R5_POWER_BUTTON_ACTION, NULL };
	static const char* const kBools[] = { R5_POWER_EXTERNAL_AWAKE,
		R5_POWER_DIM_ON_BATTERY, R5_POWER_WAKE_LID, R5_POWER_WAKE_KEYBOARD,
		R5_POWER_WAKE_MOUSE, NULL };
	for (int32 i = 0; kNumbers[i] != NULL; i++) {
		if (a.GetInt32(kNumbers[i], -1) != b.GetInt32(kNumbers[i], -1))
			return false;
	}
	for (int32 i = 0; kStrings[i] != NULL; i++) {
		if (strcmp(a.GetString(kStrings[i], ""), b.GetString(kStrings[i], ""))
				!= 0)
			return false;
	}
	for (int32 i = 0; kBools[i] != NULL; i++) {
		if (a.GetBool(kBools[i], false) != b.GetBool(kBools[i], false))
			return false;
	}
	return true;
}


static BString
minutes_label(int32 minutes)
{
	BString label;
	if (minutes == 0)
		label = B_TRANSLATE("Never");
	else if (minutes == 60)
		label = B_TRANSLATE("1 hour");
	else if (minutes == 1)
		label = B_TRANSLATE("1 minute");
	else {
		BStringFormat format(B_TRANSLATE("{0, plural, "
			"one{# minute} other{# minutes}}"));
		format.Format(label, minutes);
	}
	return label;
}


static BString
duration(bigtime_t seconds)
{
	int32 hours = (int32)(seconds / 3600);
	int32 minutes = (int32)((seconds % 3600) / 60);
	BString text;
	if (hours > 0)
		text.SetToFormat("%" B_PRId32 " h %" B_PRId32 " min", hours, minutes);
	else
		text.SetToFormat("%" B_PRId32 " min", minutes);
	return text;
}


static BMenuField*
minutes_field(const char* name, const int32* choices, size_t count)
{
	BPopUpMenu* menu = new BPopUpMenu(name);
	for (size_t i = 0; i < count; i++) {
		BMessage* message = new BMessage(kMsgEdited);
		message->AddInt32("value", choices[i]);
		menu->AddItem(new BMenuItem(minutes_label(choices[i]).String(),
			message));
	}
	return new BMenuField(name, NULL, menu);
}


static BMenuField*
choice_field(const char* name, const char* const* labels,
	const char* const* values)
{
	BPopUpMenu* menu = new BPopUpMenu(name);
	for (int32 i = 0; labels[i] != NULL; i++) {
		BMessage* message = new BMessage(kMsgEdited);
		message->AddString("value", values[i]);
		menu->AddItem(new BMenuItem(B_TRANSLATE_NOCOLLECT(labels[i]),
			message));
	}
	return new BMenuField(name, NULL, menu);
}


static BMenuField*
profile_field(const char* name)
{
	static const char* const kLabels[] = {
		B_TRANSLATE_MARK("Power saver"),
		B_TRANSLATE_MARK("Balanced"),
		B_TRANSLATE_MARK("Performance")
	};
	BPopUpMenu* menu = new BPopUpMenu(name);
	for (int32 i = 0; i < 3; i++) {
		BMessage* message = new BMessage(kMsgEdited);
		message->AddInt32("value", i);
		menu->AddItem(new BMenuItem(B_TRANSLATE_NOCOLLECT(kLabels[i]),
			message));
	}
	return new BMenuField(name, NULL, menu);
}


static void
mark_int(BMenuField* field, int32 value)
{
	BMenu* menu = field->Menu();
	for (int32 i = 0; i < menu->CountItems(); i++) {
		BMenuItem* item = menu->ItemAt(i);
		if (item->Message() != NULL
			&& item->Message()->GetInt32("value", -1) == value) {
			item->SetMarked(true);
			return;
		}
	}
}


static void
mark_string(BMenuField* field, const char* value)
{
	BMenu* menu = field->Menu();
	for (int32 i = 0; i < menu->CountItems(); i++) {
		BMenuItem* item = menu->ItemAt(i);
		if (item->Message() != NULL
			&& strcmp(item->Message()->GetString("value", ""), value) == 0) {
			item->SetMarked(true);
			return;
		}
	}
}


static int32
marked_int(BMenuField* field, int32 fallback)
{
	BMenuItem* item = field->Menu()->FindMarked();
	if (item == NULL || item->Message() == NULL)
		return fallback;
	return item->Message()->GetInt32("value", fallback);
}


static const char*
marked_string(BMenuField* field, const char* fallback)
{
	BMenuItem* item = field->Menu()->FindMarked();
	if (item == NULL || item->Message() == NULL)
		return fallback;
	return item->Message()->GetString("value", fallback);
}


static void
set_menu_item_enabled(BMenuField* field, const char* value, bool enabled)
{
	BMenu* menu = field->Menu();
	for (int32 i = 0; i < menu->CountItems(); i++) {
		BMenuItem* item = menu->ItemAt(i);
		if (item->Message() != NULL
			&& strcmp(item->Message()->GetString("value", ""), value) == 0)
			item->SetEnabled(enabled);
	}
}


// The hint text of the Settings pages (docs/SETTINGS.md): the document
// background mixed toward the document text by 140.
static rgb_color
hint_color()
{
	return mix_color(ui_color(B_DOCUMENT_BACKGROUND_COLOR),
		ui_color(B_DOCUMENT_TEXT_COLOR), 140);
}


static void
set_label_enabled(BStringView* label, bool enabled)
{
	rgb_color text = ui_color(B_PANEL_TEXT_COLOR);
	label->SetHighColor(enabled ? text
		: mix_color(text, ui_color(B_PANEL_BACKGROUND_COLOR), 128));
	label->Invalidate();
}


static BStringView*
hint(const char* name, const char* text)
{
	BStringView* view = new BStringView(name, text);
	view->SetHighColor(hint_color());
	return view;
}


// #pragma mark - BatteryGlyph


/*!	R5_LEVEL_BATTERY from libr5icons.so, four steps, in the text color. */
class BatteryGlyph : public BView {
public:
	BatteryGlyph()
		:
		BView("battery glyph", B_WILL_DRAW),
		fLevel(0)
	{
		float height = be_bold_font->Size() * 2.6f;
		SetExplicitSize(BSize(height * 1.9f, height));
	}

	virtual void AttachedToWindow()
	{
		BView::AttachedToWindow();
		AdoptParentColors();
	}

	void SetLevel(int32 level)
	{
		if (level == fLevel)
			return;
		fLevel = level;
		Invalidate();
	}

	virtual void Draw(BRect)
	{
		r5_level_glyph_draw(this, R5_LEVEL_BATTERY, Bounds(), fLevel,
			ui_color(B_PANEL_TEXT_COLOR), ViewColor(), R5_LEVEL_STYLE_NORMAL);
	}

private:
	int32	fLevel;
};


// #pragma mark - PowerView


class PowerModule;


class PowerView : public BGroupView {
public:
								PowerView(PowerModule* module,
									SettingsContext* context);
	virtual						~PowerView();

	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();
	virtual	void				MessageReceived(BMessage* message);

			void				ShowSettings(const BMessage& settings);
			void				ShowBrightness(float brightness);

private:
			BBox*				_Box(const char* name, const char* title,
									BStringView** _title = NULL);
			void				_UpdateBattery();
			void				_UpdateProfileText();
			void				_SyncEnabled();
			void				_Edited();

			PowerModule*		fModule;
			SettingsContext*	fContext;
			BPowerRoster		fRoster;
			bool				fUpdating;
			bool				fHasBattery;
			bool				fHasLid;
			bool				fSleepWorks;
			bool				fCanChangeProfile;
			bool				fHasBrightness;

			BStringView*		fSafeNote;

			BBox*				fBatteryBox;
			BatteryGlyph*		fGlyph;
			BStringView*		fPercent;
			BStringView*		fStatus;
			BStringView*		fHealth;

			BBox*				fSleepBox;
			BStringView*		fSleepColumnBattery;
			BStringView*		fSleepColumnAC;
			BStringView*		fDisplayLabel;
			BMenuField*			fDisplayBattery;
			BMenuField*			fDisplayAC;
			BStringView*		fSleepLabel;
			BMenuField*			fSleepBattery;
			BMenuField*			fSleepAC;
			BStringView*		fLidLabel;
			BMenuField*			fLid;
			BStringView*		fButtonLabel;
			BMenuField*			fButton;
			BCheckBox*			fExternal;
			BStringView*		fSleepNote1;
			BStringView*		fSleepNote2;

			BBox*				fPerformanceBox;
			BStringView*		fProfileColumnBattery;
			BStringView*		fProfileColumnAC;
			BStringView*		fProfileLabel;
			BMenuField*			fProfileBattery;
			BMenuField*			fProfileAC;
			BTextView*			fProfileText;
			BStringView*		fNoSpeed;
			BSlider*			fBrightness;

			BBox*				fWakeBox;
			BStringView*		fWakeTitle;
			BCheckBox*			fWakeLid;
			BCheckBox*			fWakeKeyboard;
			BCheckBox*			fWakeMouse;
			BStringView*		fSleepsWith;
};


class PowerModule : public SettingsModule {
public:
								PowerModule();

	virtual	const char*			Identifier() const { return "power"; }
	virtual	const char*			Name() const
									{ return B_TRANSLATE("Power"); }
	virtual	const char*			Category() const { return "system"; }
	virtual	const char*			Description() const
									{ return B_TRANSLATE("Battery, performance and sleep"); }
	virtual	const char*			Keywords() const
									{ return B_TRANSLATE_COMMENT("battery power sleep "
										"performance profile brightness lid "
										"display energy cpu",
										"Search words, separated by spaces. Keep "
										"the English words and add words in your "
										"language."); }
	virtual	bool				RequiresAdmin() const { return false; }
	virtual	BView*				CreateView(SettingsContext* context,
									BRect frame);
	virtual	status_t			LoadSettings(SettingsContext* context);
	virtual	status_t			SaveSettings(SettingsContext* context);
	virtual	status_t			RevertSettings(SettingsContext* context);
	virtual	bool				IsModified() const;
	virtual	bool				CanApplyDefaults() const;
	virtual	status_t			ApplyDefaults(SettingsContext* context);

			bool				ServiceUp() const { return fServiceUp; }
			const BMessage&		Current() const { return fCurrent; }
			void				UserEdited(const BMessage& settings);
			void				BrightnessEdited(float brightness);
			void				SettingsChangedElsewhere();
			void				ClearView(PowerView* view);

private:
			void				_Push();

			PowerView*			fView;
			SettingsContext*	fContext;
			bool				fServiceUp;
			BMessage			fCurrent;
			BMessage			fOpened;
			BMessage			fWritten;
			BMessage			fFactory;
			float				fOpenedBrightness;
			float				fBrightness;
};


// #pragma mark - PowerView


PowerView::PowerView(PowerModule* module, SettingsContext* context)
	:
	BGroupView("power page", B_VERTICAL, B_USE_DEFAULT_SPACING),
	fModule(module),
	fContext(context),
	fUpdating(false),
	fHasBattery(false),
	fHasLid(false),
	fSleepWorks(false),
	fCanChangeProfile(false),
	fHasBrightness(false)
{
	fHasBattery = fRoster.CountBatteries() > 0;
	fHasLid = BEntry(kLidDevice).Exists();
	power_sleep_state sleepState = B_POWER_SLEEP_NONE;
	fSleepWorks = fRoster.CanSleep(&sleepState);
	fCanChangeProfile = fRoster.CanChangeProfile();
	float brightness;
	fHasBrightness = BScreen().GetBrightness(&brightness) == B_OK;

	fSafeNote = hint("safe note", safe_mode()
		? B_TRANSLATE("Power settings are off in Safe mode.")
		: B_TRANSLATE("The power service is not running."));

	// Battery
	fBatteryBox = _Box("battery", B_TRANSLATE("Battery"));
	fGlyph = new BatteryGlyph;
	fPercent = new BStringView("percent", "");
	BFont big(r5_settings_bold_font());
	big.SetSize(big.Size() * 1.5f);
	fPercent->SetFont(&big);
	fStatus = new BStringView("status", "");
	fHealth = hint("health", "");
	BGroupView* battery = new BGroupView(B_VERTICAL, B_USE_HALF_ITEM_SPACING);
	BLayoutBuilder::Group<>(battery)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(fGlyph)
			.AddGroup(B_VERTICAL, 0)
				.Add(fPercent)
				.Add(fStatus)
			.End()
			.AddGlue()
		.End()
		.Add(fHealth);
	fBatteryBox->AddChild(battery);

	// Sleep
	fSleepBox = _Box("sleep", B_TRANSLATE("Sleep"));
	fSleepColumnBattery = hint("on battery", B_TRANSLATE("On battery"));
	fSleepColumnAC = hint("plugged in", B_TRANSLATE("Plugged in"));
	fDisplayLabel = new BStringView("display label",
		B_TRANSLATE("Turn off the display after"));
	fDisplayBattery = minutes_field("display battery", kPowerDisplayChoices,
		B_COUNT_OF(kPowerDisplayChoices));
	fDisplayAC = minutes_field("display ac", kPowerDisplayChoices,
		B_COUNT_OF(kPowerDisplayChoices));
	fSleepLabel = new BStringView("sleep label",
		B_TRANSLATE("Put the computer to sleep after"));
	fSleepBattery = minutes_field("sleep battery", kPowerSleepChoices,
		B_COUNT_OF(kPowerSleepChoices));
	fSleepAC = minutes_field("sleep ac", kPowerSleepChoices,
		B_COUNT_OF(kPowerSleepChoices));

	static const char* const kLidLabels[] = { B_TRANSLATE_MARK("Sleep"),
		B_TRANSLATE_MARK("Turn off the display"),
		B_TRANSLATE_MARK("Do nothing"), NULL };
	static const char* const kLidValues[] = { "sleep", "display_off",
		"nothing", NULL };
	fLidLabel = new BStringView("lid label",
		B_TRANSLATE("When the lid is closed"));
	fLid = choice_field("lid", kLidLabels, kLidValues);
	static const char* const kButtonLabels[] = {
		B_TRANSLATE_MARK("Sleep"), B_TRANSLATE_MARK("Shut down"),
		B_TRANSLATE_MARK("Ask"), B_TRANSLATE_MARK("Do nothing"), NULL };
	static const char* const kButtonValues[] = { "sleep", "shutdown", "ask",
		"nothing", NULL };
	fButtonLabel = new BStringView("button label",
		B_TRANSLATE("When the power button is pressed"));
	fButton = choice_field("button", kButtonLabels, kButtonValues);
	fExternal = new BCheckBox("external",
		B_TRANSLATE("Keep working with an external display"),
		new BMessage(kMsgEdited));
	fSleepNote1 = hint("sleep note",
		B_TRANSLATE_COMMENT("Sleep is not available on this computer yet. "
			"Until it is,", "First of two lines, one sentence: the second line "
			"is '\"Sleep\" is gray in these menus.'"));
	fSleepNote2 = hint("sleep note 2",
		B_TRANSLATE_COMMENT("\"Sleep\" is gray in these menus.",
			"Second of two lines, one sentence with the line before"));

	BGroupView* sleep = new BGroupView(B_VERTICAL, B_USE_HALF_ITEM_SPACING);
	BGridLayout* grid;
	BLayoutBuilder::Group<>(sleep)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.AddGrid(B_USE_DEFAULT_SPACING, B_USE_HALF_ITEM_SPACING)
			.GetLayout(&grid)
			.Add(fSleepColumnBattery, 1, 0)
			.Add(fSleepColumnAC, 2, 0)
			.Add(fDisplayLabel, 0, 1)
			.Add(fDisplayBattery, 1, 1)
			.Add(fDisplayAC, 2, 1)
			.Add(fSleepLabel, 0, 2)
			.Add(fSleepBattery, 1, 2)
			.Add(fSleepAC, 2, 2)
			.Add(fLidLabel, 0, 3)
			.Add(fLid, 1, 3, 2, 1)
			.Add(fButtonLabel, 0, 4)
			.Add(fButton, 1, 4, 2, 1)
		.End()
		.Add(fExternal)
		.Add(fSleepNote1)
		.Add(fSleepNote2);
	fSleepBox->AddChild(sleep);

	// Performance
	fPerformanceBox = _Box("performance", B_TRANSLATE("Performance"));
	fProfileColumnBattery = hint("profile on battery",
		B_TRANSLATE("On battery"));
	fProfileColumnAC = hint("profile plugged in",
		B_TRANSLATE("Plugged in"));
	fProfileLabel = new BStringView("profile label",
		B_TRANSLATE("Profile"));
	fProfileBattery = profile_field("profile battery");
	fProfileAC = profile_field("profile ac");
	fProfileText = new BTextView("profile text");
	fProfileText->MakeEditable(false);
	fProfileText->MakeSelectable(false);
	fProfileText->SetWordWrap(true);
	fProfileText->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	rgb_color hintColor = hint_color();
	fProfileText->SetFontAndColor(be_plain_font, B_FONT_ALL, &hintColor);
	// A BTextView does not ask for the height of its wrapped text in a
	// layout: without a height the box under it was cut off in QEMU (the
	// brightness slider and the bottom border were gone). Room for four
	// lines, the longest explanation at the narrowest width.
	font_height fontHeight;
	be_plain_font->GetHeight(&fontHeight);
	float lines = 4 * ceilf(fontHeight.ascent + fontHeight.descent
		+ fontHeight.leading);
	fProfileText->SetExplicitMinSize(BSize(be_plain_font->StringWidth(
		"Balanced: fast when you work, slower when"), lines));
	fProfileText->SetExplicitPreferredSize(BSize(B_SIZE_UNSET, lines));
	fNoSpeed = hint("no speed",
		B_TRANSLATE("This processor does not let R5 change its speed."));
	fBrightness = new BSlider("brightness", B_TRANSLATE("Brightness"),
		new BMessage(kMsgBrightness), 0, 100, B_HORIZONTAL);
	fBrightness->SetModificationMessage(new BMessage(kMsgBrightness));

	BGroupView* performance = new BGroupView(B_VERTICAL,
		B_USE_HALF_ITEM_SPACING);
	BLayoutBuilder::Group<>(performance)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.AddGrid(B_USE_DEFAULT_SPACING, B_USE_HALF_ITEM_SPACING)
			.Add(fProfileColumnBattery, 1, 0)
			.Add(fProfileColumnAC, 2, 0)
			.Add(fProfileLabel, 0, 1)
			.Add(fProfileBattery, 1, 1)
			.Add(fProfileAC, 2, 1)
		.End()
		.Add(fProfileText)
		.Add(fNoSpeed)
		.Add(fBrightness);
	fPerformanceBox->AddChild(performance);

	// Wake
	fWakeBox = _Box("wake", B_TRANSLATE("Wake"), &fWakeTitle);
	fWakeLid = new BCheckBox("wake lid",
		B_TRANSLATE("Wake when the lid is opened"),
		new BMessage(kMsgEdited));
	fWakeKeyboard = new BCheckBox("wake keyboard",
		B_TRANSLATE("Wake with the keyboard"),
		new BMessage(kMsgEdited));
	fWakeMouse = new BCheckBox("wake mouse",
		B_TRANSLATE("Wake with the mouse or touchpad"),
		new BMessage(kMsgEdited));
	const char* sleeps = B_TRANSLATE("This computer cannot sleep.");
	if (sleepState == B_POWER_SLEEP_S0IX)
		sleeps = B_TRANSLATE("This computer sleeps with Modern Standby "
			"(S0ix).");
	else if (sleepState == B_POWER_SLEEP_S3)
		sleeps = B_TRANSLATE("This computer sleeps with Suspend to RAM "
			"(S3).");
	fSleepsWith = hint("sleeps with", sleeps);
	BGroupView* wake = new BGroupView(B_VERTICAL, B_USE_HALF_ITEM_SPACING);
	BLayoutBuilder::Group<>(wake)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(fWakeLid)
		.Add(fWakeKeyboard)
		.Add(fWakeMouse)
		.Add(fSleepsWith);
	fWakeBox->AddChild(wake);

	BLayoutBuilder::Group<>(this)
		.Add(fSafeNote)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.AddGroup(B_VERTICAL, B_USE_DEFAULT_SPACING, 3.0f)
				.Add(fBatteryBox)
				.Add(fSleepBox)
				.AddGlue()
			.End()
			.AddGroup(B_VERTICAL, B_USE_DEFAULT_SPACING, 2.0f)
				.Add(fPerformanceBox)
				.Add(fWakeBox)
				.AddGlue()
			.End()
		.End()
		.AddGlue();

	// What this computer has (docs/POWER.md, section 2)
	if (fModule == NULL || fModule->ServiceUp())
		fSafeNote->Hide();
	if (!fHasBattery) {
		// one column: the plugged-in one
		fBatteryBox->Hide();
		fSleepColumnBattery->Hide();
		fSleepColumnAC->Hide();
		fDisplayBattery->Hide();
		fSleepBattery->Hide();
		fProfileColumnBattery->Hide();
		fProfileColumnAC->Hide();
		fProfileBattery->Hide();
	}
	if (!fHasLid) {
		fLidLabel->Hide();
		fLid->Hide();
		fWakeLid->Hide();
	}
	if (fSleepWorks) {
		fSleepNote1->Hide();
		fSleepNote2->Hide();
	}
	if (fCanChangeProfile)
		fNoSpeed->Hide();
	if (!fHasBrightness)
		fBrightness->Hide();
	else
		ShowBrightness(brightness);

	if (fModule != NULL)
		ShowSettings(fModule->Current());
	_UpdateBattery();
}


PowerView::~PowerView()
{
	if (fModule != NULL)
		fModule->ClearView(this);
}


BBox*
PowerView::_Box(const char* name, const char* title, BStringView** _title)
{
	BBox* box = new BBox(name);
	BStringView* label = r5_settings_box_title(title);
	box->SetLabel(label);
	if (_title != NULL)
		*_title = label;
	return box;
}


void
PowerView::AttachedToWindow()
{
	BGroupView::AttachedToWindow();
	fDisplayBattery->Menu()->SetTargetForItems(this);
	fDisplayAC->Menu()->SetTargetForItems(this);
	fSleepBattery->Menu()->SetTargetForItems(this);
	fSleepAC->Menu()->SetTargetForItems(this);
	fLid->Menu()->SetTargetForItems(this);
	fButton->Menu()->SetTargetForItems(this);
	fProfileBattery->Menu()->SetTargetForItems(this);
	fProfileAC->Menu()->SetTargetForItems(this);
	fExternal->SetTarget(this);
	fWakeLid->SetTarget(this);
	fWakeKeyboard->SetTarget(this);
	fWakeMouse->SetTarget(this);
	fBrightness->SetTarget(this);
	fProfileText->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	// Changes reach the page as messages; it never polls (docs/POWER.md).
	BPowerRoster::StartWatching(BMessenger(this));
	_SyncEnabled();
}


void
PowerView::DetachedFromWindow()
{
	BPowerRoster::StopWatching(BMessenger(this));
	BGroupView::DetachedFromWindow();
}


void
PowerView::ShowSettings(const BMessage& settings)
{
	fUpdating = true;
	mark_int(fDisplayBattery, settings.GetInt32(R5_POWER_DISPLAY_BATTERY, 5));
	mark_int(fDisplayAC, settings.GetInt32(R5_POWER_DISPLAY_AC, 10));
	mark_int(fSleepBattery, settings.GetInt32(R5_POWER_SLEEP_BATTERY, 15));
	mark_int(fSleepAC, settings.GetInt32(R5_POWER_SLEEP_AC, 30));
	mark_string(fLid, settings.GetString(R5_POWER_LID_ACTION, "display_off"));
	mark_string(fButton, settings.GetString(R5_POWER_BUTTON_ACTION, "ask"));
	fExternal->SetValue(settings.GetBool(R5_POWER_EXTERNAL_AWAKE, true)
		? B_CONTROL_ON : B_CONTROL_OFF);
	mark_int(fProfileBattery, settings.GetInt32(R5_POWER_PROFILE_BATTERY, 1));
	mark_int(fProfileAC, settings.GetInt32(R5_POWER_PROFILE_AC, 1));
	fWakeLid->SetValue(settings.GetBool(R5_POWER_WAKE_LID, true)
		? B_CONTROL_ON : B_CONTROL_OFF);
	fWakeKeyboard->SetValue(settings.GetBool(R5_POWER_WAKE_KEYBOARD, true)
		? B_CONTROL_ON : B_CONTROL_OFF);
	fWakeMouse->SetValue(settings.GetBool(R5_POWER_WAKE_MOUSE, false)
		? B_CONTROL_ON : B_CONTROL_OFF);
	fUpdating = false;
	_UpdateProfileText();
}


void
PowerView::ShowBrightness(float brightness)
{
	fUpdating = true;
	fBrightness->SetValue((int32)(brightness * 100.f + 0.5f));
	fUpdating = false;
}


void
PowerView::_UpdateBattery()
{
	if (!fHasBattery)
		return;

	power_battery_info info;
	if (fRoster.GetBatteryInfo(0, &info) != B_OK)
		return;

	BString percent;
	if (info.percent >= 0)
		percent.SetToFormat("%" B_PRId32 " %%", info.percent);
	fPercent->SetText(percent.String());
	fGlyph->SetLevel(info.percent < 0 ? 0 : (info.percent + 12) / 25);

	BString status;
	if ((info.state & B_POWER_BATTERY_DISCHARGING) != 0) {
		status = B_TRANSLATE("On battery");
		if (info.time_left > 0) {
			BString left(B_TRANSLATE("about %time% left"));
			left.ReplaceFirst("%time%", duration(info.time_left));
			status << kDot << left;
		}
	} else if ((info.state & B_POWER_BATTERY_CHARGING) != 0) {
		status = B_TRANSLATE("Charging");
		if (info.time_left > 0) {
			BString full(B_TRANSLATE("full in about %time%"));
			full.ReplaceFirst("%time%", duration(info.time_left));
			status << kDot << full;
		}
	} else {
		status = B_TRANSLATE("Plugged in");
		if (info.percent >= 99)
			status << kDot << B_TRANSLATE("full");
	}
	fStatus->SetText(status.String());

	BString health;
	// Show() and Hide() are counted: only change what is not so already
	bool hidden = fHealth->IsHidden(fHealth);
	if (info.health >= 0) {
		BString value;
		value << info.health;
		health = B_TRANSLATE("Battery health %percent% % of its design "
			"capacity");
		health.ReplaceFirst("%percent%", value);
		if (hidden)
			fHealth->Show();
	} else if (!hidden)
		fHealth->Hide();
	fHealth->SetText(health.String());
}


void
PowerView::_UpdateProfileText()
{
	// The line under the menus explains the profile in use now.
	BMenuField* field = fHasBattery && !fRoster.IsOnAC()
		? fProfileBattery : fProfileAC;
	const char* text;
	switch (marked_int(field, B_POWER_PROFILE_BALANCED)) {
		case B_POWER_PROFILE_SAVER:
			text = B_TRANSLATE("Power saver: the processor stays below its "
				"boost and leans toward saving energy. Balanced and "
				"Performance allow the boost.");
			break;
		case B_POWER_PROFILE_PERFORMANCE:
			text = B_TRANSLATE("Performance: the processor always allows its "
				"boost and leans toward speed. Power saver keeps it below "
				"its boost.");
			break;
		default:
			text = B_TRANSLATE("Balanced: fast when you work, slower when the "
				"computer waits. Power saver keeps the processor below its "
				"boost. Performance always allows the boost.");
			break;
	}
	fProfileText->SetText(text);
	rgb_color hintColor = hint_color();
	fProfileText->SetFontAndColor(be_plain_font, B_FONT_ALL, &hintColor);
}


/*!	The lock (docs/POWER.md, section 2): Profile, Sleep and Wake need it;
	Battery and brightness do not. Without the service (Safe mode) only
	Battery is left.
*/
void
PowerView::_SyncEnabled()
{
	bool service = fModule != NULL && fModule->ServiceUp();
	bool allowed = service && fContext != NULL
		&& fContext->AdminChangeAllowed();

	fDisplayBattery->SetEnabled(allowed);
	fDisplayAC->SetEnabled(allowed);
	set_label_enabled(fDisplayLabel, allowed);
	// Sleep is step 4 of docs/POWER.md: gray until it works.
	fSleepBattery->SetEnabled(allowed && fSleepWorks);
	fSleepAC->SetEnabled(allowed && fSleepWorks);
	set_label_enabled(fSleepLabel, allowed && fSleepWorks);
	fLid->SetEnabled(allowed);
	set_label_enabled(fLidLabel, allowed);
	fButton->SetEnabled(allowed);
	set_label_enabled(fButtonLabel, allowed);
	set_menu_item_enabled(fLid, "sleep", fSleepWorks);
	set_menu_item_enabled(fButton, "sleep", fSleepWorks);
	fExternal->SetEnabled(allowed);

	bool profiles = allowed && fCanChangeProfile;
	fProfileBattery->SetEnabled(profiles);
	fProfileAC->SetEnabled(profiles);
	set_label_enabled(fProfileLabel, profiles);
	fBrightness->SetEnabled(service);

	bool wake = allowed && fSleepWorks;
	fWakeLid->SetEnabled(wake);
	fWakeKeyboard->SetEnabled(wake);
	fWakeMouse->SetEnabled(wake);
	set_label_enabled(fWakeTitle, fSleepWorks);
}


void
PowerView::_Edited()
{
	if (fUpdating || fModule == NULL)
		return;
	BMessage settings(fModule->Current());
	if (fHasBattery) {
		settings.ReplaceInt32(R5_POWER_DISPLAY_BATTERY,
			marked_int(fDisplayBattery, 5));
		settings.ReplaceInt32(R5_POWER_SLEEP_BATTERY,
			marked_int(fSleepBattery, 15));
		settings.ReplaceInt32(R5_POWER_PROFILE_BATTERY,
			marked_int(fProfileBattery, 1));
	}
	settings.ReplaceInt32(R5_POWER_DISPLAY_AC, marked_int(fDisplayAC, 10));
	settings.ReplaceInt32(R5_POWER_SLEEP_AC, marked_int(fSleepAC, 30));
	settings.ReplaceInt32(R5_POWER_PROFILE_AC, marked_int(fProfileAC, 1));
	if (fHasLid) {
		settings.ReplaceString(R5_POWER_LID_ACTION,
			marked_string(fLid, "display_off"));
		settings.ReplaceBool(R5_POWER_WAKE_LID,
			fWakeLid->Value() == B_CONTROL_ON);
	}
	settings.ReplaceString(R5_POWER_BUTTON_ACTION,
		marked_string(fButton, "ask"));
	settings.ReplaceBool(R5_POWER_EXTERNAL_AWAKE,
		fExternal->Value() == B_CONTROL_ON);
	settings.ReplaceBool(R5_POWER_WAKE_KEYBOARD,
		fWakeKeyboard->Value() == B_CONTROL_ON);
	settings.ReplaceBool(R5_POWER_WAKE_MOUSE,
		fWakeMouse->Value() == B_CONTROL_ON);
	_UpdateProfileText();
	fModule->UserEdited(settings);
}


void
PowerView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgRefreshPage:
			_SyncEnabled();
			break;

		case kMsgEdited:
			_Edited();
			break;

		case kMsgBrightness:
			if (!fUpdating && fModule != NULL)
				fModule->BrightnessEdited(fBrightness->Value() / 100.f);
			break;

		case kMsgPowerChanged:
			switch (message->GetInt32("change", 0)) {
				case B_POWER_BATTERY_CHANGED:
					_UpdateBattery();
					break;
				case B_POWER_AC_CHANGED:
					_UpdateBattery();
					_UpdateProfileText();
					break;
				case B_POWER_SETTINGS_CHANGED:
					if (fModule != NULL)
						fModule->SettingsChangedElsewhere();
					break;
			}
			break;

		default:
			BGroupView::MessageReceived(message);
			break;
	}
}


// #pragma mark - PowerModule


PowerModule::PowerModule()
	:
	fView(NULL),
	fContext(NULL),
	fServiceUp(false),
	fOpenedBrightness(-1),
	fBrightness(-1)
{
	BPowerRoster roster;
	power_default_settings(fFactory, roster.CanSleep());
	fCurrent = fFactory;
	fOpened = fFactory;
	fWritten = fFactory;
}


BView*
PowerModule::CreateView(SettingsContext* context, BRect frame)
{
	(void)frame;
	fContext = context;
	fView = new PowerView(this, context);
	return fView;
}


void
PowerModule::ClearView(PowerView* view)
{
	if (fView == view)
		fView = NULL;
}


void
PowerModule::_Push()
{
	if (fView != NULL)
		fView->ShowSettings(fCurrent);
}


status_t
PowerModule::LoadSettings(SettingsContext* context)
{
	fContext = context;
	BPowerRoster roster;
	BMessage settings;
	fServiceUp = roster.GetSettings(&settings) == B_OK;
	fCurrent = fServiceUp ? settings : fFactory;
	fOpened = fCurrent;
	fWritten = fCurrent;

	float brightness;
	fOpenedBrightness = BScreen().GetBrightness(&brightness) == B_OK
		? brightness : -1;
	fBrightness = fOpenedBrightness;
	_Push();
	return B_OK;
}


void
PowerModule::UserEdited(const BMessage& settings)
{
	fCurrent = settings;
	if (fContext != NULL)
		fContext->NotifyChanged(Identifier());
}


void
PowerModule::BrightnessEdited(float brightness)
{
	// Brightness needs no lock and is not in the machine file
	// (docs/POWER.md, section 3). Haiku's BScreen call, through the
	// accelerant.
	if (BScreen().SetBrightness(brightness) == B_OK)
		fBrightness = brightness;
	if (fContext != NULL)
		fContext->NotifyChanged(Identifier());
}


void
PowerModule::SettingsChangedElsewhere()
{
	BPowerRoster roster;
	BMessage settings;
	if (roster.GetSettings(&settings) != B_OK
		|| same_settings(settings, fWritten))
		return;
	fCurrent = settings;
	fWritten = settings;
	_Push();
}


status_t
PowerModule::SaveSettings(SettingsContext* context)
{
	fContext = context;
	if (!fServiceUp || same_settings(fCurrent, fWritten))
		return B_OK;
	if (context == NULL || !context->AdminChangeAllowed())
		return B_PERMISSION_DENIED;

	BPowerRoster roster;
	status_t status = roster.SetSettings(fCurrent);
	if (status == B_OK)
		fWritten = fCurrent;
	return status;
}


status_t
PowerModule::RevertSettings(SettingsContext* context)
{
	if (fOpenedBrightness >= 0 && fBrightness != fOpenedBrightness) {
		BScreen().SetBrightness(fOpenedBrightness);
		fBrightness = fOpenedBrightness;
		if (fView != NULL)
			fView->ShowBrightness(fBrightness);
	}
	fCurrent = fOpened;
	_Push();
	return SaveSettings(context);
}


bool
PowerModule::IsModified() const
{
	return !same_settings(fCurrent, fOpened)
		|| fBrightness != fOpenedBrightness;
}


bool
PowerModule::CanApplyDefaults() const
{
	return fServiceUp && fContext != NULL && fContext->AdminChangeAllowed()
		&& !same_settings(fCurrent, fFactory);
}


status_t
PowerModule::ApplyDefaults(SettingsContext* context)
{
	// Defaults does not change the brightness (docs/POWER.md, section 2).
	fCurrent = fFactory;
	_Push();
	return SaveSettings(context);
}


// The add-on settings_power (distro/settings/ModuleAPI.md): every
// Settings page is an add-on of its own.
extern "C" _EXPORT int32
settings_api_version()
{
	return kSettingsApiVersion;
}


extern "C" _EXPORT SettingsModule*
instantiate_settings_module()
{
	return new PowerModule();
}
