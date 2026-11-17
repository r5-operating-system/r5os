/*
	Copyright 1999, Be Incorporated.   All Rights Reserved.
	This file may be used under the terms of the Be Sample Code License.
*/

#ifndef _JOYSTICK_DRIVER_H
#define _JOYSTICK_DRIVER_H

#include <stdlib.h>

#include <SupportDefs.h>
#include <Drivers.h>
#include <module.h>

typedef struct _joystick {
	bigtime_t	timestamp;
	uint32		horizontal;
	uint32		vertical;
	bool		button1;
	bool		button2;
} joystick;

/* maximum number of axes on one controller (pads count as 2 axes each) */
#define MAX_AXES 12
/* maximum number of hats on one controller -- PADS SHOULD BE RETURNED AS AXES! */
#define MAX_HATS 8
/* maximum number of buttons on one controller */
#define MAX_BUTTONS 32
/* maximum number of controllers on one port */
#define MAX_STICKS 4

typedef struct _extended_joystick {
	bigtime_t	timestamp;		/* system_time when it was read */
	uint32		buttons;		/* lsb to msb, 1 == on */
	int16		axes[MAX_AXES];	/* -32768 to 32767, X, Y, Z, U, V, W */
	uint8		hats[MAX_HATS];	/* 0 through 8 (1 == N, 3 == E, 5 == S, 7 == W) */
} _PACKED extended_joystick;


// This is a helper structure to manage variably sized data. It is here to
// make storing and accessing the flat data in the "data" member easier. When
// transferring data via read/write/ioctl only the flat data in "data" is ever
// transmitted, not the whole structure.
typedef struct _variable_joystick {
#ifdef __cplusplus
	status_t initialize(uint32 axisCount, uint32 hatCount, uint32 buttonCount)
	{
		axis_count = axisCount;
		hat_count = hatCount;
		button_blocks = (buttonCount + 31) / 32;

		data_size = sizeof(bigtime_t)			// timestamp
			+ button_blocks * sizeof(uint32)	// bitmaps
			+ axis_count * sizeof(int16)		// axis values
			+ hat_count * sizeof(uint8);		// hat values

		/* SPDX-SnippetBegin */
		/* SPDX-SnippetCopyrightText: 2026 atomozero (RenkuOS) */
		/* SPDX-License-Identifier: MIT */
		// zeroed: centered axes and released buttons until the first report
		// (RenkuOS 823649eb6e)
		data = (uint8 *)calloc(1, data_size);
		/* SPDX-SnippetEnd */
		if (data == NULL)
			return B_NO_MEMORY;

		// fill in the convenience pointers
		timestamp = (bigtime_t *)data;
		buttons = (uint32 *)&timestamp[1];
		axes = (int16 *)&buttons[button_blocks];
		hats = (uint8 *)&axes[axis_count];
		return B_OK;
	}

	status_t initialize_to_extended_joystick()
	{
		return initialize(MAX_AXES, MAX_HATS, MAX_BUTTONS);
	}
#endif

	uint32		axis_count;
	uint32		hat_count;
	uint32		button_blocks;
		// count of 32 bit button bitmap blocks == (button_count + 31) / 32

	// These pointers all point into the data section and are here for
	// convenience. They need to be set up manually by the one who creates this
	// structure or by using the initialize() method.
	bigtime_t *	timestamp;
	uint32 *	buttons;
	int16 *		axes;
	uint8 *		hats;

	// The data is always structured in the following way (see extended_joystick
	// for data interpretation):
	//		bigtime_t	timestamp;
	//		uint32		button_bitmap_blocks[button_blocks];
	//		int16		axes[axis_count];
	//		uint8		hats[hat_count];
	size_t		data_size;
	uint8 *		data;
} variable_joystick;


#define MAX_CONFIG_SIZE 100

enum {	/* flags for joystick module info */
	js_flag_force_feedback = 0x1,
	js_flag_force_feedback_directional = 0x2,
	js_flag_variable_size_reads = 0x4
};

typedef struct _joystick_module_info {
	char			module_name[64];
	char			device_name[64];
	int16			num_axes;
	int16			num_buttons;
	int16			num_hats;
	uint16			_reserved[7];
	uint32			flags;
	uint16			num_sticks;
	int16			config_size;
	char			device_config[MAX_CONFIG_SIZE];	/* device specific */
} joystick_module_info;

/* Note that joystick_module is something used by the game port driver */
/* to talk to digital joysticks; if you're writing a sound card driver */
/* and want to add support for a /dev/joystick device, use the generic_gameport */
/* module. */

typedef struct _joystick_module {
	module_info minfo;
	/** "configure" might change the "info" if it auto-detects a device */
	int (*configure)(int port, joystick_module_info * info, size_t size, void ** out_cookie);
	/** "read" actual data from device into "data" */
	int (*read)(void * cookie, int port, extended_joystick * data, size_t size);
	/** "crumble" the cookie (deallocate) when done */
	int (*crumble)(void * cookie, int port);
	/** "force" tells the joystick to exert force on the same axes as input for the specified duration */
	int (*force)(void * cookie, int port, bigtime_t duration, extended_joystick * force, size_t size);
	int _reserved_;
} joystick_module;

/** Doing force feedback means writing an extended_joystick to the device with force values.
    The "timestamp" should be the duration of the feedback. Successive writes will be queued
    by the device module. */
enum { /* Joystick driver ioctl() opcodes */
    B_JOYSTICK_GET_SPEED_COMPENSATION = B_JOYSTICK_DRIVER_BASE,
                                            /* arg -> ptr to int32 */
    B_JOYSTICK_SET_SPEED_COMPENSATION,      /* arg -> ptr to int32 */
    B_JOYSTICK_GET_MAX_LATENCY,             /* arg -> ptr to long long */
    B_JOYSTICK_SET_MAX_LATENCY,             /* arg -> ptr to long long */
    B_JOYSTICK_SET_DEVICE_MODULE,			/* arg -> ptr to joystick_module; also enters enhanced mode */
    B_JOYSTICK_GET_DEVICE_MODULE,           /* arg -> ptr to joystick_module */
	B_JOYSTICK_SET_RAW_MODE					/* arg -> ptr to bool (true or false) */
};

/* SPDX-SnippetBegin */
/* SPDX-SnippetCopyrightText: 2026 R5OS Contributors */
/* SPDX-License-Identifier: MIT */
/* R5: what a game controller is (docs/GAME-CONTROLLERS.md). An additive
 * call; the opcodes and structures above do not change. usb_hid,
 * bluetooth_hid and i2c_hid answer it through hid_shared; a driver that
 * does not know it answers B_DEV_INVALID_IOCTL or B_ERROR. */
#define R5_JOYSTICK_GET_INFO	(B_JOYSTICK_DRIVER_BASE + 82)
	/* arg -> ptr to r5_joystick_info, its "size" set by the caller */

enum {
	/* the bus numbers SDL and Linux use */
	R5_JOYSTICK_BUS_UNKNOWN = 0x00,
	R5_JOYSTICK_BUS_USB = 0x03,
	R5_JOYSTICK_BUS_BLUETOOTH = 0x05,
	R5_JOYSTICK_BUS_I2C = 0x18
};

#define R5_JOYSTICK_NAME_LENGTH		64
#define R5_JOYSTICK_MAX_AXIS_USAGES	32
#define R5_JOYSTICK_BUTTON_USAGE_WORDS	4

typedef struct _r5_joystick_info {
	uint32	size;
		/* in: sizeof(r5_joystick_info) of the caller; out: how many bytes
		   the driver filled in. Fields are only added at the end. */
	uint16	bus;					/* R5_JOYSTICK_BUS_* */
	uint16	vendor_id;				/* 0 when the transport does not say */
	uint16	product_id;
	uint16	version;
	int16	battery;				/* percent, -1 when not reported */
	uint16	axis_count;				/* the axes of a variable read */
	uint16	axis_usage[R5_JOYSTICK_MAX_AXIS_USAGES];
		/* HID Generic Desktop usage of each axis, in read order (0x30 X,
		   0x31 Y, 0x32 Z, 0x33 Rx, ...); 0 when unknown */
	char	name[R5_JOYSTICK_NAME_LENGTH];
		/* the product name, UTF-8, empty when the device has none */
	uint32	button_usages[R5_JOYSTICK_BUTTON_USAGE_WORDS];
		/* added after name (a shorter "size" back means an older driver):
		   bit n % 32 of word n / 32 is set when HID Button usage n + 1 is
		   in the report (usages 1 to 128), so a reader can tell the buttons
		   that exist from the gaps in the read */
} r5_joystick_info;
/* SPDX-SnippetEnd */

/* Speed compensation is not generally necessary, because the joystick */
/* driver is measuring using real time, not just # cycles. "0" means the */
/* default, center value. + typically returns higher values; - returns lower */
/* A typical range might be from -10 to +10, but it varies by driver */

/* Lower latency will make for more overhead in reading the joystick */
/* ideally, you set this value to just short of how long it takes you */
/* to calculate and render a frame. 30 fps -> latency 33000 */


typedef struct _generic_gameport_module {
	module_info minfo;
	status_t (*create_device)(int port, void ** out_storage);
	status_t (*delete_device)(void * storage);
	status_t (*open_hook)(void * storage, uint32 flags, void ** out_cookie);
	status_t (*close_hook)(void * cookie);
	status_t (*free_hook)(void * cookie);
	status_t (*control_hook)(void * cookie, uint32 op, void * data, size_t len);
	status_t (*read_hook)(void * cookie, off_t pos, void * data, size_t * len);
	status_t (*write_hook)(void * cookie, off_t pos, const void * data, size_t * len);
	int	_reserved_;
} generic_gameport_module;


#endif
