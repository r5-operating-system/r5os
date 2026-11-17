/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * The voice link of a headset (docs/BLUETOOTH.md, step 7c), for the
 * Audio Gateway side of HFP in bluetooth_server: which codec to use after
 * AT+BAC, and the HCI commands that open the SCO or eSCO link. Written
 * from HFP 1.8 (4.11.3 codec negotiation, 5.7 the synchronous connection
 * settings) and Core 5.4, Vol 4, Part E, 7.1.26 (Setup Synchronous
 * Connection), 7.1.27 (Accept Synchronous Connection Request).
 *
 * No I/O and no Haiku headers beyond <SupportDefs.h>: bt_voice_test and
 * the host test build it unchanged. The HFP gateway (HFPGateway.cpp, step
 * 7b) calls it; see "As built" of step 7c for what joins there.
 */
#ifndef R5_VOICE_LINK_H
#define R5_VOICE_LINK_H


#include <stddef.h>
#include <string.h>

#include <SupportDefs.h>


namespace voice {


// HFP 1.8, 4.11.3 (codec IDs).
enum {
	kCodecCVSD = 1,
	kCodecMSBC = 2
};

// Voice_Setting (Core 5.4, Vol 4, Part E, 6.12): 16-bit linear input,
// two's complement, with CVSD air coding, or transparent data (mSBC).
static const uint16 kVoiceSettingCVSD = 0x0060;
static const uint16 kVoiceSettingTransparent = 0x0063;

// HCI Link Control commands (OGF 0x01).
static const uint16 kOpSetupSynchronousConnection = 0x0428;
static const uint16 kOpAcceptSynchronousConnection = 0x0429;
static const size_t kSetupSize = 3 + 17;
static const size_t kAcceptSize = 3 + 21;

// Packet_Type bits (7.1.26): HV1, HV2, HV3, EV3, EV4, EV5, and the four
// "may not be used" bits of the EDR packets 2-EV3, 3-EV3, 2-EV5, 3-EV5.
enum {
	kPacketHV1 = 0x0001,
	kPacketHV3 = 0x0004,
	kPacketEV3 = 0x0008,
	kPacketNo2EV3 = 0x0040,
	kPacketNo3EV3 = 0x0080,
	kPacketNo2EV5 = 0x0100,
	kPacketNo3EV5 = 0x0200
};

// The settings of HFP 1.8, 5.7.1 and 5.7.2 (table 5.8): T1 and T2 for
// mSBC, S1 to S4 for CVSD over eSCO, D1 for CVSD over SCO.
enum Setting {
	kSettingT2 = 0,
	kSettingT1,
	kSettingS4,
	kSettingS3,
	kSettingS1,
	kSettingD1
};

struct Parameters {
	uint32		txBandwidth;
	uint32		rxBandwidth;
	uint16		maxLatency;
	uint16		voiceSetting;
	uint8		retransmission;
	uint16		packetType;
};


static inline bool
GetParameters(Setting setting, Parameters* out)
{
	const uint16 onlyEV3 = kPacketEV3 | kPacketNo2EV3 | kPacketNo3EV3
		| kPacketNo2EV5 | kPacketNo3EV5;
	const uint16 with2EV3 = kPacketEV3 | kPacketNo3EV3 | kPacketNo2EV5
		| kPacketNo3EV5;
	out->txBandwidth = 8000;
	out->rxBandwidth = 8000;
	switch (setting) {
		case kSettingT2:
			out->maxLatency = 0x000d;
			out->voiceSetting = kVoiceSettingTransparent;
			out->retransmission = 0x02;
			out->packetType = with2EV3;
			return true;
		case kSettingT1:
			out->maxLatency = 0x0008;
			out->voiceSetting = kVoiceSettingTransparent;
			out->retransmission = 0x02;
			out->packetType = onlyEV3;
			return true;
		case kSettingS4:
			out->maxLatency = 0x000c;
			out->voiceSetting = kVoiceSettingCVSD;
			out->retransmission = 0x02;
			out->packetType = with2EV3;
			return true;
		case kSettingS3:
			out->maxLatency = 0x000a;
			out->voiceSetting = kVoiceSettingCVSD;
			out->retransmission = 0x01;
			out->packetType = with2EV3;
			return true;
		case kSettingS1:
			out->maxLatency = 0x0007;
			out->voiceSetting = kVoiceSettingCVSD;
			out->retransmission = 0x01;
			out->packetType = onlyEV3;
			return true;
		case kSettingD1:
			out->maxLatency = 0xffff;
			out->voiceSetting = kVoiceSettingCVSD;
			out->retransmission = 0xff;
			out->packetType = kPacketHV3 | kPacketNo2EV3 | kPacketNo3EV3
				| kPacketNo2EV5 | kPacketNo3EV5;
			return true;
	}
	return false;
}


// The settings to try in turn: for mSBC T2 then T1; for CVSD S4, S3, S1
// over eSCO, and D1 when the link has no eSCO (or every eSCO try failed).
static inline int
Fallbacks(int codec, bool esco, Setting* out, int capacity)
{
	static const Setting kMSBC[] = { kSettingT2, kSettingT1 };
	static const Setting kCVSD[] = { kSettingS4, kSettingS3, kSettingS1,
		kSettingD1 };
	const Setting* list;
	int count;
	if (codec == kCodecMSBC) {
		if (!esco)
			return 0;
		list = kMSBC;
		count = 2;
	} else {
		list = esco ? kCVSD : kCVSD + 3;
		count = esco ? 4 : 1;
	}
	int written = 0;
	for (int i = 0; i < count && written < capacity; i++)
		out[written++] = list[i];
	return written;
}


static inline uint8*
PutLE16(uint8* out, uint16 value)
{
	out[0] = (uint8)value;
	out[1] = (uint8)(value >> 8);
	return out + 2;
}


static inline uint8*
PutLE32(uint8* out, uint32 value)
{
	for (int i = 0; i < 4; i++)
		out[i] = (uint8)(value >> (8 * i));
	return out + 4;
}


static inline uint8*
PutParameters(uint8* p, const Parameters& parameters)
{
	p = PutLE32(p, parameters.txBandwidth);
	p = PutLE32(p, parameters.rxBandwidth);
	p = PutLE16(p, parameters.maxLatency);
	p = PutLE16(p, parameters.voiceSetting);
	*p++ = parameters.retransmission;
	return PutLE16(p, parameters.packetType);
}


// Setup Synchronous Connection on the headset's ACL link. Returns the
// command size (kSetupSize), or 0 when "capacity" is too small.
static inline size_t
BuildSetup(uint16 aclHandle, const Parameters& parameters, uint8* out,
	size_t capacity)
{
	if (out == NULL || capacity < kSetupSize)
		return 0;
	uint8* p = PutLE16(out, kOpSetupSynchronousConnection);
	*p++ = (uint8)(kSetupSize - 3);
	p = PutLE16(p, aclHandle & 0x0fff);
	PutParameters(p, parameters);
	return kSetupSize;
}


// Accept Synchronous Connection Request, for a link the headset opens.
// "address" in the byte order of HCI (least significant byte first).
static inline size_t
BuildAccept(const uint8 address[6], const Parameters& parameters, uint8* out,
	size_t capacity)
{
	if (out == NULL || address == NULL || capacity < kAcceptSize)
		return 0;
	uint8* p = PutLE16(out, kOpAcceptSynchronousConnection);
	*p++ = (uint8)(kAcceptSize - 3);
	memcpy(p, address, 6);
	p += 6;
	PutParameters(p, parameters);
	return kAcceptSize;
}


// The codec the Audio Gateway selects (+BCS) from the Hands-Free's AT+BAC
// list ("1,2"): mSBC when both sides have it, CVSD otherwise. CVSD is
// mandatory (HFP 1.8, 5.7.1), so it is the answer for a list that has
// neither, or that cannot be read.
static inline int
ChooseCodec(const char* list, bool msbcAllowed)
{
	if (list == NULL || !msbcAllowed)
		return kCodecCVSD;
	int value = -1;
	for (const char* p = list; ; p++) {
		char c = *p;
		if (c >= '0' && c <= '9') {
			int digit = c - '0';
			value = value < 0 ? digit : value * 10 + digit;
			if (value > 255)
				return kCodecCVSD;
		} else if (c == ',' || c == ' ' || c == '\0') {
			if (value == kCodecMSBC)
				return kCodecMSBC;
			value = -1;
			if (c == '\0')
				break;
		} else
			return kCodecCVSD;
	}
	return kCodecCVSD;
}


}	// namespace voice


#endif	// R5_VOICE_LINK_H
