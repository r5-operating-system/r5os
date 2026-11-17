/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * bluetooth_hid: Bluetooth Classic HID (BR/EDR) for input_server and
 * BJoystick (docs/BLUETOOTH.md, step 4). bluetooth_server holds the L2CAP
 * channels and feeds this driver through R5_BTHID_CONTROL_NAME
 * (<R5BluetoothHID.h>). Reports are parsed by Haiku's hid_shared, the code
 * of usb_hid and i2c_hid, so a keyboard, a mouse and a gamepad behave as
 * they do over USB.
 *
 * Every pointer in an ioctl is a userland pointer: it is checked with
 * IS_USER_ADDRESS and read or written only with user_memcpy (SMAP).
 */


#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <kernel.h>
#include <lock.h>
#include <fs/devfs.h>
#include <util/AutoLock.h>

#include "bluetooth/R5BluetoothHID.h"

#include "DeviceList.h"
#include "Driver.h"
#include "HIDDevice.h"
#include "ProtocolHandler.h"


struct device_cookie {
	ProtocolHandler*	handler;	// NULL for the control node
	uint32				cookie;
};


// An output report waiting for bluetooth_server. Keyboard lights are one
// or two bytes; larger output reports are not sent.
struct output_entry {
	int32	id;
	size_t	length;
	uint8	data[64];
};

static const int32 kOutputQueueLength = 16;


int32 api_version = B_CUR_DRIVER_API_VERSION;
DeviceList* gDeviceList = NULL;

static mutex sDriverLock;
static HIDDevice* sDevices[R5_BTHID_MAX_DEVICES];
	// index = ID
static int sControlMarker;
	// the "device" of the control node in gDeviceList

static mutex sOutputLock;
static sem_id sOutputSem = -1;
static output_entry sOutput[kOutputQueueLength];
static int32 sOutputHead = 0;
static int32 sOutputCount = 0;
static thread_id sRescanThread = -1;
	// uninit_driver() waits for it, so it never runs unloaded code


status_t
bthid_queue_output(int32 id, const uint8* data, size_t length)
{
	if (length == 0 || length > sizeof(sOutput[0].data))
		return B_BAD_VALUE;

	MutexLocker locker(sOutputLock);
	bool full = sOutputCount == kOutputQueueLength;
	if (full) {
		// Drop the oldest; the semaphore already counts this slot.
		sOutputHead = (sOutputHead + 1) % kOutputQueueLength;
		sOutputCount--;
	}
	output_entry& entry
		= sOutput[(sOutputHead + sOutputCount) % kOutputQueueLength];
	entry.id = id;
	entry.length = length;
	memcpy(entry.data, data, length);
	sOutputCount++;
	locker.Unlock();

	if (!full)
		release_sem_etc(sOutputSem, 1, B_DO_NOT_RESCHEDULE);
	return B_OK;
}


// #pragma mark - devices


//! sDriverLock is held. Removes the handlers' names and frees or retires
//! the device.
static void
remove_device_locked(int32 id)
{
	HIDDevice* device = sDevices[id];
	if (device == NULL)
		return;
	sDevices[id] = NULL;

	for (uint32 i = 0;; i++) {
		ProtocolHandler* handler = device->ProtocolHandlerAt(i);
		if (handler == NULL)
			break;
		if (handler->PublishPath() != NULL)
			gDeviceList->RemoveDevice(NULL, handler);
	}

	if (device->CookieCount() > 0) {
		// Deleted in the free hook of its last file handle.
		device->Removed();
	} else
		delete device;
}


static status_t create_device_locked(device_cookie* owner,
	const uint8* descriptor, size_t descriptorLength,
	r5_bthid_create* request);


static status_t
create_device(device_cookie* owner, void* userBuffer, size_t length)
{
	if (length < sizeof(r5_bthid_create) || !IS_USER_ADDRESS(userBuffer))
		return B_BAD_ADDRESS;

	r5_bthid_create request;
	if (user_memcpy(&request, userBuffer, sizeof(request)) != B_OK)
		return B_BAD_ADDRESS;

	if (request.descriptor_length == 0
		|| request.descriptor_length > R5_BTHID_MAX_DESCRIPTOR)
		return B_BAD_VALUE;
	if (!IS_USER_ADDRESS(request.descriptor))
		return B_BAD_ADDRESS;

	uint8* descriptor = (uint8*)malloc(request.descriptor_length);
	if (descriptor == NULL)
		return B_NO_MEMORY;
	if (user_memcpy(descriptor, request.descriptor,
			request.descriptor_length) != B_OK) {
		free(descriptor);
		return B_BAD_ADDRESS;
	}

	// The parser keeps no pointer into the descriptor.
	status_t status = create_device_locked(owner, descriptor,
		request.descriptor_length, &request);
	free(descriptor);
	if (status != B_OK)
		return status;

	devfs_rescan_driver(DRIVER_NAME);

	if (user_memcpy(userBuffer, &request, sizeof(request)) != B_OK)
		return B_BAD_ADDRESS;
	return B_OK;
}


static status_t
create_device_locked(device_cookie* owner, const uint8* descriptor,
	size_t descriptorLength, r5_bthid_create* request)
{
	MutexLocker locker(sDriverLock);

	int32 id = -1;
	for (int32 i = 0; i < R5_BTHID_MAX_DEVICES; i++) {
		if (sDevices[i] == NULL) {
			id = i;
			break;
		}
	}
	if (id < 0)
		return B_BUSY;

	HIDDevice* device = new(std::nothrow) HIDDevice(id, owner, descriptor,
		descriptorLength);
	if (device == NULL)
		return B_NO_MEMORY;
	if (device->InitCheck() != B_OK) {
		status_t status = device->InitCheck();
		delete device;
		return status;
	}

	// Names as in usb_hid: the first free index under each base path.
	for (uint32 i = 0;; i++) {
		ProtocolHandler* handler = device->ProtocolHandlerAt(i);
		if (handler == NULL)
			break;

		char pathBuffer[B_DEV_NAME_LENGTH];
		const char* basePath = handler->BasePath();
		for (int32 index = 0;; index++) {
			snprintf(pathBuffer, sizeof(pathBuffer), "%s%" B_PRId32, basePath,
				index);
			if (gDeviceList->FindDevice(pathBuffer) == NULL)
				break;
		}
		char* publishPath = strdup(pathBuffer);
		if (publishPath == NULL) {
			sDevices[id] = device;
			remove_device_locked(id);
			return B_NO_MEMORY;
		}
		handler->SetPublishPath(publishPath);
		gDeviceList->AddDevice(handler->PublishPath(), handler);
		TRACE_ALWAYS("published %s\n", handler->PublishPath());
	}
	sDevices[id] = device;

	request->id = id;
	request->handlers = device->CountProtocolHandlers();
	return B_OK;
}


static status_t
destroy_device(device_cookie* owner, void* userBuffer, size_t length)
{
	int32 id;
	if (length < sizeof(id) || !IS_USER_ADDRESS(userBuffer)
		|| user_memcpy(&id, userBuffer, sizeof(id)) != B_OK)
		return B_BAD_ADDRESS;
	if (id < 0 || id >= R5_BTHID_MAX_DEVICES)
		return B_BAD_VALUE;

	MutexLocker locker(sDriverLock);
	if (sDevices[id] == NULL || sDevices[id]->Owner() != owner)
		return B_ENTRY_NOT_FOUND;
	remove_device_locked(id);
	locker.Unlock();

	devfs_rescan_driver(DRIVER_NAME);
	return B_OK;
}


static status_t
feed_device(device_cookie* owner, void* userBuffer, size_t length)
{
	r5_bthid_input request;
	if (length < sizeof(request) || !IS_USER_ADDRESS(userBuffer)
		|| user_memcpy(&request, userBuffer, sizeof(request)) != B_OK)
		return B_BAD_ADDRESS;
	if (request.id < 0 || request.id >= R5_BTHID_MAX_DEVICES
		|| request.length == 0 || request.length > R5_BTHID_MAX_REPORT)
		return B_BAD_VALUE;
	if (!IS_USER_ADDRESS(request.data))
		return B_BAD_ADDRESS;

	uint8 report[R5_BTHID_MAX_REPORT];
	if (user_memcpy(report, request.data, request.length) != B_OK)
		return B_BAD_ADDRESS;

	MutexLocker locker(sDriverLock);
	HIDDevice* device = sDevices[request.id];
	if (device == NULL || device->Owner() != owner)
		return B_ENTRY_NOT_FOUND;
	device->Feed(report, request.length);
	return B_OK;
}


static status_t
set_info(device_cookie* owner, void* userBuffer, size_t length)
{
	r5_bthid_info request;
	if (length < sizeof(request) || !IS_USER_ADDRESS(userBuffer)
		|| user_memcpy(&request, userBuffer, sizeof(request)) != B_OK)
		return B_BAD_ADDRESS;
	if (request.id < 0 || request.id >= R5_BTHID_MAX_DEVICES)
		return B_BAD_VALUE;

	MutexLocker locker(sDriverLock);
	HIDDevice* device = sDevices[request.id];
	if (device == NULL || device->Owner() != owner)
		return B_ENTRY_NOT_FOUND;
	device->SetInfo(request);
	return B_OK;
}


static status_t
get_battery(device_cookie* owner, void* userBuffer, size_t length)
{
	r5_bthid_battery request;
	if (length < sizeof(request) || !IS_USER_ADDRESS(userBuffer)
		|| user_memcpy(&request, userBuffer, sizeof(request)) != B_OK)
		return B_BAD_ADDRESS;
	if (request.id < 0 || request.id >= R5_BTHID_MAX_DEVICES)
		return B_BAD_VALUE;

	MutexLocker locker(sDriverLock);
	HIDDevice* device = sDevices[request.id];
	if (device == NULL || device->Owner() != owner)
		return B_ENTRY_NOT_FOUND;
	request.battery = device->Battery();
	locker.Unlock();

	if (user_memcpy(userBuffer, &request, sizeof(request)) != B_OK)
		return B_BAD_ADDRESS;
	return B_OK;
}


static status_t
get_output(void* userBuffer, size_t length)
{
	r5_bthid_output request;
	if (length < sizeof(request) || !IS_USER_ADDRESS(userBuffer)
		|| user_memcpy(&request, userBuffer, sizeof(request)) != B_OK)
		return B_BAD_ADDRESS;
	if (!IS_USER_ADDRESS(request.data))
		return B_BAD_ADDRESS;

	status_t status = acquire_sem_etc(sOutputSem, 1,
		B_RELATIVE_TIMEOUT | B_CAN_INTERRUPT,
		request.timeout > 0 ? request.timeout : 0);
	if (status != B_OK)
		return status;

	output_entry entry;
	{
		MutexLocker locker(sOutputLock);
		if (sOutputCount == 0)
			return B_WOULD_BLOCK;
		entry = sOutput[sOutputHead];
		sOutputHead = (sOutputHead + 1) % kOutputQueueLength;
		sOutputCount--;
	}

	if (entry.length > request.capacity)
		return B_BUFFER_OVERFLOW;
	if (user_memcpy(request.data, entry.data, entry.length) != B_OK)
		return B_BAD_ADDRESS;
	request.id = entry.id;
	request.length = entry.length;
	if (user_memcpy(userBuffer, &request, sizeof(request)) != B_OK)
		return B_BAD_ADDRESS;
	return B_OK;
}


static status_t
rescan_thread(void*)
{
	devfs_rescan_driver(DRIVER_NAME);
	return B_OK;
}


// #pragma mark - driver hooks


static status_t
bluetooth_hid_open(const char* name, uint32 flags, void** _cookie)
{
	TRACE("open(%s, %" B_PRIu32 ")\n", name, flags);

	device_cookie* cookie = new(std::nothrow) device_cookie();
	if (cookie == NULL)
		return B_NO_MEMORY;
	cookie->handler = NULL;
	cookie->cookie = 0;

	if (strcmp(name, R5_BTHID_CONTROL_NAME) == 0) {
		// Only bluetooth_server, a system service, feeds input devices.
		if (geteuid() != 0) {
			delete cookie;
			return B_PERMISSION_DENIED;
		}
		*_cookie = cookie;
		return B_OK;
	}

	MutexLocker locker(sDriverLock);
	ProtocolHandler* handler = (ProtocolHandler*)gDeviceList->FindDevice(name);
	if (handler == NULL || handler == (void*)&sControlMarker) {
		delete cookie;
		return B_ENTRY_NOT_FOUND;
	}

	status_t result = handler->Open(flags, &cookie->cookie);
	if (result != B_OK) {
		delete cookie;
		return result;
	}

	cookie->handler = handler;
	handler->Device()->AcquireCookie();
	*_cookie = cookie;
	return B_OK;
}


static status_t
bluetooth_hid_read(void* _cookie, off_t position, void* buffer,
	size_t* numBytes)
{
	device_cookie* cookie = (device_cookie*)_cookie;
	if (cookie->handler == NULL) {
		*numBytes = 0;
		return B_NOT_ALLOWED;
	}
	return cookie->handler->Read(&cookie->cookie, position, buffer, numBytes);
}


static status_t
bluetooth_hid_write(void* _cookie, off_t position, const void* buffer,
	size_t* numBytes)
{
	device_cookie* cookie = (device_cookie*)_cookie;
	if (cookie->handler == NULL) {
		*numBytes = 0;
		return B_NOT_ALLOWED;
	}
	return cookie->handler->Write(&cookie->cookie, position, buffer, numBytes);
}


static status_t
bluetooth_hid_control(void* _cookie, uint32 op, void* buffer, size_t length)
{
	device_cookie* cookie = (device_cookie*)_cookie;
	if (cookie->handler != NULL)
		return cookie->handler->Control(&cookie->cookie, op, buffer, length);

	switch (op) {
		case R5_BTHID_CREATE:
			return create_device(cookie, buffer, length);
		case R5_BTHID_DESTROY:
			return destroy_device(cookie, buffer, length);
		case R5_BTHID_INPUT:
			return feed_device(cookie, buffer, length);
		case R5_BTHID_GET_OUTPUT:
			return get_output(buffer, length);
		case R5_BTHID_SET_INFO:
			return set_info(cookie, buffer, length);
		case R5_BTHID_GET_BATTERY:
			return get_battery(cookie, buffer, length);
	}
	return B_DEV_INVALID_IOCTL;
}


static status_t
bluetooth_hid_close(void* _cookie)
{
	device_cookie* cookie = (device_cookie*)_cookie;
	if (cookie->handler == NULL)
		return B_OK;
	return cookie->handler->Close(&cookie->cookie);
}


static status_t
bluetooth_hid_free(void* _cookie)
{
	device_cookie* cookie = (device_cookie*)_cookie;

	bool rescan = false;
	mutex_lock(&sDriverLock);
	if (cookie->handler == NULL) {
		// bluetooth_server closed the control node (or quit): its devices
		// go with it.
		for (int32 i = 0; i < R5_BTHID_MAX_DEVICES; i++) {
			if (sDevices[i] != NULL && sDevices[i]->Owner() == cookie) {
				remove_device_locked(i);
				rescan = true;
			}
		}
	} else {
		HIDDevice* device = cookie->handler->Device();
		if (device->ReleaseCookie() && device->IsRemoved()) {
			// Its handlers are unpublished and no handle is left.
			delete device;
		}
	}
	mutex_unlock(&sDriverLock);

	if (rescan) {
		// Not from inside a devfs hook: a short thread republishes.
		thread_id thread = spawn_kernel_thread(rescan_thread,
			DRIVER_NAME " rescan", B_NORMAL_PRIORITY, NULL);
		if (thread >= 0) {
			// No wait here: the previous rescan may need the devfs lock
			// this hook is called under. It ended long before in practice.
			atomic_set(&sRescanThread, thread);
			resume_thread(thread);
		}
	}

	delete cookie;
	return B_OK;
}


// #pragma mark - driver API


status_t
init_hardware()
{
	return B_OK;
}


status_t
init_driver()
{
	gDeviceList = new(std::nothrow) DeviceList();
	if (gDeviceList == NULL)
		return B_NO_MEMORY;

	sOutputSem = create_sem(0, DRIVER_NAME " output");
	if (sOutputSem < 0) {
		delete gDeviceList;
		gDeviceList = NULL;
		return sOutputSem;
	}

	mutex_init(&sDriverLock, DRIVER_NAME " lock");
	mutex_init(&sOutputLock, DRIVER_NAME " output lock");
	memset(sDevices, 0, sizeof(sDevices));
	sOutputHead = 0;
	sOutputCount = 0;

	if (gDeviceList->AddDevice(R5_BTHID_CONTROL_NAME, &sControlMarker)
			!= B_OK) {
		delete_sem(sOutputSem);
		sOutputSem = -1;
		delete gDeviceList;
		gDeviceList = NULL;
		mutex_destroy(&sDriverLock);
		mutex_destroy(&sOutputLock);
		return B_NO_MEMORY;
	}
	return B_OK;
}


void
uninit_driver()
{
	// Nothing is open (devfs unloads only then), so every device can go.
	mutex_lock(&sDriverLock);
	for (int32 i = 0; i < R5_BTHID_MAX_DEVICES; i++)
		remove_device_locked(i);
	mutex_unlock(&sDriverLock);

	thread_id rescan = atomic_get_and_set(&sRescanThread, -1);
	if (rescan >= 0)
		wait_for_thread(rescan, NULL);

	delete_sem(sOutputSem);
	sOutputSem = -1;
	delete gDeviceList;
	gDeviceList = NULL;
	mutex_destroy(&sDriverLock);
	mutex_destroy(&sOutputLock);
}


const char**
publish_devices()
{
	MutexLocker locker(sDriverLock);
	return gDeviceList->PublishDevices();
}


device_hooks*
find_device(const char* name)
{
	static device_hooks hooks = {
		bluetooth_hid_open,
		bluetooth_hid_close,
		bluetooth_hid_free,
		bluetooth_hid_control,
		bluetooth_hid_read,
		bluetooth_hid_write,
		NULL,				/* select */
		NULL				/* deselect */
	};

	MutexLocker locker(sDriverLock);
	if (gDeviceList->FindDevice(name) == NULL)
		return NULL;
	return &hooks;
}
