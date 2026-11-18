/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * R5OS: one-screen Wi-Fi join, remember, Deskbar status helpers.
 */


#include "WiFiWindow.h"

#include <R5WiFi.h>

#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <ListView.h>
#include <Messenger.h>
#include <NetServer.h>
#include <NetworkInterface.h>
#include <NetworkRoster.h>
#include <OS.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextControl.h>
#include <Window.h>

#include <stdio.h>
#include <string.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "WiFiWindow"


static const uint32 kMsgRefresh		= 'wref';
static const uint32 kMsgConnect		= 'wcon';

static WiFiWindow* sWindow = NULL;


static const char*
auth_label(uint32 mode)
{
	switch (mode) {
		case B_NETWORK_AUTHENTICATION_WEP:
			return "WEP";
		case B_NETWORK_AUTHENTICATION_WPA:
			return "WPA";
		case B_NETWORK_AUTHENTICATION_WPA2:
			return "WPA2";
		case B_NETWORK_AUTHENTICATION_WPA3:
			return "WPA3";
		case B_NETWORK_AUTHENTICATION_EAP:
			return "EAP";
		default:
			return "open";
	}
}


status_t
r5_wifi_current_ssid(BString& ssid, BString* deviceName)
{
	ssid.Truncate(0);
	BNetworkRoster& roster = BNetworkRoster::Default();
	BNetworkInterface interface;
	uint32 cookie = 0;
	while (roster.GetNextInterface(&cookie, interface) == B_OK) {
		if ((interface.Flags() & IFF_LOOPBACK) != 0)
			continue;
		BNetworkDevice device(interface.Name());
		if (!device.IsWireless())
			continue;
		if (deviceName != NULL)
			*deviceName = interface.Name();
		uint32 associated = 0;
		wireless_network network;
		if (device.GetNextAssociatedNetwork(associated, network) == B_OK
			&& network.name[0] != '\0') {
			ssid = network.name;
			return B_OK;
		}
	}
	return B_ERROR;
}


status_t
r5_wifi_join_and_remember(const char* deviceName, const wireless_network& network,
	const char* password)
{
	if (deviceName == NULL || deviceName[0] == '\0' || network.name[0] == '\0')
		return B_BAD_VALUE;

	// JoinNetwork returns when the request is sent, before the handshake.
	// The password is stored by the Wi-Fi controller after the association,
	// and only when the connect window's checkbox is on.
	BNetworkDevice device(deviceName);
	return device.JoinNetwork(network.name,
		(password != NULL && password[0] != '\0') ? password : NULL);
}


class NetworkListItem : public BStringItem {
public:
	NetworkListItem(const wireless_network& network)
		:
		BStringItem(""),
		fNetwork(network)
	{
		BString label;
		label.SetToFormat("%s  (%s, signal %u)", network.name,
			auth_label(network.authentication_mode),
			(unsigned)network.signal_strength);
		SetText(label.String());
	}

	const wireless_network& Network() const { return fNetwork; }

private:
			wireless_network	fNetwork;
};


WiFiWindow::WiFiWindow()
	:
	BWindow(BRect(0, 0, 420, 360), B_TRANSLATE("Wi-Fi"),
		B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS | B_NOT_ZOOMABLE),
	fList(new BListView("networks")),
	fStatus(new BStringView("status", "")),
	fConnect(new BButton("connect", B_TRANSLATE("Connect"),
		new BMessage(kMsgConnect)))
{
	fList->SetInvocationMessage(new BMessage(kMsgConnect));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(new BStringView("hint",
			B_TRANSLATE("Choose a network and connect. "
				"R5 remembers it for next boot.")))
		.Add(new BScrollView("scroll", fList, 0, false, true))
		.AddGroup(B_HORIZONTAL)
			.Add(fStatus)
			.AddGlue()
			.Add(new BButton("refresh", B_TRANSLATE("Scan"),
				new BMessage(kMsgRefresh)))
			.Add(fConnect)
		.End();

	CenterOnScreen();
	_Refresh();
}


WiFiWindow::~WiFiWindow()
{
	if (sWindow == this)
		sWindow = NULL;
}


void
WiFiWindow::ShowWindow()
{
	if (sWindow == NULL) {
		sWindow = new WiFiWindow();
		sWindow->Show();
		return;
	}
	if (sWindow->Lock()) {
		sWindow->Activate();
		sWindow->_Refresh();
		sWindow->Unlock();
	}
}


bool
WiFiWindow::QuitRequested()
{
	Hide();
	return false;
}


void
WiFiWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgRefresh:
			_Refresh();
			break;
		case kMsgConnect:
			_ConnectSelected();
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
WiFiWindow::_Refresh()
{
	BString ssid;
	r5_wifi_current_ssid(ssid, &fDevice);
	if (ssid.Length() > 0) {
		BString text(B_TRANSLATE("Connected to %ssid%"));
		text.ReplaceFirst("%ssid%", ssid);
		fStatus->SetText(text);
	} else if (fDevice.Length() == 0)
		fStatus->SetText(B_TRANSLATE("No wireless device"));
	else
		fStatus->SetText(B_TRANSLATE("No network"));

	while (fList->CountItems() > 0)
		delete fList->RemoveItem((int32)0);

	if (fDevice.Length() == 0)
		return;

	BNetworkDevice device(fDevice.String());
	device.Scan(false, false);
	uint32 count = 0;
	wireless_network* networks = NULL;
	if (device.GetNetworks(networks, count) != B_OK || networks == NULL)
		return;
	for (uint32 i = 0; i < count; i++) {
		if (networks[i].name[0] == '\0')
			continue;
		NetworkListItem* item = new NetworkListItem(networks[i]);
		fList->AddItem(item);
		if (ssid == networks[i].name)
			fList->Select(fList->CountItems() - 1);
	}
	delete[] networks;
}


void
WiFiWindow::_ConnectSelected()
{
	int32 index = fList->CurrentSelection();
	NetworkListItem* item = dynamic_cast<NetworkListItem*>(
		fList->ItemAt(index));
	if (item == NULL || fDevice.Length() == 0)
		return;

	const wireless_network& network = item->Network();
	int32 bars = (int32)network.signal_strength * 4 / 100;
	if (bars > 4)
		bars = 4;
	BString subtitle;
	r5_wifi_connect_subtitle(subtitle, network.authentication_mode, bars,
		NULL);
	status_t status = r5_wifi_connect_dialog(fDevice.String(), network.name,
		subtitle.String(), network.authentication_mode, bars);
	if (status != B_OK) {
		BString error(B_TRANSLATE("Could not join: %error%"));
		error.ReplaceFirst("%error%", strerror(status));
		fStatus->SetText(error);
		return;
	}
	_Refresh();
}
