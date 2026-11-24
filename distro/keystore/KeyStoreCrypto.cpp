/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * R5 encrypted key store format helpers (R5KS v2 database + R5KP v1 protectors).
 */


#include "KeyStoreCrypto.h"

#include <DataIO.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>


using namespace BSecurityKit;


namespace BPrivate {

namespace {


static const uint8 kR5KSMagic[4] = { 'R', '5', 'K', 'S' };
static const char kR5KPMagic[] = "R5KP";
static const char kR5KPAadPrefix[] = "R5KP:v1:";
static const char kR5KSAadPrefix[] = "R5KS:v2:";


static void
write_le32(uint8 out[4], uint32 value)
{
	out[0] = static_cast<uint8>(value & 0xFF);
	out[1] = static_cast<uint8>((value >> 8) & 0xFF);
	out[2] = static_cast<uint8>((value >> 16) & 0xFF);
	out[3] = static_cast<uint8>((value >> 24) & 0xFF);
}


static uint32
read_le32(const uint8 in[4])
{
	return static_cast<uint32>(in[0])
		| (static_cast<uint32>(in[1]) << 8)
		| (static_cast<uint32>(in[2]) << 16)
		| (static_cast<uint32>(in[3]) << 24);
}


static void
build_db_aad(const uint8 keystoreId[kR5KSIdSize], uint8 out[8 + kR5KSIdSize])
{
	memcpy(out, kR5KSAadPrefix, 8);
	memcpy(out + 8, keystoreId, kR5KSIdSize);
}


static status_t
build_protector_aad(const uint8 keystoreId[kR5KSIdSize], const char* id,
	uint8* out, size_t outCapacity, size_t* _outSize)
{
	if (id == NULL || id[0] == '\0')
		return B_BAD_VALUE;
	size_t idLen = strlen(id);
	size_t total = 8 + kR5KSIdSize + idLen;
	if (total > outCapacity)
		return B_BUFFER_OVERFLOW;
	memcpy(out, kR5KPAadPrefix, 8);
	memcpy(out + 8, keystoreId, kR5KSIdSize);
	memcpy(out + 8 + kR5KSIdSize, id, idLen);
	*_outSize = total;
	return B_OK;
}


// Open a directory component safely relative to parentFd without following
// symlinks. When uid 0 traverses a user home directory, verify that each
// component is owned by ownerUid or root (e.g. ~/config is packagefs uid 0).
static status_t
open_component_dirfd(int parentFd, const char* name, mode_t mode,
	uid_t ownerUid, gid_t ownerGid, bool create, int* _outFd)
{
	*_outFd = -1;
	int fd;
	do {
		fd = openat(parentFd, name,
			O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	} while (fd < 0 && errno == EINTR);

	if (fd < 0 && errno == ENOENT && create) {
		if (mkdirat(parentFd, name, mode) != 0 && errno != EEXIST)
			return B_IO_ERROR;
		do {
			fd = openat(parentFd, name,
				O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		} while (fd < 0 && errno == EINTR);
		if (fd >= 0 && ownerUid != (uid_t)-1) {
			fchown(fd, ownerUid, ownerGid);
			fchmod(fd, mode);
		}
	}
	if (fd < 0) {
		if (errno == ENOENT)
			return B_ENTRY_NOT_FOUND;
		if (errno == ELOOP || errno == ENOTDIR)
			return B_NOT_ALLOWED;
		return B_IO_ERROR;
	}

	struct stat st;
	if (fstat(fd, &st) != 0 || !S_ISDIR(st.st_mode)) {
		close(fd);
		return B_NOT_ALLOWED;
	}
	if (ownerUid != (uid_t)-1 && st.st_uid != ownerUid && st.st_uid != 0) {
		close(fd);
		return B_NOT_ALLOWED;
	}

	*_outFd = fd;
	return B_OK;
}


static status_t
open_keystore_dirfd(const char* homeDir, uid_t ownerUid, gid_t ownerGid,
	bool create, int* _outDirFd)
{
	*_outDirFd = -1;
	if (homeDir == NULL || homeDir[0] != '/')
		return B_BAD_VALUE;

	int curFd;
	do {
		curFd = open(homeDir, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	} while (curFd < 0 && errno == EINTR);
	if (curFd < 0)
		return errno == ENOENT ? B_ENTRY_NOT_FOUND : B_NOT_ALLOWED;

	struct stat homeSt;
	if (fstat(curFd, &homeSt) != 0 || !S_ISDIR(homeSt.st_mode)
		|| (ownerUid != (uid_t)-1 && homeSt.st_uid != ownerUid
			&& homeSt.st_uid != 0)) {
		close(curFd);
		return B_NOT_ALLOWED;
	}

	const char* components[] = { "config", "settings", "system", "keystore" };
	const mode_t modes[]     = { 0755,     0755,       0755,     0700 };
	for (size_t i = 0; i < 4; i++) {
		int nextFd = -1;
		// Never try to create "config" (packagefs mount root).
		bool canCreate = create && (i > 0);
		status_t status = open_component_dirfd(curFd, components[i], modes[i],
			ownerUid, ownerGid, canCreate, &nextFd);
		close(curFd);
		if (status != B_OK)
			return status;
		curFd = nextFd;
	}

	if (create) {
		if (ownerUid != (uid_t)-1)
			fchown(curFd, ownerUid, ownerGid);
		fchmod(curFd, 0700);
	}

	*_outDirFd = curFd;
	return B_OK;
}


static status_t
read_file_bounded_at(int dirFd, const char* pathOrLeaf, uint8** _outData,
	size_t* _outSize, size_t maxSize)
{
	*_outData = NULL;
	*_outSize = 0;
	if (pathOrLeaf == NULL || pathOrLeaf[0] == '\0')
		return B_BAD_VALUE;

	int fd;
	do {
		fd = openat(dirFd, pathOrLeaf,
			O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	} while (fd < 0 && errno == EINTR);
	if (fd < 0) {
		int err = errno;
		if (err == ENOENT)
			return B_ENTRY_NOT_FOUND;
		if (err == ELOOP)
			return B_BAD_DATA;
		return B_IO_ERROR;
	}

	struct stat st;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
		close(fd);
		return B_BAD_DATA;
	}
	if (st.st_size < 0 || static_cast<uint64>(st.st_size) > maxSize) {
		close(fd);
		return B_BAD_DATA;
	}

	size_t fileSize = static_cast<size_t>(st.st_size);
	uint8* buf = static_cast<uint8*>(malloc(fileSize > 0 ? fileSize : 1));
	if (buf == NULL) {
		close(fd);
		return B_NO_MEMORY;
	}

	size_t total = 0;
	while (total < fileSize) {
		ssize_t n = read(fd, buf + total, fileSize - total);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			free(buf);
			close(fd);
			return B_IO_ERROR;
		}
		if (n == 0) {
			free(buf);
			close(fd);
			return B_BAD_DATA;
		}
		total += static_cast<size_t>(n);
	}
	close(fd);

	*_outData = buf;
	*_outSize = fileSize;
	return B_OK;
}


static status_t
write_file_atomic_at(int dirFd, const char* pathOrLeaf, const void* data,
	size_t size, uid_t ownerUid, gid_t ownerGid)
{
	if (pathOrLeaf == NULL || pathOrLeaf[0] == '\0'
		|| (data == NULL && size > 0))
		return B_BAD_VALUE;

	uint8 suffixBytes[8];
	status_t status = BRandom::Fill(suffixBytes, sizeof(suffixBytes));
	if (status != B_OK)
		return status;

	char suffix[17];
	for (size_t i = 0; i < sizeof(suffixBytes); i++)
		snprintf(suffix + i * 2, 3, "%02x", suffixBytes[i]);

	BString tempPath(pathOrLeaf);
	tempPath << ".tmp." << suffix;

	int fd;
	do {
		fd = openat(dirFd, tempPath.String(),
			O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
	} while (fd < 0 && errno == EINTR);
	if (fd < 0)
		return B_IO_ERROR;

	const uint8* src = static_cast<const uint8*>(data);
	size_t written = 0;
	status = B_OK;
	while (written < size) {
		ssize_t n = write(fd, src + written, size - written);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			status = B_IO_ERROR;
			break;
		}
		if (n == 0) {
			status = B_IO_ERROR;
			break;
		}
		written += static_cast<size_t>(n);
	}

	if (status == B_OK && ownerUid != (uid_t)-1) {
		if (fchown(fd, ownerUid, ownerGid) != 0)
			status = B_IO_ERROR;
	}
	if (status == B_OK && fchmod(fd, 0600) != 0)
		status = B_IO_ERROR;
	if (status == B_OK && fsync(fd) != 0)
		status = B_IO_ERROR;
	if (close(fd) != 0 && status == B_OK)
		status = B_IO_ERROR;

	if (status != B_OK) {
		unlinkat(dirFd, tempPath.String(), 0);
		return status;
	}
	if (renameat(dirFd, tempPath.String(), dirFd, pathOrLeaf) != 0) {
		unlinkat(dirFd, tempPath.String(), 0);
		return B_IO_ERROR;
	}
	return B_OK;
}


static status_t
seal_password_protector_entry(const uint8 keystoreId[kR5KSIdSize],
	const BSecureBuffer& masterKey, const BSecureBuffer& kek,
	const uint8 salt[16], const BPasswordHash::Parameters& params,
	BMessage& outEntry)
{
	if (masterKey.Size() != BSealedData::kKeySize
		|| kek.Size() != BSealedData::kKeySize
		|| !R5KeyStoreCrypto::IsAllowedPasswordParams(params))
		return B_BAD_VALUE;

	uint8 aad[128];
	size_t aadLen = 0;
	status_t status = build_protector_aad(keystoreId, kR5ProtectorPasswordId,
		aad, sizeof(aad), &aadLen);
	if (status != B_OK)
		return status;

	uint8 wrapped[BSealedData::kOverhead + BSealedData::kKeySize];
	size_t wrappedSize = 0;
	status = BSealedData::Seal(kek, masterKey.Data(), masterKey.Size(),
		aad, aadLen, wrapped, sizeof(wrapped), &wrappedSize);
	if (status != B_OK)
		return status;

	outEntry.MakeEmpty();
	if (outEntry.AddString("id", kR5ProtectorPasswordId) != B_OK
		|| outEntry.AddUInt32("type", kR5ProtectorPasswordArgon2id) != B_OK
		|| outEntry.AddInt64("created", static_cast<int64>(time(NULL))) != B_OK
		|| outEntry.AddUInt32(kR5KSFieldMemKiB, params.memoryKiB) != B_OK
		|| outEntry.AddUInt32(kR5KSFieldPasses, params.passes) != B_OK
		|| outEntry.AddUInt32(kR5KSFieldLanes, params.lanes) != B_OK
		|| outEntry.AddData(kR5KSFieldSalt, B_RAW_TYPE, salt, 16) != B_OK
		|| outEntry.AddData("wrapped_key", B_RAW_TYPE, wrapped, wrappedSize)
			!= B_OK) {
		return B_NO_MEMORY;
	}
	return B_OK;
}


static status_t
load_protectors_message_at(int dirFd, const char* pathOrLeaf, BMessage& outMsg,
	uint8 outKeystoreId[kR5KSIdSize])
{
	uint8* data = NULL;
	size_t size = 0;
	status_t status = read_file_bounded_at(dirFd, pathOrLeaf, &data, &size,
		1024 * 1024);
	if (status != B_OK)
		return status;

	BMemoryIO io(data, size);
	status = outMsg.Unflatten(&io);
	free(data);
	if (status != B_OK)
		return B_BAD_DATA;

	const char* magic = NULL;
	uint32 format = 0;
	const void* idData = NULL;
	ssize_t idSize = 0;
	if (outMsg.FindString("magic", &magic) != B_OK
		|| magic == NULL || strcmp(magic, kR5KPMagic) != 0
		|| outMsg.FindUInt32("format", &format) != B_OK
		|| format != kR5KPFormatVersion
		|| outMsg.FindData("keystore_id", B_RAW_TYPE, &idData, &idSize) != B_OK
		|| idSize != static_cast<ssize_t>(kR5KSIdSize)) {
		return B_BAD_DATA;
	}

	memcpy(outKeystoreId, idData, kR5KSIdSize);
	return B_OK;
}


static status_t
write_protectors_message_at(int dirFd, const char* pathOrLeaf,
	const BMessage& msg, uid_t ownerUid, gid_t ownerGid)
{
	ssize_t flatSize = msg.FlattenedSize();
	if (flatSize <= 0 || flatSize > 1024 * 1024)
		return B_BAD_DATA;

	uint8* flat = static_cast<uint8*>(malloc(static_cast<size_t>(flatSize)));
	if (flat == NULL)
		return B_NO_MEMORY;

	status_t status = msg.Flatten(reinterpret_cast<char*>(flat), flatSize);
	if (status == B_OK) {
		status = write_file_atomic_at(dirFd, pathOrLeaf, flat,
			static_cast<size_t>(flatSize), ownerUid, ownerGid);
	}
	free(flat);
	return status;
}


static status_t
unwrap_master_key_from_message(const BMessage& msg,
	const uint8 keystoreId[kR5KSIdSize], const BSecureBuffer& kek,
	BSecureBuffer& outMasterKey)
{
	outMasterKey.Clear();
	if (kek.Size() != BSealedData::kKeySize)
		return B_BAD_VALUE;

	BMessage entry;
	for (int32 i = 0; msg.FindMessage("protectors", i, &entry) == B_OK; i++) {
		uint32 type = 0;
		const char* id = NULL;
		if (entry.FindUInt32("type", &type) != B_OK
			|| type != kR5ProtectorPasswordArgon2id
			|| entry.FindString("id", &id) != B_OK)
			continue;

		const void* wrapped = NULL;
		ssize_t wrappedSize = 0;
		if (entry.FindData("wrapped_key", B_RAW_TYPE, &wrapped, &wrappedSize)
				!= B_OK
			|| wrappedSize < static_cast<ssize_t>(BSealedData::kOverhead)) {
			return B_BAD_DATA;
		}

		uint8 aad[128];
		size_t aadLen = 0;
		status_t status = build_protector_aad(keystoreId, id, aad, sizeof(aad),
			&aadLen);
		if (status != B_OK)
			return status;

		BSecureBuffer unwrapped;
		status = BSealedData::Open(kek, wrapped,
			static_cast<size_t>(wrappedSize), aad, aadLen, unwrapped);
		if (status != B_OK)
			return status;
		if (unwrapped.Size() != BSealedData::kKeySize) {
			unwrapped.Clear();
			return B_BAD_DATA;
		}
		return outMasterKey.SetTo(unwrapped.Data(), unwrapped.Size());
	}
	return B_ENTRY_NOT_FOUND;
}


static status_t
read_password_params_from_message(const BMessage& msg,
	const uint8 keystoreId[kR5KSIdSize], R5PasswordProtectorParams& outParams)
{
	memcpy(outParams.keystoreId, keystoreId, kR5KSIdSize);
	BMessage entry;
	for (int32 i = 0; msg.FindMessage("protectors", i, &entry) == B_OK; i++) {
		uint32 type = 0;
		if (entry.FindUInt32("type", &type) != B_OK
			|| type != kR5ProtectorPasswordArgon2id)
			continue;

		uint32 mem = 0;
		uint32 passes = 0;
		uint32 lanes = 0;
		const void* salt = NULL;
		ssize_t saltSize = 0;
		if (entry.FindUInt32(kR5KSFieldMemKiB, &mem) != B_OK
			|| entry.FindUInt32(kR5KSFieldPasses, &passes) != B_OK
			|| entry.FindUInt32(kR5KSFieldLanes, &lanes) != B_OK
			|| entry.FindData(kR5KSFieldSalt, B_RAW_TYPE, &salt, &saltSize)
				!= B_OK
			|| saltSize != 16) {
			return B_BAD_DATA;
		}

		outParams.kdf.memoryKiB = mem;
		outParams.kdf.passes = passes;
		outParams.kdf.lanes = lanes;
		if (!R5KeyStoreCrypto::IsAllowedPasswordParams(outParams.kdf))
			return B_BAD_DATA;
		memcpy(outParams.salt, salt, 16);
		return B_OK;
	}
	return B_ENTRY_NOT_FOUND;
}


static status_t
rewrap_password_protector_at(int dirFd, const char* pathOrLeaf,
	const BSecureBuffer& oldKek, const BSecureBuffer& newKek,
	const uint8 newSalt[16], const BPasswordHash::Parameters& newParams,
	uid_t ownerUid, gid_t ownerGid)
{
	BMessage existing;
	uint8 keystoreId[kR5KSIdSize];
	status_t status = load_protectors_message_at(dirFd, pathOrLeaf, existing,
		keystoreId);
	if (status != B_OK)
		return status;

	BSecureBuffer masterKey;
	status = unwrap_master_key_from_message(existing, keystoreId, oldKek,
		masterKey);
	if (status != B_OK)
		return status;

	BMessage newEntry;
	status = seal_password_protector_entry(keystoreId, masterKey, newKek,
		newSalt, newParams, newEntry);
	masterKey.Clear();
	if (status != B_OK)
		return status;

	BMessage updated;
	if (updated.AddString("magic", kR5KPMagic) != B_OK
		|| updated.AddUInt32("format", kR5KPFormatVersion) != B_OK
		|| updated.AddData("keystore_id", B_RAW_TYPE, keystoreId, kR5KSIdSize)
			!= B_OK
		|| updated.AddMessage("protectors", &newEntry) != B_OK) {
		return B_NO_MEMORY;
	}

	BMessage entry;
	for (int32 i = 0; existing.FindMessage("protectors", i, &entry) == B_OK;
		i++) {
		uint32 type = 0;
		if (entry.FindUInt32("type", &type) == B_OK
			&& type == kR5ProtectorPasswordArgon2id)
			continue;
		if (updated.AddMessage("protectors", &entry) != B_OK)
			return B_NO_MEMORY;
	}

	return write_protectors_message_at(dirFd, pathOrLeaf, updated, ownerUid,
		ownerGid);
}


static status_t
write_encrypted_database_at(int dirFd, const char* pathOrLeaf,
	const uint8 keystoreId[kR5KSIdSize], const BSecureBuffer& masterKey,
	const BMessage& keystoreMessage, uid_t ownerUid, gid_t ownerGid)
{
	if (masterKey.Size() != BSealedData::kKeySize)
		return B_BAD_VALUE;

	ssize_t flatSize = keystoreMessage.FlattenedSize();
	if (flatSize <= 0 || flatSize > 32 * 1024 * 1024)
		return B_BAD_DATA;

	BSecureBuffer plain(static_cast<size_t>(flatSize));
	if (plain.InitCheck() != B_OK)
		return plain.InitCheck();

	status_t status = keystoreMessage.Flatten(
		reinterpret_cast<char*>(plain.Data()), flatSize);
	if (status != B_OK)
		return status;

	uint8 aad[8 + kR5KSIdSize];
	build_db_aad(keystoreId, aad);

	const size_t totalSize = kR5KSHeaderSize + BSealedData::kOverhead
		+ plain.Size();
	uint8* out = static_cast<uint8*>(malloc(totalSize));
	if (out == NULL)
		return B_NO_MEMORY;

	memcpy(out, kR5KSMagic, sizeof(kR5KSMagic));
	write_le32(out + 4, kR5KSEncryptedFormatVersion);
	memcpy(out + 8, keystoreId, kR5KSIdSize);

	size_t sealedSize = 0;
	status = BSealedData::Seal(masterKey, plain.Data(), plain.Size(),
		aad, sizeof(aad), out + kR5KSHeaderSize,
		totalSize - kR5KSHeaderSize, &sealedSize);
	plain.Clear();
	if (status == B_OK) {
		status = write_file_atomic_at(dirFd, pathOrLeaf, out,
			kR5KSHeaderSize + sealedSize, ownerUid, ownerGid);
	}
	free(out);
	return status;
}


static status_t
preserve_current_as_old_at(int dirFd, bool moveProtector, BString* _outSuffix)
{
	if (_outSuffix != NULL)
		_outSuffix->Truncate(0);

	struct stat dbSt;
	if (fstatat(dirFd, kR5KSDatabaseFile, &dbSt, AT_SYMLINK_NOFOLLOW) != 0
		|| !S_ISREG(dbSt.st_mode))
		return B_ENTRY_NOT_FOUND;

	if (moveProtector) {
		struct stat protSt;
		if (fstatat(dirFd, kR5KSProtectorsFile, &protSt,
				AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(protSt.st_mode))
			return B_ENTRY_NOT_FOUND;
	}

	for (int32 slot = 1; slot <= kR5KSMaxOldBackups; slot++) {
		BString oldDb;
		BString oldProt;
		R5KeyStoreCrypto::OldBackupNames(slot, oldDb, oldProt);

		struct stat checkSt;
		if (fstatat(dirFd, oldDb.String(), &checkSt, AT_SYMLINK_NOFOLLOW) == 0
			|| errno != ENOENT)
			continue;
		if (fstatat(dirFd, oldProt.String(), &checkSt,
				AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT)
			continue;

		if (renameat(dirFd, kR5KSDatabaseFile, dirFd, oldDb.String()) != 0)
			return B_IO_ERROR;
		if (moveProtector) {
			if (renameat(dirFd, kR5KSProtectorsFile, dirFd,
					oldProt.String()) != 0) {
				renameat(dirFd, oldDb.String(), dirFd, kR5KSDatabaseFile);
				return B_IO_ERROR;
			}
		}
		if (_outSuffix != NULL) {
			if (slot == 1)
				_outSuffix->SetTo(".old");
			else
				_outSuffix->SetToFormat(".old%" B_PRId32, slot);
		}
		return B_OK;
	}

	return B_DEVICE_FULL;
}


static status_t
create_keystore_in_dirfd(int dirFd, uid_t uid, gid_t gid, const char* password)
{
	uint8 keystoreId[kR5KSIdSize];
	uint8 salt[16];
	BSecureBuffer masterKey;
	if (BRandom::Fill(keystoreId, sizeof(keystoreId)) != B_OK
		|| BRandom::Fill(salt, sizeof(salt)) != B_OK
		|| BRandom::Fill(masterKey, BSealedData::kKeySize) != B_OK) {
		return B_ERROR;
	}

	BPasswordHash::Parameters params = BPasswordHash::DefaultParameters();
	BSecureBuffer kek;
	status_t status = BPasswordHash::DeriveKey(password, strlen(password),
		salt, sizeof(salt), params, kek, BSealedData::kKeySize);
	if (status != B_OK)
		return status;

	BMessage entry;
	status = seal_password_protector_entry(keystoreId, masterKey, kek, salt,
		params, entry);
	kek.Clear();
	if (status != B_OK) {
		masterKey.Clear();
		return status;
	}

	BMessage protFile;
	if (protFile.AddString("magic", kR5KPMagic) != B_OK
		|| protFile.AddUInt32("format", kR5KPFormatVersion) != B_OK
		|| protFile.AddData("keystore_id", B_RAW_TYPE, keystoreId, kR5KSIdSize)
			!= B_OK
		|| protFile.AddMessage("protectors", &entry) != B_OK) {
		masterKey.Clear();
		return B_NO_MEMORY;
	}

	status = write_protectors_message_at(dirFd, kR5KSProtectorsFile, protFile,
		uid, gid);
	if (status != B_OK) {
		masterKey.Clear();
		return status;
	}

	BMessage emptyStore;
	emptyStore.AddUInt32("format", 1);
	status = write_encrypted_database_at(dirFd, kR5KSDatabaseFile, keystoreId,
		masterKey, emptyStore, uid, gid);
	masterKey.Clear();
	if (status != B_OK)
		unlinkat(dirFd, kR5KSProtectorsFile, 0);
	return status;
}


}	// namespace


/* static */ bool
R5KeyStoreCrypto::IsAllowedPasswordParams(
	const BPasswordHash::Parameters& params)
{
	return params.memoryKiB >= BPasswordHash::kMinMemoryKiB
		&& params.memoryKiB <= BPasswordHash::kDefaultMemoryKiB
		&& params.passes >= 1
		&& params.passes <= BPasswordHash::kDefaultPasses
		&& params.lanes == BPasswordHash::kDefaultLanes;
}


/* static */ bool
R5KeyStoreCrypto::IsEncryptedDatabase(const void* data, size_t size)
{
	if (data == NULL || size < kR5KSHeaderSize)
		return false;
	const uint8* bytes = static_cast<const uint8*>(data);
	return memcmp(bytes, kR5KSMagic, sizeof(kR5KSMagic)) == 0
		&& read_le32(bytes + 4) == kR5KSEncryptedFormatVersion;
}


/* static */ status_t
R5KeyStoreCrypto::ReadFileBounded(const char* path, uint8** _outData,
	size_t* _outSize, size_t maxSize)
{
	return read_file_bounded_at(AT_FDCWD, path, _outData, _outSize, maxSize);
}


/* static */ status_t
R5KeyStoreCrypto::WriteFileAtomic(const char* path, const void* data,
	size_t size, uid_t ownerUid, gid_t ownerGid)
{
	return write_file_atomic_at(AT_FDCWD, path, data, size, ownerUid,
		ownerGid);
}


/* static */ status_t
R5KeyStoreCrypto::ReadPasswordProtectorParams(const char* protectorsPath,
	R5PasswordProtectorParams& outParams)
{
	BMessage msg;
	uint8 keystoreId[kR5KSIdSize];
	status_t status = load_protectors_message_at(AT_FDCWD, protectorsPath, msg,
		keystoreId);
	if (status != B_OK)
		return status;
	return read_password_params_from_message(msg, keystoreId, outParams);
}


/* static */ status_t
R5KeyStoreCrypto::ReadAccountPasswordProtectorParams(const char* homeDir,
	uid_t expectedUid, R5PasswordProtectorParams& outParams)
{
	int dirFd = -1;
	status_t status = open_keystore_dirfd(homeDir, expectedUid, (gid_t)-1,
		false, &dirFd);
	if (status != B_OK)
		return status;

	BMessage msg;
	uint8 keystoreId[kR5KSIdSize];
	status = load_protectors_message_at(dirFd, kR5KSProtectorsFile, msg,
		keystoreId);
	close(dirFd);
	if (status != B_OK)
		return status;
	return read_password_params_from_message(msg, keystoreId, outParams);
}


/* static */ status_t
R5KeyStoreCrypto::WritePasswordProtector(const char* protectorsPath,
	const uint8 keystoreId[kR5KSIdSize], const BSecureBuffer& masterKey,
	const BSecureBuffer& kek, const uint8 salt[16],
	const BPasswordHash::Parameters& params, uid_t ownerUid, gid_t ownerGid)
{
	BMessage entry;
	status_t status = seal_password_protector_entry(keystoreId, masterKey, kek,
		salt, params, entry);
	if (status != B_OK)
		return status;

	BMessage file;
	if (file.AddString("magic", kR5KPMagic) != B_OK
		|| file.AddUInt32("format", kR5KPFormatVersion) != B_OK
		|| file.AddData("keystore_id", B_RAW_TYPE, keystoreId, kR5KSIdSize)
			!= B_OK
		|| file.AddMessage("protectors", &entry) != B_OK) {
		return B_NO_MEMORY;
	}

	return write_protectors_message_at(AT_FDCWD, protectorsPath, file,
		ownerUid, ownerGid);
}


/* static */ status_t
R5KeyStoreCrypto::UnwrapMasterKey(const char* protectorsPath,
	const BSecureBuffer& kek, BSecureBuffer& outMasterKey,
	uint8 outKeystoreId[kR5KSIdSize])
{
	BMessage msg;
	status_t status = load_protectors_message_at(AT_FDCWD, protectorsPath, msg,
		outKeystoreId);
	if (status != B_OK) {
		outMasterKey.Clear();
		return status;
	}
	return unwrap_master_key_from_message(msg, outKeystoreId, kek,
		outMasterKey);
}


/* static */ status_t
R5KeyStoreCrypto::RewrapPasswordProtector(const char* protectorsPath,
	const BSecureBuffer& oldKek, const BSecureBuffer& newKek,
	const uint8 newSalt[16], const BPasswordHash::Parameters& newParams,
	uid_t ownerUid, gid_t ownerGid)
{
	return rewrap_password_protector_at(AT_FDCWD, protectorsPath, oldKek,
		newKek, newSalt, newParams, ownerUid, ownerGid);
}


/* static */ status_t
R5KeyStoreCrypto::RewrapAccountPasswordProtector(const char* homeDir,
	uid_t ownerUid, gid_t ownerGid, const BSecureBuffer& oldKek,
	const BSecureBuffer& newKek, const uint8 newSalt[16],
	const BPasswordHash::Parameters& newParams)
{
	int dirFd = -1;
	status_t status = open_keystore_dirfd(homeDir, ownerUid, ownerGid, false,
		&dirFd);
	if (status != B_OK)
		return status;
	status = rewrap_password_protector_at(dirFd, kR5KSProtectorsFile, oldKek,
		newKek, newSalt, newParams, ownerUid, ownerGid);
	close(dirFd);
	return status;
}


/* static */ status_t
R5KeyStoreCrypto::WriteEncryptedDatabase(const char* databasePath,
	const uint8 keystoreId[kR5KSIdSize], const BSecureBuffer& masterKey,
	const BMessage& keystoreMessage, uid_t ownerUid, gid_t ownerGid)
{
	return write_encrypted_database_at(AT_FDCWD, databasePath, keystoreId,
		masterKey, keystoreMessage, ownerUid, ownerGid);
}


/* static */ status_t
R5KeyStoreCrypto::ReadEncryptedDatabase(const char* databasePath,
	const uint8 expectedKeystoreId[kR5KSIdSize],
	const BSecureBuffer& masterKey, BMessage& outKeystoreMessage)
{
	outKeystoreMessage.MakeEmpty();
	if (masterKey.Size() != BSealedData::kKeySize)
		return B_BAD_VALUE;

	uint8* data = NULL;
	size_t size = 0;
	status_t status = read_file_bounded_at(AT_FDCWD, databasePath, &data,
		&size, 64 * 1024 * 1024);
	if (status != B_OK)
		return status;

	if (!IsEncryptedDatabase(data, size)) {
		free(data);
		return B_BAD_DATA;
	}
	if (memcmp(data + 8, expectedKeystoreId, kR5KSIdSize) != 0) {
		free(data);
		return B_NOT_ALLOWED;
	}

	uint8 aad[8 + kR5KSIdSize];
	build_db_aad(expectedKeystoreId, aad);

	BSecureBuffer plain;
	status = BSealedData::Open(masterKey, data + kR5KSHeaderSize,
		size - kR5KSHeaderSize, aad, sizeof(aad), plain);
	free(data);
	if (status != B_OK)
		return status;

	BMemoryIO io(plain.Data(), plain.Size());
	status = outKeystoreMessage.Unflatten(&io);
	plain.Clear();
	return status;
}


/* static */ void
R5KeyStoreCrypto::OldBackupNames(int32 slot, BString& outDbName,
	BString& outProtName)
{
	if (slot <= 1) {
		outDbName.SetTo(kR5KSOldDatabaseFile);
		outProtName.SetTo(kR5KSOldProtectorsFile);
	} else {
		outDbName.SetToFormat("%s.old%" B_PRId32, kR5KSDatabaseFile, slot);
		outProtName.SetToFormat("%s.old%" B_PRId32, kR5KSProtectorsFile, slot);
	}
}


/* static */ status_t
R5KeyStoreCrypto::PreserveCurrentAsOld(const char* dirPath, bool moveProtector,
	BString* _outSuffix)
{
	if (dirPath == NULL || dirPath[0] == '\0')
		return B_BAD_VALUE;

	int dirFd;
	do {
		dirFd = open(dirPath, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	} while (dirFd < 0 && errno == EINTR);
	if (dirFd < 0)
		return errno == ENOENT ? B_ENTRY_NOT_FOUND : B_NOT_ALLOWED;

	status_t status = preserve_current_as_old_at(dirFd, moveProtector,
		_outSuffix);
	close(dirFd);
	return status;
}


/* static */ status_t
R5KeyStoreCrypto::BuildKeyStorePaths(const char* homeDir, BString& outDir,
	BString& outDatabase, BString& outProtectors)
{
	if (homeDir == NULL || homeDir[0] == '\0')
		return B_BAD_VALUE;
	outDir.SetToFormat("%s/config/settings/system/keystore", homeDir);
	outDatabase.SetToFormat("%s/%s", outDir.String(), kR5KSDatabaseFile);
	outProtectors.SetToFormat("%s/%s", outDir.String(), kR5KSProtectorsFile);
	return B_OK;
}


/* static */ status_t
R5KeyStoreCrypto::CreateAccountKeyStore(const char* homeDir, uid_t uid,
	gid_t gid, const char* password)
{
	if (homeDir == NULL || password == NULL || password[0] == '\0')
		return B_BAD_VALUE;

	int dirFd = -1;
	status_t status = open_keystore_dirfd(homeDir, uid, gid, true, &dirFd);
	if (status != B_OK)
		return status;

	status = create_keystore_in_dirfd(dirFd, uid, gid, password);
	close(dirFd);
	return status;
}


/* static */ status_t
R5KeyStoreCrypto::ResetAccountKeyStore(const char* homeDir, uid_t uid,
	gid_t gid, const char* newPassword)
{
	if (homeDir == NULL || newPassword == NULL || newPassword[0] == '\0')
		return B_BAD_VALUE;

	int dirFd = -1;
	status_t status = open_keystore_dirfd(homeDir, uid, gid, true, &dirFd);
	if (status != B_OK)
		return status;

	// Move any existing keystore pair into the next free .old / .old2 .. .old16
	// slot so consecutive resets never lose data. Refuse if all slots are full.
	struct stat curDbSt;
	struct stat curProtSt;
	bool hasCurrentDb = (fstatat(dirFd, kR5KSDatabaseFile, &curDbSt,
		AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(curDbSt.st_mode));
	bool hasCurrentProt = (fstatat(dirFd, kR5KSProtectorsFile, &curProtSt,
		AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(curProtSt.st_mode));

	if (hasCurrentDb) {
		status = preserve_current_as_old_at(dirFd, hasCurrentProt, NULL);
		if (status != B_OK) {
			close(dirFd);
			return status;
		}
	}

	status = create_keystore_in_dirfd(dirFd, uid, gid, newPassword);
	close(dirFd);
	return status;
}


}	// namespace BPrivate
