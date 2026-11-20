/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef _R5_PSTATE_INFO_H
#define _R5_PSTATE_INFO_H


#include <Drivers.h>
#include <stdint.h>


#define R5_PSTATE_GET_KERNEL_INFO	(B_DEVICE_OP_CODES_END + 0x5235)
#define R5_PSTATE_WC_DIRECT_MAP		(B_DEVICE_OP_CODES_END + 0x5236)


enum {
	R5_KERNEL_F_SMEP			= 1u << 0,
	R5_KERNEL_F_SMAP			= 1u << 1,
	R5_KERNEL_F_NX				= 1u << 2,
	R5_KERNEL_F_PAT				= 1u << 3,
	R5_KERNEL_F_X2APIC			= 1u << 4,
	R5_KERNEL_F_INVPCID			= 1u << 5,
	R5_KERNEL_F_CLFLUSHOPT		= 1u << 6,
	R5_KERNEL_F_PCID			= 1u << 7,
	R5_KERNEL_F_LARGE_PAGES		= 1u << 8,
	R5_KERNEL_F_CPPC			= 1u << 9,
	R5_KERNEL_F_HWP				= 1u << 10,
	R5_KERNEL_F_TSC_DEADLINE	= 1u << 11,
	R5_KERNEL_F_MWAIT_IDLE		= 1u << 12,
	R5_KERNEL_F_DEBUG			= 1u << 13,
};

enum {
	R5_XSAVE_FXSAVE	= 0,
	R5_XSAVE_XSAVE	= 1,
	R5_XSAVE_XSAVEC	= 2,
};

typedef struct r5_pstate_kernel_info {
	uint32_t	size;
	uint32_t	flags;
	uint32_t	xsave_variant;
	uint32_t	reserved;
	uint64_t	xcr0;
} r5_pstate_kernel_info;


#endif	/* _R5_PSTATE_INFO_H */
