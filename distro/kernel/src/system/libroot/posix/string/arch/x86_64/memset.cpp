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


static inline void
memset_repstos(uint8_t* destination, uint8_t value, size_t length)
{
	__asm__ __volatile__("rep stosb"
		: "+D" (destination), "+c" (length)
		: "a" (value)
		: "memory");
}


static inline void
memset_sse(uint8_t* destination, uint8_t value, size_t length)
{
	__m128i packed = _mm_set1_epi8(value);
	auto end = reinterpret_cast<__m128i*>(destination + length - 16);
	auto diff = reinterpret_cast<uintptr_t>(destination) % 16;
	if (diff) {
		diff = 16 - diff;
		length -= diff;
		_mm_storeu_si128(reinterpret_cast<__m128i*>(destination), packed);
	}
	auto ptr = reinterpret_cast<__m128i*>(destination + diff);
	while (length >= 64) {
		_mm_store_si128(ptr++, packed);
		_mm_store_si128(ptr++, packed);
		_mm_store_si128(ptr++, packed);
		_mm_store_si128(ptr++, packed);
		length -= 64;
	}
	while (length >= 16) {
		_mm_store_si128(ptr++, packed);
		length -= 16;
	}
	_mm_storeu_si128(end, packed);
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#ifndef _KERNEL_MODE
static inline void
memset_nt(uint8_t* destination, uint8_t value, size_t length)
{
	__m128i packed = _mm_set1_epi8(value);
	while (length != 0
		&& (reinterpret_cast<uintptr_t>(destination) & 15) != 0) {
		*destination++ = value;
		length--;
	}
	auto ptr = reinterpret_cast<__m128i*>(destination);
	while (length >= 64) {
		_mm_stream_si128(ptr++, packed);
		_mm_stream_si128(ptr++, packed);
		_mm_stream_si128(ptr++, packed);
		_mm_stream_si128(ptr++, packed);
		length -= 64;
	}
	while (length >= 16) {
		_mm_stream_si128(ptr++, packed);
		length -= 16;
	}
	_mm_sfence();
	destination = reinterpret_cast<uint8_t*>(ptr);
	while (length--)
		*destination++ = value;
}


static __attribute__((target("avx2"))) void
memset_nt_avx2(uint8_t* destination, uint8_t value, size_t length)
{
	__m256i packed = _mm256_set1_epi8(static_cast<char>(value));
	while (length != 0
		&& (reinterpret_cast<uintptr_t>(destination) & 31) != 0) {
		*destination++ = value;
		length--;
	}
	auto ptr = reinterpret_cast<__m256i*>(destination);
	while (length >= 128) {
		_mm256_stream_si256(ptr++, packed);
		_mm256_stream_si256(ptr++, packed);
		_mm256_stream_si256(ptr++, packed);
		_mm256_stream_si256(ptr++, packed);
		length -= 128;
	}
	while (length >= 32) {
		_mm256_stream_si256(ptr++, packed);
		length -= 32;
	}
	_mm_sfence();
	destination = reinterpret_cast<uint8_t*>(ptr);
	while (length--)
		*destination++ = value;
}


static __attribute__((target("avx2"))) void
memset_avx2(uint8_t* destination, uint8_t value, size_t length)
{
	__m256i packed = _mm256_set1_epi8(static_cast<char>(value));
	auto end = reinterpret_cast<__m256i*>(destination + length - 32);
	auto diff = reinterpret_cast<uintptr_t>(destination) & 31;
	if (diff) {
		diff = 32 - diff;
		length -= diff;
		_mm256_storeu_si256(reinterpret_cast<__m256i*>(destination), packed);
	}
	auto ptr = reinterpret_cast<__m256i*>(destination + diff);
	while (length >= 128) {
		_mm256_store_si256(ptr++, packed);
		_mm256_store_si256(ptr++, packed);
		_mm256_store_si256(ptr++, packed);
		_mm256_store_si256(ptr++, packed);
		length -= 128;
	}
	while (length >= 32) {
		_mm256_store_si256(ptr++, packed);
		length -= 32;
	}
	_mm256_storeu_si256(end, packed);
}
#endif
// SPDX-SnippetEnd


static inline void
memset_small(uint8_t* destination, uint8_t value, size_t length)
{
	if (length >= 8) {
		auto packed = value * 0x101010101010101ul;
		auto ptr = reinterpret_cast<uint64_t*>(destination);
		auto end = reinterpret_cast<uint64_t*>(destination + length - 8);
		while (length >= 8) {
			*ptr++ = packed;
			length -= 8;
		}
		*end = packed;
	} else if (length >= 4) {
		uint32_t packed = static_cast<uint32_t>(value) * 0x01010101u;
		__builtin_memcpy(destination, &packed, 4);
		__builtin_memcpy(destination + length - 4, &packed, 4);
	} else if (length >= 2) {
		uint16_t packed = static_cast<uint16_t>(value) * 0x0101u;
		__builtin_memcpy(destination, &packed, 2);
		__builtin_memcpy(destination + length - 2, &packed, 2);
	} else if (length == 1) {
		*destination = value;
	}
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#if defined(__HAIKU__) && defined(__x86_64__) && !defined(_KERNEL_MODE) \
	&& !defined(_BOOT_MODE) && !defined(R5_HOST_TEST)
static size_t sMemsetStreamingIfunc = (size_t)R5_CPU_LLC_FALLBACK / 4 * 3;
static size_t sMemsetRepFromIfunc = 2048;
static int sMemsetUseErmsIfunc = 0;

static __attribute__((target("avx2"))) void*
r5_memset_avx2(void* ptr, int chr, size_t length)
{
	auto value = static_cast<unsigned char>(chr);
	auto destination = static_cast<uint8_t*>(ptr);
	if (length < 32) {
		memset_small(destination, value, length);
		return ptr;
	}
	if (length >= sMemsetStreamingIfunc) {
		memset_nt_avx2(destination, value, length);
		return ptr;
	}
	if (!sMemsetUseErmsIfunc || length < sMemsetRepFromIfunc) {
		memset_avx2(destination, value, length);
		return ptr;
	}
	memset_repstos(destination, value, length);
	return ptr;
}

static void*
r5_memset_sse(void* ptr, int chr, size_t length)
{
	auto value = static_cast<unsigned char>(chr);
	auto destination = static_cast<uint8_t*>(ptr);
	if (length < 32) {
		memset_small(destination, value, length);
		return ptr;
	}
	if (length >= sMemsetStreamingIfunc) {
		memset_nt(destination, value, length);
		return ptr;
	}
	if (!sMemsetUseErmsIfunc || length < sMemsetRepFromIfunc) {
		memset_sse(destination, value, length);
		return ptr;
	}
	memset_repstos(destination, value, length);
	return ptr;
}

extern "C" {
static void* (*r5_memset_resolve(void))(void*, int, size_t)
{
	uint32_t flags = r5_x86_cpu_flags();
	sMemsetStreamingIfunc = (size_t)r5_x86_cpu_llc() / 4 * 3;
	sMemsetUseErmsIfunc = (flags & R5_CPU_ERMS) != 0 ? 1 : 0;
	sMemsetRepFromIfunc
		= ((flags & R5_CPU_ERMS) != 0 && (flags & R5_CPU_FSRM) != 0)
			? 128 : 2048;
	if ((flags & R5_CPU_AVX2) != 0)
		return r5_memset_avx2;
	return r5_memset_sse;
}

void* memset(void*, int, size_t) __attribute__((ifunc("r5_memset_resolve")));
}
#else
using MemsetBulk = void (*)(uint8_t*, uint8_t, size_t);

static MemsetBulk sMemsetBulk = memset_sse;
static size_t sMemsetStreaming;
static size_t sMemsetRepFrom = 2048;
static int sMemsetUseAvx2;
static int sMemsetChosen;


extern "C" void*
memset(void* ptr, int chr, size_t length)
{
	auto value = static_cast<unsigned char>(chr);
	auto destination = static_cast<uint8_t*>(ptr);
	if (length < 32) {
		memset_small(destination, value, length);
		return ptr;
	}

	MemsetBulk bulk;
	size_t streaming;
	size_t repFrom;
	int useAvx2;
	if (__atomic_load_n(&sMemsetChosen, __ATOMIC_ACQUIRE) == 0) {
		uint32_t flags = r5_x86_cpu_flags();
		streaming = (size_t)r5_x86_cpu_llc() / 4 * 3;
		bulk = (flags & R5_CPU_ERMS) != 0 ? memset_repstos : memset_sse;
		repFrom = ((flags & R5_CPU_ERMS) != 0 && (flags & R5_CPU_FSRM) != 0)
			? 128 : 2048;
		useAvx2 = (flags & R5_CPU_AVX2) != 0 ? 1 : 0;
		__atomic_store_n(&sMemsetStreaming, streaming, __ATOMIC_RELAXED);
		__atomic_store_n(&sMemsetRepFrom, repFrom, __ATOMIC_RELAXED);
		__atomic_store_n(&sMemsetUseAvx2, useAvx2, __ATOMIC_RELAXED);
		__atomic_store_n(&sMemsetBulk, bulk, __ATOMIC_RELAXED);
		__atomic_store_n(&sMemsetChosen, 1, __ATOMIC_RELEASE);
	} else {
		streaming = __atomic_load_n(&sMemsetStreaming, __ATOMIC_RELAXED);
		repFrom = __atomic_load_n(&sMemsetRepFrom, __ATOMIC_RELAXED);
		useAvx2 = __atomic_load_n(&sMemsetUseAvx2, __ATOMIC_RELAXED);
		bulk = __atomic_load_n(&sMemsetBulk, __ATOMIC_RELAXED);
	}

#ifndef _KERNEL_MODE
	if (length >= streaming) {
		if (useAvx2)
			memset_nt_avx2(destination, value, length);
		else
			memset_nt(destination, value, length);
		return ptr;
	}
#endif
	if (bulk != memset_repstos || length < repFrom) {
#ifndef _KERNEL_MODE
		if (useAvx2)
			memset_avx2(destination, value, length);
		else
#endif
			memset_sse(destination, value, length);
		return ptr;
	}

	memset_repstos(destination, value, length);
	return ptr;
}
#endif
// SPDX-SnippetEnd
