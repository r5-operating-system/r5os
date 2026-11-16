/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * The compose thread's refresh period, from the display mode's timing.
 * Plain C types, so scripts/test-compose-period.cpp can test it on the host.
 */
#ifndef COMPOSE_PERIOD_H
#define COMPOSE_PERIOD_H


#include <stdint.h>


// A period outside these bounds is not a display refresh (1000 Hz to 20 Hz).
// VirtualBox's vboxvideo mode has pixel_clock 60 with h_total and v_total
// equal to the visible size; read as kHz that is one frame per 15.5 s at
// 1437x649, and the compose thread waited that long between frames.
static const int64_t kComposePeriodMin = 1000;
static const int64_t kComposePeriodMax = 50000;


/*!	Microseconds per refresh from the pixel clock (kHz) and the totals, or 0
	when the timing gives no usable period. 0 means the compose runs as soon
	as damage arrives.
*/
static inline int64_t
compose_period(uint32_t pixelClock, uint32_t hTotal, uint32_t vTotal)
{
	if (pixelClock == 0 || hTotal == 0 || vTotal == 0)
		return 0;
	int64_t period = (int64_t)hTotal * vTotal * 1000 / pixelClock;
	if (period < kComposePeriodMin || period > kComposePeriodMax)
		return 0;
	return period;
}


#endif	// COMPOSE_PERIOD_H
