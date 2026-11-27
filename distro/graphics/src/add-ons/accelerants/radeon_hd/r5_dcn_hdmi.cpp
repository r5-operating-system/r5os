/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * DCN 2.1 display step 1 (docs/DISPLAY-DCN.md): hotplug and EDID of the
 * HDMI and DVI connectors, logged to the syslog.
 *
 * Nothing here changes the picture. The only registers written are the
 * DC_GPIO_DDCn MASK, A and EN registers of the DDC pad that the VBIOS
 * names for an HDMI or DVI connector, and only while the EDID is read.
 * Their values are put back afterwards. The internal panel's eDP path,
 * the GFX ring and every OTG, HUBP, DCCG, DIO and PHY register are left
 * alone. `dcn_hdmi_probe false` in the radeon_hd settings turns it off.
 *
 * Sources (Linux v6.12, all MIT, written for R5 from these, not copied):
 * - include/renoir_ip_offset.h: DMU_BASE instance 0 segment 2 is 0x34C0.
 * - include/asic_reg/dcn/dcn_2_1_0_offset.h: mmDC_GPIO_DDC1_MASK 0x28d0
 *   through mmDC_GPIO_DDCVGA_Y 0x28eb, mmDC_GPIO_HPD_A 0x28f5,
 *   mmDC_GPIO_HPD_Y 0x28f7, mmHPD0_DC_HPD_INT_STATUS 0x1f14 and
 *   mmHPD0_DC_HPD_CONTROL 0x1f16 with a stride of 8, all BASE_IDX 2.
 * - include/asic_reg/dcn/dcn_2_1_0_sh_mask.h: DDCn CLK at bit 0, DATA at
 *   bit 8, AUX_PADn_MODE at bit 16; DC_GPIO_HPDn_A/Y masks; DC_HPD_SENSE
 *   bit 1, DC_HPD_SENSE_DELAYED bit 4, DC_HPD_EN bit 28.
 * - include/atomfirmware.h: atom_gpio_pin_lut_v2_1, 8-byte
 *   atom_gpio_pin_assignment (data_a_reg_index, gpio_bitshift,
 *   gpio_mask_bitshift, gpio_id).
 * - display/dc/bios/bios_parser2.c: get_gpio_i2c_info() matches the record
 *   i2c_id against gpio_id; bios_parser_get_gpio_pin_info() does the same
 *   for the HPD pin_id.
 * - display/dc/gpio/dcn21/hw_translate_dcn21.c: offset_to_id() maps the
 *   DDC A register to the DDC line and the HPD A mask to the HPD number.
 * - display/dc/gpio/dcn21/hw_factory_dcn21.c: HPD n uses block HPD(n-1).
 * - display/dc/gpio/hw_gpio.c: the Y register is read in every mode.
 */


#include "r5_dcn_hdmi.h"

#include <stdio.h>
#include <string.h>

#include <Debug.h>
#include <driver_settings.h>

#include "accelerant.h"
#include "accelerant_protos.h"
#include "bios.h"
#include "connector.h"
#include "ddc.h"
#include "edid.h"


#define R5LOG(x...) _sPrintf("radeon_hd: dcn hdmi: " x)


// DMU_BASE__INST0_SEG2, renoir_ip_offset.h.
static const uint32 kDmuSeg2 = 0x34c0;

// DC_GPIO_DDC1..5 and DDCVGA, dcn_2_1_0_offset.h. Each pad has MASK, A,
// EN and Y at +0, +1, +2, +3.
struct r5_ddc_pad {
	const char*	name;
	uint32		mask;
};

static const r5_ddc_pad kDdcPads[] = {
	{ "DDC1", 0x28d0 },
	{ "DDC2", 0x28d4 },
	{ "DDC3", 0x28d8 },
	{ "DDC4", 0x28dc },
	{ "DDC5", 0x28e0 },
	{ "DDCVGA", 0x28e8 },
};

static const uint32 kDdcClkBit = 1u << 0;
static const uint32 kDdcDataBit = 1u << 8;
static const uint32 kDdcAuxPadMode = 1u << 16;

static const uint32 kGpioHpdA = 0x28f5;
static const uint32 kGpioHpdY = 0x28f7;
// DC_GPIO_HPD_A__DC_GPIO_HPD1_A_MASK .. HPD6, the same bits in Y.
static const uint32 kHpdMasks[] = {
	0x00000001, 0x00000100, 0x00010000, 0x01000000, 0x04000000, 0x10000000
};
// DCN 2.1 has the blocks HPD0 to HPD4.
static const uint32 kHpdBlocks = 5;
static const uint32 kHpd0IntStatus = 0x1f14;
static const uint32 kHpd0Control = 0x1f16;
static const uint32 kHpdStride = 8;
static const uint32 kHpdSense = 1u << 1;
static const uint32 kHpdSenseDelayed = 1u << 4;
static const uint32 kHpdEnable = 1u << 28;

// Master data table format 2: gpio_pin_lut is index 12 (atomfirmware.h,
// atom_master_list_of_data_tables_v2_1). The same index is used by
// gpio_general_populate().
static const int kGpioPinLutIndex = 12;
static const uint32 kPinBytes = 8;


struct r5_dcn_records {
	bool	hasI2c;
	uint8	i2cId;
	bool	hasHpd;
	uint8	hpdPin;
};

static r5_dcn_records sRecords[ATOM_MAX_SUPPORTED_DEVICE];


void
r5_dcn_note_i2c_record(uint32 connectorIndex, uint8 i2cId)
{
	if (connectorIndex >= ATOM_MAX_SUPPORTED_DEVICE)
		return;
	sRecords[connectorIndex].hasI2c = true;
	sRecords[connectorIndex].i2cId = i2cId;
}


void
r5_dcn_note_hpd_record(uint32 connectorIndex, uint8 pinId)
{
	if (connectorIndex >= ATOM_MAX_SUPPORTED_DEVICE)
		return;
	sRecords[connectorIndex].hasHpd = true;
	sRecords[connectorIndex].hpdPin = pinId;
}


static inline uint32
r5_reg(uint32 dwordIndex)
{
	return dwordIndex * 4;
}


// Looks up gpio_id in gpio_pin_lut. Returns the absolute dword index of the
// A register and the bit shift.
static bool
r5_pin_lookup(uint8 gpioId, uint32* regIndex, uint8* bitShift)
{
	uint16 size = 0;
	uint8 frev = 0;
	uint8 crev = 0;
	uint16 offset = 0;
	if (atom_parse_data_header(gAtomContext, kGpioPinLutIndex, &size, &frev,
			&crev, &offset) != B_OK)
		return false;
	// bios_parser2.c get_gpio_i2c_info() only knows content revision 1.
	if (crev != 1 || size < 4 + kPinBytes)
		return false;

	const uint8* table = gAtomContext->bios + offset;
	uint32 count = (size - 4) / kPinBytes;
	for (uint32 i = 0; i < count; i++) {
		const uint8* pin = table + 4 + i * kPinBytes;
		if (pin[6] != gpioId)
			continue;
		*regIndex = (uint32)pin[0] | ((uint32)pin[1] << 8)
			| ((uint32)pin[2] << 16) | ((uint32)pin[3] << 24);
		*bitShift = pin[4];
		return true;
	}
	return false;
}


struct r5_ddc_bus {
	uint32	maskReg;
	uint32	aReg;
	uint32	enReg;
	uint32	yReg;
};


static status_t
r5_ddc_set(void* cookie, int clock, int data)
{
	r5_ddc_bus* bus = (r5_ddc_bus*)cookie;
	// Open drain: EN=1 pulls the line low, EN=0 lets the pull-up win.
	uint32 en = Read32(OUT, bus->enReg) & ~(kDdcClkBit | kDdcDataBit);
	if (!clock)
		en |= kDdcClkBit;
	if (!data)
		en |= kDdcDataBit;
	Write32(OUT, bus->enReg, en);
	Read32(OUT, bus->enReg);
	return B_OK;
}


static status_t
r5_ddc_get(void* cookie, int* clock, int* data)
{
	r5_ddc_bus* bus = (r5_ddc_bus*)cookie;
	uint32 y = Read32(OUT, bus->yReg);
	*clock = (y & kDdcClkBit) != 0;
	*data = (y & kDdcDataBit) != 0;
	return B_OK;
}


static bool
r5_ddc_read_edid(uint32 padMask, edid1_info* edid)
{
	r5_ddc_bus bus;
	bus.maskReg = r5_reg(kDmuSeg2 + padMask);
	bus.aReg = r5_reg(kDmuSeg2 + padMask + 1);
	bus.enReg = r5_reg(kDmuSeg2 + padMask + 2);
	bus.yReg = r5_reg(kDmuSeg2 + padMask + 3);

	uint32 savedMask = Read32(OUT, bus.maskReg);
	uint32 savedA = Read32(OUT, bus.aReg);
	uint32 savedEn = Read32(OUT, bus.enReg);

	// Inputs first, A low, then hand the two pins to software in I2C pad
	// mode. That is the order gpio_lock_i2c() uses on DCE.
	Write32(OUT, bus.enReg, savedEn & ~(kDdcClkBit | kDdcDataBit));
	Write32(OUT, bus.aReg, savedA & ~(kDdcClkBit | kDdcDataBit));
	Write32(OUT, bus.maskReg,
		(savedMask & ~kDdcAuxPadMode) | kDdcClkBit | kDdcDataBit);
	Read32(OUT, bus.maskReg);

	i2c_bus i2c;
	ddc2_init_timing(&i2c);
	i2c.cookie = &bus;
	i2c.set_signals = &r5_ddc_set;
	i2c.get_signals = &r5_ddc_get;
	status_t status = ddc2_read_edid1(&i2c, edid, NULL, NULL);

	// Release the lines, then put the pad back the way it was.
	Write32(OUT, bus.enReg, savedEn);
	Write32(OUT, bus.aReg, savedA);
	Write32(OUT, bus.maskReg, savedMask);
	Read32(OUT, bus.maskReg);

	return status == B_OK;
}


static void
r5_log_edid(uint32 id, const edid1_info* edid)
{
	char name[EDID1_EXTRA_STRING_LEN + 1] = "";
	const edid1_detailed_timing* timing = NULL;
	for (int i = 0; i < EDID1_NUM_DETAILED_MONITOR_DESC; i++) {
		const edid1_detailed_monitor* monitor = &edid->detailed_monitor[i];
		if (monitor->monitor_desc_type == EDID1_MONITOR_NAME && name[0] == 0) {
			memcpy(name, monitor->data.monitor_name, EDID1_EXTRA_STRING_LEN);
			name[EDID1_EXTRA_STRING_LEN] = 0;
			for (int c = 0; name[c] != 0; c++) {
				if (name[c] == '\n') {
					name[c] = 0;
					break;
				}
			}
			for (int c = (int)strlen(name) - 1; c >= 0 && name[c] == ' '; c--)
				name[c] = 0;
		} else if (monitor->monitor_desc_type == EDID1_IS_DETAILED_TIMING
			&& timing == NULL) {
			timing = &monitor->data.detailed_timing;
		}
	}

	R5LOG("connector %" B_PRIu32 " monitor \"%s\" vendor %.3s product 0x%04"
		B_PRIx16 " edid %u.%u\n", id, name[0] != 0 ? name : "?",
		edid->vendor.manufacturer, edid->vendor.prod_id,
		edid->version.version, edid->version.revision);

	if (timing == NULL || timing->pixel_clock == 0) {
		R5LOG("connector %" B_PRIu32 " preferred mode none\n", id);
		return;
	}
	uint64 hTotal = (uint64)timing->h_active + timing->h_blank;
	uint64 vTotal = (uint64)timing->v_active + timing->v_blank;
	uint64 pixelHz = (uint64)timing->pixel_clock * 10000;
	uint64 centiHz = hTotal * vTotal > 0 ? pixelHz * 100 / (hTotal * vTotal) : 0;
	R5LOG("connector %" B_PRIu32 " preferred mode %" B_PRIu16 "x%" B_PRIu16
		"%s %" B_PRIu64 ".%02" B_PRIu64 " Hz pixel clock %" B_PRIu32 " kHz\n",
		id, timing->h_active, timing->v_active, timing->interlaced ? "i" : "",
		centiHz / 100, centiHz % 100, (uint32)timing->pixel_clock * 10);
}


static bool
r5_dcn_probe_enabled()
{
	bool enabled = true;
	void* settings = load_driver_settings("radeon_hd");
	if (settings != NULL) {
		enabled = get_driver_boolean_parameter(settings, "dcn_hdmi_probe",
			true, true);
		unload_driver_settings(settings);
	}
	return enabled;
}


static void
r5_probe_connector(uint32 id)
{
	connector_info* connector = gConnector[id];
	const r5_dcn_records& records = sRecords[id];

	// DDC pad from the I2C record.
	int32 pad = -1;
	uint32 ddcReg = 0;
	uint8 ddcShift = 0;
	if (records.hasI2c && r5_pin_lookup(records.i2cId, &ddcReg, &ddcShift)) {
		for (uint32 i = 0; i < B_COUNT_OF(kDdcPads); i++) {
			if (ddcReg == kDmuSeg2 + kDdcPads[i].mask + 1) {
				pad = (int32)i;
				break;
			}
		}
	}

	// HPD number from the HPD record.
	int32 hpd = -1;
	uint32 hpdReg = 0;
	uint8 hpdShift = 0;
	if (records.hasHpd && r5_pin_lookup(records.hpdPin, &hpdReg, &hpdShift)
		&& hpdReg == kDmuSeg2 + kGpioHpdA && hpdShift < 32) {
		for (uint32 i = 0; i < B_COUNT_OF(kHpdMasks); i++) {
			if ((1u << hpdShift) == kHpdMasks[i]) {
				hpd = (int32)i;
				break;
			}
		}
	}

	R5LOG("connector %" B_PRIu32 " %s i2c 0x%02x ddc %s (reg 0x%" B_PRIx32
		") hpd pin 0x%02x HPD%" B_PRId32 " (reg 0x%" B_PRIx32 " bit %u)\n", id,
		get_connector_name(connector->type),
		records.hasI2c ? records.i2cId : 0xff, pad >= 0 ? kDdcPads[pad].name
			: "unknown", ddcReg, records.hasHpd ? records.hpdPin : 0xff,
		hpd >= 0 ? hpd + 1 : 0, hpdReg, hpdShift);

	// Hotplug: pure reads.
	bool sense = false;
	bool pin = false;
	if (hpd >= 0) {
		uint32 y = Read32(OUT, r5_reg(kDmuSeg2 + kGpioHpdY));
		pin = (y & kHpdMasks[hpd]) != 0;
		if ((uint32)hpd < kHpdBlocks) {
			uint32 block = (uint32)hpd * kHpdStride;
			uint32 control = Read32(OUT, r5_reg(kDmuSeg2 + kHpd0Control + block));
			uint32 status = Read32(OUT,
				r5_reg(kDmuSeg2 + kHpd0IntStatus + block));
			bool enabled = (control & kHpdEnable) != 0;
			if (enabled)
				sense = (status & (kHpdSense | kHpdSenseDelayed)) != 0;
			R5LOG("connector %" B_PRIu32 " hpd control 0x%08" B_PRIx32
				" status 0x%08" B_PRIx32 " gpio y 0x%08" B_PRIx32
				" sense %s pin %s\n", id, control, status, y,
				enabled ? (sense ? "1" : "0") : "off", pin ? "1" : "0");
		} else {
			R5LOG("connector %" B_PRIu32 " gpio y 0x%08" B_PRIx32 " pin %s\n",
				id, y, pin ? "1" : "0");
		}
	}

	// EDID over DDC, only on a pad this table knows.
	bool edidFound = false;
	edid1_info edid;
	if (pad >= 0) {
		memset(&edid, 0, sizeof(edid));
		edidFound = r5_ddc_read_edid(kDdcPads[pad].mask, &edid);
	} else
		R5LOG("connector %" B_PRIu32 " ddc pad unknown, EDID not read\n", id);

	R5LOG("connector %" B_PRIu32 " connected %s (hpd %s, edid %s)\n", id,
		(edidFound || sense || pin) ? "yes" : "no",
		hpd < 0 ? "unknown" : ((sense || pin) ? "high" : "low"),
		edidFound ? "yes" : "no");
	if (edidFound)
		r5_log_edid(id, &edid);
}


void
r5_dcn_hdmi_probe()
{
	static bool sDone = false;
	if (sDone)
		return;
	sDone = true;

	radeon_shared_info& info = *gInfo->shared_info;
	// The register table above is DCN 2.1 only.
	if (info.chipsetID != RADEON_RENOIR || info.displayMajor != 2
		|| info.displayMinor != 1) {
		R5LOG("not DCN 2.1 (%" B_PRIu16 ".%" B_PRIu16 "), skipped\n",
			(uint16)info.displayMajor, (uint16)info.displayMinor);
		return;
	}
	if (!r5_dcn_probe_enabled()) {
		R5LOG("off (dcn_hdmi_probe false)\n");
		return;
	}

	uint32 found = 0;
	for (uint32 id = 0; id < ATOM_MAX_SUPPORTED_DEVICE; id++) {
		connector_info* connector = gConnector[id];
		if (connector == NULL || !connector->valid)
			continue;
		uint32 type = connector->type;
		if (type != VIDEO_CONNECTOR_HDMIA && type != VIDEO_CONNECTOR_HDMIB
			&& type != VIDEO_CONNECTOR_DVID && type != VIDEO_CONNECTOR_DVII)
			continue;
		found++;
		r5_probe_connector(id);
	}
	if (found == 0)
		R5LOG("no HDMI or DVI connector in the VBIOS\n");
}
