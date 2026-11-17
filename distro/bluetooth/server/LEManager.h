/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Bluetooth Low Energy in bluetooth_server (docs/BLUETOOTH.md, step 6):
 * the LE part of the controller setup after every reset, the LE scan
 * shown in the scan list, LE links (bluetooth_server is always the
 * central), LE Secure Connections and legacy pairing over SMP
 * (LEManagerSMP.cpp), and a GATT client that finds the HID service and
 * feeds the bluetooth_hid driver (LEManagerGATT.cpp).
 *
 * One LEManager per radio. ATT and SMP come through the two fixed-channel
 * sockets of <R5BluetoothLE.h>, which all radios share. The keys are in
 * /boot/system/settings/bluetooth/le_paired_devices, mode 0600, and are
 * never logged; neither are addresses.
 */
#ifndef R5_LE_MANAGER_H
#define R5_LE_MANAGER_H


#include <deque>
#include <vector>

#include <Locker.h>
#include <Messenger.h>
#include <String.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/HCI/btHCI_event.h>

#include <R5BluetoothLE.h>

#include "SMPCrypto.h"
#include "scan_log.h"


class LocalDeviceImpl;


// A paired LE device (le_paired_devices).
struct LEBond {
	bdaddr_t	address;		// identity address
	uint8		addressType;	// R5_BDADDR_LE_*
	uint8		ltk[16];
	uint16		ediv;
	uint8		rand[8];
	uint8		keySize;
	bool		secureConnections;
	bool		hasIRK;
	uint8		irk[16];
	bool		hid;
	BString		name;
};


class LEManager {
public:
								LEManager(LocalDeviceImpl* device);
								~LEManager();

	// The LMP feature "LE Supported (Controller)": the Event Mask gets
	// LE Meta only then.
	static	bool				Supported(const uint8 lmpFeatures[8]);

	// After the Event Mask, following every reset.
			void				Setup(const uint8 lmpFeatures[8]);
	// Every HCI event of this radio comes here first; true when it was
	// an LE event or the answer to an LE command, which the rest of
	// LocalDeviceImpl then does not see.
			bool				HandleEvent(const hci_event_header* event,
									size_t size);

	// With the BR/EDR inquiry: devices go to the same scan list as
	// BT_MSG_INQUIRY_DEVICE messages to "target".
			void				StartScan(const BMessenger& target);
			void				StopScan();
	// A Create Connection for a device the LE scan found pairs it here;
	// false when the address is not one of them.
			bool				Pair(const bdaddr_t& address,
									const BMessenger& reply);
	// A device the Classic inquiry found: Pair uses Classic for it, also
	// when it advertises on LE with the same address (dual mode).
			void				NoteClassic(const bdaddr_t& address);
	// Pair() sent this address to Classic because its LE advertisement
	// says it is dual-mode: its LE name ("LE_...") is not its Classic one.
			bool				DualModeAdvert(const bdaddr_t& address);
			void				RadioChanged(bool enabled);
	// Removes the keys of a paired LE device and ends its link.
			bool				Forget(const bdaddr_t& address);
	// BT_MSG_R5_GET_PAIRED: one entry per bond, without keys.
			void				AddPaired(BMessage& reply);
	// Ends the link to a device; its bond stays.
			bool				Disconnect(const bdaddr_t& address);
			// bluetooth_audio starts or stops a stream: the scan pauses
			// meanwhile (docs/BLUETOOTH.md, step 5).
			void				AudioChanged(bool streaming);

	// From the fixed-channel sockets and the bluetooth_hid driver.
	// The answer of a pairing window (kMsgLEUserAnswer), and the timer
	// for the SMP and ATT timeouts (every few seconds).
	static	void				UserAnswer(BMessage* message);
	static	void				Pulse();
	static	void				StartChannels();
	static	void				StopChannels();
	static	void				HIDOutput(int32 kernelID, const uint8* data,
									size_t length, void* cookie);

private:
			struct Link;
			struct Report;
			struct Seen;

	// HCI commands, one at a time (_Issue() queues).
			void				_Issue(uint16 opcode, const void* parameters,
									size_t length);
			void				_SendNext();
			void				_CommandDone(uint16 opcode, uint8 status,
									const uint8* data, size_t length);
			void				_SetupStep(int step);

			void				_LEMeta(const uint8* data, size_t size);
			void				_AdvertisingReport(const uint8* data,
									size_t size, bool extended);
			void				_LogHeard(const Seen& seen,
									const char* notListed);
			void				_ConnectionComplete(const uint8* data,
									size_t size, uint8 subevent);
			void				_Disconnected(uint16 handle, uint8 reason);
			void				_EncryptionChanged(uint16 handle, uint8 status,
									bool enabled);

			void				_SetScan(bool enable);
			void				_Connect();
			void				_CreateConnection(const bdaddr_t* address,
									uint8 addressType);
			void				_CancelConnection();
			void				_AcceptListSetup();
			void				_ResolvingListSetup();
			void				_StartEncryption(Link* link, const uint8 ltk[16],
									uint16 ediv, const uint8 rand[8]);

			Link*				_FindLink(uint16 handle);
			Link*				_FindLink(const bdaddr_t& address);
			void				_CloseLink(Link* link, bool disconnect);
			LEBond*				_FindBond(const bdaddr_t& address);
			Seen*				_FindSeen(const bdaddr_t& address);

	// Keys (LEManager.cpp)
			void				_LoadBonds();
			void				_SaveBonds();

	// SMP (LEManagerSMP.cpp)
			void				_SMPReceived(Link* link, const uint8* pdu,
									size_t size);
			void				_SMPStart(Link* link);
			void				_SMPSend(Link* link, const uint8* pdu,
									size_t size);
			void				_SMPFail(Link* link, uint8 reason);
			void				_SMPPairingResponse(Link* link,
									const uint8* pdu);
			void				_SMPPublicKey(Link* link, const uint8* pdu);
			void				_SMPConfirm(Link* link, const uint8* pdu);
			void				_SMPRandom(Link* link, const uint8* pdu);
			void				_SMPDHKeyCheck(Link* link, const uint8* pdu);
			void				_SMPSendConfirm(Link* link);
			void				_SMPSendDHKeyCheck(Link* link);
			void				_SMPAskUser(Link* link, uint32 number,
									bool compare);
			void				_UserAnswer(Link* link, bool accept,
									uint32 passkey);
			void				_Pulse();
			BString				_Name(const bdaddr_t& address);
			void				_PairResult(Link* link, bool paired);
			void				_DirectResult(bool paired);
			void				_LinkReady(Link* link);
			void				_SMPKeyDistribution(Link* link,
									const uint8* pdu, size_t size);
			void				_SMPEncrypted(Link* link);
			void				_SMPFinish(Link* link);
			void				_SMPAddresses(Link* link, uint8 a[7],
									uint8 b[7]);
			void				_SMPShowPasskey(Link* link);
			void				_SMPCleanup(Link* link);

	// GATT (LEManagerGATT.cpp)
			void				_ATTReceived(Link* link, const uint8* pdu,
									size_t size);
			void				_ATTSend(Link* link, const uint8* pdu,
									size_t size);
			void				_ATTRequest(Link* link, const uint8* pdu,
									size_t size);
			void				_ATTServerRequest(Link* link,
									const uint8* pdu, size_t size);
			void				_GATTStart(Link* link);
			void				_GATTStep(Link* link, const uint8* pdu,
									size_t size);
			void				_GATTNext(Link* link);
			void				_GATTReady(Link* link);
			void				_HIDInput(Link* link, uint16 handle,
									const uint8* value, size_t length);
			void				_HIDOutput(int32 kernelID, const uint8* data,
									size_t length);

	static	int32				_ChannelThread(void* data);
	static	void				_Received(uint16 cid,
									const sockaddr_l2cap_le& from,
									const uint8* pdu, size_t size);

private:
			LocalDeviceImpl*	fDevice;
			BLocker				fLock;

			uint8				fLMPFeatures[8];
			uint8				fFeatures[8];		// LE local features
			bdaddr_t			fLocalAddress;
			bool				fReady;
			bool				fExtended;			// extended scan, connect
			bool				fPrivacy;			// resolving list

			std::deque<std::vector<uint8> > fQueue;
			uint16				fPending;			// opcode, 0: none

			BMessenger			fScanTarget;
			bool				fScanning;
			bool				fScanOn;			// as the radio has it
			std::vector<Seen*>	fSeen;
			// One line per device a scan heard (scan_log.h).
			scan_log			fScanLog;

			// Initiating: at most one LE Create Connection runs.
			enum {
				kIdle,
				kAuto,			// to the accept list (paired devices)
				kDirect,		// to the device being paired
				kCancelling
			}					fInitiating;
			bool				fWantDirect;
			bdaddr_t			fDirectAddress;
			uint8				fDirectType;
			bigtime_t			fDirectStarted;
				// LE Create Connection has no timeout of its own

			std::vector<Link*>	fLinks;
			std::vector<LEBond*> fBonds;
			bool				fRadioEnabled;
			bool				fAcceptListDirty;
			std::vector<bdaddr_t> fClassic;
			int					fConnectRetries;
			BMessenger			fDirectReply;
			bool				fDirectResultPending;
};


#endif	// R5_LE_MANAGER_H
