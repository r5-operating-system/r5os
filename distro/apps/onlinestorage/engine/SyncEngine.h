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
	/* Set when the run stopped before deleting many files (see
	 * SetAllowLargeDelete). Nothing was changed on either side. */
	int32				pendingDeletes;
	bool				pendingDeletesInCloud;

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

	/*
	 * When *stop becomes true, Sync() returns B_CANCELED between two
	 * files and does not write the state file.
	 */
			void				SetStopFlag(volatile bool* stop);

	/*
	 * By default Sync() refuses a run that would delete at least
	 * kLargeDeleteMinimum files on one side and more than half of the
	 * files it last synced (an empty or unmounted local folder, a wrong
	 * cloud folder). It returns B_NOT_ALLOWED with result.pendingDeletes
	 * set, and changes nothing. Allow it after the user has confirmed.
	 */
			void				SetAllowLargeDelete(bool allow);

	static	const int32			kLargeDeleteMinimum = 3;

private:
			bool				_Stopped() const;

			CloudProvider&		fProvider;
			volatile bool*		fStop;
			bool				fAllowLargeDelete;
};


/* "name (conflict).ext", then "name (conflict 2).ext", ... */
BString			ConflictLeaf(const BString& leaf, int32 index);

#endif
