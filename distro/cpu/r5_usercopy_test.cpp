/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Verify user_memcpy, user_memset and user_strlcpy return B_BAD_ADDRESS
 * on bad user pointers without panicking.
 */


#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <OS.h>


extern "C" status_t _kern_get_thread_info(thread_id id, thread_info* info);
extern "C" ssize_t _kern_read(int fd, off_t pos, void* buffer, size_t bufferSize);
extern "C" ssize_t _kern_write(int fd, off_t pos, const void* buffer, size_t bufferSize);
extern "C" int _kern_open(int fd, const char* path, int openMode, int perms);
extern "C" status_t _kern_normalize_path(const char* userPath, bool traverseLink,
	char* buffer);


static int
check_bad(const char* label, status_t status)
{
	if (status != B_BAD_ADDRESS) {
		printf("r5_usercopy_test FAIL %s status=%d\n", label, (int)status);
		return 1;
	}
	return 0;
}


int
main(void)
{
	void* base = NULL;
	area_id area = create_area("uc_test", &base, B_ANY_ADDRESS,
		B_PAGE_SIZE * 2, B_NO_LOCK, B_READ_AREA | B_WRITE_AREA);
	if (area < 0) {
		printf("r5_usercopy_test FAIL create_area\n");
		return 1;
	}
	void* unmapped = (uint8_t*)base + B_PAGE_SIZE;
	if (resize_area(area, B_PAGE_SIZE) != B_OK) {
		printf("r5_usercopy_test FAIL resize_area\n");
		return 1;
	}
	void* straddle = (uint8_t*)base + B_PAGE_SIZE - 4;
	void* kernAddr = (void*)0xffffffff80000000ULL;

	int pipefd[2];
	if (pipe(pipefd) != 0) {
		printf("r5_usercopy_test FAIL pipe\n");
		return 1;
	}
	char tmp[B_PATH_NAME_LENGTH];
	int zeroFd = open("/dev/zero", O_RDONLY);
	thread_id self = find_thread(NULL);

	int rc = 0;
	rc |= check_bad("write_null", (status_t)_kern_write(pipefd[1], -1, NULL, 16));
	rc |= check_bad("write_kern", (status_t)_kern_write(pipefd[1], -1, kernAddr, 16));
	rc |= check_bad("write_unmap", (status_t)_kern_write(pipefd[1], -1, unmapped, 16));
	rc |= check_bad("write_straddle", (status_t)_kern_write(pipefd[1], -1, straddle, 16));

	rc |= check_bad("tinfo_null", _kern_get_thread_info(self, NULL));
	rc |= check_bad("tinfo_kern", _kern_get_thread_info(self, (thread_info*)kernAddr));
	rc |= check_bad("tinfo_unmap", _kern_get_thread_info(self, (thread_info*)unmapped));
	rc |= check_bad("tinfo_straddle", _kern_get_thread_info(self, (thread_info*)straddle));

	rc |= check_bad("open_null", (status_t)_kern_open(-1, NULL, O_RDONLY, 0));
	rc |= check_bad("norm_kern", _kern_normalize_path((const char*)kernAddr, true, tmp));
	rc |= check_bad("norm_unmap", _kern_normalize_path((const char*)unmapped, true, tmp));
	memset(straddle, 'a', 4);
	rc |= check_bad("norm_straddle", _kern_normalize_path((const char*)straddle, true, tmp));

	if (zeroFd >= 0) {
		rc |= check_bad("zero_unmap", (status_t)_kern_read(zeroFd, -1, unmapped, 16));
		rc |= check_bad("zero_straddle", (status_t)_kern_read(zeroFd, -1, straddle, 16));
		close(zeroFd);
	}

	close(pipefd[0]);
	close(pipefd[1]);
	delete_area(area);

	if (rc == 0)
		printf("r5_usercopy_test PASS\n");
	return rc;
}
