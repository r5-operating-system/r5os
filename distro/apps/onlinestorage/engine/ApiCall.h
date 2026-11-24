/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef ONLINE_STORAGE_API_CALL_H
#define ONLINE_STORAGE_API_CALL_H

#include "OAuth.h"
#include "Transport.h"


class AccountSession {
public:
								AccountSession(Transport& transport,
									OAuthClient& oauth, Sleeper& sleeper,
									Log& log, const char* provider);

			status_t			Call(HttpExchange& exchange);
			OAuthClient&		OAuth();

private:
			bool				_WantsAuthorization(const BString& url) const;
			bool				_TokenExpired(const HttpExchange& exchange) const;

			Transport&			fTransport;
			OAuthClient&		fOAuth;
			Sleeper&			fSleeper;
			Log&				fLog;
			const char*			fProvider;
			bool				fRefreshed;
};


#endif
