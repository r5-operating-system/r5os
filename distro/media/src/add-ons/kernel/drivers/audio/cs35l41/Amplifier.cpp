/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


/*!	One CS35L41 amplifier: power-up, the DSP bypass playback path and the
	protection faults. Written from the R5 clean-room specification
	(generated/CS35L41-SPEC.md sections 2, 4 and 5, CS35L41-SPEC-2.md
	sections 1 and 4). No DSP firmware is loaded in phase 1.

	The caller holds the driver lock around every public call.
*/


#include "Amplifier.h"

#include <string.h>

#include <ByteOrder.h>
#include <KernelExport.h>
#include <hmulti_audio.h>

#include "OtpMap.h"
#include "Registers.h"


#define TRACE_ALWAYS(a...)	dprintf("cs35l41: " a)


/*!	The hard volume limit of phase 1.

	Without the DSP firmware the amplifier has no speaker protection: no
	excursion or temperature model watches the speakers. The specification
	gives one PCM gain for the bypass path, 0x84 in REG_AMP_GAIN: PCM gain
	field (bits 9:5) 4, which it names 4.5 dB, and PDM gain 4 in bits 4:0.
	Phase 1 writes the PCM gain field as 0, its lowest step, four steps
	below that value and 31 below the top of the field. The specification
	does not give the size of a step, so no dB figure is claimed for the
	cap. The PDM gain field keeps the documented 4 (the PDM input is not
	used). No setting raises this value; phase 2 replaces it only when
	the speaker protection firmware runs.
*/
static const uint32 kPcmGainCap = 0;
static const uint32 kPdmGain = 4;
static const uint32 kCappedGain = (kPcmGainCap << AMP_GAIN_PCM_SHIFT)
	| kPdmGain;

/*!	The gain with the speaker protection firmware running: the tuned gain
	in bits 9:5 and the default PDM gain (CS35L41-SPEC-2.md section 4). No
	tuning configuration file is installed, so the tuned gain is the
	default PCM gain, 4.
*/
static const uint32 kDefaultPcmGain = 4;
static const uint32 kFirmwareGain = (kDefaultPcmGain << AMP_GAIN_PCM_SHIFT)
	| kPdmGain;


struct register_value {
	uint32	address;
	uint32	value;
};

// Silicon errata, written with the test key unlocked.
static const register_value kErrataA0[] = {
	{ 0x00003854, 0x05180240 },
	{ 0x00004100, 0x00000000 },
	{ 0x00004310, 0x00000000 },
	{ 0x00004400, 0x00000000 },
	{ 0x00007418, 0x9091a1c8 },
	{ 0x00003014, 0x0200ee0e },
	{ 0x0000381c, 0x00000051 },
	{ 0x00000054, 0x00000004 },
	{ 0x00010318, 0x00000000 },
	{ 0x00010b18, 0x00000000 },
	{ 0x02bc20e0, 0x00000000 },
	{ 0x02bc2020, 0x00000000 },
	{ 0x00002018, 0x00000000 },
	{ 0x00006c04, 0x00000000 },
	{ 0x00004c28, 0x00000000 },
	{ 0x00004c2c, 0x00000000 },
};

static const register_value kErrataB0[] = {
	{ 0x00004100, 0x00000000 },
	{ 0x00004310, 0x00000000 },
	{ 0x00004400, 0x00000000 },
	{ 0x0000381c, 0x00000051 },
	{ 0x02bc20e0, 0x00000000 },
	{ 0x02bc2020, 0x00000000 },
	{ 0x00002018, 0x00000000 },
	{ 0x00006c04, 0x00000000 },
	{ 0x00004c28, 0x00000000 },
	{ 0x00004c2c, 0x00000000 },
};

// Playback setup shared by both paths (spec section 4, "Stream prepare").
static const register_value kPrepareCommon[] = {
	{ REG_PLL_CLOCK, 0x00000430 },		// reference = 3.072 MHz BCLK
	{ REG_DSP_CLOCK, 0x00000003 },
	{ REG_GLOBAL_RATE, 0x00000003 },	// 48 kHz
	{ REG_ASP_BCLK, 0x00000021 },		// 3.072 MHz
	{ REG_ASP_FORMAT, 0x20200200 },		// 32-bit slots, I2S, consumer
	{ REG_ASP_TX_WIDTH, 0x00000018 },	// 24 bit
	{ REG_ASP_RX_WIDTH, 0x00000018 },	// 24 bit
	{ REG_ROUTE_ASP_TX1, 0x00000018 },	// VMON
	{ REG_ROUTE_ASP_TX2, 0x00000019 },	// IMON
	{ REG_ROUTE_DSP_RX3, 0x00000018 },	// VMON
	{ REG_ROUTE_DSP_RX4, 0x00000019 },	// IMON
};

// DSP bypass.
static const register_value kPrepareBypass[] = {
	{ REG_ASP_CONTROL, 0x00000002 },
	{ REG_ROUTE_DAC, 0x00000008 },		// DAC = ASP RX1
	{ REG_ROUTE_ASP_TX3, 0x00000000 },
	{ REG_ROUTE_ASP_TX4, 0x00000000 },
	{ REG_ROUTE_DSP_RX5, 0x00000020 },
	{ REG_ROUTE_DSP_RX6, 0x00000021 },
	{ REG_ASP_ENABLES, 0x00010000 },	// ASP RX1
};

// DSP firmware running.
static const register_value kPrepareDsp[] = {
	{ REG_ASP_CONTROL, 0x00000003 },
	{ REG_ROUTE_DAC, 0x00000032 },		// DAC = DSP1 TX1
	{ REG_ROUTE_ASP_TX3, 0x00000028 },	// VPMON
	{ REG_ROUTE_ASP_TX4, 0x00000029 },	// VBSTMON
	{ REG_ROUTE_DSP_RX5, 0x00000028 },	// VPMON, internal boost
	{ REG_ROUTE_DSP_RX6, 0x00000029 },	// VBSTMON
	{ REG_ASP_ENABLES, 0x00010001 },	// ASP RX1 and TX1
};

static const register_value kPrepareFinal[] = {
	{ REG_ROUTE_DSP_RX1, 0x00000008 },
	{ REG_ROUTE_DSP_RX2, 0x00000008 },
};

// The six protection faults in REG_IRQ_STATUS_1 and their release bits in
// REG_PROTECT_RELEASE (spec section 5).
static const struct {
	uint32		status;
	uint32		release;
	const char*	name;
} kFaults[] = {
	{ 0x00000040, 0x08, "boost overvoltage" },
	{ 0x00000080, 0x10, "boost undervoltage" },
	{ 0x00000100, 0x04, "boost inductor short" },
	{ 0x00008000, 0x20, "overtemperature warning" },
	{ 0x00020000, 0x40, "overtemperature" },
	{ 0x80000000, 0x02, "amplifier short" },
};

static const uint32 kMaxFaults = 3;


static uint32
fault_mask()
{
	uint32 mask = 0;
	for (size_t i = 0; i < B_COUNT_OF(kFaults); i++)
		mask |= kFaults[i].status;
	return mask;
}


Amplifier::Amplifier(const i2c_link& link, const machine_info& machine,
	uint32 index, i2c_addr address)
	:
	fLink(link),
	fMachine(machine),
	fIndex(index),
	fAddress(address),
	fRevision(0),
	fReady(false),
	fPrepared(false),
	fRunning(false),
	fCodecMuted(false),
	fAcpiMuted(false),
	fFaulted(false),
	fFaultCount(0),
	fFaultsSeen(0),
	fDspRunning(false),
	fBlockSize(16),
	fFirmwareID(0),
	fAlgorithmCount(0)
{
}


const char*
Amplifier::Side() const
{
	return fIndex == 0 ? "left" : fIndex == 1 ? "right" : "extra";
}


status_t
Amplifier::_Read(uint32 reg, uint32* value)
{
	uint8 command[4];
	*(uint32*)command = B_HOST_TO_BENDIAN_INT32(reg);
	uint8 data[4];

	status_t status = fLink.bus->acquire_bus(fLink.cookie);
	if (status != B_OK)
		return status;
	for (int32 tries = 0; tries < 3; tries++) {
		status = fLink.bus->exec_command(fLink.cookie, I2C_OP_READ_STOP,
			fAddress, command, sizeof(command), data, sizeof(data));
		if (status != B_BUSY)
			break;
		snooze(1000);
	}
	fLink.bus->release_bus(fLink.cookie);

	if (status == B_OK)
		*value = B_BENDIAN_TO_HOST_INT32(*(uint32*)data);
	return status;
}


status_t
Amplifier::_Write(uint32 reg, uint32 value)
{
	uint8 command[8];
	*(uint32*)command = B_HOST_TO_BENDIAN_INT32(reg);
	*(uint32*)(command + 4) = B_HOST_TO_BENDIAN_INT32(value);

	status_t status = fLink.bus->acquire_bus(fLink.cookie);
	if (status != B_OK)
		return status;
	for (int32 tries = 0; tries < 3; tries++) {
		status = fLink.bus->exec_command(fLink.cookie, I2C_OP_WRITE_STOP,
			fAddress, command, sizeof(command), NULL, 0);
		if (status != B_BUSY)
			break;
		snooze(1000);
	}
	fLink.bus->release_bus(fLink.cookie);

	if (status != B_OK) {
		TRACE_ALWAYS("amp %" B_PRIu32 " write %08" B_PRIx32 " failed: %s\n",
			fIndex, reg, strerror(status));
	}
	return status;
}


status_t
Amplifier::_Update(uint32 reg, uint32 mask, uint32 value)
{
	uint32 current;
	status_t status = _Read(reg, &current);
	if (status != B_OK)
		return status;
	return _Write(reg, (current & ~mask) | (value & mask));
}


/*!	Polls \a reg every millisecond until all \a bits are set. */
status_t
Amplifier::_WaitFor(uint32 reg, uint32 bits, bigtime_t timeout,
	bigtime_t* _waited)
{
	bigtime_t start = system_time();
	while (true) {
		uint32 value;
		status_t status = _Read(reg, &value);
		if (status != B_OK)
			return status;
		if ((value & bits) == bits) {
			if (_waited != NULL)
				*_waited = system_time() - start;
			return B_OK;
		}
		if (system_time() - start > timeout)
			return B_TIMED_OUT;
		snooze(1000);
	}
}


status_t
Amplifier::_CheckChip()
{
	// Wait for the OTP boot, then check it did not fail.
	status_t status = _WaitFor(REG_IRQ_STATUS_4, IRQ4_OTP_BOOT_DONE, 100000,
		NULL);
	if (status != B_OK) {
		TRACE_ALWAYS("amp %" B_PRIu32 " (%s) i2c 0x%02x: no OTP boot (%s)\n",
			fIndex, Side(), fAddress, strerror(status));
		return status;
	}
	uint32 value;
	if ((status = _Read(REG_IRQ_STATUS_3, &value)) != B_OK)
		return status;
	if ((value & IRQ3_OTP_BOOT_ERROR) != 0) {
		TRACE_ALWAYS("amp %" B_PRIu32 " (%s): OTP boot error\n", fIndex,
			Side());
		return B_IO_ERROR;
	}

	uint32 deviceID;
	if ((status = _Read(REG_DEVICE_ID, &deviceID)) != B_OK
		|| (status = _Read(REG_REVISION_ID, &fRevision)) != B_OK) {
		return status;
	}
	uint32 expected = (fRevision & 1) != 0
		? DEVICE_ID_ODD_REVISION : DEVICE_ID_EVEN_REVISION;
	if (deviceID != expected) {
		TRACE_ALWAYS("amp %" B_PRIu32 " (%s): device id %#" B_PRIx32
			", expected %#" B_PRIx32 " for revision %#" B_PRIx32 "\n",
			fIndex, Side(), deviceID, expected, fRevision);
		return B_DEVICE_NOT_FOUND;
	}
	return B_OK;
}


status_t
Amplifier::_ApplyErrata()
{
	const register_value* errata;
	size_t count;
	switch (fRevision) {
		case 0xa0:
			errata = kErrataA0;
			count = B_COUNT_OF(kErrataA0);
			break;
		case 0xb0:
		case 0xb2:
			errata = kErrataB0;
			count = B_COUNT_OF(kErrataB0);
			break;
		default:
			TRACE_ALWAYS("amp %" B_PRIu32 ": no errata for revision %#"
				B_PRIx32 "; amplifier left off\n", fIndex, fRevision);
			return B_NOT_SUPPORTED;
	}

	for (size_t i = 0; i < count; i++) {
		status_t status = _Write(errata[i].address, errata[i].value);
		if (status != B_OK)
			return status;
	}
	// The DSP core clock control, cleared on every revision.
	return _Write(REG_DSP_CORE_CONTROL, 0);
}


status_t
Amplifier::_ApplyOtp()
{
	uint32 otpID;
	status_t status = _Read(REG_OTP_ID, &otpID);
	if (status != B_OK)
		return status;

	bool mapB;
	switch (otpID & 0xff) {
		case 0x01:
		case 0x08:
			mapB = false;
			break;
		case 0x02:
		case 0x03:
		case 0x06:
			mapB = true;
			break;
		default:
			TRACE_ALWAYS("amp %" B_PRIu32 ": unknown OTP id %#" B_PRIx32
				"; amplifier left off\n", fIndex, otpID);
			return B_NOT_SUPPORTED;
	}

	uint32 words[OTP_WORDS];
	for (uint32 i = 0; i < OTP_WORDS; i++) {
		if ((status = _Read(REG_OTP_MEMORY + i * 4, &words[i])) != B_OK)
			return status;
	}

	// The fields follow each other least significant bit first and may
	// cross a word boundary, so read them as one bit stream.
	uint32 position = OTP_FIRST_BIT;
	uint32 written = 0;
	for (size_t i = 0; i < B_COUNT_OF(kOtpFields); i++) {
		const otp_field& field = kOtpFields[i];
		if (position + field.width > OTP_WORDS * 32)
			return B_BAD_DATA;

		uint32 value = 0;
		for (uint32 bit = 0; bit < field.width; bit++) {
			uint32 at = position + bit;
			value |= ((words[at / 32] >> (at % 32)) & 1) << bit;
		}
		position += field.width;

		if (field.map_b_only && !mapB)
			continue;

		uint32 mask = (field.width == 32 ? 0xffffffff
			: ((1u << field.width) - 1)) << field.shift;
		status = _Update(field.register_address, mask, value << field.shift);
		if (status != B_OK)
			return status;
		written++;
	}

	TRACE_ALWAYS("amp %" B_PRIu32 " (%s): OTP id %#" B_PRIx32 " map %c, %"
		B_PRIu32 " trim fields\n", fIndex, Side(), otpID, mapB ? 'B' : 'A',
		written);
	return B_OK;
}


status_t
Amplifier::_SetupBoost()
{
	status_t status;
	if ((status = _Update(REG_BOOST_LOOP, BOOST_LOOP_MASK,
			fMachine.boost_loop)) != B_OK
		|| (status = _Update(REG_BOOST_SLOPE,
			BOOST_INDUCTOR_MASK | BOOST_SLOPE_MASK,
			fMachine.boost_inductor
				| (fMachine.boost_slope << BOOST_SLOPE_SHIFT))) != B_OK) {
		return status;
	}

	// Peak current: ((mA - 1600) / 50) + 0x10.
	if (fMachine.peak_current < 1600)
		return B_BAD_VALUE;
	uint32 peak = (fMachine.peak_current - 1600) / 50 + 0x10;
	if ((status = _Update(REG_BOOST_PEAK, BOOST_PEAK_MASK, peak)) != B_OK)
		return status;

	return _Update(REG_POWER_BLOCKS, POWER_BOOST_MASK, POWER_BOOST_INTERNAL);
}


status_t
Amplifier::_SetupPads()
{
	// GPIO1 unused, GPIO2 the open-drain active-low interrupt output.
	status_t status;
	if ((status = _Update(REG_GPIO1_CONTROL, GPIO_POLARITY | GPIO_NOT_OUTPUT,
			GPIO_NOT_OUTPUT)) != B_OK
		|| (status = _Update(REG_GPIO2_CONTROL,
			GPIO_POLARITY | GPIO_NOT_OUTPUT, GPIO_NOT_OUTPUT)) != B_OK
		|| (status = _Update(REG_PAD_CONTROL, PAD_GPIO1_MASK | PAD_GPIO2_MASK,
			PAD_GPIO2_OPEN_DRAIN_IRQ)) != B_OK) {
		return status;
	}

	uint32 slot = fIndex < B_COUNT_OF(fMachine.slot) ? fMachine.slot[fIndex]
		: fIndex;
	return _Update(REG_ASP_RX_SLOT, ASP_RX_SLOT_MASK, slot);
}


/*!	Spec section 2, steps 2 to 10, after the shared hardware reset. */
status_t
Amplifier::PowerUp()
{
	bigtime_t start = system_time();
	fReady = false;

	status_t status = _Write(REG_SOFT_RESET, SOFT_RESET_VALUE);
	if (status != B_OK) {
		TRACE_ALWAYS("amp %" B_PRIu32 " (%s) i2c bus %" B_PRIu32 " 0x%02x:"
			" no answer (%s)\n", fIndex, Side(), fLink.path_id, fAddress,
			strerror(status));
		return status;
	}
	snooze(2000);

	if ((status = _CheckChip()) != B_OK)
		return status;

	if ((status = _Write(REG_TEST_KEY, TEST_KEY_UNLOCK_1)) != B_OK
		|| (status = _Write(REG_TEST_KEY, TEST_KEY_UNLOCK_2)) != B_OK) {
		return status;
	}
	status = _ApplyErrata();
	if (status == B_OK)
		status = _ApplyOtp();
	// Lock the test key again in every case.
	status_t lockStatus = _Write(REG_TEST_KEY, TEST_KEY_LOCK_1);
	if (lockStatus == B_OK)
		lockStatus = _Write(REG_TEST_KEY, TEST_KEY_LOCK_2);
	if (status != B_OK)
		return status;
	if (lockStatus != B_OK)
		return lockStatus;

	// Muted before the boost and the pads are set up.
	if ((status = _Write(REG_AMP_GAIN, AMP_GAIN_MUTE)) != B_OK
		|| (status = _Write(REG_DIGITAL_VOLUME, DIGITAL_VOLUME_MUTE)) != B_OK) {
		return status;
	}

	if ((status = _SetupBoost()) != B_OK || (status = _SetupPads()) != B_OK)
		return status;

	fReady = true;
	TRACE_ALWAYS("amp %" B_PRIu32 " (%s) i2c bus %" B_PRIu32 " 0x%02x: id"
		" %#x rev %#" B_PRIx32 ", boost %" B_PRIu32 " nH %" B_PRIu32 " uF %"
		B_PRIu32 " mA, slot %" B_PRIu32 ", ready in %" B_PRIdBIGTIME " ms\n",
		fIndex, Side(), fLink.path_id, fAddress,
		(fRevision & 1) != 0 ? DEVICE_ID_ODD_REVISION
			: DEVICE_ID_EVEN_REVISION,
		fRevision, fMachine.inductance, fMachine.capacitance,
		fMachine.peak_current,
		fIndex < B_COUNT_OF(fMachine.slot) ? fMachine.slot[fIndex] : fIndex,
		(system_time() - start) / 1000);
	return B_OK;
}


status_t
Amplifier::Prepare(uint32 rate)
{
	if (!fReady)
		return B_NO_INIT;
	if (fFaultCount >= kMaxFaults)
		return B_NOT_ALLOWED;
	if (rate != B_SR_48000) {
		TRACE_ALWAYS("amp %" B_PRIu32 ": stream rate %#" B_PRIx32 " is not"
			" 48 kHz; amplifier stays off\n", fIndex, rate);
		return B_BAD_VALUE;
	}

	status_t status = _WriteTable(kPrepareCommon,
		B_COUNT_OF(kPrepareCommon));
	if (status != B_OK)
		return status;

	if (fDspRunning) {
		status = _WriteTable(kPrepareDsp, B_COUNT_OF(kPrepareDsp));
		if (status == B_OK)
			status = _WriteTable(kPrepareFinal, B_COUNT_OF(kPrepareFinal));
		if (status == B_OK) {
			status = _Update(REG_POWER_BLOCKS, POWER_VMON_IMON,
				POWER_VMON_IMON);
		}
		if (status == B_OK)
			status = _Mailbox(MAILBOX_RESUME, MAILBOX_STATUS_RUNNING);
		if (status != B_OK) {
			_Update(REG_POWER_BLOCKS, POWER_VMON_IMON, 0);
			_FallBackToBypass("DSP did not resume");
		}
	}
	if (!fDspRunning) {
		status = _WriteTable(kPrepareBypass, B_COUNT_OF(kPrepareBypass));
		if (status == B_OK)
			status = _WriteTable(kPrepareFinal, B_COUNT_OF(kPrepareFinal));
		if (status != B_OK)
			return status;
	}

	status = _Update(REG_POWER_BLOCKS, POWER_AMP_ENABLE, POWER_AMP_ENABLE);
	if (status != B_OK)
		return status;

	fPrepared = true;
	return B_OK;
}


status_t
Amplifier::Start()
{
	if (!fPrepared)
		return B_NO_INIT;

	status_t status = _Update(REG_POWER_GLOBAL, POWER_GLOBAL_ENABLE,
		POWER_GLOBAL_ENABLE);
	if (status != B_OK)
		return status;

	bigtime_t waited = 0;
	status = _WaitFor(REG_IRQ_STATUS_1, IRQ1_POWER_UP_DONE, 100000, &waited);
	if (status != B_OK) {
		TRACE_ALWAYS("amp %" B_PRIu32 " (%s): no power-up done (%s)\n",
			fIndex, Side(), strerror(status));
		_Update(REG_POWER_GLOBAL, POWER_GLOBAL_ENABLE, 0);
		return status;
	}
	_Write(REG_IRQ_STATUS_1, IRQ1_POWER_UP_DONE);

	fRunning = true;
	fFaulted = false;
	_ApplyMute();
	return B_OK;
}


void
Amplifier::Stop()
{
	if (!fRunning)
		return;
	fRunning = false;
	_ApplyMute();

	_Update(REG_POWER_GLOBAL, POWER_GLOBAL_ENABLE, 0);
	if (_WaitFor(REG_IRQ_STATUS_1, IRQ1_POWER_DOWN_DONE, 100000, NULL)
			== B_OK) {
		_Write(REG_IRQ_STATUS_1, IRQ1_POWER_DOWN_DONE);
	} else {
		TRACE_ALWAYS("amp %" B_PRIu32 " (%s): no power-down done\n", fIndex,
			Side());
	}
}


void
Amplifier::Unprepare()
{
	if (!fPrepared)
		return;
	fPrepared = false;

	_Update(REG_POWER_BLOCKS, POWER_AMP_ENABLE, 0);
	if (fDspRunning) {
		if (_Mailbox(MAILBOX_PAUSE, MAILBOX_STATUS_PAUSED) != B_OK)
			_FallBackToBypass("DSP did not pause");
		_Update(REG_POWER_BLOCKS, POWER_VMON_IMON, 0);
	}

	uint32 status = 0;
	_Read(REG_IRQ_STATUS_1, &status);
	uint32 released = _ReleaseFaults();
	TRACE_ALWAYS("amp %" B_PRIu32 " (%s) stop: status %#010" B_PRIx32
		"%s, %s\n", fIndex, Side(), status,
		released != 0 ? ", faults released" : "",
		fDspRunning ? "DSP paused" : "bypass");
}


status_t
Amplifier::_WriteTable(const register_value* table, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		status_t status = _Write(table[i].address, table[i].value);
		if (status != B_OK)
			return status;
	}
	return B_OK;
}


/*!	Takes the amplifier out of its safe mode after a protection fault.
	Only called with the global enable off.
*/
uint32
Amplifier::_ReleaseFaults()
{
	uint32 status = 0;
	if (_Read(REG_IRQ_STATUS_1, &status) != B_OK)
		return 0;

	uint32 faults = (status | fFaultsSeen) & fault_mask();
	fFaultsSeen = 0;
	if (faults == 0)
		return 0;

	uint32 release = 0;
	for (size_t i = 0; i < B_COUNT_OF(kFaults); i++) {
		if ((faults & kFaults[i].status) != 0)
			release |= kFaults[i].release;
	}
	_Write(REG_PROTECT_RELEASE, 0);
	_Write(REG_PROTECT_RELEASE, release);
	_Write(REG_PROTECT_RELEASE, 0);
	_Write(REG_IRQ_STATUS_1, faults);
	return faults;
}


void
Amplifier::CheckFaults()
{
	if (!fRunning || fFaulted)
		return;

	uint32 status;
	if (_Read(REG_IRQ_STATUS_1, &status) != B_OK)
		return;
	uint32 faults = status & fault_mask();
	if (faults == 0)
		return;

	fFaulted = true;
	fFaultsSeen |= faults;
	fFaultCount++;
	_ApplyMute();

	for (size_t i = 0; i < B_COUNT_OF(kFaults); i++) {
		if ((faults & kFaults[i].status) != 0) {
			TRACE_ALWAYS("amp %" B_PRIu32 " (%s) fault: %s (%#010" B_PRIx32
				"), muted\n", fIndex, Side(), kFaults[i].name, status);
		}
	}
	if (fFaultCount >= kMaxFaults) {
		TRACE_ALWAYS("amp %" B_PRIu32 " (%s): %" B_PRIu32 " faults, off until"
			" restart\n", fIndex, Side(), fFaultCount);
	}
}


void
Amplifier::SetCodecMute(bool mute)
{
	fCodecMuted = mute;
	_ApplyMute();
}


void
Amplifier::SetAcpiMute(bool mute)
{
	if (fAcpiMuted != mute) {
		TRACE_ALWAYS("amp %" B_PRIu32 " (%s): ACPI mute %s\n", fIndex, Side(),
			mute ? "on" : "off");
	}
	fAcpiMuted = mute;
	_ApplyMute();
}


void
Amplifier::_ApplyMute()
{
	if (!fReady)
		return;

	bool play = fRunning && !fCodecMuted && !fAcpiMuted && !fFaulted
		&& fFaultCount < kMaxFaults;
	if (play) {
		_Write(REG_DIGITAL_VOLUME, DIGITAL_VOLUME_0DB);
		_Write(REG_AMP_GAIN, fDspRunning ? kFirmwareGain : kCappedGain);
	} else {
		_Write(REG_AMP_GAIN, AMP_GAIN_MUTE);
		_Write(REG_DIGITAL_VOLUME, DIGITAL_VOLUME_MUTE);
	}
}


void
Amplifier::Shutdown()
{
	Stop();
	Unprepare();
	if (fDspRunning) {
		_StopCore();
		fDspRunning = false;
	}
	if (fReady) {
		_Write(REG_AMP_GAIN, AMP_GAIN_MUTE);
		_Write(REG_DIGITAL_VOLUME, DIGITAL_VOLUME_MUTE);
	}
	fReady = false;
}


/*!	The B_SR_* rates of the bypass path: the clocks are set for 48 kHz. */
/*static*/ uint32
Amplifier::SupportedRates()
{
	return B_SR_48000;
}
