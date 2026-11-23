/*	$OpenBSD: rtsx.c,v 1.22 2020/02/18 00:06:56 cheloha Exp $	*/

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
 * Realtek RTS52xx/RTL84xx Card Reader driver.
 *
 * The chip logic of OpenBSD rtsx(4) on Haiku kernel primitives. The chip
 * sequences of the OpenBSD chips are unchanged. The R5 parts are MIT and
 * marked: Haiku waiting and DMA, and the calls into the RTS5228 part
 * (rtsx_5228.cpp), which was written from a clean-room specification.
 * The bounce-buffer path of OpenBSD is not here: the Haiku I/O scheduler
 * always hands physical, DMA-safe vectors.
 */

#include <ByteOrder.h>
#include <KernelExport.h>
#include <errno.h>
#include <string.h>

#include "rtsxreg.h"
#include "rtsx.h"
#include "rtsx_5228.h"

#define READ4(sc, reg)		rtsx_read4((sc), (reg))
#define WRITE4(sc, reg, val)	rtsx_write4((sc), (reg), (val))

#define	RTSX_READ(sc, reg, val) 				\
	do { 							\
		int err = rtsx_read((sc), (reg), (val)); 	\
		if (err) 					\
			return (err);				\
	} while (0)

#define	RTSX_WRITE(sc, reg, val) 				\
	do { 							\
		int err = rtsx_write((sc), (reg), 0xff, (val));	\
		if (err) 					\
			return (err);				\
	} while (0)

#define	RTSX_CLR(sc, reg, bits)					\
	do { 							\
		int err = rtsx_write((sc), (reg), (bits), 0); 	\
		if (err) 					\
			return (err);				\
	} while (0)

#define	RTSX_SET(sc, reg, bits)					\
	do { 							\
		int err = rtsx_write((sc), (reg), (bits), 0xff);\
		if (err) 					\
			return (err);				\
	} while (0)

#define	ISSET(t, f)	((t) & (f))
#define	SET(t, f)	((t) |= (f))
#define	delay(us)	spin(us)

static int	rtsx_bus_power_off(struct rtsx_softc *);
static int	rtsx_bus_power_on(struct rtsx_softc *);
static int	rtsx_set_bus_width(struct rtsx_softc *, int);
static int	rtsx_stop_sd_clock(struct rtsx_softc *);
static int	rtsx_switch_sd_clock(struct rtsx_softc *, uint8, int, int);
static int	rtsx_wait_intr(struct rtsx_softc *, uint32, int);
static void	rtsx_hostcmd(uint32 *, int *, uint8, uint16, uint8, uint8);
static int	rtsx_hostcmd_send(struct rtsx_softc *, int);
static int	rtsx_xfer(struct rtsx_softc *, struct rtsx_command *,
		    uint32 *);
static int	rtsx_xfer_adma(struct rtsx_softc *, struct rtsx_command *);

#ifdef RTSX_DEBUG
#define DPRINTF(n,s)	dprintf s
#else
#define DPRINTF(n,s)	do {} while(0)
#endif

#define	DEVNAME(sc)	"rtsx"


/*
 * Called by the attachment code (rtsx_haiku.cpp).
 */
int
rtsx_attach(struct rtsx_softc *sc, int attaching)
{
	uint32 sdio_cfg;

	if (rtsx_init(sc, attaching))
		return 1;

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	/* On the RTS5228, 0x724 holds the board settings, not SDIO bits. */
	if (sc->flags & RTSX_F_5228)
		return 0;
	// SPDX-SnippetEnd

	if (rtsx_read_cfg(sc, 0, RTSX_SDIOCFG_REG, &sdio_cfg) == 0) {
		if ((sdio_cfg & RTSX_SDIOCFG_SDIO_ONLY) ||
		    (sdio_cfg & RTSX_SDIOCFG_HAVE_SDIO))
			atomic_or(&sc->flags, RTSX_F_SDIO_SUPPORT);
	}

	return 0;
}

int
rtsx_init(struct rtsx_softc *sc, int attaching)
{
	uint32 status;
	uint8 version;
	int error;

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	if (sc->flags & RTSX_F_5228) {
		if (rtsx_5228_init(sc) != 0)
			return 1;
		status = READ4(sc, RTSX_BIPR);
		WRITE4(sc, RTSX_BIPR, status);
		if (attaching && (status & RTSX_SD_EXIST))
			atomic_or(&sc->flags, RTSX_F_CARD_PRESENT);
		return 0;
	}
	// SPDX-SnippetEnd

	/* Read IC version from dummy register. */
	if (sc->flags & RTSX_F_5229) {
		RTSX_READ(sc, RTSX_DUMMY_REG, &version);
		switch (version & 0x0F) {
		case RTSX_IC_VERSION_A:
		case RTSX_IC_VERSION_B:
		case RTSX_IC_VERSION_D:
			break;
		case RTSX_IC_VERSION_C:
			atomic_or(&sc->flags, RTSX_F_5229_TYPE_C);
			break;
		default:
			dprintf("rtsx_init: unknown ic %02x\n", version);
			return (1);
		}
	}

	/* Enable interrupt write-clear (default is read-clear). */
	RTSX_CLR(sc, RTSX_NFTS_TX_CTRL, RTSX_INT_READ_CLR);

	/* Clear any pending interrupts. */
	status = READ4(sc, RTSX_BIPR);
	WRITE4(sc, RTSX_BIPR, status);

	/* Check for cards already inserted at attach time. */
	if (attaching && (status & RTSX_SD_EXIST))
		atomic_or(&sc->flags, RTSX_F_CARD_PRESENT);

	/* Enable interrupts. */
	WRITE4(sc, RTSX_BIER,
	    RTSX_TRANS_OK_INT_EN | RTSX_TRANS_FAIL_INT_EN | RTSX_SD_INT_EN);

	/* Power on SSC clock. */
	RTSX_CLR(sc, RTSX_FPDCTL, RTSX_SSC_POWER_DOWN);
	delay(200);

	/* XXX magic numbers from linux driver */
	if (sc->flags & RTSX_F_5209)
		error = rtsx_write_phy(sc, 0x00, 0xB966);
	else
		error = rtsx_write_phy(sc, 0x00, 0xBA42);
	if (error) {
		dprintf("%s: cannot write phy register\n", DEVNAME(sc));
		return (1);
	}

	RTSX_SET(sc, RTSX_CLK_DIV, 0x07);

	/* Disable sleep mode. */
	RTSX_CLR(sc, RTSX_HOST_SLEEP_STATE,
	    RTSX_HOST_ENTER_S1 | RTSX_HOST_ENTER_S3);

	/* Disable card clock. */
	RTSX_CLR(sc, RTSX_CARD_CLK_EN, RTSX_CARD_CLK_EN_ALL);

	RTSX_CLR(sc, RTSX_CHANGE_LINK_STATE,
	    RTSX_FORCE_RST_CORE_EN | RTSX_NON_STICKY_RST_N_DBG | 0x04);
	RTSX_WRITE(sc, RTSX_SD30_DRIVE_SEL, RTSX_SD30_DRIVE_SEL_3V3);

	/* Enable SSC clock. */
	RTSX_WRITE(sc, RTSX_SSC_CTL1, RTSX_SSC_8X_EN | RTSX_SSC_SEL_4M);
	RTSX_WRITE(sc, RTSX_SSC_CTL2, 0x12);

	RTSX_SET(sc, RTSX_CHANGE_LINK_STATE, RTSX_MAC_PHY_RST_N_DBG);
	RTSX_SET(sc, RTSX_IRQSTAT0, RTSX_LINK_READY_INT);

	RTSX_WRITE(sc, RTSX_PERST_GLITCH_WIDTH, 0x80);

	/* Set RC oscillator to 400K. */
	RTSX_CLR(sc, RTSX_RCCTL, RTSX_RCCTL_F_2M);

	/* Request clock by driving CLKREQ pin to zero. */
	RTSX_SET(sc, RTSX_PETXCFG, RTSX_PETXCFG_CLKREQ_PIN);

	/* Set up LED GPIO. */
	if (sc->flags & RTSX_F_5209) {
		RTSX_WRITE(sc, RTSX_CARD_GPIO, 0x03);
		RTSX_WRITE(sc, RTSX_CARD_GPIO_DIR, 0x03);
	} else {
		RTSX_SET(sc, RTSX_GPIO_CTL, RTSX_GPIO_LED_ON);
		/* Switch LDO3318 source from DV33 to 3V3. */
		RTSX_CLR(sc, RTSX_LDO_PWR_SEL, RTSX_LDO_PWR_SEL_DV33);
		RTSX_SET(sc, RTSX_LDO_PWR_SEL, RTSX_LDO_PWR_SEL_3V3);
		/* Set default OLT blink period. */
		RTSX_SET(sc, RTSX_OLT_LED_CTL, RTSX_OLT_LED_PERIOD);
	}

	return (0);
}

int
rtsx_led_enable(struct rtsx_softc *sc)
{
	if (sc->flags & RTSX_F_5209) {
		RTSX_CLR(sc, RTSX_CARD_GPIO, RTSX_CARD_GPIO_LED_OFF);
		RTSX_WRITE(sc, RTSX_CARD_AUTO_BLINK,
		    RTSX_LED_BLINK_EN | RTSX_LED_BLINK_SPEED);
	} else {
		RTSX_SET(sc, RTSX_GPIO_CTL, RTSX_GPIO_LED_ON);
		RTSX_SET(sc, RTSX_OLT_LED_CTL, RTSX_OLT_LED_AUTOBLINK);
	}

	return 0;
}

int
rtsx_led_disable(struct rtsx_softc *sc)
{
	if (sc->flags & RTSX_F_5209) {
		RTSX_CLR(sc, RTSX_CARD_AUTO_BLINK, RTSX_LED_BLINK_EN);
		RTSX_WRITE(sc, RTSX_CARD_GPIO, RTSX_CARD_GPIO_LED_OFF);
	} else {
		RTSX_CLR(sc, RTSX_OLT_LED_CTL, RTSX_OLT_LED_AUTOBLINK);
		RTSX_CLR(sc, RTSX_GPIO_CTL, RTSX_GPIO_LED_ON);
	}

	return 0;
}

/*
 * Reset the host controller.  Called during initialization, when
 * cards are removed, upon resume, and during error recovery.
 */
int
rtsx_host_reset(struct rtsx_softc *sc)
{
	DPRINTF(1,("%s: host reset\n", DEVNAME(sc)));

	if (ISSET(sc->flags, RTSX_F_CARD_PRESENT))
		rtsx_soft_reset(sc);

	if (rtsx_init(sc, 0))
		return 1;

	return 0;
}

/*
 * Notice that the meaning of RTSX_PWR_GATE_CTRL changes between RTS5209 and
 * RTS5229. In RTS5209 it is a mask of disabled power gates, while in RTS5229
 * it is a mask of *enabled* gates.
 */

static int
rtsx_bus_power_off(struct rtsx_softc *sc)
{
	int error;
	uint8 disable3;

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	if (sc->flags & RTSX_F_5228)
		return rtsx_5228_power_off(sc);
	// SPDX-SnippetEnd

	error = rtsx_stop_sd_clock(sc);
	if (error)
		return error;

	/* Disable SD output. */
	RTSX_CLR(sc, RTSX_CARD_OE, RTSX_CARD_OUTPUT_EN);

	/* Turn off power. */
	disable3 = RTSX_PULL_CTL_DISABLE3;
	if (sc->flags & RTSX_F_5209)
		RTSX_SET(sc, RTSX_PWR_GATE_CTRL, RTSX_LDO3318_OFF);
	else {
		RTSX_CLR(sc, RTSX_PWR_GATE_CTRL, RTSX_LDO3318_VCC1 |
		    RTSX_LDO3318_VCC2);
		if (sc->flags & RTSX_F_5229_TYPE_C)
			disable3 = RTSX_PULL_CTL_DISABLE3_TYPE_C;
	}

	RTSX_SET(sc, RTSX_CARD_PWR_CTL, RTSX_SD_PWR_OFF);
	RTSX_CLR(sc, RTSX_CARD_PWR_CTL, RTSX_PMOS_STRG_800mA);

	/* Disable pull control. */
	RTSX_WRITE(sc, RTSX_CARD_PULL_CTL1, RTSX_PULL_CTL_DISABLE12);
	RTSX_WRITE(sc, RTSX_CARD_PULL_CTL2, RTSX_PULL_CTL_DISABLE12);
	RTSX_WRITE(sc, RTSX_CARD_PULL_CTL3, disable3);

	return 0;
}

static int
rtsx_bus_power_on(struct rtsx_softc *sc)
{
	uint8 enable3;
	int err;

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	if (sc->flags & RTSX_F_5228)
		return rtsx_5228_power_on(sc);
	// SPDX-SnippetEnd

	if (sc->flags & RTSX_F_525A) {
		err = rtsx_write(sc, RTSX_LDO_VCC_CFG1, RTSX_LDO_VCC_TUNE_MASK,
		    RTSX_LDO_VCC_3V3);
		if (err)
			return (err);
	}

	/* Select SD card. */
	RTSX_WRITE(sc, RTSX_CARD_SELECT, RTSX_SD_MOD_SEL);
	RTSX_WRITE(sc, RTSX_CARD_SHARE_MODE, RTSX_CARD_SHARE_48_SD);
	RTSX_SET(sc, RTSX_CARD_CLK_EN, RTSX_SD_CLK_EN);

	/* Enable pull control. */
	RTSX_WRITE(sc, RTSX_CARD_PULL_CTL1, RTSX_PULL_CTL_ENABLE12);
	RTSX_WRITE(sc, RTSX_CARD_PULL_CTL2, RTSX_PULL_CTL_ENABLE12);
	if (sc->flags & RTSX_F_5229_TYPE_C)
		enable3 = RTSX_PULL_CTL_ENABLE3_TYPE_C;
	else
		enable3 = RTSX_PULL_CTL_ENABLE3;
	RTSX_WRITE(sc, RTSX_CARD_PULL_CTL3, enable3);

	/*
	 * To avoid a current peak, enable card power in two phases with a
	 * delay in between.
	 */

	/* Partial power. */
	RTSX_SET(sc, RTSX_CARD_PWR_CTL, RTSX_SD_PARTIAL_PWR_ON);
	if (sc->flags & RTSX_F_5209)
		RTSX_SET(sc, RTSX_PWR_GATE_CTRL, RTSX_LDO3318_SUSPEND);
	else
		RTSX_SET(sc, RTSX_PWR_GATE_CTRL, RTSX_LDO3318_VCC1);

	delay(200);

	/* Full power. */
	RTSX_CLR(sc, RTSX_CARD_PWR_CTL, RTSX_SD_PWR_OFF);
	if (sc->flags & RTSX_F_5209)
		RTSX_CLR(sc, RTSX_PWR_GATE_CTRL, RTSX_LDO3318_OFF);
	else
		RTSX_SET(sc, RTSX_PWR_GATE_CTRL, RTSX_LDO3318_VCC2);

	/* Enable SD card output. */
	RTSX_WRITE(sc, RTSX_CARD_OE, RTSX_SD_OUTPUT_EN);

	return 0;
}

static int
rtsx_set_bus_width(struct rtsx_softc *sc, int w)
{
	uint32 bus_width;
	int error;

	switch (w) {
		case 8:
			bus_width = RTSX_BUS_WIDTH_8;
			break;
		case 4:
			bus_width = RTSX_BUS_WIDTH_4;
			break;
		case 1:
		default:
			bus_width = RTSX_BUS_WIDTH_1;
			break;
	}

	error = rtsx_write(sc, RTSX_SD_CFG1, RTSX_BUS_WIDTH_MASK, bus_width);
	return error;
}

static int
rtsx_stop_sd_clock(struct rtsx_softc *sc)
{
	RTSX_CLR(sc, RTSX_CARD_CLK_EN, RTSX_CARD_CLK_EN_ALL);
	RTSX_SET(sc, RTSX_SD_BUS_STAT, RTSX_SD_CLK_FORCE_STOP);

	return 0;
}

static int
rtsx_switch_sd_clock(struct rtsx_softc *sc, uint8 n, int div, int mcu)
{
	/* Enable SD 2.0 mode. */
	RTSX_CLR(sc, RTSX_SD_CFG1, RTSX_SD_MODE_MASK);

	RTSX_SET(sc, RTSX_CLK_CTL, RTSX_CLK_LOW_FREQ);

	RTSX_WRITE(sc, RTSX_CARD_CLK_SOURCE,
	    RTSX_CRC_FIX_CLK | RTSX_SD30_VAR_CLK0 | RTSX_SAMPLE_VAR_CLK1);
	RTSX_CLR(sc, RTSX_SD_SAMPLE_POINT_CTL, RTSX_SD20_RX_SEL_MASK);
	RTSX_WRITE(sc, RTSX_SD_PUSH_POINT_CTL, RTSX_SD20_TX_NEG_EDGE);
	RTSX_WRITE(sc, RTSX_CLK_DIV, (div << 4) | mcu);
	RTSX_CLR(sc, RTSX_SSC_CTL1, RTSX_RSTB);
	RTSX_CLR(sc, RTSX_SSC_CTL2, RTSX_SSC_DEPTH_MASK);
	RTSX_WRITE(sc, RTSX_SSC_DIV_N_0, n);
	RTSX_SET(sc, RTSX_SSC_CTL1, RTSX_RSTB);
	delay(100);

	RTSX_CLR(sc, RTSX_CLK_CTL, RTSX_CLK_LOW_FREQ);

	return 0;
}

/*
 * Enable or disable SD bus power. OpenBSD takes an OCR here; the Haiku
 * mmc_bus only knows 3.3 V, which every chip of this driver supports.
 * Return zero on success.
 */
int
rtsx_bus_power(struct rtsx_softc *sc, bool on)
{
	int error = 0;

	DPRINTF(1,("%s: bus power %d\n", DEVNAME(sc), on));

	/*
	 * Disable bus power before voltage change.
	 */
	error = rtsx_bus_power_off(sc);
	if (error)
		return error;

	delay(200);

	/* If power is disabled, reset the host and return now. */
	if (!on) {
		(void)rtsx_host_reset(sc);
		return 0;
	}

	error = rtsx_bus_power_on(sc);
	if (error)
		return error;

	error = rtsx_set_bus_width(sc, 1);
	return error;
}

/*
 * Set or change SDCLK frequency or disable the SD clock.
 * Return zero on success.
 */
int
rtsx_bus_clock(struct rtsx_softc *sc, int freq)
{
	uint8 n;
	int div;
	int mcu;
	int error = 0;

	if (freq == 0)
		return rtsx_stop_sd_clock(sc);

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	if (sc->flags & RTSX_F_5228) {
		/*
		 * SD 2.0 timing, as rtsx_switch_sd_clock() below sets it for
		 * every other chip (the SD path is the same, RTS5228 spec
		 * section 7.9). The RTS5228 power-on leaves SD 3.0 mode and a
		 * CRC clock from variable-phase clock 0, which only SDR50 and
		 * SDR104 set up; a command with a response then never ended
		 * (stick 75: CMD0 done, CMD8 no interrupt within 1 s).
		 */
		RTSX_CLR(sc, RTSX_SD_CFG1, RTSX_SD_MODE_MASK);
		RTSX_SET(sc, RTSX_CLK_CTL, RTSX_CLK_LOW_FREQ);
		RTSX_WRITE(sc, RTSX_CARD_CLK_SOURCE,
		    RTSX_CRC_FIX_CLK | RTSX_SD30_VAR_CLK0 | RTSX_SAMPLE_VAR_CLK1);
		RTSX_CLR(sc, RTSX_SD_SAMPLE_POINT_CTL, RTSX_SD20_RX_SEL_MASK);
		RTSX_WRITE(sc, RTSX_SD_PUSH_POINT_CTL, RTSX_SD20_TX_NEG_EDGE);
		RTSX_CLR(sc, RTSX_CLK_CTL, RTSX_CLK_LOW_FREQ);
		return rtsx_5228_set_clock(sc, freq);
	}
	// SPDX-SnippetEnd

	/* Round down to a supported frequency (kHz, as OpenBSD's sdmmc). */
	if (freq >= 50000)
		freq = 50000;
	else if (freq >= 25000)
		freq = 25000;
	else
		freq = 400;

	/*
	 * Configure the clock frequency.
	 */
	switch (freq) {
	case 400:
		n = 80; /* minimum */
		div = RTSX_CLK_DIV_8;
		mcu = 7;
		RTSX_SET(sc, RTSX_SD_CFG1, RTSX_CLK_DIVIDE_128);
		break;
	case 25000:
		n = 100;
		div = RTSX_CLK_DIV_4;
		mcu = 7;
		RTSX_CLR(sc, RTSX_SD_CFG1, RTSX_CLK_DIVIDE_MASK);
		break;
	case 50000:
		n = 100;
		div = RTSX_CLK_DIV_2;
		mcu = 7;
		RTSX_CLR(sc, RTSX_SD_CFG1, RTSX_CLK_DIVIDE_MASK);
		break;
	default:
		return EINVAL;
	}

	/*
	 * Enable SD clock.
	 */
	error = rtsx_switch_sd_clock(sc, n, div, mcu);
	return error;
}

int
rtsx_bus_width(struct rtsx_softc *sc, int width)
{
	return rtsx_set_bus_width(sc, width);
}

int
rtsx_read(struct rtsx_softc *sc, uint16 addr, uint8 *val)
{
	int tries = 1024;
	uint32 reg = 0;

	WRITE4(sc, RTSX_HAIMR, RTSX_HAIMR_BUSY |
	    (uint32)((addr & 0x3FFF) << 16));

	while (tries--) {
		reg = READ4(sc, RTSX_HAIMR);
		if (!(reg & RTSX_HAIMR_BUSY))
			break;
	}

	*val = (reg & 0xff);
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// while (n--) leaves n at -1 when it runs out, and at 0 when the
	// last try succeeds.
	return (tries < 0) ? ETIMEDOUT : 0;
	// SPDX-SnippetEnd
}

int
rtsx_write(struct rtsx_softc *sc, uint16 addr, uint8 mask, uint8 val)
{
	int tries = 1024;
	uint32 reg;

	WRITE4(sc, RTSX_HAIMR,
	    RTSX_HAIMR_BUSY | RTSX_HAIMR_WRITE |
	    (uint32)(((addr & 0x3FFF) << 16) |
	    (mask << 8) | val));

	while (tries--) {
		reg = READ4(sc, RTSX_HAIMR);
		if (!(reg & RTSX_HAIMR_BUSY)) {
			// SPDX-SnippetBegin
			// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
			// SPDX-License-Identifier: MIT
			/* The specification only promises completion. */
			if (sc->flags & RTSX_F_5228)
				return 0;
			// SPDX-SnippetEnd
			if (val != (reg & 0xff))
				return EIO;
			return 0;
		}
	}

	return ETIMEDOUT;
}

int
rtsx_read_phy(struct rtsx_softc *sc, uint8 addr, uint16 *val)
{
	int timeout = 100000;
	uint8 data0;
	uint8 data1;
	uint8 rwctl;

	RTSX_WRITE(sc, RTSX_PHY_ADDR, addr);
	RTSX_WRITE(sc, RTSX_PHY_RWCTL, RTSX_PHY_BUSY|RTSX_PHY_READ);

	while (timeout--) {
		RTSX_READ(sc, RTSX_PHY_RWCTL, &rwctl);
		if (!(rwctl & RTSX_PHY_BUSY))
			break;
	}

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// while (n--) leaves n at -1 when it runs out, and at 0 when the
	// last try succeeds.
	if (timeout < 0)
		return ETIMEDOUT;
	// SPDX-SnippetEnd

	RTSX_READ(sc, RTSX_PHY_DATA0, &data0);
	RTSX_READ(sc, RTSX_PHY_DATA1, &data1);
	*val = data0 | (data1 << 8);

	return 0;
}

int
rtsx_write_phy(struct rtsx_softc *sc, uint8 addr, uint16 val)
{
	int timeout = 100000;
	uint8 rwctl;

	RTSX_WRITE(sc, RTSX_PHY_DATA0, val);
	RTSX_WRITE(sc, RTSX_PHY_DATA1, val >> 8);
	RTSX_WRITE(sc, RTSX_PHY_ADDR, addr);
	RTSX_WRITE(sc, RTSX_PHY_RWCTL, RTSX_PHY_BUSY|RTSX_PHY_WRITE);

	while (timeout--) {
		RTSX_READ(sc, RTSX_PHY_RWCTL, &rwctl);
		if (!(rwctl & RTSX_PHY_BUSY))
			break;
	}

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// while (n--) leaves n at -1 when it runs out, and at 0 when the
	// last try succeeds.
	if (timeout < 0)
		return ETIMEDOUT;
	// SPDX-SnippetEnd

	return 0;
}

int
rtsx_read_cfg(struct rtsx_softc *sc, uint8 func, uint16 addr,
    uint32 *val)
{
	int tries = 1024;
	uint8 data0, data1, data2, data3, rwctl;

	RTSX_WRITE(sc, RTSX_CFGADDR0, addr);
	RTSX_WRITE(sc, RTSX_CFGADDR1, addr >> 8);
	RTSX_WRITE(sc, RTSX_CFGRWCTL, RTSX_CFG_BUSY | (func & 0x03 << 4));

	while (tries--) {
		RTSX_READ(sc, RTSX_CFGRWCTL, &rwctl);
		if (!(rwctl & RTSX_CFG_BUSY))
			break;
	}

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	// while (n--) leaves n at -1 when it runs out, and at 0 when the
	// last try succeeds.
	if (tries < 0)
		return EIO;
	// SPDX-SnippetEnd

	RTSX_READ(sc, RTSX_CFGDATA0, &data0);
	RTSX_READ(sc, RTSX_CFGDATA1, &data1);
	RTSX_READ(sc, RTSX_CFGDATA2, &data2);
	RTSX_READ(sc, RTSX_CFGDATA3, &data3);

	*val = ((uint32)data3 << 24) | (data2 << 16) | (data1 << 8) | data0;

	return 0;
}

/* Append a properly encoded host command to the host command buffer. */
static void
rtsx_hostcmd(uint32 *cmdbuf, int *n, uint8 cmd, uint16 reg,
    uint8 mask, uint8 data)
{
	if (*n >= RTSX_HOSTCMD_MAX)
		panic("rtsx: host command buffer overflow");

	cmdbuf[(*n)++] = B_HOST_TO_LENDIAN_INT32(((uint32)(cmd & 0x3) << 30) |
	    ((uint32)(reg & 0x3fff) << 16) |
	    ((uint32)(mask) << 8) |
	    ((uint32)data));
}

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
/*
 * Forget a transfer end that arrived after its waiter gave up. Otherwise the
 * next command saw it at once and returned before the chip had run it, with
 * the answer of no command. Called right before the chip is started.
 */
static void
rtsx_clear_trans(struct rtsx_softc *sc)
{
	cpu_status state = disable_interrupts();
	acquire_spinlock(&sc->intr_lock);
	sc->intr_status &= ~(RTSX_TRANS_OK_INT | RTSX_TRANS_FAIL_INT);
	release_spinlock(&sc->intr_lock);
	restore_interrupts(state);
}
// SPDX-SnippetEnd

static int
rtsx_hostcmd_send(struct rtsx_softc *sc, int ncmd)
{
	rtsx_clear_trans(sc);

	/* Tell the chip where the command buffer is and run the commands. */
	WRITE4(sc, RTSX_HCBAR, (uint32)sc->dma_phys);
	WRITE4(sc, RTSX_HCBCTLR,
	    ((ncmd * 4) & 0x00ffffff) | RTSX_START_CMD | RTSX_HW_AUTO_RSP);

	return 0;
}

static int
rtsx_xfer_exec(struct rtsx_softc *sc, uint32 dmaaddr, uint32 dmaflags)
{
	rtsx_clear_trans(sc);

	/* Tell the chip where the data buffer is and run the transfer. */
	WRITE4(sc, RTSX_HDBAR, dmaaddr);
	WRITE4(sc, RTSX_HDBCTLR, dmaflags);

	/* Wait for completion. */
	return rtsx_wait_intr(sc, RTSX_TRANS_OK_INT, 10);
}

static int
rtsx_xfer(struct rtsx_softc *sc, struct rtsx_command *cmd, uint32 *cmdbuf)
{
	int ncmd, dma_dir, error, tmode;
	int read = cmd->read;
	uint8 cfg2;

	DPRINTF(3,("%s: %s xfer: %u bytes with block size %u\n", DEVNAME(sc),
	    read ? "read" : "write",
	    cmd->datalen, cmd->blklen));

	/* Configure DMA transfer mode parameters. */
	cfg2 = RTSX_SD_NO_CHECK_WAIT_CRC_TO | RTSX_SD_CHECK_CRC16 |
	    RTSX_SD_NO_WAIT_BUSY_END | RTSX_SD_RSP_LEN_0;
	if (read) {
		dma_dir = RTSX_DMA_DIR_FROM_CARD;
		/* Use transfer mode AUTO_READ3, which assumes we've already
		 * sent the read command and gotten the response, and will
		 * send CMD 12 manually after reading multiple blocks. */
		tmode = RTSX_TM_AUTO_READ3;
		cfg2 |= RTSX_SD_CALCULATE_CRC7 | RTSX_SD_CHECK_CRC7;
	} else {
		dma_dir = RTSX_DMA_DIR_TO_CARD;
		/* Use transfer mode AUTO_WRITE3, which assumes we've already
		 * sent the write command and gotten the response, and will
		 * send CMD 12 manually after writing multiple blocks. */
		tmode = RTSX_TM_AUTO_WRITE3;
		cfg2 |= RTSX_SD_NO_CALCULATE_CRC7 | RTSX_SD_NO_CHECK_CRC7;
	}

	ncmd = 0;

	rtsx_hostcmd(cmdbuf, &ncmd, RTSX_WRITE_REG_CMD, RTSX_SD_CFG2,
	    0xff, cfg2);

	/* Queue commands to configure data transfer size. */
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_SD_BYTE_CNT_L, 0xff,
	    (cmd->blklen & 0xff));
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_SD_BYTE_CNT_H, 0xff,
	    (cmd->blklen >> 8));
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_SD_BLOCK_CNT_L, 0xff,
	    ((cmd->datalen / cmd->blklen) & 0xff));
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_SD_BLOCK_CNT_H, 0xff,
	    ((cmd->datalen / cmd->blklen) >> 8));

	/* Use the DMA ring buffer for commands which transfer data. */
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_CARD_DATA_SOURCE, 0x01, RTSX_RING_BUFFER);

	/* Configure DMA controller. */
	rtsx_hostcmd(cmdbuf, &ncmd, RTSX_WRITE_REG_CMD, RTSX_IRQSTAT0,
	    RTSX_DMA_DONE_INT, RTSX_DMA_DONE_INT);
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_DMATC3, 0xff, cmd->datalen >> 24);
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_DMATC2, 0xff, cmd->datalen >> 16);
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_DMATC1, 0xff, cmd->datalen >> 8);
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_DMATC0, 0xff, cmd->datalen);
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_DMACTL,
	    0x03 | RTSX_DMA_PACK_SIZE_MASK,
	    dma_dir | RTSX_DMA_EN | RTSX_DMA_512);

	/* Queue commands to perform SD transfer. */
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_SD_TRANSFER,
	    0xff, tmode | RTSX_SD_TRANSFER_START);
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_CHECK_REG_CMD, RTSX_SD_TRANSFER,
	    RTSX_SD_TRANSFER_END, RTSX_SD_TRANSFER_END);

	error = rtsx_hostcmd_send(sc, ncmd);
	if (error)
		goto ret;

	error = rtsx_xfer_adma(sc, cmd);
ret:
	DPRINTF(3,("%s: xfer done, error=%d\n", DEVNAME(sc), error));
	return error;
}

static int
rtsx_xfer_adma(struct rtsx_softc *sc, struct rtsx_command *cmd)
{
	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	/*
	 * One descriptor per piece of a physical vector, at most
	 * RTSX_DMA_MAX_SEGSIZE each. The RTS5228 has its own length layout.
	 */
	uint64 *descp = (uint64 *)(sc->dma_virt + RTSX_ADMA_OFFSET);
	uint32 left = cmd->datalen;
	uint32 ndesc = 0;
	int read = cmd->read;

	for (uint32 i = 0; i < cmd->vec_count && left > 0; i++) {
		generic_addr_t paddr = cmd->vecs[i].base;
		generic_size_t vlen = cmd->vecs[i].length;
		if (vlen > left)
			vlen = left;
		while (vlen > 0) {
			uint32 len = vlen > RTSX_DMA_MAX_SEGSIZE
			    ? RTSX_DMA_MAX_SEGSIZE : (uint32)vlen;
			if (ndesc >= RTSX_ADMA_MAX_DESC || paddr + len > 0x100000000ULL)
				return EINVAL;
			uint8 sgflags = RTSX_SG_VALID | RTSX_SG_TRANS_DATA;
			if (left == len)
				sgflags |= RTSX_SG_END;
			uint64 desc;
			if (sc->flags & RTSX_F_5228)
				desc = rtsx_5228_sg_desc((uint32)paddr, len, sgflags);
			else {
				desc = ((uint64)paddr << 32) |
				    ((uint64)(len & 0x00ffffff) << 12) | sgflags;
			}
			descp[ndesc++] = B_HOST_TO_LENDIAN_INT64(desc);
			paddr += len;
			vlen -= len;
			left -= len;
		}
	}
	if (left != 0 || ndesc == 0)
		return EINVAL;
	// SPDX-SnippetEnd

	return rtsx_xfer_exec(sc, (uint32)(sc->dma_phys + RTSX_ADMA_OFFSET),
	    RTSX_ADMA_MODE | RTSX_TRIG_DMA | (read ? RTSX_DMA_READ : 0));
}

int
rtsx_exec_command(struct rtsx_softc *sc, struct rtsx_command *cmd)
{
	uint32 *cmdbuf;
	uint8 rsp_type;
	uint16 r;
	int ncmd;
	int error = 0;
	uint8 *cmdkvap = sc->dma_virt;

	DPRINTF(3,("%s: executing cmd %u\n", DEVNAME(sc), cmd->opcode));

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	/* No card: fail at once instead of waiting for a timeout. */
	if (!ISSET(sc->flags, RTSX_F_CARD_PRESENT))
		return B_DEV_NO_MEDIA;
	// SPDX-SnippetEnd

	rsp_type = cmd->rsp_type;
	if (rsp_type == 0) {
		dprintf("%s: unknown response type for cmd %u\n", DEVNAME(sc),
			cmd->opcode);
		return EINVAL;
	}

	/* The command buffer queues commands the host controller will
	 * run asynchronously. */
	cmdbuf = (uint32 *)cmdkvap;
	memset(cmdbuf, 0, RTSX_HOSTCMD_BUFSIZE);
	ncmd = 0;

	/* Queue commands to set SD command index and argument. */
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_SD_CMD0, 0xff, 0x40 | cmd->opcode);
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_SD_CMD1, 0xff, cmd->arg >> 24);
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_SD_CMD2, 0xff, cmd->arg >> 16);
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_SD_CMD3, 0xff, cmd->arg >> 8);
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_SD_CMD4, 0xff, cmd->arg);

	/* Queue command to set response type. */
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_SD_CFG2, 0xff, rsp_type);

	/* Use the ping-pong buffer for commands which do not transfer data. */
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_CARD_DATA_SOURCE,
	    0x01, RTSX_PINGPONG_BUFFER);

	/* Queue commands to perform SD transfer. */
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_WRITE_REG_CMD, RTSX_SD_TRANSFER,
	    0xff, RTSX_TM_CMD_RSP | RTSX_SD_TRANSFER_START);
	rtsx_hostcmd(cmdbuf, &ncmd,
	    RTSX_CHECK_REG_CMD, RTSX_SD_TRANSFER,
	    RTSX_SD_TRANSFER_END|RTSX_SD_STAT_IDLE,
	    RTSX_SD_TRANSFER_END|RTSX_SD_STAT_IDLE);

	/* Queue commands to read back card status response.*/
	if (rsp_type == RTSX_SD_RSP_TYPE_R2) {
		for (r = RTSX_PPBUF_BASE2 + 15; r > RTSX_PPBUF_BASE2; r--)
			rtsx_hostcmd(cmdbuf, &ncmd, RTSX_READ_REG_CMD, r, 0, 0);
		rtsx_hostcmd(cmdbuf, &ncmd, RTSX_READ_REG_CMD, RTSX_SD_CMD5,
		    0, 0);
	} else if (rsp_type != RTSX_SD_RSP_TYPE_R0) {
		for (r = RTSX_SD_CMD0; r <= RTSX_SD_CMD4; r++)
			rtsx_hostcmd(cmdbuf, &ncmd, RTSX_READ_REG_CMD, r, 0, 0);
	}

	/* Run the command queue and wait for completion. */
	error = rtsx_hostcmd_send(sc, ncmd);
	if (error == 0) {
		// SPDX-SnippetBegin
		// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
		// SPDX-License-Identifier: MIT
		/* An erase stays busy longer than the one second of OpenBSD. */
		error = rtsx_wait_intr(sc, RTSX_TRANS_OK_INT,
		    cmd->busy_secs != 0 ? cmd->busy_secs : 1);
		// SPDX-SnippetEnd
	}
	if (error)
		return error;

	/* Copy card response into the response buffer. */
	if (rsp_type != RTSX_SD_RSP_TYPE_R0) {
		/* Copy bytes like sdhc(4), which on little-endian uses
		 * different byte order for short and long responses... */
		if (rsp_type == RTSX_SD_RSP_TYPE_R2) {
			memcpy(cmd->resp, cmdkvap + 1, sizeof(cmd->resp));
		} else {
			/* First byte is CHECK_REG_CMD return value, second
			 * one is the command op code -- we skip those. */
			cmd->resp[0] =
			    ((B_BENDIAN_TO_HOST_INT32(cmdbuf[0]) & 0x0000ffff) << 16) |
			    ((B_BENDIAN_TO_HOST_INT32(cmdbuf[1]) & 0xffff0000) >> 16);
		}
	}

	if (cmd->datalen) {
		error = rtsx_xfer(sc, cmd, cmdbuf);
		if (error) {
			uint8 stat1;

			if (rtsx_read(sc, RTSX_SD_STAT1, &stat1) == 0 &&
			    (stat1 & RTSX_SD_CRC_ERR))
				dprintf("%s: CRC error\n", DEVNAME(sc));
		}
	}

	return error;
}

/* Prepare for another command. */
void
rtsx_soft_reset(struct rtsx_softc *sc)
{
	DPRINTF(1,("%s: soft reset\n", DEVNAME(sc)));

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	if (sc->flags & RTSX_F_5228) {
		rtsx_5228_stop_dma(sc);
		return;
	}
	// SPDX-SnippetEnd

	/* Stop command transfer. */
	WRITE4(sc, RTSX_HCBCTLR, RTSX_STOP_CMD);

	(void)rtsx_write(sc, RTSX_CARD_STOP, RTSX_SD_STOP|RTSX_SD_CLR_ERR,
		    RTSX_SD_STOP|RTSX_SD_CLR_ERR);

	/* Stop DMA transfer. */
	WRITE4(sc, RTSX_HDBCTLR, RTSX_STOP_DMA);
	(void)rtsx_write(sc, RTSX_DMACTL, RTSX_DMA_RST, RTSX_DMA_RST);

	(void)rtsx_write(sc, RTSX_RBCTL, RTSX_RB_FLUSH, RTSX_RB_FLUSH);
}

static int
rtsx_wait_intr(struct rtsx_softc *sc, uint32 mask, int secs)
{
	uint32 status;
	int error = 0;

	mask |= RTSX_TRANS_FAIL_INT;

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	/* OpenBSD sleeps with tsleep; here the interrupt notifies intr_cond. */
	bigtime_t deadline = system_time() + secs * 1000000LL;
	for (;;) {
		ConditionVariableEntry entry;
		cpu_status state = disable_interrupts();
		acquire_spinlock(&sc->intr_lock);
		status = sc->intr_status & mask;
		if (status != 0 || !ISSET(sc->flags, RTSX_F_CARD_PRESENT)) {
			sc->intr_status &= ~status;
			release_spinlock(&sc->intr_lock);
			restore_interrupts(state);
			break;
		}
		sc->intr_cond.Add(&entry);
		release_spinlock(&sc->intr_lock);
		restore_interrupts(state);

		if (entry.Wait(B_ABSOLUTE_TIMEOUT, deadline) == B_TIMED_OUT) {
			state = disable_interrupts();
			acquire_spinlock(&sc->intr_lock);
			status = sc->intr_status & mask;
			sc->intr_status &= ~status;
			release_spinlock(&sc->intr_lock);
			restore_interrupts(state);
			if (status != 0)
				break;
			rtsx_soft_reset(sc);
			error = ETIMEDOUT;
			break;
		}
	}
	// SPDX-SnippetEnd

	/* Has the card disappeared? */
	if (!ISSET(sc->flags, RTSX_F_CARD_PRESENT))
		error = ENODEV;

	if (error == 0 && (status & RTSX_TRANS_FAIL_INT))
		error = EIO;

	return error;
}

/*
 * The interrupt handler. Card changes go to the card thread in
 * rtsx_haiku.cpp, which debounces them and powers the slot.
 */
int32
rtsx_intr(void *arg)
{
	struct rtsx_softc *sc = (struct rtsx_softc *)arg;
	uint32 enabled, status;

	enabled = READ4(sc, RTSX_BIER);
	status = READ4(sc, RTSX_BIPR);

	/* Ack interrupts. */
	WRITE4(sc, RTSX_BIPR, status);

	if (((enabled & status) == 0) || status == 0xffffffff)
		return B_UNHANDLED_INTERRUPT;

	// SPDX-SnippetBegin
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// SPDX-License-Identifier: MIT
	int32 result = B_HANDLED_INTERRUPT;
	int32 events = 0;

	if (sc->flags & RTSX_F_5228) {
		status &= enabled | 0x007fffff;
		if (status & RTS5228_SD_OC_INT)
			events |= RTSX_EVENT_OVERCURRENT;
		/* A delink ends the transfer as a failure. */
		if (status & RTS5228_DELINK_INT)
			status |= RTSX_TRANS_FAIL_INT;
	}

	if (status & RTSX_SD_INT) {
		/* A removal is seen at once, so waiting commands stop. */
		if (!(status & RTSX_SD_EXIST))
			atomic_and(&sc->flags, ~RTSX_F_CARD_PRESENT);
		events |= RTSX_EVENT_CARD;
	}

	if (events != 0) {
		atomic_or(&sc->card_events, events);
		release_sem_etc(sc->card_sem, 1, B_DO_NOT_RESCHEDULE);
		result = B_INVOKE_SCHEDULER;
	}

	if (status & (RTSX_TRANS_OK_INT | RTSX_TRANS_FAIL_INT)
	    || (events & RTSX_EVENT_CARD)) {
		acquire_spinlock(&sc->intr_lock);
		sc->intr_status |= status;
		release_spinlock(&sc->intr_lock);
		sc->intr_cond.NotifyAll();
	}

	return result;
	// SPDX-SnippetEnd
}
