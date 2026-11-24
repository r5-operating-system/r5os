/*
 * Copyright 2012, Michael Lotz, mmlr@mlotz.ch. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */


#include "KeyStoreServer.h"

#include "AppAccessRequestWindow.h"
#include "KeyRequestWindow.h"
#include "Keyring.h"
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include "KeyStoreCrypto.h"
#include "OldKeyStoreWindow.h"
// SPDX-SnippetEnd

#include <KeyStoreDefs.h>

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <DataIO.h>
// SPDX-SnippetEnd
#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Roster.h>
#include <String.h>

#include <new>

#include <stdio.h>
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>
// SPDX-SnippetEnd


using namespace BPrivate;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
using namespace BSecurityKit;

static const char* kLoginAppSignature = "application/x-vnd.Haiku-Login";
static const uint32 kMsgShowOldKeyStorePrompt = 'r5OP';

static void
wipe_message_raw_field(BMessage& msg, const char* name)
{
	const void* data = NULL;
	ssize_t size = 0;
	if (msg.FindData(name, B_RAW_TYPE, &data, &size) == B_OK
		&& data != NULL && size > 0) {
		BSecureBuffer::WipeMemory(const_cast<void*>(data),
			static_cast<size_t>(size));
	}
}
// SPDX-SnippetEnd


static const char* kMasterKeyringName = "Master";
static const char* kKeyringKeysIdentifier = "Keyrings";

static const uint32 kKeyStoreFormatVersion = 1;

static const uint32 kFlagGetKey						= 0x0001;
static const uint32 kFlagEnumerateKeys				= 0x0002;
static const uint32 kFlagAddKey						= 0x0004;
static const uint32 kFlagRemoveKey					= 0x0008;
static const uint32 kFlagAddKeyring					= 0x0010;
static const uint32 kFlagRemoveKeyring				= 0x0020;
static const uint32 kFlagEnumerateKeyrings			= 0x0040;
static const uint32 kFlagSetUnlockKey				= 0x0080;
static const uint32 kFlagRemoveUnlockKey			= 0x0100;
static const uint32 kFlagAddKeyringsToMaster		= 0x0200;
static const uint32 kFlagRemoveKeyringsFromMaster	= 0x0400;
static const uint32 kFlagEnumerateMasterKeyrings	= 0x0800;
static const uint32 kFlagQueryLockState				= 0x1000;
static const uint32 kFlagLockKeyring				= 0x2000;
static const uint32 kFlagEnumerateApplications		= 0x4000;
static const uint32 kFlagRemoveApplications			= 0x8000;

static const uint32 kDefaultAppFlags = kFlagGetKey | kFlagEnumerateKeys
	| kFlagAddKey | kFlagRemoveKey | kFlagAddKeyring | kFlagRemoveKeyring
	| kFlagEnumerateKeyrings | kFlagSetUnlockKey | kFlagRemoveUnlockKey
	| kFlagAddKeyringsToMaster | kFlagRemoveKeyringsFromMaster
	| kFlagEnumerateMasterKeyrings | kFlagQueryLockState | kFlagLockKeyring
	| kFlagEnumerateApplications | kFlagRemoveApplications;


KeyStoreServer::KeyStoreServer()
	:
	BApplication(kKeyStoreServerSignature),
	fMasterKeyring(NULL),
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	fKeyrings(20),
	fAdminPort(-1),
	fStoreUnlocked(false)
{
	memset(fKeyStoreId, 0, sizeof(fKeyStoreId));

	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
		return;

	BDirectory settingsDir(path.Path());
	path.Append("system");
	if (!settingsDir.Contains(path.Path()))
		settingsDir.CreateDirectory(path.Path(), NULL);

	settingsDir.SetTo(path.Path());
	path.Append("keystore");
	if (!settingsDir.Contains(path.Path()))
		settingsDir.CreateDirectory(path.Path(), NULL);
	chmod(path.Path(), 0700);

	fKeyStoreDir = path.Path();
	fDatabasePath.SetToFormat("%s/%s", fKeyStoreDir.String(),
		kR5KSDatabaseFile);
	fProtectorsPath.SetToFormat("%s/%s", fKeyStoreDir.String(),
		kR5KSProtectorsFile);
	fOldDatabasePath.SetToFormat("%s/%s", fKeyStoreDir.String(),
		kR5KSOldDatabaseFile);
	fOldProtectorsPath.SetToFormat("%s/%s", fKeyStoreDir.String(),
		kR5KSOldProtectorsFile);

	// Stay locked until Login hands a derived key over the admin port.
	// Nothing is read or written in plaintext while locked (autologin / live).
	if (r5_admin_port_start("r5 keystore admin", &KeyStoreServer::_AdminRequest,
			this, &fAdminPort) != B_OK) {
		fAdminPort = -1;
		syslog(LOG_ERR, "keystore_server: failed to start admin port");
	}
}
// SPDX-SnippetEnd


KeyStoreServer::~KeyStoreServer()
{
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	if (fAdminPort >= 0) {
		r5_admin_port_stop(fAdminPort);
		fAdminPort = -1;
	}
	_LockStore();
// SPDX-SnippetEnd
}


void
KeyStoreServer::MessageReceived(BMessage* message)
{
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	if (message->what == R5_ADMIN_PORT_REQUEST) {
		r5_admin_port_reply(message, fAdminPort);
		return;
	}
	if (message->what == kMsgShowOldKeyStorePrompt) {
		if (!message->IsSourceRemote() && fStoreUnlocked && _HasOldKeyStore())
			(new OldKeyStoreWindow(this))->Show();
		return;
	}
// SPDX-SnippetEnd

	BMessage reply;
	status_t result = B_UNSUPPORTED;
	app_info callingAppInfo;

	uint32 accessFlags = _AccessFlagsFor(message->what);
	if (accessFlags == 0)
		message->what = 0;

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	if (message->what != 0 && !fStoreUnlocked) {
		// Locked key store (e.g. autologin / no password): refuse all BKeyStore
		// operations with a clear locked error and never write plaintext.
		if (message->IsSourceWaiting()) {
			reply.what = KEY_STORE_RESULT;
			reply.AddInt32("result", B_NOT_ALLOWED);
			message->SendReply(&reply);
		}
		return;
	}
// SPDX-SnippetEnd

	if (message->what != 0) {
		result = _ResolveCallingApp(*message, callingAppInfo);
		if (result != B_OK)
			message->what = 0;
	}

	// Resolve the keyring for the relevant messages.
	Keyring* keyring = NULL;
	switch (message->what) {
		case KEY_STORE_GET_KEY:
		case KEY_STORE_GET_NEXT_KEY:
		case KEY_STORE_ADD_KEY:
		case KEY_STORE_REMOVE_KEY:
		case KEY_STORE_IS_KEYRING_UNLOCKED:
		case KEY_STORE_LOCK_KEYRING:
		case KEY_STORE_SET_UNLOCK_KEY:
		case KEY_STORE_REMOVE_UNLOCK_KEY:
		case KEY_STORE_ADD_KEYRING_TO_MASTER:
		case KEY_STORE_REMOVE_KEYRING_FROM_MASTER:
		case KEY_STORE_GET_NEXT_APPLICATION:
		case KEY_STORE_REMOVE_APPLICATION:
		{
			BString keyringName;
			if (message->FindString("keyring", &keyringName) != B_OK)
				keyringName = "";

			keyring = _FindKeyring(keyringName);
			if (keyring == NULL) {
				result = B_BAD_VALUE;
				message->what = 0;
					// So that we don't do anything in the second switch.
				break;
			}

			switch (message->what) {
				case KEY_STORE_GET_KEY:
				case KEY_STORE_GET_NEXT_KEY:
				case KEY_STORE_ADD_KEY:
				case KEY_STORE_REMOVE_KEY:
				case KEY_STORE_SET_UNLOCK_KEY:
				case KEY_STORE_REMOVE_UNLOCK_KEY:
				case KEY_STORE_ADD_KEYRING_TO_MASTER:
				case KEY_STORE_GET_NEXT_APPLICATION:
				case KEY_STORE_REMOVE_APPLICATION:
				{
					// These need keyring access to do anything.
					while (!keyring->IsUnlocked()) {
						status_t unlockResult = _UnlockKeyring(*keyring);
						if (unlockResult != B_OK) {
							result = unlockResult;
							message->what = 0;
							break;
						}
					}

					status_t validateResult = _ValidateAppAccess(*keyring,
						callingAppInfo, accessFlags);
					if (validateResult != B_OK) {
						result = validateResult;
						message->what = 0;
						break;
					}

					break;
				}
			}

			break;
		}
	}

	switch (message->what) {
		case KEY_STORE_GET_KEY:
		{
			BString identifier;
			if (message->FindString("identifier", &identifier) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}

			bool secondaryIdentifierOptional;
			if (message->FindBool("secondaryIdentifierOptional",
					&secondaryIdentifierOptional) != B_OK) {
				secondaryIdentifierOptional = false;
			}

			BString secondaryIdentifier;
			if (message->FindString("secondaryIdentifier",
					&secondaryIdentifier) != B_OK) {
				secondaryIdentifier = "";
				secondaryIdentifierOptional = true;
			}

			BMessage keyMessage;
			result = keyring->FindKey(identifier, secondaryIdentifier,
				secondaryIdentifierOptional, &keyMessage);
			if (result == B_OK)
				reply.AddMessage("key", &keyMessage);

			break;
		}

		case KEY_STORE_GET_NEXT_KEY:
		{
			BKeyType type;
			BKeyPurpose purpose;
			uint32 cookie;
			if (message->FindUInt32("type", (uint32*)&type) != B_OK
				|| message->FindUInt32("purpose", (uint32*)&purpose) != B_OK
				|| message->FindUInt32("cookie", &cookie) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}

			BMessage keyMessage;
			result = keyring->FindKey(type, purpose, cookie, keyMessage);
			if (result == B_OK) {
				cookie++;
				reply.AddUInt32("cookie", cookie);
				reply.AddMessage("key", &keyMessage);
			}

			break;
		}

		case KEY_STORE_ADD_KEY:
		{
			BMessage keyMessage;
			BString identifier;
			if (message->FindMessage("key", &keyMessage) != B_OK
				|| keyMessage.FindString("identifier", &identifier) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}

			BString secondaryIdentifier;
			if (keyMessage.FindString("secondaryIdentifier",
					&secondaryIdentifier) != B_OK) {
				secondaryIdentifier = "";
			}

			result = keyring->AddKey(identifier, secondaryIdentifier, keyMessage);
			if (result == B_OK)
				_WriteKeyStoreDatabase();

			break;
		}

		case KEY_STORE_REMOVE_KEY:
		{
			BMessage keyMessage;
			BString identifier;
			if (message->FindMessage("key", &keyMessage) != B_OK
				|| keyMessage.FindString("identifier", &identifier) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}

			result = keyring->RemoveKey(identifier, keyMessage);
			if (result == B_OK)
				_WriteKeyStoreDatabase();

			break;
		}

		case KEY_STORE_ADD_KEYRING:
		{
			BMessage keyMessage;
			BString keyring;
			if (message->FindString("keyring", &keyring) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}

			result = _AddKeyring(keyring);
			if (result == B_OK)
				_WriteKeyStoreDatabase();

			break;
		}

		case KEY_STORE_REMOVE_KEYRING:
		{
			BString keyringName;
			if (message->FindString("keyring", &keyringName) != B_OK)
				keyringName = "";

			result = _RemoveKeyring(keyringName);
			if (result == B_OK)
				_WriteKeyStoreDatabase();

			break;
		}

		case KEY_STORE_GET_NEXT_KEYRING:
		{
			uint32 cookie;
			if (message->FindUInt32("cookie", &cookie) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}

			keyring = fKeyrings.ItemAt(cookie);
			if (keyring == NULL) {
				result = B_ENTRY_NOT_FOUND;
				break;
			}

			cookie++;
			reply.AddUInt32("cookie", cookie);
			reply.AddString("keyring", keyring->Name());
			result = B_OK;
			break;
		}

		case KEY_STORE_IS_KEYRING_UNLOCKED:
		{
			reply.AddBool("unlocked", keyring->IsUnlocked());
			result = B_OK;
			break;
		}

		case KEY_STORE_LOCK_KEYRING:
		{
			keyring->Lock();
			result = B_OK;
			break;
		}

		case KEY_STORE_SET_UNLOCK_KEY:
		{
			BMessage keyMessage;
			if (message->FindMessage("key", &keyMessage) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}

			result = keyring->SetUnlockKey(keyMessage);
			if (result == B_OK)
				_WriteKeyStoreDatabase();

			// TODO: Update the key in the master if this keyring was added.
			break;
		}

		case KEY_STORE_REMOVE_UNLOCK_KEY:
		{
			result = keyring->RemoveUnlockKey();
			if (result == B_OK)
				_WriteKeyStoreDatabase();

			break;
		}

		case KEY_STORE_ADD_KEYRING_TO_MASTER:
		case KEY_STORE_REMOVE_KEYRING_FROM_MASTER:
		{
			// We also need access to the master keyring.
			while (!fMasterKeyring->IsUnlocked()) {
				status_t unlockResult = _UnlockKeyring(*fMasterKeyring);
				if (unlockResult != B_OK) {
					result = unlockResult;
					message->what = 0;
					break;
				}
			}

			if (message->what == 0)
				break;

			BString secondaryIdentifier = keyring->Name();
			BMessage keyMessage = keyring->UnlockKey();
			keyMessage.RemoveName("identifier");
			keyMessage.AddString("identifier", kKeyringKeysIdentifier);
			keyMessage.RemoveName("secondaryIdentifier");
			keyMessage.AddString("secondaryIdentifier", secondaryIdentifier);

			switch (message->what) {
				case KEY_STORE_ADD_KEYRING_TO_MASTER:
					result = fMasterKeyring->AddKey(kKeyringKeysIdentifier,
						secondaryIdentifier, keyMessage);
					break;

				case KEY_STORE_REMOVE_KEYRING_FROM_MASTER:
					result = fMasterKeyring->RemoveKey(kKeyringKeysIdentifier,
						keyMessage);
					break;
			}

			if (result == B_OK)
				_WriteKeyStoreDatabase();

			break;
		}

		case KEY_STORE_GET_NEXT_APPLICATION:
		{
			uint32 cookie;
			if (message->FindUInt32("cookie", &cookie) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}

			BString signature;
			BString path;
			result = keyring->GetNextApplication(cookie, signature, path);
			if (result != B_OK)
				break;

			reply.AddUInt32("cookie", cookie);
			reply.AddString("signature", signature);
			reply.AddString("path", path);
			result = B_OK;
			break;
		}

		case KEY_STORE_REMOVE_APPLICATION:
		{
			const char* signature = NULL;
			const char* path = NULL;

			if (message->FindString("signature", &signature) != B_OK) {
				result = B_BAD_VALUE;
				break;
			}

			if (message->FindString("path", &path) != B_OK)
				path = NULL;

			result = keyring->RemoveApplication(signature, path);
			if (result == B_OK)
				_WriteKeyStoreDatabase();

			break;
		}

		case 0:
		{
			// Just the error case from above.
			break;
		}

		default:
		{
			printf("unknown message received: %" B_PRIu32 " \"%.4s\"\n",
				message->what, (const char*)&message->what);
			break;
		}
	}

	if (message->IsSourceWaiting()) {
		if (result == B_OK)
			reply.what = KEY_STORE_SUCCESS;
		else {
			reply.what = KEY_STORE_RESULT;
			reply.AddInt32("result", result);
		}

		message->SendReply(&reply);
	}
}


status_t
KeyStoreServer::_ReadKeyStoreDatabase()
{
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	if (!fStoreUnlocked || fMasterKey.Size() != BSealedData::kKeySize)
		return B_NOT_ALLOWED;

	BMessage keystore;
	status_t result = R5KeyStoreCrypto::ReadEncryptedDatabase(
		fDatabasePath.String(), fKeyStoreId, fMasterKey, keystore);
	if (result != B_OK)
		return result;

	return _LoadKeyringsFromMessage(keystore);
// SPDX-SnippetEnd
}


status_t
KeyStoreServer::_WriteKeyStoreDatabase()
{
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	if (!fStoreUnlocked || fMasterKey.Size() != BSealedData::kKeySize)
		return B_NOT_ALLOWED;
// SPDX-SnippetEnd

	BMessage keystore;
	keystore.AddUInt32("format", kKeyStoreFormatVersion);

	for (int32 i = 0; i < fKeyrings.CountItems(); i++) {
		Keyring* keyring = fKeyrings.ItemAt(i);
		if (keyring == NULL)
			continue;

		BMessage keyringData;
		status_t result = keyring->WriteToMessage(keyringData);
		if (result != B_OK)
			return result;

		keystore.AddMessage("keyrings", &keyringData);
	}

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	return R5KeyStoreCrypto::WriteEncryptedDatabase(fDatabasePath.String(),
		fKeyStoreId, fMasterKey, keystore);
// SPDX-SnippetEnd
}


uint32
KeyStoreServer::_AccessFlagsFor(uint32 command) const
{
	switch (command) {
		case KEY_STORE_GET_KEY:
			return kFlagGetKey;
		case KEY_STORE_GET_NEXT_KEY:
			return kFlagEnumerateKeys;
		case KEY_STORE_ADD_KEY:
			return kFlagAddKey;
		case KEY_STORE_REMOVE_KEY:
			return kFlagRemoveKey;
		case KEY_STORE_ADD_KEYRING:
			return kFlagAddKeyring;
		case KEY_STORE_REMOVE_KEYRING:
			return kFlagRemoveKeyring;
		case KEY_STORE_GET_NEXT_KEYRING:
			return kFlagEnumerateKeyrings;
		case KEY_STORE_SET_UNLOCK_KEY:
			return kFlagSetUnlockKey;
		case KEY_STORE_REMOVE_UNLOCK_KEY:
			return kFlagRemoveUnlockKey;
		case KEY_STORE_ADD_KEYRING_TO_MASTER:
			return kFlagAddKeyringsToMaster;
		case KEY_STORE_REMOVE_KEYRING_FROM_MASTER:
			return kFlagRemoveKeyringsFromMaster;
		case KEY_STORE_GET_NEXT_MASTER_KEYRING:
			return kFlagEnumerateMasterKeyrings;
		case KEY_STORE_IS_KEYRING_UNLOCKED:
			return kFlagQueryLockState;
		case KEY_STORE_LOCK_KEYRING:
			return kFlagLockKeyring;
		case KEY_STORE_GET_NEXT_APPLICATION:
			return kFlagEnumerateApplications;
		case KEY_STORE_REMOVE_APPLICATION:
			return kFlagRemoveApplications;
	}

	return 0;
}


const char*
KeyStoreServer::_AccessStringFor(uint32 accessFlag) const
{
	switch (accessFlag) {
		case kFlagGetKey:
			return "Get keys from the keyring.";
		case kFlagEnumerateKeys:
			return "Enumerate and get keys from the keyring.";
		case kFlagAddKey:
			return "Add keys to the keyring.";
		case kFlagRemoveKey:
			return "Remove keys from the keyring.";
		case kFlagAddKeyring:
			return "Add new keyrings.";
		case kFlagRemoveKeyring:
			return "Remove keyrings.";
		case kFlagEnumerateKeyrings:
			return "Enumerate the available keyrings.";
		case kFlagSetUnlockKey:
			return "Set the unlock key of the keyring.";
		case kFlagRemoveUnlockKey:
			return "Remove the unlock key of the keyring.";
		case kFlagAddKeyringsToMaster:
			return "Add the keyring key to the master keyring.";
		case kFlagRemoveKeyringsFromMaster:
			return "Remove the keyring key from the master keyring.";
		case kFlagEnumerateMasterKeyrings:
			return "Enumerate keyrings added to the master keyring.";
		case kFlagQueryLockState:
			return "Query the lock state of the keyring.";
		case kFlagLockKeyring:
			return "Lock the keyring.";
		case kFlagEnumerateApplications:
			return "Enumerate the applications of the keyring.";
		case kFlagRemoveApplications:
			return "Remove applications from the keyring.";
	}

	return NULL;
}


status_t
KeyStoreServer::_ResolveCallingApp(const BMessage& message,
	app_info& callingAppInfo) const
{
	team_id callingTeam = message.ReturnAddress().Team();
	status_t result = be_roster->GetRunningAppInfo(callingTeam,
		&callingAppInfo);
	if (result != B_OK)
		return result;

	// Do some sanity checks.
	if (callingAppInfo.team != callingTeam)
		return B_ERROR;

	return B_OK;
}


status_t
KeyStoreServer::_ValidateAppAccess(Keyring& keyring, const app_info& appInfo,
	uint32 accessFlags)
{
	BMessage appMessage;
	BPath path(&appInfo.ref);
	status_t result = keyring.FindApplication(appInfo.signature,
		path.Path(), appMessage);
	if (result != B_OK && result != B_ENTRY_NOT_FOUND)
		return result;

	// TODO: Implement running image checksum mechanism.
	BString checksum = path.Path();

	bool appIsNew = false;
	bool appWasUpdated = false;
	uint32 appFlags = 0;
	BString appSum = "";
	if (result == B_OK) {
		if (appMessage.FindUInt32("flags", &appFlags) != B_OK
			|| appMessage.FindString("checksum", &appSum) != B_OK) {
			appIsNew = true;
			appFlags = 0;
		} else if (appSum != checksum) {
			appWasUpdated = true;
			appFlags = 0;
		}
	} else
		appIsNew = true;

	if ((accessFlags & appFlags) == accessFlags)
		return B_OK;

	const char* accessString = _AccessStringFor(accessFlags);
	bool allowAlways = false;
	result = _RequestAppAccess(keyring.Name(), appInfo.signature, path.Path(),
		accessString, appIsNew, appWasUpdated, accessFlags, allowAlways);
	if (result != B_OK || !allowAlways)
		return result;

	appMessage.MakeEmpty();
	appMessage.AddString("path", path.Path());
	appMessage.AddUInt32("flags", appFlags | accessFlags);
	appMessage.AddString("checksum", checksum);

	keyring.RemoveApplication(appInfo.signature, path.Path());
	if (keyring.AddApplication(appInfo.signature, appMessage) == B_OK)
		_WriteKeyStoreDatabase();

	return B_OK;
}


status_t
KeyStoreServer::_RequestAppAccess(const BString& keyringName,
	const char* signature, const char* path, const char* accessString,
	bool appIsNew, bool appWasUpdated, uint32 accessFlags, bool& allowAlways)
{
	AppAccessRequestWindow* requestWindow
		= new(std::nothrow) AppAccessRequestWindow(keyringName, signature, path,
			accessString, appIsNew, appWasUpdated);
	if (requestWindow == NULL)
		return B_NO_MEMORY;

	return requestWindow->RequestAppAccess(allowAlways);
}


Keyring*
KeyStoreServer::_FindKeyring(const BString& name)
{
	if (name.IsEmpty() || name == kMasterKeyringName)
		return fMasterKeyring;

	return fKeyrings.BinarySearchByKey(name, &Keyring::Compare);
}


status_t
KeyStoreServer::_AddKeyring(const BString& name)
{
	if (_FindKeyring(name) != NULL)
		return B_NAME_IN_USE;

	Keyring* keyring = new(std::nothrow) Keyring(name);
	if (keyring == NULL)
		return B_NO_MEMORY;

	if (!fKeyrings.BinaryInsert(keyring, &Keyring::Compare)) {
		delete keyring;
		return B_ERROR;
	}

	return B_OK;
}


status_t
KeyStoreServer::_RemoveKeyring(const BString& name)
{
	Keyring* keyring = _FindKeyring(name);
	if (keyring == NULL)
		return B_ENTRY_NOT_FOUND;

	if (keyring == fMasterKeyring) {
		// The master keyring can't be removed.
		return B_NOT_ALLOWED;
	}

	return fKeyrings.RemoveItem(keyring) ? B_OK : B_ERROR;
}


status_t
KeyStoreServer::_UnlockKeyring(Keyring& keyring)
{
	if (!keyring.HasUnlockKey())
		return keyring.Unlock(NULL);

	// If we are accessing a keyring that has been added to master access we
	// get the key from the master keyring and unlock with that.
	BMessage keyMessage;
	if (&keyring != fMasterKeyring && fMasterKeyring->IsUnlocked()) {
		if (fMasterKeyring->FindKey(kKeyringKeysIdentifier, keyring.Name(),
				false, &keyMessage) == B_OK) {
			// We found a key for this keyring, try to unlock with it.
			if (keyring.Unlock(&keyMessage) == B_OK)
				return B_OK;
		}
	}

	// No key, we need to request one from the user.
	status_t result = _RequestKey(keyring.Name(), keyMessage);
	if (result != B_OK)
		return result;

	return keyring.Unlock(&keyMessage);
}


status_t
KeyStoreServer::_RequestKey(const BString& keyringName, BMessage& keyMessage)
{
	KeyRequestWindow* requestWindow = new(std::nothrow) KeyRequestWindow();
	if (requestWindow == NULL)
		return B_NO_MEMORY;

	return requestWindow->RequestKey(keyringName, keyMessage);
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
/* static */ void
KeyStoreServer::_AdminRequest(BMessage* request, const r5_admin_sender& sender,
	BMessage* reply, void* cookie)
{
	KeyStoreServer* server = static_cast<KeyStoreServer*>(cookie);
	if (server == NULL || request == NULL || reply == NULL)
		return;

	// Only accept key handoff / rewrap / lock from Login (uid 0).
	team_id loginTeam = be_roster->TeamFor(kLoginAppSignature);
	if (sender.uid != 0 || loginTeam < 0 || sender.team != loginTeam) {
		reply->AddInt32(kR5KSFieldStatus, B_NOT_ALLOWED);
		return;
	}

	if (!server->Lock()) {
		reply->AddInt32(kR5KSFieldStatus, B_ERROR);
		return;
	}

	status_t status = B_BAD_VALUE;
	switch (request->what) {
		case kMsgR5KeyStoreUnlock:
		{
			const void* kekData = NULL;
			ssize_t kekSize = 0;
			if (request->FindData(kR5KSFieldKek, B_RAW_TYPE, &kekData, &kekSize)
					!= B_OK
				|| kekSize != static_cast<ssize_t>(BSealedData::kKeySize)) {
				status = B_BAD_VALUE;
				break;
			}

			BSecureBuffer kek;
			status = kek.SetTo(kekData, static_cast<size_t>(kekSize));
			if (status != B_OK)
				break;

			const void* saltData = NULL;
			ssize_t saltSize = 0;
			uint8 initSalt[16];
			bool hasInitParams = false;
			BPasswordHash::Parameters initParams
				= BPasswordHash::DefaultParameters();
			if (request->FindData(kR5KSFieldSalt, B_RAW_TYPE, &saltData,
					&saltSize) == B_OK
				&& saltSize == 16
				&& request->FindUInt32(kR5KSFieldMemKiB, &initParams.memoryKiB)
					== B_OK
				&& request->FindUInt32(kR5KSFieldPasses, &initParams.passes)
					== B_OK
				&& request->FindUInt32(kR5KSFieldLanes, &initParams.lanes)
					== B_OK) {
				memcpy(initSalt, saltData, 16);
				hasInitParams = true;
			}

			bool hasOld = false;
			status = server->_UnlockWithKek(kek,
				hasInitParams ? initSalt : NULL, hasInitParams, initParams,
				&hasOld);
			kek.Clear();
			reply->AddBool(kR5KSFieldHasOld, hasOld);
			break;
		}

		case kMsgR5KeyStoreRewrap:
		{
			const void* oldData = NULL;
			ssize_t oldSize = 0;
			const void* newData = NULL;
			ssize_t newSize = 0;
			const void* saltData = NULL;
			ssize_t saltSize = 0;
			BPasswordHash::Parameters newParams;
			if (request->FindData(kR5KSFieldOldKek, B_RAW_TYPE, &oldData,
					&oldSize) != B_OK
				|| oldSize != static_cast<ssize_t>(BSealedData::kKeySize)
				|| request->FindData(kR5KSFieldNewKek, B_RAW_TYPE, &newData,
					&newSize) != B_OK
				|| newSize != static_cast<ssize_t>(BSealedData::kKeySize)
				|| request->FindData(kR5KSFieldSalt, B_RAW_TYPE, &saltData,
					&saltSize) != B_OK
				|| saltSize != 16
				|| request->FindUInt32(kR5KSFieldMemKiB, &newParams.memoryKiB)
					!= B_OK
				|| request->FindUInt32(kR5KSFieldPasses, &newParams.passes)
					!= B_OK
				|| request->FindUInt32(kR5KSFieldLanes, &newParams.lanes)
					!= B_OK) {
				status = B_BAD_VALUE;
				break;
			}

			BSecureBuffer oldKek;
			BSecureBuffer newKek;
			status = oldKek.SetTo(oldData, static_cast<size_t>(oldSize));
			if (status == B_OK)
				status = newKek.SetTo(newData, static_cast<size_t>(newSize));
			if (status == B_OK) {
				status = server->_RewrapPasswordProtector(oldKek, newKek,
					static_cast<const uint8*>(saltData), newParams);
			}
			oldKek.Clear();
			newKek.Clear();
			break;
		}

		case kMsgR5KeyStoreLock:
			server->_LockStore();
			status = B_OK;
			break;

		default:
			status = B_BAD_VALUE;
			break;
	}

	reply->AddInt32(kR5KSFieldStatus, status);
	wipe_message_raw_field(*request, kR5KSFieldKek);
	wipe_message_raw_field(*request, kR5KSFieldOldKek);
	wipe_message_raw_field(*request, kR5KSFieldNewKek);
	server->Unlock();
}


status_t
KeyStoreServer::_LoadKeyringsFromMessage(const BMessage& keystore)
{
	fKeyrings.MakeEmpty();
	fMasterKeyring = NULL;

	int32 index = 0;
	BMessage keyringData;
	while (keystore.FindMessage("keyrings", index++, &keyringData) == B_OK) {
		Keyring* keyring = new(std::nothrow) Keyring();
		if (keyring == NULL)
			return B_NO_MEMORY;

		status_t result = keyring->ReadFromMessage(keyringData);
		if (result != B_OK) {
			delete keyring;
			continue;
		}

		if (strcmp(keyring->Name(), kMasterKeyringName) == 0)
			fMasterKeyring = keyring;

		fKeyrings.BinaryInsert(keyring, &Keyring::Compare);
	}

	if (fMasterKeyring == NULL) {
		fMasterKeyring = new(std::nothrow) Keyring(kMasterKeyringName);
		if (fMasterKeyring == NULL)
			return B_NO_MEMORY;
		fKeyrings.BinaryInsert(fMasterKeyring, &Keyring::Compare);
	}

	return B_OK;
}


status_t
KeyStoreServer::_MergeKeyringsFromMessage(const BMessage& oldKeystore)
{
	int32 index = 0;
	BMessage keyringData;
	while (oldKeystore.FindMessage("keyrings", index++, &keyringData) == B_OK) {
		Keyring* oldRing = new(std::nothrow) Keyring();
		if (oldRing == NULL)
			return B_NO_MEMORY;

		if (oldRing->ReadFromMessage(keyringData) != B_OK) {
			delete oldRing;
			continue;
		}

		Keyring* existing = _FindKeyring(oldRing->Name());
		if (existing == NULL) {
			fKeyrings.BinaryInsert(oldRing, &Keyring::Compare);
			continue;
		}

		// Merge keys from oldRing into existing keyring if both can be unlocked
		// without a per-keyring password (the common Master keyring case).
		if (!oldRing->HasUnlockKey() && oldRing->Unlock(NULL) == B_OK
			&& (!existing->HasUnlockKey() || existing->IsUnlocked())) {
			if (!existing->IsUnlocked())
				existing->Unlock(NULL);
			uint32 cookie = 0;
			BMessage keyMsg;
			while (oldRing->FindKey(B_KEY_TYPE_ANY, B_KEY_PURPOSE_ANY,
					cookie, keyMsg) == B_OK) {
				cookie++;
				BString identifier;
				BString secondary;
				if (keyMsg.FindString("identifier", &identifier) == B_OK) {
					keyMsg.FindString("secondaryIdentifier", &secondary);
					existing->AddKey(identifier, secondary, keyMsg);
				}
			}
			uint32 appCookie = 0;
			BString sig;
			BString path;
			while (oldRing->GetNextApplication(appCookie, sig, path) == B_OK) {
				BMessage appMsg;
				if (oldRing->FindApplication(sig.String(), path.String(),
						appMsg) == B_OK) {
					BMessage existingApp;
					if (existing->FindApplication(sig.String(), path.String(),
							existingApp) != B_OK) {
						existing->AddApplication(sig.String(), appMsg);
					}
				}
			}
			delete oldRing;
			continue;
		}

		// If the old keyring has its own per-keyring unlock key and the
		// colliding existing keyring is an empty post-reset placeholder,
		// replace the placeholder with the old keyring so no keys are lost.
		if (oldRing->HasUnlockKey() && !existing->HasUnlockKey()) {
			if (!existing->IsUnlocked())
				existing->Unlock(NULL);
			BMessage probe;
			if (existing->FindKey(B_KEY_TYPE_ANY, B_KEY_PURPOSE_ANY, 0, probe)
					== B_ENTRY_NOT_FOUND) {
				bool isMaster = (existing == fMasterKeyring);
				fKeyrings.RemoveItem(existing);
				fKeyrings.BinaryInsert(oldRing, &Keyring::Compare);
				if (isMaster)
					fMasterKeyring = oldRing;
				continue;
			}
		}

		// Cannot safely merge a colliding locked keyring: keep .old intact.
		delete oldRing;
		return B_BUSY;
	}
	return B_OK;
}


status_t
KeyStoreServer::_UnlockWithKek(const BSecureBuffer& kek,
	const uint8* initSalt, bool hasInitParams,
	const BPasswordHash::Parameters& initParams, bool* _hasOld)
{
	if (_hasOld != NULL)
		*_hasOld = false;
	if (fDatabasePath.IsEmpty() || fProtectorsPath.IsEmpty())
		return B_NO_INIT;

	_LockStore();

	struct stat protSt;
	bool hasProtector = (lstat(fProtectorsPath.String(), &protSt) == 0);

	if (hasProtector) {
		status_t status = R5KeyStoreCrypto::UnwrapMasterKey(
			fProtectorsPath.String(), kek, fMasterKey, fKeyStoreId);
		if (status != B_OK)
			return status;

		fStoreUnlocked = true;

		struct stat dbSt;
		if (lstat(fDatabasePath.String(), &dbSt) == 0) {
			status = _ReadKeyStoreDatabase();
			if (status != B_OK) {
				_LockStore();
				return status;
			}
		} else {
			fMasterKeyring = new(std::nothrow) Keyring(kMasterKeyringName);
			if (fMasterKeyring == NULL) {
				_LockStore();
				return B_NO_MEMORY;
			}
			fKeyrings.BinaryInsert(fMasterKeyring, &Keyring::Compare);
			status = _WriteKeyStoreDatabase();
			if (status != B_OK) {
				_LockStore();
				return status;
			}
		}
	} else {
		// First sign in with a password (fresh install or one-time migration
		// of a legacy plaintext Haiku keystore_database).
		if (!hasInitParams || initSalt == NULL)
			return B_BAD_VALUE;

		if (BRandom::Fill(fKeyStoreId, sizeof(fKeyStoreId)) != B_OK
			|| BRandom::Fill(fMasterKey, BSealedData::kKeySize) != B_OK) {
			_LockStore();
			return B_ERROR;
		}

		// If a legacy plaintext keystore_database exists, load it once so its
		// keys are migrated into the new encrypted database. Refuse to
		// overwrite an existing encrypted or unparseable non-empty database
		// when keystore_protectors is missing.
		uint8* legacyData = NULL;
		size_t legacySize = 0;
		status_t readStatus = R5KeyStoreCrypto::ReadFileBounded(
			fDatabasePath.String(), &legacyData, &legacySize);
		if (readStatus == B_OK) {
			if (R5KeyStoreCrypto::IsEncryptedDatabase(legacyData, legacySize)) {
				BSecureBuffer::WipeMemory(legacyData, legacySize);
				free(legacyData);
				_LockStore();
				return B_BAD_DATA;
			}
			if (legacySize > 0) {
				BMessage legacyMsg;
				BMemoryIO legacyIo(legacyData, legacySize);
				status_t unflattenStatus = legacyMsg.Unflatten(&legacyIo);
				BSecureBuffer::WipeMemory(legacyData, legacySize);
				free(legacyData);
				if (unflattenStatus != B_OK) {
					_LockStore();
					return B_BAD_DATA;
				}
				status_t loadStatus = _LoadKeyringsFromMessage(legacyMsg);
				if (loadStatus != B_OK) {
					_LockStore();
					return loadStatus;
				}
			} else {
				free(legacyData);
			}
		} else if (readStatus != B_ENTRY_NOT_FOUND) {
			_LockStore();
			return readStatus;
		}

		if (fMasterKeyring == NULL) {
			fMasterKeyring = new(std::nothrow) Keyring(kMasterKeyringName);
			if (fMasterKeyring == NULL) {
				_LockStore();
				return B_NO_MEMORY;
			}
			fKeyrings.BinaryInsert(fMasterKeyring, &Keyring::Compare);
		}

		status_t status = R5KeyStoreCrypto::WritePasswordProtector(
			fProtectorsPath.String(), fKeyStoreId, fMasterKey, kek, initSalt,
			initParams);
		if (status != B_OK) {
			_LockStore();
			return status;
		}

		fStoreUnlocked = true;
		status = _WriteKeyStoreDatabase();
		if (status != B_OK) {
			// Roll back protector file so next sign-in is not wedged.
			unlink(fProtectorsPath.String());
			_LockStore();
			return status;
		}
	}

	bool hasOld = _HasOldKeyStore();
	if (_hasOld != NULL)
		*_hasOld = hasOld;
	if (hasOld)
		PostMessage(kMsgShowOldKeyStorePrompt);

	return B_OK;
}


status_t
KeyStoreServer::_RewrapPasswordProtector(const BSecureBuffer& oldKek,
	const BSecureBuffer& newKek, const uint8 newSalt[16],
	const BPasswordHash::Parameters& newParams)
{
	if (fProtectorsPath.IsEmpty())
		return B_NO_INIT;
	return R5KeyStoreCrypto::RewrapPasswordProtector(fProtectorsPath.String(),
		oldKek, newKek, newSalt, newParams);
}


void
KeyStoreServer::_LockStore()
{
	fStoreUnlocked = false;
	fMasterKey.Clear();
	BSecureBuffer::WipeMemory(fKeyStoreId, sizeof(fKeyStoreId));
	fKeyrings.MakeEmpty();
	fMasterKeyring = NULL;
}


bool
KeyStoreServer::_HasOldKeyStore() const
{
	if (fOldDatabasePath.IsEmpty() || fOldProtectorsPath.IsEmpty())
		return false;
	struct stat st;
	return lstat(fOldDatabasePath.String(), &st) == 0
		&& lstat(fOldProtectorsPath.String(), &st) == 0;
}


status_t
KeyStoreServer::RecoverOldKeyStore(const char* oldPassword)
{
	if (oldPassword == NULL || oldPassword[0] == '\0')
		return B_BAD_VALUE;
	if (!Lock())
		return B_ERROR;

	status_t status = B_OK;
	if (!fStoreUnlocked || !_HasOldKeyStore()) {
		Unlock();
		return B_ENTRY_NOT_FOUND;
	}

	R5PasswordProtectorParams oldParams;
	status = R5KeyStoreCrypto::ReadPasswordProtectorParams(
		fOldProtectorsPath.String(), oldParams);
	if (status != B_OK) {
		Unlock();
		return status;
	}

	BSecureBuffer oldKek;
	status = BPasswordHash::DeriveKey(oldPassword, strlen(oldPassword),
		oldParams.salt, sizeof(oldParams.salt), oldParams.kdf, oldKek,
		BSealedData::kKeySize);
	if (status != B_OK) {
		Unlock();
		return status;
	}

	BSecureBuffer oldMasterKey;
	uint8 oldKeystoreId[kR5KSIdSize];
	status = R5KeyStoreCrypto::UnwrapMasterKey(fOldProtectorsPath.String(),
		oldKek, oldMasterKey, oldKeystoreId);
	oldKek.Clear();
	if (status != B_OK) {
		Unlock();
		return status;
	}

	BMessage oldKeystore;
	status = R5KeyStoreCrypto::ReadEncryptedDatabase(
		fOldDatabasePath.String(), oldKeystoreId, oldMasterKey, oldKeystore);
	oldMasterKey.Clear();
	if (status != B_OK) {
		Unlock();
		return status;
	}

	status = _MergeKeyringsFromMessage(oldKeystore);
	if (status == B_OK)
		status = _WriteKeyStoreDatabase();
	if (status == B_OK) {
		unlink(fOldDatabasePath.String());
		unlink(fOldProtectorsPath.String());
	}

	Unlock();
	return status;
}
// SPDX-SnippetEnd


int
main(int argc, char* argv[])
{
	KeyStoreServer* app = new(std::nothrow) KeyStoreServer();
	if (app == NULL)
		return 1;

	app->Run();
	delete app;
	return 0;
}
