#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include <String.h>

#include <bluetooth/l2cap.h>
#include <sys/time.h>

#include "Debug.h"
#include "SDPClient.h"
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include "BluetoothServer.h"
#include "HIDHost.h"
#include <syslog.h>
// SPDX-SnippetEnd


SDPClient::SDPClient(ServerRemoteDevice* rd)
	:
	fClientSocket(-1),
	fRemoteDevice(NULL),
	transactionID(1)
{
	fRemoteDevice = rd;

	fSockAddrL2cap.l2cap_family = AF_BLUETOOTH;
	fSockAddrL2cap.l2cap_bdaddr = fRemoteDevice->bdaddr;
	fSockAddrL2cap.l2cap_psm = L2CAP_PSM_SDP;
	fSockAddrL2cap.l2cap_len = sizeof(sockaddr_l2cap);
}


SDPClient::~SDPClient()
{
	Stop();
}


status_t
SDPClient::Start()
{
	TRACE_BT("SDP: Client Starting...\n");
	status_t status;

	fClientSocket = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BLUETOOTH_PROTO_L2CAP);

	status = connect(fClientSocket, (sockaddr*)&fSockAddrL2cap, sizeof(sockaddr_l2cap));
	if (status < 0) {
		TRACE_BT("SDP: Could not connect client socket (%s)...\n", strerror(status));
		Stop();
		return status;
	}

	int sockError = 0;
	socklen_t errorLength = sizeof(sockError);
	status = getsockopt(fClientSocket, SOL_SOCKET, SO_ERROR, &sockError, &errorLength);
	if (status < 0)
		TRACE_BT("SDP: Error occurred\n");
	else if (sockError == 0)
		TRACE_BT("SDP: Socket connected.\n");
	else
		TRACE_BT("SDP: Socket Not connected.\n");

	// set receive timeout
	timeval timeout;
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;
	status = setsockopt(fClientSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

	if (status < 0) {
		TRACE_BT("SDP: Client socket couldn't set receive timeout(%s)...\n", strerror(status));
		Stop();
		return status;
	}

	return status;
}


void
SDPClient::Stop()
{
	if (fClientSocket > 0) {
		close(fClientSocket);
		fClientSocket = -1;
	}
}


status_t
SDPClient::RequestServiceRecords()
{
	BMallocIO rawReqDataIO;
	SDPDataIO rawReq(&rawReqDataIO);

	// Browse Group List UUID returns all services
	rawReq.AddDataSeq(3);
	rawReq.AddUUID16(SDP_SERVICE_CLASS_PUBLIC_BROWSE_GROUP);

	// no response is larger than 400
	uint16 maxAttrByteCount = B_HOST_TO_BENDIAN_INT16(255);
	rawReqDataIO.WriteExactly(&maxAttrByteCount, 2);

	// getting all attributes start_id = 0x0000, end_id = 0xFFFF
	uint32 attrRange = 0x0000FFFF;
	rawReq.AddDataSeq(5);
	rawReq.AddUInt32(attrRange);

	char contStateData[17];
	uint8 contStateNumBytes = 0;
	sdp_pdu pduHeader;
	pduHeader.pdu_id = SDP_PDU_SERVICE_SEARCH_ATTRIBUTE_REQUEST;

	BMallocIO fullAttrLists;
	while (true) {
		pduHeader.transaction_id = B_HOST_TO_BENDIAN_INT16(transactionID++);
		pduHeader.param_len = B_HOST_TO_BENDIAN_INT16(rawReqDataIO.BufferLength()
			+ contStateNumBytes + 1);

		BMallocIO request;
		request.WriteExactly(&pduHeader, sizeof(sdp_pdu));
		request.WriteExactly(rawReqDataIO.Buffer(), rawReqDataIO.BufferLength());
		request.WriteExactly(&contStateNumBytes, 1);
		request.WriteExactly(contStateData, contStateNumBytes);

		request.Seek(0, SEEK_SET);
		printf("SDP: Request=\n");
		for (size_t i = 0; i < request.BufferLength(); i++) {
			uint8 byte = 0;
			request.ReadExactly(&byte, sizeof(uint8));
			printf("%02X ", byte);
		}
		printf("\n");

		BMallocIO reply;
		if (_IssueRequest(request.Buffer(), request.BufferLength(), &reply) != B_OK) {
			TRACE_BT("SDP: Issuing Request failed\n");
			return B_ERROR;
		}

		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		// Every length comes from the remote device: checked against what
		// arrived. An SDP Error Response is not parsed as records, and the
		// continuation state is at most 16 bytes (Core 5.4, Vol 3, Part B,
		// 4.3).
		const size_t received = reply.BufferLength();
		const uint8* bytes = (const uint8*)reply.Buffer();
		if (received < sizeof(sdp_pdu) + 2 + 1
			|| bytes[0] != SDP_PDU_SERVICE_SEARCH_ATTRIBUTE_RESPONSE) {
			TRACE_BT("SDP: not an attribute response\n");
			return B_ERROR;
		}
		size_t position = sizeof(sdp_pdu);
		uint16 attrListsSize = (uint16)((bytes[position] << 8)
			| bytes[position + 1]);
		position += 2;
		if (attrListsSize > received - position - 1) {
			TRACE_BT("SDP: attribute lists longer than the reply\n");
			return B_ERROR;
		}
		fullAttrLists.WriteExactly(bytes + position, attrListsSize);
		position += attrListsSize;

		contStateNumBytes = bytes[position++];
		if (contStateNumBytes > 16
			|| contStateNumBytes > received - position) {
			TRACE_BT("SDP: bad continuation state\n");
			return B_ERROR;
		}
		memcpy(contStateData, bytes + position, contStateNumBytes);
		// SPDX-SnippetEnd

		if (contStateNumBytes == 0)
			break;
	}

	if (contStateNumBytes) {
		TRACE_BT("SDP: Too large response\n");
		return B_ERROR;
	}

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// The records of this connection replace the earlier ones; they piled
	// up and old ones were offered to the profiles again.
	fRemoteDevice->services.MakeEmpty();
	// SPDX-SnippetEnd
	_ParseAttrLists(&fullAttrLists);
	return B_OK;
}


status_t
SDPClient::NotifyProfiles()
{
	BMessage attrListMsg;
	for (int i = 0; fRemoteDevice->services.FindMessage("service", i, &attrListMsg) == B_OK; i++) {
		void* data;
		ssize_t size;
		// 0x0001 is the attribute list for ServiceClassIDList
        if (attrListMsg.FindData("0x0001", B_RAW_TYPE, (const void**)&data, &size) != B_OK)
			continue;

		BMallocIO attrListDataIO;
		attrListDataIO.Write(data, size);
		attrListDataIO.Seek(0, SEEK_SET);
		SDPDataIO attrList(&attrListDataIO);

		sdp_uuid uuid;
		while (true) {
			uuid = attrList.ReadUUID();
			if (attrList.ReadingStatus() != B_OK)
				break;

			uint32 service_uuid = 0;
			if (uuid.type == SDP_UUID_16)
				service_uuid = GET_UUID16(uuid);
			else if (uuid.type == SDP_UUID_32)
				service_uuid = GET_UUID32(uuid);
			else
				// TODO: We should deal with 128-bit UUIDs if some services require it
				continue;

			switch (service_uuid) {
				case SDP_SERVICE_CLASS_HUMAN_INTERFACE_DEVICE:
					return _NotifyHIDProfile(&attrListMsg);

				// more services shall be added here
			}
		}
	}
	return B_ERROR;
}


void
SDPClient::_ParseAttrLists(BMallocIO* attrListsDataIO)
{
	attrListsDataIO->Seek(0, SEEK_SET);
	TRACE_BT("SDP: Parsing the AttrLists... Num Bytes=%" B_PRIdSSIZE "\n",
		attrListsDataIO->BufferLength());

	SDPDataIO attrLists(attrListsDataIO);
	attrLists.ReadSeq();

	size_t attrListSize = 0;
	while ((attrListSize = attrLists.ReadSeq())) {

		off_t endPos = attrListsDataIO->Position() + attrListSize;
		BMessage attrListMsg;
		while (attrListsDataIO->Position() < endPos) {
			uint16 attrID = attrLists.ReadNumber<uint16>();

			// attribute id's strings are formated in hex to match the bluetooth specification
			// section 5 in assigned numbers specs:
			// https://www.bluetooth.com/specifications/assigned-numbers/
			BString attrIdStr;
			attrIdStr.SetToFormat("0x%04X", attrID);

			attrLists.ParseNext(&attrListMsg, attrIdStr);
		}
		if (attrLists.ReadingStatus() == B_OK)
			fRemoteDevice->services.AddMessage("service", &attrListMsg);
	}
	TRACE_BT("SDP: Finished parsing\n");
}


status_t
SDPClient::_IssueRequest(const void* request, int32 size, BMallocIO* reply)
{
	status_t status = B_ERROR;

	if (fClientSocket < 0)
		return status;

	status = send(fClientSocket, request, size, 0);

	if (status < 0)
		return B_ERROR;

	TRACE_BT("SDP: The request was sent\n");

	char buffer[400];
	ssize_t numBytes = recv(fClientSocket, &buffer, sizeof(buffer), 0);

	if (numBytes < 0)
		return B_ERROR;

	TRACE_BT("SDP: Received the response: num bytes=%" B_PRIdSSIZE "\n", numBytes);

	reply->SetSize(0);
	reply->Write(buffer, numBytes);

	return B_OK;
}


status_t
SDPClient::_NotifyHIDProfile(BMessage* attrList)
{
	TRACE_BT("Notifying HID Profile...\n");
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// Step 4: the report descriptor (HIDDescriptorList, attribute 0x0206)
	// goes to the HID host, which opens the HID channels.
	const uint8* list;
	ssize_t listSize;
	if (attrList->FindData("0x0206", B_RAW_TYPE, (const void**)&list,
			&listSize) != B_OK || listSize <= 0) {
		syslog(LOG_NOTICE, "bluetooth_hid: HID service without a report "
			"descriptor");
		return B_NOT_SUPPORTED;
	}

	const uint8* descriptor;
	size_t length;
	if (!HIDHost::FindReportDescriptor(list, listSize, &descriptor, &length)) {
		syslog(LOG_NOTICE, "bluetooth_hid: report descriptor not readable");
		return B_BAD_DATA;
	}
	syslog(LOG_INFO, "bluetooth_hid: report descriptor, %" B_PRIuSIZE
		" bytes", length);

	HIDHost* host = ((BluetoothServer*)be_app)->GetHIDHost();
	if (host == NULL)
		return B_NO_INIT;
	host->DeviceReady(fRemoteDevice->bdaddr, descriptor, length);
	return B_OK;
	// SPDX-SnippetEnd
}
