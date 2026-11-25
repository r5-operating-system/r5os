/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * R5 encrypted key store format helpers (R5KS v2 database + R5KP v1 protectors).
 * Shared between keystore_server, Login/AccountAdmin, and unit tests.
 */
#ifndef _R5_KEYSTORE_CRYPTO_H
#define _R5_KEYSTORE_CRYPTO_H


#include "KeyStoreProtocol.h"

#include <Message.h>
#include <R5Security.h>

#include <sys/types.h>


namespace BPrivate {


struct R5PasswordProtectorParams {
	BSecurityKit::BPasswordHash::Parameters	kdf;
	uint8									salt[16];
	uint8									keystoreId[kR5KSIdSize];
};


class R5KeyStoreCrypto {
public:
	// Detect whether a buffer starts with the R5KS v2 encrypted header.
	static	bool				IsEncryptedDatabase(const void* data,
									size_t size);

	// Read only the public Argon2id parameters and salt from a protector file
	// (used by Login to derive the KEK without reading keystore_database).
	static	status_t			ReadPasswordProtectorParams(
									const char* protectorsPath,
									R5PasswordProtectorParams& outParams);

	// Safe uid-0 variant: traverses homeDir with O_NOFOLLOW | O_DIRECTORY
	// and verifies directory ownership before reading keystore_protectors.
	static	status_t			ReadAccountPasswordProtectorParams(
									const char* homeDir, uid_t expectedUid,
									R5PasswordProtectorParams& outParams);

	// Safe uid-0 variant of RewrapPasswordProtector using dirfd traversal.
	static	status_t			RewrapAccountPasswordProtector(
									const char* homeDir, uid_t ownerUid,
									gid_t ownerGid,
									const BSecurityKit::BSecureBuffer& oldKek,
									const BSecurityKit::BSecureBuffer& newKek,
									const uint8 newSalt[16],
									const BSecurityKit::BPasswordHash::Parameters& newParams);

	// Create a new protector file wrapping masterKey under kek (with given
	// salt, KDF params, and keystoreId). Writes atomically with mode 0600.
	static	status_t			WritePasswordProtector(
									const char* protectorsPath,
									const uint8 keystoreId[kR5KSIdSize],
									const BSecurityKit::BSecureBuffer& masterKey,
									const BSecurityKit::BSecureBuffer& kek,
									const uint8 salt[16],
									const BSecurityKit::BPasswordHash::Parameters& params,
									uid_t ownerUid = (uid_t)-1,
									gid_t ownerGid = (gid_t)-1);

	// Unwrap the 32-byte master key and read keystoreId from protectorsPath
	// using the Argon2id-derived 32-byte kek.
	static	status_t			UnwrapMasterKey(const char* protectorsPath,
									const BSecurityKit::BSecureBuffer& kek,
									BSecurityKit::BSecureBuffer& outMasterKey,
									uint8 outKeystoreId[kR5KSIdSize]);

	// Rewrap only the password protector in protectorsPath (preserving any
	// other protectors such as future TPM/recovery entries).
	static	status_t			RewrapPasswordProtector(
									const char* protectorsPath,
									const BSecurityKit::BSecureBuffer& oldKek,
									const BSecurityKit::BSecureBuffer& newKek,
									const uint8 newSalt[16],
									const BSecurityKit::BPasswordHash::Parameters& newParams,
									uid_t ownerUid = (uid_t)-1,
									gid_t ownerGid = (gid_t)-1);

	// Seal a flattened keystore BMessage into R5KS v2 format and write
	// atomically to databasePath with mode 0600.
	static	status_t			WriteEncryptedDatabase(
									const char* databasePath,
									const uint8 keystoreId[kR5KSIdSize],
									const BSecurityKit::BSecureBuffer& masterKey,
									const BMessage& keystoreMessage,
									uid_t ownerUid = (uid_t)-1,
									gid_t ownerGid = (gid_t)-1);

	// Read and open an R5KS v2 encrypted database file, verifying its
	// keystore_id matches expectedKeystoreId.
	static	status_t			ReadEncryptedDatabase(
									const char* databasePath,
									const uint8 expectedKeystoreId[kR5KSIdSize],
									const BSecurityKit::BSecureBuffer& masterKey,
									BMessage& outKeystoreMessage);

	// Read a raw file safely (O_NOFOLLOW | O_NONBLOCK, regular file, size cap).
	static	status_t			ReadFileBounded(const char* path,
									uint8** _outData, size_t* _outSize,
									size_t maxSize = 64 * 1024 * 1024);

	// Atomic write (temp file, mode 0600, optional fchown, fsync, rename).
	static	status_t			WriteFileAtomic(const char* path,
									const void* data, size_t size,
									uid_t ownerUid = (uid_t)-1,
									gid_t ownerGid = (gid_t)-1);

	// Account lifecycle helpers (called by Login / AccountAdmin as uid 0):
	// Create an empty encrypted keystore + password protector for a new account.
	static	status_t			CreateAccountKeyStore(const char* homeDir,
									uid_t uid, gid_t gid,
									const char* password);

	// Administrator password reset: preserve existing database+protectors as
	// .old (without clobbering an existing .old), then create a fresh empty
	// encrypted keystore wrapped with newPassword.
	static	status_t			ResetAccountKeyStore(const char* homeDir,
									uid_t uid, gid_t gid,
									const char* newPassword);

	// Move current keystore_database (and optionally keystore_protectors) into
	// the next free .old / .old2 .. .old16 slot inside dirPath. Returns
	// B_DEVICE_FULL if all backup slots are occupied (never overwrites).
	static	status_t			PreserveCurrentAsOld(const char* dirPath,
									bool moveProtector,
									BString* _outSuffix = NULL);

	// Format backup file names for slot 1..kR5KSMaxOldBackups (slot 1 -> ".old",
	// slot 2 -> ".old2", ...).
	static	void				OldBackupNames(int32 slot, BString& outDbName,
									BString& outProtName);

	// Build ~/config/settings/system/keystore directory path for a home dir.
	static	status_t			BuildKeyStorePaths(const char* homeDir,
									BString& outDir, BString& outDatabase,
									BString& outProtectors);

	// Review fix (24 Nov 2026): Login runs the Argon2id values read from an
	// account's own protector file as uid 0, before the seal is checked.
	// Only the fixed choice (at most 64 MiB, 3 passes, 1 lane) is accepted.
	static	bool				IsAllowedPasswordParams(
									const BSecurityKit::BPasswordHash::Parameters& params);
};


}	// namespace BPrivate


#endif	// _R5_KEYSTORE_CRYPTO_H
