/*
 * Copyright 2007-2009 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 * Copyright 2008 Mika Lindqvist, monni1995_at_gmail.com
 * All rights reserved. Distributed under the terms of the MIT License.
 */

#include <stdio.h>
#include <sys/socket.h>

#include <String.h>

#include <bluetooth/L2CAP/btL2CAP.h>

#include "Debug.h"
#include "SDPServer.h"
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <sys/time.h>

#include "SDPRecords.h"
// SPDX-SnippetEnd


SDPServer::SDPServer()
	:
	fThreadID(-1),
	fServerSocket(-1),
	fIsShuttingDown(false)
{
}


SDPServer::~SDPServer()
{
	Stop();
}


status_t
SDPServer::Start()
{
	if (fThreadID > 0) {
		// Already running
		return B_OK;
	}

	fIsShuttingDown = false;

	// Spawn the SDP thread
	fThreadID = spawn_thread(_ListenThread, "SDP server thread", B_NORMAL_PRIORITY, this);
	if (fThreadID < 0) {
		TRACE_BT("SDP: Failed launching the SDP server thread\n");
		return fThreadID;
	}

	return resume_thread(fThreadID);
}


void
SDPServer::Stop()
{
	if (fThreadID > 0) {
		fIsShuttingDown = true;

		// Close the socket to unblock the accept() call
		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		// 0 is a valid descriptor.
		if (fServerSocket >= 0) {
		// SPDX-SnippetEnd
			close(fServerSocket);
			fServerSocket = -1;
		}

		status_t threadReturnStatus;
		wait_for_thread(fThreadID, &threadReturnStatus);
		TRACE_BT("SDP: Server thread exited with: %s\n", strerror(threadReturnStatus));

		fThreadID = -1;
	}
}


int32
SDPServer::_ListenThread(void* data)
{
	SDPServer* server = (SDPServer*)data;
	return server->_Run();
}


int32
SDPServer::_Run()
{
	// Set up the SDP socket
	struct sockaddr_l2cap loc_addr = {0};
	status_t status;
	char buffer[512] = "";

	TRACE_BT("SDP: SDP server thread up...\n");

	fServerSocket = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BLUETOOTH_PROTO_L2CAP);

	if (fServerSocket < 0) {
		TRACE_BT("SDP: Could not create server socket ...\n");
		return B_ERROR;
	}

	// bind socket to port 0x1001 of the first available
	// bluetooth adapter
	loc_addr.l2cap_family = AF_BLUETOOTH;
	loc_addr.l2cap_bdaddr = BDADDR_ANY;
	loc_addr.l2cap_psm = B_HOST_TO_LENDIAN_INT16(1);
	loc_addr.l2cap_len = sizeof(struct sockaddr_l2cap);

	status = bind(fServerSocket, (struct sockaddr*)&loc_addr, sizeof(struct sockaddr_l2cap));

	if (status < 0) {
		TRACE_BT("SDP: Could not bind server socket (%s)...\n", strerror(status));
		close(fServerSocket);
		fServerSocket = -1;
		return status;
	}

	// Listen for up to 10 connections
	status = listen(fServerSocket, 10);

	if (status != B_OK) {
		TRACE_BT("SDP: Could not listen server socket (%s)...\n", strerror(status));
		close(fServerSocket);
		fServerSocket = -1;
		return status;
	}

	while (!fIsShuttingDown) {

		TRACE_BT("SDP: Waiting connection for socket...\n");

		uint len = sizeof(struct sockaddr_l2cap);
		int client = accept(fServerSocket, (struct sockaddr*)&loc_addr, &len);

		// Check if we woke up because of shutdown
		if (fIsShuttingDown) {
			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			if (client >= 0)
				close(client);
			// SPDX-SnippetEnd
			break;
		}

		if (client < 0) {
			TRACE_BT("SDP: Error accepting connection\n");
			continue;
		}

		TRACE_BT("SDP: Incoming connection... %d\n", client);

		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		// Every request is answered (SDPRecords.h): the A2DP source and
		// AVRCP records, an empty list for anything else, an error for a
		// broken request. The old loop read 29 bytes at a time and never
		// answered, so a headset that looks first waited until its timeout
		// (docs/BLUETOOTH.md, step 5). The other side closes the channel;
		// a silent one is dropped after 30 s.
		struct timeval timeout = { 30, 0 };
		setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout,
			sizeof(timeout));
		ssize_t receivedSize;
		uint8 request[672];	// the default L2CAP MTU
		uint8 answer[672];
		while (!fIsShuttingDown) {
			receivedSize = recv(client, request, sizeof(request), 0);
			if (receivedSize <= 0)
				break;
			size_t length = r5sdp::HandleRequest(request, receivedSize,
				answer, sizeof(answer));
			if (length > 0 && send(client, answer, length, 0) < 0)
				break;
		}
		(void)buffer;
		// SPDX-SnippetEnd

		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		// Every accepted connection used to leak its descriptor.
		close(client);
		// SPDX-SnippetEnd

		snooze(50000);
		TRACE_BT("SDP: Waiting for next connection...\n");
	}

	return B_NO_ERROR;
}
