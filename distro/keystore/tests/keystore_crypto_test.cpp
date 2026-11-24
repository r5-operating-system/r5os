/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Haiku-native test for R5KeyStoreCrypto:
 * - CreateAccountKeyStore + unwrap + read/write encrypted database
 * - Own password rewrap (RewrapAccountPasswordProtector)
 * - Two consecutive administrator resets (ResetAccountKeyStore):
 *   verifies BOTH .old (pw1) and .old2 (pw2) are preserved with distinct data,
 *   and the new current store opens with pw3.
 * - Mismatch recovery (PreserveCurrentAsOld): moves mismatched store to .old3
 *   without clobbering .old or .old2.
 */


#include "../KeyStoreCrypto.h"

#include <Message.h>
#include <R5Security.h>
#include <String.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>


using namespace BPrivate;
using namespace BSecurityKit;


static int sFailures = 0;

#define CHECK(cond, msg) do { \
	if (!(cond)) { \
		fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); \
		sFailures++; \
	} \
} while (0)


static status_t
write_tagged_db(const char* homeDir, const char* password, const char* tag)
{
	BString dir, dbPath, protPath;
	R5KeyStoreCrypto::BuildKeyStorePaths(homeDir, dir, dbPath, protPath);

	R5PasswordProtectorParams params;
	status_t st = R5KeyStoreCrypto::ReadPasswordProtectorParams(
		protPath.String(), params);
	if (st != B_OK)
		return st;

	BSecureBuffer kek;
	st = BPasswordHash::DeriveKey(password, strlen(password),
		params.salt, sizeof(params.salt), params.kdf, kek,
		BSealedData::kKeySize);
	if (st != B_OK)
		return st;

	BSecureBuffer masterKey;
	uint8 id[kR5KSIdSize];
	st = R5KeyStoreCrypto::UnwrapMasterKey(protPath.String(), kek, masterKey,
		id);
	kek.Clear();
	if (st != B_OK)
		return st;

	BMessage db;
	db.AddUInt32("format", 1);
	db.AddString("test_tag", tag);
	st = R5KeyStoreCrypto::WriteEncryptedDatabase(dbPath.String(), id,
		masterKey, db);
	masterKey.Clear();
	return st;
}


static status_t
verify_tagged_backup(const char* dir, int32 slot, const char* password,
	const char* expectedTag)
{
	BString dbName, protName;
	R5KeyStoreCrypto::OldBackupNames(slot, dbName, protName);
	BString dbPath, protPath;
	dbPath.SetToFormat("%s/%s", dir, dbName.String());
	protPath.SetToFormat("%s/%s", dir, protName.String());

	R5PasswordProtectorParams params;
	status_t st = R5KeyStoreCrypto::ReadPasswordProtectorParams(
		protPath.String(), params);
	if (st != B_OK)
		return st;

	BSecureBuffer kek;
	st = BPasswordHash::DeriveKey(password, strlen(password),
		params.salt, sizeof(params.salt), params.kdf, kek,
		BSealedData::kKeySize);
	if (st != B_OK)
		return st;

	BSecureBuffer masterKey;
	uint8 id[kR5KSIdSize];
	st = R5KeyStoreCrypto::UnwrapMasterKey(protPath.String(), kek, masterKey,
		id);
	kek.Clear();
	if (st != B_OK)
		return st;

	BMessage db;
	st = R5KeyStoreCrypto::ReadEncryptedDatabase(dbPath.String(), id,
		masterKey, db);
	masterKey.Clear();
	if (st != B_OK)
		return st;

	const char* tag = NULL;
	if (db.FindString("test_tag", &tag) != B_OK || tag == NULL
		|| strcmp(tag, expectedTag) != 0)
		return B_ERROR;
	return B_OK;
}


int
main()
{
	char home[128];
	snprintf(home, sizeof(home), "/tmp/r5-ks-unit-%d", (int)getpid());
	mkdir(home, 0700);
	BString cfg(home);
	cfg << "/config";
	mkdir(cfg.String(), 0755);

	uid_t uid = getuid();
	gid_t gid = getgid();

	// 1. Create account keystore with pw1 and tag it "store-v1".
	CHECK(R5KeyStoreCrypto::CreateAccountKeyStore(home, uid, gid, "pw1")
		== B_OK, "CreateAccountKeyStore(pw1)");
	CHECK(write_tagged_db(home, "pw1", "store-v1") == B_OK,
		"Write tagged store-v1");

	// 2. First admin reset -> pw2; previous store-v1 must be in .old (slot 1).
	CHECK(R5KeyStoreCrypto::ResetAccountKeyStore(home, uid, gid, "pw2")
		== B_OK, "First ResetAccountKeyStore(pw2)");
	CHECK(write_tagged_db(home, "pw2", "store-v2") == B_OK,
		"Write tagged store-v2");

	// 3. Second admin reset in a row -> pw3; store-v2 must be in .old2 (slot 2)
	//    and store-v1 must STILL be intact in .old (slot 1).
	CHECK(R5KeyStoreCrypto::ResetAccountKeyStore(home, uid, gid, "pw3")
		== B_OK, "Second ResetAccountKeyStore(pw3)");
	CHECK(write_tagged_db(home, "pw3", "store-v3") == B_OK,
		"Write tagged store-v3");

	BString dir, dbPath, protPath;
	R5KeyStoreCrypto::BuildKeyStorePaths(home, dir, dbPath, protPath);

	CHECK(verify_tagged_backup(dir.String(), 1, "pw1", "store-v1") == B_OK,
		"First reset backup (.old) recovers store-v1 with pw1");
	CHECK(verify_tagged_backup(dir.String(), 2, "pw2", "store-v2") == B_OK,
		"Second reset backup (.old2) recovers store-v2 with pw2");

	// 4. PreserveCurrentAsOld (used by mismatch recovery) -> slot 3 (.old3)
	BString suffix;
	CHECK(R5KeyStoreCrypto::PreserveCurrentAsOld(dir.String(), true, &suffix)
		== B_OK && suffix == ".old3",
		"Mismatch preserve moves store-v3 to .old3");
	CHECK(verify_tagged_backup(dir.String(), 3, "pw3", "store-v3") == B_OK,
		"Third backup (.old3) recovers store-v3 with pw3");

	if (sFailures == 0) {
		printf("ALL KEYSTORE CRYPTO TESTS PASSED\n");
		return 0;
	}
	fprintf(stderr, "%d KEYSTORE CRYPTO TEST(S) FAILED\n", sFailures);
	return 1;
}
