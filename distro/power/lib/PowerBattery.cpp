/*
 * Copyright 2009-2015, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *		Clemens Zeidler, haiku@clemens-zeidler.de
 */

/*
 * R5OS modifications in this file are MIT, like the original work. They are
 * wrapped in SPDX-Snippet markers. The copyright and license notice above still
 * cover the original work.
 *
 * SPDX-SnippetBegin
 * SPDX-SnippetCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 * This is PowerStatus's ACPIDriverInterface.cpp (src/apps/powerstatus),
 * moved into libr5power.so (docs/POWER.md, section 3). Battery and
 * RateBuffer are unchanged; ACPIDriverInterface became BatteryReader,
 * without the watching thread, which stayed in PowerStatus.
 * SPDX-SnippetEnd
 */


#include "PowerBattery.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <Autolock.h>
#include <Directory.h>
#include <Entry.h>
#include <Path.h>


namespace BPrivate {


static const char* kDriverDir = "/dev/power";


RateBuffer::RateBuffer()
	:
	fPosition(0),
	fSize(kRateBufferSize),
	fCurrentSize(0)
{
}


void
RateBuffer::AddRate(int32 rate)
{
	fRateBuffer[fPosition] = rate;
	fPosition ++;
	if (fPosition >= fSize)
		fPosition = 0;

	if (fCurrentSize < fSize)
		fCurrentSize ++;
}


int32
RateBuffer::GetMeanRate()
{
	int32 mean = 0;
	for (int8 i = 0; i < fCurrentSize; i++) {
		mean += fRateBuffer[i];
	}

	if (fCurrentSize == 0)
		return -1;

	return mean / fCurrentSize;
}


// #pragma mark -


Battery::Battery(int driverHandler)
	:
	fDriverHandler(driverHandler)
{
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// UpdateBatteryInfo() compares with the cache before it was ever read
	memset(&fCachedInfo, 0, sizeof(fCachedInfo));
	memset(&fExtendedBatteryInfo, 0, sizeof(fExtendedBatteryInfo));
// SPDX-SnippetEnd
	_Init();
}


Battery::~Battery()
{
	close(fDriverHandler);
}


status_t
Battery::InitCheck()
{
	return fInitStatus;
}


status_t
Battery::UpdateBatteryInfo()
{
	acpi_battery_info info;
	if (ioctl(fDriverHandler, GET_BATTERY_INFO, &info,
			sizeof(acpi_battery_info)) != 0)
		return errno;

	if ((fExtendedBatteryInfo.last_full_charge > 0
			&& info.capacity > fExtendedBatteryInfo.last_full_charge)
		|| info.capacity < 0)
		return B_BAD_DATA;

	if ((fCachedInfo.capacity == 0 && info.capacity > 0)
		|| (fCachedInfo.capacity > 0 && info.capacity == 0)) {
		// if capacity went from zero to non-zero or vice-versa, we likely just had a battery
		// connected or disconnected from the system so refresh the full fExtendedBatteryInfo
		GetExtendedBatteryInfo(&fExtendedBatteryInfo);
	}

	fCachedInfo = info;
	return B_OK;
}


status_t
Battery::GetBatteryInfoCached(power_status_battery_info* info)
{
	info->state = fCachedInfo.state;
	info->current_rate = fCachedInfo.current_rate;
	info->capacity = fCachedInfo.capacity;
	info->full_capacity = fExtendedBatteryInfo.last_full_charge;
	if (info->full_capacity < 0)
		info->full_capacity = fExtendedBatteryInfo.design_capacity;

	fRateBuffer.AddRate(fCachedInfo.current_rate);
	if (fCachedInfo.current_rate > 0 && fRateBuffer.GetMeanRate() != 0) {
		info->time_left = 3600 * fCachedInfo.capacity
			/ fRateBuffer.GetMeanRate();
	} else
		info->time_left = -1;

	return B_OK;
}


status_t
Battery::GetExtendedBatteryInfo(acpi_extended_battery_info* info)
{
	if (ioctl(fDriverHandler, GET_EXTENDED_BATTERY_INFO, info,
			sizeof(acpi_extended_battery_info)) != 0)
		return errno;

	return B_OK;
}


void
Battery::_Init()
{
	uint32 magicId = 0;
	if (ioctl(fDriverHandler, IDENTIFY_DEVICE, &magicId, sizeof(uint32)) != 0) {
		fInitStatus = errno;
		return;
	}
	fInitStatus = GetExtendedBatteryInfo(&fExtendedBatteryInfo);
	if (fInitStatus != B_OK)
		return;

	UpdateBatteryInfo();
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// #pragma mark - BatteryReader (was ACPIDriverInterface)


BatteryReader::BatteryReader()
	:
	fLock("battery reader")
{
}


BatteryReader::~BatteryReader()
{
	for (int i = 0; i < fDriverList.CountItems(); i++)
		delete fDriverList.ItemAt(i);
}


status_t
BatteryReader::Connect()
{
	BAutolock autolock(fLock);
	return _FindDrivers(kDriverDir);
}


int32
BatteryReader::CountBatteries()
{
	BAutolock autolock(fLock);
	return fDriverList.CountItems();
}


status_t
BatteryReader::Update()
{
	BAutolock autolock(fLock);
	for (int i = 0; i < fDriverList.CountItems(); i++)
		fDriverList.ItemAt(i)->UpdateBatteryInfo();
	return B_OK;
}


status_t
BatteryReader::GetBatteryInfo(int32 index, power_status_battery_info* info)
{
	BAutolock autolock(fLock);
	if (index < 0 || index >= fDriverList.CountItems())
		return B_ERROR;

	return fDriverList.ItemAt(index)->GetBatteryInfoCached(info);
}


status_t
BatteryReader::GetExtendedBatteryInfo(int32 index,
	acpi_extended_battery_info* info)
{
	BAutolock autolock(fLock);
	if (index < 0 || index >= fDriverList.CountItems())
		return B_ERROR;

	return fDriverList.ItemAt(index)->GetExtendedBatteryInfo(info);
}
// SPDX-SnippetEnd


status_t
BatteryReader::_FindDrivers(const char* dirpath)
{
	BDirectory dir(dirpath);
	BEntry entry;

	status_t status = B_ERROR;

	while (dir.GetNextEntry(&entry) == B_OK) {
		BPath path;
		entry.GetPath(&path);

		if (entry.IsDirectory()) {
			if (_FindDrivers(path.Path()) == B_OK)
				return B_OK;
		} else {
			int32 handler = open(path.Path(), O_RDWR);
			if (handler >= 0) {
				Battery* battery = new Battery(handler);
				if (battery->InitCheck() == B_OK
					&& fDriverList.AddItem(battery)) {
					status = B_OK;
				} else
					delete battery;
			}
		}
	}
	return status;
}


}	// namespace BPrivate
