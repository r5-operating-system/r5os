/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * The reports behind rumble, trigger effects and motion in libr5game
 * (docs/GAME-CONTROLLERS.md, option C phase 1): building output reports for
 * the DualSense (and Edge) and the DualShock 4 over USB and Bluetooth, the
 * CRC-32 of their Bluetooth reports, reading their motion sensors, and HID
 * PID force feedback for any controller whose report descriptor has the
 * Physical Interface Device page (0x0F).
 *
 * Sources, facts only, no code taken:
 * - PlayStation report layouts: SDL's hidapi drivers, SDL_hidapi_ps4.c and
 *   SDL_hidapi_ps5.c, and SDL's test/testcontroller.c for the trigger
 *   effect bytes (zlib, Copyright (C) 1997-2026 Sam Lantinga).
 * - HID report descriptors: USB Device Class Definition for HID 1.11.
 * - Force feedback: USB Device Class Definition for Physical Interface
 *   Devices (PID) 1.0, and the HID Usage Tables, page 0x0F.
 *
 * Plain C++ without the Haiku kits, so the unit test and the fuzz target
 * build it on the host (distro/game/tests/feel_host_test).
 */
#ifndef _GAME_CONTROLLER_REPORTS_H
#define _GAME_CONTROLLER_REPORTS_H


#include <stddef.h>
#include <stdint.h>

#ifdef __HAIKU__
#	include <SupportDefs.h>
#else
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef int16_t int16;
typedef int32_t int32;
#endif


namespace R5GameController {


// #pragma mark - CRC-32


// The CRC-32 of IEEE 802.3 (reflected polynomial 0xEDB88320), continued
// from crc; start with 0.
uint32 crc32(uint32 crc, const uint8* data, size_t length);


// #pragma mark - PlayStation controllers


enum playstation_model {
	PLAYSTATION_NONE = 0,
	PLAYSTATION_DUALSHOCK4,
	PLAYSTATION_DUALSENSE,
	PLAYSTATION_DUALSENSE_EDGE
};

playstation_model playstation_model_for(uint16 vendor, uint16 product);


// The values of game_controller_trigger_effect in <GameController.h>.
enum {
	TRIGGER_EFFECT_OFF = 0,
	TRIGGER_EFFECT_RESISTANCE = 1,
	TRIGGER_EFFECT_CLICK = 2,
	TRIGGER_EFFECT_VIBRATION = 3
};

static const size_t kTriggerEffectSize = 11;
static const size_t kPlayStationBluetoothSize = 78;
static const size_t kDualSenseUSBSize = 48;
static const size_t kDualShock4USBSize = 32;


struct playstation_effects {
	uint8	strong;
		// the left motor, low frequency
	uint8	weak;
		// the right motor, high frequency
	uint8	leftTrigger[kTriggerEffectSize];
	uint8	rightTrigger[kTriggerEffectSize];
		// DualSense: the mode byte and its parameters
	bool	improvedRumble;
		// DualSense firmware 2.24 and later, and the Edge
};

void playstation_effects_init(playstation_effects& effects);

// The 11 bytes of one DualSense trigger effect; start and strength 0 to 1.
// The simple modes 0x05 (off), 0x01 (resistance from the start position),
// 0x02 (a click between start and end) and 0x06 (vibration).
void dualsense_trigger_effect(uint32 effect, float start, float strength,
	uint8 out[kTriggerEffectSize]);

// The whole output report, report ID first; 0 when capacity is too small.
// DualSense: 0x02, 48 bytes on USB; 0x31, 78 bytes with CRC on Bluetooth.
// DualShock 4: 0x05, 32 bytes on USB; 0x11, 78 bytes with CRC on Bluetooth.
size_t build_dualsense_output(const playstation_effects& effects,
	bool bluetooth, uint8* out, size_t capacity);
size_t build_dualshock4_output(const playstation_effects& effects,
	bool bluetooth, uint8* out, size_t capacity);

// The input report with the motion sensors: 0x01 on USB, the full report
// on Bluetooth (0x11 DualShock 4, 0x31 DualSense).
uint8 playstation_motion_report(playstation_model model, bool bluetooth);


struct motion_sample {
	float	acceleration[3];
		// g
	float	rotation[3];
		// degrees per second
};

// Reads the sensors of one input report (report ID first). Uncalibrated:
// 8192 counts per g and 16 counts per degree per second (SDL's defaults
// without the controller's calibration report).
bool parse_playstation_motion(playstation_model model, bool bluetooth,
	const uint8* report, size_t length, motion_sample& sample);


// #pragma mark - HID report descriptors


static const uint32 kMaxHIDFields = 192;
static const uint32 kMaxHIDReports = 48;
static const uint32 kMaxArrayUsages = 24;
static const size_t kMaxReportBytes = 64;
	// the PID reports this code builds


enum {
	HID_INPUT = 1,
	HID_OUTPUT = 2,
	HID_FEATURE = 3
};


// One field of the PID page (0x0F). A variable field has one usage; an
// array field selects one of its usages: the value logicalMinimum + i
// selects usages[i] (HID 1.11, 6.2.2.5).
struct hid_field {
	uint8	type;
	uint8	id;
	uint16	usage;
		// variable field: its usage on page 0x0F; array field: 0
	uint16	collection;
		// the innermost PID collection around it (an array's selector
		// collection, such as Effect Type 0x25)
	uint16	report;
		// the outermost PID collection around it: which report it is
		// (Set Effect Report 0x21, ...)
	bool	isArray;
	uint8	usageCount;
	uint16	usages[kMaxArrayUsages];
	int32	logicalMinimum;
	int32	logicalMaximum;
	uint32	bitOffset;
		// after the report ID
	uint8	bitSize;
};


struct hid_report_size {
	uint8	type;
	uint8	id;
	uint32	bits;
};


struct hid_layout {
	bool			usesReportIDs;
	uint32			fieldCount;
	hid_field		fields[kMaxHIDFields];
	uint32			reportCount;
	hid_report_size	reports[kMaxHIDReports];
	bool			hasPIDPage;
};

// false when the descriptor is malformed (an End Collection without a
// Collection, a Pop without a Push, a report larger than 4096 bytes).
bool parse_hid_descriptor(const uint8* descriptor, size_t length,
	hid_layout& layout);

// Bytes of a report, with the ID byte when the device uses IDs; 0 when
// the descriptor has no such report.
size_t hid_report_length(const hid_layout& layout, uint8 type, uint8 id);


struct hid_report {
	uint8	type;
	uint8	id;
	uint32	length;
	uint8	data[kMaxReportBytes];
};


// #pragma mark - HID PID force feedback


struct pid_report_ref {
	bool	present;
	uint8	type;
	uint8	id;
};


struct pid_device {
	bool			usable;
		// Set Effect, Effect Operation and a periodic or constant force
		// report are there
	bool			periodic;
		// a sine (Set Periodic Report); else a constant force
	uint16			effectType;
		// ET Sine 0x31 or ET Constant Force 0x26
	pid_report_ref	setEffect;			// 0x21
	pid_report_ref	setPeriodic;		// 0x6E
	pid_report_ref	setConstant;		// 0x73
	pid_report_ref	effectOperation;	// 0x77
	pid_report_ref	deviceControl;		// 0x95
	pid_report_ref	deviceGain;			// 0x7D
	pid_report_ref	createNewEffect;	// 0xAB, feature
	pid_report_ref	blockLoad;			// 0x89, feature
};

// Finds the reports rumble needs.
bool pid_find(const hid_layout& layout, pid_device& device);

// Device Control (Device Reset, then Enable Actuators) and Device Gain at
// its maximum: up to three reports, in order.
uint32 pid_init_reports(const hid_layout& layout, const pid_device& device,
	hid_report* out, uint32 capacity);

// Create New Effect (feature) for the device's effect type.
bool pid_create_report(const hid_layout& layout, const pid_device& device,
	hid_report& out);

// The effect block index from a PID Block Load report (report ID first);
// false unless Block Load Status says success.
bool pid_parse_block_load(const hid_layout& layout, const pid_device& device,
	const uint8* data, size_t length, uint8& blockIndex);

// Set Effect, Set Periodic or Set Constant Force, and Effect Operation
// Start Solo: three reports. magnitude 0 to 1, duration in milliseconds
// (0: as long as the field allows), period in milliseconds (periodic).
uint32 pid_start_reports(const hid_layout& layout, const pid_device& device,
	uint8 blockIndex, float magnitude, uint32 durationMS, uint32 periodMS,
	hid_report* out, uint32 capacity);

// Effect Operation Stop.
bool pid_stop_report(const hid_layout& layout, const pid_device& device,
	uint8 blockIndex, hid_report& out);


}	// namespace R5GameController


#endif	// _GAME_CONTROLLER_REPORTS_H
