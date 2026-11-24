/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef ONLINE_STORAGE_TOKEN_STORE_H
#define ONLINE_STORAGE_TOKEN_STORE_H

#include <Message.h>
#include <String.h>


/*
 * access, refresh, and expiry (int64 microseconds) for one account.
 * Nothing here is written to disk.
 *
 * TODO: keep these in the encrypted key store once that exists.
 * docs/VPN-KIT.md, "Gap: the key store is not encrypted". Until then a
 * restart forgets the sign-in. Do not write the token to
 * ~/config/settings, the keystore_database, argv, or the log.
 */
class TokenStore {
public:
	virtual					~TokenStore();

	virtual	status_t		Load(const char* account, BMessage& token) = 0;
	virtual	status_t		Save(const char* account, const BMessage& token) = 0;
	virtual	void			Clear(const char* account) = 0;
};


class MemoryTokenStore : public TokenStore {
public:
								MemoryTokenStore();
	virtual						~MemoryTokenStore();

	virtual	status_t			Load(const char* account, BMessage& token);
	virtual	status_t			Save(const char* account, const BMessage& token);
	virtual	void				Clear(const char* account);

private:
			BMessage			fTokens;
};


void			TokenSet(BMessage& token, const char* access,
					const char* refresh, int64 expiry);
const char*		TokenAccess(const BMessage& token);
const char*		TokenRefresh(const BMessage& token);
int64			TokenExpiry(const BMessage& token);

#endif
