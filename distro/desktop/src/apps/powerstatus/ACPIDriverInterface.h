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
 */
#ifndef ACPI_DRIVER_INTERFACE_H
#define ACPI_DRIVER_INTERFACE_H


#include "DriverInterface.h"

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
// The battery reading (Battery, RateBuffer, finding the drivers) moved into
// libr5power.so (docs/POWER.md, section 3). This class keeps PowerStatus's
// interface and its watching thread, and reads through the library.
#include <PowerBattery.h>


class ACPIDriverInterface : public PowerStatusDriverInterface {
public:
								ACPIDriverInterface();
	virtual						~ACPIDriverInterface();

	virtual status_t			Connect();
	virtual status_t			GetBatteryInfo(int32 index, battery_info* info);
	virtual status_t			GetExtendedBatteryInfo(int32 index,
									acpi_extended_battery_info* info);
	virtual int32				GetBatteryCount();

protected:
	virtual void				_WatchPowerStatus();

private:
			BPrivate::BatteryReader	fReader;
};
// SPDX-SnippetEnd


#endif	// ACPI_DRIVER_INTERFACE_H
