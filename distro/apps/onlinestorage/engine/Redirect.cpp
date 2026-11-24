/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2012 Nick Craig-Wood
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 *
 * Translated from rclone (MIT), Copyright (C) 2012 by Nick Craig-Wood.
 * Source: lib/rest/rest.go, at
 * c0cef5e872f937727c89bfab71d6be11480f4d40.
 */

#include "Redirect.h"

#include "Url.h"


static BString
resolve_location(const BString& current, const BString& location)
{
	if (location.FindFirst("https://") == 0 || location.FindFirst("http://") == 0)
		return location;
	if (location.Length() > 0 && location.ByteAt(0) == '/') {
		const char* text = current.String();
		const char* scheme = strstr(text, "://");
		if (scheme == NULL)
			return location;
		const char* host = scheme + 3;
		const char* slash = strchr(host, '/');
		BString out;
		if (slash != NULL)
			out.SetTo(text, (int32)(slash - text));
		else
			out.SetTo(text);
		out << location;
		return out;
	}
	return location;
}


status_t
PlanRedirect(const BString& origin, const BString& current,
	const std::vector<BString>& previousHops, const BString& location,
	RedirectStep& step)
{
	step.url = resolve_location(current, location);
	step.downgrade = false;
	step.stripAuthorization = false;
	if (IsHttpsDowngrade(current, step.url) || IsHttpsDowngrade(origin, step.url)) {
		step.downgrade = true;
		return B_NOT_ALLOWED;
	}
	if (!SameOrigin(origin, step.url))
		step.stripAuthorization = true;
	for (size_t i = 0; i < previousHops.size(); i++) {
		if (!SameOrigin(origin, previousHops[i]))
			step.stripAuthorization = true;
	}
	return B_OK;
}
