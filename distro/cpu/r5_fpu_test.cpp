/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Verify that XMM (and YMM when AVX is active) registers keep their exact
 * values across signals, timer/device interrupts, preemption, and syscalls.
 */


#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <OS.h>


static volatile sig_atomic_t sSignalCount = 0;
static volatile sig_atomic_t sStop = 0;
static int sHasAvx = 0;


static int
detect_avx(void)
{
	uint32_t eax, ebx, ecx, edx;
	__asm__ __volatile__("cpuid"
		: "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
		: "a"(1), "c"(0));
	if ((ecx & (1u << 27)) == 0 || (ecx & (1u << 28)) == 0)
		return 0;
	uint32_t xcr0Lo, xcr0Hi;
	__asm__ __volatile__("xgetbv" : "=a"(xcr0Lo), "=d"(xcr0Hi) : "c"(0));
	return (xcr0Lo & 0x6) == 0x6;
}


static void
clobber_xmm(void)
{
	static const uint64_t junk[2] = { 0xdeadbeefcafebabeULL, 0x0123456789abcdefULL };
	__asm__ __volatile__(
		"movdqu (%0), %%xmm0\n"
		"movdqu (%0), %%xmm1\n"
		"movdqu (%0), %%xmm2\n"
		"movdqu (%0), %%xmm3\n"
		"movdqu (%0), %%xmm4\n"
		"movdqu (%0), %%xmm5\n"
		"movdqu (%0), %%xmm6\n"
		"movdqu (%0), %%xmm7\n"
		"movdqu (%0), %%xmm8\n"
		"movdqu (%0), %%xmm9\n"
		"movdqu (%0), %%xmm10\n"
		"movdqu (%0), %%xmm11\n"
		"movdqu (%0), %%xmm12\n"
		"movdqu (%0), %%xmm13\n"
		"movdqu (%0), %%xmm14\n"
		"movdqu (%0), %%xmm15\n"
		:: "r"(junk)
		: "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
		  "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15");
}


__attribute__((target("avx")))
static void
clobber_ymm(void)
{
	static const uint64_t junk[4] = {
		0xdeadbeefcafebabeULL, 0x0123456789abcdefULL,
		0xfeedfacebaadf00dULL, 0x1122334455667788ULL
	};
	__asm__ __volatile__(
		"vmovdqu (%0), %%ymm0\n"
		"vmovdqu (%0), %%ymm1\n"
		"vmovdqu (%0), %%ymm2\n"
		"vmovdqu (%0), %%ymm3\n"
		"vmovdqu (%0), %%ymm4\n"
		"vmovdqu (%0), %%ymm5\n"
		"vmovdqu (%0), %%ymm6\n"
		"vmovdqu (%0), %%ymm7\n"
		"vmovdqu (%0), %%ymm8\n"
		"vmovdqu (%0), %%ymm9\n"
		"vmovdqu (%0), %%ymm10\n"
		"vmovdqu (%0), %%ymm11\n"
		"vmovdqu (%0), %%ymm12\n"
		"vmovdqu (%0), %%ymm13\n"
		"vmovdqu (%0), %%ymm14\n"
		"vmovdqu (%0), %%ymm15\n"
		:: "r"(junk)
		: "ymm0", "ymm1", "ymm2", "ymm3", "ymm4", "ymm5", "ymm6", "ymm7",
		  "ymm8", "ymm9", "ymm10", "ymm11", "ymm12", "ymm13", "ymm14", "ymm15");
}


static void
signal_handler(int sig)
{
	(void)sig;
	if (sHasAvx)
		clobber_ymm();
	else
		clobber_xmm();
	sSignalCount++;
}


static void*
clobber_thread(void* arg)
{
	(void)arg;
	while (!sStop) {
		if (sHasAvx)
			clobber_ymm();
		else
			clobber_xmm();
		snooze(500);
	}
	return NULL;
}


static int
test_xmm(void)
{
	alignas(16) uint8_t expected[16 * 16];
	alignas(16) uint8_t actual[16 * 16];
	for (int i = 0; i < 16 * 16; i++)
		expected[i] = (uint8_t)(0x31 + i * 7);

	__asm__ __volatile__(
		"movdqa   0(%0), %%xmm0\n"
		"movdqa  16(%0), %%xmm1\n"
		"movdqa  32(%0), %%xmm2\n"
		"movdqa  48(%0), %%xmm3\n"
		"movdqa  64(%0), %%xmm4\n"
		"movdqa  80(%0), %%xmm5\n"
		"movdqa  96(%0), %%xmm6\n"
		"movdqa 112(%0), %%xmm7\n"
		"movdqa 128(%0), %%xmm8\n"
		"movdqa 144(%0), %%xmm9\n"
		"movdqa 160(%0), %%xmm10\n"
		"movdqa 176(%0), %%xmm11\n"
		"movdqa 192(%0), %%xmm12\n"
		"movdqa 208(%0), %%xmm13\n"
		"movdqa 224(%0), %%xmm14\n"
		"movdqa 240(%0), %%xmm15\n"
		"movl $50000000, %%ecx\n"
		"1: decl %%ecx\n"
		"jnz 1b\n"
		"movdqa %%xmm0,    0(%1)\n"
		"movdqa %%xmm1,   16(%1)\n"
		"movdqa %%xmm2,   32(%1)\n"
		"movdqa %%xmm3,   48(%1)\n"
		"movdqa %%xmm4,   64(%1)\n"
		"movdqa %%xmm5,   80(%1)\n"
		"movdqa %%xmm6,   96(%1)\n"
		"movdqa %%xmm7,  112(%1)\n"
		"movdqa %%xmm8,  128(%1)\n"
		"movdqa %%xmm9,  144(%1)\n"
		"movdqa %%xmm10, 160(%1)\n"
		"movdqa %%xmm11, 176(%1)\n"
		"movdqa %%xmm12, 192(%1)\n"
		"movdqa %%xmm13, 208(%1)\n"
		"movdqa %%xmm14, 224(%1)\n"
		"movdqa %%xmm15, 240(%1)\n"
		:: "r"(expected), "r"(actual)
		: "rcx", "memory",
		  "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
		  "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15");

	if (memcmp(expected, actual, sizeof(expected)) != 0) {
		printf("r5_fpu_test FAIL: XMM mismatch\n");
		return 1;
	}
	return 0;
}


__attribute__((target("avx")))
static int
test_ymm(void)
{
	alignas(32) uint8_t expected[16 * 32];
	alignas(32) uint8_t actual[16 * 32];
	for (int i = 0; i < 16 * 32; i++)
		expected[i] = (uint8_t)(0x5a + i * 11);

	__asm__ __volatile__(
		"vmovdqa   0(%0), %%ymm0\n"
		"vmovdqa  32(%0), %%ymm1\n"
		"vmovdqa  64(%0), %%ymm2\n"
		"vmovdqa  96(%0), %%ymm3\n"
		"vmovdqa 128(%0), %%ymm4\n"
		"vmovdqa 160(%0), %%ymm5\n"
		"vmovdqa 192(%0), %%ymm6\n"
		"vmovdqa 224(%0), %%ymm7\n"
		"vmovdqa 256(%0), %%ymm8\n"
		"vmovdqa 288(%0), %%ymm9\n"
		"vmovdqa 320(%0), %%ymm10\n"
		"vmovdqa 352(%0), %%ymm11\n"
		"vmovdqa 384(%0), %%ymm12\n"
		"vmovdqa 416(%0), %%ymm13\n"
		"vmovdqa 448(%0), %%ymm14\n"
		"vmovdqa 480(%0), %%ymm15\n"
		"movl $50000000, %%ecx\n"
		"1: decl %%ecx\n"
		"jnz 1b\n"
		"vmovdqa %%ymm0,    0(%1)\n"
		"vmovdqa %%ymm1,   32(%1)\n"
		"vmovdqa %%ymm2,   64(%1)\n"
		"vmovdqa %%ymm3,   96(%1)\n"
		"vmovdqa %%ymm4,  128(%1)\n"
		"vmovdqa %%ymm5,  160(%1)\n"
		"vmovdqa %%ymm6,  192(%1)\n"
		"vmovdqa %%ymm7,  224(%1)\n"
		"vmovdqa %%ymm8,  256(%1)\n"
		"vmovdqa %%ymm9,  288(%1)\n"
		"vmovdqa %%ymm10, 320(%1)\n"
		"vmovdqa %%ymm11, 352(%1)\n"
		"vmovdqa %%ymm12, 384(%1)\n"
		"vmovdqa %%ymm13, 416(%1)\n"
		"vmovdqa %%ymm14, 448(%1)\n"
		"vmovdqa %%ymm15, 480(%1)\n"
		:: "r"(expected), "r"(actual)
		: "rcx", "memory",
		  "ymm0", "ymm1", "ymm2", "ymm3", "ymm4", "ymm5", "ymm6", "ymm7",
		  "ymm8", "ymm9", "ymm10", "ymm11", "ymm12", "ymm13", "ymm14", "ymm15");

	if (memcmp(expected, actual, sizeof(expected)) != 0) {
		printf("r5_fpu_test FAIL: YMM mismatch\n");
		return 1;
	}
	return 0;
}


static void*
signal_sender(void* arg)
{
	pthread_t target = *(pthread_t*)arg;
	while (!sStop) {
		pthread_kill(target, SIGUSR1);
		snooze(2000);
	}
	return NULL;
}


int
main(void)
{
	sHasAvx = detect_avx();

	struct sigaction sa = {};
	sa.sa_handler = signal_handler;
	sigaction(SIGUSR1, &sa, NULL);

	// Exercise many syscalls first (also exercises Intel vzeroupper on entry).
	for (int i = 0; i < 10000; i++)
		getpid();

	pthread_t self = pthread_self();
	pthread_t clobberer, signaler;
	pthread_create(&clobberer, NULL, clobber_thread, NULL);
	pthread_create(&signaler, NULL, signal_sender, &self);

	int rc = 0;
	for (int round = 0; round < 5 && rc == 0; round++) {
		if (test_xmm() != 0)
			rc = 1;
		if (sHasAvx && test_ymm() != 0)
			rc = 1;
	}

	sStop = 1;
	pthread_join(signaler, NULL);
	pthread_join(clobberer, NULL);

	if (sSignalCount == 0) {
		printf("r5_fpu_test FAIL: no signals delivered\n");
		return 1;
	}
	if (rc == 0) {
		printf("r5_fpu_test PASS avx=%d signals=%d\n",
			sHasAvx, (int)sSignalCount);
	}
	return rc;
}
