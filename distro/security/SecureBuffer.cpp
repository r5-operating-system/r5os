/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * BSecurityKit::BSecureBuffer implementation.
 */


#include "R5Security.h"

#include "monocypher/monocypher.h"

#include <stdlib.h>
#include <string.h>


namespace BSecurityKit {


BSecureBuffer::BSecureBuffer()
	:
	fData(NULL),
	fSize(0),
	fInitStatus(B_OK)
{
}


BSecureBuffer::BSecureBuffer(size_t size)
	:
	fData(NULL),
	fSize(0),
	fInitStatus(B_OK)
{
	fInitStatus = Allocate(size);
}


BSecureBuffer::~BSecureBuffer()
{
	Clear();
}


status_t
BSecureBuffer::InitCheck() const
{
	return fInitStatus;
}


status_t
BSecureBuffer::Allocate(size_t size)
{
	if (size == 0) {
		Clear();
		return B_OK;
	}

	uint8* buffer = static_cast<uint8*>(calloc(1, size));
	if (buffer == NULL)
		return B_NO_MEMORY;

	Clear();
	fData = buffer;
	fSize = size;
	fInitStatus = B_OK;
	return B_OK;
}


status_t
BSecureBuffer::SetTo(const void* data, size_t size)
{
	if (size == 0) {
		Clear();
		return B_OK;
	}
	if (data == NULL)
		return B_BAD_VALUE;

	uint8* buffer = static_cast<uint8*>(malloc(size));
	if (buffer == NULL)
		return B_NO_MEMORY;

	memcpy(buffer, data, size);
	Clear();
	fData = buffer;
	fSize = size;
	fInitStatus = B_OK;
	return B_OK;
}


void
BSecureBuffer::Wipe()
{
	if (fData != NULL && fSize > 0)
		crypto_wipe(fData, fSize);
}


void
BSecureBuffer::Clear()
{
	if (fData != NULL) {
		if (fSize > 0)
			crypto_wipe(fData, fSize);
		free(fData);
		fData = NULL;
	}
	fSize = 0;
	fInitStatus = B_OK;
}


uint8*
BSecureBuffer::Data()
{
	return fData;
}


const uint8*
BSecureBuffer::Data() const
{
	return fData;
}


size_t
BSecureBuffer::Size() const
{
	return fSize;
}


bool
BSecureBuffer::IsEmpty() const
{
	return fSize == 0 || fData == NULL;
}


bool
BSecureBuffer::Equals(const BSecureBuffer& other) const
{
	if (fSize != other.fSize)
		return false;
	if (fSize == 0)
		return true;
	if (fData == NULL || other.fData == NULL)
		return false;

	volatile uint8 diff = 0;
	for (size_t i = 0; i < fSize; i++)
		diff = static_cast<uint8>(diff | (fData[i] ^ other.fData[i]));
	return diff == 0;
}


/* static */ void
BSecureBuffer::WipeMemory(void* data, size_t size)
{
	if (data != NULL && size > 0)
		crypto_wipe(data, size);
}


}	// namespace BSecurityKit
