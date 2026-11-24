/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2012 Nick Craig-Wood
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 *
 * Translated from rclone (MIT), Copyright (C) 2012 by Nick Craig-Wood.
 * Source: lib/oauthutil/oauthutil.go at
 * c0cef5e872f937727c89bfab71d6be11480f4d40.
 */

#include "OAuth.h"

#include "JsonUtil.h"
#include "Sha256.h"
#include "Url.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>


static const char* kPkceAlphabet =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";


static int64
now_micros()
{
	struct timeval tv;
	if (gettimeofday(&tv, NULL) != 0)
		return 0;
	return (int64)tv.tv_sec * 1000000 + (int64)tv.tv_usec;
}


BString
RandomToken(int32 length)
{
	BString out;
	if (length <= 0)
		return out;
	FILE* file = fopen("/dev/urandom", "rb");
	if (file == NULL)
		return out;
	for (int32 i = 0; i < length; i++) {
		unsigned char byte = 0;
		if (fread(&byte, 1, 1, file) != 1)
			break;
		out << kPkceAlphabet[byte % 66];
	}
	fclose(file);
	return out;
}


BString
PkceChallengeS256(const BString& verifier)
{
	Sha256 sha;
	sha.Update(verifier.String(), (size_t)verifier.Length());
	uint8 raw[32];
	sha.Final(raw);
	static const char* alphabet =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
	BString out;
	uint32 buffer = 0;
	int bits = 0;
	for (int i = 0; i < 32; i++) {
		buffer = (buffer << 8) | raw[i];
		bits += 8;
		while (bits >= 6) {
			bits -= 6;
			out << alphabet[(buffer >> bits) & 0x3f];
		}
	}
	if (bits > 0)
		out << alphabet[(buffer << (6 - bits)) & 0x3f];
	return out;
}


OAuthClient::OAuthClient(const OAuthConfig& config, TokenStore& store)
	:
	fConfig(config),
	fStore(store),
	fExpiry(0)
{
}


OAuthClient::~OAuthClient()
{
	fAccess.SetTo("");
	fRefresh.SetTo("");
	fVerifier.SetTo("");
}


status_t
OAuthClient::Begin(BString& authorizeUrl)
{
	fState = RandomToken(64);
	fVerifier = RandomToken(64);
	if (fState.Length() != 64 || fVerifier.Length() != 64)
		return B_IO_ERROR;
	BString challenge = PkceChallengeS256(fVerifier);
	authorizeUrl = fConfig.authUrl;
	if (authorizeUrl.FindFirst("?") < 0)
		authorizeUrl << "?";
	else
		authorizeUrl << "&";
	authorizeUrl << "client_id=" << EncodeForm(fConfig.clientId.String());
	authorizeUrl << "&response_type=code";
	authorizeUrl << "&redirect_uri=" << EncodeForm(fConfig.redirectUrl.String());
	authorizeUrl << "&scope=" << EncodeForm(fConfig.scopes.String());
	authorizeUrl << "&state=" << EncodeForm(fState.String());
	authorizeUrl << "&code_challenge=" << EncodeForm(challenge.String());
	authorizeUrl << "&code_challenge_method=S256";

	char* name = NULL;
	type_code type = 0;
	int32 count = 0;
	for (int32 i = 0; fConfig.extraAuth.GetInfo(B_STRING_TYPE, i, &name, &type,
			&count) == B_OK; i++) {
		const char* value = fConfig.extraAuth.FindString(name);
		authorizeUrl << "&" << EncodeForm(name) << "="
			<< EncodeForm(value != NULL ? value : "");
	}
	/* The authorize URL carries the client id and the challenge, never a token. */
	if (authorizeUrl.FindFirst("client_secret") >= 0)
		return B_NOT_ALLOWED;
	return B_OK;
}


const BString&
OAuthClient::State() const
{
	return fState;
}


const BString&
OAuthClient::Verifier() const
{
	return fVerifier;
}


status_t
OAuthClient::TakeCode(const BString& query, BString& code)
{
	code.SetTo("");
	BString state;
	if (!QueryValue(query, "state", state) || state != fState)
		return B_NOT_ALLOWED;
	BString error;
	if (QueryValue(query, "error", error) && error.Length() > 0)
		return B_NOT_ALLOWED;
	if (!QueryValue(query, "code", code) || code.Length() == 0)
		return B_BAD_DATA;
	return B_OK;
}


status_t
OAuthClient::_PostToken(Transport& transport, const BString& body, Log& log)
{
	HttpExchange exchange;
	exchange.method = "POST";
	exchange.url = fConfig.tokenUrl;
	SetHeader(exchange.requestHeaders, "content-type",
		"application/x-www-form-urlencoded");
	SetHeader(exchange.requestHeaders, "accept", "application/json");
	exchange.body = body;
	if (exchange.body.FindFirst("client_secret") >= 0)
		return B_NOT_ALLOWED;
	status_t status = transport.Exchange(exchange);
	if (status != B_OK)
		return status;
	if (exchange.status < 200 || exchange.status > 299) {
		log.Line("token endpoint refused the request");
		return B_NOT_ALLOWED;
	}
	BMessage message;
	status = ParseJson(exchange.responseBody, message);
	if (status != B_OK)
		return status;
	const char* access = JsonString(message, "access_token");
	if (access == NULL || access[0] == '\0')
		return B_BAD_DATA;
	fAccess.SetTo(access);
	const char* refresh = JsonString(message, "refresh_token");
	if (refresh != NULL && refresh[0] != '\0')
		fRefresh.SetTo(refresh);
	int64 expiresIn = 3600;
	JsonInt64(message, "expires_in", expiresIn);
	if (expiresIn < 0)
		expiresIn = 0;
	fExpiry = now_micros() + expiresIn * 1000000;
	_Store();
	/* Wipe the response that held the token. */
	exchange.body.SetTo("");
	exchange.responseBody.SetTo("");
	return B_OK;
}


void
OAuthClient::_Store()
{
	BMessage token;
	TokenSet(token, fAccess.String(), fRefresh.String(), fExpiry);
	fStore.Save(fConfig.account.String(), token);
}


status_t
OAuthClient::ExchangeCode(Transport& transport, const BString& code, Log& log)
{
	BString body;
	body << "grant_type=authorization_code";
	body << "&code=" << EncodeForm(code.String());
	body << "&redirect_uri=" << EncodeForm(fConfig.redirectUrl.String());
	body << "&client_id=" << EncodeForm(fConfig.clientId.String());
	body << "&code_verifier=" << EncodeForm(fVerifier.String());
	return _PostToken(transport, body, log);
}


status_t
OAuthClient::Refresh(Transport& transport, Log& log)
{
	if (fRefresh.Length() == 0)
		return B_NOT_ALLOWED;
	BString body;
	body << "grant_type=refresh_token";
	body << "&refresh_token=" << EncodeForm(fRefresh.String());
	body << "&client_id=" << EncodeForm(fConfig.clientId.String());
	return _PostToken(transport, body, log);
}


status_t
OAuthClient::Load()
{
	BMessage token;
	status_t status = fStore.Load(fConfig.account.String(), token);
	if (status != B_OK)
		return status;
	fAccess.SetTo(TokenAccess(token));
	fRefresh.SetTo(TokenRefresh(token));
	fExpiry = TokenExpiry(token);
	return fAccess.Length() > 0 ? B_OK : B_ENTRY_NOT_FOUND;
}


bool
OAuthClient::AccessExpired(int64 now) const
{
	if (fAccess.Length() == 0)
		return true;
	return now + 60 * 1000000 >= fExpiry;
}


const BString&
OAuthClient::Access() const
{
	return fAccess;
}


const char*
OAuthClient::Account() const
{
	return fConfig.account.String();
}


status_t
BindLoopback(int& fd, uint16 port, uint16& boundPort)
{
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return B_IO_ERROR;
	int on = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	struct sockaddr_in address;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(port);
	/* 127.0.0.1 only. Never INADDR_ANY. */
	if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
		close(fd);
		fd = -1;
		return B_ERROR;
	}
	if (bind(fd, (struct sockaddr*)&address, sizeof(address)) != 0) {
		close(fd);
		fd = -1;
		return B_BUSY;
	}
	if (listen(fd, 1) != 0) {
		close(fd);
		fd = -1;
		return B_IO_ERROR;
	}
	struct sockaddr_in bound;
	socklen_t length = sizeof(bound);
	if (getsockname(fd, (struct sockaddr*)&bound, &length) != 0) {
		close(fd);
		fd = -1;
		return B_IO_ERROR;
	}
	if (bound.sin_addr.s_addr != htonl(INADDR_LOOPBACK)) {
		close(fd);
		fd = -1;
		return B_NOT_ALLOWED;
	}
	boundPort = ntohs(bound.sin_port);
	return B_OK;
}


static status_t
write_all(int fd, const char* text)
{
	size_t length = strlen(text);
	size_t sent = 0;
	while (sent < length) {
		ssize_t n = send(fd, text + sent, length - sent, 0);
		if (n < 0)
			return B_IO_ERROR;
		sent += (size_t)n;
	}
	return B_OK;
}


status_t
ReadLoopbackCode(int client, const BString& expectState, BString& code,
	volatile bool* stop)
{
	code.SetTo("");
	struct timeval wait;
	wait.tv_sec = 0;
	wait.tv_usec = 200000;
	setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));
	char buffer[8192];
	size_t used = 0;
	while (used + 1 < sizeof(buffer)) {
		if (stop != NULL && *stop)
			return B_CANCELED;
		ssize_t n = recv(client, buffer + used, sizeof(buffer) - 1 - used, 0);
		if (n < 0) {
			if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
				continue;
			break;
		}
		if (n == 0)
			break;
		used += (size_t)n;
		buffer[used] = '\0';
		if (strstr(buffer, "\r\n\r\n") != NULL)
			break;
	}
	buffer[used] = '\0';

	status_t result = B_BAD_DATA;
	const char* path = NULL;
	if (strncmp(buffer, "GET ", 4) == 0)
		path = buffer + 4;
	const char* pathEnd = path != NULL ? strchr(path, ' ') : NULL;
	BString target;
	if (path != NULL && pathEnd != NULL)
		target.SetTo(path, (int32)(pathEnd - path));

	BString page;
	if (target.Length() == 0 || target.FindFirst("/") != 0) {
		page << "HTTP/1.0 404 Not Found\r\nConnection: close\r\n"
			"Content-Type: text/plain\r\nContent-Length: 9\r\n\r\nNot found";
		write_all(client, page.String());
		return B_BAD_DATA;
	}
	/* rclone's callback handler only accepts "/". */
	int32 queryAt = target.FindFirst("?");
	BString pathOnly = target;
	BString query;
	if (queryAt >= 0) {
		pathOnly.Truncate(queryAt);
		query = target;
		query.Remove(0, queryAt + 1);
	}
	if (pathOnly != "/") {
		page << "HTTP/1.0 404 Not Found\r\nConnection: close\r\n"
			"Content-Type: text/plain\r\nContent-Length: 9\r\n\r\nNot found";
		write_all(client, page.String());
		return B_BAD_DATA;
	}

	BString state;
	QueryValue(query, "state", state);
	QueryValue(query, "code", code);
	const char* body = NULL;
	const char* statusLine = "HTTP/1.0 400 Bad Request\r\n";
	if (state != expectState) {
		result = B_NOT_ALLOWED;
		body = "Sign-in failed. The state does not match. Close this page.";
		code.SetTo("");
	} else if (code.Length() == 0) {
		result = B_BAD_DATA;
		body = "Sign-in failed. No code was returned. Close this page.";
	} else {
		result = B_OK;
		statusLine = "HTTP/1.0 200 OK\r\n";
		body = "Signed in. You can close this page and go back to Online Storage.";
		/* Do not echo the code into the page. */
	}
	page << statusLine;
	page << "Connection: close\r\nContent-Type: text/html\r\nContent-Length: ";
	page << (int)strlen(body);
	page << "\r\n\r\n";
	page << body;
	write_all(client, page.String());
	return result;
}
