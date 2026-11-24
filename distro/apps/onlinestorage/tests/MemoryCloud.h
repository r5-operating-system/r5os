/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * In-memory OneDrive or Dropbox. Speaks the JSON those providers send.
 * There is no network and no account data.
 */

#ifndef ONLINE_STORAGE_MEMORY_CLOUD_H
#define ONLINE_STORAGE_MEMORY_CLOUD_H

#include "Transport.h"

#include <String.h>

#include <map>
#include <string>
#include <vector>


class MemoryCloud : public Transport {
public:
								MemoryCloud(const char* provider,
									const char* root);

	virtual	status_t			Exchange(HttpExchange& exchange);

			void				PutFile(const char* relative, const char* bytes);
			void				Remove(const char* relative);
			void				Rename(const char* from, const char* to);
			bool				Has(const char* relative) const;
			std::string			Read(const char* relative) const;

private:
			struct Node {
				std::string		id;
				std::string		revision;
				std::string		bytes;
				bool			folder;
			};
			struct Change {
				int				generation;
				std::string		path;
				bool			deleted;
				Node			node;
			};

			std::string			_Key(const std::string& relative) const;
			void				_Touch(const std::string& path, const Node& node,
									bool deleted);
			status_t			_OneDrive(HttpExchange& exchange);
			status_t			_Dropbox(HttpExchange& exchange);
			std::string			_ItemJson(const std::string& path,
									const Node& node, bool deleted) const;

			std::string			fProvider;
			std::string			fRoot;
			int					fNextId;
			int					fGeneration;
			std::map<std::string, Node> fNodes;
			std::vector<Change>	fChanges;
			std::map<std::string, std::string> fSessions;
			std::map<std::string, off_t> fSessionOffset;
			std::map<std::string, std::string> fSessionPath;
};


#endif
