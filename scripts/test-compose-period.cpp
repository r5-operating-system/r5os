/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Host test of the compositor's refresh period (compose_period.h): real
 * modes give one refresh, the VirtualBox mode gives none.
 * g++ -std=c++17 scripts/test-compose-period.cpp && ./a.out
 */

#include "../distro/app_server/compositor/compose_period.h"

#include <stdio.h>


static int sFailed = 0;


static void
expect(const char* what, int64_t got, int64_t low, int64_t high)
{
	if (got < low || got > high) {
		fprintf(stderr, "fail %s: %lld not in %lld..%lld\n", what,
			(long long)got, (long long)low, (long long)high);
		sFailed++;
	}
}


int
main(void)
{
	// VESA 1024x768 at 60 Hz (QEMU std VGA): 65 MHz, 1344 x 806
	expect("1024x768 60 Hz", compose_period(65000, 1344, 806), 16600, 16700);
	// CVT 1920x1080 at 60 Hz: 148.5 MHz, 2200 x 1125
	expect("1920x1080 60 Hz", compose_period(148500, 2200, 1125), 16600, 16700);
	// 1920x1080 at 144 Hz: 325.08 MHz, 2080 x 1085
	expect("1920x1080 144 Hz", compose_period(325080, 2080, 1085), 6900, 7000);
	// VirtualBox vboxvideo after a resize: pixel clock 60, totals = size.
	// Read as kHz that is 15.5 s per frame; it must count as no period.
	expect("VirtualBox 1437x649", compose_period(60, 1437, 649), 0, 0);
	expect("VirtualBox 1920x922", compose_period(60, 1920, 922), 0, 0);
	// No timing at all
	expect("no pixel clock", compose_period(0, 1344, 806), 0, 0);
	expect("no totals", compose_period(65000, 0, 0), 0, 0);
	// The bounds: 20 Hz is still a period, 19 Hz is not
	expect("20 Hz", compose_period(20000, 1000, 1000), 50000, 50000);
	expect("19 Hz", compose_period(19000, 1000, 1000), 0, 0);
	if (sFailed != 0)
		return 1;
	printf("ok compose period\n");
	return 0;
}
