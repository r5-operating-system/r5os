/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * RFCOMM frames, multiplexer control and the session state machine
 * (RFCOMMMux.h). Written from Bluetooth RFCOMM 1.2 and ETSI TS 07.10.
 */


#include "RFCOMMMux.h"

#include <string.h>


namespace r5rfcomm {


// The 128-bit Bluetooth base UUID after its first four bytes (Core 5.4,
// Vol 3, Part B, 2.5.1).
static const uint8_t kBaseUUIDTail[12] = {
	0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb
};
static const uint16_t kUUIDRFCOMM = 0x0003;

// V.24 signals R5 sends in MSC: EA, RTC, RTR and DV (TS 07.10, 5.4.6.3.7).
static const uint8_t kSignals = 0x8d;
static const uint8_t kSignalFC = 0x02;
// Top the other side's credits up when it has this many or fewer.
static const int kCreditLow = 3;
// More credits than this are not counted.
static const int kCreditCap = 4096;


uint8_t
Fcs(const uint8_t* data, size_t length)
{
	uint8_t crc = 0xff;
	for (size_t i = 0; i < length; i++) {
		crc ^= data[i];
		for (int bit = 0; bit < 8; bit++)
			crc = (crc & 1) != 0 ? (uint8_t)((crc >> 1) ^ 0xe0) : crc >> 1;
	}
	return (uint8_t)(0xff - crc);
}


static bool
known_type(uint8_t type)
{
	return type == kSABM || type == kUA || type == kDM || type == kDISC
		|| type == kUIH;
}


bool
ParseFrame(const uint8_t* packet, size_t size, bool creditBytes,
	Frame* frame)
{
	if (packet == NULL || frame == NULL || size < 4)
		return false;
	// One address byte: EA set (RFCOMM 1.2, 5.1.1).
	if ((packet[0] & 0x01) == 0)
		return false;
	frame->dlci = packet[0] >> 2;
	frame->commandResponse = (packet[0] & 0x02) != 0;
	frame->pollFinal = (packet[1] & kPF) != 0;
	frame->type = packet[1] & ~kPF;
	if (!known_type(frame->type))
		return false;

	size_t position;
	size_t length = packet[2] >> 1;
	if ((packet[2] & 0x01) != 0)
		position = 3;
	else {
		if (size < 5)
			return false;
		length |= (size_t)packet[3] << 7;
		position = 4;
	}
	const size_t headerEnd = position;

	frame->hasCredits = frame->type == kUIH && frame->pollFinal
		&& frame->dlci != 0 && creditBytes;
	frame->credits = 0;
	if (frame->hasCredits) {
		if (position >= size)
			return false;
		frame->credits = packet[position++];
	}
	if (size - position < 1 || size - position - 1 != length)
		return false;
	// Only UIH frames carry data here.
	if (frame->type != kUIH && length != 0)
		return false;

	// UIH: the FCS covers the address and control fields only
	// (TS 07.10, 5.3.6; RFCOMM 1.2, 5.1.1).
	size_t covered = frame->type == kUIH ? 2 : headerEnd;
	if (Fcs(packet, covered) != packet[size - 1])
		return false;

	frame->data = packet + position;
	frame->length = length;
	return true;
}


size_t
BuildFrame(uint8_t* out, size_t outSize, uint8_t dlci, bool commandResponse,
	uint8_t type, bool pollFinal, int credits, const uint8_t* data,
	size_t length)
{
	if (out == NULL || dlci > 63 || length > kMaxN1 || credits > 255
		|| (length > 0 && data == NULL))
		return 0;
	size_t lengthBytes = length > 127 ? 2 : 1;
	size_t need = 2 + lengthBytes + (credits >= 0 ? 1 : 0) + length + 1;
	if (need > outSize)
		return 0;

	out[0] = (uint8_t)((dlci << 2) | (commandResponse ? 0x02 : 0) | 0x01);
	out[1] = (uint8_t)(type | (pollFinal ? kPF : 0));
	if (lengthBytes == 1)
		out[2] = (uint8_t)((length << 1) | 0x01);
	else {
		out[2] = (uint8_t)((length << 1) & 0xfe);
		out[3] = (uint8_t)(length >> 7);
	}
	size_t position = 2 + lengthBytes;
	if (credits >= 0)
		out[position++] = (uint8_t)credits;
	if (length > 0)
		memcpy(out + position, data, length);
	position += length;
	out[position] = Fcs(out, type == kUIH ? 2 : 2 + lengthBytes);
	return position + 1;
}


bool
ParseMcc(const uint8_t* data, size_t size, Mcc* mcc, size_t* used)
{
	if (data == NULL || mcc == NULL || used == NULL || size < 2)
		return false;
	// One type byte: EA set (TS 07.10, 5.4.6.1).
	if ((data[0] & 0x01) == 0)
		return false;
	mcc->command = (data[0] & 0x02) != 0;
	mcc->type = data[0] & ~0x02;

	size_t position;
	size_t length = data[1] >> 1;
	if ((data[1] & 0x01) != 0)
		position = 2;
	else {
		if (size < 3 || (data[2] & 0x01) == 0)
			return false;
		length |= (size_t)(data[2] >> 1) << 7;
		position = 3;
	}
	if (length > size - position)
		return false;
	mcc->value = data + position;
	mcc->length = length;
	*used = position + length;
	return true;
}


size_t
BuildMcc(uint8_t* out, size_t outSize, uint8_t type, bool command,
	const uint8_t* value, size_t length)
{
	if (out == NULL || length > 127 || (length > 0 && value == NULL)
		|| outSize < 2 + length)
		return 0;
	out[0] = (uint8_t)(type | (command ? 0x02 : 0) | 0x01);
	out[1] = (uint8_t)((length << 1) | 0x01);
	if (length > 0)
		memcpy(out + 2, value, length);
	return 2 + length;
}


bool
ParseNegotiation(const uint8_t* value, size_t length,
	Negotiation* negotiation)
{
	if (value == NULL || negotiation == NULL || length < kNegotiationSize)
		return false;
	negotiation->dlci = value[0] & 0x3f;
	negotiation->convergence = value[1] >> 4;
	negotiation->priority = value[2] & 0x3f;
	negotiation->maxFrame = (uint16_t)(value[4] | (value[5] << 8));
	negotiation->credits = value[7] & 0x07;
	return true;
}


void
BuildNegotiation(uint8_t out[kNegotiationSize],
	const Negotiation& negotiation)
{
	out[0] = negotiation.dlci & 0x3f;
	// Frame type UIH (I bits 0) and the convergence layer.
	out[1] = (uint8_t)(negotiation.convergence << 4);
	out[2] = negotiation.priority & 0x3f;
	out[3] = 0;		// T1, not used by RFCOMM
	out[4] = (uint8_t)(negotiation.maxFrame & 0xff);
	out[5] = (uint8_t)(negotiation.maxFrame >> 8);
	out[6] = 0;		// N2, not used by RFCOMM
	out[7] = negotiation.credits & 0x07;
}


// One SDP data element at *offset (Core 5.4, Vol 3, Part B, 3): its type
// and where its value is. Moves *offset past it.
static bool
next_element(const uint8_t* data, size_t size, size_t* offset,
	uint8_t* type, const uint8_t** value, size_t* length)
{
	if (*offset >= size)
		return false;
	uint8_t header = data[(*offset)++];
	*type = header >> 3;
	uint8_t index = header & 0x07;
	size_t valueLength;
	if (*type == 0) {
		if (index != 0)
			return false;
		valueLength = 0;
	} else if (index <= 4) {
		static const size_t kSizes[5] = { 1, 2, 4, 8, 16 };
		valueLength = kSizes[index];
	} else {
		size_t lengthBytes = index == 5 ? 1 : index == 6 ? 2 : 4;
		if (size - *offset < lengthBytes)
			return false;
		valueLength = 0;
		for (size_t i = 0; i < lengthBytes; i++)
			valueLength = (valueLength << 8) | data[(*offset)++];
	}
	if (valueLength > size - *offset)
		return false;
	*value = data + *offset;
	*length = valueLength;
	*offset += valueLength;
	return true;
}


// A UUID element as a 16-bit UUID; false for any other value.
static bool
short_uuid(uint8_t type, const uint8_t* value, size_t length,
	uint16_t* uuid)
{
	if (type != 3)
		return false;
	if (length == 2) {
		*uuid = (uint16_t)((value[0] << 8) | value[1]);
		return true;
	}
	if ((length == 4 || length == 16) && value[0] == 0 && value[1] == 0) {
		if (length == 16 && memcmp(value + 4, kBaseUUIDTail,
				sizeof(kBaseUUIDTail)) != 0)
			return false;
		*uuid = (uint16_t)((value[2] << 8) | value[3]);
		return true;
	}
	return false;
}


uint8_t
FindChannel(const uint8_t* classList, size_t classLength,
	const uint8_t* protocolList, size_t protocolLength,
	uint16_t serviceClass)
{
	if (classList == NULL || protocolList == NULL)
		return 0;

	bool found = false;
	size_t offset = 0;
	while (!found && offset < classLength) {
		uint8_t type;
		const uint8_t* value;
		size_t length;
		if (!next_element(classList, classLength, &offset, &type, &value,
				&length))
			return 0;
		uint16_t uuid;
		if (short_uuid(type, value, length, &uuid) && uuid == serviceClass)
			found = true;
	}
	if (!found)
		return 0;

	// A sequence of protocol descriptors, each a sequence: the protocol
	// UUID, then its parameters. RFCOMM's is the server channel, a uint8.
	offset = 0;
	while (offset < protocolLength) {
		uint8_t type;
		const uint8_t* descriptor;
		size_t descriptorLength;
		if (!next_element(protocolList, protocolLength, &offset, &type,
				&descriptor, &descriptorLength))
			return 0;
		if (type != 6)
			continue;
		size_t inner = 0;
		const uint8_t* value;
		size_t length;
		uint16_t uuid;
		if (!next_element(descriptor, descriptorLength, &inner, &type,
				&value, &length)
			|| !short_uuid(type, value, length, &uuid)
			|| uuid != kUUIDRFCOMM)
			continue;
		if (!next_element(descriptor, descriptorLength, &inner, &type,
				&value, &length)
			|| type != 1 || length != 1)
			return 0;
		if (value[0] < kMinChannel || value[0] > kMaxChannel)
			return 0;
		return value[0];
	}
	return 0;
}


// #pragma mark - Mux


Mux::Mux(bool initiator, Output output, void* cookie)
	:
	fInitiator(initiator),
	fOutput(output),
	fCookie(cookie),
	fState(kIdle),
	fError(kNoError),
	fSessionOpen(false),
	fClosingSession(false),
	fDLCI(0),
	fListenDLCI(0),
	fCreditFlow(false),
	fMaxFrame(kDefaultN1),
	fSendCredits(0),
	fGivenCredits(0),
	fRemoteMSC(false),
	fRemoteStopped(false),
	fAggregateStopped(false),
	fDeadline(0),
	fStart(0),
	fReceived(0)
{
	memset(&fCounters, 0, sizeof(fCounters));
}


bool
Mux::Open(uint8_t channel, uint64_t now)
{
	if (!fInitiator || fState != kIdle || channel < kMinChannel
		|| channel > kMaxChannel)
		return false;
	// The initiator of the session sets the direction bit D to 0 for a
	// server channel on the responder (RFCOMM 1.2, 5.4).
	fDLCI = (uint8_t)(channel << 1);
	fState = kStarting;
	_SendFrame(0, true, kSABM, true, -1, NULL, 0);
	_Wait(now);
	return true;
}


bool
Mux::Listen(uint8_t channel)
{
	if (fInitiator || fState != kIdle || channel < kMinChannel
		|| channel > kMaxChannel)
		return false;
	fListenDLCI = (uint8_t)(channel << 1);
	return true;
}


void
Mux::Receive(const uint8_t* packet, size_t size, uint64_t now)
{
	if (packet == NULL || size == 0) {
		fCounters.badFrames++;
		return;
	}
	uint8_t dlci = packet[0] >> 2;
	Frame frame;
	if (!ParseFrame(packet, size, fCreditFlow && dlci != 0 && dlci == fDLCI,
			&frame)) {
		fCounters.badFrames++;
		return;
	}
	if (fState == kClosed || fState == kFailed) {
		// The DLC is gone, but DLCI 0 may still be open: the other side
		// closes it next.
		if (frame.type == kDISC) {
			bool session = frame.dlci == 0 && fSessionOpen;
			_SendFrame(frame.dlci, false, session ? kUA : kDM, true, -1,
				NULL, 0);
			if (session)
				fSessionOpen = false;
		} else if (frame.type == kSABM)
			_SendFrame(frame.dlci, false, kDM, true, -1, NULL, 0);
		return;
	}

	const bool dataDLC = frame.dlci != 0 && frame.dlci == fDLCI;
	const bool dlcUp = fState == kSignalling || fState == kOpen;

	switch (frame.type) {
		case kSABM:
			if (frame.dlci == 0) {
				// The initiator never takes the session the other way.
				if (fInitiator || fSessionOpen) {
					_SendFrame(0, false, fInitiator ? kDM : kUA, true, -1,
						NULL, 0);
					break;
				}
				fSessionOpen = true;
				_SendFrame(0, false, kUA, true, -1, NULL, 0);
				break;
			}
			if (!fInitiator && fSessionOpen && fState == kIdle
				&& fListenDLCI != 0 && frame.dlci == fListenDLCI) {
				fDLCI = frame.dlci;
				_SendFrame(fDLCI, false, kUA, true, -1, NULL, 0);
				_SendSignals(now);
				break;
			}
			_SendFrame(frame.dlci, false, kDM, true, -1, NULL, 0);
			break;

		case kUA:
			if (fState == kStarting && frame.dlci == 0) {
				fSessionOpen = true;
				_SendNegotiation(now);
			} else if (fState == kConnecting && dataDLC) {
				_SendSignals(now);
			} else if (fState == kClosing) {
				if (!fClosingSession && dataDLC) {
					fClosingSession = true;
					_SendFrame(0, true, kDISC, true, -1, NULL, 0);
					_Wait(now);
				} else if (fClosingSession && frame.dlci == 0) {
					fSessionOpen = false;
					fState = kClosed;
					fDeadline = 0;
				}
			}
			break;

		case kDM:
			if (fState == kStarting && frame.dlci == 0)
				_Fail(kRefused);
			else if ((fState == kNegotiating || fState == kConnecting) && dataDLC)
				_Fail(kRefused);
			else if (fState == kClosing) {
				// Already closed on that side.
				if (!fClosingSession && dataDLC) {
					fClosingSession = true;
					_SendFrame(0, true, kDISC, true, -1, NULL, 0);
					_Wait(now);
				} else if (fClosingSession && frame.dlci == 0) {
					fSessionOpen = false;
					fState = kClosed;
					fDeadline = 0;
				}
			} else if ((dataDLC && dlcUp)
				|| (frame.dlci == 0 && fSessionOpen))
				_Fail(kRemoteClosed);
			break;

		case kDISC:
			if (frame.dlci == 0) {
				if (!fSessionOpen) {
					_SendFrame(0, false, kDM, true, -1, NULL, 0);
					break;
				}
				_SendFrame(0, false, kUA, true, -1, NULL, 0);
				fSessionOpen = false;
				if (fState == kClosing)
					fState = kClosed;
				else
					_Fail(kRemoteClosed);
				fDeadline = 0;
				break;
			}
			if (dataDLC && (dlcUp || fState == kClosing)) {
				_SendFrame(fDLCI, false, kUA, true, -1, NULL, 0);
				if (fState == kClosing) {
					// Both closed it at once: go on with DLCI 0.
					if (!fClosingSession) {
						fClosingSession = true;
						_SendFrame(0, true, kDISC, true, -1, NULL, 0);
						_Wait(now);
					}
				} else
					_Fail(kRemoteClosed);
				break;
			}
			_SendFrame(frame.dlci, false, kDM, true, -1, NULL, 0);
			break;

		case kUIH:
			if (frame.dlci == 0) {
				if (fSessionOpen)
					_Control(frame, now);
			} else if (dataDLC && (dlcUp || fState == kClosing))
				_Data(frame);
			else
				fCounters.droppedData++;
			break;
	}
}


void
Mux::Tick(uint64_t now)
{
	if (fDeadline == 0 || now < fDeadline)
		return;
	if (fState == kClosing) {
		fState = kClosed;
		fError = kTimedOut;
		fDeadline = 0;
		return;
	}
	_Fail(kTimedOut);
}


void
Mux::Close(uint64_t now)
{
	switch (fState) {
		case kIdle:
			if (fSessionOpen) {
				fState = kClosing;
				fClosingSession = true;
				_SendFrame(0, true, kDISC, true, -1, NULL, 0);
				_Wait(now);
			} else
				fState = kClosed;
			break;
		case kStarting:
		case kNegotiating:
			fState = kClosing;
			fClosingSession = true;
			_SendFrame(0, true, kDISC, true, -1, NULL, 0);
			_Wait(now);
			break;
		case kConnecting:
		case kSignalling:
		case kOpen:
			fState = kClosing;
			fClosingSession = false;
			_SendFrame(fDLCI, true, kDISC, true, -1, NULL, 0);
			_Wait(now);
			break;
		default:
			break;
	}
}


size_t
Mux::SendSpace() const
{
	if (fState != kOpen || fAggregateStopped)
		return 0;
	if (fCreditFlow)
		return fSendCredits > 0 ? fMaxFrame : 0;
	return fRemoteStopped ? 0 : fMaxFrame;
}


bool
Mux::Send(const uint8_t* data, size_t length)
{
	if (data == NULL || length == 0 || length > SendSpace())
		return false;
	_SendFrame(fDLCI, true, kUIH, false, -1, data, length);
	if (fCreditFlow)
		fSendCredits--;
	return true;
}


size_t
Mux::Take(uint8_t* out, size_t size)
{
	size_t count = size < fReceived ? size : fReceived;
	for (size_t i = 0; i < count; i++)
		out[i] = fBuffer[(fStart + i) % kReceiveBuffer];
	fStart = (fStart + count) % kReceiveBuffer;
	fReceived -= count;
	if (count > 0)
		_GiveCredits();
	return count;
}


void
Mux::_SendFrame(uint8_t dlci, bool isCommand, uint8_t type, bool pollFinal,
	int credits, const uint8_t* data, size_t length)
{
	uint8_t frame[256];
	size_t size = BuildFrame(frame, sizeof(frame), dlci, _Command(isCommand),
		type, pollFinal, credits, data, length);
	if (size > 0 && fOutput != NULL)
		fOutput(frame, size, fCookie);
}


void
Mux::_SendMcc(uint8_t type, bool command, const uint8_t* value,
	size_t length)
{
	uint8_t message[2 + 127];
	size_t size = BuildMcc(message, sizeof(message), type, command, value,
		length);
	if (size > 0)
		_SendFrame(0, true, kUIH, false, -1, message, size);
}


void
Mux::_Fail(Error error)
{
	fState = error == kRemoteClosed ? kClosed : kFailed;
	fError = error;
	fDeadline = 0;
}


void
Mux::_Wait(uint64_t now)
{
	fDeadline = now + kTimeout;
}


void
Mux::_SendNegotiation(uint64_t now)
{
	Negotiation negotiation;
	negotiation.dlci = fDLCI;
	negotiation.convergence = 0x0f;
	negotiation.priority = 0;
	negotiation.maxFrame = kDefaultN1;
	negotiation.credits = kInitialCredits;
	uint8_t value[kNegotiationSize];
	BuildNegotiation(value, negotiation);
	fState = kNegotiating;
	_SendMcc(kMccPN, true, value, sizeof(value));
	_Wait(now);
}


void
Mux::_SendSignals(uint64_t now)
{
	uint8_t value[2] = { (uint8_t)((fDLCI << 2) | 0x03), kSignals };
	fState = kSignalling;
	_SendMcc(kMccMSC, true, value, sizeof(value));
	_Wait(now);
}


void
Mux::_GiveCredits()
{
	if (!fCreditFlow || (fState != kSignalling && fState != kOpen)
		|| fGivenCredits > kCreditLow || fMaxFrame == 0)
		return;
	// Never more credit than room: the data in the buffer and every frame
	// the other side may still send fit.
	int frames = (int)((kReceiveBuffer - fReceived) / fMaxFrame);
	int give = frames - fGivenCredits;
	if (give > 255)
		give = 255;
	if (give <= 0)
		return;
	_SendFrame(fDLCI, true, kUIH, true, give, NULL, 0);
	fGivenCredits += give;
}


void
Mux::_Control(const Frame& frame, uint64_t now)
{
	size_t position = 0;
	while (position < frame.length) {
		Mcc mcc;
		size_t used;
		if (!ParseMcc(frame.data + position, frame.length - position, &mcc,
				&used)) {
			fCounters.badFrames++;
			return;
		}
		_ControlMessage(mcc, now);
		if (fState == kClosed || fState == kFailed)
			return;
		position += used;
	}
}


void
Mux::_ControlMessage(const Mcc& mcc, uint64_t now)
{
	switch (mcc.type) {
		case kMccPN:
		{
			Negotiation asked;
			if (!ParseNegotiation(mcc.value, mcc.length, &asked)) {
				fCounters.badFrames++;
				return;
			}
			if (!mcc.command) {
				if (fState != kNegotiating || asked.dlci != fDLCI)
					return;
				if (asked.maxFrame == 0) {
					_Fail(kRefused);
					return;
				}
				if (asked.maxFrame < fMaxFrame)
					fMaxFrame = asked.maxFrame;
				fCreditFlow = asked.convergence == 0x0e;
				fSendCredits = fCreditFlow ? asked.credits : 0;
				fGivenCredits = fCreditFlow ? kInitialCredits : 0;
				fState = kConnecting;
				_SendFrame(fDLCI, true, kSABM, true, -1, NULL, 0);
				_Wait(now);
				return;
			}

			// The other side asks. Only before the DLC is open, and only
			// for the DLC this side takes.
			bool ours = !fInitiator && fState == kIdle && fListenDLCI != 0
				&& asked.dlci == fListenDLCI;
			bool current = asked.dlci != 0 && asked.dlci == fDLCI
				&& (fState == kSignalling || fState == kOpen);
			if (!ours && !current) {
				_SendFrame(asked.dlci, false, kDM, true, -1, NULL, 0);
				return;
			}
			Negotiation answer = asked;
			if (ours) {
				uint16_t size = asked.maxFrame;
				if (size == 0 || size > kDefaultN1)
					size = kDefaultN1;
				fMaxFrame = size;
				fCreditFlow = asked.convergence == 0x0f;
				fSendCredits = fCreditFlow ? asked.credits : 0;
				fGivenCredits = fCreditFlow ? kInitialCredits : 0;
			}
			answer.convergence = fCreditFlow ? 0x0e : 0;
			answer.maxFrame = fMaxFrame;
			answer.credits = fCreditFlow ? kInitialCredits : 0;
			if (!ours)
				answer.credits = 0;
			uint8_t value[kNegotiationSize];
			BuildNegotiation(value, answer);
			_SendMcc(kMccPN, false, value, sizeof(value));
			return;
		}

		case kMccMSC:
		{
			if (mcc.length < 2 || (mcc.value[0] & 0x01) == 0) {
				fCounters.badFrames++;
				return;
			}
			uint8_t dlci = mcc.value[0] >> 2;
			if (mcc.command) {
				_SendMcc(kMccMSC, false, mcc.value, mcc.length);
				if (dlci != 0 && dlci == fDLCI) {
					fRemoteMSC = true;
					// With credits the FC bit is not used (RFCOMM 1.2,
					// 6.5.3).
					if (!fCreditFlow)
						fRemoteStopped = (mcc.value[1] & kSignalFC) != 0;
				}
				return;
			}
			if (dlci != 0 && dlci == fDLCI && fState == kSignalling) {
				fState = kOpen;
				fDeadline = 0;
			}
			return;
		}

		case kMccTest:
			if (mcc.command)
				_SendMcc(kMccTest, false, mcc.value, mcc.length);
			return;

		case kMccFCon:
		case kMccFCoff:
			if (mcc.command) {
				fAggregateStopped = mcc.type == kMccFCoff;
				_SendMcc(mcc.type, false, NULL, 0);
			}
			return;

		case kMccRPN:
			if (!mcc.command || mcc.length < 1)
				return;
			if (mcc.length >= 8)
				_SendMcc(kMccRPN, false, mcc.value, 8);
			else {
				// A request for the values: 9600 bit/s, 8 data bits, no
				// parity, no flow control, XON 0x11, XOFF 0x13
				// (TS 07.10, 5.4.6.3.9).
				uint8_t value[8] = { mcc.value[0], 0x03, 0x03, 0x00, 0x11,
					0x13, 0x00, 0x00 };
				_SendMcc(kMccRPN, false, value, sizeof(value));
			}
			return;

		case kMccRLS:
			if (mcc.command)
				_SendMcc(kMccRLS, false, mcc.value, mcc.length);
			return;

		case kMccNSC:
			return;

		default:
			if (mcc.command) {
				fCounters.unknownCommands++;
				// The type byte as it came (TS 07.10, 5.4.6.3.8).
				uint8_t value = (uint8_t)(mcc.type | 0x02);
				_SendMcc(kMccNSC, false, &value, 1);
			}
			return;
	}
}


void
Mux::_Data(const Frame& frame)
{
	if (frame.hasCredits) {
		fSendCredits += frame.credits;
		if (fSendCredits > kCreditCap)
			fSendCredits = kCreditCap;
	}
	if (frame.length > 0) {
		if (fCreditFlow) {
			if (fGivenCredits <= 0) {
				fCounters.droppedData++;
				return;
			}
			fGivenCredits--;
		}
		if (frame.length > fMaxFrame
			|| frame.length > kReceiveBuffer - fReceived) {
			fCounters.droppedData++;
		} else {
			size_t end = (fStart + fReceived) % kReceiveBuffer;
			for (size_t i = 0; i < frame.length; i++)
				fBuffer[(end + i) % kReceiveBuffer] = frame.data[i];
			fReceived += frame.length;
		}
	}
	_GiveCredits();
}


}	// namespace r5rfcomm
