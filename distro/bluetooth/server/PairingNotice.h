/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * The notification at the end of a pairing started here (PairingNotice.cpp).
 */
#ifndef R5_PAIRING_NOTICE_H
#define R5_PAIRING_NOTICE_H


#include <String.h>


void r5_pairing_notice(bool paired, const BString& name);


#endif	// R5_PAIRING_NOTICE_H
