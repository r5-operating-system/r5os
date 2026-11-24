/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * SHA-256 (FIPS 180-4), written for this app. Not copied from rclone.
 */

#include "Sha256.h"

#include <stdio.h>
#include <string.h>


static uint32
rotr(uint32 x, uint32 n)
{
	return (x >> n) | (x << (32 - n));
}


Sha256::Sha256()
	:
	fBits(0),
	fUsed(0)
{
	fState[0] = 0x6a09e667;
	fState[1] = 0xbb67ae85;
	fState[2] = 0x3c6ef372;
	fState[3] = 0xa54ff53a;
	fState[4] = 0x510e527f;
	fState[5] = 0x9b05688c;
	fState[6] = 0x1f83d9ab;
	fState[7] = 0x5be0cd19;
	memset(fBuffer, 0, sizeof(fBuffer));
}


void
Sha256::_Block(const uint8 block[64])
{
	static const uint32 k[64] = {
		0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
		0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
		0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
		0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
		0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
		0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
		0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
		0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
		0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
		0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
		0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
	};
	uint32 w[64];
	for (int i = 0; i < 16; i++) {
		w[i] = ((uint32)block[i * 4] << 24) | ((uint32)block[i * 4 + 1] << 16)
			| ((uint32)block[i * 4 + 2] << 8) | (uint32)block[i * 4 + 3];
	}
	for (int i = 16; i < 64; i++) {
		uint32 s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
		uint32 s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	uint32 a = fState[0];
	uint32 b = fState[1];
	uint32 c = fState[2];
	uint32 d = fState[3];
	uint32 e = fState[4];
	uint32 f = fState[5];
	uint32 g = fState[6];
	uint32 h = fState[7];
	for (int i = 0; i < 64; i++) {
		uint32 S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
		uint32 ch = (e & f) ^ ((~e) & g);
		uint32 t1 = h + S1 + ch + k[i] + w[i];
		uint32 S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
		uint32 maj = (a & b) ^ (a & c) ^ (b & c);
		uint32 t2 = S0 + maj;
		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}
	fState[0] += a;
	fState[1] += b;
	fState[2] += c;
	fState[3] += d;
	fState[4] += e;
	fState[5] += f;
	fState[6] += g;
	fState[7] += h;
}


void
Sha256::Update(const void* data, size_t length)
{
	const uint8* bytes = (const uint8*)data;
	fBits += (uint64)length * 8;
	while (length > 0) {
		size_t room = 64 - fUsed;
		size_t take = length < room ? length : room;
		memcpy(fBuffer + fUsed, bytes, take);
		fUsed += take;
		bytes += take;
		length -= take;
		if (fUsed == 64) {
			_Block(fBuffer);
			fUsed = 0;
		}
	}
}


void
Sha256::Final(uint8 out[32])
{
	fBuffer[fUsed++] = 0x80;
	if (fUsed > 56) {
		while (fUsed < 64)
			fBuffer[fUsed++] = 0;
		_Block(fBuffer);
		fUsed = 0;
	}
	while (fUsed < 56)
		fBuffer[fUsed++] = 0;
	for (int i = 7; i >= 0; i--)
		fBuffer[fUsed++] = (uint8)((fBits >> (i * 8)) & 0xff);
	_Block(fBuffer);
	for (int i = 0; i < 8; i++) {
		out[i * 4] = (uint8)(fState[i] >> 24);
		out[i * 4 + 1] = (uint8)(fState[i] >> 16);
		out[i * 4 + 2] = (uint8)(fState[i] >> 8);
		out[i * 4 + 3] = (uint8)fState[i];
	}
}


BString
Sha256::FinalHex()
{
	uint8 out[32];
	Final(out);
	BString hex;
	for (int i = 0; i < 32; i++) {
		char pair[3];
		snprintf(pair, sizeof(pair), "%02x", out[i]);
		hex << pair;
	}
	return hex;
}


status_t
Sha256Bytes(const void* data, size_t length, BString& hex)
{
	Sha256 sha;
	sha.Update(data, length);
	hex = sha.FinalHex();
	return B_OK;
}


status_t
Sha256File(const char* path, BString& hex)
{
	FILE* file = fopen(path, "rb");
	if (file == NULL)
		return B_ENTRY_NOT_FOUND;
	Sha256 sha;
	char buffer[8192];
	for (;;) {
		size_t n = fread(buffer, 1, sizeof(buffer), file);
		if (n > 0)
			sha.Update(buffer, n);
		if (n < sizeof(buffer)) {
			if (ferror(file)) {
				fclose(file);
				return B_IO_ERROR;
			}
			break;
		}
	}
	fclose(file);
	hex = sha.FinalHex();
	return B_OK;
}
