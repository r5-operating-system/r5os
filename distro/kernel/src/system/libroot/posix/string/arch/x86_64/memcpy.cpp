/*
 * Copyright 2014, Paweł Dziepak, pdziepak@quarnos.org.
 * Distributed under the terms of the MIT License.
 */


#include <array>

#include <cstddef>
#include <cstdint>

#include <emmintrin.h>
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#ifndef _KERNEL_MODE
#include <immintrin.h>
#endif
// SPDX-SnippetEnd

#include <r5_x86_cpu.h>


namespace {


static const uintptr_t kPageSize = 4096;


// __m128i resolves to a type with an attribute, which can't get into the
// template signature, resulting in a warning. Nonetheless the code is what we
// expect, so we silent the warning.
#pragma GCC diagnostic push
#if defined __GNUC__ && __GNUC__ >= 6
#pragma GCC diagnostic ignored "-Wignored-attributes"
#endif


template<template<size_t N> class Generator, unsigned N, unsigned ...Index>
struct GenerateTable : GenerateTable<Generator, N - 1,  N - 1, Index...> {
};

template<template<size_t N> class Generator, unsigned ...Index>
struct GenerateTable<Generator, 0, Index...>
	: std::array<decltype(Generator<0>::sValue), sizeof...(Index)> {
	constexpr GenerateTable()
	:
	std::array<decltype(Generator<0>::sValue), sizeof...(Index)> {
		{ Generator<Index>::sValue... }
	}
	{
	}
};


#pragma GCC diagnostic pop


static inline void
memcpy_repmovs(uint8_t* destination, const uint8_t* source, size_t length)
{
	__asm__ __volatile__("rep movsb"
		: "+D" (destination), "+S" (source), "+c" (length)
		:
		: "memory");
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
static inline void
memcpy_small(uint8_t* destination, const uint8_t* source, size_t length)
{
	if (length >= 8) {
		uint64_t a, b;
		__builtin_memcpy(&a, source, 8);
		__builtin_memcpy(&b, source + length - 8, 8);
		__builtin_memcpy(destination, &a, 8);
		__builtin_memcpy(destination + length - 8, &b, 8);
	} else if (length >= 4) {
		uint32_t a, b;
		__builtin_memcpy(&a, source, 4);
		__builtin_memcpy(&b, source + length - 4, 4);
		__builtin_memcpy(destination, &a, 4);
		__builtin_memcpy(destination + length - 4, &b, 4);
	} else if (length >= 2) {
		uint16_t a, b;
		__builtin_memcpy(&a, source, 2);
		__builtin_memcpy(&b, source + length - 2, 2);
		__builtin_memcpy(destination, &a, 2);
		__builtin_memcpy(destination + length - 2, &b, 2);
	} else if (length == 1) {
		*destination = *source;
	}
}
// SPDX-SnippetEnd


template<size_t N>
inline void
copy_sse(__m128i* destination, const __m128i* source)
{
	auto temp = _mm_loadu_si128(source);
	_mm_storeu_si128(destination, temp);
	copy_sse<N - 1>(destination + 1, source + 1);
}


template<>
inline void
copy_sse<0>(__m128i* destination, const __m128i* source)
{
}


#ifndef _KERNEL_MODE
static inline void
memcpy_nt(uint8_t* destination, const uint8_t* source, size_t length)
{
	while (length != 0
		&& (reinterpret_cast<uintptr_t>(destination) & 15) != 0) {
		*destination++ = *source++;
		length--;
	}
	auto to = reinterpret_cast<__m128i*>(destination);
	auto from = reinterpret_cast<const __m128i*>(source);
	while (length >= 64) {
		__m128i a = _mm_loadu_si128(from);
		__m128i b = _mm_loadu_si128(from + 1);
		__m128i c = _mm_loadu_si128(from + 2);
		__m128i d = _mm_loadu_si128(from + 3);
		_mm_stream_si128(to, a);
		_mm_stream_si128(to + 1, b);
		_mm_stream_si128(to + 2, c);
		_mm_stream_si128(to + 3, d);
		to += 4;
		from += 4;
		length -= 64;
	}
	while (length >= 16) {
		_mm_stream_si128(to, _mm_loadu_si128(from));
		to++;
		from++;
		length -= 16;
	}
	_mm_sfence();
	destination = reinterpret_cast<uint8_t*>(to);
	source = reinterpret_cast<const uint8_t*>(from);
	while (length--)
		*destination++ = *source++;
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
static __attribute__((target("avx2"))) void
memcpy_nt_avx2(uint8_t* destination, const uint8_t* source, size_t length)
{
	while (length != 0
		&& (reinterpret_cast<uintptr_t>(destination) & 31) != 0) {
		*destination++ = *source++;
		length--;
	}
	auto to = reinterpret_cast<__m256i*>(destination);
	auto from = reinterpret_cast<const __m256i*>(source);
	while (length >= 128) {
		__m256i a = _mm256_loadu_si256(from);
		__m256i b = _mm256_loadu_si256(from + 1);
		__m256i c = _mm256_loadu_si256(from + 2);
		__m256i d = _mm256_loadu_si256(from + 3);
		_mm256_stream_si256(to, a);
		_mm256_stream_si256(to + 1, b);
		_mm256_stream_si256(to + 2, c);
		_mm256_stream_si256(to + 3, d);
		to += 4;
		from += 4;
		length -= 128;
	}
	while (length >= 32) {
		_mm256_stream_si256(to, _mm256_loadu_si256(from));
		to++;
		from++;
		length -= 32;
	}
	_mm_sfence();
	destination = reinterpret_cast<uint8_t*>(to);
	source = reinterpret_cast<const uint8_t*>(from);
	if (length > 0)
		memcpy_small(destination, source, length);
}


static __attribute__((target("avx2"))) void
memcpy_avx2_core(uint8_t* destination, const uint8_t* source, size_t length)
{
	if (length <= 32) {
		__m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(source));
		__m128i b = _mm_loadu_si128(
			reinterpret_cast<const __m128i*>(source + length - 16));
		_mm_storeu_si128(reinterpret_cast<__m128i*>(destination), a);
		_mm_storeu_si128(
			reinterpret_cast<__m128i*>(destination + length - 16), b);
		return;
	}
	if (length <= 64) {
		__m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(source));
		__m256i b = _mm256_loadu_si256(
			reinterpret_cast<const __m256i*>(source + length - 32));
		_mm256_storeu_si256(reinterpret_cast<__m256i*>(destination), a);
		_mm256_storeu_si256(
			reinterpret_cast<__m256i*>(destination + length - 32), b);
		return;
	}
	auto to = reinterpret_cast<__m256i*>(destination);
	auto from = reinterpret_cast<const __m256i*>(source);
	auto toEnd = reinterpret_cast<__m256i*>(destination + length - 32);
	auto fromEnd = reinterpret_cast<const __m256i*>(source + length - 32);
	while (length >= 128) {
		__m256i a = _mm256_loadu_si256(from);
		__m256i b = _mm256_loadu_si256(from + 1);
		__m256i c = _mm256_loadu_si256(from + 2);
		__m256i d = _mm256_loadu_si256(from + 3);
		_mm256_storeu_si256(to, a);
		_mm256_storeu_si256(to + 1, b);
		_mm256_storeu_si256(to + 2, c);
		_mm256_storeu_si256(to + 3, d);
		to += 4;
		from += 4;
		length -= 128;
	}
	while (length >= 32) {
		_mm256_storeu_si256(to++, _mm256_loadu_si256(from++));
		length -= 32;
	}
	if (length)
		_mm256_storeu_si256(toEnd, _mm256_loadu_si256(fromEnd));
}
// SPDX-SnippetEnd
#endif


static inline void
memcpy_sse(uint8_t* destination, const uint8_t* source, size_t length)
{
	auto to = reinterpret_cast<__m128i*>(destination);
	auto from = reinterpret_cast<const __m128i*>(source);
	auto toEnd = reinterpret_cast<__m128i*>(destination + length - 16);
	auto fromEnd = reinterpret_cast<const __m128i*>(source + length - 16);
	while (length >= 64) {
		copy_sse<4>(to, from);
		to += 4;
		from += 4;
		length -= 64;
	}
	while (length >= 16) {
		copy_sse<1>(to++, from++);
		length -= 16;
	}
	if (length) {
		copy_sse<1>(toEnd, fromEnd);
	}
}


} // namespace


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#if defined(__R5_USE_IFUNC__) && defined(__HAIKU__) && defined(__x86_64__) \
	&& !defined(_KERNEL_MODE) && !defined(_BOOT_MODE) && !defined(R5_HOST_TEST)
static size_t sMemcpyStreamingIfunc = (size_t)R5_CPU_LLC_FALLBACK / 4 * 3;
static size_t sMemcpyRepFromIfunc = 2048;
static int sMemcpyUseErmsIfunc = 0;

static inline bool
memcpy_is_4k_aliased(const uint8_t* to, const uint8_t* from)
{
	uintptr_t distance = (reinterpret_cast<uintptr_t>(to)
		- reinterpret_cast<uintptr_t>(from)) & (kPageSize - 1);
	return distance != 0 && (distance < 32 || distance > kPageSize - 16);
}

static __attribute__((target("avx2"))) void*
r5_memcpy_avx2(void* destination, const void* source, size_t length)
{
	auto to = static_cast<uint8_t*>(destination);
	auto from = static_cast<const uint8_t*>(source);
	if (length <= 16) {
		memcpy_small(to, from, length);
		return destination;
	}
	if (length >= sMemcpyStreamingIfunc) {
		memcpy_nt_avx2(to, from, length);
		return destination;
	}
	if (!sMemcpyUseErmsIfunc || length < sMemcpyRepFromIfunc
		|| memcpy_is_4k_aliased(to, from)) {
		memcpy_avx2_core(to, from, length);
		return destination;
	}
	memcpy_repmovs(to, from, length);
	return destination;
}

static void*
r5_memcpy_sse(void* destination, const void* source, size_t length)
{
	auto to = static_cast<uint8_t*>(destination);
	auto from = static_cast<const uint8_t*>(source);
	if (length <= 16) {
		memcpy_small(to, from, length);
		return destination;
	}
	if (length >= sMemcpyStreamingIfunc) {
		memcpy_nt(to, from, length);
		return destination;
	}
	if (!sMemcpyUseErmsIfunc || length < sMemcpyRepFromIfunc
		|| memcpy_is_4k_aliased(to, from)) {
		memcpy_sse(to, from, length);
		return destination;
	}
	memcpy_repmovs(to, from, length);
	return destination;
}

extern "C" {
static void* (*r5_memcpy_resolve(void))(void*, const void*, size_t)
{
	uint32_t flags = r5_x86_cpu_flags();
	sMemcpyStreamingIfunc = (size_t)r5_x86_cpu_llc() / 4 * 3;
	sMemcpyUseErmsIfunc = (flags & R5_CPU_ERMS) != 0 ? 1 : 0;
	sMemcpyRepFromIfunc
		= ((flags & R5_CPU_ERMS) != 0 && (flags & R5_CPU_FSRM) != 0)
			? 128 : 2048;
	if ((flags & R5_CPU_AVX2) != 0)
		return r5_memcpy_avx2;
	return r5_memcpy_sse;
}

void* memcpy(void*, const void*, size_t) __attribute__((ifunc("r5_memcpy_resolve")));
}
#else
using MemcpyBulk = void (*)(uint8_t*, const uint8_t*, size_t);

static MemcpyBulk sMemcpyBulk = memcpy_sse;
static size_t sMemcpyStreaming;
static size_t sMemcpyRepFrom = 2048;
static int sMemcpyUseAvx2;
static int sMemcpyChosen;


extern "C" void*
memcpy(void* destination, const void* source, size_t length)
{
	auto to = static_cast<uint8_t*>(destination);
	auto from = static_cast<const uint8_t*>(source);
	if (length <= 16) {
		memcpy_small(to, from, length);
		return destination;
	}

	MemcpyBulk bulk;
	size_t streaming;
	size_t repFrom;
	int useAvx2;
	if (__atomic_load_n(&sMemcpyChosen, __ATOMIC_ACQUIRE) == 0) {
		uint32_t flags = r5_x86_cpu_flags();
		streaming = (size_t)r5_x86_cpu_llc() / 4 * 3;
		bulk = (flags & R5_CPU_ERMS) != 0 ? memcpy_repmovs : memcpy_sse;
		repFrom = ((flags & R5_CPU_ERMS) != 0 && (flags & R5_CPU_FSRM) != 0)
			? 128 : 2048;
		useAvx2 = (flags & R5_CPU_AVX2) != 0 ? 1 : 0;
		__atomic_store_n(&sMemcpyStreaming, streaming, __ATOMIC_RELAXED);
		__atomic_store_n(&sMemcpyRepFrom, repFrom, __ATOMIC_RELAXED);
		__atomic_store_n(&sMemcpyUseAvx2, useAvx2, __ATOMIC_RELAXED);
		__atomic_store_n(&sMemcpyBulk, bulk, __ATOMIC_RELAXED);
		__atomic_store_n(&sMemcpyChosen, 1, __ATOMIC_RELEASE);
	} else {
		streaming = __atomic_load_n(&sMemcpyStreaming, __ATOMIC_RELAXED);
		repFrom = __atomic_load_n(&sMemcpyRepFrom, __ATOMIC_RELAXED);
		useAvx2 = __atomic_load_n(&sMemcpyUseAvx2, __ATOMIC_RELAXED);
		bulk = __atomic_load_n(&sMemcpyBulk, __ATOMIC_RELAXED);
	}

#ifndef _KERNEL_MODE
	if (length >= streaming) {
		if (useAvx2)
			memcpy_nt_avx2(to, from, length);
		else
			memcpy_nt(to, from, length);
		return destination;
	}
#endif

	uintptr_t distance = (reinterpret_cast<uintptr_t>(to)
		- reinterpret_cast<uintptr_t>(from)) & (kPageSize - 1);
	static const uintptr_t kAliasWindow = 32;
	bool aliased = distance != 0
		&& (distance < kAliasWindow || distance > kPageSize - 16);

	if (bulk != memcpy_repmovs || length < repFrom || aliased) {
#ifndef _KERNEL_MODE
		if (useAvx2)
			memcpy_avx2_core(to, from, length);
		else
#endif
			memcpy_sse(to, from, length);
		return destination;
	}

	memcpy_repmovs(to, from, length);
	return destination;
}
#endif
// SPDX-SnippetEnd
