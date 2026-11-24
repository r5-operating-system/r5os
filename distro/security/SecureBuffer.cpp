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
#include <sys/mman.h>
#include <unistd.h>


namespace BSecurityKit {


// Review fix (24 Nov 2026): the bytes of a secure buffer are kept out of the
// swap file. Each buffer gets whole pages of its own, because Haiku's
// munlock() makes a page swappable again for every buffer on it. mlock() is
// best effort: when it fails (a limit on a host), the buffer still works and
// is still wiped.
static size_t
secure_span(size_t size)
{
	long page = sysconf(_SC_PAGESIZE);
	if (page <= 0)
		page = 4096;
	return (size + (size_t)page - 1) / (size_t)page * (size_t)page;
}


static uint8*
secure_alloc(size_t size)
{
	long page = sysconf(_SC_PAGESIZE);
	if (page <= 0)
		page = 4096;
	size_t span = secure_span(size);
	if (span < size)
		return NULL;
	void* buffer = NULL;
	if (posix_memalign(&buffer, (size_t)page, span) != 0)
		return NULL;
	memset(buffer, 0, span);
	mlock(buffer, span);
	return static_cast<uint8*>(buffer);
}


static void
secure_free(uint8* buffer, size_t size)
{
	if (buffer == NULL)
		return;
	size_t span = secure_span(size);
	crypto_wipe(buffer, span);
	munlock(buffer, span);
	free(buffer);
}


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

	uint8* buffer = secure_alloc(size);
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

	uint8* buffer = secure_alloc(size);
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
		secure_free(fData, fSize);
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
