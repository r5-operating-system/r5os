/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * DCN 2.1 step 1 (hotplug and EDID) and step 2a (what the firmware left),
 * docs/DISPLAY-DCN.md.
 *
 * Step 1 writes only the DC_GPIO_DDCn MASK, A and EN registers of the DDC
 * pad the VBIOS names for an HDMI or DVI connector, while the EDID is read,
 * and puts their values back. Step 2a only reads.
 *
 * Sources (Linux v6.12, all MIT, AMD; R5 code written from them):
 * - bios/bios_parser2.c: get_gpio_i2c_info() and
 *   bios_parser_get_gpio_pin_info() match the record id against gpio_id.
 * - gpio/dcn21/hw_translate_dcn21.c: offset_to_id() maps the DDC A register
 *   to the DDC line and the HPD A mask to the HPD number.
 * - gpio/dcn21/hw_factory_dcn21.c: HPD n uses block HPD(n-1).
 * - gpio/hw_gpio.c: dal_hw_gpio_get_value() reads Y in every mode.
 * - gpio/hw_hpd.c, link/protocols/link_hpd.c: the sense bits.
 * Deliberate difference: Linux reads the EDID with the DC_I2C hardware
 * engine (dce/dce_i2c_hw.c). R5 bit-bangs the same pad through its GPIO
 * registers with Haiku's DDC code, the way Linux's own software engine
 * (dce/dce_i2c_sw.c) does. It touches no DC_I2C arbitration the firmware or
 * DMCUB could be using.
 */


#include "dcn_display.h"

#include <stdio.h>
#include <string.h>

#include <Debug.h>
#include <OS.h>

#include "accelerant.h"
#include "accelerant_protos.h"
#include "bios.h"
#include "connector.h"
#include "dcn21_regs.h"
#include "ddc.h"
#include "edid.h"


#define HDMILOG(x...) _sPrintf("radeon_hd: dcn hdmi: " x)
#define HEADLOG(x...) _sPrintf("radeon_hd: dcn head: " x)


struct dcn21_ddc_pad {
	const char*	name;
	uint32		mask;
};

static const dcn21_ddc_pad kDdcPads[] = {
	{ "DDC1", DCN21_GPIO_DDC1_MASK },
	{ "DDC2", DCN21_GPIO_DDC2_MASK },
	{ "DDC3", DCN21_GPIO_DDC3_MASK },
	{ "DDC4", DCN21_GPIO_DDC4_MASK },
	{ "DDC5", DCN21_GPIO_DDC5_MASK },
	{ "DDCVGA", DCN21_GPIO_DDCVGA_MASK },
};

// DC_GPIO_HPD_A__DC_GPIO_HPD1_A_MASK .. HPD6; the same bits in Y.
static const uint32 kHpdMasks[] = {
	0x00000001, 0x00000100, 0x00010000, 0x01000000, 0x04000000, 0x10000000
};


static inline uint32
dcn21_read(uint32 offset)
{
	return Read32(OUT, DCN21_BYTES(offset));
}


static inline void
dcn21_write(uint32 offset, uint32 value)
{
	Write32(OUT, DCN21_BYTES(offset), value);
}


// 0xffffffff is what a register that does not answer reads. None of the
// registers read here can hold it: each has reserved bits.
static inline bool
dcn21_all_ones(uint32 value)
{
	return value == 0xffffffff;
}


struct dcn21_ddc_bus {
	uint32	mask;
	uint32	a;
	uint32	en;
	uint32	y;
};


static status_t
dcn21_ddc_set(void* cookie, int clock, int data)
{
	dcn21_ddc_bus* bus = (dcn21_ddc_bus*)cookie;
	// Open drain: EN=1 pulls the line low, EN=0 lets the pull-up win.
	uint32 en = dcn21_read(bus->en);
	// A pad that stops answering is not written back as all ones.
	if (dcn21_all_ones(en))
		return B_IO_ERROR;
	en &= ~(DCN21_DDC_CLK | DCN21_DDC_DATA);
	if (!clock)
		en |= DCN21_DDC_CLK;
	if (!data)
		en |= DCN21_DDC_DATA;
	dcn21_write(bus->en, en);
	dcn21_read(bus->en);
	return B_OK;
}


static status_t
dcn21_ddc_get(void* cookie, int* clock, int* data)
{
	dcn21_ddc_bus* bus = (dcn21_ddc_bus*)cookie;
	uint32 y = dcn21_read(bus->y);
	*clock = (y & DCN21_DDC_CLK) != 0;
	*data = (y & DCN21_DDC_DATA) != 0;
	return B_OK;
}


// ddc2_read_edid1() has its own retry count and the i2c layer its own
// timeouts (accelerants/common/i2c.c); nothing here waits without a limit.
static bool
dcn21_ddc_read_edid(uint32 padMask, edid1_info* edid)
{
	dcn21_ddc_bus bus;
	bus.mask = padMask;
	bus.a = padMask + 1;
	bus.en = padMask + 2;
	bus.y = padMask + 3;

	uint32 savedMask = dcn21_read(bus.mask);
	uint32 savedA = dcn21_read(bus.a);
	uint32 savedEn = dcn21_read(bus.en);
	// A pad that reads all ones does not answer: never write to it.
	if (dcn21_all_ones(savedMask) || dcn21_all_ones(savedA)
		|| dcn21_all_ones(savedEn) || dcn21_all_ones(dcn21_read(bus.y))) {
		HDMILOG("ddc pad registers read 0xffffffff, EDID not read\n");
		return false;
	}

	// Inputs first, A low, then hand both pins to software in I2C pad mode,
	// the order gpio_lock_i2c() uses on DCE.
	dcn21_write(bus.en, savedEn & ~(DCN21_DDC_CLK | DCN21_DDC_DATA));
	dcn21_write(bus.a, savedA & ~(DCN21_DDC_CLK | DCN21_DDC_DATA));
	dcn21_write(bus.mask, (savedMask & ~DCN21_DDC_AUX_PAD_MODE)
		| DCN21_DDC_CLK | DCN21_DDC_DATA);
	dcn21_read(bus.mask);

	i2c_bus i2c;
	ddc2_init_timing(&i2c);
	i2c.cookie = &bus;
	i2c.set_signals = &dcn21_ddc_set;
	i2c.get_signals = &dcn21_ddc_get;
	status_t status = ddc2_read_edid1(&i2c, edid, NULL, NULL);

	// Release the lines, then put the pad back the way it was.
	dcn21_write(bus.en, savedEn);
	dcn21_write(bus.a, savedA);
	dcn21_write(bus.mask, savedMask);
	dcn21_read(bus.mask);

	return status == B_OK;
}


static status_t
dcn21_probe_connector(uint32 id, bool hasI2c, uint8 i2cId, bool hasHpd,
	uint8 hpdPin, dcn_connector_state* state, edid1_info* edid)
{
	connector_info* connector = gConnector[id];
	state->connector = id;
	state->ddcPad = -1;
	state->hpd = -1;

	// DDC pad from the I2C record (hw_translate_dcn21.c offset_to_id()).
	uint32 ddcReg = 0;
	uint8 ddcShift = 0;
	if (hasI2c && dcn_vbios_pin_lookup(i2cId, &ddcReg, &ddcShift)) {
		for (uint32 i = 0; i < B_COUNT_OF(kDdcPads); i++) {
			if (ddcReg == DCN21_SEG2 + kDdcPads[i].mask + 1) {
				state->ddcPad = (int32)i;
				break;
			}
		}
	}

	// HPD number from the HPD record.
	uint32 hpdReg = 0;
	uint8 hpdShift = 0;
	if (hasHpd && dcn_vbios_pin_lookup(hpdPin, &hpdReg, &hpdShift)
		&& hpdReg == DCN21_SEG2 + DCN21_GPIO_HPD_A && hpdShift < 32) {
		for (uint32 i = 0; i < B_COUNT_OF(kHpdMasks); i++) {
			if ((1u << hpdShift) == kHpdMasks[i]) {
				state->hpd = (int32)i;
				break;
			}
		}
	}

	HDMILOG("connector %" B_PRIu32 " %s i2c 0x%02x ddc %s (reg 0x%" B_PRIx32
		") hpd pin 0x%02x HPD%" B_PRId32 " (reg 0x%" B_PRIx32 " bit %u)\n", id,
		get_connector_name(connector->type), hasI2c ? i2cId : 0xff,
		state->ddcPad >= 0 ? kDdcPads[state->ddcPad].name : "unknown", ddcReg,
		hasHpd ? hpdPin : 0xff, state->hpd >= 0 ? state->hpd + 1 : 0, hpdReg,
		(unsigned)hpdShift);

	// Hotplug: pure reads. A register that reads all ones says nothing.
	bool sense = false;
	bool pin = false;
	if (state->hpd >= 0) {
		uint32 y = dcn21_read(DCN21_GPIO_HPD_Y);
		pin = !dcn21_all_ones(y) && (y & kHpdMasks[state->hpd]) != 0;
		if ((uint32)state->hpd < DCN21_HPD_BLOCKS) {
			uint32 block = (uint32)state->hpd * DCN21_HPD_STRIDE;
			uint32 control = dcn21_read(DCN21_HPD0_CONTROL + block);
			uint32 status = dcn21_read(DCN21_HPD0_INT_STATUS + block);
			bool enabled = !dcn21_all_ones(control)
				&& (control & DCN21_HPD_EN) != 0;
			if (enabled && !dcn21_all_ones(status)) {
				sense = (status & (DCN21_HPD_SENSE | DCN21_HPD_SENSE_DELAYED))
					!= 0;
			}
			HDMILOG("connector %" B_PRIu32 " hpd control 0x%08" B_PRIx32
				" status 0x%08" B_PRIx32 " gpio y 0x%08" B_PRIx32
				" sense %s pin %s\n", id, control, status, y,
				enabled ? (sense ? "1" : "0") : "off", pin ? "1" : "0");
		} else {
			HDMILOG("connector %" B_PRIu32 " gpio y 0x%08" B_PRIx32 " pin %s\n",
				id, y, pin ? "1" : "0");
		}
	}
	state->hpdHigh = sense || pin;

	// EDID over DDC, only on a pad of the table above.
	if (state->ddcPad >= 0)
		state->edidFound = dcn21_ddc_read_edid(kDdcPads[state->ddcPad].mask,
			edid);
	else
		HDMILOG("connector %" B_PRIu32 " ddc pad unknown, EDID not read\n", id);

	HDMILOG("connector %" B_PRIu32 " connected %s (hpd %s, edid %s)\n", id,
		(state->edidFound || state->hpdHigh) ? "yes" : "no",
		state->hpd < 0 ? "unknown" : (state->hpdHigh ? "high" : "low"),
		state->edidFound ? "yes" : "no");
	return B_OK;
}


// Step 2a ------------------------------------------------------------------


// atomfirmware.h atom_master_list_of_command_functions_v2_1, from 0.
struct dcn21_table_name {
	const char*	name;
	int			index;
};

static const dcn21_table_name kTables[] = {
	{ "digxencodercontrol", 4 },
	{ "setpixelclock", 12 },
	{ "enabledisppowergating", 13 },
	{ "blankcrtc", 34 },
	{ "enablecrtc", 35 },
	{ "selectcrtc_source", 42 },
	{ "setcrtc_usingdtdtiming", 49 },
	{ "dig1transmittercontrol", 76 },
};


static void
dcn21_log_pipes()
{
	// Segment 1 (the DCCG) is read only when segment 2, which sticks 1 and 2
	// showed is safe to read, answers; until then segment 1 is unproven.
	// Stop immediately on all-ones so we never pay 60 x 139 ms of bus timeouts.
	if (dcn21_all_ones(dcn21_read(DCN21_DC_PINSTRAPS))) {
		HEADLOG("DC_PINSTRAPS 0xffffffff, pipe log skipped\n");
		return;
	}
	uint32 dmcub = dcn21_read(DCN21_DMCUB_CNTL);
	HEADLOG("dmcub cntl 0x%08" B_PRIx32 " (%s), dispclk cntl 0x%08" B_PRIx32
		"\n", dmcub, dcn21_all_ones(dmcub) ? "does not answer"
			: (dmcub & DCN21_DMCUB_ENABLE) != 0 ? "running" : "off",
		Read32(OUT, DCN21_SEG1_BYTES(DCN21_DENTIST_DISPCLK_CNTL)));

	for (uint32 i = 0; i < DCN21_OTG_COUNT; i++) {
		uint32 otg = i * DCN21_OTG_STRIDE;
		uint32 control = dcn21_read(DCN21_OTG_CONTROL + otg);
		uint32 hTotal = (dcn21_read(DCN21_OTG_H_TOTAL + otg)
			& DCN21_OTG_FIELD15_MASK) + 1;
		uint32 vTotal = (dcn21_read(DCN21_OTG_V_TOTAL + otg)
			& DCN21_OTG_FIELD15_MASK) + 1;
		uint32 hBlank = dcn21_read(DCN21_OTG_H_BLANK_START_END + otg);
		uint32 vBlank = dcn21_read(DCN21_OTG_V_BLANK_START_END + otg);
		uint32 hubp = i * DCN21_HUBP_STRIDE;
		uint32 opp = i * DCN21_OPP_STRIDE;
		HEADLOG("otg%" B_PRIu32 " control 0x%08" B_PRIx32 " %s total %" B_PRIu32
			"x%" B_PRIu32 " active %" B_PRIu32 "x%" B_PRIu32 " pixel rate 0x%08"
			B_PRIx32 " frame %" B_PRIu32 "\n", i, control,
			dcn21_all_ones(control) ? "does not answer"
				: (control & DCN21_OTG_MASTER_EN) != 0 ? "on" : "off",
			hTotal, vTotal,
			(hBlank & DCN21_OTG_FIELD15_MASK)
				- ((hBlank >> 16) & DCN21_OTG_FIELD15_MASK),
			(vBlank & DCN21_OTG_FIELD15_MASK)
				- ((vBlank >> 16) & DCN21_OTG_FIELD15_MASK),
			Read32(OUT, DCN21_SEG1_BYTES(DCN21_OTG_PIXEL_RATE_CNTL + i * 4)),
			dcn21_read(DCN21_OTG_STATUS_FRAME_COUNT + otg)
				& DCN21_OTG_FRAME_COUNT_MASK);
		HEADLOG("hubp%" B_PRIu32 " cntl 0x%08" B_PRIx32 " surface 0x%08" B_PRIx32
			"%08" B_PRIx32 " opp 0x%08" B_PRIx32 " fmt 0x%08" B_PRIx32 " dpg 0x%08"
			B_PRIx32 "\n", i, dcn21_read(DCN21_HUBP_DCHUBP_CNTL + hubp),
			dcn21_read(DCN21_HUBPREQ_PRIMARY_ADDRESS_HIGH + hubp),
			dcn21_read(DCN21_HUBPREQ_PRIMARY_ADDRESS + hubp),
			dcn21_read(DCN21_OPP_PIPE_CONTROL + opp),
			dcn21_read(DCN21_FMT_BIT_DEPTH_CONTROL + opp),
			dcn21_read(DCN21_DPG_CONTROL + opp));
	}
	for (uint32 i = 0; i < DCN21_DIG_COUNT; i++) {
		uint32 dig = i * DCN21_DIG_STRIDE;
		HEADLOG("dig%" B_PRIu32 " fe 0x%08" B_PRIx32 " be 0x%08" B_PRIx32 "\n",
			i, dcn21_read(DCN21_DIG_FE_CNTL + dig),
			dcn21_read(DCN21_DIG_BE_CNTL + dig));
	}
	for (uint32 i = 0; i < B_COUNT_OF(kTables); i++) {
		uint8 frev = 0;
		uint8 crev = 0;
		if (atom_parse_cmd_header(gAtomContext, kTables[i].index, &frev, &crev)
				== B_OK) {
			HEADLOG("vbios table %s (%d) v%u.%u\n", kTables[i].name,
				kTables[i].index, (unsigned)frev, (unsigned)crev);
		} else {
			HEADLOG("vbios table %s (%d) missing\n", kTables[i].name,
				kTables[i].index);
		}
	}
}


// The access gate. Reads only.
// - DC_PINSTRAPS is the first DCN register Linux reads
//   (resource/dcn21/dcn21_resource.c read_dce_straps()); all ones is not a
//   strap value.
// - The firmware drives the panel, so one OTG must be on, with a horizontal
//   total a real mode has.
// - For comparison, a register outside DCN the kernel reads in the same
//   boot: the SMU mailbox response (MP1 segment 0 0x16000 + 0x29a,
//   r5_smu12.cpp), so the log shows whether the accelerant's mapping answers.
static status_t
dcn21_check_access(char* why, size_t whySize)
{
	static const uint32 kSmuResponseByte = (0x16000u + 0x29au) * 4u;
	uint32 smu = Read32(OUT, kSmuResponseByte);
	uint32 straps = dcn21_read(DCN21_DC_PINSTRAPS);
	// Stop on the first all-ones read: on the 5625U each non-answering DCN
	// read takes ~139 ms, so looping over all four OTGs would add >1.1 s.
	if (dcn21_all_ones(straps)) {
		snprintf(why, whySize, "DC_PINSTRAPS 0xffffffff (smu response 0x%08"
			B_PRIx32 "%s)", smu, dcn21_all_ones(smu)
				? ", the mapping does not answer either" : "");
		return B_ERROR;
	}

	int32 panelOtg = -1;
	uint32 panelTotal = 0;
	for (uint32 i = 0; i < DCN21_OTG_COUNT; i++) {
		uint32 otg = i * DCN21_OTG_STRIDE;
		uint32 control = dcn21_read(DCN21_OTG_CONTROL + otg);
		if (dcn21_all_ones(control) || (control & DCN21_OTG_MASTER_EN) == 0)
			continue;
		uint32 hTotal = dcn21_read(DCN21_OTG_H_TOTAL + otg);
		if (dcn21_all_ones(hTotal))
			continue;
		uint32 total = (hTotal & DCN21_OTG_FIELD15_MASK) + 1;
		if (total >= 320 && total <= 16384) {
			panelOtg = (int32)i;
			panelTotal = total;
			break;
		}
	}

	if (panelOtg < 0) {
		snprintf(why, whySize, "no OTG is on with a real mode (pinstraps 0x%08"
			B_PRIx32 ", smu response 0x%08" B_PRIx32 ")", straps, smu);
		return B_ERROR;
	}
	snprintf(why, whySize, "pinstraps 0x%08" B_PRIx32 ", panel on otg%" B_PRId32
		" total %" B_PRIu32 ", smu response 0x%08" B_PRIx32, straps, panelOtg,
		panelTotal, smu);
	return B_OK;
}


status_t dcn21_probe_path(const char* name);
status_t dcn21_head_start(const dcn_connector_state* state,
	const edid1_info* edid);


const dcn_ops gDcn21Ops = {
	"DCN 2.1",
	dcn21_probe_connector,
	dcn21_log_pipes,
	dcn21_check_access,
	dcn21_probe_path,
	dcn21_head_start,
};
