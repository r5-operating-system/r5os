/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * The voice node of the h2generic driver (docs/BLUETOOTH.md, step 7c).
 * Only when the driver settings file (~/config/settings/kernel/drivers/
 * h2generic) says "bt_voice true", h2generic publishes
 * /dev/bluetooth/voice/<n> for each radio. While a SCO or eSCO link is up
 * it runs the radio's isochronous endpoints, and the node carries that
 * link's HCI SCO packets (Core 5.4, Vol 4, Part E, 5.4.3):
 *
 *	read()	one whole packet (header and data) per call, at most
 *			R5_BTVOICE_MAX_PACKET bytes; it waits at most
 *			R5_BTVOICE_READ_TIMEOUT and then returns 0 bytes;
 *	write()	one whole packet per call on the link's handle; B_WOULD_BLOCK
 *			while R5_BTVOICE_TX_SLOTS packets are on their way.
 *
 * Only uid 0 may open it (bluetooth_audio runs in media_addon_server), and
 * only once at a time.
 */
#ifndef R5_BLUETOOTH_VOICE_H
#define R5_BLUETOOTH_VOICE_H


#include <Drivers.h>
#include <SupportDefs.h>


// The directory, relative to /dev; the nodes are numbered like the radios.
#define R5_BTVOICE_DIRECTORY		"bluetooth/voice"

#define R5_BTVOICE_MAX_PACKET		(3 + 255)
#define R5_BTVOICE_READ_TIMEOUT		100000
#define R5_BTVOICE_TX_SLOTS			8

// Air_Mode values (Core 5.4, Vol 4, Part E, 7.7.35).
enum {
	R5_BTVOICE_AIR_ULAW = 0,
	R5_BTVOICE_AIR_ALAW = 1,
	R5_BTVOICE_AIR_CVSD = 2,
	R5_BTVOICE_AIR_TRANSPARENT = 3
};

enum {
	// r5_btvoice_link: the voice link of this radio, if any.
	R5_BTVOICE_GET_LINK = B_DEVICE_OP_CODES_END + 0x5301
};

struct r5_btvoice_link {
	uint32		running;		// 1 while the isochronous transfers run
	uint16		handle;			// the SCO or eSCO connection handle
	uint8		air_mode;		// R5_BTVOICE_AIR_*
	uint8		alt_setting;	// the USB alternate setting in use
	uint16		rx_length;		// from Synchronous Connection Complete
	uint16		tx_length;
	uint32		rx_packets;		// whole SCO packets received
	uint32		dropped;		// lost to a full ring or a broken stream
};


#endif	// R5_BLUETOOTH_VOICE_H
