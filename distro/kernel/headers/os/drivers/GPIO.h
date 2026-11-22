/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef _GPIO_H
#define _GPIO_H


/*!	General purpose I/O pins for kernel drivers.

	A driver finds a pin through the ACPI resource that names it (GpioIo or
	GpioInt in _CRS), then reads it or drives it. The controller part is
	per vendor behind this interface; drivers do not touch controller
	registers. The contract is in docs/GPIO.md. Calls are only ever added
	at the end of gpio_module_info.
*/


#include <ACPI.h>
#include <module.h>


#define B_GPIO_MODULE_NAME	"generic/gpio/v1"


typedef struct gpio_pin_cookie* gpio_pin;


enum {
	B_GPIO_CONNECTION_INTERRUPT	= 0,	// GpioInt
	B_GPIO_CONNECTION_IO		= 1,	// GpioIo
};

enum {
	B_GPIO_RESTRICT_NONE		= 0,
	B_GPIO_RESTRICT_INPUT		= 1,	// the firmware says: input only
	B_GPIO_RESTRICT_OUTPUT		= 2,	// the firmware says: output only
	B_GPIO_RESTRICT_PRESERVE	= 3,
};


typedef struct gpio_pin_info {
	uint32		size;				// sizeof(gpio_pin_info), set by the caller
	uint32		pin;				// pin number on its controller
	uint8		connection;			// B_GPIO_CONNECTION_*
	uint8		restriction;		// B_GPIO_RESTRICT_*
	bool		active_low;			// GpioInt polarity
	bool		output_enabled;		// current state of the pin
	char		controller[32];		// ACPI hardware ID of the controller
} gpio_pin_info;


typedef struct gpio_module_info {
	module_info	info;

	// The \a index'th GpioIo or GpioInt resource in the _CRS of \a device,
	// counted in _CRS order. B_NOT_SUPPORTED if no vendor part drives
	// that controller.
	status_t	(*get_acpi_pin)(acpi_handle device, uint32 index,
					gpio_pin* _pin);
	// A pin by controller path (the ResourceSource of the ACPI resource)
	// and pin number.
	status_t	(*get_pin)(const char* controllerPath, uint32 pin,
					gpio_pin* _pin);
	void		(*put_pin)(gpio_pin pin);

	status_t	(*get_pin_info)(gpio_pin pin, gpio_pin_info* info);
	status_t	(*read_pin)(gpio_pin pin, bool* _high);
	// Sets the output level. Does not change the output enable.
	status_t	(*write_pin)(gpio_pin pin, bool high);
	status_t	(*set_output_enabled)(gpio_pin pin, bool enabled);
} gpio_module_info;


#endif	// _GPIO_H
