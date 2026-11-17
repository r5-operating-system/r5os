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
#include "SDPParse.h"
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
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// First every record of the public browse group, all attributes, in
	// pieces of at most 255 attribute bytes (Core 5.4, Vol 3, Part B,
	// 4.7). The WH-1000XM3 (stick 51) gave no Hands-Free channel from this
	// browse: when a headset's browse brings none, a search for the
	// Hands-Free service class itself follows (HFP 1.8, 5.3), with only the
	// attributes HFP needs.
	static const uint8 kBrowse[] = {
		0x35, 0x03, 0x19, 0x10, 0x02,			// public browse group
		0x00, 0xff,								// at most 255 bytes a piece
		0x35, 0x05, 0x0a, 0x00, 0x00, 0xff, 0xff	// attributes 0 to 0xffff
	};
	BMallocIO lists;
	const bool browsed = _SearchAttributes(kBrowse, sizeof(kBrowse), &lists)
		== B_OK;

	// The records of this connection replace the earlier ones; they piled
	// up and old ones were offered to the profiles again.
	fRemoteDevice->services.MakeEmpty();
	std::vector<uint16> classes;
	if (browsed)
		_ParseAttrLists(&lists, &classes);
	_LogRecords(browsed ? "sdp:" : "sdp: browse not read,", classes);

	bool searched = false;
	if (_MayOfferHandsFree()
		&& BluetoothServer::R5RFCOMMChannel(fRemoteDevice->services,
			r5sdpparse::kServiceHandsFree) == 0) {
		uint8 parameters[32];
		static const uint16 kAttributes[] = { 0x0001, 0x0004, 0x0009,
			0x0311 };
		size_t length = r5sdpparse::BuildSearchAttributes(
			r5sdpparse::kServiceHandsFree, kAttributes,
			sizeof(kAttributes) / sizeof(kAttributes[0]), 255, parameters,
			sizeof(parameters));
		BMallocIO handsFree;
		searched = length > 0
			&& _SearchAttributes(parameters, length, &handsFree) == B_OK;
		std::vector<uint16> found;
		if (searched)
			_ParseAttrLists(&handsFree, &found);
		_LogRecords(searched ? "sdp: hands-free search,"
			: "sdp: hands-free search not read,", found);
	}
	return browsed || searched ? B_OK : B_ERROR;
	// SPDX-SnippetEnd
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
/*!	A Service Search Attribute transaction (Core 5.4, Vol 3, Part B, 4.7):
	the request with these parameters, again with each continuation state,
	until the device sends none. The attribute lists of all pieces go to
	lists, as one answer.
*/
status_t
SDPClient::_SearchAttributes(const uint8* parameters, size_t length,
	BMallocIO* lists)
{
	// A device that never ends its continuation is not followed for ever.
	static const int kMaxPieces = 64;
	uint8 continuation[16];
	size_t continuationLength = 0;
	for (int piece = 0; piece < kMaxPieces; piece++) {
		const uint16 transaction = transactionID++;
		const size_t parameterLength = length + 1 + continuationLength;
		uint8 header[5] = { SDP_PDU_SERVICE_SEARCH_ATTRIBUTE_REQUEST,
			(uint8)(transaction >> 8), (uint8)transaction,
			(uint8)(parameterLength >> 8), (uint8)parameterLength };
		uint8 count = (uint8)continuationLength;
		BMallocIO request;
		request.WriteExactly(header, sizeof(header));
		request.WriteExactly(parameters, length);
		request.WriteExactly(&count, 1);
		if (continuationLength > 0)
			request.WriteExactly(continuation, continuationLength);

		BMallocIO reply;
		if (_IssueRequest(request.Buffer(), request.BufferLength(), &reply)
				!= B_OK) {
			TRACE_BT("SDP: Issuing Request failed\n");
			return B_ERROR;
		}

		const uint8* pieceLists;
		size_t pieceLength;
		const uint8* state;
		size_t stateLength;
		if (!r5sdpparse::ReadAnswerPiece((const uint8*)reply.Buffer(),
				reply.BufferLength(), &pieceLists, &pieceLength, &state,
				&stateLength)) {
			TRACE_BT("SDP: not a sound attribute response\n");
			return B_ERROR;
		}
		if (pieceLength > 0)
			lists->WriteExactly(pieceLists, pieceLength);
		memcpy(continuation, state, stateLength);
		continuationLength = stateLength;
		if (continuationLength == 0)
			return B_OK;
	}
	TRACE_BT("SDP: Too large response\n");
	return B_ERROR;
}


/*!	Hands-Free comes from a headset or another audio device (major device
	class 4), or from a device whose class is not known here.
*/
bool
SDPClient::_MayOfferHandsFree() const
{
	const uint8* deviceClass = fRemoteDevice->classOfDevice;
	if (deviceClass[0] == 0 && deviceClass[1] == 0 && deviceClass[2] == 0)
		return true;
	return (deviceClass[1] & 0x1f) == 4;
}


//!	One line per answer, without addresses: the records' service classes.
void
SDPClient::_LogRecords(const char* what, const std::vector<uint16>& classes)
{
	BString line;
	line.SetToFormat("bluetooth_server: %s %" B_PRIuSIZE " records:", what,
		classes.size());
	for (size_t i = 0; i < classes.size()
			&& i < r5sdpparse::kMaxLoggedClasses; i++) {
		BString item;
		item.SetToFormat(" 0x%04x", classes[i]);
		line << item;
	}
	if (classes.size() > r5sdpparse::kMaxLoggedClasses)
		line << " ...";
	syslog(LOG_INFO, "%s", line.String());
}
// SPDX-SnippetEnd


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


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
namespace {


/*!	One attribute into the record message, stored as SDPDataIO::ParseNext()
	stored it, so the profiles read it as before: numbers as numbers, UUIDs
	as sdp_uuid, strings (and now URLs) as strings, a sequence as its
	contents. 128-bit integers and Nil are left out, as before.
*/
void
add_attribute(BMessage* record, uint16 id, const r5sdpparse::Element& value)
{
	BString label;
	label.SetToFormat("0x%04X", id);
	uint64 number = 0;
	for (size_t i = 0; i < value.length && i < 8; i++)
		number = (number << 8) | value.value[i];

	switch (value.type) {
		case r5sdpparse::kTypeBool:
			record->AddBool(label, value.value[0] != 0);
			break;

		case r5sdpparse::kTypeUnsigned:
			if (value.length == 1)
				record->AddUInt8(label, (uint8)number);
			else if (value.length == 2)
				record->AddUInt16(label, (uint16)number);
			else if (value.length == 4)
				record->AddUInt32(label, (uint32)number);
			else if (value.length == 8)
				record->AddUInt64(label, number);
			break;

		case r5sdpparse::kTypeSigned:
			if (value.length == 1)
				record->AddInt8(label, (int8)number);
			else if (value.length == 2)
				record->AddInt16(label, (int16)number);
			else if (value.length == 4)
				record->AddInt32(label, (int32)number);
			else if (value.length == 8)
				record->AddInt64(label, (int64)number);
			break;

		case r5sdpparse::kTypeUUID:
		{
			BMallocIO element;
			element.WriteExactly(value.start, value.total);
			element.Seek(0, SEEK_SET);
			SDPDataIO reader(&element);
			sdp_uuid uuid = reader.ReadUUID();
			if (reader.ReadingStatus() == B_OK)
				record->AddData(label, B_RAW_TYPE, &uuid, sizeof(uuid));
			break;
		}

		case r5sdpparse::kTypeString:
		case r5sdpparse::kTypeURL:
			record->AddString(label,
				BString((const char*)value.value, value.length));
			break;

		case r5sdpparse::kTypeSequence:
			if (value.length > 0)
				record->AddData(label, B_RAW_TYPE, value.value, value.length);
			break;

		case r5sdpparse::kTypeAlternative:
		{
			// A choice of protocol stacks or other sequences: the first one
			// is kept, as a sequence would be.
			size_t offset = 0;
			r5sdpparse::Element first;
			if (r5sdpparse::NextElement(value.value, value.length, &offset,
					&first)
				&& first.type == r5sdpparse::kTypeSequence
				&& first.length > 0)
				record->AddData(label, B_RAW_TYPE, first.value, first.length);
			break;
		}
	}
}


struct RecordCollector {
	BMessage*				services;
	std::vector<uint16>*	classes;

	void operator()(const uint8* value, size_t length)
	{
		BMessage record;
		uint16 serviceClass = 0;
		r5sdpparse::ForEachAttribute(value, length,
			[&](uint16 id, const r5sdpparse::Element& element) {
				if (id == 0x0001
					&& element.type == r5sdpparse::kTypeSequence) {
					serviceClass = r5sdpparse::FirstServiceClass(
						element.value, element.length);
				}
				add_attribute(&record, id, element);
			});
		services->AddMessage("service", &record);
		if (classes != NULL)
			classes->push_back(serviceClass);
	}
};


} // namespace


/*!	The records of one answer, added to fRemoteDevice->services. It used
	SDPDataIO for the whole answer: a Nil or URL value, a 128-bit integer or
	any other element it did not step over put it out of step, and that
	record and every record after it were lost. Each element is stepped
	over by its own length now (SDPParse.h), and classes gets each record's
	first service class (0 when it has none as a 16-bit UUID).
*/
void
SDPClient::_ParseAttrLists(BMallocIO* attrListsDataIO,
	std::vector<uint16>* classes)
{
	RecordCollector collect = { &fRemoteDevice->services, classes };
	r5sdpparse::ForEachRecord((const uint8*)attrListsDataIO->Buffer(),
		attrListsDataIO->BufferLength(), collect);
	TRACE_BT("SDP: Finished parsing\n");
}
// SPDX-SnippetEnd


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

	// Step 8 (docs/GAME-CONTROLLERS.md): the vendor and product ID of the
	// Device ID record (PnP Information, attributes 0x0201 to 0x0203), so a
	// game controller gets its standard layout, and the device's name.
	// Only a USB-IF vendor ID (VendorIDSource 0x0205 = 2) is the one the
	// mapping database knows; a Bluetooth SIG company ID is not.
	uint16 vendorID = 0;
	uint16 productID = 0;
	uint16 version = 0;
	BMessage record;
	for (int32 i = 0; fRemoteDevice->services.FindMessage("service", i,
			&record) == B_OK; i++) {
		uint16 source = 0;
		if (record.FindUInt16("0x0205", &source) != B_OK || source != 2
			|| record.FindUInt16("0x0201", &vendorID) != B_OK
			|| record.FindUInt16("0x0202", &productID) != B_OK) {
			vendorID = productID = 0;
			continue;
		}
		if (record.FindUInt16("0x0203", &version) != B_OK)
			version = 0;
		break;
	}
	const char* name = fRemoteDevice->friendly_name.String();
	if (vendorID != 0 || name[0] != '\0') {
		syslog(LOG_INFO, "bluetooth_hid: device id %04x:%04x", vendorID,
			productID);
		host->SetDeviceInfo(fRemoteDevice->bdaddr, vendorID, productID,
			version, name);
	}

	host->DeviceReady(fRemoteDevice->bdaddr, descriptor, length);
	return B_OK;
	// SPDX-SnippetEnd
}
