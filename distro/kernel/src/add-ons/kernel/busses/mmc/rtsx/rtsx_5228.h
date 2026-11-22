/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Realtek RTS5228 registers, written from the clean-room specification
 * (RTS5228-SPEC.md, sections in brackets). The names are R5's own.
 * Registers the RTS5228 shares with the older chips use the OpenBSD names
 * in rtsxreg.h.
 */
#ifndef _RTSX_5228_H_
#define _RTSX_5228_H_


/* MMIO interrupt bits [5.1] */
#define RTS5228_BIER_MASK		0x32400000
#define RTS5228_SD_OC_INT		0x00400000
#define RTS5228_DELINK_INT		0x01000000

/* Internal registers */
#define RTS5228_SSC_POWER		0xfc00	/* bit 0: SSC PLL power down */
#define RTS5228_CLK_CTL			0xfc02	/* bit 0: low-frequency clock */
#define RTS5228_CLK_DIV			0xfc03	/* [7:4] divider, [3:0] MCU count */
#define RTS5228_SSC_DIV_N		0xfc0f
#define RTS5228_SSC_CTL1		0xfc11
#define RTS5228_SSC_CTL2		0xfc12	/* [2:0] SSC depth */
#define RTS5228_RCCTL			0xfc14
#define RTS5228_LED_BLINK		0xfc1e
#define RTS5228_LED_CTL			0xfc1f
#define RTS5228_VPTX_CLK		0xfc2a	/* bit 6: TX phase clock run */
#define RTS5228_VPRX_CLK		0xfc2b	/* bit 6: RX phase clock run */
#define RTS5228_CLK_SOURCE		0xfc2e

#define RTS5228_DISCHARGE		0xfd50	/* bit 5: pull discharge control */
#define RTS5228_SHARE_MODE		0xfd52
#define RTS5228_DRIVE_SEL		0xfd53
#define RTS5228_CARD_STOP		0xfd54
#define RTS5228_CARD_OE			0xfd55
#define RTS5228_DRIVE_CLK		0xfd5a
#define RTS5228_CARD_SELECT		0xfd5c
#define RTS5228_DRIVE_CMD		0xfd5e
#define RTS5228_DRIVE_DAT		0xfd5f
#define RTS5228_PULL_CTL2		0xfd61
#define RTS5228_PULL_CTL3		0xfd62
#define RTS5228_CARD_CLK_EN		0xfd69
#define RTS5228_OCP_CTL			0xfd6a
#define RTS5228_OCP_GLITCH		0xfd6c
#define RTS5228_OCP_STAT		0xfd6e
#define RTS5228_AUTO_PWR_OFF	0xfd71
#define RTS5228_SD_CFG1			0xfda0
#define RTS5228_SD_CFG3			0xfda2
#define RTS5228_SD_BUS_STAT		0xfda5
#define RTS5228_SD_PAD_CTL		0xfda6
#define RTS5228_SD_SAMPLE		0xfda7
#define RTS5228_SD_PUSH			0xfda8
#define RTS5228_SD30_CLK_STOP	0xfdb8

#define RTS5228_IRQSTAT0		0xfe21
#define RTS5228_RBCTL			0xfe34
#define RTS5228_LTR_LAT0		0xfe44	/* 0xfe44..0xfe47, little endian */
#define RTS5228_LTR_CTL			0xfe4a
#define RTS5228_PCLK_CTL		0xfe55
#define RTS5228_ASPM_FORCE		0xfe57
#define RTS5228_CLK_PM			0xfe58
#define RTS5228_UPME_DEBUG		0xfe59
#define RTS5228_LINK_STATE		0xfe5b
#define RTS5228_PERST_GLITCH	0xfe5c
#define RTS5228_SLEEP_STATE		0xfe60
#define RTS5228_PM_DEBUG		0xfe71
#define RTS5228_NFTS_TX_CTRL	0xfe72
#define RTS5228_PWD_SUSPEND		0xfe76
#define RTS5228_L1SS_AUX		0xfe8d
#define RTS5228_L1SS_PG3		0xfe8f
#define RTS5228_IC_VERSION		0xfe90
#define RTS5228_OOBS_CFG0		0xfea6
#define RTS5228_OOBS_CFG1		0xfea7
#define RTS5228_OOBS_CFG2		0xfea8
#define RTS5228_OOBS_CFG3		0xfea9
#define RTS5228_DMA_RESET		0xfebf

#define RTS5228_RELINK_TIME0	0xff01
#define RTS5228_RELINK_TIME1	0xff02
#define RTS5228_AUTOLOAD_CFG	0xff03	/* CD/WP polarity, CLKREQ, relink */
#define RTS5228_LDO_CTL			0xff70	/* LDO1 soft start, LDO3318 */
#define RTS5228_DV3318_CFG		0xff71	/* [6:4] I/O voltage */
#define RTS5228_LDO_OCP			0xff72
#define RTS5228_LDO1_CFG		0xff73	/* [7:6] ramp, [3:1] voltage */
#define RTS5228_PME_FORCE		0xff78
#define RTS5228_CD_RESUME		0xff7c
#define RTS5228_PM_CTL			0xff7e

/* PCIe PHY registers [conventions] */
#define RTS5228_PHY_CFG1		0x01	/* bit 9: OOBS polling */
#define RTS5228_PHY_OPT			0x07
#define RTS5228_PHY_VOLTAGE		0x08	/* bit 9: 1 = 3.3 V */

/* PCI configuration space [2.2] */
#define RTS5228_CFG_SETTINGS1	0x724
#define RTS5228_CFG_SETTINGS2	0x814
#define RTS5228_CFG_TX_IDLE		0x70f
#define RTS5228_EXT_CAP_L1SS	0x001e

/* Scatter-gather descriptor [7.1] */
#define RTS5228_SG_LEN_LOW_SHIFT	16
#define RTS5228_SG_LEN_HIGH_SHIFT	6

#endif	/* _RTSX_5228_H_ */
