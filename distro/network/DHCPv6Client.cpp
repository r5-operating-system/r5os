/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * DHCPv6 (RFC 8415) for IA_NA and DNS (RFC 3646).
 */


#include "DHCPv6Client.h"

#include "IPv6Dad.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet6/in6.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <Message.h>
#include <NetworkAddress.h>
#include <NetworkInterface.h>
#include <OS.h>
#include <String.h>

#include "NetServer.h"


static const uint16 kClientPort = 546;
static const uint16 kServerPort = 547;
static const int kMaxTries = 3;

static const uint8 kSolicit = 1;
static const uint8 kAdvertise = 2;
static const uint8 kRequest = 3;
static const uint8 kRenew = 5;
static const uint8 kRebind = 6;
static const uint8 kReply = 7;
static const uint8 kInformationRequest = 11;

static const uint16 kOptClientId = 1;
static const uint16 kOptServerId = 2;
static const uint16 kOptIaNa = 3;
static const uint16 kOptIaAddr = 5;
static const uint16 kOptOro = 6;
static const uint16 kOptElapsed = 8;
static const uint16 kOptStatusCode = 13;
static const uint16 kOptDnsServers = 23;

static const uint16 kStatusSuccess = 0;
static const uint16 kStatusUnspecFail = 1;
static const uint16 kStatusNoAddrsAvail = 2;
static const uint16 kStatusNoBinding = 3;


static bool
append_option(uint8* out, size_t& used, size_t cap, uint16 code,
	const void* data, uint16 length)
{
	if (used + 4 + length > cap)
		return false;
	uint16 netCode = htons(code);
	uint16 netLength = htons(length);
	memcpy(out + used, &netCode, 2);
	memcpy(out + used + 2, &netLength, 2);
	if (length > 0 && data != NULL)
		memcpy(out + used + 4, data, length);
	used += 4 + length;
	return true;
}


DHCPv6Client::DHCPv6Client(BMessenger target, const char* device)
	:
	AutoconfigClient("dhcpv6", target, device),
	fThread(-1),
	fSocket(-1),
	fWakeRead(-1),
	fWakeWrite(-1),
	fStop(0),
	fTransaction(0),
	fServerIdLength(0),
	fHaveMAC(false),
	fHaveOffer(false),
	fInstalled(false),
	fPreferred(0),
	fT1(0),
	fT2(0),
	fValid(0),
	fLeaseStart(0)
{
	memset(fMAC, 0, sizeof(fMAC));
	memset(fServerId, 0, sizeof(fServerId));
	memset(&fLeaseAddress, 0, sizeof(fLeaseAddress));

	int pipeFds[2];
	if (pipe(pipeFds) == 0) {
		fWakeRead = pipeFds[0];
		fWakeWrite = pipeFds[1];
		fcntl(fWakeRead, F_SETFL, O_NONBLOCK);
		fcntl(fWakeWrite, F_SETFL, O_NONBLOCK);
	}

	BNetworkAddress link;
	BNetworkInterface interface(device);
	if (interface.GetHardwareAddress(link) == B_OK
		&& link.LinkLevelAddressLength() == 6) {
		memcpy(fMAC, link.LinkLevelAddress(), sizeof(fMAC));
		fHaveMAC = true;
	}
	fTransaction = ((uint32)system_time() ^ (uint32)find_thread(NULL))
		& 0x00ffffff;
}


bool
DHCPv6Client::_Stopped() const
{
	return fStop != 0;
}


void
DHCPv6Client::_Wake()
{
	if (fWakeWrite < 0)
		return;
	char byte = 1;
	write(fWakeWrite, &byte, 1);
}


void
DHCPv6Client::_ForgetLease()
{
	if (!fInstalled)
		return;
	BNetworkInterface interface(Device());
	BNetworkAddress address(fLeaseAddress, 0);
	interface.RemoveAddress(address);
	fInstalled = false;
	char text[INET6_ADDRSTRLEN];
	inet_ntop(AF_INET6, &fLeaseAddress, text, sizeof(text));
	syslog(LOG_INFO, "dhcpv6 %s: removed %s\n", Device(), text);
}


DHCPv6Client::~DHCPv6Client()
{
	fStop = 1;
	_Wake();
	thread_id thread = fThread;
	if (thread != -1) {
		UnlockLooper();
		status_t result;
		wait_for_thread(thread, &result);
		LockLooper();
	}
	if (fWakeRead >= 0)
		close(fWakeRead);
	if (fWakeWrite >= 0)
		close(fWakeWrite);
	fWakeRead = -1;
	fWakeWrite = -1;
}


status_t
DHCPv6Client::Start()
{
	if (!fHaveMAC)
		return B_BAD_VALUE;
	if (fThread != -1)
		return EINPROGRESS;

	fThread = spawn_thread(_Thread, "DHCPv6", B_NORMAL_PRIORITY, this);
	return resume_thread(fThread);
}


status_t
DHCPv6Client::_Thread(void* data)
{
	DHCPv6Client* client = static_cast<DHCPv6Client*>(data);
	status_t status = client->_Run();
	client->fThread = -1;
	return status;
}


status_t
DHCPv6Client::_EnsureLinkLocal()
{
	in6_addr local;
	return r5_ipv6_link_local(Device(), local);
}


int
DHCPv6Client::_OpenSocket(uint32& ifIndex)
{
	BNetworkInterface interface(Device());
	ifIndex = interface.Index();
	if (ifIndex == 0)
		ifIndex = if_nametoindex(Device());

	int socket = ::socket(AF_INET6, SOCK_DGRAM, 0);
	if (socket < 0)
		return -1;

	int hops = 1;
	setsockopt(socket, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &hops, sizeof(hops));
	if (setsockopt(socket, SOL_SOCKET, SO_BINDTODEVICE, &ifIndex,
			sizeof(ifIndex)) < 0) {
		syslog(LOG_ERR, "dhcpv6 %s: SO_BINDTODEVICE: %s\n", Device(),
			strerror(errno));
		close(socket);
		return -1;
	}

	sockaddr_in6 local;
	memset(&local, 0, sizeof(local));
	local.sin6_len = sizeof(local);
	local.sin6_family = AF_INET6;
	local.sin6_port = htons(kClientPort);
	local.sin6_scope_id = ifIndex;
	r5_ipv6_link_local(Device(), local.sin6_addr);

	if (bind(socket, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) {
		syslog(LOG_ERR, "dhcpv6 %s: bind: %s\n", Device(), strerror(errno));
		close(socket);
		return -1;
	}

	ipv6_mreq membership;
	memset(&membership, 0, sizeof(membership));
	inet_pton(AF_INET6, "ff02::1:2", &membership.ipv6mr_multiaddr);
	membership.ipv6mr_interface = ifIndex;
	setsockopt(socket, IPPROTO_IPV6, IPV6_JOIN_GROUP, &membership,
		sizeof(membership));
	return socket;
}


static const char*
status_name(uint16 code)
{
	switch (code) {
		case kStatusSuccess:
			return "Success";
		case kStatusUnspecFail:
			return "UnspecFail";
		case kStatusNoAddrsAvail:
			return "NoAddrsAvail";
		case kStatusNoBinding:
			return "NoBinding";
		default:
			return "other";
	}
}


static uint16
read_status(const uint8* data, uint16 length, const char* device,
	const char* where)
{
	if (length < 2)
		return kStatusSuccess;
	uint16 code;
	memcpy(&code, data, 2);
	code = ntohs(code);
	if (code == kStatusSuccess) {
		syslog(LOG_INFO, "dhcpv6 %s: %s status Success\n", device, where);
	} else {
		syslog(LOG_ERR, "dhcpv6 %s: %s status %u (%s)\n", device, where, code,
			status_name(code));
	}
	return code;
}


ssize_t
DHCPv6Client::_BuildSolicit(uint8* out, size_t outSize, uint8 type)
{
	if (outSize < 4)
		return -1;
	out[0] = type;
	out[1] = (fTransaction >> 16) & 0xff;
	out[2] = (fTransaction >> 8) & 0xff;
	out[3] = fTransaction & 0xff;
	size_t used = 4;

	uint8 duid[10];
	uint16 duidType = htons(3);
	uint16 hwType = htons(1);
	memcpy(duid, &duidType, 2);
	memcpy(duid + 2, &hwType, 2);
	memcpy(duid + 4, fMAC, 6);
	if (!append_option(out, used, outSize, kOptClientId, duid, sizeof(duid)))
		return -1;

	uint16 elapsed = 0;
	if (!append_option(out, used, outSize, kOptElapsed, &elapsed, 2))
		return -1;

	uint16 dns = htons(kOptDnsServers);
	if (!append_option(out, used, outSize, kOptOro, &dns, 2))
		return -1;

	bool includeServer = type == kRequest || type == kRenew;
	bool includeAddress = (type == kRequest || type == kRenew || type == kRebind)
		&& (fHaveOffer || fInstalled);
	if (type != kInformationRequest) {
		uint8 ia[64];
		memset(ia, 0, sizeof(ia));
		memcpy(ia, fMAC + 2, 4);
		uint32 t1 = htonl(type == kSolicit ? 0 : fT1);
		uint32 t2 = htonl(type == kSolicit ? 0 : fT2);
		memcpy(ia + 4, &t1, 4);
		memcpy(ia + 8, &t2, 4);
		size_t iaLength = 12;
		if (includeAddress) {
			uint16 code = htons(kOptIaAddr);
			uint16 length = htons(24);
			memcpy(ia + iaLength, &code, 2);
			memcpy(ia + iaLength + 2, &length, 2);
			memcpy(ia + iaLength + 4, &fLeaseAddress, 16);
			uint32 preferred = htonl(fPreferred);
			uint32 valid = htonl(fValid);
			memcpy(ia + iaLength + 20, &preferred, 4);
			memcpy(ia + iaLength + 24, &valid, 4);
			iaLength += 28;
		}
		if (!append_option(out, used, outSize, kOptIaNa, ia, (uint16)iaLength))
			return -1;
	}

	if (includeServer && fServerIdLength > 0
		&& !append_option(out, used, outSize, kOptServerId, fServerId,
			fServerIdLength))
		return -1;

	return (ssize_t)used;
}


status_t
DHCPv6Client::_Send(int socket, uint32 ifIndex, const uint8* packet,
	size_t length)
{
	sockaddr_in6 dest;
	memset(&dest, 0, sizeof(dest));
	dest.sin6_len = sizeof(dest);
	dest.sin6_family = AF_INET6;
	dest.sin6_port = htons(kServerPort);
	dest.sin6_scope_id = ifIndex;
	inet_pton(AF_INET6, "ff02::1:2", &dest.sin6_addr);

	if (sendto(socket, packet, length, 0, reinterpret_cast<sockaddr*>(&dest),
			sizeof(dest)) < 0) {
		syslog(LOG_ERR, "dhcpv6 %s: send: %s\n", Device(), strerror(errno));
		return errno;
	}
	return B_OK;
}


status_t
DHCPv6Client::_HandleReply(const uint8* packet, ssize_t bytes, uint32 ifIndex)
{
	if (bytes < 4)
		return B_BAD_VALUE;
	uint8 type = packet[0];
	uint32 xid = ((uint32)packet[1] << 16) | ((uint32)packet[2] << 8) | packet[3];
	if (xid != fTransaction)
		return B_BAD_VALUE;
	if (type != kAdvertise && type != kReply)
		return B_BAD_VALUE;

	const uint8* option = packet + 4;
	const uint8* end = packet + bytes;
	in6_addr leased;
	bool haveAddress = false;
	uint32 preferred = 0;
	uint32 valid = 0;
	uint32 t1 = 0;
	uint32 t2 = 0;
	bool haveTiming = false;
	BMessage resolver(kMsgConfigureResolver);
	int32 nameservers = 0;
	bool failed = false;

	while (option + 4 <= end) {
		uint16 code;
		uint16 length;
		memcpy(&code, option, 2);
		memcpy(&length, option + 2, 2);
		code = ntohs(code);
		length = ntohs(length);
		const uint8* data = option + 4;
		if (data + length > end)
			break;

		if (code == kOptStatusCode) {
			if (read_status(data, length, Device(), "reply") != kStatusSuccess)
				failed = true;
		} else if (code == kOptServerId && length <= sizeof(fServerId)) {
			memcpy(fServerId, data, length);
			fServerIdLength = length;
		} else if (code == kOptIaNa && length >= 12) {
			memcpy(&t1, data + 4, 4);
			memcpy(&t2, data + 8, 4);
			t1 = ntohl(t1);
			t2 = ntohl(t2);
			haveTiming = true;
			const uint8* inner = data + 12;
			const uint8* innerEnd = data + length;
			bool iaFailed = false;
			while (inner + 4 <= innerEnd) {
				uint16 innerCode;
				uint16 innerLength;
				memcpy(&innerCode, inner, 2);
				memcpy(&innerLength, inner + 2, 2);
				innerCode = ntohs(innerCode);
				innerLength = ntohs(innerLength);
				if (inner + 4 + innerLength > innerEnd)
					break;
				if (innerCode == kOptStatusCode) {
					if (read_status(inner + 4, innerLength, Device(), "IA_NA")
							!= kStatusSuccess)
						iaFailed = true;
				} else if (innerCode == kOptIaAddr && innerLength >= 24
					&& !iaFailed) {
					bool addrFailed = false;
					if (innerLength > 24) {
						const uint8* extra = inner + 4 + 24;
						const uint8* extraEnd = inner + 4 + innerLength;
						while (extra + 4 <= extraEnd) {
							uint16 extraCode;
							uint16 extraLength;
							memcpy(&extraCode, extra, 2);
							memcpy(&extraLength, extra + 2, 2);
							extraCode = ntohs(extraCode);
							extraLength = ntohs(extraLength);
							if (extra + 4 + extraLength > extraEnd)
								break;
							if (extraCode == kOptStatusCode
								&& read_status(extra + 4, extraLength, Device(),
									"IA_ADDR") != kStatusSuccess)
								addrFailed = true;
							extra += 4 + extraLength;
						}
					}
					if (!addrFailed) {
						memcpy(&leased, inner + 4, 16);
						memcpy(&preferred, inner + 20, 4);
						memcpy(&valid, inner + 24, 4);
						preferred = ntohl(preferred);
						valid = ntohl(valid);
						haveAddress = true;
					}
				}
				inner += 4 + innerLength;
			}
			if (iaFailed)
				haveAddress = false;
		} else if (code == kOptDnsServers && length >= 16) {
			for (uint16 offset = 0; offset + 16 <= length; offset += 16) {
				in6_addr dns;
				memcpy(&dns, data + offset, 16);
				char text[INET6_ADDRSTRLEN];
				if (inet_ntop(AF_INET6, &dns, text, sizeof(text)) != NULL) {
					resolver.AddString("nameserver", text);
					nameservers++;
				}
			}
		}

		option += 4 + length;
	}

	if (failed)
		return B_ERROR;

	if (type == kAdvertise) {
		if (haveAddress) {
			fLeaseAddress = leased;
			fPreferred = preferred;
			fValid = valid;
			fT1 = t1;
			fT2 = t2;
			fHaveOffer = true;
		}
		return 1;
	}

	if (haveAddress) {
		char text[INET6_ADDRSTRLEN];
		inet_ntop(AF_INET6, &leased, text, sizeof(text));
		fLeaseAddress = leased;
		fPreferred = preferred;
		fValid = valid;
		if (haveTiming) {
			fT1 = t1;
			fT2 = t2;
		}
		if (fValid != 0 && fValid != 0xffffffff) {
			if (fT1 == 0 || fT1 >= fValid)
				fT1 = fValid / 2;
			if (fT2 == 0 || fT2 > fValid || fT2 < fT1)
				fT2 = fValid - fValid / 5;
		}
		fHaveOffer = true;
		fLeaseStart = system_time();
		if (valid == 0) {
			_ForgetLease();
			fHaveOffer = false;
			syslog(LOG_INFO, "dhcpv6 %s: %s valid lifetime 0\n", Device(),
				text);
		} else if (r5_ipv6_has_address(Device(), leased)) {
			fInstalled = true;
			syslog(LOG_INFO, "dhcpv6 %s: %s already assigned, T1=%" B_PRIu32
				" T2=%" B_PRIu32 " valid=%" B_PRIu32 "\n", Device(), text,
				fT1, fT2, fValid);
		} else {
			status_t dad = r5_ipv6_dad_until(Device(), ifIndex, fMAC, leased,
				&fStop);
			if (dad != B_OK)
				return dad;

			BNetworkInterface interface(Device());
			BNetworkAddress address(leased, 0);
			BNetworkAddress mask(AF_INET6,
				"ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", (uint16)0,
				B_UNCONFIGURED_ADDRESS_FAMILIES);
			BNetworkInterfaceAddress slot;
			slot.SetAddress(address);
			slot.SetMask(mask);
			status_t status = interface.AddAddress(slot);
			if (status != B_OK) {
				syslog(LOG_ERR, "dhcpv6 %s: add address failed: %s\n",
					Device(), strerror(status));
				return status;
			}
			fInstalled = true;
			syslog(LOG_INFO, "dhcpv6 %s: %s/128 T1=%" B_PRIu32 " T2=%" B_PRIu32
				" valid=%" B_PRIu32 "\n", Device(), text, fT1, fT2, fValid);
		}
	}

	if (nameservers > 0) {
		resolver.AddString("device", Device());
		resolver.AddInt32("nameserver_count", nameservers);
		Target().SendMessage(&resolver);
	}

	if (!haveAddress && nameservers == 0)
		return B_BAD_VALUE;
	return B_OK;
}


status_t
DHCPv6Client::_Run()
{
	for (int32 i = 0; i < 15 && !_Stopped(); i++) {
		if (_EnsureLinkLocal() == B_OK)
			break;
		for (int32 slice = 0; slice < 2 && !_Stopped(); slice++)
			snooze(100000);
	}
	if (_Stopped())
		return B_CANCELED;
	if (_EnsureLinkLocal() != B_OK) {
		syslog(LOG_ERR, "dhcpv6 %s: waiting for link-local timed out\n",
			Device());
		return B_TIMED_OUT;
	}

	uint32 ifIndex = 0;
	int socket = _OpenSocket(ifIndex);
	if (socket < 0)
		return errno;
	fSocket = socket;

	uint8 phase = kSolicit;
	int32 solicitTries = 0;
	bool askedForInformation = false;
	bigtime_t nextSend = 0;
	status_t status = B_TIMED_OUT;

	while (!_Stopped()) {
		bigtime_t now = system_time();
		if (fInstalled && fValid != 0 && fValid != 0xffffffff) {
			bigtime_t expireAt = fLeaseStart + (bigtime_t)fValid * 1000000;
			if (now >= expireAt) {
				_ForgetLease();
				fHaveOffer = false;
				fServerIdLength = 0;
				phase = kSolicit;
				nextSend = now;
				syslog(LOG_INFO, "dhcpv6 %s: lease expired\n", Device());
			}
		}

		if (now >= nextSend) {
			uint8 type = 0;
			if (fInstalled) {
				if (fT1 != 0xffffffff) {
					bigtime_t renewAt = fLeaseStart
						+ (bigtime_t)fT1 * 1000000;
					bigtime_t rebindAt = fLeaseStart
						+ (bigtime_t)fT2 * 1000000;
					if (now >= rebindAt)
						type = kRebind;
					else if (now >= renewAt)
						type = kRenew;
				}
			} else if (solicitTries >= kMaxTries && phase == kSolicit
				&& !askedForInformation) {
				type = kInformationRequest;
				askedForInformation = true;
			} else
				type = phase;

			if (type != 0) {
				fTransaction = (fTransaction + 1) & 0x00ffffff;
				if (fTransaction == 0)
					fTransaction = 1;
				uint8 packet[768];
				ssize_t length = _BuildSolicit(packet, sizeof(packet), type);
				if (length > 0)
					_Send(socket, ifIndex, packet, (size_t)length);
				if (type == kSolicit)
					solicitTries++;
				if (type == kRenew || type == kRebind)
					nextSend = now + 10000000;
				else if (type == kInformationRequest)
					nextSend = now + 60000000;
				else
					nextSend = now + 2000000;
				syslog(LOG_INFO, "dhcpv6 %s: sent type %u\n", Device(), type);
			} else if (fInstalled && fT1 != 0xffffffff) {
				nextSend = fLeaseStart + (bigtime_t)fT1 * 1000000;
			} else
				nextSend = now + 60000000;
		}

		bigtime_t waitUs = 1000000;
		if (nextSend > system_time()) {
			waitUs = nextSend - system_time();
			if (waitUs > 1000000)
				waitUs = 1000000;
		}
		fd_set readSet;
		FD_ZERO(&readSet);
		FD_SET(socket, &readSet);
		int maxFd = socket;
		if (fWakeRead >= 0) {
			FD_SET(fWakeRead, &readSet);
			if (fWakeRead > maxFd)
				maxFd = fWakeRead;
		}
		timeval wait;
		wait.tv_sec = waitUs / 1000000;
		wait.tv_usec = waitUs % 1000000;
		int ready = select(maxFd + 1, &readSet, NULL, NULL, &wait);
		if (ready < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (fWakeRead >= 0 && FD_ISSET(fWakeRead, &readSet)) {
			char ignored;
			while (read(fWakeRead, &ignored, 1) > 0)
				;
		}
		if (ready == 0 || _Stopped() || !FD_ISSET(socket, &readSet))
			continue;

		uint8 incoming[1280];
		sockaddr_in6 from;
		socklen_t fromLen = sizeof(from);
		ssize_t bytes = recvfrom(socket, incoming, sizeof(incoming), 0,
			reinterpret_cast<sockaddr*>(&from), &fromLen);
		if (bytes < 0)
			continue;

		status = _HandleReply(incoming, bytes, ifIndex);
		if (status == 1) {
			phase = kRequest;
			nextSend = system_time();
		} else if (status == B_OK && fInstalled) {
			nextSend = fT1 == 0xffffffff
				? system_time() + 60000000
				: fLeaseStart + (bigtime_t)fT1 * 1000000;
		} else if (status != B_OK && status != 1) {
			if (fInstalled && (status == B_ERROR || status == B_BAD_VALUE)) {
				_ForgetLease();
				fHaveOffer = false;
				fServerIdLength = 0;
			}
			phase = kSolicit;
			nextSend = system_time() + 2000000;
		}
	}

	fSocket = -1;
	close(socket);
	if (!fInstalled && status != B_OK)
		syslog(LOG_INFO, "dhcpv6 %s: no server\n", Device());
	return _Stopped() ? B_CANCELED : status;
}
