/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * A notification when a pairing started on the Bluetooth page ends
 * (docs/BLUETOOTH.md, steps 4 and 6). It replaces Haiku's "Incoming
 * Connection" window, which showed at every connection, also for a
 * pairing started here. No address and no key is shown.
 */


#include "PairingNotice.h"

#include <syslog.h>

#include <Catalog.h>
#include <Notification.h>

#include <bluetoothserver_p.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PairingNotice"


void
r5_pairing_result(bool paired, const BString& name, BMessenger* reply,
	const bdaddr_t& address)
{
	BString device = name;
	if (device.Length() == 0)
		device = B_TRANSLATE("the device");

	BString content = paired
		? B_TRANSLATE("Paired with %name%.")
		: B_TRANSLATE("Pairing with %name% did not work. Try again.");
	content.ReplaceFirst("%name%", device);

	BNotification notification(paired ? B_INFORMATION_NOTIFICATION
		: B_ERROR_NOTIFICATION);
	notification.SetGroup("Bluetooth");
	notification.SetTitle(B_TRANSLATE("Bluetooth"));
	notification.SetContent(content);
	notification.SetMessageID("r5-bluetooth-pairing");
	status_t status = notification.Send(5000000);
	syslog(LOG_INFO, "bluetooth_server: pairing %s, notice %s (%#" B_PRIx32 ")",
		paired ? "ok" : "failed", status == B_OK ? "sent" : "not sent",
		status);

	// The page that asked shows the same sentence in its status line.
	if (reply != NULL && reply->IsValid()) {
		BMessage result(BT_MSG_R5_PAIR_RESULT);
		result.AddData("bdaddr", B_RAW_TYPE, &address, sizeof(address));
		result.AddBool("paired", paired);
		result.AddString("name", name);
		reply->SendMessage(&result, (BHandler*)NULL, 1000000);
	}
	if (reply != NULL)
		*reply = BMessenger();
}
