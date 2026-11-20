/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Allocate and free write-combined kernel areas and check the bytes.
 * The ioctl is R5_PSTATE_WC_DIRECT_MAP in r5_pstate_info.h
 * (B_DEVICE_OP_CODES_END + 0x5236). AMD64 only.
 */


#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <Drivers.h>
#include <OS.h>
#include <fcntl.h>


int
main(void)
{
	int fd = open("/dev/misc/r5_pstate", O_RDONLY);
	if (fd < 0) {
		printf("r5_wc_gtt FAIL open\n");
		return 1;
	}

	int rc = ioctl(fd, B_DEVICE_OP_CODES_END + 0x5236, NULL, 0);
	close(fd);
	if (rc != 0) {
		printf("r5_wc_gtt FAIL %d %s\n", rc, strerror(rc < 0 ? -rc : rc));
		return 1;
	}
	printf("r5_wc_gtt PASS\n");
	return 0;
}
