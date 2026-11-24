/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * R5 Security Kit (libr5security.so).
 * Contract: docs/LIBR5.md ("Security Kit"); R5Book: security-kit.html.
 *
 * Cryptographic engine: Monocypher 4 (Argon2id, XChaCha20-Poly1305,
 * constant-time wipe). All calls are additive; shipped signatures do not
 * change.
 */
#ifndef _R5_SECURITY_H
#define _R5_SECURITY_H


#include <String.h>
#include <SupportDefs.h>

#include <stddef.h>


namespace BSecurityKit {


class BSecureBuffer {
public:
								BSecureBuffer();
	explicit					BSecureBuffer(size_t size);
								~BSecureBuffer();

			status_t			InitCheck() const;

			status_t			Allocate(size_t size);
			status_t			SetTo(const void* data, size_t size);

			void				Wipe();
			void				Clear();

			uint8*				Data();
			const uint8*		Data() const;
			size_t				Size() const;
			bool				IsEmpty() const;

			bool				Equals(const BSecureBuffer& other) const;

	static	void				WipeMemory(void* data, size_t size);

private:
								BSecureBuffer(const BSecureBuffer& other);
			BSecureBuffer&		operator=(const BSecureBuffer& other);

			uint8*				fData;
			size_t				fSize;
			status_t			fInitStatus;
			uint32				_reserved[4];
};


class BRandom {
public:
	static	status_t			Fill(void* buffer, size_t size);
	static	status_t			Fill(BSecureBuffer& buffer);
	static	status_t			Fill(BSecureBuffer& buffer, size_t size);
};


class BPasswordHash {
public:
	struct Parameters {
		uint32					memoryKiB;
		uint32					passes;
		uint32					lanes;
	};

	static	const uint32		kDefaultMemoryKiB = 65536;
	static	const uint32		kDefaultPasses = 3;
	static	const uint32		kDefaultLanes = 1;
	static	const uint32		kMinMemoryKiB = 8;
	static	const uint32		kMaxMemoryKiB = 1024 * 1024;
	static	const uint32		kMaxPasses = 64;
	static	const uint32		kMaxLanes = 16;
	static	const size_t		kDefaultSaltSize = 16;
	static	const size_t		kDefaultHashSize = 32;

	static	Parameters			DefaultParameters();

	static	status_t			DeriveKey(const void* password,
									size_t passwordLength,
									const void* salt, size_t saltLength,
									const Parameters& params,
									BSecureBuffer& outKey,
									size_t outLength = kDefaultHashSize);

	static	status_t			DeriveKey(const void* password,
									size_t passwordLength,
									const void* salt, size_t saltLength,
									const void* secretKey,
									size_t secretKeyLength,
									const void* associatedData,
									size_t associatedDataLength,
									const Parameters& params,
									BSecureBuffer& outKey,
									size_t outLength = kDefaultHashSize);

	static	status_t			Hash(const char* password, BString& outEncoded,
									const Parameters& params
										= DefaultParameters());

	static	status_t			Verify(const char* password,
									const char* encoded, bool* _matches);

	static	bool				NeedsRehash(const char* encoded,
									const Parameters& params
										= DefaultParameters());
};


class BSealedData {
public:
	static	const size_t		kKeySize = 32;
	static	const size_t		kNonceSize = 24;
	static	const size_t		kMacSize = 16;
	static	const size_t		kHeaderSize = 8;
	static	const size_t		kOverhead = kHeaderSize + kNonceSize + kMacSize;
	static	const uint8			kFormatVersion = 1;
	static	const uint8			kCipherXChaCha20Poly1305 = 1;

	static	status_t			Seal(const BSecureBuffer& key,
									const void* plaintext,
									size_t plaintextSize,
									const void* aad, size_t aadSize,
									void* outSealed, size_t outCapacity,
									size_t* _outSize);

	static	status_t			Open(const BSecureBuffer& key,
									const void* sealed, size_t sealedSize,
									const void* aad, size_t aadSize,
									BSecureBuffer& outPlaintext);

	static	status_t			SealToFile(const BSecureBuffer& key,
									const void* plaintext,
									size_t plaintextSize,
									const void* aad, size_t aadSize,
									const char* path);

	static	status_t			OpenFromFile(const BSecureBuffer& key,
									const char* path,
									const void* aad, size_t aadSize,
									BSecureBuffer& outPlaintext);
};


}	// namespace BSecurityKit


#endif	// _R5_SECURITY_H
