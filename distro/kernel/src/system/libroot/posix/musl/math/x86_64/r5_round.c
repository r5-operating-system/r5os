/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * SSE4.1 roundsd / roundss. x86-64-v2 has the instruction, so there is no
 * runtime dispatch. The immediate is the Intel rounding mode: 1 floor,
 * 2 ceil, 3 trunc, 4 current mode (rint); bit 3 (0x08) suppresses FE_INEXACT
 * for floor, ceil and trunc per ISO C / IEEE 754.
 */


static inline double __attribute__((target("sse4.1")))
r5_roundsd(double value, int mode)
{
	double result;
	__asm__("roundsd %2, %1, %0" : "=x" (result) : "x" (value), "i" (mode));
	return result;
}


static inline float __attribute__((target("sse4.1")))
r5_roundss(float value, int mode)
{
	float result;
	__asm__("roundss %2, %1, %0" : "=x" (result) : "x" (value), "i" (mode));
	return result;
}


double floor(double x) { return r5_roundsd(x, 1 | 8); }
double ceil(double x) { return r5_roundsd(x, 2 | 8); }
double trunc(double x) { return r5_roundsd(x, 3 | 8); }
double rint(double x) { return r5_roundsd(x, 4); }

float floorf(float x) { return r5_roundss(x, 1 | 8); }
float ceilf(float x) { return r5_roundss(x, 2 | 8); }
float truncf(float x) { return r5_roundss(x, 3 | 8); }
float rintf(float x) { return r5_roundss(x, 4); }
