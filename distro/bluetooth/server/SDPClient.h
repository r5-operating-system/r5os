/*
 * Copyright 2026, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 * 		mohammedrattia <mohammedrattia@gmail.com>
 */
#ifndef _SDP_CLIENT_H_
#define _SDP_CLIENT_H_

#include <bluetooth/sdp.h>
#include <bluetooth/SDPBuffer.h>
#include <Message.h>
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <vector>
// SPDX-SnippetEnd

#include "LocalDeviceImpl.h"


class SDPClient {
public:

						SDPClient(ServerRemoteDevice* rd);
						~SDPClient();

	status_t			Start();
	void				Stop();


	status_t			RequestServiceRecords();
	status_t			NotifyProfiles();

private:

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	void				_ParseAttrLists(BMallocIO* attrListsDataIO,
							std::vector<uint16>* classes);
	status_t			_SearchAttributes(const uint8* parameters,
							size_t length, BMallocIO* lists);
	bool				_MayOfferHandsFree() const;
	void				_LogRecords(const char* what,
							const std::vector<uint16>& classes);
	// SPDX-SnippetEnd
	status_t			_IssueRequest(const void *request, int32 size, BMallocIO* reply);

	status_t			_NotifyHIDProfile(BMessage* attrList);


	int					fClientSocket;
	ServerRemoteDevice* fRemoteDevice;
	sockaddr_l2cap		fSockAddrL2cap;

	uint16 transactionID;
};


#endif //_SDP_CLIENT_H_
