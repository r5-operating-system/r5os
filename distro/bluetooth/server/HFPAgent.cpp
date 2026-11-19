/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * HFP Audio Gateway AT commands (HFPAgent.h). Written from Hands-Free
 * Profile 1.8 and Apple's accessory design guidelines.
 */


#include "HFPAgent.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>


namespace r5hfp {


// A command line longer than this is refused.
static const size_t kMaxLine = 200;

// The AG indicators (HFP 1.8, 4.34.2), in this order in +CIND.
static const char* const kIndicators = "+CIND: (\"service\",(0,1)),"
	"(\"call\",(0,1)),(\"callsetup\",(0-3)),(\"callheld\",(0-2)),"
	"(\"signal\",(0-5)),(\"roam\",(0,1)),(\"battchg\",(0-5))";
// No network service, no call, full signal and charge: R5 is a computer.
static const char* const kIndicatorValues = "+CIND: 0,0,0,0,5,0,5";


namespace {

struct Answer {
	char*	buffer;
	size_t	size;
	size_t	length;
	bool	full;

	void Add(const char* format, ...)
	{
		if (full)
			return;
		va_list args;
		va_start(args, format);
		int count = vsnprintf(buffer + length, size - length, format, args);
		va_end(args);
		if (count < 0 || (size_t)count >= size - length) {
			full = true;
			return;
		}
		length += count;
	}

	void Line(const char* text) { Add("\r\n%s\r\n", text); }
	void Ok() { Line("OK"); }
	void Error() { Line("ERROR"); }
};

}	// namespace


int
ParseNumbers(const char* text, size_t length, long* values, int count)
{
	int found = 0;
	size_t i = 0;
	while (true) {
		while (i < length && text[i] == ' ')
			i++;
		if (i >= length || text[i] < '0' || text[i] > '9')
			return -1;
		long value = 0;
		while (i < length && text[i] >= '0' && text[i] <= '9') {
			value = value * 10 + (text[i] - '0');
			if (value > 999999)
				return -1;
			i++;
		}
		if (found >= count)
			return -1;
		values[found++] = value;
		while (i < length && text[i] == ' ')
			i++;
		if (i == length)
			return found;
		if (text[i] != ',')
			return -1;
		i++;
	}
}


static bool
starts(const char* command, size_t length, const char* prefix,
	const char** rest, size_t* restLength)
{
	size_t prefixLength = strlen(prefix);
	if (length < prefixLength || memcmp(command, prefix, prefixLength) != 0)
		return false;
	*rest = command + prefixLength;
	*restLength = length - prefixLength;
	return true;
}


static bool
is(const char* command, size_t length, const char* text)
{
	return length == strlen(text) && memcmp(command, text, length) == 0;
}


Agent::Agent(bool codecNegotiation)
	:
	fServiceLevel(false),
	fBatteryIndicator(false),
	fBattery(-1),
	fFeatures(0),
	fSpeaker(-1),
	fMicrophone(-1),
	fEvents(0),
	fButton(""),
	fUnknown(0),
	fCodecNegotiation(codecNegotiation),
	fConfirmedCodec(0)
{
	fCodecs[0] = '\0';
	fLastUnknown[0] = '\0';
}


bool
Agent::CanNegotiateCodecs() const
{
	return fCodecNegotiation
		&& (fFeatures & kHandsFreeCodecNegotiation) != 0
		&& fCodecs[0] != '\0';
}


uint32_t
Agent::TakeEvents()
{
	uint32_t events = fEvents;
	fEvents = 0;
	return events;
}


size_t
Agent::Handle(const char* line, size_t length, char* out, size_t outSize)
{
	if (out == NULL || outSize < kMaxAnswer)
		return 0;
	Answer answer = { out, outSize, 0, false };
	if (line == NULL)
		length = 0;
	out[0] = '\0';

	// The command in upper case, without spaces around it.
	char command[kMaxLine + 1];
	size_t start = 0;
	while (start < length && (line[start] == ' ' || line[start] == '\n'))
		start++;
	size_t end = length;
	while (end > start && (line[end - 1] == ' ' || line[end - 1] == '\r'))
		end--;
	if (end - start > kMaxLine || end - start < 2) {
		answer.Error();
		return answer.length;
	}
	for (size_t i = start; i < end; i++) {
		char c = line[i];
		command[i - start] = c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
	}
	size_t commandLength = end - start;
	command[commandLength] = '\0';
	if (command[0] != 'A' || command[1] != 'T') {
		answer.Error();
		return answer.length;
	}
	const char* c = command + 2;
	size_t n = commandLength - 2;
	const char* rest;
	size_t restLength;
	long values[16];

	if (starts(c, n, "+BRSF=", &rest, &restLength)) {
		if (ParseNumbers(rest, restLength, values, 1) != 1) {
			answer.Error();
			return answer.length;
		}
		fFeatures = (uint32_t)values[0];
		answer.Add("\r\n+BRSF: %u\r\n", (unsigned)(kGatewayFeatures
			| (fCodecNegotiation ? kGatewayCodecNegotiation : 0)));
		answer.Ok();
	} else if (starts(c, n, "+BAC=", &rest, &restLength)) {
		// The headset's codecs (HFP 1.8, 4.34.2): kept for +BCS.
		if (ParseNumbers(rest, restLength, values, 16) < 1
			|| restLength > kMaxCodecList) {
			answer.Error();
			return answer.length;
		}
		memcpy(fCodecs, rest, restLength);
		fCodecs[restLength] = '\0';
		answer.Ok();
	} else if (fCodecNegotiation && starts(c, n, "+BCS=", &rest, &restLength)) {
		// The headset confirms the codec of the AG's +BCS (HFP 1.8, 4.11.3).
		if (ParseNumbers(rest, restLength, values, 1) != 1
			|| values[0] < 1 || values[0] > 255) {
			answer.Error();
			return answer.length;
		}
		fConfirmedCodec = (int)values[0];
		fEvents |= kEventCodec;
		answer.Ok();
	} else if (fCodecNegotiation && is(c, n, "+BCC")) {
		// The headset asks the AG to start a codec connection.
		fEvents |= kEventCodecRequest;
		answer.Ok();
	} else if (is(c, n, "+CIND=?")) {
		answer.Line(kIndicators);
		answer.Ok();
	} else if (is(c, n, "+CIND?")) {
		answer.Line(kIndicatorValues);
		answer.Ok();
	} else if (starts(c, n, "+CMER=", &rest, &restLength)) {
		answer.Ok();
		// Without three-way calling the SLC is up now (HFP 1.8, 4.2.1.6).
		// With HF indicators on both sides BIND follows; R5 does not wait
		// for it, so a headset that skips BIND still gets its session.
		if (!fServiceLevel) {
			fServiceLevel = true;
			fEvents |= kEventServiceLevel;
		}
	} else if (is(c, n, "+CHLD=?")) {
		answer.Line("+CHLD: (0)");
		answer.Ok();
	} else if (is(c, n, "+BIND=?")) {
		// The HF indicators the AG takes: only the battery.
		answer.Add("\r\n+BIND: (%d)\r\n", kIndicatorBattery);
		answer.Ok();
	} else if (is(c, n, "+BIND?")) {
		answer.Add("\r\n+BIND: %d,1\r\n", kIndicatorBattery);
		answer.Ok();
	} else if (starts(c, n, "+BIND=", &rest, &restLength)) {
		int count = ParseNumbers(rest, restLength, values, 16);
		if (count < 1) {
			answer.Error();
			return answer.length;
		}
		for (int i = 0; i < count; i++) {
			if (values[i] == kIndicatorBattery)
				fBatteryIndicator = true;
		}
		answer.Ok();
	} else if (starts(c, n, "+BIEV=", &rest, &restLength)) {
		if (ParseNumbers(rest, restLength, values, 2) != 2
			|| values[0] != kIndicatorBattery || values[1] > 100) {
			answer.Error();
			return answer.length;
		}
		fBattery = (int)values[1];
		fEvents |= kEventBattery;
		answer.Ok();
	} else if (starts(c, n, "+XAPL=", &rest, &restLength)) {
		// The Apple extension: R5 takes only the battery report (bit 1).
		// "iPhone" is the fixed token of that protocol's answer.
		answer.Line("+XAPL=iPhone,2");
		answer.Ok();
	} else if (starts(c, n, "+IPHONEACCEV=", &rest, &restLength)) {
		int count = ParseNumbers(rest, restLength, values, 16);
		if (count < 1 || count != 1 + 2 * values[0]) {
			answer.Error();
			return answer.length;
		}
		for (int i = 0; i < values[0]; i++) {
			long key = values[1 + 2 * i];
			long value = values[2 + 2 * i];
			// Key 1 is the battery, 0 to 9 for 10 % to 100 %.
			if (key == 1 && value <= 9) {
				fBattery = (int)((value + 1) * 10);
				fEvents |= kEventBattery;
			}
		}
		answer.Ok();
	} else if (starts(c, n, "+VGS=", &rest, &restLength)
		|| starts(c, n, "+VGM=", &rest, &restLength)) {
		if (ParseNumbers(rest, restLength, values, 1) != 1
			|| values[0] > 15) {
			answer.Error();
			return answer.length;
		}
		if (c[3] == 'S')
			fSpeaker = (int)values[0];
		else
			fMicrophone = (int)values[0];
		fEvents |= kEventVolume;
		answer.Ok();
	} else if (starts(c, n, "+CKPD=", &rest, &restLength)) {
		// The headset profile's button.
		fButton = "headset";
		fEvents |= kEventButton;
		answer.Ok();
	} else if (is(c, n, "A") || is(c, n, "+CHUP")
		|| starts(c, n, "+BLDN", &rest, &restLength)
		|| starts(c, n, "+BVRA=", &rest, &restLength)
		|| (n >= 1 && c[0] == 'D')) {
		// Buttons for calls: there is no call, so they fail (HFP 1.8,
		// 4.13 to 4.25), and the log shows them.
		fButton = is(c, n, "A") ? "answer" : is(c, n, "+CHUP") ? "hang up"
			: c[0] == 'D' ? "dial" : c[2] == 'L' ? "redial" : "voice";
		fEvents |= kEventButton;
		answer.Error();
	} else if (starts(c, n, "+NREC=", &rest, &restLength)
		|| starts(c, n, "+CMEE=", &rest, &restLength)
		|| starts(c, n, "+CLIP=", &rest, &restLength)
		|| starts(c, n, "+CCWA=", &rest, &restLength)
		|| starts(c, n, "+COPS=", &rest, &restLength)
		|| starts(c, n, "+BIA=", &rest, &restLength)
		|| starts(c, n, "+BCS=", &rest, &restLength)
		|| is(c, n, "+CLCC") || is(c, n, "+CNUM") || is(c, n, "+BTRH?")) {
		// Taken, nothing to report: no calls, no number.
		answer.Ok();
	} else if (n == 0) {
		// A bare AT, which some units send to check the link.
		answer.Ok();
	} else if (is(c, n, "+COPS?")) {
		answer.Line("+COPS: 0");
		answer.Ok();
	} else {
		// Hands-Free Profile 1.8 answers a command it does not define
		// with ERROR. The text is kept so the log can name it.
		size_t keep = n < sizeof(fLastUnknown) - 1 ? n
			: sizeof(fLastUnknown) - 1;
		size_t out = 0;
		for (size_t i = 0; i < keep; i++) {
			unsigned char ch = (unsigned char)c[i];
			fLastUnknown[out++] = (ch >= 32 && ch < 127) ? (char)ch : '?';
		}
		fLastUnknown[out] = '\0';
		fUnknown++;
		answer.Error();
	}
	return answer.full ? 0 : answer.length;
}


}	// namespace r5hfp
