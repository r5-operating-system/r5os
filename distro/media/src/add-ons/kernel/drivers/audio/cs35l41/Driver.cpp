/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


/*!	Driver for the Cirrus Logic CS35L41 speaker amplifiers behind an HD
	Audio codec (ACPI CSC3551). Phase 1: DSP bypass with a hard volume
	limit, no firmware.

	One ACPI device holds every amplifier. Haiku's I2C controller registers
	a node for its first I2cSerialBus only, so this driver reads all
	addresses from _CRS and talks to each amplifier through the I2C bus
	interface with that address.

	Clean room: written from generated/CS35L41-SPEC.md, CS35L41-SPEC-2.md
	and CS35L41-DESIGN.md, R5's distro tree and Haiku's own code. No Linux
	code was read.
*/


#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ACPI.h>
#include <KernelExport.h>
#include <ByteOrder.h>
#include <GPIO.h>
#include <AudioAmp.h>
#include <device_manager.h>
#include <i2c.h>
#include <lock.h>
#include <util/AutoLock.h>

extern "C" {
#	include "acpi.h"
}

#include "Amplifier.h"


#define TRACE_ALWAYS(a...)	dprintf("cs35l41: " a)

#define CS35L41_DRIVER_NAME	"drivers/audio/hmulti/cs35l41/driver_v1"
#define ACPI_NAME_CS35L41	"CSC3551"

#define MAX_AMPS			2

// ACPI Notify on the amplifier device: the mute override changed.
#define NOTIFY_MUTE_CHANGED	0x91
// Speaker fault poll while playing.
#define POLL_INTERVAL		100000


/*!	Machines this driver knows. ACPI gives the I2C addresses and GPIOs;
	the boost parts and TDM slots are not in ACPI. Values are from the
	clean-room specification (CS35L41-SPEC.md sections 1 and 2).
*/
static const machine_info kMachines[] = {
	{
		0x103c8a31, "HP ENVY x360 15", 2, { 0, 1 },
		1000, 24, 4100,			// 1.0 uH, 24 uF, 4100 mA
		0x4924, 0x0, 0x75
	},
};


struct crs_info {
	uint32			address_count;
	i2c_addr		addresses[MAX_AMPS + 2];
	uint32			gpio_count;
};


struct cs35l41_driver {
	device_node*		node;
	acpi_handle			handle;
	uint32				subsystem;
	const machine_info*	machine;

	i2c_link			link;
	mutex				lock;

	Amplifier*			amps[MAX_AMPS];
	int32				amp_ids[MAX_AMPS];
	uint32				amp_count;

	gpio_pin			reset;
	gpio_pin			speaker_id;

	sem_id				events;
	thread_id			worker;
	bool				quit;
	int32				notify_pending;
	bool				notify_installed;
};


static device_manager_info* sDeviceManager;
static acpi_module_info* sACPI;
static audio_amp_module_info* sAudioAmp;
static gpio_module_info* sGPIO;


//	#pragma mark - ACPI


static acpi_status
parse_crs(ACPI_RESOURCE* resource, void* context)
{
	crs_info* info = (crs_info*)context;

	if (resource->Type == ACPI_RESOURCE_TYPE_SERIAL_BUS
		&& resource->Data.CommonSerialBus.Type
			== ACPI_RESOURCE_SERIAL_TYPE_I2C) {
		if (info->address_count < B_COUNT_OF(info->addresses)) {
			info->addresses[info->address_count++] = B_LENDIAN_TO_HOST_INT16(
				resource->Data.I2cSerialBus.SlaveAddress);
		}
	} else if (resource->Type == ACPI_RESOURCE_TYPE_GPIO)
		info->gpio_count++;
	return AE_OK;
}


/*!	The HDA codec subsystem from _SUB, a string such as "103C8A31". */
static uint32
read_subsystem(acpi_handle handle)
{
	acpi_data buffer;
	buffer.pointer = NULL;
	buffer.length = ACPI_ALLOCATE_BUFFER;
	if (sACPI->evaluate_method(handle, "_SUB", NULL, &buffer) != B_OK
		|| buffer.pointer == NULL) {
		return 0;
	}

	uint32 subsystem = 0;
	acpi_object_type* object = (acpi_object_type*)buffer.pointer;
	if (object->object_type == ACPI_TYPE_STRING
		&& object->string.string != NULL) {
		subsystem = strtoul(object->string.string, NULL, 16);
	}
	free(buffer.pointer);
	return subsystem;
}


/*!	The ACPI mute override: _DSM function 5, byte 0 of the result
	(CS35L41-SPEC-2.md section 3). Greater than 0 forces mute.
*/
static status_t
read_acpi_mute(acpi_handle handle, bool* _muted)
{
	// 50d90cdc-3de4-4f18-b528-c7fe3b71f40d
	static const uint8 kMuteUuid[16] = { 0xdc, 0x0c, 0xd9, 0x50, 0xe4, 0x3d,
		0x18, 0x4f, 0xb5, 0x28, 0xc7, 0xfe, 0x3b, 0x71, 0xf4, 0x0d };

	acpi_object_type arguments[4];
	arguments[0].object_type = ACPI_TYPE_BUFFER;
	arguments[0].buffer.buffer = (void*)kMuteUuid;
	arguments[0].buffer.length = sizeof(kMuteUuid);
	arguments[1].object_type = ACPI_TYPE_INTEGER;
	arguments[1].integer.integer = 0;
	arguments[2].object_type = ACPI_TYPE_INTEGER;
	arguments[2].integer.integer = 5;
	arguments[3].object_type = ACPI_TYPE_PACKAGE;
	arguments[3].package.objects = NULL;
	arguments[3].package.count = 0;

	acpi_objects list;
	list.count = 4;
	list.pointer = arguments;

	acpi_data buffer;
	buffer.pointer = NULL;
	buffer.length = ACPI_ALLOCATE_BUFFER;
	status_t status = sACPI->evaluate_method(handle, "_DSM", &list, &buffer);
	if (status != B_OK || buffer.pointer == NULL)
		return status != B_OK ? status : B_ERROR;

	acpi_object_type* object = (acpi_object_type*)buffer.pointer;
	status = B_OK;
	if (object->object_type == ACPI_TYPE_BUFFER
		&& object->buffer.length > 0 && object->buffer.buffer != NULL) {
		*_muted = ((uint8*)object->buffer.buffer)[0] > 0;
	} else if (object->object_type == ACPI_TYPE_INTEGER)
		*_muted = (object->integer.integer & 0xff) > 0;
	else
		status = B_BAD_DATA;
	free(buffer.pointer);
	return status;
}


static void
apply_acpi_mute(cs35l41_driver* driver, bool log)
{
	bool muted = false;
	status_t status = read_acpi_mute(driver->handle, &muted);
	if (status != B_OK) {
		if (log)
			TRACE_ALWAYS("no ACPI mute override (%s)\n", strerror(status));
		return;
	}
	if (log)
		TRACE_ALWAYS("ACPI mute override %s\n", muted ? "on" : "off");
	for (uint32 i = 0; i < driver->amp_count; i++)
		driver->amps[i]->SetAcpiMute(muted);
}


static void
acpi_notify(acpi_handle device, uint32 value, void* context)
{
	cs35l41_driver* driver = (cs35l41_driver*)context;
	if (value != NOTIFY_MUTE_CHANGED)
		return;
	atomic_set(&driver->notify_pending, 1);
	release_sem_etc(driver->events, 1, B_DO_NOT_RESCHEDULE);
}


//	#pragma mark - worker


static status_t
worker_thread(void* data)
{
	cs35l41_driver* driver = (cs35l41_driver*)data;
	while (true) {
		acquire_sem_etc(driver->events, 1, B_RELATIVE_TIMEOUT, POLL_INTERVAL);

		MutexLocker locker(driver->lock);
		if (driver->quit)
			break;
		if (atomic_get_and_set(&driver->notify_pending, 0) != 0)
			apply_acpi_mute(driver, true);
		for (uint32 i = 0; i < driver->amp_count; i++)
			driver->amps[i]->CheckFaults();
	}
	return B_OK;
}


//	#pragma mark - amplifier hooks


static cs35l41_driver* sDriver;
	// one CSC3551 device per machine; the hooks find their lock here


static status_t
amp_prepare(void* cookie, const audio_amp_format* format)
{
	MutexLocker locker(sDriver->lock);
	return ((Amplifier*)cookie)->Prepare(format != NULL ? format->rate : 0);
}


static status_t
amp_start(void* cookie)
{
	MutexLocker locker(sDriver->lock);
	return ((Amplifier*)cookie)->Start();
}


static void
amp_stop(void* cookie)
{
	MutexLocker locker(sDriver->lock);
	((Amplifier*)cookie)->Stop();
}


static void
amp_unprepare(void* cookie)
{
	MutexLocker locker(sDriver->lock);
	((Amplifier*)cookie)->Unprepare();
}


static void
amp_set_mute(void* cookie, bool mute)
{
	MutexLocker locker(sDriver->lock);
	((Amplifier*)cookie)->SetCodecMute(mute);
}


static int32
amp_set_gain(void* cookie, int32 gain)
{
	// Phase 1: the gain is the fixed cap (Amplifier.cpp), nothing raises it.
	return 0;
}


static const audio_amp_hooks kHooks = {
	sizeof(audio_amp_hooks),
	amp_prepare,
	amp_start,
	amp_stop,
	amp_unprepare,
	amp_set_mute,
	amp_set_gain,
	NULL,
	NULL,
};


//	#pragma mark - driver module


static float
cs35l41_support(device_node* parent)
{
	const char* bus;
	if (sDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false)
			!= B_OK || strcmp(bus, "i2c") != 0) {
		return -1;
	}

	const char* hid;
	if (sDeviceManager->get_attr_string(parent, ACPI_DEVICE_HID_ITEM, &hid,
			false) != B_OK || hid == NULL || strcmp(hid, ACPI_NAME_CS35L41) != 0) {
		return 0.0;
	}
	return 0.8;
}


static status_t
cs35l41_register_device(device_node* node)
{
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
			{ .string = "CS35L41 speaker amplifiers" }},
		{ B_DEVICE_FLAGS, B_UINT32_TYPE, { .ui32 = B_KEEP_DRIVER_LOADED }},
		{ NULL }
	};
	return sDeviceManager->register_node(node, CS35L41_DRIVER_NAME, attrs,
		NULL, NULL);
}


static void
free_driver(cs35l41_driver* driver)
{
	for (uint32 i = 0; i < driver->amp_count; i++)
		delete driver->amps[i];
	if (driver->events >= 0)
		delete_sem(driver->events);
	sGPIO->put_pin(driver->reset);
	sGPIO->put_pin(driver->speaker_id);
	mutex_destroy(&driver->lock);
	delete driver;
}


static status_t
cs35l41_init_driver(device_node* node, void** _cookie)
{
	if (sDriver != NULL) {
		TRACE_ALWAYS("a second CSC3551 device is not supported\n");
		return B_BUSY;
	}

	cs35l41_driver* driver = new(std::nothrow) cs35l41_driver;
	if (driver == NULL)
		return B_NO_MEMORY;
	driver->node = node;
	driver->handle = NULL;
	driver->subsystem = 0;
	driver->machine = NULL;
	driver->amp_count = 0;
	driver->events = -1;
	driver->worker = -1;
	driver->quit = false;
	driver->notify_pending = 0;
	driver->notify_installed = false;
	driver->reset = NULL;
	driver->speaker_id = NULL;
	mutex_init(&driver->lock, "cs35l41");

	// node -> I2C device node (ACPI handle) -> I2C bus node.
	device_node* deviceNode = sDeviceManager->get_parent_node(node);
	uint64 handle = 0;
	sDeviceManager->get_attr_uint64(deviceNode, ACPI_DEVICE_HANDLE_ITEM,
		&handle, false);
	driver->handle = (acpi_handle)(addr_t)handle;
	device_node* busNode = sDeviceManager->get_parent_node(deviceNode);
	sDeviceManager->put_node(deviceNode);

	driver->link.path_id = 0;
	sDeviceManager->get_attr_uint32(busNode, I2C_BUS_PATH_ID_ITEM,
		&driver->link.path_id, false);
	status_t status = sDeviceManager->get_driver(busNode,
		(driver_module_info**)&driver->link.bus,
		(void**)&driver->link.cookie);
	sDeviceManager->put_node(busNode);
	if (status != B_OK || driver->handle == NULL) {
		TRACE_ALWAYS("CSC3551 without I2C bus or ACPI handle\n");
		free_driver(driver);
		return B_DEVICE_NOT_FOUND;
	}

	crs_info crs = {};
	sACPI->walk_resources(driver->handle, (char*)"_CRS", parse_crs, &crs);
	driver->subsystem = read_subsystem(driver->handle);

	for (size_t i = 0; i < B_COUNT_OF(kMachines); i++) {
		if (kMachines[i].subsystem == driver->subsystem)
			driver->machine = &kMachines[i];
	}

	char addresses[32] = "";
	for (uint32 i = 0; i < crs.address_count; i++) {
		char one[8];
		snprintf(one, sizeof(one), " 0x%02x", crs.addresses[i]);
		strlcat(addresses, one, sizeof(addresses));
	}
	TRACE_ALWAYS("CSC3551 subsystem %08" B_PRIx32 " (%s), i2c bus %" B_PRIu32
		",%s, %" B_PRIu32 " gpios\n", driver->subsystem,
		driver->machine != NULL ? driver->machine->name : "unknown machine",
		driver->link.path_id, addresses, crs.gpio_count);

	if (driver->machine == NULL) {
		TRACE_ALWAYS("no boost data for this machine; amplifiers left off\n");
		free_driver(driver);
		return B_NOT_SUPPORTED;
	}
	if (crs.address_count < driver->machine->amp_count) {
		TRACE_ALWAYS("ACPI lists %" B_PRIu32 " amplifiers, expected %" B_PRIu32
			"\n", crs.address_count, driver->machine->amp_count);
	}

	// GPIO 0 is the shared reset line, GPIO 1 the speaker ID.
	// Spec section 2 step 1: one hardware reset, shared by every amplifier.
	// When the firmware already drives the line high (released), it is
	// only logged; the software reset of each amplifier follows anyway.
	status_t gpioStatus = sGPIO->get_acpi_pin(driver->handle, 0,
		&driver->reset);
	if (gpioStatus == B_OK) {
		gpio_pin_info info = {};
		info.size = sizeof(info);
		sGPIO->get_pin_info(driver->reset, &info);
		bool high = false;
		sGPIO->read_pin(driver->reset, &high);
		const char* action;
		if (info.connection != B_GPIO_CONNECTION_IO
			|| info.restriction == B_GPIO_RESTRICT_INPUT) {
			action = "not an output, not driven";
		} else if (info.output_enabled && high) {
			action = "released, not driven";
		} else {
			sGPIO->write_pin(driver->reset, false);
			sGPIO->set_output_enabled(driver->reset, true);
			snooze(2000);
			sGPIO->write_pin(driver->reset, true);
			snooze(2000);
			action = "pulsed low for 2 ms";
		}
		TRACE_ALWAYS("reset gpio %s pin %" B_PRIu32 ": output %d level %d, %s\n",
			info.controller, info.pin, info.output_enabled ? 1 : 0,
			high ? 1 : 0, action);
	} else {
		driver->reset = NULL;
		TRACE_ALWAYS("reset gpio: %s, not driven\n", strerror(gpioStatus));
	}

	gpioStatus = sGPIO->get_acpi_pin(driver->handle, 1, &driver->speaker_id);
	if (gpioStatus == B_OK) {
		bool high = false;
		sGPIO->read_pin(driver->speaker_id, &high);
		TRACE_ALWAYS("speaker id %d\n", high ? 1 : 0);
	} else {
		driver->speaker_id = NULL;
		TRACE_ALWAYS("speaker id gpio: %s\n", strerror(gpioStatus));
	}

	uint32 count = min_c(crs.address_count, (uint32)MAX_AMPS);
	for (uint32 i = 0; i < count; i++) {
		Amplifier* amp = new(std::nothrow) Amplifier(driver->link,
			*driver->machine, i, crs.addresses[i]);
		if (amp == NULL)
			break;
		driver->amps[driver->amp_count++] = amp;
		amp->PowerUp();
	}

	TRACE_ALWAYS("DSP bypass: no speaker protection, PCM gain capped at its"
		" lowest step\n");

	driver->events = create_sem(0, "cs35l41 events");
	apply_acpi_mute(driver, true);
	if (driver->events >= 0 && sACPI->install_notify_handler(driver->handle,
			ACPI_DEVICE_NOTIFY, acpi_notify, driver) == B_OK) {
		driver->notify_installed = true;
	}

	if (driver->events >= 0) {
		driver->worker = spawn_kernel_thread(worker_thread, "cs35l41 worker",
			B_NORMAL_PRIORITY, driver);
		if (driver->worker >= 0)
			resume_thread(driver->worker);
	}

	sDriver = driver;
	for (uint32 i = 0; i < driver->amp_count; i++) {
		driver->amp_ids[i] = -1;
		if (!driver->amps[i]->IsReady())
			continue;
		audio_amp_info info = {};
		info.size = sizeof(info);
		info.subsystem = driver->subsystem;
		info.channel = i;
		info.rates = Amplifier::SupportedRates();
		info.max_gain = 0;
		snprintf(info.name, sizeof(info.name), "cs35l41 %s",
			driver->amps[i]->Side());
		sAudioAmp->register_amp(&info, &kHooks, driver->amps[i],
			&driver->amp_ids[i]);
	}

	*_cookie = driver;
	return B_OK;
}


static void
cs35l41_uninit_driver(void* cookie)
{
	cs35l41_driver* driver = (cs35l41_driver*)cookie;

	// Unregister first: this waits for a hook that is running.
	for (uint32 i = 0; i < driver->amp_count; i++) {
		if (driver->amp_ids[i] >= 0)
			sAudioAmp->unregister_amp(driver->amp_ids[i]);
	}
	if (driver->notify_installed) {
		sACPI->remove_notify_handler(driver->handle, ACPI_DEVICE_NOTIFY,
			acpi_notify);
	}

	mutex_lock(&driver->lock);
	driver->quit = true;
	mutex_unlock(&driver->lock);
	if (driver->worker >= 0) {
		release_sem(driver->events);
		status_t result;
		wait_for_thread(driver->worker, &result);
	}

	for (uint32 i = 0; i < driver->amp_count; i++)
		driver->amps[i]->Shutdown();

	sDriver = NULL;
	free_driver(driver);
}


static status_t
std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
		case B_MODULE_UNINIT:
			return B_OK;
	}
	return B_ERROR;
}


static driver_module_info sCS35L41Driver = {
	{
		CS35L41_DRIVER_NAME,
		0,
		std_ops
	},

	cs35l41_support,
	cs35l41_register_device,
	cs35l41_init_driver,
	cs35l41_uninit_driver,
	NULL,	// register_child_devices
	NULL,	// rescan_child_devices
	NULL,	// device_removed
	NULL,	// suspend
	NULL,	// resume
};


module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info**)&sDeviceManager },
	{ B_ACPI_MODULE_NAME, (module_info**)&sACPI },
	{ AUDIO_AMP_MODULE_NAME, (module_info**)&sAudioAmp },
	{ B_GPIO_MODULE_NAME, (module_info**)&sGPIO },
	{}
};


module_info* modules[] = {
	(module_info*)&sCS35L41Driver,
	NULL
};
