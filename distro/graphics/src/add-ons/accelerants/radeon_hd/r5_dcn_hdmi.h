/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * DCN 2.1 display step 1 (docs/DISPLAY-DCN.md): read the hotplug state
 * and the EDID of HDMI and DVI connectors. Read-only for the picture:
 * no OTG, HUBP, DCCG, DIO or PHY register is written.
 */
#ifndef R5_DCN_HDMI_H
#define R5_DCN_HDMI_H


#include <SupportDefs.h>


// Called from connector_walk_object_records() for each I2C and HPD record of
// a display path, with the raw ids from the VBIOS (atom_i2c_record.i2c_id,
// atom_hpd_int_record.pin_id).
void r5_dcn_note_i2c_record(uint32 connectorIndex, uint8 i2cId);
void r5_dcn_note_hpd_record(uint32 connectorIndex, uint8 pinId);

// Called once from detect_displays() on DCN. Logs one block per HDMI or DVI
// connector. Does not change gDisplay[], so app_server sees no new screen.
void r5_dcn_hdmi_probe();


#endif	// R5_DCN_HDMI_H
