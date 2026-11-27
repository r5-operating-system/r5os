/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * DCN 2.1 step 2 (docs/DISPLAY-DCN.md): with `dcn_hdmi_modeset true`, light
 * one HDMI or DVI connector on a pipe the firmware left off, as a DVI (TMDS)
 * stream without a plane. The OPP's display pattern generator (DPG) shows
 * colour squares. The panel's OTG, OPP, DIG and PHY are never written.
 *
 * The sequence is Linux v6.12 DC (all MIT, AMD) for a stream without a
 * plane on DCN 2.1, in the same order with the same values:
 *   hwss/dce110/dce110_hwseq.c  dce110_apply_single_controller_ctx_to_hw()
 *   hwss/dcn20/dcn20_hwseq.c    dcn20_enable_stream_timing(),
 *                               dcn20_blank_pixel_data(),
 *                               dcn20_wait_for_blank_complete(),
 *                               dcn20_setup_vupdate_interrupt(),
 *                               dcn20_enable_stream()
 *   hwss/dcn10/dcn10_hwseq.c    dcn10_get_vupdate_offset_from_vsync()
 *   optc/dcn10/dcn10_optc.c     optc1_enable_optc_clock(),
 *                               optc1_program_timing(),
 *                               optc1_program_global_sync(),
 *                               optc1_set_vtg_params(), optc1_set_drr(),
 *                               optc1_set_static_screen_control(),
 *                               optc1_setup_vertical_interrupt2()
 *   optc/dcn20/dcn20_optc.c     optc2_enable_crtc(),
 *                               optc2_setup_manual_trigger()
 *   opp/dcn10/dcn10_opp.c       opp1_program_fmt(), opp1_set_dyn_expansion(),
 *                               opp1_pipe_clock_control()
 *   opp/dcn20/dcn20_opp.c       opp2_set_disp_pattern_generator(),
 *                               opp2_program_left_edge_extra_pixel(),
 *                               opp2_dpg_is_pending()
 *   core/dc_resource.c          resource_build_bit_depth_reduction_params(),
 *                               resource_map_clock_resources()
 *   dce/dce_clock_source.c      dce112_program_pix_clk(),
 *                               dce112_program_pixel_clk_resync()
 *   bios/command_table2.c       set_pixel_clock_v7(),
 *                               encoder_control_digx_v1_5(),
 *                               transmitter_control_v1_6()
 *   bios/dce112/command_table_helper2_dce112.c (parameter values)
 *   dio/dcn10/dcn10_stream_encoder.c enc1_dig_connect_to_otg(),
 *                               enc1_setup_stereo_sync(),
 *                               enc1_stream_encoder_dvi_set_stream_attribute()
 *   dio/dcn10/dcn10_link_encoder.c dcn10_link_encoder_setup(),
 *                               dcn10_link_encoder_connect_dig_be_to_fe(),
 *                               dcn10_link_encoder_enable_tmds_output()
 *   link/link_dpms.c            link_set_dpms_on(), enable_link_hdmi()
 *   link/hwss/link_hwss_dio.c   setup_dio_stream_encoder(),
 *                               setup_dio_stream_attribute()
 *
 * Deliberate differences (docs/DISPLAY-DCN.md, "Step 2, compared with
 * Linux"): DVI signal on the HDMI connector; DPG colour squares instead of
 * black (Linux's own visual_confirm path); no DISPCLK/DCFCLK update and no
 * watermarks (no plane, no HUBP fetch); VSTARTUP/VUPDATE/VREADY without DML;
 * no DMCUB (the VBIOS tables run in the accelerant's interpreter, the
 * fallback command_table2.c has); no dc init_hw (the firmware state is kept,
 * as with Linux's seamless boot).
 */


#include "dcn_display.h"

#include <string.h>

#include <Debug.h>
#include <OS.h>

#include "accelerant.h"
#include "accelerant_protos.h"
#include "bios.h"
#include "connector.h"
#include "dcn21_regs.h"
#include "edid.h"


#define HEADLOG(x...) _sPrintf("radeon_hd: dcn head: " x)


// atomfirmware.h atom_master_list_of_command_functions_v2_1, from 0.
static const int kTableDigEncoderControl = 4;
static const int kTableSetPixelClock = 12;
static const int kTableTransmitterControl = 76;

// atomfirmware.h and atombios.h values.
static const uint8 kAtomComboPhyPll0 = 20;			// ATOM_COMBOPHY_PLL0
static const uint8 kAtomEncoderModeDvi = 2;			// ATOM_ENCODER_MODE_DVI
static const uint8 kAtomEncoderCmdStreamSetup = 0x0f;
static const uint8 kAtomTransmitterDisable = 0;
static const uint8 kAtomTransmitterEnable = 1;
static const uint8 kAtomDigModeV6Dvi = 2;			// ATOM_TRANSMITTER_DIGMODE_V6_DVI
// hw_shared.h: TEST_PATTERN_MODE_COLORSQUARES_RGB 0,
// TEST_PATTERN_DYN_RANGE_VESA 0, TEST_PATTERN_COLOR_FORMAT_BPC_8 1.
static const uint32 kDpgBitDepth8 = 1;
// optc2_setup_manual_trigger(): TRIGA source 21, rising edge 1.
static const uint32 kTrigaSource = 21;

// DVI single link stops at 165 MHz.
static const uint32 kDviSingleLinkKHz = 165000;

// ObjectID.h encoder ids.
static const uint16 kUniphy = 0x1e;
static const uint16 kUniphy1 = 0x20;
static const uint16 kUniphy2 = 0x21;

// Waits, with Linux's limits.
static const bigtime_t kClockOnTimeout = 1000;		// REG_WAIT 1 x 1000 us
static const int kBlankPolls = 1000;				// 1000 x 100 us


// The VBIOS parameter blocks, laid out as in atomfirmware.h.
struct dcn21_set_pixel_clock_v1_7 {
	uint32	pixclk_100hz;
	uint8	pll_id;
	uint8	encoderobjid;
	uint8	encoder_mode;
	uint8	miscinfo;
	uint8	crtc_id;
	uint8	deep_color_ratio;
	uint8	reserved1[2];
	uint32	reserved2;
} _PACKED;

struct dcn21_dig_encoder_stream_setup_v1_5 {
	uint8	digid;
	uint8	action;
	uint8	digmode;
	uint8	lanenum;
	uint32	pclk_10khz;
	uint8	bitpercolor;
	uint8	dplinkrate_270mhz;
	uint8	reserved[2];
} _PACKED;

struct dcn21_dig_transmitter_control_v1_6 {
	uint8	phyid;
	uint8	action;
	uint8	digmode;
	uint8	lanenum;
	uint32	symclk_10khz;
	uint8	hpdsel;
	uint8	digfe_sel;
	uint8	connobj_id;
	uint8	reserved;
	uint32	reserved1;
} _PACKED;

static_assert(sizeof(dcn21_set_pixel_clock_v1_7) == 16, "pixel clock v1.7");
static_assert(sizeof(dcn21_dig_encoder_stream_setup_v1_5) == 12,
	"stream setup v1.5");
static_assert(sizeof(dcn21_dig_transmitter_control_v1_6) == 16,
	"transmitter control v1.6");


// The pipe being started. Bounds are checked once in dcn21_head_start().
struct dcn21_head {
	uint32		otg;		// instance 0..3; OPP, DPG and FMT use the same
	uint32		dig;		// instance 0..4 = transmitter = PHY
	int32		hpd;		// 0-based
	uint8		encoderObject;
	uint8		connectorObject;
	dcn_timing	timing;
	bool		otgStarted;
	bool		phyOn;
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


static inline void
dcn21_update(uint32 offset, uint32 mask, uint32 value)
{
	dcn21_write(offset, (dcn21_read(offset) & ~mask) | (value & mask));
}


static bool
dcn21_wait_set(uint32 offset, uint32 bit, bigtime_t timeout)
{
	bigtime_t end = system_time() + timeout;
	do {
		if ((dcn21_read(offset) & bit) != 0)
			return true;
		spin(10);
	} while (system_time() < end);
	return (dcn21_read(offset) & bit) != 0;
}


static status_t
dcn21_run_table(int index, const void* params, size_t bytes)
{
	// The tables may use the rest of their ps_allocation block, so they get
	// a larger zeroed block. atom_execute_table() stops a table that loops
	// for more than ATOM_OP_JMP_TIMEOUT (5) seconds.
	uint32 block[16];
	memset(block, 0, sizeof(block));
	memcpy(block, params, bytes);
	return atom_execute_table(gAtomContext, index, block);
}


static int32
dcn21_transmitter(const encoder_info& encoder)
{
	// link_factory.c translate_encoder_to_transmitter().
	if (!encoder.valid || encoder.isExternal)
		return -1;
	int32 base;
	switch (encoder.objectID) {
		case kUniphy:
			base = 0;
			break;
		case kUniphy1:
			base = 2;
			break;
		case kUniphy2:
			base = 4;
			break;
		default:
			return -1;
	}
	if (encoder.linkEnumeration == 1)
		return base;
	if (encoder.linkEnumeration == 2)
		return base + 1;
	return -1;
}


// Undo, for a step that failed after the OTG started.
static void
dcn21_head_off(const dcn21_head& head)
{
	uint32 dig = head.dig * DCN21_DIG_STRIDE;
	if (head.phyOn) {
		dcn21_dig_transmitter_control_v1_6 tx;
		memset(&tx, 0, sizeof(tx));
		tx.phyid = (uint8)head.dig;
		tx.action = kAtomTransmitterDisable;
		tx.digmode = kAtomDigModeV6Dvi;
		tx.lanenum = 4;
		tx.symclk_10khz = head.timing.pixelKHz / 10;
		tx.hpdsel = (uint8)(head.hpd + 1);
		tx.connobj_id = head.connectorObject;
		status_t status = dcn21_run_table(kTableTransmitterControl, &tx,
			sizeof(tx));
		HEADLOG("transmitter %" B_PRIu32 " off: %s\n", head.dig,
			strerror(status));
	}
	dcn21_update(DCN21_DIG_BE_CNTL + dig, DCN21_DIG_FE_SOURCE_SELECT_MASK, 0);
	if (head.otgStarted) {
		dcn21_update(DCN21_OTG_CONTROL + head.otg * DCN21_OTG_STRIDE,
			DCN21_OTG_MASTER_EN, 0);
	}
	dcn21_update(DCN21_DPG_CONTROL + head.otg * DCN21_OPP_STRIDE,
		DCN21_DPG_EN, 0);
	HEADLOG("otg%" B_PRIu32 " off\n", head.otg);
}


// opp1_program_fmt() with resource_build_bit_depth_reduction_params() for
// 8 bpc RGB and the default dither option (DITHER_OPTION_SPATIAL8), and
// clamping CLAMPING_FULL_RANGE (0, the value a new stream has); then
// opp1_set_dyn_expansion() for a DVI signal (only the reset).
static void
dcn21_program_fmt(uint32 opp)
{
	// opp1_set_truncation(): TRUNCATE_ENABLED 0.
	dcn21_update(DCN21_FMT_BIT_DEPTH_CONTROL + opp, DCN21_FMT_TRUNCATE_EN
		| DCN21_FMT_TRUNCATE_MODE | DCN21_FMT_TRUNCATE_DEPTH_MASK, 0);
	// opp1_set_spatial_dither(): clear, FRAME_RANDOM 0 so the counter
	// fields are 0, seeds 0, then the SPATIAL8 flags.
	dcn21_update(DCN21_FMT_BIT_DEPTH_CONTROL + opp, DCN21_FMT_SPATIAL_DITHER_EN
		| DCN21_FMT_SPATIAL_DITHER_MODE_MASK
		| DCN21_FMT_SPATIAL_DITHER_DEPTH_MASK | DCN21_FMT_TEMPORAL_DITHER_EN
		| DCN21_FMT_HIGHPASS_RANDOM_ENABLE | DCN21_FMT_FRAME_RANDOM_ENABLE
		| DCN21_FMT_RGB_RANDOM_ENABLE, 0);
	dcn21_update(DCN21_FMT_CONTROL + opp,
		DCN21_FMT_DITHER_FRAME_COUNTER_MAX_MASK
		| DCN21_FMT_DITHER_FRAME_COUNTER_BIT_SWAP_MASK, 0);
	dcn21_write(DCN21_FMT_DITHER_RAND_R_SEED + opp, 0);
	dcn21_write(DCN21_FMT_DITHER_RAND_G_SEED + opp, 0);
	dcn21_write(DCN21_FMT_DITHER_RAND_B_SEED + opp, 0);
	dcn21_update(DCN21_FMT_BIT_DEPTH_CONTROL + opp, DCN21_FMT_SPATIAL_DITHER_EN
		| DCN21_FMT_SPATIAL_DITHER_MODE_MASK
		| DCN21_FMT_SPATIAL_DITHER_DEPTH_MASK
		| DCN21_FMT_HIGHPASS_RANDOM_ENABLE | DCN21_FMT_FRAME_RANDOM_ENABLE
		| DCN21_FMT_RGB_RANDOM_ENABLE, DCN21_FMT_SPATIAL_DITHER_EN
		| (1u << DCN21_FMT_SPATIAL_DITHER_DEPTH_SHIFT)
		| DCN21_FMT_HIGHPASS_RANDOM_ENABLE | DCN21_FMT_RGB_RANDOM_ENABLE);
	// opp1_set_clamping(): reset, then CLAMPING_FULL_RANGE.
	dcn21_update(DCN21_FMT_CLAMP_CNTL + opp, DCN21_FMT_CLAMP_DATA_EN
		| DCN21_FMT_CLAMP_COLOR_FORMAT_MASK, 0);
	dcn21_update(DCN21_FMT_CLAMP_CNTL + opp, DCN21_FMT_CLAMP_DATA_EN
		| DCN21_FMT_CLAMP_COLOR_FORMAT_MASK, DCN21_FMT_CLAMP_DATA_EN);
	// opp1_set_pixel_encoding(): RGB.
	dcn21_update(DCN21_FMT_CONTROL + opp, DCN21_FMT_PIXEL_ENCODING_MASK
		| DCN21_FMT_SUBSAMPLING_MODE_MASK | DCN21_FMT_CBCR_BIT_REDUCTION_BYPASS,
		0);
	// opp1_set_dyn_expansion(): reset; DVI is not in the list that enables it.
	dcn21_update(DCN21_FMT_DYNAMIC_EXP_CNTL + opp,
		DCN21_FMT_DYNAMIC_EXP_EN | DCN21_FMT_DYNAMIC_EXP_MODE, 0);
}


// optc1_set_drr() with zero parameters: only optc2_setup_manual_trigger().
// Then optc1_set_static_screen_control(event 0, 2 frames).
static void
dcn21_drr_and_static_screen(uint32 otgIndex)
{
	uint32 otg = otgIndex * DCN21_OTG_STRIDE;
	dcn21_update(DCN21_OTG_V_TOTAL_CONTROL + otg, DCN21_OTG_V_TOTAL_MIN_SEL
		| DCN21_OTG_V_TOTAL_MAX_SEL | DCN21_OTG_FORCE_LOCK_ON_EVENT
		| DCN21_OTG_SET_V_TOTAL_MIN_MASK_MASK, DCN21_OTG_V_TOTAL_MIN_SEL
		| DCN21_OTG_V_TOTAL_MAX_SEL
		| ((1u << 1) << DCN21_OTG_SET_V_TOTAL_MIN_MASK_SHIFT));
	dcn21_write(DCN21_OTG_TRIGA_CNTL + otg,
		(kTrigaSource << DCN21_OTG_TRIGA_SOURCE_SELECT_SHIFT)
		| (otgIndex << DCN21_OTG_TRIGA_SOURCE_PIPE_SELECT_SHIFT)
		| (1u << DCN21_OTG_TRIGA_RISING_EDGE_DETECT_SHIFT)
		| DCN21_OTG_TRIGA_CLEAR);
	dcn21_write(DCN21_OTG_STATIC_SCREEN_CONTROL + otg,
		2u << DCN21_OTG_STATIC_SCREEN_FRAME_COUNT_SHIFT);
}


// dcn20_enable_stream_timing(), for one OTG without ODM. Returns an error
// instead of waiting without a limit.
static status_t
dcn21_enable_stream_timing(dcn21_head& head)
{
	const dcn_timing& t = head.timing;
	uint32 o = head.otg;
	uint32 otg = o * DCN21_OTG_STRIDE;
	uint32 odm = o * DCN21_ODM_STRIDE;
	uint32 opp = o * DCN21_OPP_STRIDE;

	// set_pixel_rate_div: dccg21_funcs has none.

	// optc1_enable_optc_clock(true).
	dcn21_update(DCN21_OPTC_INPUT_CLOCK_CONTROL + odm,
		DCN21_OPTC_INPUT_CLK_EN | DCN21_OPTC_INPUT_CLK_GATE_DIS,
		DCN21_OPTC_INPUT_CLK_EN | DCN21_OPTC_INPUT_CLK_GATE_DIS);
	bool optcOn = dcn21_wait_set(DCN21_OPTC_INPUT_CLOCK_CONTROL + odm,
		DCN21_OPTC_INPUT_CLK_ON, kClockOnTimeout);
	dcn21_update(DCN21_OTG_CLOCK_CONTROL + otg, DCN21_OTG_CLOCK_EN
		| DCN21_OTG_CLOCK_GATE_DIS, DCN21_OTG_CLOCK_EN
		| DCN21_OTG_CLOCK_GATE_DIS);
	bool otgOn = dcn21_wait_set(DCN21_OTG_CLOCK_CONTROL + otg,
		DCN21_OTG_CLOCK_ON, kClockOnTimeout);
	HEADLOG("otg%" B_PRIu32 " clocks optc %s otg %s\n", o,
		optcOn ? "on" : "OFF", otgOn ? "on" : "OFF");
	if (!optcOn || !otgOn)
		return B_TIMED_OUT;

	// program_pix_clk: dcn20_program_pix_clk() -> dce112_program_pix_clk():
	// SET_XTALIN_REF_SRC (0) for a PLL, no spread spectrum change on DCN.
	dcn21_set_pixel_clock_v1_7 clock;
	memset(&clock, 0, sizeof(clock));
	clock.pixclk_100hz = t.pixelKHz * 10;
	clock.pll_id = kAtomComboPhyPll0;
	clock.encoderobjid = head.encoderObject;
	clock.encoder_mode = kAtomEncoderModeDvi;
	clock.crtc_id = (uint8)o;
	status_t status = dcn21_run_table(kTableSetPixelClock, &clock,
		sizeof(clock));
	HEADLOG("setpixelclock: %s, pixel rate 0x%08" B_PRIx32 "\n",
		strerror(status), dcn21_read(DCN21_OTG_PIXEL_RATE_CNTL + o * 4));
	if (status != B_OK)
		return status;
	// dce112_program_pixel_clk_resync(): not HDMI, so deep colour 0. PLL0
	// is PHYPLLA (clk_src_regs[0] in dcn21_resource.c).
	dcn21_update(DCN21_PHYPLLA_PIXCLK_RESYNC_CNTL,
		DCN21_PHYPLL_DCCG_DEEP_COLOR_CNTL_MASK, 0);

	// optc1_program_timing().
	uint32 hBlankStart = t.hTotal - t.hFrontPorch;
	uint32 hBlankEnd = hBlankStart - t.hActive;
	uint32 vBlankStart = t.vTotal - t.vFrontPorch;
	uint32 vBlankEnd = vBlankStart - t.vActive;
	dcn21_write(DCN21_OTG_H_TOTAL + otg, t.hTotal - 1);
	dcn21_update(DCN21_OTG_H_SYNC_A + otg, 0x7fff7fffu, t.hSync << 16);
	dcn21_update(DCN21_OTG_H_BLANK_START_END + otg, 0x7fff7fffu,
		hBlankStart | (hBlankEnd << 16));
	dcn21_update(DCN21_OTG_H_SYNC_A_CNTL + otg, 1, t.hPositive ? 0 : 1);
	dcn21_write(DCN21_OTG_V_TOTAL + otg, t.vTotal - 1);
	// set_vtotal_min_max(v_total, v_total).
	dcn21_write(DCN21_OTG_V_TOTAL_MAX + otg, t.vTotal - 1);
	dcn21_write(DCN21_OTG_V_TOTAL_MIN + otg, t.vTotal - 1);
	dcn21_update(DCN21_OTG_V_SYNC_A + otg, 0x7fff7fffu, t.vSync << 16);
	dcn21_update(DCN21_OTG_V_BLANK_START_END + otg, 0x7fff7fffu,
		vBlankStart | (vBlankEnd << 16));
	dcn21_update(DCN21_OTG_V_SYNC_A_CNTL + otg, 1, t.vPositive ? 0 : 1);
	// DVI: start point 0, field number 0.
	dcn21_update(DCN21_OTG_INTERLACE_CONTROL + otg, 1, 0);
	dcn21_update(DCN21_VTG_CONTROL + o, DCN21_VTG_ENABLE, 0);
	dcn21_update(DCN21_OTG_CONTROL + otg,
		DCN21_OTG_START_POINT_CNTL | DCN21_OTG_FIELD_NUMBER_CNTL, 0);
	// optc1_program_global_sync(). Linux takes these from DML for the HUBP
	// fetch. This pipe has no plane: VSTARTUP is the last blank line before
	// active, VUPDATE offset 0 width 1, VREADY 1 (difference, see the doc).
	uint32 vstartup = t.vTotal - t.vActive - 1;
	if (vstartup > DCN21_OTG_VSTARTUP_MASK)
		vstartup = DCN21_OTG_VSTARTUP_MASK;
	dcn21_write(DCN21_OTG_VSTARTUP_PARAM + otg, vstartup);
	dcn21_write(DCN21_OTG_VUPDATE_PARAM + otg,
		1u << DCN21_OTG_VUPDATE_WIDTH_SHIFT);
	dcn21_write(DCN21_OTG_VREADY_PARAM + otg, 1);
	// optc1_set_vtg_params(program_fp2 true).
	uint32 vInit = t.vTotal - t.vFrontPorch;
	int32 lineStart = (int32)(vInit - t.vActive) - (int32)vstartup + 1;
	uint32 fp2 = lineStart < 0 ? (uint32)-lineStart : 0;
	dcn21_update(DCN21_VTG_CONTROL + o, DCN21_VTG_FP2_MASK
		| DCN21_VTG_VCOUNT_INIT_MASK, fp2 | (vInit << DCN21_VTG_VCOUNT_INIT_SHIFT));
	// OPTC_DATA_FORMAT 0 (RGB), OTG_H_TIMING_DIV_BY2 0.
	dcn21_update(DCN21_OPTC_DATA_FORMAT_CONTROL + odm,
		DCN21_OPTC_DATA_FORMAT_MASK, 0);
	dcn21_update(DCN21_OTG_H_TIMING_CNTL + otg, 1, 0);

	// set_out_rate_control: the DCN 2.0 MPC has none.
	// opp1_pipe_clock_control(true), opp2_program_left_edge_extra_pixel(RGB).
	dcn21_update(DCN21_OPP_PIPE_CONTROL + opp, DCN21_OPP_PIPE_CLOCK_EN,
		DCN21_OPP_PIPE_CLOCK_EN);
	dcn21_update(DCN21_FMT_422_CONTROL + opp,
		DCN21_FMT_LEFT_EDGE_EXTRA_PIXEL_COUNT, 0);

	// dcn20_blank_pixel_data(true) with visual_confirm: DPG colour squares
	// (opp2_set_disp_pattern_generator).
	dcn21_write(DCN21_DPG_DIMENSIONS + opp,
		(t.hActive << DCN21_DPG_WIDTH_SHIFT) | t.vActive);
	dcn21_write(DCN21_DPG_OFFSET_SEGMENT + opp, 0);
	dcn21_update(DCN21_DPG_CONTROL + opp, DCN21_DPG_CONTROL_FIELDS,
		DCN21_DPG_EN | (kDpgBitDepth8 << DCN21_DPG_BIT_DEPTH_SHIFT)
		| (6u << DCN21_DPG_VRES_SHIFT) | (6u << DCN21_DPG_HRES_SHIFT));

	// optc2_enable_crtc().
	dcn21_update(DCN21_OPTC_DATA_SOURCE_SELECT + odm,
		DCN21_OPTC_SEG0_SRC_SEL_MASK | DCN21_OPTC_NUM_OF_INPUT_SEGMENT,
		o << DCN21_OPTC_SEG0_SRC_SEL_SHIFT);
	dcn21_update(DCN21_VTG_CONTROL + o, DCN21_VTG_ENABLE, DCN21_VTG_ENABLE);
	dcn21_update(DCN21_OTG_CONTROL + otg, DCN21_OTG_DISABLE_POINT_MASK
		| DCN21_OTG_MASTER_EN, (3u << DCN21_OTG_DISABLE_POINT_SHIFT)
		| DCN21_OTG_MASTER_EN);
	head.otgStarted = true;

	// dcn20_wait_for_blank_complete(): up to 1000 x 100 us.
	int polls = 0;
	while (polls < kBlankPolls
		&& (dcn21_read(DCN21_DPG_STATUS + opp)
			& DCN21_DPG_DOUBLE_BUFFER_PENDING) != 0) {
		snooze(100);
		polls++;
	}
	if (polls == kBlankPolls) {
		HEADLOG("otg%" B_PRIu32 " dpg still pending after 100 ms\n", o);
		return B_TIMED_OUT;
	}

	dcn21_drr_and_static_screen(o);

	// Not in Linux: check that the OTG counts frames, so a pixel clock that
	// did not arrive stops here and not after the PHY is on.
	uint32 frame0 = dcn21_read(DCN21_OTG_STATUS_FRAME_COUNT + otg)
		& DCN21_OTG_FRAME_COUNT_MASK;
	snooze(100000);
	uint32 frame1 = dcn21_read(DCN21_OTG_STATUS_FRAME_COUNT + otg)
		& DCN21_OTG_FRAME_COUNT_MASK;
	HEADLOG("otg%" B_PRIu32 " control 0x%08" B_PRIx32 " frames %" B_PRIu32
		" -> %" B_PRIu32 " in 100 ms\n", o, dcn21_read(DCN21_OTG_CONTROL + otg),
		frame0, frame1);
	if (frame1 == frame0)
		return B_TIMED_OUT;
	return B_OK;
}


// link_set_dpms_on() for a DVI signal.
static status_t
dcn21_dpms_on(dcn21_head& head)
{
	const dcn_timing& t = head.timing;
	uint32 dig = head.dig * DCN21_DIG_STRIDE;

	// dcn10_link_encoder_setup(DVI).
	dcn21_update(DCN21_DIG_BE_CNTL + dig, DCN21_DIG_MODE_MASK,
		DCN21_DIG_MODE_DVI << DCN21_DIG_MODE_SHIFT);

	// setup_dio_stream_attribute(): enc1_setup_stereo_sync(otg, false), then
	// enc1_stream_encoder_dvi_set_stream_attribute(): VBIOS stream setup
	// (no colour depth set, so bitpercolor 0), then the TMDS fields.
	dcn21_update(DCN21_DIG_FE_CNTL + dig, DCN21_DIG_STEREOSYNC_SELECT_MASK
		| DCN21_DIG_STEREOSYNC_GATE_EN, (head.otg
			<< DCN21_DIG_STEREOSYNC_SELECT_SHIFT) | DCN21_DIG_STEREOSYNC_GATE_EN);
	dcn21_dig_encoder_stream_setup_v1_5 stream;
	memset(&stream, 0, sizeof(stream));
	stream.digid = (uint8)head.dig;
	stream.action = kAtomEncoderCmdStreamSetup;
	stream.digmode = kAtomEncoderModeDvi;
	stream.lanenum = 4;
	stream.pclk_10khz = t.pixelKHz / 10;
	status_t status = dcn21_run_table(kTableDigEncoderControl, &stream,
		sizeof(stream));
	HEADLOG("digxencodercontrol stream setup: %s\n", strerror(status));
	if (status != B_OK)
		return status;
	dcn21_update(DCN21_DIG_FE_CNTL + dig, DCN21_DIG_TMDS_PIXEL_ENCODING
		| DCN21_DIG_TMDS_COLOR_FORMAT_MASK, 0);

	// update_info_frame: none for DVI.

	// enable_link_hdmi(): setup_dio_stream_encoder() connects the back end
	// to this front end (dcn10_link_encoder_connect_dig_be_to_fe); the
	// DCN 2.1 stream encoder has no enable_stream, map_stream_to_link,
	// set_input_mode or enable_fifo.
	dcn21_update(DCN21_DIG_BE_CNTL + dig, DCN21_DIG_FE_SOURCE_SELECT_MASK,
		dcn21_read(DCN21_DIG_BE_CNTL + dig) | ((1u << head.dig)
			<< DCN21_DIG_FE_SOURCE_SELECT_SHIFT));

	// dcn10_link_encoder_enable_tmds_output(): VBIOS transmitter enable.
	dcn21_dig_transmitter_control_v1_6 tx;
	memset(&tx, 0, sizeof(tx));
	tx.phyid = (uint8)head.dig;
	tx.action = kAtomTransmitterEnable;
	tx.digmode = kAtomDigModeV6Dvi;
	tx.lanenum = 4;
	tx.symclk_10khz = t.pixelKHz / 10;
	tx.hpdsel = (uint8)(head.hpd + 1);
	tx.digfe_sel = 0;	// dig_encoder_sel_to_atom() is 0 after DCE 8
	tx.connobj_id = head.connectorObject;
	status = dcn21_run_table(kTableTransmitterControl, &tx, sizeof(tx));
	HEADLOG("dig1transmittercontrol enable: %s\n", strerror(status));
	if (status != B_OK)
		return status;
	head.phyOn = true;

	// dcn20_enable_stream(): setup_stream_encoder() again (the same bit),
	// update_info_frame (none), set_early_control (empty on DCN 2.x).
	// unblank_stream is for DP only.
	return B_OK;
}


status_t
dcn21_head_start(const dcn_connector_state* state, const edid1_info* edid)
{
	if (state == NULL || edid == NULL
		|| state->connector >= ATOM_MAX_SUPPORTED_DEVICE)
		return B_BAD_VALUE;
	connector_info* connector = gConnector[state->connector];
	HEADLOG("connector %" B_PRIu32 " start (dcn_hdmi_modeset true)\n",
		state->connector);

	dcn21_head head;
	memset(&head, 0, sizeof(head));
	if (!dcn_timing_from_edid(edid, kDviSingleLinkKHz, &head.timing)) {
		HEADLOG("no EDID timing for DVI single link, not started\n");
		return B_NOT_SUPPORTED;
	}
	int32 transmitter = dcn21_transmitter(connector->encoder);
	if (transmitter < 0 || (uint32)transmitter >= DCN21_DIG_COUNT) {
		HEADLOG("encoder 0x%x enum %" B_PRIu32 " has no DCN 2.1 DIG, not"
			" started\n", (unsigned)connector->encoder.objectID,
			connector->encoder.linkEnumeration);
		return B_NOT_SUPPORTED;
	}
	if (state->hpd < 0) {
		HEADLOG("no HPD pin, not started\n");
		return B_NOT_SUPPORTED;
	}
	head.dig = (uint32)transmitter;
	head.hpd = state->hpd;
	head.encoderObject = (uint8)connector->encoder.objectID;
	head.connectorObject = (uint8)connector->objectID;

	// command_table2.c picks these implementations by revision.
	uint8 frev;
	uint8 crev;
	if (atom_parse_cmd_header(gAtomContext, kTableSetPixelClock, &frev, &crev)
			!= B_OK || crev != 7
		|| atom_parse_cmd_header(gAtomContext, kTableDigEncoderControl, &frev,
			&crev) != B_OK || crev != 5
		|| atom_parse_cmd_header(gAtomContext, kTableTransmitterControl, &frev,
			&crev) != B_OK || crev != 6) {
		HEADLOG("VBIOS tables are not setpixelclock 7, digxencodercontrol 5"
			" and dig1transmittercontrol 6, not started\n");
		return B_NOT_SUPPORTED;
	}

	// The DIG of this connector must be free: no front end routed to it.
	uint32 beCntl = dcn21_read(DCN21_DIG_BE_CNTL + head.dig * DCN21_DIG_STRIDE);
	if ((beCntl & DCN21_DIG_FE_SOURCE_SELECT_MASK) != 0) {
		HEADLOG("dig%" B_PRIu32 " be 0x%08" B_PRIx32 " is in use, not started\n",
			head.dig, beCntl);
		return B_BUSY;
	}

	// The first OTG the firmware left off; the panel's OTG is on.
	bool found = false;
	for (uint32 i = 0; i < DCN21_OTG_COUNT; i++) {
		uint32 control = dcn21_read(DCN21_OTG_CONTROL + i * DCN21_OTG_STRIDE);
		if ((control & (DCN21_OTG_MASTER_EN | DCN21_OTG_CURRENT_MASTER_EN))
				== 0) {
			head.otg = i;
			found = true;
			break;
		}
	}
	if (!found) {
		HEADLOG("no free OTG, not started\n");
		return B_BUSY;
	}

	HEADLOG("connector %" B_PRIu32 " otg%" B_PRIu32 " dig%" B_PRIu32 " phy %"
		B_PRIu32 " pll combophy0 hpd%" B_PRId32 " mode %" B_PRIu32 "x%" B_PRIu32
		" total %" B_PRIu32 "x%" B_PRIu32 " %" B_PRIu32 " kHz dvi\n",
		state->connector, head.otg, head.dig, head.dig, head.hpd + 1,
		head.timing.hActive, head.timing.vActive, head.timing.hTotal,
		head.timing.vTotal, head.timing.pixelKHz);

	uint32 opp = head.otg * DCN21_OPP_STRIDE;
	uint32 otg = head.otg * DCN21_OTG_STRIDE;

	// dce110_apply_single_controller_ctx_to_hw(), in its order.
	// disable_stream_gating: DSC only; no DSC on this stream.
	dcn21_program_fmt(opp);
	status_t status = dcn21_enable_stream_timing(head);
	if (status != B_OK) {
		dcn21_head_off(head);
		return status;
	}
	// dcn20_setup_vupdate_interrupt(): start line from
	// dcn10_get_vupdate_offset_from_vsync().
	int32 asicBlankEnd = (int32)(head.timing.vTotal
		- (head.timing.vActive + head.timing.vFrontPorch));
	int32 vstartup = (int32)(dcn21_read(DCN21_OTG_VSTARTUP_PARAM + otg)
		& DCN21_OTG_VSTARTUP_MASK);
	int32 startLine = asicBlankEnd - vstartup + 1;
	if (startLine < 0)
		startLine = 0;
	dcn21_write(DCN21_OTG_VERTICAL_INTERRUPT2_POSITION + otg,
		(uint32)startLine & DCN21_OTG_FIELD15_MASK);
	// set_drr and set_static_screen_control again, as the caller does.
	dcn21_drr_and_static_screen(head.otg);
	// enc1_dig_connect_to_otg().
	dcn21_update(DCN21_DIG_FE_CNTL + head.dig * DCN21_DIG_STRIDE,
		DCN21_DIG_SOURCE_SELECT_MASK, head.otg);

	status = dcn21_dpms_on(head);
	if (status != B_OK) {
		dcn21_head_off(head);
		return status;
	}

	snooze(100000);
	uint32 dig = head.dig * DCN21_DIG_STRIDE;
	HEADLOG("head on: otg%" B_PRIu32 " control 0x%08" B_PRIx32 " frame %"
		B_PRIu32 " pixel rate 0x%08" B_PRIx32 " dig%" B_PRIu32 " fe 0x%08"
		B_PRIx32 " be 0x%08" B_PRIx32 " dpg 0x%08" B_PRIx32 "\n", head.otg,
		dcn21_read(DCN21_OTG_CONTROL + otg),
		dcn21_read(DCN21_OTG_STATUS_FRAME_COUNT + otg)
			& DCN21_OTG_FRAME_COUNT_MASK,
		dcn21_read(DCN21_OTG_PIXEL_RATE_CNTL + head.otg * 4), head.dig,
		dcn21_read(DCN21_DIG_FE_CNTL + dig), dcn21_read(DCN21_DIG_BE_CNTL + dig),
		dcn21_read(DCN21_DPG_CONTROL + opp));
	return B_OK;
}
