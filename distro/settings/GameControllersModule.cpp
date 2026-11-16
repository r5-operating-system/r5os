/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Settings, Devices, Game controllers (docs/GAME-CONTROLLERS.md, option B;
 * the picture is docs/design/gamecontrollers-mockup). One card per
 * controller, the live test of the chosen one with its button labels, and
 * the shapes R5 draws. The pictures are BGameControllerView from
 * libr5game.so. The label choice is stored per controller model by
 * BGameController::SetLabelStyle() as soon as it changes; Revert puts back
 * the choices from when the page was opened, Defaults gives every
 * controller its own labels again. No administrator is needed: the choice
 * is the person's own.
 */


#include "NetworkPage.h"
#include "SettingsFont.h"
#include "SettingsMessages.h"
#include "SettingsModule.h"

#include <Box.h>
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
									// the label choice changed elsewhere

private:
			void				_Rebuild();
			void				_Select(const char* device);
			void				_UpdateTest();
			ControllerCard*		_SelectedCard() const;

			GameControllersModule* fModule;
			SettingsContext*	fContext;
			BGroupView*			fCards;
			BStringView*		fEmpty;
			std::vector<ControllerCard*> fCardList;
			BString				fSelected;

			BMenuField*			fLabelsField;
			BMenuItem*			fXboxItem;
			BMenuItem*			fPlayStationItem;
			BStringView*		fIDs;
			BGameControllerView* fTest;
			BStringView*		fTestHint;
			BMessageRunner*		fBatteryRunner;
};


// #pragma mark - GameControllersModule


class GameControllersModule : public SettingsModule {
public:
								GameControllersModule()
									:
									fPage(NULL)
								{
								}

	virtual	const char*			Identifier() const
									{ return "gamecontrollers"; }
	virtual	const char*			Name() const { return "Game controllers"; }
	virtual	const char*			Category() const { return "devices"; }
	virtual	const char*			Description() const
									{ return "Test your controllers and "
										"choose their button labels"; }
	virtual	const char*			Keywords() const
									{ return "game controller gamepad "
										"joystick joypad test buttons "
										"playstation xbox nintendo"; }

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

	// The page writes each choice itself (SetLabels()); the window calls
	// this after the change.
	virtual	status_t			SaveSettings(SettingsContext* context)
									{ (void)context; return B_OK; }

	virtual	status_t			RevertSettings(SettingsContext* context)
								{
									(void)context;
									status_t result = B_OK;
									std::map<uint32,
										game_controller_labels>::iterator i;
									for (i = fOriginal.begin();
											i != fOriginal.end(); i++) {
										status_t status
											= BGameController::SetLabelStyle(
												i->first >> 16,
												i->first & 0xffff, i->second);
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

	virtual	status_t			ApplyDefaults(SettingsContext* context)
								{
									(void)context;
									status_t status
										= BGameController::ResetLabelStyles();
									std::map<uint32,
										game_controller_labels>::iterator i;
									for (i = fCurrent.begin();
											i != fCurrent.end(); i++)
										i->second
											= B_GAME_CONTROLLER_LABELS_AUTOMATIC;
									// A model first changed by Defaults
									// can be reverted as well.
									_RefreshPage();
									return status;
								}

			status_t			SetLabels(uint16 vendor, uint16 product,
									game_controller_labels labels)
								{
									uint32 key = model_key(vendor, product);
									if (fOriginal.find(key)
											== fOriginal.end()) {
										fOriginal[key]
											= BGameController::LabelStyleFor(
												vendor, product);
									}
									status_t status
										= BGameController::SetLabelStyle(
											vendor, product, labels);
									if (status == B_OK)
										fCurrent[key] = labels;
									if (fContext != NULL)
										fContext->NotifyChanged(Identifier());
									return status;
								}

			// A controller on the page: Revert puts its choice back to
			// what it was when the page was opened.
			void				Remember(uint16 vendor, uint16 product)
								{
									if (vendor == 0)
										return;
									uint32 key = model_key(vendor, product);
									if (fOriginal.find(key)
											!= fOriginal.end())
										return;
									game_controller_labels labels
										= BGameController::LabelStyleFor(
											vendor, product);
									fOriginal[key] = labels;
									fCurrent[key] = labels;
								}

			void				PageGone(GameControllersPage* page)
								{
									if (fPage == page)
										fPage = NULL;
								}

private:
			void				_RefreshPage()
								{
									if (fPage != NULL && fPage->LockLooper()) {
										fPage->Refresh();
										fPage->UnlockLooper();
									}
								}

			SettingsContext*	fContext = NULL;
			GameControllersPage* fPage;
			std::map<uint32, game_controller_labels> fOriginal;
			std::map<uint32, game_controller_labels> fCurrent;
};


// #pragma mark - GameControllersPage


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
	bluetooth->SetTarget(this);

	BGroupView* left = new BGroupView("controllers", B_VERTICAL,
		roundf(8 * unit));
	left->SetExplicitMinSize(BSize(roundf(236 * unit), B_SIZE_UNSET));
	left->SetExplicitMaxSize(BSize(roundf(236 * unit), B_SIZE_UNLIMITED));
	BLayoutBuilder::Group<>(left)
		.Add(fCards)
		.AddGlue()
		.Add(hint)
		.Add(bluetooth);

	// The live test.
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
		.AddGroup(B_HORIZONTAL, roundf(8 * unit))
			.Add(fLabelsField)
			.AddGlue()
			.Add(fIDs)
		.End()
		.Add(fTest, 1)
		.Add(fTestHint);
	testBox->AddChild(test);

	// The shapes R5 draws, as small still pictures.
	FilledBox* shapesBox = new FilledBox("shapes box");
	shapesBox->SetLabel(new BoxTitle("Shapes R5 draws"));
	BGroupView* shapes = new BGroupView(B_HORIZONTAL, roundf(10 * unit));
	shapes->SetViewColor(network_box_fill());
	struct shape_row {
		game_controller_family	family;
		const char*				caption;
	};
	static const shape_row kShapes[] = {
		{ B_GAME_CONTROLLER_FAMILY_PLAYSTATION, "PlayStation" },
		{ B_GAME_CONTROLLER_FAMILY_XBOX, "Xbox" },
		{ B_GAME_CONTROLLER_FAMILY_NINTENDO, "Nintendo (A and B swapped)" },
		{ B_GAME_CONTROLLER_FAMILY_SEGA_GENESIS, "Sega Genesis" },
		{ B_GAME_CONTROLLER_FAMILY_UNKNOWN,
			"Unknown: from its axes and buttons" }
	};
	BLayoutBuilder::Group<> shapesBuilder(shapes);
	shapesBuilder.SetInsets(roundf(14 * unit), roundf(6 * unit),
		roundf(14 * unit), roundf(8 * unit));
	for (size_t i = 0; i < sizeof(kShapes) / sizeof(kShapes[0]); i++) {
		BGameControllerView* picture = new BGameControllerView("shape");
		picture->SetFamily(kShapes[i].family);
		picture->SetViewColor(network_box_fill());
		picture->SetLowColor(network_box_fill());
		picture->SetExplicitMinSize(BSize(roundf(104 * unit),
			roundf(62 * unit)));
		picture->SetExplicitMaxSize(BSize(roundf(104 * unit),
			roundf(62 * unit)));
		BStringView* caption = new BStringView("caption", kShapes[i].caption);
		BFont captionFont(be_plain_font);
		captionFont.SetSize(be_plain_font->Size() * 0.85f);
		caption->SetFont(&captionFont);
		caption->SetAlignment(B_ALIGN_CENTER);
		caption->SetViewColor(network_box_fill());
		caption->SetLowColor(network_box_fill());
		caption->SetHighColor(quiet_color(190));
		shapesBuilder
			.AddGroup(B_VERTICAL, roundf(2 * unit))
				.Add(picture)
				.Add(caption)
			.End();
	}
	shapesBox->AddChild(shapes);

	BLayoutBuilder::Group<>(this)
		.AddGroup(B_HORIZONTAL, roundf(12 * unit), 1)
			.Add(left)
			.Add(testBox, 1)
		.End()
		.Add(shapesBox);

	_Rebuild();
}


GameControllersPage::~GameControllersPage()
{
	if (fModule != NULL)
		fModule->PageGone(this);
}


void
GameControllersPage::AttachedToWindow()
{
	BGroupView::AttachedToWindow();
	fLabelsField->Menu()->SetTargetForItems(this);
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
	BGameController::StopWatching(BMessenger(this));
	delete fBatteryRunner;
	fBatteryRunner = NULL;
	BGroupView::DetachedFromWindow();
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
	_UpdateTest();
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
		fLabelsField->SetEnabled(false);
		fXboxItem->SetMarked(true);
		fIDs->SetText("");
		fTestHint->SetText(fCardList.empty()
			? "Plug in a controller or pair one to test it here."
			: "This controller cannot be opened.");
		return;
	}

	ControllerCard* card = _SelectedCard();
	if (card != NULL)
		card->SetBattery(controller->BatteryLevel());

	game_controller_family family = controller->Family();
	bool choosable = controller->VendorID() != 0
		&& family != B_GAME_CONTROLLER_FAMILY_SEGA_GENESIS
		&& family != B_GAME_CONTROLLER_FAMILY_UNKNOWN;
	fLabelsField->SetEnabled(choosable);
	game_controller_labels labels = controller->LabelStyle();
	if (labels == B_GAME_CONTROLLER_LABELS_PLAYSTATION)
		fPlayStationItem->SetMarked(true);
	else
		fXboxItem->SetMarked(true);
	fTest->SetLabels(labels);

	if (controller->VendorID() != 0) {
		BString ids;
		ids.SetToFormat("Vendor %04X \xc2\xb7 Product %04X",
			controller->VendorID(), controller->ProductID());
		fIDs->SetText(ids.String());
	} else
		fIDs->SetText("");
	fTestHint->SetText("Press a button or move a stick: it lights up here.");
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
