/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


/*!	The speaker protection firmware of one CS35L41: loading the program
	(.wmfw) and the tuning (.bin) into DSP memory, starting the core, and
	the mailbox. Written from the R5 clean-room specification
	(CS35L41-SPEC.md section 3, CS35L41-SPEC-2.md sections 2 and 4).

	The caller holds the driver lock.
*/


#include "Amplifier.h"

#include <string.h>

#include <ByteOrder.h>
#include <KernelExport.h>

#include "Registers.h"


#define TRACE_ALWAYS(a...)	dprintf("cs35l41: " a)

// .wmfw block types
#define WMFW_XM_UNPACKED		0x05
#define WMFW_YM_UNPACKED		0x06
#define WMFW_PM_PACKED			0x10
#define WMFW_XM_PACKED			0x11
#define WMFW_YM_PACKED			0x12
#define WMFW_ABSOLUTE			0xf0
#define WMFW_DESCRIPTORS		0xf2

// .bin block types
#define BIN_ABSOLUTE			0xf000
#define BIN_LONG_OFFSET			0xf400
#define BIN_NAME				0xfe00
#define BIN_INFO				0xff00
#define BIN_METADATA			0xfc00

#define WMFW_HEADER_SIZE		40
#define WMFW_CORE_HALO			4
#define WMFW_FORMAT_VERSION		3
#define BIN_HEADER_SIZE			16

// Firmware state poll: 1 ms steps, at most 15 ms (spec section 3).
#define STATE_POLL_TRIES		15
#define MAILBOX_POLL_TRIES		100
#define PAUSE_POLL_TRIES		5


static inline uint32
read_le16(const uint8* data)
{
	return data[0] | ((uint32)data[1] << 8);
}


static inline uint32
read_le32(const uint8* data)
{
	return data[0] | ((uint32)data[1] << 8) | ((uint32)data[2] << 16)
		| ((uint32)data[3] << 24);
}


static inline uint32
round_up_4(uint32 value)
{
	return (value + 3) & ~(uint32)3;
}


/*!	The register address of DSP word \a word in \a region. Packed XM and
	YM hold four 24-bit words in three registers; the address is rounded
	down to a register (CS35L41-SPEC-2.md section 2).
*/
static status_t
region_address(uint32 region, uint32 word, uint32* _address)
{
	switch (region) {
		case WMFW_XM_UNPACKED:
			*_address = DSP_XM_UNPACKED + word * 4;
			return B_OK;
		case WMFW_YM_UNPACKED:
			*_address = DSP_YM_UNPACKED + word * 4;
			return B_OK;
		case WMFW_PM_PACKED:
			*_address = DSP_PM_PACKED + word * 5;
			return B_OK;
		case WMFW_XM_PACKED:
		case WMFW_YM_PACKED:
			*_address = ((region == WMFW_XM_PACKED ? DSP_XM_PACKED
				: DSP_YM_PACKED) + word * 3) & ~(uint32)3;
			return B_OK;
	}
	return B_BAD_TYPE;
}


/*!	Writes \a size bytes from \a reg as one raw byte stream. The chip steps
	the address by itself, so a block that starts on a register and has a
	length of whole registers is split into transfers of up to fBlockSize
	data bytes, each with its own address. A block that does not start on
	a register or is not whole registers cannot be split that way; the
	specification writes it in one bus transfer, so it is sent in one
	transfer, which works only while it fits the controller's FIFO.
*/
status_t
Amplifier::_WriteBlock(uint32 reg, const uint8* data, size_t size)
{
	bool aligned = (reg % 4) == 0 && (size % 4) == 0;
	if (!aligned && size > fBlockSize) {
		TRACE_ALWAYS("amp %" B_PRIu32 ": block of %" B_PRIuSIZE " bytes at %08"
			B_PRIx32 " is not register aligned and longer than one I2C"
			" transfer (%" B_PRIu32 " bytes); not written\n", fIndex, size, reg,
			fBlockSize);
		return B_NOT_SUPPORTED;
	}

	size_t done = 0;
	while (done < size) {
		size_t chunk = min_c(size - done, (size_t)fBlockSize);
		uint8 buffer[4 + 32];
		*(uint32*)buffer = B_HOST_TO_BENDIAN_INT32(reg + (uint32)done);
		memcpy(buffer + 4, data + done, chunk);

		status_t status = fLink.bus->acquire_bus(fLink.cookie);
		if (status != B_OK)
			return status;
		for (int32 tries = 0; tries < 3; tries++) {
			status = fLink.bus->exec_command(fLink.cookie, I2C_OP_WRITE_STOP,
				fAddress, buffer, 4 + chunk, NULL, 0);
			if (status != B_BUSY)
				break;
			snooze(1000);
		}
		fLink.bus->release_bus(fLink.cookie);

		if (status == B_BAD_VALUE && fBlockSize > 4 && aligned) {
			// The controller FIFO is smaller than the transfer.
			fBlockSize = 4;
			continue;
		}
		if (status != B_OK) {
			TRACE_ALWAYS("amp %" B_PRIu32 " block write %08" B_PRIx32
				" failed: %s\n", fIndex, reg + (uint32)done, strerror(status));
			return status;
		}
		done += chunk;
	}
	return B_OK;
}


status_t
Amplifier::_LoadProgram(const firmware_file& file)
{
	const uint8* data = file.data;
	if (file.size < WMFW_HEADER_SIZE || memcmp(data, "WMFW", 4) != 0)
		return B_BAD_DATA;
	uint32 headerSize = read_le32(data + 4);
	if (headerSize < WMFW_HEADER_SIZE || headerSize > file.size
		|| data[10] != WMFW_CORE_HALO || data[11] != WMFW_FORMAT_VERSION) {
		TRACE_ALWAYS("%s: not a Halo program, version 3\n", file.name);
		return B_BAD_DATA;
	}

	uint32 blocks = 0;
	size_t position = headerSize;
	while (position + 8 <= file.size) {
		const uint8* block = data + position;
		uint32 offset = block[0] | ((uint32)block[1] << 8)
			| ((uint32)block[2] << 16);
		uint32 type = block[3];
		uint32 length = read_le32(block + 4);
		if (length > file.size - position - 8)
			return B_BAD_DATA;
		const uint8* payload = block + 8;
		position += 8 + length;

		uint32 address;
		if (type == WMFW_ABSOLUTE)
			address = offset;
		else if (region_address(type, offset, &address) != B_OK)
			continue;

		status_t status = _WriteBlock(address, payload, length);
		if (status != B_OK) {
			TRACE_ALWAYS("%s: block %" B_PRIu32 " (type %#" B_PRIx32
				") failed: %s\n", file.name, blocks, type, strerror(status));
			return status;
		}
		blocks++;
	}
	return position == file.size ? B_OK : B_BAD_DATA;
}


/*!	The algorithm table the program left at the start of XM. */
status_t
Amplifier::_ReadAlgorithms()
{
	uint32 header[10];
	for (uint32 i = 0; i < 10; i++) {
		status_t status = _Read(DSP_XM_UNPACKED + i * 4, &header[i]);
		if (status != B_OK)
			return status;
		header[i] &= 0x00ffffff;
	}
	fFirmwareID = header[3];

	uint32 count = header[9];
	if (count > MAX_DSP_ALGORITHMS) {
		TRACE_ALWAYS("amp %" B_PRIu32 ": %" B_PRIu32 " algorithms, using the"
			" first %d\n", fIndex, count, MAX_DSP_ALGORITHMS);
		count = MAX_DSP_ALGORITHMS;
	}

	fAlgorithmCount = 0;
	for (uint32 i = 0; i < count; i++) {
		uint32 entry[6];
		for (uint32 j = 0; j < 6; j++) {
			status_t status = _Read(DSP_ALGORITHM_TABLE + (i * 6 + j) * 4,
				&entry[j]);
			if (status != B_OK)
				return status;
			entry[j] &= 0x00ffffff;
		}
		dsp_algorithm& algorithm = fAlgorithms[fAlgorithmCount++];
		algorithm.id = entry[0];
		algorithm.version = entry[1];
		algorithm.xm_base = entry[2];
		algorithm.ym_base = entry[4];
		TRACE_ALWAYS("amp %" B_PRIu32 " runtime algorithm %" B_PRIu32 ": id %#"
			B_PRIx32 " version %#" B_PRIx32 " xm base %#" B_PRIx32 " size %#"
			B_PRIx32 " ym base %#" B_PRIx32 " size %#" B_PRIx32 "\n", fIndex, i,
			entry[0], entry[1], entry[2], entry[3], entry[4], entry[5]);
	}

	TRACE_ALWAYS("amp %" B_PRIu32 " (%s): firmware id %#" B_PRIx32
		" version %#" B_PRIx32 ", %" B_PRIu32 " algorithms\n", fIndex, Side(),
		fFirmwareID, header[4], fAlgorithmCount);
	return fAlgorithmCount > 0 ? B_OK : B_BAD_DATA;
}


const dsp_algorithm*
Amplifier::_Algorithm(uint32 id) const
{
	for (uint32 i = 0; i < fAlgorithmCount; i++) {
		if (fAlgorithms[i].id == id)
			return &fAlgorithms[i];
	}
	return NULL;
}


status_t
Amplifier::_LoadTuning(const firmware_file& file)
{
	const uint8* data = file.data;
	if (file.size < BIN_HEADER_SIZE || memcmp(data, "WMDR", 4) != 0)
		return B_BAD_DATA;
	uint32 headerSize = read_le32(data + 4);
	uint32 revision = read_le32(data + 8) & 0xff;
	if (headerSize < BIN_HEADER_SIZE || headerSize > file.size
		|| revision < 1 || revision > 3) {
		TRACE_ALWAYS("%s: not a tuning file of revision 1 to 3\n", file.name);
		return B_BAD_DATA;
	}

	size_t position = headerSize;
	uint32 index = 0;
	for (; position + 20 <= file.size; index++) {
		const uint8* block = data + position;
		uint32 offset = read_le16(block);
		uint32 type = read_le16(block + 2);
		uint32 algorithmID = read_le32(block + 4);
		uint32 version = read_le32(block + 8);
		uint32 longOffset = read_le32(block + 12);
		uint32 length = read_le32(block + 16);
		if (length > file.size - position - 20)
			return B_BAD_DATA;
		const uint8* payload = block + 20;
		position += 20 + round_up_4(length);
		if (position > file.size)
			position = file.size;

		if (type == BIN_NAME || type == BIN_INFO || type == BIN_METADATA)
			continue;

		uint32 address;
		if (type == BIN_ABSOLUTE) {
			if (algorithmID == fFirmwareID && offset == 0) {
				// The spec names a "global region base" here without an
				// address; such a block is not written.
				TRACE_ALWAYS("%s: global absolute block skipped\n", file.name);
				continue;
			}
			address = offset;
		} else {
			uint32 region = type;
			if ((type & 0xff00) == BIN_LONG_OFFSET) {
				region = type & 0xff;
				offset = longOffset;
			}
			const dsp_algorithm* algorithm = _Algorithm(algorithmID);
			if (algorithm == NULL) {
				TRACE_ALWAYS("%s: block %" B_PRIu32 ": no algorithm %#" B_PRIx32
					" in the program\n", file.name, index, algorithmID);
				return B_BAD_DATA;
			}
			if ((version >> 8) != algorithm->version) {
				TRACE_ALWAYS("%s: algorithm %#" B_PRIx32 " version %#" B_PRIx32
					", program has %#" B_PRIx32 "\n", file.name, algorithmID,
					version >> 8, algorithm->version);
			}
			uint32 base;
			switch (region) {
				case WMFW_XM_UNPACKED:
				case WMFW_XM_PACKED:
					base = algorithm->xm_base;
					break;
				case WMFW_YM_UNPACKED:
				case WMFW_YM_PACKED:
					base = algorithm->ym_base;
					break;
				default:
					// PM and unknown regions have no algorithm region.
					TRACE_ALWAYS("%s: block %" B_PRIu32 " type %#" B_PRIx32
						": no algorithm region\n", file.name, index, type);
					return B_BAD_DATA;
			}
			// The algorithm's base word goes through the region formula;
			// the block offset is in bytes of register address and is
			// added as it is (CS35L41-SPEC-2.md, round 4).
			if (region_address(region, base, &address) != B_OK)
				return B_BAD_DATA;
			address += offset;
		}

		status_t status = _WriteBlock(address, payload, length);
		if (status != B_OK) {
			TRACE_ALWAYS("%s: block %" B_PRIu32 " (type %#" B_PRIx32 ", %"
				B_PRIu32 " bytes at %08" B_PRIx32 ") failed: %s\n", file.name,
				index, type, length, address, strerror(status));
			return status;
		}
	}
	return B_OK;
}


/*!	Logs every algorithm and control of the program's descriptor blocks
	(type 0xF2), once per boot.
*/
void
Amplifier::_LogDescriptors(const firmware_file& file)
{
	const uint8* data = file.data;
	size_t position = read_le32(data + 4);

	while (position + 8 <= file.size) {
		const uint8* block = data + position;
		uint32 type = block[3];
		uint32 length = read_le32(block + 4);
		if (length > file.size - position - 8)
			return;
		const uint8* payload = block + 8;
		position += 8 + length;
		if (type != WMFW_DESCRIPTORS || length < 8)
			continue;

		size_t at = 4;
		uint32 id = read_le32(payload);
		uint32 nameLength = payload[at];
		char name[64] = "";
		if (at + 1 + nameLength <= length) {
			memcpy(name, payload + at + 1, min_c(nameLength,
				(uint32)sizeof(name) - 1));
		}
		at += round_up_4(nameLength + 1);
		if (at + 2 > length)
			continue;
		at += round_up_4(read_le16(payload + at) + 2);
		if (at + 4 > length)
			continue;
		uint32 count = read_le32(payload + at);
		at += 4;
		TRACE_ALWAYS("%s: descriptor algorithm %#" B_PRIx32 " \"%s\", %"
			B_PRIu32 " controls\n", file.name, id, name, count);

		for (uint32 i = 0; i < count && at + 9 <= length; i++) {
			size_t start = at;
			uint32 offset = read_le16(payload + at);
			uint32 region = read_le16(payload + at + 2);
			uint32 size = read_le32(payload + at + 4);
			uint32 controlLength = payload[at + 8];
			char control[64] = "";
			if (start + 9 + controlLength <= length) {
				memcpy(control, payload + start + 9, min_c(controlLength,
					(uint32)sizeof(control) - 1));
			}
			TRACE_ALWAYS("  algorithm %#" B_PRIx32 " control \"%s\" offset %"
				B_PRIu32 " region %#" B_PRIx32 "\n", id, control, offset,
				region);
			at = start + 8 + size;
		}
	}
}


/*!	The word offset of control \a name of \a algorithm, from the program's
	descriptor blocks (CS35L41-SPEC-2.md section 2, type 0xF2).
	B_ENTRY_NOT_FOUND: the algorithm has no descriptor block.
	B_NAME_NOT_FOUND: the algorithm has no control of that name.
	B_BAD_TYPE: the control is not in XM (unpacked).
*/
status_t
Amplifier::_FindControl(const firmware_file& file, uint32 algorithm,
	const char* name, uint32* _offset)
{
	const uint8* data = file.data;
	size_t position = read_le32(data + 4);
	size_t nameLength = strlen(name);
	bool algorithmFound = false;

	while (position + 8 <= file.size) {
		const uint8* block = data + position;
		uint32 type = block[3];
		uint32 length = read_le32(block + 4);
		if (length > file.size - position - 8)
			return B_BAD_DATA;
		const uint8* payload = block + 8;
		position += 8 + length;
		if (type != WMFW_DESCRIPTORS || length < 8)
			continue;

		size_t at = 0;
		uint32 id = read_le32(payload);
		at += 4;
		at += round_up_4(payload[at] + 1);
		if (at + 2 > length)
			continue;
		at += round_up_4(read_le16(payload + at) + 2);
		if (at + 4 > length)
			continue;
		uint32 count = read_le32(payload + at);
		at += 4;
		if (id != algorithm)
			continue;
		algorithmFound = true;

		for (uint32 i = 0; i < count && at + 9 <= length; i++) {
			size_t start = at;
			uint32 offset = read_le16(payload + at);
			uint32 region = read_le16(payload + at + 2);
			uint32 size = read_le32(payload + at + 4);
			uint32 controlLength = payload[at + 8];
			if (start + 9 + controlLength <= length
				&& controlLength == nameLength
				&& memcmp(payload + start + 9, name, nameLength) == 0) {
				if (region != WMFW_XM_UNPACKED)
					return B_BAD_TYPE;
				*_offset = offset;
				return B_OK;
			}
			at = start + 8 + size;
		}
	}
	return algorithmFound ? B_NAME_NOT_FOUND : B_ENTRY_NOT_FOUND;
}


/*!	Applies the factory calibration from the EFI variable's contents
	(CS35L41-SPEC-2.md, round 3): an 8-byte header (total size, entry
	count) and 20-byte entries (silicon ID, calibration time, ambient
	temperature, status, speaker resistance), all little-endian. The entry
	is found by the amplifier's silicon ID, or by the amplifier's index.
*/
status_t
Amplifier::_ApplyCalibration(const firmware_file& program, const uint8* data,
	size_t size)
{
	if (size < 8)
		return B_BAD_DATA;
	uint32 total = read_le32(data);
	uint32 count = read_le32(data + 4);
	if (total != 0 && total < size)
		size = total;
	count = min_c(count, (uint32)128);
	count = min_c(count, (uint32)((size - 8) / 20));

	uint32 idLow = 0;
	uint32 idHigh = 0;
	_Read(REG_SILICON_ID_LOW, &idLow);
	_Read(REG_SILICON_ID_HIGH, &idHigh);
	uint64 siliconID = ((uint64)idHigh << 32) | idLow;

	const uint8* chosen = NULL;
	if (siliconID != 0) {
		for (uint32 i = 0; i < count && chosen == NULL; i++) {
			const uint8* entry = data + 8 + i * 20;
			uint64 id = ((uint64)read_le32(entry + 4) << 32)
				| read_le32(entry);
			uint64 time = ((uint64)read_le32(entry + 12) << 32)
				| read_le32(entry + 8);
			if (time != 0 && id != 0 && id == siliconID)
				chosen = entry;
		}
	}
	if (chosen == NULL && fIndex < count) {
		const uint8* entry = data + 8 + fIndex * 20;
		uint64 id = ((uint64)read_le32(entry + 4) << 32) | read_le32(entry);
		uint64 time = ((uint64)read_le32(entry + 12) << 32)
			| read_le32(entry + 8);
		if (time != 0 && (id == 0 || siliconID == 0))
			chosen = entry;
	}
	if (chosen == NULL) {
		TRACE_ALWAYS("amp %" B_PRIu32 " (%s): no calibration entry for"
			" silicon id %#" B_PRIx64 " (%" B_PRIu32 " entries)\n", fIndex,
			Side(), siliconID, count);
		return B_ENTRY_NOT_FOUND;
	}

	int32 ambient = (int8)chosen[16];
	uint32 calStatus = chosen[17];
	uint32 resistance = read_le16(chosen + 18);

	const dsp_algorithm* algorithm = _Algorithm(CALIBRATION_ALGORITHM);
	if (algorithm == NULL)
		return B_NAME_NOT_FOUND;
	static const char* const kControls[] = {
		"CAL_AMBIENT", "CAL_R", "CAL_STATUS", "CAL_CHECKSUM"
	};
	uint32 values[] = {
		(uint32)ambient, resistance, calStatus, resistance + 1
	};
	for (size_t i = 0; i < B_COUNT_OF(kControls); i++) {
		uint32 offset;
		status_t status = _FindControl(program, CALIBRATION_ALGORITHM,
			kControls[i], &offset);
		if (status == B_OK) {
			status = _Write(DSP_XM_UNPACKED
				+ (algorithm->xm_base + offset) * 4, values[i]);
		}
		if (status != B_OK) {
			TRACE_ALWAYS("amp %" B_PRIu32 ": calibration control %s: %s\n",
				fIndex, kControls[i], strerror(status));
			return status;
		}
	}

	TRACE_ALWAYS("amp %" B_PRIu32 " (%s): factory calibration applied,"
		" resistance %" B_PRIu32 ", ambient %" B_PRId32 ", status %" B_PRIu32
		"\n", fIndex, Side(), resistance, ambient, calStatus);
	return B_OK;
}


/*!	Writes \a command to the firmware mailbox and waits for \a expected in
	the status.
*/
status_t
Amplifier::_Mailbox(uint32 command, uint32 expected)
{
	status_t status = _Write(REG_MAILBOX_COMMAND, command);
	if (status != B_OK)
		return status;

	int32 tries = command == MAILBOX_PAUSE ? PAUSE_POLL_TRIES
		: MAILBOX_POLL_TRIES;
	uint32 value = 0;
	for (int32 i = 0; i < tries; i++) {
		snooze(1000);
		if ((status = _Read(REG_MAILBOX_STATUS, &value)) != B_OK)
			return status;
		if (value == expected)
			return B_OK;
		if (value == 0xffffffff || value == 0x00ffffff) {
			TRACE_ALWAYS("amp %" B_PRIu32 ": mailbox error %#" B_PRIx32 "\n",
				fIndex, value);
			return B_ERROR;
		}
	}
	TRACE_ALWAYS("amp %" B_PRIu32 ": mailbox %" B_PRIu32 " not done (status %#"
		B_PRIx32 ")\n", fIndex, command, value);
	return B_TIMED_OUT;
}


void
Amplifier::_StopCore()
{
	_Update(REG_DSP_WATCHDOG, 1, 0);
	_Update(REG_DSP_CORE_CONTROL, DSP_CORE_ENABLE, 0);
	_Update(REG_DSP_SOFT_RESET, 1, 1);
}


void
Amplifier::_FallBackToBypass(const char* reason)
{
	TRACE_ALWAYS("amp %" B_PRIu32 " (%s): firmware off (%s); DSP bypass,"
		" PCM gain capped at its lowest step\n", fIndex, Side(), reason);
	_StopCore();
	fDspRunning = false;
}


/*!	Loads \a program and \a tuning, starts the DSP and leaves the firmware
	paused until playback (spec section 3). On failure the core is stopped
	and the amplifier stays in DSP bypass.
*/
status_t
Amplifier::StartFirmware(const firmware_file& program,
	const firmware_file& tuning, const uint8* calibration,
	size_t calibrationSize)
{
	if (!fReady)
		return B_NO_INIT;

	bigtime_t start = system_time();
	fDspRunning = false;

	// The DSP clock, as playback sets it.
	status_t status = _Write(REG_DSP_CLOCK, 0x00000003);
	if (status == B_OK)
		status = _LoadProgram(program);
	if (status == B_OK)
		status = _ReadAlgorithms();
	if (status == B_OK)
		status = _LoadTuning(tuning);
	if (status != B_OK) {
		_FallBackToBypass("download failed");
		return status;
	}
	if (calibration != NULL)
		_ApplyCalibration(program, calibration, calibrationSize);

	if (fIndex == 0)
		_LogDescriptors(program);

	uint32 stateOffset = 0;
	const dsp_algorithm* firmware = _Algorithm(FIRMWARE_ALGORITHM);
	status_t found = _FindControl(program, FIRMWARE_ALGORITHM, "HALO_STATE",
		&stateOffset);
	if (firmware == NULL || found != B_OK) {
		const char* step;
		if (firmware == NULL)
			step = "algorithm not in the runtime table";
		else if (found == B_ENTRY_NOT_FOUND)
			step = "algorithm not in the descriptors";
		else if (found == B_NAME_NOT_FOUND)
			step = "control name not found";
		else if (found == B_BAD_TYPE)
			step = "region mismatch";
		else
			step = strerror(found);
		TRACE_ALWAYS("amp %" B_PRIu32 " (%s): state control of algorithm %#"
			B_PRIx32 " \"HALO_STATE\": %s\n", fIndex, Side(),
			(uint32)FIRMWARE_ALGORITHM, step);
		_FallBackToBypass("no firmware state control");
		return B_BAD_DATA;
	}
	uint32 stateRegister = DSP_XM_UNPACKED
		+ (firmware->xm_base + stateOffset) * 4;

	// Stream rates, then memory protection, then the core.
	for (uint32 i = 0; i < DSP_RATE_COUNT; i++) {
		_Write(REG_DSP_RX_RATE_FIRST + i * 8, 1);
		_Write(REG_DSP_TX_RATE_FIRST + i * 8, 1);
	}
	// The spec gives two ranges, 0x02BC3000-0x02BC304C and
	// 0x02BC3008-0x02BC305C; every 32-bit register of their union is
	// opened.
	_Write(REG_DSP_PROTECT_LOCK, DSP_PROTECT_UNLOCK_1);
	_Write(REG_DSP_PROTECT_LOCK, DSP_PROTECT_UNLOCK_2);
	for (uint32 reg = REG_DSP_PROTECT_FIRST; reg <= REG_DSP_PROTECT_LAST;
			reg += 4) {
		_Write(reg, 0xffffffff);
	}
	_Write(REG_DSP_PROTECT_LOCK, 0);

	_Update(REG_DSP_CORE_CONTROL, DSP_CORE_ENABLE | DSP_CORE_START,
		DSP_CORE_ENABLE | DSP_CORE_START);
	_Update(REG_DSP_CORE_CONTROL, DSP_CORE_START, 0);

	uint32 state = 0;
	bool running = false;
	for (int32 i = 0; i < STATE_POLL_TRIES && !running; i++) {
		snooze(1000);
		if (_Read(stateRegister, &state) == B_OK
			&& (state & 0x00ffffff) == FIRMWARE_STATE_RUNNING) {
			running = true;
		}
	}
	uint32 mailbox = 0xffffffff;
	_Read(REG_MAILBOX_STATUS, &mailbox);
	if (!running || (mailbox != MAILBOX_STATUS_RUNNING
			&& mailbox != MAILBOX_STATUS_PAUSED)) {
		TRACE_ALWAYS("amp %" B_PRIu32 " (%s): DSP state %#" B_PRIx32
			", mailbox %#" B_PRIx32 "\n", fIndex, Side(), state, mailbox);
		_FallBackToBypass("DSP did not run");
		return B_TIMED_OUT;
	}

	if (_Mailbox(MAILBOX_PAUSE, MAILBOX_STATUS_PAUSED) != B_OK) {
		_FallBackToBypass("DSP did not pause");
		return B_ERROR;
	}

	fDspRunning = true;
	TRACE_ALWAYS("amp %" B_PRIu32 " (%s): %s + %s, DSP state %" B_PRIu32
		" (running), paused, %" B_PRIdBIGTIME " ms\n", fIndex, Side(),
		program.name, tuning.name, state & 0x00ffffff,
		(system_time() - start) / 1000);
	return B_OK;
}
