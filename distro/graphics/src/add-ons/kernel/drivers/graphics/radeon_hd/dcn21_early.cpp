/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * DCN 2.1: is the display block readable, and through which path?
 * (docs/DISPLAY-DCN.md, "Register access"). radeon_hd_init() calls it
 * before and after the GPU start. Nothing in DCN is written.
 *
 * Stick 2 (28 Nov 2026) read 0xffffffff from every DCN register already
 * before the GPU start, through direct BAR5 at DMU_BASE segment 2. Before
 * the GPU start (one thread, nothing else uses the index registers) this
 * file also reads DC_PINSTRAPS through every other path Linux v6.12 (MIT,
 * AMD) has:
 * - the other DMU segments (renoir_ip_offset.h: 0x12, 0xC0, 0x34C0);
 * - the DMU base the IP discovery table gives: Linux reads Renoir's register
 *   bases from that table (amdgpu_discovery.c amdgpu_discovery_set_ip_blocks
 *   default case, amdgpu_discovery_reg_base_init), the table at
 *   RCC_CONFIG_MEMSIZE << 20 - DISCOVERY_TMR_OFFSET
 *   (amdgpu_discovery_read_binary_from_mem, mmRCC_CONFIG_MEMSIZE 0xde3),
 *   read through MM_INDEX/MM_INDEX_HI/MM_DATA (amdgpu_device_mm_access);
 *   layout from include/discovery.h, DMU_HWID 271 from soc15_hw_ip.h;
 * - indirect through MM_INDEX/MM_DATA (amdgpu_mm_rreg style, bit 31 clear);
 * - indirect through PCIE_INDEX2/PCIE_DATA2 (amdgpu_device_indirect_rreg,
 *   nbio_v7_0 get_pcie_index_offset; nbio_7_0_offset.h 0xe/0xf, BASE_IDX 0);
 * - MM_INDEX/MM_DATA through the I/O BAR 4 (rio_mem).
 * The only writes are to the bus interface's index registers, as Linux
 * does for each indirect read; each is put back. The time it takes is
 * logged.
 */


#include "radeon_hd_private.h"

#include <KernelExport.h>
#include <PCI.h>
#include <string.h>


extern pci_module_info* gPCI;


static const uint32 kSeg[3] = { 0x12, 0xc0, 0x34c0 };
static const uint32 kPinstraps = 0x2880;
static const uint32 kOtg0Control = 0x1b41;
static const uint32 kOtg0HTotal = 0x1b2a;
static const uint32 kOtgStride = 0x80;
static const uint32 kDmcubCntl = 0x32a0;

// Bus interface (nbio_7_0_offset.h; NBIO segment 0 is 0, segment 2 0xd20).
static const uint32 kMmIndex = 0x0;
static const uint32 kMmData = 0x1;
static const uint32 kMmIndexHi = 0x6;
static const uint32 kPcieIndex2 = 0xe;
static const uint32 kPcieData2 = 0xf;
static const uint32 kRccConfigMemsize = 0xde3;	// amdgpu_discovery.c
static const uint32 kIpDiscoveryVersion = 0x16a00;	// amdgpu_discovery.c

// discovery.h and amdgpu_discovery.h.
static const uint32 kBinarySignature = 0x28211407;
static const uint32 kTableSignature = 0x53445049;
static const uint64 kDiscoveryTmrOffset = 64 * 1024;
static const uint32 kDiscoveryTmrSize = 10 * 1024;
static const uint16 kDmuHwId = 271;


struct dcn21_mmio {
	radeon_info*	info;
	uint64			bytes;
};


static uint32
mmio_read(const dcn21_mmio& m, uint32 dword)
{
	uint64 byte = (uint64)dword * 4;
	if (byte + 4 > m.bytes)
		return 0xffffffff;
	return *(volatile uint32*)(m.info->registers + byte);
}


static void
mmio_write(const dcn21_mmio& m, uint32 dword, uint32 value)
{
	uint64 byte = (uint64)dword * 4;
	if (byte + 4 > m.bytes)
		return;
	*(volatile uint32*)(m.info->registers + byte) = value;
}


// amdgpu_device_mm_access(): one dword of VRAM (bit 31 set) or of the
// register space (bit 31 clear) through MM_INDEX, MM_INDEX_HI and MM_DATA.
static uint32
mm_read(const dcn21_mmio& m, uint64 address, bool vram)
{
	mmio_write(m, kMmIndex, ((uint32)address & 0x7fffffff)
		| (vram ? 0x80000000u : 0));
	mmio_write(m, kMmIndexHi, (uint32)(address >> 31));
	return mmio_read(m, kMmData);
}


static bool
discovery_read(const dcn21_mmio& m, uint64 base, uint32 offset, uint32 bytes,
	void* out)
{
	if (offset + bytes > kDiscoveryTmrSize)
		return false;
	uint8* dst = (uint8*)out;
	uint32 first = offset & ~3u;
	for (uint32 at = first; at < offset + bytes; at += 4) {
		uint32 word = mm_read(m, base + at, true);
		for (uint32 b = 0; b < 4; b++) {
			uint32 pos = at + b;
			if (pos >= offset && pos < offset + bytes)
				dst[pos - offset] = (uint8)(word >> (8 * b));
		}
	}
	return true;
}


// The DMU bases from the IP discovery table, or false.
static bool
dcn21_discovery_dmu(const dcn21_mmio& m, uint32* bases, uint32* count,
	uint64* tablePos)
{
	uint32 memsize = mmio_read(m, kRccConfigMemsize);
	if (memsize == 0 || memsize == 0xffffffff || memsize > (1u << 20))
		return false;
	uint64 base = ((uint64)memsize << 20) - kDiscoveryTmrOffset;
	*tablePos = base;

	uint32 signature = 0;
	discovery_read(m, base, 0, 4, &signature);
	if (signature != kBinarySignature) {
		dprintf("radeon_hd: dcn early: discovery at vram 0x%" B_PRIx64
			": signature 0x%08" B_PRIx32 ", not 0x%08" B_PRIx32 "\n", base,
			signature, kBinarySignature);
		return false;
	}
	// binary_header: signature, version 2x16, checksum, size, then
	// table_list[0] = { offset, checksum, size, padding }.
	uint16 tableOffset = 0;
	discovery_read(m, base, 12, 2, &tableOffset);
	uint32 tableSignature = 0;
	uint16 version = 0;
	discovery_read(m, base, tableOffset, 4, &tableSignature);
	discovery_read(m, base, tableOffset + 4, 2, &version);
	if (tableSignature != kTableSignature) {
		dprintf("radeon_hd: dcn early: discovery ip table at 0x%x signature"
			" 0x%08" B_PRIx32 "\n", tableOffset, tableSignature);
		return false;
	}
	// ip_discovery_header: signature 4, version 2, size 2, id 4,
	// num_dies 2, die_info[16] of { die_id, die_offset }, then 2 bytes
	// whose first bit says 64-bit bases (version 4).
	uint16 dieOffset = 0;
	discovery_read(m, base, tableOffset + 16, 2, &dieOffset);
	uint8 flags = 0;
	discovery_read(m, base, tableOffset + 14 + 16 * 4, 1, &flags);
	bool bases64 = version >= 4 && (flags & 1) != 0;
	uint16 numIps = 0;
	discovery_read(m, base, dieOffset + 2, 2, &numIps);

	uint32 at = dieOffset + 4;
	for (uint16 i = 0; i < numIps && i < 256; i++) {
		uint8 entry[8];
		if (!discovery_read(m, base, at, 8, entry))
			return false;
		uint16 hwId = (uint16)(entry[0] | (entry[1] << 8));
		uint8 numBases = entry[3];
		uint32 baseBytes = bases64 ? 8 : 4;
		if (hwId == kDmuHwId) {
			*count = 0;
			for (uint8 b = 0; b < numBases && b < 6; b++) {
				uint32 value = 0;
				discovery_read(m, base, at + 8 + b * baseBytes, 4, &value);
				bases[(*count)++] = value;
			}
			dprintf("radeon_hd: dcn early: discovery v%u: DMU %u.%u.%u bases"
				" %u\n", version, entry[4], entry[5], entry[6], numBases);
			return true;
		}
		at += 8 + numBases * baseBytes;
	}
	dprintf("radeon_hd: dcn early: discovery: no DMU among %u ips\n", numIps);
	return false;
}


static void
dcn21_early_paths(const dcn21_mmio& m)
{
	bigtime_t start = system_time();
	uint32 pinstrapsDword = kSeg[2] + kPinstraps;

	dprintf("radeon_hd: dcn early: RCC_CONFIG_MEMSIZE 0x%08" B_PRIx32
		" IP_DISCOVERY_VERSION 0x%08" B_PRIx32 "\n",
		mmio_read(m, kRccConfigMemsize), mmio_read(m, kIpDiscoveryVersion));
	for (int s = 0; s < 3; s++) {
		dprintf("radeon_hd: dcn early: pinstraps direct seg 0x%" B_PRIx32
			": 0x%08" B_PRIx32 "\n", kSeg[s],
			mmio_read(m, kSeg[s] + kPinstraps));
	}

	// Indirect, through MM_INDEX/MM_DATA (register space).
	uint32 savedIndex = mmio_read(m, kMmIndex);
	uint32 savedIndexHi = mmio_read(m, kMmIndexHi);
	dprintf("radeon_hd: dcn early: pinstraps via MM_INDEX: 0x%08" B_PRIx32
		"\n", mm_read(m, (uint64)pinstrapsDword * 4, false));

	// The discovery table, through the same registers (VRAM space).
	uint32 bases[6];
	uint32 count = 0;
	uint64 tablePos = 0;
	if (dcn21_discovery_dmu(m, bases, &count, &tablePos)) {
		for (uint32 b = 0; b < count; b++) {
			dprintf("radeon_hd: dcn early: discovery DMU base %" B_PRIu32
				" 0x%08" B_PRIx32 "%s\n", b, bases[b],
				b < 3 && bases[b] == kSeg[b] ? " (= renoir_ip_offset.h)" : "");
		}
		if (count > 2) {
			dprintf("radeon_hd: dcn early: pinstraps via discovery base"
				" 0x%" B_PRIx32 ": 0x%08" B_PRIx32 "\n", bases[2],
				mmio_read(m, bases[2] + kPinstraps));
		}
	} else if (tablePos != 0) {
		dprintf("radeon_hd: dcn early: discovery table not read at vram 0x%"
			B_PRIx64 "\n", tablePos);
	}
	mmio_write(m, kMmIndexHi, savedIndexHi);
	mmio_write(m, kMmIndex, savedIndex);

	// Indirect, through PCIE_INDEX2/PCIE_DATA2.
	uint32 savedPcie = mmio_read(m, kPcieIndex2);
	mmio_write(m, kPcieIndex2, pinstrapsDword * 4);
	mmio_read(m, kPcieIndex2);
	uint32 viaPcie = mmio_read(m, kPcieData2);
	mmio_write(m, kPcieIndex2, savedPcie);
	dprintf("radeon_hd: dcn early: pinstraps via PCIE_INDEX2: 0x%08" B_PRIx32
		"\n", viaPcie);

	// MM_INDEX/MM_DATA through the I/O BAR.
	pci_info* pci = m.info->pci;
	if (gPCI != NULL && pci != NULL
		&& (pci->u.h0.base_register_flags[4] & PCI_address_space) != 0
		&& pci->u.h0.base_register_sizes[4] >= 8) {
		int port = (int)pci->u.h0.base_registers[4];
		uint32 savedIo = gPCI->read_io_32(port);
		gPCI->write_io_32(port, pinstrapsDword * 4);
		uint32 viaIo = gPCI->read_io_32(port + 4);
		gPCI->write_io_32(port, savedIo);
		dprintf("radeon_hd: dcn early: pinstraps via io bar 4 (0x%x): 0x%08"
			B_PRIx32 "\n", port, viaIo);
	}

	dprintf("radeon_hd: dcn early: paths took %" B_PRId64 " us\n",
		system_time() - start);
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
	dcn21_mmio m = { &info, area.size };
	bigtime_t start = system_time();
	uint32 seg2 = kSeg[2];
	dprintf("radeon_hd: dcn early: %s: pinstraps 0x%08" B_PRIx32
		" dmcub 0x%08" B_PRIx32 " otg0 0x%08" B_PRIx32 " htotal 0x%08"
		B_PRIx32 " otg1 0x%08" B_PRIx32 " otg2 0x%08" B_PRIx32 " otg3 0x%08"
		B_PRIx32 " (mmio %" B_PRIu64 " KiB)\n", stage,
		mmio_read(m, seg2 + kPinstraps), mmio_read(m, seg2 + kDmcubCntl),
		mmio_read(m, seg2 + kOtg0Control), mmio_read(m, seg2 + kOtg0HTotal),
		mmio_read(m, seg2 + kOtg0Control + kOtgStride),
		mmio_read(m, seg2 + kOtg0Control + 2 * kOtgStride),
		mmio_read(m, seg2 + kOtg0Control + 3 * kOtgStride), m.bytes / 1024);
	if (strcmp(stage, "before gpu start") == 0)
		dcn21_early_paths(m);
	dprintf("radeon_hd: dcn early: %s took %" B_PRId64 " us\n", stage,
		system_time() - start);
}
