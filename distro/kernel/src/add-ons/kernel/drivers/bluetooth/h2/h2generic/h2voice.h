/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Voice links for h2generic (docs/BLUETOOTH.md, step 7c): isochronous
 * transfers on the alternate setting a SCO or eSCO link needs, started at
 * Synchronous Connection Complete and stopped at its Disconnection
 * Complete, and the /dev/bluetooth/voice/<n> node that carries the link's
 * SCO packets (<R5BluetoothVoice.h>).
 *
 * Off unless ~/config/settings/kernel/drivers/h2generic says
 * "bt_voice true". While it is off, h2_voice_enabled() is false, every
 * other call here returns at once, no node is published, and the driver
 * does what it did before: on the MediaTek MT7922 a standing isochronous
 * transfer once starved the HCI events (the note at BT_UP in
 * h2generic.cpp).
 */
#ifndef _H2VOICE_H_
#define _H2VOICE_H_


#include "h2generic.h"


// init_driver() and uninit_driver().
void		h2_voice_init();
void		h2_voice_uninit();

bool		h2_voice_enabled();

// device_open() after the radio is registered, and device_close() before
// its queues are cancelled: the voice state follows the open radio.
void		h2_voice_device_open(bt_usb_dev* bdev);
void		h2_voice_device_close(bt_usb_dev* bdev);

// event_complete(): the bytes that go up as events.
void		h2_voice_event_bytes(bt_usb_dev* bdev, const uint8* data,
				size_t size);

// publish_devices(): the voice node of radio "num" (malloc'ed, the caller
// frees it), or NULL while voice is off.
char*		h2_voice_node_name(int32 num);

// find_device(): the hooks of a voice node, or NULL for another name.
device_hooks* h2_voice_find_device(const char* name);


#endif	// _H2VOICE_H_
