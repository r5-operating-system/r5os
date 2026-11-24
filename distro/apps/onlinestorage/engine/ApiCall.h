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


/*
 * For a download: asks the transport to stream the body into
 * "<localFile>.r5partial" and moves it onto localFile only for a 2xx
 * status, so a large file is never held in memory and a failed download
 * never replaces the old copy. A transport that cannot stream (the test
 * transports) returns the body in memory; it is then written the same way.
 * exchange.status is left for the caller's own status checks.
 */
status_t		CallIntoFile(AccountSession& session, HttpExchange& exchange,
					const char* localFile);


#endif
