#include <math.h>

#include <r5_x86_cpu.h>


#if __FMA__

double fma(double x, double y, double z)
{
	__asm__ ("vfmadd132sd %1, %2, %0" : "+x" (x) : "x" (y), "x" (z));
	return x;
}

#elif __FMA4__

double fma(double x, double y, double z)
{
	__asm__ ("vfmaddsd %3, %2, %1, %0" : "=x" (x) : "x" (x), "x" (y), "x" (z));
	return x;
}

#else

#define fma r5_fma_c
#include "../fma.c"
#undef fma


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
static double __attribute__((target("fma")))
r5_fma_fma3(double x, double y, double z)
{
	__asm__ ("vfmadd132sd %1, %2, %0" : "+x" (x) : "x" (y), "x" (z));
	return x;
}


static double __attribute__((target("fma4")))
r5_fma_fma4(double x, double y, double z)
{
	__asm__ ("vfmaddsd %3, %2, %1, %0" : "=x" (x) : "x" (x), "x" (y), "x" (z));
	return x;
}


#if defined(__R5_USE_IFUNC__) && defined(__HAIKU__) && defined(__x86_64__) \
	&& !defined(_KERNEL_MODE) && !defined(_BOOT_MODE) && !defined(R5_HOST_TEST)
static double (*r5_fma_resolve(void))(double, double, double)
{
	uint32_t flags = r5_x86_cpu_flags();
	if ((flags & R5_CPU_FMA) != 0)
		return r5_fma_fma3;
	if ((flags & R5_CPU_FMA4) != 0)
		return r5_fma_fma4;
	return r5_fma_c;
}


double fma(double, double, double) __attribute__((ifunc("r5_fma_resolve")));
#else
static double (*r5_fma_impl)(double, double, double);


double
fma(double x, double y, double z)
{
	double (*fn)(double, double, double)
		= __atomic_load_n(&r5_fma_impl, __ATOMIC_ACQUIRE);
	if (fn == 0) {
		uint32_t flags = r5_x86_cpu_flags();
		if ((flags & R5_CPU_FMA) != 0)
			fn = r5_fma_fma3;
		else if ((flags & R5_CPU_FMA4) != 0)
			fn = r5_fma_fma4;
		else
			fn = r5_fma_c;
		__atomic_store_n(&r5_fma_impl, fn, __ATOMIC_RELEASE);
	}
	return fn(x, y, z);
}
#endif
// SPDX-SnippetEnd

#endif
