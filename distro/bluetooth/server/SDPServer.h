/*
 * Copyright 2007 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef _SDP_SERVER_H_
#define _SDP_SERVER_H_


#include "OS.h"
#include "SupportDefs.h"


class SDPServer {
public:

							SDPServer();
							~SDPServer();

		status_t            Start();
        void                Stop();

private:
        static int32        _ListenThread(void* data);
        int32               _Run();

        thread_id           fThreadID;
		int                 fServerSocket;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		int32               fClientSocket;
// SPDX-SnippetEnd

        bool                fIsShuttingDown;
};


#endif
