/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef CS35L41_FIRMWARE_H
#define CS35L41_FIRMWARE_H


#include "Amplifier.h"


status_t	find_firmware(const char* id, int32 speakerID, const char* amp,
				firmware_file* program, firmware_file* tuning);
void		free_firmware(firmware_file* file);


#endif	// CS35L41_FIRMWARE_H
