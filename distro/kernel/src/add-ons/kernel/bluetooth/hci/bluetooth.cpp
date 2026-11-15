/*
 * Copyright 2008, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 */

#include <new>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <KernelExport.h>
#include <lock.h>
#include <SupportDefs.h>
#include <util/AutoLock.h>
#include <util/DoublyLinkedList.h>

#include <net_buffer.h>
#include <net_device.h>
#include <net_stack.h>
#include <NetBufferUtilities.h>

#include <btDebug.h>
#include <btCoreData.h>
#include <btModules.h>
#include <CodeHandler.h>
#define KERNEL_LAND
#include <PortListener.h>
#undef KERNEL_LAND

#include <bluetooth/HCI/btHCI.h>
#include <bluetooth/HCI/btHCI_acl.h>
#include <bluetooth/HCI/btHCI_command.h>
#include <bluetooth/HCI/btHCI_event.h>
#include <bluetooth/HCI/btHCI_transport.h>
#include <bluetooth/HCI/btHCI_sco.h>
#include <bluetooth/bdaddrUtils.h>

#include "acl.h"
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include "hci_assemble.h"
// SPDX-SnippetEnd


int32 api_version = B_CUR_DRIVER_API_VERSION;


typedef PortListener<void,
	HCI_MAX_FRAME_SIZE, // Event Body can hold max 255 + 2 header
	24					// Some devices have sent chunks of 24 events(inquiry result)
	> BluetoothRawDataPort;


// Modules references
net_buffer_module_info* gBufferModule = NULL;
struct bluetooth_core_data_module_info* btCoreData = NULL;

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// Recursive: the RX thread holds it while a packet is assembled and posted,
// and that path may look a device up again.
static recursive_lock sListLock;
// SPDX-SnippetEnd
static sem_id sLinkChangeSemaphore;
static DoublyLinkedList<bluetooth_device> sDeviceList;

BluetoothRawDataPort* BluetoothRXPort;

// forward declarations
status_t HciPacketHandler(void* data, int32 code, size_t size);


bluetooth_device*
FindDeviceByID(hci_id hid)
{
	bluetooth_device* device;
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	RecursiveLocker locker(&sListLock);
	// SPDX-SnippetEnd

	DoublyLinkedList<bluetooth_device>::Iterator iterator
		= sDeviceList.GetIterator();

	while (iterator.HasNext()) {
		device = iterator.Next();
		if (device->index == hid)
			return device;
	}

	return NULL;
}


status_t
PostTransportPacket(hci_id hid, bt_packet_t type, void* data, size_t count)
{
	uint32 code = 0;

	Bluetooth::CodeHandler::SetDevice(&code, hid);
	Bluetooth::CodeHandler::SetProtocol(&code, type);

	return BluetoothRXPort->Trigger(code, data, count);
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// The reassembly itself is hci_assemble.h, which scripts/fuzz-bt.sh also
// runs on the host. The old code here panicked on an event header split
// over two transfers and on every event that arrived in pieces, looped
// forever on SCO, and did not check the ACL allocation.
struct NetBufferAssembleOps {
	typedef net_buffer* Buffer;

	bluetooth_device* device;

	Buffer Current(uint8 type)
	{
		return device->fBuffersRx[type];
	}

	void SetCurrent(uint8 type, Buffer buffer)
	{
		device->fBuffersRx[type] = buffer;
		if (buffer == NULL)
			device->fExpectedPacketSize[type] = 0;
	}

	Buffer Create()
	{
		net_buffer* buffer = gBufferModule->create(0);
		return buffer;
	}

	bool Append(Buffer buffer, const uint8* data, size_t size)
	{
		return gBufferModule->append(buffer, data, size) == B_OK;
	}

	size_t Size(Buffer buffer)
	{
		return buffer->size;
	}

	bool Read(Buffer buffer, size_t offset, uint8* data, size_t size)
	{
		return gBufferModule->read(buffer, offset, data, size) == B_OK;
	}

	void Destroy(Buffer buffer)
	{
		gBufferModule->free(buffer);
	}

	void PostEvent(const uint8* event, size_t size)
	{
		TRACE("%s: EVENT posted in HCI
", __func__);
		btCoreData->PostEvent(device, (void*)event, size);
	}

	void PostAcl(Buffer buffer)
	{
		buffer->protocol = BT_ACL;
		AclAssembly(buffer, device->index);
	}
};


status_t
Assemble(bluetooth_device* bluetoothDevice, bt_packet_t type, void* data,
	size_t count)
{
	NetBufferAssembleOps ops = { bluetoothDevice };
	status_t status = hci_assemble(ops, (uint8)type, (const uint8*)data,
		count);
	if (status == B_BAD_VALUE)
		ERROR("%s: unknown packet type %d
", __func__, type);
	else if (status != B_OK)
		ERROR("%s: dropped a packet of type %d: %s
", __func__, type,
			strerror(status));
	return status;
}
// SPDX-SnippetEnd


status_t
HciPacketHandler(void* data, int32 code, size_t size)
{
	hci_id deviceId = Bluetooth::CodeHandler::Device(code);

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// Held until the packet is posted, so UnregisterDriver() cannot free the
	// device in between.
	RecursiveLocker locker(&sListLock);
	// SPDX-SnippetEnd
	bluetooth_device* bluetoothDevice = FindDeviceByID(deviceId);

	TRACE("%s: to assemble %" B_PRIuSIZE " bytes of 0x%" B_PRIx32 "\n",
		__func__, size, deviceId);

	if (bluetoothDevice != NULL) {
		return Assemble(bluetoothDevice, Bluetooth::CodeHandler::Protocol(code),
			data, size);
	} else {
		ERROR("%s: Device 0x%" B_PRIx32 " could not be matched\n", __func__,
			deviceId);
	}

	return B_ERROR;
}


//	#pragma mark -


status_t
RegisterDriver(bt_hci_transport_hooks* hooks, bluetooth_device** _device)
{

	bluetooth_device* device = new (std::nothrow) bluetooth_device;
	if (device == NULL)
		return B_NO_MEMORY;

	for (int index = 0; index < HCI_NUM_PACKET_TYPES; index++) {
		device->fBuffersRx[index] = NULL;
		device->fExpectedPacketSize[index] = 0;
	}

	device->info = NULL; // not yet used
	device->hooks = hooks;
	device->supportedPacketTypes = (HCI_DM1 | HCI_DH1 | HCI_HV1);
	device->linkMode = (HCI_LM_ACCEPT);
	device->mtu = L2CAP_MTU_MINIMUM; // TODO: ensure specs min value

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	RecursiveLocker _(&sListLock);
	// SPDX-SnippetEnd

	if (sDeviceList.IsEmpty())
		device->index = HCI_DEVICE_INDEX_OFFSET; // REVIEW: dev index
	else {
		device->index = (sDeviceList.Tail())->index + 1; // REVIEW!
		TRACE("%s: List not empty\n", __func__);
	}

	sDeviceList.Add(device);

	TRACE("%s: Device %" B_PRIx32 "\n", __func__, device->index);

	*_device = device;

	return B_OK;
}


status_t
UnregisterDriver(hci_id id)
{
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// The RX thread uses the list too (HciPacketHandler).
	RecursiveLocker locker(&sListLock);
	// SPDX-SnippetEnd
	bluetooth_device* device = FindDeviceByID(id);

	if (device == NULL)
		return B_ERROR;

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// A packet that was half assembled when the radio went away.
	for (int index = 0; index < HCI_NUM_PACKET_TYPES; index++) {
		if (device->fBuffersRx[index] != NULL)
			gBufferModule->free(device->fBuffersRx[index]);
		device->fBuffersRx[index] = NULL;
	}
	// SPDX-SnippetEnd

	if (device->GetDoublyLinkedListLink()->next != NULL
		|| device->GetDoublyLinkedListLink()->previous != NULL
		|| device == sDeviceList.Head())
		sDeviceList.Remove(device);

	delete device;

	return B_OK;
}


status_t
PostCommand(hci_id hciId, net_buffer* buffer)
{
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	if (buffer == NULL)
		return B_BAD_VALUE;
	// SPDX-SnippetEnd

	bluetooth_device* device = FindDeviceByID(hciId);
	if (device == NULL) {
		ERROR("%s: No device 0x%" B_PRIx32 "\n", __func__, hciId);
		return B_ERROR;
	}

	buffer->protocol = BT_COMMAND;

	return device->hooks->SendCommand(hciId, buffer);
}


// PostACL
status_t
PostACL(hci_id hciId, net_buffer* buffer)
{
	net_buffer* curr_frame = NULL;
	net_buffer* next_frame = buffer;
	uint8 flag = HCI_ACL_PACKET_START;

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	if (buffer == NULL)
		return B_BAD_VALUE;
	// SPDX-SnippetEnd

	uint16 handle = buffer->type; // TODO: CodeHandler

	bluetooth_device* device = FindDeviceByID(hciId);

	if (device == NULL) {
		ERROR("%s: No device 0x%" B_PRIx32 "\n", __func__, hciId);
		return B_ERROR;
	}

	TRACE("%s: index 0x%" B_PRIx32 " try to send bt packet of %" B_PRIu32
		" bytes (flags 0x%" B_PRIx32 "):\n", __func__, device->index,
		buffer->size, buffer->flags);

	// TODO: ATOMIC! any other thread should stop here
	do {
		// Divide packet if big enough
		curr_frame = next_frame;

		if (curr_frame->size > device->mtu) {
			next_frame = curr_frame;
			curr_frame = gBufferModule->split(next_frame, device->mtu);
			if (curr_frame == NULL)
				return B_NO_MEMORY;
		} else {
			next_frame = NULL;
		}

		// Attach acl header
		{
			NetBufferPrepend<struct hci_acl_header> bufferHeader(curr_frame);
			status_t status = bufferHeader.Status();
			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			// The frame was leaked here, and the rest went out without it.
			if (status < B_OK) {
				gBufferModule->free(curr_frame);
				if (next_frame != NULL)
					gBufferModule->free(next_frame);
				return status;
			}

			// The ACL header is little-endian (Core 6.0, Vol 4, Part E,
			// 5.4.2).
			bufferHeader->handle = B_HOST_TO_LENDIAN_INT16(
				pack_acl_handle_flags(handle, flag, 0));
			bufferHeader->alen = B_HOST_TO_LENDIAN_INT16(
				curr_frame->size - sizeof(struct hci_acl_header));
			// SPDX-SnippetEnd
		}

		// Send to driver
		curr_frame->protocol = BT_ACL;

		TRACE("%s: Tolower nbuf %p!\n", __func__, curr_frame);
		// We could pass a cookie and avoid the driver fetch the Id
		device->hooks->SendACL(device->index, curr_frame);
		flag = HCI_ACL_PACKET_FRAGMENT;

	} while (next_frame != NULL);

	return B_OK;
}


status_t
PostSCO(hci_id hciId, net_buffer* buffer)
{
	bluetooth_device* device = FindDeviceByID(hciId);

	if (device == NULL) {
		ERROR("%s: No device 0x%" B_PRIx32 "\n", __func__, hciId);
		return B_ERROR;
	}

	buffer->protocol = BT_SCO;

	return device->hooks->SendSCO(hciId, buffer);
}


status_t
PostESCO(hci_id hciId, net_buffer* buffer)
{
	return B_ERROR;
}


static int
dump_bluetooth_devices(int argc, char** argv)
{
	bluetooth_device*	device;

	DoublyLinkedList<bluetooth_device>::Iterator iterator
		= sDeviceList.GetIterator();

	while (iterator.HasNext()) {
		device = iterator.Next();
		kprintf("\tindex=%" B_PRIx32 " @%p hooks=%p\n", device->index,
			device, device->hooks);
	}

	return 0;
}


static status_t
bluetooth_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
		{
			status_t status;

			status = get_module(NET_BUFFER_MODULE_NAME,
				(module_info**)&gBufferModule);

			if (status < B_OK) {
				panic("no way Dude we need that!");
				return status;
			}

			status = get_module(BT_CORE_DATA_MODULE_NAME,
				(module_info**)&btCoreData);
			if (status < B_OK) {
				ERROR("%s: problem getting bt core data module\n", __func__);
				return status;
			}

			new (&sDeviceList) DoublyLinkedList<bluetooth_device>;
				// static C++ objects are not initialized in the module startup

			BluetoothRXPort = new BluetoothRawDataPort(BT_RX_PORT_NAME,
				(BluetoothRawDataPort::port_listener_func)&HciPacketHandler);

			if (BluetoothRXPort->Launch() != B_OK) {
				ERROR("%s: RX thread creation failed!\n", __func__);
				// we Cannot do much here ... avoid registering
			} else {
				TRACE("%s: RX thread launched!\n", __func__);
			}

			sLinkChangeSemaphore = create_sem(0, "bt sem");
			if (sLinkChangeSemaphore < B_OK) {
				// SPDX-SnippetBegin
				// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
				// SPDX-License-Identifier: MIT
				// NET_STACK_MODULE_NAME was never taken here.
				put_module(BT_CORE_DATA_MODULE_NAME);
				put_module(NET_BUFFER_MODULE_NAME);
				// SPDX-SnippetEnd
				ERROR("%s: Link change semaphore failed\n", __func__);
				return sLinkChangeSemaphore;
			}

			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			recursive_lock_init(&sListLock, "bluetooth devices");

			// status = InitializeAclConnectionThread();
			// That thread was never started, so the "Connection Thread
			// error" logged here on every boot said nothing.
			// SPDX-SnippetEnd

			add_debugger_command("btLocalDevices", &dump_bluetooth_devices,
				"Lists Bluetooth LocalDevices registered in the Stack");

			return B_OK;
		}

		case B_MODULE_UNINIT:
		{
			delete_sem(sLinkChangeSemaphore);

			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			recursive_lock_destroy(&sListLock);
			put_module(NET_BUFFER_MODULE_NAME);
			// NET_STACK_MODULE_NAME was never taken, so it is not put.
			put_module(BT_CORE_DATA_MODULE_NAME);
			// SPDX-SnippetEnd
			remove_debugger_command("btLocalDevices", &dump_bluetooth_devices);
			// status_t status = QuitAclConnectionThread();

			return B_OK;
		}

		default:
			return B_ERROR;
	}
}


bt_hci_module_info sBluetoothModule = {
	{
		BT_HCI_MODULE_NAME, 
		B_KEEP_LOADED, 
		bluetooth_std_ops
	}, 
	RegisterDriver, 
	UnregisterDriver,
	FindDeviceByID, 
	PostTransportPacket, 
	PostCommand, 
	PostACL, 
	PostSCO, 
	PostESCO
};


module_info* modules[] = {
	(module_info*)&sBluetoothModule,
	NULL
};

