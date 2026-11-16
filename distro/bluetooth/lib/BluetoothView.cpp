/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * BluetoothView (libr5bt.so): the radio, the radio switch, and the devices
 * nearby. The summary is a BAppearanceKit::BCard (docs/APPEARANCE-KIT.md);
 * the switch and the list stay on the panel. The Bluetooth glyph and the
 * signal bars come from libr5icons.so.
 */


#include <R5Bluetooth.h>

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
#include <ScrollView.h>
#include <StringFormat.h>
#include <StringView.h>
#include <Window.h>

#include <Card.h>
#include <R5Icons.h>

#include <bluetooth/bdaddrUtils.h>

#include <new>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "BluetoothView"


static const uint32 kMsgRadioSwitch = 'btvS';
static const uint32 kMsgRadioChosen = 'btvR';
static const uint32 kMsgScanButton = 'btvB';


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


// #pragma mark - GlyphView


/*!	R5_GLYPH_BLUETOOTH in the panel text color, faint while off. */
class GlyphView : public BView {
public:
	GlyphView()
		:
		BView("bluetooth glyph", B_WILL_DRAW),
		fActive(false)
	{
		float side = roundf(32.0f * font_scale());
		SetExplicitSize(BSize(side, side));
		SetViewColor(B_TRANSPARENT_COLOR);
	}

	virtual void AttachedToWindow()
	{
		BView::AttachedToWindow();
		AdoptParentColors();
	}

	virtual void Draw(BRect updateRect)
	{
		(void)updateRect;
		SetHighColor(LowColor());
		FillRect(Bounds());
		rgb_color color = ui_color(B_PANEL_TEXT_COLOR);
		if (!fActive)
			color = tint_color(LowColor(), B_DARKEN_2_TINT);
		r5_glyph_draw(this, R5_GLYPH_BLUETOOTH, Bounds(), color);
	}

	void SetActive(bool active)
	{
		if (active == fActive)
			return;
		fActive = active;
		Invalidate();
	}

private:
	bool	fActive;
};


// #pragma mark - DeviceItem


/*!	Two lines: the name, then the kind and the address. The signal bars
	are at the right when the radio reported a signal strength.
*/
class DeviceItem : public BListItem {
public:
	DeviceItem(const BluetoothDeviceInfo& info)
		:
		fInfo(info)
	{
	}

	const BluetoothDeviceInfo& Info() const { return fInfo; }

	virtual void Update(BView* owner, const BFont* font)
	{
		(void)owner;
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
		owner->SetHighColor(tint_color(text, background.IsLight()
			? B_LIGHTEN_1_TINT : B_DARKEN_1_TINT));
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


// #pragma mark - BluetoothView


BluetoothView::BluetoothView(bool settingsPage)
	:
	BGroupView("bluetooth", B_VERTICAL),
	fSettingsPage(settingsPage),
	fRadioSwitchAllowed(true),
	fHasState(false),
	fRadioFailed(false),
	fController(NULL)
{
	BFont bold = bold_font();

	GlyphView* glyph = new GlyphView();
	fSummaryIcon = glyph;
	fSummaryTitle = new BStringView("summary title", "");
	fSummaryTitle->SetFont(&bold);
	fSummaryDetail = new BStringView("summary detail", "");
	fSummaryTitle->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fSummaryDetail->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	BAppearanceKit::BCard* summary = new BAppearanceKit::BCard("summary",
		BAppearanceKit::B_CARD_PANEL, B_HORIZONTAL);
	BLayoutBuilder::Group<>(summary, B_HORIZONTAL)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(glyph)
		.AddGroup(B_VERTICAL, 0)
			.Add(fSummaryTitle)
			.Add(fSummaryDetail)
		.End()
		.AddGlue();

	// The box title is the switch, like "Enable notifications".
	fRadioSwitch = new BCheckBox("radio switch", B_TRANSLATE("Bluetooth"),
		new BMessage(kMsgRadioSwitch));
	fRadioSwitch->SetFont(&bold);
	fRadioMenu = new BPopUpMenu(B_TRANSLATE("No radio"));
	fRadioField = new BMenuField("radio", B_TRANSLATE("Radio:"), fRadioMenu);
	fRadioNote = new BStringView("radio note", "");
	fRadioNote->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	BBox* radioBox = new BBox("radio box");
	radioBox->SetLabel(fRadioSwitch);
	BGroupView* radioInner = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
	BLayoutBuilder::Group<>(radioInner)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(fRadioField)
		.Add(fRadioNote);
	radioBox->AddChild(radioInner);

	fDeviceList = new BListView("devices", B_SINGLE_SELECTION_LIST);
	BScrollView* scroller = new BScrollView("devices scroll", fDeviceList, 0,
		false, true);
	scroller->SetExplicitMinSize(BSize(B_SIZE_UNSET,
		roundf(150 * font_scale())));
	fScanStatus = new BStringView("scan status", "");
	fScanStatus->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fScanButton = new BButton("scan", B_TRANSLATE("Scan"),
		new BMessage(kMsgScanButton));

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
			.Add(fScanButton)
		.End();
	devicesBox->AddChild(devicesInner);

	BLayoutBuilder::Group<>(this)
		.SetInsets(settingsPage ? 0 : B_USE_WINDOW_SPACING)
		.Add(summary)
		.Add(radioBox)
		.Add(devicesBox)
		.AddGlue();

	_SetState(fState);
}


BluetoothView::~BluetoothView()
{
	_StopController();
	for (int32 i = fDeviceList->CountItems() - 1; i >= 0; i--)
		delete fDeviceList->RemoveItem(i);
}


void
BluetoothView::AttachedToWindow()
{
	BGroupView::AttachedToWindow();

	fRadioSwitch->SetTarget(this);
	fScanButton->SetTarget(this);
	fRadioMenu->SetTargetForItems(this);

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
			if (state.Unarchive(*message) == B_OK)
				_SetState(state);
			break;
		}

		case kMsgBluetoothRadioResult:
		{
			// The state message that follows sets the switch back when the
			// change did not take.
			fRadioFailed = message->GetInt32("status", B_ERROR) != B_OK;
			_UpdateRadio();
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
			BMessage request(fState.scanning ? kMsgBluetoothCancelScan
				: kMsgBluetoothScan);
			_SendToController(&request);
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
	fState = state;
	fHasState = true;

	_UpdateRadio();
	if (devicesChanged)
		_UpdateDevices();
	_UpdateControls();
}


void
BluetoothView::_UpdateRadio()
{
	BString title;
	BString detail;
	const BluetoothRadioInfo* radio = fState.CurrentRadio();

	switch (fState.service) {
		case BLUETOOTH_SERVICE_STARTING:
			title = B_TRANSLATE("Starting Bluetooth" B_UTF8_ELLIPSIS);
			break;
		case BLUETOOTH_SERVICE_FAILED:
			title = B_TRANSLATE("Bluetooth is not available");
			detail = B_TRANSLATE("The Bluetooth service could not be "
				"started.");
			break;
		case BLUETOOTH_SERVICE_STOPPED:
			title = B_TRANSLATE("Bluetooth is not available");
			detail = B_TRANSLATE("The Bluetooth service stopped.");
			break;
		default:
		{
			// "0 radios" is also the pass line of docs/BLUETOOTH.md, step 3a.
			static BStringFormat format(B_TRANSLATE(
				"{0, plural, one{# radio} other{# radios}}"));
			format.Format(title, (int32)fState.radios.size());
			if (radio != NULL) {
				detail = radio->name.IsEmpty()
					? BString(B_TRANSLATE("Bluetooth radio")) : radio->name;
				detail << " \xC2\xB7 " << bdaddrUtils::ToString(radio->address);
			} else if (fState.radios.empty())
				detail = B_TRANSLATE("No Bluetooth radio was found.");
			break;
		}
	}

	fSummaryTitle->SetText(title.String());
	fSummaryDetail->SetText(detail.String());
	static_cast<GlyphView*>(fSummaryIcon)->SetActive(
		fState.service == BLUETOOTH_SERVICE_RUNNING && fState.radioEnabled
		&& radio != NULL);

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

	BString note;
	if (fState.service == BLUETOOTH_SERVICE_RUNNING) {
		if (fRadioFailed) {
			note = B_TRANSLATE("Bluetooth could not be turned on or off.");
		} else if (!fRadioSwitchAllowed) {
			note = B_TRANSLATE("Only an administrator can turn Bluetooth on "
				"or off.");
		} else if (!fState.radioEnabled) {
			note = B_TRANSLATE("This computer does not look for devices and "
				"does not accept connections.");
		}
	}
	if (note.IsEmpty()) {
		note = B_TRANSLATE("Turns Bluetooth on or off for everyone who uses "
			"this computer.");
	}
	fRadioNote->SetText(note.String());
}


void
BluetoothView::_UpdateDevices()
{
	BString selected;
	DeviceItem* current = dynamic_cast<DeviceItem*>(
		fDeviceList->ItemAt(fDeviceList->CurrentSelection()));
	if (current != NULL)
		selected = current->Info().AddressString();

	for (int32 i = fDeviceList->CountItems() - 1; i >= 0; i--)
		delete fDeviceList->RemoveItem(i);

	for (size_t i = 0; i < fState.devices.size(); i++) {
		DeviceItem* item = new DeviceItem(fState.devices[i]);
		fDeviceList->AddItem(item);
		if (!selected.IsEmpty() && item->Info().AddressString() == selected)
			fDeviceList->Select(fDeviceList->CountItems() - 1);
	}
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

	BString status;
	if (!running || radio == NULL)
		status = "";
	else if (!fState.radioEnabled)
		status = B_TRANSLATE("Bluetooth is off.");
	else if (fState.scanning)
		status = B_TRANSLATE("Looking for devices" B_UTF8_ELLIPSIS);
	else if (fState.scanned) {
		static BStringFormat format(B_TRANSLATE("{0, plural, "
			"=0{No devices found.} one{# device found.} "
			"other{# devices found.}}"));
		format.Format(status, (int32)fState.devices.size());
	} else {
		status = B_TRANSLATE("Make sure the other device can be found, then "
			"press Scan.");
	}
	fScanStatus->SetText(status.String());
}
