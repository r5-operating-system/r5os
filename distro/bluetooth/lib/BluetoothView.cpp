/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * BluetoothView (libr5bt.so): My devices (the paired devices, with an
 * information panel for the one chosen), the devices nearby, and at the
 * bottom the Bluetooth switch with the radio. The information panel is a
 * BAppearanceKit::BCard (docs/APPEARANCE-KIT.md); the lists stay on the
 * panel. The signal bars come from libr5icons.so.
 */


#include <R5Bluetooth.h>

#include <Application.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <ListView.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <ScrollView.h>
#include <StringFormat.h>
#include <StringView.h>
#include <MessageRunner.h>
#include <Window.h>

#include <Card.h>
#include <R5Icons.h>

#include <bluetooth/bdaddrUtils.h>

#include <new>
#include <string.h>
#include <strings.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "BluetoothView"


static const uint32 kMsgRadioSwitch = 'btvS';
static const uint32 kMsgRadioChosen = 'btvR';
static const uint32 kMsgScanButton = 'btvB';
static const uint32 kMsgPairButton = 'btvP';
static const uint32 kMsgForgetButton = 'btvF';
static const uint32 kMsgDeviceSelected = 'btvD';
static const uint32 kMsgMineSelected = 'btvM';
static const uint32 kMsgDisconnectButton = 'btvX';
static const uint32 kMsgUseForSound = 'btvU';
static const uint32 kMsgTestButtons = 'btvT';
static const uint32 kMsgGamepadPoll = 'btvG';


static BButton*
pair_button(BView* view)
{
	return dynamic_cast<BButton*>(view->FindView("pair"));
}


static float
font_scale()
{
	return be_plain_font->Size() / 12.0f;
}


static BFont
bold_font()
{
	BFont font(be_bold_font);
	return font;
}


// The second line of a row and the notes: the text color, a step fainter.
static rgb_color
faint_color(rgb_color text, rgb_color background)
{
	return tint_color(text, background.IsLight()
		? B_LIGHTEN_1_TINT : B_DARKEN_1_TINT);
}


// Internal to libr5bt.so: Settings has a DeviceItem class of its own, and an
// exported one here made the program's DrawItem() draw these rows (crash on the
// 5625U after the first scan, 16 Nov 2026).
namespace {


// #pragma mark - DeviceItem


/*!	Two lines: the name, then the kind and the address. The signal bars
	are at the right when the radio reported a signal strength. A disabled
	row (Bluetooth off) is drawn faint.
*/
class DeviceItem : public BListItem {
public:
	DeviceItem(const BluetoothDeviceInfo& info)
		:
		fInfo(info)
	{
	}

	const BluetoothDeviceInfo& Info() const { return fInfo; }
	void SetInfo(const BluetoothDeviceInfo& info) { fInfo = info; }

	virtual void Update(BView* owner, const BFont* font)
	{
		font_height height;
		font->GetHeight(&height);
		fLineHeight = ceilf(height.ascent + height.descent + height.leading);
		SetHeight(2 * fLineHeight + roundf(8 * font_scale()));
		SetWidth(owner->Bounds().Width());
	}

	virtual void DrawItem(BView* owner, BRect frame, bool complete)
	{
		(void)complete;
		rgb_color background = ui_color(IsSelected()
			? B_LIST_SELECTED_BACKGROUND_COLOR : B_LIST_BACKGROUND_COLOR);
		rgb_color text = ui_color(IsSelected()
			? B_LIST_SELECTED_ITEM_TEXT_COLOR : B_LIST_ITEM_TEXT_COLOR);
		if (!IsEnabled())
			text = faint_color(faint_color(text, background), background);
		owner->SetLowColor(background);
		owner->FillRect(frame, B_SOLID_LOW);

		float inset = roundf(6 * font_scale());
		float barsSide = 2 * fLineHeight - inset;
		BRect bars(frame.right - inset - barsSide, frame.top + inset,
			frame.right - inset, frame.top + inset + barsSide);
		float textRight = bars.left - inset;

		font_height height;
		owner->GetFontHeight(&height);

		owner->SetHighColor(text);
		BString name = fInfo.DisplayName();
		owner->TruncateString(&name, B_TRUNCATE_END,
			textRight - frame.left - inset);
		owner->DrawString(name.String(), BPoint(frame.left + inset,
			frame.top + inset / 2 + height.ascent));

		BString detail = fInfo.Kind();
		if (!fInfo.name.IsEmpty() || detail.IsEmpty()) {
			if (!detail.IsEmpty())
				detail << " \xC2\xB7 ";
			detail << fInfo.AddressString();
		} else
			detail = fInfo.AddressString();
		owner->SetHighColor(faint_color(text, background));
		owner->TruncateString(&detail, B_TRUNCATE_END,
			textRight - frame.left - inset);
		owner->DrawString(detail.String(), BPoint(frame.left + inset,
			frame.top + inset / 2 + fLineHeight + height.ascent));

		int32 level = fInfo.SignalLevel();
		if (level >= 0) {
			r5_level_glyph_draw(owner, R5_LEVEL_SIGNAL, bars, level, text,
				background, R5_LEVEL_STYLE_NORMAL);
		}
	}

private:
	BluetoothDeviceInfo	fInfo;
	float				fLineHeight;
};


// #pragma mark - PairedItem


/*!	A row of My devices: the name, then the kind; at the right a dot and
	"Connected" or "Not connected".
*/
class PairedItem : public BListItem {
public:
	PairedItem(const BluetoothPairedDevice& device)
		:
		fDevice(device)
	{
	}

	const BluetoothPairedDevice& Device() const { return fDevice; }
	void SetDevice(const BluetoothPairedDevice& device) { fDevice = device; }

	virtual void Update(BView* owner, const BFont* font)
	{
		font_height height;
		font->GetHeight(&height);
		fLineHeight = ceilf(height.ascent + height.descent + height.leading);
		SetHeight(2 * fLineHeight + roundf(8 * font_scale()));
		SetWidth(owner->Bounds().Width());
	}

	virtual void DrawItem(BView* owner, BRect frame, bool complete)
	{
		(void)complete;
		rgb_color background = ui_color(IsSelected()
			? B_LIST_SELECTED_BACKGROUND_COLOR : B_LIST_BACKGROUND_COLOR);
		rgb_color text = ui_color(IsSelected()
			? B_LIST_SELECTED_ITEM_TEXT_COLOR : B_LIST_ITEM_TEXT_COLOR);
		if (!IsEnabled())
			text = faint_color(faint_color(text, background), background);
		owner->SetLowColor(background);
		owner->FillRect(frame, B_SOLID_LOW);

		float inset = roundf(6 * font_scale());
		font_height height;
		owner->GetFontHeight(&height);

		// The state at the right, on the first line
		BString state = fDevice.connected ? B_TRANSLATE("Connected")
			: B_TRANSLATE("Not connected");
		float stateWidth = owner->StringWidth(state.String());
		float dot = roundf(height.ascent * 0.6f);
		float stateLeft = frame.right - inset - stateWidth;
		float baseline = frame.top + inset / 2 + height.ascent;
		BRect dotFrame(stateLeft - inset / 2 - dot, baseline - dot,
			stateLeft - inset / 2, baseline);
		rgb_color dotColor = fDevice.connected && IsEnabled()
			? ui_color(B_SUCCESS_COLOR) : faint_color(text, background);
		owner->SetHighColor(dotColor);
		owner->FillEllipse(dotFrame);
		owner->SetHighColor(faint_color(text, background));
		owner->DrawString(state.String(), BPoint(stateLeft, baseline));

		float textRight = dotFrame.left - inset;
		owner->SetHighColor(text);
		BString name = fDevice.info.DisplayName();
		owner->TruncateString(&name, B_TRUNCATE_END,
			textRight - frame.left - inset);
		owner->DrawString(name.String(), BPoint(frame.left + inset, baseline));

		BString kind = fDevice.Kind();
		if (kind.IsEmpty())
			kind = B_TRANSLATE("Bluetooth device");
		owner->SetHighColor(faint_color(text, background));
		owner->TruncateString(&kind, B_TRUNCATE_END,
			frame.right - frame.left - 2 * inset);
		owner->DrawString(kind.String(), BPoint(frame.left + inset,
			baseline + fLineHeight));
	}

private:
	BluetoothPairedDevice	fDevice;
	float					fLineHeight;
};


} // namespace


// #pragma mark - MyDevices


struct BluetoothView::MyDevices {
	BListView*			list;
	BStringView*		name;
	BStringView*		kind;
	BStringView*		statusLabel;
	BStringView*		status;
	BStringView*		connectionLabel;
	BStringView*		connection;
	BStringView*		addressLabel;
	BStringView*		address;
	BStringView*		batteryLabel;
	BStringView*		battery;
	BCheckBox*			useForSound;
	BButton*			testButtons;
	BButton*			disconnect;
	BButton*			forget;
	std::vector<BluetoothPairedDevice> paired;
	bool				hasSound;
	bdaddr_t			soundAddress;
		// the headset this person's sound goes to
};


static bool
same_paired(const std::vector<BluetoothPairedDevice>& a,
	const std::vector<BluetoothPairedDevice>& b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); i++) {
		if (!bdaddrUtils::Compare(a[i].info.address, b[i].info.address)
			|| a[i].info.name != b[i].info.name
			|| a[i].info.deviceClass != b[i].info.deviceClass
			|| a[i].lowEnergy != b[i].lowEnergy || a[i].hid != b[i].hid
			|| a[i].connected != b[i].connected
			|| a[i].battery != b[i].battery)
			return false;
	}
	return true;
}


// #pragma mark - BluetoothView


BluetoothView::BluetoothView(bool settingsPage)
	:
	BGroupView("bluetooth", B_VERTICAL),
	fSettingsPage(settingsPage),
	fRadioSwitchAllowed(true),
	fHasState(false),
	fRadioFailed(false),
	fController(NULL),
	fSummaryIcon(NULL),
	fSummaryTitle(NULL),
	fSummaryDetail(NULL),
	fPairStatus(new BString),
	fPairing(0),
	fMine(new MyDevices)
{
	BFont bold = bold_font();
	const float scale = font_scale();

	// My devices: the list, and the panel for the device chosen in it.
	fMine->list = new BListView("my devices", B_SINGLE_SELECTION_LIST);
	fMine->list->SetSelectionMessage(new BMessage(kMsgMineSelected));
	BScrollView* mineScroller = new BScrollView("my devices scroll",
		fMine->list, 0, false, true);
	// A width of its own: in the narrow Bluetooth window the panel took
	// all of it and the list was a strip (stick 52).
	mineScroller->SetExplicitMinSize(BSize(roundf(200 * scale),
		roundf(140 * scale)));

	fMine->name = new BStringView("device name", "");
	fMine->name->SetFont(&bold);
	fMine->kind = new BStringView("device kind", "");
	fMine->statusLabel = new BStringView("status label", "");
	fMine->status = new BStringView("status", "");
	fMine->connectionLabel = new BStringView("connection label", "");
	fMine->connection = new BStringView("connection", "");
	fMine->addressLabel = new BStringView("address label", "");
	fMine->address = new BStringView("address", "");
	fMine->batteryLabel = new BStringView("battery label", "");
	fMine->battery = new BStringView("battery", "");
	fMine->useForSound = new BCheckBox("use for sound",
		B_TRANSLATE("Use for sound"), new BMessage(kMsgUseForSound));
	fMine->testButtons = new BButton("test buttons",
		B_TRANSLATE("Test buttons" B_UTF8_ELLIPSIS),
		new BMessage(kMsgTestButtons));
	fMine->hasSound = false;
	memset(&fMine->soundAddress, 0, sizeof(fMine->soundAddress));
	BStringView* texts[] = { fMine->name, fMine->kind, fMine->status,
		fMine->connection, fMine->address, fMine->battery };
	for (size_t i = 0; i < sizeof(texts) / sizeof(texts[0]); i++) {
		texts[i]->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
		// A long device name (up to 248 bytes) is cut with an ellipsis:
		// as its full width it made the page wider than the window.
		texts[i]->SetTruncation(B_TRUNCATE_END);
		texts[i]->SetExplicitMinSize(BSize(roundf(60 * scale),
			B_SIZE_UNSET));
	}
	fMine->disconnect = new BButton("disconnect", B_TRANSLATE("Disconnect"),
		new BMessage(kMsgDisconnectButton));
	fMine->forget = new BButton("forget", B_TRANSLATE("Forget"),
		new BMessage(kMsgForgetButton));

	BAppearanceKit::BCard* info = new BAppearanceKit::BCard("device info",
		BAppearanceKit::B_CARD_PANEL, B_VERTICAL);
	// As wide as its line of buttons needs; the list takes the rest. An
	// explicit minimum of 250 pixels used to stand in for the buttons'
	// own: narrower than three buttons, so they ran past the window's
	// right edge (stick 51).
	BLayoutBuilder::Group<>(info, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.AddGroup(B_VERTICAL, 0)
			.Add(fMine->name)
			.Add(fMine->kind)
		.End()
		.AddGrid(B_USE_DEFAULT_SPACING, 0)
			.Add(fMine->statusLabel, 0, 0)
			.Add(fMine->status, 1, 0)
			.Add(fMine->connectionLabel, 0, 1)
			.Add(fMine->connection, 1, 1)
			.Add(fMine->addressLabel, 0, 2)
			.Add(fMine->address, 1, 2)
			.Add(fMine->batteryLabel, 0, 3)
			.Add(fMine->battery, 1, 3)
		.End()
		.AddGlue()
		// "Use for sound" only for a headset, "Test buttons…" only for a
		// game controller. One line, so the page fits the window's height
		// at its default size with a device chosen (stick 51 had a scroll
		// bar there).
		.AddGroup(B_HORIZONTAL)
			.Add(fMine->useForSound)
			.Add(fMine->testButtons)
			.AddGlue()
			.Add(fMine->disconnect)
			.Add(fMine->forget)
		.End();

	BStringView* mineTitle = new BStringView("my devices title",
		B_TRANSLATE("My devices"));
	mineTitle->SetFont(&bold);
	BBox* mineBox = new BBox("my devices box");
	mineBox->SetLabel(mineTitle);
	BGroupView* mineInner = new BGroupView(B_HORIZONTAL);
	BLayoutBuilder::Group<>(mineInner)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(mineScroller, 10.0f)
		.Add(info, 0.0f);
	mineBox->AddChild(mineInner);

	// Devices nearby: the scan. A paired device is under My devices.
	fDeviceList = new BListView("devices", B_SINGLE_SELECTION_LIST);
	BScrollView* scroller = new BScrollView("devices scroll", fDeviceList, 0,
		false, true);
	// At least three rows; it takes the rest of the page's height.
	scroller->SetExplicitMinSize(BSize(B_SIZE_UNSET, roundf(60 * scale)));
	fScanStatus = new BStringView("scan status", "");
	fScanStatus->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	// The hint and the pairing result are cut, not the page made wider.
	fScanStatus->SetTruncation(B_TRUNCATE_END);
	fScanStatus->SetExplicitMinSize(BSize(roundf(60 * scale), B_SIZE_UNSET));
	fScanButton = new BButton("scan", B_TRANSLATE("Scan"),
		new BMessage(kMsgScanButton));
	fDeviceList->SetSelectionMessage(new BMessage(kMsgDeviceSelected));
	BButton* pairButton = new BButton("pair", B_TRANSLATE("Pair"),
		new BMessage(kMsgPairButton));

	BStringView* devicesTitle = new BStringView("devices title",
		B_TRANSLATE("Devices nearby"));
	devicesTitle->SetFont(&bold);
	BBox* devicesBox = new BBox("devices box");
	devicesBox->SetLabel(devicesTitle);
	BGroupView* devicesInner = new BGroupView(B_VERTICAL,
		B_USE_SMALL_SPACING);
	BLayoutBuilder::Group<>(devicesInner)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(scroller)
		.AddGroup(B_HORIZONTAL)
			.Add(fScanStatus)
			.AddGlue()
			.Add(pairButton)
			.Add(fScanButton)
		.End();
	devicesBox->AddChild(devicesInner);

	// The last line: the switch and the radio, without a box. Not used
	// often, so it does not stand above the devices.
	fRadioSwitch = new BCheckBox("radio switch", B_TRANSLATE("Bluetooth on"),
		new BMessage(kMsgRadioSwitch));
	fRadioMenu = new BPopUpMenu(B_TRANSLATE("No radio"));
	fRadioField = new BMenuField("radio", NULL, fRadioMenu);
	fRadioNote = new BStringView("radio note", "");
	fRadioNote->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fRadioNote->SetTruncation(B_TRUNCATE_END);
	fRadioNote->SetExplicitMinSize(BSize(roundf(60 * scale), B_SIZE_UNSET));

	// Devices nearby takes the rest of the height, down to the Bluetooth
	// line (in Settings: as far down as the list of pages).
	BLayoutBuilder::Group<>(this)
		.SetInsets(settingsPage ? 0 : B_USE_WINDOW_SPACING)
		.Add(mineBox, 0.0f)
		.Add(devicesBox, 10.0f)
		.AddGroup(B_HORIZONTAL)
			.Add(fRadioSwitch)
			.Add(fRadioNote)
			.AddGlue()
			.Add(fRadioField)
		.End();

	_SetState(fState);
	_UpdateMine();
}


BluetoothView::~BluetoothView()
{
	_StopController();
	for (int32 i = fDeviceList->CountItems() - 1; i >= 0; i--)
		delete fDeviceList->RemoveItem(i);
	for (int32 i = fMine->list->CountItems() - 1; i >= 0; i--)
		delete fMine->list->RemoveItem(i);
	delete fPairStatus;
	delete fMine;
}


void
BluetoothView::AttachedToWindow()
{
	BGroupView::AttachedToWindow();

	fRadioSwitch->SetTarget(this);
	fScanButton->SetTarget(this);
	fDeviceList->SetTarget(this);
	fMine->list->SetTarget(this);
	fMine->disconnect->SetTarget(this);
	fMine->forget->SetTarget(this);
	fMine->useForSound->SetTarget(this);
	fMine->testButtons->SetTarget(this);
	if (BButton* pairButton = pair_button(this))
		pairButton->SetTarget(this);
	fRadioMenu->SetTargetForItems(this);

	fRadioNote->SetHighColor(faint_color(ui_color(B_PANEL_TEXT_COLOR),
		ui_color(B_PANEL_BACKGROUND_COLOR)));
	fMine->kind->SetHighColor(fRadioNote->HighColor());

	if (fController == NULL) {
		fController = new(std::nothrow) BluetoothController(BMessenger(this));
		if (fController != NULL && fController->Start() != B_OK) {
			delete fController;
			fController = NULL;
		}
		if (fController != NULL)
			fControllerMessenger = BMessenger(fController);
	}
}


void
BluetoothView::DetachedFromWindow()
{
	_StopController();
	BGroupView::DetachedFromWindow();
}


void
BluetoothView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgBluetoothStateChanged:
		{
			BluetoothState state;
			if (state.Unarchive(*message) != B_OK)
				break;
			std::vector<BluetoothPairedDevice> paired;
			BluetoothState::UnarchivePaired(*message, paired);
			bool pairedChanged = !same_paired(paired, fMine->paired);
			fMine->paired.swap(paired);
			bdaddr_t sound;
			const bool hasSound = BluetoothState::UnarchiveSoundDevice(
				*message, sound);
			if (hasSound != fMine->hasSound || (hasSound
					&& !bdaddrUtils::Compare(sound, fMine->soundAddress))) {
				fMine->hasSound = hasSound;
				if (hasSound)
					fMine->soundAddress = sound;
				pairedChanged = true;
			}
			_SetState(state);
			if (pairedChanged) {
				_UpdateMine();
				_UpdateDevices();
				_UpdateControls();
			}
			break;
		}

		case kMsgBluetoothRadioResult:
		{
			// The state message that follows sets the switch back when the
			// change did not take.
			fRadioFailed = message->GetInt32("status", B_ERROR) != B_OK;
			_UpdateRadio();
			// A refused change sends no new state (it did not change), so
			// the check box is set back here.
			_UpdateControls();
			break;
		}

		case kMsgRadioSwitch:
		{
			if (!fRadioSwitchAllowed) {
				_UpdateControls();
				break;
			}
			BMessage request(kMsgBluetoothSetRadio);
			request.AddBool("on", fRadioSwitch->Value() == B_CONTROL_ON);
			_SendToController(&request);
			break;
		}

		case kMsgRadioChosen:
		{
			int32 id;
			if (message->FindInt32("hci_id", &id) == B_OK) {
				BMessage request(kMsgBluetoothSelectRadio);
				request.AddInt32("hci_id", id);
				_SendToController(&request);
			}
			break;
		}

		case kMsgScanButton:
		{
			fPairStatus->Truncate(0);
			BMessage request(fState.scanning ? kMsgBluetoothCancelScan
				: kMsgBluetoothScan);
			_SendToController(&request);
			break;
		}

		case kMsgDeviceSelected:
			_UpdateControls();
			break;

		case kMsgUseForSound:
		{
			PairedItem* item = dynamic_cast<PairedItem*>(
				fMine->list->ItemAt(fMine->list->CurrentSelection()));
			if (item == NULL)
				break;
			BMessage request(kMsgBluetoothUseForSound);
			request.AddData("bdaddr", B_ANY_TYPE,
				&item->Device().info.address, sizeof(bdaddr_t));
			request.AddBool("use",
				fMine->useForSound->Value() == B_CONTROL_ON);
			_SendToController(&request);
			break;
		}

		case kMsgTestButtons:
		{
			// The live test is the Game controllers page of Settings
			// (docs/GAME-CONTROLLERS.md, step 8), which also runs when
			// Settings is open already.
			// BRoster::Launch() refuses a launch of the calling team itself
			// (B_BAD_VALUE, nothing sent), so on this page inside Settings
			// the button did nothing (stick 51). There the same "--page"
			// arguments go to Settings directly, as B_ARGV_RECEIVED.
			static const char* kSettings = "application/x-vnd.R5OS-Settings";
			app_info info;
			if (be_app != NULL && be_app->GetAppInfo(&info) == B_OK
				&& strcasecmp(info.signature, kSettings) == 0) {
				BMessage argv(B_ARGV_RECEIVED);
				argv.AddInt32("argc", 3);
				argv.AddString("argv", "Settings");
				argv.AddString("argv", "--page");
				argv.AddString("argv", "gamecontrollers");
				be_app->PostMessage(&argv);
				break;
			}
			const char* args[] = { "--page", "gamecontrollers", NULL };
			be_roster->Launch(kSettings, 2, args);
			break;
		}

		case kMsgMineSelected:
			_UpdateInfo();
			break;

		case kMsgPairButton:
		{
			DeviceItem* item = dynamic_cast<DeviceItem*>(
				fDeviceList->ItemAt(fDeviceList->CurrentSelection()));
			if (item == NULL)
				break;
			BMessage request(kMsgBluetoothPair);
			request.AddData("bdaddr", B_ANY_TYPE, &item->Info().address,
				sizeof(bdaddr_t));
			request.AddString("name", item->Info().name);
			request.AddUInt32("class", item->Info().deviceClass);
			_SendToController(&request);

			// Pairing… until the server says how it ended.
			BString name = item->Info().name;
			if (name.IsEmpty())
				name = B_TRANSLATE("the device");
			*fPairStatus = B_TRANSLATE("Pairing with %name%" B_UTF8_ELLIPSIS);
			fPairStatus->ReplaceFirst("%name%", name);
			fPairing = 1;
			_UpdateControls();
			break;
		}

		case kMsgDisconnectButton:
		case kMsgForgetButton:
		{
			PairedItem* item = dynamic_cast<PairedItem*>(
				fMine->list->ItemAt(fMine->list->CurrentSelection()));
			if (item == NULL)
				break;
			BMessage request(message->what == kMsgForgetButton
				? kMsgBluetoothForget : kMsgBluetoothDisconnect);
			request.AddData("bdaddr", B_ANY_TYPE,
				&item->Device().info.address, sizeof(bdaddr_t));
			request.AddString("name", item->Device().info.name);
			_SendToController(&request);
			break;
		}

		case kMsgBluetoothForgetResult:
		{
			BString name = message->GetString("name", "");
			if (name.IsEmpty())
				name = B_TRANSLATE("the device");
			*fPairStatus = message->GetBool("forgotten", false)
				? B_TRANSLATE("%name% is forgotten. Pair it again to use it.")
				: B_TRANSLATE("%name% was not paired.");
			fPairStatus->ReplaceFirst("%name%", name);
			_UpdateControls();
			break;
		}

		case kMsgBluetoothPairResult:
		{
			BString name = message->GetString("name", "");
			if (name.IsEmpty())
				name = B_TRANSLATE("the device");
			*fPairStatus = message->GetBool("paired", false)
				? B_TRANSLATE("Paired with %name%.")
				: B_TRANSLATE("Pairing with %name% did not work.");
			fPairStatus->ReplaceFirst("%name%", name);
			fPairing = 0;
			_UpdateControls();
			break;
		}

		default:
			BGroupView::MessageReceived(message);
			break;
	}
}


void
BluetoothView::SetRadioSwitchAllowed(bool allowed)
{
	if (allowed == fRadioSwitchAllowed)
		return;
	fRadioSwitchAllowed = allowed;
	fRadioFailed = false;
	_UpdateRadio();
	_UpdateControls();
}


void
BluetoothView::_StopController()
{
	if (fController == NULL)
		return;
	// Asked to quit, not locked and quit here: Lock() waits while the
	// controller sits in a libbluetooth call without a timeout, and its
	// destructor cancels a scan with one. Both now happen on its own thread,
	// never on the window's.
	fControllerMessenger.SendMessage(B_QUIT_REQUESTED);
	fController = NULL;
	fControllerMessenger = BMessenger();
}


void
BluetoothView::_SendToController(BMessage* message)
{
	if (fControllerMessenger.IsValid())
		fControllerMessenger.SendMessage(message);
}


bool
BluetoothView::_IsPaired(const bdaddr_t& address) const
{
	for (size_t i = 0; i < fMine->paired.size(); i++) {
		if (bdaddrUtils::Compare(fMine->paired[i].info.address, address))
			return true;
	}
	return false;
}


static bool
same_devices(const std::vector<BluetoothDeviceInfo>& a,
	const std::vector<BluetoothDeviceInfo>& b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); i++) {
		if (!bdaddrUtils::Compare(a[i].address, b[i].address)
			|| a[i].name != b[i].name || a[i].rssi != b[i].rssi
			|| a[i].deviceClass != b[i].deviceClass)
			return false;
	}
	return true;
}


void
BluetoothView::_SetState(const BluetoothState& state)
{
	bool devicesChanged = !fHasState
		|| !same_devices(state.devices, fState.devices);
	bool enabledChanged = state.radioEnabled != fState.radioEnabled
		|| state.service != fState.service;
	if (state.radioEnabled != fState.radioEnabled)
		fRadioFailed = false;
	// A pairing cannot end well once bluetooth_server is gone: "Pairing…"
	// would stay and keep Pair and Forget disabled.
	if (fPairing != 0 && state.service != BLUETOOTH_SERVICE_RUNNING) {
		fPairing = 0;
		fPairStatus->Truncate(0);
	}
	fState = state;
	fHasState = true;

	_UpdateRadio();
	if (devicesChanged)
		_UpdateDevices();
	if (enabledChanged)
		_UpdateMine();
	_UpdateControls();
}


void
BluetoothView::_UpdateRadio()
{
	BString note;
	const BluetoothRadioInfo* radio = fState.CurrentRadio();

	switch (fState.service) {
		case BLUETOOTH_SERVICE_STARTING:
			note = B_TRANSLATE("Starting Bluetooth" B_UTF8_ELLIPSIS);
			break;
		case BLUETOOTH_SERVICE_FAILED:
			note = B_TRANSLATE("Bluetooth is not available: the service could "
				"not be started.");
			break;
		case BLUETOOTH_SERVICE_STOPPED:
			note = B_TRANSLATE("Bluetooth is not available: the service "
				"stopped.");
			break;
		default:
			if (fState.radios.empty())
				note = B_TRANSLATE("No Bluetooth radio was found.");
			else if (fRadioFailed)
				note = B_TRANSLATE("Bluetooth could not be turned on or off.");
			else if (!fRadioSwitchAllowed) {
				note = B_TRANSLATE("Only an administrator can turn Bluetooth "
					"on or off.");
			} else if (fState.radios.size() == 1 && radio != NULL) {
				note = radio->name.IsEmpty()
					? BString(B_TRANSLATE("Bluetooth radio")) : radio->name;
			}
			break;
	}
	fRadioNote->SetText(note.String());

	// The radio menu, only when there is more than one radio.
	bool rebuild = fRadioMenu->CountItems() != (int32)fState.radios.size();
	for (int32 i = 0; !rebuild && i < fRadioMenu->CountItems(); i++) {
		BMessage* message = fRadioMenu->ItemAt(i)->Message();
		if (message == NULL
			|| message->GetInt32("hci_id", -1) != fState.radios[i].id)
			rebuild = true;
	}
	if (rebuild) {
		while (BMenuItem* item = fRadioMenu->RemoveItem((int32)0))
			delete item;
		for (size_t i = 0; i < fState.radios.size(); i++) {
			const BluetoothRadioInfo& info = fState.radios[i];
			BString label = info.name.IsEmpty()
				? bdaddrUtils::ToString(info.address) : info.name;
			BMessage* message = new BMessage(kMsgRadioChosen);
			message->AddInt32("hci_id", info.id);
			BMenuItem* item = new BMenuItem(label.String(), message);
			fRadioMenu->AddItem(item);
		}
		if (Window() != NULL)
			fRadioMenu->SetTargetForItems(this);
	}
	for (int32 i = 0; i < fRadioMenu->CountItems(); i++) {
		BMenuItem* item = fRadioMenu->ItemAt(i);
		item->SetMarked(item->Message()->GetInt32("hci_id", -1)
			== fState.radio);
	}
	if (fState.radios.size() > 1) {
		if (fRadioField->IsHidden(fRadioField))
			fRadioField->Show();
	} else if (!fRadioField->IsHidden(fRadioField))
		fRadioField->Hide();
}


void
BluetoothView::_UpdateDevices()
{
	BString selected;
	DeviceItem* current = dynamic_cast<DeviceItem*>(
		fDeviceList->ItemAt(fDeviceList->CurrentSelection()));
	if (current != NULL)
		selected = current->Info().AddressString();

	// A device that is paired is under My devices, not here.
	std::vector<BluetoothDeviceInfo> shown;
	for (size_t i = 0; i < fState.devices.size(); i++) {
		if (!_IsPaired(fState.devices[i].address))
			shown.push_back(fState.devices[i]);
	}
	const bool enabled = fState.service == BLUETOOTH_SERVICE_RUNNING
		&& fState.radioEnabled;

	// Every scan answer brings a new list. The same devices in the same
	// order are updated where they are; otherwise the list is filled again
	// at the same scroll position. It emptied the list each time, which
	// scrolled back to the top and lost a device selected further down.
	bool sameOrder = fDeviceList->CountItems() == (int32)shown.size();
	for (int32 i = 0; sameOrder && i < fDeviceList->CountItems(); i++) {
		DeviceItem* item = dynamic_cast<DeviceItem*>(fDeviceList->ItemAt(i));
		sameOrder = item != NULL && item->Info().AddressString()
			== shown[i].AddressString();
	}
	if (sameOrder) {
		for (int32 i = 0; i < fDeviceList->CountItems(); i++) {
			DeviceItem* item = static_cast<DeviceItem*>(
				fDeviceList->ItemAt(i));
			item->SetInfo(shown[i]);
			item->SetEnabled(enabled);
			fDeviceList->InvalidateItem(i);
		}
		return;
	}

	const float top = fDeviceList->Bounds().top;
	fDeviceList->SetSelectionMessage(NULL);
	for (int32 i = fDeviceList->CountItems() - 1; i >= 0; i--)
		delete fDeviceList->RemoveItem(i);

	for (size_t i = 0; i < shown.size(); i++) {
		DeviceItem* item = new DeviceItem(shown[i]);
		item->SetEnabled(enabled);
		fDeviceList->AddItem(item);
		if (!selected.IsEmpty() && item->Info().AddressString() == selected)
			fDeviceList->Select(fDeviceList->CountItems() - 1);
	}
	fDeviceList->ScrollTo(0, top);
	fDeviceList->SetSelectionMessage(new BMessage(kMsgDeviceSelected));
}


void
BluetoothView::_UpdateMine()
{
	BListView* list = fMine->list;
	const std::vector<BluetoothPairedDevice>& paired = fMine->paired;
	const bool enabled = fState.service == BLUETOOTH_SERVICE_RUNNING
		&& fState.radioEnabled;

	BString selected;
	PairedItem* current = dynamic_cast<PairedItem*>(
		list->ItemAt(list->CurrentSelection()));
	if (current != NULL)
		selected = current->Device().info.AddressString();

	// Updated in place when the same devices are in the same order, as in
	// _UpdateDevices(): the selection and the scroll position stay.
	bool sameOrder = list->CountItems() == (int32)paired.size();
	for (int32 i = 0; sameOrder && i < list->CountItems(); i++) {
		PairedItem* item = dynamic_cast<PairedItem*>(list->ItemAt(i));
		sameOrder = item != NULL && item->Device().info.AddressString()
			== paired[i].info.AddressString();
	}
	if (sameOrder) {
		for (int32 i = 0; i < list->CountItems(); i++) {
			PairedItem* item = static_cast<PairedItem*>(list->ItemAt(i));
			item->SetDevice(paired[i]);
			item->SetEnabled(enabled);
			list->InvalidateItem(i);
		}
	} else {
		const float top = list->Bounds().top;
		list->SetSelectionMessage(NULL);
		for (int32 i = list->CountItems() - 1; i >= 0; i--)
			delete list->RemoveItem(i);
		for (size_t i = 0; i < paired.size(); i++) {
			PairedItem* item = new PairedItem(paired[i]);
			item->SetEnabled(enabled);
			list->AddItem(item);
			if (!selected.IsEmpty()
				&& item->Device().info.AddressString() == selected)
				list->Select(list->CountItems() - 1);
		}
		// The first device is shown when nothing was chosen yet.
		if (list->CurrentSelection() < 0 && list->CountItems() > 0)
			list->Select(0);
		list->ScrollTo(0, top);
		list->SetSelectionMessage(new BMessage(kMsgMineSelected));
	}
	_UpdateInfo();
}


// The battery line takes no room when the device reports no level.
void
BluetoothView::_ShowBattery(bool show)
{
	BView* views[] = { fMine->batteryLabel, fMine->battery };
	for (size_t i = 0; i < 2; i++) {
		if (show == views[i]->IsHidden(views[i])) {
			if (show)
				views[i]->Show();
			else
				views[i]->Hide();
		}
	}
}


void
BluetoothView::_UpdateInfo()
{
	PairedItem* item = dynamic_cast<PairedItem*>(
		fMine->list->ItemAt(fMine->list->CurrentSelection()));
	const bool running = fState.service == BLUETOOTH_SERVICE_RUNNING;

	if (item == NULL) {
		fMine->name->SetText(fMine->paired.empty()
			? B_TRANSLATE("No paired devices") : "");
		fMine->kind->SetText(fMine->paired.empty()
			? B_TRANSLATE("Pair one under Devices nearby.")
			: B_TRANSLATE("Choose a device."));
		fMine->statusLabel->SetText("");
		fMine->status->SetText("");
		fMine->connectionLabel->SetText("");
		fMine->connection->SetText("");
		fMine->addressLabel->SetText("");
		fMine->address->SetText("");
		_ShowBattery(false);
		if (!fMine->useForSound->IsHidden(fMine->useForSound))
			fMine->useForSound->Hide();
		if (!fMine->testButtons->IsHidden(fMine->testButtons))
			fMine->testButtons->Hide();
		fMine->disconnect->SetEnabled(false);
		fMine->forget->SetEnabled(false);
		return;
	}

	const BluetoothPairedDevice& device = item->Device();
	BString kind = device.Kind();
	if (kind.IsEmpty())
		kind = B_TRANSLATE("Bluetooth device");
	fMine->name->SetText(device.info.DisplayName().String());
	fMine->kind->SetText(kind.String());
	fMine->statusLabel->SetText(B_TRANSLATE("Status:"));
	if (running && !fState.radioEnabled)
		fMine->status->SetText(B_TRANSLATE("Bluetooth is off"));
	else {
		fMine->status->SetText(device.connected ? B_TRANSLATE("Connected")
			: B_TRANSLATE("Not connected"));
	}
	fMine->connectionLabel->SetText(B_TRANSLATE("Uses:"));
	fMine->connection->SetText(device.lowEnergy
		? B_TRANSLATE("Bluetooth Low Energy")
		: B_TRANSLATE("Bluetooth Classic"));
	fMine->addressLabel->SetText(B_TRANSLATE("Address:"));
	fMine->address->SetText(device.info.AddressString().String());
	fMine->disconnect->SetEnabled(running && fState.radioEnabled
		&& device.connected);
	fMine->forget->SetEnabled(running && fPairing == 0);

	// Battery: only what the device reports while connected.
	if (device.connected && device.battery >= 0) {
		BString level;
		level << device.battery << " %";
		fMine->batteryLabel->SetText(B_TRANSLATE("Battery:"));
		fMine->battery->SetText(level.String());
		_ShowBattery(true);
	} else
		_ShowBattery(false);

	// A headset: its sound choice. A game controller: the button test.
	const bool audio = device.IsAudio();
	if (audio == fMine->useForSound->IsHidden(fMine->useForSound)) {
		if (audio)
			fMine->useForSound->Show();
		else
			fMine->useForSound->Hide();
	}
	fMine->useForSound->SetValue(fMine->hasSound
		&& bdaddrUtils::Compare(fMine->soundAddress, device.info.address)
			? B_CONTROL_ON : B_CONTROL_OFF);
	fMine->useForSound->SetEnabled(running);
	const bool controller = device.IsGameController();
	if (controller == fMine->testButtons->IsHidden(fMine->testButtons)) {
		if (controller)
			fMine->testButtons->Show();
		else
			fMine->testButtons->Hide();
	}
	fMine->testButtons->SetEnabled(device.connected);
}


void
BluetoothView::_UpdateControls()
{
	bool running = fState.service == BLUETOOTH_SERVICE_RUNNING;
	const BluetoothRadioInfo* radio = fState.CurrentRadio();

	fRadioSwitch->SetValue(fState.radioEnabled ? B_CONTROL_ON
		: B_CONTROL_OFF);
	fRadioSwitch->SetEnabled(running && fRadioSwitchAllowed);
	fRadioField->SetEnabled(running && fState.radios.size() > 1
		&& !fState.scanning);

	fScanButton->SetLabel(fState.scanning ? B_TRANSLATE("Stop")
		: B_TRANSLATE("Scan"));
	fScanButton->SetEnabled(running && fState.radioEnabled && radio != NULL);
	if (BButton* pairButton = pair_button(this)) {
		pairButton->SetEnabled(running && fState.radioEnabled && radio != NULL
			&& fDeviceList->CurrentSelection() >= 0 && fPairing == 0);
	}
	_UpdateInfo();

	BString status;
	if (!running || radio == NULL)
		status = "";
	else if (!fState.radioEnabled)
		status = B_TRANSLATE("Bluetooth is off.");
	else if (fState.scanning)
		status = B_TRANSLATE("Looking for devices" B_UTF8_ELLIPSIS);
	else if (fState.scanned) {
		static BStringFormat format(B_TRANSLATE("{0, plural, "
			"=0{No new devices found.} one{# new device found.} "
			"other{# new devices found.}}"));
		format.Format(status, (int32)fDeviceList->CountItems());
	} else {
		status = B_TRANSLATE("Make sure the other device can be found, then "
			"press Scan.");
	}
	// The pairing status replaces the device count until the next scan.
	if (running && fState.radioEnabled && !fState.scanning
		&& fPairStatus->Length() > 0)
		status = *fPairStatus;
	fScanStatus->SetText(status.String());
}
