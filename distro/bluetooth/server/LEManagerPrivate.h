/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * What LEManager.cpp, LEManagerSMP.cpp and LEManagerGATT.cpp share.
 */
#ifndef R5_LE_MANAGER_PRIVATE_H
#define R5_LE_MANAGER_PRIVATE_H


#include "LEManager.h"

#include <syslog.h>


// HCI LE commands (OGF 0x08; Core 6.0, Vol 4, Part E, 7.8) and the few
// others the LE setup needs.
enum {
	kOpDisconnect				= 0x0406,
	kOpWriteLEHostSupport		= 0x0c6d,
	kOpReadBDAddr				= 0x1009,
	kOpLESetEventMask			= 0x2001,
	kOpLEReadBufferSize			= 0x2002,
	kOpLEReadFeatures			= 0x2003,
	kOpLESetScanParameters		= 0x200b,
	kOpLESetScanEnable			= 0x200c,
	kOpLECreateConnection		= 0x200d,
	kOpLECreateConnectionCancel	= 0x200e,
	kOpLEClearAcceptList		= 0x2010,
	kOpLEAddAcceptList			= 0x2011,
	kOpLEEnableEncryption		= 0x2019,
	kOpLELTKNegativeReply		= 0x201b,
	kOpLEConnParamReply			= 0x2020,
	kOpLEConnParamNegativeReply	= 0x2021,
	kOpLEWriteDataLength		= 0x2024,
	kOpLEAddResolvingList		= 0x2027,
	kOpLEClearResolvingList		= 0x2029,
	kOpLESetResolutionEnable	= 0x202d,
	kOpLESetDefaultPHY			= 0x2031,
	kOpLESetExtScanParameters	= 0x2041,
	kOpLESetExtScanEnable		= 0x2042,
	kOpLEExtCreateConnection	= 0x2043
};

// LE features (Vol 6, Part B, 4.6): bit numbers.
enum {
	kFeatureEncryption			= 0,
	kFeatureConnParamRequest	= 1,
	kFeatureDataLength			= 5,
	kFeaturePrivacy				= 6,
	kFeature2MPHY				= 8,
	kFeatureCodedPHY			= 11,
	kFeatureExtendedAdvertising	= 12
};

// SMP (Vol 3, Part H, 3.5 and 3.6).
enum {
	kSMPPairingRequest			= 0x01,
	kSMPPairingResponse			= 0x02,
	kSMPPairingConfirm			= 0x03,
	kSMPPairingRandom			= 0x04,
	kSMPPairingFailed			= 0x05,
	kSMPEncryptionInformation	= 0x06,
	kSMPCentralIdentification	= 0x07,
	kSMPIdentityInformation		= 0x08,
	kSMPIdentityAddress			= 0x09,
	kSMPSigningInformation		= 0x0a,
	kSMPSecurityRequest			= 0x0b,
	kSMPPublicKey				= 0x0c,
	kSMPDHKeyCheck				= 0x0d,
	kSMPKeypress				= 0x0e
};

// Pairing Failed reasons (3.5.5).
enum {
	kSMPPasskeyEntryFailed		= 0x01,
	kSMPAuthRequirements		= 0x03,
	kSMPConfirmFailed			= 0x04,
	kSMPPairingNotSupported		= 0x05,
	kSMPEncryptionKeySize		= 0x06,
	kSMPUnspecified				= 0x08,
	kSMPInvalidParameters		= 0x0a,
	kSMPDHKeyCheckFailed		= 0x0b
};

// Key distribution bits (3.6.1).
enum {
	kKeyEnc		= 0x01,
	kKeyId		= 0x02,
	kKeySign	= 0x04
};


struct LEManager::Report {
	uint16		valueHandle;
	uint16		cccd;			// Client Characteristic Configuration
	uint16		reference;		// Report Reference descriptor
	uint8		id;
	uint8		type;			// 1 input, 2 output, 3 feature
};


struct LEManager::Link {
	uint16		handle;
	bdaddr_t	address;
	uint8		addressType;	// R5_BDADDR_LE_*
	bool		encrypted;
	bool		pairRequested;	// made by Pair on the Bluetooth page

	// SMP
	enum {
		kSMPIdle,
		kSMPWaitResponse,
		kSMPWaitPublicKey,
		kSMPWaitConfirm,
		kSMPWaitRandom,
		kSMPWaitDHKeyCheck,
		kSMPWaitUser,		// a window waits for the person
		kSMPWaitEncryption,
		kSMPWaitKeys,
		kSMPDone,
		kSMPFailed
	}			smpState;
	bool		pairing;
	bigtime_t	smpStarted;
	bigtime_t	smpLast;		// last SMP PDU (30 s timeout)
	bool		UsesPasskey() const
					{ return method == kPasskeyDisplay
						|| method == kPasskeyEntry; }
	bool		secureConnections;
	enum {
		kJustWorks,
		kNumericComparison,
		kPasskeyDisplay,		// we show it, the device types it
		kPasskeyEntry			// the device shows it, the person types it
	}			method;
	uint8		preq[7];
	uint8		pres[7];
	SMPKeyPair	keyPair;
	uint8		peerPublic[64];
	uint8		dhKey[32];
	uint8		localNonce[16];
	uint8		peerNonce[16];
	uint8		peerConfirm[16];
	uint32		passkey;
	int			round;			// passkey bit, 0 to 19
	uint8		ltk[16];
	uint8		macKey[16];
	BMessenger	passkeyWindow;
	uint8		keySize;
	uint8		keysExpected;	// from the device, kKey* bits
	uint8		keysReceived;
	LEBond		bond;

	// ATT and GATT
	uint16		mtu;
	bool		requestPending;
	bigtime_t	attLast;		// when the pending request went
	int			gattStep;
	uint16		serviceStart;
	uint16		serviceEnd;
	uint16		next;			// where the running discovery goes on
	struct Characteristic {
		uint16	declaration;
		uint16	value;
		uint8	properties;
		uint16	uuid;
		uint16	cccd;
		uint16	reference;
	};
	std::vector<Characteristic> characteristics;
	size_t		index;
	std::vector<uint8> reportMap;
	std::vector<Report> reports;
	bool		usesReportIDs;
	int32		kernelID;
};


struct LEManager::Seen {
	bdaddr_t	address;
	uint8		addressType;
	BString		name;
	uint32		classOfDevice;
	int8		rssi;
	bool		connectable;
	bigtime_t	lastSeen;
	bigtime_t	lastSent;
};


static inline bool
le_has_feature(const uint8 features[8], int bit)
{
	return (features[bit / 8] & (1 << (bit % 8))) != 0;
}


static inline void
le_put16(uint8* to, uint16 value)
{
	to[0] = value & 0xff;
	to[1] = value >> 8;
}


#endif	// R5_LE_MANAGER_PRIVATE_H
