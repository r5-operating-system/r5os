/*
 * Copyright 2012, Michael Lotz, mmlr@mlotz.ch. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KEY_STORE_SERVER_H
#define _KEY_STORE_SERVER_H


#include <Application.h>
#include <File.h>
#include <Key.h>
#include <ObjectList.h>
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <R5Admin.h>
#include <R5Security.h>
#include <String.h>

#include "KeyStoreProtocol.h"
// SPDX-SnippetEnd


struct app_info;
class Keyring;

typedef BObjectList<Keyring, true> KeyringList;


class KeyStoreServer : public BApplication {
public:
									KeyStoreServer();
virtual								~KeyStoreServer();

virtual	void						MessageReceived(BMessage* message);

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		status_t					RecoverOldKeyStore(const char* oldPassword);
// SPDX-SnippetEnd

private:
		status_t					_ReadKeyStoreDatabase();
		status_t					_WriteKeyStoreDatabase();

		uint32						_AccessFlagsFor(uint32 command) const;
		const char*					_AccessStringFor(uint32 accessFlag) const;
		status_t					_ResolveCallingApp(const BMessage& message,
										app_info& callingAppInfo) const;

		status_t					_ValidateAppAccess(Keyring& keyring,
										const app_info& appInfo,
										uint32 accessFlags);
		status_t					_RequestAppAccess(
										const BString& keyringName,
										const char* signature,
										const char* path,
										const char* accessString, bool appIsNew,
										bool appWasUpdated, uint32 accessFlags,
										bool& allowAlways);

		Keyring*					_FindKeyring(const BString& name);

		status_t					_AddKeyring(const BString& name);
		status_t					_RemoveKeyring(const BString& name);

		status_t					_UnlockKeyring(Keyring& keyring);

		status_t					_RequestKey(const BString& keyringName,
										BMessage& keyMessage);

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	static	void					_AdminRequest(BMessage* request,
										const r5_admin_sender& sender,
										BMessage* reply, void* cookie);
		status_t					_LoadKeyringsFromMessage(
										const BMessage& keystore);
		status_t					_MergeKeyringsFromMessage(
										const BMessage& oldKeystore);
		status_t					_UnlockWithKek(
										const BSecurityKit::BSecureBuffer& kek,
										const uint8* initSalt,
										bool hasInitParams,
										const BSecurityKit::BPasswordHash::Parameters& initParams,
										bool recoverMismatch,
										bool* _hasOld);
		status_t					_RewrapPasswordProtector(
										const BSecurityKit::BSecureBuffer& oldKek,
										const BSecurityKit::BSecureBuffer& newKek,
										const uint8 newSalt[16],
										const BSecurityKit::BPasswordHash::Parameters& newParams);
		status_t					_UnlockInMemory();
		void						_LockStore();
		bool						_HasOldKeyStore() const;
		void						_PostLockedAlert(const char* text);
		void						_PostSlotsFullAlert();
// SPDX-SnippetEnd

		Keyring*					fMasterKeyring;
		KeyringList					fKeyrings;
		BFile						fKeyStoreFile;

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		port_id						fAdminPort;
		bool						fStoreUnlocked;
		bool						fMemoryOnly;
		BSecurityKit::BSecureBuffer	fMasterKey;
		uint8						fKeyStoreId[BPrivate::kR5KSIdSize];
		BString						fKeyStoreDir;
		BString						fDatabasePath;
		BString						fProtectorsPath;
		BString						fOldDatabasePath;
		BString						fOldProtectorsPath;
// SPDX-SnippetEnd
};


#endif // _KEY_STORE_SERVER_H
