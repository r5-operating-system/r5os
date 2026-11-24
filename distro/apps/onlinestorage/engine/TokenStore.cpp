/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#include "TokenStore.h"


TokenStore::~TokenStore()
{
}


MemoryTokenStore::MemoryTokenStore()
{
}


MemoryTokenStore::~MemoryTokenStore()
{
	fTokens.MakeEmpty();
}


status_t
MemoryTokenStore::Load(const char* account, BMessage& token)
{
	if (account == NULL || account[0] == '\0')
		return B_BAD_VALUE;
	return fTokens.FindMessage(account, &token);
}


status_t
MemoryTokenStore::Save(const char* account, const BMessage& token)
{
	if (account == NULL || account[0] == '\0')
		return B_BAD_VALUE;
	fTokens.RemoveName(account);
	return fTokens.AddMessage(account, &token);
}


void
MemoryTokenStore::Clear(const char* account)
{
	if (account != NULL)
		fTokens.RemoveName(account);
}


void
TokenSet(BMessage& token, const char* access, const char* refresh,
	int64 expiry)
{
	token.MakeEmpty();
	token.AddString("access", access != NULL ? access : "");
	token.AddString("refresh", refresh != NULL ? refresh : "");
	token.AddInt64("expiry", expiry);
}


const char*
TokenAccess(const BMessage& token)
{
	const char* value = token.FindString("access");
	return value != NULL ? value : "";
}


const char*
TokenRefresh(const BMessage& token)
{
	const char* value = token.FindString("refresh");
	return value != NULL ? value : "";
}


int64
TokenExpiry(const BMessage& token)
{
	int64 value = 0;
	if (token.FindInt64("expiry", &value) != B_OK)
		return 0;
	return value;
}
