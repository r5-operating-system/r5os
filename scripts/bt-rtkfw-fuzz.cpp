/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Host fuzz harness for the Realtek firmware parser (h2generic/h2rtkfw.h,
 * docs/BLUETOOTH-2.md step B0), built with AddressSanitizer and UBSan by
 * scripts/fuzz-bt.sh (target rtkfw): random files, random files with a
 * real signature and trailer, and damaged copies of a well-formed file go
 * through the type check, the v1 patch, the config check and the choice.
 * Every file is an exact-size heap copy, so a read past it is caught.
 * Usage: bt-rtkfw-fuzz <seconds>. Prints "rtkfw_fuzz: <seconds>s 0 crashes".
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <vector>

#include "h2rtkfw.h"


static uint64_t gState = 0x9e3779b97f4a7c15ULL;


static uint32_t
next_word()
{
	gState ^= gState << 13;
	gState ^= gState >> 7;
	gState ^= gState << 17;
	return (uint32_t)gState;
}


static void
check(bool condition, const char* what)
{
	if (!condition) {
		fprintf(stderr, "bt-rtkfw-fuzz: check failed: %s\n", what);
		abort();
	}
}


static std::vector<uint8>
well_formed()
{
	uint16 count = 1 + next_word() % 6;
	std::vector<uint8> f = { 'R', 'e', 'a', 'l', 't', 'e', 'c', 'h',
		1, 2, 3, 4, (uint8)count, 0 };
	size_t table = 14 + count * 8;
	for (uint16 i = 0; i < count; i++) {
		f.push_back((uint8)(i + 1));
		f.push_back(0);
	}
	for (uint16 i = 0; i < count; i++) {
		uint16 length = 4 + next_word() % 300;
		f.push_back((uint8)length);
		f.push_back((uint8)(length >> 8));
	}
	for (uint16 i = 0; i < count; i++) {
		uint32 offset = table + next_word() % 64;
		for (int b = 0; b < 4; b++)
			f.push_back((uint8)(offset >> (8 * b)));
	}
	size_t body = next_word() % 600;
	for (size_t i = 0; i < body; i++)
		f.push_back((uint8)next_word());
	const uint8 trailer[] = { 0xff, (uint8)(next_word() % 60), 0x01, 0x00,
		0x51, 0x04, 0xfd, 0x77 };
	f.insert(f.end(), trailer, trailer + sizeof(trailer));
	return f;
}


static void
run_one(const std::vector<uint8>& source)
{
	// Exact-size heap copy, so ASan sees any read past the end.
	size_t size = source.size();
	uint8* data = (uint8*)malloc(size > 0 ? size : 1);
	if (size > 0)
		memcpy(data, source.data(), size);

	uint8 project = 0;
	h2_rtk_fw_type type = h2_rtk_file_type(data, size, &project);
	check(type == H2_RTK_FW_UNKNOWN || size >= 8 + 4, "type size");

	static uint8 patch[H2_RTK_MAX_PATCH];
	size_t outSize = (next_word() & 1) != 0 ? sizeof(patch)
		: next_word() % 400;
	uint8 rom = (uint8)(next_word() % 8);
	size_t length = h2_rtk_patch_v1(data, size, rom, patch, outSize, NULL);
	check(length == 0 || (length >= 4 && length <= outSize && length <= size),
		"patch length");

	h2_rtk_config_ok(data, size);

	h2_rtk_choice choice;
	if (h2_rtk_choose((uint16)next_word(), (uint16)next_word(),
			(uint16)next_word(), (uint16)(next_word() % 16),
			(uint8)(next_word() % 16), &choice))
		h2_rtk_fits(choice, project, (uint16)next_word());

	if (length > 0) {
		size_t count = h2_rtk_fragments(length);
		size_t sent = 0;
		for (size_t i = 0; i < count; i++) {
			size_t piece = h2_rtk_fragment_length(i, count, length);
			check(piece <= H2_RTK_FRAGMENT_SIZE, "piece size");
			check(i * H2_RTK_FRAGMENT_SIZE + piece <= length, "piece inside");
			sent += piece;
			uint8 index = h2_rtk_fragment_index(i, count);
			check(((index & H2_RTK_FRAGMENT_LAST) != 0) == (i == count - 1),
				"last flag");
		}
		check(sent == length, "all sent");
	}
	free(data);
}


int
main(int argc, char** argv)
{
	int seconds = argc > 1 ? atoi(argv[1]) : 60;
	time_t end = time(NULL) + seconds;
	gState ^= (uint64_t)time(NULL);
	unsigned long rounds = 0;

	while (time(NULL) < end) {
		std::vector<uint8> f;
		switch (next_word() % 4) {
			case 0:
			{
				size_t size = next_word() % 200;
				for (size_t i = 0; i < size; i++)
					f.push_back((uint8)next_word());
				break;
			}
			case 1:
				f = well_formed();
				break;
			default:
			{
				f = well_formed();
				size_t flips = 1 + next_word() % 8;
				for (size_t i = 0; i < flips && !f.empty(); i++)
					f[next_word() % f.size()] = (uint8)next_word();
				if ((next_word() & 3) == 0 && !f.empty())
					f.resize(next_word() % f.size());
				break;
			}
		}
		run_one(f);
		rounds++;
	}

	printf("rtkfw_fuzz: %lu files\n", rounds);
	printf("rtkfw_fuzz: %ds 0 crashes\n", seconds);
	return 0;
}
