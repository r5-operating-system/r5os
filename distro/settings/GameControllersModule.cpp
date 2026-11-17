/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Settings, Devices, Game controllers (docs/GAME-CONTROLLERS.md, option B;
 * the picture is docs/design/gamecontrollers-mockup). One card per
 * controller, and the live test of the chosen one with its layout, its
 * button labels and Set up buttons… (a window that asks for each control
 * in turn). The pictures are BGameControllerView from libr5game.so. Every
 * choice is stored per controller model by BGameController as soon as it
 * changes; Revert puts back the choices from when the page was opened,
 * Defaults gives every controller its own labels again and removes the
 * chosen controller's layout and button setup. No administrator is needed:
 * the choice is the person's own.
 */


#include "NetworkPage.h"
#include "SettingsFont.h"
#include "SettingsMessages.h"
#include "SettingsModule.h"

#include <Box.h>
#include <Button.h>
#include <Card.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <PopUpMenu.h>
#include <SpaceLayoutItem.h>
#include <String.h>
#include <StringView.h>
#include <TextView.h>
#include <Window.h>

#include <GameController.h>
#include <GameControllerView.h>
#include <R5Icons.h>

#include <math.h>
#include <stdlib.h>

#include <map>
#include <string>
#include <vector>


static const uint32 kMsgSelectController = 'gcSe';
static const uint32 kMsgLabelsChosen = 'gcLb';
static const uint32 kMsgOpenBluetooth = 'gcBt';
static const uint32 kMsgBatteryTick = 'gcBa';


struct controller_entry {
	BString		device;
	BString		name;
	uint16		vendor;
	uint16		product;
	game_controller_connection connection;
	game_controller_family family;
	int32		battery;
};


static uint32
model_key(uint16 vendor, uint16 product)
{
	return (uint32)vendor << 16 | product;
}


static rgb_color
quiet_color(uint8 amount)
{
	return mix_color(ui_color(B_DOCUMENT_BACKGROUND_COLOR),
		ui_color(B_DOCUMENT_TEXT_COLOR), amount);
}


static const char*
family_layout_text(game_controller_family family)
{
	switch (family) {
		case B_GAME_CONTROLLER_FAMILY_PLAYSTATION:
			return "PlayStation layout";
		case B_GAME_CONTROLLER_FAMILY_XBOX:
			return "Xbox layout";
		case B_GAME_CONTROLLER_FAMILY_NINTENDO:
			return "Nintendo layout";
		case B_GAME_CONTROLLER_FAMILY_SEGA_GENESIS:
			return "Sega Genesis layout";
		default:
			return "built from what it reports";
	}
}


static const char*
connection_text(game_controller_connection connection)
{
	switch (connection) {
		case B_GAME_CONTROLLER_CONNECTION_USB:
			return "USB";
		case B_GAME_CONTROLLER_CONNECTION_BLUETOOTH:
			return "Bluetooth";
		default:
			return "Connected";
	}
}


// #pragma mark - ControllerCard


/*!	One controller: its name, how it is connected and which layout it
	has, and the battery when the controller reports it. Drawn in one view
	so a click anywhere on the card chooses it.
*/
class ControllerCard : public BAppearanceKit::BCard {
public:
	ControllerCard(const controller_entry& entry, BHandler* target)
		:
		BAppearanceKit::BCard("controller card",
			BAppearanceKit::B_CARD_DOCUMENT, B_VERTICAL),
		fEntry(entry),
		fTarget(target),
		fSelected(false)
	{
	}

	const controller_entry& Entry() const { return fEntry; }

	void SetBattery(int32 battery)
	{
		if (fEntry.battery == battery)
			return;
		fEntry.battery = battery;
		Invalidate();
	}

	void SetSelected(bool selected)
	{
		if (fSelected == selected)
			return;
		fSelected = selected;
		Invalidate();
	}

	virtual BSize MinSize()
	{
		float unit = be_plain_font->Size() / 13.0f;
		return BSize(roundf(200 * unit), _Height());
	}

	virtual BSize MaxSize()
	{
		return BSize(B_SIZE_UNLIMITED, _Height());
	}

	virtual BSize PreferredSize()
	{
		return MinSize();
	}

	virtual void MouseDown(BPoint where)
	{
		(void)where;
		BMessage message(kMsgSelectController);
		message.AddString("device", fEntry.device);
		BMessenger(fTarget).SendMessage(&message);
	}

	virtual void Draw(BRect updateRect)
	{
		BAppearanceKit::BCard::Draw(updateRect);
		BRect bounds = Bounds();
		float unit = be_plain_font->Size() / 13.0f;
		if (fSelected) {
			SetHighColor(ui_color(B_STATUS_BAR_COLOR));
			SetPenSize(2);
			float radius = be_plain_font->Size() * 0.25f;
			StrokeRoundRect(bounds.InsetByCopy(1, 1), radius, radius);
			SetPenSize(1);
		}

		float x = roundf(11 * unit);
		float y = roundf(9 * unit);
		const BFont* bold = r5_settings_bold_font();
		font_height height;
		bold->GetHeight(&height);
		SetFont(bold);
		SetHighColor(ui_color(B_DOCUMENT_TEXT_COLOR));
		y += ceilf(height.ascent);
		BString name(fEntry.name);
		bold->TruncateString(&name, B_TRUNCATE_END, bounds.Width() - 2 * x);
		DrawString(name.String(), BPoint(x, y));
		y += ceilf(height.descent) + roundf(3 * unit);

		BFont small(r5_settings_plain_font());
		small.SetSize(small.Size() * 0.88f);
		small.GetHeight(&height);
		SetFont(&small);
		SetHighColor(quiet_color(140));
		BString detail(connection_text(fEntry.connection));
		detail << " \xc2\xb7 " << family_layout_text(fEntry.family);
		small.TruncateString(&detail, B_TRUNCATE_END,
			bounds.Width() - 2 * x);
		y += ceilf(height.ascent);
		DrawString(detail.String(), BPoint(x, y));
		y += ceilf(height.descent) + roundf(4 * unit);

		y += ceilf(height.ascent);
		if (fEntry.battery >= 0) {
			float glyphHeight = roundf(12 * unit);
			BRect glyph(x, y - ceilf(height.ascent) + 1, x + glyphHeight * 2,
				y - ceilf(height.ascent) + glyphHeight);
			rgb_color text = quiet_color(190);
			r5_level_glyph_draw(this, R5_LEVEL_BATTERY, glyph,
				(fEntry.battery + 12) / 25, text, ViewColor(),
				R5_LEVEL_STYLE_NORMAL);
			BString battery;
			battery.SetToFormat("Battery %" B_PRId32 " %%", fEntry.battery);
			SetHighColor(text);
			DrawString(battery.String(),
				BPoint(glyph.right + roundf(6 * unit), y));
		} else {
			SetHighColor(quiet_color(110));
			DrawString("No battery reported", BPoint(x, y));
		}
	}

private:
	float _Height()
	{
		float unit = be_plain_font->Size() / 13.0f;
		font_height bold;
		r5_settings_bold_font()->GetHeight(&bold);
		BFont small(r5_settings_plain_font());
		small.SetSize(small.Size() * 0.88f);
		font_height plain;
		small.GetHeight(&plain);
		return ceilf(bold.ascent + bold.descent)
			+ 2 * ceilf(plain.ascent + plain.descent)
			+ roundf((9 + 3 + 4 + 10) * unit);
	}

	controller_entry	fEntry;
	BHandler*			fTarget;
	bool				fSelected;
};


// #pragma mark - names of the standard layout


// SDL's mapping keys, the form BGameController::SetSavedMapping() takes.
static const char* const kButtonKeys[B_CONTROLLER_BUTTON_COUNT] = {
	"a", "b", "x", "y", "back", "guide", "start", "leftstick", "rightstick",
	"leftshoulder", "rightshoulder", "dpup", "dpdown", "dpleft", "dpright",
	"misc1", "paddle1", "paddle2", "paddle3", "paddle4", "touchpad"
};

static const char* const kAxisKeys[B_CONTROLLER_AXIS_COUNT] = {
	"leftx", "lefty", "rightx", "righty", "lefttrigger", "righttrigger"
};


// One question of Set up buttons…: a button of the standard layout, or an
// axis (a stick pushed right or down, a trigger pulled).
struct setup_step {
	bool		axis;
	int32		target;		// game_controller_button or _axis
	BString		prompt;
};


static const char*
face_name(game_controller_family family, game_controller_labels labels,
	game_controller_button button)
{
	static const char* const kXbox[4] = { "A", "B", "X", "Y" };
	static const char* const kPlayStation[4]
		= { "Cross (\xe2\x9c\x95)", "Circle (\xe2\x97\x8b)",
			"Square (\xe2\x96\xa1)", "Triangle (\xe2\x96\xb3)" };
	// The Nintendo shape has A and B, and X and Y, the other way round.
	static const char* const kNintendo[4] = { "B", "A", "Y", "X" };
	if (family == B_GAME_CONTROLLER_FAMILY_NINTENDO)
		return kNintendo[button];
	if (labels == B_GAME_CONTROLLER_LABELS_PLAYSTATION)
		return kPlayStation[button];
	return kXbox[button];
}


static std::vector<setup_step>
setup_steps(game_controller_family family, game_controller_labels labels)
{
	std::vector<setup_step> steps;
	setup_step step;
	step.axis = false;

	static const char* const kPlace[4] = { "the lower", "the right",
		"the left", "the upper" };

	if (family == B_GAME_CONTROLLER_FAMILY_SEGA_GENESIS) {
		// As the picture draws it: X Y Z over A B C; C and Z are the
		// shoulders of the standard layout, L and R its triggers.
		struct sega_key {
			bool		axis;
			int32		target;
			const char*	name;
		};
		static const sega_key kKeys[] = {
			{ false, B_CONTROLLER_BUTTON_A, "A" },
			{ false, B_CONTROLLER_BUTTON_B, "B" },
			{ false, B_CONTROLLER_BUTTON_RIGHT_SHOULDER, "C" },
			{ false, B_CONTROLLER_BUTTON_X, "X" },
			{ false, B_CONTROLLER_BUTTON_Y, "Y" },
			{ false, B_CONTROLLER_BUTTON_LEFT_SHOULDER, "Z" },
			{ true, B_CONTROLLER_AXIS_LEFT_TRIGGER, "L" },
			{ true, B_CONTROLLER_AXIS_RIGHT_TRIGGER, "R" }
		};
		for (size_t i = 0; i < sizeof(kKeys) / sizeof(kKeys[0]); i++) {
			step.axis = kKeys[i].axis;
			step.target = kKeys[i].target;
			step.prompt.SetToFormat("Press %s", kKeys[i].name);
			steps.push_back(step);
		}
	} else {
		for (int32 i = 0; i < 4; i++) {
			step.axis = false;
			step.target = B_CONTROLLER_BUTTON_A + i;
			step.prompt.SetToFormat("Press %s, %s face button",
				face_name(family, labels, (game_controller_button)i),
				kPlace[i]);
			steps.push_back(step);
		}
		bool playstation = labels == B_GAME_CONTROLLER_LABELS_PLAYSTATION
			&& family != B_GAME_CONTROLLER_FAMILY_NINTENDO;
		bool nintendo = family == B_GAME_CONTROLLER_FAMILY_NINTENDO;
		const char* names[4];
		if (playstation) {
			names[0] = "L1"; names[1] = "R1"; names[2] = "L2"; names[3] = "R2";
		} else if (nintendo) {
			names[0] = "L"; names[1] = "R"; names[2] = "ZL"; names[3] = "ZR";
		} else {
			names[0] = "LB"; names[1] = "RB"; names[2] = "LT"; names[3] = "RT";
		}
		step.axis = false;
		step.target = B_CONTROLLER_BUTTON_LEFT_SHOULDER;
		step.prompt.SetToFormat("Press %s, the left shoulder button",
			names[0]);
		steps.push_back(step);
		step.target = B_CONTROLLER_BUTTON_RIGHT_SHOULDER;
		step.prompt.SetToFormat("Press %s, the right shoulder button",
			names[1]);
		steps.push_back(step);
		step.axis = true;
		step.target = B_CONTROLLER_AXIS_LEFT_TRIGGER;
		step.prompt.SetToFormat("Pull %s, the left trigger", names[2]);
		steps.push_back(step);
		step.target = B_CONTROLLER_AXIS_RIGHT_TRIGGER;
		step.prompt.SetToFormat("Pull %s, the right trigger", names[3]);
		steps.push_back(step);
		step.target = B_CONTROLLER_AXIS_LEFT_X;
		step.prompt = "Push the left stick to the right";
		steps.push_back(step);
		step.target = B_CONTROLLER_AXIS_LEFT_Y;
		step.prompt = "Push the left stick down";
		steps.push_back(step);
		step.target = B_CONTROLLER_AXIS_RIGHT_X;
		step.prompt = "Push the right stick to the right";
		steps.push_back(step);
		step.target = B_CONTROLLER_AXIS_RIGHT_Y;
		step.prompt = "Push the right stick down";
		steps.push_back(step);
		step.axis = false;
		step.target = B_CONTROLLER_BUTTON_LEFT_STICK;
		step.prompt = "Press the left stick in";
		steps.push_back(step);
		step.target = B_CONTROLLER_BUTTON_RIGHT_STICK;
		step.prompt = "Press the right stick in";
		steps.push_back(step);
	}

	step.axis = false;
	static const struct {
		int32		target;
		const char*	prompt;
	} kRest[] = {
		{ B_CONTROLLER_BUTTON_DPAD_UP, "Press the d-pad up" },
		{ B_CONTROLLER_BUTTON_DPAD_DOWN, "Press the d-pad down" },
		{ B_CONTROLLER_BUTTON_DPAD_LEFT, "Press the d-pad to the left" },
		{ B_CONTROLLER_BUTTON_DPAD_RIGHT, "Press the d-pad to the right" },
		{ B_CONTROLLER_BUTTON_START, "Press Start" },
		{ B_CONTROLLER_BUTTON_BACK, "Press Select" }
	};
	for (size_t i = 0; i < sizeof(kRest) / sizeof(kRest[0]); i++) {
		step.target = kRest[i].target;
		step.prompt = kRest[i].prompt;
		steps.push_back(step);
	}
	return steps;
}


// #pragma mark - ButtonSetupWindow


static const uint32 kMsgSetupPoll = 'gcSp';
static const uint32 kMsgSetupSkip = 'gcSk';
static const uint32 kMsgSetupDone = 'gcSd';


/*!	Set up buttons…: asks for one control after the other and notes what
	the controller sends for it (a raw button, a hat direction, an axis).
	At the end it sends kMsgSetupDone with the mapping text to the page,
	which saves it. Cancel saves nothing.
*/
class ButtonSetupWindow : public BWindow {
public:
	ButtonSetupWindow(BWindow* parent, BMessenger page, const char* device,
		game_controller_family family, game_controller_labels labels)
		:
		BWindow(BRect(0, 0, 10, 10), "Set up buttons",
			B_TITLED_WINDOW_LOOK, B_MODAL_SUBSET_WINDOW_FEEL,
			B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS
				| B_ASYNCHRONOUS_CONTROLS | B_CLOSE_ON_ESCAPE),
		fPage(page),
		fSteps(setup_steps(family, labels)),
		fStep(0),
		fWaitRelease(false),
		fSettle(0),
		fRunner(NULL)
	{
		memset(&fCaught, 0, sizeof(fCaught));
		if (parent != NULL)
			AddToSubset(parent);
		fController.Open(device);

		float unit = network_unit();
		BStringView* name = new BStringView("controller",
			fController.Name());
		name->SetFont(r5_settings_bold_font());
		fPrompt = new BStringView("prompt", "");
		BFont big(be_bold_font);
		big.SetSize(be_bold_font->Size() * 1.35f);
		fPrompt->SetFont(&big);
		fPrompt->SetExplicitMinSize(BSize(roundf(380 * unit), B_SIZE_UNSET));
		fProgress = new BStringView("progress", "");
		fProgress->SetHighColor(quiet_color(140));
		BStringView* hint = new BStringView("hint",
			"Skip a control your controller does not have.");
		hint->SetHighColor(quiet_color(140));

		BButton* skip = new BButton("skip", "Skip",
			new BMessage(kMsgSetupSkip));
		BButton* cancel = new BButton("cancel", "Cancel",
			new BMessage(B_QUIT_REQUESTED));

		BLayoutBuilder::Group<>(this, B_VERTICAL, roundf(8 * unit))
			.SetInsets(B_USE_WINDOW_SPACING)
			.Add(name)
			.Add(fPrompt)
			.Add(fProgress)
			.Add(hint)
			.AddStrut(roundf(6 * unit))
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(cancel)
				.Add(skip)
			.End();

		if (parent != NULL)
			CenterIn(parent->Frame());
		else
			CenterOnScreen();
		_StartStep();
		BMessage poll(kMsgSetupPoll);
		fRunner = new BMessageRunner(BMessenger(this), &poll, 16000);
	}

	virtual ~ButtonSetupWindow()
	{
		delete fRunner;
	}

	virtual void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case kMsgSetupPoll:
				_Poll();
				break;
			case kMsgSetupSkip:
				_Next();
				break;
			default:
				BWindow::MessageReceived(message);
				break;
		}
	}

private:
	struct source {
		char	kind;		// 'b', 'h', 'a'; 0 none
		int32	index;
		uint8	hatMask;
		int8	half;		// axis: +1, -1, 0 whole
		bool	invert;
	};

	void _Read(bool* buttons, uint8* hats, int16* axes)
	{
		for (int32 i = 0; i < fController.CountRawButtons() && i < 128; i++)
			buttons[i] = fController.IsRawButtonDown(i);
		for (int32 i = 0; i < fController.CountRawHats() && i < 8; i++)
			hats[i] = fController.RawHatValue(i);
		for (int32 i = 0; i < fController.CountRawAxes() && i < 32; i++)
			axes[i] = fController.RawAxisValue(i);
	}

	void _StartStep()
	{
		if (fStep >= fSteps.size()) {
			_Finish();
			return;
		}
		fPrompt->SetText(fSteps[fStep].prompt.String());
		BString progress;
		progress.SetToFormat("Step %" B_PRIuSIZE " of %" B_PRIuSIZE,
			fStep + 1, fSteps.size());
		fProgress->SetText(progress.String());
		// Rest values: what the controller sends while nothing is pressed,
		// taken over the first polls of the step (the first read after
		// Open() can still be all zeros).
		fSettle = 10;
		memset(fRestButtons, 0, sizeof(fRestButtons));
		memset(fRestHats, 0, sizeof(fRestHats));
		memset(fRestAxes, 0, sizeof(fRestAxes));
		if (fController.IsOpen() && fController.Update() == B_OK)
			_Read(fRestButtons, fRestHats, fRestAxes);
	}

	void _Next()
	{
		fStep++;
		fWaitRelease = false;
		_StartStep();
	}

	bool _Released(const bool* buttons, const uint8* hats,
		const int16* axes) const
	{
		switch (fCaught.kind) {
			case 'b':
				return !buttons[fCaught.index];
			case 'h':
				return hats[fCaught.index] == 0;
			case 'a':
				return abs(axes[fCaught.index] - fRestAxes[fCaught.index])
					< 8192;
		}
		return true;
	}

	void _Poll()
	{
		if (!fController.IsOpen() || fController.Update() != B_OK)
			return;
		bool buttons[128];
		uint8 hats[8];
		int16 axes[32];
		memset(buttons, 0, sizeof(buttons));
		memset(hats, 0, sizeof(hats));
		memset(axes, 0, sizeof(axes));
		_Read(buttons, hats, axes);

		if (fSettle > 0) {
			_Read(fRestButtons, fRestHats, fRestAxes);
			fSettle--;
			return;
		}
		if (fWaitRelease) {
			if (_Released(buttons, hats, axes))
				_Next();
			return;
		}
		if (fStep >= fSteps.size())
			return;

		const setup_step& step = fSteps[fStep];
		source caught;
		memset(&caught, 0, sizeof(caught));
		for (int32 i = 0; i < 128 && caught.kind == 0; i++) {
			if (buttons[i] && !fRestButtons[i]) {
				caught.kind = 'b';
				caught.index = i;
			}
		}
		for (int32 i = 0; i < 8 && caught.kind == 0; i++) {
			// Only a straight direction: 1 up, 3 right, 5 down, 7 left.
			if (hats[i] != fRestHats[i] && hats[i] != 0
				&& (hats[i] & 1) != 0) {
				static const uint8 kMasks[9]
					= { 0, 1, 3, 2, 6, 4, 12, 8, 9 };
				caught.kind = 'h';
				caught.index = i;
				caught.hatMask = hats[i] <= 8 ? kMasks[hats[i]] : 0;
			}
		}
		for (int32 i = 0; i < 32 && caught.kind == 0; i++) {
			int32 delta = (int32)axes[i] - fRestAxes[i];
			if (abs(delta) < 16384)
				continue;
			caught.kind = 'a';
			caught.index = i;
			if (step.axis && (step.target == B_CONTROLLER_AXIS_LEFT_TRIGGER
					|| step.target == B_CONTROLLER_AXIS_RIGHT_TRIGGER)) {
				// A trigger that rests at one end is the whole axis; one
				// that rests in the middle is the half it moves into.
				if (fRestAxes[i] < -16384)
					caught.half = 0;
				else if (fRestAxes[i] > 16384) {
					caught.half = 0;
					caught.invert = true;
				} else
					caught.half = delta > 0 ? 1 : -1;
			} else if (step.axis) {
				// Pushed right or down: the positive end.
				caught.invert = delta < 0;
			} else
				caught.half = delta > 0 ? 1 : -1;
		}
		if (caught.kind == 0)
			return;
		// A stick takes only an axis; a hat is never a trigger.
		if (step.axis && step.target < B_CONTROLLER_AXIS_LEFT_TRIGGER
			&& caught.kind != 'a')
			return;
		if (step.axis && caught.kind == 'h')
			return;

		_Store(step, caught);
		fCaught = caught;
		fWaitRelease = true;
	}

	void _Store(const setup_step& step, const source& caught)
	{
		BString value;
		switch (caught.kind) {
			case 'b':
				value.SetToFormat("b%" B_PRId32, caught.index);
				break;
			case 'h':
				value.SetToFormat("h%" B_PRId32 ".%u", caught.index,
					caught.hatMask);
				break;
			default:
				value.SetToFormat("%sa%" B_PRId32 "%s",
					caught.half > 0 ? "+" : (caught.half < 0 ? "-" : ""),
					caught.index, caught.invert ? "~" : "");
				break;
		}
		BString pair;
		pair << (step.axis ? kAxisKeys[step.target]
			: kButtonKeys[step.target]) << ":" << value << ",";
		fMappingText << pair;
	}

	void _Finish()
	{
		delete fRunner;
		fRunner = NULL;
		if (fMappingText.Length() > 0) {
			BMessage done(kMsgSetupDone);
			done.AddString("mapping", fMappingText);
			done.AddInt32("vendor", fController.VendorID());
			done.AddInt32("product", fController.ProductID());
			fPage.SendMessage(&done);
		}
		PostMessage(B_QUIT_REQUESTED);
	}

	BMessenger			fPage;
	BGameController		fController;
	std::vector<setup_step> fSteps;
	size_t				fStep;
	bool				fWaitRelease;
	int32				fSettle;
	source				fCaught;
	BMessageRunner*		fRunner;
	BStringView*		fPrompt;
	BStringView*		fProgress;
	BString				fMappingText;
	bool				fRestButtons[128];
	uint8				fRestHats[8];
	int16				fRestAxes[32];
};


// #pragma mark - GameControllersPage


class GameControllersModule;


class GameControllersPage : public BGroupView {
public:
								GameControllersPage(
									GameControllersModule* module,
									SettingsContext* context);
	virtual						~GameControllersPage();

	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();
	virtual	void				MessageReceived(BMessage* message);

			void				Refresh();
									// a choice changed elsewhere

private:
			void				_Rebuild();
			void				_Select(const char* device);
			void				_UpdateTest();
			void				_CloseSetup();
			ControllerCard*		_SelectedCard() const;

			GameControllersModule* fModule;
			SettingsContext*	fContext;
			BGroupView*			fCards;
			BStringView*		fEmpty;
			std::vector<ControllerCard*> fCardList;
			BString				fSelected;

			BMenuField*			fLayoutField;
			BMenuItem*			fLayoutItems[5];
			BMenuItem*			fOwnSetupItem;
			BMenuField*			fLabelsField;
			BMenuItem*			fXboxItem;
			BMenuItem*			fPlayStationItem;
			BButton*			fSetupButton;
			BStringView*		fIDs;
			BGameControllerView* fTest;
			BStringView*		fTestHint;
			BMessageRunner*		fBatteryRunner;
			BMessenger			fSetup;
};


// #pragma mark - GameControllersModule


// What is stored for one controller model.
struct model_choice {
	game_controller_labels	labels;
	game_controller_family	layout;
	BString					mapping;

	bool operator==(const model_choice& other) const
	{
		return labels == other.labels && layout == other.layout
			&& mapping == other.mapping;
	}

	bool operator!=(const model_choice& other) const
	{
		return !(*this == other);
	}
};


static model_choice
read_choice(uint16 vendor, uint16 product)
{
	model_choice choice;
	choice.labels = BGameController::LabelStyleFor(vendor, product);
	choice.layout = BGameController::LayoutFor(vendor, product);
	char mapping[1024];
	if (BGameController::GetSavedMapping(vendor, product, mapping,
			sizeof(mapping)) == B_OK)
		choice.mapping = mapping;
	return choice;
}


class GameControllersModule : public SettingsModule {
public:
								GameControllersModule()
									:
									fContext(NULL),
									fPage(NULL),
									fSelectedVendor(0),
									fSelectedProduct(0)
								{
								}

	virtual	const char*			Identifier() const
									{ return "gamecontrollers"; }
	virtual	const char*			Name() const { return "Game controllers"; }
	virtual	const char*			Category() const { return "devices"; }
	virtual	const char*			Description() const
									{ return "Test your controllers and "
										"set up their buttons"; }
	virtual	const char*			Keywords() const
									{ return "game controller gamepad "
										"joystick joypad test buttons "
										"layout mapping setup "
										"playstation xbox nintendo sega"; }

	virtual	BView*				CreateView(SettingsContext* context,
									BRect frame)
								{
									(void)frame;
									fContext = context;
									fPage = new GameControllersPage(this,
										context);
									return fPage;
								}

	virtual	status_t			LoadSettings(SettingsContext* context)
								{
									(void)context;
									fOriginal.clear();
									fCurrent.clear();
									return B_OK;
								}

	// The page writes each choice itself; the window calls this after the
	// change.
	virtual	status_t			SaveSettings(SettingsContext* context)
									{ (void)context; return B_OK; }

	// Every controller model seen on the page gets the labels, layout and
	// button setup it had when the page was opened.
	virtual	status_t			RevertSettings(SettingsContext* context)
								{
									(void)context;
									status_t result = B_OK;
									std::map<uint32, model_choice>::iterator
										i;
									for (i = fOriginal.begin();
											i != fOriginal.end(); i++) {
										status_t status = _Write(i->first,
											i->second);
										if (status != B_OK)
											result = status;
									}
									fCurrent = fOriginal;
									_RefreshPage();
									return result;
								}

	virtual	bool				IsModified() const
								{
									return fCurrent != fOriginal;
								}

	virtual	bool				CanApplyDefaults() const { return true; }

	// Every controller its own labels again, as before; the chosen
	// controller also loses its layout choice and its button setup.
	virtual	status_t			ApplyDefaults(SettingsContext* context)
								{
									(void)context;
									status_t status
										= BGameController::ResetLabelStyles();
									std::map<uint32, model_choice>::iterator
										i;
									for (i = fCurrent.begin();
											i != fCurrent.end(); i++)
										i->second.labels
											= B_GAME_CONTROLLER_LABELS_AUTOMATIC;
									if (fSelectedVendor != 0) {
										Remember(fSelectedVendor,
											fSelectedProduct);
										uint32 key = model_key(
											fSelectedVendor,
											fSelectedProduct);
										model_choice& now = fCurrent[key];
										now.layout
											= B_GAME_CONTROLLER_FAMILY_UNKNOWN;
										now.mapping = "";
										status_t written = _Write(key, now);
										if (status == B_OK)
											status = written;
									}
									_RefreshPage();
									return status;
								}

			status_t			SetLabels(uint16 vendor, uint16 product,
									game_controller_labels labels)
								{
									Remember(vendor, product);
									uint32 key = model_key(vendor, product);
									status_t status
										= BGameController::SetLabelStyle(
											vendor, product, labels);
									if (status == B_OK)
										fCurrent[key].labels = labels;
									_Changed();
									return status;
								}

			// A layout from the menu replaces a button setup.
			status_t			SetLayout(uint16 vendor, uint16 product,
									game_controller_family layout)
								{
									Remember(vendor, product);
									uint32 key = model_key(vendor, product);
									model_choice now = fCurrent[key];
									now.layout = layout;
									now.mapping = "";
									status_t status = _Write(key, now);
									fCurrent[key] = read_choice(vendor,
										product);
									_Changed();
									return status;
								}

			status_t			SetMapping(uint16 vendor, uint16 product,
									const char* mapping)
								{
									Remember(vendor, product);
									uint32 key = model_key(vendor, product);
									status_t status
										= BGameController::SetSavedMapping(
											vendor, product, mapping);
									fCurrent[key] = read_choice(vendor,
										product);
									_Changed();
									return status;
								}

			// A controller on the page: Revert puts its choices back to
			// what they were when the page was opened.
			void				Remember(uint16 vendor, uint16 product)
								{
									if (vendor == 0)
										return;
									uint32 key = model_key(vendor, product);
									if (fOriginal.find(key)
											!= fOriginal.end())
										return;
									model_choice choice = read_choice(vendor,
										product);
									fOriginal[key] = choice;
									fCurrent[key] = choice;
								}

			void				SetSelected(uint16 vendor, uint16 product)
								{
									fSelectedVendor = vendor;
									fSelectedProduct = product;
								}

			void				PageGone(GameControllersPage* page)
								{
									if (fPage == page)
										fPage = NULL;
								}

private:
			status_t			_Write(uint32 key, const model_choice& choice)
								{
									uint16 vendor = key >> 16;
									uint16 product = key & 0xffff;
									status_t result = B_OK;
									status_t status
										= BGameController::SetLabelStyle(
											vendor, product, choice.labels);
									if (status != B_OK)
										result = status;
									status = BGameController::SetLayout(
										vendor, product, choice.layout);
									if (status != B_OK)
										result = status;
									status = BGameController::SetSavedMapping(
										vendor, product,
										choice.mapping.String());
									if (status != B_OK)
										result = status;
									return result;
								}

			void				_Changed()
								{
									if (fContext != NULL)
										fContext->NotifyChanged(Identifier());
								}

			void				_RefreshPage()
								{
									if (fPage != NULL && fPage->LockLooper()) {
										fPage->Refresh();
										fPage->UnlockLooper();
									}
								}

			SettingsContext*	fContext;
			GameControllersPage* fPage;
			uint16				fSelectedVendor;
			uint16				fSelectedProduct;
			std::map<uint32, model_choice> fOriginal;
			std::map<uint32, model_choice> fCurrent;
};


// #pragma mark - GameControllersPage


static const uint32 kMsgLayoutChosen = 'gcLa';
static const uint32 kMsgSetupButtons = 'gcSb';


static const struct {
	game_controller_family	family;
	const char*				label;
} kLayouts[5] = {
	{ B_GAME_CONTROLLER_FAMILY_UNKNOWN, "Automatic" },
	{ B_GAME_CONTROLLER_FAMILY_XBOX, "Xbox" },
	{ B_GAME_CONTROLLER_FAMILY_PLAYSTATION, "PlayStation" },
	{ B_GAME_CONTROLLER_FAMILY_NINTENDO, "Nintendo" },
	{ B_GAME_CONTROLLER_FAMILY_SEGA_GENESIS, "Sega Genesis" }
};


GameControllersPage::GameControllersPage(GameControllersModule* module,
	SettingsContext* context)
	:
	BGroupView("game controllers page", B_VERTICAL, B_USE_DEFAULT_SPACING),
	fModule(module),
	fContext(context),
	fBatteryRunner(NULL)
{
	float unit = network_unit();

	fCards = new BGroupView("cards", B_VERTICAL, roundf(8 * unit));
	fEmpty = new BStringView("no controller",
		"No game controller is connected.");
	fEmpty->SetHighColor(quiet_color(140));

	BTextView* hint = new BTextView("plug in hint");
	hint->MakeEditable(false);
	hint->MakeSelectable(false);
	hint->SetStylable(true);
	hint->SetInsets(0, 0, 0, 0);
	hint->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	hint->SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
	BFont small(be_plain_font);
	small.SetSize(be_plain_font->Size() * 0.9f);
	rgb_color quiet = quiet_color(140);
	hint->SetFontAndColor(&small, B_FONT_ALL, &quiet);
	hint->SetText("A new controller shows up here when you plug it in, or "
		"when you pair it on the Bluetooth page.");
	hint->SetExplicitMinSize(BSize(roundf(200 * unit), roundf(48 * unit)));
	LinkControl* bluetooth = new LinkControl("bluetooth link",
		"Open Bluetooth", new BMessage(kMsgOpenBluetooth));
	bluetooth->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	bluetooth->SetLowUIColor(B_PANEL_BACKGROUND_COLOR);

	BGroupView* left = new BGroupView("controllers", B_VERTICAL,
		roundf(8 * unit));
	left->SetExplicitMinSize(BSize(roundf(236 * unit), B_SIZE_UNSET));
	left->SetExplicitMaxSize(BSize(roundf(236 * unit), B_SIZE_UNLIMITED));
	BLayoutBuilder::Group<>(left)
		.Add(fCards)
		.AddGlue()
		.Add(hint)
		.Add(bluetooth);

	// The layout: where the buttons are and how they are read. A fixed
	// layout, or the person's own setup, comes before the database.
	BPopUpMenu* layoutMenu = new BPopUpMenu("layout");
	for (int32 i = 0; i < 5; i++) {
		BMessage* message = new BMessage(kMsgLayoutChosen);
		message->AddInt32("layout", kLayouts[i].family);
		fLayoutItems[i] = new BMenuItem(kLayouts[i].label, message);
		layoutMenu->AddItem(fLayoutItems[i]);
	}
	layoutMenu->AddSeparatorItem();
	fOwnSetupItem = new BMenuItem("Your own setup", NULL);
	fOwnSetupItem->SetEnabled(false);
	layoutMenu->AddItem(fOwnSetupItem);
	fLayoutField = new BMenuField("layout field", "Layout", layoutMenu);
	fLayoutField->SetViewColor(network_box_fill());
	fLayoutField->SetLowColor(network_box_fill());

	// The labels: what is printed on the face buttons.
	BPopUpMenu* menu = new BPopUpMenu("labels");
	BMessage* xbox = new BMessage(kMsgLabelsChosen);
	xbox->AddInt32("labels", B_GAME_CONTROLLER_LABELS_XBOX);
	fXboxItem = new BMenuItem("Xbox (A B X Y)", xbox);
	BMessage* playstation = new BMessage(kMsgLabelsChosen);
	playstation->AddInt32("labels", B_GAME_CONTROLLER_LABELS_PLAYSTATION);
	fPlayStationItem = new BMenuItem(
		"PlayStation (\xe2\x9c\x95 \xe2\x97\x8b \xe2\x96\xa1 \xe2\x96\xb3)",
		playstation);
	menu->AddItem(fXboxItem);
	menu->AddItem(fPlayStationItem);
	fLabelsField = new BMenuField("labels field", "Button labels", menu);
	fLabelsField->SetViewColor(network_box_fill());
	fLabelsField->SetLowColor(network_box_fill());

	fSetupButton = new BButton("setup buttons", "Set up buttons\xe2\x80\xa6",
		new BMessage(kMsgSetupButtons));

	fIDs = new BStringView("controller ids", "");
	fIDs->SetHighColor(quiet_color(140));
	fIDs->SetViewColor(network_box_fill());
	fIDs->SetLowColor(network_box_fill());

	fTest = new BGameControllerView("controller test");
	fTest->SetViewColor(network_box_fill());
	fTest->SetLowColor(network_box_fill());
	fTest->SetExplicitMinSize(BSize(roundf(300 * unit), roundf(170 * unit)));
	fTest->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));

	fTestHint = new BStringView("test hint",
		"Press a button or move a stick: it lights up here.");
	fTestHint->SetAlignment(B_ALIGN_CENTER);
	fTestHint->SetHighColor(quiet_color(160));
	fTestHint->SetViewColor(network_box_fill());
	fTestHint->SetLowColor(network_box_fill());
	fTestHint->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	FilledBox* testBox = new FilledBox("test box");
	testBox->SetLabel(new BoxTitle("Test"));
	BGroupView* test = new BGroupView(B_VERTICAL, roundf(8 * unit));
	test->SetViewColor(network_box_fill());
	BLayoutBuilder::Group<>(test)
		.SetInsets(roundf(14 * unit), roundf(8 * unit), roundf(14 * unit),
			roundf(12 * unit))
		.AddGrid(roundf(8 * unit), roundf(6 * unit))
			.Add(fLayoutField->CreateLabelLayoutItem(), 0, 0)
			.Add(fLayoutField->CreateMenuBarLayoutItem(), 1, 0)
			.Add(BSpaceLayoutItem::CreateGlue(), 2, 0)
			.Add(fIDs, 3, 0)
			.Add(fLabelsField->CreateLabelLayoutItem(), 0, 1)
			.Add(fLabelsField->CreateMenuBarLayoutItem(), 1, 1)
			.Add(BSpaceLayoutItem::CreateGlue(), 2, 1)
			.Add(fSetupButton, 3, 1)
		.End()
		.Add(fTest, 1)
		.Add(fTestHint);
	testBox->AddChild(test);

	BLayoutBuilder::Group<>(this)
		.AddGroup(B_HORIZONTAL, roundf(12 * unit), 1)
			.Add(left)
			.Add(testBox, 1)
		.End();

	_Rebuild();
}


GameControllersPage::~GameControllersPage()
{
	_CloseSetup();
	if (fModule != NULL)
		fModule->PageGone(this);
}


void
GameControllersPage::AttachedToWindow()
{
	BGroupView::AttachedToWindow();
	fLayoutField->Menu()->SetTargetForItems(this);
	fLabelsField->Menu()->SetTargetForItems(this);
	fSetupButton->SetTarget(this);
	LinkControl* bluetooth = dynamic_cast<LinkControl*>(
		FindView("bluetooth link"));
	if (bluetooth != NULL)
		bluetooth->SetTarget(this);
	BGameController::StartWatching(BMessenger(this));
	// The battery of the chosen controller, now and then.
	BMessage tick(kMsgBatteryTick);
	fBatteryRunner = new BMessageRunner(BMessenger(this), &tick, 10000000);
	// A controller plugged in while the page was away.
	_Rebuild();
}


void
GameControllersPage::DetachedFromWindow()
{
	// Nothing of libr5game.so and of this add-on may keep running once the
	// page is gone: Settings unloads both (CHANGELOG.md, 17 Nov 2026).
	_CloseSetup();
	BGameController::StopWatching(BMessenger(this));
	delete fBatteryRunner;
	fBatteryRunner = NULL;
	fTest->SetDevice(NULL);
	BGroupView::DetachedFromWindow();
}


void
GameControllersPage::_CloseSetup()
{
	// The setup window's code is in this add-on: it goes before the page.
	BLooper* looper = NULL;
	if (fSetup.IsValid() && fSetup.LockTarget()) {
		fSetup.Target(&looper);
		if (looper != NULL)
			looper->Quit();
	}
	fSetup = BMessenger();
}


void
GameControllersPage::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_GAME_CONTROLLER_ADDED:
		case B_GAME_CONTROLLER_REMOVED:
			_Rebuild();
			break;

		case kMsgSelectController:
		{
			const char* device;
			if (message->FindString("device", &device) == B_OK)
				_Select(device);
			break;
		}

		case kMsgLayoutChosen:
		{
			int32 layout;
			BGameController* controller = fTest->Controller();
			if (controller == NULL || controller->VendorID() == 0
				|| message->FindInt32("layout", &layout) != B_OK)
				break;
			fModule->SetLayout(controller->VendorID(),
				controller->ProductID(), (game_controller_family)layout);
			_Rebuild();
			break;
		}

		case kMsgLabelsChosen:
		{
			int32 labels;
			BGameController* controller = fTest->Controller();
			if (controller == NULL || controller->VendorID() == 0
				|| message->FindInt32("labels", &labels) != B_OK)
				break;
			// The family's own labels are stored as Automatic, so a
			// later Defaults has nothing to undo for them.
			game_controller_labels own
				= controller->Family() == B_GAME_CONTROLLER_FAMILY_PLAYSTATION
					? B_GAME_CONTROLLER_LABELS_PLAYSTATION
					: B_GAME_CONTROLLER_LABELS_XBOX;
			game_controller_labels choice = (game_controller_labels)labels;
			fModule->SetLabels(controller->VendorID(),
				controller->ProductID(),
				choice == own ? B_GAME_CONTROLLER_LABELS_AUTOMATIC : choice);
			_UpdateTest();
			break;
		}

		case kMsgSetupButtons:
		{
			BGameController* controller = fTest->Controller();
			if (controller == NULL || controller->VendorID() == 0
				|| fSetup.IsValid())
				break;
			ButtonSetupWindow* window = new ButtonSetupWindow(Window(),
				BMessenger(this), fSelected.String(), controller->Family(),
				controller->LabelStyle());
			fSetup = BMessenger(window);
			window->Show();
			break;
		}

		case kMsgSetupDone:
		{
			const char* mapping;
			int32 vendor;
			int32 product;
			if (message->FindString("mapping", &mapping) == B_OK
				&& message->FindInt32("vendor", &vendor) == B_OK
				&& message->FindInt32("product", &product) == B_OK
				&& vendor != 0) {
				fModule->SetMapping((uint16)vendor, (uint16)product,
					mapping);
				_Rebuild();
			}
			break;
		}

		case kMsgBatteryTick:
		{
			ControllerCard* card = _SelectedCard();
			if (card != NULL && fTest->Controller() != NULL)
				card->SetBattery(fTest->Controller()->BatteryLevel());
			break;
		}

		case kMsgOpenBluetooth:
			if (fContext != NULL)
				fContext->OpenPage("bluetooth");
			break;

		default:
			BGroupView::MessageReceived(message);
			break;
	}
}


void
GameControllersPage::Refresh()
{
	// Revert or Defaults changed what Open() reads: open everything again.
	_Rebuild();
}


void
GameControllersPage::_Rebuild()
{
	// The test closes its controller first, so every card can open each
	// controller once for its name and layout.
	BString keep = fSelected;
	fTest->SetDevice(NULL);

	for (size_t i = 0; i < fCardList.size(); i++) {
		fCardList[i]->RemoveSelf();
		delete fCardList[i];
	}
	fCardList.clear();
	if (fEmpty->Parent() != NULL)
		fEmpty->RemoveSelf();

	BGameController probe;
	probe.RescanDevices();
	for (int32 i = 0; i < probe.CountDevices(); i++) {
		char device[B_OS_NAME_LENGTH];
		if (probe.GetDeviceName(i, device, sizeof(device)) != B_OK)
			continue;
		BGameController controller;
		if (controller.Open(device) != B_OK)
			continue;
		controller_entry entry;
		entry.device = device;
		entry.name = controller.Name();
		entry.vendor = controller.VendorID();
		entry.product = controller.ProductID();
		entry.connection = controller.Connection();
		entry.family = controller.Family();
		entry.battery = controller.BatteryLevel();
		controller.Close();
		fModule->Remember(entry.vendor, entry.product);
		ControllerCard* card = new ControllerCard(entry, this);
		fCardList.push_back(card);
		fCards->GroupLayout()->AddView(card);
	}
	if (fCardList.empty())
		fCards->GroupLayout()->AddView(fEmpty);

	bool found = false;
	for (size_t i = 0; i < fCardList.size(); i++) {
		if (fCardList[i]->Entry().device == keep)
			found = true;
	}
	if (!found)
		keep = fCardList.empty() ? "" : fCardList[0]->Entry().device;
	fSelected = "";
	_Select(keep.String());
}


void
GameControllersPage::_Select(const char* device)
{
	fSelected = device != NULL ? device : "";
	for (size_t i = 0; i < fCardList.size(); i++)
		fCardList[i]->SetSelected(fCardList[i]->Entry().device == fSelected);
	fTest->SetDevice(fSelected.Length() > 0 ? fSelected.String() : NULL);
	if (fTest->Controller() == NULL)
		fTest->SetFamily(B_GAME_CONTROLLER_FAMILY_UNKNOWN);
	BGameController* controller = fTest->Controller();
	if (controller != NULL)
		fModule->SetSelected(controller->VendorID(), controller->ProductID());
	else
		fModule->SetSelected(0, 0);
	_UpdateTest();
}


ControllerCard*
GameControllersPage::_SelectedCard() const
{
	for (size_t i = 0; i < fCardList.size(); i++) {
		if (fCardList[i]->Entry().device == fSelected)
			return fCardList[i];
	}
	return NULL;
}


void
GameControllersPage::_UpdateTest()
{
	BGameController* controller = fTest->Controller();
	if (controller == NULL) {
		fLayoutField->SetEnabled(false);
		fLayoutItems[0]->SetMarked(true);
		fLabelsField->SetEnabled(false);
		fXboxItem->SetMarked(true);
		fSetupButton->SetEnabled(false);
		fIDs->SetText("");
		fTestHint->SetText(fCardList.empty()
			? "Plug in a controller or pair one to test it here."
			: "This controller cannot be opened.");
		return;
	}

	ControllerCard* card = _SelectedCard();
	if (card != NULL)
		card->SetBattery(controller->BatteryLevel());

	// A choice is stored per model, so it needs the vendor and product ID.
	bool known = controller->VendorID() != 0;
	fLayoutField->SetEnabled(known);
	fSetupButton->SetEnabled(known);
	if (controller->HasSavedMapping())
		fOwnSetupItem->SetMarked(true);
	else {
		game_controller_family layout = BGameController::LayoutFor(
			controller->VendorID(), controller->ProductID());
		for (int32 i = 0; i < 5; i++) {
			if (kLayouts[i].family == layout)
				fLayoutItems[i]->SetMarked(true);
		}
	}

	game_controller_family family = controller->Family();
	bool choosable = known && family != B_GAME_CONTROLLER_FAMILY_SEGA_GENESIS
		&& family != B_GAME_CONTROLLER_FAMILY_UNKNOWN;
	fLabelsField->SetEnabled(choosable);
	game_controller_labels labels = controller->LabelStyle();
	if (labels == B_GAME_CONTROLLER_LABELS_PLAYSTATION)
		fPlayStationItem->SetMarked(true);
	else
		fXboxItem->SetMarked(true);
	fTest->SetLabels(labels);

	if (known) {
		BString ids;
		ids.SetToFormat("Vendor %04X \xc2\xb7 Product %04X",
			controller->VendorID(), controller->ProductID());
		fIDs->SetText(ids.String());
	} else
		fIDs->SetText("");
	fTestHint->SetText(controller->HasMapping()
		? "Press a button or move a stick: it lights up here."
		: "Choose a layout, or set up the buttons one by one.");
}


// An add-on (settings_gamecontrollers), like settings_bluetooth: it links
// libr5game.so.
extern "C" _EXPORT int32
settings_api_version()
{
	return kSettingsApiVersion;
}


extern "C" _EXPORT SettingsModule*
instantiate_settings_module()
{
	return new GameControllersModule();
}
