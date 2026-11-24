/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2012 Nick Craig-Wood
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 *
 * Translated from rclone (MIT), Copyright (C) 2012 by Nick Craig-Wood.
 * Source: lib/oauthutil/oauthutil.go (getAuthURL, configExchange,
 * handleAuth, authServer.Init) at
 * c0cef5e872f937727c89bfab71d6be11480f4d40.
 *
 * Differences, chosen because this is a public desktop client:
 * the redirect is http://127.0.0.1:<port>/ (rclone's default string is
 * http://localhost:<port>/), there is no client secret, and PKCE S256
 * is always on. The failure page does not repeat the expected state.
 */

#ifndef ONLINE_STORAGE_OAUTH_H
#define ONLINE_STORAGE_OAUTH_H

#include "TokenStore.h"
#include "Transport.h"

#include <String.h>


struct OAuthConfig {
	BString				account;
	BString				clientId;
	BString				authUrl;
	BString				tokenUrl;
	BString				redirectUrl;
	BString				scopes;
	/* Extra authorize query pairs, for example token_access_type. */
	BMessage			extraAuth;
};


class OAuthClient {
public:
								OAuthClient(const OAuthConfig& config,
									TokenStore& store);
								~OAuthClient();

			status_t			Begin(BString& authorizeUrl);
			const BString&		State() const;
			const BString&		Verifier() const;

	/* Rejects a missing or mismatched state. Does not log the code. */
			status_t			TakeCode(const BString& query, BString& code);

			status_t			ExchangeCode(Transport& transport,
									const BString& code, Log& log);
			status_t			Refresh(Transport& transport, Log& log);
			status_t			Load();
			bool				AccessExpired(int64 now) const;

			const BString&		Access() const;
			const char*			Account() const;

private:
			status_t			_PostToken(Transport& transport,
									const BString& body, Log& log);
			void				_Store();

			OAuthConfig			fConfig;
			TokenStore&			fStore;
			BString				fState;
			BString				fVerifier;
			BString				fAccess;
			BString				fRefresh;
			int64				fExpiry;
};


/* Always 127.0.0.1. port 0 asks the kernel for a free port (tests). */
status_t		BindLoopback(int& fd, uint16 port, uint16& boundPort);

/*
 * Read one HTTP request from a connected socket. On a state match, write
 * a short HTML page and return the code. The expected state is not written
 * back to the client.
 */
/*
 * stop is checked between receives. NULL means read until the header
 * ends. A timed-out receive is tried again until stop is set.
 */
/* 200 ms polls a loopback connection may stay silent (5 seconds). */
const int32		kLoopbackIdleLimit = 25;

status_t		ReadLoopbackCode(int client, const BString& expectState,
					BString& code, volatile bool* stop = NULL);

BString			PkceChallengeS256(const BString& verifier);
BString			RandomToken(int32 length);

#endif
