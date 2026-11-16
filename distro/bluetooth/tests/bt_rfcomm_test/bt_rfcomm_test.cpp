/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * RFCOMM of bluetooth_server (docs/BLUETOOTH.md, step 7a) without a
 * radio:
 *  - the FCS and frames against frames written out from TS 07.10 and
 *    RFCOMM 1.2, and malformed frames;
 *  - PN and MSC, and the RFCOMM channel in an SDP record of a headset's
 *    Hands-Free service;
 *  - two muxes back to back (initiator and responder): session, PN with
 *    credits, DLC, MSC, data both ways, credit top-up, close; a refused
 *    DLC and a timeout;
 *  - two RFCOMMLink objects over ports, with their reader threads
 *    (Haiku only), and RFCOMMLink::Connect() with no radio.
 * Built on the host too with -DR5_RFCOMM_HOST (without the RFCOMMLink
 * part).
 */


#include <stdio.h>
#include <string.h>

#include <vector>

#include "RFCOMMMux.h"

#ifndef R5_RFCOMM_HOST
#	include <OS.h>
#	include "RFCOMMLink.h"
#endif


using namespace r5rfcomm;


static int sFailures = 0;


static void
report(const char* name, bool ok)
{
	printf("%-48s %s\n", name, ok ? "ok" : "FAILED");
	if (!ok)
		sFailures++;
}


static void
test_fcs_and_frames()
{
	// SABM on DLCI 0 from the initiator, UA from the responder, DISC and
	// DM on DLCI 0.
	static const uint8_t kSABM0[] = { 0x03, 0x3f, 0x01, 0x1c };
	static const uint8_t kUA0[] = { 0x03, 0x73, 0x01, 0xd7 };
	report("FCS of SABM on DLCI 0 is 0x1c", Fcs(kSABM0, 3) == 0x1c);
	report("FCS of UA on DLCI 0 is 0xd7", Fcs(kUA0, 3) == 0xd7);

	uint8_t out[300];
	size_t size = BuildFrame(out, sizeof(out), 0, true, kSABM, true, -1,
		NULL, 0);
	report("SABM 0 built as in TS 07.10",
		size == sizeof(kSABM0) && memcmp(out, kSABM0, size) == 0);
	size = BuildFrame(out, sizeof(out), 0, true, kUA, true, -1, NULL, 0);
	report("UA 0 built as in TS 07.10",
		size == sizeof(kUA0) && memcmp(out, kUA0, size) == 0);

	Frame frame;
	report("SABM 0 parses", ParseFrame(kSABM0, sizeof(kSABM0), false, &frame)
		&& frame.dlci == 0 && frame.type == kSABM && frame.pollFinal
		&& frame.commandResponse && frame.length == 0);

	uint8_t bad[sizeof(kSABM0)];
	memcpy(bad, kSABM0, sizeof(bad));
	bad[3] ^= 0x01;
	report("wrong FCS refused", !ParseFrame(bad, sizeof(bad), false,
		&frame));
	report("short frame refused", !ParseFrame(kSABM0, 3, false, &frame));
	memcpy(bad, kSABM0, sizeof(bad));
	bad[0] &= ~0x01;
	report("address without EA refused", !ParseFrame(bad, sizeof(bad),
		false, &frame));

	// UIH with data: the FCS covers the address and control fields only.
	const char* text = "AT+BRSF=191\r";
	size = BuildFrame(out, sizeof(out), 6, true, kUIH, false, -1,
		(const uint8_t*)text, strlen(text));
	report("UIH FCS over address and control",
		size == 4 + strlen(text) && out[size - 1] == Fcs(out, 2));
	report("UIH round trip", ParseFrame(out, size, true, &frame)
		&& frame.dlci == 6 && frame.type == kUIH && !frame.hasCredits
		&& frame.length == strlen(text)
		&& memcmp(frame.data, text, frame.length) == 0);
	report("UIH one byte short refused", !ParseFrame(out, size - 1, true,
		&frame));

	// 200 bytes: a two-byte length field.
	uint8_t data[200];
	for (size_t i = 0; i < sizeof(data); i++)
		data[i] = (uint8_t)i;
	size = BuildFrame(out, sizeof(out), 6, false, kUIH, true, 5, data,
		sizeof(data));
	report("long UIH with credit, two length bytes",
		size == 2 + 2 + 1 + sizeof(data) + 1 && (out[2] & 0x01) == 0
		&& ParseFrame(out, size, true, &frame) && frame.hasCredits
		&& frame.credits == 5 && frame.length == sizeof(data)
		&& memcmp(frame.data, data, sizeof(data)) == 0);
	report("credit byte only where credits run",
		ParseFrame(out, size, false, &frame) == false);
	report("frame larger than its buffer refused",
		BuildFrame(out, 10, 6, true, kUIH, false, -1, data, 20) == 0);
}


static void
test_control()
{
	Negotiation asked = { 6, 0x0f, 0, 127, 7 };
	uint8_t value[kNegotiationSize];
	BuildNegotiation(value, asked);
	static const uint8_t kPN[] = { 0x06, 0xf0, 0x00, 0x00, 0x7f, 0x00, 0x00,
		0x07 };
	report("PN value as in RFCOMM 1.2", memcmp(value, kPN, sizeof(kPN))
		== 0);

	uint8_t message[16];
	size_t size = BuildMcc(message, sizeof(message), kMccPN, true, value,
		sizeof(value));
	report("PN command type byte 0x83, length 8",
		size == 10 && message[0] == 0x83 && message[1] == 0x11);
	Mcc mcc;
	size_t used;
	Negotiation parsed;
	report("PN round trip", ParseMcc(message, size, &mcc, &used)
		&& used == size && mcc.type == kMccPN && mcc.command
		&& ParseNegotiation(mcc.value, mcc.length, &parsed)
		&& parsed.dlci == 6 && parsed.convergence == 0x0f
		&& parsed.maxFrame == 127 && parsed.credits == 7);
	report("MCC longer than its frame refused",
		!ParseMcc(message, size - 1, &mcc, &used));
	report("PN shorter than 8 refused",
		!ParseNegotiation(value, 7, &parsed));

	uint8_t signals[2] = { (6 << 2) | 0x03, 0x8d };
	size = BuildMcc(message, sizeof(message), kMccMSC, true, signals, 2);
	report("MSC command type byte 0xe3", size == 4 && message[0] == 0xe3);
}


static void
test_sdp()
{
	// A Hands-Free record: classes Hands-Free (0x111e) and Generic Audio
	// (0x1203); protocols L2CAP, then RFCOMM on channel 3.
	static const uint8_t kClasses[] = { 0x19, 0x11, 0x1e, 0x19, 0x12, 0x03 };
	static const uint8_t kProtocols[] = { 0x35, 0x03, 0x19, 0x01, 0x00, 0x35,
		0x05, 0x19, 0x00, 0x03, 0x08, 0x03 };
	report("SDP: Hands-Free on channel 3",
		FindChannel(kClasses, sizeof(kClasses), kProtocols,
			sizeof(kProtocols), 0x111e) == 3);
	report("SDP: no Audio Gateway class",
		FindChannel(kClasses, sizeof(kClasses), kProtocols,
			sizeof(kProtocols), 0x111f) == 0);

	// The same class as a 128-bit UUID on the base UUID.
	static const uint8_t kClasses128[] = { 0x1c, 0x00, 0x00, 0x11, 0x1e,
		0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34,
		0xfb };
	report("SDP: 128-bit class UUID",
		FindChannel(kClasses128, sizeof(kClasses128), kProtocols,
			sizeof(kProtocols), 0x111e) == 3);

	uint8_t broken[sizeof(kProtocols)];
	memcpy(broken, kProtocols, sizeof(broken));
	broken[6] = 0x08;	// the RFCOMM sequence claims 8 bytes
	report("SDP: sequence past the end",
		FindChannel(kClasses, sizeof(kClasses), broken, sizeof(broken),
			0x111e) == 0);
	memcpy(broken, kProtocols, sizeof(broken));
	broken[11] = 31;
	report("SDP: channel 31 refused",
		FindChannel(kClasses, sizeof(kClasses), broken, sizeof(broken),
			0x111e) == 0);
	report("SDP: truncated",
		FindChannel(kClasses, sizeof(kClasses), kProtocols,
			sizeof(kProtocols) - 1, 0x111e) == 0);
}


// #pragma mark - two muxes back to back


struct Wire {
	std::vector<std::vector<uint8_t> > frames;
};


static void
to_wire(const uint8_t* frame, size_t length, void* cookie)
{
	((Wire*)cookie)->frames.push_back(std::vector<uint8_t>(frame,
		frame + length));
}


// Delivers what each side sent to the other until both are quiet.
static void
pump(Mux& a, Wire& fromA, Mux& b, Wire& fromB, uint64_t now)
{
	for (int round = 0; round < 1000; round++) {
		if (fromA.frames.empty() && fromB.frames.empty())
			return;
		std::vector<std::vector<uint8_t> > toB, toA;
		toB.swap(fromA.frames);
		toA.swap(fromB.frames);
		for (size_t i = 0; i < toB.size(); i++)
			b.Receive(&toB[i][0], toB[i].size(), now);
		for (size_t i = 0; i < toA.size(); i++)
			a.Receive(&toA[i][0], toA[i].size(), now);
	}
}


static void
test_loopback()
{
	Wire fromInitiator, fromResponder;
	Mux initiator(true, to_wire, &fromInitiator);
	Mux responder(false, to_wire, &fromResponder);
	uint64_t now = 1000;

	report("responder listens on channel 3", responder.Listen(3));
	report("initiator opens channel 3", initiator.Open(3, now));
	report("first frame is SABM 0", fromInitiator.frames.size() == 1
		&& fromInitiator.frames[0].size() == 4
		&& fromInitiator.frames[0][1] == 0x3f);
	pump(initiator, fromInitiator, responder, fromResponder, now);
	report("both open", initiator.GetState() == Mux::kOpen
		&& responder.GetState() == Mux::kOpen);
	report("DLCI 6, credit flow, N1 127", initiator.DataDLCI() == 6
		&& responder.DataDLCI() == 6 && initiator.CreditFlow()
		&& responder.CreditFlow() && initiator.MaxFrame() == 127);
	report("MSC both ways", initiator.RemoteSignalled()
		&& responder.RemoteSignalled());
	report("7 credits each way", initiator.SendCredits() == 7
		&& responder.SendCredits() == 7);
	report("no wait left", initiator.Deadline() == 0
		&& responder.Deadline() == 0);

	const char* command = "AT+BRSF=191\r";
	report("responder sends", responder.Send((const uint8_t*)command,
		strlen(command)));
	pump(initiator, fromInitiator, responder, fromResponder, now);
	uint8_t buffer[kReceiveBuffer];
	size_t got = initiator.Take(buffer, sizeof(buffer));
	report("initiator gets it", got == strlen(command)
		&& memcmp(buffer, command, got) == 0);

	// Twenty frames of 127 bytes: the credits run out while nobody reads,
	// and come back as the responder reads.
	uint8_t block[127];
	memset(block, 'x', sizeof(block));
	int sent = 0;
	while (initiator.SendSpace() > 0 && sent < 20) {
		initiator.Send(block, sizeof(block));
		sent++;
	}
	report("credits stop the sender", sent == 7
		&& initiator.SendSpace() == 0);
	pump(initiator, fromInitiator, responder, fromResponder, now);
	report("credits topped up while room is left",
		initiator.SendSpace() > 0);
	size_t total = 0;
	for (int round = 0; round < 50 && sent < 20; round++) {
		while (initiator.SendSpace() > 0 && sent < 20) {
			initiator.Send(block, sizeof(block));
			sent++;
		}
		pump(initiator, fromInitiator, responder, fromResponder, now);
		total += responder.Take(buffer, sizeof(buffer));
		pump(initiator, fromInitiator, responder, fromResponder, now);
	}
	total += responder.Take(buffer, sizeof(buffer));
	report("20 frames arrive whole", sent == 20
		&& total == 20 * sizeof(block));
	report("no data dropped", initiator.GetCounters().droppedData == 0
		&& responder.GetCounters().droppedData == 0);
	report("no bad frames", initiator.GetCounters().badFrames == 0
		&& responder.GetCounters().badFrames == 0);

	initiator.Close(now);
	pump(initiator, fromInitiator, responder, fromResponder, now);
	report("initiator closed", initiator.GetState() == Mux::kClosed
		&& initiator.GetError() == Mux::kNoError);
	report("responder saw the close",
		responder.GetState() == Mux::kClosed
		&& responder.GetError() == Mux::kRemoteClosed);
}


static void
test_refused_and_timeout()
{
	Wire fromInitiator, fromResponder;
	Mux initiator(true, to_wire, &fromInitiator);
	Mux responder(false, to_wire, &fromResponder);
	responder.Listen(4);
	initiator.Open(3, 0);
	pump(initiator, fromInitiator, responder, fromResponder, 0);
	report("DLC on a channel nobody listens on: refused",
		initiator.GetState() == Mux::kFailed
		&& initiator.GetError() == Mux::kRefused);

	Wire nowhere;
	Mux lonely(true, to_wire, &nowhere);
	lonely.Open(1, 5);
	lonely.Tick(5 + kTimeout - 1);
	report("still waiting before the timeout",
		lonely.GetState() == Mux::kStarting);
	lonely.Tick(5 + kTimeout);
	report("SABM without an answer times out",
		lonely.GetState() == Mux::kFailed
		&& lonely.GetError() == Mux::kTimedOut);

	Mux unused(true, to_wire, &nowhere);
	report("channel 0 and 31 refused", !unused.Open(0, 0)
		&& !unused.Open(31, 0));
}


#ifndef R5_RFCOMM_HOST
// #pragma mark - two links over ports


class PortTransport : public RFCOMMTransport {
public:
	PortTransport(port_id out, port_id in)
		:
		fOut(out),
		fIn(in)
	{
	}

	virtual status_t Send(const void* data, size_t length)
	{
		return write_port_etc(fOut, 'rfcm', data, length, B_RELATIVE_TIMEOUT,
			1000000);
	}

	virtual ssize_t Receive(void* buffer, size_t size, bigtime_t timeout)
	{
		int32 code;
		ssize_t received = read_port_etc(fIn, &code, buffer, size,
			B_RELATIVE_TIMEOUT, timeout);
		if (received == B_TIMED_OUT || received == B_WOULD_BLOCK)
			return B_TIMED_OUT;
		return received > 0 ? received : B_ERROR;
	}

	virtual void Shutdown()
	{
		close_port(fIn);
	}

private:
	port_id		fOut;
	port_id		fIn;
};


struct Accept {
	PortTransport*	transport;
	RFCOMMLink*		link;
	status_t		status;
};


static status_t
accept_thread(void* data)
{
	Accept* accept = (Accept*)data;
	accept->status = RFCOMMLink::Start(accept->transport, false, 3, 5000000,
		&accept->link);
	return B_OK;
}


static void
test_links()
{
	port_id toResponder = create_port(16, "rfcomm test a");
	port_id toInitiator = create_port(16, "rfcomm test b");
	Accept accept = { new PortTransport(toInitiator, toResponder), NULL,
		B_ERROR };
	thread_id thread = spawn_thread(accept_thread, "rfcomm accept",
		B_NORMAL_PRIORITY, &accept);
	resume_thread(thread);

	RFCOMMLink* initiator = NULL;
	bigtime_t start = system_time();
	status_t status = RFCOMMLink::Start(
		new PortTransport(toResponder, toInitiator), true, 3, 5000000,
		&initiator);
	status_t result;
	wait_for_thread(thread, &result);
	report("links: both open", status == B_OK && accept.status == B_OK
		&& initiator != NULL && accept.link != NULL
		&& initiator->IsOpen() && accept.link->IsOpen());
	printf("links: open after %" B_PRId64 " ms\n",
		(system_time() - start) / 1000);
	if (status != B_OK || accept.status != B_OK) {
		delete initiator;
		delete accept.link;
		delete_port(toResponder);
		delete_port(toInitiator);
		return;
	}

	const char* text = "AT+BRSF=191\rAT+CIND=?\r";
	report("links: send two lines", accept.link->Send(text, strlen(text),
		1000000) == B_OK);
	char line[64];
	report("links: first line", initiator->ReadLine(line, sizeof(line),
		1000000) == B_OK && strcmp(line, "AT+BRSF=191") == 0);
	report("links: second line", initiator->ReadLine(line, sizeof(line),
		1000000) == B_OK && strcmp(line, "AT+CIND=?") == 0);
	report("links: no third line, timeout",
		initiator->ReadLine(line, sizeof(line), 300000) == B_TIMED_OUT);

	// More than the credits and the buffer of the other side hold at
	// once: Send() waits for credit while the reader drains.
	static char big[4000];
	memset(big, 'y', sizeof(big));
	big[sizeof(big) - 1] = '\n';
	struct Sender {
		static status_t Run(void* data)
		{
			RFCOMMLink* link = (RFCOMMLink*)data;
			return link->Send(big, sizeof(big), 5000000);
		}
	};
	thread_id sender = spawn_thread(Sender::Run, "rfcomm sender",
		B_NORMAL_PRIORITY, initiator);
	resume_thread(sender);
	size_t total = 0;
	char piece[512];
	while (total < sizeof(big) - 1) {
		if (accept.link->ReadLine(piece, sizeof(piece), 5000000) != B_OK)
			break;
		total += strlen(piece);
	}
	status_t sendResult = B_ERROR;
	wait_for_thread(sender, &sendResult);
	report("links: 4000 bytes through 7-credit windows",
		sendResult == B_OK && total == sizeof(big) - 1);

	start = system_time();
	initiator->Close();
	report("links: close answered",
		initiator->Error() == r5rfcomm::Mux::kNoError);
	printf("links: closed after %" B_PRId64 " ms\n",
		(system_time() - start) / 1000);
	snooze(300000);
	report("links: the other side saw it", !accept.link->IsOpen()
		&& accept.link->ReadLine(line, sizeof(line), 100000) == B_ERROR);
	delete initiator;
	delete accept.link;
	delete_port(toResponder);
	delete_port(toInitiator);
}


static void
test_connect_without_radio()
{
	bdaddr_t address;
	memset(&address, 0, sizeof(address));
	RFCOMMLink* link = NULL;
	report("Connect() refuses channel 0",
		RFCOMMLink::Connect(address, 0, 1000000, &link) == B_BAD_VALUE);
	bigtime_t start = system_time();
	status_t status = RFCOMMLink::Connect(address, 1, 5000000, &link);
	bigtime_t took = system_time() - start;
	printf("Connect() without a radio: %s after %" B_PRId64 " ms\n",
		strerror(status), took / 1000);
	report("Connect() without a radio fails", status != B_OK
		&& link == NULL);
	if (status == B_OK)
		delete link;
}
#endif


int
main()
{
	test_fcs_and_frames();
	test_control();
	test_sdp();
	test_loopback();
	test_refused_and_timeout();
#ifndef R5_RFCOMM_HOST
	test_links();
	test_connect_without_radio();
#endif
	printf("bt_rfcomm_test: %d failed\n", sFailures);
	return sFailures == 0 ? 0 : 1;
}
