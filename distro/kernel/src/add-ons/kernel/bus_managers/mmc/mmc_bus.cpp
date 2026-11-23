/*
 * Copyright 2018-2020 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		B Krishnan Iyer, krishnaniyer97@gmail.com
 */
#include "mmc_bus.h"

#include <Errors.h>

#include <stdint.h>


MMCBus::MMCBus(device_node* node)
	:
	fNode(node),
	fController(NULL),
	fCookie(NULL),
	fStatus(B_OK),
	fWorkerThread(-1),
	fActiveDevice(0),
	fCardType(CARD_TYPE_UNKNOWN)
{
	CALLED();

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	fCardNode = NULL;
// SPDX-SnippetEnd

	// Get the parent info, it includes the API to send commands to the hardware
	device_node* parent = gDeviceManager->get_parent_node(node);
	fStatus = gDeviceManager->get_driver(parent,
		(driver_module_info**)&fController, &fCookie);
	gDeviceManager->put_node(parent);

	if (fStatus != B_OK) {
		ERROR("Not able to establish the bus %s\n",
			strerror(fStatus));
		return;
	}

	fScanSemaphore = create_sem(0, "MMC bus scan");
	fLockSemaphore = create_sem(1, "MMC bus lock");
	fWorkerThread = spawn_kernel_thread(_WorkerThread, "SD bus controller",
		B_NORMAL_PRIORITY, this);
	resume_thread(fWorkerThread);

	fController->set_scan_semaphore(fCookie, fScanSemaphore);
}


MMCBus::~MMCBus()
{
	CALLED();

	// Tell the worker thread we want to stop
	fStatus = B_SHUTTING_DOWN;

	// Delete the semaphores (this will unlock the worker thread if it was
	// waiting on them)
	delete_sem(fScanSemaphore);
	delete_sem(fLockSemaphore);

	// Wait for the worker thread to terminate
	status_t result;
	if (fWorkerThread != 0)
		wait_for_thread(fWorkerThread, &result);
}


status_t
MMCBus::InitCheck()
{
	return fStatus;
}


void
MMCBus::Rescan()
{
	// Just wake up the thread for a scan
	release_sem(fScanSemaphore);
}


status_t
MMCBus::ExecuteCommand(uint16_t rca, uint8_t command, uint32_t argument,
	uint32_t* response)
{
	status_t status = _ActivateDevice(rca);
	if (status != B_OK)
		return status;
	return fController->execute_command(fCookie, command, argument, response);
}


status_t
MMCBus::DoIO(uint16_t rca, uint8_t command, IOOperation* operation,
	bool offsetAsSectors)
{
	status_t status = _ActivateDevice(rca);
	if (status != B_OK)
		return status;
	return fController->do_io(fCookie, command, operation, offsetAsSectors);
}


void
MMCBus::SetClock(int frequency)
{
	fController->set_clock(fCookie, frequency);
}


void
MMCBus::SetBusWidth(int width)
{
	fController->set_bus_width(fCookie, width);
}


void
MMCBus::SetCardType(card_type type)
{
	fCardType = type;
	fController->set_card_type(fCookie, type);
}


status_t
MMCBus::_ActivateDevice(uint16_t rca)
{
	// Do nothing if the device is already activated
	if (fActiveDevice == rca)
		return B_OK;

	uint32_t response;
	status_t result;
	result = fController->execute_command(fCookie, SELECT_DESELECT_CARD, ((uint32)rca) << 16,
		&response);

	if (result == B_OK)
		fActiveDevice = rca;

	return result;
}


void MMCBus::_AcquireScanSemaphore()
{
	ReleaseBus();
	acquire_sem(fScanSemaphore);
	AcquireBus();
}


void
MMCBus::_TerminateBus()
{
	fController->terminate_bus(fCookie);
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
/*
 * Forget the card of the last scan. A scan starts when a card is put in, and
 * its CMD0 puts every card back in the idle state: no card is selected, the
 * RCA and the 4 bit bus are gone (SD physical layer, CMD0). So the node of
 * the last card no longer matches a card. It is removed, so that the card
 * found now gets a new node and mmc_disk reads its size and sets its bus
 * again; mmc_disk then fails the I/O of the old node. Bus lock held.
 */
void
MMCBus::_RemoveCard()
{
	// Without this the CMD0 below first sent CMD7 to deselect the old card,
	// and a card put in again does not answer that.
	fActiveDevice = 0;

	if (fCardNode == NULL)
		return;

	device_node* node = fCardNode;
	fCardNode = NULL;

	// mmc_disk may wait for the bus with an I/O; do not hold the bus while
	// its node and driver go away.
	ReleaseBus();
	gDeviceManager->unregister_node(node);
	AcquireBus();
}
// SPDX-SnippetEnd


status_t
MMCBus::_WorkerThread(void* cookie)
{
	MMCBus* bus = (MMCBus*)cookie;
	uint32_t response;

	bus->AcquireBus();

	// We assume the bus defaults to 400kHz clock and has already powered on
	// cards.

	// Reset all cards on the bus
	// This does not work if the bus has not been powered on yet (the command
	// will timeout), in that case we wait until asked to scan again when a
	// card has been inserted and powered on.
	status_t result;
	do {
		bus->_AcquireScanSemaphore();

		// Check if we need to exit early (possible if the parent device did
		// not manage initialize itself correctly)
		if (bus->fStatus == B_SHUTTING_DOWN) {
			bus->ReleaseBus();
			return B_OK;
		}

		TRACE("Reset the bus...\n");
		result = bus->ExecuteCommand(0, GO_IDLE_STATE, 0, NULL);
		TRACE("CMD0 result: %s\n", strerror(result));
	} while (result != B_OK);

	// Need to wait at least 8 clock cycles after CMD0 before sending the next
	// command. With the default 400kHz clock that would be 20 microseconds,
	// but we need to wait at least 20ms here, otherwise the next command times
	// out
	snooze(30000);

	while (bus->fStatus != B_SHUTTING_DOWN) {
		TRACE("Scanning the bus\n");

		// Use the low speed clock and 1bit bus width for scanning
		bus->SetClock(400);
		bus->SetBusWidth(1);

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		// A card put in after the first scan gets its own CMD0 (the one
		// above only ran for the card present at the start).
		bus->_RemoveCard();
		bus->ExecuteCommand(0, GO_IDLE_STATE, 0, NULL);
		snooze(30000);

		// The card must be ready within one second (SD physical layer,
		// ACMD41); give it two, then wait for the next card instead of
		// asking forever (stick 75: hundreds of tries, also after the card
		// was gone).
		static const int kMaxReadyTries = 20;
		bool scanFailed = false;
// SPDX-SnippetEnd

		// Probe the voltage range
		enum {
			// Table 4-40 in physical layer specification v8.00
			// All other values are currently reserved
			HOST_27_36V = 1, // Host supplied voltage 2.7-3.6V
		};

		// An arbitrary value, we just need to check that the response
		// containts the same.
		static const uint8 kVoltageCheckPattern = 0xAA;
		uint8_t cardType = CARD_TYPE_SD;
		// FIXME MMC cards will not reply to this! They expect CMD1 instead
		// SD v1 cards will also not reply, but we can proceed to ACMD41
		// If ACMD41 also does not work, it may be an SDIO card, too
		uint32_t probe = (HOST_27_36V << 8) | kVoltageCheckPattern;
		uint32_t hcs = 1 << 30;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		// Was read uninitialized when CMD1 or ACMD41 failed.
		uint32_t ocr = 0;
		int tries = 0;
// SPDX-SnippetEnd
		status_t status = bus->ExecuteCommand(0, SD_SEND_IF_COND, probe, &response);
		if (status != B_OK) {
			TRACE("Card does not implement CMD8, may be a V1 SD or MMC card\n");
			// Do not check for SDHC support in this case
			hcs = 0;

			TRACE("Trying MMC CMD1 initialization...\n");
			do {
				status = bus->ExecuteCommand(0, MMC_SEND_OP_COND, 0xFF8000, &ocr);
				// full voltage window, byte addressable, should look into this.
				if (status != B_OK) {
					TRACE("MMC CMD1 failed\n");
					break;
				}
				if ((ocr & (1 << 31)) == 0) {
					TRACE("MMC card is busy\n");
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
					if (++tries >= kMaxReadyTries) {
						ERROR("MMC card did not get ready\n");
						status = B_TIMED_OUT;
						break;
					}
// SPDX-SnippetEnd
					snooze(100000);
				}
			} while ((ocr & (1 << 31)) == 0);

			if (status == B_OK && (ocr & (1 << 31)) != 0) {
				TRACE("Detected MMC card after CMD1\n");
				if ((ocr & (1 << 30)) != 0)
					cardType = CARD_TYPE_MMC_EXTENDED_CAPACITY;
				else
					cardType = CARD_TYPE_MMC;
			}
		} else if (response != probe) {
			ERROR("Card does not support voltage range (expected %x, "
				"reply %x)\n", probe, response);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			// Wait for another card; ending the thread left the slot dead
			// until the next start.
			bus->_AcquireScanSemaphore();
			continue;
// SPDX-SnippetEnd
		}

		// Probe OCR, waiting for card to become ready
		// We keep repeating ACMD41 until the card replies that it is
		// initialized. For MMC we already probed using CMD1 above.
		if ((cardType != CARD_TYPE_MMC) && (cardType != CARD_TYPE_MMC_EXTENDED_CAPACITY)) {
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			// A failed CMD55 or ACMD41 (no card, no answer) ends this scan;
			// until now it was ignored and the loop went on with an old OCR.
			tries = 0;
			do {
				uint32_t cardStatus = 0;
				while ((status = bus->ExecuteCommand(0, SD_APP_CMD, 0,
						&cardStatus)) == B_BUSY) {
					ERROR("Card locked after CMD8...\n");
					if (++tries >= kMaxReadyTries)
						break;
					snooze(1000000);
				}
				if (status != B_OK) {
					ERROR("CMD55 failed: %s\n", strerror(status));
					scanFailed = true;
					break;
				}
				if ((cardStatus & 0xFFFF8000) != 0)
					ERROR("SD card reports error %x\n", cardStatus);
				if ((cardStatus & (1 << 5)) == 0)
					ERROR("Card did not enter ACMD mode\n");

				status = bus->ExecuteCommand(0, SD_SEND_OP_COND,
					hcs | 0xFF8000, &ocr);
				if (status != B_OK) {
					ERROR("ACMD41 failed: %s\n", strerror(status));
					scanFailed = true;
					break;
				}

				if ((ocr & (1 << 31)) == 0) {
					TRACE("Card is busy\n");
					if (++tries >= kMaxReadyTries) {
						ERROR("SD card did not get ready\n");
						scanFailed = true;
						break;
					}
					snooze(100000);
				}
			} while ((ocr & (1 << 31)) == 0);
		}

		if (scanFailed) {
			bus->_AcquireScanSemaphore();
			continue;
		}
// SPDX-SnippetEnd

		// FIXME this should be asked to each card, when there are multiple
		// ones. So ACMD41 should be moved inside the probing loop below?
		if (cardType == CARD_TYPE_SD) {
			if ((ocr & hcs) != 0)
				cardType = CARD_TYPE_SDHC;
			if ((ocr & (1 << 29)) != 0)
				cardType = CARD_TYPE_UHS2;
			if ((ocr & (1 << 24)) != 0)
				TRACE("Card supports 1.8v");
		}
		TRACE("Voltage range: %x\n", ocr & 0xFFFFFF);

		// Set the card type so the next commands have the correct reply types
		// (MMC and SD commands with the same identifier sometime expect different responses)
		bus->SetCardType((card_type)cardType);

		// TODO send CMD11 to switch to low voltage mode if card supports it?

		// We use CMD2 (ALL_SEND_CID) and CMD3 (SEND_RELATIVE_ADDR) to assign
		// an RCA to all cards. Initially all cards have an RCA of 0 and will
		// all receive CMD2. But only one of them will reply (they do collision
		// detection while sending the CID in reply). We assign a new RCA to
		// that first card, and repeat the process with the remaining ones
		// until no one answers to CMD2. Then we know all cards have an RCA
		// (and a matching published device on our side).
		uint32_t cid[4];
		uint32_t vendor;
		char name[7];
		uint32_t serial;
		uint16_t revision;
		uint8_t month;
		uint16_t year;
		uint16_t rca;
		bool cardFound = false;
		// This being an if statement as opposed to a while statement restricts
		// it to one device per bus.
		if ((cardType == CARD_TYPE_MMC) || (cardType == CARD_TYPE_MMC_EXTENDED_CAPACITY)) {
			if (bus->ExecuteCommand(0, ALL_SEND_CID, 0, cid) == B_OK) {
				// We currently support only a single card, so use a fixed RCA.
				rca = 1;
				status
					= bus->ExecuteCommand(0, MMC_SET_RELATIVE_ADDR, ((uint32)rca) << 16, &response);
				TRACE("MMC RCA: %x Status: %x\n", rca, response & 0xFFFF);
				if (status != B_OK) {
					TRACE("Failed to set RCA for MMC card\n");
				} else {
					MMCCid mmcCid(cid);
					vendor = mmcCid.VendorID();
					mmcCid.ProductName(name);
					serial = mmcCid.ProductSerial();
					revision = mmcCid.ProductRevision();
					month = mmcCid.ManufactureMonth();
					year = mmcCid.ManufactureYear(true);
					TRACE("MMC CID: MID=%" B_PRIu32 ", name=\"%s\", PSN=%" B_PRIu32
						  ", PRV=%u, MDT=%u/%u\n",
						vendor, name, serial, revision, month, year);
					cardFound = true;
				}
			}
		} else if (bus->ExecuteCommand(0, ALL_SEND_CID, 0, cid) == B_OK) {
			bus->ExecuteCommand(0, SD_SEND_RELATIVE_ADDR, 0, &response);

			TRACE("RCA: %x Status: %x\n", response >> 16, response & 0xFFFF);

			if ((response & 0xFF00) != 0x500) {
				TRACE("Card did not enter data state\n");
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
				// Wait for the next card. "break" ended the scan thread, so
				// no card in this slot was seen again until the next start.
				ERROR("Card did not enter data state\n");
				bus->_AcquireScanSemaphore();
				continue;
// SPDX-SnippetEnd
			}

			// The card now has an RCA and it entered the data phase, which
			// means our initializing job is over, we can pass it on to the
			// mmc_disk driver.
			rca = response >> 16;
			SDCid sdCid(cid);
			vendor = sdCid.VendorID();
			sdCid.ProductName(name);
			serial = sdCid.ProductSerial();
			revision = sdCid.ProductRevision();
			month = sdCid.ManufactureMonth();
			year = sdCid.ManufactureYear();
			cardFound = true;
		}

		if (cardFound) {
			device_attr attrs[] = {
				{ B_DEVICE_BUS, B_STRING_TYPE, {.string = "mmc" }},
				{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "mmc device" }},
				{ B_DEVICE_VENDOR_ID, B_UINT32_TYPE, {.ui32 = vendor}},
				{ B_DEVICE_ID, B_STRING_TYPE, {.string = name}},
				{ B_DEVICE_UNIQUE_ID, B_UINT32_TYPE, {.ui32 = serial}},
				{ "mmc/revision", B_UINT16_TYPE, {.ui16 = revision}},
				{ "mmc/month", B_UINT8_TYPE, {.ui8 = month}},
				{ "mmc/year", B_UINT16_TYPE, {.ui16 = year}},
				{ kMmcRcaAttribute, B_UINT16_TYPE, {.ui16 = rca}},
				{ kMmcTypeAttribute, B_UINT8_TYPE, {.ui8 = cardType}},
				{}
			};

			// publish child device for the card
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
			// Kept, so that the next scan can remove it (_RemoveCard).
			if (gDeviceManager->register_node(bus->fNode, MMC_BUS_MODULE_NAME,
					attrs, NULL, &bus->fCardNode) != B_OK)
				bus->fCardNode = NULL;
// SPDX-SnippetEnd
		}

		// TODO if there is a single card active, check if it supports CMD6
		// (spec version 1.10 or later in SCR). If it does, check if CMD6 can
		// enable high speed mode, use that to go to 50MHz instead of 25.
		bus->SetClock(25000);

		// FIXME we also need to unpublish devices that are gone. Probably need
		// to "ping" all RCAs somehow? Or is there an interrupt we can look for
		// to detect added/removed cards?

		// Wait for the next scan request
		// The thread will spend most of its time waiting here
		bus->_AcquireScanSemaphore();
	}

	bus->ReleaseBus();

	TRACE("poller thread terminating");
	return B_OK;
}
