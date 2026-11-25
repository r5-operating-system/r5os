// ClipboardHandler.h

#ifndef CLIPBOARD_HANDLER_H
#define CLIPBOARD_HANDLER_H

#include <Handler.h>
#include <Message.h>

class Clipboard;

class ClipboardHandler : public BHandler {
public:
	ClipboardHandler();
	virtual ~ClipboardHandler();

	virtual void MessageReceived(BMessage *message);

private:
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	Clipboard *_GetClipboard(uid_t uid, const char *name);
// SPDX-SnippetEnd

	struct ClipboardMap;

	ClipboardMap	*fClipboards;
};

#endif	// CLIPBOARD_HANDLER_H

