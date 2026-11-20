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
	// The ID in the speaker protection firmware names ("103c8a29").
	const char*	firmware_id;
};


// A firmware file read into memory (Firmware.cpp).
struct firmware_file {
	uint8*		data;
	size_t		size;
	char		name[96];
};


// The location of one algorithm in DSP memory, from the algorithm table.
struct dsp_algorithm {
	uint32		id;
	uint32		version;
	uint32		xm_base;			// word offsets
	uint32		ym_base;
};

#define MAX_DSP_ALGORITHMS	16


struct register_value;


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

			status_t			StartFirmware(const firmware_file& program,
									const firmware_file& tuning,
									const uint8* calibration,
									size_t calibrationSize);
			bool				IsFirmwareRunning() const
									{ return fDspRunning; }

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
			status_t			_WriteTable(const register_value* table,
									size_t count);
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

			// DSP firmware (AmplifierDsp.cpp)
			status_t			_WriteBlock(uint32 reg, const uint8* data,
									size_t size);
			status_t			_LoadProgram(const firmware_file& file);
			status_t			_ReadAlgorithms();
			status_t			_LoadTuning(const firmware_file& file);
			status_t			_FindControl(const firmware_file& file,
									uint32 algorithm, const char* name,
									uint32* _offset);
			const dsp_algorithm* _Algorithm(uint32 id) const;
			status_t			_ApplyCalibration(
									const firmware_file& program,
									const uint8* data, size_t size);
			status_t			_Mailbox(uint32 command, uint32 expected);
			void				_StopCore();
			void				_FallBackToBypass(const char* reason);

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

			bool				fDspRunning;
			uint32				fBlockSize;
			uint32				fFirmwareID;
			dsp_algorithm		fAlgorithms[MAX_DSP_ALGORITHMS];
			uint32				fAlgorithmCount;
};


#endif	// CS35L41_AMPLIFIER_H
