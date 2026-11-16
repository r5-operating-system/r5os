/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 */


#include "PasskeyWindow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Application.h>
#include <Button.h>
#include <Catalog.h>
#include <Font.h>
#include <LayoutBuilder.h>
#include <StringView.h>
#include <TextControl.h>

#include <bluetooth/HCI/btHCI_command.h>
#include <bluetooth/HCI/btHCI_event.h>
#include <bluetoothserver_p.h>
#include <CommandManager.h>

#include "R5Pairing.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PasskeyWindow"


enum {
	kMsgPair	= 'r5pp',
	kMsgCancel	= 'r5pc'
};


PasskeyWindow::PasskeyWindow(const bdaddr_t& address, hci_id hid,
	const BString& name)
	:
	BWindow(BRect(0, 0, 10, 10), B_TRANSLATE("Bluetooth pairing"),
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_NOT_RESIZABLE
			| B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fAddress(address),
	fHid(hid),
	fEntry(true),
	fCompare(false),
	fLE(false),
	fAnswered(false),
	fPasskey(0),
	fTyped(0),
	fPasskeyText(NULL),
	fTypedView(NULL),
	fPairButton(NULL)
{
	_Build(name);
}


PasskeyWindow::PasskeyWindow(const bdaddr_t& address, hci_id hid,
	const BString& name, uint32 passkey)
	:
	BWindow(BRect(0, 0, 10, 10), B_TRANSLATE("Bluetooth pairing"),
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_NOT_RESIZABLE
			| B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fAddress(address),
	fHid(hid),
	fEntry(false),
	fCompare(false),
	fLE(false),
	fAnswered(true),
	fPasskey(passkey),
	fTyped(0),
	fPasskeyText(NULL),
	fTypedView(NULL),
	fPairButton(NULL)
{
	_Build(name);
}


PasskeyWindow::PasskeyWindow(const bdaddr_t& address, hci_id hid,
	const BString& name, uint32 number, bool compare, bool le)
	:
	BWindow(BRect(0, 0, 10, 10), B_TRANSLATE("Bluetooth pairing"),
		B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_NOT_RESIZABLE
			| B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fAddress(address),
	fHid(hid),
	fEntry(!compare && number == kLEEntry),
	fCompare(compare),
	fLE(le),
	fAnswered(!compare && number != kLEEntry),
	fPasskey(number == kLEEntry ? 0 : number),
	fTyped(0),
	fPasskeyText(NULL),
	fTypedView(NULL),
	fPairButton(NULL)
{
	_Build(name);
}


void
PasskeyWindow::_Build(const BString& name)
{
	BString text;
	BLayoutBuilder::Group<> builder(this, B_VERTICAL);
	builder.SetInsets(B_USE_WINDOW_SPACING);

	if (fCompare) {
		text = B_TRANSLATE("Does %name% show this number?");
		text.ReplaceFirst("%name%", name);

		char digits[8];
		snprintf(digits, sizeof(digits), "%06" B_PRIu32, fPasskey);
		BStringView* numberView = new BStringView("number", digits);
		BFont font(be_bold_font);
		font.SetSize(font.Size() * 2.5f);
		numberView->SetFont(&font);
		numberView->SetAlignment(B_ALIGN_CENTER);
		memset(digits, 0, sizeof(digits));

		fPairButton = new BButton("pair", B_TRANSLATE("Yes, pair"),
			new BMessage(kMsgPair));
		builder.Add(new BStringView("text", text))
			.Add(numberView)
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(new BButton("cancel", B_TRANSLATE("No"),
					new BMessage(kMsgCancel)))
				.Add(fPairButton)
			.End();
		SetDefaultButton(fPairButton);
	} else if (fEntry) {
		text = B_TRANSLATE("Type the number that %name% shows.");
		text.ReplaceFirst("%name%", name);
		fPasskeyText = new BTextControl("passkey", NULL, "", NULL);
		fPasskeyText->TextView()->SetMaxBytes(6);
		for (uint32 c = 0; c < 128; c++) {
			if (c < '0' || c > '9')
				fPasskeyText->TextView()->DisallowChar(c);
		}
		fPairButton = new BButton("pair", B_TRANSLATE("Pair"),
			new BMessage(kMsgPair));

		builder.Add(new BStringView("text", text))
			.Add(fPasskeyText)
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(new BButton("cancel", B_TRANSLATE("Cancel"),
					new BMessage(kMsgCancel)))
				.Add(fPairButton)
			.End();
		SetDefaultButton(fPairButton);
		fPasskeyText->MakeFocus(true);
	} else {
		text = B_TRANSLATE("Type this number on %name%, then press Enter.");
		text.ReplaceFirst("%name%", name);

		char digits[8];
		snprintf(digits, sizeof(digits), "%06" B_PRIu32, fPasskey);
		BStringView* passkeyView = new BStringView("passkey", digits);
		BFont font(be_bold_font);
		font.SetSize(font.Size() * 2.5f);
		passkeyView->SetFont(&font);
		passkeyView->SetAlignment(B_ALIGN_CENTER);
		memset(digits, 0, sizeof(digits));

		fTypedView = new BStringView("typed", "");
		BButton* close = new BButton("close", B_TRANSLATE("Close"),
			new BMessage(B_QUIT_REQUESTED));

		builder.Add(new BStringView("text", text))
			.Add(passkeyView)
			.Add(fTypedView)
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(close)
			.End();
		SetDefaultButton(close);
		_UpdateTyped();
	}

	CenterOnScreen();
}


void
PasskeyWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgPair:
			_SendReply(true);
			if (fAnswered)
				Quit();
			break;

		case kMsgCancel:
			_SendReply(false);
			Quit();
			break;

		case kMsgPasskeyKeypress:
		{
			// 0 started, 1 digit entered, 2 digit erased, 3 cleared,
			// 4 completed (7.7.49).
			uint8 type;
			if (message->FindUInt8("type", &type) != B_OK)
				break;
			if (type == 1)
				fTyped++;
			else if (type == 2 && fTyped > 0)
				fTyped--;
			else if (type == 0 || type == 3)
				fTyped = 0;
			_UpdateTyped();
			break;
		}

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool
PasskeyWindow::QuitRequested()
{
	// Closing the entry window without an answer refuses the pairing at
	// once, instead of letting the remote wait for its timeout.
	if (!fAnswered)
		_SendReply(false);
	return true;
}


void
PasskeyWindow::_SendReply(bool accept)
{
	if (fAnswered)
		return;

	uint32 passkey = 0;
	if (accept && fEntry) {
		const char* text = fPasskeyText->Text();
		size_t length = strlen(text);
		if (length == 0 || length > 6
			|| strspn(text, "0123456789") != length) {
			fPasskeyText->MarkAsInvalid(true);
			return;
		}
		passkey = (uint32)strtoul(text, NULL, 10);
		if (passkey > R5_PASSKEY_MAX) {
			fPasskeyText->MarkAsInvalid(true);
			return;
		}
	}

	if (fLE) {
		// LE pairing runs in LEManager, not in the radio.
		BMessage answer(kMsgLEUserAnswer);
		answer.AddData("bdaddr", B_RAW_TYPE, &fAddress, sizeof(fAddress));
		answer.AddBool("accept", accept);
		answer.AddUInt32("passkey", passkey);
		be_app_messenger.SendMessage(&answer);
		passkey = 0;
		fAnswered = true;
		return;
	}

	BMessage request(BT_MSG_HANDLE_SIMPLE_REQUEST);
	request.AddInt32("hci_id", fHid);
	request.AddInt16("eventExpected", HCI_EVENT_CMD_COMPLETE);

	if (fCompare) {
		// Numeric Comparison: User Confirmation Reply or Negative Reply
		// (Core 6.0, Vol 4, Part E, 7.1.30 and 7.1.31).
		if (accept) {
			BluetoothCommand<typed_command(hci_cp_user_confirm_reply)>
				command(OGF_LINK_CONTROL, OCF_USER_CONFIRM_REPLY);
			command->bdaddr = fAddress;
			request.AddData("raw command", B_ANY_TYPE, command.Data(),
				command.Size());
			request.AddInt16("opcodeExpected",
				PACK_OPCODE(OGF_LINK_CONTROL, OCF_USER_CONFIRM_REPLY));
		} else {
			BluetoothCommand<typed_command(hci_cp_user_confirm_neg_reply)>
				command(OGF_LINK_CONTROL, OCF_USER_CONFIRM_NEG_REPLY);
			command->bdaddr = fAddress;
			request.AddData("raw command", B_ANY_TYPE, command.Data(),
				command.Size());
			request.AddInt16("opcodeExpected",
				PACK_OPCODE(OGF_LINK_CONTROL, OCF_USER_CONFIRM_NEG_REPLY));
		}
	} else if (accept) {
		BluetoothCommand<typed_command(r5_hci_cp_user_passkey_reply)>
			command(OGF_LINK_CONTROL, R5_OCF_USER_PASSKEY_REPLY);
		command->bdaddr = fAddress;
		command->passkey = B_HOST_TO_LENDIAN_INT32(passkey);
		request.AddData("raw command", B_ANY_TYPE, command.Data(),
			command.Size());
		request.AddInt16("opcodeExpected",
			PACK_OPCODE(OGF_LINK_CONTROL, R5_OCF_USER_PASSKEY_REPLY));
		// The message stays in this process (the window is in
		// bluetooth_server); the copy in the command buffer goes with it.
		command->passkey = 0;
		passkey = 0;
	} else {
		BluetoothCommand<typed_command(r5_hci_cp_user_passkey_neg_reply)>
			command(OGF_LINK_CONTROL, R5_OCF_USER_PASSKEY_NEG_REPLY);
		command->bdaddr = fAddress;
		request.AddData("raw command", B_ANY_TYPE, command.Data(),
			command.Size());
		request.AddInt16("opcodeExpected",
			PACK_OPCODE(OGF_LINK_CONTROL, R5_OCF_USER_PASSKEY_NEG_REPLY));
	}

	// Asynchronous: the window does not wait on the server's own looper.
	be_app_messenger.SendMessage(&request);
	fAnswered = true;
}


void
PasskeyWindow::_UpdateTyped()
{
	if (fTypedView == NULL)
		return;

	BString text;
	if (fTyped == 0)
		text = B_TRANSLATE("No digits typed yet.");
	else {
		text = B_TRANSLATE("Digits typed: %count%");
		BString count;
		count << fTyped;
		text.ReplaceFirst("%count%", count);
	}
	fTypedView->SetText(text);
}
