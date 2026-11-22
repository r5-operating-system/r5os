/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Calls between the three parts of the rtsx driver: the OpenBSD core
 * (rtsx.cpp, ISC), the RTS5228 part (rtsx_5228.cpp) and the Haiku glue
 * (rtsx_haiku.cpp).
 */
#ifndef _RTSX_H_
#define _RTSX_H_


#include <SupportDefs.h>
#include <util/iovec_support.h>

#include "rtsxvar.h"


/* Size of the one DMA buffer: the host command buffer, then the ADMA table. */
#define RTSX_DMA_BUFSIZE		4096
#define RTSX_HOSTCMD_BUFSIZE	1024
#define RTSX_HOSTCMD_MAX		(RTSX_HOSTCMD_BUFSIZE / 4)
#define RTSX_ADMA_OFFSET		RTSX_HOSTCMD_BUFSIZE
#define RTSX_ADMA_MAX_DESC		((RTSX_DMA_BUFSIZE - RTSX_ADMA_OFFSET) / 8)

/* Largest piece one ADMA descriptor carries (OpenBSD RTSX_DMA_MAX_SEGSIZE). */
#define RTSX_DMA_MAX_SEGSIZE	0x80000
/* Largest transfer the driver accepts from the I/O scheduler, in blocks. */
#define RTSX_MAX_TRANSFER_BLOCKS	1024
#define RTSX_BLOCK_SIZE			512


/* One SD command, the Haiku form of OpenBSD's struct sdmmc_command. */
struct rtsx_command {
	uint8					opcode;
	uint32					arg;
	uint8					rsp_type;	/* RTSX_SD_RSP_TYPE_* */
	uint32					resp[4];

	/* Data phase: physical vectors from the I/O scheduler. */
	bool					read;
	uint32					datalen;
	uint32					blklen;
	const generic_io_vec*	vecs;
	uint32					vec_count;
};


static inline uint32
rtsx_read4(struct rtsx_softc* sc, uint32 reg)
{
	return *(volatile uint32*)(sc->regs_base + reg);
}


static inline void
rtsx_write4(struct rtsx_softc* sc, uint32 reg, uint32 value)
{
	*(volatile uint32*)(sc->regs_base + reg) = value;
}


/* rtsx.cpp: OpenBSD core */
int		rtsx_init(struct rtsx_softc* sc, int attaching);
int		rtsx_host_reset(struct rtsx_softc* sc);
int		rtsx_bus_power(struct rtsx_softc* sc, bool on);
int		rtsx_bus_clock(struct rtsx_softc* sc, int kilohertz);
int		rtsx_bus_width(struct rtsx_softc* sc, int width);
int		rtsx_exec_command(struct rtsx_softc* sc, struct rtsx_command* cmd);
void	rtsx_soft_reset(struct rtsx_softc* sc);
int		rtsx_led_enable(struct rtsx_softc* sc);
int		rtsx_led_disable(struct rtsx_softc* sc);
int		rtsx_read(struct rtsx_softc* sc, uint16 addr, uint8* val);
int		rtsx_write(struct rtsx_softc* sc, uint16 addr, uint8 mask, uint8 val);
int		rtsx_read_phy(struct rtsx_softc* sc, uint8 addr, uint16* val);
int		rtsx_write_phy(struct rtsx_softc* sc, uint8 addr, uint16 val);
int		rtsx_read_cfg(struct rtsx_softc* sc, uint8 func, uint16 addr,
			uint32* val);

/* rtsx_5228.cpp: RTS5228, written from the clean-room specification */
int		rtsx_5228_init(struct rtsx_softc* sc);
int		rtsx_5228_power_on(struct rtsx_softc* sc);
int		rtsx_5228_power_off(struct rtsx_softc* sc);
int		rtsx_5228_set_clock(struct rtsx_softc* sc, int kilohertz);
void	rtsx_5228_overcurrent(struct rtsx_softc* sc);
void	rtsx_5228_shutdown(struct rtsx_softc* sc);
void	rtsx_5228_stop_dma(struct rtsx_softc* sc);
uint64	rtsx_5228_sg_desc(uint32 address, uint32 length, uint8 flags);

#endif	/* _RTSX_H_ */
