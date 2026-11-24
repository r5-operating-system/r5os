/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef ONLINE_STORAGE_TRANSPORT_H
#define ONLINE_STORAGE_TRANSPORT_H

#include <Message.h>
#include <String.h>


/* One HTTP exchange. Header names are stored in lower case. */
struct HttpExchange {
	BString				method;
	BString				url;
	BMessage			requestHeaders;
	BString				body;
	int32				status;
	BMessage			responseHeaders;
	BString				responseBody;

						HttpExchange();
	void				ClearResponse();
};


class Transport {
public:
	virtual					~Transport();

	virtual	status_t		Exchange(HttpExchange& exchange) = 0;
};


class Sleeper {
public:
	virtual					~Sleeper();
	virtual	void			WaitSeconds(int32 seconds) = 0;
};


class RealSleeper : public Sleeper {
public:
	virtual	void			WaitSeconds(int32 seconds);
};


class Log {
public:
	virtual					~Log();
	/* Must not be given a token, a verifier, or an authorization code. */
	virtual	void			Line(const char* text) = 0;
};


class StderrLog : public Log {
public:
	virtual	void			Line(const char* text);
};


const char*		HeaderValue(const BMessage& headers, const char* name);
status_t		SetHeader(BMessage& headers, const char* name,
					const char* value);

#endif
