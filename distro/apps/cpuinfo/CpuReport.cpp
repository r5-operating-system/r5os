/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#include "CpuReport.h"

#include <OS.h>

#include <ctype.h>
#include <fcntl.h>
#include <new>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifdef __x86_64__
#include <r5_pstate_info.h>
#include <r5_x86_cpu.h>
#endif


static void
append_item(BString& list, const char* item)
{
	if (list.Length() > 0)
		list << ", ";
	list << item;
}


static BString
format_cache_size(uint64 bytes)
{
	BString text;
	if (bytes >= 1024ULL * 1024ULL && (bytes % (1024ULL * 1024ULL)) == 0)
		text.SetToFormat("%llu MB", (unsigned long long)(bytes / (1024ULL * 1024ULL)));
	else if (bytes >= 1024ULL)
		text.SetToFormat("%llu KB", (unsigned long long)(bytes / 1024ULL));
	else
		text.SetToFormat("%llu B", (unsigned long long)bytes);
	return text;
}


static void
collect_cores_threads(BString& out)
{
	system_info sysInfo = {};
	uint32 threads = 0;
	if (get_system_info(&sysInfo) == B_OK)
		threads = sysInfo.cpu_count;

	uint32 cores = 0;
	uint32 count = 0;
	if (get_cpu_topology_info(NULL, &count) == B_OK && count > 0) {
		cpu_topology_node_info* nodes = new(std::nothrow) cpu_topology_node_info[count];
		if (nodes != NULL) {
			if (get_cpu_topology_info(nodes, &count) == B_OK) {
				for (uint32 i = 0; i < count; i++) {
					if (nodes[i].type == B_TOPOLOGY_CORE)
						cores++;
				}
			}
			delete[] nodes;
		}
	}

	if (cores > 0 && threads > 0) {
		out.SetToFormat("%u core%s / %u thread%s", cores, cores == 1 ? "" : "s",
			threads, threads == 1 ? "" : "s");
	} else if (threads > 0) {
		out.SetToFormat("%u thread%s", threads, threads == 1 ? "" : "s");
	} else {
		out = "Unknown";
	}
}


static void
collect_kernel_line(BString& out)
{
#ifdef __x86_64__
	int fd = open("/dev/misc/r5_pstate", O_RDONLY);
	if (fd < 0) {
		out = "Not reported";
		return;
	}

	r5_pstate_kernel_info info = {};
	info.size = sizeof(info);
	int rc = ioctl(fd, R5_PSTATE_GET_KERNEL_INFO, &info, sizeof(info));
	close(fd);
	if (rc < 0) {
		out = "Not reported";
		return;
	}

	out = (info.flags & R5_KERNEL_F_DEBUG) ? "debug" : "release";
	if (info.flags & R5_KERNEL_F_SMEP) append_item(out, "SMEP");
	if (info.flags & R5_KERNEL_F_SMAP) append_item(out, "SMAP");
	if (info.flags & R5_KERNEL_F_NX) append_item(out, "NX");
	if (info.flags & R5_KERNEL_F_PAT) append_item(out, "PAT");
	if (info.flags & R5_KERNEL_F_X2APIC) append_item(out, "x2APIC");
	if (info.flags & R5_KERNEL_F_INVPCID) append_item(out, "INVPCID");
	if (info.flags & R5_KERNEL_F_CLFLUSHOPT) append_item(out, "CLFLUSHOPT");
	if (info.flags & R5_KERNEL_F_PCID) append_item(out, "PCID");
	append_item(out, (info.flags & R5_KERNEL_F_LARGE_PAGES)
		? "2MB pages: on" : "2MB pages: off");
	if (info.flags & R5_KERNEL_F_CPPC) append_item(out, "CPPC");
	if (info.flags & R5_KERNEL_F_HWP) append_item(out, "HWP");

	const char* xsave = "FXSAVE";
	if (info.xsave_variant == R5_XSAVE_XSAVEC)
		xsave = "XSAVEC";
	else if (info.xsave_variant == R5_XSAVE_XSAVE)
		xsave = "XSAVE";
	append_item(out, xsave);

	append_item(out, (info.flags & R5_KERNEL_F_TSC_DEADLINE)
		? "TSC-deadline" : "APIC timer");
	append_item(out, (info.flags & R5_KERNEL_F_MWAIT_IDLE)
		? "MWAIT idle: cpuidle" : "MWAIT idle: off");

	BString xcr0Str;
	xcr0Str.SetToFormat("XCR0 0x%llx", (unsigned long long)info.xcr0);
	append_item(out, xcr0Str.String());
#else
	out = "Not reported";
#endif
}


#ifdef __x86_64__
static void
collect_x86_64(CpuReport& report)
{
	uint32 eax = 0, ebx = 0, ecx = 0, edx = 0;
	r5_x86_cpuid(0, 0, &eax, &ebx, &ecx, &edx);
	uint32 maxBasic = eax;

	char vendorBuf[13];
	memcpy(vendorBuf + 0, &ebx, 4);
	memcpy(vendorBuf + 4, &edx, 4);
	memcpy(vendorBuf + 8, &ecx, 4);
	vendorBuf[12] = '\0';
	report.vendor = vendorBuf[0] != '\0' ? vendorBuf : "Unknown";

	const bool isIntel = strcmp(vendorBuf, "GenuineIntel") == 0;
	const bool isAmd = strcmp(vendorBuf, "AuthenticAMD") == 0
		|| strcmp(vendorBuf, "HygonGenuine") == 0;

	uint32 extMax = 0;
	r5_x86_cpuid(0x80000000, 0, &extMax, &ebx, &ecx, &edx);

	if (extMax >= 0x80000004) {
		char brandBuf[49];
		uint32* words = reinterpret_cast<uint32*>(brandBuf);
		for (uint32 i = 0; i < 3; i++) {
			r5_x86_cpuid(0x80000002 + i, 0,
				&words[i * 4 + 0], &words[i * 4 + 1],
				&words[i * 4 + 2], &words[i * 4 + 3]);
		}
		brandBuf[48] = '\0';
		const char* trimmed = brandBuf;
		while (*trimmed != '\0' && isspace((unsigned char)*trimmed))
			trimmed++;
		report.brand = trimmed[0] != '\0' ? trimmed : "Unknown";
	} else {
		report.brand = "Unknown";
	}

	uint32 l1Eax = 0, l1Ebx = 0, l1Ecx = 0, l1Edx = 0;
	if (maxBasic >= 1)
		r5_x86_cpuid(1, 0, &l1Eax, &l1Ebx, &l1Ecx, &l1Edx);

	uint32 baseFamily = (l1Eax >> 8) & 0xf;
	uint32 baseModel = (l1Eax >> 4) & 0xf;
	uint32 stepping = l1Eax & 0xf;
	uint32 extFamily = (l1Eax >> 20) & 0xff;
	uint32 extModel = (l1Eax >> 16) & 0xf;
	uint32 family = baseFamily == 0xf ? baseFamily + extFamily : baseFamily;
	uint32 model = (baseFamily == 0x6 || baseFamily == 0xf)
		? (extModel << 4) | baseModel : baseModel;
	report.familyModelStepping.SetToFormat(
		"Family 0x%x, Model 0x%x, Stepping %u", family, model, stepping);

	// Caches (L1d, L1i, L2, L3)
	uint64 l1d = 0, l1i = 0, l2 = 0, l3 = 0;
	uint32 cacheLeaf = 0;
	if (isIntel && maxBasic >= 4)
		cacheLeaf = 4;
	else if (isAmd && extMax >= 0x8000001d)
		cacheLeaf = 0x8000001d;

	if (cacheLeaf != 0) {
		for (uint32 sub = 0; sub < 16; sub++) {
			r5_x86_cpuid(cacheLeaf, sub, &eax, &ebx, &ecx, &edx);
			uint32 type = eax & 0x1f;
			if (type == 0)
				break;
			uint32 level = (eax >> 5) & 0x7;
			uint64 line = (ebx & 0xfff) + 1;
			uint64 parts = ((ebx >> 12) & 0x3ff) + 1;
			uint64 ways = ((ebx >> 22) & 0x3ff) + 1;
			uint64 sets = (uint64)ecx + 1;
			uint64 bytes = ways * parts * line * sets;
			if (level == 1 && type == 1)
				l1d = bytes;
			else if (level == 1 && type == 2)
				l1i = bytes;
			else if (level == 2)
				l2 = bytes;
			else if (level == 3)
				l3 = bytes;
		}
	}
	if (l1d > 0) {
		BString s;
		s.SetToFormat("L1d %s", format_cache_size(l1d).String());
		append_item(report.caches, s.String());
	}
	if (l1i > 0) {
		BString s;
		s.SetToFormat("L1i %s", format_cache_size(l1i).String());
		append_item(report.caches, s.String());
	}
	if (l2 > 0) {
		BString s;
		s.SetToFormat("L2 %s", format_cache_size(l2).String());
		append_item(report.caches, s.String());
	}
	if (l3 > 0) {
		BString s;
		s.SetToFormat("L3 %s", format_cache_size(l3).String());
		append_item(report.caches, s.String());
	}
	if (report.caches.Length() == 0)
		report.caches = "Unknown";

	uint32 l7Eax = 0, l7Ebx = 0, l7Ecx = 0, l7Edx = 0;
	if (maxBasic >= 7)
		r5_x86_cpuid(7, 0, &l7Eax, &l7Ebx, &l7Ecx, &l7Edx);

	uint32 ext1Ecx = 0, ext1Edx = 0;
	if (extMax >= 0x80000001)
		r5_x86_cpuid(0x80000001, 0, &eax, &ebx, &ext1Ecx, &ext1Edx);

	uint32 ext7Edx = 0;
	if (extMax >= 0x80000007)
		r5_x86_cpuid(0x80000007, 0, &eax, &ebx, &ecx, &ext7Edx);

	const bool osxsave = (l1Ecx & (1u << 27)) != 0;
	uint64 xcr0 = osxsave ? r5_x86_xcr0() : 0;
	const bool osAvx = osxsave && ((xcr0 & 0x6) == 0x6);
	const bool osAvx512 = osAvx && ((xcr0 & 0xe0) == 0xe0);

	// x86-64 microarchitecture feature levels (v1 - v4)
	const bool hasV1 = (l1Edx & (1u << 0)) != 0   // FPU
		&& (l1Edx & (1u << 8)) != 0               // CX8
		&& (l1Edx & (1u << 15)) != 0              // CMOV
		&& (l1Edx & (1u << 23)) != 0              // MMX
		&& (l1Edx & (1u << 24)) != 0              // FXSR
		&& (l1Edx & (1u << 25)) != 0              // SSE
		&& (l1Edx & (1u << 26)) != 0;             // SSE2
	const bool hasV2 = hasV1
		&& (l1Ecx & (1u << 0)) != 0               // SSE3
		&& (l1Ecx & (1u << 9)) != 0               // SSSE3
		&& (l1Ecx & (1u << 13)) != 0              // CMPXCHG16B
		&& (l1Ecx & (1u << 19)) != 0              // SSE4.1
		&& (l1Ecx & (1u << 20)) != 0              // SSE4.2
		&& (l1Ecx & (1u << 23)) != 0              // POPCNT
		&& (ext1Ecx & (1u << 0)) != 0;            // LAHF_LM
	const bool hasV3 = hasV2 && osAvx
		&& (l1Ecx & (1u << 12)) != 0              // FMA
		&& (l1Ecx & (1u << 22)) != 0              // MOVBE
		&& (l1Ecx & (1u << 28)) != 0              // AVX
		&& (l1Ecx & (1u << 29)) != 0              // F16C
		&& (l7Ebx & (1u << 3)) != 0               // BMI1
		&& (l7Ebx & (1u << 5)) != 0               // AVX2
		&& (l7Ebx & (1u << 8)) != 0               // BMI2
		&& (ext1Ecx & (1u << 5)) != 0;            // LZCNT
	const bool hasV4 = hasV3 && osAvx512
		&& (l7Ebx & (1u << 16)) != 0              // AVX512F
		&& (l7Ebx & (1u << 17)) != 0              // AVX512DQ
		&& (l7Ebx & (1u << 28)) != 0              // AVX512CD
		&& (l7Ebx & (1u << 30)) != 0              // AVX512BW
		&& (l7Ebx & (1u << 31)) != 0;             // AVX512VL

	if (hasV4)
		report.x86_64Level = "x86-64-v4";
	else if (hasV3)
		report.x86_64Level = "x86-64-v3";
	else if (hasV2)
		report.x86_64Level = "x86-64-v2";
	else if (hasV1)
		report.x86_64Level = "x86-64-v1";
	else
		report.x86_64Level = "Unknown";

	// Extra features
	if (l1Ecx & (1u << 25)) append_item(report.extraFeatures, "AES-NI");
	if (l7Ebx & (1u << 29)) append_item(report.extraFeatures, "SHA");
	if (l7Ebx & (1u << 9)) append_item(report.extraFeatures, "ERMS");
	if (l7Edx & (1u << 4)) append_item(report.extraFeatures, "FSRM");
	if (l7Ebx & (1u << 10)) append_item(report.extraFeatures, "INVPCID");
	if (l1Ecx & (1u << 17)) append_item(report.extraFeatures, "PCID");
	if (l7Ebx & (1u << 23)) append_item(report.extraFeatures, "CLFLUSHOPT");
	if (ext1Edx & (1u << 26)) append_item(report.extraFeatures, "PDPE1GB");
	if (ext7Edx & (1u << 8)) append_item(report.extraFeatures, "Invariant TSC");
	if (l1Ecx & (1u << 21)) append_item(report.extraFeatures, "x2APIC");
	if (l7Ebx & (1u << 7)) append_item(report.extraFeatures, "SMEP");
	if (l7Ebx & (1u << 20)) append_item(report.extraFeatures, "SMAP");
	if (ext1Edx & (1u << 20)) append_item(report.extraFeatures, "NX");
	if (l1Edx & (1u << 16)) append_item(report.extraFeatures, "PAT");
	if (l7Edx & (1u << 15)) {
		if (maxBasic >= 0x1a) {
			r5_x86_cpuid(0x1a, 0, &eax, &ebx, &ecx, &edx);
			uint32 coreType = eax >> 24;
			if (coreType == 0x40)
				append_item(report.extraFeatures, "Hybrid (P-core)");
			else if (coreType == 0x20)
				append_item(report.extraFeatures, "Hybrid (E-core)");
			else
				append_item(report.extraFeatures, "Hybrid");
		} else {
			append_item(report.extraFeatures, "Hybrid");
		}
	}
	if (report.extraFeatures.Length() == 0)
		report.extraFeatures = "None";

	// What R5 uses
	report.buildBaseline = "x86-64-v2";

	uint32 r5Flags = (uint32)r5_x86_cpu_pack();
	const char* memcpyPath = "SSE2";
	if ((r5Flags & R5_CPU_ERMS) != 0 && (r5Flags & R5_CPU_FSRM) != 0)
		memcpyPath = "FSRM/ERMS";
	else if ((r5Flags & R5_CPU_ERMS) != 0)
		memcpyPath = "ERMS";

	const char* stringPath = (r5Flags & R5_CPU_AVX2) ? "AVX2" : "SSE4.2";

	const char* libmPath = "baseline";
	if (r5Flags & R5_CPU_FMA)
		libmPath = "FMA";
	else if (r5Flags & R5_CPU_FMA4)
		libmPath = "FMA4";

	report.userSpacePaths.SetToFormat(
		"memcpy: %s, strings: %s, libm: %s",
		memcpyPath, stringPath, libmPath);

	if (osxsave) {
		BString bits;
		if (xcr0 & (1ULL << 0)) append_item(bits, "x87");
		if (xcr0 & (1ULL << 1)) append_item(bits, "SSE");
		if (xcr0 & (1ULL << 2)) append_item(bits, "AVX");
		if ((xcr0 & 0xe0) == 0xe0) append_item(bits, "AVX-512");
		if (bits.Length() > 0) {
			report.xcr0.SetToFormat("0x%llx (%s)",
				(unsigned long long)xcr0, bits.String());
		} else {
			report.xcr0.SetToFormat("0x%llx", (unsigned long long)xcr0);
		}
	} else {
		report.xcr0 = "Not enabled";
	}
}
#endif


void
CollectCpuReport(CpuReport& report)
{
	collect_cores_threads(report.coresThreads);
	collect_kernel_line(report.kernelLine);

#ifdef __x86_64__
	collect_x86_64(report);
#else
	report.vendor = "Unknown";
	report.brand = "Unknown";
	report.familyModelStepping = "Unknown";
	report.caches = "Unknown";
	report.x86_64Level = "Not applicable";
	report.extraFeatures = "Not reported";
	report.buildBaseline = "Unknown";
	report.userSpacePaths = "Not reported";
	report.xcr0 = "Not reported";
#endif
}


BString
FormatCpuReport(const CpuReport& report)
{
	BString out;
	out << "CPU hardware\n";
	out << "  Vendor:           " << report.vendor << "\n";
	out << "  Brand:            " << report.brand << "\n";
	out << "  Family/Model/Stp: " << report.familyModelStepping << "\n";
	out << "  Cores / Threads:  " << report.coresThreads << "\n";
	out << "  Caches:           " << report.caches << "\n";
	out << "  x86-64 level:     " << report.x86_64Level << "\n";
	out << "  Extra features:   " << report.extraFeatures << "\n\n";

	out << "What R5 uses\n";
	out << "  Build baseline:   " << report.buildBaseline << "\n";
	out << "  User-space paths: " << report.userSpacePaths << "\n";
	out << "  XCR0:             " << report.xcr0 << "\n";
	out << "  Kernel:           " << report.kernelLine << "\n";
	return out;
}
