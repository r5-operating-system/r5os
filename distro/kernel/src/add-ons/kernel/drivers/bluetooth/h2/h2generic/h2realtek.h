/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Firmware for Realtek Bluetooth radios (docs/BLUETOOTH-2.md, step B0).
 */
#ifndef _H2REALTEK_H_
#define _H2REALTEK_H_

#include "h2generic.h"

/* Hand a Realtek radio its patch, then log what it reports.
 *
 * Returns B_OK for a device this has nothing to do with, and for a Realtek
 * radio it has no firmware entry for (it is used as it is). Returns an error,
 * after one syslog line, when the radio's firmware is missing, does not fit
 * the radio, or is refused: the radio then stays off.
 */
status_t realtek_setup(bt_usb_dev* bdev);

/* Forget that a radio was patched, when it is unplugged. */
void realtek_forget(bt_usb_dev* bdev);

#endif /* _H2REALTEK_H_ */
