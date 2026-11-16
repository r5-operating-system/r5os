/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * HCI commands and events for Secure Simple Pairing that the Haiku headers
 * do not define (Core 6.0, Vol 4, Part E), and the Event Mask that
 * bluetooth_server writes after every HCI Reset (docs/BLUETOOTH.md, step 4).
 */
#ifndef R5_PAIRING_H
#define R5_PAIRING_H


#include <bluetooth/bluetooth.h>


// Controller and baseband commands (OGF 0x03).
#define R5_OCF_SET_EVENT_MASK				0x0001	// 7.3.1
#define R5_OCF_WRITE_SIMPLE_PAIRING_MODE	0x0056	// 7.3.59

// Link control commands (OGF 0x01).
#define R5_OCF_USER_PASSKEY_REPLY			0x002E	// 7.1.32
#define R5_OCF_USER_PASSKEY_NEG_REPLY		0x002F	// 7.1.33

// Events (7.7).
#define R5_HCI_EVENT_USER_PASSKEY_NOTIFICATION	0x3B	// 7.7.48
#define R5_HCI_EVENT_KEYPRESS_NOTIFICATION		0x3C	// 7.7.49

// IO capability KeyboardDisplay (7.1.29), not in btHCI_event.h.
#define R5_HCI_IO_CAP_KEYBOARD_DISPLAY		0x04

// LMP feature "Secure Simple Pairing (Controller Support)", bit 51 of the
// LMP features (Vol 2, Part C, 3.3): octet 6, bit 3.
#define R5_LMP_FEATURES_SSP_OCTET			6
#define R5_LMP_FEATURES_SSP_BIT				0x08

// Passkey Entry: the passkey is 0 to 999999 (7.1.32).
#define R5_PASSKEY_MAX						999999


struct r5_hci_ev_user_passkey_request {
	bdaddr_t	bdaddr;
} __attribute__((packed));

struct r5_hci_ev_user_passkey_notification {
	bdaddr_t	bdaddr;
	uint32		passkey;
} __attribute__((packed));

struct r5_hci_ev_keypress_notification {
	bdaddr_t	bdaddr;
	uint8		type;
} __attribute__((packed));

struct r5_hci_cp_set_event_mask {
	uint8		mask[8];
} __attribute__((packed));

struct r5_hci_cp_write_simple_pairing_mode {
	uint8		mode;
} __attribute__((packed));

struct r5_hci_cp_user_passkey_reply {
	bdaddr_t	bdaddr;
	uint32		passkey;
} __attribute__((packed));

struct r5_hci_cp_user_passkey_neg_reply {
	bdaddr_t	bdaddr;
} __attribute__((packed));


// The default after a reset is bits 0 to 44 (7.3.1); bit n is event code
// n + 1. The Secure Simple Pairing events are added only when the
// controller has the SSP feature: IO Capability Request and Response, User
// Confirmation Request, User Passkey Request, Remote OOB Data Request and
// Simple Pairing Complete (codes 0x31 to 0x36, bits 48 to 53), User Passkey
// Notification and Keypress Notification (0x3B and 0x3C, bits 58 and 59).
static inline uint64
r5_event_mask(const uint8 features[8])
{
	uint64 mask = 0x00001FFFFFFFFFFFULL;
	if (features != NULL && (features[R5_LMP_FEATURES_SSP_OCTET]
			& R5_LMP_FEATURES_SSP_BIT) != 0) {
		for (int bit = 48; bit <= 53; bit++)
			mask |= 1ULL << bit;
		mask |= 1ULL << 58;
		mask |= 1ULL << 59;
	}
	return mask;
}


static inline bool
r5_has_ssp(const uint8 features[8])
{
	return features != NULL && (features[R5_LMP_FEATURES_SSP_OCTET]
		& R5_LMP_FEATURES_SSP_BIT) != 0;
}


#endif // R5_PAIRING_H
