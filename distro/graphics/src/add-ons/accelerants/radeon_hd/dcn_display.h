/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * The DCN display track of the radeon_hd accelerant (docs/DISPLAY-DCN.md).
 * dcn_display.cpp is version-neutral: the version table, the crash marker,
 * the DCN lock, the VBIOS and EDID helpers, and the connector loop. Each DCN
 * version has its own dcnNN_*.cpp files behind one dcn_ops table.
 *
 * The DCN code shares nothing with the GFX9 ring, the GART or RADV: no
 * state, no lock. It never takes a GPU lock, and the GPU code never takes
 * the DCN lock.
 */
#ifndef DCN_DISPLAY_H
#define DCN_DISPLAY_H


#include <SupportDefs.h>


struct edid1_info;


// Called from connector_walk_object_records() for each I2C and HPD record of
// a display path, with the raw ids from the VBIOS (atom_i2c_record.i2c_id,
// atom_hpd_int_record.pin_id).
void dcn_note_i2c_record(uint32 connectorIndex, uint8 i2cId);
void dcn_note_hpd_record(uint32 connectorIndex, uint8 pinId);

// Called once from detect_displays() on DCN. Never changes gDisplay[].
void dcn_display_probe();


// What step 1 found on one connector.
struct dcn_connector_state {
	uint32	connector;
	int32	ddcPad;		// index in the version's pad table, -1 unknown
	int32	hpd;		// 0-based HPD number, -1 unknown
	bool	hpdHigh;
	bool	edidFound;
};

// One DCN version. Every call returns an error instead of waiting forever.
struct dcn_ops {
	const char*	name;
	// Step 1: hotplug and EDID of one connector. Logs; fills state and edid.
	status_t	(*probe_connector)(uint32 connector, bool hasI2c, uint8 i2cId,
					bool hasHpd, uint8 hpdPin, dcn_connector_state* state,
					edid1_info* edid);
	// Step 2a: log what the firmware left. Reads only.
	void		(*log_pipes)();
	// Reads only. B_OK when the display registers answer with values the
	// hardware can have; otherwise why, and nothing may write a DCN
	// register this boot.
	status_t	(*check_access)(char* why, size_t whySize);
	// Step 2: light the connector on a free pipe (opt-in).
	status_t	(*head_start)(const dcn_connector_state* state,
					const edid1_info* edid);
};

extern const dcn_ops gDcn21Ops;


// Helpers for the version files ---------------------------------------------

// atomfirmware gpio_pin_lut v2.1: absolute dword index of the A register and
// the bit shift for gpio_id.
bool dcn_vbios_pin_lookup(uint8 gpioId, uint32* regIndex, uint8* bitShift);

// The crash marker. Before a step that writes DCN registers, arm it with the
// step's name; clear it when the step returned. A boot (or an app_server
// restart) that finds it armed switches DCN off until the radeon_hd settings
// change.
void dcn_marker_arm(const char* step);
void dcn_marker_clear();

// A failed step switches DCN off for the rest of this boot.
void dcn_switch_off(const char* why);
bool dcn_is_off();

// radeon_hd settings switch.
bool dcn_setting(const char* name, bool defaultValue);


// A timing from the EDID, in the fields Linux dc_crtc_timing uses.
struct dcn_timing {
	uint32	pixelKHz;
	uint32	hActive;
	uint32	hTotal;
	uint32	hFrontPorch;
	uint32	hSync;
	uint32	vActive;
	uint32	vTotal;
	uint32	vFrontPorch;
	uint32	vSync;
	bool	hPositive;
	bool	vPositive;
};

// The first progressive detailed timing up to maxKHz.
bool dcn_timing_from_edid(const edid1_info* edid, uint32 maxKHz,
	dcn_timing* timing);


#endif	// DCN_DISPLAY_H
