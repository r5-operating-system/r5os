/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Private protocol between Login (uid 0) and keystore_server (session user)
 * over keystore_server's libr5admin port, plus R5KS / R5KP format constants.
 */
#ifndef _R5_KEYSTORE_PROTOCOL_H
#define _R5_KEYSTORE_PROTOCOL_H


#include <SupportDefs.h>


namespace BPrivate {


// Admin-port messages accepted by keystore_server (only from Login, uid 0).
static const uint32 kMsgR5KeyStoreUnlock = 'r5KU';
static const uint32 kMsgR5KeyStoreRewrap = 'r5KR';
static const uint32 kMsgR5KeyStoreLock   = 'r5KL';

// Message field names (never logged).
static const char* const kR5KSFieldKek        = "kek";
static const char* const kR5KSFieldOldKek     = "old_kek";
static const char* const kR5KSFieldNewKek     = "new_kek";
static const char* const kR5KSFieldSalt       = "salt";
static const char* const kR5KSFieldMemKiB     = "kdf_mem_kib";
static const char* const kR5KSFieldPasses     = "kdf_passes";
static const char* const kR5KSFieldLanes      = "kdf_lanes";
static const char* const kR5KSFieldStatus     = "status";
static const char* const kR5KSFieldHasOld     = "has_old";
static const char* const kR5KSFieldRecoverMismatch = "recover_mismatch";
// Accounts step 5: Try R5 (the live medium) has no password. Login asks for a
// key store that lives in memory only; nothing is read from or written to
// disk, and everything in it is gone at log out or shut down.
static const char* const kR5KSFieldMemoryOnly = "memory_only";

static const int32 kR5KSMaxOldBackups = 16;

// File names under ~/config/settings/system/keystore/
static const char* const kR5KSDatabaseFile    = "keystore_database";
static const char* const kR5KSProtectorsFile  = "keystore_protectors";
static const char* const kR5KSOldDatabaseFile = "keystore_database.old";
static const char* const kR5KSOldProtectorsFile = "keystore_protectors.old";

// R5KS encrypted database format (version 2):
//   bytes 0..3   : magic "R5KS"
//   bytes 4..7   : uint32 format = 2 (little-endian)
//   bytes 8..23  : 16-byte random keystore_id
//   bytes 24..   : BSealedData (R5SD) blob sealed under the 32-byte master key
//                  with AAD = "R5KS:v2:" || keystore_id
static const uint32 kR5KSEncryptedFormatVersion = 2;
static const size_t kR5KSHeaderSize = 4 + 4 + 16;	// 24 bytes
static const size_t kR5KSIdSize = 16;

// R5KP protector file format (version 1, flattened BMessage):
//   "magic"       : "R5KP"
//   "format"      : uint32(1)
//   "keystore_id" : 16 bytes
//   "protectors"  : repeated BMessage:
//       "id"          : BString ("password", future "tpm2", "recovery")
//       "type"        : uint32 (kR5ProtectorPasswordArgon2id = 1, ...)
//       "created"     : int64 (Unix seconds)
//       "kdf_mem_kib" : uint32 (type 1)
//       "kdf_passes"  : uint32 (type 1)
//       "kdf_lanes"   : uint32 (type 1)
//       "salt"        : 16 bytes (type 1)
//       "wrapped_key" : BSealedData blob wrapping 32-byte master key
//                       with AAD = "R5KP:v1:" || keystore_id || id
static const uint32 kR5KPFormatVersion = 1;
static const uint32 kR5ProtectorPasswordArgon2id = 1;
static const uint32 kR5ProtectorTPM2 = 2;			// reserved (#4589)
static const uint32 kR5ProtectorRecoveryKey = 3;	// reserved

static const char* const kR5ProtectorPasswordId = "password";


}	// namespace BPrivate


#endif	// _R5_KEYSTORE_PROTOCOL_H
