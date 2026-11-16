/* 
 * Copyright 2008 Oliver Ruiz Dorantes. All rights reserved.
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef L2CAP_INTERNAL_H
#define L2CAP_INTERNAL_H


#include <btCoreData.h>
#include <net_protocol.h>
#include <net_stack.h>


extern bluetooth_core_data_module_info *btCoreData;
extern bt_hci_module_info* btDevices;
extern net_buffer_module_info *gBufferModule;
extern net_stack_module_info *gStackModule;
extern net_socket_module_info *gSocketModule;


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <util/AutoLock.h>

// A command identifier holds a socket reference (acquire_socket() when it
// is allocated). Whoever takes the identifier out of the table owns that
// reference and releases it: the receive thread for an answer, or the
// waiting thread when it gives up. Taking it under the connection lock
// makes sure only one of them does.

// Takes `ident` out of the table and returns its endpoint, or NULL.
static inline void*
l2cap_take_command_ident(HciConnection* conn, uint8 ident)
{
	MutexLocker _(&conn->fLock);
	auto iter = conn->fInUseIdents.Find(ident);
	if (iter == conn->fInUseIdents.End())
		return NULL;
	void* owner = iter->Value();
	conn->fInUseIdents.Remove(ident);
	return owner;
}


// Takes `ident` out of the table only while it still belongs to `owner`.
static inline bool
l2cap_claim_command_ident(HciConnection* conn, uint8 ident, void* owner)
{
	MutexLocker _(&conn->fLock);
	auto iter = conn->fInUseIdents.Find(ident);
	if (iter == conn->fInUseIdents.End() || iter->Value() != owner)
		return false;
	conn->fInUseIdents.Remove(ident);
	return true;
}


// Puts back an identifier that still waits for more answers (a pending
// connection, a configuration continuation).
static inline void
l2cap_keep_command_ident(HciConnection* conn, uint8 ident, void* owner)
{
	MutexLocker _(&conn->fLock);
	conn->fInUseIdents.Insert(ident, owner);
}
// SPDX-SnippetEnd


#endif	// L2CAP_INTERNAL_H
