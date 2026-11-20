/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


/*!	The list of speaker amplifiers behind HD Audio codecs.
	See audio_amp.h and docs/AUDIO-AMP.md.
*/


#include <audio_amp.h>

#include <stdio.h>
#include <string.h>

#include <KernelExport.h>
#include <lock.h>
#include <util/AutoLock.h>


#define TRACE_ALWAYS(a...)	dprintf("audio_amp: " a)

#define MAX_AMPS			8
#define MAX_SUBSYSTEMS		4


struct amp_entry {
	bool					used;
	int32					id;
	audio_amp_info			info;
	audio_amp_hooks			hooks;
	void*					cookie;
};

// The last mute the codec asked for, so that an amplifier that registers
// later starts in the same state.
struct subsystem_state {
	bool					used;
	uint32					subsystem;
	bool					muted;
};


static mutex sLock = MUTEX_INITIALIZER("audio amp list");
static amp_entry sAmps[MAX_AMPS];
static subsystem_state sStates[MAX_SUBSYSTEMS];
static int32 sNextID = 1;


static bool
matches(const amp_entry& amp, uint32 subsystem)
{
	return amp.used && (amp.info.subsystem == 0
		|| amp.info.subsystem == subsystem);
}


static subsystem_state*
state_for(uint32 subsystem, bool create)
{
	for (int32 i = 0; i < MAX_SUBSYSTEMS; i++) {
		if (sStates[i].used && sStates[i].subsystem == subsystem)
			return &sStates[i];
	}
	if (!create)
		return NULL;
	for (int32 i = 0; i < MAX_SUBSYSTEMS; i++) {
		if (!sStates[i].used) {
			sStates[i].used = true;
			sStates[i].subsystem = subsystem;
			sStates[i].muted = false;
			return &sStates[i];
		}
	}
	return NULL;
}


/*!	Calls \a function for each amplifier of \a subsystem in channel order.
	The list lock is held, so an amplifier cannot unregister while one of
	its hooks runs.
*/
template<typename Function>
static void
for_each_amp(uint32 subsystem, Function function)
{
	bool done[MAX_AMPS] = {};
	while (true) {
		int32 next = -1;
		for (int32 i = 0; i < MAX_AMPS; i++) {
			if (done[i] || !matches(sAmps[i], subsystem))
				continue;
			if (next < 0 || sAmps[i].info.channel < sAmps[next].info.channel)
				next = i;
		}
		if (next < 0)
			return;
		done[next] = true;
		function(sAmps[next]);
	}
}


static status_t
register_amp(const audio_amp_info* info, const audio_amp_hooks* hooks,
	void* cookie, int32* _id)
{
	if (info == NULL || hooks == NULL || _id == NULL)
		return B_BAD_VALUE;

	MutexLocker locker(sLock);

	for (int32 i = 0; i < MAX_AMPS; i++) {
		if (sAmps[i].used)
			continue;

		amp_entry& amp = sAmps[i];
		memset(&amp, 0, sizeof(amp));
		memcpy(&amp.info, info, min_c(info->size, sizeof(amp.info)));
		memcpy(&amp.hooks, hooks, min_c(hooks->size, sizeof(amp.hooks)));
		amp.info.name[sizeof(amp.info.name) - 1] = '\0';
		amp.cookie = cookie;
		amp.id = sNextID++;
		amp.used = true;
		*_id = amp.id;

		TRACE_ALWAYS("%s registered, subsystem %08" B_PRIx32 " channel %"
			B_PRIu32 "\n", amp.info.name, amp.info.subsystem,
			amp.info.channel);

		// Start in the codec's current mute state.
		subsystem_state* state = amp.info.subsystem != 0
			? state_for(amp.info.subsystem, false) : NULL;
		if (state == NULL && amp.info.subsystem == 0) {
			for (int32 j = 0; j < MAX_SUBSYSTEMS; j++) {
				if (sStates[j].used && sStates[j].muted)
					state = &sStates[j];
			}
		}
		if (state != NULL && state->muted && amp.hooks.set_mute != NULL)
			amp.hooks.set_mute(amp.cookie, true);
		return B_OK;
	}

	return B_NO_MEMORY;
}


static void
unregister_amp(int32 id)
{
	MutexLocker locker(sLock);

	for (int32 i = 0; i < MAX_AMPS; i++) {
		if (sAmps[i].used && sAmps[i].id == id) {
			TRACE_ALWAYS("%s unregistered\n", sAmps[i].info.name);
			sAmps[i].used = false;
			return;
		}
	}
}


static int32
count(uint32 subsystem)
{
	MutexLocker locker(sLock);

	int32 found = 0;
	for (int32 i = 0; i < MAX_AMPS; i++) {
		if (matches(sAmps[i], subsystem))
			found++;
	}
	return found;
}


static uint32
rates(uint32 subsystem)
{
	MutexLocker locker(sLock);

	uint32 result = 0;
	bool any = false;
	for (int32 i = 0; i < MAX_AMPS; i++) {
		if (!matches(sAmps[i], subsystem))
			continue;
		result = any ? (result & sAmps[i].info.rates) : sAmps[i].info.rates;
		any = true;
	}
	return result;
}


static status_t
prepare(uint32 subsystem, const audio_amp_format* format)
{
	MutexLocker locker(sLock);

	status_t result = B_OK;
	for_each_amp(subsystem, [&](amp_entry& amp) {
		if (amp.hooks.prepare == NULL)
			return;
		status_t status = amp.hooks.prepare(amp.cookie, format);
		if (status != B_OK) {
			TRACE_ALWAYS("%s prepare: %s\n", amp.info.name, strerror(status));
			result = status;
		}
	});
	return result;
}


static status_t
start(uint32 subsystem)
{
	MutexLocker locker(sLock);

	status_t result = B_OK;
	for_each_amp(subsystem, [&](amp_entry& amp) {
		if (amp.hooks.start == NULL)
			return;
		status_t status = amp.hooks.start(amp.cookie);
		if (status != B_OK) {
			TRACE_ALWAYS("%s start: %s\n", amp.info.name, strerror(status));
			result = status;
		}
	});
	return result;
}


static void
stop(uint32 subsystem)
{
	MutexLocker locker(sLock);

	for_each_amp(subsystem, [&](amp_entry& amp) {
		if (amp.hooks.stop != NULL)
			amp.hooks.stop(amp.cookie);
	});
}


static void
unprepare(uint32 subsystem)
{
	MutexLocker locker(sLock);

	for_each_amp(subsystem, [&](amp_entry& amp) {
		if (amp.hooks.unprepare != NULL)
			amp.hooks.unprepare(amp.cookie);
	});
}


static void
set_mute(uint32 subsystem, bool mute)
{
	MutexLocker locker(sLock);

	subsystem_state* state = state_for(subsystem, true);
	if (state != NULL)
		state->muted = mute;

	for_each_amp(subsystem, [&](amp_entry& amp) {
		if (amp.hooks.set_mute != NULL)
			amp.hooks.set_mute(amp.cookie, mute);
	});
}


static void
set_gain(uint32 subsystem, int32 gain)
{
	MutexLocker locker(sLock);

	for_each_amp(subsystem, [&](amp_entry& amp) {
		if (amp.hooks.set_gain != NULL)
			amp.hooks.set_gain(amp.cookie, min_c(gain, amp.info.max_gain));
	});
}


static status_t
suspend(uint32 subsystem)
{
	MutexLocker locker(sLock);

	status_t result = B_OK;
	for_each_amp(subsystem, [&](amp_entry& amp) {
		status_t status = amp.hooks.suspend != NULL
			? amp.hooks.suspend(amp.cookie) : B_NOT_SUPPORTED;
		if (status != B_OK)
			result = status;
	});
	return result;
}


static status_t
resume(uint32 subsystem)
{
	MutexLocker locker(sLock);

	status_t result = B_OK;
	for_each_amp(subsystem, [&](amp_entry& amp) {
		status_t status = amp.hooks.resume != NULL
			? amp.hooks.resume(amp.cookie) : B_NOT_SUPPORTED;
		if (status != B_OK)
			result = status;
	});
	return result;
}


static status_t
std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			memset(sAmps, 0, sizeof(sAmps));
			memset(sStates, 0, sizeof(sStates));
			return B_OK;
		case B_MODULE_UNINIT:
			return B_OK;
	}
	return B_ERROR;
}


static audio_amp_module_info sAudioAmpModule = {
	{
		AUDIO_AMP_MODULE_NAME,
		0,
		std_ops
	},

	register_amp,
	unregister_amp,

	count,
	rates,
	prepare,
	start,
	stop,
	unprepare,
	set_mute,
	set_gain,
	suspend,
	resume,
};


module_info* modules[] = {
	(module_info*)&sAudioAmpModule,
	NULL
};
