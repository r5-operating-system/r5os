/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Firmware for Realtek Bluetooth radios (docs/BLUETOOTH-2.md, step B0).
 *
 * A Realtek radio such as the RTL8761CU in the ASUS USB-BT600 starts from
 * its ROM and answers standard HCI commands, but needs a patch from the host
 * before it is a usable controller. This is done the way FreeBSD's rtlbtfw
 * (usr.sbin/bluetooth/rtlbtfw, BSD-2-Clause) describes it; the code is R5's
 * own:
 *
 *  1. Read Local Version: a Realtek radio says so in its manufacturer field.
 *  2. The firmware name comes from the USB ID table or, failing that, from
 *     what the chip reports (h2rtkfw.h).
 *  3. The file's project id must fit the chip, and vendor command 0xfc6d
 *     says which ROM version the chip has, which picks the patch in the file.
 *  4. The patch, with the _config.bin after it, goes over in 252-byte pieces
 *     with vendor command 0xfc20.
 *  5. The radio is asked again what it is, and its features are logged.
 *
 * Commands go out as class control transfers on endpoint zero. Their answers
 * come on the interrupt endpoint, which nobody else reads yet at this point
 * (h2generic starts its event reads at BT_UP, after open).
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <KernelExport.h>
#include <util/atomic.h>

#include "h2generic.h"
#include "h2debug.h"
#include "h2mediatek.h"
#include "h2realtek.h"
#include "h2rtkfw.h"


#define REALTEK_VENDOR			0x0bda

#define HCI_EVENT_COMMAND_COMPLETE	0x0e
#define HCI_EVENT_COMMAND_STATUS	0x0f

#define OP_READ_LOCAL_VERSION		0x1001
#define OP_READ_LOCAL_FEATURES		0x1003
#define OP_LE_READ_LOCAL_FEATURES	0x2003

#define COMMAND_TIMEOUT			2000000		/* two seconds, as rtlbtfw */
#define EVENT_BUFFER_SIZE		260
#define MAX_PATCHED				8


struct hci_version_info {
	uint8	hci_version;
	uint16	hci_revision;
	uint8	lmp_version;
	uint16	manufacturer;
	uint16	lmp_subversion;
};


struct event_wait {
	sem_id	done;
	status_t status;
	size_t	actual;
	uint8	buffer[EVENT_BUFFER_SIZE];
};


static bt_usb_dev* sPatched[MAX_PATCHED];


static void
event_arrived(void* cookie, status_t status, void* data, size_t actual)
{
	event_wait* wait = (event_wait*)cookie;
	wait->status = status;
	wait->actual = actual;
	release_sem_etc(wait->done, 1, B_DO_NOT_RESCHEDULE);
}


/* One HCI command, and the parameters of its Command Complete. Other events
 * that arrive in between (a late answer, a vendor event) are passed over.
 */
static status_t
hci_command(bt_usb_dev* bdev, event_wait* wait, uint16 opcode,
	const uint8* parameters, uint8 parameterLength, uint8* reply,
	size_t replySize, size_t* _replyLength)
{
	uint8 packet[3 + 255];
	size_t actual = 0;

	packet[0] = opcode & 0xff;
	packet[1] = opcode >> 8;
	packet[2] = parameterLength;
	if (parameterLength > 0)
		memcpy(packet + 3, parameters, parameterLength);

	status_t status = usb->send_request(bdev->dev, bdev->ctrl_req, 0, 0, 0,
		3 + parameterLength, packet, &actual);
	if (status != B_OK)
		return status;

	bigtime_t deadline = system_time() + COMMAND_TIMEOUT;
	size_t have = 0;

	while (system_time() < deadline) {
		size_t room = sizeof(wait->buffer) - have;
		status = usb->queue_interrupt(bdev->intr_in_ep->handle,
			wait->buffer + have, room, event_arrived, wait);
		if (status != B_OK)
			return status;

		status = acquire_sem_etc(wait->done, 1, B_ABSOLUTE_TIMEOUT, deadline);
		if (status != B_OK) {
			usb->cancel_queued_transfers(bdev->intr_in_ep->handle);
			// The cancel calls back once; take that back so the semaphore
			// is even for the next command.
			acquire_sem_etc(wait->done, 1, B_RELATIVE_TIMEOUT, 100000);
			return B_TIMED_OUT;
		}
		if (wait->status != B_OK)
			return wait->status;

		have += min_c(wait->actual, room);
		if (have < 2)
			continue;
		size_t eventLength = 2 + wait->buffer[1];
		if (have < eventLength && have < sizeof(wait->buffer))
			continue;	// the rest of the event is in the next packet
		have = 0;

		const uint8* event = wait->buffer;
		if (event[0] == HCI_EVENT_COMMAND_STATUS && eventLength >= 6
			&& h2_rtk_le16(event + 4) == opcode && event[2] != 0)
			return B_IO_ERROR;
		if (event[0] != HCI_EVENT_COMMAND_COMPLETE || eventLength < 5
			|| h2_rtk_le16(event + 3) != opcode)
			continue;

		size_t length = min_c(eventLength - 5, replySize);
		memcpy(reply, event + 5, length);
		*_replyLength = length;
		return B_OK;
	}

	return B_TIMED_OUT;
}


static status_t
read_version(bt_usb_dev* bdev, event_wait* wait, hci_version_info* info)
{
	uint8 reply[9];
	size_t length = 0;
	status_t status = hci_command(bdev, wait, OP_READ_LOCAL_VERSION, NULL, 0,
		reply, sizeof(reply), &length);
	if (status != B_OK)
		return status;
	if (length < sizeof(reply) || reply[0] != 0)
		return B_IO_ERROR;

	info->hci_version = reply[1];
	info->hci_revision = h2_rtk_le16(reply + 2);
	info->lmp_version = reply[4];
	info->manufacturer = h2_rtk_le16(reply + 5);
	info->lmp_subversion = h2_rtk_le16(reply + 7);
	return B_OK;
}


static uint64
read_features(bt_usb_dev* bdev, event_wait* wait, uint16 opcode, bool* _ok)
{
	uint8 reply[9];
	size_t length = 0;
	*_ok = false;
	if (hci_command(bdev, wait, opcode, NULL, 0, reply, sizeof(reply),
			&length) != B_OK || length < sizeof(reply) || reply[0] != 0)
		return 0;

	uint64 bits = 0;
	for (int i = 7; i >= 0; i--)
		bits = (bits << 8) | reply[1 + i];
	*_ok = true;
	return bits;
}


/* What the BT6 plan needs to know about a radio (docs/BLUETOOTH-2.md, B0):
 * its version, and the LMP and LE feature bits. LE bits from Core 6.0,
 * Vol 6, Part B, 4.6; LMP bit 38 is "LE Supported (Controller)", Vol 2,
 * Part C, 3.3.
 */
static void
log_controller(bt_usb_dev* bdev, event_wait* wait, const char* name)
{
	hci_version_info info;
	if (read_version(bdev, wait, &info) != B_OK) {
		ERROR("realtek: %s patched, but Read Local Version failed\n", name);
		return;
	}
	ERROR("realtek: %s hci version %#x revision %#x, lmp version %#x"
		" subversion %#x, manufacturer %#x\n", name, info.hci_version,
		info.hci_revision, info.lmp_version, info.lmp_subversion,
		info.manufacturer);

	bool ok = false;
	uint64 lmp = read_features(bdev, wait, OP_READ_LOCAL_FEATURES, &ok);
	if (!ok) {
		ERROR("realtek: %s lmp features not read\n", name);
		return;
	}
	ERROR("realtek: %s lmp features %#018" B_PRIx64 " (le %s)\n", name, lmp,
		(lmp & (1ULL << 38)) != 0 ? "yes" : "no");
	if ((lmp & (1ULL << 38)) == 0)
		return;

	uint64 le = read_features(bdev, wait, OP_LE_READ_LOCAL_FEATURES, &ok);
	if (!ok) {
		ERROR("realtek: %s le features not read\n", name);
		return;
	}
#define LE_BIT(n) ((le & (1ULL << (n))) != 0 ? 1 : 0)
	ERROR("realtek: %s le features %#018" B_PRIx64 ": 2M %d, coded %d,"
		" extended adv %d, periodic adv %d, CIS central %d, CIS peripheral %d,"
		" ISO broadcaster %d, synchronized receiver %d\n", name, le, LE_BIT(8),
		LE_BIT(11), LE_BIT(12), LE_BIT(13), LE_BIT(28), LE_BIT(29), LE_BIT(30),
		LE_BIT(31));
#undef LE_BIT
}


static bool
was_patched(bt_usb_dev* bdev)
{
	for (int i = 0; i < MAX_PATCHED; i++) {
		if (atomic_pointer_get(&sPatched[i]) == bdev)
			return true;
	}
	return false;
}


static void
mark_patched(bt_usb_dev* bdev)
{
	for (int i = 0; i < MAX_PATCHED; i++) {
		if (atomic_pointer_test_and_set(&sPatched[i], bdev,
				(bt_usb_dev*)NULL) == NULL)
			return;
	}
}


void
realtek_forget(bt_usb_dev* bdev)
{
	for (int i = 0; i < MAX_PATCHED; i++)
		atomic_pointer_test_and_set(&sPatched[i], (bt_usb_dev*)NULL, bdev);
}


static status_t
download(bt_usb_dev* bdev, event_wait* wait, const uint8* patch,
	size_t length)
{
	size_t count = h2_rtk_fragments(length);
	uint8 command[1 + H2_RTK_FRAGMENT_SIZE];

	for (size_t i = 0; i < count; i++) {
		size_t piece = h2_rtk_fragment_length(i, count, length);
		command[0] = h2_rtk_fragment_index(i, count);
		memcpy(command + 1, patch + i * H2_RTK_FRAGMENT_SIZE, piece);

		uint8 reply[2];
		size_t replyLength = 0;
		status_t status = hci_command(bdev, wait, H2_RTK_OP_DOWNLOAD, command,
			(uint8)(1 + piece), reply, sizeof(reply), &replyLength);
		if (status != B_OK)
			return status;
		if (replyLength < 1 || reply[0] != 0) {
			ERROR("realtek: piece %" B_PRIuSIZE " of %" B_PRIuSIZE
				" refused (%#x)\n", i + 1, count,
				replyLength > 0 ? reply[0] : 0xff);
			return B_IO_ERROR;
		}
	}
	return B_OK;
}


static status_t
patch_radio(bt_usb_dev* bdev, event_wait* wait, const hci_version_info& info,
	const h2_rtk_choice& choice)
{
	const char* name = choice.name;
	char file[64];
	uint8* image = NULL;
	size_t imageSize = 0;

	snprintf(file, sizeof(file), "%s_fw.bin", name);
	if (h2_firmware_load(file, &image, &imageSize) != B_OK) {
		ERROR("realtek: this radio needs %s, which is not installed. It stays"
			" off.\n", file);
		return B_ENTRY_NOT_FOUND;
	}

	uint8 fileProject = 0;
	h2_rtk_fw_type type = h2_rtk_file_type(image, imageSize, &fileProject);
	if (type != H2_RTK_FW_V1) {
		ERROR("realtek: %s is %s. The radio stays off.\n", file,
			type == H2_RTK_FW_V2 ? "an RTBTCore (v2) file, not handled yet"
				: "not a Realtek firmware file");
		free(image);
		return B_BAD_DATA;
	}

	// The file says which chip it is for. With a USB ID entry that is the
	// entry's project id; otherwise FreeBSD's table turns it into the LMP
	// subversion the chip reported.
	if (!h2_rtk_fits(choice, fileProject, info.lmp_subversion)) {
		ERROR("realtek: %s is for project %#x, not this radio (lmp subversion"
			" %#x). It stays off.\n", file, fileProject, info.lmp_subversion);
		free(image);
		return B_BAD_DATA;
	}

	uint8 rom[2];
	size_t romLength = 0;
	status_t status = hci_command(bdev, wait, H2_RTK_OP_READ_ROM_VERSION, NULL,
		0, rom, sizeof(rom), &romLength);
	if (status == B_OK && (romLength < 2 || rom[0] != 0))
		status = B_IO_ERROR;
	if (status != B_OK) {
		ERROR("realtek: the radio did not say its ROM version (%s). It stays"
			" off.\n", strerror(status));
		free(image);
		return status;
	}

	// The patch and the config after it.
	uint8* patch = (uint8*)malloc(H2_RTK_MAX_PATCH);
	if (patch == NULL) {
		free(image);
		return B_NO_MEMORY;
	}
	uint32 fileVersion = 0;
	size_t length = h2_rtk_patch_v1(image, imageSize, rom[1], patch,
		H2_RTK_MAX_PATCH, &fileVersion);
	free(image);
	if (length == 0) {
		ERROR("realtek: %s has no patch for ROM version %u. It stays off.\n",
			file, rom[1]);
		free(patch);
		return B_BAD_DATA;
	}

	snprintf(file, sizeof(file), "%s_config.bin", name);
	uint8* config = NULL;
	size_t configSize = 0;
	if (h2_firmware_load(file, &config, &configSize) == B_OK) {
		if (!h2_rtk_config_ok(config, configSize)
			|| configSize > H2_RTK_MAX_PATCH - length) {
			ERROR("realtek: %s is not a config file. The radio stays off.\n",
				file);
			free(config);
			free(patch);
			return B_BAD_DATA;
		}
		memcpy(patch + length, config, configSize);
		length += configSize;
		free(config);
	} else if ((choice.flags & H2_RTK_NEEDS_CONFIG) != 0) {
		ERROR("realtek: this radio needs %s, which is not installed. It stays"
			" off.\n", file);
		free(patch);
		return B_ENTRY_NOT_FOUND;
	}

	bigtime_t started = system_time();
	status = download(bdev, wait, patch, length);
	free(patch);
	if (status != B_OK) {
		ERROR("realtek: the download of %s failed (%s). The radio stays off."
			"\n", name, strerror(status));
		return status;
	}

	ERROR("realtek: %s, version %#" B_PRIx32 ", ROM version %u, %" B_PRIuSIZE
		" bytes in %" B_PRIdBIGTIME " ms\n", name, fileVersion, rom[1], length,
		(system_time() - started) / 1000);
	return B_OK;
}


status_t
realtek_setup(bt_usb_dev* bdev)
{
	const usb_device_descriptor* descriptor
		= usb->get_device_descriptor(bdev->dev);
	if (descriptor == NULL || bdev->intr_in_ep == NULL)
		return B_OK;

	if (h2_rtk_find_usb(descriptor->vendor_id, descriptor->product_id) == NULL
		&& descriptor->vendor_id != REALTEK_VENDOR)
		return B_OK;

	if (was_patched(bdev)) {
		TRACE("%s: already patched while plugged in\n", __func__);
		return B_OK;
	}

	event_wait* wait = (event_wait*)malloc(sizeof(event_wait));
	if (wait == NULL)
		return B_NO_MEMORY;
	wait->done = create_sem(0, "h2generic realtek");
	if (wait->done < 0) {
		status_t status = wait->done;
		free(wait);
		return status;
	}

	status_t status = B_OK;
	hci_version_info info;
	if (read_version(bdev, wait, &info) != B_OK) {
		// Not an answer we can use: leave the radio as it is.
		ERROR("realtek: %04x:%04x did not answer Read Local Version\n",
			descriptor->vendor_id, descriptor->product_id);
	} else if (info.manufacturer != H2_RTK_MANUFACTURER) {
		TRACE("%s: manufacturer %#x is not Realtek\n", __func__,
			info.manufacturer);
	} else {
		ERROR("realtek: %04x:%04x from ROM: hci version %#x revision %#x,"
			" lmp version %#x subversion %#x\n", descriptor->vendor_id,
			descriptor->product_id, info.hci_version, info.hci_revision,
			info.lmp_version, info.lmp_subversion);

		h2_rtk_choice choice;
		if (!h2_rtk_choose(descriptor->vendor_id, descriptor->product_id,
				info.lmp_subversion, info.hci_revision, info.hci_version,
				&choice)) {
			ERROR("realtek: no firmware entry for this chip; it is used as it"
				" is\n");
		} else {
			status = patch_radio(bdev, wait, info, choice);
			if (status == B_OK) {
				mark_patched(bdev);
				log_controller(bdev, wait, choice.name);
			}
		}
	}

	delete_sem(wait->done);
	free(wait);
	return status;
}
