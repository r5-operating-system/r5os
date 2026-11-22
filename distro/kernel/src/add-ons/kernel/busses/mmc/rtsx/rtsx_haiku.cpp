/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Haiku attachment of the rtsx driver: the device table, the device_manager
 * modules, the mmc_bus_interface for the mmc bus manager, the interrupt,
 * the DMA buffer, the card detect thread and the shutdown hook.
 *
 * The node layout follows sdhci: the PCI node gets the controller node
 * (busses/mmc/rtsx/driver_v1), which publishes one slot node
 * (busses/mmc/rtsx/device/v1) with mmc_bus as its fixed child.
 */


#include <new>
#include <stdio.h>
#include <string.h>

#include <KernelExport.h>
#include <PCI.h>
#include <bus/PCI.h>
#include <device_manager.h>
#include <r5_shutdown.h>
#include <util/AutoLock.h>

#include "IORequest.h"
#include "mmc.h"
#include "rtsxreg.h"
#include "rtsx.h"


#define RTSX_DEVICE_MODULE_NAME	"busses/mmc/rtsx/driver_v1"
#define RTSX_BUS_MODULE_NAME	"busses/mmc/rtsx/device/v1"

#define RTSX_VENDOR_REALTEK		0x10ec

#define ERROR(x...)	dprintf("rtsx: " x)
//#define TRACE_RTSX
#ifdef TRACE_RTSX
#	define TRACE(x...) dprintf("rtsx: " x)
#else
#	define TRACE(x...) ;
#endif


/*
 * The one device table. The OpenBSD chips are the ones rtsx_pci.c matches
 * (OpenBSD sys/dev/pci/rtsx_pci.c 1.16), with its flags and BARs. The
 * RTS5228 is R5's addition.
 */
static const rtsx_chip kChips[] = {
	{ 0x5209, "RTS5209",	RTSX_F_5209,	0 },
	{ 0x5227, "RTS5227",	0,				0 },
	{ 0x5228, "RTS5228",	RTSX_F_5228,	0 },
	{ 0x5229, "RTS5229",	RTSX_F_5229,	0 },
	{ 0x522a, "RTS522A",	0,				0 },
	{ 0x5249, "RTS5249",	RTSX_F_5229,	0 },
	{ 0x525a, "RTS525A",	RTSX_F_525A,	1 },
	{ 0x5286, "RTL8402",	0,				0 },
	{ 0x5287, "RTL8411B",	0,				0 },
	{ 0x5289, "RTL8411",	0,				0 },
};


struct rtsx_controller {
	device_node*	node;
};


static device_manager_info* gDeviceManager;
static device_module_info* gMMCBusController;


static const rtsx_chip*
find_chip(uint16 vendor, uint16 device)
{
	if (vendor != RTSX_VENDOR_REALTEK)
		return NULL;
	for (size_t i = 0; i < B_COUNT_OF(kChips); i++) {
		if (kChips[i].device == device)
			return &kChips[i];
	}
	return NULL;
}


/* SD response type per command, as sdhci chooses its reply types. */
static uint8
response_type(uint8 command, card_type type)
{
	bool mmc = type == CARD_TYPE_MMC
		|| type == CARD_TYPE_MMC_EXTENDED_CAPACITY;

	switch (command) {
		case GO_IDLE_STATE:
			return RTSX_SD_RSP_TYPE_R0;
		case MMC_SEND_OP_COND:
		case SD_SEND_OP_COND:
			return RTSX_SD_RSP_TYPE_R3;
		case ALL_SEND_CID:
		case SEND_CSD:
			return RTSX_SD_RSP_TYPE_R2;
		case SD_SEND_RELATIVE_ADDR:
			return mmc ? RTSX_SD_RSP_TYPE_R1 : RTSX_SD_RSP_TYPE_R6;
		case SD_SET_BUS_WIDTH:
			return mmc ? RTSX_SD_RSP_TYPE_R1B : RTSX_SD_RSP_TYPE_R1;
		case SD_SEND_IF_COND:
			return mmc ? RTSX_SD_RSP_TYPE_R1 : RTSX_SD_RSP_TYPE_R7;
		case SELECT_DESELECT_CARD:
		case SD_STOP_TRANSMISSION:
		case SD_ERASE:
			return RTSX_SD_RSP_TYPE_R1B;
		case SD_APP_CMD:
		case SD_ERASE_WR_BLK_START:
		case SD_ERASE_WR_BLK_END:
		case SD_READ_SINGLE_BLOCK:
		case SD_READ_MULTIPLE_BLOCKS:
		case SD_WRITE_SINGLE_BLOCK:
		case SD_WRITE_MULTIPLE_BLOCKS:
			return RTSX_SD_RSP_TYPE_R1;
		default:
			return 0;
	}
}


// #pragma mark - card power and detection


/* Power the slot for a new card and tell mmc_bus once. Lock held. */
static void
card_power_on(rtsx_softc* sc)
{
	if (!sc->powered && !sc->oc_tripped) {
		if (rtsx_bus_power(sc, true) == 0
			&& rtsx_bus_clock(sc, 400) == 0) {
			sc->powered = true;
			(void)rtsx_led_enable(sc);
			dprintf("rtsx: card inserted\n");
		} else
			ERROR("cannot power the card slot\n");
	}

	if (sc->powered && !sc->announced && sc->scan_sem >= 0) {
		sc->announced = true;
		release_sem_etc(sc->scan_sem, 1, B_DO_NOT_RESCHEDULE);
	}
}


/* The card is gone. Lock held. */
static void
card_power_off(rtsx_softc* sc)
{
	atomic_and(&sc->flags, ~RTSX_F_CARD_PRESENT);
	bool wasPowered = sc->powered;
	sc->powered = false;
	sc->announced = false;
	sc->oc_tripped = false;
	(void)rtsx_bus_power(sc, false);
	(void)rtsx_led_disable(sc);
	if (wasPowered) {
		// mmc_bus cannot unpublish a card yet; its disk only gets errors.
		dprintf("rtsx: card removed\n");
	}
}


/* Bring the slot in line with the card detect bit. Lock held. */
static void
card_update(rtsx_softc* sc)
{
	// A change while the previous one is handled latches a new event, but
	// rtsx_init on removal clears the latch, so check until it is stable.
	for (int i = 0; i < 4; i++) {
		bool present = (rtsx_read4(sc, RTSX_BIPR) & RTSX_SD_EXIST) != 0;
		if (present && !sc->powered && !sc->oc_tripped) {
			atomic_or(&sc->flags, RTSX_F_CARD_PRESENT);
			card_power_on(sc);
		} else if (!present && (sc->powered
				|| (sc->flags & RTSX_F_CARD_PRESENT) != 0
				|| sc->oc_tripped)) {
			card_power_off(sc);
		} else
			break;
	}
}


static status_t
card_thread(void* data)
{
	rtsx_softc* sc = (rtsx_softc*)data;

	while (acquire_sem(sc->card_sem) == B_OK && !sc->quit) {
		int32 events = atomic_and(&sc->card_events, 0);

		if ((events & RTSX_EVENT_OVERCURRENT) != 0
			&& (sc->flags & RTSX_F_5228) != 0) {
			mutex_lock(&sc->lock);
			rtsx_5228_overcurrent(sc);
			if (sc->powered) {
				atomic_and(&sc->flags, ~RTSX_F_CARD_PRESENT);
				sc->powered = false;
				sc->oc_tripped = true;
			}
			mutex_unlock(&sc->lock);
		}

		if ((events & RTSX_EVENT_CARD) != 0) {
			// Debounce: the contacts settle before the slot is powered.
			snooze(200000);
			if (sc->quit)
				break;
			mutex_lock(&sc->lock);
			card_update(sc);
			mutex_unlock(&sc->lock);
		}
	}
	return B_OK;
}


static void
shutdown_hook(bool reboot, void* cookie)
{
	rtsx_softc* sc = (rtsx_softc*)cookie;

	// A stuck command holds the lock for at most its 10 s timeout; do not
	// hold up the shutdown for it.
	bool locked = false;
	for (int i = 0; i < 100; i++) {
		if (mutex_trylock(&sc->lock) == B_OK) {
			locked = true;
			break;
		}
		snooze(10000);
	}

	sc->quit = true;
	if ((sc->flags & RTSX_F_5228) != 0) {
		(void)rtsx_5228_power_off(sc);
		rtsx_5228_shutdown(sc);
	} else
		rtsx_write4(sc, RTSX_BIER, 0);

	if (locked)
		mutex_unlock(&sc->lock);
}


// #pragma mark - slot node (mmc_bus_interface)


static void
free_softc(rtsx_softc* sc)
{
	if (sc->shutdown_hook)
		r5_unregister_shutdown_hook(shutdown_hook, sc);
	if (sc->regs_base != NULL)
		rtsx_write4(sc, RTSX_BIER, 0);

	if (sc->card_thread >= 0) {
		sc->quit = true;
		delete_sem(sc->card_sem);
		sc->card_sem = -1;
		status_t result;
		wait_for_thread(sc->card_thread, &result);
	}
	if (sc->card_sem >= 0)
		delete_sem(sc->card_sem);

	if (sc->irq_installed)
		remove_io_interrupt_handler(sc->irq, rtsx_intr, sc);
	if (sc->msi) {
		sc->pci->disable_msi(sc->device);
		sc->pci->unconfigure_msi(sc->device);
	}
	if (sc->dma_area >= 0)
		delete_area(sc->dma_area);
	if (sc->regs_area >= 0)
		delete_area(sc->regs_area);
	mutex_destroy(&sc->lock);
	delete sc;
}


static status_t
init_bus(device_node* node, void** _cookie)
{
	device_node* parent = gDeviceManager->get_parent_node(node);
	device_node* pciParent = gDeviceManager->get_parent_node(parent);
	pci_device_module_info* pci;
	pci_device* device;
	gDeviceManager->get_driver(pciParent, (driver_module_info**)&pci,
		(void**)&device);

	uint16 vendor = 0, deviceID = 0;
	gDeviceManager->get_attr_uint16(pciParent, B_DEVICE_VENDOR_ID, &vendor,
		false);
	gDeviceManager->get_attr_uint16(pciParent, B_DEVICE_ID, &deviceID, false);
	gDeviceManager->put_node(pciParent);
	gDeviceManager->put_node(parent);

	const rtsx_chip* chip = find_chip(vendor, deviceID);
	if (chip == NULL)
		return B_BAD_VALUE;

	rtsx_softc* sc = new(std::nothrow) rtsx_softc;
	if (sc == NULL)
		return B_NO_MEMORY;
	memset((void*)sc, 0, sizeof(*sc));
	sc->chip = chip;
	sc->flags = chip->flags;
	sc->node = node;
	sc->pci = pci;
	sc->device = device;
	sc->regs_area = -1;
	sc->dma_area = -1;
	sc->card_sem = -1;
	sc->card_thread = -1;
	sc->scan_sem = -1;
	sc->type = CARD_TYPE_UNKNOWN;
	B_INITIALIZE_SPINLOCK(&sc->intr_lock);
	sc->intr_cond.Init(sc, "rtsx interrupt");
	mutex_init(&sc->lock, "rtsx");

	pci_info info;
	pci->get_pci_info(device, &info);

	// OpenBSD rtsx_pci_attach: a reader without ASIC is not handled.
	if ((chip->flags & RTSX_F_5228) == 0
		&& (pci->read_pci_config(device, RTSX_CFG_PCI, 4)
			& RTSX_CFG_ASIC) != 0) {
		ERROR("%s: no asic\n", chip->name);
		free_softc(sc);
		return B_ERROR;
	}

	uint8 bar = chip->bar;
	phys_addr_t physical = info.u.h0.base_registers[bar];
	uint64 size = info.u.h0.base_register_sizes[bar];
	if ((info.u.h0.base_register_flags[bar] & PCI_address_space) != 0) {
		ERROR("%s: BAR %u is not memory\n", chip->name, bar);
		free_softc(sc);
		return B_ERROR;
	}
	if ((info.u.h0.base_register_flags[bar] & PCI_address_type)
			== PCI_address_type_64 && bar < 5) {
		physical |= (uint64)info.u.h0.base_registers[bar + 1] << 32;
		size |= (uint64)info.u.h0.base_register_sizes[bar + 1] << 32;
	}
	if (physical == 0 || size == 0) {
		ERROR("%s: no registers\n", chip->name);
		free_softc(sc);
		return B_ERROR;
	}

	sc->regs_area = map_physical_memory("rtsx registers", physical, size,
		B_ANY_KERNEL_BLOCK_ADDRESS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
		(void**)&sc->regs_base);
	if (sc->regs_area < 0) {
		ERROR("%s: cannot map registers\n", chip->name);
		status_t status = sc->regs_area;
		free_softc(sc);
		return status;
	}

	// One 4 KiB buffer below 4 GiB: host commands, then ADMA descriptors.
	sc->dma_area = create_area("rtsx dma", (void**)&sc->dma_virt,
		B_ANY_KERNEL_ADDRESS, RTSX_DMA_BUFSIZE, B_32_BIT_CONTIGUOUS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	physical_entry entry;
	if (sc->dma_area < 0
		|| get_memory_map(sc->dma_virt, RTSX_DMA_BUFSIZE, &entry, 1) != B_OK
		|| entry.address + RTSX_DMA_BUFSIZE > 0x100000000ULL) {
		ERROR("%s: cannot allocate DMA memory\n", chip->name);
		free_softc(sc);
		return B_NO_MEMORY;
	}
	sc->dma_phys = entry.address;
	memset(sc->dma_virt, 0, RTSX_DMA_BUFSIZE);

	uint16 command = pci->read_pci_config(device, PCI_command, 2);
	command &= ~PCI_command_io;
	command |= PCI_command_master | PCI_command_memory;
	pci->set_powerstate(device, PCI_pm_state_d0);

	// MSI first, as OpenBSD; the legacy line otherwise.
	uint32 vector;
	if (pci->get_msi_count(device) >= 1
		&& pci->configure_msi(device, 1, &vector) == B_OK) {
		if (pci->enable_msi(device) == B_OK) {
			sc->msi = true;
			sc->irq = vector;
			command |= PCI_command_int_disable;
		} else
			pci->unconfigure_msi(device);
	}
	if (!sc->msi) {
		sc->irq = info.u.h0.interrupt_line;
		command &= ~PCI_command_int_disable;
		if (sc->irq == 0 || sc->irq == 0xff) {
			ERROR("%s: no interrupt\n", chip->name);
			free_softc(sc);
			return B_ERROR;
		}
	}
	pci->write_pci_config(device, PCI_command, 2, command);

	sc->card_sem = create_sem(0, "rtsx card events");
	if (sc->card_sem < 0) {
		free_softc(sc);
		return B_NO_MEMORY;
	}

	status_t status = install_io_interrupt_handler(sc->irq, rtsx_intr, sc,
		0);
	if (status != B_OK) {
		ERROR("%s: cannot install interrupt %" B_PRIu32 "\n", chip->name,
			sc->irq);
		free_softc(sc);
		return status;
	}
	sc->irq_installed = true;

	dprintf("rtsx: Realtek %s card reader (%04x:%04x), %s %" B_PRIu32 "\n",
		chip->name, vendor, deviceID, sc->msi ? "MSI vector" : "IRQ",
		sc->irq);

	mutex_lock(&sc->lock);
	int error = rtsx_attach(sc, 1);
	mutex_unlock(&sc->lock);
	if (error != 0) {
		ERROR("%s: cannot initialize chip\n", chip->name);
		free_softc(sc);
		return B_ERROR;
	}
	if ((sc->flags & RTSX_F_5228) != 0) {
		dprintf("rtsx: RTS5228 version %c, ASPM %#x, L1SS %#x, LTR %d\n",
			'A' + sc->r5228.ic_version, sc->r5228.aspm_bits,
			sc->r5228.l1ss_bits, sc->r5228.ltr_enabled);
	}

	sc->card_thread = spawn_kernel_thread(card_thread, "rtsx card detect",
		B_NORMAL_PRIORITY, sc);
	if (sc->card_thread < 0) {
		free_softc(sc);
		return B_NO_MEMORY;
	}
	resume_thread(sc->card_thread);

	sc->shutdown_hook
		= r5_register_shutdown_hook(shutdown_hook, sc) == B_OK;

	*_cookie = sc;
	return B_OK;
}


static void
uninit_bus(void* cookie)
{
	rtsx_softc* sc = (rtsx_softc*)cookie;
	mutex_lock(&sc->lock);
	if (sc->powered && (sc->flags & RTSX_F_5228) != 0)
		(void)rtsx_5228_power_off(sc);
	mutex_unlock(&sc->lock);
	free_softc(sc);
}


static void
bus_removed(void* cookie)
{
}


static status_t
set_clock(void* controller, uint32_t kilohertz)
{
	rtsx_softc* sc = (rtsx_softc*)controller;
	MutexLocker locker(sc->lock);
	if (!sc->powered)
		return B_DEV_NO_MEDIA;
	return rtsx_bus_clock(sc, kilohertz);
}


static status_t
execute_command(void* controller, uint8_t command, uint32_t argument,
	uint32_t* response)
{
	rtsx_softc* sc = (rtsx_softc*)controller;

	rtsx_command cmd;
	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = command;
	cmd.arg = argument;
	cmd.rsp_type = response_type(command, sc->type);
	if (cmd.rsp_type == 0) {
		ERROR("unknown command %u\n", command);
		return B_BAD_DATA;
	}

	MutexLocker locker(sc->lock);
	if (!sc->powered)
		return B_DEV_NO_MEDIA;
	status_t status = rtsx_exec_command(sc, &cmd);
	locker.Unlock();

	if (status != B_OK) {
		TRACE("command %u failed: %s\n", command, strerror(status));
		return status;
	}

	if (response != NULL) {
		if (cmd.rsp_type == RTSX_SD_RSP_TYPE_R2)
			memcpy(response, cmd.resp, sizeof(cmd.resp));
		else if (cmd.rsp_type != RTSX_SD_RSP_TYPE_R0)
			response[0] = cmd.resp[0];
	}
	return B_OK;
}


static status_t
do_io(void* controller, uint8_t command, IOOperation* operation,
	bool offsetAsSectors)
{
	rtsx_softc* sc = (rtsx_softc*)controller;

	off_t offset = operation->Offset();
	generic_size_t length = operation->Length();
	if (offset % RTSX_BLOCK_SIZE != 0 || length % RTSX_BLOCK_SIZE != 0
		|| length == 0
		|| length / RTSX_BLOCK_SIZE > RTSX_MAX_TRANSFER_BLOCKS)
		return B_BAD_VALUE;

	rtsx_command cmd;
	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = command;
	cmd.arg = offsetAsSectors ? offset / RTSX_BLOCK_SIZE : offset;
	cmd.rsp_type = response_type(command, sc->type);
	cmd.read = !operation->IsWrite();
	cmd.datalen = length;
	cmd.blklen = RTSX_BLOCK_SIZE;
	cmd.vecs = operation->Vecs();
	cmd.vec_count = operation->VecCount();
	if (cmd.rsp_type == 0)
		return B_BAD_DATA;

	MutexLocker locker(sc->lock);
	if (!sc->powered)
		return B_DEV_NO_MEDIA;

	status_t status = rtsx_exec_command(sc, &cmd);

	// AUTO_READ3 and AUTO_WRITE3 leave CMD12 to the driver (OpenBSD lets
	// sdmmc send it); mmc_disk always uses the multiple-block commands.
	if (command == SD_READ_MULTIPLE_BLOCKS
		|| command == SD_WRITE_MULTIPLE_BLOCKS) {
		rtsx_command stop;
		memset(&stop, 0, sizeof(stop));
		stop.opcode = SD_STOP_TRANSMISSION;
		stop.rsp_type = RTSX_SD_RSP_TYPE_R1B;
		status_t stopStatus = rtsx_exec_command(sc, &stop);
		if (status == B_OK && stopStatus != B_OK)
			status = stopStatus;
	}

	if (status != B_OK) {
		ERROR("%s of %" B_PRIuGENADDR " bytes at %" B_PRIdOFF " failed: %s\n",
			cmd.read ? "read" : "write", length, offset, strerror(status));
	}
	return status;
}


static void
set_scan_semaphore(void* controller, sem_id sem)
{
	rtsx_softc* sc = (rtsx_softc*)controller;
	MutexLocker locker(sc->lock);
	sc->scan_sem = sem;
	if ((sc->flags & RTSX_F_CARD_PRESENT) != 0)
		card_power_on(sc);
}


static void
set_bus_width(void* controller, int width)
{
	rtsx_softc* sc = (rtsx_softc*)controller;
	MutexLocker locker(sc->lock);
	if (sc->powered)
		(void)rtsx_bus_width(sc, width);
}


static void
terminate_bus(void* controller)
{
	rtsx_softc* sc = (rtsx_softc*)controller;
	MutexLocker locker(sc->lock);
	// mmc_bus gave up on this card; it stays off until it is reinserted.
	if (sc->powered) {
		sc->powered = false;
		(void)rtsx_bus_power(sc, false);
	}
}


static void
set_card_type(void* controller, card_type type)
{
	rtsx_softc* sc = (rtsx_softc*)controller;
	sc->type = type;
}


static mmc_bus_interface gRtsxBusModule = {
	.info = {
		.info = {
			.name = RTSX_BUS_MODULE_NAME,
		},
		.init_driver = init_bus,
		.uninit_driver = uninit_bus,
		.device_removed = bus_removed,
	},

	.set_clock = set_clock,
	.execute_command = execute_command,
	.do_io = do_io,
	.set_scan_semaphore = set_scan_semaphore,
	.set_bus_width = set_bus_width,
	.terminate_bus = terminate_bus,
	.set_card_type = set_card_type,
};


// #pragma mark - controller node


static float
supports_device(device_node* parent)
{
	const char* bus;
	uint16 vendor, device, type;

	if (gDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false)
			!= B_OK || strcmp(bus, "pci") != 0)
		return 0.0f;
	if (gDeviceManager->get_attr_uint16(parent, B_DEVICE_VENDOR_ID, &vendor,
			false) != B_OK
		|| gDeviceManager->get_attr_uint16(parent, B_DEVICE_ID, &device,
			false) != B_OK
		|| gDeviceManager->get_attr_uint16(parent, B_DEVICE_TYPE, &type,
			false) != B_OK)
		return 0.0f;

	// As OpenBSD: only the undefined class. Readers that also show up as
	// a standard SD host are left to sdhci.
	if (type != PCI_undefined || find_chip(vendor, device) == NULL)
		return 0.0f;

	return 0.8f;
}


static status_t
register_device(device_node* parent)
{
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
			{ .string = "Realtek card reader" }},
		{}
	};

	return gDeviceManager->register_node(parent, RTSX_DEVICE_MODULE_NAME,
		attrs, NULL, NULL);
}


static status_t
init_device(device_node* node, void** _cookie)
{
	rtsx_controller* controller = new(std::nothrow) rtsx_controller;
	if (controller == NULL)
		return B_NO_MEMORY;
	controller->node = node;
	*_cookie = controller;
	return B_OK;
}


static void
uninit_device(void* cookie)
{
	delete (rtsx_controller*)cookie;
}


static status_t
register_child_devices(void* cookie)
{
	rtsx_controller* controller = (rtsx_controller*)cookie;

	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, { .string = "SD bus" }},
		{ B_DEVICE_FIXED_CHILD, B_STRING_TYPE,
			{ .string = MMC_BUS_MODULE_NAME }},
		{ B_DEVICE_BUS, B_STRING_TYPE, { .string = "mmc" }},

		// DMA limits: 32-bit addresses, whole sectors, the ADMA table size
		// and the largest piece one descriptor carries.
		{ B_DMA_ALIGNMENT, B_UINT32_TYPE, { .ui32 = RTSX_BLOCK_SIZE - 1 }},
		{ B_DMA_HIGH_ADDRESS, B_UINT64_TYPE, { .ui64 = 0xffffffffULL }},
		{ B_DMA_MAX_SEGMENT_COUNT, B_UINT32_TYPE, { .ui32 = 128 }},
		{ B_DMA_MAX_SEGMENT_BLOCKS, B_UINT32_TYPE,
			{ .ui32 = RTSX_DMA_MAX_SEGSIZE / RTSX_BLOCK_SIZE }},
		{ B_DMA_MAX_TRANSFER_BLOCKS, B_UINT32_TYPE,
			{ .ui32 = RTSX_MAX_TRANSFER_BLOCKS }},
		{ NULL }
	};

	return gDeviceManager->register_node(controller->node,
		RTSX_BUS_MODULE_NAME, attrs, NULL, NULL);
}


static driver_module_info sRtsxDevice = {
	{
		RTSX_DEVICE_MODULE_NAME,
		0,
		NULL
	},
	supports_device,
	register_device,
	init_device,
	uninit_device,
	register_child_devices,
	NULL,	// rescan
	NULL,	// device removed
};


module_dependency module_dependencies[] = {
	{ MMC_BUS_MODULE_NAME, (module_info**)&gMMCBusController },
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info**)&gDeviceManager },
	{}
};


module_info* modules[] = {
	(module_info*)&sRtsxDevice,
	(module_info*)&gRtsxBusModule,
	NULL
};
