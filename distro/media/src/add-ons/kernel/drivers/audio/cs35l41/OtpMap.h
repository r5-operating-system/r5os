/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef CS35L41_OTP_MAP_H
#define CS35L41_OTP_MAP_H


/*!	The OTP trim map: 99 bit fields, packed least significant bit first
	from word 2, bit 16 of OTP memory. Each field is written into its bit
	range of the target register. Map A (OTP IDs 0x01 and 0x08) skips
	field 94; map B (OTP IDs 0x02, 0x03 and 0x06) writes it. From the R5
	clean-room specification (CS35L41-SPEC-2.md, section 1).
*/


#include <SupportDefs.h>


struct otp_field {
	uint8		width;
	uint8		shift;
	bool		map_b_only;
	uint32		register_address;
};


#define OTP_FIRST_BIT	(2 * 32 + 16)


static const otp_field kOtpFields[] = {
	{  4,  0, false, 0x00002030 },	// 1
	{  1,  7, false, 0x00002030 },	// 2
	{  6, 24, false, 0x0000208c },	// 3
	{  4, 14, false, 0x00002090 },	// 4
	{  4, 10, false, 0x00002090 },	// 5
	{  4, 11, false, 0x0000300c },	// 6
	{  2, 23, false, 0x0000394c },	// 7
	{  7,  0, false, 0x00003950 },	// 8
	{  7,  8, false, 0x00003950 },	// 9
	{  8, 16, false, 0x00003950 },	// 10
	{  8, 24, false, 0x00003950 },	// 11
	{  7,  0, false, 0x00003954 },	// 12
	{  7,  8, false, 0x00003954 },	// 13
	{  8, 16, false, 0x00003954 },	// 14
	{  8, 24, false, 0x00003954 },	// 15
	{  7,  0, false, 0x00003958 },	// 16
	{  7,  8, false, 0x00003958 },	// 17
	{  8, 16, false, 0x00003958 },	// 18
	{  8, 24, false, 0x00003958 },	// 19
	{  7,  0, false, 0x0000395c },	// 20
	{  7,  8, false, 0x0000395c },	// 21
	{  8, 16, false, 0x0000395c },	// 22
	{  8, 24, false, 0x0000395c },	// 23
	{  8,  0, false, 0x0000416c },	// 24
	{  7,  0, false, 0x00004160 },	// 25
	{  8,  8, false, 0x0000416c },	// 26
	{ 10, 16, false, 0x00004160 },	// 27
	{ 12, 16, false, 0x0000416c },	// 28
	{  1, 28, false, 0x0000416c },	// 29
	{  6,  0, false, 0x00004170 },	// 30
	{  1,  6, false, 0x00004170 },	// 31
	{  6,  8, false, 0x00004170 },	// 32
	{  1, 14, false, 0x00004170 },	// 33
	{  9, 16, false, 0x00004170 },	// 34
	{  5,  0, false, 0x00004360 },	// 35
	{  9,  6, false, 0x00004360 },	// 36
	{  8,  0, false, 0x00004448 },	// 37
	{  8,  8, false, 0x00004448 },	// 38
	{  8, 16, false, 0x00004448 },	// 39
	{  8, 24, false, 0x00004448 },	// 40
	{  3,  0, false, 0x0000444c },	// 41
	{  5,  0, false, 0x00006e30 },	// 42
	{  5,  8, false, 0x00006e30 },	// 43
	{  5, 16, false, 0x00006e30 },	// 44
	{  5, 24, false, 0x00006e30 },	// 45
	{  5,  0, false, 0x00006e34 },	// 46
	{  5,  8, false, 0x00006e34 },	// 47
	{  5, 16, false, 0x00006e34 },	// 48
	{  5, 24, false, 0x00006e34 },	// 49
	{  5,  0, false, 0x00006e38 },	// 50
	{  5,  8, false, 0x00006e38 },	// 51
	{  5, 16, false, 0x00006e38 },	// 52
	{  5, 24, false, 0x00006e38 },	// 53
	{  5,  0, false, 0x00006e3c },	// 54
	{  5,  8, false, 0x00006e3c },	// 55
	{  5, 16, false, 0x00006e3c },	// 56
	{  5, 24, false, 0x00006e3c },	// 57
	{  5,  0, false, 0x00006e40 },	// 58
	{  5,  8, false, 0x00006e40 },	// 59
	{  5, 16, false, 0x00006e40 },	// 60
	{  5, 24, false, 0x00006e40 },	// 61
	{  5,  0, false, 0x00006e44 },	// 62
	{ 10,  0, false, 0x00006e48 },	// 63
	{ 10, 10, false, 0x00006e48 },	// 64
	{ 10, 20, false, 0x00006e48 },	// 65
	{ 10,  0, false, 0x00006e4c },	// 66
	{ 10, 10, false, 0x00006e4c },	// 67
	{ 10, 20, false, 0x00006e4c },	// 68
	{ 10,  0, false, 0x00006e50 },	// 69
	{ 10, 10, false, 0x00006e50 },	// 70
	{ 10, 20, false, 0x00006e50 },	// 71
	{ 10,  0, false, 0x00006e54 },	// 72
	{ 10, 10, false, 0x00006e54 },	// 73
	{ 10, 20, false, 0x00006e54 },	// 74
	{ 10,  0, false, 0x00006e58 },	// 75
	{ 10, 10, false, 0x00006e58 },	// 76
	{ 10, 20, false, 0x00006e58 },	// 77
	{ 10,  0, false, 0x00006e5c },	// 78
	{ 10, 10, false, 0x00006e5c },	// 79
	{ 10, 20, false, 0x00006e5c },	// 80
	{ 10,  0, false, 0x00006e60 },	// 81
	{ 10, 10, false, 0x00006e60 },	// 82
	{ 10, 20, false, 0x00006e60 },	// 83
	{ 10,  0, false, 0x00006e64 },	// 84
	{  5,  7, false, 0x00007418 },	// 85
	{  5,  0, false, 0x0000741c },	// 86
	{  4, 11, false, 0x0000741c },	// 87
	{  4, 19, false, 0x0000741c },	// 88
	{  1, 17, false, 0x00007434 },	// 89
	{  7, 18, false, 0x00007434 },	// 90
	{  9,  0, false, 0x00007068 },	// 91
	{  1,  7, false, 0x0000410c },	// 92
	{  7,  0, false, 0x0000400c },	// 93
	{  1, 11, true , 0x00004000 },	// 94
	{  8,  0, false, 0x00017040 },	// 95
	{  8,  8, false, 0x00017040 },	// 96
	{  8, 16, false, 0x00017040 },	// 97
	{  8, 24, false, 0x00017040 },	// 98
	{ 24,  0, false, 0x00017044 },	// 99
};


#endif	// CS35L41_OTP_MAP_H
