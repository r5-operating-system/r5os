/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Time without a system call (docs/KERNEL.md, "Time in user space").
 *
 *   time_test bench            cycles per call, user path and system call
 *   time_test load <seconds>   monotonic under load, across CPUs, and the
 *                              agreement with the kernel's clock
 *   time_test skew <seconds>   the cross-CPU check for system_time(), the
 *                              system call and CLOCK_MONOTONIC, per CPU pair
 *   time_test rtc              real-time change (root): CLOCK_REALTIME,
 *                              gettimeofday() and time() follow, the
 *                              monotonic clock does not jump
 *
 * Every line starts with "time " and ends in "ok" or "FAIL".
 */

#include <OS.h>

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <x86intrin.h>


extern "C" status_t _kern_get_clock(clockid_t clockID, bigtime_t* _time);
extern "C" bigtime_t _kern_system_time();
extern "C" status_t _kern_set_real_time_clock(bigtime_t time);


static int sFailures = 0;


static void
report(bool ok, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	printf("time ");
	vprintf(format, args);
	printf(" %s\n", ok ? "ok" : "FAIL");
	va_end(args);
	fflush(stdout);
	if (!ok)
		sFailures++;
}


static inline int64_t
mono_ns()
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (int64_t)now.tv_sec * 1000000000LL + now.tv_nsec;
}


static inline int64_t
real_ns()
{
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	return (int64_t)now.tv_sec * 1000000000LL + now.tv_nsec;
}


// #pragma mark - bench


typedef void (*bench_call)();

static volatile int64_t sSink;

static void call_system_time() { sSink += system_time(); }
static void call_system_time_nsecs() { sSink += system_time_nsecs(); }
static void call_mono() { sSink += mono_ns(); }
static void call_real() { sSink += real_ns(); }
// The code clock_gettime(CLOCK_MONOTONIC) ran before this change:
// system_time() in microseconds, then bigtime_to_timespec().
static void call_mono_old()
{
	bigtime_t micro = system_time();
	struct timespec now;
	now.tv_sec = micro / 1000000;
	now.tv_nsec = (micro % 1000000) * 1000;
	sSink += now.tv_nsec;
}
static void call_gettimeofday()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	sSink += tv.tv_usec;
}
static void call_time() { sSink += time(NULL); }
static void call_real_time_clock_usecs() { sSink += real_time_clock_usecs(); }
static void call_kern_system_time() { sSink += _kern_system_time(); }
static void call_kern_get_clock_mono()
{
	bigtime_t value;
	_kern_get_clock(CLOCK_MONOTONIC, &value);
	sSink += value;
}
static void call_kern_get_clock_real()
{
	bigtime_t value;
	_kern_get_clock(CLOCK_REALTIME, &value);
	sSink += value;
}


/*!	The best of five rounds, in TSC cycles per call. */
static double
bench(bench_call call, int count)
{
	double best = 1e18;
	for (int round = 0; round < 5; round++) {
		uint32_t aux;
		uint64_t start = __rdtscp(&aux);
		for (int i = 0; i < count; i++)
			call();
		uint64_t end = __rdtscp(&aux);
		double perCall = (double)(end - start) / count;
		if (perCall < best)
			best = perCall;
	}
	return best;
}


static int
run_bench()
{
	struct entry {
		const char*	name;
		bench_call	call;
		bool		syscall;
	} entries[] = {
		{ "system_time", call_system_time, false },
		{ "system_time_nsecs", call_system_time_nsecs, false },
		{ "clock_gettime_monotonic", call_mono, false },
		{ "clock_gettime_monotonic_old_us", call_mono_old, false },
		{ "clock_gettime_realtime", call_real, false },
		{ "gettimeofday", call_gettimeofday, false },
		{ "time", call_time, false },
		{ "real_time_clock_usecs", call_real_time_clock_usecs, false },
		{ "syscall_system_time", call_kern_system_time, true },
		{ "syscall_get_clock_monotonic", call_kern_get_clock_mono, true },
		{ "syscall_get_clock_realtime", call_kern_get_clock_real, true },
	};
	double user = 0;
	double kernel = 0;
	for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
		double cycles = bench(entries[i].call, entries[i].syscall
			? 200000 : 2000000);
		printf("time bench %s %.1f cycles\n", entries[i].name, cycles);
		if (strcmp(entries[i].name, "clock_gettime_monotonic") == 0)
			user = cycles;
		if (strcmp(entries[i].name, "syscall_get_clock_monotonic") == 0)
			kernel = cycles;
	}
	report(user > 0 && kernel > user, "bench monotonic %.1f vs syscall %.1f "
		"cycles, %.1fx", user, kernel, user > 0 ? kernel / user : 0.0);

	struct timespec resolution;
	clock_getres(CLOCK_MONOTONIC, &resolution);
	report(resolution.tv_sec == 0 && resolution.tv_nsec == 1,
		"clock_getres monotonic %ld ns", (long)resolution.tv_nsec);
	return 0;
}


// #pragma mark - load


static volatile int64_t sLatest = 0;
static volatile bool sStop = false;

struct load_result {
	int64_t		reads;
	int64_t		ownBackwards;
	int64_t		crossBackwards;
	int64_t		worstCrossNs;
	int64_t		migrations;
	int64_t		subMicro;
};


/*!	Each thread reads the clock in a loop. Within the thread the value
	must never go back. Across threads: a value published by another
	thread was read before this thread's read began, so this read must
	not be smaller (the TSC-sync check). RDTSCP's aux is the CPU number
	the kernel writes in TSC_AUX; a change is a migration.
*/
static void*
load_thread(void* data)
{
	load_result* result = (load_result*)data;
	int64_t last = 0;
	uint32_t lastCpu = ~0u;
	while (!sStop) {
		int64_t published = __atomic_load_n(&sLatest, __ATOMIC_ACQUIRE);
		int64_t now = mono_ns();
		uint32_t cpu;
		__rdtscp(&cpu);
		result->reads++;
		if (now < last)
			result->ownBackwards++;
		if (now < published) {
			result->crossBackwards++;
			if (published - now > result->worstCrossNs)
				result->worstCrossNs = published - now;
		}
		if (now % 1000 != 0)
			result->subMicro++;
		last = now;
		int64_t seen = published;
		while (now > seen && !__atomic_compare_exchange_n(&sLatest, &seen,
				now, true, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
		}
		if (lastCpu != ~0u && cpu != lastCpu)
			result->migrations++;
		lastCpu = cpu;
		// Every 4096 reads: give the CPU up, so the scheduler moves us.
		if ((result->reads & 4095) == 0)
			sched_yield();
	}
	return NULL;
}


// #pragma mark - skew


/*!	The cross-CPU check of run_load() for three clocks, to tell the TSC
	itself from the user path: "user" is system_time() (microseconds, user
	space), "kernel" is _kern_system_time() (the same clock through a
	system call), "ns" is clock_gettime(CLOCK_MONOTONIC). A published value
	carries the CPU that read it (low 6 bits), so the worst step back is
	kept per ordered pair of CPUs: a constant offset between two TSCs shows
	as one direction of a pair, noise as both.
*/
enum { kSkewUser, kSkewKernel, kSkewNs };
static const int kSkewCpus = 16;
static volatile int64_t sSkewLatest = 0;
static volatile bool sSkewStop = false;
static int sSkewClock = kSkewNs;

struct skew_result {
	int64_t		reads;
	int64_t		backwards;
	int64_t		worst;
	int64_t		pair[kSkewCpus][kSkewCpus];
};


static inline int64_t
skew_now()
{
	if (sSkewClock == kSkewUser)
		return system_time() * 1000;
	if (sSkewClock == kSkewKernel)
		return _kern_system_time() * 1000;
	return mono_ns();
}


static void*
skew_thread(void* data)
{
	skew_result* result = (skew_result*)data;
	while (!sSkewStop) {
		int64_t published = __atomic_load_n(&sSkewLatest, __ATOMIC_ACQUIRE);
		int64_t now = skew_now();
		uint32_t cpu;
		__rdtscp(&cpu);
		cpu &= 63;
		result->reads++;
		int64_t publishedNs = published >> 6;
		uint32_t publishedCpu = (uint32_t)(published & 63);
		if (now < publishedNs) {
			int64_t back = publishedNs - now;
			result->backwards++;
			if (back > result->worst)
				result->worst = back;
			if (cpu < kSkewCpus && publishedCpu < kSkewCpus
				&& back > result->pair[publishedCpu][cpu])
				result->pair[publishedCpu][cpu] = back;
		}
		int64_t mine = (now << 6) | cpu;
		int64_t seen = published;
		while ((seen >> 6) < now && !__atomic_compare_exchange_n(&sSkewLatest,
				&seen, mine, true, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
		}
		if ((result->reads & 4095) == 0)
			sched_yield();
	}
	return NULL;
}


static int
run_skew(int seconds)
{
	static const char* kNames[] = { "user", "kernel", "ns" };
	system_info info;
	get_system_info(&info);
	int threads = (int)info.cpu_count * 2;
	if (threads > 32)
		threads = 32;
	for (int clock = kSkewUser; clock <= kSkewNs; clock++) {
		sSkewClock = clock;
		sSkewLatest = 0;
		sSkewStop = false;
		pthread_t thread[32];
		static skew_result results[32];
		memset(results, 0, sizeof(results));
		for (int i = 0; i < threads; i++)
			pthread_create(&thread[i], NULL, skew_thread, &results[i]);
		snooze((bigtime_t)seconds * 1000000);
		sSkewStop = true;
		skew_result total;
		memset(&total, 0, sizeof(total));
		for (int i = 0; i < threads; i++) {
			pthread_join(thread[i], NULL);
			total.reads += results[i].reads;
			total.backwards += results[i].backwards;
			if (results[i].worst > total.worst)
				total.worst = results[i].worst;
			for (int a = 0; a < kSkewCpus; a++) {
				for (int b = 0; b < kSkewCpus; b++) {
					if (results[i].pair[a][b] > total.pair[a][b])
						total.pair[a][b] = results[i].pair[a][b];
				}
			}
		}
		printf("time skew %s %lld reads, %lld backwards, worst %lld ns\n",
			kNames[clock], (long long)total.reads,
			(long long)total.backwards, (long long)total.worst);
		int cpus = (int)info.cpu_count < kSkewCpus ? (int)info.cpu_count
			: kSkewCpus;
		for (int a = 0; a < cpus; a++) {
			char line[256];
			int used = snprintf(line, sizeof(line), "time skew %s from cpu %d:",
				kNames[clock], a);
			for (int b = 0; b < cpus && used < (int)sizeof(line) - 16; b++) {
				used += snprintf(line + used, sizeof(line) - used, " %lld",
					(long long)total.pair[a][b]);
			}
			printf("%s\n", line);
		}
		fflush(stdout);
	}
	return 0;
}


static int
run_load(int seconds)
{
	system_info info;
	get_system_info(&info);
	int threads = (int)info.cpu_count * 2;
	if (threads > 64)
		threads = 64;
	pthread_t thread[64];
	load_result results[64];
	memset(results, 0, sizeof(results));
	for (int i = 0; i < threads; i++)
		pthread_create(&thread[i], NULL, load_thread, &results[i]);

	// Agreement with the kernel while the load runs: the kernel's value
	// must lie between two user reads around the system call.
	int64_t worstAgree = 0;
	int64_t bracketMisses = 0;
	int64_t realMisses = 0;
	int64_t checks = 0;
	bigtime_t end = system_time() + (bigtime_t)seconds * 1000000;
	while (system_time() < end) {
		for (int i = 0; i < 1000; i++) {
			int64_t before = mono_ns();
			bigtime_t kernel;
			_kern_get_clock(CLOCK_MONOTONIC, &kernel);
			int64_t after = mono_ns();
			checks++;
			if (kernel < before / 1000 || kernel > after / 1000)
				bracketMisses++;
			int64_t spread = after - before;
			if (spread > worstAgree)
				worstAgree = spread;

			int64_t realBefore = real_ns();
			bigtime_t realKernel;
			_kern_get_clock(CLOCK_REALTIME, &realKernel);
			int64_t realAfter = real_ns();
			if (realKernel < realBefore / 1000
				|| realKernel > realAfter / 1000)
				realMisses++;
		}
		snooze(1000);
	}
	sStop = true;
	load_result total;
	memset(&total, 0, sizeof(total));
	for (int i = 0; i < threads; i++) {
		pthread_join(thread[i], NULL);
		total.reads += results[i].reads;
		total.ownBackwards += results[i].ownBackwards;
		total.crossBackwards += results[i].crossBackwards;
		total.migrations += results[i].migrations;
		total.subMicro += results[i].subMicro;
		if (results[i].worstCrossNs > total.worstCrossNs)
			total.worstCrossNs = results[i].worstCrossNs;
	}
	report(total.ownBackwards == 0, "load %d threads on %d cpus, %lld reads, "
		"%lld migrations, backwards in a thread %lld", threads,
		(int)info.cpu_count, (long long)total.reads,
		(long long)total.migrations, (long long)total.ownBackwards);
	report(total.crossBackwards == 0, "load across cpus backwards %lld, "
		"worst %lld ns", (long long)total.crossBackwards,
		(long long)total.worstCrossNs);
	report(total.migrations > 0, "load migrations seen %lld",
		(long long)total.migrations);
	report(total.subMicro > 0, "load nanosecond values %lld of %lld",
		(long long)total.subMicro, (long long)total.reads);
	report(bracketMisses == 0, "agree monotonic with the system call, "
		"%lld checks, %lld outside, widest bracket %lld ns",
		(long long)checks, (long long)bracketMisses, (long long)worstAgree);
	report(realMisses == 0, "agree realtime with the system call, "
		"%lld checks, %lld outside", (long long)checks,
		(long long)realMisses);

	// gettimeofday() and time() against CLOCK_REALTIME
	int64_t gtodMisses = 0;
	int64_t timeMisses = 0;
	for (int i = 0; i < 100000; i++) {
		int64_t before = real_ns();
		struct timeval tv;
		gettimeofday(&tv, NULL);
		time_t seconds = time(NULL);
		int64_t after = real_ns();
		int64_t usecs = (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
		if (usecs < before / 1000 || usecs > after / 1000)
			gtodMisses++;
		if (seconds < before / 1000000000LL || seconds > after / 1000000000LL)
			timeMisses++;
	}
	report(gtodMisses == 0 && timeMisses == 0, "agree gettimeofday %lld "
		"time %lld outside of 100000", (long long)gtodMisses,
		(long long)timeMisses);
	return 0;
}


// #pragma mark - rtc


static int
run_rtc()
{
	const int64_t kStep = 3600LL * 1000000000LL;
	int64_t monoBefore = mono_ns();
	int64_t realBefore = real_ns();
	bigtime_t target = (realBefore + kStep) / 1000;
	status_t status = _kern_set_real_time_clock(target);
	if (status != B_OK) {
		report(false, "rtc set_real_time_clock: %s (needs root)",
			strerror(status));
		return 1;
	}
	int64_t monoAfter = mono_ns();
	int64_t realAfter = real_ns();
	struct timeval tv;
	gettimeofday(&tv, NULL);
	time_t seconds = time(NULL);
	bigtime_t kernelReal;
	_kern_get_clock(CLOCK_REALTIME, &kernelReal);

	int64_t realJump = realAfter - realBefore;
	int64_t monoJump = monoAfter - monoBefore;
	// The jump is the step plus the time the calls took (well under 50 ms).
	report(realJump >= kStep && realJump - kStep < 50000000LL,
		"rtc realtime jumped %lld ms", (long long)(realJump / 1000000));
	report(monoJump >= 0 && monoJump < 50000000LL,
		"rtc monotonic moved %lld us", (long long)(monoJump / 1000));
	int64_t gtod = (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
	report(gtod >= realAfter / 1000 && gtod - realAfter / 1000 < 50000,
		"rtc gettimeofday follows (%lld us after)",
		(long long)(gtod - realAfter / 1000));
	report(seconds >= realAfter / 1000000000LL
		&& seconds - realAfter / 1000000000LL <= 1, "rtc time() follows");
	report(kernelReal >= realAfter / 1000
		&& kernelReal - realAfter / 1000 < 50000,
		"rtc kernel realtime agrees (%lld us)",
		(long long)(kernelReal - realAfter / 1000));

	// Back: the step is taken off again.
	int64_t now = real_ns();
	_kern_set_real_time_clock((now - kStep) / 1000);
	int64_t back = real_ns();
	report(now - back > kStep - 50000000LL && now - back <= kStep,
		"rtc set back, realtime moved %lld ms",
		(long long)((back - now) / 1000000));
	return 0;
}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s bench | load <seconds> | rtc\n", argv[0]);
		return 2;
	}
	if (strcmp(argv[1], "bench") == 0)
		run_bench();
	else if (strcmp(argv[1], "load") == 0)
		run_load(argc > 2 ? atoi(argv[2]) : 10);
	else if (strcmp(argv[1], "rtc") == 0)
		run_rtc();
	else if (strcmp(argv[1], "skew") == 0)
		run_skew(argc > 2 ? atoi(argv[2]) : 5);
	else
		return 2;
	printf("time done %s failures %d\n", argv[1], sFailures);
	return sFailures == 0 ? 0 : 1;
}
