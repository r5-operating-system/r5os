/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * The voice link of a radio (docs/BLUETOOTH.md, step 7c, the part that
 * joins HFP and the voice path): Setup Synchronous Connection with the
 * settings of HFP 1.8, table 5.8, in turn; Accept Synchronous Connection
 * Request for a link the headset opens while R5 wants one, Reject
 * Synchronous Connection Request otherwise; the end of the link. Written
 * from Core 5.4, Vol 4, Part E (7.1.6, 7.1.26 to 7.1.28, 7.7.4, 7.7.5,
 * 7.7.15, 7.7.35). The kernel (h2generic) starts and stops the voice
 * transfers by itself from the same events. No address is logged.
 */


#include "LocalDeviceImpl.h"

#include <string.h>
#include <syslog.h>

#include <bluetooth/HCI/btHCI.h>
#include <bluetooth/HCI/btHCI_event.h>
#include <bluetooth/bluetooth_error.h>

#include "VoiceLink.h"


static const uint16 kNoVoiceHandle = 0xffff;
static const uint16 kOpDisconnect = 0x0406;
static const uint16 kOpRejectSynchronousConnection = 0x042a;
// Synchronous Connection Complete: status, handle, address, link type,
// interval, window, two lengths and the air mode.
static const size_t kSyncCompleteSize = 17;


static const char*
setting_name(int setting)
{
	static const char* const kNames[] = { "T2", "T1", "S4", "S3", "S1",
		"D1" };
	return setting >= 0 && setting < 6 ? kNames[setting] : "?";
}


static const char*
codec_name(int codec)
{
	return codec == voice::kCodecMSBC ? "mSBC" : "CVSD";
}


status_t
LocalDeviceImpl::R5VoiceOpen(const bdaddr_t& address, int codec)
{
	ServerRemoteDevice* rd = RemoteDeviceByAddr(address);
	if (rd == NULL || rd->conn_state != RemoteDevice::CONNECTED
		|| rd->handle == ServerRemoteDevice::kNoHandle)
		return B_NOT_ALLOWED;
	if (fVoiceHandle != kNoVoiceHandle || fVoicePending)
		return B_BUSY;

	voice::Setting settings[4];
	int count = voice::Fallbacks(codec, true, settings, 4);
	if (count <= 0)
		return B_NOT_SUPPORTED;
	fVoiceAddress = address;
	fVoiceWanted = true;
	fVoiceCodec = codec;
	fVoiceCount = count;
	for (int i = 0; i < count; i++)
		fVoiceSettings[i] = settings[i];
	fVoiceIndex = -1;
	fVoiceAcl = rd->handle;
	fVoicePending = true;
	_R5VoiceSetupNext();
	return B_OK;
}


void
LocalDeviceImpl::R5VoiceClose(const bdaddr_t& address)
{
	if (memcmp(&address, &fVoiceAddress, sizeof(address)) != 0)
		return;
	fVoiceWanted = false;
	// A setup on its way is ended when it completes.
	if (fVoiceHandle == kNoVoiceHandle)
		return;
	uint8 command[3 + 3];
	command[0] = kOpDisconnect & 0xff;
	command[1] = kOpDisconnect >> 8;
	command[2] = 3;
	command[3] = fVoiceHandle & 0xff;
	command[4] = (fVoiceHandle >> 8) & 0x0f;
	command[5] = BT_REMOTE_USER_ENDED_CONNECTION;
	if (R5IssueCommand(command, sizeof(command)) != B_OK)
		syslog(LOG_NOTICE, "bluetooth_server: voice: cannot end the link");
}


void
LocalDeviceImpl::_R5VoiceSetupNext()
{
	while (++fVoiceIndex < fVoiceCount) {
		voice::Parameters parameters;
		if (!voice::GetParameters((voice::Setting)fVoiceSettings[fVoiceIndex],
				&parameters))
			continue;
		uint8 command[voice::kSetupSize];
		if (voice::BuildSetup(fVoiceAcl, parameters, command,
				sizeof(command)) == 0)
			continue;
		syslog(LOG_INFO, "bluetooth_server: voice: setting up %s (%s)",
			codec_name(fVoiceCodec),
			setting_name(fVoiceSettings[fVoiceIndex]));
		if (R5IssueCommand(command, sizeof(command)) == B_OK)
			return;
	}
	fVoicePending = false;
	syslog(LOG_NOTICE, "bluetooth_server: voice: no setting worked");
}


void
LocalDeviceImpl::_R5VoiceRequest(const uint8* data)
{
	// Connection Request: address, class of device, link type.
	bdaddr_t address;
	memcpy(&address, data, sizeof(address));
	const uint8 linkType = data[9];
	const bool wanted = fVoiceWanted && fVoiceHandle == kNoVoiceHandle
		&& memcmp(&address, &fVoiceAddress, sizeof(address)) == 0;

	if (wanted) {
		// eSCO (link type 2) with the codec's first setting, SCO with D1.
		voice::Setting setting = voice::kSettingD1;
		if (linkType == 0x02) {
			voice::Setting settings[4];
			if (voice::Fallbacks(fVoiceCodec, true, settings, 4) > 0)
				setting = settings[0];
		}
		voice::Parameters parameters;
		uint8 command[voice::kAcceptSize];
		if (voice::GetParameters(setting, &parameters)
			&& voice::BuildAccept((const uint8*)&address, parameters,
				command, sizeof(command)) != 0
			&& R5IssueCommand(command, sizeof(command)) == B_OK) {
			fVoicePending = true;
			fVoiceCount = 0;
			fVoiceIndex = 0;
			syslog(LOG_INFO, "bluetooth_server: voice: the headset opens "
				"the link (%s, %s)", linkType == 0x02 ? "eSCO" : "SCO",
				setting_name(setting));
			return;
		}
	}

	uint8 command[3 + 7];
	command[0] = kOpRejectSynchronousConnection & 0xff;
	command[1] = kOpRejectSynchronousConnection >> 8;
	command[2] = 7;
	memcpy(command + 3, &address, 6);
	command[9] = BT_REJECTED_LIMITED_RESOURCES;
	R5IssueCommand(command, sizeof(command));
	syslog(LOG_INFO, "bluetooth_server: voice: a voice link request was "
		"turned down (no microphone use)");
}


bool
LocalDeviceImpl::_R5VoiceEvent(const struct hci_event_header* event)
{
	const uint8* data = (const uint8*)(event + 1);
	switch (event->ecode) {
		case HCI_EVENT_CONN_REQUEST:
			// Only voice requests; ACL requests go on as before.
			if (event->elen < 10 || data[9] == HCI_ACL_CONN)
				return false;
			_R5VoiceRequest(data);
			return true;

		case HCI_EVENT_SYNCHRONOUS_CONNECTION_COMPLETED:
		{
			if (event->elen < kSyncCompleteSize)
				return true;
			const uint8 status = data[0];
			const uint16 handle = (data[1] | (data[2] << 8)) & 0x0fff;
			if (status == BT_OK) {
				fVoicePending = false;
				fVoiceHandle = handle;
				syslog(LOG_INFO, "bluetooth_server: voice: link up (%s, %s, "
					"air mode %u)", data[9] == 0x02 ? "eSCO" : "SCO",
					codec_name(fVoiceCodec), data[16]);
				// The microphone was let go while the link came up.
				if (!fVoiceWanted)
					R5VoiceClose(fVoiceAddress);
				return true;
			}
			syslog(LOG_NOTICE, "bluetooth_server: voice: setup failed (%#x)",
				status);
			if (fVoicePending && fVoiceWanted)
				_R5VoiceSetupNext();
			else
				fVoicePending = false;
			return true;
		}

		case HCI_EVENT_DISCONNECTION_COMPLETE:
		{
			if (event->elen < 4 || fVoiceHandle == kNoVoiceHandle)
				return false;
			const uint16 handle = (data[1] | (data[2] << 8)) & 0x0fff;
			if (handle != fVoiceHandle || data[0] != BT_OK)
				return false;
			fVoiceHandle = kNoVoiceHandle;
			syslog(LOG_INFO, "bluetooth_server: voice: link down (%#x)",
				data[3]);
			return true;
		}

		case HCI_EVENT_CMD_STATUS:
		{
			// A setup or accept the radio refused at once: the next one.
			if (event->elen < 4)
				return false;
			const uint16 opcode = data[2] | (data[3] << 8);
			if (opcode != voice::kOpSetupSynchronousConnection
				&& opcode != voice::kOpAcceptSynchronousConnection
				&& opcode != kOpRejectSynchronousConnection)
				return false;
			if (data[0] != BT_OK && fVoicePending) {
				syslog(LOG_NOTICE, "bluetooth_server: voice: command "
					"refused (%#x)", data[0]);
				if (opcode == voice::kOpSetupSynchronousConnection
					&& fVoiceWanted)
					_R5VoiceSetupNext();
				else
					fVoicePending = false;
			}
			return true;
		}
	}
	return false;
}
