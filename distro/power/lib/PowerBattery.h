/*
 * Copyright 2009-2015, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
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
 * Private to libr5power.so and PowerStatus: the ACPI battery reader that was
 * PowerStatus's ACPIDriverInterface (src/apps/powerstatus), moved into the
 * library so it exists once (docs/POWER.md, section 3). The watching thread
 * and the APM interface stayed in PowerStatus.
 * SPDX-SnippetEnd
 */
#ifndef _R5_POWER_BATTERY_H
#define _R5_POWER_BATTERY_H


#include <Locker.h>
#include <ObjectList.h>

#include "device/power_managment.h"


namespace BPrivate {


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// PowerStatus's own battery_info, unchanged
struct power_status_battery_info {
	int8		state;
	int32		capacity;
	int32		full_capacity;
	time_t		time_left;
	int32		current_rate;
};
// SPDX-SnippetEnd


const int8 kRateBufferSize = 10;


class RateBuffer {
public:
								RateBuffer();
			void				AddRate(int32 rate);
			int32				GetMeanRate();

private:
			int32				fRateBuffer[kRateBufferSize];
			int8				fPosition;
			int8				fSize;
			int8				fCurrentSize;
};


class Battery {
public:
								Battery(int driverHandler);
								~Battery();

			status_t			InitCheck();

	// Read battery info and update the cache.
			status_t			UpdateBatteryInfo();
			status_t			GetBatteryInfoCached(
									power_status_battery_info* info);
			status_t			GetExtendedBatteryInfo(
									acpi_extended_battery_info* info);

private:
			void				_Init();

private:
			int					fDriverHandler;
			status_t			fInitStatus;

			acpi_extended_battery_info	fExtendedBatteryInfo;
			RateBuffer			fRateBuffer;
			acpi_battery_info	fCachedInfo;
};


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
/*!	The batteries under /dev/power, as PowerStatus's ACPIDriverInterface
	found and read them. Locked inside; any thread may call it.
*/
class BatteryReader {
public:
								BatteryReader();
								~BatteryReader();

			status_t			Connect();
				// finds the batteries; B_OK when there is at least one
			int32				CountBatteries();
			status_t			Update();
				// reads every battery again
			status_t			GetBatteryInfo(int32 index,
									power_status_battery_info* info);
			status_t			GetExtendedBatteryInfo(int32 index,
									acpi_extended_battery_info* info);

private:
			status_t			_FindDrivers(const char* dirpath);

			BLocker				fLock;
			BObjectList<Battery> fDriverList;
};
// SPDX-SnippetEnd


}	// namespace BPrivate


#endif	// _R5_POWER_BATTERY_H
