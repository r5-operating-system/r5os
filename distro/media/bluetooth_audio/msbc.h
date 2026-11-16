/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * mSBC over a voice link (HFP 1.8, appendix A; docs/BLUETOOTH.md,
 * step 7c). Each 57-byte mSBC frame travels in a 60-byte packet: the H2
 * synchronization header (0x01 and one of 0x08, 0x38, 0xc8, 0xf8 for the
 * sequence numbers 0 to 3), the frame, and one byte of padding. The SCO
 * packets of the link do not keep those boundaries (on USB they carry 24
 * or 48 bytes each), so the receiver looks for the header in the byte
 * stream.
 *
 * Plain C++ with <stdint.h> only, so the host test and the add-on build
 * the same code.
 */
#ifndef R5_MSBC_H
#define R5_MSBC_H


#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "sbc_decoder.h"


namespace sbc {


static const size_t kMSBCPacketLength = 60;
static const uint8_t kH2Sync = 0x01;
static const uint8_t kH2Sequence[4] = { 0x08, 0x38, 0xc8, 0xf8 };


// Returns the sequence number 0 to 3 of an H2 header's second byte, or -1.
static inline int
H2Sequence(uint8_t value)
{
	for (int i = 0; i < 4; i++) {
		if (kH2Sequence[i] == value)
			return i;
	}
	return -1;
}


// Builds one 60-byte packet around a 57-byte frame.
static inline void
MSBCPacket(const uint8_t* frame, int sequence, uint8_t* packet)
{
	packet[0] = kH2Sync;
	packet[1] = kH2Sequence[sequence & 3];
	memcpy(packet + 2, frame, kMSBCFrameLength);
	packet[2 + kMSBCFrameLength] = 0;
}


// Finds the mSBC frames in the payload of the SCO packets.
class MSBCDeframer {
public:
	MSBCDeframer()
	{
		Reset();
	}

	void Reset()
	{
		fHave = 0;
		fExpected = -1;
		fSkipped = 0;
		fPad = false;
	}

	// Takes "size" bytes. frame(const uint8_t* frame57, int lost) is called
	// for each frame found; lost is the number of frames that the sequence
	// numbers say are missing before it (0 to 3).
	template<typename Sink>
	void Feed(const uint8_t* data, size_t size, Sink& frame)
	{
		while (size > 0) {
			size_t take = sizeof(fBuffer) - fHave;
			if (take > size)
				take = size;
			memcpy(fBuffer + fHave, data, take);
			fHave += take;
			data += take;
			size -= take;
			_Scan(frame);
		}
	}

	// Bytes thrown away while looking for a header.
	uint32_t Skipped() const { return fSkipped; }

private:
	template<typename Sink>
	void _Scan(Sink& frame)
	{
		size_t start = 0;
		while (true) {
			// The padding byte after a frame is not counted as skipped.
			if (fPad && start < fHave) {
				if (fBuffer[start] == 0)
					start++;
				fPad = false;
			}

			// Look for 0x01, a sequence byte and the mSBC sync word.
			size_t at = start;
			while (at + 3 <= fHave && !(fBuffer[at] == kH2Sync
					&& H2Sequence(fBuffer[at + 1]) >= 0
					&& fBuffer[at + 2] == kMSBCSyncWord))
				at++;
			if (at + 3 > fHave) {
				// No header in what is here; keep the last two bytes, which
				// may be the start of one.
				size_t keep = fHave - start < 2 ? fHave - start : 2;
				fSkipped += (uint32_t)(fHave - start - keep);
				start = fHave - keep;
				break;
			}
			fSkipped += (uint32_t)(at - start);
			start = at;
			if (fHave - start < 2 + kMSBCFrameLength)
				break;

			int sequence = H2Sequence(fBuffer[start + 1]);
			int lost = 0;
			if (fExpected >= 0)
				lost = (sequence - fExpected) & 3;
			fExpected = (sequence + 1) & 3;
			frame(fBuffer + start + 2, lost);

			// The next search starts right after the frame; the padding
			// byte is passed over when it comes.
			start += 2 + kMSBCFrameLength;
			fPad = true;
		}
		memmove(fBuffer, fBuffer + start, fHave - start);
		fHave -= start;
	}

	uint8_t		fBuffer[2 * kMSBCPacketLength];
	size_t		fHave;
	int			fExpected;
	uint32_t	fSkipped;
	bool		fPad;
};


}	// namespace sbc


#endif	// R5_MSBC_H
