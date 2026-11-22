/*	$OpenBSD: rtsxvar.h,v 1.6 2017/10/09 16:12:20 stsp Exp $	*/

/*
 * Copyright (c) 2006 Uwe Stuehler <uwe@openbsd.org>
 * Copyright (c) 2012 Stefan Sperling <stsp@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * SPDX-License-Identifier: ISC
 *
 * The softc of OpenBSD rtsx(4), on Haiku kernel primitives. The R5 parts
 * (Haiku fields, the RTS5228 state and flag) are MIT, marked below.
 */

#ifndef _RTSXVAR_H_
#define _RTSXVAR_H_

#include <KernelExport.h>
#include <bus/PCI.h>
#include <condition_variable.h>
#include <lock.h>

#include "mmc.h"

/* Number of registers to save for suspend/resume in terms of their ranges. */
#define RTSX_NREG ((0XFDAE - 0XFDA0) + (0xFD69 - 0xFD32) + (0xFE34 - 0xFE20))

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT

/* One entry of the device table in rtsx_haiku.cpp. */
struct rtsx_chip {
	uint16		device;		/* PCI device id, vendor 0x10ec */
	const char*	name;
	int			flags;		/* RTSX_F_* chip flags */
	uint8		bar;		/* index of the register BAR */
};

/* RTS5228 board and PCIe settings, read once at attach (rtsx_5228.cpp). */
struct rtsx_5228_state {
	uint8		ic_version;
	uint8		aspm_bits;		/* configured ASPM enable bits */
	uint8		drive_1v8;		/* pad drive index for 1.8 V */
	uint8		drive_3v3;		/* pad drive index for 3.3 V */
	bool		rtd3;
	bool		mmc_disabled;
	bool		reverse_socket;
	bool		cd_invert;
	bool		wp_invert;
	bool		l1ss_present;
	uint8		l1ss_bits;		/* L1 PM substates control 1, bits 3..0 */
	bool		ltr_enabled;
	bool		aspm_was_enabled;
	uint8		pcie_cap;		/* offset of the PCIe capability */
	uint32		cur_clock_mhz;	/* internal SSC clock last programmed */
};
// SPDX-SnippetEnd

struct rtsx_softc {
	int			flags;
	uint32		intr_status;	/* soft interrupt status */
	uint8		regs[RTSX_NREG];/* host controller state */
	uint32		regs4[6];		/* host controller state */

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	const rtsx_chip*		chip;
	device_node*			node;
	pci_device_module_info*	pci;
	pci_device*				device;

	area_id			regs_area;
	volatile uint8*	regs_base;		/* mapped register BAR */

	area_id			dma_area;
	uint8*			dma_virt;		/* host command buffer, then ADMA table */
	phys_addr_t		dma_phys;

	uint32			irq;
	bool			msi;
	bool			irq_installed;
	spinlock		intr_lock;		/* protects intr_status */
	ConditionVariable intr_cond;

	mutex			lock;			/* one chip operation at a time */
	sem_id			card_sem;		/* card detect and overcurrent events */
	thread_id		card_thread;
	int32			card_events;	/* RTSX_EVENT_* from the interrupt */
	volatile bool	quit;
	sem_id			scan_sem;		/* mmc_bus rescan semaphore */
	card_type		type;
	bool			powered;		/* slot powered for the current card */
	bool			announced;		/* mmc_bus told about the current card */
	bool			oc_tripped;		/* overcurrent: off until reinserted */
	bool			shutdown_hook;
	int32			command_failures;	/* logged with the chip state */

	rtsx_5228_state	r5228;
	// SPDX-SnippetEnd
};

/* Host controller functions (rtsx.cpp). */
int	rtsx_attach(struct rtsx_softc *, int);
int32	rtsx_intr(void *);

/* flag values */
#define	RTSX_F_CARD_PRESENT	0x01
#define	RTSX_F_SDIO_SUPPORT	0x02
#define	RTSX_F_5209		0x04
#define	RTSX_F_5229		0x08
#define	RTSX_F_5229_TYPE_C	0x10
#define	RTSX_F_525A		0x20
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#define	RTSX_F_5228		0x40

/* Events the interrupt hands to the card thread. */
#define	RTSX_EVENT_CARD		0x01
#define	RTSX_EVENT_OVERCURRENT	0x02
// SPDX-SnippetEnd

#endif
