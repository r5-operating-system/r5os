/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * The end of a pairing started on the Bluetooth page (PairingNotice.cpp).
 */
#ifndef R5_PAIRING_NOTICE_H
#define R5_PAIRING_NOTICE_H


#include <Messenger.h>
#include <String.h>

#include <bluetooth/bluetooth.h>


// A notification, and BT_MSG_R5_PAIR_RESULT to *reply (the page that asked),
// which is then cleared so a pairing ends once.
void r5_pairing_result(bool paired, const BString& name, BMessenger* reply,
	const bdaddr_t& address);


#endif	// R5_PAIRING_NOTICE_H
