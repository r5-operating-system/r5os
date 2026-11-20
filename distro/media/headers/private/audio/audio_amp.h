/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef _AUDIO_AMP_H
#define _AUDIO_AMP_H


/*!	Speaker amplifiers that sit behind an HD Audio codec.

	Some laptops feed their speakers through separate amplifier chips on
	I2C or SPI. The codec sends the samples; the amplifier must be powered,
	unmuted and stopped around each stream. An amplifier driver registers
	here. The HD Audio driver calls the playback hooks for its codec
	subsystem and never needs to know which amplifier it drives.

	The contract is in docs/AUDIO-AMP.md. Calls are only ever added at the
	end of these structures; a call that shipped does not change.
*/


#include <module.h>


#define AUDIO_AMP_MODULE_NAME	"generic/audio_amp/v1"


typedef struct audio_amp_format {
	uint32		rate;			// B_SR_* of the stream
	uint32		format;			// B_FMT_* of the stream
	uint32		channels;
} audio_amp_format;


/*!	Implemented by an amplifier driver. Every hook may sleep. They are
	called from thread context, never with interrupts off or a spinlock
	held. A hook that fails only affects that amplifier.
*/
typedef struct audio_amp_hooks {
	uint32		size;			// sizeof(audio_amp_hooks)

	// Before the codec stream runs, and after it runs.
	status_t	(*prepare)(void* cookie, const audio_amp_format* format);
	status_t	(*start)(void* cookie);
	// Before the codec stream stops, and after it stopped.
	void		(*stop)(void* cookie);
	void		(*unprepare)(void* cookie);

	// Mute from the codec side, for example while headphones are in.
	void		(*set_mute)(void* cookie, bool mute);
	// Gain in hundredths of a decibel. The amplifier clamps it to its
	// own maximum and returns the gain it applied.
	int32		(*set_gain)(void* cookie, int32 gain);

	status_t	(*suspend)(void* cookie);
	status_t	(*resume)(void* cookie);
} audio_amp_hooks;


typedef struct audio_amp_info {
	uint32		size;			// sizeof(audio_amp_info)
	uint32		subsystem;		// HDA codec subsystem, 0 = any analog codec
	uint32		channel;		// 0 left, 1 right, ...
	uint32		rates;			// B_SR_* the amplifier accepts
	int32		max_gain;		// hundredths of a decibel, the hard limit
	char		name[32];		// for logs
} audio_amp_info;


typedef struct audio_amp_module_info {
	module_info	info;

	// Amplifier side.
	status_t	(*register_amp)(const audio_amp_info* info,
					const audio_amp_hooks* hooks, void* cookie, int32* _id);
	void		(*unregister_amp)(int32 id);

	// HD Audio side. Each call goes to every amplifier of that subsystem
	// (and to those registered with subsystem 0), in channel order.
	int32		(*count)(uint32 subsystem);
	uint32		(*rates)(uint32 subsystem);
	status_t	(*prepare)(uint32 subsystem, const audio_amp_format* format);
	status_t	(*start)(uint32 subsystem);
	void		(*stop)(uint32 subsystem);
	void		(*unprepare)(uint32 subsystem);
	void		(*set_mute)(uint32 subsystem, bool mute);
	void		(*set_gain)(uint32 subsystem, int32 gain);
	status_t	(*suspend)(uint32 subsystem);
	status_t	(*resume)(uint32 subsystem);
} audio_amp_module_info;


#endif	// _AUDIO_AMP_H
