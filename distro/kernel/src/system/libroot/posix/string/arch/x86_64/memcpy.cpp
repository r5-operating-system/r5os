/*
 * Copyright 2014, Paweł Dziepak, pdziepak@quarnos.org.
 * Distributed under the terms of the MIT License.
 */


#include <array>

#include <cstddef>
#include <cstdint>

#include <emmintrin.h>

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


template<size_t N>
inline void copy_small(uint8_t* destination, const uint8_t* source)
{
	struct data {
		uint8_t x[N];
	};
	*reinterpret_cast<data*>(destination)
		= *reinterpret_cast<const data*>(source);
}


template<size_t N>
struct SmallGenerator {
	constexpr static void (*sValue)(uint8_t*, const uint8_t*) = copy_small<N>;
};
constexpr static GenerateTable<SmallGenerator, 8> table_small;


static inline void
memcpy_small(uint8_t* destination, const uint8_t* source, size_t length)
{
	if (length < 8) {
		table_small[length](destination, source);
	} else {
		auto to = reinterpret_cast<uint64_t*>(destination);
		auto from = reinterpret_cast<const uint64_t*>(source);
		*to = *from;
		to = reinterpret_cast<uint64_t*>(destination + length - 8);
		from = reinterpret_cast<const uint64_t*>(source + length - 8);
		*to = *from;
	}
}


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


template<size_t N>
struct SSEGenerator {
	constexpr static void (*sValue)(__m128i*, const __m128i*) = copy_sse<N>;
};
constexpr static GenerateTable<SSEGenerator, 4> table_sse;


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
	if (length >= 16) {
		table_sse[length / 16](to, from);
		length %= 16;
	}
	if (length) {
		copy_sse<1>(toEnd, fromEnd);
	}
}


} // namespace


using MemcpyBulk = void (*)(uint8_t*, const uint8_t*, size_t);

static MemcpyBulk sMemcpyBulk = memcpy_sse;
static size_t sMemcpyStreaming;
static size_t sMemcpyRepFrom = 2048;


static void
memcpy_choose_bulk(void)
{
	uint32_t flags = r5_x86_cpu_flags();
	sMemcpyStreaming = (size_t)r5_x86_cpu_llc() / 4 * 3;
	if ((flags & R5_CPU_ERMS) != 0) {
		sMemcpyBulk = memcpy_repmovs;
		if ((flags & R5_CPU_FSRM) != 0)
			sMemcpyRepFrom = 128;
	}
}


extern "C" void*
memcpy(void* destination, const void* source, size_t length)
{
	auto to = static_cast<uint8_t*>(destination);
	auto from = static_cast<const uint8_t*>(source);
	if (length <= 16) {
		memcpy_small(to, from, length);
		return destination;
	}

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	static int chosen;
	if (__atomic_load_n(&chosen, __ATOMIC_ACQUIRE) == 0) {
		memcpy_choose_bulk();
		__atomic_store_n(&chosen, 1, __ATOMIC_RELEASE);
	}

	if (length >= sMemcpyStreaming) {
		memcpy_nt(to, from, length);
		return destination;
	}

	// FSRM makes a short rep movsb fast. Without it the old cut stays.
	// Both cuts were stored by memcpy_choose_bulk.
	if (sMemcpyBulk != memcpy_repmovs || length < sMemcpyRepFrom) {
		memcpy_sse(to, from, length);
		return destination;
	}
// SPDX-SnippetEnd

	// rep movsb slows down about 14 times when the destination is less than
	// 16 bytes from the source, modulo the page size (4K aliasing; a
	// Threadripper 1950X copies at 2.4 instead of 34 GB/s). The SSE loop
	// keeps its speed there.
	uintptr_t distance = (reinterpret_cast<uintptr_t>(to)
		- reinterpret_cast<uintptr_t>(from)) & (kPageSize - 1);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// On Zen 3 (Ryzen 5 5625U) the slow window reaches 31 bytes: 16 KiB at a
	// distance of 16 to 31 copied at 3.6-3.8 GB/s with rep movsb, against
	// 54 GB/s with the SSE loop; from 32 on rep movsb is fast again.
	static const uintptr_t kAliasWindow = 32;
	if (distance != 0 && (distance < kAliasWindow
			|| distance > kPageSize - 16)) {
// SPDX-SnippetEnd
		memcpy_sse(to, from, length);
		return destination;
	}

	sMemcpyBulk(to, from, length);
	return destination;
}
