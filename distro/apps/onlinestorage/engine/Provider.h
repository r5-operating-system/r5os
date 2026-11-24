/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef ONLINE_STORAGE_PROVIDER_H
#define ONLINE_STORAGE_PROVIDER_H

#include <String.h>

#include <vector>


struct CloudItem {
	BString				id;
	BString				path;
	BString				revision;
	BString				contentHash;
	off_t				size;
	bool				isFolder;
	bool				deleted;

						CloudItem();
};


class CloudProvider {
public:
	virtual					~CloudProvider();

	virtual	const char*		Name() const = 0;

	/* folderPath is relative to the chosen cloud folder. "" is that folder. */
	virtual	status_t		List(const BString& folderPath,
								std::vector<CloudItem>& items) = 0;
	virtual	status_t		Stat(const BString& path, CloudItem& item) = 0;
	virtual	status_t		Download(const BString& path,
								const char* localFile) = 0;
	/*
	 * expectedRevision is the token the service uses to reject a write
	 * when the item changed: OneDrive eTag (If-Match), Dropbox rev
	 * (mode update). NULL or "" creates the file and fails if it exists.
	 * B_BUSY means the cloud item changed and nothing was written.
	 */
	virtual	status_t		Upload(const BString& path,
								const char* localFile,
								const char* expectedRevision = NULL) = 0;
	virtual	status_t		CreateFolder(const BString& path) = 0;
	virtual	status_t		Move(const BString& fromPath,
								const BString& toPath) = 0;
	virtual	status_t		Delete(const BString& path) = 0;

	/*
	 * cursor "" lists the chosen folder as it is now and returns a cursor.
	 * A later call returns only what changed, including deletions.
	 * On failure the previous cursor must be kept by the caller.
	 */
	virtual	status_t		Changes(const BString& cursor, BString& newCursor,
								std::vector<CloudItem>& items) = 0;

	/* A cursor for changes that happen after this moment. */
	virtual	status_t		Tip(BString& cursor) = 0;
};


BString			JoinRelative(const BString& parent, const BString& name);
BString			ParentPath(const BString& path);
BString			LeafName(const BString& path);

#endif
