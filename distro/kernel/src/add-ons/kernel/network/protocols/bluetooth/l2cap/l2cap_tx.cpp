/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * One send thread for everything L2CAP gives to the HCI layer
 * (docs/BLUETOOTH.md, review of steps 1c and 6).
 *
 * The HCI receive thread holds the device list lock (sListLock of the
 * bluetooth module) while it delivers a packet up to L2CAP, which takes an
 * endpoint lock; PostACL() takes the same list lock. A thread that sent
 * while holding an endpoint lock (connect(), close(), the send timer, a
 * credit-based channel, a fixed-channel sendto()) could so wait for the
 * receive thread while the receive thread waited for its endpoint: a
 * deadlock that hung all of Bluetooth. Now the senders only queue; this
 * thread calls PostACL() without holding any L2CAP lock. Packets keep
 * their order.
 */


#include <KernelExport.h>
#include <lock.h>
#include <stdlib.h>

#include <btDebug.h>

#include "l2cap_internal.h"


struct tx_entry {
	tx_entry*	next;
	hci_id		hid;
	net_buffer*	buffer;
};


static mutex sTXLock = MUTEX_INITIALIZER("l2cap tx");
static tx_entry* sTXHead = NULL;
static tx_entry* sTXTail = NULL;
static sem_id sTXSemaphore = -1;
static thread_id sTXThread = -1;
static volatile bool sTXQuit = false;


static status_t
l2cap_tx_thread(void*)
{
	while (true) {
		if (acquire_sem(sTXSemaphore) != B_OK)
			break;

		mutex_lock(&sTXLock);
		tx_entry* entry = sTXHead;
		if (entry != NULL) {
			sTXHead = entry->next;
			if (sTXHead == NULL)
				sTXTail = NULL;
		}
		mutex_unlock(&sTXLock);

		if (entry == NULL) {
			if (sTXQuit)
				break;
			continue;
		}
		// PostACL() owns the buffer, also when it fails.
		btDevices->PostACL(entry->hid, entry->buffer);
		free(entry);
	}
	return B_OK;
}


status_t
l2cap_tx_init()
{
	sTXQuit = false;
	sTXSemaphore = create_sem(0, "l2cap tx");
	if (sTXSemaphore < B_OK)
		return sTXSemaphore;
	sTXThread = spawn_kernel_thread(l2cap_tx_thread, "l2cap tx",
		B_URGENT_DISPLAY_PRIORITY, NULL);
	if (sTXThread < B_OK) {
		delete_sem(sTXSemaphore);
		sTXSemaphore = -1;
		return sTXThread;
	}
	resume_thread(sTXThread);
	return B_OK;
}


void
l2cap_tx_uninit()
{
	if (sTXThread >= 0) {
		sTXQuit = true;
		release_sem(sTXSemaphore);
		status_t result;
		wait_for_thread(sTXThread, &result);
		sTXThread = -1;
	}
	// What was still queued is not sent.
	mutex_lock(&sTXLock);
	tx_entry* entry = sTXHead;
	sTXHead = sTXTail = NULL;
	mutex_unlock(&sTXLock);
	while (entry != NULL) {
		tx_entry* next = entry->next;
		gBufferModule->free(entry->buffer);
		free(entry);
		entry = next;
	}
	if (sTXSemaphore >= 0) {
		delete_sem(sTXSemaphore);
		sTXSemaphore = -1;
	}
}


status_t
l2cap_post_acl(hci_id hid, net_buffer* buffer)
{
	if (buffer == NULL)
		return B_BAD_VALUE;
	tx_entry* entry = (tx_entry*)malloc(sizeof(tx_entry));
	if (entry == NULL || sTXSemaphore < 0) {
		free(entry);
		gBufferModule->free(buffer);
		return B_NO_MEMORY;
	}
	entry->next = NULL;
	entry->hid = hid;
	entry->buffer = buffer;

	mutex_lock(&sTXLock);
	if (sTXTail != NULL)
		sTXTail->next = entry;
	else
		sTXHead = entry;
	sTXTail = entry;
	mutex_unlock(&sTXLock);

	release_sem_etc(sTXSemaphore, 1, B_DO_NOT_RESCHEDULE);
	return B_OK;
}
