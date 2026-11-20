/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


/*!	GPIO pins for kernel drivers (GPIO.h, docs/GPIO.md).

	The module resolves an ACPI GpioIo or GpioInt resource to a pin on its
	controller and hands the register work to a vendor part. Only AMD FCH
	controllers have a vendor part so far; Intel follows with the X1.
*/


#include <GPIO.h>

#include <new>
#include <stdlib.h>
#include <string.h>

#include <KernelExport.h>

extern "C" {
#	include "acpi.h"
}


#define TRACE_ALWAYS(a...)	dprintf("gpio: " a)


struct gpio_vendor {
	const char*		name;
	const char*		hids[4];
	// Byte offset of a pin's register from the controller's MMIO base.
	uint32			(*register_offset)(uint32 pin);
	bool			(*read)(volatile uint32* reg);
	void			(*write)(volatile uint32* reg, bool high);
	void			(*set_output)(volatile uint32* reg, bool enabled);
	bool			(*is_output)(volatile uint32* reg);
};


struct gpio_pin_cookie {
	area_id				area;
	volatile uint32*	reg;
	const gpio_vendor*	vendor;
	gpio_pin_info		info;
};


static acpi_module_info* sACPI;


//	#pragma mark - AMD FCH


// One 32-bit register per pin. Pin state bit 16, output value bit 22,
// output enable bit 23. Same layout R5's i2c_hid reads for its interrupt
// line (InputReadyLine.cpp).
#define AMD_PIN_STATE		(1u << 16)
#define AMD_OUTPUT_VALUE	(1u << 22)
#define AMD_OUTPUT_ENABLE	(1u << 23)


static uint32
amd_register_offset(uint32 pin)
{
	return pin * 4;
}


static bool
amd_read(volatile uint32* reg)
{
	return (*reg & AMD_PIN_STATE) != 0;
}


static void
amd_write(volatile uint32* reg, bool high)
{
	uint32 value = *reg;
	if (high)
		value |= AMD_OUTPUT_VALUE;
	else
		value &= ~AMD_OUTPUT_VALUE;
	*reg = value;
}


static void
amd_set_output(volatile uint32* reg, bool enabled)
{
	uint32 value = *reg;
	if (enabled)
		value |= AMD_OUTPUT_ENABLE;
	else
		value &= ~AMD_OUTPUT_ENABLE;
	*reg = value;
}


static bool
amd_is_output(volatile uint32* reg)
{
	return (*reg & AMD_OUTPUT_ENABLE) != 0;
}


static const gpio_vendor kVendors[] = {
	{
		"AMD FCH",
		{ "AMD0030", "AMDI0030", NULL, NULL },
		amd_register_offset,
		amd_read,
		amd_write,
		amd_set_output,
		amd_is_output,
	},
};


//	#pragma mark - ACPI


struct controller_memory {
	uint32	base;
	uint32	length;
};


static acpi_status
find_memory(ACPI_RESOURCE* resource, void* context)
{
	controller_memory* memory = (controller_memory*)context;
	if (memory->base != 0)
		return AE_OK;
	if (resource->Type == ACPI_RESOURCE_TYPE_FIXED_MEMORY32) {
		memory->base = resource->Data.FixedMemory32.Address;
		memory->length = resource->Data.FixedMemory32.AddressLength;
	} else if (resource->Type == ACPI_RESOURCE_TYPE_MEMORY32) {
		memory->base = resource->Data.Memory32.Minimum;
		memory->length = resource->Data.Memory32.AddressLength;
	}
	return AE_OK;
}


struct gpio_search {
	uint32			index;
	uint32			seen;
	bool			found;
	char			controller[128];
	uint32			pin;
	uint8			connection;
	uint8			restriction;
	bool			active_low;
};


static acpi_status
find_gpio(ACPI_RESOURCE* resource, void* context)
{
	gpio_search* search = (gpio_search*)context;
	if (search->found || resource->Type != ACPI_RESOURCE_TYPE_GPIO)
		return AE_OK;
	if (search->seen++ != search->index)
		return AE_OK;

	ACPI_RESOURCE_GPIO& gpio = resource->Data.Gpio;
	if (gpio.PinTableLength == 0 || gpio.ResourceSource.StringPtr == NULL)
		return AE_OK;

	strlcpy(search->controller, gpio.ResourceSource.StringPtr,
		sizeof(search->controller));
	search->pin = gpio.PinTable[0];
	search->connection = gpio.ConnectionType == ACPI_RESOURCE_GPIO_TYPE_IO
		? B_GPIO_CONNECTION_IO : B_GPIO_CONNECTION_INTERRUPT;
	search->restriction = gpio.IoRestriction;
	search->active_low = gpio.Polarity == ACPI_ACTIVE_LOW;
	search->found = true;
	return AE_OK;
}


//	#pragma mark - module API


static status_t
get_pin(const char* controllerPath, uint32 pin, gpio_pin* _pin)
{
	if (controllerPath == NULL || _pin == NULL)
		return B_BAD_VALUE;

	char* hid = NULL;
	if (sACPI->get_device_info(controllerPath, &hid, NULL, 0, NULL, NULL)
			!= B_OK || hid == NULL) {
		return B_ENTRY_NOT_FOUND;
	}

	const gpio_vendor* vendor = NULL;
	for (size_t i = 0; i < B_COUNT_OF(kVendors) && vendor == NULL; i++) {
		for (size_t j = 0; kVendors[i].hids[j] != NULL; j++) {
			if (strcmp(hid, kVendors[i].hids[j]) == 0) {
				vendor = &kVendors[i];
				break;
			}
		}
	}
	if (vendor == NULL) {
		TRACE_ALWAYS("controller %s (%s) not supported\n", controllerPath,
			hid);
		free(hid);
		return B_NOT_SUPPORTED;
	}

	gpio_pin_cookie* cookie = new(std::nothrow) gpio_pin_cookie;
	if (cookie == NULL) {
		free(hid);
		return B_NO_MEMORY;
	}
	memset(&cookie->info, 0, sizeof(cookie->info));
	cookie->info.size = sizeof(cookie->info);
	cookie->info.pin = pin;
	cookie->info.connection = B_GPIO_CONNECTION_IO;
	strlcpy(cookie->info.controller, hid, sizeof(cookie->info.controller));
	cookie->vendor = vendor;
	cookie->area = -1;
	cookie->reg = NULL;
	free(hid);

	acpi_handle controller;
	controller_memory memory = { 0, 0 };
	if (sACPI->get_handle(NULL, controllerPath, &controller) != B_OK
		|| sACPI->walk_resources(controller, (char*)"_CRS", find_memory,
			&memory) != B_OK || memory.base == 0) {
		delete cookie;
		return B_ENTRY_NOT_FOUND;
	}

	uint32 offset = vendor->register_offset(pin);
	if (offset + sizeof(uint32) > memory.length) {
		delete cookie;
		return B_BAD_VALUE;
	}

	phys_addr_t address = (phys_addr_t)memory.base + offset;
	phys_addr_t page = address & ~(phys_addr_t)(B_PAGE_SIZE - 1);
	void* mapped;
	cookie->area = map_physical_memory("gpio pin", page, B_PAGE_SIZE,
		B_ANY_KERNEL_ADDRESS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
		&mapped);
	if (cookie->area < 0) {
		status_t status = cookie->area;
		delete cookie;
		return status;
	}
	cookie->reg = (volatile uint32*)((addr_t)mapped + (address - page));

	*_pin = cookie;
	return B_OK;
}


static status_t
get_acpi_pin(acpi_handle device, uint32 index, gpio_pin* _pin)
{
	if (device == NULL || _pin == NULL)
		return B_BAD_VALUE;

	gpio_search search = {};
	search.index = index;
	if (sACPI->walk_resources(device, (char*)"_CRS", find_gpio, &search)
			!= B_OK || !search.found) {
		return B_ENTRY_NOT_FOUND;
	}

	status_t status = get_pin(search.controller, search.pin, _pin);
	if (status != B_OK)
		return status;

	gpio_pin_info& info = (*_pin)->info;
	info.connection = search.connection;
	info.restriction = search.restriction;
	info.active_low = search.active_low;
	return B_OK;
}


static void
put_pin(gpio_pin pin)
{
	if (pin == NULL)
		return;
	if (pin->area >= 0)
		delete_area(pin->area);
	delete pin;
}


static status_t
get_pin_info(gpio_pin pin, gpio_pin_info* info)
{
	if (pin == NULL || info == NULL || info->size == 0)
		return B_BAD_VALUE;

	gpio_pin_info current = pin->info;
	current.output_enabled = pin->vendor->is_output(pin->reg);
	memcpy(info, &current, min_c(info->size, sizeof(current)));
	info->size = min_c(info->size, sizeof(current));
	return B_OK;
}


static status_t
read_pin(gpio_pin pin, bool* _high)
{
	if (pin == NULL || _high == NULL)
		return B_BAD_VALUE;
	*_high = pin->vendor->read(pin->reg);
	return B_OK;
}


static status_t
write_pin(gpio_pin pin, bool high)
{
	if (pin == NULL)
		return B_BAD_VALUE;
	if (pin->info.restriction == B_GPIO_RESTRICT_INPUT)
		return B_NOT_ALLOWED;
	pin->vendor->write(pin->reg, high);
	return B_OK;
}


static status_t
set_output_enabled(gpio_pin pin, bool enabled)
{
	if (pin == NULL)
		return B_BAD_VALUE;
	if (enabled && pin->info.restriction == B_GPIO_RESTRICT_INPUT)
		return B_NOT_ALLOWED;
	pin->vendor->set_output(pin->reg, enabled);
	return B_OK;
}


static status_t
std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			return get_module(B_ACPI_MODULE_NAME, (module_info**)&sACPI);
		case B_MODULE_UNINIT:
			put_module(B_ACPI_MODULE_NAME);
			return B_OK;
	}
	return B_ERROR;
}


static gpio_module_info sGpioModule = {
	{
		B_GPIO_MODULE_NAME,
		0,
		std_ops
	},

	get_acpi_pin,
	get_pin,
	put_pin,
	get_pin_info,
	read_pin,
	write_pin,
	set_output_enabled,
};


module_info* modules[] = {
	(module_info*)&sGpioModule,
	NULL
};
