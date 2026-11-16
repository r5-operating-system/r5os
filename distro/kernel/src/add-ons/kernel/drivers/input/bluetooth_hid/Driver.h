/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Bluetooth HID (BR/EDR), fed by bluetooth_server through the control node
 * in <R5BluetoothHID.h>. The names below are what hid_shared expects of a
 * driver (as in usb_hid and i2c_hid).
 */
#ifndef _BLUETOOTH_HID_DRIVER_H_
#define _BLUETOOTH_HID_DRIVER_H_

#include <Drivers.h>
#include <KernelExport.h>
#include <OS.h>
#include <util/kernel_cpp.h>

#include "DeviceList.h"

#define DRIVER_NAME			"bluetooth_hid"
#define DEVICE_PATH_SUFFIX	"bluetooth"
#define DEVICE_NAME			"Bluetooth"

extern DeviceList *gDeviceList;

// Driver.cpp: an output report for bluetooth_server (R5_BTHID_GET_OUTPUT).
status_t bthid_queue_output(int32 id, const uint8* data, size_t length);


//#define TRACE_BLUETOOTH_HID
#ifdef TRACE_BLUETOOTH_HID
#	define TRACE(x...) dprintf(DRIVER_NAME ": " x)
#else
#	define TRACE(x...)
#endif
#define ERROR(x...) dprintf(DRIVER_NAME ": " x)
#define TRACE_ALWAYS(x...)	dprintf(DRIVER_NAME ": " x)
#define CALLED() TRACE("CALLED %s\n", __PRETTY_FUNCTION__)

#endif // _BLUETOOTH_HID_DRIVER_H_
