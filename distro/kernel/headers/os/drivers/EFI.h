/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef _EFI_H
#define _EFI_H


/*!	EFI runtime services for kernel drivers.

	INTERFACE ONLY: the module that implements it is not built yet. A
	driver loads it with get_module() and must work without it (get_module()
	fails today). When the module lands, drivers that use this header work
	without a change. The contract is in docs/EFI.md. Calls are only ever
	added at the end of efi_module_info.
*/


#include <module.h>


#define B_EFI_MODULE_NAME	"generic/efi/v1"


// An EFI vendor GUID, in the field layout of the UEFI specification.
typedef struct efi_vendor_guid {
	uint32		data1;
	uint16		data2;
	uint16		data3;
	uint8		data4[8];
} efi_vendor_guid;


// Variable attributes (UEFI specification).
enum {
	B_EFI_VARIABLE_NON_VOLATILE			= 0x00000001,
	B_EFI_VARIABLE_BOOTSERVICE_ACCESS	= 0x00000002,
	B_EFI_VARIABLE_RUNTIME_ACCESS		= 0x00000004,
};


typedef struct efi_module_info {
	module_info	info;

	// Reads variable "name" (UTF-8) of "vendor" into "buffer". "*_size" is
	// the buffer size on entry and the variable size on return.
	// B_BUFFER_OVERFLOW when the buffer is too small (the size is still
	// set); "buffer" may be NULL with "*_size" 0 to ask for the size.
	// B_ENTRY_NOT_FOUND when the variable does not exist. "_attributes"
	// may be NULL.
	status_t	(*get_variable)(const char* name,
					const efi_vendor_guid* vendor, void* buffer, size_t* _size,
					uint32* _attributes);

	// Steps through the variable names: start with an empty "name".
	// B_ENTRY_NOT_FOUND after the last one.
	status_t	(*get_next_variable_name)(char* name, size_t nameSize,
					efi_vendor_guid* vendor);
} efi_module_info;


#endif	// _EFI_H
