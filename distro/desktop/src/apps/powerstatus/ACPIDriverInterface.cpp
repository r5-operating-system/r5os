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
 */


#include "ACPIDriverInterface.h"


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// The reading itself is libr5power.so's BPrivate::BatteryReader, which is
// this file's former Battery, RateBuffer and _FindDrivers() (docs/POWER.md,
// section 3).


ACPIDriverInterface::ACPIDriverInterface()
{
}


ACPIDriverInterface::~ACPIDriverInterface()
{
}


status_t
ACPIDriverInterface::Connect()
{
	return fReader.Connect();
}


status_t
ACPIDriverInterface::GetBatteryInfo(int32 index, battery_info* info)
{
	BPrivate::power_status_battery_info status;
	status_t result = fReader.GetBatteryInfo(index, &status);
	if (result != B_OK)
		return result;

	info->state = status.state;
	info->capacity = status.capacity;
	info->full_capacity = status.full_capacity;
	info->time_left = status.time_left;
	info->current_rate = status.current_rate;
	return B_OK;
}


status_t
ACPIDriverInterface::GetExtendedBatteryInfo(int32 index,
	acpi_extended_battery_info* info)
{
	return fReader.GetExtendedBatteryInfo(index, info);
}


int32
ACPIDriverInterface::GetBatteryCount()
{
	return fReader.CountBatteries();
}
// SPDX-SnippetEnd


void
ACPIDriverInterface::_WatchPowerStatus()
{
	const bigtime_t kUpdateInterval = 2000000;
		// every two seconds

	while (atomic_get(&fIsWatching) > 0) {
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
		fReader.Update();
// SPDX-SnippetEnd
		Broadcast(kMsgUpdate);
		acquire_sem_etc(fWaitSem, 1, B_RELATIVE_TIMEOUT, kUpdateInterval);
	}
}
