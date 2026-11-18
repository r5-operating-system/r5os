/*
 * Copyright 2026, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#include <R5WiFiView.h>

#include <Alert.h>
#include <Window.h>
#include <Application.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <IconView.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Resources.h>
#include <Roster.h>
#include <ScrollView.h>
#include <SeparatorView.h>
#include <StringView.h>
#include <TextControl.h>

#include <R5WiFiJoinOther.h>
#include <R5WiFiListView.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "WiFiView"

#define QUOTED_NAME B_UTF8_OPEN_QUOTE "%name%" B_UTF8_CLOSE_QUOTE


static const uint32 kMsgPower = 'wpow';
static const uint32 kMsgDevice = 'wdev';
static const uint32 kMsgDisconnect = 'wdis';
static const uint32 kMsgKnownSelected = 'wkns';
static const uint32 kMsgOtherSelected = 'wots';
static const uint32 kMsgInvoke = 'winv';
static const uint32 kMsgJoinSelected = 'wjsl';
static const uint32 kMsgForget = 'wfgt';
static const uint32 kMsgForgetConfirmed = 'wfgc';
static BMessenger sConnectWindow;
static int32 sConnectToken = 0;
static bool sUserJoin = false;
static const uint32 kMsgAdvanced = 'wadv';

static const bigtime_t kPanelScanInterval = 30000000;
static const bigtime_t kRequestLifetime = 20000000;


static BStringView*
MakeHeading(const char* name, const char* label)
{
	BStringView* view = new BStringView(name, label);
	BFont font(be_bold_font);
	view->SetFont(&font);
	return view;
}


WiFiView::WiFiView(bool settingsPage)
	:
	BGroupView("wifi", B_VERTICAL),
	fSettingsPage(settingsPage),
	fController(NULL),
	fHasState(false),
	fShowingNetServerError(false),
	fRequestedAuthentication(B_NETWORK_AUTHENTICATION_NONE),
	fRequestedTime(0)
{
	IconView* icon = NULL;
	if (!settingsPage) {
		icon = new IconView(B_LARGE_ICON);
		size_t iconSize;
		const uint8* iconData = (const uint8*)
			be_app->AppResources()->LoadResource(B_VECTOR_ICON_TYPE,
				"BEOS:ICON", &iconSize);
		if (iconData != NULL)
			icon->SetIcon(iconData, iconSize);
	}

	fPowerCheck = new BCheckBox("power", B_TRANSLATE("Wi-Fi"),
		new BMessage(kMsgPower));
	BFont bold(be_bold_font);
	fPowerCheck->SetFont(&bold);
	fPowerCheck->SetEnabled(false);

	fDeviceMenu = new BPopUpMenu("adapters");
	fDeviceField = new BMenuField("adapter", B_TRANSLATE("Adapter:"),
		fDeviceMenu);

	fCurrentSignal = new SignalView("signal");
	fCurrentName = new BStringView("current name",
		B_TRANSLATE("Looking for Wi-Fi adapters" B_UTF8_ELLIPSIS));
	fCurrentName->SetFont(&bold);
	fCurrentName->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fCurrentStatus = new BStringView("current status", "");
	fCurrentStatus->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fDisconnectButton = new BButton("disconnect", B_TRANSLATE("Disconnect"),
		new BMessage(kMsgDisconnect));
	fDisconnectButton->SetEnabled(false);

	fKnownLabel = MakeHeading("known label", B_TRANSLATE("Known networks"));
	fKnownList = new NetworkListView("known networks");
	fKnownList->SetSelectionMessage(new BMessage(kMsgKnownSelected));
	fKnownList->SetInvocationMessage(new BMessage(kMsgInvoke));
	fForgetButton = new BButton("forget",
		B_TRANSLATE("Forget" B_UTF8_ELLIPSIS), new BMessage(kMsgForget));
	fForgetButton->SetEnabled(false);

	fOtherLabel = MakeHeading("other label", B_TRANSLATE("Other networks"));
	fScanLabel = new BStringView("scanning", "");
	fScanLabel->SetHighUIColor(B_PANEL_TEXT_COLOR, B_LIGHTEN_1_TINT);
	fOtherList = new NetworkListView("other networks");
	fOtherList->SetSelectionMessage(new BMessage(kMsgOtherSelected));
	fOtherList->SetInvocationMessage(new BMessage(kMsgInvoke));

	BScrollView* knownScroll = new BScrollView("known scroll", fKnownList,
		0, false, true);
	knownScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET,
		be_plain_font->Size() * 6));
	BScrollView* otherScroll = new BScrollView("other scroll", fOtherList,
		0, false, true);
	otherScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET,
		be_plain_font->Size() * 10));

	fMessageView = new BStringView("message", "");
	fMessageView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	BButton* joinOther = new BButton("join other",
		B_TRANSLATE("Join other network" B_UTF8_ELLIPSIS),
		new BMessage(kMsgWiFiJoinOther));
	BButton* advanced = new BButton("advanced",
		B_TRANSLATE("Advanced" B_UTF8_ELLIPSIS), new BMessage(kMsgAdvanced));
	fJoinButton = new BButton("join", B_TRANSLATE("Join"),
		new BMessage(kMsgJoinSelected));
	fJoinButton->SetEnabled(false);

	BGroupLayout* header = new BGroupLayout(B_HORIZONTAL);
	if (icon != NULL)
		header->AddView(icon);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(settingsPage ? 0 : B_USE_WINDOW_SPACING)
		.AddGroup(header)
			.Add(fPowerCheck)
			.AddGlue()
			.Add(fDeviceField)
		.End()
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(fCurrentSignal)
			.AddGroup(B_VERTICAL, 0)
				.Add(fCurrentName)
				.Add(fCurrentStatus)
			.End()
			.Add(fDisconnectButton)
		.End()
		.Add(new BSeparatorView(B_HORIZONTAL))
		.AddGroup(B_HORIZONTAL)
			.Add(fKnownLabel)
			.AddGlue()
			.Add(fForgetButton)
		.End()
		.Add(knownScroll, 1)
		.AddGroup(B_HORIZONTAL)
			.Add(fOtherLabel)
			.AddGlue()
			.Add(fScanLabel)
		.End()
		.Add(otherScroll, 3)
		.Add(fMessageView)
		.AddGroup(B_HORIZONTAL)
			.Add(joinOther)
			.Add(advanced)
			.AddGlue()
			.Add(fJoinButton)
		.End();

	fDeviceField->Hide();
}


WiFiView::~WiFiView()
{
	if (sConnectWindow.IsValid())
		sConnectWindow.PostMessage(B_QUIT_REQUESTED);
	sConnectWindow = BMessenger();
	_StopController();
}


static void
target_invokers(BView* parent, BHandler* target)
{
	for (int32 i = 0; i < parent->CountChildren(); i++) {
		BView* child = parent->ChildAt(i);
		BInvoker* invoker = dynamic_cast<BInvoker*>(child);
		if (invoker != NULL)
			invoker->SetTarget(target);
		target_invokers(child, target);
	}
}


void
WiFiView::AttachedToWindow()
{
	BGroupView::AttachedToWindow();

	// The controls talk to this view, not to the window it sits in
	target_invokers(this, this);
	fDeviceMenu->SetTargetForItems(this);
	// In Settings the window's own controls keep Enter; the password field
	// joins on Enter by itself.
	if (!fSettingsPage)
		Window()->SetDefaultButton(fJoinButton);
	Window()->SetPulseRate(350000);

	if (fController == NULL) {
		fController = new WiFiController(BMessenger(this), "panel",
			kPanelScanInterval);
		if (fController->Start() == B_OK)
			fControllerMessenger = BMessenger(fController);
		else {
			delete fController;
			fController = NULL;
			_SetMessage(B_TRANSLATE("Could not start the Wi-Fi monitor."),
				true);
		}
	}
}


void
WiFiView::DetachedFromWindow()
{
	if (sConnectWindow.IsValid())
		sConnectWindow.PostMessage(B_QUIT_REQUESTED);
	sConnectWindow = BMessenger();
	_StopController();
	BGroupView::DetachedFromWindow();
}


void
WiFiView::_StopController()
{
	if (fControllerMessenger.IsValid())
		fControllerMessenger.SendMessage(B_QUIT_REQUESTED);
	fControllerMessenger = BMessenger();
	fController = NULL;
}


void
WiFiView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgWiFiStateChanged:
		{
			WiFiState state;
			if (state.Unarchive(*message) == B_OK)
				_SetState(state);
			break;
		}

		case kMsgWiFiJoinResult:
			_JoinResult(message);
			break;

		case kMsgWiFiConnectClosed:
			if (message->GetInt32("token", 0) == sConnectToken)
				sConnectWindow = BMessenger();
			break;

		case kMsgPower:
		{
			BMessage power(kMsgWiFiSetPower);
			power.AddBool("on", fPowerCheck->Value() == B_CONTROL_ON);
			fControllerMessenger.SendMessage(&power);
			break;
		}

		case kMsgDevice:
		{
			const char* device;
			if (message->FindString("device", &device) == B_OK) {
				BMessage select(kMsgWiFiSelectDevice);
				select.AddString("device", device);
				fControllerMessenger.SendMessage(&select);
			}
			break;
		}

		case kMsgDisconnect:
			fControllerMessenger.SendMessage(kMsgWiFiLeave);
			break;

		case kMsgKnownSelected:
		case kMsgOtherSelected:
		{
			NetworkListView* list = message->what == kMsgKnownSelected
				? fKnownList : fOtherList;
			NetworkListView* other = list == fKnownList
				? fOtherList : fKnownList;
			const WiFiNetworkInfo* selected = list->SelectedNetwork();
			if (selected != NULL)
				other->DeselectAll();
			_UpdateButtons();
			break;
		}

		case kMsgInvoke:
		case kMsgJoinSelected:
			_JoinSelected();
			break;

		case kMsgForget:
			_Forget();
			break;

		case kMsgForgetConfirmed:
		{
			const char* name;
			if (message->GetInt32("which", 1) == 0
				&& message->FindString("name", &name) == B_OK) {
				BMessage forget(kMsgWiFiForget);
				forget.AddString("name", name);
				fControllerMessenger.SendMessage(&forget);
			}
			break;
		}

		case kMsgAdvanced:
			be_roster->Launch("application/x-vnd.Haiku-Network");
			break;

		case kMsgWiFiJoinOther:
			_ShowJoinOther();
			break;

		case kMsgWiFiSelectNetwork:
		{
			// Sent by the WiFiStatus applet for a network needing a password
			const char* name;
			if (message->FindString("name", &name) != B_OK)
				break;
			const char* device;
			if (message->FindString("device", &device) == B_OK
				&& fState.device != device) {
				BMessage select(kMsgWiFiSelectDevice);
				select.AddString("device", device);
				fControllerMessenger.SendMessage(&select);
			}
			uint32 authentication = message->GetUInt32("authentication",
				B_NETWORK_AUTHENTICATION_WPA2);
			fRequestedName = name;
			fRequestedAuthentication = authentication;
			fRequestedTime = system_time();
			if (!fOtherList->Select(name))
				fKnownList->Select(name);
			_ShowPrompt(name, authentication);
			break;
		}

		default:
			BGroupView::MessageReceived(message);
			break;
	}
}


void
WiFiView::_SetState(const WiFiState& state)
{
	fState = state;
	fHasState = true;

	bool available = state.state != WIFI_STATE_NO_ADAPTER;
	bool on = available && state.state != WIFI_STATE_OFF;
	fPowerCheck->SetEnabled(available);
	fPowerCheck->SetValue(on ? B_CONTROL_ON : B_CONTROL_OFF);

	_UpdateDevices();
	_UpdateCurrent();

	std::vector<WiFiNetworkInfo> known;
	std::vector<WiFiNetworkInfo> other;
	for (size_t i = 0; i < state.networks.size(); i++) {
		// The current network is shown at the top, and among the known
		// networks if it is saved, so that it can be forgotten.
		const WiFiNetworkInfo& info = state.networks[i];
		if (info.saved)
			known.push_back(info);
		else if (info.inRange && (info.name != state.current || !on))
			other.push_back(info);
	}
	fKnownList->SetShowAvailability(on);
	fKnownList->SetNetworks(known);
	fOtherList->SetNetworks(other);

	if (!fRequestedName.IsEmpty()) {
		if (system_time() - fRequestedTime > kRequestLifetime)
			fRequestedName = "";
		else if (fOtherList->SelectedNetwork() == NULL
			&& fKnownList->SelectedNetwork() == NULL) {
			if (fOtherList->Select(fRequestedName)
				|| fKnownList->Select(fRequestedName))
				fRequestedName = "";
		}
	}

	fScanLabel->SetText(state.scanning && on
		? B_TRANSLATE("Scanning" B_UTF8_ELLIPSIS) : "");

	if (!state.netServerRunning && !fShowingNetServerError) {
		_SetMessage(B_TRANSLATE("The network service is not responding; "
			"saved networks are unavailable."), true);
		fShowingNetServerError = true;
	} else if (state.netServerRunning && fShowingNetServerError) {
		_SetMessage("", false);
		fShowingNetServerError = false;
	}

	_UpdateButtons();
}


static int
WifiStandardRank(const BString& name)
{
	if (name == "Wi-Fi 7")
		return 7;
	if (name == "Wi-Fi 6E")
		return 6;
	if (name == "Wi-Fi 6")
		return 5;
	if (name == "Wi-Fi 5")
		return 4;
	if (name == "Wi-Fi 4")
		return 3;
	return 0;
}


void
WiFiView::_UpdateCurrent()
{
	BString name;
	BString status;
	int32 bars = 0;
	bool enabled = false;
	bool animate = false;

	const WiFiNetworkInfo* current = fState.FindNetwork(fState.current);
	if (current != NULL && current->inRange)
		bars = current->bars;

	switch (fState.state) {
		case WIFI_STATE_NO_ADAPTER:
			name = B_TRANSLATE("No Wi-Fi adapter found");
			break;
		case WIFI_STATE_OFF:
			name = B_TRANSLATE("Wi-Fi is off");
			status = B_TRANSLATE("Turn Wi-Fi on to see nearby networks.");
			break;
		case WIFI_STATE_DISCONNECTED:
			name = B_TRANSLATE("Not connected");
			status = B_TRANSLATE("Choose a network to join.");
			break;
		case WIFI_STATE_CONNECTING:
			name = fState.current;
			status = B_TRANSLATE("Connecting" B_UTF8_ELLIPSIS);
			animate = true;
			break;
		case WIFI_STATE_OBTAINING_ADDRESS:
			name = fState.current;
			status = B_TRANSLATE("Connected, obtaining an IP address"
				B_UTF8_ELLIPSIS);
			enabled = true;
			break;
		case WIFI_STATE_NO_INTERNET:
			name = fState.current;
			status = B_TRANSLATE("Connected, no Internet (%address%)");
			status.ReplaceFirst("%address%", fState.address);
			enabled = true;
			break;
		case WIFI_STATE_CONNECTED:
			name = fState.current;
			status = B_TRANSLATE("Connected (%address%)");
			status.ReplaceFirst("%address%", fState.address);
			enabled = true;
			break;
	}

	BString linkLine = WiFiLinkLine(fState.link);
	if (linkLine.Length() > 0 && status.Length() > 0)
		status << " · " << linkLine;
	else if (linkLine.Length() > 0)
		status = linkLine;

	fCurrentName->SetText(name);
	fCurrentStatus->SetText(status);
	fCurrentSignal->SetSignal(bars, enabled, animate);

	/* What the router offers goes in the details, never in the status line,
	 * which says what this link uses. The hint shows only when the router
	 * offers a newer standard than the link uses.
	 */
	BString routerLine;
	const BString& offered = fState.link.routerStandard;
	if (fState.IsAssociated() && offered.Length() > 0) {
		routerLine = B_TRANSLATE("Router: %standard%");
		routerLine.ReplaceFirst("%standard%", offered);
		const BString& used = fState.link.standard;
		if (WifiStandardRank(used) < WifiStandardRank(offered)) {
			BString hint = B_TRANSLATE("Your router supports %standard%.");
			hint.ReplaceFirst("%standard%", offered);
			routerLine << "\n" << hint;
		}
	}

	if (current != NULL && current->inRange) {
		BString tip = NetworkToolTip(*current);
		if (routerLine.Length() > 0)
			tip << "\n" << routerLine;
		fCurrentSignal->SetToolTip(tip.String());
		fCurrentStatus->SetToolTip(tip.String());
	} else {
		fCurrentSignal->SetToolTip((const char*)NULL);
		fCurrentStatus->SetToolTip((const char*)NULL);
	}

	fDisconnectButton->SetEnabled(fState.IsAssociated()
		|| fState.state == WIFI_STATE_CONNECTING);
}


void
WiFiView::_UpdateDevices()
{
	bool changed = fDeviceMenu->CountItems() != (int32)fState.devices.size();
	for (int32 i = 0; !changed && i < fDeviceMenu->CountItems(); i++) {
		if (fState.devices[i] != fDeviceMenu->ItemAt(i)->Label())
			changed = true;
	}

	if (changed) {
		fDeviceMenu->RemoveItems(0, fDeviceMenu->CountItems(), true);
		for (size_t i = 0; i < fState.devices.size(); i++) {
			BMessage* message = new BMessage(kMsgDevice);
			message->AddString("device", fState.devices[i]);
			fDeviceMenu->AddItem(new BMenuItem(fState.devices[i], message));
		}
		fDeviceMenu->SetTargetForItems(this);
	}

	BMenuItem* item = fDeviceMenu->FindItem(fState.device);
	if (item != NULL && !item->IsMarked())
		item->SetMarked(true);

	bool show = fState.devices.size() > 1;
	if (show && fDeviceField->IsHidden())
		fDeviceField->Show();
	else if (!show && !fDeviceField->IsHidden())
		fDeviceField->Hide();
}


void
WiFiView::_UpdateButtons()
{
	const WiFiNetworkInfo* selected = _SelectedNetwork();
	bool on = fState.state != WIFI_STATE_NO_ADAPTER
		&& fState.state != WIFI_STATE_OFF;
	bool isCurrent = selected != NULL && selected->name == fState.current;
	fJoinButton->SetEnabled(on && selected != NULL && selected->inRange
		&& !isCurrent);
	fForgetButton->SetEnabled(fKnownList->SelectedNetwork() != NULL);
}


void
WiFiView::_JoinResult(BMessage* message)
{
	const char* name = message->GetString("name", "");
	status_t status = message->GetInt32("status", B_ERROR);
	const char* error = message->GetString("error", NULL);

	if (fJoiningName == name)
		fJoiningName = "";

	if (status == B_OK || status == B_CANCELED)
		sUserJoin = false;

	bool shown = sConnectWindow.IsValid();
	if (!shown && status != B_OK && status != B_CANCELED && sUserJoin) {
		const WiFiNetworkInfo* info = fState.FindNetwork(name);
		uint32 authentication = info != NULL ? info->authentication
			: B_NETWORK_AUTHENTICATION_WPA2;
		_ShowPrompt(name, authentication);
		shown = sConnectWindow.IsValid();
	}
	if (shown) {
		BMessage result(kMsgWiFiJoinResult);
		result.AddString("name", name);
		result.AddInt32("status", status);
		result.AddBool("key_failed", message->GetBool("key_failed", false));
		if (error != NULL)
			result.AddString("error", error);
		sConnectWindow.PostMessage(&result);
	} else if (status == B_OK) {
		_SetMessage(error != NULL ? error : "", error != NULL);
	} else if (status != B_CANCELED) {
		_SetMessage(error != NULL ? error
			: B_TRANSLATE("Could not connect."), true);
	} else if (error != NULL) {
		_SetMessage(error, false);
	}

	_UpdateButtons();
}


const WiFiNetworkInfo*
WiFiView::_SelectedNetwork() const
{
	const WiFiNetworkInfo* selected = fOtherList->SelectedNetwork();
	if (selected == NULL)
		selected = fKnownList->SelectedNetwork();
	return selected;
}


void
WiFiView::_JoinSelected()
{
	const WiFiNetworkInfo* selected = _SelectedNetwork();
	if (selected == NULL || !selected->inRange
		|| fState.state == WIFI_STATE_OFF
		|| fState.state == WIFI_STATE_NO_ADAPTER) {
		return;
	}
	if (selected->name == fState.current)
		return;

	sUserJoin = true;
	if (selected->secured && !selected->saved) {
		_ShowPrompt(selected->name, selected->authentication);
		return;
	}

	BMessage join(kMsgWiFiJoin);
	join.AddString("name", selected->name);
	join.AddBool("remember", true);
	fJoiningName = selected->name;
	_SetMessage("", false);
	fControllerMessenger.SendMessage(&join);
	_UpdateButtons();
}


void
WiFiView::_ShowPrompt(const char* name, uint32 authentication)
{
	if (sConnectWindow.IsValid())
		sConnectWindow.PostMessage(B_QUIT_REQUESTED);
	sConnectWindow = BMessenger();
	sConnectToken++;
	sUserJoin = true;
	int32 bars = 0;
	const WiFiNetworkInfo* info = fState.FindNetwork(name);
	if (info != NULL)
		bars = info->bars;
	const char* standard = NULL;
	if (fState.current == name && fState.link.routerStandard.Length() > 0)
		standard = fState.link.routerStandard.String();
	BString subtitle;
	r5_wifi_connect_subtitle(subtitle, authentication, bars, standard);
	WiFiConnectWindow* window = new WiFiConnectWindow(fControllerMessenger,
		name, subtitle.String(), authentication, bars, BMessenger(this),
		sConnectToken, -1, NULL);
	sConnectWindow = BMessenger(window);
	window->Show();
}


void
WiFiView::_SetMessage(const char* text, bool error)
{
	fMessageView->SetHighUIColor(error ? B_FAILURE_COLOR
		: B_PANEL_TEXT_COLOR);
	fMessageView->SetText(text);
	fMessageView->SetToolTip(text != NULL && text[0] != '\0' ? text : NULL);
}


void
WiFiView::_Forget()
{
	const WiFiNetworkInfo* selected = fKnownList->SelectedNetwork();
	if (selected == NULL)
		return;

	BString text(B_TRANSLATE("Forget the Wi-Fi network " QUOTED_NAME "?\n\n"
		"Its saved password is removed and it will no longer be joined "
		"automatically."));
	text.ReplaceAll("%name%", selected->name);
	// Keep the harmless choice as the default button
	BAlert* alert = new BAlert(B_TRANSLATE("Forget network"), text,
		B_TRANSLATE("Forget"), B_TRANSLATE("Cancel"), NULL,
		B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(1, B_ESCAPE);
	BMessage* message = new BMessage(kMsgForgetConfirmed);
	message->AddString("name", selected->name);
	alert->Go(new BInvoker(message, this));
}


void
WiFiView::_ShowJoinOther()
{
	if (fState.state == WIFI_STATE_NO_ADAPTER) {
		_SetMessage(B_TRANSLATE("There is no Wi-Fi adapter."), true);
		return;
	}

	JoinOtherWindow* window = new JoinOtherWindow(Window(),
		fControllerMessenger);
	window->Show();
}
