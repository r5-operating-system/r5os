/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * The shared-memory link between an application and app_server
 * (docs/design/APP-SERVER-RING.md).
 *
 * One area per link (BApplication or BWindow) holds two single-producer,
 * single-consumer byte rings: application to app_server, and app_server to
 * application. LinkSender::Flush() writes its batch into the ring as one
 * record instead of write_port(). The reader is woken by a small port
 * message on the link's own port only when it said it is going to sleep.
 *
 * app_server creates the area and maps it into the one client team with
 * _kern_r5_share_area(); the area is never cloneable. app_server trusts
 * nothing in it: it keeps its own read position, checks every position and
 * record size it reads, and copies each batch into its own buffer before
 * LinkReceiver parses it. A fault closes the ring; the link then uses the
 * port, as it always does when no ring is set up.
 *
 * Order: a batch that does not fit in the ring goes to the port as before,
 * with a marker record in the ring after it. The reader takes ring records
 * in order and, at a marker, the one port batch that belongs to it. Port
 * batches from other senders on the same port carry no prefix and are
 * taken as they come, as before.
 */


#include <LinkRing.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/uio.h>
#include <unistd.h>
#include <new>

#include <LinkReceiver.h>
#include <LinkSender.h>
#include <ServerLink.h>
#include <ServerProtocol.h>

#include "link_message.h"
#include "syscalls.h"


namespace BPrivate {


static const uint32 kSharedMagic = 'r5LR';
static const uint32 kHeaderSize = B_PAGE_SIZE;
static const uint32 kToServerSize = 32768;
static const uint32 kToClientSize = 16384;

static const uint32 kRecordData = 1;
static const uint32 kRecordMarker = 2;
static const uint32 kRecordHeaderSize = 8;

static const bigtime_t kLogInterval = 10000000;


struct link_ring_direction {
	uint32				head;			// written by the producer
	uint32				_pad0[15];
	uint32				tail;			// written by the consumer
	uint32				_pad1[15];
	uint32				readerSleeping;	// set by the consumer
	uint32				closed;			// set by app_server
	uint32				_pad2[14];
};

struct link_ring_shared {
	uint32				magic;
	uint32				version;
	uint32				toServerSize;
	uint32				toClientSize;
	uint32				_pad[12];
	link_ring_direction	toServer;
	link_ring_direction	toClient;
};

struct link_ring_record {
	uint32				size;
	uint32				type;
};


static link_ring_stats sStats;
static bigtime_t sLastLog;


static inline uint32
align8(uint32 size)
{
	return (size + 7) & ~(uint32)7;
}


static inline uint32
load_acquire(uint32* value)
{
	return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}


static bool
valid_size(uint32 size)
{
	return size >= B_PAGE_SIZE && size <= 1024 * 1024
		&& (size & (size - 1)) == 0;
}


// #pragma mark - LinkRing


LinkRing::LinkRing(bool server, area_id area, uint8* base,
	uint32 toServerSize, uint32 toClientSize)
	:
	fReferenceCount(1),
	fServer(server),
	fArea(area),
	fBase(base),
	fShared((link_ring_shared*)base),
	fOutHead(0),
	fOutTail(0),
	fOutClosed(false),
	fOutEnabled(!server),
	fInTail(0),
	fInClosed(false),
	fPeeked(false),
	fPeekType(kEmpty),
	fPeekSize(0),
	fPeerTeam(-1)
{
	uint8* toServer = base + kHeaderSize;
	uint8* toClient = toServer + toServerSize;
	if (server) {
		fOut = &fShared->toClient;
		fOutData = toClient;
		fOutSize = toClientSize;
		fIn = &fShared->toServer;
		fInData = toServer;
		fInSize = toServerSize;
	} else {
		fOut = &fShared->toServer;
		fOutData = toServer;
		fOutSize = toServerSize;
		fIn = &fShared->toClient;
		fInData = toClient;
		fInSize = toClientSize;
		// Start where app_server's side is now: zero for a new ring.
		fOutHead = fOutTail = load_acquire(&fOut->head);
		fInTail = load_acquire(&fIn->tail);
	}
}


LinkRing::~LinkRing()
{
	if (fServer) {
		// The client finds both directions closed and uses the port, which
		// reports that the link is gone.
		__atomic_store_n(&fShared->toServer.closed, 1, __ATOMIC_RELEASE);
		__atomic_store_n(&fShared->toClient.closed, 1, __ATOMIC_RELEASE);
	}
	delete_area(fArea);
	if (fServer) {
		atomic_add(&sStats.rings, -1);
		LogStats(false);
	}
}


/*static*/ LinkRing*
LinkRing::CreateServer(team_id client, area_id* _clientArea,
	uint32* _toServerSize, uint32* _toClientSize)
{
	size_t size = kHeaderSize + kToServerSize + kToClientSize;
	void* base = NULL;
	area_id area = create_area("r5 link ring", &base, B_ANY_ADDRESS, size,
		B_NO_LOCK, B_READ_AREA | B_WRITE_AREA);
	if (area < 0)
		return NULL;

	link_ring_shared* shared = (link_ring_shared*)base;
	memset(shared, 0, sizeof(link_ring_shared));
	shared->magic = kSharedMagic;
	shared->version = kLinkRingVersion;
	shared->toServerSize = kToServerSize;
	shared->toClientSize = kToClientSize;

	// Only this team gets the ring; nobody can clone it.
	area_id clientArea = _kern_r5_share_area(area, client,
		B_READ_AREA | B_WRITE_AREA);
	if (clientArea < 0) {
		delete_area(area);
		return NULL;
	}

	LinkRing* ring = new(std::nothrow) LinkRing(true, area, (uint8*)base,
		kToServerSize, kToClientSize);
	if (ring == NULL) {
		delete_area(area);
		return NULL;
	}
	ring->fPeerTeam = client;

	atomic_add(&sStats.rings, 1);
	atomic_add64(&sStats.ringsTotal, 1);
	*_clientArea = clientArea;
	*_toServerSize = kToServerSize;
	*_toClientSize = kToClientSize;
	return ring;
}


/*static*/ LinkRing*
LinkRing::CreateClient(area_id area, uint32 toServerSize, uint32 toClientSize,
	team_id server)
{
	area_info info;
	if (get_area_info(area, &info) != B_OK || info.team != getpid())
		return NULL;

	link_ring_shared* shared = (link_ring_shared*)info.address;
	if (!valid_size(toServerSize) || !valid_size(toClientSize)
		|| (size_t)kHeaderSize + toServerSize + toClientSize > info.size
		|| shared->magic != kSharedMagic
		|| shared->version != (uint32)kLinkRingVersion
		|| shared->toServerSize != toServerSize
		|| shared->toClientSize != toClientSize) {
		delete_area(area);
		return NULL;
	}

	LinkRing* ring = new(std::nothrow) LinkRing(false, area,
		(uint8*)info.address, toServerSize, toClientSize);
	if (ring == NULL)
		delete_area(area);
	else
		ring->fPeerTeam = server;
	return ring;
}


bool
LinkRing::PeerGone() const
{
	team_info info;
	return fPeerTeam >= 0 && get_team_info(fPeerTeam, &info) != B_OK;
}


void
LinkRing::AcquireReference()
{
	atomic_add(&fReferenceCount, 1);
}


void
LinkRing::ReleaseReference()
{
	if (atomic_add(&fReferenceCount, -1) == 1)
		delete this;
}


// #pragma mark - writer


/*!	Free bytes in the direction this side writes. The consumer's position
	is checked: it may only have moved forward, and not past what was
	written.
*/
uint32
LinkRing::_Free(bool& broken)
{
	uint32 tail = load_acquire(&fOut->tail);
	if (tail - fOutTail > fOutHead - fOutTail || (tail & 7) != 0) {
		broken = true;
		return 0;
	}
	broken = false;
	fOutTail = tail;
	return fOutSize - (fOutHead - fOutTail);
}


void
LinkRing::_Put(const void* data, size_t size)
{
	uint32 position = fOutHead & (fOutSize - 1);
	uint32 first = fOutSize - position;
	if (first > size)
		first = size;
	memcpy(fOutData + position, data, first);
	if (first < size)
		memcpy(fOutData, (const uint8*)data + first, size - first);
	fOutHead += size;
}


void
LinkRing::_Publish(port_id port)
{
	__atomic_store_n(&fOut->head, fOutHead, __ATOMIC_RELEASE);
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	if (__atomic_load_n(&fOut->readerSleeping, __ATOMIC_RELAXED) != 0
		&& __atomic_exchange_n(&fOut->readerSleeping, 0, __ATOMIC_SEQ_CST)
			!= 0) {
		// Never wait here: when the port is full the reader is awake.
		write_port_etc(port, kLinkRingWakeCode, NULL, 0, B_RELATIVE_TIMEOUT,
			0);
		if (fServer)
			atomic_add64(&sStats.wakesOut, 1);
	}
}


/*!	Client only: the ring is full while app_server works through it. The
	port route would block in write_port() here; this waits with a short,
	growing sleep instead.
*/
status_t
LinkRing::_WaitForSpace(port_id port, uint32 needed, bigtime_t timeout)
{
	bigtime_t start = system_time();
	bigtime_t delay = 10;
	while (true) {
		bool broken;
		uint32 free = _Free(broken);
		if (broken) {
			_CloseWrite();
			return B_NOT_ALLOWED;
		}
		if (free >= needed)
			return B_OK;
		if (load_acquire(&fOut->closed) != 0) {
			fOutClosed = true;
			return B_NOT_ALLOWED;
		}
		bigtime_t waited = system_time() - start;
		if (timeout != B_INFINITE_TIMEOUT && waited >= timeout)
			return B_TIMED_OUT;
		// A crashed app_server does not close the ring; the port route
		// would report B_BAD_PORT_ID.
		if (waited > 100000 && delay >= 1000 && PeerGone())
			return B_BAD_PORT_ID;

		// app_server reads the ring whenever it is awake.
		if (__atomic_exchange_n(&fOut->readerSleeping, 0, __ATOMIC_SEQ_CST)
				!= 0) {
			write_port_etc(port, kLinkRingWakeCode, NULL, 0,
				B_RELATIVE_TIMEOUT, 0);
		}
		snooze(delay);
		if (delay < 1000)
			delay *= 2;
	}
}


void
LinkRing::_CloseWrite()
{
	if (fOutClosed)
		return;
	fOutClosed = true;
	if (fServer) {
		// The client takes what is still in the ring, then the port.
		__atomic_store_n(&fOut->closed, 1, __ATOMIC_RELEASE);
		atomic_add64(&sStats.closed, 1);
	}
}


status_t
LinkRing::Write(port_id port, const char* data, size_t size,
	bigtime_t timeout)
{
	if (fOutClosed || !fOutEnabled)
		return B_NOT_ALLOWED;
	if (!fServer && load_acquire(&fOut->closed) != 0) {
		// app_server closed this direction after a fault.
		fOutClosed = true;
		return B_NOT_ALLOWED;
	}
	if (size == 0 || size > kMaxBufferSize)
		return B_NOT_ALLOWED;

	uint32 needed = kRecordHeaderSize + align8(size);
	// One marker always stays free, so that a batch for the port can
	// still be ordered.
	if (needed + kRecordHeaderSize <= fOutSize) {
		bool broken;
		uint32 free = _Free(broken);
		if (broken) {
			_CloseWrite();
			return B_NOT_ALLOWED;
		}
		bool fits = free >= needed + kRecordHeaderSize;
		if (!fits && !fServer) {
			status_t status = _WaitForSpace(port,
				needed + kRecordHeaderSize, timeout);
			if (status != B_OK)
				return status;
			fits = true;
		}
		if (fits) {
			link_ring_record record = { (uint32)size, kRecordData };
			_Put(&record, sizeof(record));
			_Put(data, size);
			fOutHead = align8(fOutHead);
			_Publish(port);
			if (fServer)
				atomic_add64(&sStats.recordsOut, 1);
			return B_OK;
		}
	}

	// The batch goes to the port, followed by a marker in the ring. First
	// make sure the marker fits, so the ring never waits for a batch that
	// was not sent.
	bool broken;
	uint32 free = _Free(broken);
	if (broken) {
		_CloseWrite();
		return B_NOT_ALLOWED;
	}
	if (free < kRecordHeaderSize) {
		if (fServer) {
			// app_server never waits for a client.
			_CloseWrite();
			return B_NOT_ALLOWED;
		}
		status_t status = _WaitForSpace(port, kRecordHeaderSize, timeout);
		if (status != B_OK)
			return status;
	}

	message_header prefix = { (int32)sizeof(message_header),
		kLinkRingFallbackCode, 0 };
	iovec vecs[2] = {
		{ &prefix, sizeof(prefix) },
		{ (void*)data, size }
	};
	status_t status;
	do {
		status = _kern_writev_port_etc(port, kLinkCode, vecs, 2,
			sizeof(prefix) + size,
			timeout == B_INFINITE_TIMEOUT ? 0 : B_RELATIVE_TIMEOUT, timeout);
	} while (status == B_INTERRUPTED);
	if (status < B_OK)
		return status;

	link_ring_record record = { 0, kRecordMarker };
	_Put(&record, sizeof(record));
	_Publish(port);
	if (fServer)
		atomic_add64(&sStats.fallbacksOut, 1);
	return B_OK;
}


// #pragma mark - reader


/*!	The kind of the next record. A record that does not make sense closes
	this direction (kBroken, once); app_server then ignores the ring and
	the client's later batches come through the port.
*/
int32
LinkRing::Peek()
{
	if (fPeeked)
		return fPeekType;
	if (fInClosed)
		return kEmpty;

	uint32 head = load_acquire(&fIn->head);
	uint32 available = head - fInTail;
	if (available == 0)
		return kEmpty;

	link_ring_record record;
	bool valid = available <= fInSize && (available & 7) == 0;
	if (valid) {
		// Read the header once; only this copy is used.
		volatile uint32* source
			= (volatile uint32*)(fInData + (fInTail & (fInSize - 1)));
		record.size = source[0];
		record.type = source[1];
		if (record.type == kRecordData) {
			valid = record.size > 0 && record.size <= kMaxBufferSize
				&& kRecordHeaderSize + align8(record.size) <= available;
		} else if (record.type == kRecordMarker)
			valid = record.size == 0;
		else
			valid = false;
	}
	if (!valid) {
		CloseRead();
		return kBroken;
	}

	fPeeked = true;
	fPeekType = record.type == kRecordData ? kData : kMarker;
	fPeekSize = record.size;
	return fPeekType;
}


/*!	Copies the record found by Peek() into \a buffer (at least PeekSize()
	bytes) and gives its room back to the writer.
*/
void
LinkRing::Read(char* buffer)
{
	uint32 position = (fInTail + kRecordHeaderSize) & (fInSize - 1);
	uint32 size = fPeekSize;
	uint32 first = fInSize - position;
	if (first > size)
		first = size;
	memcpy(buffer, fInData + position, first);
	if (first < size)
		memcpy(buffer + first, fInData, size - first);

	fInTail += kRecordHeaderSize + align8(size);
	__atomic_store_n(&fIn->tail, fInTail, __ATOMIC_RELEASE);
	fPeeked = false;

	if (fServer) {
		// The client uses its ring, so replies may go there too.
		fOutEnabled = true;
		if ((atomic_add64(&sStats.recordsIn, 1) & 0x3fff) == 0)
			LogStats(false);
	}
}


void
LinkRing::SkipMarker()
{
	fInTail += kRecordHeaderSize;
	__atomic_store_n(&fIn->tail, fInTail, __ATOMIC_RELEASE);
	fPeeked = false;
}


bool
LinkRing::IsReadClosed()
{
	if (fInClosed)
		return true;
	// app_server closes the direction it writes; the client sees that
	// here. app_server never takes this flag from the client.
	return !fServer && load_acquire(&fIn->closed) != 0;
}


void
LinkRing::CloseRead()
{
	if (fInClosed)
		return;
	fInClosed = true;
	fPeeked = false;
	if (fServer) {
		__atomic_store_n(&fIn->closed, 1, __ATOMIC_RELEASE);
		atomic_add64(&sStats.closed, 1);
		syslog(LOG_WARNING, "app_server: link ring closed after bad data, "
			"the link uses its port");
		debug_printf("app_server: link ring closed after bad data, the link "
			"uses its port\n");
	}
}


/*!	Says that the reader is about to wait on its port. Returns false when a
	record came in meanwhile, so the reader does not wait.
*/
bool
LinkRing::PrepareSleep()
{
	if (IsReadClosed())
		return true;
	__atomic_store_n(&fIn->readerSleeping, 1, __ATOMIC_SEQ_CST);
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	if (Peek() != kEmpty) {
		__atomic_store_n(&fIn->readerSleeping, 0, __ATOMIC_RELAXED);
		return false;
	}
	return true;
}


void
LinkRing::EndSleep()
{
	if (!fInClosed)
		__atomic_store_n(&fIn->readerSleeping, 0, __ATOMIC_RELAXED);
}


void
LinkRing::CountWake()
{
	if (fServer)
		atomic_add64(&sStats.wakesIn, 1);
}


void
LinkRing::CountFallbackIn()
{
	if (fServer)
		atomic_add64(&sStats.fallbacksIn, 1);
}


// #pragma mark - statistics


/*static*/ void
LinkRing::GetStats(link_ring_stats* stats)
{
	stats->rings = atomic_get(&sStats.rings);
	stats->ringsTotal = atomic_get64(&sStats.ringsTotal);
	stats->recordsIn = atomic_get64(&sStats.recordsIn);
	stats->recordsOut = atomic_get64(&sStats.recordsOut);
	stats->wakesIn = atomic_get64(&sStats.wakesIn);
	stats->wakesOut = atomic_get64(&sStats.wakesOut);
	stats->fallbacksIn = atomic_get64(&sStats.fallbacksIn);
	stats->fallbacksOut = atomic_get64(&sStats.fallbacksOut);
	stats->closed = atomic_get64(&sStats.closed);
}


/*!	app_server: one syslog line, at most every ten seconds unless forced.
	Each ring record replaced one port message; each wake is a port message
	the port route would not have needed.
*/
/*static*/ void
LinkRing::LogStats(bool force)
{
	bigtime_t now = system_time();
	if (!force && now - sLastLog < kLogInterval)
		return;
	sLastLog = now;

	link_ring_stats stats;
	GetStats(&stats);
	int64 saved = stats.recordsIn + stats.recordsOut - stats.wakesIn
		- stats.wakesOut;
	char line[320];
	snprintf(line, sizeof(line), "app_server: link ring: %" B_PRId32
		" open, %" B_PRId64 " set up; in %" B_PRId64 " batches, %" B_PRId64
		" wakes; out %" B_PRId64 " batches, %" B_PRId64 " wakes; ordered port "
		"batches %" B_PRId64 "/%" B_PRId64 "; closed %" B_PRId64 "; port "
		"messages saved %" B_PRId64, stats.rings, stats.ringsTotal,
		stats.recordsIn, stats.wakesIn, stats.recordsOut, stats.wakesOut,
		stats.fallbacksIn, stats.fallbacksOut, stats.closed, saved);
	syslog(LOG_INFO, "%s", line);
	debug_printf("%s\n", line);
}


// #pragma mark - set-up


status_t
link_ring_connect(ServerLink& link)
{
	link_ring_disconnect(link);

	link.StartMessage(AS_R5_SET_UP_LINK_RING);
	link.Attach<int32>(kLinkRingVersion);
	int32 code;
	if (link.FlushWithReply(code) != B_OK || code != B_OK)
		return B_NOT_ALLOWED;

	area_id area;
	uint32 toServerSize;
	uint32 toClientSize;
	team_id server;
	if (link.Read<area_id>(&area) != B_OK
		|| link.Read<uint32>(&toServerSize) != B_OK
		|| link.Read<uint32>(&toClientSize) != B_OK
		|| link.Read<team_id>(&server) != B_OK)
		return B_ERROR;

	LinkRing* ring = LinkRing::CreateClient(area, toServerSize,
		toClientSize, server);
	if (ring == NULL)
		return B_ERROR;

	link_ring_attach(link, ring);
	ring->ReleaseReference();
	return B_OK;
}


void
link_ring_disconnect(ServerLink& link)
{
	link.Sender().SetRing(NULL);
	link.Receiver().SetRing(NULL);
}


void
link_ring_attach(ServerLink& link, LinkRing* ring)
{
	link.Sender().SetRing(ring);
	link.Receiver().SetRing(ring);
}


}	// namespace BPrivate
