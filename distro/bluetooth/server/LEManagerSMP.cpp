/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * LE pairing over SMP, as the initiator (docs/BLUETOOTH.md, step 6;
 * Core 6.0, Vol 3, Part H): LE Secure Connections with ECDH P-256
 * (SMPCrypto, OpenSSL libcrypto), Just Works, Numeric Comparison and
 * Passkey Entry where this computer shows the number; legacy pairing for
 * devices without Secure Connections. Keys, nonces and the passkey are
 * never logged.
 */


#include "LEManagerPrivate.h"

#include <string.h>

#include <openssl/crypto.h>
#include <openssl/rand.h>

#include <bluetooth/bdaddrUtils.h>

#include "LocalDeviceImpl.h"
#include "PairingNotice.h"
#include "PasskeyWindow.h"
#include "le_parse.h"


// Our IO capability, KeyboardDisplay, as for Classic (step 4). Our
// AuthReq: bonding and Secure Connections. MITM is not asked for: a
// device that needs it asks itself, and a mouse without keys or display
// then still pairs with Just Works (2.3.5.1).
static const uint8 kIOCapability = 0x04;
static const uint8 kAuthReq = 0x01 | 0x08;
static const uint8 kAuthMITM = 0x04;
static const uint8 kAuthSC = 0x08;
// We ask for the device's LTK (legacy) and identity; we give none.
static const uint8 kResponderKeys = kKeyEnc | kKeyId;
static const uint8 kMinimumKeySize = 7;

// keysReceived: halves of a key pair seen so far.
static const uint8 kGotEncryptionInformation = 0x10;
static const uint8 kGotIdentityInformation = 0x20;


static bool
random_bytes(uint8* buffer, size_t size)
{
	return RAND_bytes(buffer, (int)size) == 1;
}


void
LEManager::_SMPCleanup(Link* link)
{
	smp_ecdh_free(&link->keyPair);
	OPENSSL_cleanse(link->dhKey, sizeof(link->dhKey));
	OPENSSL_cleanse(link->localNonce, sizeof(link->localNonce));
	OPENSSL_cleanse(link->peerNonce, sizeof(link->peerNonce));
	OPENSSL_cleanse(link->ltk, sizeof(link->ltk));
	OPENSSL_cleanse(link->macKey, sizeof(link->macKey));
	OPENSSL_cleanse(link->bond.ltk, sizeof(link->bond.ltk));
	OPENSSL_cleanse(link->bond.irk, sizeof(link->bond.irk));
	link->passkey = 0;
	if (link->passkeyWindow.IsValid())
		link->passkeyWindow.SendMessage(B_QUIT_REQUESTED);
	link->passkeyWindow = BMessenger();
}


void
LEManager::_SMPStart(Link* link)
{
	// One pairing at a time; one that got stuck for longer than the SMP
	// timeout (30 s, Vol 3, Part H, 3.4) may start again.
	if (link->pairing && link->smpState != Link::kSMPDone
		&& link->smpState != Link::kSMPFailed
		&& system_time() - link->smpStarted < 30000000)
		return;
	if (link->pairing && link->smpState != Link::kSMPDone
		&& link->smpState != Link::kSMPFailed)
		syslog(LOG_NOTICE, "bluetooth_server: smp pairing timed out");
	link->smpStarted = system_time();

	_SMPCleanup(link);
	link->pairing = true;
	link->keysReceived = 0;
	link->keysExpected = 0;
	memset(&link->bond.address, 0, sizeof(link->bond.address));
	link->bond.hasIRK = false;

	// Pairing Request (3.5.1): IO capability, no OOB data, AuthReq,
	// largest key, keys we give, keys we want.
	const uint8 request[7] = { kSMPPairingRequest, kIOCapability, 0x00,
		kAuthReq, 16, 0x00, kResponderKeys };
	memcpy(link->preq, request, sizeof(request));
	link->smpState = Link::kSMPWaitResponse;
	_SMPSend(link, request, sizeof(request));
}


void
LEManager::_SMPFail(Link* link, uint8 reason)
{
	const uint8 failed[2] = { kSMPPairingFailed, reason };
	if (link->smpState != Link::kSMPIdle && link->smpState != Link::kSMPDone)
		_SMPSend(link, failed, sizeof(failed));
	syslog(LOG_NOTICE, "bluetooth_server: smp pairing failed (%#x)", reason);
	_PairResult(link, false);
	_SMPCleanup(link);
	link->pairing = false;
	link->smpState = Link::kSMPFailed;
}


void
LEManager::_SMPReceived(Link* link, const uint8* pdu, size_t size)
{
	if (size == 0)
		return;
	link->smpLast = system_time();
	uint8 code = pdu[0];
	const size_t expected = le_smp_pdu_length(code);
	if (expected == 0) {
		// Command Not Supported.
		const uint8 failed[2] = { kSMPPairingFailed, 0x07 };
		_SMPSend(link, failed, sizeof(failed));
		return;
	}
	if (size != expected) {
		if (!link->pairing)
			return;
		_SMPFail(link, kSMPInvalidParameters);
		code = 0;
	}

	switch (code) {
		case kSMPSecurityRequest:
		{
			// The device asks for security. A paired one gets its key; a
			// new one only while a pairing from here is wanted.
			if (link->pairing || link->encrypted)
				break;
			LEBond* bond = _FindBond(link->address);
			if (bond != NULL)
				_StartEncryption(link, bond->ltk, bond->ediv, bond->rand);
			else if (link->pairRequested)
				_SMPStart(link);
			break;
		}

		case kSMPPairingRequest:
			// We are never the responder.
			if (link->pairing)
				break;
			{
				const uint8 failed[2] = { kSMPPairingFailed,
					kSMPPairingNotSupported };
				_SMPSend(link, failed, sizeof(failed));
			}
			break;

		case kSMPPairingFailed:
			if (!link->pairing)
				break;
			syslog(LOG_NOTICE, "bluetooth_server: smp pairing failed, the"
				" device refused (%#x)", pdu[1]);
			_PairResult(link, false);
			_SMPCleanup(link);
			link->pairing = false;
			link->smpState = Link::kSMPFailed;
			break;

		case kSMPPairingResponse:
			if (link->smpState == Link::kSMPWaitResponse)
				_SMPPairingResponse(link, pdu);
			break;

		case kSMPPublicKey:
			if (link->smpState == Link::kSMPWaitPublicKey)
				_SMPPublicKey(link, pdu);
			break;

		case kSMPPairingConfirm:
			if (link->smpState == Link::kSMPWaitConfirm)
				_SMPConfirm(link, pdu);
			break;

		case kSMPPairingRandom:
			if (link->smpState == Link::kSMPWaitRandom)
				_SMPRandom(link, pdu);
			break;

		case kSMPDHKeyCheck:
			if (link->smpState == Link::kSMPWaitDHKeyCheck)
				_SMPDHKeyCheck(link, pdu);
			break;

		case kSMPEncryptionInformation:
		case kSMPCentralIdentification:
		case kSMPIdentityInformation:
		case kSMPIdentityAddress:
		case kSMPSigningInformation:
			// Keys come over the encrypted link, but their PDUs can be
			// read before the Encryption Change event (another thread).
			if (link->smpState == Link::kSMPWaitKeys
				|| link->smpState == Link::kSMPWaitEncryption)
				_SMPKeyDistribution(link, pdu, size);
			break;

		case kSMPKeypress:
			if (link->passkeyWindow.IsValid()) {
				BMessage keypress(kMsgPasskeyKeypress);
				keypress.AddUInt8("type", pdu[1]);
				link->passkeyWindow.SendMessage(&keypress);
			}
			break;
	}

	if (link->smpState == Link::kSMPFailed && _FindBond(link->address) == NULL) {
		// A failed pairing leaves a link that has no use.
		_CloseLink(link, true);
	}
}


void
LEManager::_SMPPairingResponse(Link* link, const uint8* pdu)
{
	memcpy(link->pres, pdu, sizeof(link->pres));
	const uint8 io = pdu[1];
	const uint8 auth = pdu[3];
	link->keySize = pdu[4];
	if (link->keySize < kMinimumKeySize || link->keySize > 16) {
		_SMPFail(link, kSMPEncryptionKeySize);
		return;
	}

	link->secureConnections = (auth & kAuthSC) != 0 && (kAuthReq & kAuthSC) != 0;
	// Only keys we asked for; Secure Connections has no LTK to hand over.
	link->keysExpected = pdu[6] & kResponderKeys;
	if (link->secureConnections)
		link->keysExpected &= ~kKeyEnc;

	// The method (2.3.5.1, tables 2.7 and 2.8), we being KeyboardDisplay
	// and the initiator. Just Works unless the device asks for MITM.
	link->method = Link::kJustWorks;
	if ((auth & kAuthMITM) != 0) {
		switch (io) {
			case 0x00:	// DisplayOnly: the person types its number here
				link->method = Link::kPasskeyEntry;
				break;
			case 0x01:	// DisplayYesNo
				link->method = link->secureConnections
					? Link::kNumericComparison : Link::kPasskeyEntry;
				break;
			case 0x02:	// KeyboardOnly: we show, it types
				link->method = Link::kPasskeyDisplay;
				break;
			case 0x04:	// KeyboardDisplay
				link->method = link->secureConnections
					? Link::kNumericComparison : Link::kPasskeyDisplay;
				break;
			default:	// NoInputNoOutput
				break;
		}
	}

	if (link->method == Link::kPasskeyDisplay) {
		uint32 value;
		if (!random_bytes((uint8*)&value, sizeof(value))) {
			_SMPFail(link, kSMPUnspecified);
			return;
		}
		link->passkey = value % 1000000;
		OPENSSL_cleanse(&value, sizeof(value));
		_SMPShowPasskey(link);
	}

	if (link->secureConnections) {
		if (!smp_ecdh_generate(&link->keyPair)) {
			_SMPFail(link, kSMPUnspecified);
			return;
		}
		uint8 key[65];
		key[0] = kSMPPublicKey;
		memcpy(key + 1, link->keyPair.publicKey, 64);
		link->smpState = Link::kSMPWaitPublicKey;
		_SMPSend(link, key, sizeof(key));
		return;
	}

	// Legacy: TK is the passkey, or 0 for Just Works (2.3.5.5). A
	// passkey the device shows is asked first.
	link->round = 0;
	if (link->method == Link::kPasskeyEntry) {
		_SMPAskUser(link, PasskeyWindow::kLEEntry, false);
		return;
	}
	_SMPSendConfirm(link);
}


void
LEManager::_SMPAddresses(Link* link, uint8 a[7], uint8 b[7])
{
	// SMPCrypto's order: the address LSB-first, then its type (0 public,
	// 1 random). A is us (public), B the device.
	memcpy(a, &fLocalAddress, 6);
	a[6] = 0;
	memcpy(b, &link->address, 6);
	b[6] = link->addressType == R5_BDADDR_LE_RANDOM ? 1 : 0;
}


void
LEManager::_SMPPublicKey(Link* link, const uint8* pdu)
{
	memcpy(link->peerPublic, pdu + 1, 64);
	// A device that sends back our own key attacks the pairing
	// (Vol 3, Part H, 2.3.5.6.1).
	if (memcmp(link->peerPublic, link->keyPair.publicKey, 64) == 0
		|| !smp_ecdh_compute(&link->keyPair, link->peerPublic, link->dhKey)) {
		_SMPFail(link, kSMPInvalidParameters);
		return;
	}

	link->round = 0;
	if (link->method == Link::kPasskeyEntry) {
		_SMPAskUser(link, PasskeyWindow::kLEEntry, false);
		return;
	}
	if (link->method == Link::kPasskeyDisplay) {
		_SMPSendConfirm(link);
		return;
	}
	// Just Works and Numeric Comparison: the device commits first.
	if (!random_bytes(link->localNonce, sizeof(link->localNonce))) {
		_SMPFail(link, kSMPUnspecified);
		return;
	}
	link->smpState = Link::kSMPWaitConfirm;
}


void
LEManager::_SMPSendConfirm(Link* link)
{
	if (!random_bytes(link->localNonce, sizeof(link->localNonce))) {
		_SMPFail(link, kSMPUnspecified);
		return;
	}

	uint8 confirm[17];
	confirm[0] = kSMPPairingConfirm;
	bool ok;
	if (link->secureConnections) {
		// Passkey Entry, one bit per round: Cai = f4(PKax, PKbx, Nai, rai).
		const uint8 bit = 0x80 | ((link->passkey >> link->round) & 1);
		ok = smp_f4(link->keyPair.publicKey, link->peerPublic,
			link->localNonce, bit, confirm + 1);
	} else {
		// Mconfirm = c1(TK, Mrand, preq, pres, iat, rat, ia, ra).
		uint8 tk[16] = {};
		tk[0] = link->passkey & 0xff;
		tk[1] = (link->passkey >> 8) & 0xff;
		tk[2] = (link->passkey >> 16) & 0xff;
		uint8 a[7], b[7];
		_SMPAddresses(link, a, b);
		ok = smp_c1(tk, link->localNonce, link->preq, link->pres, a[6], b[6],
			a, b, confirm + 1);
		OPENSSL_cleanse(tk, sizeof(tk));
	}
	if (!ok) {
		_SMPFail(link, kSMPUnspecified);
		return;
	}
	link->smpState = Link::kSMPWaitConfirm;
	_SMPSend(link, confirm, sizeof(confirm));
}


void
LEManager::_SMPConfirm(Link* link, const uint8* pdu)
{
	memcpy(link->peerConfirm, pdu + 1, 16);
	uint8 random[17];
	random[0] = kSMPPairingRandom;
	memcpy(random + 1, link->localNonce, 16);
	link->smpState = Link::kSMPWaitRandom;
	_SMPSend(link, random, sizeof(random));
	OPENSSL_cleanse(random, sizeof(random));
}


void
LEManager::_SMPRandom(Link* link, const uint8* pdu)
{
	memcpy(link->peerNonce, pdu + 1, 16);
	uint8 check[16];
	uint8 a[7], b[7];
	_SMPAddresses(link, a, b);

	if (!link->secureConnections) {
		uint8 tk[16] = {};
		tk[0] = link->passkey & 0xff;
		tk[1] = (link->passkey >> 8) & 0xff;
		tk[2] = (link->passkey >> 16) & 0xff;
		bool ok = smp_c1(tk, link->peerNonce, link->preq, link->pres, a[6],
			b[6], a, b, check) && memcmp(check, link->peerConfirm, 16) == 0;
		// STK = s1(TK, Srand, Mrand), shortened to the key size.
		uint8 stk[16];
		ok = ok && smp_s1(tk, link->peerNonce, link->localNonce, stk);
		OPENSSL_cleanse(tk, sizeof(tk));
		if (!ok) {
			OPENSSL_cleanse(stk, sizeof(stk));
			_SMPFail(link, kSMPConfirmFailed);
			return;
		}
		memset(stk + link->keySize, 0, 16 - link->keySize);
		const uint8 zero[8] = {};
		link->smpState = Link::kSMPWaitEncryption;
		_StartEncryption(link, stk, 0, zero);
		OPENSSL_cleanse(stk, sizeof(stk));
		return;
	}

	// Cb = f4(PKbx, PKax, Nb, z): z is 0, or the passkey bit.
	const uint8 z = link->UsesPasskey()
		? (0x80 | ((link->passkey >> link->round) & 1)) : 0;
	if (!smp_f4(link->peerPublic, link->keyPair.publicKey, link->peerNonce, z,
			check) || memcmp(check, link->peerConfirm, 16) != 0) {
		_SMPFail(link, link->UsesPasskey()
			? kSMPPasskeyEntryFailed : kSMPConfirmFailed);
		return;
	}

	if (link->UsesPasskey() && ++link->round < 20) {
		_SMPSendConfirm(link);
		return;
	}

	if (link->method == Link::kNumericComparison) {
		// The six digits both sides show; the person says whether they
		// are the same (2.3.5.6.2).
		uint32 value;
		if (!smp_g2(link->keyPair.publicKey, link->peerPublic,
				link->localNonce, link->peerNonce, &value)) {
			_SMPFail(link, kSMPUnspecified);
			return;
		}
		_SMPAskUser(link, value, true);
		OPENSSL_cleanse(&value, sizeof(value));
		return;
	}

	_SMPSendDHKeyCheck(link);
}


void
LEManager::_SMPSendDHKeyCheck(Link* link)
{
	uint8 a[7], b[7];
	_SMPAddresses(link, a, b);

	// MacKey and LTK = f5(DHKey, Na, Nb, A, B); Ea = f6(MacKey, Na, Nb, r,
	// IOcapA, A, B) (2.3.5.6.5).
	uint8 r[16] = {};
	if (link->UsesPasskey()) {
		r[0] = link->passkey & 0xff;
		r[1] = (link->passkey >> 8) & 0xff;
		r[2] = (link->passkey >> 16) & 0xff;
	}
	uint8 dhCheck[17];
	dhCheck[0] = kSMPDHKeyCheck;
	if (!smp_f5(link->dhKey, link->localNonce, link->peerNonce, a, b,
			link->macKey, link->ltk)
		|| !smp_f6(link->macKey, link->localNonce, link->peerNonce, r,
			link->preq + 1, a, b, dhCheck + 1)) {
		OPENSSL_cleanse(r, sizeof(r));
		_SMPFail(link, kSMPUnspecified);
		return;
	}
	OPENSSL_cleanse(r, sizeof(r));
	link->smpState = Link::kSMPWaitDHKeyCheck;
	_SMPSend(link, dhCheck, sizeof(dhCheck));
}


void
LEManager::_SMPDHKeyCheck(Link* link, const uint8* pdu)
{
	// Eb = f6(MacKey, Nb, Na, r, IOcapB, B, A).
	uint8 a[7], b[7];
	_SMPAddresses(link, a, b);
	uint8 r[16] = {};
	if (link->UsesPasskey()) {
		r[0] = link->passkey & 0xff;
		r[1] = (link->passkey >> 8) & 0xff;
		r[2] = (link->passkey >> 16) & 0xff;
	}
	uint8 expected[16];
	const bool ok = smp_f6(link->macKey, link->peerNonce, link->localNonce, r,
		link->pres + 1, b, a, expected) && memcmp(expected, pdu + 1, 16) == 0;
	OPENSSL_cleanse(r, sizeof(r));
	if (!ok) {
		_SMPFail(link, kSMPDHKeyCheckFailed);
		return;
	}

	memset(link->ltk + link->keySize, 0, 16 - link->keySize);
	const uint8 zero[8] = {};
	link->smpState = Link::kSMPWaitEncryption;
	_StartEncryption(link, link->ltk, 0, zero);
}


void
LEManager::_SMPEncrypted(Link* link)
{
	if (link->smpState != Link::kSMPWaitEncryption)
		return;
	if ((link->keysReceived & link->keysExpected) != link->keysExpected) {
		link->smpState = Link::kSMPWaitKeys;
		return;
	}
	_SMPFinish(link);
}


void
LEManager::_SMPKeyDistribution(Link* link, const uint8* pdu, size_t size)
{
	switch (pdu[0]) {
		case kSMPEncryptionInformation:
			memcpy(link->bond.ltk, pdu + 1, 16);
			link->keysReceived |= kGotEncryptionInformation;
			break;
		case kSMPCentralIdentification:
			if ((link->keysReceived & kGotEncryptionInformation) == 0)
				break;
			link->bond.ediv = pdu[1] | (pdu[2] << 8);
			memcpy(link->bond.rand, pdu + 3, 8);
			link->keysReceived |= kKeyEnc;
			break;
		case kSMPIdentityInformation:
			memcpy(link->bond.irk, pdu + 1, 16);
			link->keysReceived |= kGotIdentityInformation;
			break;
		case kSMPIdentityAddress:
			if ((link->keysReceived & kGotIdentityInformation) == 0)
				break;
			link->bond.addressType = (pdu[1] & 0x01) != 0
				? R5_BDADDR_LE_RANDOM : R5_BDADDR_LE_PUBLIC;
			memcpy(&link->bond.address, pdu + 2, 6);
			link->bond.hasIRK = true;
			link->keysReceived |= kKeyId;
			break;
		case kSMPSigningInformation:
			// Signed writes are not used here.
			link->keysReceived |= kKeySign;
			break;
	}

	if (link->smpState == Link::kSMPWaitKeys
		&& (link->keysReceived & link->keysExpected) == link->keysExpected)
		_SMPFinish(link);
}


void
LEManager::_SMPFinish(Link* link)
{
	LEBond* bond = new(std::nothrow) LEBond;
	if (bond == NULL) {
		_SMPFail(link, kSMPUnspecified);
		return;
	}

	const bdaddr_t none = {{0, 0, 0, 0, 0, 0}};
	const bool identity = link->bond.hasIRK
		&& !bdaddrUtils::Compare(link->bond.address, none);
	bond->address = identity ? link->bond.address : link->address;
	bond->addressType = identity ? link->bond.addressType : link->addressType;
	bond->hasIRK = identity;
	memcpy(bond->irk, link->bond.irk, 16);
	bond->keySize = link->keySize;
	bond->secureConnections = link->secureConnections;
	bond->hid = false;
	Seen* seen = _FindSeen(link->address);
	if (seen != NULL)
		bond->name = seen->name;

	bool keep = true;
	if (link->secureConnections) {
		memcpy(bond->ltk, link->ltk, 16);
		bond->ediv = 0;
		memset(bond->rand, 0, sizeof(bond->rand));
	} else if ((link->keysReceived & kKeyEnc) != 0) {
		memcpy(bond->ltk, link->bond.ltk, 16);
		memset(bond->ltk + link->keySize, 0, 16 - link->keySize);
		bond->ediv = link->bond.ediv;
		memcpy(bond->rand, link->bond.rand, 8);
	} else {
		// Without the device's LTK it cannot be encrypted again later.
		keep = false;
	}

	syslog(LOG_INFO, "bluetooth_server: smp %s pairing ok%s",
		link->secureConnections ? "sc" : "legacy",
		keep ? "" : " (no key to keep, not paired)");
	_PairResult(link, keep);

	if (keep) {
		// The link keeps its connection address: the kernel names its
		// PDUs by it. A later link to the identity address finds the bond.
		// A new pairing replaces an older bond of the same identity
		// address (Pair removed the bond of the address it advertised,
		// often a private one): _FindBond() would return the old LTK.
		for (size_t i = 0; i < fBonds.size(); i++) {
			if (bdaddrUtils::Compare(fBonds[i]->address, bond->address)) {
				memset(fBonds[i]->ltk, 0, sizeof(fBonds[i]->ltk));
				memset(fBonds[i]->irk, 0, sizeof(fBonds[i]->irk));
				delete fBonds[i];
				fBonds.erase(fBonds.begin() + i);
				break;
			}
		}
		fBonds.push_back(bond);
		_SaveBonds();
		fAcceptListDirty = true;
		if (fInitiating == kAuto)
			_CancelConnection();
	} else {
		OPENSSL_cleanse(bond->ltk, sizeof(bond->ltk));
		delete bond;
	}

	_SMPCleanup(link);
	link->pairing = false;
	link->smpState = Link::kSMPDone;
	_GATTStart(link);
}


void
LEManager::_SMPShowPasskey(Link* link)
{
	// The person types this number on the keyboard (PasskeyWindow of
	// step 4, display mode, which sends nothing to the radio).
	BString name;
	Seen* seen = _FindSeen(link->address);
	if (seen != NULL)
		name = seen->name;
	if (name.Length() == 0)
		name = "the device";
	PasskeyWindow* window = new(std::nothrow) PasskeyWindow(link->address,
		fDevice->GetID(), name, link->passkey);
	if (window == NULL)
		return;
	link->passkeyWindow = BMessenger(window);
	window->Show();
}
