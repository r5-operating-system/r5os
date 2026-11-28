/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 *
 * DCN 2.1: other ways to reach the display registers, for diagnosis
 * (docs/DISPLAY-DCN.md, "Register access"). One path per boot, chosen by
 * `dcn_probe_path <name>` in the radeon_hd settings (`boot-next dcnpath`).
 * None runs by default. The caller arms the crash marker (fsync) with the
 * path's name before calling, so a path that hangs the machine is named by
 * the next start and is not run again with the same settings.
 *
 * Stick 3 (28 Nov 2026) froze hard with the earlier kernel version of these
 * probes and left no log. That version also read "DMU segment 0x12/0xC0 +
 * DC_PINSTRAPS"; those dwords (0x2892, 0x2940) lie in GFX register space
 * (GC_BASE segment 0 is 0x2000), and GFX registers may hang the bus while
 * GFX is power-gated. Those reads are gone; they were never display
 * registers.
 *
 * Paths (Linux v6.12, MIT, AMD):
 * - "discovery": the IP discovery table at RCC_CONFIG_MEMSIZE << 20 - 64 KiB,
 *   read through MM_INDEX/MM_INDEX_HI/MM_DATA with bit 31 set (VRAM), as
 *   amdgpu_discovery_read_binary_from_mem() and amdgpu_device_mm_access()
 *   do on Renoir at init; layout include/discovery.h; DMU_HWID 271
 *   (soc15_hw_ip.h). Logs the DMU bases; reads no register at them.
 * - "mm_index": DC_PINSTRAPS through MM_INDEX/MM_DATA with bit 31 clear
 *   (register space).
 * - "pcie_index2": DC_PINSTRAPS through PCIE_INDEX2/PCIE_DATA2
 *   (amdgpu_device_indirect_rreg, nbio_v7_0; nbio_7_0_offset.h 0xe/0xf),
 *   with the MMIO byte offset as the address, as pcie_rreg(reg * 4) gets it.
 * - "smu_status": read-only SMU mailbox and SMUIO power-state registers
 *   outside DCN (MP1_BASE seg 0 0x16000: driver mailbox C2PMSG_66/82/90 and
 *   VBIOS/display mailbox C2PMSG_67/83/91, rn_clk_mgr_vbios_smu.c;
 *   SMUIO_BASE seg 0 0x16800 mmSMUIO_GFX_MISC_CNTL 0xc8, smu_v12_0.c
 *   smu_v12_0_get_gfxoff_status(); SMUIO_BASE seg 1 0x16a00
 *   mmPWR_MISC_CNTL_STATUS 0x183, smuio_12_0_0_offset.h).
 * - "clk_status": read-only display clock registers outside DCN (CLK_BASE
 *   seg 1 0x16e00, clk_10_0_2_offset.h, read by rn_clk_mgr.c
 *   get_vco_frequency_from_reg() and rn_dump_clk_registers_internal():
 *   CLK1_CLK_PLL_REQ, CLK1_CLK0..3_CURRENT_CNT, BYPASS_CNTL, DS_CNTL,
 *   ALLOW_DS). Stops immediately if the first read returns 0xffffffff.
 * - "dccg_seg1": read-only read of one real DMU segment-1 register,
 *   mmDENTIST_DISPCLK_CNTL ((0x00c0 + 0x0064) * 4 = 0x490, dcn_2_1_0_offset.h
 *   BASE_IDX 1), to tell whether DMU segment 1 answers while segment 2 does not.
 * - "bus_probe": read-only reads of NBIF0 (mmRCC_DEV0_EPF0_STRAP0 0x0d2f,
 *   mmBIF_FB_EN 0x0e1f; nbio_7_0_offset.h BASE_IDX 2, NBIF0_BASE seg 2
 *   0x0d20) and MMHUB (mmMC_VM_FB_LOCATION_BASE/TOP 0x1a82c/0x1a82d;
 *   mmhub_1_0_offset.h BASE_IDX 0, MMHUB_BASE seg 0 0x1a000), to see whether
 *   MMHUB (the fabric behind which DMU sits) answers on BAR5.
 * - "vbios_smu_wake": sends VBIOSSMC_MSG_GetSmuVersion (0x2) and
 *   VBIOSSMC_MSG_SetDisplayCount (0xc, param 1 = DCN_PWR_STATE_MISSION_MODE)
 *   on the already-proven VBIOS/display SMU mailbox (MP1 C2PMSG_67/83/91,
 *   rn_clk_mgr_vbios_smu.c:134, 233-246), then re-reads DC_PINSTRAPS.
 * - "smu_disable_gfxoff": sends PPSMC_MSG_GetSmuVersion (0x2) and
 *   PPSMC_MSG_DisableGfxOff (0x8) on the already-proven driver SMU mailbox
 *   (MP1 C2PMSG_66/82/90, smu_v12_0.c:158-180), polls SMUIO_GFX_MISC_CNTL
 *   for PWR_GFXOFF_STATUS == 2 (up to 500 ms), then re-reads DC_PINSTRAPS.
 * Only the bus interface's index registers are written in discovery/mm_index/
 * pcie_index2; each is put back. smu_status, clk_status, dccg_seg1 and
 * bus_probe perform zero writes. vbios_smu_wake and smu_disable_gfxoff write
 * only to the SMU mailboxes outside DMU.
 */


#include "dcn_display.h"

#include <stdio.h>
#include <string.h>

#include <Debug.h>
#include <OS.h>

#include "accelerant.h"
#include "accelerant_protos.h"
#include "dcn21_regs.h"


#define PATHLOG(x...) _sPrintf("radeon_hd: dcn path: " x)


// Bus interface, absolute dwords (nbio_7_0_offset.h; NBIO segment 0 is 0).
static const uint32 kMmIndex = 0x0;
static const uint32 kMmData = 0x1;
static const uint32 kMmIndexHi = 0x6;
static const uint32 kPcieIndex2 = 0xe;
static const uint32 kPcieData2 = 0xf;
static const uint32 kRccConfigMemsize = 0xde3;	// amdgpu_discovery.c

// discovery.h, amdgpu_discovery.h.
static const uint32 kBinarySignature = 0x28211407;
static const uint32 kTableSignature = 0x53445049;
static const uint64 kDiscoveryTmrOffset = 64 * 1024;
static const uint32 kDiscoveryTmrSize = 10 * 1024;
static const uint16 kDmuHwId = 271;


static inline uint32
rd(uint32 dword)
{
	return Read32(OUT, dword * 4);
}


static inline void
wr(uint32 dword, uint32 value)
{
	Write32(OUT, dword * 4, value);
}


// amdgpu_device_mm_access(): one dword through MM_INDEX/MM_INDEX_HI/MM_DATA.
static uint32
mm_read(uint64 address, bool vram)
{
	wr(kMmIndex, ((uint32)address & 0x7fffffff) | (vram ? 0x80000000u : 0));
	wr(kMmIndexHi, (uint32)(address >> 31));
	return rd(kMmData);
}


static bool
table_read(uint64 base, uint32 offset, uint32 bytes, void* out)
{
	if (offset + bytes > kDiscoveryTmrSize)
		return false;
	uint8* dst = (uint8*)out;
	for (uint32 at = offset & ~3u; at < offset + bytes; at += 4) {
		uint32 word = mm_read(base + at, true);
		for (uint32 b = 0; b < 4; b++) {
			uint32 pos = at + b;
			if (pos >= offset && pos < offset + bytes)
				dst[pos - offset] = (uint8)(word >> (8 * b));
		}
	}
	return true;
}


static status_t
path_discovery()
{
	uint32 memsize = rd(kRccConfigMemsize);
	PATHLOG("discovery: RCC_CONFIG_MEMSIZE 0x%08" B_PRIx32 "\n", memsize);
	if (memsize == 0 || memsize == 0xffffffff || memsize > (1u << 20))
		return B_BAD_DATA;
	uint64 base = ((uint64)memsize << 20) - kDiscoveryTmrOffset;
	PATHLOG("discovery: reading vram 0x%" B_PRIx64 " through MM_INDEX\n",
		base);

	uint32 savedIndex = rd(kMmIndex);
	uint32 savedIndexHi = rd(kMmIndexHi);
	status_t status = B_OK;
	uint32 signature = 0;
	table_read(base, 0, 4, &signature);
	if (signature != kBinarySignature) {
		PATHLOG("discovery: signature 0x%08" B_PRIx32 ", not 0x%08" B_PRIx32
			"\n", signature, kBinarySignature);
		status = B_BAD_DATA;
	}
	uint16 tableOffset = 0;
	uint32 tableSignature = 0;
	uint16 version = 0;
	if (status == B_OK) {
		table_read(base, 12, 2, &tableOffset);
		table_read(base, tableOffset, 4, &tableSignature);
		table_read(base, tableOffset + 4, 2, &version);
		if (tableSignature != kTableSignature) {
			PATHLOG("discovery: ip table at 0x%x signature 0x%08" B_PRIx32
				"\n", tableOffset, tableSignature);
			status = B_BAD_DATA;
		}
	}
	if (status == B_OK) {
		uint16 dieOffset = 0;
		uint8 flags = 0;
		uint16 numIps = 0;
		table_read(base, tableOffset + 16, 2, &dieOffset);
		table_read(base, tableOffset + 14 + 16 * 4, 1, &flags);
		table_read(base, dieOffset + 2, 2, &numIps);
		bool bases64 = version >= 4 && (flags & 1) != 0;
		uint32 at = dieOffset + 4;
		bool found = false;
		for (uint16 i = 0; i < numIps && i < 256 && !found; i++) {
			uint8 entry[8];
			if (!table_read(base, at, 8, entry))
				break;
			uint16 hwId = (uint16)(entry[0] | (entry[1] << 8));
			uint8 numBases = entry[3];
			uint32 baseBytes = bases64 ? 8 : 4;
			if (hwId == kDmuHwId) {
				found = true;
				PATHLOG("discovery v%u: DMU %u.%u.%u, %u bases\n", version,
					entry[4], entry[5], entry[6], numBases);
				for (uint8 b = 0; b < numBases && b < 6; b++) {
					uint32 value = 0;
					table_read(base, at + 8 + b * baseBytes, 4, &value);
					uint32 expected = b == 1 ? DCN21_SEG1 : b == 2 ? DCN21_SEG2
						: 0xffffffff;
					PATHLOG("discovery: DMU base %u 0x%08" B_PRIx32 "%s\n", b,
						value, value == expected ? " (= renoir_ip_offset.h)" : "");
				}
			}
			at += 8 + numBases * baseBytes;
		}
		if (!found)
			PATHLOG("discovery: no DMU among %u ips\n", numIps);
	}
	wr(kMmIndexHi, savedIndexHi);
	wr(kMmIndex, savedIndex);
	return status;
}


static status_t
path_mm_index()
{
	uint64 address = (uint64)(DCN21_SEG2 + DCN21_DC_PINSTRAPS) * 4;
	PATHLOG("mm_index: reading DC_PINSTRAPS at 0x%" B_PRIx64 " through"
		" MM_INDEX\n", address);
	uint32 savedIndex = rd(kMmIndex);
	uint32 savedIndexHi = rd(kMmIndexHi);
	uint32 value = mm_read(address, false);
	wr(kMmIndexHi, savedIndexHi);
	wr(kMmIndex, savedIndex);
	PATHLOG("mm_index: DC_PINSTRAPS 0x%08" B_PRIx32 "\n", value);
	return B_OK;
}


static status_t
path_pcie_index2()
{
	uint32 address = (DCN21_SEG2 + DCN21_DC_PINSTRAPS) * 4;
	PATHLOG("pcie_index2: reading DC_PINSTRAPS at 0x%" B_PRIx32 " through"
		" PCIE_INDEX2\n", address);
	uint32 saved = rd(kPcieIndex2);
	wr(kPcieIndex2, address);
	rd(kPcieIndex2);
	uint32 value = rd(kPcieData2);
	wr(kPcieIndex2, saved);
	PATHLOG("pcie_index2: DC_PINSTRAPS 0x%08" B_PRIx32 "\n", value);
	return B_OK;
}


// Read-only SMU & SMUIO status outside DCN (Linux v6.12, MIT, AMD):
// - renoir_ip_offset.h: MP1_BASE seg 0 = 0x16000, SMUIO_BASE seg 0 = 0x16800,
//   SMUIO_BASE seg 1 = 0x16a00.
// - mp_12_0_0_offset.h: C2PMSG_66/82/90 (driver mailbox) = 0x282/0x292/0x29a;
//   C2PMSG_67/83/91 (VBIOS/display mailbox, rn_clk_mgr_vbios_smu.c) =
//   0x283/0x293/0x29b.
// - smuio_12_0_0_offset.h & smu_v12_0.c:151: mmSMUIO_GFX_MISC_CNTL = 0xc8
//   (BASE_IDX 0), mmPWR_MISC_CNTL_STATUS = 0x183 (BASE_IDX 1).
static status_t
path_smu_status()
{
	static const uint32 kMp1Seg0 = 0x16000u;
	static const uint32 kSmuioSeg0 = 0x16800u;
	static const uint32 kSmuioSeg1 = 0x16a00u;

	uint32 drvMsg = rd(kMp1Seg0 + 0x282u);
	uint32 drvParam = rd(kMp1Seg0 + 0x292u);
	uint32 drvResp = rd(kMp1Seg0 + 0x29au);
	PATHLOG("smu_status: driver mailbox msg66 0x%08" B_PRIx32 " param82 0x%08"
		B_PRIx32 " resp90 0x%08" B_PRIx32 "\n", drvMsg, drvParam, drvResp);

	uint32 vbiosMsg = rd(kMp1Seg0 + 0x283u);
	uint32 vbiosParam = rd(kMp1Seg0 + 0x293u);
	uint32 vbiosResp = rd(kMp1Seg0 + 0x29bu);
	PATHLOG("smu_status: vbios mailbox msg67 0x%08" B_PRIx32 " param83 0x%08"
		B_PRIx32 " resp91 0x%08" B_PRIx32 "\n", vbiosMsg, vbiosParam,
		vbiosResp);

	uint32 gfxMisc = rd(kSmuioSeg0 + 0x00c8u);
	if (gfxMisc == 0xffffffffu) {
		PATHLOG("smu_status: SMUIO_GFX_MISC_CNTL 0xffffffff (does not"
			" answer)\n");
		return B_OK;
	}
	uint32 gfxoff = (gfxMisc >> 1) & 3u;
	const char* gfxoffName = gfxoff == 0 ? "GFXOFF"
		: gfxoff == 1 ? "exiting"
		: gfxoff == 2 ? "ON" : "entering";
	uint32 pwrMisc = rd(kSmuioSeg1 + 0x0183u);
	PATHLOG("smu_status: SMUIO_GFX_MISC_CNTL 0x%08" B_PRIx32 " (gfxoff %"
		B_PRIu32 " %s) PWR_MISC_CNTL_STATUS 0x%08" B_PRIx32 "\n", gfxMisc,
		gfxoff, gfxoffName, pwrMisc);
	return B_OK;
}


// Read-only display clock status outside DCN (Linux v6.12, MIT, AMD):
// - renoir_ip_offset.h: CLK_BASE seg 1 = 0x16e00.
// - clk_10_0_2_offset.h & rn_clk_mgr.c (get_vco_frequency_from_reg,
//   rn_dump_clk_registers_internal): CLK1_CLK_PLL_REQ 0x000f,
//   CLK1_CLK0..3_CURRENT_CNT 0x008a..0x008d (dispclk, dppclk, dprefclk,
//   dcfclk in 100 kHz units), CLK1_CLK0_BYPASS_CNTL 0x0049,
//   CLK1_CLK3_BYPASS_CNTL 0x0067, CLK1_CLK3_DS_CNTL 0x0060,
//   CLK1_CLK3_ALLOW_DS 0x0061.
static status_t
path_clk_status()
{
	static const uint32 kClkSeg1 = 0x16e00u;

	uint32 pllReq = rd(kClkSeg1 + 0x000fu);
	if (pllReq == 0xffffffffu) {
		PATHLOG("clk_status: CLK1_CLK_PLL_REQ 0xffffffff (CLK1 does not"
			" answer)\n");
		return B_OK;
	}
	uint32 dispCnt = rd(kClkSeg1 + 0x008au);
	uint32 dppCnt = rd(kClkSeg1 + 0x008bu);
	uint32 dprefCnt = rd(kClkSeg1 + 0x008cu);
	uint32 dcfCnt = rd(kClkSeg1 + 0x008du);
	uint32 dispBypass = rd(kClkSeg1 + 0x0049u);
	uint32 dcfBypass = rd(kClkSeg1 + 0x0067u);
	uint32 dcfDsCntl = rd(kClkSeg1 + 0x0060u);
	uint32 dcfAllowDs = rd(kClkSeg1 + 0x0061u);

	PATHLOG("clk_status: PLL_REQ 0x%08" B_PRIx32 " dispclk %" B_PRIu32
		" MHz (cnt 0x%" B_PRIx32 " bypass 0x%" B_PRIx32 ") dppclk %" B_PRIu32
		" MHz dprefclk %" B_PRIu32 " MHz\n", pllReq, dispCnt / 10, dispCnt,
		dispBypass & 7u, dppCnt / 10, dprefCnt / 10);
	PATHLOG("clk_status: dcfclk %" B_PRIu32 " MHz (cnt 0x%" B_PRIx32
		" bypass 0x%" B_PRIx32 ") ds_cntl 0x%08" B_PRIx32 " allow_ds 0x%08"
		B_PRIx32 "\n", dcfCnt / 10, dcfCnt, dcfBypass & 7u, dcfDsCntl,
		dcfAllowDs);
	return B_OK;
}


// Read-only probe of one real DMU segment-1 register:
// mmDENTIST_DISPCLK_CNTL ((0x00c0 + 0x0064) * 4 = 0x490, dcn_2_1_0_offset.h
// BASE_IDX 1).
static status_t
path_dccg_seg1()
{
	uint32 byte = DCN21_SEG1_BYTES(DCN21_DENTIST_DISPCLK_CNTL);
	PATHLOG("dccg_seg1: reading DENTIST_DISPCLK_CNTL at byte 0x%" B_PRIx32
		"\n", byte);
	uint32 value = Read32(OUT, byte);
	PATHLOG("dccg_seg1: DENTIST_DISPCLK_CNTL 0x%08" B_PRIx32 "\n", value);
	return B_OK;
}


// Read-only NBIO + MMHUB probe outside DMU (Linux v6.12, MIT, AMD):
// - renoir_ip_offset.h: NBIF0_BASE seg 2 = 0x0d20, MMHUB_BASE seg 0 = 0x1a000.
// - nbio_7_0_offset.h: mmRCC_DEV0_EPF0_STRAP0 = 0x000f (BASE_IDX 2),
//   mmBIF_FB_EN = 0x00ff (BASE_IDX 2).
// - mmhub_1_0_offset.h: mmMC_VM_FB_LOCATION_BASE = 0x082c (BASE_IDX 0),
//   mmMC_VM_FB_LOCATION_TOP = 0x082d (BASE_IDX 0).
static status_t
path_bus_probe()
{
	static const uint32 kNbif0Seg2 = 0x00000d20u;
	static const uint32 kMmhubSeg0 = 0x0001a000u;

	uint32 strap0 = rd(kNbif0Seg2 + 0x000fu);
	uint32 bifFbEn = rd(kNbif0Seg2 + 0x00ffu);
	PATHLOG("bus_probe: NBIF0 EPF0_STRAP0 0x%08" B_PRIx32 " BIF_FB_EN 0x%08"
		B_PRIx32 "\n", strap0, bifFbEn);

	uint32 fbBase = rd(kMmhubSeg0 + 0x082cu);
	if (fbBase == 0xffffffffu) {
		PATHLOG("bus_probe: MMHUB FB_LOCATION_BASE 0xffffffff (MMHUB does not"
			" answer)\n");
		return B_OK;
	}
	uint32 fbTop = rd(kMmhubSeg0 + 0x082du);
	PATHLOG("bus_probe: MMHUB FB_LOCATION_BASE 0x%08" B_PRIx32 " TOP 0x%08"
		B_PRIx32 "\n", fbBase, fbTop);
	return B_OK;
}


// Bounded SMU mailbox helper for MP1 seg 0 (Linux v6.12, MIT, AMD:
// rn_clk_mgr_vbios_smu.c:80-132, smu_cmn.c:109-123, 407-468).
// Wait limit: 200000 * 10 us = 2 s, matching rn_clk_mgr_vbios_smu.c:105.
static uint32
smu_mailbox_poll(uint32 respDword)
{
	uint32 val = 0;
	for (uint32 i = 0; i < 200000; i++) {
		val = rd(respDword);
		if (val != 0)
			break;
		snooze(10);
	}
	return val;
}


static uint32
smu_mailbox_send(uint32 msgDword, uint32 paramDword, uint32 respDword,
	uint32 msgId, uint32 param, uint32* outParam)
{
	uint32 before = smu_mailbox_poll(respDword);
	if (before == 0 || before == 0xffffffffu)
		return before;
	wr(respDword, 0);
	wr(paramDword, param);
	wr(msgDword, msgId);
	uint32 after = smu_mailbox_poll(respDword);
	if (outParam != NULL)
		*outParam = rd(paramDword);
	return after;
}


// Send VBIOSSMC_MSG_GetSmuVersion (0x2) and VBIOSSMC_MSG_SetDisplayCount (0xc,
// param 1 = DCN_PWR_STATE_MISSION_MODE) on the VBIOS/display SMU mailbox
// (rn_clk_mgr_vbios_smu.c:134, 233-246), then re-read DC_PINSTRAPS.
static status_t
path_vbios_smu_wake()
{
	static const uint32 kMp1Seg0 = 0x16000u;
	static const uint32 kMsg67 = kMp1Seg0 + 0x283u;
	static const uint32 kParam83 = kMp1Seg0 + 0x293u;
	static const uint32 kResp91 = kMp1Seg0 + 0x29bu;

	uint32 smuVer = 0;
	uint32 resp = smu_mailbox_send(kMsg67, kParam83, kResp91, 0x2u, 0u,
		&smuVer);
	PATHLOG("vbios_smu_wake: GetSmuVersion resp 0x%08" B_PRIx32 " ver 0x%08"
		B_PRIx32 "\n", resp, smuVer);
	if (resp != 1u)
		return B_IO_ERROR;

	uint32 outParam = 0;
	resp = smu_mailbox_send(kMsg67, kParam83, kResp91, 0xcu, 1u, &outParam);
	PATHLOG("vbios_smu_wake: SetDisplayCount(1) resp 0x%08" B_PRIx32
		" out 0x%08" B_PRIx32 "\n", resp, outParam);
	snooze(1000);

	uint32 straps = Read32(OUT, DCN21_BYTES(DCN21_DC_PINSTRAPS));
	if (straps == 0xffffffffu) {
		PATHLOG("vbios_smu_wake: after wake DC_PINSTRAPS 0xffffffff\n");
		return B_OK;
	}
	uint32 dispclk = Read32(OUT, DCN21_SEG1_BYTES(DCN21_DENTIST_DISPCLK_CNTL));
	PATHLOG("vbios_smu_wake: after wake DC_PINSTRAPS 0x%08" B_PRIx32
		" DENTIST_DISPCLK_CNTL 0x%08" B_PRIx32 "\n", straps, dispclk);
	return B_OK;
}


// Send PPSMC_MSG_GetSmuVersion (0x2) and PPSMC_MSG_DisableGfxOff (0x8) on the
// driver SMU mailbox (smu_v12_0.c:158-180), poll SMUIO_GFX_MISC_CNTL for
// PWR_GFXOFF_STATUS == 2 (up to 500 ms), then re-read DC_PINSTRAPS.
static status_t
path_smu_disable_gfxoff()
{
	static const uint32 kMp1Seg0 = 0x16000u;
	static const uint32 kSmuioSeg0 = 0x16800u;
	static const uint32 kMsg66 = kMp1Seg0 + 0x282u;
	static const uint32 kParam82 = kMp1Seg0 + 0x292u;
	static const uint32 kResp90 = kMp1Seg0 + 0x29au;

	uint32 smuVer = 0;
	uint32 resp = smu_mailbox_send(kMsg66, kParam82, kResp90, 0x2u, 0u,
		&smuVer);
	PATHLOG("smu_disable_gfxoff: GetSmuVersion resp 0x%08" B_PRIx32
		" ver 0x%08" B_PRIx32 "\n", resp, smuVer);
	if (resp != 1u)
		return B_IO_ERROR;

	uint32 outParam = 0;
	resp = smu_mailbox_send(kMsg66, kParam82, kResp90, 0x8u, 0u, &outParam);
	uint32 gfxMisc = 0;
	uint32 gfxoff = 0;
	for (uint32 ms = 0; ms < 500; ms++) {
		gfxMisc = rd(kSmuioSeg0 + 0x00c8u);
		gfxoff = (gfxMisc >> 1) & 3u;
		if (gfxoff == 2u)
			break;
		snooze(1000);
	}
	PATHLOG("smu_disable_gfxoff: DisableGfxOff resp 0x%08" B_PRIx32
		" SMUIO_GFX_MISC_CNTL 0x%08" B_PRIx32 " (gfxoff %" B_PRIu32 ")\n",
		resp, gfxMisc, gfxoff);

	uint32 straps = Read32(OUT, DCN21_BYTES(DCN21_DC_PINSTRAPS));
	if (straps == 0xffffffffu) {
		PATHLOG("smu_disable_gfxoff: after DisableGfxOff DC_PINSTRAPS"
			" 0xffffffff\n");
		return B_OK;
	}
	uint32 dispclk = Read32(OUT, DCN21_SEG1_BYTES(DCN21_DENTIST_DISPCLK_CNTL));
	PATHLOG("smu_disable_gfxoff: after DisableGfxOff DC_PINSTRAPS 0x%08"
		B_PRIx32 " DENTIST_DISPCLK_CNTL 0x%08" B_PRIx32 "\n", straps, dispclk);
	return B_OK;
}


status_t
dcn21_probe_path(const char* name)
{
	bigtime_t start = system_time();
	status_t status;
	if (strcmp(name, "discovery") == 0)
		status = path_discovery();
	else if (strcmp(name, "mm_index") == 0)
		status = path_mm_index();
	else if (strcmp(name, "pcie_index2") == 0)
		status = path_pcie_index2();
	else if (strcmp(name, "smu_status") == 0)
		status = path_smu_status();
	else if (strcmp(name, "clk_status") == 0)
		status = path_clk_status();
	else if (strcmp(name, "dccg_seg1") == 0)
		status = path_dccg_seg1();
	else if (strcmp(name, "bus_probe") == 0)
		status = path_bus_probe();
	else if (strcmp(name, "vbios_smu_wake") == 0)
		status = path_vbios_smu_wake();
	else if (strcmp(name, "smu_disable_gfxoff") == 0)
		status = path_smu_disable_gfxoff();
	else {
		PATHLOG("unknown path \"%s\" (discovery, mm_index, pcie_index2,"
			" smu_status, clk_status, dccg_seg1, bus_probe, vbios_smu_wake,"
			" smu_disable_gfxoff)\n", name);
		return B_BAD_VALUE;
	}
	PATHLOG("%s: done in %" B_PRId64 " us\n", name, system_time() - start);
	return status;
}
