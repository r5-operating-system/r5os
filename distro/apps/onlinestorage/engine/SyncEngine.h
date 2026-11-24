/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Two-way sync of one folder pair. This is R5's own policy (the first
 * Dropbox client: keep both sides, a conflict keeps both copies). It is
 * not a translation of rclone's one-way sync or bisync.
 */

#ifndef ONLINE_STORAGE_SYNC_ENGINE_H
#define ONLINE_STORAGE_SYNC_ENGINE_H

#include "Provider.h"

#include <String.h>


struct SyncResult {
	bool				upToDate;
	BString				error;
	int32				uploaded;
	int32				downloaded;
	int32				deleted;
	int32				conflicts;

						SyncResult();
};


class SyncEngine {
public:
								SyncEngine(CloudProvider& provider);

	/*
	 * statePath is outside the folder pair. It records paths, ids,
	 * revisions and content hashes. It does not record tokens.
	 * A failure leaves the previous state file in place.
	 */
			status_t			Sync(const char* localRoot,
									const char* statePath, SyncResult& result);

private:
			CloudProvider&		fProvider;
};


/* "name (conflict).ext", then "name (conflict 2).ext", ... */
BString			ConflictLeaf(const BString& leaf, int32 index);

#endif
