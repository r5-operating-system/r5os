/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * The Realtek Bluetooth firmware files, without kernel dependencies, so the
 * driver (h2realtek.cpp), the host test (scripts/test-rtkfw.sh) and the fuzz
 * target (scripts/fuzz-bt.sh rtkfw) run the same code (docs/BLUETOOTH-2.md,
 * step B0).
 *
 * The file format and the download rules follow FreeBSD's rtlbtfw
 * (usr.sbin/bluetooth/rtlbtfw, BSD-2-Clause, copyright 2013 Adrian Chadd,
 * 2019 Vladimir Kondratyev, 2023 Future Crew LLC). This is R5's own code
 * written from that description, not a copy, except the two tables marked
 * as BSD-2-Clause snippets below, which are FreeBSD's data:
 *
 *	h2_rtk_find_usb		the radios R5 names by USB ID, with the firmware
 *						file and the project id that file must carry;
 *	h2_rtk_find_ic		FreeBSD's table: firmware name from what the chip
 *						reports in Read Local Version;
 *	h2_rtk_project_lmp	FreeBSD's table: project id to LMP subversion;
 *	h2_rtk_file_type	the file signature and the project id in the
 *						extension trailer at the end of the file;
 *	h2_rtk_patch_v1		the patch for one ROM version out of a "Realtech"
 *						(v1) file, with the file's version in its last
 *						four bytes;
 *	h2_rtk_config_ok	the shape of a _config.bin file;
 *	h2_rtk_fragments	how the patch is cut into download commands.
 *
 * Only the headers of a firmware file are read here. Its contents are
 * handed to the radio as they are; nothing here looks inside them.
 * Every offset and length from a file is checked; nothing here allocates.
 */
#ifndef _H2RTKFW_H_
#define _H2RTKFW_H_


#include <string.h>

#include <SupportDefs.h>


#define H2_RTK_MANUFACTURER			0x005d
	// Realtek Semiconductor Corporation in the Bluetooth company
	// identifiers (Assigned Numbers, 7.1).

#define H2_RTK_OP_READ_ROM_VERSION	0xfc6d
#define H2_RTK_OP_DOWNLOAD			0xfc20
#define H2_RTK_FRAGMENT_SIZE		252
#define H2_RTK_FRAGMENT_LAST		0x80
#define H2_RTK_MAX_PATCH			(64 * 1024 + 256)

#define H2_RTK_HEADER_V1_SIZE		14	// signature 8, version 4, count 2
#define H2_RTK_HEADER_V2_SIZE		20	// signature 8, version 8, count 4
#define H2_RTK_CONFIG_HEADER_SIZE	6	// signature 4, length 2

enum h2_rtk_fw_type {
	H2_RTK_FW_UNKNOWN = 0,
	H2_RTK_FW_V1,
	H2_RTK_FW_V2
};

// FreeBSD's flag for a chip that cannot do without its _config.bin.
#define H2_RTK_NEEDS_CONFIG			0x01

struct h2_rtk_usb_entry {
	uint16		vendor;
	uint16		product;
	const char*	name;		// file name before "_fw.bin" / "_config.bin"
	uint8		project;	// the project id the file must carry
	uint8		flags;
};

struct h2_rtk_ic_entry {
	uint16		lmp_subversion;
	uint16		hci_revision;
	uint8		hci_version;
	uint8		flags;
	const char*	name;
};


/* The one radio R5 names by its USB ID: the ASUS USB-BT600 (a Realtek
 * RTL8761CU). FreeBSD's table has no 8761C, so the firmware is named here,
 * and the project id is the one in linux-firmware's rtl8761cu_fw.bin
 * (0x33, read from its trailer).
 */
static const h2_rtk_usb_entry kH2RtkUsbTable[] = {
	{ 0x0b05, 0x1d70, "rtl8761cu", 0x33, 0 },	// ASUS USB-BT600
};


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2013 Adrian Chadd, 2019 Vladimir Kondratyev, 2023 Future Crew LLC
// SPDX-License-Identifier: BSD-2-Clause
// FreeBSD rtlbtfw, rtlbt_fw.c: rtlbt_ic_id_table and project_ids, as data.
// Conditions and disclaimer: LICENSES/rtlbtfw-BSD-2-Clause.txt.
static const h2_rtk_ic_entry kH2RtkIcTable[] = {
	{ 0x1200, 0xb, 0x6, 0, "rtl8723a" },
	{ 0x8723, 0xb, 0x6, 0, "rtl8723b" },
	{ 0x8723, 0xd, 0x8, H2_RTK_NEEDS_CONFIG, "rtl8723d" },
	{ 0x8821, 0xa, 0x6, 0, "rtl8821a" },
	{ 0x8821, 0xc, 0x8, 0, "rtl8821c" },
	{ 0x8761, 0xa, 0x6, 0, "rtl8761a" },
	{ 0x8761, 0xb, 0xa, 0, "rtl8761bu" },
	{ 0x8822, 0xc, 0xa, 0, "rtl8822cu" },
	{ 0x8822, 0xb, 0x7, H2_RTK_NEEDS_CONFIG, "rtl8822b" },
	{ 0x8852, 0xa, 0xb, 0, "rtl8852au" },
	{ 0x8852, 0xb, 0xb, 0, "rtl8852bu" },
	{ 0x8852, 0xc, 0xc, 0, "rtl8852cu" },
	{ 0x8851, 0xb, 0xc, 0, "rtl8851bu" },
	{ 0x8922, 0xa, 0xc, 0, "rtl8922au" },
	{ 0x8852, 0x87, 0xc, 0, "rtl8852btu" },
};


// Project ids; zero is a gap.
static const uint16 kH2RtkProjectLmp[] = {
	0x1200, 0x8723, 0x8821, 0x8761, 0, 0, 0, 0x8703,	// 0-7
	0x8822, 0x8723, 0x8821, 0, 0, 0x8822, 0x8761, 0,	// 8-15
	0, 0, 0x8852, 0x8723, 0x8852, 0, 0, 0,				// 16-23
	0, 0x8852, 0, 0, 0, 0, 0, 0,						// 24-31
	0, 0x8822, 0, 0, 0x8851, 0, 0, 0,					// 32-39
	0, 0, 0, 0, 0x8922, 0, 0, 0x8852,					// 40-47
};
// SPDX-SnippetEnd


static inline uint16
h2_rtk_le16(const uint8* bytes)
{
	return (uint16)(bytes[0] | (bytes[1] << 8));
}


static inline uint32
h2_rtk_le32(const uint8* bytes)
{
	return (uint32)bytes[0] | ((uint32)bytes[1] << 8)
		| ((uint32)bytes[2] << 16) | ((uint32)bytes[3] << 24);
}


static inline const h2_rtk_usb_entry*
h2_rtk_find_usb(uint16 vendor, uint16 product)
{
	for (size_t i = 0; i < sizeof(kH2RtkUsbTable) / sizeof(kH2RtkUsbTable[0]);
			i++) {
		if (kH2RtkUsbTable[i].vendor == vendor
			&& kH2RtkUsbTable[i].product == product)
			return &kH2RtkUsbTable[i];
	}
	return NULL;
}


static inline const h2_rtk_ic_entry*
h2_rtk_find_ic(uint16 lmpSubversion, uint16 hciRevision, uint8 hciVersion)
{
	for (size_t i = 0; i < sizeof(kH2RtkIcTable) / sizeof(kH2RtkIcTable[0]);
			i++) {
		if (kH2RtkIcTable[i].lmp_subversion == lmpSubversion
			&& kH2RtkIcTable[i].hci_revision == hciRevision
			&& kH2RtkIcTable[i].hci_version == hciVersion)
			return &kH2RtkIcTable[i];
	}
	return NULL;
}


/* The LMP subversion of a project id, or 0 when FreeBSD does not know it. */
static inline uint16
h2_rtk_project_lmp(uint8 project)
{
	if (project >= sizeof(kH2RtkProjectLmp) / sizeof(kH2RtkProjectLmp[0]))
		return 0;
	return kH2RtkProjectLmp[project];
}


/* Which firmware a Realtek radio gets. The USB ID table comes first, since a
 * radio there may report what FreeBSD's table gives another chip; then the
 * chip's own report. Returns false when neither knows the radio.
 */
struct h2_rtk_choice {
	const char*	name;
	uint8		project;
	bool		knownProject;	// project is the one the file must carry
	uint8		flags;
};


static inline bool
h2_rtk_choose(uint16 vendor, uint16 product, uint16 lmpSubversion,
	uint16 hciRevision, uint8 hciVersion, h2_rtk_choice* choice)
{
	const h2_rtk_usb_entry* usbEntry = h2_rtk_find_usb(vendor, product);
	if (usbEntry != NULL) {
		choice->name = usbEntry->name;
		choice->project = usbEntry->project;
		choice->knownProject = true;
		choice->flags = usbEntry->flags;
		return true;
	}

	const h2_rtk_ic_entry* icEntry
		= h2_rtk_find_ic(lmpSubversion, hciRevision, hciVersion);
	if (icEntry != NULL) {
		choice->name = icEntry->name;
		choice->project = 0;
		choice->knownProject = false;
		choice->flags = icEntry->flags;
		return true;
	}
	return false;
}


/* Whether a file whose trailer names fileProject is for this radio. */
static inline bool
h2_rtk_fits(const h2_rtk_choice& choice, uint8 fileProject,
	uint16 lmpSubversion)
{
	if (choice.knownProject)
		return fileProject == choice.project;
	uint16 lmp = h2_rtk_project_lmp(fileProject);
	return lmp != 0 && lmp == lmpSubversion;
}


/* The signature at the start, and the project id: the trailer at the end is
 * the four bytes 51 04 fd 77, and before it, read backwards, instructions of
 * an opcode, a length and that many bytes of data. Opcode 0 with one byte is
 * the project id; opcode 0xff ends the list.
 */
static inline h2_rtk_fw_type
h2_rtk_file_type(const uint8* data, size_t size, uint8* outProject)
{
	static const uint8 kSignatureV1[8]
		= { 'R', 'e', 'a', 'l', 't', 'e', 'c', 'h' };
	static const uint8 kSignatureV2[8]
		= { 'R', 'T', 'B', 'T', 'C', 'o', 'r', 'e' };
	static const uint8 kTrailer[4] = { 0x51, 0x04, 0xfd, 0x77 };

	h2_rtk_fw_type type;
	size_t headerSize;

	if (data == NULL || size < 8)
		return H2_RTK_FW_UNKNOWN;

	if (memcmp(data, kSignatureV1, 8) == 0) {
		type = H2_RTK_FW_V1;
		headerSize = H2_RTK_HEADER_V1_SIZE;
	} else if (memcmp(data, kSignatureV2, 8) == 0) {
		type = H2_RTK_FW_V2;
		headerSize = H2_RTK_HEADER_V2_SIZE;
	} else
		return H2_RTK_FW_UNKNOWN;

	if (size < headerSize + sizeof(kTrailer) + 4)
		return H2_RTK_FW_UNKNOWN;
	if (memcmp(data + size - sizeof(kTrailer), kTrailer, sizeof(kTrailer))
			!= 0)
		return H2_RTK_FW_UNKNOWN;

	// "end" is one past the next byte to read, walking backwards; it never
	// goes below the header.
	size_t end = size - sizeof(kTrailer);
	while (end >= headerSize + 2) {
		uint8 opcode = data[end - 1];
		uint8 length = data[end - 2];
		end -= 2;
		if (length > end - headerSize)
			return H2_RTK_FW_UNKNOWN;
		end -= length;

		if (opcode == 0x00) {
			if (length != 1)
				return H2_RTK_FW_UNKNOWN;
			*outProject = data[end];
			return type;
		}
		if (opcode == 0xff)
			break;
	}

	return H2_RTK_FW_UNKNOWN;
}


/* Out of a v1 file, the patch for the chip whose ROM version is given. The
 * file lists its patches by chip id, which counts from one where the ROM
 * version counts from zero. The patch's last four bytes are replaced by the
 * file's version. Returns the patch length, or 0 when there is none or the
 * file does not hold together.
 */
static inline size_t
h2_rtk_patch_v1(const uint8* data, size_t size, uint8 romVersion,
	uint8* out, size_t outSize, uint32* outFileVersion)
{
	if (data == NULL || size < H2_RTK_HEADER_V1_SIZE)
		return 0;

	uint32 fileVersion = h2_rtk_le32(data + 8);
	uint16 count = h2_rtk_le16(data + 12);
	const uint8* table = data + H2_RTK_HEADER_V1_SIZE;
	// chip ids (2 each), lengths (2 each), offsets (4 each)
	if (size < H2_RTK_HEADER_V1_SIZE + 4 + 4 + (size_t)count * 8)
		return 0;

	for (uint16 i = 0; i < count; i++) {
		if (h2_rtk_le16(table + 2 * i) != (uint16)(romVersion + 1))
			continue;

		size_t length = h2_rtk_le16(table + 2 * count + 2 * i);
		size_t offset = h2_rtk_le32(table + 4 * count + 4 * i);
		if (length < 4 || offset > size || length > size - offset
			|| length > outSize)
			return 0;

		memcpy(out, data + offset, length - 4);
		out[length - 4] = fileVersion & 0xff;
		out[length - 3] = (fileVersion >> 8) & 0xff;
		out[length - 2] = (fileVersion >> 16) & 0xff;
		out[length - 1] = (fileVersion >> 24) & 0xff;
		if (outFileVersion != NULL)
			*outFileVersion = fileVersion;
		return length;
	}

	return 0;
}


/* A config file: the signature 55 ab 23 87, a length, and that many bytes of
 * entries (an offset of two bytes, a length of one, the data). It is sent
 * after the patch, as it is.
 */
static inline bool
h2_rtk_config_ok(const uint8* data, size_t size)
{
	static const uint8 kSignature[4] = { 0x55, 0xab, 0x23, 0x87 };

	if (data == NULL || size < H2_RTK_CONFIG_HEADER_SIZE
		|| memcmp(data, kSignature, 4) != 0)
		return false;

	size_t length = h2_rtk_le16(data + 4);
	if (length != size - H2_RTK_CONFIG_HEADER_SIZE)
		return false;

	size_t at = H2_RTK_CONFIG_HEADER_SIZE;
	while (at < size) {
		if (size - at < 3)
			return false;
		size_t entry = data[at + 2];
		at += 3;
		if (entry > size - at)
			return false;
		at += entry;
	}
	return true;
}


/* The download is cut the way rtlbtfw cuts it: length / 252 + 1 commands,
 * all of 252 bytes except the last, which holds what is left (none when the
 * length is a multiple of 252). The index byte starts at 0, counts to 0x7f,
 * goes on from 1, and the last one has 0x80 added.
 */
static inline size_t
h2_rtk_fragments(size_t length)
{
	return length / H2_RTK_FRAGMENT_SIZE + 1;
}


static inline uint8
h2_rtk_fragment_index(size_t fragment, size_t count)
{
	uint8 index = fragment == 0 ? 0 : (uint8)((fragment - 1) % 0x7f + 1);
	if (fragment == count - 1)
		index |= H2_RTK_FRAGMENT_LAST;
	return index;
}


static inline size_t
h2_rtk_fragment_length(size_t fragment, size_t count, size_t length)
{
	if (fragment == count - 1)
		return length % H2_RTK_FRAGMENT_SIZE;
	return H2_RTK_FRAGMENT_SIZE;
}


#endif /* _H2RTKFW_H_ */
