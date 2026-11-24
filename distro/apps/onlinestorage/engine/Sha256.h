/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef ONLINE_STORAGE_SHA256_H
#define ONLINE_STORAGE_SHA256_H

#include <String.h>

#include <stddef.h>


class Sha256 {
public:
								Sha256();
			void				Update(const void* data, size_t length);
			void				Final(uint8 out[32]);
			BString				FinalHex();

private:
			void				_Block(const uint8 block[64]);

			uint32				fState[8];
			uint64				fBits;
			uint8				fBuffer[64];
			size_t				fUsed;
};


status_t		Sha256File(const char* path, BString& hex);
status_t		Sha256Bytes(const void* data, size_t length, BString& hex);

#endif
