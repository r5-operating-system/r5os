/*
 * Copyright 2007 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 * Copyright 2008 Mika Lindqvist, monni1995_at_gmail.com
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "h2generic.h"

#include <kernel.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <KernelExport.h>
#include <ByteOrder.h>
#include <Drivers.h>
#include <StackOrHeapArray.h>

#include <btModules.h>

#include "bluetooth/HCI/btHCI_transport.h"
#include "h2cfg.h"
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include "h2command.h"
#include "h2voice.h"
// SPDX-SnippetEnd
#include "h2debug.h"
#include "h2mediatek.h"
#include "h2transactions.h"
#include "h2util.h"
#include "snet_buffer.h"


int32 api_version = B_CUR_DRIVER_API_VERSION;

// Modules
static const char* usb_name = B_USB_MODULE_NAME;
static const char* hci_name = BT_HCI_MODULE_NAME;
static const char* btDevices_name = BT_HCI_MODULE_NAME;


usb_module_info* usb = NULL;
bt_hci_module_info* hci = NULL; // TODO remove / clean
struct bt_hci_module_info* btDevices = NULL;
struct net_buffer_module_info* nb = NULL;
struct bluetooth_core_data_module_info* btCoreData = NULL;

// Driver Global data
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// A radio, its voice node (step 7c, only with bt_voice) and the final NULL.
#define MAX_PUBLISHED_NAMES (2 * MAX_BT_GENERIC_USB_DEVICES + 1)
static char* publish_names[MAX_PUBLISHED_NAMES];
// SPDX-SnippetEnd

int32 dev_count = 0; // number of connected devices
static bt_usb_dev* bt_usb_devices[MAX_BT_GENERIC_USB_DEVICES];
sem_id dev_table_sem = -1; // sem to synchronize access to device table

status_t submit_nbuffer(hci_id hid, net_buffer* nbuf);

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// HCI command timeouts (h2command.h). One tracker per device slot, indexed
// by bt_usb_dev::num. The watchdog thread never touches a bt_usb_dev: it
// posts the timeout by HCI id, which the HCI module drops for a radio that
// is gone.
static h2_command_tracker sCommands[MAX_BT_GENERIC_USB_DEVICES];
static hci_id sCommandRadio[MAX_BT_GENERIC_USB_DEVICES];
static spinlock sCommandLock = B_SPINLOCK_INITIALIZER;
static sem_id sCommandWake = -1;
static thread_id sCommandThread = -1;
static volatile bool sCommandQuit = false;

// An HCI command: opcode, parameter length, at most 255 bytes of
// parameters (Core 6.0, Vol 4, Part E, 5.4.1).
static const size_t kMaxCommandSize = 3 + 255;

// The event stream of each device slot (h2_event_stream). Only
// event_complete() uses it, and one event transfer is in flight per device.
// A transfer is max(HCI_MAX_EVENT_SIZE, the endpoint's packet size) bytes,
// and an interrupt packet is at most 1024 bytes (USB 2.0, 5.7.3).
static const size_t kMaxEventTransfer = 1024;
static h2_event_stream sEventStreams[MAX_BT_GENERIC_USB_DEVICES];
static uint8 sEventOut[MAX_BT_GENERIC_USB_DEVICES]
	[kMaxEventTransfer + H2_EVENT_HEAD_SIZE];


static cpu_status
command_lock()
{
	cpu_status state = disable_interrupts();
	acquire_spinlock(&sCommandLock);
	return state;
}


static void
command_unlock(cpu_status state)
{
	release_spinlock(&sCommandLock);
	restore_interrupts(state);
}


static void
command_forget(int32 num)
{
	if (num < 0 || num >= MAX_BT_GENERIC_USB_DEVICES)
		return;
	cpu_status state = command_lock();
	h2_command_reset(&sCommands[num]);
	sCommandRadio[num] = -1;
	command_unlock(state);
	h2_event_stream_reset(&sEventStreams[num]);
}


static status_t
command_watchdog(void*)
{
	while (!sCommandQuit) {
		bigtime_t next = -1;

		for (int32 num = 0; num < MAX_BT_GENERIC_USB_DEVICES; num++) {
			uint16 expired[H2_TRACKED_COMMANDS];
			int count = 0;

			cpu_status state = command_lock();
			hci_id radio = sCommandRadio[num];
			bigtime_t now = system_time();
			while (count < H2_TRACKED_COMMANDS
				&& h2_command_expired(&sCommands[num], now, &expired[count]))
				count++;
			bigtime_t deadline = h2_command_next_deadline(&sCommands[num]);
			command_unlock(state);

			if (deadline >= 0 && (next < 0 || deadline < next))
				next = deadline;

			for (int i = 0; i < count; i++) {
				ERROR("%s: command %#06x got no answer in %" B_PRIdBIGTIME
					" ms; reporting status %#04x\n", __func__, expired[i],
					kH2CommandTimeout / 1000, kH2TimeoutStatus);
				if (radio < 0 || btDevices == NULL)
					continue;
				uint8 event[H2_TIMEOUT_EVENT_SIZE];
				h2_command_timeout_event(expired[i], event);
				btDevices->PostTransportPacket(radio, BT_EVENT, event,
					sizeof(event));
			}
		}

		if (next < 0)
			acquire_sem(sCommandWake);
		else
			acquire_sem_etc(sCommandWake, 1, B_ABSOLUTE_TIMEOUT, next);
	}
	return B_OK;
}


// Judges one event from its first bytes: an answer frees its command's
// slot, and an answer that comes after its timeout was reported is dropped.
struct DropLateAnswer {
	int32	num;

	bool operator()(const uint8* head, size_t size) const
	{
		uint16 opcode;
		if (!h2_command_answer_head(head, size, &opcode))
			return false;

		cpu_status state = command_lock();
		bool drop = h2_command_answered(&sCommands[num], opcode);
		command_unlock(state);

		if (drop) {
			ERROR("h2_command_events: dropped the late answer for command "
				"%#06x\n", opcode);
		}
		return drop;
	}
};


// The answer check ran on each USB transfer and needed it to be one whole
// event, so a split or a shared transfer was not seen: the command then
// timed out although it was answered, or a late answer went up after its
// timeout (review of #4288). It now follows the event boundaries.
const uint8*
h2_command_events(bt_usb_dev* bdev, const uint8* data, size_t size,
	size_t* _outSize)
{
	if (bdev == NULL || bdev->num < 0 || bdev->num >= MAX_BT_GENERIC_USB_DEVICES
		|| data == NULL) {
		*_outSize = size;
		return data;
	}

	h2_event_stream* stream = &sEventStreams[bdev->num];
	if (size > kMaxEventTransfer) {
		// Cannot happen with the transfer size used; pass it on unjudged
		// and start over at the next transfer.
		h2_event_stream_reset(stream);
		*_outSize = size;
		return data;
	}

	DropLateAnswer drop = { bdev->num };
	*_outSize = h2_event_stream_filter(stream, data, size,
		sEventOut[bdev->num], drop);
	return sEventOut[bdev->num];
}
// SPDX-SnippetEnd

usb_support_descriptor supported_devices[] = {
	// Generic Bluetooth USB device
	// Class, SubClass, and Protocol codes that describe a Bluetooth device
	{ UDCLASS_WIRELESS, UDSUBCLASS_RF, UDPROTO_BLUETOOTH, 0, 0 },
	// Intel combo cards (Linux btusb BTUSB_INTEL_COMBINED). They report
	// the Bluetooth class, but are listed explicitly like in btusb. Their
	// firmware is loaded by bt_firmware before bluetooth_server runs.
	{ 0, 0, 0, 0x8087, 0x07dc },	// Wireless 7260
	{ 0, 0, 0, 0x8087, 0x0a2a },	// Wireless 7265 / 3160 / 3165
	{ 0, 0, 0, 0x8087, 0x0aa7 },	// Wireless-AC 3168
	{ 0, 0, 0, 0x8087, 0x0a2b },	// Wireless 8260 / 8265
	{ 0, 0, 0, 0x8087, 0x0aaa },	// Wireless-AC 9460 / 9560
	{ 0, 0, 0, 0x8087, 0x0025 },	// Wireless-AC 9260
	{ 0, 0, 0, 0x8087, 0x0026 },	// Wi-Fi 6 AX201
	{ 0, 0, 0, 0x8087, 0x0029 },	// Wi-Fi 6 AX200
	{ 0, 0, 0, 0x8087, 0x0032 },	// Wi-Fi 6E AX210
	{ 0, 0, 0, 0x8087, 0x0033 },	// Wi-Fi 6E AX211 / AX411
	{ 0, 0, 0, 0x8087, 0x0035 },	// Wi-Fi 7 BE2xx
	{ 0, 0, 0, 0x8087, 0x0036 },
	{ 0, 0, 0, 0x8087, 0x0037 },
	{ 0, 0, 0, 0x8087, 0x0038 },
	{ 0, 0, 0, 0x8087, 0x0039 },

	// Broadcom BCM2035
	{ 0, 0, 0, 0x0a5c, 0x200a },
	{ 0, 0, 0, 0x0a5c, 0x2009 },

	// Devices taken from the linux Driver
	// MediaTek MT76x0E
	{ 0, 0, 0, 0x0e8d, 0x763f },
	// Broadcom SoftSailing reporting vendor specific
	{ 0, 0, 0, 0x0a5c, 0x21e1 },

	// Apple MacBookPro 7,1
	{ 0, 0, 0, 0x05ac, 0x8213 },
	// Apple iMac11,1
	{ 0, 0, 0, 0x05ac, 0x8215 },
	// Apple MacBookPro6,2
	{ 0, 0, 0, 0x05ac, 0x8218 },
	// Apple MacBookAir3,1, MacBookAir3,2
	{ 0, 0, 0, 0x05ac, 0x821b },
	// Apple MacBookAir4,1
	{ 0, 0, 0, 0x05ac, 0x821f },
	// Apple MacBookPro8,2
	{ 0, 0, 0, 0x05ac, 0x821a },
	// Apple MacMini5,1
	{ 0, 0, 0, 0x05ac, 0x8281 },

	// AVM BlueFRITZ! USB v2.0
	{ 0, 0, 0, 0x057c, 0x3800 },
	// Bluetooth Ultraport Module from IBM
	{ 0, 0, 0, 0x04bf, 0x030a },
	// ALPS Modules with non-standard id
	{ 0, 0, 0, 0x044e, 0x3001 },
	{ 0, 0, 0, 0x044e, 0x3002 },
	// Ericsson with non-standard id
	{ 0, 0, 0, 0x0bdb, 0x1002 },

	// Canyon CN-BTU1 with HID interfaces
	{ 0, 0, 0, 0x0c10, 0x0000 },

	// Broadcom BCM20702A0
	{ 0, 0, 0, 0x413c, 0x8197 },

};

/* add a device to the list of connected devices */
static bt_usb_dev*
spawn_device(usb_device usb_dev)
{
	CALLED();

	int32 i;
	status_t err = B_OK;
	bt_usb_dev* new_bt_dev = NULL;

	// 16 usb dongles...
	if (dev_count >= MAX_BT_GENERIC_USB_DEVICES) {
		ERROR("%s: Device table full\n", __func__);
		goto exit;
	}

	// try the allocation
	new_bt_dev = (bt_usb_dev*)malloc(sizeof(bt_usb_dev));
	if (new_bt_dev == NULL) {
		ERROR("%s: Unable to malloc new bt device\n", __func__);
		goto exit;
	}
	memset(new_bt_dev, 0, sizeof(bt_usb_dev));

	// We will need this sem for some flow control
	new_bt_dev->cmd_complete = create_sem(1,
		BLUETOOTH_DEVICE_DEVFS_NAME "cmd_complete");
	if (new_bt_dev->cmd_complete < 0) {
		err = new_bt_dev->cmd_complete;
		ERROR("%s: Unable to create bt device command semaphore: %" B_PRId32 "\n", __func__, err);
		goto bail0;
	}

	// and this for something else
	new_bt_dev->lock = create_sem(1, BLUETOOTH_DEVICE_DEVFS_NAME "lock");
	if (new_bt_dev->lock < 0) {
		err = new_bt_dev->lock;
		ERROR("%s: Unable to create bt device lock semaphore: %" B_PRId32 "\n", __func__, err);
		goto bail1;
	}

	// find a free slot and fill out the name
	acquire_sem(dev_table_sem);
	for (i = 0; i < MAX_BT_GENERIC_USB_DEVICES; i++) {
		if (bt_usb_devices[i] == NULL) {
			bt_usb_devices[i] = new_bt_dev;
			sprintf(new_bt_dev->name, "%s/%" B_PRId32,
				BLUETOOTH_DEVICE_PATH, i);
			new_bt_dev->num = i;
			TRACE("%s: added device %p %" B_PRId32 " %s\n", __func__,
				bt_usb_devices[i], new_bt_dev->num, new_bt_dev->name);
			break;
		}
	}
	release_sem_etc(dev_table_sem, 1, B_DO_NOT_RESCHEDULE);

	// In the case we cannot find a free slot
	if (i >= MAX_BT_GENERIC_USB_DEVICES) {
		ERROR("%s: Device could not be added\n", __func__);
		goto bail2;
	}

	new_bt_dev->dev = usb_dev;
	// TODO: currently only server opens
	new_bt_dev->open_count = 0;

	dev_count++;
	return new_bt_dev;

bail2:
	delete_sem(new_bt_dev->lock);
bail1:
	delete_sem(new_bt_dev->cmd_complete);
bail0:
	free(new_bt_dev);
	new_bt_dev = NULL;
exit:
	return new_bt_dev;
}


// remove a device from the list of connected devices
static void
kill_device(bt_usb_dev* bdev)
{
	if (bdev != NULL) {
		TRACE("%s: (%p)\n", __func__, bdev);

		delete_sem(bdev->lock);
		delete_sem(bdev->cmd_complete);

		// mark it free
		bt_usb_devices[bdev->num] = NULL;

		free(bdev);
		dev_count--;
	}
}


bt_usb_dev*
fetch_device(bt_usb_dev* dev, hci_id hid)
{
	int i;

//	TRACE("%s: (%p) or %d\n", __func__, dev, hid);

	acquire_sem(dev_table_sem);
	if (dev != NULL) {
		for (i = 0; i < MAX_BT_GENERIC_USB_DEVICES; i++) {
			if (bt_usb_devices[i] == dev) {
				release_sem_etc(dev_table_sem, 1, B_DO_NOT_RESCHEDULE);
				return bt_usb_devices[i];
			}
		}
	} else {
		for (i = 0; i < MAX_BT_GENERIC_USB_DEVICES; i++) {
			if (bt_usb_devices[i] != NULL && bt_usb_devices[i]->hdev == hid) {
				release_sem_etc(dev_table_sem, 1, B_DO_NOT_RESCHEDULE);
				return bt_usb_devices[i];
			}
		}
	}

	release_sem_etc(dev_table_sem, 1, B_DO_NOT_RESCHEDULE);

	return NULL;
}


#if 0
#pragma mark -
#endif

// called by USB Manager when device is added to the USB
static status_t
device_added(usb_device dev, void** cookie)
{
	const usb_interface_info* 		interface;
	const usb_device_descriptor* 	desc;
	const usb_configuration_info*	config;
	const usb_interface_info*		uif;
	const usb_endpoint_info*		ep;

	status_t 	err = B_ERROR;
	bt_usb_dev* new_bt_dev = spawn_device(dev);
	int e;

	TRACE("%s: device_added(%p)\n", __func__, new_bt_dev);

	if (new_bt_dev == NULL) {
		ERROR("%s: Couldn't allocate device record.\n", __func__);
		err = ENOMEM;
		goto bail_no_mem;
	}

	// we only have 1 configuration number 0
	config = usb->get_nth_configuration(dev, 0);
	// dump_usb_configuration_info(config);
	if (config == NULL) {
		ERROR("%s: Couldn't get default USB config.\n", __func__);
		err = B_ERROR;
		goto bail;
	}

	TRACE("%s: found %" B_PRIuSIZE " alt interfaces.\n", __func__,
		config->interface->alt_count);

	// set first interface
	interface = &config->interface->alt[0];
	err = usb->set_alt_interface(new_bt_dev->dev, interface);

	if (err != B_OK) {
		ERROR("%s: set_alt_interface() error.\n", __func__);
		goto bail;
	}

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// With bt_voice (h2voice.h) the isochronous interface stays at setting
	// 0, which takes no bandwidth, until a voice link needs another one.
	if (h2_voice_enabled()) {
		if (config->interface_count > 1 && config->interface[1].alt_count > 0
			&& usb->set_alt_interface(new_bt_dev->dev,
				&config->interface[1].alt[0]) != B_OK) {
			ERROR("%s: set_alt_interface() error on interface 1, alt 0.\n",
				__func__);
		}
		new_bt_dev->driver_info |= BT_SCO_NOT_WORKING;
	} else
	// SPDX-SnippetEnd
	// TODO: Do this only when sco is needed
	if (config->interface_count > 1 && config->interface[1].alt_count > 5) {
		interface = &config->interface[1].alt[5];
		err = usb->set_alt_interface(new_bt_dev->dev, interface);

		if (err != B_OK) {
			ERROR("%s: set_alt_interface() error on interface 1, alt 2.\n", __func__);
			new_bt_dev->driver_info |= BT_SCO_NOT_WORKING;
		}
	} else {
		new_bt_dev->driver_info |= BT_SCO_NOT_WORKING;
	}

	// call set_configuration() only after calling set_alt_interface()
	err = usb->set_configuration(dev, config);
	if (err != B_OK) {
		ERROR("%s: set_configuration() error.\n", __func__);
		goto bail;
	}

	// Place to find out whats our concrete device and set up some special
	// info to our driver. If this code increases too much reconsider
	// this implementation
	desc = usb->get_device_descriptor(dev);
	if (desc->vendor_id == 0x0a5c
		&& (desc->product_id == 0x200a
			|| desc->product_id == 0x2009
			|| desc->product_id == 0x2035)) {

		new_bt_dev->driver_info = BT_WILL_NEED_A_RESET | BT_SCO_NOT_WORKING;

	}
	/*
	else if ( desc->vendor_id == YOUR_VENDOR_HERE
		&& desc->product_id == YOUR_PRODUCT_HERE ) {
		YOUR_SPECIAL_FLAGS_HERE
	}
	*/

	if (new_bt_dev->driver_info & BT_IGNORE_THIS_DEVICE) {
		err = ENODEV;
		goto bail;
	}

	// security check
	if (config->interface->active->descr->interface_number > 0) {
		ERROR("%s: Strange condition happened %d\n", __func__,
			config->interface->active->descr->interface_number);
		err = B_ERROR;
		goto bail;
	}

	TRACE("%s: Found %" B_PRIuSIZE " interfaces. Expected 3\n", __func__,
		config->interface_count);

	// Find endpoints that we need
	for (size_t i = 0; i < config->interface_count; i++) {
		uif = config->interface[i].active;

		// Events and ACL data use the first interface only. MediaTek radios
		// have a third interface whose bulk and interrupt endpoints look
		// just like the HCI ones (from the x399-workstation branch).
		bool primary = uif->descr->interface_number == 0;

		for (e = 0; e < uif->descr->num_endpoints; e++) {

			ep = &uif->endpoint[e];
			switch (ep->descr->attributes & USB_ENDPOINT_ATTR_MASK) {
				case USB_ENDPOINT_ATTR_INTERRUPT:
					if (!primary)
						break;
					if (ep->descr->endpoint_address & USB_ENDPOINT_ADDR_DIR_IN) {
						new_bt_dev->intr_in_ep = ep;
						new_bt_dev->max_packet_size_intr_in = ep->descr->max_packet_size;
						TRACE("%s: INT in\n", __func__);
					} else {
						TRACE("%s: INT out\n", __func__);
					}
					break;

				case USB_ENDPOINT_ATTR_BULK:
					if (!primary)
						break;
					if (ep->descr->endpoint_address & USB_ENDPOINT_ADDR_DIR_IN) {
						new_bt_dev->bulk_in_ep = ep;
						new_bt_dev->max_packet_size_bulk_in = ep->descr->max_packet_size;
						TRACE("%s: BULK in\n", __func__);
					} else {
						new_bt_dev->bulk_out_ep = ep;
						new_bt_dev->max_packet_size_bulk_out = ep->descr->max_packet_size;
						TRACE("%s: BULK out\n", __func__);
					}
					break;
				case USB_ENDPOINT_ATTR_ISOCHRONOUS:
					if (ep->descr->endpoint_address & USB_ENDPOINT_ADDR_DIR_IN) {
						new_bt_dev->iso_in_ep = ep;
						new_bt_dev->max_packet_size_iso_in = ep->descr->max_packet_size;
						TRACE("%s: ISO in found\n", __func__);
					} else {
						new_bt_dev->iso_out_ep = ep;
						new_bt_dev->max_packet_size_iso_out = ep->descr->max_packet_size;
						TRACE("%s: ISO out found\n", __func__);
					}
					break;
			}
		}
	}

	if (!new_bt_dev->bulk_in_ep || !new_bt_dev->bulk_out_ep
		|| !new_bt_dev->intr_in_ep) {
		ERROR("%s: Minimal # endpoints for BT not found\n", __func__);
		goto bail;
	}


	if (!new_bt_dev->iso_in_ep || !new_bt_dev->iso_out_ep) {
		ERROR("%s: Endpoints for BT SCO not found\n", __func__);
		new_bt_dev->driver_info |= BT_SCO_NOT_WORKING;
	}
	// Look into the devices suported to understand this
	if (new_bt_dev->driver_info & BT_DIGIANSWER)
		new_bt_dev->ctrl_req = USB_TYPE_VENDOR;
	else
		new_bt_dev->ctrl_req = USB_TYPE_CLASS;

	new_bt_dev->connected = true;

	// set the cookie that will be passed to other USB
	// hook functions (currently device_removed() is the only other)
	*cookie = new_bt_dev;
	TRACE("%s: Ok %p\n", __func__, new_bt_dev);
	return B_OK;

bail:
	kill_device(new_bt_dev);
bail_no_mem:
	*cookie = NULL;

	return err;
}


// Called by USB Manager when device is removed from the USB
static status_t
device_removed(void* cookie)
{
	bt_usb_dev* bdev = fetch_device((bt_usb_dev*)cookie, 0);

	TRACE("%s: device_removed(%p)\n", __func__, bdev);

	if (bdev == NULL) {
		ERROR("%s: Device not present in driver.\n", __func__);
		return B_ERROR;
	}

	if (!TEST_AND_CLEAR(&bdev->state, RUNNING))
		ERROR("%s: wasnt running?\n", __func__);

	TRACE("%s: Cancelling queues...\n", __func__);
	if (bdev->intr_in_ep != NULL)
		usb->cancel_queued_transfers(bdev->intr_in_ep->handle);
	if (bdev->bulk_in_ep != NULL)
		usb->cancel_queued_transfers(bdev->bulk_in_ep->handle);
	if (bdev->bulk_out_ep != NULL)
		usb->cancel_queued_transfers(bdev->bulk_out_ep->handle);
	if (bdev->iso_in_ep != NULL)
		usb->cancel_queued_transfers(bdev->iso_in_ep->handle);
	if (bdev->iso_out_ep != NULL)
		usb->cancel_queued_transfers(bdev->iso_out_ep->handle);

	bdev->connected = false;

	return B_OK;
}


static bt_hci_transport_hooks bluetooth_hooks = {
	&submit_nbuffer, 
	&submit_nbuffer, 
	&submit_nbuffer, 
	NULL, 
	NULL, 
	H2
};


static usb_notify_hooks notify_hooks = {
	&device_added,
	&device_removed
};

#if 0
#pragma mark -
#endif

status_t
submit_nbuffer(hci_id hid, net_buffer* nbuf)
{
	bt_usb_dev* bdev = NULL;

	bdev = fetch_device(NULL, hid);

	TRACE("%s: index=%" B_PRId32 " nbuf=%p bdev=%p\n", __func__, hid,
		nbuf, bdev);

	if (bdev != NULL) {
		switch (nbuf->protocol) {
			case BT_SCO:
				if (bdev->driver_info & BT_SCO_NOT_WORKING)
					return B_NOT_SUPPORTED;
				return submit_tx_sco(bdev, nbuf);
				break;
			case BT_COMMAND:
			{
				snet_buffer* snbuf = snb_fetch(&bdev->snetBufferRecycleTrash, nbuf->size);
				if (snbuf == NULL)
					return B_NO_MEMORY;

				BStackOrHeapArray<uint8, 512> data(nbuf->size);
				if (!data.IsValid()) {
					snb_park(&bdev->snetBufferRecycleTrash, snbuf);
					return B_NO_MEMORY;
				}

				nb->read(nbuf, 0, data, nbuf->size);
				snb_put(snbuf, data, nbuf->size);

				return submit_tx_command(bdev, snbuf);
				break;
			}

			case BT_ACL:
				return submit_tx_acl(bdev, nbuf);
			break;

			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			// A packet type this driver does not send is an error for the
			// caller, not a reason to stop the machine.
			default:
				ERROR("%s: no protocol %" B_PRIu32 "\n", __func__,
					(uint32)nbuf->protocol);
				return B_BAD_VALUE;
			// SPDX-SnippetEnd

		}
	}

	return B_ERROR;

}


// implements the POSIX open()
static status_t
device_open(const char* name, uint32 flags, void **cookie)
{
	CALLED();

	status_t err = ENODEV;
	bt_usb_dev* bdev = NULL;
	hci_id hdev;
	int i;

	acquire_sem(dev_table_sem);
	for (i = 0; i < MAX_BT_GENERIC_USB_DEVICES; i++) {
		if (bt_usb_devices[i] && !strcmp(name, bt_usb_devices[i]->name)) {
			bdev = bt_usb_devices[i];
			break;
		}
	}
	release_sem_etc(dev_table_sem, 1, B_DO_NOT_RESCHEDULE);

	if (bdev == NULL) {
		ERROR("%s: Device not found in the open list!", __func__);
		*cookie = NULL;
		return B_ERROR;
	}

	// Set RUNNING
	if (TEST_AND_SET(&bdev->state, RUNNING)) {
		ERROR("%s: dev already running! - reOpened device!\n", __func__);
		return B_ERROR;
	}

	// MediaTek radios answer no HCI command until they have their firmware.
	// This runs at open rather than attach because it reads the firmware
	// from disk. Other radios return at once.
	status_t setupStatus = mediatek_setup(bdev);
	if (setupStatus != B_OK) {
		ERROR("%s: the radio could not be made ready: %s\n", __func__,
			strerror(setupStatus));
		TEST_AND_CLEAR(&bdev->state, RUNNING);
		return setupStatus;
	}

	acquire_sem(bdev->lock);
	// TX structures
	for (i = 0; i < BT_DRIVER_TXCOVERAGE; i++) {
		list_init(&bdev->nbuffersTx[i]);
		bdev->nbuffersPendingTx[i] = 0;
	}

	// RX structures
	bdev->eventRx = NULL;
	for (i = 0; i < BT_DRIVER_RXCOVERAGE; i++) {
		bdev->nbufferRx[i] = NULL;
	}

	// dumping the USB frames
	init_room(&bdev->eventRoom);
	init_room(&bdev->aclRoom);
	init_room(&bdev->scoRoom);

	list_init(&bdev->snetBufferRecycleTrash);

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	command_forget(bdev->num);
	// SPDX-SnippetEnd

	// Allocate set and register the HCI device
	if (btDevices != NULL) {
		bluetooth_device* ndev;
		// TODO: Fill the transport descriptor
		err = btDevices->RegisterDriver(&bluetooth_hooks, &ndev);

		if (err == B_OK) {
			bdev->hdev = hdev = ndev->index; // Get the index
			bdev->ndev = ndev;  // Get the net_device

		} else {
			hdev = bdev->num; // XXX: Lets try to go on
		}
	} else {
		hdev = bdev->num; // XXX: Lets try to go on
	}

	bdev->hdev = hdev;

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	h2_voice_device_open(bdev);
	// SPDX-SnippetEnd

	*cookie = bdev;
	release_sem(bdev->lock);

	return B_OK;

}


/* called when a client calls POSIX close() on the driver, but I/O
 * requests may still be pending
 */
static status_t
device_close(void* cookie)
{
	CALLED();

	int32 i;
	void* item;
	bt_usb_dev* bdev = (bt_usb_dev*)cookie;

	if (bdev == NULL)
		panic("bad cookie");
	// Completion callbacks must stop submitting receives before cancellation.
	const bool wasRunning = TEST_AND_CLEAR(&bdev->state, RUNNING);

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// No timeout is reported for a radio that is being closed.
	command_forget(bdev->num);
	// The voice transfers stop before the queues below are cancelled.
	h2_voice_device_close(bdev);
	// SPDX-SnippetEnd

	// Clean queues

	if (bdev->connected == true) {
		TRACE("%s: Cancelling queues...\n", __func__);

		if (bdev->intr_in_ep != NULL)
			usb->cancel_queued_transfers(bdev->intr_in_ep->handle);

		if (bdev->bulk_in_ep!=NULL)
			usb->cancel_queued_transfers(bdev->bulk_in_ep->handle);

		if (bdev->bulk_out_ep!=NULL)
			usb->cancel_queued_transfers(bdev->bulk_out_ep->handle);

		if (bdev->iso_in_ep != NULL)
			usb->cancel_queued_transfers(bdev->iso_in_ep->handle);

		if (bdev->iso_out_ep != NULL)
			usb->cancel_queued_transfers(bdev->iso_out_ep->handle);
	}

	// TX
	for (i = 0; i < BT_DRIVER_TXCOVERAGE; i++) {
		if (i == BT_COMMAND) {
			while ((item = list_remove_head_item(&bdev->nbuffersTx[i])) != NULL)
				snb_free((snet_buffer*)item);
		} else {
			while ((item = list_remove_head_item(&bdev->nbuffersTx[i])) != NULL)
				nb_destroy((net_buffer*)item);
		}
	}
	// RX
	for (i = 0; i < BT_DRIVER_RXCOVERAGE; i++) {
		nb_destroy(bdev->nbufferRx[i]);
	}
	snb_free(bdev->eventRx);

	purge_room(&bdev->eventRoom);
	purge_room(&bdev->aclRoom);
	purge_room(&bdev->scoRoom);

	// Device no longer in our Stack
	if (btDevices != NULL)
		btDevices->UnregisterDriver(bdev->hdev);

	// unSet RUNNING
	if (!wasRunning) {
		ERROR("%s: %s not running?\n", __func__, bdev->name);
		return B_ERROR;
	}

	return B_OK;
}


// Called after device_close(), when all pending I / O requests have returned
static status_t
device_free(void* cookie)
{
	CALLED();

	status_t err = B_OK;
	bt_usb_dev* bdev = (bt_usb_dev*)cookie;

	if (!bdev->connected)
		kill_device(bdev);

	return err;
}


// implements the POSIX ioctl()
static status_t
device_control(void* cookie, uint32 msg, void* params, size_t size)
{
	status_t 	err = B_ERROR;
	bt_usb_dev*	bdev = (bt_usb_dev*)cookie;
	snet_buffer* snbuf;
	#if BT_DRIVER_SUPPORTS_ACL // ACL
	int32	i;
	#endif

	TOUCH(size);
	TRACE("%s: ioctl() opcode %" B_PRId32 " size %" B_PRIuSIZE ".\n", __func__,
		msg, size);

	if (bdev == NULL) {
		TRACE("%s: Bad cookie\n", __func__);
		return B_BAD_VALUE;
	}

	if (params == NULL || !IS_USER_ADDRESS(params)) {
		TRACE("%s: Invalid pointer control\n", __func__);
		return B_BAD_VALUE;
	}

	acquire_sem(bdev->lock);

	switch (msg) {
		case ISSUE_BT_COMMAND: {
			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			// The size came from the caller and went straight to alloca(),
			// a failed copy returned with bdev->lock still held, and a failed
			// buffer fetch was not checked.
			uint16 opcode;
			uint8 command[kMaxCommandSize];
			if (size < 3 || size > kMaxCommandSize) {
				TRACE("%s: Invalid size control\n", __func__);
				err = B_BAD_VALUE;
				break;
			}

			if (user_memcpy(command, params, size) != B_OK) {
				err = B_BAD_ADDRESS;
				break;
			}
			h2_command_opcode(command, size, &opcode);

			// TODO: Reuse from some TXcompleted queue
			// snbuf = snb_create(size);
			snbuf = snb_fetch(&bdev->snetBufferRecycleTrash, size);
			if (snbuf == NULL) {
				err = B_NO_MEMORY;
				break;
			}
			snb_put(snbuf, command, size);

			// Noted before it is sent: the answer can come before
			// submit_tx_command() returns.
			uint16 dropped;
			cpu_status state = command_lock();
			int slot = h2_command_sent(&sCommands[bdev->num], opcode,
				system_time(), &dropped);
			sCommandRadio[bdev->num] = bdev->hdev;
			command_unlock(state);

			// A full tracker gave up on its oldest command; report its
			// timeout now, or its sender waits forever (review of #4288).
			if (dropped != 0 && btDevices != NULL) {
				ERROR("%s: too many commands waiting; reporting %#06x as "
					"timed out\n", __func__, dropped);
				uint8 event[H2_TIMEOUT_EVENT_SIZE];
				h2_command_timeout_event(dropped, event);
				btDevices->PostTransportPacket(bdev->hdev, BT_EVENT, event,
					sizeof(event));
			}

			err = submit_tx_command(bdev, snbuf);
			if (err != B_OK) {
				ERROR("%s: Queing failed at submit_tx_command()\n", __func__);
				snb_park(&bdev->snetBufferRecycleTrash, snbuf);
				// Only this command's slot: freeing by opcode took the
				// oldest one of that opcode, which may still be in flight.
				state = command_lock();
				h2_command_unsent(&sCommands[bdev->num], slot, opcode);
				command_unlock(state);
			} else {
				TRACE("%s: command launched\n", __func__);
				if (sCommandWake >= 0)
					release_sem_etc(sCommandWake, 1, B_DO_NOT_RESCHEDULE);
			}
			// SPDX-SnippetEnd
			break;
		}

		case BT_UP:
			//  EVENTS
			err = submit_rx_event(bdev);
			if (err != B_OK) {
				bdev->state &= ~ANCILLYANT;
				ERROR("%s: Queuing failed at submit_rx_event()\n", __func__);
				break;
			}

			#if BT_DRIVER_SUPPORTS_ACL // ACL
			for (i = 0; i < MAX_ACL_IN_WINDOW; i++) {
				err = submit_rx_acl(bdev);
				if (err != B_OK && i == 0) {
					bdev->state &= ~ANCILLYANT;
						// Set the flaq in the HCI world
					ERROR("%s: Queuing failed at submit_rx_acl()\n",
						__func__);
					break;
				}
			}
			#endif

			// SCO carries voice, and nothing here asks for voice until a
			// connection wants it. Listening for it anyway means standing
			// isochronous transfers on an endpoint whose alternate setting
			// reserves bandwidth every frame, which the bus honours whether
			// or not anything is said - and on the MediaTek MT7922 that is
			// enough to keep the events we do need from getting through. Wait
			// until a SCO connection exists, as the note here has long asked.

			bdev->state |= RUNNING;

			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			// The pass line of docs/BLUETOOTH.md, step 1a.
			ERROR("controller ready (%s)\n", bdev->name);
			// SPDX-SnippetEnd
		break;

		case GET_STATS:
			err = user_memcpy(params, &bdev->stat, sizeof(bt_hci_statistics));
		break;

		case GET_HCI_ID:
			err = user_memcpy(params, &bdev->hdev, sizeof(hci_id));
		break;


	default:
		ERROR("%s: Invalid opcode.\n", __func__);
		err = B_DEV_INVALID_IOCTL;
		break;
	}

	release_sem(bdev->lock);
	return err;
}


// implements the POSIX read()
static status_t
device_read(void* cookie, off_t pos, void* buffer, size_t* count)
{
	TRACE("%s: Reading... count = %" B_PRIuSIZE "\n", __func__, *count);

	*count = 0;
	return B_OK;
}


// implements the POSIX write()
static status_t
device_write(void* cookie, off_t pos, const void* buffer, size_t* count)
{
	CALLED();

	return B_ERROR;
}


#if 0
#pragma mark -
#endif


static int
dump_driver(int argc, char** argv)
{
	int i;
	snet_buffer* item = NULL;

	for (i = 0; i < MAX_BT_GENERIC_USB_DEVICES; i++) {

		if (bt_usb_devices[i] != NULL) {
			kprintf("%s : \n", bt_usb_devices[i]->name);
			kprintf("\teventroom = %d\taclroom = %d\t\tscoroom = %dcommand & events =%d\n",
				snb_packets(&bt_usb_devices[i]->eventRoom),
				snb_packets(&bt_usb_devices[i]->aclRoom), snb_packets(&bt_usb_devices[i]->scoRoom),
				snb_packets(&bt_usb_devices[i]->snetBufferRecycleTrash));

			while ((item = (snet_buffer*)list_get_next_item(
				&bt_usb_devices[i]->snetBufferRecycleTrash, item)) != NULL)
				snb_dump(item);
		}
	}

	return 0;
}


// called each time the driver is loaded by the kernel
status_t
init_driver(void)
{
	CALLED();
	int j;

	if (get_module(BT_CORE_DATA_MODULE_NAME,
		(module_info**)&btCoreData) != B_OK) {
		ERROR("%s: cannot get module '%s'\n", __func__,
			BT_CORE_DATA_MODULE_NAME);
		return B_ERROR;
	}

	// BT devices MODULE INITS
	if (get_module(btDevices_name, (module_info**)&btDevices) != B_OK) {
		ERROR("%s: cannot get module '%s'\n", __func__, btDevices_name);
		goto err_release3;
	}

	// HCI MODULE INITS
	if (get_module(hci_name, (module_info**)&hci) != B_OK) {
		ERROR("%s: cannot get module '%s'\n", __func__, hci_name);
#ifndef BT_SURVIVE_WITHOUT_HCI
		goto err_release2;
#endif
	}

	// USB MODULE INITS
	if (get_module(usb_name, (module_info**)&usb) != B_OK) {
		ERROR("%s: cannot get module '%s'\n", __func__, usb_name);
		goto err_release1;
	}

	if (get_module(NET_BUFFER_MODULE_NAME, (module_info**)&nb) != B_OK) {
		ERROR("%s: cannot get module '%s'\n", __func__,
			NET_BUFFER_MODULE_NAME);
#ifndef BT_SURVIVE_WITHOUT_NET_BUFFERS
		goto err_release;
#endif
	}

	// GENERAL INITS
	dev_table_sem = create_sem(1, BLUETOOTH_DEVICE_DEVFS_NAME "dev_table_lock");
	if (dev_table_sem < 0) {
		goto err;
	}

	for (j = 0; j < MAX_BT_GENERIC_USB_DEVICES; j++) {
		bt_usb_devices[j] = NULL;
	}

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	for (j = 0; j < MAX_BT_GENERIC_USB_DEVICES; j++)
		command_forget(j);
	sCommandQuit = false;
	sCommandWake = create_sem(0, "h2generic command timeouts");
	if (sCommandWake >= 0) {
		sCommandThread = spawn_kernel_thread(command_watchdog,
			"h2generic command timeouts", B_NORMAL_PRIORITY, NULL);
		if (sCommandThread >= 0)
			resume_thread(sCommandThread);
	}
	if (sCommandWake < 0 || sCommandThread < 0)
		ERROR("%s: no HCI command timeouts\n", __func__);
	// Voice (step 7c) is off unless the driver settings say bt_voice true.
	h2_voice_init();
	// SPDX-SnippetEnd

	// Note: After here device_added and publish devices hooks are called
	usb->register_driver(BLUETOOTH_DEVICE_DEVFS_NAME, supported_devices,
		B_COUNT_OF(supported_devices), NULL);
	usb->install_notify(BLUETOOTH_DEVICE_DEVFS_NAME, &notify_hooks);

	add_debugger_command("bth2generic", &dump_driver,
		"Lists H2 Transport device info");

	return B_OK;

err:	// Releasing
	put_module(NET_BUFFER_MODULE_NAME);
err_release:
	put_module(usb_name);
err_release1:
	put_module(hci_name);
#ifndef BT_SURVIVE_WITHOUT_HCI
err_release2:
#endif
	put_module(btDevices_name);
err_release3:
	put_module(BT_CORE_DATA_MODULE_NAME);

	return B_ERROR;
}


// called just before the kernel unloads the driver
void
uninit_driver(void)
{
	CALLED();

	int32 j;

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	for (j = 0; j < MAX_PUBLISHED_NAMES; j++) {
		free(publish_names[j]);
		publish_names[j] = NULL;
	}
	// SPDX-SnippetEnd

	for (j = 0; j < MAX_BT_GENERIC_USB_DEVICES; j++) {

		if (bt_usb_devices[j] != NULL) {
			//	if (connected_dev != NULL) {
			//		debugf("Device %p still exists.\n",	connected_dev);
			//	}
			ERROR("%s: %s still present?\n", __func__, bt_usb_devices[j]->name);
			kill_device(bt_usb_devices[j]);
		}
	}

	usb->uninstall_notify(BLUETOOTH_DEVICE_DEVFS_NAME);

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	h2_voice_uninit();
	sCommandQuit = true;
	if (sCommandThread >= 0) {
		release_sem(sCommandWake);
		status_t result;
		wait_for_thread(sCommandThread, &result);
		sCommandThread = -1;
	}
	if (sCommandWake >= 0) {
		delete_sem(sCommandWake);
		sCommandWake = -1;
	}
	// SPDX-SnippetEnd

	remove_debugger_command("bth2generic", &dump_driver);

	// Releasing modules
	put_module(usb_name);
	put_module(hci_name);
	// TODO: netbuffers

	delete_sem(dev_table_sem);
}


const char**
publish_devices(void)
{
	CALLED();
	int32 j;
	int32 i = 0;

	char* str;

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	for (j = 0; j < MAX_PUBLISHED_NAMES; j++) {
		free(publish_names[j]);
		publish_names[j] = NULL;
	}
	// SPDX-SnippetEnd

	acquire_sem(dev_table_sem);
	for (j = 0; j < MAX_BT_GENERIC_USB_DEVICES; j++) {
		if (bt_usb_devices[j] != NULL && bt_usb_devices[j]->connected) {
			str = strdup(bt_usb_devices[j]->name);
			if (str) {
				publish_names[i++] = str;
				TRACE("%s: publishing %s\n", __func__, bt_usb_devices[j]->name);
			}
			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			// NULL while voice is off: nothing more is published then.
			str = h2_voice_node_name(j);
			if (str != NULL)
				publish_names[i++] = str;
			// SPDX-SnippetEnd
		}
	}
	release_sem_etc(dev_table_sem, 1, B_DO_NOT_RESCHEDULE);

	publish_names[i] = NULL;
	TRACE("%s: published %" B_PRId32 " devices\n", __func__, i);

	// TODO: this method might make better memory use
	// dev_names = (char**)malloc(sizeof(char*) * (dev_count + 1));
	// if (dev_names) {
	// for (i = 0; i < MAX_NUM_DEVS; i++) {
	//	if ((dev != NULL) // dev + \n
	//	&& (dev_names[i] = (char*)malloc(strlen(DEVICE_PATH) + 2))) {
	//	sprintf(dev_names[i], "%s%ld", DEVICE_PATH, dev->num);
	//	debugf("publishing \"%s\"\n", dev_names[i]);
	//	}
	// }

	return (const char**)publish_names;
}


static device_hooks hooks = {
	device_open,
	device_close,
	device_free,
	device_control,
	device_read,
	device_write,
	NULL,
	NULL,
	NULL,
	NULL
};


device_hooks*
find_device(const char* name)
{
	CALLED();

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	device_hooks* voiceHooks = h2_voice_find_device(name);
	if (voiceHooks != NULL)
		return voiceHooks;
	// SPDX-SnippetEnd

	return &hooks;
}
