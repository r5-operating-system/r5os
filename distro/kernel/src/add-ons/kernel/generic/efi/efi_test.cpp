/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


/*!	Test driver for generic/efi (docs/EFI-RUNTIME.md). Not in any package:
	build it with "jam -q efi_test" and copy it into a test image. Loading it
	(any open of /dev/misc/efi_test) logs the results to the syslog and the
	serial port with the prefix "efi_test:".
*/


#include <string.h>

#include <Drivers.h>
#include <EFI.h>
#include <KernelExport.h>


int32 api_version = B_CUR_DRIVER_API_VERSION;

static const efi_vendor_guid kGlobalVariable = { 0x8be4df61, 0x93ca, 0x11d2,
	{ 0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c } };
static const efi_vendor_guid kSmartAmp = { 0x53559579, 0x8753, 0x4f5c,
	{ 0x91, 0x30, 0xe8, 0x2a, 0xcf, 0xb8, 0xd8, 0x93 } };

static const char* sDevices[] = { "misc/efi_test", NULL };


static void
run_tests()
{
	efi_module_info* efi;
	status_t status = get_module(B_EFI_MODULE_NAME, (module_info**)&efi);
	if (status != B_OK) {
		dprintf("efi_test: get_module: %s\n", strerror(status));
		return;
	}

	// Size query: no buffer.
	size_t size = 0;
	status = efi->get_variable("BootOrder", &kGlobalVariable, NULL, &size,
		NULL);
	dprintf("efi_test: BootOrder size query: %s, %lu bytes\n",
		strerror(status), (unsigned long)size);

	uint8 data[256];
	uint32 attributes = 0;
	size = sizeof(data);
	bigtime_t start = system_time();
	status = efi->get_variable("BootOrder", &kGlobalVariable, data, &size,
		&attributes);
	bigtime_t duration = system_time() - start;
	dprintf("efi_test: BootOrder: %s, %lu bytes, attributes %#" B_PRIx32
		", %" B_PRIdBIGTIME " us,", strerror(status), (unsigned long)size,
		attributes, duration);
	for (size_t i = 0; status == B_OK && i + 1 < size && i < 16; i += 2)
		dprintf(" %04x", data[i] | (data[i + 1] << 8));
	dprintf("\n");

	size = sizeof(data);
	status = efi->get_variable("SmartAmpCalibrationData", &kSmartAmp, data,
		&size, NULL);
	dprintf("efi_test: SmartAmpCalibrationData: %s, %lu bytes\n",
		strerror(status), (unsigned long)size);

	size = sizeof(data);
	status = efi->get_variable("R5NoSuchVariable", &kGlobalVariable, data,
		&size, NULL);
	dprintf("efi_test: missing variable: %s\n", strerror(status));

	// A call with interrupts off must be refused, not run.
	size = sizeof(data);
	cpu_status state = disable_interrupts();
	status = efi->get_variable("BootOrder", &kGlobalVariable, data, &size,
		NULL);
	restore_interrupts(state);
	dprintf("efi_test: with interrupts off: %s\n", strerror(status));

	char name[256] = "";
	efi_vendor_guid vendor;
	memset(&vendor, 0, sizeof(vendor));
	int32 count = 0;
	while ((status = efi->get_next_variable_name(name, sizeof(name), &vendor))
			== B_OK && count < 500) {
		if (count < 12) {
			dprintf("efi_test: variable %s {%08" B_PRIx32 "-%04x-%04x}\n",
				name, vendor.data1, vendor.data2, vendor.data3);
		}
		count++;
	}
	dprintf("efi_test: %" B_PRId32 " variables, end: %s\n", count,
		strerror(status));

	put_module(B_EFI_MODULE_NAME);
}


status_t
init_hardware()
{
	return B_OK;
}


status_t
init_driver()
{
	run_tests();
	return B_OK;
}


void
uninit_driver()
{
}


const char**
publish_devices()
{
	return sDevices;
}


static status_t
test_open(const char* name, uint32 flags, void** _cookie)
{
	*_cookie = NULL;
	return B_OK;
}


static status_t
test_close(void* cookie)
{
	return B_OK;
}


static status_t
test_free(void* cookie)
{
	return B_OK;
}


static status_t
test_read(void* cookie, off_t position, void* buffer, size_t* _length)
{
	*_length = 0;
	return B_OK;
}


static status_t
test_write(void* cookie, off_t position, const void* buffer, size_t* _length)
{
	return B_NOT_ALLOWED;
}


static status_t
test_control(void* cookie, uint32 op, void* buffer, size_t length)
{
	return B_DEV_INVALID_IOCTL;
}


device_hooks*
find_device(const char* name)
{
	static device_hooks hooks = {
		test_open, test_close, test_free, test_control, test_read, test_write
	};
	return &hooks;
}
