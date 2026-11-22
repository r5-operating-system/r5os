/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Realtek RTS5228 support for the rtsx driver.
 *
 * Clean room: written only from RTS5228-SPEC.md, a facts-only description
 * of the hardware. No Linux source was read for this file. Section numbers
 * of that specification are given in brackets.
 *
 * Not done: UHS-I (1.8 V signalling, tuning), runtime power management
 * (the reader stays in the active state, ASPM forced off) and suspend.
 */


#include <KernelExport.h>
#include <errno.h>
#include <string.h>

#include "rtsxreg.h"
#include "rtsx.h"
#include "rtsx_5228.h"


// A failed step names its source line, so the stick log shows which
// register access of a sequence failed (stick 75: "cannot power the card
// slot" with nothing else).
#define CHECK(x) \
	do { \
		int _error = (x); \
		if (_error != 0) { \
			dprintf("rtsx: RTS5228 step at line %d failed: %s\n", \
				__LINE__, strerror(_error)); \
			return _error; \
		} \
	} while (0)


static inline int
reg_write(struct rtsx_softc* sc, uint16 reg, uint8 mask, uint8 value)
{
	return rtsx_write(sc, reg, mask, value);
}


/* PCIe Link Control, at the PCIe capability + 0x10 [2.3 step 6, 6.1] */
static uint16
link_control(struct rtsx_softc* sc)
{
	if (sc->r5228.pcie_cap == 0)
		return 0;
	return sc->pci->read_pci_config(sc->device, sc->r5228.pcie_cap + 0x10, 2);
}


static void
set_link_control(struct rtsx_softc* sc, uint16 value)
{
	if (sc->r5228.pcie_cap == 0)
		return;
	sc->pci->write_pci_config(sc->device, sc->r5228.pcie_cap + 0x10, 2,
		value);
}


static int
phy_update(struct rtsx_softc* sc, uint8 reg, uint16 clear, uint16 set)
{
	uint16 value;
	CHECK(rtsx_read_phy(sc, reg, &value));
	value = (value & ~clear) | set;
	return rtsx_write_phy(sc, reg, value);
}


/* [2.2] board settings and PCIe state, read once per init */
static void
read_settings(struct rtsx_softc* sc)
{
	rtsx_5228_state& s = sc->r5228;
	uint8 value;

	if (rtsx_read(sc, RTS5228_IC_VERSION, &value) == 0)
		s.ic_version = value & 0x0f;

	s.aspm_was_enabled = true;
	if (rtsx_read(sc, RTS5228_ASPM_FORCE, &value) == 0)
		s.aspm_was_enabled = (value & 0x30) != 0x30;

	uint8 pcieCap;
	if (sc->pci->find_pci_capability(sc->device, PCI_cap_id_pcie, &pcieCap)
			== B_OK) {
		s.pcie_cap = pcieCap;
	} else
		s.pcie_cap = 0;

	uint16 l1ss;
	s.l1ss_present = sc->pci->find_pci_extended_capability(sc->device,
		RTS5228_EXT_CAP_L1SS, &l1ss) == B_OK;
	s.l1ss_bits = 0;
	s.ltr_enabled = false;
	if (s.l1ss_present) {
		s.l1ss_bits = sc->pci->read_pci_config(sc->device, l1ss + 0x08, 4)
			& 0x0f;
		if (s.pcie_cap != 0) {
			uint16 devctl2 = sc->pci->read_pci_config(sc->device,
				s.pcie_cap + 0x28, 2);
			s.ltr_enabled = (devctl2 & 0x0400) != 0;
		}
	}

	/* Defaults before the board settings */
	s.drive_1v8 = 3;
	s.drive_3v3 = 3;
	s.aspm_bits = 0x02;
	s.rtd3 = false;
	s.mmc_disabled = false;
	s.reverse_socket = false;
	s.cd_invert = false;
	s.wp_invert = false;

	uint32 settings1 = sc->pci->read_pci_config(sc->device,
		RTS5228_CFG_SETTINGS1, 4);
	if ((settings1 & 0x01000000) != 0)
		return;

	s.aspm_bits = (settings1 >> 28) & 0x03;
	s.drive_1v8 = (settings1 >> 26) & 0x03;

	uint32 settings2 = sc->pci->read_pci_config(sc->device,
		RTS5228_CFG_SETTINGS2, 4);
	s.rtd3 = (settings2 & 0x00000002) != 0;
	s.mmc_disabled = (settings2 & 0x00000010) != 0;
	s.drive_3v3 = (settings2 >> 5) & 0x03;
	s.reverse_socket = (settings2 & 0x00004000) != 0;
	s.wp_invert = (settings2 & 0x00400000) != 0;
	s.cd_invert = (settings2 & 0x00800000) != 0;
}


/* [3.3] pad drive strength for CLK, CMD and DAT */
static int
set_drive(struct rtsx_softc* sc, bool lowVoltage)
{
	static const uint8 k3v3[4][3] = {
		{ 0x13, 0x13, 0x13 },
		{ 0x96, 0x96, 0x96 },
		{ 0x7f, 0x7f, 0x7f },
		{ 0x96, 0x96, 0x96 },
	};
	static const uint8 k1v8[4][3] = {
		{ 0x99, 0x99, 0x99 },
		{ 0xb5, 0xb5, 0xb5 },
		{ 0xe6, 0x7e, 0xfe },
		{ 0x6b, 0x6b, 0x6b },
	};

	const uint8* drive = lowVoltage
		? k1v8[sc->r5228.drive_1v8 & 3] : k3v3[sc->r5228.drive_3v3 & 3];
	CHECK(reg_write(sc, RTS5228_DRIVE_CLK, 0xff, drive[0]));
	CHECK(reg_write(sc, RTS5228_DRIVE_CMD, 0xff, drive[1]));
	return reg_write(sc, RTS5228_DRIVE_DAT, 0xff, drive[2]);
}


/* [3.3] signal voltage; only 3.3 V is used until UHS-I is added */
static int
switch_voltage_3v3(struct rtsx_softc* sc)
{
	CHECK(reg_write(sc, RTS5228_DISCHARGE, 0x20, 0x20));
	CHECK(phy_update(sc, RTS5228_PHY_VOLTAGE, 0, 0x0200));
	CHECK(reg_write(sc, RTS5228_DV3318_CFG, 0x70, 0x70));
	CHECK(reg_write(sc, RTS5228_SD_PAD_CTL, 0x80, 0x00));
	return set_drive(sc, false);
}


/* [6.1] ASPM off while the reader is active */
static int
disable_aspm(struct rtsx_softc* sc)
{
	set_link_control(sc, link_control(sc) & ~0x0003);
	CHECK(reg_write(sc, RTS5228_ASPM_FORCE, 0x33, 0x30));
	CHECK(reg_write(sc, RTS5228_SD_CFG1, 0x10, 0x00));
	snooze(10000);
	return 0;
}


/* [2.3 step 13] */
static int
chip_setup(struct rtsx_softc* sc)
{
	rtsx_5228_state& s = sc->r5228;

	CHECK(reg_write(sc, RTS5228_CD_RESUME, 0xf0, 0xf0));

	if (s.l1ss_bits != 0) {
		CHECK(phy_update(sc, RTS5228_PHY_CFG1, 0x0200, 0));
		CHECK(reg_write(sc, RTS5228_OOBS_CFG2, 0xff, 0x03));
		CHECK(reg_write(sc, RTS5228_OOBS_CFG3, 0xff, 0x00));
	} else {
		CHECK(phy_update(sc, RTS5228_PHY_CFG1, 0, 0x0200));
		CHECK(reg_write(sc, RTS5228_OOBS_CFG0, 0xff, 0x32));
		CHECK(reg_write(sc, RTS5228_OOBS_CFG1, 0xff, 0x05));
		CHECK(reg_write(sc, RTS5228_OOBS_CFG2, 0xff, 0x83));
		CHECK(reg_write(sc, RTS5228_OOBS_CFG3, 0xff, 0xde));
	}

	CHECK(reg_write(sc, RTS5228_ASPM_FORCE, 0xff, 0x00));

	if (s.ltr_enabled) {
		/* Active latency 60 us */
		CHECK(reg_write(sc, RTS5228_LTR_LAT0, 0xff, 0x3c));
		CHECK(reg_write(sc, RTS5228_LTR_LAT0 + 1, 0xff, 0x88));
		CHECK(reg_write(sc, RTS5228_LTR_LAT0 + 2, 0xff, 0x00));
		CHECK(reg_write(sc, RTS5228_LTR_LAT0 + 3, 0xff, 0x00));
		CHECK(reg_write(sc, RTS5228_LTR_CTL, 0xc0, 0xc0));
	}

	CHECK(reg_write(sc, RTS5228_L1SS_AUX, 0x01, 0x00));
	CHECK(reg_write(sc, RTS5228_L1SS_PG3, 0xff, 0x00));
	CHECK(reg_write(sc, RTS5228_UPME_DEBUG, 0x02, 0x02));
	CHECK(reg_write(sc, RTS5228_PCLK_CTL, 0x20, 0x20));
	CHECK(reg_write(sc, RTS5228_PM_DEBUG, 0x08, 0x08));
	CHECK(reg_write(sc, RTS5228_CLK_PM, 0x01, 0x01));
	CHECK(reg_write(sc, RTS5228_LED_BLINK, 0x0f, 0x02));
	CHECK(set_drive(sc, false));

	if (s.reverse_socket)
		CHECK(reg_write(sc, RTS5228_AUTOLOAD_CFG, 0x30, 0x30));
	else {
		CHECK(reg_write(sc, RTS5228_AUTOLOAD_CFG, 0x20,
			s.cd_invert ? 0x20 : 0x00));
		CHECK(reg_write(sc, RTS5228_AUTOLOAD_CFG, 0x10,
			s.wp_invert ? 0x10 : 0x00));
	}

	/* CLKREQ# is forced low when no L1 substate is enabled. */
	CHECK(reg_write(sc, RTS5228_AUTOLOAD_CFG, 0x80,
		s.l1ss_bits == 0 ? 0x80 : 0x00));

	CHECK(reg_write(sc, RTS5228_PWD_SUSPEND, 0xff, 0xfb));

	if (s.rtd3) {
		CHECK(reg_write(sc, RTS5228_PM_CTL, 0x01, 0x01));
		CHECK(reg_write(sc, RTS5228_PME_FORCE, 0x30, 0x30));
	} else {
		CHECK(reg_write(sc, RTS5228_PM_CTL, 0x01, 0x00));
		CHECK(reg_write(sc, RTS5228_PME_FORCE, 0x30, 0x20));
	}
	return reg_write(sc, RTS5228_PM_CTL, 0x10, 0x00);
}


/* [2.3] hardware initialisation, steps 1 to 14; step 15 is the caller's */
int
rtsx_5228_init(struct rtsx_softc* sc)
{
	read_settings(sc);
	sc->r5228.cur_clock_mhz = 0;

	CHECK(reg_write(sc, RTS5228_SLEEP_STATE, 0x03, 0x00));

	/* 1, 2, 3 */
	CHECK(reg_write(sc, RTS5228_LDO1_CFG, 0xc0, 0x80));
	rtsx_write4(sc, RTSX_HCBAR, (uint32)sc->dma_phys);
	rtsx_write4(sc, RTSX_BIER, RTS5228_BIER_MASK);

	/* 4, 5: SSC PLL power on */
	CHECK(reg_write(sc, RTS5228_SSC_POWER, 0x01, 0x00));
	spin(200);

	/* 6 */
	if (sc->r5228.aspm_was_enabled)
		CHECK(disable_aspm(sc));

	/* 7 */
	CHECK(rtsx_write_phy(sc, RTS5228_PHY_OPT, 0x8f40));

	/* 8: core clock, link and interrupt configuration */
	CHECK(reg_write(sc, RTS5228_CLK_DIV, 0x07, 0x07));
	CHECK(reg_write(sc, RTS5228_SLEEP_STATE, 0x03, 0x00));
	CHECK(reg_write(sc, RTS5228_CARD_CLK_EN, 0x1e, 0x00));
	CHECK(reg_write(sc, RTS5228_LINK_STATE, 0x0a, 0x00));
	CHECK(reg_write(sc, RTS5228_DRIVE_SEL, 0xff, 0x41));
	CHECK(reg_write(sc, RTS5228_SSC_CTL1, 0xff, 0x50));
	CHECK(reg_write(sc, RTS5228_SSC_CTL2, 0xff, 0x03));
	CHECK(reg_write(sc, RTS5228_LINK_STATE, 0x16, 0x10));
	CHECK(reg_write(sc, RTS5228_IRQSTAT0, 0x20, 0x20));
	CHECK(reg_write(sc, RTS5228_PERST_GLITCH, 0xff, 0x80));
	CHECK(reg_write(sc, RTS5228_RCCTL, 0x01, 0x00));
	CHECK(reg_write(sc, RTS5228_NFTS_TX_CTRL, 0x02, 0x00));

	/* 9 */
	CHECK(reg_write(sc, RTS5228_CLK_PM, 0x01, 0x01));

	/* 10: overcurrent protection [3.5] */
	CHECK(reg_write(sc, RTS5228_LDO_OCP, 0x12, 0x12));
	CHECK(reg_write(sc, RTS5228_LDO_OCP, 0xe0, 0x60));
	CHECK(reg_write(sc, RTS5228_LDO_OCP, 0x0c, 0x08));
	uint8 ocp;
	CHECK(rtsx_read(sc, RTS5228_LDO_OCP, &ocp));
	CHECK(reg_write(sc, RTS5228_OCP_GLITCH, 0x0f, 0x05));
	CHECK(reg_write(sc, RTS5228_OCP_CTL, 0xff, 0x0c));
	CHECK(reg_write(sc, RTS5228_LDO_OCP, 0x12, 0x12));

	/* 11: CLKREQ# enable */
	set_link_control(sc, link_control(sc) | 0x0100);

	/* 12: host TX idle L1 entry */
	sc->pci->write_pci_config(sc->device, RTS5228_CFG_TX_IDLE, 1, 0x5b);

	/* 13 */
	CHECK(chip_setup(sc));

	/* 14: register-based ASPM control, forced off */
	return reg_write(sc, RTS5228_ASPM_FORCE, 0x30, 0x30);
}


/* [3.2] card power on at 3.3 V */
int
rtsx_5228_power_on(struct rtsx_softc* sc)
{
	snooze(100000);
	CHECK(reg_write(sc, RTS5228_CARD_SELECT, 0x07, 0x02));
	CHECK(reg_write(sc, RTS5228_SHARE_MODE, 0x0f, 0x04));
	CHECK(reg_write(sc, RTS5228_CARD_CLK_EN, 0x04, 0x04));
	CHECK(reg_write(sc, RTS5228_PULL_CTL2, 0xff, 0xaa));
	CHECK(reg_write(sc, RTS5228_PULL_CTL3, 0xff, 0xe9));

	/* 1 to 8: regulators */
	CHECK(reg_write(sc, RTS5228_OCP_CTL, 0xff, 0x0c));
	CHECK(reg_write(sc, RTS5228_LDO_OCP, 0x12, 0x12));
	CHECK(reg_write(sc, RTS5228_AUTO_PWR_OFF, 0x01, 0x01));
	CHECK(reg_write(sc, RTS5228_LDO1_CFG, 0x0e, 0x0e));
	CHECK(reg_write(sc, RTS5228_LDO_CTL, 0x03, 0x01));
	snooze(2000);
	CHECK(reg_write(sc, RTS5228_LDO_CTL, 0x03, 0x03));
	CHECK(reg_write(sc, RTS5228_LDO_CTL, 0x08, 0x08));
	snooze(20000);

	/* 9 to 11: SD pads and controller */
	CHECK(reg_write(sc, RTS5228_CARD_OE, 0x04, 0x04));
	CHECK(reg_write(sc, RTS5228_SD_CFG1, 0xff, 0x80));
	CHECK(reg_write(sc, RTS5228_SD_SAMPLE, 0xff, 0x00));
	CHECK(reg_write(sc, RTS5228_SD_PUSH, 0xff, 0x00));
	CHECK(reg_write(sc, RTS5228_CARD_STOP, 0x44, 0x44));
	CHECK(reg_write(sc, RTS5228_SD_CFG3, 0x10, 0x00));
	CHECK(reg_write(sc, RTS5228_SD30_CLK_STOP, 0x07, 0x00));
	CHECK(reg_write(sc, RTS5228_SD_CFG1, 0x1c, 0x18));
	CHECK(reg_write(sc, RTS5228_CLK_CTL, 0x01, 0x01));
	CHECK(reg_write(sc, RTS5228_CLK_SOURCE, 0xff, 0x21));
	CHECK(reg_write(sc, RTS5228_CLK_CTL, 0x01, 0x00));

	/* 12: 74 clocks for the card; then the clock waits for commands */
	snooze(1000);
	CHECK(reg_write(sc, RTS5228_CARD_OE, 0x04, 0x04));
	CHECK(reg_write(sc, RTS5228_SD_BUS_STAT, 0x80, 0x80));
	snooze(1000);
	CHECK(reg_write(sc, RTS5228_SD_BUS_STAT, 0xc0, 0x00));

	sc->r5228.cur_clock_mhz = 0;
	return 0;
}


/* [3.4] command and DMA stop, also used as the soft reset */
void
rtsx_5228_stop_dma(struct rtsx_softc* sc)
{
	rtsx_write4(sc, RTSX_HCBCTLR, RTSX_STOP_CMD);
	(void)reg_write(sc, RTS5228_CARD_STOP, 0x44, 0x44);
	rtsx_write4(sc, RTSX_HDBCTLR, RTSX_STOP_DMA);
	(void)reg_write(sc, RTS5228_DMA_RESET, 0xc0, 0xc0);
	(void)reg_write(sc, RTS5228_RBCTL, 0x80, 0x80);
}


/* [3.4] card power off */
int
rtsx_5228_power_off(struct rtsx_softc* sc)
{
	CHECK(reg_write(sc, RTS5228_CARD_CLK_EN, 0x04, 0x00));
	CHECK(reg_write(sc, RTS5228_CARD_OE, 0x04, 0x00));

	rtsx_write4(sc, RTSX_HCBCTLR, RTSX_STOP_CMD);
	rtsx_write4(sc, RTSX_HDBCTLR, RTSX_STOP_DMA);
	CHECK(reg_write(sc, RTS5228_DMA_RESET, 0xc0, 0xc0));
	CHECK(reg_write(sc, RTS5228_RBCTL, 0x80, 0x80));

	CHECK(switch_voltage_3v3(sc));

	CHECK(reg_write(sc, RTS5228_LDO_CTL, 0x0f, 0x00));
	CHECK(reg_write(sc, RTS5228_AUTO_PWR_OFF, 0x01, 0x00));
	CHECK(reg_write(sc, RTS5228_OCP_CTL, 0x0c, 0x00));
	CHECK(reg_write(sc, RTS5228_LDO_OCP, 0x12, 0x00));

	CHECK(reg_write(sc, RTS5228_PULL_CTL2, 0xff, 0x55));
	return reg_write(sc, RTS5228_PULL_CTL3, 0xff, 0xd5);
}


/*
 * [4] SD clock. The Haiku mmc_bus uses identification (400 kHz) and
 * default speed (25 MHz), so the timing is always a double-clock one with
 * nominal SSC depth 512K, and the variable-phase clocks are not used.
 */
int
rtsx_5228_set_clock(struct rtsx_softc* sc, int kilohertz)
{
	bool initial = kilohertz <= 1000;
	uint32 mhz;

	if (initial) {
		CHECK(reg_write(sc, RTS5228_SD_CFG1, 0xc0, 0x80));
		mhz = 30;
	} else {
		CHECK(reg_write(sc, RTS5228_SD_CFG1, 0xc0, 0x00));
		mhz = (uint32)kilohertz / 1000 * 2;
	}

	if (mhz == sc->r5228.cur_clock_mhz)
		return 0;

	if (mhz <= 4 || mhz - 4 > 396)
		return EINVAL;
	uint32 n = mhz - 4;

	uint32 mcu = 125 / mhz + 3;
	if (mcu > 15)
		mcu = 15;

	uint32 div = 1;
	while (n < 76 && div < 4) {
		n = (n + 4) * 2 - 4;
		div++;
	}
	n = n / 2 - 1;

	/* [4.2] depth: 512K, one less for double-clock, then per divider */
	int depth = 0x05;
	if (depth > 1)
		depth--;
	if (div > 1) {
		depth -= div - 1;
		if (depth < 0x01)
			depth = 0x01;
	}

	/* [4.3] */
	CHECK(reg_write(sc, RTS5228_CLK_CTL, 0x01, 0x01));
	CHECK(reg_write(sc, RTS5228_CLK_DIV, 0xff, (div << 4) | mcu));
	CHECK(reg_write(sc, RTS5228_SSC_CTL1, 0x80, 0x00));
	CHECK(reg_write(sc, RTS5228_SSC_CTL2, 0x07, depth));
	CHECK(reg_write(sc, RTS5228_SSC_DIV_N, 0xff, n));
	CHECK(reg_write(sc, RTS5228_SSC_CTL1, 0x80, 0x80));
	spin(130);
	CHECK(reg_write(sc, RTS5228_CLK_CTL, 0x01, 0x00));

	sc->r5228.cur_clock_mhz = mhz;
	return 0;
}


/* [3.5] overcurrent: clear the status first, then power off the slot */
void
rtsx_5228_overcurrent(struct rtsx_softc* sc)
{
	uint8 status;
	if (rtsx_read(sc, RTS5228_OCP_STAT, &status) != 0
		|| (status & 0x06) == 0)
		return;

	dprintf("rtsx: RTS5228 overcurrent (status %#x), slot powered off\n",
		status);
	(void)reg_write(sc, RTS5228_OCP_CTL, 0x03, 0x03);
	snooze(1000);
	(void)reg_write(sc, RTS5228_OCP_CTL, 0x03, 0x00);
	(void)rtsx_5228_power_off(sc);
	(void)reg_write(sc, RTS5228_CARD_OE, 0x04, 0x00);
}


/* [6.2] power off for a restart or power off (sleep state S1) */
void
rtsx_5228_shutdown(struct rtsx_softc* sc)
{
	(void)reg_write(sc, RTS5228_LED_CTL, 0x02, 0x00);
	rtsx_write4(sc, RTSX_BIER, 0);
	(void)reg_write(sc, RTS5228_AUTOLOAD_CFG, 0x08, 0x08);
	(void)reg_write(sc, RTS5228_SLEEP_STATE, 0x03, 0x01);
	(void)reg_write(sc, RTS5228_RELINK_TIME0, 0xff, 0x00);
	(void)reg_write(sc, RTS5228_RELINK_TIME1, 0xff, 0x00);
	(void)reg_write(sc, RTS5228_AUTOLOAD_CFG, 0x01, 0x00);
	(void)reg_write(sc, RTS5228_PM_CTL, 0x10, 0x10);
	(void)reg_write(sc, RTS5228_CD_RESUME, 0xf0, 0x00);
	(void)reg_write(sc, RTS5228_PM_CTL, 0x01, 0x00);
	(void)reg_write(sc, RTS5228_PME_FORCE, 0x30, 0x20);
	(void)reg_write(sc, RTS5228_SSC_POWER, 0x01, 0x01);
}


/* [7.1] scatter-gather descriptor with the RTS5228 length layout */
uint64
rtsx_5228_sg_desc(uint32 address, uint32 length, uint8 flags)
{
	return ((uint64)address << 32)
		| ((uint64)(length & 0xffff) << RTS5228_SG_LEN_LOW_SHIFT)
		| ((uint64)((length >> 16) & 0x3ff) << RTS5228_SG_LEN_HIGH_SHIFT)
		| (flags & 0x3f);
}
