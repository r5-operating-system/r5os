/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


/*!	Finds and reads the speaker protection firmware files.

	Files live in data/firmware/cirrus (non-packaged first, then the system
	data directory). The search order is CS35L41-SPEC.md section 3.
*/


#include "Firmware.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <FindDirectory.h>
#include <KernelExport.h>


#define FIRMWARE_DIRECTORY	"/firmware/cirrus/"
#define MAX_FIRMWARE_SIZE	(512 * 1024)


static status_t
read_file(const char* name, firmware_file* file)
{
	static const directory_which kPlaces[] = {
		B_SYSTEM_NONPACKAGED_DATA_DIRECTORY,
		B_SYSTEM_DATA_DIRECTORY,
	};

	for (size_t i = 0; i < B_COUNT_OF(kPlaces); i++) {
		char path[B_PATH_NAME_LENGTH];
		if (find_directory(kPlaces[i], -1, false, path, sizeof(path)) != B_OK)
			continue;
		strlcat(path, FIRMWARE_DIRECTORY, sizeof(path));
		strlcat(path, name, sizeof(path));

		int fd = open(path, O_RDONLY);
		if (fd < 0)
			continue;

		off_t size = lseek(fd, 0, SEEK_END);
		if (size <= 0 || size > MAX_FIRMWARE_SIZE
			|| lseek(fd, 0, SEEK_SET) != 0) {
			close(fd);
			return B_BAD_DATA;
		}
		uint8* data = (uint8*)malloc((size_t)size);
		if (data == NULL) {
			close(fd);
			return B_NO_MEMORY;
		}
		size_t done = 0;
		while (done < (size_t)size) {
			ssize_t bytes = read(fd, data + done, (size_t)size - done);
			if (bytes <= 0)
				break;
			done += bytes;
		}
		close(fd);
		if (done != (size_t)size) {
			free(data);
			return B_IO_ERROR;
		}

		file->data = data;
		file->size = done;
		strlcpy(file->name, name, sizeof(file->name));
		return B_OK;
	}
	return B_ENTRY_NOT_FOUND;
}


void
free_firmware(firmware_file* file)
{
	free(file->data);
	file->data = NULL;
	file->size = 0;
	file->name[0] = '\0';
}


/*!	The program (.wmfw) and tuning (.bin) for one amplifier, in the order
	of CS35L41-SPEC.md section 3 with type "spk-prot" on "dsp1":
	1. ID-spkidN-amp.wmfw, ID-spkidN-amp.bin
	2. ID-amp.wmfw, ID-spkidN-amp.bin
	3. ID-spkidN.wmfw, ID-spkidN-amp.bin or ID-spkidN.bin
	4. ID.wmfw, ID-spkidN-amp.bin or ID-spkidN.bin
	5. cs35l41-dsp1-spk-prot.wmfw, cs35l41-dsp1-spk-prot.bin
	Names are lower case.
*/
status_t
find_firmware(const char* id, int32 speakerID, const char* amp,
	firmware_file* program, firmware_file* tuning)
{
	char base[64];
	snprintf(base, sizeof(base), "cs35l41-dsp1-spk-prot-%s", id);
	for (char* c = base; *c != '\0'; c++) {
		if (*c >= 'A' && *c <= 'Z')
			*c += 'a' - 'A';
	}

	char spkid[16] = "";
	if (speakerID >= 0)
		snprintf(spkid, sizeof(spkid), "-spkid%" B_PRId32, speakerID);

	struct candidate {
		char	program[96];
		char	tuning[2][96];
	} candidates[5];
	memset(candidates, 0, sizeof(candidates));

	snprintf(candidates[0].program, 96, "%s%s-%s.wmfw", base, spkid, amp);
	snprintf(candidates[0].tuning[0], 96, "%s%s-%s.bin", base, spkid, amp);
	snprintf(candidates[1].program, 96, "%s-%s.wmfw", base, amp);
	snprintf(candidates[1].tuning[0], 96, "%s%s-%s.bin", base, spkid, amp);
	snprintf(candidates[2].program, 96, "%s%s.wmfw", base, spkid);
	snprintf(candidates[2].tuning[0], 96, "%s%s-%s.bin", base, spkid, amp);
	snprintf(candidates[2].tuning[1], 96, "%s%s.bin", base, spkid);
	snprintf(candidates[3].program, 96, "%s.wmfw", base);
	snprintf(candidates[3].tuning[0], 96, "%s%s-%s.bin", base, spkid, amp);
	snprintf(candidates[3].tuning[1], 96, "%s%s.bin", base, spkid);
	snprintf(candidates[4].program, 96, "cs35l41-dsp1-spk-prot.wmfw");
	snprintf(candidates[4].tuning[0], 96, "cs35l41-dsp1-spk-prot.bin");

	for (size_t i = 0; i < B_COUNT_OF(candidates); i++) {
		if (read_file(candidates[i].program, program) != B_OK)
			continue;
		for (size_t j = 0; j < 2; j++) {
			if (candidates[i].tuning[j][0] != '\0'
				&& read_file(candidates[i].tuning[j], tuning) == B_OK) {
				return B_OK;
			}
		}
		free_firmware(program);
	}
	return B_ENTRY_NOT_FOUND;
}
