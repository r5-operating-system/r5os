/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * The Audio Gateway side of the HFP service level connection
 * (Hands-Free Profile 1.8, docs/BLUETOOTH.md step 7b), without audio.
 * Written from the HFP specification and, for the battery that many
 * headsets send the Apple way, Apple's published accessory design
 * guidelines (AT+XAPL, AT+IPHONEACCEV).
 *
 * No I/O and no Haiku headers: the test and the host fuzzer
 * (scripts/bt-hfp-fuzz.cpp) build it unchanged. HFPGateway.h runs it on
 * an RFCOMMLink.
 */
#ifndef R5_HFP_AGENT_H
#define R5_HFP_AGENT_H


#include <stddef.h>
#include <stdint.h>


namespace r5hfp {


// The AG's features in +BRSF (HFP 1.8, 4.34.1): HF indicators (bit 10)
// only. No calls, no voice recognition, no codec negotiation (there is no
// voice path before 7c), and no extended error codes: failures are a plain
// ERROR, so +CME ERROR is never owed after AT+CMEE.
static const uint32_t kGatewayFeatures = 0x0400;
// HF indicator 2 is the battery level, 0 to 100 (Bluetooth Assigned
// Numbers, HFP HF indicators).
static const int kIndicatorBattery = 2;

// An answer never needs more than this.
static const size_t kMaxAnswer = 256;

enum {
	kEventServiceLevel	= 0x01,		// the SLC is up (AT+CMER answered)
	kEventBattery		= 0x02,
	kEventButton		= 0x04,
	kEventVolume		= 0x08
};


class Agent {
public:
						Agent();

	// One AT command line from the Hands-Free unit, without its CR.
	// Writes the AG's answer (each line in CR LF, as HFP 1.8, 4.33.1
	// asks) into out, at most outSize - 1 bytes and a NUL, and returns its
	// length; 0 when out is too small.
			size_t		Handle(const char* line, size_t length, char* out,
							size_t outSize);

			bool		ServiceLevel() const { return fServiceLevel; }
	// The battery in percent, -1 while the headset has not sent one.
			int			Battery() const { return fBattery; }
			uint32_t	HandsFreeFeatures() const { return fFeatures; }
	// 0 to 15, -1 before the first AT+VGS or AT+VGM.
			int			SpeakerVolume() const { return fSpeaker; }
			int			MicrophoneVolume() const { return fMicrophone; }
	// The kEvent bits since the last call.
			uint32_t	TakeEvents();
	// The last button, a fixed English word for the log: "answer",
	// "hang up", "redial", "voice", "dial" or "headset".
			const char*	LastButton() const { return fButton; }
			uint32_t	Unknown() const { return fUnknown; }

private:
			bool		fServiceLevel;
			bool		fBatteryIndicator;
			int			fBattery;
			uint32_t	fFeatures;
			int			fSpeaker;
			int			fMicrophone;
			uint32_t	fEvents;
			const char*	fButton;
			uint32_t	fUnknown;
};


// Reads up to count comma-separated decimal numbers from text (spaces
// allowed); returns how many it read, or -1 on anything else. Values
// above 999999 are refused.
int			ParseNumbers(const char* text, size_t length, long* values,
				int count);


}	// namespace r5hfp


#endif	// R5_HFP_AGENT_H
