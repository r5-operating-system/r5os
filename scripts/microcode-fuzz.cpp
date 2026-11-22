/*
 * Copyright 2026, R5OS Contributors.
 * SPDX-License-Identifier: MIT
 *
 * Host harness for the kernel microcode parsers find_microcode_intel() and
 * find_microcode_amd(). fuzz-microcode.sh copies both functions out of
 * distro/kernel/src/system/kernel/arch/x86/arch_cpu.cpp unchanged and builds
 * this file with AddressSanitizer.
 *
 * Usage: microcode-fuzz check <files...>
 *        microcode-fuzz intel|amd <seconds> <seed files...>
 * check: every patch in every file is found for its own signature.
 * intel, amd: mutated seeds and random data; a found patch must lie inside
 * the buffer, and nothing may read outside it (ASan).
 */


#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;
typedef uintptr_t addr_t;

#define B_PRIu32 PRIu32
#define B_PRIx32 PRIx32
#define IA32_MSR_PLATFORM_ID 0x17


// Same layout as headers/private/kernel/arch/x86/arch_cpu.h.
struct intel_microcode_header {
	uint32 header_version;
	uint32 update_revision;
	uint32 date;
	uint32 processor_signature;
	uint32 checksum;
	uint32 loader_revision;
	uint32 processor_flags;
	uint32 data_size;
	uint32 total_size;
	uint32 reserved[3];
};

struct intel_microcode_extended_signature_header {
	uint32 extended_signature_count;
	uint32 extended_checksum;
	uint32 reserved[3];
};

struct intel_microcode_extended_signature {
	uint32 processor_signature;
	uint32 processor_flags;
	uint32 checksum;
};

struct amd_container_header {
	uint32 magic;
};

struct amd_section_header {
	uint32 type;
	uint32 size;
};

struct amd_equiv_cpu_entry {
	uint32 installed_cpu;
	uint32 fixed_errata_mask;
	uint32 fixed_errata_compare;
	uint16 equiv_cpu;
	uint16 res;
};

struct amd_microcode_header {
	uint32 data_code;
	uint32 patch_id;
	uint16 mc_patch_data_id;
	uint8 mc_patch_data_len;
	uint8 init_flag;
	uint32 mc_patch_data_checksum;
	uint32 nb_dev_id;
	uint32 sb_dev_id;
	uint16 processor_rev_id;
	uint8 nb_rev_id;
	uint8 sb_rev_id;
	uint8 bios_api_rev;
	uint8 reserved1[3];
	uint32 match_reg[8];
};

struct cpuid_info {
	struct {
		uint32 eax, ebx, edx, ecx;
	} regs;
};


static uint32 sSignature;
static uint64 sPlatformMsr;


static void
get_current_cpuid(cpuid_info* info, uint32, uint32)
{
	memset(info, 0, sizeof(*info));
	info->regs.eax = sSignature;
}


static uint64
x86_read_msr(uint32)
{
	return sPlatformMsr;
}


static void
dprintf(const char*, ...)
{
}


#include "microcode-parsers.inc"


static uint64_t gState = 0x9e3779b97f4a7c15ULL;


static uint32_t
next_word()
{
	gState ^= gState << 7;
	gState ^= gState >> 9;
	gState ^= gState << 8;
	return (uint32_t)gState;
}


struct Seed {
	uint8* data;
	size_t size;
};


static Seed
read_file(const char* path)
{
	Seed seed = { NULL, 0 };
	FILE* file = fopen(path, "rb");
	if (file == NULL)
		return seed;
	fseek(file, 0, SEEK_END);
	long length = ftell(file);
	fseek(file, 0, SEEK_SET);
	if (length > 0) {
		seed.data = (uint8*)malloc(length);
		seed.size = fread(seed.data, 1, length, file);
	}
	fclose(file);
	return seed;
}


// A copy with exactly size bytes after a start that is offset bytes past a
// 16-byte boundary, so ASan sees any read past the end.
static uint8*
place(const uint8* bytes, size_t size, size_t offset, uint8** block)
{
	*block = (uint8*)aligned_alloc(16, (offset + size + 15) & ~(size_t)15);
	uint8* start = *block + offset;
	memcpy(start, bytes, size);
	return start;
}


// A found Intel patch is inside the buffer, header and all.
static void
check_inside(const void* result, const uint8* start, size_t size)
{
	const uint8* at = (const uint8*)result;
	if (at < start || at + sizeof(intel_microcode_header) > start + size) {
		fprintf(stderr, "result outside the buffer\n");
		abort();
	}
}


static int
check_intel(const char* path, Seed seed)
{
	int failures = 0;
	size_t offset = 0;
	while (offset + sizeof(intel_microcode_header) <= seed.size) {
		intel_microcode_header header;
		memcpy(&header, seed.data + offset, sizeof(header));
		uint32 total = header.total_size != 0 ? header.total_size : 2048;
		sSignature = header.processor_signature;
		uint32 flags = header.processor_flags;
		int bit = 0;
		while (bit < 8 && (flags & (1u << bit)) == 0)
			bit++;
		sPlatformMsr = (uint64)bit << 50;
		uint8* block;
		uint8* start = place(seed.data, seed.size, 0, &block);
		intel_microcode_header* found
			= find_microcode_intel((addr_t)start, seed.size, 0);
		if (found == NULL || found->processor_signature != sSignature) {
			printf("FAIL %s patch at %zu signature %08x\n", path, offset,
				sSignature);
			failures++;
		}
		free(block);
		offset += total;
	}
	return failures;
}


static int
check_amd(const char* path, Seed seed)
{
	int failures = 0;
	if (seed.size < 12)
		return 1;
	uint32 tableSize;
	memcpy(&tableSize, seed.data + 8, 4);
	for (uint32 i = 0; (i + 1) * sizeof(amd_equiv_cpu_entry) <= tableSize;
			i++) {
		amd_equiv_cpu_entry entry;
		memcpy(&entry, seed.data + 12 + i * sizeof(entry), sizeof(entry));
		if (entry.installed_cpu == 0)
			break;
		sSignature = entry.installed_cpu;
		uint8* block;
		uint8* start = place(seed.data, seed.size, 0, &block);
		amd_microcode_header* found
			= find_microcode_amd((addr_t)start, seed.size, 0);
		if (found == NULL || ((addr_t)found % 16) != 0
			|| found->processor_rev_id != entry.equiv_cpu) {
			printf("FAIL %s cpu %08x\n", path, entry.installed_cpu);
			failures++;
		}
		free(block);
	}
	return failures;
}


static void
mutate(uint8* bytes, size_t size)
{
	if (size == 0)
		return;
	int edits = 1 + next_word() % 8;
	for (int i = 0; i < edits; i++) {
		size_t at = next_word() % size;
		switch (next_word() % 4) {
			case 0:
				bytes[at] = (uint8)next_word();
				break;
			case 1:
				if (at + 4 <= size) {
					uint32 value = next_word() % 4 == 0 ? 0 : next_word();
					memcpy(bytes + at, &value, 4);
				}
				break;
			case 2:
				// Size fields: Intel total and data size, AMD section sizes.
				if (at + 4 <= size) {
					static const uint32 kValues[] = { 0, 1, 4, 16, 48, 2000,
						2048, 0x7fffffff, 0xfffffff0, 0xffffffff };
					uint32 value = kValues[next_word() % 10];
					size_t field = at & ~(size_t)3;
					if (field + 4 <= size)
						memcpy(bytes + field, &value, 4);
				}
				break;
			default:
				bytes[at] ^= (uint8)(1u << (next_word() % 8));
				break;
		}
	}
}


static void
fuzz(bool intel, int seconds, Seed* seeds, int seedCount)
{
	time_t begin = time(NULL);
	uint64 rounds = 0;
	uint64 found = 0;
	while ((int)(time(NULL) - begin) < seconds) {
		size_t size;
		uint8* bytes;
		if (seedCount > 0 && next_word() % 8 != 0) {
			Seed& seed = seeds[next_word() % seedCount];
			size = seed.size;
			// Cut the file at a random place now and then.
			if (next_word() % 4 == 0 && size > 0)
				size = next_word() % size;
			bytes = (uint8*)malloc(size + 1);
			memcpy(bytes, seed.data, size);
			mutate(bytes, size);
			// Ask for a signature that is in the file: offset 12 is the
			// Intel processor_signature and the first AMD installed_cpu.
			if (size >= 16)
				memcpy(&sSignature, bytes + 12, 4);
		} else {
			size = next_word() % 4096;
			bytes = (uint8*)malloc(size + 1);
			for (size_t i = 0; i < size; i++)
				bytes[i] = (uint8)next_word();
			if (!intel && size >= 4 && next_word() % 2 == 0)
				memcpy(bytes, "DMA\0", 4);
			sSignature = next_word();
		}
		sPlatformMsr = (uint64)(next_word() % 8) << 50;
		size_t offset = next_word() % 4 == 0 ? next_word() % 16 : 0;
		uint8* block;
		uint8* start = place(bytes, size, offset, &block);
		uint32 level = next_word() % 2 == 0 ? 0 : next_word();
		if (intel) {
			intel_microcode_header* result
				= find_microcode_intel((addr_t)start, size, level);
			if (result != NULL) {
				check_inside(result, start, size);
				found++;
				if (((addr_t)result % 16) != 0) {
					fprintf(stderr, "unaligned Intel patch returned\n");
					abort();
				}
			}
		} else {
			amd_microcode_header* result
				= find_microcode_amd((addr_t)start, size, level);
			if (result != NULL) {
				found++;
				if (((addr_t)result % 16) != 0) {
					fprintf(stderr, "unaligned AMD patch returned\n");
					abort();
				}
			}
		}
		free(block);
		free(bytes);
		rounds++;
	}
	printf("%s: %" PRIu64 " rounds, %" PRIu64 " patches found, no fault\n",
		intel ? "intel" : "amd", rounds, found);
}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: microcode-fuzz check|intel|amd ...\n");
		return 2;
	}
	if (strcmp(argv[1], "check") == 0) {
		int failures = 0;
		int files = 0;
		for (int i = 2; i < argc; i++) {
			Seed seed = read_file(argv[i]);
			if (seed.data == NULL) {
				printf("FAIL cannot read %s\n", argv[i]);
				failures++;
				continue;
			}
			bool amd = seed.size >= 4 && memcmp(seed.data, "DMA\0", 4) == 0;
			failures += amd ? check_amd(argv[i], seed)
				: check_intel(argv[i], seed);
			files++;
			free(seed.data);
		}
		printf("check: %d files, %d failures\n", files, failures);
		return failures == 0 ? 0 : 1;
	}
	if (argc < 3)
		return 2;
	bool intel = strcmp(argv[1], "intel") == 0;
	int seconds = atoi(argv[2]);
	int seedCount = argc - 3;
	Seed* seeds = (Seed*)calloc(seedCount + 1, sizeof(Seed));
	int used = 0;
	for (int i = 0; i < seedCount; i++) {
		seeds[used] = read_file(argv[3 + i]);
		if (seeds[used].data != NULL)
			used++;
	}
	fuzz(intel, seconds, seeds, used);
	for (int i = 0; i < used; i++)
		free(seeds[i].data);
	free(seeds);
	return 0;
}
