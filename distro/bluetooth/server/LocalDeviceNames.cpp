/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Device names for the Bluetooth page. A Classic device's name comes in
 * its Extended Inquiry Response; a device that sends none (or answers an
 * inquiry in standard or RSSI mode), or connects without a known name, is
 * asked for it with Remote Name Request after the scan, one at a time,
 * with a timeout each (Core 5.4, Vol 4, Part E, 7.1.19, 7.1.20, 7.7.7).
 * An answer is stored in the device's record (and in the saved list for a
 * paired device), kept for the next scan, and sent to the page that
 * scanned. The kit's own Remote Name Request (RemoteDevice::
 * GetFriendlyName()) still gets its answer. A name is logged only in the
 * scan lines, with the address masked (scan_log.h).
 */


#include "LocalDeviceImpl.h"

#include <string.h>
#include <syslog.h>

#include <OS.h>

#include <bluetooth/bdaddrUtils.h>
#include <bluetooth/LinkKeyUtils.h>
#include <bluetooth/HCI/btHCI.h>
#include <bluetooth/HCI/btHCI_command.h>
#include <bluetooth/HCI/btHCI_event.h>
#include <bluetooth/bluetooth_error.h>

#include "BluetoothServer.h"


static const uint16 kOpRemoteNameRequest = 0x0419;
static const uint16 kOpRemoteNameRequestCancel = 0x041a;
// A page lasts up to the Page Timeout (5.12 s by default) and the name
// exchange a little more.
static const bigtime_t kNameTimeout = 12000000;
// The most devices waiting for a name, and the most names kept.
static const size_t kMaxNamesWanted = 32;
static const size_t kMaxNamesKnown = 64;
// Remote Name Request Complete: status, address, then 248 name bytes.
static const size_t kNameCompleteHeader = 1 + 6;
static const size_t kMaxNameLength = 248;


const BString*
LocalDeviceImpl::_R5KnownName(const bdaddr_t& address) const
{
	for (size_t i = 0; i < fNamesKnown.size(); i++) {
		if (bdaddrUtils::Compare(fNamesKnown[i].address, address))
			return &fNamesKnown[i].name;
	}
	return NULL;
}


void
LocalDeviceImpl::_R5NameWanted(const bdaddr_t& address,
	uint8 pageScanRepetitionMode, uint16 clockOffset,
	const uint8 deviceClass[3])
{
	if (_R5KnownName(address) != NULL)
		return;
	if (fNameAsking && bdaddrUtils::Compare(fNameCurrent.address, address))
		return;
	for (size_t i = 0; i < fNamesWanted.size(); i++) {
		if (bdaddrUtils::Compare(fNamesWanted[i].address, address)) {
			// The newer answer has the newer clock offset.
			fNamesWanted[i].pageScanRepetitionMode = pageScanRepetitionMode;
			fNamesWanted[i].clockOffset = clockOffset;
			return;
		}
	}
	if (fNamesWanted.size() >= kMaxNamesWanted)
		return;
	NameWanted wanted;
	wanted.address = address;
	wanted.pageScanRepetitionMode = pageScanRepetitionMode;
	wanted.clockOffset = clockOffset;
	memcpy(wanted.deviceClass, deviceClass, sizeof(wanted.deviceClass));
	fNamesWanted.push_back(wanted);
}


/*!	For an inquiry answer: the name, when one is known, as "friendly_name"
	for this answer (an empty one otherwise, so each answer of a message
	keeps its own index); a device without one waits for the scan's end.
*/
void
LocalDeviceImpl::_R5AddInquiryName(BMessage& reply, const bdaddr_t& address,
	uint8 pageScanRepetitionMode, uint16 clockOffset,
	const uint8 deviceClass[3])
{
	const BString* known = _R5KnownName(address);
	if (known == NULL) {
		ServerRemoteDevice* rd = RemoteDeviceByAddr(address);
		if (rd != NULL && rd->friendly_name.Length() > 0)
			known = &rd->friendly_name;
	}
	reply.AddString("friendly_name", known != NULL ? known->String() : "");
	if (known == NULL) {
		// Clock_Offset of the answer is bits 16 to 2 of the clock offset;
		// bit 15 says it is valid (7.1.19).
		_R5NameWanted(address, pageScanRepetitionMode,
			(uint16)(clockOffset | 0x8000), deviceClass);
	}
}


void
LocalDeviceImpl::_R5NameHeard(const bdaddr_t& address, const BString& name,
	const uint8* deviceClass, bool tellPage)
{
	if (name.Length() == 0)
		return;

	bool stored = false;
	for (size_t i = 0; i < fNamesKnown.size() && !stored; i++) {
		if (bdaddrUtils::Compare(fNamesKnown[i].address, address)) {
			fNamesKnown[i].name = name;
			stored = true;
		}
	}
	if (!stored) {
		if (fNamesKnown.size() >= kMaxNamesKnown)
			fNamesKnown.erase(fNamesKnown.begin());
		NameKnown known;
		known.address = address;
		known.name = name;
		fNamesKnown.push_back(known);
	}
	for (size_t i = 0; i < fNamesWanted.size(); i++) {
		if (bdaddrUtils::Compare(fNamesWanted[i].address, address)) {
			fNamesWanted.erase(fNamesWanted.begin() + i);
			break;
		}
	}

	// A record without a name gets this one; a paired device keeps it.
	uint8 recordClass[3] = { 0, 0, 0 };
	ServerRemoteDevice* rd = RemoteDeviceByAddr(address);
	if (rd != NULL) {
		memcpy(recordClass, rd->classOfDevice, sizeof(recordClass));
		if (rd->friendly_name.Length() == 0) {
			rd->friendly_name = name;
			linkkey_t nullKey = LinkKeyUtils::NullKey();
			if (!LinkKeyUtils::Compare(&rd->link_key, &nullKey))
				SaveRemoteDevices();
		}
	}

	// The page that scanned: an answer like the inquiry's, which names a
	// device it already lists.
	if (!tellPage || !fNameTarget.IsValid())
		return;
	BMessage reply(BT_MSG_INQUIRY_DEVICE);
	reply.AddUInt8("count", 1);
	reply.AddData("bdaddr", B_ANY_TYPE, &address, sizeof(bdaddr_t));
	reply.AddData("dev_class", B_ANY_TYPE,
		deviceClass != NULL ? deviceClass : recordClass, 3);
	reply.AddString("friendly_name", name);
	reply.AddBool("friendly_name_is_complete", true);
	reply.AddBool("r5_name", true);
	const status_t sent = fNameTarget.SendMessage(&reply, (BHandler*)NULL, 0);

	char addressText[18];
	char nameText[SCAN_LOG_NAME_MAX + 1];
	scan_log_format(address.b, name.String(), addressText, nameText);
	syslog(LOG_INFO, "bluetooth_server: hci%" B_PRId32 " name of classic %s"
		" \"%s\": %s", GetID(), addressText, nameText,
		sent == B_OK ? "sent to the list" : "not sent");
}


void
LocalDeviceImpl::_R5AskNextName()
{
	if (fNameAsking || fNameInquiring
		|| !((BluetoothServer*)be_app)->RadioEnabled())
		return;

	while (!fNamesWanted.empty()) {
		NameWanted wanted = fNamesWanted.front();
		fNamesWanted.erase(fNamesWanted.begin());
		if (_R5KnownName(wanted.address) != NULL)
			continue;

		// Remote Name Request: address, Page_Scan_Repetition_Mode, a
		// reserved byte, Clock_Offset (7.1.19).
		uint8 command[3 + 10];
		command[0] = (uint8)kOpRemoteNameRequest;
		command[1] = (uint8)(kOpRemoteNameRequest >> 8);
		command[2] = 10;
		memcpy(command + 3, &wanted.address, sizeof(bdaddr_t));
		command[9] = wanted.pageScanRepetitionMode;
		command[10] = 0;
		command[11] = (uint8)wanted.clockOffset;
		command[12] = (uint8)(wanted.clockOffset >> 8);
		if (R5IssueCommand(command, sizeof(command)) != B_OK)
			return;
		fNameAsking = true;
		fNameStatusPending = true;
		fNameCurrent = wanted;
		fNameDeadline = system_time() + kNameTimeout;
		return;
	}
}


//!	Ends the Remote Name Request on its way: a scan or a pairing needs the
//!	radio's paging now.
void
LocalDeviceImpl::_R5CancelName()
{
	if (!fNameAsking)
		return;
	uint8 command[3 + 6];
	command[0] = (uint8)kOpRemoteNameRequestCancel;
	command[1] = (uint8)(kOpRemoteNameRequestCancel >> 8);
	command[2] = 6;
	memcpy(command + 3, &fNameCurrent.address, sizeof(bdaddr_t));
	if (R5IssueCommand(command, sizeof(command)) == B_OK) {
		fNameCancelled = true;
		fNameCancelledAddress = fNameCurrent.address;
	}
	fNameAsking = false;
	fNameStatusPending = false;
}


void
LocalDeviceImpl::_R5InquiryStarted(const BMessenger& target)
{
	fNameTarget = target;
	fNameInquiring = true;
	_R5CancelName();
	// This scan names again what waits for a name.
	fNamesWanted.clear();
	scan_log_reset(&fClassicLog);
	fClassicDropped = 0;
	memset(fInquiryEvents, 0, sizeof(fInquiryEvents));
}


void
LocalDeviceImpl::_R5InquiryEnded()
{
	if (fNameInquiring) {
		syslog(LOG_INFO, "bluetooth_server: hci%" B_PRId32 " inquiry heard"
			" %" B_PRIu32 " classic devices, %" B_PRIu32 " sent to the list"
			", %" B_PRIu32 " names to ask (%" B_PRIu32 " answers past the"
			" logged ones, %" B_PRIu32 " with no scan waiting; events from"
			" the radio: %" B_PRIu32 " standard, %" B_PRIu32 " with rssi, %"
			B_PRIu32 " extended)", GetID(),
			fClassicLog.heard, fClassicLog.listed,
			(uint32)fNamesWanted.size(), fClassicLog.unlogged,
			fClassicDropped, fInquiryEvents[0], fInquiryEvents[1],
			fInquiryEvents[2]);
	}
	fNameInquiring = false;
	_R5AskNextName();
}


void
LocalDeviceImpl::_R5ConnectedWithoutName(ServerRemoteDevice* rd)
{
	if (rd == NULL || rd->friendly_name.Length() > 0)
		return;
	const BString* known = _R5KnownName(rd->bdaddr);
	if (known != NULL) {
		_R5NameHeard(rd->bdaddr, *known, NULL, false);
		return;
	}
	// On a link that is up the controller does not page; the clock offset
	// is not needed.
	_R5NameWanted(rd->bdaddr, rd->pscan_rep_mode, 0, rd->classOfDevice);
	_R5AskNextName();
}


//!	A pairing from the page: its paging goes first, the scan's names wait.
void
LocalDeviceImpl::_R5PairingStarts()
{
	_R5CancelName();
	fNamesWanted.clear();
}


void
LocalDeviceImpl::R5NamePulse()
{
	if (fNameAsking && system_time() > fNameDeadline) {
		syslog(LOG_INFO, "bluetooth_server: hci%" B_PRId32 " name request "
			"timed out", GetID());
		_R5CancelName();
		_R5AskNextName();
	}
}


/*!	The answers to this file's own commands. A Remote Name Request
	Complete for an address the kit asked (a petition waits for it) goes on
	to the kit as well.
*/
bool
LocalDeviceImpl::_R5NameEvent(const struct hci_event_header* event)
{
	const uint8* data = (const uint8*)(event + 1);
	switch (event->ecode) {
		case HCI_EVENT_CMD_STATUS:
		{
			// status, number of commands, opcode
			if (event->elen < 4)
				return false;
			const uint16 opcode = data[2] | (data[3] << 8);
			if (opcode != kOpRemoteNameRequest || !fNameAsking
				|| !fNameStatusPending
				|| FindPetition(HCI_EVENT_CMD_STATUS, opcode) != NULL)
				return false;
			fNameStatusPending = false;
			if (data[0] != BT_OK) {
				syslog(LOG_INFO, "bluetooth_server: hci%" B_PRId32 " name "
					"request refused (%#x)", GetID(), data[0]);
				fNameAsking = false;
				_R5AskNextName();
			}
			return true;
		}

		case HCI_EVENT_CMD_COMPLETE:
		{
			// number of commands, opcode, status
			if (event->elen < 4)
				return false;
			const uint16 opcode = data[1] | (data[2] << 8);
			if (opcode != kOpRemoteNameRequestCancel
				|| FindPetition(HCI_EVENT_CMD_COMPLETE, opcode) != NULL)
				return false;
			return true;
		}

		case HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE:
		{
			if (event->elen < kNameCompleteHeader)
				return false;
			const uint8 status = data[0];
			bdaddr_t address;
			memcpy(&address, data + 1, sizeof(address));
			const bool ours = fNameAsking
				&& bdaddrUtils::Compare(address, fNameCurrent.address);
			const bool cancelled = !ours && fNameCancelled
				&& bdaddrUtils::Compare(address, fNameCancelledAddress);
			if (cancelled)
				fNameCancelled = false;

			if (status == BT_OK) {
				size_t length = event->elen - kNameCompleteHeader;
				if (length > kMaxNameLength)
					length = kMaxNameLength;
				const char* text = (const char*)data + kNameCompleteHeader;
				BString name(text, strnlen(text, length));
				_R5NameHeard(address, name,
					ours ? fNameCurrent.deviceClass : NULL, true);
			}
			if (ours) {
				if (status != BT_OK) {
					syslog(LOG_INFO, "bluetooth_server: hci%" B_PRId32
						" name not read (%#x)", GetID(), status);
				}
				fNameAsking = false;
				fNameStatusPending = false;
			}
			const bool kitWaits = FindPetition(
				HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE) != NULL;
			if (ours)
				_R5AskNextName();
			return (ours || cancelled) && !kitWaits;
		}
	}
	return false;
}


// #pragma mark - scan lines


//!	An inquiry answer sent to the page that scans (sent: its SendReply()).
void
LocalDeviceImpl::_R5LogInquiryReply(const BMessage& reply, status_t sent)
{
	uint8 count = 0;
	if (reply.FindUInt8("count", &count) != B_OK)
		return;
	for (uint8 i = 0; i < count; i++) {
		const void* address;
		ssize_t size;
		if (reply.FindData("bdaddr", B_ANY_TYPE, i, &address, &size) != B_OK
			|| size != (ssize_t)sizeof(bdaddr_t))
			continue;
		const uint8* bytes = ((const bdaddr_t*)address)->b;
		if (!scan_log_note(&fClassicLog, bytes, sent == B_OK))
			continue;

		const char* name = NULL;
		reply.FindString("friendly_name", i, &name);
		int8 rssi = HCI_RSSI_INVALID;
		reply.FindInt8("rssi", i, &rssi);
		uint32 deviceClass = 0;
		const void* classData;
		if (reply.FindData("dev_class", B_ANY_TYPE, i, &classData, &size)
				== B_OK && size == 3) {
			const uint8* record = (const uint8*)classData;
			deviceClass = record[0] | (record[1] << 8) | (record[2] << 16);
		}
		char addressText[18];
		char nameText[SCAN_LOG_NAME_MAX + 1];
		scan_log_format(bytes, name, addressText, nameText);
		syslog(LOG_INFO, "bluetooth_server: hci%" B_PRId32 " scan heard"
			" classic %s \"%s\"%s rssi %d class %#" B_PRIx32 ": %s",
			GetID(), addressText, nameText,
			nameText[0] == '\0' ? " (name asked after the scan)" : "",
			(int)rssi, deviceClass,
			sent == B_OK ? "sent to the list" : "not sent: the list did not"
				" take it");
	}
}


//!	An inquiry answer with no scan request waiting for it: dropped.
void
LocalDeviceImpl::_R5LogInquiryDropped(uint8 eventCode)
{
	if (fClassicDropped++ == 0) {
		syslog(LOG_NOTICE, "bluetooth_server: hci%" B_PRId32 " classic"
			" inquiry answer (event %#x) while no scan waits: not sent",
			GetID(), eventCode);
	}
}
