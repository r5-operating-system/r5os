/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#include "Provider.h"


CloudItem::CloudItem()
	:
	size(0),
	isFolder(false),
	deleted(false)
{
}


CloudProvider::~CloudProvider()
{
}


BString
JoinRelative(const BString& parent, const BString& name)
{
	if (parent.Length() == 0)
		return name;
	if (name.Length() == 0)
		return parent;
	BString out = parent;
	out << "/" << name;
	return out;
}


BString
ParentPath(const BString& path)
{
	int32 slash = path.FindLast("/");
	if (slash < 0)
		return BString("");
	BString parent;
	parent.SetTo(path.String(), slash);
	return parent;
}


BString
LeafName(const BString& path)
{
	int32 slash = path.FindLast("/");
	if (slash < 0)
		return path;
	BString leaf = path;
	leaf.Remove(0, slash + 1);
	return leaf;
}
