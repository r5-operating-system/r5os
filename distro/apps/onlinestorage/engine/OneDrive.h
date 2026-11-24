/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2012 Nick Craig-Wood
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 *
 * Translated from rclone (MIT), Copyright (C) 2012 by Nick Craig-Wood.
 * Source: backend/onedrive/onedrive.go and backend/onedrive/api/types.go
 * at c0cef5e872f937727c89bfab71d6be11480f4d40.
 *
 * Scopes are Files.ReadWrite and offline_access, which the owner chose.
 * rclone's default scope list is wider (Files.Read.All, Sites.Read.All).
 * The national clouds, SharePoint, and QuickXorHash are not translated.
 */

#ifndef ONLINE_STORAGE_ONEDRIVE_H
#define ONLINE_STORAGE_ONEDRIVE_H

#include "ApiCall.h"
#include "Provider.h"


class OneDriveProvider : public CloudProvider {
public:
								OneDriveProvider(Transport& transport,
									OAuthClient& oauth, Sleeper& sleeper,
									Log& log, const char* root);

	virtual	const char*			Name() const;
	virtual	status_t			List(const BString& folderPath,
									std::vector<CloudItem>& items);
	virtual	status_t			Stat(const BString& path, CloudItem& item);
	virtual	status_t			Download(const BString& path,
									const char* localFile);
	virtual	status_t			Upload(const BString& path,
									const char* localFile,
									const char* expectedRevision = NULL);
	virtual	status_t			CreateFolder(const BString& path);
	virtual	status_t			Move(const BString& fromPath,
									const BString& toPath);
	virtual	status_t			Delete(const BString& path);
	virtual	status_t			Changes(const BString& cursor,
									BString& newCursor,
									std::vector<CloudItem>& items);
	virtual	status_t			Tip(BString& cursor);

private:
			BString				_ItemUrl(const BString& path,
									const char* route) const;
			status_t			_ReadItem(const BMessage& message,
									const BString& listFolder, bool fromList,
									CloudItem& item) const;
			status_t			_CollectPages(const BString& firstUrl,
									const BString& listFolder, bool fromList,
									std::vector<CloudItem>& items,
									BString& finalCursor, bool delta);

			AccountSession		fSession;
			BString				fRoot;
};


OAuthConfig		OneDriveOAuthConfig();

#endif
