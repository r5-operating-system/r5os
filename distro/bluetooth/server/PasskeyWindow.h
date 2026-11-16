/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Secure Simple Pairing, Passkey Entry (docs/BLUETOOTH.md, step 4): shows
 * the passkey that a keyboard types, or asks for the passkey that a remote
 * shows. The passkey is never logged.
 */
#ifndef R5_PASSKEY_WINDOW_H
#define R5_PASSKEY_WINDOW_H


#include <String.h>
#include <Window.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/HCI/btHCI.h>


class BButton;
class BStringView;
class BTextControl;


// From LocalDeviceImpl: the keyboard reported a key (uint8 "type",
// Core 6.0, Vol 4, Part E, 7.7.49).
enum {
	kMsgPasskeyKeypress = 'r5pk'
};


class PasskeyWindow : public BWindow {
public:
	// Entry: the person types the passkey that the remote shows.
								PasskeyWindow(const bdaddr_t& address,
									hci_id hid, const BString& name);
	// Display: the person types this passkey on the remote.
								PasskeyWindow(const bdaddr_t& address,
									hci_id hid, const BString& name,
									uint32 passkey);

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			void				_Build(const BString& name);
			void				_SendReply(bool accept);
			void				_UpdateTyped();

			bdaddr_t			fAddress;
			hci_id				fHid;
			bool				fEntry;
			bool				fAnswered;
			uint32				fPasskey;
			int32				fTyped;
			BTextControl*		fPasskeyText;
			BStringView*		fTypedView;
			BButton*			fPairButton;
};


#endif // R5_PASSKEY_WINDOW_H
