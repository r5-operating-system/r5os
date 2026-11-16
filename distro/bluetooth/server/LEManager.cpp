/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Bluetooth Low Energy in bluetooth_server (docs/BLUETOOTH.md, step 6):
 * controller setup, scan, links, encryption and the paired-device file.
 * Core 6.0, Vol 4, Part E, 7.8 for the commands and 7.7.65 for the LE Meta
 * events. Neither addresses nor keys are logged.
 */


#include "LEManagerPrivate.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <Autolock.h>
#include <DataIO.h>
#include <File.h>
#include <Path.h>

#include <bluetooth/bdaddrUtils.h>
#include <bluetooth/HCI/btHCI_command.h>
#include <bluetoothserver_p.h>

#include "BluetoothServer.h"
#include "LocalDeviceImpl.h"
#include "PairingNotice.h"
#include "PasskeyWindow.h"
#include "le_parse.h"


// Seen-list size, and how often one device is sent to the scan list.
static const size_t kMaxSeen = 64;
static const bigtime_t kReportInterval = 1000000;

// Connection parameters (1.25 ms units, timeout 10 ms): BlueZ's defaults,
// an interval of 30 to 50 ms, no latency, a 420 ms supervision timeout.
// A mouse asks for its own short interval right after it connected (the
// first stick on the 5625U used 7.5 to 15 ms and every link to a
// Designer Mouse failed with 0x3E). The scan: 60 ms interval, 30 ms window
// for the list; while connecting the whole interval (0.625 ms units).
static const uint16 kConnIntervalMin = 0x0018;
static const uint16 kConnIntervalMax = 0x0028;
static const uint16 kConnLatency = 0;
static const uint16 kConnTimeout = 0x002a;
static const uint16 kScanInterval = 0x0060;
static const uint16 kScanWindow = 0x0030;
static const uint16 kConnectScanWindow = 0x0060;
// How often a pairing connect is tried again after "Connection Failed to
// be Established" (0x3E), which a device that is busy can cause.
// None since the review of boot 3: one try, one line in the log.
static const int kMaxConnectRetries = 0;

static const char* kBondsFile = "le_paired_devices";

// The fixed-channel sockets, shared by every radio.
static int sChannel[2] = { -1, -1 };	// ATT, SMP
static thread_id sChannelThread[2] = { -1, -1 };
static BLocker sManagersLock("le managers");
static std::vector<LEManager*> sManagers;


LEManager::LEManager(LocalDeviceImpl* device)
	:
	fDevice(device),
	fLock("le manager"),
	fReady(false),
	fExtended(false),
	fPrivacy(false),
	fPending(0),
	fScanning(false),
	fScanOn(false),
	fInitiating(kIdle),
	fWantDirect(false),
	fDirectType(R5_BDADDR_LE_PUBLIC),
	fRadioEnabled(true),
	fAcceptListDirty(false),
	fConnectRetries(0),
	fDirectResultPending(false)
{
	memset(fLMPFeatures, 0, sizeof(fLMPFeatures));
	memset(fFeatures, 0, sizeof(fFeatures));
	memset(&fLocalAddress, 0, sizeof(fLocalAddress));
	memset(&fDirectAddress, 0, sizeof(fDirectAddress));
	fDirectStarted = 0;
	_LoadBonds();

	BAutolock _(sManagersLock);
	sManagers.push_back(this);
}


LEManager::~LEManager()
{
	{
		BAutolock _(sManagersLock);
		for (size_t i = 0; i < sManagers.size(); i++) {
			if (sManagers[i] == this) {
				sManagers.erase(sManagers.begin() + i);
				break;
			}
		}
	}

	BAutolock _(fLock);
	while (!fLinks.empty())
		_CloseLink(fLinks.back(), false);
	for (size_t i = 0; i < fSeen.size(); i++)
		delete fSeen[i];
	for (size_t i = 0; i < fBonds.size(); i++) {
		memset(fBonds[i]->ltk, 0, sizeof(fBonds[i]->ltk));
		memset(fBonds[i]->irk, 0, sizeof(fBonds[i]->irk));
		delete fBonds[i];
	}
}


/*static*/ bool
LEManager::Supported(const uint8 lmpFeatures[8])
{
	// "LE Supported (Controller)", LMP feature bit 38 (Vol 2, Part C,
	// 3.3): octet 4, bit 6.
	return lmpFeatures != NULL && (lmpFeatures[4] & 0x40) != 0;
}


void
LEManager::Setup(const uint8 lmpFeatures[8])
{
	BAutolock _(fLock);

	// A reset dropped every link, scan and pending command.
	while (!fLinks.empty())
		_CloseLink(fLinks.back(), false);
	fQueue.clear();
	fPending = 0;
	fReady = false;
	fScanOn = false;
	fInitiating = kIdle;
	memcpy(fLMPFeatures, lmpFeatures, sizeof(fLMPFeatures));

	// The host supports LE (a dual-mode controller needs to hear it,
	// 7.3.79), our address for SMP, then the LE features, which decide
	// the rest (_CommandDone()).
	const uint8 hostSupport[2] = { 1, 0 };
	_Issue(kOpWriteLEHostSupport, hostSupport, sizeof(hostSupport));
	// On the MT7922 a Classic pairing ended in 0x37 (Simple Pairing Not
	// Supported by Host): Write LE Host Support may clear the SSP host bit
	// of feature page 1. Simple Pairing Mode is written again after it,
	// and pages 1 and 2 are read: page 2 says whether the controller has
	// Secure Connections, which the host then turns on, as Linux does.
	if ((lmpFeatures[6] & 0x08) != 0) {
		const uint8 on = 1;
		_Issue(kOpWriteSimplePairingMode, &on, 1);
	}
	const uint8 page1 = 1;
	_Issue(kOpReadLocalExtFeatures, &page1, 1);
	const uint8 page2 = 2;
	_Issue(kOpReadLocalExtFeatures, &page2, 1);
	_Issue(kOpReadBDAddr, NULL, 0);
	_Issue(kOpLEReadFeatures, NULL, 0);
}


void
LEManager::_SetupStep(int step)
{
	// After the LE features (step 0): the event mask and buffers, data
	// length and PHY where supported, the resolving list, the accept list.
	if (step != 0)
		return;

	fExtended = le_has_feature(fFeatures, kFeatureExtendedAdvertising);
	fPrivacy = le_has_feature(fFeatures, kFeaturePrivacy);

	// Bit n is subevent n + 1 (7.8.1): Connection Complete, Advertising
	// Report, Connection Update Complete, Read Remote Features Complete,
	// Long Term Key Request; the rest only when the controller has the
	// feature.
	uint64 mask = 0x1f;
	if (le_has_feature(fFeatures, kFeatureConnParamRequest))
		mask |= 1ULL << 5;		// Remote Connection Parameter Request
	if (le_has_feature(fFeatures, kFeatureDataLength))
		mask |= 1ULL << 6;		// Data Length Change
	if (fPrivacy)
		mask |= 1ULL << 9;		// Enhanced Connection Complete
	if (le_has_feature(fFeatures, kFeature2MPHY)
		|| le_has_feature(fFeatures, kFeatureCodedPHY))
		mask |= 1ULL << 11;		// PHY Update Complete
	if (fExtended)
		mask |= 1ULL << 12;		// Extended Advertising Report
	uint8 bytes[8];
	for (int i = 0; i < 8; i++)
		bytes[i] = (uint8)(mask >> (8 * i));
	_Issue(kOpLESetEventMask, bytes, sizeof(bytes));
	syslog(LOG_INFO, "bluetooth_server: hci%" B_PRId32 " le event mask %#"
		B_PRIx64 " (%s scan)", fDevice->GetID(), mask,
		fExtended ? "extended" : "legacy");

	// The kernel reads the LE buffer length from this answer (btCoreData).
	_Issue(kOpLEReadBufferSize, NULL, 0);

	if (le_has_feature(fFeatures, kFeatureDataLength)) {
		// 251 bytes, 2120 us: one ATT PDU of 247 bytes per packet.
		uint8 length[4];
		le_put16(length, 251);
		le_put16(length + 2, 2120);
		_Issue(kOpLEWriteDataLength, length, sizeof(length));
	}
	// No PHY preference: the controller's default (1M) until a device
	// asks for another, the least to go wrong while a link is made.

	_ResolvingListSetup();
	_AcceptListSetup();
	fReady = true;
	if (fScanning)
		_SetScan(true);
	_Connect();
}


bool
LEManager::HandleEvent(const hci_event_header* event, size_t size)
{
	if (event == NULL || size < sizeof(*event)
		|| size - sizeof(*event) < event->elen)
		return false;
	const uint8* data = (const uint8*)(event + 1);
	const size_t length = event->elen;

	BAutolock _(fLock);
	switch (event->ecode) {
		case 0x3e:
			_LEMeta(data, length);
			return true;

		case HCI_EVENT_CMD_COMPLETE:
		case HCI_EVENT_CMD_STATUS:
		{
			// Complete: commands, opcode (2), status, parameters.
			// Status: status, commands, opcode (2).
			const bool complete = event->ecode == HCI_EVENT_CMD_COMPLETE;
			if (length < (complete ? 3u : 4u))
				return false;
			const uint16 opcode = complete ? (data[1] | (data[2] << 8))
				: (data[2] | (data[3] << 8));
			if (opcode == 0 || opcode != fPending)
				return false;
			if (complete) {
				const uint8 status = length > 3 ? data[3] : 0xff;
				_CommandDone(opcode, status, data + 4,
					length > 4 ? length - 4 : 0);
			} else {
				// A command that ends in an event (Create Connection,
				// Enable Encryption) is done here, or refused here.
				_CommandDone(opcode, data[0], NULL, 0);
			}
			return true;
		}

		case HCI_EVENT_DISCONNECTION_COMPLETE:
		{
			// status, handle (2), reason
			if (length < 4 || data[0] != BT_OK)
				return false;
			const uint16 handle = (data[1] | (data[2] << 8)) & 0x0fff;
			if (_FindLink(handle) == NULL)
				return false;
			_Disconnected(handle, data[3]);
			return true;
		}

		case HCI_EVENT_ENCRYPT_CHANGE:
		{
			// status, handle (2), enabled
			if (length < 4)
				return false;
			const uint16 handle = (data[1] | (data[2] << 8)) & 0x0fff;
			if (_FindLink(handle) == NULL)
				return false;
			_EncryptionChanged(handle, data[0], data[3] != 0);
			return true;
		}

		case 0x30:
		{
			// Encryption Key Refresh Complete: status, handle (2).
			if (length < 3)
				return false;
			const uint16 handle = (data[1] | (data[2] << 8)) & 0x0fff;
			if (_FindLink(handle) == NULL)
				return false;
			_EncryptionChanged(handle, data[0], true);
			return true;
		}
	}
	return false;
}


// #pragma mark - commands


void
LEManager::_Issue(uint16 opcode, const void* parameters, size_t length)
{
	if (length > 255)
		return;
	std::vector<uint8> command(3 + length);
	le_put16(&command[0], opcode);
	command[2] = (uint8)length;
	if (length > 0)
		memcpy(&command[3], parameters, length);
	fQueue.push_back(command);
	if (fPending == 0)
		_SendNext();
}


void
LEManager::_SendNext()
{
	while (fPending == 0 && !fQueue.empty()) {
		std::vector<uint8> command = fQueue.front();
		fQueue.pop_front();
		const uint16 opcode = command[0] | (command[1] << 8);
		fPending = opcode;
		status_t status = fDevice->R5IssueCommand(&command[0], command.size());
		// Wipe what may carry a key (Enable Encryption, resolving list).
		memset(&command[0], 0, command.size());
		if (status != B_OK) {
			syslog(LOG_WARNING, "bluetooth_server: hci%" B_PRId32
				" le command %#06x not sent", fDevice->GetID(), opcode);
			fPending = 0;
			_CommandDone(opcode, BT_ERROR, NULL, 0);
		}
	}
}


void
LEManager::_CommandDone(uint16 opcode, uint8 status, const uint8* data,
	size_t length)
{
	fPending = 0;

	switch (opcode) {
		case kOpReadBDAddr:
		{
			if (status == BT_OK && length >= 6)
				memcpy(&fLocalAddress, data, 6);
			const bdaddr_t none = {{0, 0, 0, 0, 0, 0}};
			syslog(LOG_INFO, "bluetooth_server: hci%" B_PRId32 " local address"
				" %s", fDevice->GetID(), status == BT_OK
					&& !bdaddrUtils::Compare(fLocalAddress, none)
						? "set" : "missing");
			break;
		}

		case kOpReadLocalExtFeatures:
			// Status, page, maximum page, features (8).
			if (status != BT_OK || length < 10) {
				syslog(LOG_NOTICE, "bluetooth_server: hci%" B_PRId32
					" local features page not read (%#x)", fDevice->GetID(),
					status);
				break;
			}
			if (data[0] == 1) {
				syslog(LOG_INFO, "bluetooth_server: hci%" B_PRId32 " local"
					" features page 1 %#x (ssp host %s, le host %s)",
					fDevice->GetID(), data[2],
					(data[2] & 0x01) != 0 ? "on" : "off",
					(data[2] & 0x02) != 0 ? "on" : "off");
			} else if (data[0] == 2) {
				// Secure Connections (Controller Support): page 2, bit 8.
				const bool sc = (data[3] & 0x01) != 0;
				syslog(LOG_INFO, "bluetooth_server: hci%" B_PRId32 " local"
					" features page 2 %#x%02x (secure connections %s)",
					fDevice->GetID(), data[3], data[2],
					sc ? "supported" : "not supported");
				if (sc) {
					const uint8 on = 1;
					_Issue(kOpWriteSCHostSupport, &on, 1);
				}
			}
			break;

		case kOpWriteSCHostSupport:
		case kOpWriteSimplePairingMode:
			syslog(LOG_INFO, "bluetooth_server: hci%" B_PRId32 " %s %s (%#x)",
				fDevice->GetID(), opcode == kOpWriteSCHostSupport
					? "secure connections host" : "simple pairing mode",
				status == BT_OK ? "on" : "failed", status);
			break;

		case kOpLEReadFeatures:
			if (status == BT_OK && length >= 8)
				memcpy(fFeatures, data, 8);
			else {
				syslog(LOG_WARNING, "bluetooth_server: hci%" B_PRId32
					" le features not read (%#x)", fDevice->GetID(), status);
			}
			_SetupStep(0);
			break;

		case kOpLECreateConnection:
		case kOpLEExtCreateConnection:
			if (status != BT_OK) {
				syslog(LOG_WARNING, "bluetooth_server: hci%" B_PRId32
					" le connect refused (%#x)", fDevice->GetID(), status);
				if (fInitiating == kDirect) {
					fWantDirect = false;
					_DirectResult(false);
				}
				fInitiating = kIdle;
			}
			break;

		case kOpLEReadRemoteFeatures:
			// Refused: go on without it, for the link that waits.
			if (status != BT_OK) {
				syslog(LOG_NOTICE, "bluetooth_server: hci%" B_PRId32
					" le remote features not read (%#x)", fDevice->GetID(),
					status);
				for (size_t i = 0; i < fLinks.size(); i++) {
					if (fLinks[i]->featuresPending) {
						_LinkReady(fLinks[i]);
						break;
					}
				}
			}
			break;

		case kOpLEEnableEncryption:
			if (status != BT_OK) {
				syslog(LOG_WARNING, "bluetooth_server: hci%" B_PRId32
					" le encryption refused (%#x)", fDevice->GetID(), status);
			}
			break;

		case kOpLESetScanEnable:
		case kOpLESetExtScanEnable:
			if (status != BT_OK) {
				syslog(LOG_WARNING, "bluetooth_server: hci%" B_PRId32
					" le scan not changed (%#x)", fDevice->GetID(), status);
			}
			break;

		case kOpLEClearAcceptList:
		case kOpLEAddAcceptList:
		case kOpLEClearResolvingList:
		case kOpLEAddResolvingList:
		case kOpLESetResolutionEnable:
			// A refused list write is written again at the next connect,
			// or a paired device stays unknown to the controller until the
			// next reset.
			if (status != BT_OK) {
				syslog(LOG_NOTICE, "bluetooth_server: hci%" B_PRId32
					" le list command %#06x refused (%#x), written again later",
					fDevice->GetID(), opcode, status);
				fAcceptListDirty = true;
			}
			break;

		default:
			if (status != BT_OK) {
				syslog(LOG_NOTICE, "bluetooth_server: hci%" B_PRId32
					" le command %#06x answered with status %#x",
					fDevice->GetID(), opcode, status);
			}
			break;
	}

	_SendNext();
}


// #pragma mark - LE Meta


void
LEManager::_LEMeta(const uint8* data, size_t size)
{
	if (size < 1)
		return;
	const uint8 subevent = data[0];
	switch (subevent) {
		case 0x01:	// Connection Complete
		case 0x0a:	// Enhanced Connection Complete
		case 0x29:	// Enhanced Connection Complete v2
			_ConnectionComplete(data, size, subevent);
			break;

		case 0x02:
			_AdvertisingReport(data + 1, size - 1, false);
			break;

		case 0x0d:
			_AdvertisingReport(data + 1, size - 1, true);
			break;

		case 0x04:
		{
			// LE Read Remote Features Complete: status, handle (2),
			// features (8).
			if (size < 4)
				break;
			const uint16 handle = (data[2] | (data[3] << 8)) & 0x0fff;
			Link* link = _FindLink(handle);
			if (link == NULL || !link->featuresPending)
				break;
			syslog(LOG_INFO, "bluetooth_server: hci%" B_PRId32 " le remote"
				" features read (%#x)", fDevice->GetID(), data[1]);
			_LinkReady(link);
			break;
		}

		case 0x05:
		{
			// Long Term Key Request: only a peripheral gets one; we are
			// never one.
			if (size < 3)
				break;
			uint8 handle[2] = { data[1], (uint8)(data[2] & 0x0f) };
			_Issue(kOpLELTKNegativeReply, handle, sizeof(handle));
			break;
		}

		case 0x06:
		{
			// Remote Connection Parameter Request: handle, interval min
			// and max, latency, timeout. The same rule as the kernel's
			// L2CAP Connection Parameter Update (l2cap, step 1c).
			if (size < 11)
				break;
			const uint16 minimum = data[3] | (data[4] << 8);
			const uint16 maximum = data[5] | (data[6] << 8);
			const uint16 latency = data[7] | (data[8] << 8);
			const uint16 timeout = data[9] | (data[10] << 8);
			const bool valid = minimum >= 6 && maximum <= 3200
				&& minimum <= maximum && latency <= 499 && timeout >= 10
				&& timeout <= 3200
				&& (uint32)timeout * 4 > (1 + (uint32)latency) * maximum;
			if (valid) {
				uint8 reply[14];
				memcpy(reply, data + 1, 10);
				le_put16(reply + 10, 0);
				le_put16(reply + 12, 0);
				_Issue(kOpLEConnParamReply, reply, sizeof(reply));
			} else {
				// Unacceptable Connection Parameters.
				const uint8 reply[3] = { data[1], data[2], 0x3b };
				_Issue(kOpLEConnParamNegativeReply, reply, sizeof(reply));
			}
			break;
		}
	}
}


void
LEManager::_AdvertisingReport(const uint8* data, size_t size, bool extended)
{
	size_t offset = 1;
	le_adv_report report;
	const bigtime_t now = system_time();
	while (le_adv_report_next(data, size, extended, &offset, &report)) {
		// Anonymous advertisers (0xFF) cannot be connected to.
		if (report.address_type > 3)
			continue;

		bdaddr_t address;
		memcpy(&address, report.address, sizeof(address));
		Seen* seen = _FindSeen(address);
		if (seen == NULL) {
			if (fSeen.size() >= kMaxSeen) {
				// Forget the one heard longest ago.
				size_t oldest = 0;
				for (size_t i = 1; i < fSeen.size(); i++) {
					if (fSeen[i]->lastSeen < fSeen[oldest]->lastSeen)
						oldest = i;
				}
				delete fSeen[oldest];
				fSeen.erase(fSeen.begin() + oldest);
			}
			seen = new(std::nothrow) Seen;
			if (seen == NULL)
				return;
			seen->address = address;
			seen->classOfDevice = 0;
			seen->connectable = false;
			seen->lastSent = 0;
			seen->eventType = 0;
			seen->extendedReport = false;
			seen->hciAddressType = 0;
			seen->flags = 0;
			seen->primaryPhy = 1;
			fSeen.push_back(seen);
		}
		seen->addressType = (report.address_type & 0x01) != 0
			? R5_BDADDR_LE_RANDOM : R5_BDADDR_LE_PUBLIC;
		seen->rssi = report.rssi;
		seen->lastSeen = now;
		if (!report.scan_response) {
			seen->eventType = report.event_type;
			seen->extendedReport = extended;
		}
		if (report.connectable)
			seen->connectable = true;
		if (!report.scan_response) {
			seen->hciAddressType = report.address_type;
			seen->primaryPhy = report.primary_phy;
		}

		le_ad_info info;
		memset(&info, 0, sizeof(info));
		le_ad_parse(report.data, report.data_length, &info);
		if (info.has_flags)
			seen->flags = info.flags;
		if (info.name[0] != '\0')
			seen->name = info.name;
		const uint32 classOfDevice = le_ad_class_of_device(&info);
		if (classOfDevice != 0)
			seen->classOfDevice = classOfDevice;

		// The scan list shows a device that can be connected to and has a
		// name, at most once a second.
		if (!fScanning || !fScanTarget.IsValid() || !seen->connectable
			|| seen->name.Length() == 0 || now - seen->lastSent < kReportInterval)
			continue;
		seen->lastSent = now;

		uint8 deviceClass[3] = { (uint8)(seen->classOfDevice & 0xff),
			(uint8)((seen->classOfDevice >> 8) & 0xff),
			(uint8)((seen->classOfDevice >> 16) & 0xff) };
		BMessage reply(BT_MSG_INQUIRY_DEVICE);
		reply.AddUInt8("count", 1);
		reply.AddData("bdaddr", B_ANY_TYPE, &address, sizeof(bdaddr_t));
		reply.AddData("dev_class", B_ANY_TYPE, deviceClass, 3);
		reply.AddUInt8("page_repetition_mode", 0x02);
		reply.AddUInt8("scan_period_mode", 0);
		reply.AddUInt16("clock_offset", 0);
		reply.AddInt8("rssi", seen->rssi);
		reply.AddString("friendly_name", seen->name);
		reply.AddBool("r5_le", true);
		fScanTarget.SendMessage(&reply, (BHandler*)NULL, 0);
	}
}


void
LEManager::_ConnectionComplete(const uint8* data, size_t size, uint8 subevent)
{
	// subevent, status, handle (2), role, peer address type, peer address
	// (6), ... (7.7.65.1, 7.7.65.10).
	const size_t needed = subevent == 0x01 ? 19 : subevent == 0x0a ? 31 : 34;
	if (size < needed)
		return;

	const uint8 status = data[1];
	const bool wasDirect = fInitiating == kDirect;
	const bool wasCancelling = fInitiating == kCancelling;
	fInitiating = kIdle;

	if (status != BT_OK) {
		// 0x02 (Unknown Connection Identifier) after Create Connection
		// Cancel.
		if (status != 0x02 || !wasCancelling) {
			syslog(LOG_NOTICE, "bluetooth_server: hci%" B_PRId32
				" le connect failed (%#x)", fDevice->GetID(), status);
			if (wasDirect) {
				fWantDirect = false;
				_DirectResult(false);
			}
		}
		_Connect();
		return;
	}

	const uint16 handle = (data[2] | (data[3] << 8)) & 0x0fff;
	// Role, address type, interval, latency, timeout: no address.
	const size_t times = subevent == 0x01 ? 12 : subevent == 0x0a ? 24 : 24;
	syslog(LOG_INFO, "bluetooth_server: hci%" B_PRId32 " le link role %u"
		" type %u interval %u latency %u timeout %u", fDevice->GetID(),
		data[4], data[5], data[times] | (data[times + 1] << 8),
		data[times + 2] | (data[times + 3] << 8),
		data[times + 4] | (data[times + 5] << 8));
	if (data[4] != 0x00) {
		// We never advertise; a link where we are the peripheral is not
		// ours.
		const uint8 command[3] = { (uint8)(handle & 0xff),
			(uint8)(handle >> 8), 0x13 };
		_Issue(kOpDisconnect, command, sizeof(command));
		_Connect();
		return;
	}

	Link* link = new(std::nothrow) Link;
	if (link == NULL)
		return;
	link->handle = handle;
	memcpy(&link->address, data + 6, sizeof(bdaddr_t));
	link->addressType = (data[5] & 0x01) != 0
		? R5_BDADDR_LE_RANDOM : R5_BDADDR_LE_PUBLIC;
	link->encrypted = false;
	link->pairRequested = fWantDirect
		&& bdaddrUtils::Compare(link->address, fDirectAddress);
	link->smpState = Link::kSMPIdle;
	link->pairing = false;
	link->smpStarted = 0;
	link->smpLast = 0;
	link->attLast = 0;
	link->secureConnections = false;
	link->method = Link::kJustWorks;
	link->keyPair.privateKey = NULL;
	link->passkey = 0;
	link->round = 0;
	link->keySize = 0;
	link->keysExpected = 0;
	link->keysReceived = 0;
	link->mtu = 23;
	link->requestPending = false;
	link->gattStep = 0;
	link->serviceStart = 0;
	link->serviceEnd = 0;
	link->next = 0;
	link->index = 0;
	link->usesReportIDs = false;
	link->kernelID = -1;
	link->resultPending = false;
	link->batteryStart = 0;
	link->batteryEnd = 0;
	link->batteryHandle = 0;
	link->battery = -1;
	link->featuresPending = false;
	fLinks.push_back(link);
	if (link->pairRequested) {
		fWantDirect = false;
		link->pairReply = fDirectReply;
		link->resultPending = fDirectResultPending;
		fDirectReply = BMessenger();
		fDirectResultPending = false;
	}

	syslog(LOG_INFO, "bluetooth_server: hci%" B_PRId32 " le link up%s",
		fDevice->GetID(), link->pairRequested ? " (pairing)" : "");

	if (!link->pairRequested && _FindBond(link->address) == NULL) {
		// Only paired devices and the one being paired get a link.
		_CloseLink(link, true);
		_Connect();
		return;
	}

	// First the link layer's own feature exchange, as BlueZ does: LE Read
	// Remote Features, and pairing or encryption once it completed
	// (7.8.21, subevent 0x04). SMP went out at once before.
	link->featuresPending = true;
	uint8 command[2];
	le_put16(command, link->handle);
	_Issue(kOpLEReadRemoteFeatures, command, sizeof(command));
	_Connect();
}


void
LEManager::_LinkReady(Link* link)
{
	link->featuresPending = false;
	LEBond* bond = _FindBond(link->address);
	if (link->pairRequested)
		_SMPStart(link);
	else if (bond != NULL)
		_StartEncryption(link, bond->ltk, bond->ediv, bond->rand);
}


void
LEManager::_Disconnected(uint16 handle, uint8 reason)
{
	Link* link = _FindLink(handle);
	if (link == NULL)
		return;
	syslog(LOG_INFO, "bluetooth_server: hci%" B_PRId32 " le link down (%#x)",
		fDevice->GetID(), reason);
	// "Connection Failed to be Established": the device did not answer in
	// the first connection events. A pairing connect is tried again.
	const bool retry = reason == 0x3e && link->pairRequested
		&& fConnectRetries < kMaxConnectRetries;
	if (link->pairing && link->smpState != Link::kSMPDone && !retry) {
		syslog(LOG_NOTICE, "bluetooth_server: smp pairing failed (link lost)");
	}
	if (!retry)
		_PairResult(link, false);
	if (retry) {
		fConnectRetries++;
		fWantDirect = true;
		fDirectAddress = link->address;
		fDirectType = link->addressType == R5_BDADDR_LE_RANDOM ? 1 : 0;
		syslog(LOG_INFO, "bluetooth_server: hci%" B_PRId32 " le connect"
			" again (%d)", fDevice->GetID(), fConnectRetries);
	}
	_CloseLink(link, false);
	_Connect();
}


void
LEManager::_EncryptionChanged(uint16 handle, uint8 status, bool enabled)
{
	Link* link = _FindLink(handle);
	if (link == NULL)
		return;

	if (status != BT_OK || !enabled) {
		syslog(LOG_NOTICE, "bluetooth_server: hci%" B_PRId32
			" le encryption failed (%#x)%s", fDevice->GetID(), status,
			link->pairing ? "" : "; pair the device again");
		if (link->pairing)
			_SMPFail(link, kSMPUnspecified);
		else if (status == 0x06) {
			// PIN or Key Missing: the device forgot the pairing. Its key
			// goes, so the accept list does not bring it back forever.
			for (size_t i = 0; i < fBonds.size(); i++) {
				if (!bdaddrUtils::Compare(fBonds[i]->address, link->address))
					continue;
				memset(fBonds[i]->ltk, 0, sizeof(fBonds[i]->ltk));
				memset(fBonds[i]->irk, 0, sizeof(fBonds[i]->irk));
				delete fBonds[i];
				fBonds.erase(fBonds.begin() + i);
				_SaveBonds();
				fAcceptListDirty = true;
				break;
			}
		}
		_CloseLink(link, true);
		if (fAcceptListDirty && fInitiating == kAuto)
			_CancelConnection();
		_Connect();
		return;
	}

	const bool first = !link->encrypted;
	link->encrypted = true;
	if (link->pairing) {
		_SMPEncrypted(link);
		return;
	}
	if (first) {
		syslog(LOG_INFO, "bluetooth_server: hci%" B_PRId32 " le encryption on",
			fDevice->GetID());
		_GATTStart(link);
	}
}


// #pragma mark - scan and connect


void
LEManager::StartScan(const BMessenger& target)
{
	BAutolock _(fLock);
	fScanTarget = target;
	fScanning = true;
	for (size_t i = 0; i < fSeen.size(); i++)
		fSeen[i]->lastSent = 0;
	_SetScan(true);
}


void
LEManager::StopScan()
{
	BAutolock _(fLock);
	fScanning = false;
	fScanTarget = BMessenger();
	_SetScan(false);
}


void
LEManager::_SetScan(bool enable)
{
	if (enable && (!fReady || !fRadioEnabled))
		return;
	// While audio streams, a scan would take the radio's time from it; the
	// scan comes back with AudioChanged(false). AudioStreaming() also ends
	// when the audio team is gone without saying so.
	if (enable && ((BluetoothServer*)be_app)->AudioStreaming())
		return;
	if (enable == fScanOn)
		return;
	fScanOn = enable;

	if (fExtended) {
		if (enable) {
			// Own address public, accept all, 1M PHY: active, interval,
			// window.
			uint8 parameters[3 + 5];
			parameters[0] = 0x00;
			parameters[1] = 0x00;
			parameters[2] = 0x01;
			parameters[3] = 0x01;
			le_put16(parameters + 4, kScanInterval);
			le_put16(parameters + 6, kScanWindow);
			_Issue(kOpLESetExtScanParameters, parameters, sizeof(parameters));
		}
		// Enable, no duplicate filter (names come in scan responses),
		// no duration or period.
		const uint8 command[6] = { (uint8)(enable ? 1 : 0), 0, 0, 0, 0, 0 };
		_Issue(kOpLESetExtScanEnable, command, sizeof(command));
	} else {
		if (enable) {
			// Active, interval, window, own address public, accept all.
			uint8 parameters[7];
			parameters[0] = 0x01;
			le_put16(parameters + 1, kScanInterval);
			le_put16(parameters + 3, kScanWindow);
			parameters[5] = 0x00;
			parameters[6] = 0x00;
			_Issue(kOpLESetScanParameters, parameters, sizeof(parameters));
		}
		const uint8 command[2] = { (uint8)(enable ? 1 : 0), 0 };
		_Issue(kOpLESetScanEnable, command, sizeof(command));
	}
	syslog(LOG_INFO, "bluetooth_server: hci%" B_PRId32 " le scan %s",
		fDevice->GetID(), enable ? "on" : "off");
}


bool
LEManager::Pair(const bdaddr_t& address, const BMessenger& reply)
{
	BAutolock _(fLock);
	Seen* seen = _FindSeen(address);
	if (seen == NULL)
		return false;
	for (size_t i = 0; i < fClassic.size(); i++) {
		if (bdaddrUtils::Compare(fClassic[i], address))
			return false;
	}

	if (!fRadioEnabled || !fReady) {
		syslog(LOG_NOTICE, "bluetooth_server: hci%" B_PRId32
			" le pairing not started (radio not ready)", fDevice->GetID());
		BMessenger answer(reply);
		r5_pairing_result(false, seen->name, &answer, address);
		return true;
	}

	// One pairing at a time: pressing Pair again while it runs does not
	// start it over (the first stick logged 18 starts in 14 s).
	Link* running = _FindLink(address);
	if ((fWantDirect && bdaddrUtils::Compare(fDirectAddress, address))
		|| (running != NULL && running->pairing
			&& system_time() - running->smpStarted < 30000000))
		return true;
	fConnectRetries = 0;

	// Pair means a new pairing: the old keys go.
	for (size_t i = 0; i < fBonds.size(); i++) {
		if (bdaddrUtils::Compare(fBonds[i]->address, address)) {
			memset(fBonds[i]->ltk, 0, sizeof(fBonds[i]->ltk));
			delete fBonds[i];
			fBonds.erase(fBonds.begin() + i);
			_SaveBonds();
			break;
		}
	}
	syslog(LOG_INFO, "bluetooth_server: hci%" B_PRId32 " le pairing started"
		" (advertising type %#x, %s report, address type %u, flags %#x,"
		" primary phy %u)", fDevice->GetID(), seen->eventType,
		seen->extendedReport ? "extended" : "legacy", seen->hciAddressType,
		seen->flags, seen->primaryPhy);

	Link* link = _FindLink(address);
	if (link != NULL) {
		link->pairRequested = true;
		link->pairReply = reply;
		link->resultPending = true;
		_SMPStart(link);
		return true;
	}

	// Pair on another device while one connects: the first page gets its
	// result, and its connect attempt makes way.
	if (fWantDirect)
		_DirectResult(false);
	fWantDirect = true;
	fDirectAddress = address;
	// The address type as the report gave it (0 to 3): a random identity
	// address (3) must reach the controller as 3, not as plain random.
	fDirectType = seen->hciAddressType;
	fDirectReply = reply;
	fDirectResultPending = true;
	if (fInitiating == kAuto || fInitiating == kDirect)
		_CancelConnection();
	else
		_Connect();
	return true;
}


void
LEManager::AudioChanged(bool streaming)
{
	BAutolock _(fLock);
	if (streaming)
		_SetScan(false);
	else if (fScanning)
		_SetScan(true);
}


void
LEManager::RadioChanged(bool enabled)
{
	BAutolock _(fLock);
	fRadioEnabled = enabled;
	if (enabled) {
		if (fScanning)
			_SetScan(true);
		_Connect();
		return;
	}
	_SetScan(false);
	fWantDirect = false;
	if (fInitiating == kAuto || fInitiating == kDirect)
		_CancelConnection();
	while (!fLinks.empty())
		_CloseLink(fLinks.back(), true);
}


void
LEManager::_Connect()
{
	if (!fReady || !fRadioEnabled || fInitiating != kIdle)
		return;

	// A new pairing changed the accept list; it can only be written while
	// nothing initiates.
	if (fAcceptListDirty) {
		fAcceptListDirty = false;
		// The controller refuses the resolving list while it scans
		// (7.8.38, 7.8.44, 0x0C): a scan pauses around the rewrite.
		const bool resume = fPrivacy && fScanOn;
		if (resume)
			_SetScan(false);
		_ResolvingListSetup();
		_AcceptListSetup();
		if (resume)
			_SetScan(true);
	}

	if (fWantDirect) {
		fInitiating = kDirect;
		fDirectStarted = system_time();
		_CreateConnection(&fDirectAddress, fDirectType);
		return;
	}

	// A paired device that is not connected comes back by itself: the
	// controller connects to any device on the accept list that
	// advertises.
	bool wanted = false;
	for (size_t i = 0; i < fBonds.size() && !wanted; i++) {
		if (_FindLink(fBonds[i]->address) == NULL)
			wanted = true;
	}
	if (!wanted)
		return;
	fInitiating = kAuto;
	_CreateConnection(NULL, 0);
}


void
LEManager::_CreateConnection(const bdaddr_t* address, uint8 addressType)
{
	const uint8 filter = address == NULL ? 0x01 : 0x00;
	// Peer_Address_Type 0 to 3 (7.8.12, 7.8.66): what the report said.
	uint8 type = addressType & 0x03;
	bdaddr_t peer;
	memset(&peer, 0, sizeof(peer));
	if (address != NULL)
		peer = *address;

	// The last retry of a pairing connect goes the legacy way, in case the
	// extended initiator is what fails (outside diagnosis of the first
	// sticks); the controller may refuse it once extended commands ran.
	// Legacy only on a controller without extended advertising: after
	// extended commands a legacy one is refused with 0x0C.
	const bool legacy = !fExtended;
	syslog(LOG_INFO, "bluetooth_server: hci%" B_PRId32 " le connect via %s",
		fDevice->GetID(), legacy ? "legacy" : "extended");
	if (!legacy) {
		// Filter policy, own address type, peer address type, peer
		// address, PHYs (1M), then for 1M: scan interval and window,
		// interval min and max, latency, timeout, CE length min and max.
		uint8 command[10 + 16];
		command[0] = filter;
		command[1] = 0x00;
		command[2] = type;
		memcpy(command + 3, &peer, 6);
		command[9] = 0x01;
		uint8* phy = command + 10;
		le_put16(phy + 0, kScanInterval);
		le_put16(phy + 2, kConnectScanWindow);
		le_put16(phy + 4, kConnIntervalMin);
		le_put16(phy + 6, kConnIntervalMax);
		le_put16(phy + 8, kConnLatency);
		le_put16(phy + 10, kConnTimeout);
		le_put16(phy + 12, 0);
		le_put16(phy + 14, 0);
		_Issue(kOpLEExtCreateConnection, command, sizeof(command));
	} else {
		// Scan interval and window, filter policy, peer address type and
		// address, own address type, interval min and max, latency,
		// timeout, CE length min and max.
		uint8 command[25];
		le_put16(command + 0, kScanInterval);
		le_put16(command + 2, kConnectScanWindow);
		command[4] = filter;
		command[5] = type;
		memcpy(command + 6, &peer, 6);
		command[12] = 0x00;
		le_put16(command + 13, kConnIntervalMin);
		le_put16(command + 15, kConnIntervalMax);
		le_put16(command + 17, kConnLatency);
		le_put16(command + 19, kConnTimeout);
		le_put16(command + 21, 0);
		le_put16(command + 23, 0);
		_Issue(kOpLECreateConnection, command, sizeof(command));
	}
	syslog(LOG_INFO, "bluetooth_server: hci%" B_PRId32 " le connect started"
		" (%s)", fDevice->GetID(), address != NULL ? "pairing" : "paired devices");
}


void
LEManager::_CancelConnection()
{
	if (fInitiating != kAuto && fInitiating != kDirect)
		return;
	fInitiating = kCancelling;
	_Issue(kOpLECreateConnectionCancel, NULL, 0);
}


void
LEManager::_AcceptListSetup()
{
	// Only while nothing initiates with it (7.8.16).
	_Issue(kOpLEClearAcceptList, NULL, 0);
	for (size_t i = 0; i < fBonds.size(); i++) {
		uint8 entry[7];
		entry[0] = fBonds[i]->addressType == R5_BDADDR_LE_RANDOM ? 1 : 0;
		memcpy(entry + 1, &fBonds[i]->address, 6);
		_Issue(kOpLEAddAcceptList, entry, sizeof(entry));
	}
}


void
LEManager::_ResolvingListSetup()
{
	// The IRKs of paired devices, so the controller knows them under their
	// private addresses (7.8.38); only while nothing initiates, with
	// resolution off while the list changes (7.8.44).
	if (!fPrivacy)
		return;
	const uint8 off = 0;
	_Issue(kOpLESetResolutionEnable, &off, 1);
	_Issue(kOpLEClearResolvingList, NULL, 0);
	bool any = false;
	for (size_t i = 0; i < fBonds.size(); i++) {
		const LEBond* bond = fBonds[i];
		if (!bond->hasIRK)
			continue;
		uint8 entry[1 + 6 + 16 + 16];
		entry[0] = bond->addressType == R5_BDADDR_LE_RANDOM ? 1 : 0;
		memcpy(entry + 1, &bond->address, 6);
		memcpy(entry + 7, bond->irk, 16);
		memset(entry + 23, 0, 16);	// we use our public address
		_Issue(kOpLEAddResolvingList, entry, sizeof(entry));
		memset(entry, 0, sizeof(entry));
		any = true;
	}
	if (any) {
		const uint8 on = 1;
		_Issue(kOpLESetResolutionEnable, &on, 1);
	}
}


void
LEManager::NoteClassic(const bdaddr_t& address)
{
	BAutolock _(fLock);
	for (size_t i = 0; i < fClassic.size(); i++) {
		if (bdaddrUtils::Compare(fClassic[i], address))
			return;
	}
	if (fClassic.size() >= kMaxSeen)
		fClassic.erase(fClassic.begin());
	fClassic.push_back(address);
}


void
LEManager::_StartEncryption(Link* link, const uint8 ltk[16], uint16 ediv,
	const uint8 rand[8])
{
	// Handle, random (8), EDIV, LTK (7.8.24).
	uint8 command[2 + 8 + 2 + 16];
	le_put16(command, link->handle);
	memcpy(command + 2, rand, 8);
	le_put16(command + 10, ediv);
	memcpy(command + 12, ltk, 16);
	_Issue(kOpLEEnableEncryption, command, sizeof(command));
	memset(command, 0, sizeof(command));
}


// #pragma mark - links


LEManager::Link*
LEManager::_FindLink(uint16 handle)
{
	for (size_t i = 0; i < fLinks.size(); i++) {
		if (fLinks[i]->handle == handle)
			return fLinks[i];
	}
	return NULL;
}


LEManager::Link*
LEManager::_FindLink(const bdaddr_t& address)
{
	for (size_t i = 0; i < fLinks.size(); i++) {
		if (bdaddrUtils::Compare(fLinks[i]->address, address))
			return fLinks[i];
	}
	return NULL;
}


void
LEManager::_CloseLink(Link* link, bool disconnect)
{
	// A pairing started here ends with this link at the latest.
	_PairResult(link, false);
	if (disconnect) {
		// Remote User Terminated Connection.
		const uint8 command[3] = { (uint8)(link->handle & 0xff),
			(uint8)(link->handle >> 8), 0x13 };
		_Issue(kOpDisconnect, command, sizeof(command));
	}
	if (link->kernelID >= 0) {
		HIDHost* host = ((BluetoothServer*)be_app)->GetHIDHost();
		if (host != NULL)
			host->DestroyLEDevice(link->kernelID);
		link->kernelID = -1;
	}
	_SMPCleanup(link);
	for (size_t i = 0; i < fLinks.size(); i++) {
		if (fLinks[i] == link) {
			fLinks.erase(fLinks.begin() + i);
			break;
		}
	}
	delete link;
}


LEBond*
LEManager::_FindBond(const bdaddr_t& address)
{
	for (size_t i = 0; i < fBonds.size(); i++) {
		if (bdaddrUtils::Compare(fBonds[i]->address, address))
			return fBonds[i];
	}
	return NULL;
}


LEManager::Seen*
LEManager::_FindSeen(const bdaddr_t& address)
{
	for (size_t i = 0; i < fSeen.size(); i++) {
		if (bdaddrUtils::Compare(fSeen[i]->address, address))
			return fSeen[i];
	}
	return NULL;
}


// #pragma mark - paired devices


void
LEManager::_LoadBonds()
{
	BPath path;
	if (r5_bluetooth_settings_path(path, false) != B_OK
		|| path.Append(kBondsFile) != B_OK)
		return;
	BFile file(path.Path(), B_READ_ONLY);
	BMessage archive;
	if (file.InitCheck() != B_OK || archive.Unflatten(&file) != B_OK)
		return;

	BMessage entry;
	for (int32 i = 0; archive.FindMessage("bond", i, &entry) == B_OK; i++) {
		const void* data;
		ssize_t size;
		LEBond* bond = new(std::nothrow) LEBond;
		if (bond == NULL)
			break;
		memset(bond->irk, 0, sizeof(bond->irk));
		bool ok = entry.FindData("address", B_RAW_TYPE, &data, &size) == B_OK
			&& size == 6;
		if (ok)
			memcpy(&bond->address, data, 6);
		ok = ok && entry.FindData("ltk", B_RAW_TYPE, &data, &size) == B_OK
			&& size == 16;
		if (ok)
			memcpy(bond->ltk, data, 16);
		ok = ok && entry.FindData("rand", B_RAW_TYPE, &data, &size) == B_OK
			&& size == 8;
		if (ok)
			memcpy(bond->rand, data, 8);
		ok = ok && entry.FindUInt8("type", &bond->addressType) == B_OK
			&& entry.FindUInt16("ediv", &bond->ediv) == B_OK
			&& entry.FindUInt8("keysize", &bond->keySize) == B_OK;
		if (!ok) {
			memset(bond->ltk, 0, sizeof(bond->ltk));
			delete bond;
			continue;
		}
		bond->secureConnections = entry.GetBool("sc", false);
		bond->hid = entry.GetBool("hid", false);
		bond->name = entry.GetString("name", "");
		bond->hasIRK = entry.FindData("irk", B_RAW_TYPE, &data, &size) == B_OK
			&& size == 16;
		if (bond->hasIRK)
			memcpy(bond->irk, data, 16);
		fBonds.push_back(bond);
	}
	// The flattened copy held keys too.
	archive.MakeEmpty();
}


void
LEManager::_SaveBonds()
{
	BPath directory;
	if (r5_bluetooth_settings_path(directory, true) != B_OK)
		return;
	BPath path(directory);
	BPath temporary(directory);
	if (path.Append(kBondsFile) != B_OK
		|| temporary.Append("le_paired_devices.new") != B_OK)
		return;

	BMessage archive;
	for (size_t i = 0; i < fBonds.size(); i++) {
		const LEBond* bond = fBonds[i];
		BMessage entry;
		entry.AddData("address", B_RAW_TYPE, &bond->address, 6);
		entry.AddUInt8("type", bond->addressType);
		entry.AddData("ltk", B_RAW_TYPE, bond->ltk, 16);
		entry.AddUInt16("ediv", bond->ediv);
		entry.AddData("rand", B_RAW_TYPE, bond->rand, 8);
		entry.AddUInt8("keysize", bond->keySize);
		entry.AddBool("sc", bond->secureConnections);
		entry.AddBool("hid", bond->hid);
		entry.AddString("name", bond->name);
		if (bond->hasIRK)
			entry.AddData("irk", B_RAW_TYPE, bond->irk, 16);
		archive.AddMessage("bond", &entry);
	}

	// Created 0600 before anything is written, so the keys are never
	// readable by others, not even for a moment.
	int fd = open(temporary.Path(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
		S_IRUSR | S_IWUSR);
	if (fd < 0) {
		syslog(LOG_ERR, "bluetooth_server: cannot save le paired devices: %s",
			strerror(errno));
		return;
	}
	fchmod(fd, S_IRUSR | S_IWUSR);
	BMallocIO flat;
	status_t status = archive.Flatten(&flat);
	archive.MakeEmpty();
	if (status == B_OK) {
		const ssize_t length = flat.BufferLength();
		if (write(fd, flat.Buffer(), length) != length || fsync(fd) != 0)
			status = B_IO_ERROR;
	}
	// The flattened keys, wiped before the memory goes back.
	memset((void*)flat.Buffer(), 0, flat.BufferLength());
	close(fd);
	if (status != B_OK || rename(temporary.Path(), path.Path()) != 0) {
		unlink(temporary.Path());
		syslog(LOG_ERR, "bluetooth_server: cannot save le paired devices");
	}
}


// #pragma mark - fixed channels


/*static*/ void
LEManager::StartChannels()
{
	const uint16 cids[2] = { R5_L2CAP_CID_ATT, R5_L2CAP_CID_SMP };
	for (int i = 0; i < 2; i++) {
		if (sChannel[i] >= 0)
			continue;
		int fd = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BLUETOOTH_PROTO_L2CAP);
		if (fd < 0) {
			syslog(LOG_WARNING, "bluetooth_server: no le channel socket: %s",
				strerror(errno));
			return;
		}
		sockaddr_l2cap_le address;
		memset(&address, 0, sizeof(address));
		address.l2cap_len = sizeof(address);
		address.l2cap_family = AF_BLUETOOTH;
		address.l2cap_bdaddr = BDADDR_ANY;
		address.l2cap_cid = cids[i];
		if (bind(fd, (sockaddr*)&address, sizeof(address)) != 0) {
			syslog(LOG_WARNING, "bluetooth_server: le channel %#x not bound:"
				" %s", cids[i], strerror(errno));
			close(fd);
			continue;
		}
		sChannel[i] = fd;
		sChannelThread[i] = spawn_thread(_ChannelThread,
			i == 0 ? "bt le att" : "bt le smp", B_URGENT_DISPLAY_PRIORITY,
			(void*)(addr_t)i);
		if (sChannelThread[i] >= 0)
			resume_thread(sChannelThread[i]);
	}
	syslog(LOG_INFO, "bluetooth_server: le channels %s",
		sChannel[0] >= 0 && sChannel[1] >= 0 ? "ready" : "missing");
}


/*static*/ void
LEManager::StopChannels()
{
	for (int i = 0; i < 2; i++) {
		if (sChannel[i] < 0)
			continue;
		shutdown(sChannel[i], SHUT_RDWR);
		close(sChannel[i]);
		sChannel[i] = -1;
		if (sChannelThread[i] >= 0) {
			status_t result;
			wait_for_thread(sChannelThread[i], &result);
			sChannelThread[i] = -1;
		}
	}
}


/*static*/ int32
LEManager::_ChannelThread(void* data)
{
	const int index = (int)(addr_t)data;
	const uint16 cid = index == 0 ? R5_L2CAP_CID_ATT : R5_L2CAP_CID_SMP;
	uint8 buffer[1100];
	while (true) {
		const int fd = sChannel[index];
		if (fd < 0)
			break;
		sockaddr_l2cap_le from;
		socklen_t fromLength = sizeof(from);
		memset(&from, 0, sizeof(from));
		ssize_t bytes = recvfrom(fd, buffer, sizeof(buffer), 0,
			(sockaddr*)&from, &fromLength);
		if (bytes < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (bytes == 0 || fromLength < (socklen_t)sizeof(from))
			continue;
		_Received(cid, from, buffer, bytes);
	}
	memset(buffer, 0, sizeof(buffer));
	return 0;
}


/*static*/ void
LEManager::_Received(uint16 cid, const sockaddr_l2cap_le& from,
	const uint8* pdu, size_t size)
{
	BAutolock managers(sManagersLock);
	for (size_t i = 0; i < sManagers.size(); i++) {
		LEManager* manager = sManagers[i];
		BAutolock _(manager->fLock);
		Link* link = manager->_FindLink(from.l2cap_bdaddr);
		if (link == NULL)
			continue;
		if (cid == R5_L2CAP_CID_SMP)
			manager->_SMPReceived(link, pdu, size);
		else
			manager->_ATTReceived(link, pdu, size);
		return;
	}
}


/*static*/ void
LEManager::HIDOutput(int32 kernelID, const uint8* data, size_t length,
	void* cookie)
{
	BAutolock managers(sManagersLock);
	for (size_t i = 0; i < sManagers.size(); i++) {
		BAutolock _(sManagers[i]->fLock);
		sManagers[i]->_HIDOutput(kernelID, data, length);
	}
}


// Sends one PDU on a fixed channel of the link (sendto, <R5BluetoothLE.h>).
static void
le_send_fixed(int index, uint16 cid, const bdaddr_t& address, uint8 type,
	const uint8* pdu, size_t size)
{
	if (sChannel[index] < 0)
		return;
	sockaddr_l2cap_le to;
	memset(&to, 0, sizeof(to));
	to.l2cap_len = sizeof(to);
	to.l2cap_family = AF_BLUETOOTH;
	to.l2cap_bdaddr = address;
	to.l2cap_cid = cid;
	to.l2cap_bdaddr_type = type;
	if (sendto(sChannel[index], pdu, size, 0, (sockaddr*)&to, sizeof(to)) < 0) {
		syslog(LOG_NOTICE, "bluetooth_server: le pdu on channel %#x not sent:"
			" %s", cid, strerror(errno));
	}
}


void
LEManager::_SMPSend(Link* link, const uint8* pdu, size_t size)
{
	link->smpLast = system_time();
	le_send_fixed(1, R5_L2CAP_CID_SMP, link->address, link->addressType, pdu,
		size);
}


void
LEManager::_ATTSend(Link* link, const uint8* pdu, size_t size)
{
	le_send_fixed(0, R5_L2CAP_CID_ATT, link->address, link->addressType, pdu,
		size);
}


// #pragma mark - pairing windows and timeouts


BString
LEManager::_Name(const bdaddr_t& address)
{
	Seen* seen = _FindSeen(address);
	return seen != NULL ? seen->name : BString();
}


void
LEManager::_SMPAskUser(Link* link, uint32 number, bool compare)
{
	// Numeric Comparison (yes or no) or Passkey Entry (the person types
	// the number the device shows). The pairing waits; the SMP timeout
	// still runs (Vol 3, Part H, 3.4).
	PasskeyWindow* window = new(std::nothrow) PasskeyWindow(link->address,
		fDevice->GetID(), _Name(link->address), number, compare, true);
	if (window == NULL) {
		_SMPFail(link, compare ? 0x0c : kSMPPasskeyEntryFailed);
		return;
	}
	if (link->passkeyWindow.IsValid())
		link->passkeyWindow.SendMessage(B_QUIT_REQUESTED);
	link->passkeyWindow = BMessenger(window);
	link->smpState = Link::kSMPWaitUser;
	syslog(LOG_INFO, "bluetooth_server: smp %s shown",
		compare ? "numeric comparison" : "passkey entry");
	window->Show();
}


/*static*/ void
LEManager::UserAnswer(BMessage* message)
{
	const void* data;
	ssize_t size;
	if (message->FindData("bdaddr", B_RAW_TYPE, &data, &size) != B_OK
		|| size != (ssize_t)sizeof(bdaddr_t))
		return;
	const bdaddr_t& address = *(const bdaddr_t*)data;
	const bool accept = message->GetBool("accept", false);
	const uint32 passkey = message->GetUInt32("passkey", 0);

	BAutolock managers(sManagersLock);
	for (size_t i = 0; i < sManagers.size(); i++) {
		LEManager* manager = sManagers[i];
		BAutolock _(manager->fLock);
		Link* link = manager->_FindLink(address);
		if (link != NULL) {
			manager->_UserAnswer(link, accept, passkey);
			return;
		}
	}
}


void
LEManager::_UserAnswer(Link* link, bool accept, uint32 passkey)
{
	if (link->smpState != Link::kSMPWaitUser)
		return;
	link->passkeyWindow = BMessenger();
	link->smpLast = system_time();

	const bool compare = link->method == Link::kNumericComparison;
	if (!accept || passkey > 999999) {
		// Numeric Comparison Failed (0x0C) or Passkey Entry Failed.
		_SMPFail(link, compare ? 0x0c : kSMPPasskeyEntryFailed);
		if (_FindBond(link->address) == NULL)
			_CloseLink(link, true);
		return;
	}
	if (compare) {
		_SMPSendDHKeyCheck(link);
		return;
	}
	link->passkey = passkey;
	_SMPSendConfirm(link);
}


/*static*/ void
LEManager::Pulse()
{
	BAutolock managers(sManagersLock);
	for (size_t i = 0; i < sManagers.size(); i++) {
		BAutolock _(sManagers[i]->fLock);
		sManagers[i]->_Pulse();
	}
}


void
LEManager::_Pulse()
{
	// SMP and ATT both end a procedure that got no answer in 30 s, and the
	// link with it (Vol 3, Part H, 3.4; Part F, 3.3.3).
	const bigtime_t now = system_time();
	const bigtime_t limit = 30000000;

	// A device that stopped advertising: the controller would try for
	// ever, and paired mice and keyboards could not come back meanwhile.
	if (fInitiating == kDirect && fDirectStarted != 0
		&& now - fDirectStarted > limit) {
		syslog(LOG_NOTICE, "bluetooth_server: hci%" B_PRId32
			" le pairing connect timed out", fDevice->GetID());
		fWantDirect = false;
		_DirectResult(false);
		_CancelConnection();
	}

	std::vector<Link*> expired;
	for (size_t i = 0; i < fLinks.size(); i++) {
		Link* link = fLinks[i];
		const bool smpWaits = link->pairing
			&& link->smpState != Link::kSMPDone
			&& link->smpState != Link::kSMPFailed && link->smpLast != 0
			&& now - link->smpLast > limit;
		const bool attWaits = link->requestPending && link->attLast != 0
			&& now - link->attLast > limit;
		if (smpWaits) {
			syslog(LOG_NOTICE, "bluetooth_server: smp pairing timed out");
			_PairResult(link, false);
			_SMPCleanup(link);
			link->pairing = false;
			link->smpState = Link::kSMPFailed;
			expired.push_back(link);
		} else if (attWaits) {
			syslog(LOG_NOTICE, "bluetooth_server: gatt request timed out");
			expired.push_back(link);
		}
	}
	for (size_t i = 0; i < expired.size(); i++)
		_CloseLink(expired[i], true);
	if (!expired.empty())
		_Connect();
}


void
LEManager::_PairResult(Link* link, bool paired)
{
	if (!link->resultPending)
		return;
	link->resultPending = false;
	r5_pairing_result(paired, _Name(link->address), &link->pairReply,
		link->address);
}


void
LEManager::_DirectResult(bool paired)
{
	if (!fDirectResultPending)
		return;
	fDirectResultPending = false;
	r5_pairing_result(paired, _Name(fDirectAddress), &fDirectReply,
		fDirectAddress);
}


bool
LEManager::Forget(const bdaddr_t& address)
{
	BAutolock _(fLock);
	bool found = false;
	for (size_t i = 0; i < fBonds.size(); i++) {
		if (!bdaddrUtils::Compare(fBonds[i]->address, address))
			continue;
		memset(fBonds[i]->ltk, 0, sizeof(fBonds[i]->ltk));
		memset(fBonds[i]->irk, 0, sizeof(fBonds[i]->irk));
		delete fBonds[i];
		fBonds.erase(fBonds.begin() + i);
		_SaveBonds();
		fAcceptListDirty = true;
		found = true;
		break;
	}
	Link* link = _FindLink(address);
	if (link != NULL) {
		_CloseLink(link, true);
		found = true;
	}
	if (found) {
		// The accept list is written again once nothing initiates.
		if (fInitiating == kAuto)
			_CancelConnection();
		_Connect();
	}
	return found;
}


void
LEManager::AddPaired(BMessage& reply)
{
	BAutolock _(fLock);
	for (size_t i = 0; i < fBonds.size(); i++) {
		const LEBond* bond = fBonds[i];
		bool connected = false;
		int32 battery = -1;
		for (size_t j = 0; j < fLinks.size() && !connected; j++) {
			const Link* link = fLinks[j];
			connected = link->encrypted
				&& (bdaddrUtils::Compare(link->address, bond->address)
					|| bdaddrUtils::Compare(link->bond.address, bond->address));
			if (connected)
				battery = link->battery;
		}
		reply.AddData("bdaddr", B_ANY_TYPE, &bond->address,
			sizeof(bond->address));
		reply.AddString("name", bond->name);
		reply.AddUInt32("class", 0);
		reply.AddBool("le", true);
		reply.AddBool("hid", bond->hid);
		reply.AddBool("connected", connected);
		reply.AddInt32("battery", battery);
	}
}


bool
LEManager::Disconnect(const bdaddr_t& address)
{
	BAutolock _(fLock);
	for (size_t i = 0; i < fLinks.size(); i++) {
		Link* link = fLinks[i];
		if (bdaddrUtils::Compare(link->address, address)
			|| bdaddrUtils::Compare(link->bond.address, address)) {
			_CloseLink(link, true);
			return true;
		}
	}
	return false;
}
