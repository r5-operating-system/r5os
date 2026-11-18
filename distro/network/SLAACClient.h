/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef SLAAC_CLIENT_H
#define SLAAC_CLIENT_H


#include "AutoconfigClient.h"

#include <NetworkInterface.h>
#include <OS.h>
#include <netinet/in.h>


class SLAACClient : public AutoconfigClient {
public:
								SLAACClient(BMessenger target,
									const char* device);
	virtual						~SLAACClient();

	virtual	status_t			Start();

private:
			struct PrefixRecord {
				in6_addr		prefix;
				in6_addr		address;
				bigtime_t		validUntil;
				uint8			length;
				bool			haveAddress;
				bool			onLink;
				bool			inUse;
			};

	static	status_t			_Thread(void* data);
			status_t			_Run();
			status_t			_EnsureLinkLocal(BNetworkInterface& interface);
			status_t			_BindAndJoin(int socket, uint32 ifIndex);
			status_t			_SendSolicit(int socket, uint32 ifIndex);
			status_t			_ApplyAdvert(const uint8* buffer, ssize_t bytes,
									const sockaddr_in6& from, uint32 ifIndex);
			status_t			_ApplyDefaultRoute(const sockaddr_in6& from,
									uint32 ifIndex);
			void				_ApplyResolver(BMessage& resolver,
									int32 nameservers);
			void				_BuildSlaacAddress(const in6_addr& prefix,
									in6_addr& out) const;
			BString				_ToString(const in6_addr& address) const;
			bool				_Stopped() const;
			void				_Wake();
			void				_ExpirePrefixes();
			void				_WithdrawPrefix(PrefixRecord& record);
			status_t			_ApplyPrefix(const in6_addr& prefix,
									uint8 length, uint8 flags, uint32 valid,
									uint32 ifIndex);
			void				_RemoveDefaultRoute(const char* why);

			thread_id			fThread;
			int					fSocket;
			int					fWakeRead;
			int					fWakeWrite;
			volatile int32		fStop;
			uint8				fMAC[6];
			bool				fHaveMAC;
			bool				fHaveDefaultRoute;
			sockaddr_in6		fDefaultGateway;
			bigtime_t			fRouterUntil;
				// when the router lifetime of fDefaultGateway ends
			PrefixRecord		fPrefixes[8];
};


#endif	// SLAAC_CLIENT_H
