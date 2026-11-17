/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * IPv4 mDNS lookup and DNS-SD browse (Haiku #1042).
 */

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "r5_mdns.h"

#define MDNS_PORT			5353
#define MDNS_ADDR			"224.0.0.251"
#define MDNS_ADDR6			"ff02::fb"
#define MDNS_TIMEOUT_MS		800
#define MDNS_MAX_ADDRS		8
#define DNS_TYPE_A			1
#define DNS_TYPE_AAAA		28
#define DNS_TYPE_PTR		12
#define DNS_TYPE_TXT		16
#define DNS_TYPE_SRV		33
#define DNS_CLASS_IN		1


static int
ends_with_local(const char* name)
{
	size_t len;
	const char* tail;

	if (name == NULL)
		return 0;
	len = strlen(name);
	if (len > 0 && name[len - 1] == '.')
		len--;
	if (len < 6)
		return 0;
	tail = name + len - 6;
	if (tail[0] != '.')
		return 0;
	return strncasecmp(tail + 1, "local", 5) == 0;
}


int
r5_mdns_name_is_local(const char* hostname)
{
	return ends_with_local(hostname);
}


static void
join_mdns4(int sock)
{
	struct ip_mreq mreq;
	int reuse = 1;

	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	memset(&mreq, 0, sizeof(mreq));
	if (inet_pton(AF_INET, MDNS_ADDR, &mreq.imr_multiaddr) != 1)
		return;
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
}


static void
join_mdns6(int sock, uint32_t scope)
{
	struct ipv6_mreq mreq;
	int reuse = 1;

	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
	memset(&mreq, 0, sizeof(mreq));
	if (inet_pton(AF_INET6, MDNS_ADDR6, &mreq.ipv6mr_multiaddr) != 1)
		return;
	mreq.ipv6mr_interface = scope;
	setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP, &mreq, sizeof(mreq));
	if (scope != 0)
		setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_IF, &scope,
			sizeof(scope));
}


static uint32_t
mdns_link_scope(void)
{
	struct ifaddrs* ifa;
	struct ifaddrs* p;
	uint32_t scope = 0;

	if (getifaddrs(&ifa) != 0)
		return 0;
	for (p = ifa; p != NULL; p = p->ifa_next) {
		struct sockaddr_in6* addr;
		if (p->ifa_addr == NULL || p->ifa_addr->sa_family != AF_INET6)
			continue;
		if ((p->ifa_flags & IFF_LOOPBACK) != 0 || (p->ifa_flags & IFF_UP) == 0)
			continue;
		addr = (struct sockaddr_in6*)p->ifa_addr;
		if (!IN6_IS_ADDR_LINKLOCAL(&addr->sin6_addr))
			continue;
		scope = if_nametoindex(p->ifa_name);
		if (scope != 0)
			break;
	}
	freeifaddrs(ifa);
	return scope;
}


static size_t
put_dns_name(uint8_t* out, size_t cap, size_t off, const char* name)
{
	char copy[256];
	char* label;
	char* rest;
	size_t n;

	n = strlen(name);
	if (n >= sizeof(copy))
		return 0;
	memcpy(copy, name, n + 1);
	if (n > 0 && copy[n - 1] == '.')
		copy[n - 1] = '\0';

	rest = copy;
	while (rest != NULL && *rest != '\0') {
		label = rest;
		rest = strchr(rest, '.');
		if (rest != NULL) {
			*rest = '\0';
			rest++;
		}
		n = strlen(label);
		if (n == 0 || n > 63 || off + n + 1 >= cap)
			return 0;
		out[off++] = (uint8_t)n;
		memcpy(out + off, label, n);
		off += n;
	}
	if (off + 1 >= cap)
		return 0;
	out[off++] = 0;
	return off;
}


static int
decode_dns_name(const uint8_t* msg, size_t len, size_t off, char* out,
	size_t cap)
{
	int jumps = 0;
	size_t dlen = 0;

	if (cap == 0)
		return -1;
	out[0] = '\0';
	while (off < len) {
		uint8_t lab = msg[off];
		if (lab == 0)
			break;
		if ((lab & 0xc0) == 0xc0) {
			if (off + 1 >= len)
				return -1;
			off = ((size_t)(lab & 0x3f) << 8) | msg[off + 1];
			if (++jumps > 16)
				return -1;
			continue;
		}
		if ((lab & 0xc0) != 0 || off + 1 + lab >= len)
			return -1;
		if (dlen != 0) {
			if (dlen + 1 >= cap)
				return -1;
			out[dlen++] = '.';
		}
		if (dlen + lab >= cap)
			return -1;
		memcpy(out + dlen, msg + off + 1, lab);
		dlen += lab;
		off += 1 + lab;
	}
	out[dlen] = '\0';
	if (dlen > 0 && out[dlen - 1] == '.')
		out[dlen - 1] = '\0';
	return 0;
}


static int
skip_dns_name(const uint8_t* msg, size_t len, size_t* off)
{
	int jumps = 0;
	size_t pos = *off;
	int consumed = 0;

	while (pos < len) {
		uint8_t lab = msg[pos];
		if (lab == 0) {
			if (!consumed)
				*off = pos + 1;
			return 0;
		}
		if ((lab & 0xc0) == 0xc0) {
			if (pos + 1 >= len)
				return -1;
			if (!consumed)
				*off = pos + 2;
			pos = ((size_t)(lab & 0x3f) << 8) | msg[pos + 1];
			if (++jumps > 16)
				return -1;
			consumed = 1;
			continue;
		}
		if ((lab & 0xc0) != 0 || pos + 1 + lab >= len)
			return -1;
		pos += 1 + lab;
		if (!consumed)
			*off = pos;
	}
	return -1;
}


static int
read_u16(const uint8_t* msg, size_t len, size_t* off, uint16_t* value)
{
	if (*off + 2 > len)
		return -1;
	*value = (uint16_t)((msg[*off] << 8) | msg[*off + 1]);
	*off += 2;
	return 0;
}


static size_t
build_query(uint8_t* out, size_t cap, const char* name, uint16_t type)
{
	size_t off = 12;

	if (cap < 12)
		return 0;
	memset(out, 0, 12);
	out[5] = 1;
	off = put_dns_name(out, cap, off, name);
	if (off == 0 || off + 4 > cap)
		return 0;
	out[off++] = (uint8_t)(type >> 8);
	out[off++] = (uint8_t)type;
	out[off++] = 0;
	out[off++] = DNS_CLASS_IN;
	return off;
}


static int
mdns_query(const char* name, uint16_t type, uint8_t* replies, size_t replyCap,
	size_t* replyLens, int maxReplies, int timeoutMs)
{
	uint8_t query[512];
	struct sockaddr_in dest;
	struct pollfd pfd;
	size_t qlen;
	int sock;
	int nfound = 0;

	qlen = build_query(query, sizeof(query), name, type);
	if (qlen == 0)
		return 0;

	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0)
		return 0;
	join_mdns4(sock);

	memset(&dest, 0, sizeof(dest));
	dest.sin_len = sizeof(dest);
	dest.sin_family = AF_INET;
	dest.sin_port = htons(MDNS_PORT);
	if (inet_pton(AF_INET, MDNS_ADDR, &dest.sin_addr) != 1) {
		close(sock);
		return 0;
	}

	if (sendto(sock, query, qlen, 0, (struct sockaddr*)&dest, sizeof(dest)) < 0) {
		close(sock);
		return 0;
	}

	int sock6 = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	if (sock6 >= 0) {
		struct sockaddr_in6 dest6;
		int only = 1;
		uint32_t scope = mdns_link_scope();
		memset(&dest6, 0, sizeof(dest6));
		dest6.sin6_len = sizeof(dest6);
		dest6.sin6_family = AF_INET6;
		dest6.sin6_port = htons(MDNS_PORT);
		dest6.sin6_scope_id = scope;
		inet_pton(AF_INET6, MDNS_ADDR6, &dest6.sin6_addr);
		setsockopt(sock6, IPPROTO_IPV6, IPV6_V6ONLY, &only, sizeof(only));
		join_mdns6(sock6, scope);
		sendto(sock6, query, qlen, 0, (struct sockaddr*)&dest6, sizeof(dest6));
	}

	pfd.fd = sock;
	pfd.events = POLLIN;
	while (nfound < maxReplies) {
		int n;
		ssize_t got;
		struct pollfd pfds[2];
		int nfds = 1;

		pfds[0] = pfd;
		if (sock6 >= 0) {
			pfds[1].fd = sock6;
			pfds[1].events = POLLIN;
			nfds = 2;
		}
		n = poll(pfds, nfds, nfound == 0 ? timeoutMs : 150);
		if (n <= 0)
			break;
		int si;
		for (si = 0; si < nfds && nfound < maxReplies; si++) {
			if ((pfds[si].revents & POLLIN) == 0)
				continue;
			got = recv(pfds[si].fd, replies + nfound * replyCap, replyCap, 0);
			if (got <= 0)
				continue;
			replyLens[nfound] = (size_t)got;
			nfound++;
		}
	}
	close(sock);
	if (sock6 >= 0)
		close(sock6);
	return nfound;
}


static int
foreach_rr(const uint8_t* msg, size_t len,
	int (*cb)(const uint8_t*, size_t, size_t, uint16_t, uint16_t,
		const uint8_t*, uint16_t, void*),
	void* cookie)
{
	size_t off = 12;
	uint16_t qd, an, ns, ar, i, total;

	if (len < 12)
		return 0;
	qd = (uint16_t)((msg[4] << 8) | msg[5]);
	an = (uint16_t)((msg[6] << 8) | msg[7]);
	ns = (uint16_t)((msg[8] << 8) | msg[9]);
	ar = (uint16_t)((msg[10] << 8) | msg[11]);

	for (i = 0; i < qd; i++) {
		uint16_t type, classValue;
		if (skip_dns_name(msg, len, &off) != 0)
			return -1;
		if (read_u16(msg, len, &off, &type) != 0
			|| read_u16(msg, len, &off, &classValue) != 0)
			return -1;
	}

	total = (uint16_t)(an + ns + ar);
	for (i = 0; i < total; i++) {
		size_t nameOff = off;
		uint16_t type, classValue, rdlength;

		if (skip_dns_name(msg, len, &off) != 0)
			return -1;
		if (read_u16(msg, len, &off, &type) != 0
			|| read_u16(msg, len, &off, &classValue) != 0)
			return -1;
		if (off + 4 > len)
			return -1;
		off += 4;
		if (read_u16(msg, len, &off, &rdlength) != 0)
			return -1;
		if (off + rdlength > len)
			return -1;
		if (cb(msg, len, nameOff, type, classValue, msg + off, rdlength,
				cookie) != 0)
			return -1;
		off += rdlength;
	}
	return 0;
}


struct a_parse_state {
	struct in_addr addrs[MDNS_MAX_ADDRS];
	int count;
};


static int
parse_a_cb(const uint8_t* msg, size_t len, size_t nameOff, uint16_t type,
	uint16_t classValue, const uint8_t* rdata, uint16_t rdlength, void* cookie)
{
	struct a_parse_state* st = (struct a_parse_state*)cookie;

	(void)msg;
	(void)len;
	(void)nameOff;
	(void)classValue;
	if (type == DNS_TYPE_A && rdlength == 4 && st->count < MDNS_MAX_ADDRS) {
		memcpy(&st->addrs[st->count], rdata, 4);
		st->count++;
	}
	return 0;
}


static int
parse_a_records(const uint8_t* msg, size_t len, struct in_addr* addrs, int max)
{
	struct a_parse_state state;
	int n;

	memset(&state, 0, sizeof(state));
	foreach_rr(msg, len, parse_a_cb, &state);
	n = state.count;
	if (n > max)
		n = max;
	if (n > 0)
		memcpy(addrs, state.addrs, (size_t)n * sizeof(struct in_addr));
	return n;
}


static struct addrinfo*
make_ai6(const struct addrinfo* pai, const struct in6_addr* addr)
{
	struct addrinfo* ai;
	struct sockaddr_in6* sin6;

	ai = (struct addrinfo*)calloc(1, sizeof(*ai) + sizeof(*sin6));
	if (ai == NULL)
		return NULL;
	sin6 = (struct sockaddr_in6*)(ai + 1);
	sin6->sin6_len = sizeof(*sin6);
	sin6->sin6_family = AF_INET6;
	sin6->sin6_addr = *addr;
	if (IN6_IS_ADDR_LINKLOCAL(addr))
		sin6->sin6_scope_id = mdns_link_scope();

	if (pai != NULL) {
		ai->ai_flags = pai->ai_flags;
		ai->ai_socktype = pai->ai_socktype;
		ai->ai_protocol = pai->ai_protocol;
	}
	ai->ai_family = AF_INET6;
	ai->ai_addrlen = sizeof(*sin6);
	ai->ai_addr = (struct sockaddr*)sin6;
	return ai;
}


struct aaaa_parse_state {
	struct in6_addr addrs[MDNS_MAX_ADDRS];
	int count;
};


static int
parse_aaaa_cb(const uint8_t* msg, size_t len, size_t nameOff, uint16_t type,
	uint16_t classValue, const uint8_t* rdata, uint16_t rdlength, void* cookie)
{
	struct aaaa_parse_state* st = (struct aaaa_parse_state*)cookie;

	(void)msg;
	(void)len;
	(void)nameOff;
	(void)classValue;
	if (type == DNS_TYPE_AAAA && rdlength == 16 && st->count < MDNS_MAX_ADDRS) {
		memcpy(&st->addrs[st->count], rdata, 16);
		st->count++;
	}
	return 0;
}


static int
parse_aaaa_records(const uint8_t* msg, size_t len, struct in6_addr* addrs,
	int max)
{
	struct aaaa_parse_state state;
	int n;

	memset(&state, 0, sizeof(state));
	foreach_rr(msg, len, parse_aaaa_cb, &state);
	n = state.count;
	if (n > max)
		n = max;
	if (n > 0)
		memcpy(addrs, state.addrs, (size_t)n * sizeof(struct in6_addr));
	return n;
}


static struct addrinfo*
make_ai(const struct addrinfo* pai, struct in_addr addr)
{
	struct addrinfo* ai;
	struct sockaddr_in* sin;

	ai = (struct addrinfo*)calloc(1, sizeof(*ai) + sizeof(*sin));
	if (ai == NULL)
		return NULL;
	sin = (struct sockaddr_in*)(ai + 1);
	sin->sin_len = sizeof(*sin);
	sin->sin_family = AF_INET;
	sin->sin_addr = addr;

	if (pai != NULL) {
		ai->ai_flags = pai->ai_flags;
		ai->ai_socktype = pai->ai_socktype;
		ai->ai_protocol = pai->ai_protocol;
	}
	ai->ai_family = AF_INET;
	ai->ai_addrlen = sizeof(*sin);
	ai->ai_addr = (struct sockaddr*)sin;
	return ai;
}


int
r5_mdns_getaddrinfo(const char* hostname, const struct addrinfo* pai,
	struct addrinfo** res)
{
	uint8_t replies[MDNS_MAX_ADDRS][1500];
	size_t lens[MDNS_MAX_ADDRS];
	struct addrinfo* head = NULL;
	struct addrinfo* tail = NULL;
	struct in_addr found[MDNS_MAX_ADDRS];
	struct in6_addr found6[MDNS_MAX_ADDRS];
	int nmsg;
	int nfound = 0;
	int nfound6 = 0;
	int i;
	int want4 = (pai == NULL || pai->ai_family == AF_UNSPEC
		|| pai->ai_family == AF_INET);
	int want6 = (pai == NULL || pai->ai_family == AF_UNSPEC
		|| pai->ai_family == AF_INET6);

	if (res == NULL)
		return EAI_FAIL;
	*res = NULL;
	if (hostname == NULL || !ends_with_local(hostname))
		return EAI_NONAME;
	if (!want4 && !want6)
		return EAI_FAMILY;

	if (want4) {
		nmsg = mdns_query(hostname, DNS_TYPE_A, (uint8_t*)replies, 1500, lens,
			MDNS_MAX_ADDRS, MDNS_TIMEOUT_MS);
		for (i = 0; i < nmsg && nfound < MDNS_MAX_ADDRS; i++) {
			nfound += parse_a_records(replies[i], lens[i], found + nfound,
				MDNS_MAX_ADDRS - nfound);
		}
	}
	if (want6) {
		nmsg = mdns_query(hostname, DNS_TYPE_AAAA, (uint8_t*)replies, 1500, lens,
			MDNS_MAX_ADDRS, MDNS_TIMEOUT_MS);
		for (i = 0; i < nmsg && nfound6 < MDNS_MAX_ADDRS; i++) {
			nfound6 += parse_aaaa_records(replies[i], lens[i], found6 + nfound6,
				MDNS_MAX_ADDRS - nfound6);
		}
	}

	for (i = 0; i < nfound; i++) {
		struct addrinfo* ai = make_ai(pai, found[i]);
		if (ai == NULL) {
			freeaddrinfo(head);
			return EAI_MEMORY;
		}
		if (head == NULL)
			head = ai;
		else
			tail->ai_next = ai;
		tail = ai;
	}
	for (i = 0; i < nfound6; i++) {
		struct addrinfo* ai = make_ai6(pai, &found6[i]);
		if (ai == NULL) {
			freeaddrinfo(head);
			return EAI_MEMORY;
		}
		if (head == NULL)
			head = ai;
		else
			tail->ai_next = ai;
		tail = ai;
	}

	if (head == NULL)
		return EAI_NODATA;
	*res = head;
	return 0;
}


struct browse_state {
	r5_mdns_service* out;
	int max;
	int count;
	const char* wantType;
};


static void
txt_get(const uint8_t* rdata, uint16_t rdlength, const char* key, char* dst,
	size_t cap)
{
	size_t off = 0;
	size_t keyLen = strlen(key);

	dst[0] = '\0';
	while (off < rdlength) {
		uint8_t n = rdata[off++];
		if (n == 0 || off + n > rdlength)
			break;
		if (n > keyLen && strncasecmp((const char*)rdata + off, key, keyLen) == 0
			&& rdata[off + keyLen] == '=') {
			size_t vlen = n - keyLen - 1;
			if (vlen >= cap)
				vlen = cap - 1;
			memcpy(dst, rdata + off + keyLen + 1, vlen);
			dst[vlen] = '\0';
			return;
		}
		off += n;
	}
}


static void
instance_from_ptr(const char* fqdn, char* name, size_t nameCap, char* type,
	size_t typeCap)
{
	const char* dot = strchr(fqdn, '.');
	if (dot == NULL) {
		strlcpy(name, fqdn, nameCap);
		strlcpy(type, "", typeCap);
		return;
	}
	size_t n = (size_t)(dot - fqdn);
	if (n >= nameCap)
		n = nameCap - 1;
	memcpy(name, fqdn, n);
	name[n] = '\0';
	strlcpy(type, dot + 1, typeCap);
}


static void
strip_local_suffix(char* name)
{
	size_t n;

	if (name == NULL)
		return;
	n = strlen(name);
	while (n > 0 && name[n - 1] == '.')
		name[--n] = '\0';
	if (n >= 6 && strcasecmp(name + n - 6, ".local") == 0)
		name[n - 6] = '\0';
}


static int
service_type_ok(const char* got, const char* want)
{
	char a[256];
	char b[256];

	if (want == NULL || want[0] == '\0')
		return 1;
	if (got == NULL || got[0] == '\0')
		return 0;
	strlcpy(a, got, sizeof(a));
	strlcpy(b, want, sizeof(b));
	strip_local_suffix(a);
	strip_local_suffix(b);
	return strcasecmp(a, b) == 0;
}


static r5_mdns_service*
find_or_add(struct browse_state* st, const char* instance)
{
	int i;
	char name[64];
	char type[64];

	instance_from_ptr(instance, name, sizeof(name), type, sizeof(type));
	for (i = 0; i < st->count; i++) {
		if (strcasecmp(st->out[i].name, name) == 0)
			return &st->out[i];
	}
	if (st->count >= st->max)
		return NULL;
	memset(&st->out[st->count], 0, sizeof(st->out[0]));
	strlcpy(st->out[st->count].name, name, sizeof(st->out[0].name));
	strlcpy(st->out[st->count].type, type, sizeof(st->out[0].type));
	return &st->out[st->count++];
}


static int
browse_cb(const uint8_t* msg, size_t len, size_t nameOff, uint16_t type,
	uint16_t classValue, const uint8_t* rdata, uint16_t rdlength, void* cookie)
{
	struct browse_state* st = (struct browse_state*)cookie;
	char owner[256];
	r5_mdns_service* svc;

	(void)classValue;
	if (decode_dns_name(msg, len, nameOff, owner, sizeof(owner)) != 0)
		return 0;

	if (type == DNS_TYPE_PTR) {
		char target[256];
		if (!service_type_ok(owner, st->wantType))
			return 0;
		if (decode_dns_name(msg, len, (size_t)(rdata - msg), target,
				sizeof(target)) != 0)
			return 0;
		find_or_add(st, target);
		return 0;
	}

	if (type == DNS_TYPE_SRV && rdlength >= 6) {
		char name[64];
		char svcType[64];
		char target[256];
		instance_from_ptr(owner, name, sizeof(name), svcType, sizeof(svcType));
		(void)name;
		if (!service_type_ok(svcType, st->wantType))
			return 0;
		svc = find_or_add(st, owner);
		if (svc == NULL)
			return 0;
		svc->port = (uint16_t)((rdata[4] << 8) | rdata[5]);
		if (decode_dns_name(msg, len, (size_t)(rdata + 6 - msg), target,
				sizeof(target)) == 0)
			strlcpy(svc->host, target, sizeof(svc->host));
		return 0;
	}

	if (type == DNS_TYPE_TXT) {
		char name[64];
		char svcType[64];
		char rp[128];
		instance_from_ptr(owner, name, sizeof(name), svcType, sizeof(svcType));
		(void)name;
		if (!service_type_ok(svcType, st->wantType))
			return 0;
		svc = find_or_add(st, owner);
		if (svc == NULL)
			return 0;
		if (rdlength < sizeof(svc->txt)) {
			memcpy(svc->txt, rdata, rdlength);
			svc->txt[rdlength] = '\0';
		}
		txt_get(rdata, rdlength, "pdl", svc->pdl, sizeof(svc->pdl));
		txt_get(rdata, rdlength, "rp", rp, sizeof(rp));
		if (rp[0] == '\0')
			strlcpy(rp, "ipp/print", sizeof(rp));
		if (svc->host[0] != '\0' && svc->port != 0) {
			const char* scheme = strstr(svc->type, "_ipps.") != NULL
				? "ipps" : "ipp";
			snprintf(svc->uri, sizeof(svc->uri), "%s://%s:%u/%s", scheme,
				svc->host, svc->port, rp[0] == '/' ? rp + 1 : rp);
		}
		return 0;
	}

	if (type == DNS_TYPE_A && rdlength == 4) {
		int i;
		for (i = 0; i < st->count; i++) {
			if (strcasecmp(st->out[i].host, owner) == 0)
				memcpy(&st->out[i].addr, rdata, 4);
		}
	}
	return 0;
}


static void
finish_uris(r5_mdns_service* svcs, int count)
{
	int i;
	for (i = 0; i < count; i++) {
		char rp[128] = "ipp/print";
		if (svcs[i].uri[0] != '\0')
			continue;
		if (svcs[i].host[0] == '\0' || svcs[i].port == 0)
			continue;
		snprintf(svcs[i].uri, sizeof(svcs[i].uri), "ipp://%s:%u/%s",
			svcs[i].host, svcs[i].port, rp);
	}
}


int
r5_mdns_browse(const char* serviceType, r5_mdns_service* out, int max,
	int timeoutMs)
{
	char qname[256];
	uint8_t replies[8][1500];
	size_t lens[8];
	struct browse_state st;
	int nmsg;
	int i;

	if (serviceType == NULL || out == NULL || max <= 0)
		return 0;

	if (ends_with_local(serviceType))
		strlcpy(qname, serviceType, sizeof(qname));
	else
		snprintf(qname, sizeof(qname), "%s.local", serviceType);

	st.out = out;
	st.max = max;
	st.count = 0;
	st.wantType = serviceType;

	nmsg = mdns_query(qname, DNS_TYPE_PTR, (uint8_t*)replies, 1500, lens, 8,
		timeoutMs > 0 ? timeoutMs : 1200);
	for (i = 0; i < nmsg; i++)
		foreach_rr(replies[i], lens[i], browse_cb, &st);

	for (i = 0; i < st.count; i++) {
		if (st.out[i].host[0] == '\0' || st.out[i].port == 0) {
			char inst[256];
			uint8_t one[1500];
			size_t oneLen;
			int n;

			snprintf(inst, sizeof(inst), "%s.%s", st.out[i].name,
				st.out[i].type[0] ? st.out[i].type : qname);
			n = mdns_query(inst, DNS_TYPE_SRV, one, 1500, &oneLen, 1, 400);
			if (n > 0)
				foreach_rr(one, oneLen, browse_cb, &st);
		}
		if (st.out[i].host[0] != '\0' && st.out[i].addr.s_addr == 0) {
			uint8_t one[1500];
			size_t oneLen;
			int n = mdns_query(st.out[i].host, DNS_TYPE_A, one, 1500, &oneLen,
				1, 400);
			if (n > 0)
				foreach_rr(one, oneLen, browse_cb, &st);
		}
	}

	finish_uris(st.out, st.count);
	return st.count;
}
