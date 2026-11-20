/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef CS35L41_REGISTERS_H
#define CS35L41_REGISTERS_H


/*!	Registers of the Cirrus Logic CS35L41 amplifier. Addresses and values
	are from the R5 clean-room specification (generated/CS35L41-SPEC.md
	and CS35L41-SPEC-2.md); the names are R5's own. Each register is 32
	bits, big-endian on I2C, with a 4-byte stride.
*/


#include <SupportDefs.h>


// Identification
#define REG_DEVICE_ID				0x00000000
#define REG_REVISION_ID				0x00000004
#define REG_OTP_ID					0x00000010
#define REG_SOFT_RESET				0x00000020
#define REG_TEST_KEY				0x00000040
#define REG_OTP_MEMORY				0x00000400
#define OTP_WORDS					32

#define DEVICE_ID_ODD_REVISION		0x00035b40
#define DEVICE_ID_EVEN_REVISION		0x00035a40
#define SOFT_RESET_VALUE			0x5a000000
#define TEST_KEY_UNLOCK_1			0x00000055
#define TEST_KEY_UNLOCK_2			0x000000aa
#define TEST_KEY_LOCK_1				0x000000cc
#define TEST_KEY_LOCK_2				0x00000033

// Power
#define REG_POWER_GLOBAL			0x00002014
#define POWER_GLOBAL_ENABLE			0x00000001
#define REG_POWER_BLOCKS			0x00002018
#define POWER_AMP_ENABLE			0x00000001
#define POWER_BOOST_MASK			0x00000030
#define POWER_BOOST_INTERNAL		0x00000020
#define REG_PROTECT_RELEASE			0x00002034

// GPIO pads of the chip
#define REG_PAD_CONTROL				0x0000242c
#define PAD_GPIO1_MASK				0x00030000
#define PAD_GPIO2_MASK				0x07000000
#define PAD_GPIO2_OPEN_DRAIN_IRQ	0x02000000
#define REG_GPIO1_CONTROL			0x00011008
#define REG_GPIO2_CONTROL			0x0001100c
#define GPIO_POLARITY				0x00001000
#define GPIO_NOT_OUTPUT				0x80000000

// Clocks
#define REG_PLL_CLOCK				0x00002c04
#define REG_DSP_CLOCK				0x00002c08
#define REG_GLOBAL_RATE				0x00002c0c

// Boost converter
#define REG_BOOST_PEAK				0x00003808
#define BOOST_PEAK_MASK				0x0000007f
#define REG_BOOST_LOOP				0x00003810
#define BOOST_LOOP_MASK				0x0000ffff
#define REG_BOOST_SLOPE				0x00003814
#define BOOST_INDUCTOR_MASK			0x00000003
#define BOOST_SLOPE_MASK			0x0000ff00
#define BOOST_SLOPE_SHIFT			8

// Serial audio port
#define REG_ASP_ENABLES				0x00004800
#define REG_ASP_BCLK				0x00004804
#define REG_ASP_FORMAT				0x00004808
#define REG_ASP_CONTROL				0x0000480c
#define REG_ASP_RX_SLOT				0x00004820
#define ASP_RX_SLOT_MASK			0x0000003f
#define REG_ASP_TX_WIDTH			0x00004830
#define REG_ASP_RX_WIDTH			0x00004840

// Signal routing
#define REG_ROUTE_DAC				0x00004c00
#define REG_ROUTE_ASP_TX1			0x00004c20
#define REG_ROUTE_ASP_TX2			0x00004c24
#define REG_ROUTE_ASP_TX3			0x00004c28
#define REG_ROUTE_ASP_TX4			0x00004c2c
#define REG_ROUTE_DSP_RX1			0x00004c40
#define REG_ROUTE_DSP_RX2			0x00004c44
#define REG_ROUTE_DSP_RX3			0x00004c48
#define REG_ROUTE_DSP_RX4			0x00004c4c
#define REG_ROUTE_DSP_RX5			0x00004c50
#define REG_ROUTE_DSP_RX6			0x00004c54

// Volume
#define REG_DIGITAL_VOLUME			0x00006000
#define DIGITAL_VOLUME_0DB			0x00008000	// high-pass filter on, 0 dB
#define DIGITAL_VOLUME_MUTE			0x0000a678
#define REG_AMP_GAIN				0x00006c04
#define AMP_GAIN_PCM_SHIFT			5			// PCM gain in bits 9:5
#define AMP_GAIN_PCM_MASK			0x000003e0
#define AMP_GAIN_PDM_MASK			0x0000001f
#define AMP_GAIN_MUTE				0x00000000

// Interrupt status (write 1 to clear)
#define REG_IRQ_STATUS_1			0x00010010
#define REG_IRQ_STATUS_3			0x00010018
#define REG_IRQ_STATUS_4			0x0001001c
#define IRQ1_POWER_UP_DONE			0x01000000
#define IRQ1_POWER_DOWN_DONE		0x00800000
#define IRQ3_OTP_BOOT_ERROR			0x80000000
#define IRQ4_OTP_BOOT_DONE			0x00000002

// Power blocks used with the DSP
#define POWER_VMON_IMON				0x00003000	// bits 12 and 13

// DSP (CS35L41-SPEC.md section 3, CS35L41-SPEC-2.md section 2)
#define REG_DSP_CORE_CONTROL		0x02bc1000
#define DSP_CORE_ENABLE				0x00000001
#define DSP_CORE_START				0x00000200
#define REG_DSP_WATCHDOG			0x02bc7000
#define REG_DSP_SOFT_RESET			0x02b80010
#define REG_DSP_RX_RATE_FIRST		0x02b80080	// RX1..RX8, stride 8
#define REG_DSP_TX_RATE_FIRST		0x02b80280	// TX1..TX8, stride 8
#define DSP_RATE_COUNT				8
#define REG_DSP_PROTECT_LOCK		0x02bc3140
#define DSP_PROTECT_UNLOCK_1		0x00005555
#define DSP_PROTECT_UNLOCK_2		0x0000aaaa
#define REG_DSP_PROTECT_FIRST		0x02bc3000
#define REG_DSP_PROTECT_LAST		0x02bc305c

#define DSP_XM_UNPACKED				0x02800000
#define DSP_YM_UNPACKED				0x03400000
#define DSP_PM_PACKED				0x03800000
#define DSP_XM_PACKED				0x02000000
#define DSP_YM_PACKED				0x02c00000
#define DSP_ALGORITHM_TABLE			(DSP_XM_UNPACKED + 0x28)

// Firmware mailbox
#define REG_MAILBOX_STATUS			0x00013004
#define REG_MAILBOX_COMMAND			0x00013020
#define MAILBOX_PAUSE				1
#define MAILBOX_RESUME				2
#define MAILBOX_STATUS_RUNNING		0
#define MAILBOX_STATUS_PAUSED		1

// Silicon unique ID (OTP die status), selects the factory calibration
#define REG_SILICON_ID_LOW			0x00017040
#define REG_SILICON_ID_HIGH			0x00017044

// Calibration algorithm (controls CAL_AMBIENT, CAL_R, CAL_STATUS,
// CAL_CHECKSUM in XM)
#define CALIBRATION_ALGORITHM		0x000000cd

// Firmware state control values (algorithm 0x000400a4, "HALO_STATE")
#define FIRMWARE_ALGORITHM			0x000400a4
#define FIRMWARE_STATE_RUNNING		2


#endif	// CS35L41_REGISTERS_H
