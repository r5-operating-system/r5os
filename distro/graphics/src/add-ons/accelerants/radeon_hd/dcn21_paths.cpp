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
 * Only the bus interface's index registers are written; each is put back.
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
	else {
		PATHLOG("unknown path \"%s\" (discovery, mm_index, pcie_index2)\n",
			name);
		return B_BAD_VALUE;
	}
	PATHLOG("%s: done in %" B_PRId64 " us\n", name, system_time() - start);
	return status;
}
