#include <math.h>

#include <r5_x86_cpu.h>


#if __FMA__

float fmaf(float x, float y, float z)
{
	__asm__ ("vfmadd132ss %1, %2, %0" : "+x" (x) : "x" (y), "x" (z));
	return x;
}

#elif __FMA4__

float fmaf(float x, float y, float z)
{
	__asm__ ("vfmaddss %3, %2, %1, %0" : "=x" (x) : "x" (x), "x" (y), "x" (z));
	return x;
}

#else

#define fmaf r5_fmaf_c
#include "../fmaf.c"
#undef fmaf


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
static float __attribute__((target("fma")))
r5_fmaf_fma3(float x, float y, float z)
{
	__asm__ ("vfmadd132ss %1, %2, %0" : "+x" (x) : "x" (y), "x" (z));
	return x;
}


static float __attribute__((target("fma4")))
r5_fmaf_fma4(float x, float y, float z)
{
	__asm__ ("vfmaddss %3, %2, %1, %0" : "=x" (x) : "x" (x), "x" (y), "x" (z));
	return x;
}


#if defined(__HAIKU__) && defined(__x86_64__) && !defined(_KERNEL_MODE) \
	&& !defined(_BOOT_MODE) && !defined(R5_HOST_TEST)
static float (*r5_fmaf_resolve(void))(float, float, float)
{
	uint32_t flags = r5_x86_cpu_flags();
	if ((flags & R5_CPU_FMA) != 0)
		return r5_fmaf_fma3;
	if ((flags & R5_CPU_FMA4) != 0)
		return r5_fmaf_fma4;
	return r5_fmaf_c;
}


float fmaf(float, float, float) __attribute__((ifunc("r5_fmaf_resolve")));
#else
static float (*r5_fmaf_impl)(float, float, float);


float
fmaf(float x, float y, float z)
{
	float (*fn)(float, float, float)
		= __atomic_load_n(&r5_fmaf_impl, __ATOMIC_ACQUIRE);
	if (fn == 0) {
		uint32_t flags = r5_x86_cpu_flags();
		if ((flags & R5_CPU_FMA) != 0)
			fn = r5_fmaf_fma3;
		else if ((flags & R5_CPU_FMA4) != 0)
			fn = r5_fmaf_fma4;
		else
			fn = r5_fmaf_c;
		__atomic_store_n(&r5_fmaf_impl, fn, __ATOMIC_RELEASE);
	}
	return fn(x, y, z);
}
#endif
// SPDX-SnippetEnd

#endif
