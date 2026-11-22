/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * CPPC through the ACPI _CPC registers (docs/POWER.md, section 4), for AMD
 * processors without the CPPC MSR. Written from the ACPI specification
 * (6.x): the _CPC package (section 8.4.6.1), the generic register
 * descriptor, and the generic Platform Communications Channel (PCCT
 * subspace types 0, 1 and 2, chapter 14).
 *
 * Only registers in system memory or in a generic PCC subspace are used.
 * Anything else (system I/O, functional fixed hardware, the extended PCC
 * types) makes the init fail: no profile, the menus are gray, and the
 * processor runs as the firmware started it. Nothing here was tested on a
 * computer that needs it (CHANGELOG.md, 22 Nov 2026).
 */


#include "amd_cppc_acpi.h"

#include <stdlib.h>
#include <string.h>

#include <ACPI.h>
#include <device_manager.h>
#include <KernelExport.h>

#include <arch_cpu.h>
#include <cpu.h>
#include <kernel.h>
#include <lock.h>
#include <r5_power_profile.h>
#include <smp.h>
#include <util/AutoLock.h>


// _CPC package entries (ACPI 6.x, table 8.31)
enum {
	kCPCNumEntries = 0,
	kCPCRevision,
	kCPCHighestPerf,
	kCPCNominalPerf,
	kCPCLowestNonlinearPerf,
	kCPCLowestPerf,
	kCPCGuaranteedPerfReg,
	kCPCDesiredPerfReg,
	kCPCMinPerfReg,
	kCPCMaxPerfReg,
	kCPCPerfReductionToleranceReg,
	kCPCTimeWindowReg,
	kCPCCounterWraparoundTime,
	kCPCReferencePerfCounterReg,
	kCPCDeliveredPerfCounterReg,
	kCPCPerfLimitedReg,
	kCPCEnableReg,
	kCPCAutonomousSelectionEnable,
	kCPCAutonomousActivityWindowReg,
	kCPCEnergyPerfPreferenceReg,

	kCPCMinimumEntries = kCPCEnableReg + 1,
	kCPCEntryCount = kCPCEnergyPerfPreferenceReg + 1
};

// Address spaces of a generic register descriptor
enum {
	kSpaceSystemMemory	= 0x00,
	kSpaceSystemIO		= 0x01,
	kSpacePCC			= 0x0a
};

// The energy preference values the AMD documentation names (arch_cpu.h);
// the same ones the CPPC MSR path writes.
static const uint32 kProfileEPP[R5_POWER_PROFILE_COUNT] = {
	AMD_CPPC_EPP_BALANCE_POWERSAVE,
	AMD_CPPC_EPP_BALANCE_PERFORMANCE,
	AMD_CPPC_EPP_PERFORMANCE
};

static const uint32 kMaxPCCSubspaces = 4;
static const uint32 kMaxMappings = 16;
static const bigtime_t kPCCMinimumTimeout = 5000;
static const bigtime_t kPCCTimeoutFactor = 10;
	// a command may take this many times the subspace's nominal latency


struct generic_register {
	bool	present;
	bool	isInteger;
	uint64	integer;
	uint8	space;
	uint8	bitWidth;
	uint8	bitOffset;
	uint8	accessSize;
		// for a PCC register: the subspace ID
	uint64	address;
};

struct cpc_info {
	bool				valid;
	generic_register	entries[kCPCEntryCount];
	uint32				highest;
	uint32				nominal;
	uint32				lowestNonlinear;
};

struct pcc_subspace {
	bool		used;
	bool		ready;
	uint8		id;
	uint64		base;
	uint64		length;
	uint8*		shared;
	area_id		area;
	generic_register doorbell;
	uint64		preserve;
	uint64		write;
	uint32		nominalLatency;
		// microseconds
};

struct memory_mapping {
	phys_addr_t	page;
	uint8*		address;
	area_id		area;
};

// the generic register descriptor as it sits in a _CPC buffer
struct acpi_generic_register_descriptor {
	uint8	descriptor;
	uint16	length;
	uint8	spaceID;
	uint8	bitWidth;
	uint8	bitOffset;
	uint8	accessSize;
	uint64	address;
} __attribute__((packed));


static cpc_info* sCPC;
static int32 sCPUCount;
static pcc_subspace sPCC[kMaxPCCSubspaces];
static memory_mapping sMappings[kMaxMappings];
static int32 sMappingCount;
static mutex sLock = MUTEX_INITIALIZER("amd cppc acpi");
static acpi_module_info* sACPI;
static device_manager_info* sDeviceManager;


//	#pragma mark - system memory


static uint8*
map_register(uint64 address, uint32 size)
{
	phys_addr_t page = ROUNDDOWN(address, B_PAGE_SIZE);
	if (ROUNDDOWN(address + size - 1, B_PAGE_SIZE) != page)
		return NULL;

	for (int32 i = 0; i < sMappingCount; i++) {
		if (sMappings[i].page == page)
			return sMappings[i].address + (address - page);
	}
	if (sMappingCount >= (int32)kMaxMappings)
		return NULL;

	void* virtualAddress;
	area_id area = map_physical_memory("cppc register", page, B_PAGE_SIZE,
		B_ANY_KERNEL_ADDRESS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
		&virtualAddress);
	if (area < 0)
		return NULL;

	memory_mapping& mapping = sMappings[sMappingCount++];
	mapping.page = page;
	mapping.address = (uint8*)virtualAddress;
	mapping.area = area;
	return mapping.address + (address - page);
}


static uint32
access_bytes(const generic_register& reg)
{
	// The access size field: 1 byte, 2 word, 3 dword, 4 qword; 0 means
	// the bit width decides.
	switch (reg.accessSize) {
		case 1: return 1;
		case 2: return 2;
		case 3: return 4;
		case 4: return 8;
	}
	uint32 bits = reg.bitWidth + reg.bitOffset;
	if (bits <= 8)
		return 1;
	if (bits <= 16)
		return 2;
	if (bits <= 32)
		return 4;
	return 8;
}


static uint64
field_mask(const generic_register& reg)
{
	if (reg.bitWidth == 0 || reg.bitWidth >= 64)
		return ~(uint64)0;
	return ((uint64)1 << reg.bitWidth) - 1;
}


static uint64
read_raw(uint8* address, uint32 bytes)
{
	switch (bytes) {
		case 1: return *(volatile uint8*)address;
		case 2: return *(volatile uint16*)address;
		case 4: return *(volatile uint32*)address;
		default: return *(volatile uint64*)address;
	}
}


static void
write_raw(uint8* address, uint32 bytes, uint64 value)
{
	switch (bytes) {
		case 1: *(volatile uint8*)address = (uint8)value; break;
		case 2: *(volatile uint16*)address = (uint16)value; break;
		case 4: *(volatile uint32*)address = (uint32)value; break;
		default: *(volatile uint64*)address = value; break;
	}
}


static uint64
read_field(uint8* address, const generic_register& reg, uint32 bytes)
{
	return (read_raw(address, bytes) >> reg.bitOffset) & field_mask(reg);
}


static void
write_field(uint8* address, const generic_register& reg, uint32 bytes,
	uint64 value)
{
	uint64 mask = field_mask(reg) << reg.bitOffset;
	uint64 raw = read_raw(address, bytes);
	raw = (raw & ~mask) | ((value << reg.bitOffset) & mask);
	write_raw(address, bytes, raw);
}


//	#pragma mark - PCC


static pcc_subspace*
find_subspace(uint8 id)
{
	for (uint32 i = 0; i < kMaxPCCSubspaces; i++) {
		if (sPCC[i].used && sPCC[i].id == id)
			return &sPCC[i];
	}
	return NULL;
}


static bool
ring_doorbell(pcc_subspace& subspace)
{
	const generic_register& doorbell = subspace.doorbell;
	uint32 bytes = access_bytes(doorbell);
	if (doorbell.space == kSpaceSystemMemory) {
		uint8* address = map_register(doorbell.address, bytes);
		if (address == NULL)
			return false;
		uint64 value = read_raw(address, bytes);
		write_raw(address, bytes,
			(value & subspace.preserve) | subspace.write);
		return true;
	}
	if (doorbell.space == kSpaceSystemIO) {
		uint16 port = (uint16)doorbell.address;
		switch (bytes) {
			case 1:
				out8((uint8)((in8(port) & subspace.preserve)
					| subspace.write), port);
				return true;
			case 2:
				out16((uint16)((in16(port) & subspace.preserve)
					| subspace.write), port);
				return true;
			case 4:
				out32((uint32)((in32(port) & subspace.preserve)
					| subspace.write), port);
				return true;
		}
	}
	return false;
}


static bool
wait_command_complete(pcc_subspace& subspace)
{
	volatile uint16* status = (volatile uint16*)(subspace.shared + 6);
	bigtime_t timeout = max_c(kPCCMinimumTimeout,
		(bigtime_t)subspace.nominalLatency * kPCCTimeoutFactor);
	bigtime_t end = system_time() + timeout;
	while ((*status & 0x1) == 0) {
		if (system_time() > end)
			return false;
		spin(10);
	}
	// bit 2: the platform reports an error
	return (*status & 0x4) == 0;
}


/*!	Runs one PCC command (0 read, 1 write) on \a subspace. The caller holds
	sLock. The OS writes the signature and the command, clears "command
	complete", rings the doorbell and waits for the platform.
*/
static bool
pcc_command(pcc_subspace& subspace, uint16 command)
{
	if (!subspace.ready)
		return false;

	if (!wait_command_complete(subspace))
		return false;

	*(volatile uint32*)subspace.shared = 0x50434300 | subspace.id;
	*(volatile uint16*)(subspace.shared + 4) = command;
	*(volatile uint16*)(subspace.shared + 6) = 0;
	memory_write_barrier();

	if (!ring_doorbell(subspace))
		return false;

	return wait_command_complete(subspace);
}


/*!	Reads the subspaces the _CPC registers name from the PCCT and maps their
	shared memory. Only the generic types (0, 1 and 2) are used.
*/
static status_t
setup_pcc()
{
	bool needed = false;
	for (uint32 i = 0; i < kMaxPCCSubspaces; i++)
		needed |= sPCC[i].used;
	if (!needed)
		return B_OK;

	uint8* table = NULL;
	if (sACPI->get_table("PCCT", 0, (void**)&table) != B_OK || table == NULL) {
		dprintf("R5 CPPC: _CPC names PCC registers, but there is no PCCT\n");
		return B_NOT_SUPPORTED;
	}

	uint32 tableLength;
	memcpy(&tableLength, table + 4, sizeof(tableLength));

	// header (36 bytes), flags (4) and reserved (8), then the subspaces
	uint32 offset = 48;
	uint8 index = 0;
	while (offset + 2 <= tableLength) {
		uint8 type = table[offset];
		uint8 length = table[offset + 1];
		if (length < 2 || offset + length > tableLength)
			break;

		pcc_subspace* subspace = find_subspace(index);
		if (subspace != NULL) {
			if (type > 2 || length < 62) {
				dprintf("R5 CPPC: PCC subspace %u has type %u, not "
					"supported\n", index, type);
				return B_NOT_SUPPORTED;
			}
			const uint8* entry = table + offset;
			memcpy(&subspace->base, entry + 8, sizeof(uint64));
			memcpy(&subspace->length, entry + 16, sizeof(uint64));
			subspace->doorbell.present = true;
			subspace->doorbell.space = entry[24];
			subspace->doorbell.bitWidth = entry[25];
			subspace->doorbell.bitOffset = entry[26];
			subspace->doorbell.accessSize = entry[27];
			memcpy(&subspace->doorbell.address, entry + 28, sizeof(uint64));
			memcpy(&subspace->preserve, entry + 36, sizeof(uint64));
			memcpy(&subspace->write, entry + 44, sizeof(uint64));
			memcpy(&subspace->nominalLatency, entry + 52, sizeof(uint32));

			if (subspace->length < 8 || subspace->length > 64 * B_PAGE_SIZE
				|| (subspace->doorbell.space != kSpaceSystemMemory
					&& subspace->doorbell.space != kSpaceSystemIO)) {
				dprintf("R5 CPPC: PCC subspace %u cannot be used\n", index);
				return B_NOT_SUPPORTED;
			}

			phys_addr_t start = ROUNDDOWN(subspace->base, B_PAGE_SIZE);
			size_t size = ROUNDUP(subspace->base + subspace->length - start,
				B_PAGE_SIZE);
			void* address;
			subspace->area = map_physical_memory("cppc pcc", start, size,
				B_ANY_KERNEL_ADDRESS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
				&address);
			if (subspace->area < 0)
				return subspace->area;
			subspace->shared = (uint8*)address + (subspace->base - start);
			subspace->ready = true;
		}

		offset += length;
		index++;
	}

	for (uint32 i = 0; i < kMaxPCCSubspaces; i++) {
		if (sPCC[i].used && !sPCC[i].ready) {
			dprintf("R5 CPPC: PCC subspace %u is not in the PCCT\n",
				sPCC[i].id);
			return B_NOT_SUPPORTED;
		}
	}
	return B_OK;
}


//	#pragma mark - registers


static uint8*
register_address(const generic_register& reg, uint32 bytes)
{
	if (reg.space == kSpaceSystemMemory)
		return map_register(reg.address, bytes);

	if (reg.space == kSpacePCC) {
		pcc_subspace* subspace = find_subspace(reg.accessSize);
		// the address is the offset into the communication space, which
		// follows the 8-byte header
		if (subspace == NULL || !subspace->ready
			|| 8 + reg.address + bytes > subspace->length)
			return NULL;
		return subspace->shared + 8 + reg.address;
	}
	return NULL;
}


static uint32
pcc_bytes(const generic_register& reg)
{
	// In a PCC register the access size field holds the subspace ID, so
	// the bit width alone says how wide it is.
	uint32 bits = reg.bitWidth + reg.bitOffset;
	if (bits <= 8)
		return 1;
	if (bits <= 16)
		return 2;
	if (bits <= 32)
		return 4;
	return 8;
}


static uint32
register_bytes(const generic_register& reg)
{
	return reg.space == kSpacePCC ? pcc_bytes(reg) : access_bytes(reg);
}


static bool
register_usable(const generic_register& reg)
{
	if (!reg.present)
		return false;
	if (reg.isInteger)
		return true;
	if (reg.space != kSpaceSystemMemory && reg.space != kSpacePCC)
		return false;
	if (reg.address == 0)
		return false;
	return reg.space == kSpacePCC || register_address(reg,
		register_bytes(reg)) != NULL;
}


static uint64
read_register(const generic_register& reg)
{
	if (reg.isInteger)
		return reg.integer;
	uint32 bytes = register_bytes(reg);
	uint8* address = register_address(reg, bytes);
	if (address == NULL)
		return 0;
	return read_field(address, reg, bytes);
}


static void
write_register(const generic_register& reg, uint64 value)
{
	if (reg.isInteger || !reg.present)
		return;
	uint32 bytes = register_bytes(reg);
	uint8* address = register_address(reg, bytes);
	if (address != NULL)
		write_field(address, reg, bytes, value);
}


/*!	Runs \a command on every PCC subspace the CPUs use. Holds sLock. */
static bool
pcc_all(uint16 command)
{
	bool ok = true;
	for (uint32 i = 0; i < kMaxPCCSubspaces; i++) {
		if (sPCC[i].used && !pcc_command(sPCC[i], command)) {
			dprintf("R5 CPPC: PCC subspace %u did not complete command %u\n",
				sPCC[i].id, command);
			ok = false;
		}
	}
	return ok;
}


//	#pragma mark - _CPC


static bool
parse_entry(const acpi_object_type& object, generic_register& reg)
{
	memset(&reg, 0, sizeof(reg));
	if (object.object_type == ACPI_TYPE_INTEGER) {
		reg.present = true;
		reg.isInteger = true;
		reg.integer = object.integer.integer;
		return true;
	}
	if (object.object_type != ACPI_TYPE_BUFFER
		|| object.buffer.length < sizeof(acpi_generic_register_descriptor))
		return false;

	acpi_generic_register_descriptor descriptor;
	memcpy(&descriptor, object.buffer.buffer, sizeof(descriptor));
	// 0x82: generic register descriptor
	if (descriptor.descriptor != 0x82)
		return false;

	reg.space = descriptor.spaceID;
	reg.bitWidth = descriptor.bitWidth;
	reg.bitOffset = descriptor.bitOffset;
	reg.accessSize = descriptor.accessSize;
	reg.address = descriptor.address;
	// A register "not present" is a system memory register at address 0
	reg.present = !(reg.space == kSpaceSystemMemory && reg.address == 0);
	return true;
}


static bool
note_subspace(const generic_register& reg)
{
	if (!reg.present || reg.isInteger || reg.space != kSpacePCC)
		return true;
	if (find_subspace(reg.accessSize) != NULL)
		return true;
	for (uint32 i = 0; i < kMaxPCCSubspaces; i++) {
		if (!sPCC[i].used) {
			sPCC[i].used = true;
			sPCC[i].id = reg.accessSize;
			return true;
		}
	}
	return false;
}


static int32
processor_index(acpi_device_module_info* acpi, acpi_device cookie,
	bool isDevice)
{
	acpi_data buffer;
	buffer.pointer = NULL;
	buffer.length = ACPI_ALLOCATE_BUFFER;
	if (acpi->evaluate_method(cookie, isDevice ? "_UID" : NULL, NULL,
			&buffer) != B_OK)
		return -1;

	acpi_object_type* object = (acpi_object_type*)buffer.pointer;
	uint32 id;
	bool ok = true;
	if (!isDevice)
		id = object->processor.cpu_id;
	else if (object->object_type == ACPI_TYPE_INTEGER)
		id = (uint32)object->integer.integer;
	else
		ok = false;
	free(buffer.pointer);
	if (!ok)
		return -1;

	for (int32 i = 0; i < sCPUCount; i++) {
		if ((uint32)gCPU[i].arch.acpi_processor_id == id)
			return i;
	}
	return -1;
}


static status_t
read_cpc(acpi_device_module_info* acpi, acpi_device cookie, cpc_info& info)
{
	acpi_data buffer;
	buffer.pointer = NULL;
	buffer.length = ACPI_ALLOCATE_BUFFER;
	status_t status = acpi->evaluate_method(cookie, "_CPC", NULL, &buffer);
	if (status != B_OK)
		return status;

	acpi_object_type* package = (acpi_object_type*)buffer.pointer;
	status = B_BAD_DATA;
	if (package->object_type == ACPI_TYPE_PACKAGE
		&& package->package.count >= kCPCMinimumEntries) {
		uint32 count = min_c(package->package.count,
			(uint32)kCPCEntryCount);
		bool ok = true;
		for (uint32 i = 0; ok && i < count; i++) {
			if (i == kCPCNumEntries || i == kCPCRevision)
				continue;
			ok = parse_entry(package->package.objects[i], info.entries[i])
				&& note_subspace(info.entries[i]);
		}
		if (ok) {
			info.valid = true;
			status = B_OK;
		}
	}
	free(buffer.pointer);
	return status;
}


static status_t
find_processors()
{
	device_node* root = sDeviceManager->get_root_node();
	status_t status = B_OK;
	int32 found = 0;
	bool isDevice = false;
	device_node* processor = NULL;
	while (true) {
		// Processor objects first, then processor devices (ACPI0007), as
		// x86_acpi_cstates looks for them
		device_attr acpiAttrs[] = {
			{ B_DEVICE_BUS, B_STRING_TYPE, { .string = "acpi" }},
			{ ACPI_DEVICE_TYPE_ITEM, B_UINT32_TYPE,
				{ .ui32 = ACPI_TYPE_PROCESSOR }},
			{ NULL }
		};
		device_attr deviceAttrs[] = {
			{ B_DEVICE_BUS, B_STRING_TYPE, { .string = "acpi" }},
			{ ACPI_DEVICE_HID_ITEM, B_STRING_TYPE, { .string = "ACPI0007" }},
			{ NULL }
		};
		if (sDeviceManager->find_child_node(root,
				isDevice ? deviceAttrs : acpiAttrs, &processor) != B_OK) {
			if (isDevice || found > 0)
				break;
			isDevice = true;
			processor = NULL;
			continue;
		}

		acpi_device_module_info* acpi;
		acpi_device cookie;
		if (sDeviceManager->get_driver(processor,
				(driver_module_info**)&acpi, (void**)&cookie) != B_OK)
			continue;

		int32 index = processor_index(acpi, cookie, isDevice);
		if (index < 0 || sCPC[index].valid)
			continue;
		if (read_cpc(acpi, cookie, sCPC[index]) == B_OK)
			found++;
	}
	// find_child_node() released the last node when it found no further one
	sDeviceManager->put_node(root);

	if (found != sCPUCount) {
		dprintf("R5 CPPC: _CPC found for %" B_PRId32 " of %" B_PRId32
			" CPUs\n", found, sCPUCount);
		status = B_NOT_SUPPORTED;
	}
	return status;
}


//	#pragma mark - interface


status_t
amd_cppc_acpi_init()
{
	sCPUCount = smp_get_num_cpus();
	for (int32 i = 0; i < sCPUCount; i++) {
		if (gCPU[i].arch.vendor != VENDOR_AMD)
			return B_NOT_SUPPORTED;
	}

	// Loaded here, not in module_dependencies: a computer that has the
	// CPPC MSR never needs them (docs/POWER.md, the EFI rule does the same)
	// One line when this path gives up, so a boot log says why a computer
	// without the CPPC MSR has no profiles.
	dprintf("R5 CPPC: no CPPC MSR, trying the ACPI _CPC registers\n");
	if (get_module(B_ACPI_MODULE_NAME, (module_info**)&sACPI) != B_OK) {
		dprintf("R5 CPPC: no ACPI module, no _CPC\n");
		sACPI = NULL;
		return B_NOT_SUPPORTED;
	}
	if (get_module(B_DEVICE_MANAGER_MODULE_NAME,
			(module_info**)&sDeviceManager) != B_OK) {
		dprintf("R5 CPPC: no device manager, no _CPC\n");
		sDeviceManager = NULL;
		put_module(B_ACPI_MODULE_NAME);
		sACPI = NULL;
		return B_NOT_SUPPORTED;
	}

	sCPC = (cpc_info*)calloc(sCPUCount, sizeof(cpc_info));
	if (sCPC == NULL) {
		amd_cppc_acpi_uninit();
		return B_NO_MEMORY;
	}

	status_t status = find_processors();
	if (status == B_OK)
		status = setup_pcc();

	MutexLocker locker(sLock);
	if (status == B_OK && !pcc_all(0))
		status = B_IO_ERROR;

	// Every CPU needs the levels and the two limit registers
	for (int32 i = 0; status == B_OK && i < sCPUCount; i++) {
		cpc_info& info = sCPC[i];
		if (!register_usable(info.entries[kCPCHighestPerf])
			|| !register_usable(info.entries[kCPCNominalPerf])
			|| !register_usable(info.entries[kCPCLowestNonlinearPerf])
			|| !register_usable(info.entries[kCPCMinPerfReg])
			|| !register_usable(info.entries[kCPCMaxPerfReg])
			|| !register_usable(info.entries[kCPCDesiredPerfReg])) {
			dprintf("R5 CPPC: cpu %" B_PRId32 ": _CPC registers R5 cannot "
				"reach\n", i);
			status = B_NOT_SUPPORTED;
			break;
		}
		info.highest = (uint32)read_register(info.entries[kCPCHighestPerf]);
		info.nominal = (uint32)read_register(info.entries[kCPCNominalPerf]);
		info.lowestNonlinear = (uint32)read_register(
			info.entries[kCPCLowestNonlinearPerf]);
		if (info.highest == 0 || info.nominal == 0
			|| info.nominal > info.highest
			|| info.lowestNonlinear > info.nominal) {
			dprintf("R5 CPPC: cpu %" B_PRId32 ": invalid _CPC levels "
				"%" B_PRIu32 "/%" B_PRIu32 "/%" B_PRIu32 "\n", i,
				info.highest, info.nominal, info.lowestNonlinear);
			status = B_BAD_DATA;
		}
	}

	if (status == B_OK) {
		// Turn CPPC on, and let the processor choose its speed where the
		// firmware offers that.
		for (int32 i = 0; i < sCPUCount; i++) {
			cpc_info& info = sCPC[i];
			if (register_usable(info.entries[kCPCEnableReg]))
				write_register(info.entries[kCPCEnableReg], 1);
			if (register_usable(info.entries[kCPCAutonomousSelectionEnable]))
				write_register(info.entries[kCPCAutonomousSelectionEnable], 1);
		}
		if (!pcc_all(1))
			status = B_IO_ERROR;
	}
	locker.Unlock();

	if (status != B_OK) {
		amd_cppc_acpi_uninit();
		return status;
	}

	dprintf("using AMD CPPC through ACPI _CPC (highest %" B_PRIu32
		", nominal %" B_PRIu32 ", lowest nonlinear %" B_PRIu32 ")\n",
		sCPC[0].highest, sCPC[0].nominal, sCPC[0].lowestNonlinear);
	return B_OK;
}


void
amd_cppc_acpi_uninit()
{
	for (uint32 i = 0; i < kMaxPCCSubspaces; i++) {
		if (sPCC[i].area >= 0 && sPCC[i].ready)
			delete_area(sPCC[i].area);
	}
	memset(sPCC, 0, sizeof(sPCC));
	for (int32 i = 0; i < sMappingCount; i++)
		delete_area(sMappings[i].area);
	sMappingCount = 0;

	free(sCPC);
	sCPC = NULL;

	if (sDeviceManager != NULL)
		put_module(B_DEVICE_MANAGER_MODULE_NAME);
	if (sACPI != NULL)
		put_module(B_ACPI_MODULE_NAME);
	sDeviceManager = NULL;
	sACPI = NULL;
}


status_t
amd_cppc_acpi_set_profile(int32 profile)
{
	if (profile < 0 || profile >= R5_POWER_PROFILE_COUNT || sCPC == NULL)
		return B_BAD_VALUE;

	MutexLocker locker(sLock);
	for (int32 i = 0; i < sCPUCount; i++) {
		cpc_info& info = sCPC[i];
		uint32 maximum = profile == R5_POWER_PROFILE_SAVER
			? info.nominal : info.highest;
		bool epp = register_usable(info.entries[kCPCEnergyPerfPreferenceReg]);
		bool autonomous = epp || register_usable(
			info.entries[kCPCAutonomousSelectionEnable]);

		write_register(info.entries[kCPCMinPerfReg], info.lowestNonlinear);
		write_register(info.entries[kCPCMaxPerfReg], maximum);
		// With autonomous selection the processor picks its speed between
		// the limits, and a desired level of 0 leaves that to it. Without,
		// it holds the desired level: the profile's highest.
		write_register(info.entries[kCPCDesiredPerfReg],
			autonomous ? 0 : maximum);
		if (epp) {
			write_register(info.entries[kCPCEnergyPerfPreferenceReg],
				kProfileEPP[profile]);
		}
	}
	bool written = pcc_all(1);

	for (int32 i = 0; i < sCPUCount; i++) {
		cpc_info& info = sCPC[i];
		dprintf("R5 CPPC: cpu %" B_PRId32 " acpi profile %" B_PRId32
			" min %" B_PRIu32 " max %" B_PRIu32 " epp %" B_PRId32 "\n", i,
			profile, info.lowestNonlinear,
			profile == R5_POWER_PROFILE_SAVER ? info.nominal : info.highest,
			register_usable(info.entries[kCPCEnergyPerfPreferenceReg])
				? (int32)kProfileEPP[profile] : -1);
	}
	return written ? B_OK : B_IO_ERROR;
}
