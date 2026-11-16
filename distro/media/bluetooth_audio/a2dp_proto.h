/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * AVDTP 1.3, A2DP 1.3.2 (SBC codec element, media packet header), AVCTP 1.4
 * and AVRCP 1.6 packet parsing and building, for bluetooth_audio. Plain
 * C++ with <stdint.h> only, so scripts/fuzz-bt.sh builds the same code on
 * the host. Every parser takes a length and never reads past it; nothing
 * here allocates.
 */
#ifndef R5_A2DP_PROTO_H
#define R5_A2DP_PROTO_H


#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "sbc_encoder.h"


namespace a2dp {


static const uint16_t kPSMAVDTP = 0x0019;
static const uint16_t kPSMAVCTP = 0x0017;

// The default L2CAP MTU (Core 6.0, Vol 3, Part A, 5.1). The R5 L2CAP socket
// does not report the MTU the other side asked for, so media packets stay
// within this.
static const size_t kMediaMTU = 672;


// #pragma mark - AVDTP (AVDTP 1.3, 8.4 to 8.21)


enum {
	kMessageCommand = 0,
	kMessageGeneralReject = 1,
	kMessageAccept = 2,
	kMessageReject = 3
};

enum {
	kPacketSingle = 0,
	kPacketStart = 1,
	kPacketContinue = 2,
	kPacketEnd = 3
};

enum {
	kDiscover = 0x01,
	kGetCapabilities = 0x02,
	kSetConfiguration = 0x03,
	kGetConfiguration = 0x04,
	kReconfigure = 0x05,
	kOpen = 0x06,
	kStart = 0x07,
	kClose = 0x08,
	kSuspend = 0x09,
	kAbort = 0x0a,
	kSecurityControl = 0x0b,
	kGetAllCapabilities = 0x0c,
	kDelayReport = 0x0d
};

// Service categories (8.21).
enum {
	kCategoryMediaTransport = 0x01,
	kCategoryReporting = 0x02,
	kCategoryRecovery = 0x03,
	kCategoryContentProtection = 0x04,
	kCategoryHeaderCompression = 0x05,
	kCategoryMultiplexing = 0x06,
	kCategoryMediaCodec = 0x07,
	kCategoryDelayReporting = 0x08
};

// Error codes (8.20.6.2) and the A2DP codec errors (A2DP 5.1.3).
enum {
	kErrorBadHeaderFormat = 0x01,
	kErrorBadLength = 0x11,
	kErrorBadACPSEID = 0x12,
	kErrorSEPInUse = 0x13,
	kErrorSEPNotInUse = 0x14,
	kErrorBadServiceCategory = 0x17,
	kErrorBadPayloadFormat = 0x18,
	kErrorNotSupportedCommand = 0x19,
	kErrorInvalidCapabilities = 0x1a,
	kErrorBadMediaTransportFormat = 0x23,
	kErrorUnsupportedConfiguration = 0x29,
	kErrorBadState = 0x31,
	kErrorNotSupportedCodecType = 0xc2,
	kErrorNotSupportedSamplingFrequency = 0xc4
};

// The rate R5's node runs at and the only one it offers: the mixer
// delivers it and R5 does not resample. A2DP 4.3.2: every SBC sink
// supports 44.1 kHz.
static const int kSourceRate = 44100;

static const uint8_t kMediaTypeAudio = 0x00;
static const uint8_t kCodecSBC = 0x00;

// SBC codec information element bits (A2DP 4.3.2).
enum {
	kSBC16000 = 0x80, kSBC32000 = 0x40, kSBC44100 = 0x20, kSBC48000 = 0x10,
	kSBCMono = 0x08, kSBCDual = 0x04, kSBCStereo = 0x02, kSBCJoint = 0x01,
	kSBCBlocks4 = 0x80, kSBCBlocks8 = 0x40, kSBCBlocks12 = 0x20,
	kSBCBlocks16 = 0x10,
	kSBCSubbands4 = 0x08, kSBCSubbands8 = 0x04,
	kSBCAllocSNR = 0x02, kSBCAllocLoudness = 0x01
};

// A2DP 4.3.2: "the maximum bit rate ... 512 kb/s for two-channel modes"
// (320 kb/s for mono). The best SBC setting is the highest bitpool the
// headset accepts within this.
static const uint32_t kMaxBitRateStereo = 512000;
static const uint32_t kMaxBitRateMono = 320000;


struct SignalHeader {
	uint8_t		label;
	uint8_t		packetType;
	uint8_t		messageType;
	uint8_t		signal;
	size_t		length;		// bytes of header before the payload
};


// Single packets only: signalling messages R5 sends and needs are far
// smaller than the L2CAP MTU, so fragmented ones are refused.
static inline bool
ParseSignalHeader(const uint8_t* data, size_t size, SignalHeader* header)
{
	if (data == NULL || size < 1)
		return false;
	header->label = data[0] >> 4;
	header->packetType = (data[0] >> 2) & 3;
	header->messageType = data[0] & 3;
	if (header->packetType != kPacketSingle)
		return false;
	// A General Reject carries the signal identifier too (AVDTP 1.3,
	// 8.18); an older one may stop after the first byte.
	if (size < 2) {
		if (header->messageType != kMessageGeneralReject)
			return false;
		header->signal = 0;
		header->length = 1;
		return true;
	}
	header->signal = data[1] & 0x3f;
	header->length = 2;
	return true;
}


static inline size_t
BuildSignalHeader(uint8_t* out, size_t size, uint8_t label,
	uint8_t messageType, uint8_t signal)
{
	if (size < 2)
		return 0;
	out[0] = (uint8_t)(((label & 0x0f) << 4) | (kPacketSingle << 2)
		| (messageType & 3));
	out[1] = (uint8_t)(signal & 0x3f);
	return 2;
}


struct EndpointInfo {
	uint8_t		seid;
	bool		inUse;
	uint8_t		mediaType;
	bool		sink;
};


// The Discover response payload (8.6.2): two bytes per endpoint. Returns
// the number of endpoints read, -1 when the payload is malformed.
static inline int
ParseDiscover(const uint8_t* data, size_t size, EndpointInfo* endpoints,
	int maxEndpoints)
{
	if ((size % 2) != 0 || size == 0)
		return -1;
	int count = 0;
	for (size_t i = 0; i + 1 < size && count < maxEndpoints; i += 2) {
		EndpointInfo& info = endpoints[count];
		info.seid = (data[i] >> 2) & 0x3f;
		info.inUse = (data[i] >> 1) & 1;
		info.mediaType = data[i + 1] >> 4;
		info.sink = (data[i + 1] >> 3) & 1;
		if (info.seid == 0)
			return -1;
		count++;
	}
	return count;
}


struct SBCCapabilities {
	bool		mediaTransport;
	bool		delayReporting;
	bool		sbc;			// an SBC media codec element was found
	bool		otherCodec;		// a media codec element other than SBC
	uint8_t		frequencies;	// kSBC16000 ...
	uint8_t		channelModes;	// kSBCMono ...
	uint8_t		blocks;			// kSBCBlocks4 ...
	uint8_t		subbands;		// kSBCSubbands4 ...
	uint8_t		allocations;	// kSBCAllocSNR ...
	uint8_t		minBitpool;
	uint8_t		maxBitpool;
};


// Walks a list of service capabilities (8.21). Returns 0 or the AVDTP error
// code; *_badCategory is the category it refers to.
static inline uint8_t
ParseCapabilities(const uint8_t* data, size_t size, SBCCapabilities* caps,
	uint8_t* _badCategory)
{
	memset(caps, 0, sizeof(*caps));
	*_badCategory = 0;
	size_t offset = 0;
	while (offset < size) {
		if (size - offset < 2) {
			*_badCategory = data[offset];
			return kErrorBadLength;
		}
		uint8_t category = data[offset];
		uint8_t length = data[offset + 1];
		offset += 2;
		if (length > size - offset) {
			*_badCategory = category;
			return kErrorBadLength;
		}
		const uint8_t* element = data + offset;
		offset += length;

		switch (category) {
			case kCategoryMediaTransport:
				if (length != 0) {
					*_badCategory = category;
					return kErrorBadMediaTransportFormat;
				}
				caps->mediaTransport = true;
				break;
			case kCategoryDelayReporting:
				if (length != 0) {
					*_badCategory = category;
					return kErrorBadPayloadFormat;
				}
				caps->delayReporting = true;
				break;
			case kCategoryMediaCodec:
				if (length < 2) {
					*_badCategory = category;
					return kErrorBadPayloadFormat;
				}
				if ((element[0] >> 4) != kMediaTypeAudio
					|| element[1] != kCodecSBC) {
					caps->otherCodec = true;
					break;
				}
				if (length != 6) {
					*_badCategory = category;
					return kErrorBadPayloadFormat;
				}
				caps->sbc = true;
				caps->frequencies = element[2] & 0xf0;
				caps->channelModes = element[2] & 0x0f;
				caps->blocks = element[3] & 0xf0;
				caps->subbands = element[3] & 0x0c;
				caps->allocations = element[3] & 0x03;
				caps->minBitpool = element[4];
				caps->maxBitpool = element[5];
				break;
			case kCategoryReporting:
			case kCategoryRecovery:
			case kCategoryContentProtection:
			case kCategoryHeaderCompression:
			case kCategoryMultiplexing:
				// Known, not used by R5: skipped.
				break;
			default:
				*_badCategory = category;
				return kErrorBadServiceCategory;
		}
	}
	return 0;
}


// The capabilities of R5's SBC source endpoint (8.7.2): media transport,
// SBC at kSourceRate with every other setting, bitpool 2 to 250, and delay
// reporting.
static inline size_t
BuildSourceCapabilities(uint8_t* out, size_t size, bool delayReporting)
{
	size_t need = 2 + 8 + (delayReporting ? 2 : 0);
	if (size < need)
		return 0;
	size_t i = 0;
	out[i++] = kCategoryMediaTransport;
	out[i++] = 0;
	out[i++] = kCategoryMediaCodec;
	out[i++] = 6;
	out[i++] = kMediaTypeAudio << 4;
	out[i++] = kCodecSBC;
	out[i++] = kSBC44100 | kSBCMono | kSBCDual | kSBCStereo | kSBCJoint;
	out[i++] = kSBCBlocks4 | kSBCBlocks8 | kSBCBlocks12 | kSBCBlocks16
		| kSBCSubbands4 | kSBCSubbands8 | kSBCAllocSNR | kSBCAllocLoudness;
	out[i++] = 2;
	out[i++] = 250;
	if (delayReporting) {
		out[i++] = kCategoryDelayReporting;
		out[i++] = 0;
	}
	return i;
}


// The SBC element and media transport for Set Configuration (8.9.1).
static inline size_t
BuildConfiguration(uint8_t* out, size_t size, const sbc::Config& config,
	bool delayReporting)
{
	size_t length = BuildSourceCapabilities(out, size, delayReporting);
	if (length == 0)
		return 0;

	// Same layout as the capabilities, with one bit per field and the
	// bitpool range narrowed to the chosen bitpool.
	static const uint8_t kFrequency[4] = { kSBC16000, kSBC32000, kSBC44100,
		kSBC48000 };
	static const uint8_t kMode[4] = { kSBCMono, kSBCDual, kSBCStereo,
		kSBCJoint };
	out[6] = (uint8_t)(kFrequency[config.frequency & 3]
		| kMode[config.mode & 3]);
	uint8_t blocks = config.blocks == 4 ? kSBCBlocks4
		: config.blocks == 8 ? kSBCBlocks8
		: config.blocks == 12 ? kSBCBlocks12 : kSBCBlocks16;
	out[7] = (uint8_t)(blocks
		| (config.subbands == 4 ? kSBCSubbands4 : kSBCSubbands8)
		| (config.allocation == sbc::kAllocationSNR ? kSBCAllocSNR
			: kSBCAllocLoudness));
	out[8] = (uint8_t)config.bitpool;
	out[9] = (uint8_t)config.bitpool;
	return length;
}


static inline bool
single_bit(uint8_t value)
{
	return value != 0 && (value & (value - 1)) == 0;
}


// A configuration the headset sent (Set Configuration from its side):
// exactly one bit in each field, a bitpool range R5 can use.
static inline bool
ConfigFromCapabilities(const SBCCapabilities& caps, sbc::Config* config)
{
	if (!caps.sbc || !single_bit(caps.frequencies)
		|| !single_bit(caps.channelModes) || !single_bit(caps.blocks)
		|| !single_bit(caps.subbands) || !single_bit(caps.allocations))
		return false;
	config->frequency = caps.frequencies == kSBC16000 ? sbc::kFrequency16000
		: caps.frequencies == kSBC32000 ? sbc::kFrequency32000
		: caps.frequencies == kSBC44100 ? sbc::kFrequency44100
		: sbc::kFrequency48000;
	config->mode = caps.channelModes == kSBCMono ? sbc::kModeMono
		: caps.channelModes == kSBCDual ? sbc::kModeDualChannel
		: caps.channelModes == kSBCStereo ? sbc::kModeStereo
		: sbc::kModeJointStereo;
	config->blocks = caps.blocks == kSBCBlocks4 ? 4
		: caps.blocks == kSBCBlocks8 ? 8
		: caps.blocks == kSBCBlocks12 ? 12 : 16;
	config->subbands = caps.subbands == kSBCSubbands4 ? 4 : 8;
	config->allocation = caps.allocations == kSBCAllocSNR
		? sbc::kAllocationSNR : sbc::kAllocationLoudness;
	if (caps.minBitpool < 2 || caps.maxBitpool < caps.minBitpool)
		return false;
	// The highest bitpool in the range the headset chose, within the
	// encoder's and A2DP's limits.
	config->bitpool = caps.maxBitpool;
	while (config->bitpool > caps.minBitpool
		&& (config->bitpool > sbc::MaxBitpool(*config)
			|| sbc::BitRate(*config) > (config->mode == sbc::kModeMono
				? kMaxBitRateMono : kMaxBitRateStereo)
			|| sbc::FrameLength(*config) + 13 > kMediaMTU))
		config->bitpool--;
	return sbc::ValidConfig(*config);
}


// Chooses the best setting the headset supports at the mixer's rate (no
// other: R5 does not resample), joint stereo, 16 blocks, 8 subbands,
// Loudness, and the highest bitpool within the headset's range, A2DP's bit
// rate limit and one media packet.
static inline bool
ChooseConfiguration(const SBCCapabilities& caps, int preferredRate,
	sbc::Config* config)
{
	if (!caps.sbc)
		return false;

	// Only the rate the audio comes in: R5 does not resample.
	uint8_t frequency = preferredRate == 48000 ? kSBC48000
		: preferredRate == 44100 ? kSBC44100
		: preferredRate == 32000 ? kSBC32000
		: preferredRate == 16000 ? kSBC16000 : 0;
	if ((caps.frequencies & frequency) == 0)
		return false;
	static const uint8_t kModes[4] = { kSBCJoint, kSBCStereo, kSBCDual,
		kSBCMono };
	uint8_t mode = 0;
	for (int i = 0; i < 4 && mode == 0; i++) {
		if (caps.channelModes & kModes[i])
			mode = kModes[i];
	}
	static const uint8_t kBlocks[4] = { kSBCBlocks16, kSBCBlocks12,
		kSBCBlocks8, kSBCBlocks4 };
	uint8_t blocks = 0;
	for (int i = 0; i < 4 && blocks == 0; i++) {
		if (caps.blocks & kBlocks[i])
			blocks = kBlocks[i];
	}
	uint8_t subbands = (caps.subbands & kSBCSubbands8) ? kSBCSubbands8
		: (caps.subbands & kSBCSubbands4);
	uint8_t allocation = (caps.allocations & kSBCAllocLoudness)
		? kSBCAllocLoudness : (caps.allocations & kSBCAllocSNR);
	if (frequency == 0 || mode == 0 || blocks == 0 || subbands == 0
		|| allocation == 0)
		return false;

	SBCCapabilities chosen = caps;
	chosen.frequencies = frequency;
	chosen.channelModes = mode;
	chosen.blocks = blocks;
	chosen.subbands = subbands;
	chosen.allocations = allocation;
	return ConfigFromCapabilities(chosen, config);
}


// The RTP header (RFC 3550, 5.1) and the SBC media payload header
// (A2DP 4.3.4) in front of "frames" SBC frames.
static const size_t kMediaHeaderLength = 13;

static inline size_t
BuildMediaHeader(uint8_t* out, size_t size, uint16_t sequence,
	uint32_t timestamp, uint32_t ssrc, int frames)
{
	if (size < kMediaHeaderLength || frames < 1 || frames > 15)
		return 0;
	out[0] = 0x80;				// version 2, no padding, extension, CSRC
	out[1] = 96;				// dynamic payload type, no marker
	out[2] = (uint8_t)(sequence >> 8);
	out[3] = (uint8_t)sequence;
	out[4] = (uint8_t)(timestamp >> 24);
	out[5] = (uint8_t)(timestamp >> 16);
	out[6] = (uint8_t)(timestamp >> 8);
	out[7] = (uint8_t)timestamp;
	out[8] = (uint8_t)(ssrc >> 24);
	out[9] = (uint8_t)(ssrc >> 16);
	out[10] = (uint8_t)(ssrc >> 8);
	out[11] = (uint8_t)ssrc;
	out[12] = (uint8_t)(frames & 0x0f);	// not fragmented
	return kMediaHeaderLength;
}


// #pragma mark - AVCTP and AVRCP


static const uint16_t kAVRemoteControlPID = 0x110e;

enum {
	kCTypeControl = 0x0,
	kCTypeStatus = 0x1,
	kCTypeNotify = 0x3,
	kResponseNotImplemented = 0x8,
	kResponseAccepted = 0x9,
	kResponseRejected = 0xa,
	kResponseStable = 0xc,
	kResponseChanged = 0xd,
	kResponseInterim = 0xf
};

enum {
	kOpcodeVendorDependent = 0x00,
	kOpcodeUnitInfo = 0x30,
	kOpcodeSubunitInfo = 0x31,
	kOpcodePassThrough = 0x7c
};

// AV/C panel subunit (type 0x09, ID 0).
static const uint8_t kSubunitPanel = 0x48;
static const uint8_t kSubunitUnit = 0xff;

// PASS THROUGH operation IDs (AV/C Panel Subunit, table 9.21).
enum {
	kOpVolumeUp = 0x41,
	kOpVolumeDown = 0x42,
	kOpPlay = 0x44,
	kOpStop = 0x45,
	kOpPause = 0x46,
	kOpForward = 0x4b,
	kOpBackward = 0x4c
};

// AVRCP PDUs (AVRCP 1.6, 6.x) and events (6.7.2).
enum {
	kPDUGetCapabilities = 0x10,
	kPDURegisterNotification = 0x31,
	kPDUSetAbsoluteVolume = 0x50
};

enum {
	kCapabilityCompanyID = 0x02,
	kCapabilityEvents = 0x03
};

enum {
	kEventPlaybackStatusChanged = 0x01,
	kEventVolumeChanged = 0x0d
};

static const uint32_t kBluetoothSIGCompanyID = 0x001958;

// AVRCP error codes (6.15.3).
enum {
	kAVRCPErrorInvalidCommand = 0x00,
	kAVRCPErrorInvalidParameter = 0x01
};


struct AVCTPHeader {
	uint8_t		label;
	bool		response;
	bool		invalidPID;
	uint16_t	pid;
};


// Single AVCTP packets only (AVCTP 1.4, 6.1.1): 3 header bytes.
static inline bool
ParseAVCTP(const uint8_t* data, size_t size, AVCTPHeader* header)
{
	if (data == NULL || size < 3)
		return false;
	if (((data[0] >> 2) & 3) != kPacketSingle)
		return false;
	header->label = data[0] >> 4;
	header->response = (data[0] >> 1) & 1;
	header->invalidPID = data[0] & 1;
	header->pid = (uint16_t)((data[1] << 8) | data[2]);
	return true;
}


static inline size_t
BuildAVCTP(uint8_t* out, size_t size, uint8_t label, bool response)
{
	if (size < 3)
		return 0;
	out[0] = (uint8_t)(((label & 0x0f) << 4) | (kPacketSingle << 2)
		| (response ? 2 : 0));
	out[1] = (uint8_t)(kAVRemoteControlPID >> 8);
	out[2] = (uint8_t)kAVRemoteControlPID;
	return 3;
}


struct AVCFrame {
	uint8_t			ctype;		// command type or response code
	uint8_t			subunit;
	uint8_t			opcode;
	const uint8_t*	operands;
	size_t			operandLength;
};


static inline bool
ParseAVC(const uint8_t* data, size_t size, AVCFrame* frame)
{
	if (data == NULL || size < 3)
		return false;
	frame->ctype = data[0] & 0x0f;
	frame->subunit = data[1];
	frame->opcode = data[2];
	frame->operands = data + 3;
	frame->operandLength = size - 3;
	return true;
}


struct PassThrough {
	uint8_t		operation;
	bool		released;
};


static inline bool
ParsePassThrough(const AVCFrame& frame, PassThrough* passThrough)
{
	if (frame.opcode != kOpcodePassThrough || frame.operandLength < 2)
		return false;
	passThrough->operation = frame.operands[0] & 0x7f;
	passThrough->released = (frame.operands[0] >> 7) & 1;
	// operand 1 is the length of the operation data that follows
	return (size_t)frame.operands[1] <= frame.operandLength - 2;
}


struct VendorPDU {
	uint8_t			pdu;
	uint8_t			packetType;
	uint16_t		length;
	const uint8_t*	parameters;
};


// AVRCP 1.6, 6.3.1: company ID (3 bytes), PDU ID, packet type, parameter
// length (2 bytes, big-endian), parameters.
static inline bool
ParseVendor(const AVCFrame& frame, VendorPDU* pdu)
{
	if (frame.opcode != kOpcodeVendorDependent || frame.operandLength < 7)
		return false;
	uint32_t company = ((uint32_t)frame.operands[0] << 16)
		| ((uint32_t)frame.operands[1] << 8) | frame.operands[2];
	if (company != kBluetoothSIGCompanyID)
		return false;
	pdu->pdu = frame.operands[3];
	pdu->packetType = frame.operands[4] & 3;
	pdu->length = (uint16_t)((frame.operands[5] << 8) | frame.operands[6]);
	pdu->parameters = frame.operands + 7;
	if (pdu->packetType != kPacketSingle)
		return false;
	return (size_t)pdu->length <= frame.operandLength - 7;
}


// An AVRCP vendor dependent message: AVCTP header, AV/C header, PDU.
static inline size_t
BuildVendor(uint8_t* out, size_t size, uint8_t label, bool response,
	uint8_t ctype, uint8_t pdu, const uint8_t* parameters, uint16_t length)
{
	size_t need = 3 + 3 + 7 + (size_t)length;
	if (size < need || (length > 0 && parameters == NULL))
		return 0;
	BuildAVCTP(out, size, label, response);
	out[3] = ctype;
	out[4] = kSubunitPanel;
	out[5] = kOpcodeVendorDependent;
	out[6] = (uint8_t)(kBluetoothSIGCompanyID >> 16);
	out[7] = (uint8_t)(kBluetoothSIGCompanyID >> 8);
	out[8] = (uint8_t)kBluetoothSIGCompanyID;
	out[9] = pdu;
	out[10] = kPacketSingle;
	out[11] = (uint8_t)(length >> 8);
	out[12] = (uint8_t)length;
	if (length > 0)
		memcpy(out + 13, parameters, length);
	return need;
}


// AVRCP volume is 0 .. 0x7f (6.13.1); the top bit is reserved.
static inline uint8_t
VolumeFromGain(float gain)
{
	if (!(gain > 0.0f))
		return 0;
	if (gain >= 1.0f)
		return 0x7f;
	return (uint8_t)(gain * 127.0f + 0.5f);
}


static inline float
GainFromVolume(uint8_t volume)
{
	return (float)(volume & 0x7f) / 127.0f;
}


}	// namespace a2dp


#endif	// R5_A2DP_PROTO_H
