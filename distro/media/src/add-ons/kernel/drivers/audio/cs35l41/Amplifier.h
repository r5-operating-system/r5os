/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef CS35L41_AMPLIFIER_H
#define CS35L41_AMPLIFIER_H


#include <SupportDefs.h>

#include <i2c.h>


// The machine data the chip needs and ACPI does not give: the boost
// converter parts and the TDM slot of each amplifier.
struct machine_info {
	uint32		subsystem;			// HDA codec subsystem, from ACPI _SUB
	const char*	name;
	uint32		amp_count;
	uint32		slot[2];			// ASP RX slot per amplifier
	// Boost converter: inductor in nH, capacitor in uF, peak current in mA,
	// and the register values for those parts.
	uint32		inductance;
	uint32		capacitance;
	uint32		peak_current;
	uint32		boost_loop;			// REG_BOOST_LOOP bits 15:0
	uint32		boost_inductor;		// REG_BOOST_SLOPE bits 1:0
	uint32		boost_slope;		// REG_BOOST_SLOPE bits 15:8
};


struct i2c_link {
	i2c_bus_interface*	bus;
	i2c_bus				cookie;
	uint32				path_id;
};


class Amplifier {
public:
								Amplifier(const i2c_link& link,
									const machine_info& machine,
									uint32 index, i2c_addr address);

			status_t			PowerUp();

			status_t			Prepare(uint32 rate);
			status_t			Start();
			void				Stop();
			void				Unprepare();

			void				SetCodecMute(bool mute);
			void				SetAcpiMute(bool mute);
			void				CheckFaults();

			bool				IsReady() const { return fReady; }
			bool				IsRunning() const { return fRunning; }
			uint32				Index() const { return fIndex; }
			i2c_addr			Address() const { return fAddress; }
			const char*			Side() const;

	static	uint32				SupportedRates();

			void				Shutdown();

private:
			status_t			_Read(uint32 reg, uint32* value);
			status_t			_Write(uint32 reg, uint32 value);
			status_t			_Update(uint32 reg, uint32 mask,
									uint32 value);
			status_t			_WaitFor(uint32 reg, uint32 bits,
									bigtime_t timeout, bigtime_t* _waited);

			status_t			_CheckChip();
			status_t			_ApplyErrata();
			status_t			_ApplyOtp();
			status_t			_SetupBoost();
			status_t			_SetupPads();
			void				_ApplyMute();
			uint32				_ReleaseFaults();

			i2c_link			fLink;
			const machine_info&	fMachine;
			uint32				fIndex;
			i2c_addr			fAddress;

			uint32				fRevision;
			bool				fReady;
			bool				fPrepared;
			bool				fRunning;
			bool				fCodecMuted;
			bool				fAcpiMuted;
			bool				fFaulted;
			uint32				fFaultCount;
			uint32				fFaultsSeen;
};


#endif	// CS35L41_AMPLIFIER_H
