/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Which host may see the access token is R5's rule: only the provider's
 * own API hosts. An upload or download URL on another host is already
 * authenticated by the URL itself (rclone leaves those URLs without the
 * Graph bearer when the host is not Graph).
 */

#include "ApiCall.h"

#include "LocalFile.h"

#include "Retry.h"
#include "Url.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>


static int64
now_micros()
{
	struct timeval tv;
	if (gettimeofday(&tv, NULL) != 0)
		return 0;
	return (int64)tv.tv_sec * 1000000 + (int64)tv.tv_usec;
}


AccountSession::AccountSession(Transport& transport, OAuthClient& oauth,
	Sleeper& sleeper, Log& log, const char* provider)
	:
	fTransport(transport),
	fOAuth(oauth),
	fSleeper(sleeper),
	fLog(log),
	fProvider(provider),
	fRefreshed(false)
{
}


OAuthClient&
AccountSession::OAuth()
{
	return fOAuth;
}


bool
AccountSession::_WantsAuthorization(const BString& url) const
{
	BString host = UrlHost(url);
	if (strcmp(fProvider, "onedrive") == 0) {
		return host == "graph.microsoft.com"
			|| host == "login.microsoftonline.com";
	}
	if (strcmp(fProvider, "dropbox") == 0) {
		return host == "api.dropboxapi.com"
			|| host == "content.dropboxapi.com"
			|| host == "www.dropbox.com";
	}
	return false;
}


bool
AccountSession::_TokenExpired(const HttpExchange& exchange) const
{
	if (exchange.status != 401)
		return false;
	const char* auth = HeaderValue(exchange.responseHeaders, "www-authenticate");
	if (auth != NULL && strstr(auth, "expired_token") != NULL)
		return true;
	if (exchange.responseBody.FindFirst("InvalidAuthenticationToken") >= 0)
		return true;
	if (exchange.responseBody.FindFirst("expired_access_token") >= 0)
		return true;
	return false;
}


status_t
AccountSession::Call(HttpExchange& exchange)
{
	fOAuth.Load();
	if (fOAuth.AccessExpired(now_micros()) && !fRefreshed) {
		status_t status = fOAuth.Refresh(fTransport, fLog);
		if (status != B_OK)
			return status;
		fRefreshed = true;
	}

	for (int32 pass = 0; pass < 2; pass++) {
		exchange.requestHeaders.RemoveName("authorization");
		if (_WantsAuthorization(exchange.url)) {
			BString header;
			header << "Bearer " << fOAuth.Access();
			SetHeader(exchange.requestHeaders, "authorization", header.String());
		}
		status_t status = CallWithRetry(fTransport, exchange, fSleeper, fLog,
			fProvider);
		/* Drop the bearer from the struct the caller still holds. */
		exchange.requestHeaders.RemoveName("authorization");
		if (status != B_OK)
			return status;
		if (pass == 0 && _TokenExpired(exchange)) {
			fLog.Line("access token expired, refreshing");
			status = fOAuth.Refresh(fTransport, fLog);
			if (status != B_OK)
				return status;
			fRefreshed = true;
			continue;
		}
		return B_OK;
	}
	return B_NOT_ALLOWED;
}


status_t
CallIntoFile(AccountSession& session, HttpExchange& exchange,
	const char* localFile)
{
	status_t status = LocalMakeParents(localFile);
	if (status != B_OK)
		return status;
	BString temp;
	temp << localFile << ".r5partial";
	exchange.responseFile = temp;
	status = session.Call(exchange);
	bool ok = status == B_OK && exchange.status >= 200 && exchange.status <= 299;
	if (!ok) {
		remove(temp.String());
		return status;
	}
	if (exchange.responseInFile) {
		if (rename(temp.String(), localFile) != 0) {
			remove(temp.String());
			return B_IO_ERROR;
		}
		return B_OK;
	}
	remove(temp.String());
	return LocalWriteAtomic(localFile, exchange.responseBody.String(),
		(size_t)exchange.responseBody.Length());
}
