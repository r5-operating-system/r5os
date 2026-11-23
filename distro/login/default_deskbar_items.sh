#!/bin/sh
# SPDX-FileCopyrightText: 2026 R5OS Contributors
# SPDX-License-Identifier: MIT

# Deskbar replicants come from r5-tray and Login desklink.
# ProcessController -deskbar opens a window and showed Damned on quit.

# The battery, once per person (this script runs at the first sign-in,
# after Deskbar is up). PowerStatus --deskbar quits without a word on a
# computer without a battery, and when its item is already there; a
# person who removes it later keeps it removed.
if [ -x /boot/system/apps/PowerStatus ] \
		&& ls /dev/power/acpi_battery/ 2>/dev/null | grep -q .; then
	/boot/system/apps/PowerStatus --deskbar &
fi
true
