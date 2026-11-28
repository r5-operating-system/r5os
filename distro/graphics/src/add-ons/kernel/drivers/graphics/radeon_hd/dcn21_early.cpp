/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * DCN 2.1: is the display block readable? (docs/DISPLAY-DCN.md, "Register
 * access"). Reads only; nothing is written. radeon_hd_init() calls it before
 * and after the GPU start (which sends the SMU mode-2 reset on Renoir), so
 * one boot shows whether the display registers answer, and from when on
 * they stop.
 *
 * Offsets (Linux v6.12, MIT, AMD): DMU_BASE segment 2 0x34C0
 * (renoir_ip_offset.h); mmDC_PINSTRAPS 0x2880, mmOTG0_OTG_CONTROL 0x1b41
 * with OTG stride 0x80, mmOTG0_OTG_H_TOTAL 0x1b2a, mmDMCUB_CNTL 0x32a0, all
 * BASE_IDX 2 (dcn_2_1_0_offset.h). DC_PINSTRAPS is the first DCN register
 * Linux reads (resource/dcn21/dcn21_resource.c read_dce_straps()).
 *
 * Only these direct BAR5 reads, which stick 2 showed to be safe. The other
 * ways in (indirect, discovery table) froze stick 3 when they ran here,
 * where no crash marker can record them; they live in the accelerant now,
 * one per boot behind a setting (dcn21_paths.cpp).
 */


#include "radeon_hd_private.h"

#include <KernelExport.h>


static const uint32 kSeg2 = 0x34c0;
static const uint32 kPinstraps = 0x2880;
static const uint32 kOtg0Control = 0x1b41;
static const uint32 kOtg0HTotal = 0x1b2a;
static const uint32 kOtgStride = 0x80;
static const uint32 kDmcubCntl = 0x32a0;


static uint32
dcn21_early_read(radeon_info& info, uint32 offset, uint64 mmioBytes)
{
	uint64 byte = (uint64)(kSeg2 + offset) * 4;
	if (byte + 4 > mmioBytes)
		return 0xffffffff;
	return *(volatile uint32*)(info.registers + byte);
}


void
r5_dcn21_early_log(radeon_info& info, const char* stage)
{
	if (info.displayMajor != 2 || info.displayMinor != 1
		|| info.registers == 0 || info.registers_area < 0)
		return;
	area_info area;
	if (get_area_info(info.registers_area, &area) != B_OK)
		return;
	uint64 bytes = area.size;
	bigtime_t start = system_time();
	dprintf("radeon_hd: dcn early: %s: pinstraps 0x%08" B_PRIx32
		" dmcub 0x%08" B_PRIx32 " otg0 0x%08" B_PRIx32 " htotal 0x%08"
		B_PRIx32 " otg1 0x%08" B_PRIx32 " otg2 0x%08" B_PRIx32 " otg3 0x%08"
		B_PRIx32 " (mmio %" B_PRIu64 " KiB)\n", stage,
		dcn21_early_read(info, kPinstraps, bytes),
		dcn21_early_read(info, kDmcubCntl, bytes),
		dcn21_early_read(info, kOtg0Control, bytes),
		dcn21_early_read(info, kOtg0HTotal, bytes),
		dcn21_early_read(info, kOtg0Control + kOtgStride, bytes),
		dcn21_early_read(info, kOtg0Control + 2 * kOtgStride, bytes),
		dcn21_early_read(info, kOtg0Control + 3 * kOtgStride, bytes),
		bytes / 1024);
	dprintf("radeon_hd: dcn early: %s took %" B_PRId64 " us\n", stage,
		system_time() - start);
}
