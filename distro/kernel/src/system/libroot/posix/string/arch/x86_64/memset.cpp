/*
 * Copyright 2014, Paweł Dziepak, pdziepak@quarnos.org.
 * Distributed under the terms of the MIT License.
 */


#include <array>

#include <cstddef>
#include <cstdint>

#include <emmintrin.h>

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
	} else {
		while (length--) {
			*destination++ = value;
		}
	}
}


using MemsetBulk = void (*)(uint8_t*, uint8_t, size_t);

static MemsetBulk sMemsetBulk = memset_sse;
static size_t sMemsetStreaming;
static const size_t sMemsetRepFrom = 2048;


static void
memset_choose_bulk(void)
{
	sMemsetStreaming = (size_t)r5_x86_cpu_llc() / 4 * 3;
	if ((r5_x86_cpu_flags() & R5_CPU_ERMS) != 0)
		sMemsetBulk = memset_repstos;
}


extern "C" void*
memset(void* ptr, int chr, size_t length)
{
	auto value = static_cast<unsigned char>(chr);
	auto destination = static_cast<uint8_t*>(ptr);
	if (length < 32) {
		memset_small(destination, value, length);
		return ptr;
	}

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	static int chosen;
	if (__atomic_load_n(&chosen, __ATOMIC_ACQUIRE) == 0) {
		memset_choose_bulk();
		__atomic_store_n(&chosen, 1, __ATOMIC_RELEASE);
	}

	if (length >= sMemsetStreaming) {
		memset_nt(destination, value, length);
		return ptr;
	}
	if (sMemsetBulk != memset_repstos || length < sMemsetRepFrom) {
		memset_sse(destination, value, length);
		return ptr;
	}
// SPDX-SnippetEnd

	sMemsetBulk(destination, value, length);
	return ptr;
}
