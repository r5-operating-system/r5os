/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Decisions made for this first version:
 * - Every sync lists the chosen folder (Changes with an empty cursor, or
 *   a delta applied onto the saved snapshot). The saved cursor is a tip
 *   taken after a successful sync, so the next delta is only later edits.
 * - A file the user deleted is removed on the other side only when that
 *   side is still the copy we last synced. If that side also changed, the
 *   changed copy is kept.
 * - A conflict leaves the cloud bytes on the original name and writes the
 *   local bytes beside them as "name (conflict)". Neither side is
 *   overwritten in place before the other copy exists.
 * - Content is SHA-256. A timestamp-only touch is not a change.
 * - Symlinks are skipped.
 */

#include "SyncEngine.h"

#include "LocalFile.h"
#include "Sha256.h"
#include "Url.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <map>
#include <vector>


SyncResult::SyncResult()
	:
	upToDate(false),
	uploaded(0),
	downloaded(0),
	deleted(0),
	conflicts(0)
{
}


BString
ConflictLeaf(const BString& leaf, int32 index)
{
	BString suffix = " (conflict)";
	if (index > 1) {
		suffix << " ";
		suffix << (int)index;
	}
	int32 dot = leaf.FindLast(".");
	BString out;
	if (dot > 0) {
		out.SetTo(leaf.String(), dot);
		out << suffix;
		out << leaf.String() + dot;
	} else
		out << leaf << suffix;
	return out;
}


struct Entry {
	BString				path;
	BString				id;
	BString				revision;
	BString				sha;
	/* OneDrive eTag, used as If-Match. Not the change token. */
	BString				matchToken;
	off_t				size;
	bool				isFolder;
	bool				present;

						Entry()
							: size(0), isFolder(false), present(false) {}
};


static BString
fold_path(const BString& path)
{
	BString out;
	for (int32 i = 0; i < path.Length(); i++) {
		char c = path.ByteAt(i);
		if (c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
		out << c;
	}
	return out;
}


/* Another spelling of the same path. OneDrive and Dropbox compare names
 * without regard to case, so uploading the other spelling replaces the file.
 */
static BString
same_fold(const std::map<BString, Entry>& view, const BString& path)
{
	BString want = fold_path(path);
	for (std::map<BString, Entry>::const_iterator it = view.begin();
			it != view.end(); it++) {
		if (it->second.isFolder || it->first == path)
			continue;
		if (fold_path(it->first) == want)
			return it->first;
	}
	return BString("");
}


static BString
local_join(const char* root, const BString& relative)
{
	BString path;
	path << root;
	if (relative.Length() > 0) {
		if (path.ByteAt(path.Length() - 1) != '/')
			path << "/";
		path << relative;
	}
	return path;
}


static void
scan_dir(const char* root, const BString& relative, std::map<BString, Entry>& out,
	status_t& status)
{
	if (status != B_OK)
		return;
	BString directory = local_join(root, relative);
	DIR* dir = opendir(directory.String());
	if (dir == NULL) {
		status = B_ENTRY_NOT_FOUND;
		return;
	}
	while (dirent* ent = readdir(dir)) {
		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
			continue;
		BString name = ent->d_name;
		if (name.FindFirst(".r5partial") >= 0)
			continue;
		BString rel = JoinRelative(relative, name);
		BString full = local_join(root, rel);
		struct stat st;
		if (lstat(full.String(), &st) != 0) {
			/* A name we cannot stat is not a delete. Stop before any change. */
			status = B_IO_ERROR;
			break;
		}
		if (S_ISLNK(st.st_mode))
			continue;
		Entry entry;
		entry.path = rel;
		entry.present = true;
		entry.size = (off_t)st.st_size;
		if (S_ISDIR(st.st_mode)) {
			entry.isFolder = true;
			out[rel] = entry;
			scan_dir(root, rel, out, status);
		} else if (S_ISREG(st.st_mode)) {
			if (Sha256File(full.String(), entry.sha) != B_OK) {
				status = B_IO_ERROR;
				break;
			}
			out[rel] = entry;
		}
	}
	closedir(dir);
}


static status_t
load_state(const char* path, BString& cursor, std::map<BString, Entry>& saved)
{
	cursor.SetTo("");
	saved.clear();
	FILE* file = fopen(path, "rb");
	if (file == NULL)
		return B_ENTRY_NOT_FOUND;
	char line[4096];
	while (fgets(line, sizeof(line), file) != NULL) {
		size_t length = strlen(line);
		while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
			line[--length] = '\0';
		if (line[0] == '#' || line[0] == '\0')
			continue;
		if (strncmp(line, "cursor ", 7) == 0) {
			cursor = DecodeForm(line + 7);
			continue;
		}
		char kind = line[0];
		if ((kind != 'F' && kind != 'D') || line[1] != ' ')
			continue;
		BString pathEnc;
		BString idEnc;
		BString revEnc;
		BString sizeText;
		BString shaEnc;
		const char* cursorText = line + 2;
		BString fields[6];
		int32 count = 0;
		const char* start = cursorText;
		for (const char* p = cursorText;; p++) {
			if (*p == ' ' || *p == '\0') {
				if (count < 6)
					fields[count].SetTo(start, (int32)(p - start));
				count++;
				start = p + 1;
				if (*p == '\0')
					break;
			}
		}
		Entry entry;
		entry.path = DecodeForm(fields[0].String());
		entry.id = DecodeForm(fields[1].String());
		entry.present = true;
		entry.isFolder = kind == 'D';
		if (!entry.isFolder && count >= 5) {
			entry.revision = DecodeForm(fields[2].String());
			entry.size = (off_t)strtoll(fields[3].String(), NULL, 10);
			entry.sha = fields[4];
			if (count >= 6)
				entry.matchToken = DecodeForm(fields[5].String());
		}
		if (entry.path.Length() > 0)
			saved[entry.path] = entry;
	}
	fclose(file);
	return B_OK;
}


static status_t
save_state(const char* path, const BString& cursor,
	const std::map<BString, Entry>& saved)
{
	BString temp;
	temp << path << ".r5partial";
	FILE* file = fopen(temp.String(), "wb");
	if (file == NULL)
		return B_IO_ERROR;
	fprintf(file, "# onlinestorage-state 1\n");
	fprintf(file, "cursor %s\n", EncodeForm(cursor.String()).String());
	for (std::map<BString, Entry>::const_iterator it = saved.begin();
			it != saved.end(); it++) {
		const Entry& entry = it->second;
		if (!entry.present)
			continue;
		if (entry.isFolder) {
			fprintf(file, "D %s %s\n", EncodeForm(entry.path.String()).String(),
				EncodeForm(entry.id.String()).String());
		} else {
			fprintf(file, "F %s %s %s %lld %s %s\n",
				EncodeForm(entry.path.String()).String(),
				EncodeForm(entry.id.String()).String(),
				EncodeForm(entry.revision.String()).String(),
				(long long)entry.size, entry.sha.String(),
				EncodeForm(entry.matchToken.String()).String());
		}
	}
	if (fclose(file) != 0) {
		remove(temp.String());
		return B_IO_ERROR;
	}
	if (rename(temp.String(), path) != 0) {
		remove(temp.String());
		return B_IO_ERROR;
	}
	return B_OK;
}


static void
apply_delta(std::map<BString, Entry>& view, const std::vector<CloudItem>& delta,
	bool replace)
{
	if (replace)
		view.clear();
	for (size_t i = 0; i < delta.size(); i++) {
		const CloudItem& item = delta[i];
		if (item.deleted || replace) {
			if (!item.id.IsEmpty()) {
				for (std::map<BString, Entry>::iterator it = view.begin();
						it != view.end();) {
					if (it->second.id == item.id)
						view.erase(it++);
					else
						++it;
				}
			}
			if (item.deleted) {
				view.erase(item.path);
				/* A deleted folder's children are not always listed. */
				if (item.path.Length() > 0) {
					BString prefix;
					prefix << item.path << "/";
					for (std::map<BString, Entry>::iterator child = view.begin();
							child != view.end();) {
						if (child->first.FindFirst(prefix.String()) == 0)
							view.erase(child++);
						else
							++child;
					}
				}
				continue;
			}
		}
		if (item.deleted)
			continue;
		if (!item.id.IsEmpty()) {
			for (std::map<BString, Entry>::iterator it = view.begin();
					it != view.end();) {
				if (it->second.id == item.id && it->first != item.path)
					view.erase(it++);
				else
					++it;
			}
		}
		Entry entry;
		entry.path = item.path;
		entry.id = item.id;
		entry.revision = item.revision;
		entry.size = item.size;
		entry.isFolder = item.isFolder;
		entry.matchToken = item.contentHash;
		entry.present = true;
		if (!replace) {
			std::map<BString, Entry>::iterator old = view.find(item.path);
			if (old != view.end()) {
				entry.sha = old->second.sha;
				if (entry.matchToken.Length() == 0)
					entry.matchToken = old->second.matchToken;
			}
		}
		view[item.path] = entry;
	}
}


static Entry
from_cloud(const CloudItem& item)
{
	Entry entry;
	entry.path = item.path;
	entry.id = item.id;
	entry.revision = item.revision;
	entry.size = item.size;
	entry.isFolder = item.isFolder;
	entry.matchToken = item.contentHash;
	entry.present = true;
	return entry;
}


static status_t
copy_file(const char* from, const char* to)
{
	FILE* in = fopen(from, "rb");
	if (in == NULL)
		return B_ENTRY_NOT_FOUND;
	std::vector<char> data;
	char buffer[8192];
	for (;;) {
		size_t n = fread(buffer, 1, sizeof(buffer), in);
		if (n > 0)
			data.insert(data.end(), buffer, buffer + n);
		if (n < sizeof(buffer)) {
			if (ferror(in)) {
				fclose(in);
				return B_IO_ERROR;
			}
			break;
		}
	}
	fclose(in);
	const void* bytes = data.empty() ? (const void*)"" : (const void*)&data[0];
	return LocalWriteAtomic(to, bytes, data.size());
}


static BString
find_id_path(const std::map<BString, Entry>& view, const BString& id)
{
	if (id.Length() == 0)
		return BString("");
	for (std::map<BString, Entry>::const_iterator it = view.begin();
			it != view.end(); it++) {
		if (it->second.id == id)
			return it->first;
	}
	return BString("");
}


SyncEngine::SyncEngine(CloudProvider& provider)
	:
	fProvider(provider)
{
}


status_t
SyncEngine::Sync(const char* localRoot, const char* statePath, SyncResult& result)
{
	result = SyncResult();
	if (localRoot == NULL || statePath == NULL)
		return B_BAD_VALUE;
	if (!LocalIsDirectory(localRoot)) {
		result.error = "The local folder is missing.";
		return B_ENTRY_NOT_FOUND;
	}

	BString cursor;
	std::map<BString, Entry> saved;
	load_state(statePath, cursor, saved);

	std::vector<CloudItem> delta;
	BString newCursor;
	status_t status = fProvider.Changes(cursor, newCursor, delta);
	if (status != B_OK) {
		result.error = "Could not read the cloud folder.";
		return status;
	}
	std::map<BString, Entry> remote = saved;
	apply_delta(remote, delta, cursor.Length() == 0);

	std::map<BString, Entry> local;
	status = B_OK;
	scan_dir(localRoot, "", local, status);
	if (status != B_OK) {
		result.error = "Could not read the local folder.";
		return status;
	}

	/* Remote renames: same id, new path, local copy still the old bytes. */
	for (std::map<BString, Entry>::iterator it = remote.begin();
			it != remote.end(); it++) {
		if (it->second.id.Length() == 0)
			continue;
		BString oldPath = find_id_path(saved, it->second.id);
		if (oldPath.Length() == 0 || oldPath == it->first)
			continue;
		std::map<BString, Entry>::iterator localOld = local.find(oldPath);
		std::map<BString, Entry>::iterator savedOld = saved.find(oldPath);
		if (localOld == local.end() || savedOld == saved.end())
			continue;
		if (localOld->second.isFolder != it->second.isFolder)
			continue;
		if (!localOld->second.isFolder && localOld->second.sha != savedOld->second.sha)
			continue;
		if (local.find(it->first) != local.end())
			continue;
		status = LocalRename(local_join(localRoot, oldPath).String(),
			local_join(localRoot, it->first).String());
		if (status != B_OK) {
			result.error = "Could not rename a local file.";
			return status;
		}
		Entry moved = localOld->second;
		moved.path = it->first;
		local.erase(oldPath);
		local[it->first] = moved;
		saved.erase(oldPath);
	}

	/* Local renames: one vanished path and one new path with the same hash. */
	std::vector<BString> vanished;
	std::vector<BString> appeared;
	for (std::map<BString, Entry>::iterator it = saved.begin();
			it != saved.end(); it++) {
		if (!it->second.isFolder && local.find(it->first) == local.end()
			&& remote.find(it->first) != remote.end()
			&& remote[it->first].revision == it->second.revision)
			vanished.push_back(it->first);
	}
	for (std::map<BString, Entry>::iterator it = local.begin();
			it != local.end(); it++) {
		if (!it->second.isFolder && saved.find(it->first) == saved.end()
			&& remote.find(it->first) == remote.end())
			appeared.push_back(it->first);
	}
	std::vector<bool> used(appeared.size(), false);
	for (size_t v = 0; v < vanished.size(); v++) {
		int matches = 0;
		size_t match = 0;
		for (size_t a = 0; a < appeared.size(); a++) {
			if (used[a])
				continue;
			if (local[appeared[a]].sha == saved[vanished[v]].sha
				&& local[appeared[a]].size == saved[vanished[v]].size)
				matches++, match = a;
		}
		if (matches != 1)
			continue;
		used[match] = true;
		status = fProvider.Move(vanished[v], appeared[match]);
		if (status != B_OK) {
			result.error = "Could not rename a cloud file.";
			return status;
		}
		CloudItem stat;
		if (fProvider.Stat(appeared[match], stat) == B_OK)
			remote[appeared[match]] = from_cloud(stat);
		else {
			remote[appeared[match]] = remote[vanished[v]];
			remote[appeared[match]].path = appeared[match];
		}
		remote.erase(vanished[v]);
		saved.erase(vanished[v]);
	}

	std::map<BString, bool> seen;
	for (std::map<BString, Entry>::iterator it = local.begin();
			it != local.end(); it++)
		seen[it->first] = true;
	for (std::map<BString, Entry>::iterator it = remote.begin();
			it != remote.end(); it++)
		seen[it->first] = true;
	for (std::map<BString, Entry>::iterator it = saved.begin();
			it != saved.end(); it++)
		seen[it->first] = true;

	for (std::map<BString, bool>::iterator it = seen.begin();
			it != seen.end(); it++) {
		const BString& path = it->first;
		bool inLocal = local.find(path) != local.end();
		bool inRemote = remote.find(path) != remote.end();
		bool inSaved = saved.find(path) != saved.end();
		Entry localEntry = inLocal ? local[path] : Entry();
		Entry remoteEntry = inRemote ? remote[path] : Entry();
		Entry savedEntry = inSaved ? saved[path] : Entry();

		if (inLocal && inRemote && localEntry.isFolder && remoteEntry.isFolder)
			continue;
		if (inLocal && localEntry.isFolder && !inRemote && inSaved) {
			bool keep = false;
			BString prefix;
			prefix << path << "/";
			for (std::map<BString, Entry>::iterator child = local.begin();
					child != local.end(); child++) {
				if (child->first.FindFirst(prefix.String()) != 0)
					continue;
				if (child->second.isFolder)
					continue;
				std::map<BString, Entry>::iterator previous = saved.find(child->first);
				if (previous == saved.end() || previous->second.sha != child->second.sha)
					keep = true;
			}
			if (!keep) {
				for (std::map<BString, Entry>::iterator child = local.begin();
						child != local.end();) {
					if (child->first.FindFirst(prefix.String()) != 0
						|| child->second.isFolder) {
						++child;
						continue;
					}
					/* The service may already have removed the child. */
					status = fProvider.Delete(child->first);
					if (status != B_OK) {
						result.error = "Could not delete a cloud file.";
						return status;
					}
					LocalRemoveFile(local_join(localRoot, child->first).String());
					local.erase(child++);
				}
				std::vector<BString> dirs;
				dirs.push_back(path);
				for (std::map<BString, Entry>::iterator child = local.begin();
						child != local.end(); child++) {
					if (child->second.isFolder
						&& child->first.FindFirst(prefix.String()) == 0)
						dirs.push_back(child->first);
				}
				for (size_t i = 0; i < dirs.size(); i++) {
					for (size_t j = i + 1; j < dirs.size(); j++) {
						if (dirs[j].Length() > dirs[i].Length()) {
							BString swap = dirs[i];
							dirs[i] = dirs[j];
							dirs[j] = swap;
						}
					}
				}
				for (size_t i = 0; i < dirs.size(); i++) {
					status = fProvider.Delete(dirs[i]);
					if (status != B_OK) {
						result.error = "Could not delete a cloud folder.";
						return status;
					}
					LocalRemoveEmptyDir(local_join(localRoot, dirs[i]).String());
					local.erase(dirs[i]);
				}
				continue;
			}
		}
		if (inLocal && localEntry.isFolder && !inRemote) {
			status = fProvider.CreateFolder(path);
			if (status != B_OK && status != B_FILE_EXISTS) {
				result.error = "Could not create a cloud folder.";
				return status;
			}
			remote[path] = localEntry;
			remote[path].isFolder = true;
			continue;
		}
		if (inRemote && remoteEntry.isFolder && !inLocal && inSaved) {
			bool keep = false;
			BString prefix;
			prefix << path << "/";
			for (std::map<BString, Entry>::iterator child = remote.begin();
					child != remote.end(); child++) {
				if (child->first.FindFirst(prefix.String()) != 0)
					continue;
				if (child->second.isFolder)
					continue;
				std::map<BString, Entry>::iterator previous = saved.find(child->first);
				if (previous == saved.end()
					|| previous->second.revision != child->second.revision)
					keep = true;
			}
			if (!keep) {
				std::vector<BString> children;
				for (std::map<BString, Entry>::iterator child = remote.begin();
						child != remote.end(); child++) {
					if (child->first.FindFirst(prefix.String()) == 0)
						children.push_back(child->first);
				}
				for (size_t i = 0; i < children.size(); i++) {
					if (!remote[children[i]].isFolder) {
						status = fProvider.Delete(children[i]);
						if (status != B_OK) {
							result.error = "Could not delete a cloud file.";
							return status;
						}
					}
					remote.erase(children[i]);
				}
				status = fProvider.Delete(path);
				if (status != B_OK) {
					result.error = "Could not delete a cloud folder.";
					return status;
				}
				remote.erase(path);
				continue;
			}
		}
		if (inRemote && remoteEntry.isFolder && !inLocal) {
			BString full = local_join(localRoot, path);
			full << "/.keep";
			status = LocalMakeParents(full.String());
			if (status != B_OK) {
				result.error = "Could not create a local folder.";
				return status;
			}
			local[path] = remoteEntry;
			continue;
		}

		bool localChanged = inSaved && inLocal && !localEntry.isFolder
			&& savedEntry.sha != localEntry.sha;
		bool remoteChanged = inSaved && inRemote && !remoteEntry.isFolder
			&& savedEntry.revision != remoteEntry.revision;

		if (inLocal && inRemote && !inSaved && !localEntry.isFolder
			&& !remoteEntry.isFolder) {
			bool same = localEntry.size == remoteEntry.size;
			BString remoteSha;
			if (same) {
				BString temp;
				temp << local_join(localRoot, path) << ".r5partial";
				if (fProvider.Download(path, temp.String()) == B_OK)
					Sha256File(temp.String(), remoteSha);
				remove(temp.String());
				same = remoteSha.Length() > 0 && remoteSha == localEntry.sha;
			}
			if (!same) {
				localChanged = true;
				remoteChanged = true;
				inSaved = true;
				savedEntry = remoteEntry;
				savedEntry.sha = "";
			} else {
				remoteEntry.sha = localEntry.sha;
				remote[path] = remoteEntry;
				continue;
			}
		}

		if (inLocal && !localEntry.isFolder && (!inRemote || localChanged)
			&& !remoteChanged) {
			CloudItem current;
			status_t looked = fProvider.Stat(path, current);
			if (looked == B_OK && !current.deleted && !current.isFolder) {
				BString expected = inRemote ? remoteEntry.revision : BString("");
				if (current.revision != expected) {
					remoteEntry = from_cloud(current);
					remote[path] = remoteEntry;
					inRemote = true;
					remoteChanged = true;
					localChanged = true;
					if (!inSaved) {
						inSaved = true;
						savedEntry.sha.SetTo("");
					}
				}
			} else if (looked != B_OK && looked != B_ENTRY_NOT_FOUND) {
				result.error = "Could not check the cloud file.";
				return looked;
			}
		}

		if (inLocal && inRemote && !localEntry.isFolder && !remoteEntry.isFolder
			&& localChanged && remoteChanged) {
			BString slot;
			for (int32 index = 1; index < 100; index++) {
				BString candidate = JoinRelative(ParentPath(path),
					ConflictLeaf(LeafName(path), index));
				BString full = local_join(localRoot, candidate);
				if (!LocalExists(full.String())) {
					slot = candidate;
					break;
				}
				BString existing;
				if (Sha256File(full.String(), existing) == B_OK
					&& existing == localEntry.sha) {
					slot = candidate;
					break;
				}
			}
			if (slot.Length() == 0) {
				result.error = "Could not name the conflict copy.";
				return B_IO_ERROR;
			}
			BString slotFull = local_join(localRoot, slot);
			if (!LocalExists(slotFull.String())) {
				status = copy_file(local_join(localRoot, path).String(),
					slotFull.String());
				if (status != B_OK) {
					result.error = "Could not keep both copies.";
					return status;
				}
			}
			bool remoteHasSlot = remote.find(slot) != remote.end();
			if (!remoteHasSlot) {
				BString parent = ParentPath(slot);
				if (parent.Length() > 0)
					fProvider.CreateFolder(parent);
				status = fProvider.Upload(slot, slotFull.String());
				if (status != B_OK) {
					result.error = "Could not upload the conflict copy.";
					return status;
				}
				CloudItem stat;
				Entry uploaded;
				uploaded.path = slot;
				uploaded.sha = localEntry.sha;
				uploaded.size = localEntry.size;
				uploaded.present = true;
				if (fProvider.Stat(slot, stat) == B_OK)
					uploaded = from_cloud(stat);
				uploaded.sha = localEntry.sha;
				remote[slot] = uploaded;
				local[slot] = uploaded;
				local[slot].sha = localEntry.sha;
			}
			status = fProvider.Download(path, local_join(localRoot, path).String());
			if (status != B_OK) {
				result.error = "Could not download the cloud copy.";
				return status;
			}
			BString downloadedSha;
			Sha256File(local_join(localRoot, path).String(), downloadedSha);
			remoteEntry.sha = downloadedSha;
			remote[path] = remoteEntry;
			local[path] = remoteEntry;
			local[path].sha = downloadedSha;
			result.conflicts++;
			result.uploaded++;
			result.downloaded++;
			continue;
		}

		if (!inRemote && inLocal && inSaved && !localChanged
			&& !localEntry.isFolder) {
			status = LocalRemoveFile(local_join(localRoot, path).String());
			if (status != B_OK) {
				result.error = "Could not delete a local file.";
				return status;
			}
			local.erase(path);
			remote.erase(path);
			result.deleted++;
			continue;
		}

		if (inRemote && !inLocal && inSaved && !remoteChanged
			&& !remoteEntry.isFolder) {
			status = fProvider.Delete(path);
			if (status != B_OK) {
				result.error = "Could not delete a cloud file.";
				return status;
			}
			remote.erase(path);
			result.deleted++;
			continue;
		}

		if (inLocal && !localEntry.isFolder && (!inRemote || localChanged)
			&& !remoteChanged) {
			BString parent = ParentPath(path);
			if (parent.Length() > 0)
				fProvider.CreateFolder(parent);
			BString other = same_fold(remote, path);
			if (!inRemote && other.Length() > 0) {
				Entry cloud = remote[other];
				bool same = localEntry.size == cloud.size && cloud.sha.Length() > 0
					&& cloud.sha == localEntry.sha;
				if (!same && localEntry.size == cloud.size) {
					BString temp;
					temp << local_join(localRoot, path) << ".r5partial";
					BString remoteSha;
					if (fProvider.Download(other, temp.String()) == B_OK)
						Sha256File(temp.String(), remoteSha);
					remove(temp.String());
					same = remoteSha.Length() > 0 && remoteSha == localEntry.sha;
					cloud.sha = remoteSha;
					remote[other] = cloud;
				}
				if (same) {
					if (local.find(other) == local.end()) {
						LocalRename(local_join(localRoot, path).String(),
							local_join(localRoot, other).String());
						local[other] = localEntry;
						local[other].path = other;
						local[other].sha = localEntry.sha;
					} else
						LocalRemoveFile(local_join(localRoot, path).String());
					local.erase(path);
					continue;
				}
				BString slot = JoinRelative(ParentPath(other),
					ConflictLeaf(LeafName(path), 1));
				BString slotFull = local_join(localRoot, slot);
				status = copy_file(local_join(localRoot, path).String(),
					slotFull.String());
				if (status != B_OK) {
					result.error = "Could not keep both copies.";
					return status;
				}
				LocalRemoveFile(local_join(localRoot, path).String());
				local.erase(path);
				status = fProvider.Upload(slot, slotFull.String(), NULL);
				if (status != B_OK) {
					result.error = "Could not upload the conflict copy.";
					return status;
				}
				CloudItem stat;
				Entry uploaded;
				uploaded.path = slot;
				uploaded.sha = localEntry.sha;
				uploaded.size = localEntry.size;
				uploaded.present = true;
				if (fProvider.Stat(slot, stat) == B_OK)
					uploaded = from_cloud(stat);
				uploaded.sha = localEntry.sha;
				remote[slot] = uploaded;
				local[slot] = uploaded;
				result.conflicts++;
				result.uploaded++;
				continue;
			}
			const char* match = NULL;
			if (inRemote) {
				if (strcmp(fProvider.Name(), "onedrive") == 0) {
					if (remoteEntry.matchToken.Length() > 0)
						match = remoteEntry.matchToken.String();
				} else if (remoteEntry.revision.Length() > 0)
					match = remoteEntry.revision.String();
			}
			status = fProvider.Upload(path,
				local_join(localRoot, path).String(), match);
			if (status == B_BUSY) {
				CloudItem again;
				if (fProvider.Stat(path, again) != B_OK || again.deleted) {
					result.error = "The cloud file changed during upload.";
					return B_BUSY;
				}
				remoteEntry = from_cloud(again);
				remote[path] = remoteEntry;
				BString slot = JoinRelative(ParentPath(path),
					ConflictLeaf(LeafName(path), 1));
				BString slotFull = local_join(localRoot, slot);
				status = copy_file(local_join(localRoot, path).String(),
					slotFull.String());
				if (status != B_OK) {
					result.error = "Could not keep both copies.";
					return status;
				}
				status = fProvider.Upload(slot, slotFull.String(), NULL);
				if (status != B_OK) {
					result.error = "Could not upload the conflict copy.";
					return status;
				}
				status = fProvider.Download(path,
					local_join(localRoot, path).String());
				if (status != B_OK) {
					result.error = "Could not download the cloud copy.";
					return status;
				}
				result.conflicts++;
				result.uploaded++;
				result.downloaded++;
				continue;
			}
			if (status != B_OK) {
				result.error = "Could not upload a file.";
				return status;
			}
			CloudItem stat;
			Entry uploaded = localEntry;
			if (fProvider.Stat(path, stat) == B_OK) {
				uploaded = from_cloud(stat);
				uploaded.sha = localEntry.sha;
			}
			remote[path] = uploaded;
			result.uploaded++;
			continue;
		}

		if (inRemote && !remoteEntry.isFolder && (!inLocal || remoteChanged)
			&& !localChanged) {
			status = fProvider.Download(path, local_join(localRoot, path).String());
			if (status != B_OK) {
				result.error = "Could not download a file.";
				return status;
			}
			BString sha;
			Sha256File(local_join(localRoot, path).String(), sha);
			remoteEntry.sha = sha;
			remote[path] = remoteEntry;
			local[path] = remoteEntry;
			result.downloaded++;
			continue;
		}
	}

	/*
	 * Keep the cursor from the listing. A tip taken after the uploads
	 * would skip a cloud edit that arrived while this sync was running,
	 * and the next local edit could replace it.
	 */
	status = save_state(statePath, newCursor, remote);
	if (status != B_OK) {
		result.error = "Could not record the sync.";
		return status;
	}
	result.upToDate = true;
	result.error.SetTo("");
	return B_OK;
}
