/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * RFCOMM (Bluetooth RFCOMM 1.2, the subset of ETSI TS 07.10 it adapts),
 * docs/BLUETOOTH.md step 7a. Written from the specifications.
 *
 * This part has no I/O, no threads and no Haiku headers, so the loopback
 * test and the host fuzzer (scripts/bt-rfcomm-fuzz.cpp) build it as it is:
 *  - frames: address, control, length, optional credit byte, data, FCS;
 *  - multiplexer control commands on DLCI 0 (PN, MSC, Test, FCon, FCoff,
 *    RPN, RLS, NSC);
 *  - Mux, the state machine of one RFCOMM session with one data DLC,
 *    with credit-based flow control. It is fed whole L2CAP payloads
 *    (one frame each) and hands its frames to an output callback;
 *  - the RFCOMM server channel in an SDP record.
 * RFCOMMLink.h puts it on an L2CAP socket.
 */
#ifndef R5_RFCOMM_MUX_H
#define R5_RFCOMM_MUX_H


#include <stddef.h>
#include <stdint.h>


namespace r5rfcomm {


// Frame types, control field without the P/F bit (TS 07.10, 5.2.1.3).
enum {
	kSABM	= 0x2f,
	kUA		= 0x63,
	kDM		= 0x0f,
	kDISC	= 0x43,
	kUIH	= 0xef,
	kPF		= 0x10
};

// Multiplexer control types, the type byte with EA set and C/R clear
// (TS 07.10, 5.4.6.3; RFCOMM 1.2, 4.3).
enum {
	kMccPN		= 0x81,
	kMccTest	= 0x21,
	kMccFCon	= 0xa1,
	kMccFCoff	= 0x61,
	kMccMSC		= 0xe1,
	kMccNSC		= 0x11,
	kMccRPN		= 0x91,
	kMccRLS		= 0x51
};

// The first server channel and the last (RFCOMM 1.2, 5.4).
static const uint8_t kMinChannel = 1;
static const uint8_t kMaxChannel = 30;

// The default maximum frame size N1 (TS 07.10, 5.7.2), which R5 asks for:
// it fits the default L2CAP MTU of 672 with room for the frame header.
static const uint16_t kDefaultN1 = 127;
// The largest frame data this parser takes; the length field holds 15 bits.
static const uint16_t kMaxN1 = 32767;
// Bytes around the data: address, control, two length bytes, credit, FCS.
static const size_t kMaxOverhead = 6;
// Credits R5 gives in PN (k holds 3 bits) and at most in one credit byte.
static const uint8_t kInitialCredits = 7;
// Received data waiting for the reader; credits never let more arrive.
static const size_t kReceiveBuffer = 2048;
// Every wait for an answer (UA, PN, MSC) ends after this many
// microseconds.
static const uint64_t kTimeout = 20000000;


// FCS of TS 07.10, 5.2.1.6 (CRC-8, x^8 + x^2 + x + 1, reflected, preset
// 0xff, ones complement).
uint8_t		Fcs(const uint8_t* data, size_t length);


struct Frame {
	uint8_t			dlci;
	bool			commandResponse;	// the C/R bit of the address
	uint8_t			type;				// kSABM ... kUIH
	bool			pollFinal;
	bool			hasCredits;
	uint8_t			credits;
	const uint8_t*	data;
	size_t			length;
};

// Parses one frame that fills size bytes exactly. creditBytes: a UIH
// with P/F set on a DLCI other than 0 carries a credit byte. False on
// anything malformed, including a wrong FCS.
bool		ParseFrame(const uint8_t* packet, size_t size,
				bool creditBytes, Frame* frame);
// Writes a frame; credits < 0 writes no credit byte. 0 when out is too
// small or the data too long.
size_t		BuildFrame(uint8_t* out, size_t outSize, uint8_t dlci,
				bool commandResponse, uint8_t type, bool pollFinal,
				int credits, const uint8_t* data, size_t length);


struct Mcc {
	uint8_t			type;		// kMccPN ...
	bool			command;
	const uint8_t*	value;
	size_t			length;
};

// One multiplexer control message at the start of data; *used is its
// size, so several messages in one frame are read one after another.
bool		ParseMcc(const uint8_t* data, size_t size, Mcc* mcc,
				size_t* used);
size_t		BuildMcc(uint8_t* out, size_t outSize, uint8_t type,
				bool command, const uint8_t* value, size_t length);


// The parameter negotiation values (TS 07.10, 5.4.6.3.1; RFCOMM 1.2, 5.5.3
// and 6.5.3: CL 0xf asks for credit-based flow control, 0xe accepts it,
// and k carries the initial credits).
struct Negotiation {
	uint8_t		dlci;
	uint8_t		convergence;	// the CL bits: 0xf, 0xe or 0
	uint8_t		priority;
	uint16_t	maxFrame;		// N1
	uint8_t		credits;		// k
};

static const size_t kNegotiationSize = 8;
bool		ParseNegotiation(const uint8_t* value, size_t length,
				Negotiation* negotiation);
void		BuildNegotiation(uint8_t out[kNegotiationSize],
				const Negotiation& negotiation);


// The RFCOMM server channel of a service, from the attributes as
// SDPDataIO keeps them (the data of the outer sequence only):
// ServiceClassIDList (0x0001) and ProtocolDescriptorList (0x0004).
// 0 when classList has no serviceClass (a 16-bit UUID, or the same as a
// 32-bit or a 128-bit UUID on the Bluetooth base) or protocolList no
// RFCOMM channel in 1 to 30. Bounds-checked.
uint8_t		FindChannel(const uint8_t* classList, size_t classLength,
				const uint8_t* protocolList, size_t protocolLength,
				uint16_t serviceClass);


class Mux {
public:
	typedef void		(*Output)(const uint8_t* frame, size_t length,
							void* cookie);

	enum State {
		kIdle,
		kStarting,		// initiator: SABM on DLCI 0 sent
		kNegotiating,	// initiator: PN sent
		kConnecting,	// initiator: SABM on the data DLCI sent
		kSignalling,	// DLC open, our MSC not answered yet
		kOpen,			// data flows
		kClosing,		// DISC sent
		kClosed,
		kFailed
	};

	enum Error {
		kNoError,
		kTimedOut,
		kRefused,		// DM, or a PN answer we cannot use
		kRemoteClosed,	// the other side sent DISC or DM
		kProtocol		// a frame that breaks the protocol
	};

	struct Counters {
		uint32_t		badFrames;		// not parsed, or a wrong FCS
		uint32_t		droppedData;	// data without credit, or no room
		uint32_t		unknownCommands;
	};

						Mux(bool initiator, Output output, void* cookie);

	// Initiator: opens the session and a DLC on that server channel of
	// the other side. False when the channel is out of range or the mux
	// was used before.
			bool		Open(uint8_t channel, uint64_t now);
	// Responder: accepts a DLC on this server channel of its own.
			bool		Listen(uint8_t channel);
	// The other side's frame (one L2CAP payload).
			void		Receive(const uint8_t* packet, size_t size,
							uint64_t now);
	// Ends a wait that took longer than kTimeout.
			void		Tick(uint64_t now);
	// DISC on the data DLC, then on DLCI 0. kClosed when both are
	// answered or timed out.
			void		Close(uint64_t now);

	// Data bytes Send() takes now in one frame; 0 while there is no credit
	// or the other side asked to stop.
			size_t		SendSpace() const;
			bool		Send(const uint8_t* data, size_t length);
	// Takes received data; may give the other side new credits.
			size_t		Take(uint8_t* out, size_t size);
			size_t		Pending() const { return fReceived; }

			State		GetState() const { return fState; }
			Error		GetError() const { return fError; }
			bool		IsInitiator() const { return fInitiator; }
			bool		CreditFlow() const { return fCreditFlow; }
			uint16_t	MaxFrame() const { return fMaxFrame; }
			uint8_t		DataDLCI() const { return fDLCI; }
			bool		RemoteSignalled() const { return fRemoteMSC; }
			int			SendCredits() const { return fSendCredits; }
			int			GivenCredits() const { return fGivenCredits; }
			uint64_t	Deadline() const { return fDeadline; }
	const	Counters&	GetCounters() const { return fCounters; }

private:
			bool		_Command(bool isCommand) const
							{ return isCommand == fInitiator; }
			void		_SendFrame(uint8_t dlci, bool isCommand,
							uint8_t type, bool pollFinal, int credits,
							const uint8_t* data, size_t length);
			void		_SendMcc(uint8_t type, bool command,
							const uint8_t* value, size_t length);
			void		_Fail(Error error);
			void		_Wait(uint64_t now);
			void		_SendNegotiation(uint64_t now);
			void		_SendSignals(uint64_t now);
			void		_GiveCredits();
			void		_Control(const Frame& frame, uint64_t now);
			void		_ControlMessage(const Mcc& mcc, uint64_t now);
			void		_Data(const Frame& frame);

			bool		fInitiator;
			Output		fOutput;
			void*		fCookie;
			State		fState;
			Error		fError;
			bool		fSessionOpen;
			bool		fClosingSession;
			uint8_t		fDLCI;
			uint8_t		fListenDLCI;
			bool		fCreditFlow;
			uint16_t	fMaxFrame;
			int			fSendCredits;
			int			fGivenCredits;
			bool		fRemoteMSC;
			bool		fRemoteStopped;		// MSC FC without credit flow
			bool		fAggregateStopped;	// FCoff
			uint64_t	fDeadline;			// 0: no wait
			uint8_t		fBuffer[kReceiveBuffer];
			size_t		fStart;
			size_t		fReceived;
			Counters	fCounters;
};


}	// namespace r5rfcomm


#endif	// R5_RFCOMM_MUX_H
