/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef ONLINE_STORAGE_SCRIPTED_TRANSPORT_H
#define ONLINE_STORAGE_SCRIPTED_TRANSPORT_H

#include "Transport.h"

#include <vector>


struct ScriptedReply {
	const char*			method;
	const char*			urlPart;
	int32				status;
	const char*			retryAfter;
	const char*			body;
	bool				used;

						ScriptedReply()
							: method(NULL), urlPart(NULL), status(200),
							retryAfter(NULL), body(""), used(false) {}
};


class ScriptedTransport : public Transport {
public:
			void			Add(const char* method, const char* urlPart,
								int32 status, const char* body);
			void			AddRetry(const char* method, const char* urlPart,
								int32 status, const char* retryAfter,
								const char* body);
			int32			Unused() const;

	virtual	status_t		Exchange(HttpExchange& exchange);

private:
			std::vector<ScriptedReply> fReplies;
};


class CaptureLog : public Log {
public:
			BString			text;
	virtual	void			Line(const char* line);
};


class NoSleep : public Sleeper {
public:
			int32			waited;
								NoSleep() : waited(0) {}
	virtual	void			WaitSeconds(int32 seconds);
};

#endif
