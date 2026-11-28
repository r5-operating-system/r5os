/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Version-neutral part of the DCN display track (docs/DISPLAY-DCN.md).
 *
 * - The version table: the radeon_hd device table (driver.cpp) gives each
 *   PCI ID its display version (RHD_DISPLAY_DCN, major, minor). This file
 *   maps that version to one dcn_ops; a later generation adds a row and its
 *   own dcnNN_*.cpp files, nothing else.
 * - The crash marker and the switch-off for this boot.
 * - The DCN lock. Only this file takes it. It is never held together with a
 *   GPU lock: no DCN path calls into the GFX9, GART or RADV code.
 * - The VBIOS gpio_pin_lut lookup (atomfirmware.h atom_gpio_pin_lut_v2_1, as
 *   bios_parser2.c get_gpio_i2c_info() and bios_parser_get_gpio_pin_info()
 *   read it) and the EDID helpers.
 */


#include "dcn_display.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <Debug.h>
#include <OS.h>
#include <FindDirectory.h>
#include <driver_settings.h>

#include "accelerant.h"
#include "accelerant_protos.h"
#include "bios.h"
#include "connector.h"
#include "edid.h"


#define DCNLOG(x...) _sPrintf("radeon_hd: dcn: " x)


struct dcn_version {
	uint8			major;
	uint8			minor;
	const dcn_ops*	ops;
};

// The one table from display version to code. The PCI IDs that have this
// version are in the radeon_hd device table, nowhere else.
static const dcn_version kDcnVersions[] = {
	{ 2, 1, &gDcn21Ops },
};


struct dcn_records {
	bool	hasI2c;
	uint8	i2cId;
	bool	hasHpd;
	uint8	hpdPin;
};

static dcn_records sRecords[ATOM_MAX_SUPPORTED_DEVICE];
static pthread_mutex_t sDcnLock = PTHREAD_MUTEX_INITIALIZER;
static bool sDcnOff = false;
static char sMarkerPath[B_PATH_NAME_LENGTH];
static uint64 sSettingsGeneration = 0;

// atomfirmware.h: master data table format 2, gpio_pin_lut is index 12
// (atom_master_list_of_data_tables_v2_1). gpio_general_populate() uses the
// same index.
static const int kGpioPinLutIndex = 12;
static const uint32 kPinBytes = 8;


void
dcn_note_i2c_record(uint32 connectorIndex, uint8 i2cId)
{
	if (connectorIndex >= ATOM_MAX_SUPPORTED_DEVICE)
		return;
	sRecords[connectorIndex].hasI2c = true;
	sRecords[connectorIndex].i2cId = i2cId;
}


void
dcn_note_hpd_record(uint32 connectorIndex, uint8 pinId)
{
	if (connectorIndex >= ATOM_MAX_SUPPORTED_DEVICE)
		return;
	sRecords[connectorIndex].hasHpd = true;
	sRecords[connectorIndex].hpdPin = pinId;
}


bool
dcn_vbios_pin_lookup(uint8 gpioId, uint32* regIndex, uint8* bitShift)
{
	uint16 size = 0;
	uint8 frev = 0;
	uint8 crev = 0;
	uint16 offset = 0;
	if (atom_parse_data_header(gAtomContext, kGpioPinLutIndex, &size, &frev,
			&crev, &offset) != B_OK)
		return false;
	// bios_parser2.c get_gpio_i2c_info() only knows content revision 1.
	if (crev != 1 || size < 4 + kPinBytes)
		return false;
	// The table must lie inside the VBIOS image
	// (bios_parser2.c GET_IMAGE checks against bios_size).
	uint32 romSize = gInfo->shared_info->rom_size;
	if (gAtomContext == NULL || gAtomContext->bios == NULL || romSize == 0
		|| (uint32)offset + (uint32)size > romSize)
		return false;

	const uint8* table = gAtomContext->bios + offset;
	uint32 count = (size - 4) / kPinBytes;
	for (uint32 i = 0; i < count; i++) {
		const uint8* pin = table + 4 + i * kPinBytes;
		if (pin[6] != gpioId)
			continue;
		*regIndex = (uint32)pin[0] | ((uint32)pin[1] << 8)
			| ((uint32)pin[2] << 16) | ((uint32)pin[3] << 24);
		*bitShift = pin[4];
		return true;
	}
	return false;
}


bool
dcn_setting(const char* name, bool defaultValue)
{
	bool value = defaultValue;
	void* settings = load_driver_settings("radeon_hd");
	if (settings != NULL) {
		value = get_driver_boolean_parameter(settings, name, defaultValue,
			true);
		unload_driver_settings(settings);
	}
	return value;
}


void
dcn_switch_off(const char* why)
{
	if (!sDcnOff)
		DCNLOG("off for this boot: %s; the panel stays on the firmware "
			"picture\n", why);
	sDcnOff = true;
}


bool
dcn_is_off()
{
	return sDcnOff;
}


// The crash marker -----------------------------------------------------------


// The settings generation: a change of either radeon_hd settings file (the
// stick's boot-next writes one) gives DCN one new try.
static uint64
dcn_settings_generation()
{
	uint64 generation = 0;
	const directory_which dirs[] = {
		B_USER_SETTINGS_DIRECTORY, B_SYSTEM_SETTINGS_DIRECTORY
	};
	for (size_t i = 0; i < B_COUNT_OF(dirs); i++) {
		char path[B_PATH_NAME_LENGTH];
		if (find_directory(dirs[i], -1, false, path, sizeof(path)) != B_OK)
			continue;
		strlcat(path, "/kernel/drivers/radeon_hd", sizeof(path));
		struct stat st;
		if (stat(path, &st) == 0)
			generation += (uint64)st.st_mtim.tv_sec * 1000003ull
				+ (uint64)st.st_mtim.tv_nsec + (uint64)st.st_size;
	}
	return generation;
}


static bool
dcn_marker_write(const char* text)
{
	int fd = open(sMarkerPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return false;
	ssize_t length = (ssize_t)strlen(text);
	bool ok = write(fd, text, length) == length;
	// The marker must be on the disk before the step runs.
	ok = fsync(fd) == 0 && ok;
	close(fd);
	return ok;
}


// Returns false when DCN must stay off this boot.
static bool
dcn_marker_start()
{
	sSettingsGeneration = dcn_settings_generation();

	// Leaves room for the file name in sMarkerPath.
	char dir[B_PATH_NAME_LENGTH - 32];
	if (find_directory(B_SYSTEM_SETTINGS_DIRECTORY, -1, false, dir,
			sizeof(dir)) != B_OK || access(dir, W_OK) != 0) {
		if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, true, dir,
				sizeof(dir)) != B_OK) {
			dcn_switch_off("no directory for the crash marker");
			return false;
		}
	}
	snprintf(sMarkerPath, sizeof(sMarkerPath), "%s/radeon_hd_dcn_marker",
		dir);

	FILE* file = fopen(sMarkerPath, "r");
	if (file == NULL)
		return true;
	char state[16] = "";
	char step[32] = "";
	unsigned long long generation = 0;
	int fields = fscanf(file, "%15s %31s %llu", state, step, &generation);
	fclose(file);
	if (fields != 3) {
		unlink(sMarkerPath);
		return true;
	}

	if (generation != sSettingsGeneration) {
		DCNLOG("marker \"%s %s\" is from older settings, DCN tries again\n",
			state, step);
		unlink(sMarkerPath);
		return true;
	}
	if (strcmp(state, "armed") == 0) {
		DCNLOG("the last start did not finish step %s (crash or hang)\n",
			step);
		char text[96];
		snprintf(text, sizeof(text), "failed %s %llu\n", step, generation);
		dcn_marker_write(text);
	} else
		DCNLOG("step %s failed before with these settings\n", step);
	dcn_switch_off("crash marker; change the radeon_hd settings to try again");
	return false;
}


bool
dcn_marker_arm(const char* step)
{
	char text[96];
	snprintf(text, sizeof(text), "armed %s %llu\n", step,
		(unsigned long long)sSettingsGeneration);
	if (dcn_marker_write(text))
		return true;
	// Without the marker on the disk a crash in this step could repeat at
	// every start: the step does not run.
	dcn_switch_off("the crash marker could not be written");
	return false;
}


void
dcn_marker_clear()
{
	unlink(sMarkerPath);
}


// EDID ---------------------------------------------------------------------


bool
dcn_timing_from_edid(const edid1_info* edid, uint32 maxKHz,
	dcn_timing* timing)
{
	for (int i = 0; i < EDID1_NUM_DETAILED_MONITOR_DESC; i++) {
		const edid1_detailed_monitor* monitor = &edid->detailed_monitor[i];
		if (monitor->monitor_desc_type != EDID1_IS_DETAILED_TIMING)
			continue;
		const edid1_detailed_timing& t = monitor->data.detailed_timing;
		uint32 khz = (uint32)t.pixel_clock * 10;
		if (khz == 0 || t.interlaced || khz > maxKHz || t.h_active == 0
			|| t.v_active == 0 || t.h_sync_width == 0 || t.v_sync_width == 0) {
			DCNLOG("edid timing %ux%u %" B_PRIu32 " kHz%s skipped (limit %"
				B_PRIu32 " kHz, progressive)\n", (unsigned)t.h_active,
				(unsigned)t.v_active, khz, t.interlaced ? " interlaced" : "",
				maxKHz);
			continue;
		}
		timing->pixelKHz = khz;
		timing->hActive = t.h_active;
		timing->hTotal = (uint32)t.h_active + t.h_blank;
		timing->hFrontPorch = t.h_sync_off;
		timing->hSync = t.h_sync_width;
		timing->vActive = t.v_active;
		timing->vTotal = (uint32)t.v_active + t.v_blank;
		// apply_front_porch_workaround() (dcn10_optc.c): progressive needs
		// at least one front porch line.
		timing->vFrontPorch = t.v_sync_off > 0 ? t.v_sync_off : 1;
		timing->vSync = t.v_sync_width;
		// create_display_modes.cpp: digital separate sync (3), misc bit 0
		// is the horizontal and bit 1 the vertical polarity.
		timing->hPositive = t.sync == 3 && (t.misc & 1) != 0;
		timing->vPositive = t.sync == 3 && (t.misc & 2) != 0;
		if (timing->hFrontPorch + timing->hSync >= t.h_blank
			|| timing->vFrontPorch + timing->vSync >= t.v_blank
			|| timing->hTotal > 0x7fff || timing->vTotal > 0x7fff) {
			DCNLOG("edid timing %" B_PRIu32 "x%" B_PRIu32 " is out of range\n",
				timing->hActive, timing->vActive);
			continue;
		}
		return true;
	}
	return false;
}


static void
dcn_log_edid(uint32 id, const edid1_info* edid)
{
	char name[EDID1_EXTRA_STRING_LEN + 1] = "";
	const edid1_detailed_timing* timing = NULL;
	for (int i = 0; i < EDID1_NUM_DETAILED_MONITOR_DESC; i++) {
		const edid1_detailed_monitor* monitor = &edid->detailed_monitor[i];
		if (monitor->monitor_desc_type == EDID1_MONITOR_NAME && name[0] == 0) {
			memcpy(name, monitor->data.monitor_name, EDID1_EXTRA_STRING_LEN);
			name[EDID1_EXTRA_STRING_LEN] = 0;
			for (int c = 0; name[c] != 0; c++) {
				if (name[c] == '\n') {
					name[c] = 0;
					break;
				}
			}
			for (int c = (int)strlen(name) - 1; c >= 0 && name[c] == ' '; c--)
				name[c] = 0;
			// Printable ASCII only in the syslog.
			for (int c = 0; name[c] != 0; c++) {
				if (name[c] < 0x20 || name[c] > 0x7e)
					name[c] = '?';
			}
		} else if (monitor->monitor_desc_type == EDID1_IS_DETAILED_TIMING
			&& timing == NULL) {
			timing = &monitor->data.detailed_timing;
		}
	}

	_sPrintf("radeon_hd: dcn hdmi: connector %" B_PRIu32 " monitor \"%s\""
		" vendor %.3s product 0x%04x edid %u.%u\n", id,
		name[0] != 0 ? name : "?", edid->vendor.manufacturer,
		(unsigned)edid->vendor.prod_id, (unsigned)edid->version.version,
		(unsigned)edid->version.revision);

	if (timing == NULL || timing->pixel_clock == 0) {
		_sPrintf("radeon_hd: dcn hdmi: connector %" B_PRIu32
			" preferred mode none\n", id);
		return;
	}
	uint64 hTotal = (uint64)timing->h_active + timing->h_blank;
	uint64 vTotal = (uint64)timing->v_active + timing->v_blank;
	uint64 pixelHz = (uint64)timing->pixel_clock * 10000;
	uint64 centiHz = hTotal * vTotal > 0 ? pixelHz * 100 / (hTotal * vTotal) : 0;
	_sPrintf("radeon_hd: dcn hdmi: connector %" B_PRIu32 " preferred mode %ux%u"
		"%s %" B_PRIu64 ".%02" B_PRIu64 " Hz pixel clock %" B_PRIu32 " kHz\n",
		id, (unsigned)timing->h_active, (unsigned)timing->v_active,
		timing->interlaced ? "i" : "", centiHz / 100, centiHz % 100,
		(uint32)timing->pixel_clock * 10);
}


// The probe ----------------------------------------------------------------


static const dcn_ops*
dcn_find_ops()
{
	radeon_shared_info& info = *gInfo->shared_info;
	for (size_t i = 0; i < B_COUNT_OF(kDcnVersions); i++) {
		if (kDcnVersions[i].major == info.displayMajor
			&& kDcnVersions[i].minor == info.displayMinor)
			return kDcnVersions[i].ops;
	}
	return NULL;
}


static void
dcn_probe_locked()
{
	const dcn_ops* ops = dcn_find_ops();
	if (ops == NULL) {
		DCNLOG("no code for DCN %u.%u, skipped\n",
			(unsigned)gInfo->shared_info->displayMajor,
			(unsigned)gInfo->shared_info->displayMinor);
		return;
	}
	if (!dcn_setting("dcn_hdmi_probe", true)) {
		DCNLOG("off (dcn_hdmi_probe false)\n");
		return;
	}
	if (!dcn_marker_start())
		return;

	DCNLOG("%s\n", ops->name);

	// At most one diagnostic path per boot, named in the settings. The
	// marker names it on the disk before it runs, so a path that hangs the
	// machine is reported by the next start and not run again.
	char path[32] = "";
	void* settings = load_driver_settings("radeon_hd");
	if (settings != NULL) {
		const char* value = get_driver_parameter(settings, "dcn_probe_path",
			NULL, NULL);
		if (value != NULL)
			strlcpy(path, value, sizeof(path));
		unload_driver_settings(settings);
	}
	if (path[0] != 0) {
		char step[48];
		snprintf(step, sizeof(step), "path-%s", path);
		DCNLOG("dcn_probe_path %s: arming the marker, then the access\n",
			path);
		if (!dcn_marker_arm(step))
			return;
		ops->probe_path(path);
		dcn_marker_clear();
	}
	// Check register access BEFORE log_pipes(): each non-answering DCN read
	// takes ~139 ms on the 5625U, so running log_pipes() first added ~9.5 s
	// of timeouts per boot. No DCN register is written before the registers
	// are known to answer (stick 1 of PR 4712).
	if (!dcn_marker_arm("pipes"))
		return;
	char why[128] = "";
	status_t access = ops->check_access(why, sizeof(why));
	if (access != B_OK) {
		dcn_marker_clear();
		DCNLOG("registers do not answer: %s\n", why);
		dcn_switch_off("the display registers do not answer");
		return;
	}
	DCNLOG("registers answer: %s\n", why);
	ops->log_pipes();
	dcn_marker_clear();

	bool started = false;
	uint32 found = 0;
	for (uint32 id = 0; id < ATOM_MAX_SUPPORTED_DEVICE && !dcn_is_off(); id++) {
		connector_info* connector = gConnector[id];
		if (connector == NULL || !connector->valid)
			continue;
		uint32 type = connector->type;
		if (type != VIDEO_CONNECTOR_HDMIA && type != VIDEO_CONNECTOR_HDMIB
			&& type != VIDEO_CONNECTOR_DVID && type != VIDEO_CONNECTOR_DVII)
			continue;
		found++;

		const dcn_records& records = sRecords[id];
		dcn_connector_state state;
		edid1_info edid;
		memset(&state, 0, sizeof(state));
		memset(&edid, 0, sizeof(edid));
		if (!dcn_marker_arm("hotplug"))
			break;
		status_t status = ops->probe_connector(id, records.hasI2c,
			records.i2cId, records.hasHpd, records.hpdPin, &state, &edid);
		dcn_marker_clear();
		if (status != B_OK) {
			dcn_switch_off("hotplug probe failed");
			break;
		}
		if (!state.edidFound)
			continue;
		dcn_log_edid(id, &edid);

		if (started || !dcn_setting("dcn_hdmi_modeset", false))
			continue;
		started = true;
		if (!dcn_marker_arm("head"))
			break;
		status = ops->head_start(&state, &edid);
		dcn_marker_clear();
		if (status != B_OK)
			dcn_switch_off("the HDMI head did not start");
	}
	if (found == 0)
		DCNLOG("no HDMI or DVI connector in the VBIOS\n");
}


void
dcn_display_probe()
{
	static bool sDone = false;
	pthread_mutex_lock(&sDcnLock);
	if (!sDone) {
		sDone = true;
		// The time the display track adds to the accelerant's start (the
		// crash marker's fsync on a USB stick is most of it).
		bigtime_t start = system_time();
		dcn_probe_locked();
		DCNLOG("probe took %" B_PRId64 " us\n", system_time() - start);
	}
	pthread_mutex_unlock(&sDcnLock);
}
