/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * BSecurityKit::BPasswordHash implementation (Argon2id via Monocypher 4).
 */


#include "R5Security.h"

#include "monocypher/monocypher.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


namespace BSecurityKit {

namespace {


static const char kBase64Alphabet[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";


static int
base64_value(char ch)
{
	if (ch >= 'A' && ch <= 'Z')
		return ch - 'A';
	if (ch >= 'a' && ch <= 'z')
		return ch - 'a' + 26;
	if (ch >= '0' && ch <= '9')
		return ch - '0' + 52;
	if (ch == '+')
		return 62;
	if (ch == '/')
		return 63;
	return -1;
}


static void
base64_encode_nopad(const uint8* input, size_t inputLength, BString& out)
{
	out.Truncate(0);
	size_t i = 0;
	while (i + 3 <= inputLength) {
		uint32 v = (static_cast<uint32>(input[i]) << 16)
			| (static_cast<uint32>(input[i + 1]) << 8)
			| static_cast<uint32>(input[i + 2]);
		out.Append(kBase64Alphabet[(v >> 18) & 0x3F], 1);
		out.Append(kBase64Alphabet[(v >> 12) & 0x3F], 1);
		out.Append(kBase64Alphabet[(v >> 6) & 0x3F], 1);
		out.Append(kBase64Alphabet[v & 0x3F], 1);
		i += 3;
	}
	size_t rem = inputLength - i;
	if (rem == 1) {
		uint32 v = static_cast<uint32>(input[i]) << 16;
		out.Append(kBase64Alphabet[(v >> 18) & 0x3F], 1);
		out.Append(kBase64Alphabet[(v >> 12) & 0x3F], 1);
	} else if (rem == 2) {
		uint32 v = (static_cast<uint32>(input[i]) << 16)
			| (static_cast<uint32>(input[i + 1]) << 8);
		out.Append(kBase64Alphabet[(v >> 18) & 0x3F], 1);
		out.Append(kBase64Alphabet[(v >> 12) & 0x3F], 1);
		out.Append(kBase64Alphabet[(v >> 6) & 0x3F], 1);
	}
}


static status_t
base64_decode_nopad(const char* input, size_t inputLength,
	BSecureBuffer& out)
{
	if (input == NULL || inputLength == 0 || (inputLength % 4) == 1)
		return B_BAD_DATA;

	size_t fullGroups = inputLength / 4;
	size_t rem = inputLength % 4;
	size_t outLength = fullGroups * 3 + (rem == 2 ? 1 : rem == 3 ? 2 : 0);

	status_t status = out.Allocate(outLength);
	if (status != B_OK)
		return status;

	uint8* dst = out.Data();
	size_t di = 0;
	for (size_t g = 0; g < fullGroups; g++) {
		int a = base64_value(input[g * 4 + 0]);
		int b = base64_value(input[g * 4 + 1]);
		int c = base64_value(input[g * 4 + 2]);
		int d = base64_value(input[g * 4 + 3]);
		if (a < 0 || b < 0 || c < 0 || d < 0) {
			out.Clear();
			return B_BAD_DATA;
		}
		uint32 v = (static_cast<uint32>(a) << 18)
			| (static_cast<uint32>(b) << 12)
			| (static_cast<uint32>(c) << 6)
			| static_cast<uint32>(d);
		dst[di++] = static_cast<uint8>((v >> 16) & 0xFF);
		dst[di++] = static_cast<uint8>((v >> 8) & 0xFF);
		dst[di++] = static_cast<uint8>(v & 0xFF);
	}

	size_t tail = fullGroups * 4;
	if (rem == 2) {
		int a = base64_value(input[tail]);
		int b = base64_value(input[tail + 1]);
		if (a < 0 || b < 0 || (b & 0x0F) != 0) {
			out.Clear();
			return B_BAD_DATA;
		}
		dst[di++] = static_cast<uint8>((a << 2) | (b >> 4));
	} else if (rem == 3) {
		int a = base64_value(input[tail]);
		int b = base64_value(input[tail + 1]);
		int c = base64_value(input[tail + 2]);
		if (a < 0 || b < 0 || c < 0 || (c & 0x03) != 0) {
			out.Clear();
			return B_BAD_DATA;
		}
		dst[di++] = static_cast<uint8>((a << 2) | (b >> 4));
		dst[di++] = static_cast<uint8>(((b & 0x0F) << 4) | (c >> 2));
	}

	return B_OK;
}


static bool
parse_uint32_strict(const char* start, const char* end, uint32* _value)
{
	if (start == NULL || end == NULL || start >= end)
		return false;
	if (end - start > 1 && *start == '0')
		return false;

	uint64 value = 0;
	for (const char* p = start; p < end; p++) {
		if (*p < '0' || *p > '9')
			return false;
		value = value * 10 + static_cast<uint64>(*p - '0');
		if (value > UINT32_MAX)
			return false;
	}
	*_value = static_cast<uint32>(value);
	return true;
}


static bool
validate_parameters(const BPasswordHash::Parameters& params)
{
	if (params.lanes < 1 || params.lanes > BPasswordHash::kMaxLanes)
		return false;
	if (params.passes < 1 || params.passes > BPasswordHash::kMaxPasses)
		return false;
	if (params.memoryKiB < 8 * params.lanes
		|| params.memoryKiB > BPasswordHash::kMaxMemoryKiB)
		return false;
	return true;
}


static status_t
parse_phc_string(const char* encoded, BPasswordHash::Parameters& outParams,
	BSecureBuffer& outSalt, BSecureBuffer& outHash)
{
	if (encoded == NULL)
		return B_BAD_VALUE;

	static const char kPrefix[] = "$argon2id$v=19$m=";
	const size_t prefixLen = sizeof(kPrefix) - 1;
	if (strncmp(encoded, kPrefix, prefixLen) != 0)
		return B_BAD_DATA;

	const char* p = encoded + prefixLen;
	const char* tTag = strstr(p, ",t=");
	if (tTag == NULL)
		return B_BAD_DATA;
	if (!parse_uint32_strict(p, tTag, &outParams.memoryKiB))
		return B_BAD_DATA;

	p = tTag + 3;
	const char* pTag = strstr(p, ",p=");
	if (pTag == NULL)
		return B_BAD_DATA;
	if (!parse_uint32_strict(p, pTag, &outParams.passes))
		return B_BAD_DATA;

	p = pTag + 3;
	const char* saltSep = strchr(p, '$');
	if (saltSep == NULL)
		return B_BAD_DATA;
	if (!parse_uint32_strict(p, saltSep, &outParams.lanes))
		return B_BAD_DATA;

	if (!validate_parameters(outParams))
		return B_BAD_DATA;

	const char* saltStart = saltSep + 1;
	const char* hashSep = strchr(saltStart, '$');
	if (hashSep == NULL || hashSep == saltStart)
		return B_BAD_DATA;

	const char* hashStart = hashSep + 1;
	if (*hashStart == '\0' || strchr(hashStart, '$') != NULL)
		return B_BAD_DATA;

	status_t status = base64_decode_nopad(saltStart,
		static_cast<size_t>(hashSep - saltStart), outSalt);
	if (status != B_OK)
		return status;
	if (outSalt.Size() < 8) {
		outSalt.Clear();
		return B_BAD_DATA;
	}

	status = base64_decode_nopad(hashStart, strlen(hashStart), outHash);
	if (status != B_OK) {
		outSalt.Clear();
		return status;
	}
	if (outHash.Size() < 4 || outHash.Size() > 1024) {
		outSalt.Clear();
		outHash.Clear();
		return B_BAD_DATA;
	}

	return B_OK;
}


}	// namespace


/* static */ BPasswordHash::Parameters
BPasswordHash::DefaultParameters()
{
	Parameters params;
	params.memoryKiB = kDefaultMemoryKiB;
	params.passes = kDefaultPasses;
	params.lanes = kDefaultLanes;
	return params;
}


/* static */ status_t
BPasswordHash::DeriveKey(const void* password, size_t passwordLength,
	const void* salt, size_t saltLength, const Parameters& params,
	BSecureBuffer& outKey, size_t outLength)
{
	return DeriveKey(password, passwordLength, salt, saltLength,
		NULL, 0, NULL, 0, params, outKey, outLength);
}


/* static */ status_t
BPasswordHash::DeriveKey(const void* password, size_t passwordLength,
	const void* salt, size_t saltLength, const void* secretKey,
	size_t secretKeyLength, const void* associatedData,
	size_t associatedDataLength, const Parameters& params,
	BSecureBuffer& outKey, size_t outLength)
{
	if ((password == NULL && passwordLength > 0)
		|| salt == NULL || saltLength < 8
		|| (secretKey == NULL && secretKeyLength > 0)
		|| (associatedData == NULL && associatedDataLength > 0)
		|| outLength < 4 || outLength > 1024)
		return B_BAD_VALUE;

	if (passwordLength > UINT32_MAX || saltLength > UINT32_MAX
		|| secretKeyLength > UINT32_MAX || associatedDataLength > UINT32_MAX)
		return B_BAD_VALUE;

	if (!validate_parameters(params))
		return B_BAD_VALUE;

	if (static_cast<size_t>(params.memoryKiB) > SIZE_MAX / 1024)
		return B_NO_MEMORY;
	const size_t workBytes = static_cast<size_t>(params.memoryKiB) * 1024;
	void* workArea = malloc(workBytes);
	if (workArea == NULL)
		return B_NO_MEMORY;

	BSecureBuffer derived;
	status_t status = derived.Allocate(outLength);
	if (status != B_OK) {
		free(workArea);
		return status;
	}

	crypto_argon2_config config;
	config.algorithm = CRYPTO_ARGON2_ID;
	config.nb_blocks = params.memoryKiB;
	config.nb_passes = params.passes;
	config.nb_lanes = params.lanes;

	crypto_argon2_inputs inputs;
	inputs.pass = static_cast<const uint8_t*>(password);
	inputs.salt = static_cast<const uint8_t*>(salt);
	inputs.pass_size = static_cast<uint32_t>(passwordLength);
	inputs.salt_size = static_cast<uint32_t>(saltLength);

	crypto_argon2_extras extras;
	extras.key = static_cast<const uint8_t*>(secretKey);
	extras.ad = static_cast<const uint8_t*>(associatedData);
	extras.key_size = static_cast<uint32_t>(secretKeyLength);
	extras.ad_size = static_cast<uint32_t>(associatedDataLength);

	crypto_argon2(derived.Data(), static_cast<uint32_t>(outLength), workArea,
		config, inputs, extras);

	crypto_wipe(workArea, workBytes);
	free(workArea);

	status = outKey.SetTo(derived.Data(), derived.Size());
	derived.Clear();
	return status;
}


/* static */ status_t
BPasswordHash::Hash(const char* password, BString& outEncoded,
	const Parameters& params)
{
	if (password == NULL)
		return B_BAD_VALUE;
	if (!validate_parameters(params))
		return B_BAD_VALUE;

	BSecureBuffer salt;
	status_t status = BRandom::Fill(salt, kDefaultSaltSize);
	if (status != B_OK)
		return status;

	BSecureBuffer hash;
	status = DeriveKey(password, strlen(password), salt.Data(), salt.Size(),
		params, hash, kDefaultHashSize);
	if (status != B_OK)
		return status;

	BString saltB64;
	BString hashB64;
	base64_encode_nopad(salt.Data(), salt.Size(), saltB64);
	base64_encode_nopad(hash.Data(), hash.Size(), hashB64);

	char header[96];
	int written = snprintf(header, sizeof(header),
		"$argon2id$v=19$m=%" B_PRIu32 ",t=%" B_PRIu32 ",p=%" B_PRIu32 "$",
		params.memoryKiB, params.passes, params.lanes);
	if (written <= 0 || static_cast<size_t>(written) >= sizeof(header))
		return B_ERROR;

	outEncoded.SetTo(header);
	outEncoded << saltB64 << "$" << hashB64;
	return B_OK;
}


/* static */ status_t
BPasswordHash::Verify(const char* password, const char* encoded,
	bool* _matches)
{
	if (_matches != NULL)
		*_matches = false;
	if (password == NULL || encoded == NULL || _matches == NULL)
		return B_BAD_VALUE;

	Parameters params;
	BSecureBuffer salt;
	BSecureBuffer expected;
	status_t status = parse_phc_string(encoded, params, salt, expected);
	if (status != B_OK)
		return status;

	BSecureBuffer derived;
	status = DeriveKey(password, strlen(password), salt.Data(), salt.Size(),
		params, derived, expected.Size());
	if (status != B_OK)
		return status;

	*_matches = derived.Equals(expected);
	return B_OK;
}


/* static */ bool
BPasswordHash::NeedsRehash(const char* encoded, const Parameters& params)
{
	if (encoded == NULL || !validate_parameters(params))
		return true;

	Parameters parsed;
	BSecureBuffer salt;
	BSecureBuffer expected;
	if (parse_phc_string(encoded, parsed, salt, expected) != B_OK)
		return true;

	return parsed.memoryKiB < params.memoryKiB
		|| parsed.passes < params.passes
		|| parsed.lanes != params.lanes
		|| expected.Size() < kDefaultHashSize;
}


}	// namespace BSecurityKit
