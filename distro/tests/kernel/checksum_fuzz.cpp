// SPDX-License-Identifier: MIT
// Copyright 2026 R5OS Contributors
//
// Host fuzz test for Step 2: compares the original 16-bit Haiku network stack
// compute_checksum() with the R5 unrolled 64-bit add-with-carry implementation
// across random buffers, odd lengths, and odd/unaligned start addresses for 60 s.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

static uint16
compute_checksum_old(uint8* _buffer, size_t length)
{
	uint16* buffer = (uint16*)_buffer;
	uint32 sum = 0;

	while (length >= 2) {
		uint16 word;
		std::memcpy(&word, buffer, sizeof(word));
		sum += word;
		buffer++;
		length -= 2;
	}

	if (length) {
		sum += *(uint8*)buffer;
	}

	while (sum >> 16) {
		sum = (sum & 0xffff) + (sum >> 16);
	}

	return sum;
}

static inline uint32
fold_checksum_64_to_32(uint64 sum)
{
	sum = (sum & 0xffffffffULL) + (sum >> 32);
	sum = (sum & 0xffffffffULL) + (sum >> 32);
	return (uint32)sum;
}

static inline uint16
fold_checksum_32_to_16(uint32 sum)
{
	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);
	return (uint16)sum;
}

static uint16
compute_checksum_new(uint8* _buffer, size_t length)
{
	if (length == 0)
		return 0;

	const uint8* buf = _buffer;
	bool oddStart = ((uintptr_t)buf & 1) != 0;
	uint64 sum64 = 0;

	if (oddStart) {
		sum64 = (uint64)(*buf++) << 8;
		length--;
	}

	while (length >= 64) {
		uint64 carry = 0;
		asm(
			"addq 0*8(%[p]), %[sum]\n\t"
			"adcq 1*8(%[p]), %[sum]\n\t"
			"adcq 2*8(%[p]), %[sum]\n\t"
			"adcq 3*8(%[p]), %[sum]\n\t"
			"adcq 4*8(%[p]), %[sum]\n\t"
			"adcq 5*8(%[p]), %[sum]\n\t"
			"adcq 6*8(%[p]), %[sum]\n\t"
			"adcq 7*8(%[p]), %[sum]\n\t"
			"adcq $0, %[carry]\n\t"
			: [sum] "+r"(sum64), [carry] "+r"(carry)
			: [p] "r"(buf)
			: "cc", "memory");
		sum64 += carry;
		if (sum64 < carry)
			sum64++;
		buf += 64;
		length -= 64;
	}

	while (length >= 8) {
		uint64 word;
		__builtin_memcpy(&word, buf, sizeof(word));
		sum64 += word;
		if (sum64 < word)
			sum64++;
		buf += 8;
		length -= 8;
	}

	uint64 tail = 0;
	if (length >= 4) {
		uint32 word32;
		__builtin_memcpy(&word32, buf, sizeof(word32));
		tail += word32;
		buf += 4;
		length -= 4;
	}
	if (length >= 2) {
		uint16 word16;
		__builtin_memcpy(&word16, buf, sizeof(word16));
		tail += word16;
		buf += 2;
		length -= 2;
	}
	if (length != 0)
		tail += *buf;

	sum64 += tail;
	if (sum64 < tail)
		sum64++;

	uint16 result = fold_checksum_32_to_16(fold_checksum_64_to_32(sum64));
	if (oddStart)
		result = (uint16)((result << 8) | (result >> 8));
	return result;
}

static uint64_t
xorshift64(uint64_t& state)
{
	uint64_t x = state;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	state = x;
	return x;
}

int
main(int argc, char** argv)
{
	int durationSeconds = 60;
	if (argc > 1)
		durationSeconds = std::atoi(argv[1]);

	constexpr size_t kBufCap = 65536 + 64;
	std::vector<uint8> arena(kBufCap, 0);
	uint64_t rng = 0x9e3779b97f4a7c15ULL;

	// First: deterministic sweep over all lengths 0..512 and alignments 0..15
	// with all-0xFF, all-0x00, alternating, and random data.
	for (int pattern = 0; pattern < 4; pattern++) {
		for (size_t i = 0; i < 1024; i++) {
			if (pattern == 0)
				arena[i] = 0xff;
			else if (pattern == 1)
				arena[i] = 0x00;
			else if (pattern == 2)
				arena[i] = (i & 1) ? 0xaa : 0x55;
			else
				arena[i] = (uint8)xorshift64(rng);
		}
		for (size_t align = 0; align < 16; align++) {
			for (size_t len = 0; len <= 512; len++) {
				uint16 oldSum = compute_checksum_old(arena.data() + align, len);
				uint16 newSum = compute_checksum_new(arena.data() + align, len);
				if (oldSum != newSum) {
					std::fprintf(stderr,
						"MISMATCH deterministic pattern=%d align=%zu len=%zu: old=0x%04x new=0x%04x\n",
						pattern, align, len, oldSum, newSum);
					return 1;
				}
			}
		}
	}

	auto start = std::chrono::steady_clock::now();
	auto deadline = start + std::chrono::seconds(durationSeconds);
	uint64_t iterations = 0;
	uint64_t oddLengths = 0;
	uint64_t oddAddresses = 0;

	while (std::chrono::steady_clock::now() < deadline) {
		// Refill a slice of the arena with fresh random words
		uint64_t* words = reinterpret_cast<uint64_t*>(arena.data());
		size_t maxLen = (xorshift64(rng) % 4 == 0)
			? (xorshift64(rng) % 65536)
			: (xorshift64(rng) % 2048);
		size_t offset = xorshift64(rng) % 31;
		size_t wordsToFill = (offset + maxLen + 7) / 8;
		for (size_t i = 0; i < wordsToFill; i++)
			words[i] = xorshift64(rng);

		// Also inject occasional all-ones runs to stress carry propagation
		if ((iterations & 0x3f) == 0 && wordsToFill > 4) {
			for (size_t i = 0; i < wordsToFill; i += 2)
				words[i] = ~0ULL;
		}

		for (int sub = 0; sub < 32; sub++) {
			size_t align = xorshift64(rng) & 15;
			size_t len = xorshift64(rng) % (maxLen + 1);
			uint8* ptr = arena.data() + align;
			if (((uintptr_t)ptr & 1) != 0)
				oddAddresses++;
			if ((len & 1) != 0)
				oddLengths++;

			uint16 oldSum = compute_checksum_old(ptr, len);
			uint16 newSum = compute_checksum_new(ptr, len);
			if (oldSum != newSum) {
				std::fprintf(stderr,
					"MISMATCH iter=%llu align=%zu len=%zu: old=0x%04x new=0x%04x\n",
					(unsigned long long)iterations, align, len, oldSum, newSum);
				return 1;
			}
			iterations++;
		}
	}

	auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::steady_clock::now() - start).count();
	std::printf(
		"PASS: compute_checksum fuzz %llds, iterations=%llu (odd_len=%llu, odd_addr=%llu), 0 mismatches\n",
		(long long)elapsed,
		(unsigned long long)iterations,
		(unsigned long long)oddLengths,
		(unsigned long long)oddAddresses);
	return 0;
}
