/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


/*!	EFI runtime services for kernel drivers: generic/efi/v1 (<EFI.h>).

	The EFI boot loader hands over the runtime services table and the
	runtime ranges of the memory map (<boot/r5_efi_runtime.h>). The firmware
	runs in its own address space with those ranges identity mapped, one call
	at a time, with interrupts off. A firmware fault turns the module off for
	the rest of the boot. Without the hand-over (BIOS boot, safe mode,
	"efi_runtime false" in the kernel settings) the module does not load.
	Design and limits: docs/EFI-RUNTIME.md.
*/


#include <EFI.h>

#include <string.h>

#include <KernelExport.h>
#include <driver_settings.h>
#include <safemode_defs.h>

#include <boot_item.h>
#include <lock.h>
#include <util/AutoLock.h>
#include <vm/vm.h>

#include <efi/runtime-services.h>

#include "efi_arch.h"


//#define TRACE_EFI
#ifdef TRACE_EFI
#	define TRACE(x...) dprintf("efi: " x)
#else
#	define TRACE(x...) ;
#endif
#define ERROR(x...) dprintf("efi: " x)


#define EFI_RUNTIME_SERVICES_SIGNATURE	0x56524553544e5552ULL

static const size_t kMaxNameChars = 1024;
	// UCS-2 characters, terminator included
static const size_t kMaxDataSize = 64 * 1024;


static mutex sLock = MUTEX_INITIALIZER("efi runtime");
static bool sBroken = false;
static uint32 sSupported = 0;
static uint64 sGetVariable = 0;
static uint64 sGetNextVariableName = 0;

// The firmware only ever sees these buffers, never the caller's memory.
static uint16 sName[kMaxNameChars];
static uint8 sData[kMaxDataSize];
static efi_vendor_guid sGuid;
static uint64 sDataSize;
static uint64 sNameSize;
static uint32 sAttributes;


static status_t
to_status(uint64 efiStatus)
{
	switch (efiStatus) {
		case EFI_SUCCESS:
			return B_OK;
		case EFI_NOT_FOUND:
			return B_ENTRY_NOT_FOUND;
		case EFI_BUFFER_TOO_SMALL:
			return B_BUFFER_OVERFLOW;
		case EFI_INVALID_PARAMETER:
			return B_BAD_VALUE;
		case EFI_UNSUPPORTED:
			return B_NOT_SUPPORTED;
		case EFI_DEVICE_ERROR:
			return B_IO_ERROR;
		case EFI_SECURITY_VIOLATION:
			return B_NOT_ALLOWED;
		default:
			ERROR("unexpected firmware status %#" B_PRIx64 "\n", efiStatus);
			return B_ERROR;
	}
}


/*!	Calls the firmware. Must hold sLock. */
static status_t
call_firmware(uint64 function, uint64 arg1, uint64 arg2, uint64 arg3,
	uint64 arg4, uint64 arg5, uint64* _efiStatus)
{
	if (sBroken)
		return B_NOT_SUPPORTED;

	uint64 efiStatus = efi_arch_call(function, arg1, arg2, arg3, arg4, arg5);
	if (efiStatus == EFI_ARCH_FAULTED) {
		sBroken = true;
		ERROR("the firmware faulted; EFI runtime services are off until the "
			"next start\n");
		return B_NOT_SUPPORTED;
	}

	*_efiStatus = efiStatus;
	return B_OK;
}


/*!	UTF-8 to UCS-2 with surrogate pairs, terminated. */
static status_t
utf8_to_ucs2(const char* source, uint16* target, size_t targetChars)
{
	const uint8* in = (const uint8*)source;
	size_t count = 0;

	while (*in != 0) {
		uint32 c;
		if (in[0] < 0x80) {
			c = in[0];
			in++;
		} else if ((in[0] & 0xe0) == 0xc0 && (in[1] & 0xc0) == 0x80) {
			c = ((in[0] & 0x1f) << 6) | (in[1] & 0x3f);
			in += 2;
		} else if ((in[0] & 0xf0) == 0xe0 && (in[1] & 0xc0) == 0x80
			&& (in[2] & 0xc0) == 0x80) {
			c = ((in[0] & 0x0f) << 12) | ((in[1] & 0x3f) << 6) | (in[2] & 0x3f);
			in += 3;
		} else if ((in[0] & 0xf8) == 0xf0 && (in[1] & 0xc0) == 0x80
			&& (in[2] & 0xc0) == 0x80 && (in[3] & 0xc0) == 0x80) {
			c = ((in[0] & 0x07) << 18) | ((in[1] & 0x3f) << 12)
				| ((in[2] & 0x3f) << 6) | (in[3] & 0x3f);
			in += 4;
		} else
			return B_BAD_VALUE;

		if (c >= 0x10000) {
			if (c > 0x10ffff || count + 2 >= targetChars)
				return B_BAD_VALUE;
			c -= 0x10000;
			target[count++] = 0xd800 | (c >> 10);
			target[count++] = 0xdc00 | (c & 0x3ff);
		} else {
			if (count + 1 >= targetChars)
				return B_BAD_VALUE;
			target[count++] = c;
		}
	}

	target[count] = 0;
	return B_OK;
}


/*!	Terminated UCS-2 (at most "sourceChars") to UTF-8, terminated. */
static status_t
ucs2_to_utf8(const uint16* source, size_t sourceChars, char* target,
	size_t targetSize)
{
	size_t length = 0;
	for (size_t i = 0; i < sourceChars && source[i] != 0; i++) {
		uint32 c = source[i];
		if (c >= 0xd800 && c < 0xdc00 && i + 1 < sourceChars
			&& source[i + 1] >= 0xdc00 && source[i + 1] < 0xe000) {
			c = 0x10000 + ((c - 0xd800) << 10) + (source[i + 1] - 0xdc00);
			i++;
		}

		uint8 bytes[4];
		size_t size;
		if (c < 0x80) {
			bytes[0] = c;
			size = 1;
		} else if (c < 0x800) {
			bytes[0] = 0xc0 | (c >> 6);
			bytes[1] = 0x80 | (c & 0x3f);
			size = 2;
		} else if (c < 0x10000) {
			bytes[0] = 0xe0 | (c >> 12);
			bytes[1] = 0x80 | ((c >> 6) & 0x3f);
			bytes[2] = 0x80 | (c & 0x3f);
			size = 3;
		} else {
			bytes[0] = 0xf0 | (c >> 18);
			bytes[1] = 0x80 | ((c >> 12) & 0x3f);
			bytes[2] = 0x80 | ((c >> 6) & 0x3f);
			bytes[3] = 0x80 | (c & 0x3f);
			size = 4;
		}

		if (length + size >= targetSize)
			return B_BUFFER_OVERFLOW;
		memcpy(target + length, bytes, size);
		length += size;
	}

	if (targetSize == 0)
		return B_BUFFER_OVERFLOW;
	target[length] = '\0';
	return B_OK;
}


//	#pragma mark - module API


static status_t
efi_get_variable(const char* name, const efi_vendor_guid* vendor,
	void* buffer, size_t* _size, uint32* _attributes)
{
	if (name == NULL || name[0] == '\0' || vendor == NULL || _size == NULL
		|| (buffer == NULL && *_size != 0)) {
		return B_BAD_VALUE;
	}
	if ((sSupported & R5_EFI_RT_GET_VARIABLE) == 0)
		return B_NOT_SUPPORTED;

	MutexLocker locker(sLock);

	status_t status = utf8_to_ucs2(name, sName, kMaxNameChars);
	if (status != B_OK)
		return status;

	sGuid = *vendor;
	sAttributes = 0;
	sDataSize = *_size < kMaxDataSize ? *_size : kMaxDataSize;

	uint64 efiStatus;
	status = call_firmware(sGetVariable, (addr_t)sName, (addr_t)&sGuid,
		(addr_t)&sAttributes, (addr_t)&sDataSize, (addr_t)sData, &efiStatus);
	if (status != B_OK)
		return status;

	status = to_status(efiStatus);
	if (status == B_OK) {
		if (sDataSize > kMaxDataSize || sDataSize > *_size) {
			// The firmware claims more than it was given room for.
			ERROR("GetVariable returned %" B_PRIu64 " bytes for a %" B_PRIuSIZE
				" byte buffer\n", sDataSize, *_size);
			return B_ERROR;
		}
		memcpy(buffer, sData, sDataSize);
		*_size = sDataSize;
		if (_attributes != NULL)
			*_attributes = sAttributes;
	} else if (status == B_BUFFER_OVERFLOW)
		*_size = sDataSize;

	TRACE("get_variable(\"%s\"): %s, %" B_PRIuSIZE " bytes\n", name,
		strerror(status), *_size);
	return status;
}


static status_t
efi_get_next_variable_name(char* name, size_t nameSize,
	efi_vendor_guid* vendor)
{
	if (name == NULL || nameSize == 0 || vendor == NULL)
		return B_BAD_VALUE;
	if ((sSupported & R5_EFI_RT_GET_NEXT_VARIABLE_NAME) == 0)
		return B_NOT_SUPPORTED;

	MutexLocker locker(sLock);

	// The name must be terminated inside "nameSize".
	if (strnlen(name, nameSize) == nameSize)
		return B_BAD_VALUE;

	status_t status = utf8_to_ucs2(name, sName, kMaxNameChars);
	if (status != B_OK)
		return status;

	if (name[0] != '\0')
		sGuid = *vendor;
	else
		memset(&sGuid, 0, sizeof(sGuid));
	sNameSize = sizeof(sName);

	uint64 efiStatus;
	status = call_firmware(sGetNextVariableName, (addr_t)&sNameSize,
		(addr_t)sName, (addr_t)&sGuid, 0, 0, &efiStatus);
	if (status != B_OK)
		return status;

	status = to_status(efiStatus);
	if (status != B_OK)
		return status;

	if (sNameSize > sizeof(sName))
		return B_ERROR;

	status = ucs2_to_utf8(sName, sNameSize / sizeof(uint16), name, nameSize);
	if (status == B_OK)
		*vendor = sGuid;
	return status;
}


//	#pragma mark - module


static bool
efi_disabled_by_settings()
{
	bool disabled = false;
	void* handle = load_driver_settings("kernel");
	if (handle != NULL) {
		disabled = !get_driver_boolean_parameter(handle, "efi_runtime", true,
			true);
		unload_driver_settings(handle);
	}

	handle = load_driver_settings(B_SAFEMODE_DRIVER_SETTINGS);
	if (handle != NULL) {
		if (get_driver_boolean_parameter(handle, "safemode", false, false)
			|| get_driver_boolean_parameter(handle, "disable_efi_runtime",
				false, false)) {
			disabled = true;
		}
		unload_driver_settings(handle);
	}

	return disabled;
}


static status_t
efi_init()
{
	size_t size = 0;
	const r5_efi_runtime_args* args = (const r5_efi_runtime_args*)
		get_boot_item(R5_EFI_RUNTIME_BOOT_ITEM, &size);
	if (args == NULL) {
		TRACE("no hand-over from the boot loader (BIOS boot)\n");
		return B_ENTRY_NOT_FOUND;
	}

	if (size < sizeof(r5_efi_runtime_args) || args->size > size
		|| args->magic != R5_EFI_RUNTIME_MAGIC
		|| args->version != R5_EFI_RUNTIME_VERSION) {
		ERROR("hand-over block is not valid\n");
		return B_BAD_DATA;
	}
	if ((args->flags & R5_EFI_RUNTIME_COMPLETE) == 0
		|| (args->flags & R5_EFI_RUNTIME_IDENTITY) == 0) {
		ERROR("runtime map incomplete or not identity mapped (flags %#"
			B_PRIx32 ", SetVirtualAddressMap %#" B_PRIx64 ")\n", args->flags,
			args->set_virtual_status);
		return B_NOT_SUPPORTED;
	}
	if (args->descriptor_offset + (uint64)args->descriptor_count
			* args->descriptor_size > args->size
		|| ((args->flags & R5_EFI_RUNTIME_ATTRIBUTES) != 0
			&& args->attribute_offset + (uint64)args->attribute_count
				* args->attribute_descriptor_size > args->size)) {
		ERROR("hand-over block is truncated\n");
		return B_BAD_DATA;
	}

	if (efi_disabled_by_settings()) {
		dprintf("efi: runtime services turned off in the settings\n");
		return B_NOT_ALLOWED;
	}

	// The table sits in runtime data, which the kernel does not map; read
	// it through the physical address (identity mapped for the firmware).
	efi_runtime_services table;
	if (vm_memcpy_from_physical(&table, args->runtime_services, sizeof(table),
			false) != B_OK
		|| table.Hdr.Signature != EFI_RUNTIME_SERVICES_SIGNATURE
		|| table.Hdr.HeaderSize < sizeof(table)) {
		ERROR("runtime services table at %#" B_PRIx64 " is not valid\n",
			args->runtime_services);
		return B_BAD_DATA;
	}

	sGetVariable = (addr_t)table.GetVariable;
	sGetNextVariableName = (addr_t)table.GetNextVariableName;
	sSupported = args->supported;
	if (sGetVariable == 0)
		sSupported &= ~R5_EFI_RT_GET_VARIABLE;
	if (sGetNextVariableName == 0)
		sSupported &= ~R5_EFI_RT_GET_NEXT_VARIABLE_NAME;
	if ((sSupported & (R5_EFI_RT_GET_VARIABLE
			| R5_EFI_RT_GET_NEXT_VARIABLE_NAME)) == 0) {
		ERROR("the firmware offers no variable services after boot\n");
		return B_NOT_SUPPORTED;
	}

	status_t status = efi_arch_init(args);
	if (status != B_OK) {
		ERROR("no runtime address space: %s\n", strerror(status));
		return status;
	}

	sBroken = false;
	dprintf("efi: runtime services ready, revision %" B_PRIu32 ".%" B_PRIu32
		", %" B_PRIu32 " ranges, attributes table %s, services %#" B_PRIx32
		"\n", table.Hdr.Revision >> 16, table.Hdr.Revision & 0xffff,
		args->descriptor_count,
		(args->flags & R5_EFI_RUNTIME_ATTRIBUTES) != 0 ? "yes" : "no",
		sSupported);
	return B_OK;
}


static status_t
efi_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			return efi_init();
		case B_MODULE_UNINIT:
			efi_arch_uninit();
			return B_OK;
		default:
			return B_ERROR;
	}
}


static efi_module_info sEFIModule = {
	{
		B_EFI_MODULE_NAME,
		B_KEEP_LOADED,
		efi_std_ops
	},

	efi_get_variable,
	efi_get_next_variable_name
};


module_info* modules[] = {
	(module_info*)&sEFIModule,
	NULL
};
