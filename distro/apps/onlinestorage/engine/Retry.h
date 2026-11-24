/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2012 Nick Craig-Wood
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 *
 * Translated from rclone (MIT), Copyright (C) 2012 by Nick Craig-Wood.
 * Source: fs/fserrors/error.go (ShouldRetry, ShouldRetryHTTP) and the
 * shouldRetry functions in backend/onedrive/onedrive.go and
 * backend/dropbox/dropbox.go, at c0cef5e872f937727c89bfab71d6be11480f4d40.
 */

#ifndef ONLINE_STORAGE_RETRY_H
#define ONLINE_STORAGE_RETRY_H

#include "Transport.h"


enum {
	kRetryGiveUp = 0,
	kRetryAgain = 1,
	kRetryAfter = 2,
	kRetryFatal = 3
};


struct RetryDecision {
	int32				action;
	int32				waitSeconds;
};


/*
 * HTTP codes rclone retries for OneDrive: 429, 500, 502, 503, 504, 509.
 * 507 is fatal. Dropbox retries too_many_requests and a Retry-After,
 * and does not retry insufficient_space or malformed_path.
 */
RetryDecision	DecideRetry(const HttpExchange& exchange, const char* provider);


status_t		CallWithRetry(Transport& transport, HttpExchange& exchange,
					Sleeper& sleeper, Log& log, const char* provider);

#endif
