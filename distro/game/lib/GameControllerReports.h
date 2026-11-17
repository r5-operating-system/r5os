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
 * Phase 2 adds the Nintendo Switch controllers (HD rumble, subcommands,
 * SPI flash reads, the IMU) and the Xbox controllers' rumble over
 * Bluetooth and over USB (GIP).
 *
 * Sources, facts only, no code taken:
 * - PlayStation report layouts: SDL's hidapi drivers, SDL_hidapi_ps4.c and
 *   SDL_hidapi_ps5.c, and SDL's test/testcontroller.c for the trigger
 *   effect bytes (zlib, Copyright (C) 1997-2026 Sam Lantinga).
 * - Switch and Xbox layouts: SDL_hidapi_switch.c and SDL_hidapi_xboxone.c
 *   (the same license and copyright), read on 17 Nov 2026 at commit
 *   4cc7adcaee482b63b4e1a0de77f52e200aa87b90; the Switch rumble amplitude
 *   steps as described in dekuNukem's Nintendo_Switch_Reverse_Engineering
 *   (rumble_data_table.md, cited by SDL), computed here, not copied.
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


// #pragma mark - Nintendo Switch controllers (phase 2)


enum switch_model {
	SWITCH_NONE = 0,
	SWITCH_PRO,
	SWITCH_JOYCON_LEFT,
	SWITCH_JOYCON_RIGHT
};

switch_model switch_model_for(uint16 vendor, uint16 product);

static const size_t kSwitchBluetoothSize = 49;
static const size_t kSwitchUSBSize = 64;
	// output reports with their ID, as SDL pads them

// HD rumble (SDL_hidapi_switch.c; the amplitude table of dekuNukem's
// Nintendo_Switch_Reverse_Engineering, rumble_data_table.md): four bytes
// per side, the high band (frequency code 0x74) carrying weak, the low
// band (frequency code 0x3D) carrying strong; both 0 gives the neutral
// 00 01 40 40. Both sides get the same.
void switch_rumble_encode(float strong, float weak, uint8 out[4]);

// The amplitude index 0 to 100 of the table above for 0 to 1: the
// table's logarithmic steps above 12 %, a straight line below.
uint8 switch_amplitude_index(float amplitude);

// Output report 0x10 (rumble only) and 0x01 (rumble and a subcommand),
// the report ID first, counter 0 to 15, zeros up to length (0: the
// transport's size). 0 when it does not fit.
size_t build_switch_rumble(uint8 counter, float strong, float weak,
	size_t length, uint8* out, size_t capacity);
size_t build_switch_subcommand(uint8 counter, float strong, float weak,
	uint8 command, const uint8* args, size_t argLength, size_t length,
	uint8* out, size_t capacity);

// Subcommand 0x10: read size (at most 29) bytes of SPI flash at address.
size_t build_switch_spi_read(uint8 counter, float strong, float weak,
	uint32 address, uint8 size, size_t length, uint8* out, size_t capacity);

// The data of the reply 0x21 to that read, when it is one: the reply has
// the subcommand in byte 14, then the address and size it read, then the
// bytes.
bool parse_switch_spi_reply(const uint8* report, size_t length,
	uint32 address, uint8 size, uint8* data);

// The IMU's scale. The factory calibration is 24 bytes of SPI flash at
// 0x6020: accelerometer origin and sensitivity, gyroscope origin and
// sensitivity, three int16 each. Without it, the nominal scale: 4096
// counts per g and 14.2842 counts per degree per second (SDL's defaults).
struct switch_imu_calibration {
	float	accelScale[3];
		// g per count
	float	gyroScale[3];
		// degrees per second per count
	int16	gyroOffset[3];
	bool	factory;
};

static const uint32 kSwitchIMUCalibrationAddress = 0x6020;
static const uint8 kSwitchIMUCalibrationSize = 24;

void switch_imu_nominal(switch_imu_calibration& calibration);
// false (and the nominal scale) when the data is unusable.
bool switch_imu_from_factory(const uint8 data[24],
	switch_imu_calibration& calibration);

// The newest of the three samples of a full report (0x30 or 0x31; after
// the report ID: twelve bytes of state, then three samples of
// accelerometer X Y Z and gyroscope X Y Z, int16 each), in the axes of the
// PlayStation controllers as SDL turns them. false when the report has
// no sensors yet (all zero) or is too short.
bool parse_switch_motion(const switch_imu_calibration& calibration,
	const uint8* report, size_t length, motion_sample& sample);


// #pragma mark - Xbox controllers (phase 2)


enum xbox_model {
	XBOX_NONE = 0,
	XBOX_BLUETOOTH,
		// HID over Bluetooth Classic or Low Energy: output report 0x03
	XBOX_GIP
		// over USB, Gaming Input Protocol, through usb_hid: output 0x09
};

// Bluetooth from the IDs (SDL_IsJoystickBluetoothXboxOne()); GIP from the
// driver's R5_JOYSTICK_FLAG_GIP.
xbox_model xbox_model_for(uint16 vendor, uint16 product, bool bluetooth,
	bool gip);

// The trigger motors: Microsoft's own controllers (SDL:
// ControllerHasTriggerRumble()).
bool xbox_has_trigger_rumble(uint16 vendor);

static const size_t kXboxBluetoothRumbleSize = 9;
static const size_t kXboxGIPRumbleSize = 13;

// The rumble report, each motor 0 to 1 sent as 0 to 100, the longest
// duration (2.55 s, repeated; libr5game stops it). Bluetooth: 0x03, the
// motors enabled (0x0F), left trigger, right trigger, strong, weak,
// duration, delay, repeat. GIP: the same after the header 09 00 seq 09 00.
size_t build_xbox_bluetooth_rumble(float leftTrigger, float rightTrigger,
	float strong, float weak, uint8* out, size_t capacity);
size_t build_xbox_gip_rumble(uint8 sequence, float leftTrigger,
	float rightTrigger, float strong, float weak, uint8* out,
	size_t capacity);


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
