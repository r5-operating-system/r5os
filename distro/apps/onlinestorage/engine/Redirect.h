/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2012 Nick Craig-Wood
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 *
 * Translated from rclone (MIT), Copyright (C) 2012 by Nick Craig-Wood.
 * Source: lib/rest/rest.go StripHeadersOnCrossHostRedirectFn and
 * redirectLeavesHost, at c0cef5e872f937727c89bfab71d6be11480f4d40.
 *
 * Authorization is removed once any hop leaves the original host, and a
 * redirect from https to http is refused.
 */

#ifndef ONLINE_STORAGE_REDIRECT_H
#define ONLINE_STORAGE_REDIRECT_H

#include "Transport.h"

#include <vector>


struct RedirectStep {
	BString				url;
	bool				downgrade;
	bool				stripAuthorization;
};


status_t		PlanRedirect(const BString& origin, const BString& current,
					const std::vector<BString>& previousHops,
					const BString& location, RedirectStep& step);

#endif
