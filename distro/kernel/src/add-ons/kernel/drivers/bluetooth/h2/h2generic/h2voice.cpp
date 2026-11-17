/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Voice links for h2generic (h2voice.h, docs/BLUETOOTH.md, step 7c).
 *
 * Who touches what:
 *	- event_complete() (the USB callback of the one event transfer) follows
 *	  the events and marks a slot dirty; it never blocks.
 *	- The "h2generic voice" thread starts and stops the transfers of a
 *	  dirty slot, under the slot's mutex: set_alt_interface() blocks.
 *	- voice_rx_complete() and voice_tx_complete() (USB callbacks) only use
 *	  the slot's spinlock and its own transfer; they never touch a
 *	  bt_usb_dev.
 *	- The node hooks take the slot's mutex for write() and open(), the
 *	  spinlock for read().
 * slot.bdev is set by device_open() and cleared by device_close() under the
 * mutex, before the radio can be freed (device_free() comes after close).
 */


#include "h2voice.h"

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <driver_settings.h>
#include <kernel.h>
#include <KernelExport.h>
#include <lock.h>

#include <R5BluetoothVoice.h>

#include "h2debug.h"
#include "h2sco.h"


// Transfers kept in flight each way. Each carries H2_VOICE_ISO_PACKETS
// USB frames, 8 ms at full speed.
#define H2_VOICE_RX_TRANSFERS		2
#define H2_VOICE_ISO_PACKETS		8

#define VOICE_NODE_PREFIX			R5_BTVOICE_DIRECTORY "/"


struct voice_rx_transfer {
	int32						num;
	uint32						generation;
	usb_iso_packet_descriptor	descriptors[H2_VOICE_ISO_PACKETS];
	uint8						data[H2_VOICE_ISO_PACKETS
									* H2_VOICE_MAX_ISO_PACKET];
};

struct voice_tx_transfer {
	int32						num;
	uint32						generation;
	bool						busy;
	usb_iso_packet_descriptor	descriptors[(H2_SCO_MAX_PACKET
									+ H2_VOICE_BYTES_8BIT - 1)
									/ H2_VOICE_BYTES_8BIT];
	uint8						data[H2_SCO_MAX_PACKET];
};

// Allocated at the first start and kept until the driver is unloaded, so a
// late USB callback never finds it freed.
struct voice_buffers {
	h2_sco_ring					ring;
	voice_rx_transfer			rx[H2_VOICE_RX_TRANSFERS];
	voice_tx_transfer			tx[R5_BTVOICE_TX_SLOTS];
};

struct voice_slot {
	mutex						lock;
	spinlock					spin;
	sem_id						readable;

	// Under lock.
	bt_usb_dev*					bdev;
	bool						opened;
	int							alt;
	usb_pipe					inPipe;
	usb_pipe					outPipe;
	uint16						inSize;
	uint16						outSize;
	voice_buffers*				buffers;

	// Under spin.
	bool						running;
	uint32						generation;
	bool						linkUp;
	uint16						handle;
	uint8						airMode;
	uint16						rxLength;
	uint16						txLength;
	uint32						rxPackets;
	h2_sco_assembler			assembler;

	// Only event_complete() (one event transfer per radio).
	h2_voice_events				events;

	int32						dirty;
};


static bool sEnabled = false;
static voice_slot sSlots[MAX_BT_GENERIC_USB_DEVICES];
static sem_id sWake = -1;
static thread_id sThread = -1;
static volatile bool sQuit = false;


static cpu_status
slot_lock(voice_slot& slot)
{
	cpu_status state = disable_interrupts();
	acquire_spinlock(&slot.spin);
	return state;
}


static void
slot_unlock(voice_slot& slot, cpu_status state)
{
	release_spinlock(&slot.spin);
	restore_interrupts(state);
}


static const char*
air_mode_name(uint8 airMode)
{
	switch (airMode) {
		case H2_AIR_MODE_ULAW:
			return "u-law";
		case H2_AIR_MODE_ALAW:
			return "A-law";
		case H2_AIR_MODE_CVSD:
			return "CVSD";
		case H2_AIR_MODE_TRANSPARENT:
			return "transparent";
	}
	return "unknown";
}


// #pragma mark - transfers


static void voice_rx_complete(void* cookie, status_t status, void* data,
	size_t actualLength);


struct RingSink {
	voice_slot*	slot;
	int			pushed;

	void operator()(const uint8* packet, size_t size)
	{
		if (h2_sco_ring_push(&slot->buffers->ring, packet, size)) {
			slot->rxPackets++;
			pushed++;
		}
	}
};


static status_t
voice_queue_rx(voice_slot& slot, voice_rx_transfer* transfer)
{
	for (int i = 0; i < H2_VOICE_ISO_PACKETS; i++) {
		transfer->descriptors[i].request_length = slot.inSize;
		transfer->descriptors[i].actual_length = 0;
		transfer->descriptors[i].status = B_OK;
	}
	uint32 frame = 0;
	return usb->queue_isochronous(slot.inPipe, transfer->data,
		(size_t)H2_VOICE_ISO_PACKETS * slot.inSize, transfer->descriptors,
		H2_VOICE_ISO_PACKETS, &frame, USB_ISO_ASAP, voice_rx_complete,
		transfer);
}


static void
voice_rx_complete(void* cookie, status_t status, void* data,
	size_t actualLength)
{
	voice_rx_transfer* transfer = (voice_rx_transfer*)cookie;
	if (transfer == NULL || status == B_CANCELED)
		return;
	if (transfer->num < 0 || transfer->num >= MAX_BT_GENERIC_USB_DEVICES)
		return;
	voice_slot& slot = sSlots[transfer->num];

	cpu_status state = slot_lock(slot);
	if (!slot.running || transfer->generation != slot.generation) {
		slot_unlock(slot, state);
		return;
	}

	RingSink sink = { &slot, 0 };
	if (status == B_OK || status == B_DEV_DATA_UNDERRUN) {
		// inSize was checked against H2_VOICE_MAX_ISO_PACKET at start, and
		// each length is checked against it here: the bus reports it.
		for (int i = 0; i < H2_VOICE_ISO_PACKETS; i++) {
			const usb_iso_packet_descriptor& packet
				= transfer->descriptors[i];
			size_t length = packet.actual_length;
			if (length > slot.inSize)
				length = slot.inSize;
			h2_sco_assemble(&slot.assembler, slot.handle,
				transfer->data + (size_t)i * slot.inSize, length,
				packet.status == B_OK, sink);
		}
	} else {
		h2_sco_assemble(&slot.assembler, slot.handle, NULL, 0, false, sink);
	}
	slot_unlock(slot, state);

	if (sink.pushed > 0)
		release_sem_etc(slot.readable, 1, B_DO_NOT_RESCHEDULE);

	if (status == B_DEV_STALLED) {
		ERROR("voice endpoint stalled, no longer receiving\n");
		return;
	}

	// A stop between the check above and this queue is harmless: the stop
	// changes the alternate setting, which removes this pipe and its
	// transfers, and a callback of the old generation does not queue again.
	status_t error = voice_queue_rx(slot, transfer);
	if (error != B_OK)
		ERROR("voice receive stopped: %s\n", strerror(error));
}


static void
voice_tx_complete(void* cookie, status_t status, void* data,
	size_t actualLength)
{
	voice_tx_transfer* transfer = (voice_tx_transfer*)cookie;
	if (transfer == NULL || transfer->num < 0
		|| transfer->num >= MAX_BT_GENERIC_USB_DEVICES)
		return;
	voice_slot& slot = sSlots[transfer->num];

	cpu_status state = slot_lock(slot);
	if (transfer->generation == slot.generation)
		transfer->busy = false;
	slot_unlock(slot, state);
}


// #pragma mark - start and stop


static void
endpoint_sizes(const usb_interface_info* info, uint16* _in, uint16* _out,
	usb_pipe* _inPipe, usb_pipe* _outPipe)
{
	*_in = 0;
	*_out = 0;
	if (_inPipe != NULL)
		*_inPipe = 0;
	if (_outPipe != NULL)
		*_outPipe = 0;
	if (info == NULL || info->descr == NULL)
		return;

	for (size_t e = 0; e < info->endpoint_count; e++) {
		const usb_endpoint_info& endpoint = info->endpoint[e];
		if (endpoint.descr == NULL
			|| (endpoint.descr->attributes & USB_ENDPOINT_ATTR_MASK)
				!= USB_ENDPOINT_ATTR_ISOCHRONOUS)
			continue;
		// Bits 10..0 are the size (USB 2.0, 9.6.6).
		uint16 size = endpoint.descr->max_packet_size & 0x07ff;
		if (endpoint.descr->endpoint_address & USB_ENDPOINT_ADDR_DIR_IN) {
			if (*_in == 0) {
				*_in = size;
				if (_inPipe != NULL)
					*_inPipe = endpoint.handle;
			}
		} else if (*_out == 0) {
			*_out = size;
			if (_outPipe != NULL)
				*_outPipe = endpoint.handle;
		}
	}
}


static void
voice_stop_locked(voice_slot& slot)
{
	cpu_status state = slot_lock(slot);
	bool wasRunning = slot.running;
	slot.running = false;
	slot.generation++;
	slot_unlock(slot, state);
	if (!wasRunning)
		return;

	bt_usb_dev* bdev = slot.bdev;
	if (slot.inPipe != 0)
		usb->cancel_queued_transfers(slot.inPipe);
	if (slot.outPipe != 0)
		usb->cancel_queued_transfers(slot.outPipe);
	slot.inPipe = 0;
	slot.outPipe = 0;

	if (bdev != NULL && bdev->connected) {
		const usb_configuration_info* config
			= usb->get_nth_configuration(bdev->dev, 0);
		if (config != NULL && config->interface_count > 1
			&& config->interface[1].alt_count > 0) {
			status_t status = usb->set_alt_interface(bdev->dev,
				&config->interface[1].alt[0]);
			if (status != B_OK) {
				ERROR("voice off, but alternate setting 0 was "
					"refused: %s\n", strerror(status));
			}
		}
	}
	slot.alt = 0;
	release_sem_etc(slot.readable, 1, B_DO_NOT_RESCHEDULE);
	ERROR("voice off (%s)\n",
		bdev != NULL ? bdev->name : "radio gone");
}


static void
voice_start_locked(voice_slot& slot, int32 num, uint16 handle, uint8 airMode)
{
	bt_usb_dev* bdev = slot.bdev;
	if (bdev == NULL || !bdev->connected || (bdev->state & RUNNING) == 0)
		return;

	const usb_configuration_info* config
		= usb->get_nth_configuration(bdev->dev, 0);
	if (config == NULL || config->interface_count < 2) {
		ERROR("voice link, but the radio has no isochronous "
			"interface\n");
		return;
	}
	const usb_interface_list* list = &config->interface[1];

	uint16 inSizes[H2_VOICE_MAX_ALTS];
	uint16 outSizes[H2_VOICE_MAX_ALTS];
	int count = list->alt_count < H2_VOICE_MAX_ALTS
		? (int)list->alt_count : H2_VOICE_MAX_ALTS;
	for (int alt = 0; alt < count; alt++) {
		endpoint_sizes(&list->alt[alt], &inSizes[alt], &outSizes[alt], NULL,
			NULL);
	}
	uint16 need = h2_voice_bytes_per_frame(airMode);
	int alt = h2_voice_choose_alt(inSizes, outSizes, count, need);
	if (alt < 0) {
		ERROR("voice link (%s), but no alternate setting has "
			"%u-byte isochronous packets\n", air_mode_name(airMode), need);
		return;
	}

	if (slot.buffers == NULL) {
		slot.buffers = (voice_buffers*)calloc(1, sizeof(voice_buffers));
		if (slot.buffers == NULL) {
			ERROR("no memory for voice\n");
			return;
		}
	}

	status_t status = usb->set_alt_interface(bdev->dev, &list->alt[alt]);
	if (status != B_OK) {
		ERROR("voice link, but alternate setting %d was refused: "
			"%s\n", alt, strerror(status));
		return;
	}

	uint16 inSize;
	uint16 outSize;
	usb_pipe inPipe;
	usb_pipe outPipe;
	endpoint_sizes(list->active, &inSize, &outSize, &inPipe, &outPipe);
	if (inPipe == 0 || outPipe == 0 || inSize < need || outSize < need
		|| inSize > H2_VOICE_MAX_ISO_PACKET
		|| outSize > H2_VOICE_MAX_ISO_PACKET) {
		ERROR("alternate setting %d has no usable voice "
			"endpoints\n", alt);
		usb->set_alt_interface(bdev->dev, &list->alt[0]);
		return;
	}

	slot.alt = alt;
	slot.inPipe = inPipe;
	slot.outPipe = outPipe;
	slot.inSize = inSize;
	slot.outSize = outSize;

	cpu_status state = slot_lock(slot);
	slot.generation++;
	uint32 generation = slot.generation;
	slot.running = true;
	slot.rxPackets = 0;
	h2_sco_ring_reset(&slot.buffers->ring);
	h2_sco_assembler_reset(&slot.assembler);
	for (int i = 0; i < R5_BTVOICE_TX_SLOTS; i++) {
		slot.buffers->tx[i].num = num;
		slot.buffers->tx[i].generation = generation;
		slot.buffers->tx[i].busy = false;
	}
	for (int i = 0; i < H2_VOICE_RX_TRANSFERS; i++) {
		slot.buffers->rx[i].num = num;
		slot.buffers->rx[i].generation = generation;
	}
	slot_unlock(slot, state);

	int queued = 0;
	for (int i = 0; i < H2_VOICE_RX_TRANSFERS; i++) {
		status = voice_queue_rx(slot, &slot.buffers->rx[i]);
		if (status != B_OK) {
			ERROR("voice receive could not start: %s\n",
				strerror(status));
			break;
		}
		queued++;
	}
	if (queued == 0) {
		voice_stop_locked(slot);
		return;
	}

	ERROR("voice on (%s, %s), alternate setting %d, %u-byte "
		"packets\n", bdev->name, air_mode_name(airMode), alt, inSize);
}


static status_t
voice_thread(void*)
{
	while (!sQuit) {
		acquire_sem(sWake);
		if (sQuit)
			break;

		for (int32 num = 0; num < MAX_BT_GENERIC_USB_DEVICES; num++) {
			voice_slot& slot = sSlots[num];
			if (atomic_get_and_set(&slot.dirty, 0) == 0)
				continue;

			mutex_lock(&slot.lock);
			cpu_status state = slot_lock(slot);
			bool want = slot.linkUp;
			bool running = slot.running;
			uint16 handle = slot.handle;
			uint8 airMode = slot.airMode;
			slot_unlock(slot, state);

			if (running && !want)
				voice_stop_locked(slot);
			else if (!running && want)
				voice_start_locked(slot, num, handle, airMode);
			mutex_unlock(&slot.lock);
		}
	}
	return B_OK;
}


// #pragma mark - driver hooks


void
h2_voice_init()
{
	sEnabled = false;
	void* settings = load_driver_settings("h2generic");
	if (settings != NULL) {
		sEnabled = get_driver_boolean_parameter(settings, "bt_voice", false,
			true);
		unload_driver_settings(settings);
	}
	if (!sEnabled)
		return;

	for (int32 num = 0; num < MAX_BT_GENERIC_USB_DEVICES; num++) {
		voice_slot& slot = sSlots[num];
		memset(&slot, 0, sizeof(slot));
		mutex_init(&slot.lock, "h2generic voice");
		B_INITIALIZE_SPINLOCK(&slot.spin);
		slot.readable = create_sem(0, "h2generic voice readable");
		h2_voice_events_reset(&slot.events);
	}

	sQuit = false;
	sWake = create_sem(0, "h2generic voice");
	if (sWake >= 0) {
		sThread = spawn_kernel_thread(voice_thread, "h2generic voice",
			B_URGENT_DISPLAY_PRIORITY, NULL);
		if (sThread >= 0)
			resume_thread(sThread);
	}
	if (sWake < 0 || sThread < 0) {
		ERROR("bt_voice is set, but voice could not start\n");
		if (sWake >= 0)
			delete_sem(sWake);
		sWake = -1;
		for (int32 num = 0; num < MAX_BT_GENERIC_USB_DEVICES; num++) {
			delete_sem(sSlots[num].readable);
			mutex_destroy(&sSlots[num].lock);
		}
		sEnabled = false;
		return;
	}
	ERROR("voice enabled (bt_voice)\n");
}


void
h2_voice_uninit()
{
	if (!sEnabled)
		return;

	sQuit = true;
	release_sem(sWake);
	status_t result;
	wait_for_thread(sThread, &result);
	sThread = -1;
	delete_sem(sWake);
	sWake = -1;

	for (int32 num = 0; num < MAX_BT_GENERIC_USB_DEVICES; num++) {
		voice_slot& slot = sSlots[num];
		// Every radio was closed before the driver goes, so no transfer
		// is left that could use the buffers.
		free(slot.buffers);
		slot.buffers = NULL;
		delete_sem(slot.readable);
		mutex_destroy(&slot.lock);
	}
	sEnabled = false;
}


bool
h2_voice_enabled()
{
	return sEnabled;
}


void
h2_voice_device_open(bt_usb_dev* bdev)
{
	if (!sEnabled || bdev == NULL || bdev->num < 0
		|| bdev->num >= MAX_BT_GENERIC_USB_DEVICES)
		return;
	voice_slot& slot = sSlots[bdev->num];

	mutex_lock(&slot.lock);
	slot.bdev = bdev;
	h2_voice_events_reset(&slot.events);
	cpu_status state = slot_lock(slot);
	slot.linkUp = false;
	slot_unlock(slot, state);
	mutex_unlock(&slot.lock);
}


void
h2_voice_device_close(bt_usb_dev* bdev)
{
	if (!sEnabled || bdev == NULL || bdev->num < 0
		|| bdev->num >= MAX_BT_GENERIC_USB_DEVICES)
		return;
	voice_slot& slot = sSlots[bdev->num];

	mutex_lock(&slot.lock);
	cpu_status state = slot_lock(slot);
	slot.linkUp = false;
	slot_unlock(slot, state);
	voice_stop_locked(slot);
	slot.bdev = NULL;
	mutex_unlock(&slot.lock);
	release_sem_etc(slot.readable, 1, 0);
}


struct LinkSink {
	voice_slot*	slot;
	bool		changed;

	void operator()(const h2_voice_event& event)
	{
		cpu_status state = slot_lock(*slot);
		if (event.kind == H2_VOICE_LINK_UP) {
			// One voice link per radio, as HFP uses it.
			if (!slot->linkUp) {
				slot->linkUp = true;
				slot->handle = event.handle;
				slot->airMode = event.airMode;
				slot->rxLength = event.rxLength;
				slot->txLength = event.txLength;
				changed = true;
			}
		} else if (slot->linkUp && event.handle == slot->handle) {
			slot->linkUp = false;
			changed = true;
		}
		slot_unlock(*slot, state);
	}
};


void
h2_voice_event_bytes(bt_usb_dev* bdev, const uint8* data, size_t size)
{
	if (!sEnabled || bdev == NULL || data == NULL || bdev->num < 0
		|| bdev->num >= MAX_BT_GENERIC_USB_DEVICES)
		return;
	voice_slot& slot = sSlots[bdev->num];

	LinkSink sink = { &slot, false };
	h2_voice_events_feed(&slot.events, data, size, sink);
	if (sink.changed) {
		atomic_set(&slot.dirty, 1);
		release_sem_etc(sWake, 1, B_DO_NOT_RESCHEDULE);
	}
}


char*
h2_voice_node_name(int32 num)
{
	if (!sEnabled || num < 0 || num >= MAX_BT_GENERIC_USB_DEVICES)
		return NULL;
	char name[64];
	snprintf(name, sizeof(name), VOICE_NODE_PREFIX "%" B_PRId32, num);
	return strdup(name);
}


// #pragma mark - the voice node


static int32
node_number(const char* name)
{
	size_t prefix = strlen(VOICE_NODE_PREFIX);
	if (name == NULL || strncmp(name, VOICE_NODE_PREFIX, prefix) != 0)
		return -1;
	const char* digits = name + prefix;
	if (digits[0] < '0' || digits[0] > '9')
		return -1;
	int32 num = 0;
	for (; *digits != '\0'; digits++) {
		if (*digits < '0' || *digits > '9')
			return -1;
		num = num * 10 + (*digits - '0');
		if (num >= MAX_BT_GENERIC_USB_DEVICES)
			return -1;
	}
	return num;
}


static status_t
voice_open(const char* name, uint32 flags, void** _cookie)
{
	int32 num = node_number(name);
	if (!sEnabled || num < 0)
		return B_ENTRY_NOT_FOUND;
	// Only media_addon_server, a system service, carries voice.
	if (geteuid() != 0)
		return B_PERMISSION_DENIED;

	voice_slot& slot = sSlots[num];
	mutex_lock(&slot.lock);
	status_t status = B_OK;
	if (slot.bdev == NULL)
		status = B_DEV_NOT_READY;
	else if (slot.opened)
		status = B_BUSY;
	else
		slot.opened = true;
	mutex_unlock(&slot.lock);
	if (status != B_OK)
		return status;

	*_cookie = &slot;
	return B_OK;
}


static status_t
voice_close(void* cookie)
{
	voice_slot* slot = (voice_slot*)cookie;
	mutex_lock(&slot->lock);
	slot->opened = false;
	mutex_unlock(&slot->lock);
	release_sem_etc(slot->readable, 1, 0);
	return B_OK;
}


static status_t
voice_free(void* cookie)
{
	return B_OK;
}


static status_t
voice_read(void* cookie, off_t position, void* buffer, size_t* _length)
{
	voice_slot* slot = (voice_slot*)cookie;
	size_t capacity = *_length;
	*_length = 0;
	if (buffer == NULL || !IS_USER_ADDRESS(buffer))
		return B_BAD_ADDRESS;

	uint8 packet[H2_SCO_MAX_PACKET];
	bigtime_t deadline = system_time() + R5_BTVOICE_READ_TIMEOUT;
	while (true) {
		int size = 0;
		cpu_status state = slot_lock(*slot);
		bool running = slot->running;
		if (running && slot->buffers != NULL) {
			size = h2_sco_ring_pop(&slot->buffers->ring, packet,
				capacity < sizeof(packet) ? capacity : sizeof(packet));
		}
		slot_unlock(*slot, state);

		if (size < 0)
			return B_BUFFER_OVERFLOW;
		if (size > 0) {
			if (user_memcpy(buffer, packet, size) != B_OK)
				return B_BAD_ADDRESS;
			*_length = size;
			return B_OK;
		}

		status_t status = acquire_sem_etc(slot->readable, 1,
			B_ABSOLUTE_TIMEOUT | B_CAN_INTERRUPT, deadline);
		if (status == B_TIMED_OUT || status == B_WOULD_BLOCK)
			return B_OK;
		if (status != B_OK)
			return status;
	}
}


static status_t
voice_write(void* cookie, off_t position, const void* buffer,
	size_t* _length)
{
	voice_slot* slot = (voice_slot*)cookie;
	size_t length = *_length;
	*_length = 0;
	if (buffer == NULL || !IS_USER_ADDRESS(buffer))
		return B_BAD_ADDRESS;
	if (length < H2_SCO_HEADER_SIZE || length > H2_SCO_MAX_PACKET)
		return B_BAD_VALUE;

	uint8 packet[H2_SCO_MAX_PACKET];
	if (user_memcpy(packet, buffer, length) != B_OK)
		return B_BAD_ADDRESS;

	mutex_lock(&slot->lock);
	status_t status = B_OK;
	voice_tx_transfer* transfer = NULL;

	cpu_status state = slot_lock(*slot);
	if (!slot->running || slot->buffers == NULL || slot->outPipe == 0)
		status = B_DEV_NOT_READY;
	else if (!h2_sco_tx_check(packet, length, slot->handle))
		status = B_BAD_VALUE;
	else {
		for (int i = 0; i < R5_BTVOICE_TX_SLOTS; i++) {
			if (!slot->buffers->tx[i].busy) {
				transfer = &slot->buffers->tx[i];
				transfer->busy = true;
				break;
			}
		}
		if (transfer == NULL)
			status = B_WOULD_BLOCK;
	}
	slot_unlock(*slot, state);

	if (status == B_OK) {
		// One isochronous packet per USB frame, each at most the endpoint
		// size; outSize is at least 9 (checked at start), so the
		// descriptors always suffice.
		uint32 count = (length + slot->outSize - 1) / slot->outSize;
		size_t remaining = length;
		for (uint32 i = 0; i < count; i++) {
			size_t piece = remaining > slot->outSize ? slot->outSize
				: remaining;
			transfer->descriptors[i].request_length = piece;
			transfer->descriptors[i].actual_length = 0;
			transfer->descriptors[i].status = B_OK;
			remaining -= piece;
		}
		memcpy(transfer->data, packet, length);
		uint32 frame = 0;
		status = usb->queue_isochronous(slot->outPipe, transfer->data, length,
			transfer->descriptors, count, &frame, USB_ISO_ASAP,
			voice_tx_complete, transfer);
		if (status != B_OK) {
			state = slot_lock(*slot);
			transfer->busy = false;
			slot_unlock(*slot, state);
		} else
			*_length = length;
	}
	mutex_unlock(&slot->lock);
	return status;
}


static status_t
voice_control(void* cookie, uint32 op, void* buffer, size_t length)
{
	voice_slot* slot = (voice_slot*)cookie;
	if (op != R5_BTVOICE_GET_LINK)
		return B_DEV_INVALID_IOCTL;
	if (buffer == NULL || !IS_USER_ADDRESS(buffer)
		|| length < sizeof(r5_btvoice_link))
		return B_BAD_VALUE;

	r5_btvoice_link link;
	memset(&link, 0, sizeof(link));
	mutex_lock(&slot->lock);
	link.alt_setting = (uint8)slot->alt;
	cpu_status state = slot_lock(*slot);
	link.running = slot->running ? 1 : 0;
	link.handle = slot->handle;
	link.air_mode = slot->airMode;
	link.rx_length = slot->rxLength;
	link.tx_length = slot->txLength;
	link.rx_packets = slot->rxPackets;
	if (slot->buffers != NULL) {
		link.dropped = slot->buffers->ring.dropped
			+ slot->assembler.broken;
	}
	slot_unlock(*slot, state);
	mutex_unlock(&slot->lock);

	return user_memcpy(buffer, &link, sizeof(link));
}


static device_hooks sVoiceHooks = {
	voice_open,
	voice_close,
	voice_free,
	voice_control,
	voice_read,
	voice_write,
	NULL,
	NULL,
	NULL,
	NULL
};


device_hooks*
h2_voice_find_device(const char* name)
{
	if (!sEnabled || node_number(name) < 0)
		return NULL;
	return &sVoiceHooks;
}
