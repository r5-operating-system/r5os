/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * BSecurityKit::BSealedData implementation (XChaCha20-Poly1305 via Monocypher 4).
 *
 * Wire format (R5SD, version 1):
 *   bytes 0..3   : magic "R5SD"
 *   byte  4      : version (1)
 *   byte  5      : cipher (1 = XChaCha20-Poly1305)
 *   bytes 6..7   : reserved (0, 0)
 *   bytes 8..31  : 24-byte random nonce
 *   bytes 32..47 : 16-byte Poly1305 MAC
 *   bytes 48..   : ciphertext (same length as plaintext)
 *
 * The 8-byte header is always authenticated alongside caller-supplied AAD.
 */


#include "R5Security.h"

#include "monocypher/monocypher.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>


namespace BSecurityKit {

namespace {


static const uint8 kHeader[BSealedData::kHeaderSize] = {
	'R', '5', 'S', 'D',
	BSealedData::kFormatVersion,
	BSealedData::kCipherXChaCha20Poly1305,
	0, 0
};

static const size_t kMaxSealedFileSize = 64 * 1024 * 1024;


class CombinedAad {
public:
	CombinedAad()
		:
		fHeap(NULL),
		fData(NULL),
		fSize(0)
	{
	}

	~CombinedAad()
	{
		free(fHeap);
	}

	status_t Build(const uint8 header[BSealedData::kHeaderSize],
		const void* aad, size_t aadSize)
	{
		if (aadSize > SIZE_MAX - BSealedData::kHeaderSize)
			return B_BAD_VALUE;
		fSize = BSealedData::kHeaderSize + aadSize;
		if (fSize <= sizeof(fStack)) {
			fData = fStack;
		} else {
			fHeap = static_cast<uint8*>(malloc(fSize));
			if (fHeap == NULL) {
				fSize = 0;
				return B_NO_MEMORY;
			}
			fData = fHeap;
		}
		memcpy(fData, header, BSealedData::kHeaderSize);
		if (aadSize > 0)
			memcpy(fData + BSealedData::kHeaderSize, aad, aadSize);
		return B_OK;
	}

	const uint8* Data() const { return fData; }
	size_t Size() const { return fSize; }

private:
	uint8	fStack[BSealedData::kHeaderSize + 256];
	uint8*	fHeap;
	uint8*	fData;
	size_t	fSize;
};


static status_t
write_all(int fd, const uint8* data, size_t size)
{
	size_t written = 0;
	while (written < size) {
		ssize_t n = write(fd, data + written, size - written);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return B_IO_ERROR;
		}
		if (n == 0)
			return B_IO_ERROR;
		written += static_cast<size_t>(n);
	}
	return B_OK;
}


}	// namespace


/* static */ status_t
BSealedData::Seal(const BSecureBuffer& key, const void* plaintext,
	size_t plaintextSize, const void* aad, size_t aadSize,
	void* outSealed, size_t outCapacity, size_t* _outSize)
{
	if (_outSize != NULL)
		*_outSize = 0;

	if (key.Size() != kKeySize || key.Data() == NULL)
		return B_BAD_VALUE;
	if ((plaintext == NULL && plaintextSize > 0)
		|| (aad == NULL && aadSize > 0)
		|| outSealed == NULL)
		return B_BAD_VALUE;
	if (plaintextSize > SIZE_MAX - kOverhead)
		return B_BAD_VALUE;

	const size_t totalSize = kOverhead + plaintextSize;
	if (outCapacity < totalSize)
		return B_BUFFER_OVERFLOW;

	// Refuse overlapping plaintext and output buffers.
	if (plaintextSize > 0) {
		const uintptr_t pStart = reinterpret_cast<uintptr_t>(plaintext);
		const uintptr_t pEnd = pStart + plaintextSize;
		const uintptr_t oStart = reinterpret_cast<uintptr_t>(outSealed);
		const uintptr_t oEnd = oStart + totalSize;
		if (pStart < oEnd && oStart < pEnd)
			return B_BAD_VALUE;
	}

	CombinedAad combinedAad;
	status_t status = combinedAad.Build(kHeader, aad, aadSize);
	if (status != B_OK)
		return status;

	uint8* out = static_cast<uint8*>(outSealed);
	memcpy(out, kHeader, kHeaderSize);

	uint8* nonce = out + kHeaderSize;
	status = BRandom::Fill(nonce, kNonceSize);
	if (status != B_OK) {
		BSecureBuffer::WipeMemory(out, totalSize);
		return status;
	}

	uint8* mac = nonce + kNonceSize;
	uint8* ciphertext = mac + kMacSize;

	crypto_aead_lock(ciphertext, mac, key.Data(), nonce,
		combinedAad.Data(), combinedAad.Size(),
		static_cast<const uint8_t*>(plaintext), plaintextSize);

	if (_outSize != NULL)
		*_outSize = totalSize;
	return B_OK;
}


/* static */ status_t
BSealedData::Open(const BSecureBuffer& key, const void* sealed,
	size_t sealedSize, const void* aad, size_t aadSize,
	BSecureBuffer& outPlaintext)
{
	if (key.Size() != kKeySize || key.Data() == NULL) {
		outPlaintext.Clear();
		return B_BAD_VALUE;
	}
	if (sealed == NULL || (aad == NULL && aadSize > 0)) {
		outPlaintext.Clear();
		return B_BAD_VALUE;
	}
	if (sealedSize < kOverhead) {
		outPlaintext.Clear();
		return B_BAD_DATA;
	}

	const uint8* in = static_cast<const uint8*>(sealed);
	if (memcmp(in, kHeader, kHeaderSize) != 0) {
		outPlaintext.Clear();
		return B_BAD_DATA;
	}

	CombinedAad combinedAad;
	status_t status = combinedAad.Build(in, aad, aadSize);
	if (status != B_OK) {
		outPlaintext.Clear();
		return status;
	}

	const uint8* nonce = in + kHeaderSize;
	const uint8* mac = nonce + kNonceSize;
	const uint8* ciphertext = mac + kMacSize;
	const size_t plaintextSize = sealedSize - kOverhead;

	BSecureBuffer decrypted;
	if (plaintextSize > 0) {
		status = decrypted.Allocate(plaintextSize);
		if (status != B_OK) {
			outPlaintext.Clear();
			return status;
		}
	}

	int rc = crypto_aead_unlock(decrypted.Data(), mac, key.Data(), nonce,
		combinedAad.Data(), combinedAad.Size(), ciphertext, plaintextSize);
	if (rc != 0) {
		decrypted.Clear();
		outPlaintext.Clear();
		return B_NOT_ALLOWED;
	}

	if (plaintextSize == 0) {
		outPlaintext.Clear();
		return B_OK;
	}

	status = outPlaintext.SetTo(decrypted.Data(), decrypted.Size());
	decrypted.Clear();
	if (status != B_OK)
		outPlaintext.Clear();
	return status;
}


/* static */ status_t
BSealedData::SealToFile(const BSecureBuffer& key, const void* plaintext,
	size_t plaintextSize, const void* aad, size_t aadSize, const char* path)
{
	if (path == NULL || path[0] == '\0')
		return B_BAD_VALUE;
	if (plaintextSize > kMaxSealedFileSize - kOverhead)
		return B_BAD_VALUE;

	const size_t totalSize = kOverhead + plaintextSize;
	uint8* sealed = static_cast<uint8*>(malloc(totalSize));
	if (sealed == NULL)
		return B_NO_MEMORY;

	size_t sealedSize = 0;
	status_t status = Seal(key, plaintext, plaintextSize, aad, aadSize,
		sealed, totalSize, &sealedSize);
	if (status != B_OK) {
		free(sealed);
		return status;
	}

	uint8 suffixBytes[8];
	status = BRandom::Fill(suffixBytes, sizeof(suffixBytes));
	if (status != B_OK) {
		free(sealed);
		return status;
	}

	char suffix[17];
	for (size_t i = 0; i < sizeof(suffixBytes); i++)
		snprintf(suffix + i * 2, 3, "%02x", suffixBytes[i]);

	BString tempPath(path);
	tempPath << ".tmp." << suffix;

	int fd = open(tempPath.String(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC,
		0600);
	if (fd < 0) {
		free(sealed);
		return B_IO_ERROR;
	}

	status = write_all(fd, sealed, sealedSize);
	free(sealed);

	if (status == B_OK && fsync(fd) != 0)
		status = B_IO_ERROR;
	if (close(fd) != 0 && status == B_OK)
		status = B_IO_ERROR;

	if (status != B_OK) {
		unlink(tempPath.String());
		return status;
	}

	if (rename(tempPath.String(), path) != 0) {
		unlink(tempPath.String());
		return B_IO_ERROR;
	}
	return B_OK;
}


/* static */ status_t
BSealedData::OpenFromFile(const BSecureBuffer& key, const char* path,
	const void* aad, size_t aadSize, BSecureBuffer& outPlaintext)
{
	if (path == NULL || path[0] == '\0') {
		outPlaintext.Clear();
		return B_BAD_VALUE;
	}

	int fd;
	do {
		fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	} while (fd < 0 && errno == EINTR);
	if (fd < 0) {
		int err = errno;
		outPlaintext.Clear();
		if (err == ENOENT)
			return B_ENTRY_NOT_FOUND;
		if (err == ELOOP)
			return B_BAD_DATA;
		return B_IO_ERROR;
	}

	struct stat st;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
		close(fd);
		outPlaintext.Clear();
		return B_BAD_DATA;
	}
	if (st.st_size < static_cast<off_t>(kOverhead)
		|| static_cast<uint64>(st.st_size) > kMaxSealedFileSize) {
		close(fd);
		outPlaintext.Clear();
		return B_BAD_DATA;
	}

	const size_t fileSize = static_cast<size_t>(st.st_size);
	uint8* buffer = static_cast<uint8*>(malloc(fileSize));
	if (buffer == NULL) {
		close(fd);
		outPlaintext.Clear();
		return B_NO_MEMORY;
	}

	size_t totalRead = 0;
	while (totalRead < fileSize) {
		ssize_t n = read(fd, buffer + totalRead, fileSize - totalRead);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			free(buffer);
			close(fd);
			outPlaintext.Clear();
			return B_IO_ERROR;
		}
		if (n == 0) {
			free(buffer);
			close(fd);
			outPlaintext.Clear();
			return B_BAD_DATA;
		}
		totalRead += static_cast<size_t>(n);
	}
	close(fd);

	status_t status = Open(key, buffer, fileSize, aad, aadSize, outPlaintext);
	free(buffer);
	return status;
}


}	// namespace BSecurityKit
