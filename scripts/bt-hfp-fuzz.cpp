/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Host fuzz harness for the HFP Audio Gateway AT commands of
 * bluetooth_server (distro/bluetooth/server/HFPAgent.cpp, docs/BLUETOOTH.md
 * step 7b), built with AddressSanitizer and UBSan by scripts/fuzz-bt.sh
 * (target "hfp"). Every line lives in a heap buffer of exactly its length.
 * Lines are random bytes, or known commands with random arguments.
 * Checked besides "no fault": every answer ends in OK or ERROR in CR LF,
 * fits its buffer and has no NUL inside; the battery stays -1 or 0 to
 * 100, the volumes -1 or 0 to 15.
 * Prints "hfp_fuzz: <seconds>s 0 crashes" when it ends.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <string>

#include "HFPAgent.h"


using namespace r5hfp;


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
		fprintf(stderr, "hfp_fuzz: check failed: %s\n", what);
		abort();
	}
}


static const char* const kCommands[] = {
	"AT+BRSF=", "AT+BAC=", "AT+CIND=?", "AT+CIND?", "AT+CMER=", "AT+CHLD=?",
	"AT+BIND=", "AT+BIND=?", "AT+BIND?", "AT+BIEV=", "AT+XAPL=",
	"AT+IPHONEACCEV=", "AT+VGS=", "AT+VGM=", "AT+CKPD=", "ATA", "AT+CHUP",
	"AT+BLDN", "AT+BVRA=", "ATD", "AT+NREC=", "AT+CMEE=", "AT+COPS?",
	"AT+CLCC", "AT", "at+biev=", ""
};


static std::string
random_line()
{
	std::string line;
	if (next_word() % 4 == 0) {
		size_t length = next_word() % 300;
		for (size_t i = 0; i < length; i++)
			line += (char)next_word();
		return line;
	}
	line = kCommands[next_word() % (sizeof(kCommands) / sizeof(kCommands[0]))];
	int count = next_word() % 10;
	for (int i = 0; i < count; i++) {
		if (i > 0)
			line += next_word() % 8 == 0 ? ' ' : ',';
		switch (next_word() % 5) {
			case 0:
				line += std::to_string(next_word() % 10);
				break;
			case 1:
				line += std::to_string(next_word() % 200);
				break;
			case 2:
				line += std::to_string(next_word());
				break;
			case 3:
				line += (char)next_word();
				break;
			default:
				line += "";
				break;
		}
	}
	if (next_word() % 8 == 0)
		line += '\r';
	return line;
}


static bool
ends_with(const char* text, size_t length, const char* tail)
{
	size_t tailLength = strlen(tail);
	return length >= tailLength
		&& memcmp(text + length - tailLength, tail, tailLength) == 0;
}


int
main(int argc, char** argv)
{
	int seconds = argc > 1 ? atoi(argv[1]) : 60;
	if (seconds <= 0)
		seconds = 60;
	gState ^= (uint64_t)time(NULL);
	time_t end = time(NULL) + seconds;
	unsigned long lines = 0;
	unsigned long batteries = 0;
	while (time(NULL) < end) {
		Agent agent;
		int count = 1 + next_word() % 40;
		for (int i = 0; i < count; i++) {
			std::string line = random_line();
			char* copy = (char*)malloc(line.size() > 0 ? line.size() : 1);
			memcpy(copy, line.data(), line.size());
			size_t outSize = next_word() % 8 == 0 ? next_word() % 300
				: kMaxAnswer;
			char* out = (char*)malloc(outSize > 0 ? outSize : 1);
			size_t length = agent.Handle(copy, line.size(),
				outSize > 0 ? out : NULL, outSize);
			if (outSize < kMaxAnswer)
				check(length == 0, "small buffer refused");
			else {
				check(length > 0 && length < outSize, "answer fits");
				check(strlen(out) == length, "no NUL inside");
				check(ends_with(out, length, "\r\nOK\r\n")
					|| ends_with(out, length, "\r\nERROR\r\n"),
					"ends in OK or ERROR");
			}
			check(agent.Battery() >= -1 && agent.Battery() <= 100,
				"battery range");
			check(agent.SpeakerVolume() >= -1 && agent.SpeakerVolume() <= 15
				&& agent.MicrophoneVolume() >= -1
				&& agent.MicrophoneVolume() <= 15, "volume range");
			if ((agent.TakeEvents() & kEventBattery) != 0)
				batteries++;
			free(out);
			free(copy);
			lines++;
		}
	}
	printf("hfp_fuzz: %lu lines, %lu battery reports\n", lines, batteries);
	printf("hfp_fuzz: %ds 0 crashes\n", seconds);
	return 0;
}
