/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Private to libr5game.so and its test: the SDL_GameControllerDB line
 * format, the choice of entry for a vendor and product ID, and the step
 * from raw values to the standard layout. No hardware, no app_server.
 *
 * R5 reads the Windows entries of the database (GUID 03000000vvvv0000
 * pppp000000000000, the DirectInput form). DirectInput numbers buttons by
 * their HID usage and axes by usage (X, Y, Z, Rx, Ry, Rz, then sliders),
 * which is the order hid_shared gives buttons and, sorted by usage, axes.
 */
#ifndef R5_GAME_CONTROLLER_MAPPING_H
#define R5_GAME_CONTROLLER_MAPPING_H


#include <SupportDefs.h>

#include "GameController.h"


namespace R5GameController {


enum source_type {
	SOURCE_NONE = 0,
	SOURCE_BUTTON,
	SOURCE_AXIS,
	SOURCE_HAT
};

struct mapping_source {
	uint8	type;		// source_type
	uint8	index;		// button, axis (DirectInput order) or hat number
	uint8	hatMask;	// 1 up, 2 right, 4 down, 8 left
	int8	half;		// axis: 0 the whole axis, 1 its upper, -1 its lower half
	bool	invert;		// axis: "~"
};

// One target axis can be fed as a whole, or as two halves ("+leftx",
// "-leftx"), as some d-pads and sliders are.
struct axis_mapping {
	mapping_source	whole;
	mapping_source	positive;
	mapping_source	negative;
};

static const int32 kNameLength = 64;

struct controller_mapping {
	char			name[kNameLength];
	mapping_source	buttons[B_CONTROLLER_BUTTON_COUNT];
	axis_mapping	axes[B_CONTROLLER_AXIS_COUNT];
};

// The raw state, after BJoystick::Update(). Axes are in DirectInput order
// (axis_order() below), up to kMaxAxes.
static const int32 kMaxAxes = 32;
static const int32 kMaxHats = 8;
static const int32 kMaxButtons = 128;

struct raw_state {
	int32	axisCount;
	int16	axes[kMaxAxes];
	int32	hatCount;
	uint8	hats[kMaxHats];			// hid_shared: 0 centered, 1 N ... 8 NW
	int32	buttonCount;
	bool	buttons[kMaxButtons];
};

struct mapped_state {
	uint32	buttons;				// bit per game_controller_button
	int16	axes[B_CONTROLLER_AXIS_COUNT];
};


// "030000004c050000e60c000000000000": true with the vendor and product ID
// when the GUID has the DirectInput vendor and product form.
bool		parse_guid(const char* guid, uint16* vendor, uint16* product);

// One line of the database. True when it is a Windows entry for vendor
// and product (any version, any bus) and parses; the mapping is cleared
// first. Comments, blank lines and other platforms give false.
bool		parse_line(const char* line, uint16 vendor, uint16 product,
				controller_mapping& mapping);

// The first matching line of a file, else B_ENTRY_NOT_FOUND.
status_t	find_mapping(const char* path, uint16 vendor, uint16 product,
				controller_mapping& mapping);

// The mapping in the R5 file first (R5's own additions, MIT; none ship
// yet), then in the unmodified database. Both in game_controllers under
// B_SYSTEM_NONPACKAGED_DATA_DIRECTORY, else under B_SYSTEM_DATA_DIRECTORY.
status_t	find_system_mapping(uint16 vendor, uint16 product,
				controller_mapping& mapping);

// The raw axis behind DirectInput axis n: the raw axes sorted by their HID
// usage, the stable way. Unknown usages (0) keep their place at the end.
void		axis_order(const uint16* usages, int32 count, int32* order);

// hid_shared's hat value (0 centered, 1 N, 2 NE ... 8 NW) as the
// DirectInput direction bits of "h0.1" to "h0.8".
uint8		hat_mask(uint8 hat);

void		map_state(const controller_mapping& mapping,
				const raw_state& raw, mapped_state& out);

bool		mapping_has_button(const controller_mapping& mapping,
				game_controller_button button);
bool		mapping_has_axis(const controller_mapping& mapping,
				game_controller_axis axis);

game_controller_family family_for(uint16 vendor, uint16 product,
				const char* name, bool hasMapping);


}	// namespace R5GameController


#endif	// R5_GAME_CONTROLLER_MAPPING_H
