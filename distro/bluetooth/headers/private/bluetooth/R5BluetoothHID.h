/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * The control node of the bluetooth_hid driver (docs/BLUETOOTH.md, step 4).
 * bluetooth_server (uid 0) holds the L2CAP HID channels (PSM 0x11 and
 * 0x13) and hands the report descriptor and every input report to the
 * driver. The driver parses them with Haiku's hid_shared code and publishes
 * input/keyboard/bluetooth/N, input/mouse/bluetooth/N and
 * joystick/bluetooth/N, so input_server and BJoystick read them like USB.
 */
#ifndef R5_BLUETOOTH_HID_H
#define R5_BLUETOOTH_HID_H


#include <Drivers.h>
#include <SupportDefs.h>


// The node, relative to /dev. Only uid 0 may open it.
#define R5_BTHID_CONTROL_NAME		"bluetooth/hid/control"

// Limits the driver enforces (a Bluetooth HID report descriptor is a few
// hundred bytes; a DualSense input report is at most 78 bytes).
#define R5_BTHID_MAX_DESCRIPTOR		4096
#define R5_BTHID_MAX_REPORT			512
#define R5_BTHID_MAX_DEVICES		16

enum {
	// r5_bthid_create: parse the descriptor and publish the devices.
	R5_BTHID_CREATE = B_DEVICE_OP_CODES_END + 0x5201,
	// int32 id: unpublish; open readers get an error.
	R5_BTHID_DESTROY,
	// r5_bthid_input: one input report (report ID first, if the
	// descriptor uses report IDs), as it came after the DATA header.
	R5_BTHID_INPUT,
	// r5_bthid_output: wait for an output report (keyboard lights).
	R5_BTHID_GET_OUTPUT,
	// r5_bthid_info: vendor and product ID and the name of a device, for
	// R5_JOYSTICK_GET_INFO (docs/GAME-CONTROLLERS.md, step 8). Sent right
	// after R5_BTHID_CREATE when the SDP Device ID record has them.
	R5_BTHID_SET_INFO
};

struct r5_bthid_create {
	const uint8*	descriptor;			// in
	uint32			descriptor_length;	// in
	int32			id;					// out
	uint32			handlers;			// out: keyboard, mouse, joystick...
};

struct r5_bthid_input {
	int32			id;
	uint32			length;
	const uint8*	data;
};

struct r5_bthid_output {
	bigtime_t		timeout;			// in, relative
	uint8*			data;				// in: buffer of capacity bytes
	uint32			capacity;			// in
	int32			id;					// out
	uint32			length;				// out: report ID first
};

struct r5_bthid_info {
	int32			id;
	uint16			vendor_id;			// Device ID attribute 0x0201
	uint16			product_id;			// 0x0202
	uint16			version;			// 0x0203
	uint16			_reserved;
	char			name[64];			// the device's name, UTF-8
};


#endif // R5_BLUETOOTH_HID_H
