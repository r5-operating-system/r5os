/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef CPU_REPORT_H
#define CPU_REPORT_H


#include <String.h>


struct CpuReport {
	BString vendor;
	BString brand;
	BString familyModelStepping;
	BString coresThreads;
	BString caches;
	BString x86_64Level;
	BString extraFeatures;

	BString buildBaseline;
	BString userSpacePaths;
	BString xcr0;
	BString kernelLine;
};


void CollectCpuReport(CpuReport& report);
BString FormatCpuReport(const CpuReport& report);


#endif	// CPU_REPORT_H
