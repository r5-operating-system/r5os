/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2012 Nick Craig-Wood
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 *
 * Translated from rclone (MIT), Copyright (C) 2012 by Nick Craig-Wood.
 * Source: backend/dropbox/dropbox.go and backend/dropbox/batcher.go
 * at c0cef5e872f937727c89bfab71d6be11480f4d40.
 *
 * The authorize request sends token_access_type=offline and does not
 * send access_type=offline (rclone sets NoOffline for that reason).
 * sharing.write and account_info.read are omitted: this app does not
 * share folders or call About. Paper, export, and shared-folder
 * mounting are not translated.
 */

#ifndef ONLINE_STORAGE_DROPBOX_H
#define ONLINE_STORAGE_DROPBOX_H

#include "ApiCall.h"
#include "Provider.h"


class DropboxProvider : public CloudProvider {
public:
								DropboxProvider(Transport& transport,
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
			BString				_ApiPath(const BString& relative) const;
			status_t			_ReadEntry(const BMessage& entry,
									CloudItem& item) const;
			status_t			_Rpc(const char* route, const BString& body,
									BMessage& reply);
			status_t			_ListPath(const BString& apiPath, bool recursive,
									std::vector<CloudItem>& items,
									BString& cursor);

			AccountSession		fSession;
			BString				fRoot;
};


OAuthConfig		DropboxOAuthConfig();

#endif
