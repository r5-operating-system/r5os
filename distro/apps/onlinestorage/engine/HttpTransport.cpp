/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2012 Nick Craig-Wood
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 *
 * Translated from rclone (MIT), Copyright (C) 2012 by Nick Craig-Wood.
 * Source: lib/rest/rest.go (which redirects are followed, and when a
 * redirect becomes a GET with no body) at
 * c0cef5e872f937727c89bfab71d6be11480f4d40.
 *
 * A 301, 302 or 303 becomes a GET with an empty body, as Go's client
 * does. A 307 or 308 keeps the body only when the hop stays on the same
 * host. A cross-host hop never carries the body, so a refresh token is
 * not sent to another machine.
 */

#include "HttpTransport.h"

#include "Redirect.h"
#include "Url.h"

#include <DataIO.h>
#include <ExclusiveBorrow.h>
#include <File.h>
#include <HttpFields.h>
#include <HttpRequest.h>
#include <HttpResult.h>
#include <HttpSession.h>
#include <Url.h>

#include <stdio.h>

#include <memory>
#include <string_view>
#include <vector>


using BPrivate::Network::BHttpFields;
using BPrivate::Network::BHttpMethod;
using BPrivate::Network::BHttpRequest;
using BPrivate::Network::BHttpSession;
using BPrivate::Network::BBorrow;
using BPrivate::Network::BExclusiveBorrow;
using BPrivate::Network::make_exclusive_borrow;


/* An error page is small; never read more of it than this into memory. */
static const off_t kMaxErrorBody = 1024 * 1024;


static void
read_error_body(const char* path, BString& out)
{
	out.SetTo("");
	FILE* file = fopen(path, "rb");
	if (file == NULL)
		return;
	char buffer[8192];
	off_t total = 0;
	size_t n;
	while (total < kMaxErrorBody
		&& (n = fread(buffer, 1, sizeof(buffer), file)) > 0) {
		out.Append(buffer, (int32)n);
		total += (off_t)n;
	}
	fclose(file);
}


static BHttpMethod
method_of(const BString& name)
{
	if (name == "GET")
		return BHttpMethod(BHttpMethod::Get);
	if (name == "POST")
		return BHttpMethod(BHttpMethod::Post);
	if (name == "PUT")
		return BHttpMethod(BHttpMethod::Put);
	if (name == "DELETE")
		return BHttpMethod(BHttpMethod::Delete);
	if (name == "PATCH")
		return BHttpMethod("PATCH");
	return BHttpMethod(name.String());
}


status_t
HttpTransport::Exchange(HttpExchange& exchange)
{
	BString current = exchange.url;
	BString origin = exchange.url;
	std::vector<BString> hops;
	BMessage headers = exchange.requestHeaders;
	for (int32 redirect = 0; redirect < 6; redirect++) {
		try {
			/* The URL is already encoded. encode=true would turn % into %25. */
			BHttpRequest request(BUrl(current.String(), false));
			request.SetMethod(method_of(exchange.method));
			request.SetMaxRedirections(0);
			request.SetTimeout(60 * 1000000);
			BHttpFields fields;
			char* name = NULL;
			type_code type = 0;
			int32 count = 0;
			for (int32 i = 0; headers.GetInfo(B_STRING_TYPE, i, &name, &type,
					&count) == B_OK; i++) {
				const char* value = headers.FindString(name);
				if (value != NULL)
					fields.AddField(name, value);
			}
			request.SetFields(fields);
			if (exchange.body.Length() > 0 || exchange.method == "POST"
				|| exchange.method == "PUT") {
				std::unique_ptr<BMallocIO> input(new BMallocIO());
				if (exchange.body.Length() > 0)
					input->Write(exchange.body.String(),
						(size_t)exchange.body.Length());
				input->Seek(0, SEEK_SET);
				off_t size = (off_t)exchange.body.Length();
				const char* mime = HeaderValue(headers, "content-type");
				request.SetRequestBody(std::move(input),
					mime != NULL ? mime : "application/octet-stream", size);
			}
			/*
			 * A download streams into exchange.responseFile, so a large file
			 * is never one BString (whose length is an int32).
			 */
			bool toFile = exchange.responseFile.Length() > 0;
			BExclusiveBorrow<BFile> target;
			if (toFile) {
				target = make_exclusive_borrow<BFile>(
					exchange.responseFile.String(),
					B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
				if (target->InitCheck() != B_OK)
					return B_IO_ERROR;
			}
			BHttpSession session;
			BPrivate::Network::BHttpResult result = toFile
				? session.Execute(std::move(request), BBorrow<BDataIO>(target))
				: session.Execute(std::move(request));
			exchange.status = result.Status().code;
			exchange.responseBody.SetTo("");
			exchange.responseInFile = false;
			/* Body() waits until the whole response is in. */
			const BPrivate::Network::BHttpBody& body = result.Body();
			if (toFile) {
				if (exchange.status >= 200 && exchange.status <= 299)
					exchange.responseInFile = true;
				else {
					read_error_body(exchange.responseFile.String(),
						exchange.responseBody);
				}
			} else if (body.text.has_value())
				exchange.responseBody = body.text.value();
			exchange.responseHeaders.MakeEmpty();
			const BHttpFields& responseFields = result.Fields();
			for (BHttpFields::ConstIterator it = responseFields.begin();
					it != responseFields.end(); it++) {
				std::string_view fieldName = it->Name();
				std::string_view fieldValue = it->Value();
				BString name;
				BString value;
				name.SetTo(fieldName.data(), (int32)fieldName.size());
				value.SetTo(fieldValue.data(), (int32)fieldValue.size());
				SetHeader(exchange.responseHeaders, name.String(),
					value.String());
			}
			if (exchange.status != 301 && exchange.status != 302
				&& exchange.status != 303 && exchange.status != 307
				&& exchange.status != 308)
				return B_OK;
			const char* location = HeaderValue(exchange.responseHeaders,
				"location");
			if (location == NULL || location[0] == '\0')
				return B_OK;
			RedirectStep step;
			status_t planned = PlanRedirect(origin, current, hops, location, step);
			if (planned != B_OK)
				return planned;
			hops.push_back(current);
			current = step.url;
			if (step.stripAuthorization)
				headers.RemoveName("authorization");
			if (exchange.status == 301 || exchange.status == 302
				|| exchange.status == 303 || step.stripAuthorization) {
				exchange.method = "GET";
				exchange.body.SetTo("");
			}
			exchange.url = current;
		} catch (...) {
			return B_IO_ERROR;
		}
	}
	return B_TIMED_OUT;
}
