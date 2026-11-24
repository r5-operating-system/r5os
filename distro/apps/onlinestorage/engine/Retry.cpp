/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2012 Nick Craig-Wood
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 *
 * Translated from rclone (MIT), Copyright (C) 2012 by Nick Craig-Wood.
 * Source: fs/fserrors/error.go and backend/onedrive/onedrive.go
 * shouldRetry, backend/dropbox/dropbox.go shouldRetry, at
 * c0cef5e872f937727c89bfab71d6be11480f4d40.
 *
 * The low-level attempt count is 5, the same cap as oauthutil.Token.
 */

#include "Retry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static bool
body_has(const BString& body, const char* phrase)
{
	return body.FindFirst(phrase) >= 0;
}


static bool
http_code_retried(int32 status)
{
	switch (status) {
		case 429:
		case 500:
		case 502:
		case 503:
		case 504:
		case 509:
			return true;
		default:
			return false;
	}
}


RetryDecision
DecideRetry(const HttpExchange& exchange, const char* provider)
{
	RetryDecision decision;
	decision.action = kRetryGiveUp;
	decision.waitSeconds = 0;
	int32 status = exchange.status;
	if (status == 0)
		return decision;

	if (provider != NULL && strcmp(provider, "dropbox") == 0) {
		if (body_has(exchange.responseBody, "insufficient_space")) {
			decision.action = kRetryFatal;
			return decision;
		}
		if (body_has(exchange.responseBody, "malformed_path")) {
			decision.action = kRetryGiveUp;
			return decision;
		}
	}

	if (provider != NULL && strcmp(provider, "onedrive") == 0) {
		if (status == 507) {
			decision.action = kRetryFatal;
			return decision;
		}
		if (status == 400 && body_has(exchange.responseBody, "pathIsTooLong")) {
			decision.action = kRetryGiveUp;
			return decision;
		}
	}

	if (status == 429 || status == 503) {
		const char* retryAfter = HeaderValue(exchange.responseHeaders,
			"retry-after");
		if (retryAfter != NULL && retryAfter[0] != '\0') {
			char* end = NULL;
			long seconds = strtol(retryAfter, &end, 10);
			if (end != retryAfter && seconds >= 0 && seconds < 24 * 60 * 60) {
				decision.action = kRetryAfter;
				decision.waitSeconds = (int32)seconds;
				return decision;
			}
		}
		if (provider != NULL && strcmp(provider, "dropbox") == 0
			&& (body_has(exchange.responseBody, "too_many_requests")
				|| body_has(exchange.responseBody, "too_many_write_operations"))) {
			decision.action = kRetryAgain;
			return decision;
		}
	}

	if (http_code_retried(status)) {
		decision.action = kRetryAgain;
		return decision;
	}

	/* A dropped connection is status 0; the caller retries that itself. */
	return decision;
}


status_t
CallWithRetry(Transport& transport, HttpExchange& exchange, Sleeper& sleeper,
	Log& log, const char* provider)
{
	const int32 kMaxTries = 5;
	BString method = exchange.method;
	BString url = exchange.url;
	BMessage headers = exchange.requestHeaders;
	BString body = exchange.body;

	for (int32 attempt = 1; attempt <= kMaxTries; attempt++) {
		exchange.method = method;
		exchange.url = url;
		exchange.requestHeaders = headers;
		exchange.body = body;
		exchange.ClearResponse();
		status_t status = transport.Exchange(exchange);
		if (status != B_OK) {
			if (attempt == kMaxTries)
				return status;
			BString line;
			line.SetToFormat("request failed, try %" B_PRId32, attempt);
			log.Line(line.String());
			sleeper.WaitSeconds(1);
			continue;
		}
		RetryDecision decision = DecideRetry(exchange, provider);
		if (decision.action == kRetryGiveUp || decision.action == kRetryFatal)
			return B_OK;
		if (attempt == kMaxTries)
			return B_OK;
		int32 wait = decision.action == kRetryAfter ? decision.waitSeconds : 1;
		BString line;
		line.SetToFormat("HTTP %" B_PRId32 ", retry in %" B_PRId32 "s",
			exchange.status, wait);
		log.Line(line.String());
		sleeper.WaitSeconds(wait);
	}
	return B_OK;
}
