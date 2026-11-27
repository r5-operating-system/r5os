/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * The shared-memory link between an application and app_server
 * (docs/design/APP-SERVER-RING.md). Private to libbe and app_server.
 */
#ifndef _LINK_RING_H
#define _LINK_RING_H


#include <OS.h>


namespace BPrivate {

class LinkReceiver;
class LinkSender;
class ServerLink;


// Port message codes next to kLinkCode ('_PTL'). A wake carries no data.
static const int32 kLinkRingWakeCode = '_PTW';
// First message header of a port batch that belongs to a marker in the ring.
static const uint32 kLinkRingFallbackCode = 'r5RF';
static const int32 kLinkRingVersion = 1;


struct link_ring_stats {
	int32	rings;				// rings now open (server)
	int64	ringsTotal;			// rings set up since app_server started
	int64	recordsIn;			// batches read from rings
	int64	recordsOut;			// batches written into rings
	int64	wakesIn;			// wake port messages read
	int64	wakesOut;			// wake port messages written
	int64	fallbacksIn;		// port batches that a ring marker ordered
	int64	fallbacksOut;
	int64	closed;				// rings or directions closed for a fault
};


class LinkRing {
public:
	enum {
		kEmpty = 0,
		kData,
		kMarker,
		kBroken
	};

	static	LinkRing*			CreateServer(team_id client,
									area_id* _clientArea,
									uint32* _toServerSize,
									uint32* _toClientSize);
	static	LinkRing*			CreateClient(area_id area,
									uint32 toServerSize,
									uint32 toClientSize,
									team_id server);

			void				AcquireReference();
			void				ReleaseReference();

			bool				IsServer() const { return fServer; }
			// The other side has ended (app_server crashed, or the client).
			bool				PeerGone() const;

	// Writer side, used by LinkSender::Flush(). B_NOT_ALLOWED means that
	// the ring no longer takes data and the batch must go to the port as
	// before; anything else is the result of the send.
			status_t			Write(port_id port, const char* data,
									size_t size, bigtime_t timeout);

	// Reader side, used by LinkReceiver.
			int32				Peek();
			size_t				PeekSize() const { return fPeekSize; }
			void				Read(char* buffer);
			void				SkipMarker();
			bool				IsReadClosed();
			void				CloseRead();
			bool				PrepareSleep();
			void				EndSleep();
			void				CountWake();
			void				CountFallbackIn();

	static	void				GetStats(link_ring_stats* stats);
	static	void				LogStats(bool force);

private:
								LinkRing(bool server, area_id area,
									uint8* base, uint32 toServerSize,
									uint32 toClientSize);
								~LinkRing();

			uint32				_Free(bool& broken);
			void				_Put(const void* data, size_t size);
			void				_Publish(port_id port);
			status_t			_WaitForSpace(port_id port, uint32 needed,
									bigtime_t timeout);
			void				_CloseWrite();

private:
			int32				fReferenceCount;
			bool				fServer;
			area_id				fArea;
			uint8*				fBase;
			struct link_ring_shared* fShared;

			// The direction this side writes
			struct link_ring_direction* fOut;
			uint8*				fOutData;
			uint32				fOutSize;
			uint32				fOutHead;
			uint32				fOutTail;
			bool				fOutClosed;
			bool				fOutEnabled;

			// The direction this side reads
			struct link_ring_direction* fIn;
			uint8*				fInData;
			uint32				fInSize;
			uint32				fInTail;
			bool				fInClosed;
			bool				fPeeked;
			int32				fPeekType;
			uint32				fPeekSize;
			team_id				fPeerTeam;
};


// Client side of the set-up (BApplication and BWindow). Asks app_server for
// a ring on this link. Without one the link stays on ports.
status_t	link_ring_connect(ServerLink& link);
// Drops the ring of this link (before the link is pointed at another port).
void		link_ring_disconnect(ServerLink& link);
// Server side: puts the ring on both halves of the link.
void		link_ring_attach(ServerLink& link, LinkRing* ring);

}	// namespace BPrivate


#endif	// _LINK_RING_H
