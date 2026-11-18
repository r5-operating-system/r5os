/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * IPv6 SLAAC for net_server (RFC 4861/4862) with DAD (Haiku #8319).
 */


#include "SLAACClient.h"
#include "IPv6Dad.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/icmp6.h>
#include <netinet/in.h>
#include <netinet6/in6.h>
#include <net/route.h>
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
#include <NetworkRoute.h>
#include <OS.h>
#include <String.h>

#include "NetServer.h"


static const int kMaxSolicits = 3;
static const bigtime_t kSolicitInterval = 4000000;
static const bigtime_t kRetryInterval = 60000000;
static const uint8 kRdnsOption = 25;


SLAACClient::SLAACClient(BMessenger target, const char* device)
	:
	AutoconfigClient("slaac", target, device),
	fThread(-1),
	fSocket(-1),
	fWakeRead(-1),
	fWakeWrite(-1),
	fStop(0),
	fHaveMAC(false),
	fHaveDefaultRoute(false),
	fRouterUntil(0)
{
	memset(fMAC, 0, sizeof(fMAC));
	memset(&fDefaultGateway, 0, sizeof(fDefaultGateway));
	memset(fPrefixes, 0, sizeof(fPrefixes));

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
}


bool
SLAACClient::_Stopped() const
{
	return fStop != 0;
}


void
SLAACClient::_Wake()
{
	if (fWakeWrite < 0)
		return;
	char byte = 1;
	write(fWakeWrite, &byte, 1);
}


SLAACClient::~SLAACClient()
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
	// The client goes away with the link (AutoconfigLooper deletes it on
	// link down): take its addresses, on-link routes and default route with
	// it, so a network switch does not leave the old prefix behind.
	for (int32 i = 0; i < 8; i++) {
		if (fPrefixes[i].inUse)
			_WithdrawPrefix(fPrefixes[i]);
	}
	_RemoveDefaultRoute("link gone");
	if (fWakeRead >= 0)
		close(fWakeRead);
	if (fWakeWrite >= 0)
		close(fWakeWrite);
	fWakeRead = -1;
	fWakeWrite = -1;
}


status_t
SLAACClient::Start()
{
	if (!fHaveMAC)
		return B_BAD_VALUE;
	if (fThread != -1)
		return EINPROGRESS;

	fThread = spawn_thread(_Thread, "SLAAC", B_NORMAL_PRIORITY, this);
	return resume_thread(fThread);
}


status_t
SLAACClient::_Thread(void* data)
{
	SLAACClient* client = static_cast<SLAACClient*>(data);
	status_t status = client->_Run();
	client->fThread = -1;
	return status;
}


status_t
SLAACClient::_EnsureLinkLocal(BNetworkInterface& interface)
{
	in6_addr raw;
	memset(&raw, 0, sizeof(raw));
	raw.s6_addr[0] = 0xfe;
	raw.s6_addr[1] = 0x80;
	raw.s6_addr[8] = fMAC[0] ^ 0x02;
	raw.s6_addr[9] = fMAC[1];
	raw.s6_addr[10] = fMAC[2];
	raw.s6_addr[11] = 0xff;
	raw.s6_addr[12] = 0xfe;
	raw.s6_addr[13] = fMAC[3];
	raw.s6_addr[14] = fMAC[4];
	raw.s6_addr[15] = fMAC[5];

	in6_addr existing;
	if (r5_ipv6_link_local(Device(), existing) == B_OK)
		return B_OK;
	if (_Stopped())
		return B_CANCELED;

	uint32 ifIndex = interface.Index();
	if (ifIndex == 0)
		ifIndex = if_nametoindex(Device());
	// Only another node holding the address stops us. DAD runs before the
	// interface has any IPv6 address, so its socket may fail (no source to
	// send from); that is not a collision, and refusing the link-local
	// address would turn IPv6 off on the link (QEMU, link-local only LANs).
	status_t dad = r5_ipv6_dad_until(Device(), ifIndex, fMAC, raw, &fStop);
	if (dad == B_NAME_IN_USE || dad == B_CANCELED)
		return dad;
	if (dad != B_OK) {
		syslog(LOG_INFO, "slaac %s: DAD could not run (%s); adding the "
			"link-local address\n", Device(), strerror(dad));
	}

	BNetworkAddress local(raw, 0);
	BNetworkAddress mask(AF_INET6, "ffff:ffff:ffff:ffff::", (uint16)0,
		B_UNCONFIGURED_ADDRESS_FAMILIES);
	BNetworkInterfaceAddress address;
	address.SetAddress(local);
	address.SetMask(mask);
	return interface.AddAddress(address);
}


void
SLAACClient::_BuildSlaacAddress(const in6_addr& prefix, in6_addr& out) const
{
	out = prefix;
	out.s6_addr[8] = fMAC[0] ^ 0x02;
	out.s6_addr[9] = fMAC[1];
	out.s6_addr[10] = fMAC[2];
	out.s6_addr[11] = 0xff;
	out.s6_addr[12] = 0xfe;
	out.s6_addr[13] = fMAC[3];
	out.s6_addr[14] = fMAC[4];
	out.s6_addr[15] = fMAC[5];
}


BString
SLAACClient::_ToString(const in6_addr& address) const
{
	char text[INET6_ADDRSTRLEN];
	if (inet_ntop(AF_INET6, &address, text, sizeof(text)) == NULL)
		return BString();
	return BString(text);
}


status_t
SLAACClient::_BindAndJoin(int socket, uint32 ifIndex)
{
	int hops = 255;
	setsockopt(socket, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &hops, sizeof(hops));
	setsockopt(socket, IPPROTO_IPV6, IPV6_UNICAST_HOPS, &hops, sizeof(hops));
	if (setsockopt(socket, SOL_SOCKET, SO_BINDTODEVICE, &ifIndex,
			sizeof(ifIndex)) < 0) {
		syslog(LOG_ERR, "slaac %s: SO_BINDTODEVICE: %s\n", Device(),
			strerror(errno));
		return errno;
	}

	BNetworkInterface interface(Device());
	sockaddr_in6 local;
	memset(&local, 0, sizeof(local));
	local.sin6_len = sizeof(local);
	local.sin6_family = AF_INET6;
	local.sin6_scope_id = ifIndex;

	BNetworkInterfaceAddress slot;
	int32 count = interface.CountAddresses();
	for (int32 i = 0; i < count; i++) {
		if (interface.GetAddressAt(i, slot) != B_OK)
			continue;
		const BNetworkAddress& address = slot.Address();
		if (address.Family() != AF_INET6)
			continue;
		const sockaddr_in6& in6
			= *reinterpret_cast<const sockaddr_in6*>(&address.SockAddr());
		if (!IN6_IS_ADDR_LINKLOCAL(&in6.sin6_addr))
			continue;
		local.sin6_addr = in6.sin6_addr;
		break;
	}

	if (bind(socket, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) {
		syslog(LOG_ERR, "slaac %s: bind failed: %s\n", Device(),
			strerror(errno));
	}

	ipv6_mreq membership;
	memset(&membership, 0, sizeof(membership));
	inet_pton(AF_INET6, "ff02::1", &membership.ipv6mr_multiaddr);
	membership.ipv6mr_interface = ifIndex;
	if (setsockopt(socket, IPPROTO_IPV6, IPV6_JOIN_GROUP, &membership,
			sizeof(membership)) < 0) {
		syslog(LOG_ERR, "slaac %s: join ff02::1 failed: %s (index %" B_PRIu32
			")\n", Device(), strerror(errno), ifIndex);
		return errno;
	}
	return B_OK;
}


status_t
SLAACClient::_SendSolicit(int socket, uint32 ifIndex)
{
	uint8 packet[16];
	memset(packet, 0, sizeof(packet));
	struct nd_router_solicit* rs
		= reinterpret_cast<struct nd_router_solicit*>(packet);
	rs->nd_rs_type = ND_ROUTER_SOLICIT;
	rs->nd_rs_code = 0;
	rs->nd_rs_cksum = 0;
	packet[8] = ND_OPT_SOURCE_LINKADDR;
	packet[9] = 1;
	memcpy(packet + 10, fMAC, 6);

	sockaddr_in6 dest;
	memset(&dest, 0, sizeof(dest));
	dest.sin6_len = sizeof(dest);
	dest.sin6_family = AF_INET6;
	dest.sin6_scope_id = ifIndex;
	inet_pton(AF_INET6, "ff02::2", &dest.sin6_addr);

	if (sendto(socket, packet, sizeof(packet), 0,
			reinterpret_cast<sockaddr*>(&dest), sizeof(dest)) < 0) {
		syslog(LOG_ERR, "slaac %s: RS send failed: %s\n", Device(),
			strerror(errno));
		return errno;
	}
	syslog(LOG_INFO, "slaac %s: router solicitation sent\n", Device());
	return B_OK;
}


status_t
SLAACClient::_ApplyDefaultRoute(const sockaddr_in6& from, uint32 ifIndex)
{
	sockaddr_in6 gatewayAddr = from;
	gatewayAddr.sin6_len = sizeof(gatewayAddr);
	gatewayAddr.sin6_family = AF_INET6;
	gatewayAddr.sin6_port = 0;
	if (IN6_IS_ADDR_LINKLOCAL(&gatewayAddr.sin6_addr)
		&& gatewayAddr.sin6_scope_id == 0)
		gatewayAddr.sin6_scope_id = ifIndex;

	// The same router again: the route is already there. Removing and adding
	// it on every RA left a gap with no default route.
	if (fHaveDefaultRoute
		&& memcmp(&fDefaultGateway.sin6_addr, &gatewayAddr.sin6_addr,
			sizeof(gatewayAddr.sin6_addr)) == 0
		&& fDefaultGateway.sin6_scope_id == gatewayAddr.sin6_scope_id)
		return B_OK;

	BNetworkAddress gateway(gatewayAddr);
	BNetworkInterface routeInterface(Device());
	bool removed = false;
	if (fHaveDefaultRoute) {
		BNetworkRoute previous;
		previous.SetGateway(
			reinterpret_cast<const sockaddr&>(fDefaultGateway));
		previous.SetFlags(RTF_STATIC | RTF_DEFAULT | RTF_GATEWAY);
		if (routeInterface.RemoveRoute(AF_INET6, previous) == B_OK)
			removed = true;
	}
	status_t routeStatus = routeInterface.AddDefaultRoute(gateway);
	if (routeStatus != B_OK) {
		if (removed)
			fHaveDefaultRoute = false;
		syslog(LOG_ERR, "slaac %s: default route failed: %s\n", Device(),
			strerror(routeStatus));
		return routeStatus;
	}
	fDefaultGateway = gatewayAddr;
	syslog(LOG_INFO, "slaac %s: default route via %s%%%" B_PRIu32 "\n",
		Device(), _ToString(gatewayAddr.sin6_addr).String(),
		gatewayAddr.sin6_scope_id);
	return B_OK;
}


void
SLAACClient::_RemoveDefaultRoute(const char* why)
{
	if (!fHaveDefaultRoute)
		return;
	BNetworkRoute route;
	BNetworkInterface routeInterface(Device());
	route.SetGateway(reinterpret_cast<const sockaddr&>(fDefaultGateway));
	route.SetFlags(RTF_STATIC | RTF_DEFAULT | RTF_GATEWAY);
	routeInterface.RemoveRoute(AF_INET6, route);
	fHaveDefaultRoute = false;
	fRouterUntil = 0;
	syslog(LOG_INFO, "slaac %s: route via %s removed (%s)\n", Device(),
		_ToString(fDefaultGateway.sin6_addr).String(), why);
}


void
SLAACClient::_ApplyResolver(BMessage& resolver, int32 nameservers)
{
	if (nameservers <= 0)
		return;
	resolver.AddString("device", Device());
	resolver.AddInt32("nameserver_count", nameservers);
	Target().SendMessage(&resolver);
}


static void
prefix_mask(in6_addr& mask, uint8 length)
{
	memset(&mask, 0, sizeof(mask));
	if (length > 128)
		length = 128;
	for (uint8 i = 0; i < length / 8; i++)
		mask.s6_addr[i] = 0xff;
	if ((length % 8) != 0)
		mask.s6_addr[length / 8] = (uint8)(0xff << (8 - (length % 8)));
}


static void
mask_prefix(in6_addr& prefix, uint8 length)
{
	if (length > 128)
		length = 128;
	uint8 first = length / 8;
	if ((length % 8) != 0) {
		prefix.s6_addr[first] &= (uint8)(0xff << (8 - (length % 8)));
		first++;
	}
	for (uint8 i = first; i < 16; i++)
		prefix.s6_addr[i] = 0;
}


void
SLAACClient::_ExpirePrefixes()
{
	bigtime_t now = system_time();
	// The router lifetime ends like a prefix does (RFC 4861 section 6.3.4).
	if (fHaveDefaultRoute && fRouterUntil != 0 && now >= fRouterUntil)
		_RemoveDefaultRoute("router lifetime ended");
	for (int32 i = 0; i < 8; i++) {
		if (!fPrefixes[i].inUse || fPrefixes[i].validUntil == 0)
			continue;
		if (now < fPrefixes[i].validUntil)
			continue;
		syslog(LOG_INFO, "slaac %s: prefix %s/%u expired\n", Device(),
			_ToString(fPrefixes[i].prefix).String(), fPrefixes[i].length);
		_WithdrawPrefix(fPrefixes[i]);
	}
}


void
SLAACClient::_WithdrawPrefix(PrefixRecord& record)
{
	BNetworkInterface interface(Device());
	if (record.haveAddress) {
		BNetworkAddress address(record.address, 0);
		interface.RemoveAddress(address);
		record.haveAddress = false;
	}
	if (record.onLink) {
		in6_addr rawMask;
		prefix_mask(rawMask, record.length);
		sockaddr_in6 destination;
		sockaddr_in6 mask;
		memset(&destination, 0, sizeof(destination));
		memset(&mask, 0, sizeof(mask));
		destination.sin6_len = sizeof(destination);
		destination.sin6_family = AF_INET6;
		destination.sin6_addr = record.prefix;
		mask.sin6_len = sizeof(mask);
		mask.sin6_family = AF_INET6;
		mask.sin6_addr = rawMask;
		BNetworkRoute route;
		route.SetDestination(reinterpret_cast<const sockaddr&>(destination));
		route.SetMask(reinterpret_cast<const sockaddr&>(mask));
		route.SetFlags(RTF_STATIC);
		interface.RemoveRoute(AF_INET6, route);
		record.onLink = false;
	}
	record.inUse = false;
}


status_t
SLAACClient::_ApplyPrefix(const in6_addr& rawPrefix, uint8 length, uint8 flags,
	uint32 valid, uint32 ifIndex)
{
	bool autonomous = (flags & ND_OPT_PI_FLAG_AUTO) != 0 && length == 64
		&& !IN6_IS_ADDR_LINKLOCAL(&rawPrefix);
	bool onLink = (flags & ND_OPT_PI_FLAG_ONLINK) != 0 && length > 0
		&& length <= 128;
	if (!autonomous && !onLink)
		return B_OK;

	in6_addr prefix = rawPrefix;
	mask_prefix(prefix, length);

	PrefixRecord* slot = NULL;
	for (int32 i = 0; i < 8; i++) {
		if (fPrefixes[i].inUse
			&& fPrefixes[i].length == length
			&& memcmp(&fPrefixes[i].prefix, &prefix, sizeof(prefix)) == 0) {
			slot = &fPrefixes[i];
			break;
		}
	}
	if (slot == NULL) {
		for (int32 i = 0; i < 8; i++) {
			if (!fPrefixes[i].inUse) {
				slot = &fPrefixes[i];
				memset(slot, 0, sizeof(*slot));
				slot->prefix = prefix;
				slot->length = length;
				slot->inUse = true;
				break;
			}
		}
	}
	if (slot == NULL) {
		syslog(LOG_ERR, "slaac %s: prefix table full\n", Device());
		return B_NO_MEMORY;
	}

	bigtime_t now = system_time();
	static const bigtime_t kTwoHours = 7200 * (bigtime_t)1000000;
	if (slot->haveAddress) {
		// RFC 4862 section 5.5.3 e): an RA can shorten the lifetime of an
		// address we hold only to two hours, so one forged RA cannot take
		// the address away (valid 0 included).
		bigtime_t received = valid == 0xffffffff ? 0
			: now + (bigtime_t)valid * 1000000;
		bool infiniteNow = slot->validUntil == 0;
		bigtime_t remaining = infiniteNow ? B_INFINITE_TIMEOUT
			: slot->validUntil - now;
		if (valid == 0xffffffff || (bigtime_t)valid * 1000000 > kTwoHours
			|| (!infiniteNow && received > slot->validUntil)) {
			slot->validUntil = received;
		} else if (remaining <= kTwoHours) {
			// keep the lifetime we have
		} else {
			slot->validUntil = now + kTwoHours;
			syslog(LOG_INFO, "slaac %s: prefix %s/%u lifetime cut to 2 hours\n",
				Device(), _ToString(prefix).String(), length);
		}
	} else if (valid == 0) {
		syslog(LOG_INFO, "slaac %s: prefix %s/%u withdrawn\n", Device(),
			_ToString(prefix).String(), length);
		_WithdrawPrefix(*slot);
		return B_OK;
	} else if (valid == 0xffffffff)
		slot->validUntil = 0;
	else
		slot->validUntil = now + (bigtime_t)valid * 1000000;
	if (valid == 0)
		return B_OK;

	if (onLink && !slot->onLink) {
		in6_addr rawMask;
		prefix_mask(rawMask, length);
		sockaddr_in6 destinationAddr;
		sockaddr_in6 maskAddr;
		memset(&destinationAddr, 0, sizeof(destinationAddr));
		memset(&maskAddr, 0, sizeof(maskAddr));
		destinationAddr.sin6_len = sizeof(destinationAddr);
		destinationAddr.sin6_family = AF_INET6;
		destinationAddr.sin6_addr = prefix;
		maskAddr.sin6_len = sizeof(maskAddr);
		maskAddr.sin6_family = AF_INET6;
		maskAddr.sin6_addr = rawMask;
		BNetworkRoute route;
		route.SetDestination(
			reinterpret_cast<const sockaddr&>(destinationAddr));
		route.SetMask(reinterpret_cast<const sockaddr&>(maskAddr));
		route.SetFlags(RTF_STATIC);
		BNetworkInterface interface(Device());
		status_t status = interface.AddRoute(route);
		if (status == B_OK || status == B_FILE_EXISTS) {
			slot->onLink = true;
			syslog(LOG_INFO, "slaac %s: on-link %s/%u\n", Device(),
				_ToString(prefix).String(), length);
		} else {
			syslog(LOG_ERR, "slaac %s: on-link %s/%u failed: %s\n", Device(),
				_ToString(prefix).String(), length, strerror(status));
		}
	}

	if (!autonomous || _Stopped())
		return _Stopped() ? B_CANCELED : B_OK;

	in6_addr global;
	_BuildSlaacAddress(prefix, global);
	slot->address = global;
	if (slot->haveAddress || r5_ipv6_has_address(Device(), global)) {
		slot->haveAddress = true;
		return B_OK;
	}

	status_t dad = r5_ipv6_dad_until(Device(), ifIndex, fMAC, global, &fStop);
	if (dad != B_OK)
		return dad;

	BNetworkInterface interface(Device());
	BNetworkAddress address(global, 0);
	BNetworkAddress mask(AF_INET6, "ffff:ffff:ffff:ffff::", (uint16)0,
		B_UNCONFIGURED_ADDRESS_FAMILIES);
	BNetworkInterfaceAddress slotAddress;
	slotAddress.SetAddress(address);
	slotAddress.SetMask(mask);
	status_t status = interface.AddAddress(slotAddress);
	if (status != B_OK && status != B_FILE_EXISTS) {
		syslog(LOG_ERR, "slaac %s: add %s failed: %s\n", Device(),
			_ToString(global).String(), strerror(status));
		return status;
	}
	slot->haveAddress = true;
	syslog(LOG_INFO, "slaac %s: %s\n", Device(), _ToString(global).String());
	return B_OK;
}


status_t
SLAACClient::_ApplyAdvert(const uint8* buffer, ssize_t bytes,
	const sockaddr_in6& from, uint32 ifIndex)
{
	if (bytes < (ssize_t)sizeof(struct nd_router_advert))
		return B_BAD_VALUE;

	// RFC 4861 section 6.1.2: a router advertisement comes from a link-local
	// address with hop limit 255, so it cannot have been routed here. The
	// hop limit can only be checked when the raw socket hands us the IPv6
	// header.
	if (!IN6_IS_ADDR_LINKLOCAL(&from.sin6_addr)) {
		syslog(LOG_INFO, "slaac %s: RA from %s ignored: not link-local\n",
			Device(), _ToString(from.sin6_addr).String());
		return B_BAD_VALUE;
	}

	const uint8* data = buffer;
	ssize_t length = bytes;
	if (length >= 40 && (data[0] >> 4) == 6) {
		if (data[7] != 255) {
			syslog(LOG_INFO, "slaac %s: RA ignored: hop limit %u\n", Device(),
				data[7]);
			return B_BAD_VALUE;
		}
		data += 40;
		length -= 40;
	}
	if (length < (ssize_t)sizeof(struct nd_router_advert))
		return B_BAD_VALUE;

	const struct nd_router_advert* ra
		= reinterpret_cast<const struct nd_router_advert*>(data);
	if (ra->nd_ra_type != ND_ROUTER_ADVERT || ra->nd_ra_code != 0)
		return B_BAD_VALUE;

	uint16 lifetime = ntohs(ra->nd_ra_router_lifetime);
	syslog(LOG_INFO, "slaac %s: RA from %s lifetime=%u M=%d O=%d\n",
		Device(), _ToString(from.sin6_addr).String(), lifetime,
		(ra->nd_ra_flags_reserved & ND_RA_FLAG_MANAGED) != 0,
		(ra->nd_ra_flags_reserved & ND_RA_FLAG_OTHER) != 0);
	const uint8* option = data + sizeof(struct nd_router_advert);
	const uint8* end = data + length;

	bool havePrefix = false;
	bool addedAddress = false;
	BMessage resolver(kMsgConfigureResolver);
	int32 nameservers = 0;
	uint8 raFlags = ra->nd_ra_flags_reserved;
	bool managed = (raFlags & ND_RA_FLAG_MANAGED) != 0;
	bool other = (raFlags & ND_RA_FLAG_OTHER) != 0;

	while (option + 2 <= end) {
		uint8 type = option[0];
		uint8 optLen = option[1];
		if (optLen == 0)
			break;
		size_t bytesLen = optLen * 8;
		if (option + bytesLen > end)
			break;

		if (type == ND_OPT_PREFIX_INFORMATION
			&& bytesLen >= sizeof(struct nd_opt_prefix_info)) {
			const struct nd_opt_prefix_info* info
				= reinterpret_cast<const struct nd_opt_prefix_info*>(option);
			char prefixText[INET6_ADDRSTRLEN];
			inet_ntop(AF_INET6, &info->nd_opt_pi_prefix, prefixText,
				sizeof(prefixText));
			syslog(LOG_INFO, "slaac %s: PIO %s/%u A=%d L=%d\n", Device(),
				prefixText, info->nd_opt_pi_prefix_len,
				(info->nd_opt_pi_flags_reserved & ND_OPT_PI_FLAG_AUTO) != 0,
				(info->nd_opt_pi_flags_reserved & ND_OPT_PI_FLAG_ONLINK) != 0);
			uint32 valid = ntohl(info->nd_opt_pi_valid_time);
			uint32 preferred = ntohl(info->nd_opt_pi_preferred_time);
			// RFC 4862 section 5.5.3 c): preferred above valid is ignored.
			if (preferred > valid) {
				syslog(LOG_INFO, "slaac %s: PIO %s ignored: preferred %" B_PRIu32
					" > valid %" B_PRIu32 "\n", Device(), prefixText, preferred,
					valid);
				option += bytesLen;
				continue;
			}
			bool autonomous
				= (info->nd_opt_pi_flags_reserved & ND_OPT_PI_FLAG_AUTO) != 0
				&& info->nd_opt_pi_prefix_len == 64
				&& !IN6_IS_ADDR_LINKLOCAL(&info->nd_opt_pi_prefix);
			if (autonomous)
				havePrefix = true;
			status_t prefixStatus = _ApplyPrefix(info->nd_opt_pi_prefix,
				info->nd_opt_pi_prefix_len, info->nd_opt_pi_flags_reserved,
				valid, ifIndex);
			if (prefixStatus == B_CANCELED)
				return prefixStatus;
			if (autonomous && prefixStatus == B_OK && valid > 0)
				addedAddress = true;
		} else if (type == kRdnsOption && bytesLen >= 24) {
			const uint8* addr = option + 8;
			const uint8* addrEnd = option + bytesLen;
			while (addr + 16 <= addrEnd) {
				in6_addr dns;
				memcpy(&dns, addr, 16);
				BString text = _ToString(dns);
				if (text.Length() > 0) {
					resolver.AddString("nameserver", text.String());
					nameservers++;
				}
				addr += 16;
			}
		}

		option += bytesLen;
	}

	_ApplyResolver(resolver, nameservers);

	if (!havePrefix) {
		syslog(LOG_INFO, "slaac %s: RA from %s M=%d O=%d without SLAAC /64 "
			"prefix (DHCPv6 %s)\n", Device(),
			_ToString(from.sin6_addr).String(), managed, other,
			(managed || other) ? "should run" : "may be absent");
	}

	sockaddr_in6 router = from;
	if (IN6_IS_ADDR_LINKLOCAL(&router.sin6_addr) && router.sin6_scope_id == 0)
		router.sin6_scope_id = ifIndex;
	bool sameRouter = fHaveDefaultRoute
		&& memcmp(&fDefaultGateway.sin6_addr, &router.sin6_addr,
			sizeof(router.sin6_addr)) == 0
		&& fDefaultGateway.sin6_scope_id == router.sin6_scope_id;
	if (lifetime == 0) {
		// Only the router that is our gateway can take the route away.
		if (sameRouter)
			_RemoveDefaultRoute("RA lifetime 0");
	} else {
		// A second router does not replace a working one; it may take over
		// once the first one's lifetime has ended.
		if (!fHaveDefaultRoute || sameRouter) {
			if (_ApplyDefaultRoute(from, ifIndex) == B_OK) {
				fHaveDefaultRoute = true;
				fRouterUntil = system_time() + (bigtime_t)lifetime * 1000000;
			}
		}
	}
	(void)addedAddress;

	return havePrefix ? B_OK : B_BAD_VALUE;
}


status_t
SLAACClient::_Run()
{
	BNetworkInterface interface(Device());
	status_t status = B_ERROR;
	for (int32 i = 0; i < 10 && !_Stopped(); i++) {
		status = _EnsureLinkLocal(interface);
		if (status == B_OK || status == B_CANCELED)
			break;
		for (int32 slice = 0; slice < 5 && !_Stopped(); slice++)
			snooze(100000);
	}
	if (status != B_OK) {
		syslog(LOG_ERR, "slaac %s: no link-local: %s\n", Device(),
			strerror(status));
		return status;
	}

	int socket = ::socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
	if (socket < 0) {
		syslog(LOG_ERR, "slaac %s: raw socket failed: %s\n", Device(),
			strerror(errno));
		return errno;
	}
	fSocket = socket;

	uint32 ifIndex = interface.Index();
	if (ifIndex == 0)
		ifIndex = if_nametoindex(Device());
	syslog(LOG_INFO, "slaac %s: listening, ifindex %" B_PRIu32 "\n", Device(),
		ifIndex);

	_BindAndJoin(socket, ifIndex);

	int32 solicits = 0;
	bigtime_t nextSolicit = 0;
	bool configured = false;

	while (!_Stopped()) {
		_ExpirePrefixes();
		bigtime_t now = system_time();
		if (now >= nextSolicit && (!configured || solicits < kMaxSolicits)) {
			_SendSolicit(socket, ifIndex);
			solicits++;
			if (solicits < kMaxSolicits)
				nextSolicit = now + kSolicitInterval;
			else
				nextSolicit = now + kRetryInterval;
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
		wait.tv_sec = 1;
		wait.tv_usec = 0;
		int ready = select(maxFd + 1, &readSet, NULL, NULL, &wait);
		if (ready < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EBADF || _Stopped())
				break;
			syslog(LOG_ERR, "slaac %s: select: %s\n", Device(),
				strerror(errno));
			break;
		}
		if (fWakeRead >= 0 && FD_ISSET(fWakeRead, &readSet)) {
			char ignored;
			while (read(fWakeRead, &ignored, 1) > 0)
				;
		}
		if (ready == 0 || _Stopped()
			|| (fWakeRead >= 0 && FD_ISSET(fWakeRead, &readSet)
				&& !FD_ISSET(socket, &readSet)))
			continue;

		uint8 packet[1280];
		sockaddr_in6 from;
		socklen_t fromLen = sizeof(from);
		ssize_t bytes = recvfrom(socket, packet, sizeof(packet), 0,
			reinterpret_cast<sockaddr*>(&from), &fromLen);
		if (bytes < 0) {
			if (errno != EAGAIN && errno != EINTR && errno != ETIMEDOUT)
				syslog(LOG_ERR, "slaac %s: recv: %s\n", Device(),
					strerror(errno));
			continue;
		}

		status = _ApplyAdvert(packet, bytes, from, ifIndex);
		if (status == B_OK) {
			configured = true;
			solicits = kMaxSolicits;
			nextSolicit = now + kRetryInterval;
		}
	}

	fSocket = -1;
	close(socket);
	return configured ? B_OK : B_TIMED_OUT;
}
