/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Host test of the Realtek firmware parser and choice (h2generic/h2rtkfw.h,
 * docs/BLUETOOTH-2.md step B0), built by scripts/test-rtkfw.sh with
 * AddressSanitizer and UBSan. It reads the headers of the shipped
 * linux-firmware files in distro/firmware/realtek and checks hand-made ones.
 * Usage: bt-rtkfw-test <firmware dir>. Prints "rtkfw_test: ok".
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "h2rtkfw.h"


static int gFailures = 0;


static void
check(bool condition, const char* what)
{
	if (!condition) {
		fprintf(stderr, "rtkfw_test: FAILED %s\n", what);
		gFailures++;
	}
}


static std::vector<uint8>
read_file(const std::string& path)
{
	std::vector<uint8> data;
	FILE* file = fopen(path.c_str(), "rb");
	if (file == NULL)
		return data;
	uint8 buffer[4096];
	size_t got;
	while ((got = fread(buffer, 1, sizeof(buffer), file)) > 0)
		data.insert(data.end(), buffer, buffer + got);
	fclose(file);
	return data;
}


static void
test_shipped(const std::string& dir)
{
	std::vector<uint8> fw = read_file(dir + "/rtl8761cu_fw.bin");
	std::vector<uint8> config = read_file(dir + "/rtl8761cu_config.bin");
	check(fw.size() == 10296, "rtl8761cu_fw.bin is 10296 bytes");
	check(config.size() == 11, "rtl8761cu_config.bin is 11 bytes");
	if (fw.size() != 10296 || config.size() != 11)
		return;

	uint8 project = 0xee;
	check(h2_rtk_file_type(fw.data(), fw.size(), &project) == H2_RTK_FW_V1,
		"shipped file is v1");
	check(project == 0x33, "shipped file is project 0x33");

	// Four patches for chip ids 0..3, all 252 bytes at 0x30. A chip id is
	// the ROM version plus one, so ROM versions 0, 1 and 2 have one.
	uint8 patch[H2_RTK_MAX_PATCH];
	for (uint8 rom = 0; rom < 3; rom++) {
		uint32 version = 0;
		size_t length = h2_rtk_patch_v1(fw.data(), fw.size(), rom, patch,
			sizeof(patch), &version);
		check(length == 252, "patch is 252 bytes");
		check(version == 0x4618d3f3, "file version");
		check(memcmp(patch, fw.data() + 0x30, 248) == 0, "patch body");
		check(patch[248] == 0xf3 && patch[249] == 0xd3 && patch[250] == 0x18
			&& patch[251] == 0x46, "file version in the last four bytes");
	}
	check(h2_rtk_patch_v1(fw.data(), fw.size(), 3, patch, sizeof(patch), NULL)
		== 0, "no patch for ROM version 3");
	check(h2_rtk_patch_v1(fw.data(), fw.size(), 255, patch, sizeof(patch),
		NULL) == 0, "ROM version 255 is not chip id 0");
	check(h2_rtk_patch_v1(fw.data(), fw.size(), 0, patch, 100, NULL) == 0,
		"output too small");

	check(h2_rtk_config_ok(config.data(), config.size()), "shipped config");

	// What the driver sends: 252 + 11 bytes in two pieces.
	size_t total = 252 + config.size();
	size_t count = h2_rtk_fragments(total);
	check(count == 2, "two pieces");
	check(h2_rtk_fragment_index(0, count) == 0x00, "first index 0");
	check(h2_rtk_fragment_index(1, count) == 0x81, "last index 0x81");
	check(h2_rtk_fragment_length(0, count, total) == 252, "first 252");
	check(h2_rtk_fragment_length(1, count, total) == 11, "last 11");
}


static void
test_choice()
{
	h2_rtk_choice choice;
	check(h2_rtk_choose(0x0b05, 0x1d70, 0x8761, 0xb, 0xa, &choice),
		"USB-BT600 chosen");
	check(strcmp(choice.name, "rtl8761cu") == 0,
		"USB ID wins over the chip report");
	check(choice.knownProject && choice.project == 0x33, "project 0x33");
	check(h2_rtk_fits(choice, 0x33, 0x1234), "project 0x33 fits");
	check(!h2_rtk_fits(choice, 0x0e, 0x8761), "8761BU file does not fit");

	check(h2_rtk_choose(0x0bda, 0x8771, 0x8761, 0xb, 0xa, &choice),
		"8761BU by chip report");
	check(strcmp(choice.name, "rtl8761bu") == 0, "rtl8761bu");
	check(!choice.knownProject, "chip report entry");
	check(h2_rtk_fits(choice, 14, 0x8761), "project 14 is 8761");
	check(!h2_rtk_fits(choice, 8, 0x8761), "project 8 is 8822");
	check(!h2_rtk_fits(choice, 51, 0x8761), "unknown project does not fit");
	check(!h2_rtk_fits(choice, 255, 0), "out of range does not fit");

	check(h2_rtk_choose(0x0bda, 0x0001, 0x8822, 0xb, 0x7, &choice)
		&& (choice.flags & H2_RTK_NEEDS_CONFIG) != 0, "8822B needs config");
	check(!h2_rtk_choose(0x0bda, 0x0001, 0x9999, 1, 1, &choice),
		"unknown chip");
	check(h2_rtk_find_usb(0x0e8d, 0x0608) == NULL, "MediaTek not in table");
	check(h2_rtk_project_lmp(51) == 0 && h2_rtk_project_lmp(47) == 0x8852
		&& h2_rtk_project_lmp(0) == 0x1200, "project table");
}


static std::vector<uint8>
make_v1(uint16 count, uint16 chip, uint16 length, uint32 offset,
	const std::vector<uint8>& trailer)
{
	std::vector<uint8> f = { 'R', 'e', 'a', 'l', 't', 'e', 'c', 'h',
		0x78, 0x56, 0x34, 0x12, (uint8)count, (uint8)(count >> 8) };
	for (uint16 i = 0; i < count; i++) {
		f.push_back((uint8)(chip + i));
		f.push_back((uint8)((chip + i) >> 8));
	}
	for (uint16 i = 0; i < count; i++) {
		f.push_back((uint8)length);
		f.push_back((uint8)(length >> 8));
	}
	for (uint16 i = 0; i < count; i++) {
		for (int b = 0; b < 4; b++)
			f.push_back((uint8)(offset >> (8 * b)));
	}
	while (f.size() < 64)
		f.push_back(0xaa);
	f.insert(f.end(), trailer.begin(), trailer.end());
	return f;
}


static void
test_made()
{
	const std::vector<uint8> good = { 0xff, 0x05, 0x01, 0x00, 0x51, 0x04,
		0xfd, 0x77 };
	uint8 project = 0;
	uint8 patch[256];

	std::vector<uint8> f = make_v1(1, 1, 16, 32, good);
	check(h2_rtk_file_type(f.data(), f.size(), &project) == H2_RTK_FW_V1
		&& project == 5, "made v1, project 5");
	check(h2_rtk_patch_v1(f.data(), f.size(), 0, patch, sizeof(patch), NULL)
		== 16, "made patch");
	check(patch[12] == 0x78 && patch[15] == 0x12, "made version");

	f = make_v1(1, 1, 3, 32, good);
	check(h2_rtk_patch_v1(f.data(), f.size(), 0, patch, sizeof(patch), NULL)
		== 0, "patch shorter than four bytes");
	f = make_v1(1, 1, 16, 0xfffffff0, good);
	check(h2_rtk_patch_v1(f.data(), f.size(), 0, patch, sizeof(patch), NULL)
		== 0, "offset past the end");
	f = make_v1(1, 1, 200, 32, good);
	check(h2_rtk_patch_v1(f.data(), f.size(), 0, patch, sizeof(patch), NULL)
		== 0, "length past the end");
	f = make_v1(40, 1, 16, 32, good);
	f.resize(40);
	check(h2_rtk_patch_v1(f.data(), f.size(), 0, patch, sizeof(patch), NULL)
		== 0, "table past the end");

	// Trailer instructions that walk off the front, a wrong id length,
	// no trailer, a short file, a v2 signature.
	f = make_v1(1, 1, 16, 32, { 0xff, 0xf0, 0x00, 0x51, 0x04, 0xfd, 0x77 });
	check(h2_rtk_file_type(f.data(), f.size(), &project) == H2_RTK_FW_UNKNOWN,
		"instruction longer than the file");
	f = make_v1(1, 1, 16, 32, { 0x05, 0x06, 0x02, 0x00, 0x51, 0x04, 0xfd,
		0x77 });
	check(h2_rtk_file_type(f.data(), f.size(), &project) == H2_RTK_FW_UNKNOWN,
		"project id of two bytes");
	f = make_v1(1, 1, 16, 32, { 1, 2, 3, 4 });
	check(h2_rtk_file_type(f.data(), f.size(), &project) == H2_RTK_FW_UNKNOWN,
		"no trailer");
	check(h2_rtk_file_type(f.data(), 7, &project) == H2_RTK_FW_UNKNOWN,
		"seven bytes");
	f = make_v1(1, 1, 16, 32, good);
	memcpy(f.data(), "RTBTCore", 8);
	check(h2_rtk_file_type(f.data(), f.size(), &project) == H2_RTK_FW_V2,
		"v2 signature");
	f = make_v1(1, 1, 16, 32, { 0xff, 0x00, 0x51, 0x04, 0xfd, 0x77 });
	check(h2_rtk_file_type(f.data(), f.size(), &project) == H2_RTK_FW_UNKNOWN,
		"end before the project id");

	const uint8 config[] = { 0x55, 0xab, 0x23, 0x87, 0x05, 0x00, 0xf7, 0x00,
		0x02, 0x00, 0x00 };
	uint8 bad[sizeof(config)];
	memcpy(bad, config, sizeof(config));
	bad[4] = 6;
	check(!h2_rtk_config_ok(bad, sizeof(bad)), "config length wrong");
	memcpy(bad, config, sizeof(config));
	bad[8] = 3;
	check(!h2_rtk_config_ok(bad, sizeof(bad)), "config entry too long");
	check(!h2_rtk_config_ok(config, 5), "config too short");
	bad[0] = 0;
	check(!h2_rtk_config_ok(bad, sizeof(bad)), "config signature");

	// Index wrap as rtlbtfw: 0, 1..0x7f, then 1 again.
	size_t count = h2_rtk_fragments(300 * 252 - 5);
	check(count == 300, "300 pieces");
	check(h2_rtk_fragment_index(127, count) == 0x7f, "index 0x7f");
	check(h2_rtk_fragment_index(128, count) == 0x01, "index wraps to 1");
	check(h2_rtk_fragment_index(299, count) == (45 | 0x80), "last wrapped");
	count = h2_rtk_fragments(504);
	check(count == 3 && h2_rtk_fragment_length(2, count, 504) == 0,
		"a multiple of 252 ends with an empty piece, as rtlbtfw");
}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: bt-rtkfw-test <firmware dir>\n");
		return 2;
	}
	test_shipped(argv[1]);
	test_choice();
	test_made();
	if (gFailures != 0) {
		fprintf(stderr, "rtkfw_test: %d failed\n", gFailures);
		return 1;
	}
	printf("rtkfw_test: ok\n");
	return 0;
}
