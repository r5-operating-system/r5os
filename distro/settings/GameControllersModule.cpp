/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Settings, Devices, Game controllers (docs/GAME-CONTROLLERS.md, option B;
 * the picture is docs/design/gamecontrollers-mockup, layout A). The
 * controllers are a BCardStrip at the bottom of the page, one card each;
 * above it the live test of the chosen one with its layout, its button
 * labels and Set up buttons… (a window that asks for each control in
 * turn). The pictures are BGameControllerView from libr5game.so. Without a
 * controller the page shows a gray example and a dashed card. Every
 * choice is stored per controller model by BGameController as soon as it
 * changes; Revert puts back the choices from when the page was opened,
 * Defaults gives every controller its own labels again and removes the
 * chosen controller's layout and button setup. No administrator is needed:
 * the choice is the person's own.
 *
 * Beside the picture (under it in a narrow window) are Rumble, Motion and
 * Trigger feedback (option C, phase 1, after feedback-*.png in the
 * mockup), each shown only when the chosen controller has it, and all
 * three gray without a controller. Nothing of it is stored: Close() stops the
 * rumble and frees the triggers when another controller is chosen or the
 * page goes.
 */


#include "NetworkPage.h"
#include "SettingsFont.h"
#include "SettingsMessages.h"
#include "SettingsModule.h"

#include <Bitmap.h>
#include <Box.h>
#include <Button.h>
#include <CardStrip.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <PopUpMenu.h>
#include <ScrollBar.h>
#include <Slider.h>
#include <SpaceLayoutItem.h>
#include <String.h>
#include <StringView.h>
#include <TextView.h>
#include <Window.h>

#include <GameController.h>
#include <GameControllerMotionView.h>
#include <GameControllerView.h>
#include <R5Icons.h>

#include <math.h>
#include <stdlib.h>

#include <algorithm>

#include <map>
#include <string>
#include <vector>


static const uint32 kMsgSelectController = 'gcSe';
static const uint32 kMsgLabelsChosen = 'gcLb';
static const uint32 kMsgOpenBluetooth = 'gcBt';
static const uint32 kMsgBatteryTick = 'gcBa';
static const uint32 kMsgRumble = 'gcRu';
static const uint32 kMsgFeelTick = 'gcFt';
static const uint32 kMsgTriggerChanged = 'gcTr';


// A text on the page that turns gray without a controller, and how to
// give it its own colour back.
struct quiet_text {
	BStringView*	view;
	rgb_color		color;
	bool			panelText;	// it follows B_PANEL_TEXT_COLOR
};


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


// The second line of a controller's card: how it is connected, and the
// battery when the controller reports it.
static BString
card_subtitle(const controller_entry& entry)
{
	BString text(connection_text(entry.connection));
	if (entry.battery >= 0)
		text << " \xc2\xb7 " << entry.battery << " %";
	return text;
}


static void
set_shown(BView* view, bool shown)
{
	if (shown && view->IsHidden(view))
		view->Show();
	else if (!shown && !view->IsHidden(view))
		view->Hide();
}


// #pragma mark - UpperArea


class UpperArea;


class UpperBar : public BScrollBar {
public:
	UpperBar(UpperArea* owner);

	virtual void ValueChanged(float newValue);

private:
	UpperArea*	fOwner;
};


/*!	The page above the card strip. It scrolls by itself, so the strip
	stays at the bottom of the page; the window's own page scroller
	(SettingsWindow.cpp) would move the strip with it. Where the picture
	and the feel groups do not fit side by side, the groups go under the
	picture (fSplit turns vertical) and the area scrolls.
*/
class UpperArea : public BView {
public:
	UpperArea(BView* content, BGroupLayout* split, BView* first,
		BView* second)
		:
		BView("upper area", B_WILL_DRAW | B_FRAME_EVENTS),
		fContent(content),
		fSplit(split),
		fFirst(first),
		fSecond(second),
		fBar(NULL),
		fBusy(false)
	{
		SetFlags(Flags() | B_SUPPORTS_LAYOUT);
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		AddChild(fContent);
		fBar = new UpperBar(this);
		AddChild(fBar);
		fBar->Hide();
	}

	// Small, so the window never scrolls the page itself.
	virtual BSize MinSize()
	{
		float unit = network_unit();
		return BSize(roundf(280 * unit), roundf(80 * unit));
	}

	virtual BSize MaxSize()
	{
		return BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED);
	}

	virtual BSize PreferredSize()
	{
		return MinSize();
	}

	void ScrollToValue(float value)
	{
		if (!fBusy)
			fContent->MoveTo(0, -value);
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == B_MOUSE_WHEEL_CHANGED && !fBar->IsHidden()) {
			float delta = 0;
			message->FindFloat("be:wheel_delta_y", &delta);
			fBar->SetValue(fBar->Value()
				+ delta * roundf(40 * network_unit()));
			return;
		}
		BView::MessageReceived(message);
	}

	virtual void DoLayout()
	{
		if (fBusy)
			return;
		fBusy = true;
		float width = Bounds().Width();
		float height = Bounds().Height();
		float barWidth = fBar->PreferredSize().width;
		float contentWidth = width;
		_Arrange(contentWidth);
		float contentHeight = _ContentHeight();
		bool overflow = contentHeight > height + 1.0f;
		if (overflow) {
			contentWidth = std::max(0.0f, width - barWidth - 1.0f);
			_Arrange(contentWidth);
			contentHeight = _ContentHeight();
			overflow = contentHeight > height + 1.0f;
		}
		if (!overflow)
			contentHeight = height;

		float maxValue = overflow ? contentHeight - height : 0;
		float value = std::max(0.0f, std::min(fBar->Value(), maxValue));
		fContent->MoveTo(0, -value);
		fContent->ResizeTo(contentWidth, contentHeight);
		fContent->InvalidateLayout(true);
		fContent->Layout(true);
		if (overflow) {
			set_shown(fBar, true);
			fBar->MoveTo(width - barWidth, 0);
			fBar->ResizeTo(barWidth, height);
			fBar->SetRange(0, maxValue);
			fBar->SetSteps(roundf(20 * network_unit()), height);
			fBar->SetProportion(height / contentHeight);
			if (fBar->Value() != value)
				fBar->SetValue(value);
		} else {
			fBar->SetRange(0, 0);
			set_shown(fBar, false);
		}
		fBusy = false;
	}

private:
	// Side by side when the picture at its smallest and the groups as
	// they like to be fit, else one above the other.
	void _Arrange(float width)
	{
		float unit = network_unit();
		float need = fFirst->MinSize().width
			+ fSecond->PreferredSize().width + fSplit->Spacing()
			+ roundf(36 * unit);
		orientation wanted = width >= need ? B_HORIZONTAL : B_VERTICAL;
		if (fSplit->Orientation() != wanted)
			fSplit->SetOrientation(wanted);
	}

	// The height the content needs at least: where it fits, the layout
	// shares out the rest.
	float _ContentHeight()
	{
		fContent->InvalidateLayout(true);
		return fContent->MinSize().height;
	}

	BView*			fContent;
	BGroupLayout*	fSplit;
	BView*			fFirst;
	BView*			fSecond;
	UpperBar*		fBar;
	bool			fBusy;
};


UpperBar::UpperBar(UpperArea* owner)
	:
	BScrollBar(BRect(0, 0, 14, 14), "upper bar", NULL, 0, 0, B_VERTICAL),
	fOwner(owner)
{
}


void
UpperBar::ValueChanged(float newValue)
{
	if (fOwner != NULL)
		fOwner->ScrollToValue(newValue);
	SetValue(newValue);
}


// #pragma mark - ExamplePicture


/*!	Without a controller: the Xbox Series shape in gray, nothing lit, as an
	example of what the test shows (BGameControllerView::PictureBitmap()).
*/
class ExamplePicture : public BView {
public:
	ExamplePicture()
		:
		BView("example picture", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
		fBitmap(NULL)
	{
		float unit = network_unit();
		SetExplicitMinSize(BSize(roundf(300 * unit), roundf(170 * unit)));
		SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
	}

	virtual ~ExamplePicture()
	{
		delete fBitmap;
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == B_COLORS_UPDATED) {
			delete fBitmap;
			fBitmap = NULL;
			Invalidate();
		}
		BView::MessageReceived(message);
	}

	virtual void Draw(BRect updateRect)
	{
		(void)updateRect;
		BRect bounds = Bounds();
		// The live picture keeps a band above the drawing for the trigger
		// caps; the example keeps the same place.
		BRect area = bounds.InsetByCopy(roundf(bounds.Width() * 0.04f),
			roundf(bounds.Height() * 0.06f));
		area.top += roundf(bounds.Height() * 0.06f);
		if (!area.IsValid())
			return;
		if (fBitmap == NULL
			|| fBitmap->Bounds().IntegerWidth() != area.IntegerWidth()
			|| fBitmap->Bounds().IntegerHeight() != area.IntegerHeight()) {
			delete fBitmap;
			rgb_color line = mix_color(ViewColor(),
				ui_color(B_PANEL_TEXT_COLOR), 100);
			fBitmap = BGameControllerView::PictureBitmap(0, 0,
				B_GAME_CONTROLLER_FAMILY_XBOX, area.IntegerWidth() + 1,
				area.IntegerHeight() + 1, line);
		}
		if (fBitmap == NULL)
			return;
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		DrawBitmap(fBitmap, area.LeftTop());
		SetDrawingMode(B_OP_COPY);
	}

private:
	BBitmap*	fBitmap;
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
		// An axis question looks at the axes first: an analog trigger
		// often sets a button as well.
		if (step.axis)
			_CatchAxis(step, axes, caught);
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
		if (!step.axis)
			_CatchAxis(step, axes, caught);
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

	// The first axis moved more than half way from where it rested.
	void _CatchAxis(const setup_step& step, const int16* axes,
		source& caught) const
	{
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


// #pragma mark - RumbleWaves


/*!	Three small arcs beside the Rumble buttons while the controller
	rumbles, in the accent colour.
*/
class RumbleWaves : public BView {
public:
	RumbleWaves()
		:
		BView("rumble waves", B_WILL_DRAW),
		fActive(false)
	{
		float unit = network_unit();
		SetExplicitMinSize(BSize(roundf(30 * unit), roundf(24 * unit)));
		SetExplicitMaxSize(BSize(roundf(30 * unit), roundf(24 * unit)));
	}

	void SetActive(bool active)
	{
		if (active == fActive)
			return;
		fActive = active;
		Invalidate();
	}

	virtual void Draw(BRect updateRect)
	{
		(void)updateRect;
		if (!fActive)
			return;
		BRect bounds = Bounds();
		float unit = network_unit();
		SetDrawingMode(B_OP_ALPHA);
		SetHighColor(ui_color(B_CONTROL_HIGHLIGHT_COLOR));
		SetPenSize(std::max(1.5f, 2 * unit));
		BPoint center(bounds.left, (bounds.top + bounds.bottom) / 2);
		for (int i = 0; i < 3; i++) {
			float radius = (6 + 7 * i) * unit;
			StrokeArc(center, radius, radius * 1.1f, -40, 80);
		}
		SetPenSize(1);
		SetDrawingMode(B_OP_COPY);
	}

private:
	bool	fActive;
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
			int32				_SelectedIndex() const;
			void				_UpdateBattery(int32 battery);
			void				_UpdatePictures();
			void				_UpdateFeel();
			void				_ApplyTrigger();
			BView*				_MakeFeelGroup(const char* name,
									const char* title,
									BGroupView** content);
			void				_AddQuiet(BStringView* view);

			GameControllersModule* fModule;
			SettingsContext*	fContext;
			BAppearanceKit::BCardStrip* fStrip;
			std::vector<controller_entry> fEntries;
			BString				fSelected;
			ExamplePicture*		fExample;
			// Text that turns gray without a controller, with its colour.
			std::vector<quiet_text> fQuiet;

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

			// Rumble, Motion and Trigger feedback.
			BView*				fRumbleGroup;
			BButton*			fLightButton;
			BButton*			fStrongButton;
			BButton*			fTriggerButton;
			RumbleWaves*		fWaves;
			bigtime_t			fRumbleUntil;
			BView*				fMotionGroup;
			BGameControllerMotionView* fMotion;
			BView*				fTriggerGroup;
			BMenuField*			fEffectField;
			BMenuItem*			fEffectItems[4];
			BSlider*			fStartSlider;
			BStringView*		fStartValue;
			BSlider*			fStrengthSlider;
			BStringView*		fStrengthValue;
			BMessageRunner*		fFeelRunner;
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
									// Before anything changes, for Revert.
									Remember(fSelectedVendor,
										fSelectedProduct);
									status_t status
										= BGameController::ResetLabelStyles();
									std::map<uint32, model_choice>::iterator
										i;
									for (i = fCurrent.begin();
											i != fCurrent.end(); i++)
										i->second.labels
											= B_GAME_CONTROLLER_LABELS_AUTOMATIC;
									if (fSelectedVendor != 0) {
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
	fBatteryRunner(NULL),
	fRumbleUntil(0),
	fFeelRunner(NULL)
{
	float unit = network_unit();

	// The controllers: a card each, in a strip at the bottom. Without one,
	// a dashed card that leads to Bluetooth.
	fStrip = new BAppearanceKit::BCardStrip("controllers",
		new BMessage(kMsgSelectController));
	fStrip->SetEmptyText("No controller",
		"Plug one in with USB, or pair one in Bluetooth.");
	fStrip->SetEmptyAction("Open Bluetooth\xe2\x80\xa6",
		new BMessage(kMsgOpenBluetooth));

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
	fXboxItem = new BMenuItem("Xbox", xbox);
	BMessage* playstation = new BMessage(kMsgLabelsChosen);
	playstation->AddInt32("labels", B_GAME_CONTROLLER_LABELS_PLAYSTATION);
	// Short, as in the mockup: the menus, Set up buttons and the gap
	// between them fit one row at the smallest window.
	fPlayStationItem = new BMenuItem("PlayStation", playstation);
	menu->AddItem(fXboxItem);
	menu->AddItem(fPlayStationItem);
	fLabelsField = new BMenuField("labels field", "Button labels", menu);
	fLabelsField->SetViewColor(network_box_fill());
	fLabelsField->SetLowColor(network_box_fill());

	fSetupButton = new BButton("setup buttons", "Set up buttons\xe2\x80\xa6",
		new BMessage(kMsgSetupButtons));

	// Under the picture, not beside Layout: at the smallest window (910 by
	// 560) the two do not fit in one row.
	fIDs = new BStringView("controller ids", "");
	fIDs->SetAlignment(B_ALIGN_CENTER);
	fIDs->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
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

	// Rumble, Motion and Trigger feedback (option C, phase 1), under the
	// picture, so the page keeps its width at the smallest window.
	BFont boldFont(r5_settings_bold_font());
	rgb_color quietText = quiet_color(150);

	BMessage* light = new BMessage(kMsgRumble);
	light->AddFloat("strong", 0.2f);
	light->AddFloat("weak", 0.5f);
	fLightButton = new BButton("rumble light", "Light", light);
	BMessage* strong = new BMessage(kMsgRumble);
	strong->AddFloat("strong", 1.0f);
	strong->AddFloat("weak", 1.0f);
	fStrongButton = new BButton("rumble strong", "Strong", strong);
	// Xbox controllers: the motors in the triggers (phase 2), a button only
	// while the chosen controller has them.
	BMessage* triggerRumble = new BMessage(kMsgRumble);
	triggerRumble->AddBool("triggers", true);
	fTriggerButton = new BButton("rumble triggers", "Triggers",
		triggerRumble);
	fWaves = new RumbleWaves();
	BStringView* rumbleHint = new BStringView("rumble hint",
		"One second, both motors.");
	rumbleHint->SetHighColor(quietText);
	BGroupView* content;
	fRumbleGroup = _MakeFeelGroup("rumble group", "Rumble", &content);
	BLayoutBuilder::Group<>(content)
		.AddGroup(B_HORIZONTAL, roundf(8 * unit))
			.Add(fLightButton)
			.Add(fStrongButton)
			.Add(fTriggerButton)
			.Add(fWaves)
			.AddGlue()
		.End()
		.Add(rumbleHint);

	fMotion = new BGameControllerMotionView("motion view");
	fMotionGroup = _MakeFeelGroup("motion group", "Motion", &content);
	BLayoutBuilder::Group<>(content)
		.Add(fMotion);

	static const char* const kEffects[4] = { "Off", "Resistance",
		"Trigger click", "Vibration" };
	BPopUpMenu* effectMenu = new BPopUpMenu("effect");
	for (int32 i = 0; i < 4; i++) {
		BMessage* message = new BMessage(kMsgTriggerChanged);
		message->AddInt32("effect", i);
		fEffectItems[i] = new BMenuItem(kEffects[i], message);
		effectMenu->AddItem(fEffectItems[i]);
	}
	fEffectItems[0]->SetMarked(true);
	fEffectField = new BMenuField("effect field", NULL, effectMenu);
	BStringView* effectLabel = new BStringView("effect label", "Effect");
	effectLabel->SetFont(&boldFont);
	BStringView* effectWhere = new BStringView("effect where",
		"pull L2 or R2 to feel it");
	effectWhere->SetHighColor(quietText);
	fStartSlider = new BSlider("starts at", NULL,
		new BMessage(kMsgTriggerChanged), 0, 100, B_HORIZONTAL);
	fStartSlider->SetModificationMessage(new BMessage(kMsgTriggerChanged));
	fStartSlider->SetValue(40);
	fStrengthSlider = new BSlider("strength", NULL,
		new BMessage(kMsgTriggerChanged), 0, 100, B_HORIZONTAL);
	fStrengthSlider->SetModificationMessage(
		new BMessage(kMsgTriggerChanged));
	fStrengthSlider->SetValue(70);
	BStringView* startLabel = new BStringView("start label", "Starts at");
	startLabel->SetFont(&boldFont);
	BStringView* strengthLabel = new BStringView("strength label",
		"Strength");
	strengthLabel->SetFont(&boldFont);
	fStartValue = new BStringView("start value", "40 %");
	fStartValue->SetFont(&boldFont);
	fStrengthValue = new BStringView("strength value", "70 %");
	fStrengthValue->SetFont(&boldFont);
	float valueWidth = ceilf(boldFont.StringWidth("100 %"));
	fStartValue->SetExplicitMinSize(BSize(valueWidth, B_SIZE_UNSET));
	fStrengthValue->SetExplicitMinSize(BSize(valueWidth, B_SIZE_UNSET));
	fTriggerGroup = _MakeFeelGroup("trigger group", "Trigger feedback",
		&content);
	BLayoutBuilder::Group<>(content)
		.AddGrid(roundf(8 * unit), roundf(4 * unit))
			.Add(effectLabel, 0, 0)
			.AddGroup(B_HORIZONTAL, roundf(8 * unit), 1, 0, 2)
				.Add(fEffectField)
				.Add(effectWhere)
				.AddGlue()
			.End()
			.Add(startLabel, 0, 1)
			.Add(fStartSlider, 1, 1)
			.Add(fStartValue, 2, 1)
			.Add(strengthLabel, 0, 2)
			.Add(fStrengthSlider, 1, 2)
			.Add(fStrengthValue, 2, 2)
		.End();

	BView* feelViews[] = { fWaves, fMotion, fEffectField, effectLabel,
		effectWhere, fStartSlider, fStrengthSlider, startLabel,
		strengthLabel, fStartValue, fStrengthValue,
		rumbleHint };
	for (size_t i = 0; i < sizeof(feelViews) / sizeof(feelViews[0]); i++) {
		feelViews[i]->SetViewColor(network_box_fill());
		feelViews[i]->SetLowColor(network_box_fill());
	}

	BView* quietViews[] = { rumbleHint, effectLabel, effectWhere, startLabel,
		strengthLabel, fStartValue, fStrengthValue };
	for (size_t i = 0; i < sizeof(quietViews) / sizeof(quietViews[0]); i++)
		_AddQuiet(static_cast<BStringView*>(quietViews[i]));

	fExample = new ExamplePicture();
	fExample->SetViewColor(network_box_fill());
	fExample->SetLowColor(network_box_fill());

	// Layout A of the mockup: the menus in one row, the picture at the
	// left, Rumble, Motion and Trigger feedback at the right. UpperArea
	// puts the groups under the picture where they do not fit beside it.
	BGroupView* pictureColumn = new BGroupView(B_VERTICAL, roundf(6 * unit));
	pictureColumn->SetViewColor(network_box_fill());
	BLayoutBuilder::Group<>(pictureColumn)
		.Add(fTest, 1)
		.Add(fExample, 1)
		.Add(fTestHint)
		.Add(fIDs);
	BGroupView* feelColumn = new BGroupView(B_VERTICAL, roundf(2 * unit));
	feelColumn->SetViewColor(network_box_fill());
	BLayoutBuilder::Group<>(feelColumn)
		.Add(fRumbleGroup)
		.Add(fMotionGroup)
		.Add(fTriggerGroup)
		.AddGlue();
	BGroupView* split = new BGroupView(B_HORIZONTAL, roundf(14 * unit));
	split->SetViewColor(network_box_fill());
	split->GroupLayout()->AddView(pictureColumn, 3);
	split->GroupLayout()->AddView(feelColumn, 2);

	FilledBox* testBox = new FilledBox("test box");
	// Tight at the top and the bottom, so the page fits at 1030 by 610
	// with a controller and does not scroll.
	BGroupView* test = new BGroupView(B_VERTICAL, roundf(6 * unit));
	test->SetViewColor(network_box_fill());
	BLayoutBuilder::Group<>(test)
		.SetInsets(roundf(14 * unit), roundf(6 * unit), roundf(14 * unit),
			roundf(6 * unit))
		.AddGroup(B_HORIZONTAL, roundf(8 * unit))
			.Add(fLayoutField->CreateLabelLayoutItem())
			.Add(fLayoutField->CreateMenuBarLayoutItem())
			.AddStrut(roundf(6 * unit))
			.Add(fLabelsField->CreateLabelLayoutItem())
			.Add(fLabelsField->CreateMenuBarLayoutItem())
			.AddGlue()
			.Add(fSetupButton)
		.End()
		.Add(split, 1);
	testBox->AddChild(test);

	UpperArea* upper = new UpperArea(testBox, split->GroupLayout(),
		pictureColumn, feelColumn);

	// The strip stays at the bottom, as wide as the page.
	GroupLayout()->SetSpacing(roundf(8 * unit));
	BLayoutBuilder::Group<>(this)
		.Add(upper, 1)
		.Add(fStrip);

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
	fLightButton->SetTarget(this);
	fStrongButton->SetTarget(this);
	fTriggerButton->SetTarget(this);
	fEffectField->Menu()->SetTargetForItems(this);
	fStartSlider->SetTarget(this);
	fStrengthSlider->SetTarget(this);
	BMessage feelTick(kMsgFeelTick);
	fFeelRunner = new BMessageRunner(BMessenger(this), &feelTick, 33000);
	fStrip->SetTarget(this);
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
	delete fFeelRunner;
	fFeelRunner = NULL;
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
			int32 index;
			if (message->FindInt32("index", &index) == B_OK && index >= 0
				&& index < (int32)fEntries.size())
				_Select(fEntries[index].device.String());
			break;
		}

		case B_COLORS_UPDATED:
			// The card pictures are drawn in the text colour.
			_UpdatePictures();
			BGroupView::MessageReceived(message);
			break;

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
			if (fTest->Controller() != NULL)
				_UpdateBattery(fTest->Controller()->BatteryLevel());
			break;

		case kMsgRumble:
		{
			float strongMotor;
			float weakMotor;
			BGameController* controller = fTest->Controller();
			if (controller != NULL && message->GetBool("triggers", false)) {
				if (controller->SetTriggerRumble(0.6f, 0.6f, 1000000)
						== B_OK) {
					fRumbleUntil = system_time() + 1000000;
					fWaves->SetActive(true);
				}
				break;
			}
			if (controller == NULL || !controller->CanRumble()
				|| message->FindFloat("strong", &strongMotor) != B_OK
				|| message->FindFloat("weak", &weakMotor) != B_OK)
				break;
			if (controller->SetRumble(strongMotor, weakMotor, 1000000)
					== B_OK) {
				fRumbleUntil = system_time() + 1000000;
				fWaves->SetActive(true);
			}
			break;
		}

		case kMsgTriggerChanged:
			_ApplyTrigger();
			break;

		case kMsgFeelTick:
		{
			if (fRumbleUntil != 0 && system_time() >= fRumbleUntil) {
				fRumbleUntil = 0;
				fWaves->SetActive(false);
			}
			// The picture reads the controller (Update()); this only shows
			// the last motion sample.
			BGameController* controller = fTest->Controller();
			game_controller_motion motion;
			if (controller != NULL && controller->IsMotionEnabled()
				&& controller->GetMotion(&motion) == B_OK)
				fMotion->SetMotion(motion);
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

	fStrip->MakeEmpty();
	fEntries.clear();

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
		fEntries.push_back(entry);
		fStrip->AddItem(new BAppearanceKit::BCardStripItem(entry.name,
			card_subtitle(entry)));
	}
	_UpdatePictures();

	bool found = false;
	for (size_t i = 0; i < fEntries.size(); i++) {
		if (fEntries[i].device == keep)
			found = true;
	}
	if (!found)
		keep = fEntries.empty() ? "" : fEntries[0].device;
	fSelected = "";
	_Select(keep.String());
}


// Each card's picture: the controller's model, small, in the text colour.
void
GameControllersPage::_UpdatePictures()
{
	BSize size = fStrip->PictureSize();
	rgb_color line = mix_color(ui_color(B_DOCUMENT_BACKGROUND_COLOR),
		ui_color(B_DOCUMENT_TEXT_COLOR), 190);
	for (size_t i = 0; i < fEntries.size(); i++) {
		BAppearanceKit::BCardStripItem* item = fStrip->ItemAt(i);
		if (item == NULL)
			continue;
		BBitmap* picture = BGameControllerView::PictureBitmap(
			fEntries[i].vendor, fEntries[i].product, fEntries[i].family,
			size.width, size.height, line);
		item->SetPicture(picture);
		delete picture;
	}
}


void
GameControllersPage::_Select(const char* device)
{
	fSelected = device != NULL ? device : "";
	fStrip->Select(_SelectedIndex());
	fTest->SetDevice(fSelected.Length() > 0 ? fSelected.String() : NULL);
	if (fTest->Controller() == NULL)
		fTest->SetFamily(B_GAME_CONTROLLER_FAMILY_UNKNOWN);
	BGameController* controller = fTest->Controller();
	if (controller != NULL)
		fModule->SetSelected(controller->VendorID(), controller->ProductID());
	else
		fModule->SetSelected(0, 0);
	_UpdateTest();
	_UpdateFeel();
}


/*!	Shows the feel groups the chosen controller has. A newly opened
	controller does not rumble and has free triggers, so the menu goes back
	to Off; motion is switched on for the Motion group.
*/
void
GameControllersPage::_UpdateFeel()
{
	BGameController* controller = fTest->Controller();
	bool rumble = controller != NULL && controller->CanRumble();
	bool motion = controller != NULL && controller->HasMotion();
	bool triggers = controller != NULL && controller->HasTriggerEffects();
	// No controller: all three, gray, as an example.
	bool example = fEntries.empty();

	fRumbleUntil = 0;
	fWaves->SetActive(false);
	fMotion->Reset();
	fMotion->SetEnabled(motion);
	if (motion)
		controller->SetMotionEnabled(true);
	fEffectItems[0]->SetMarked(true);
	fTest->SetTriggerMark(B_CONTROLLER_TRIGGER_LEFT, -1);
	fTest->SetTriggerMark(B_CONTROLLER_TRIGGER_RIGHT, -1);

	fLightButton->SetEnabled(rumble);
	fStrongButton->SetEnabled(rumble);
	bool triggerRumble = controller != NULL && controller->HasTriggerRumble();
	fTriggerButton->SetEnabled(triggerRumble);
	set_shown(fTriggerButton, triggerRumble);
	fEffectField->SetEnabled(triggers);
	fStartSlider->SetEnabled(triggers);
	fStrengthSlider->SetEnabled(triggers);
	rgb_color gray = mix_color(network_box_fill(),
		ui_color(B_PANEL_TEXT_COLOR), 100);
	for (size_t i = 0; i < fQuiet.size(); i++) {
		quiet_text& text = fQuiet[i];
		if (example)
			text.view->SetHighColor(gray);
		else if (text.panelText)
			text.view->SetHighUIColor(B_PANEL_TEXT_COLOR);
		else
			text.view->SetHighColor(text.color);
		text.view->Invalidate();
	}

	BView* groups[3] = { fRumbleGroup, fMotionGroup, fTriggerGroup };
	bool shown[3] = { rumble || example, motion || example,
		triggers || example };
	for (int i = 0; i < 3; i++)
		set_shown(groups[i], shown[i]);
}


void
GameControllersPage::_AddQuiet(BStringView* view)
{
	quiet_text text;
	text.view = view;
	text.color = view->HighColor();
	text.panelText = view->HasDefaultColors();
	fQuiet.push_back(text);
}


/*!	A group inside the Test box: a thin box on the box fill with a bold
	title; *_content is the BGroupView to fill.
*/
BView*
GameControllersPage::_MakeFeelGroup(const char* name, const char* title,
	BGroupView** _content)
{
	float unit = network_unit();
	BBox* box = new BBox(name);
	box->SetViewColor(network_box_fill());
	box->SetLowColor(network_box_fill());
	BGroupView* content = new BGroupView(B_VERTICAL, roundf(6 * unit));
	content->SetViewColor(network_box_fill());
	BLayoutBuilder::Group<>(content)
		.SetInsets(roundf(10 * unit), roundf(2 * unit), roundf(10 * unit),
			roundf(4 * unit));
	box->AddChild(content);
	BStringView* label = new BStringView("group title", title);
	label->SetFont(r5_settings_bold_font());
	label->SetViewColor(network_box_fill());
	label->SetLowColor(network_box_fill());
	box->SetLabel(label);
	_AddQuiet(label);
	*_content = content;
	return box;
}


void
GameControllersPage::_ApplyTrigger()
{
	int32 start = fStartSlider->Value();
	int32 strength = fStrengthSlider->Value();
	BString text;
	text.SetToFormat("%" B_PRId32 " %%", start);
	fStartValue->SetText(text.String());
	text.SetToFormat("%" B_PRId32 " %%", strength);
	fStrengthValue->SetText(text.String());

	int32 effect = 0;
	for (int32 i = 0; i < 4; i++) {
		if (fEffectItems[i]->IsMarked())
			effect = i;
	}
	float mark = effect == B_TRIGGER_EFFECT_OFF ? -1 : start / 100.0f;
	fTest->SetTriggerMark(B_CONTROLLER_TRIGGER_LEFT, mark);
	fTest->SetTriggerMark(B_CONTROLLER_TRIGGER_RIGHT, mark);

	BGameController* controller = fTest->Controller();
	if (controller == NULL || !controller->HasTriggerEffects())
		return;
	for (int32 i = 0; i < 2; i++) {
		controller->SetTriggerEffect((game_controller_trigger)i,
			(game_controller_trigger_effect)effect, start / 100.0f,
			strength / 100.0f);
	}
}


int32
GameControllersPage::_SelectedIndex() const
{
	for (size_t i = 0; i < fEntries.size(); i++) {
		if (fEntries[i].device == fSelected)
			return (int32)i;
	}
	return -1;
}


// The battery on the chosen controller's card.
void
GameControllersPage::_UpdateBattery(int32 battery)
{
	int32 index = _SelectedIndex();
	if (index < 0 || fEntries[index].battery == battery)
		return;
	fEntries[index].battery = battery;
	BAppearanceKit::BCardStripItem* item = fStrip->ItemAt(index);
	if (item != NULL)
		item->SetSubtitle(card_subtitle(fEntries[index]));
}


void
GameControllersPage::_UpdateTest()
{
	BGameController* controller = fTest->Controller();
	bool example = fEntries.empty();
	set_shown(fTest, !example);
	set_shown(fExample, example);
	set_shown(fTestHint, !example);
	if (controller == NULL) {
		fLayoutField->SetEnabled(false);
		fLayoutItems[0]->SetMarked(true);
		fLabelsField->SetEnabled(false);
		fXboxItem->SetMarked(true);
		fSetupButton->SetEnabled(false);
		fIDs->SetText("");
		fTestHint->SetText("This controller cannot be opened.");
		return;
	}

	_UpdateBattery(controller->BatteryLevel());

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
