/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Haiku's libnetservices2 session. Redirects are followed here, not by
 * the session, so Authorization can be removed when the host changes
 * (see Redirect.cpp). Exceptions from the session become status_t.
 */

#ifndef ONLINE_STORAGE_HTTP_TRANSPORT_H
#define ONLINE_STORAGE_HTTP_TRANSPORT_H

#include "Transport.h"


class HttpTransport : public Transport {
public:
	virtual	status_t		Exchange(HttpExchange& exchange);
};

#endif
