/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef ONLINE_STORAGE_CLIENT_CONFIG_H
#define ONLINE_STORAGE_CLIENT_CONFIG_H

/*
 * Placeholder until the owner registers a public desktop client.
 * Jam can override either macro. There is no client secret: both
 * providers are authorization-code + PKCE public clients.
 */
#ifndef R5_ONEDRIVE_CLIENT_ID
#define R5_ONEDRIVE_CLIENT_ID "00000000-0000-0000-0000-000000000000"
#endif

#ifndef R5_DROPBOX_CLIENT_ID
#define R5_DROPBOX_CLIENT_ID "r5-placeholder"
#endif

/* rclone's loopback port (lib/oauthutil bindPort). Bound on 127.0.0.1 only. */
static const int kOAuthLoopbackPort = 53682;

#endif
