/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * BSecurityKit::BRandom implementation.
 */


#include "R5Security.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>


namespace BSecurityKit {


/* static */ status_t
BRandom::Fill(void* buffer, size_t size)
{
	if (size == 0)
		return B_OK;
	if (buffer == NULL)
		return B_BAD_VALUE;

	int fd;
	do {
		fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	} while (fd < 0 && errno == EINTR);
	if (fd < 0) {
		BSecureBuffer::WipeMemory(buffer, size);
		return B_ERROR;
	}

	uint8* out = static_cast<uint8*>(buffer);
	size_t remaining = size;
	while (remaining > 0) {
		ssize_t bytesRead = read(fd, out, remaining);
		if (bytesRead < 0) {
			if (errno == EINTR)
				continue;
			close(fd);
			BSecureBuffer::WipeMemory(buffer, size);
			return B_IO_ERROR;
		}
		if (bytesRead == 0) {
			close(fd);
			BSecureBuffer::WipeMemory(buffer, size);
			return B_IO_ERROR;
		}
		out += bytesRead;
		remaining -= static_cast<size_t>(bytesRead);
	}

	close(fd);
	return B_OK;
}


/* static */ status_t
BRandom::Fill(BSecureBuffer& buffer)
{
	if (buffer.IsEmpty())
		return B_OK;
	return Fill(buffer.Data(), buffer.Size());
}


/* static */ status_t
BRandom::Fill(BSecureBuffer& buffer, size_t size)
{
	status_t status = buffer.Allocate(size);
	if (status != B_OK)
		return status;
	status = Fill(buffer.Data(), buffer.Size());
	if (status != B_OK)
		buffer.Clear();
	return status;
}


}	// namespace BSecurityKit
