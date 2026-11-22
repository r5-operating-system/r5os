/*
 * Copyright 2013, Paweł Dziepak, pdziepak@quarnos.org.
 * Copyright 2002-2008, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2002, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */

/* This file contains the cpu functions (init, etc). */


#include <cpu.h>
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <r5_shutdown.h>
// SPDX-SnippetEnd
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
#include <stddef.h>

#include <ACPI.h>
#include <kernel.h>
#include <lock.h>
#include <r5_power_profile.h>
// SPDX-SnippetEnd
#include <arch/cpu.h>
#include <arch/system_info.h>

#include <string.h>

#include <cpufreq.h>
#include <cpuidle.h>

#include <boot/kernel_args.h>
#include <kscheduler.h>
#include <thread.h>
#include <thread_types.h>
#include <util/AutoLock.h>
#include <util/ThreadAutoLock.h>


/* global per-cpu structure */
cpu_ent gCPU[SMP_MAX_CPUS];
CPUSet gCPUEnabled;

uint32 gCPUCacheLevelCount;
static cpu_topology_node sCPUTopology;

static cpufreq_module_info* sCPUPerformanceModule;
static cpuidle_module_info* sCPUIdleModule;
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
static int32 sPowerProfile = -1;
	// R5_POWER_PROFILE_*, -1 until something set it
static mutex sPowerProfileLock = MUTEX_INITIALIZER("power profile");
// SPDX-SnippetEnd

static spinlock sSetCpuLock;


status_t
cpu_init(kernel_args *args)
{
	return arch_cpu_init(args);
}


status_t
cpu_init_percpu(kernel_args *args, int curr_cpu)
{
	return arch_cpu_init_percpu(args, curr_cpu);
}


status_t
cpu_init_post_vm(kernel_args *args)
{
	return arch_cpu_init_post_vm(args);
}


/*!	Gets the best ranked module of those in \a prefix. Only the one it
	returns is still loaded: the others were put again before anybody could
	use them. (The other CPUs already run when the modules are loaded, so
	the one in use must not be replaced by a better one found later: that
	unloaded the first one under the CPUs idling in it.)
*/
template<typename ModuleInfo>
static ModuleInfo*
get_best_cpu_module(const char* prefix)
{
	ModuleInfo* best = NULL;

	void* cookie = open_module_list(prefix);
	while (true) {
		char name[B_FILE_NAME_LENGTH];
		size_t nameLength = sizeof(name);
		if (read_next_module_name(cookie, name, &nameLength) != B_OK)
			break;

		ModuleInfo* current = NULL;
		if (get_module(name, (module_info**)&current) != B_OK)
			continue;

		dprintf("found %s module: %s\n", prefix, name);

		if (best == NULL || best->rank < current->rank) {
			if (best != NULL)
				put_module(best->info.name);
			best = current;
		} else
			put_module(name);
	}
	close_module_list(cookie);

	return best;
}


static void
load_cpufreq_module()
{
	sCPUPerformanceModule
		= get_best_cpu_module<cpufreq_module_info>(CPUFREQ_MODULES_PREFIX);

	if (sCPUPerformanceModule == NULL)
		dprintf("no valid cpufreq module found\n");
	else
		scheduler_update_policy();
}


static void
load_cpuidle_module()
{
	cpuidle_module_info* module
		= get_best_cpu_module<cpuidle_module_info>(CPUIDLE_MODULES_PREFIX);
	if (module == NULL) {
		dprintf("no valid cpuidle module found\n");
		return;
	}

	dprintf("using cpuidle module %s\n", module->info.name);
	memory_write_barrier();
	sCPUIdleModule = module;
}


status_t
cpu_init_post_modules(kernel_args *args)
{
	status_t result = arch_cpu_init_post_modules(args);
	if (result != B_OK)
		return result;

	load_cpufreq_module();
	load_cpuidle_module();
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	r5_shutdown_log_previous();
// SPDX-SnippetEnd
	return B_OK;
}


status_t
cpu_preboot_init_percpu(kernel_args *args, int curr_cpu)
{
	// set the cpu number in the local cpu structure so that
	// we can use it for get_current_cpu
	memset((void*)&gCPU[curr_cpu], 0, sizeof(gCPU[curr_cpu]));
	gCPU[curr_cpu].cpu_num = curr_cpu;
	gCPUEnabled.SetBitAtomic(curr_cpu);

	B_INITIALIZE_SPINLOCK(&gCPU[curr_cpu].irqs_lock);

	return arch_cpu_preboot_init_percpu(args, curr_cpu);
}


bigtime_t
cpu_get_active_time(int32 cpu)
{
	if (cpu < 0 || cpu > smp_get_num_cpus())
		return 0;

	bigtime_t activeTime;
	uint32 count;

	do {
		count = acquire_read_seqlock(&gCPU[cpu].active_time_lock);
		activeTime = gCPU[cpu].active_time;
	} while (!release_read_seqlock(&gCPU[cpu].active_time_lock, count));

	return activeTime;
}


uint64
cpu_frequency(int32 cpu)
{
	if (cpu < 0 || cpu >= smp_get_num_cpus())
		return 0;
	uint64 frequency = 0;
	arch_get_frequency(&frequency, cpu);
	return frequency;
}


void
clear_caches(void *address, size_t length, uint32 flags)
{
	// TODO: data cache
	if ((B_INVALIDATE_ICACHE & flags) != 0) {
		arch_cpu_sync_icache(address, length);
	}
}


static status_t
cpu_create_topology_node(cpu_topology_node* node, int32* maxID, int32 id)
{
	cpu_topology_level level = static_cast<cpu_topology_level>(node->level - 1);
	ASSERT(level >= 0);

	cpu_topology_node* newNode = new(std::nothrow) cpu_topology_node;
	if (newNode == NULL)
		return B_NO_MEMORY;
	node->children[id] = newNode;

	newNode->level = level;
	if (level != CPU_TOPOLOGY_SMT) {
		newNode->children_count = maxID[level - 1];
		newNode->children
			= new(std::nothrow) cpu_topology_node*[maxID[level - 1]];
		if (newNode->children == NULL)
			return B_NO_MEMORY;

		memset(newNode->children, 0,
			maxID[level - 1] * sizeof(cpu_topology_node*));
	} else {
		newNode->children_count = 0;
		newNode->children = NULL;
	}

	return B_OK;
}


static void
cpu_rebuild_topology_tree(cpu_topology_node* node, int32* lastID)
{
	if (node->children == NULL)
		return;

	int32 count = 0;
	for (int32 i = 0; i < node->children_count; i++) {
		if (node->children[i] == NULL)
			continue;

		if (count != i)
			node->children[count] = node->children[i];

		if (node->children[count]->level != CPU_TOPOLOGY_SMT)
			node->children[count]->id = lastID[node->children[count]->level]++;

		cpu_rebuild_topology_tree(node->children[count], lastID);
		count++;
	}
	node->children_count = count;
}


status_t
cpu_build_topology_tree(void)
{
	sCPUTopology.level = CPU_TOPOLOGY_LEVELS;

	int32 maxID[CPU_TOPOLOGY_LEVELS];
	memset(&maxID, 0, sizeof(maxID));

	const int32 kCPUCount = smp_get_num_cpus();
	for (int32 i = 0; i < kCPUCount; i++) {
		for (int32 j = 0; j < CPU_TOPOLOGY_LEVELS; j++)
			maxID[j] = max_c(maxID[j], gCPU[i].topology_id[j]);
	}

	for (int32 j = 0; j < CPU_TOPOLOGY_LEVELS; j++)
		maxID[j]++;

	sCPUTopology.children_count = maxID[CPU_TOPOLOGY_LEVELS - 1];
	sCPUTopology.children
		= new(std::nothrow) cpu_topology_node*[maxID[CPU_TOPOLOGY_LEVELS - 1]];
	if (sCPUTopology.children == NULL)
		return B_NO_MEMORY;
	memset(sCPUTopology.children, 0,
		maxID[CPU_TOPOLOGY_LEVELS - 1] * sizeof(cpu_topology_node*));

	for (int32 i = 0; i < kCPUCount; i++) {
		cpu_topology_node* node = &sCPUTopology;
		for (int32 j = CPU_TOPOLOGY_LEVELS - 1; j >= 0; j--) {
			int32 id = gCPU[i].topology_id[j];
			if (node->children[id] == NULL) {
				status_t result = cpu_create_topology_node(node, maxID, id);
				if (result != B_OK)
					return result;
			}

			node = node->children[id];
		}

		ASSERT(node->level == CPU_TOPOLOGY_SMT);
		node->id = i;
	}

	int32 lastID[CPU_TOPOLOGY_LEVELS];
	memset(&lastID, 0, sizeof(lastID));
	cpu_rebuild_topology_tree(&sCPUTopology, lastID);

	return B_OK;
}


const cpu_topology_node*
get_cpu_topology(void)
{
	return &sCPUTopology;
}


void
cpu_set_scheduler_mode(enum scheduler_mode mode)
{
	if (sCPUPerformanceModule != NULL)
		sCPUPerformanceModule->cpufreq_set_scheduler_mode(mode);
	if (sCPUIdleModule != NULL)
		sCPUIdleModule->cpuidle_set_scheduler_mode(mode);
// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// A scheduler mode set on its own (ProcessController, an old program)
	// is the profile a version 2 module maps it to: power saving is Power
	// saver, low latency keeps the boost and EPP performance it always had.
	atomic_set(&sPowerProfile, mode == SCHEDULER_MODE_POWER_SAVING
		? R5_POWER_PROFILE_SAVER : R5_POWER_PROFILE_PERFORMANCE);
// SPDX-SnippetEnd
}


// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
//	#pragma mark - processor profile (docs/POWER.md, section 4)


static cpufreq_module_info_v2*
cpufreq_v2_module()
{
	if (sCPUPerformanceModule == NULL)
		return NULL;

	// A version 2 module says so in its name; the first part of its
	// module_info is the version 1 structure.
	const char* name = sCPUPerformanceModule->info.name;
	size_t length = strlen(name);
	size_t suffixLength = strlen(CPUFREQ_MODULE_V2_SUFFIX);
	if (length < suffixLength
		|| strcmp(name + length - suffixLength, CPUFREQ_MODULE_V2_SUFFIX) != 0)
		return NULL;

	return (cpufreq_module_info_v2*)sCPUPerformanceModule;
}


/*!	Sets the processor profile: the scheduler mode (Power saver is power
	saving, Balanced and Performance are low latency), the idle module's
	mode, and the profile of a version 2 cpufreq module. A version 1 module
	follows the scheduler mode, as before.
*/
static status_t
cpu_set_power_profile(int32 profile)
{
	if (profile < 0 || profile >= R5_POWER_PROFILE_COUNT)
		return B_BAD_VALUE;

	MutexLocker locker(sPowerProfileLock);

	scheduler_mode mode = profile == R5_POWER_PROFILE_SAVER
		? SCHEDULER_MODE_POWER_SAVING : SCHEDULER_MODE_LOW_LATENCY;
	status_t status = scheduler_set_operation_mode(mode);
	if (status != B_OK)
		return status;

	cpufreq_module_info_v2* module = cpufreq_v2_module();
	if (module == NULL) {
		cpu_set_scheduler_mode(mode);
		atomic_set(&sPowerProfile, profile);
		return B_OK;
	}

	// Not the module's version 1 hook: that would apply the scheduler
	// mode's profile on every CPU, too, at the same time as this one.
	if (sCPUIdleModule != NULL)
		sCPUIdleModule->cpuidle_set_scheduler_mode(mode);

	status = module->cpufreq_set_profile(profile);
	if (status == B_OK)
		atomic_set(&sPowerProfile, profile);
	return status;
}


/*!	What the firmware offers for sleep (docs/POWER.md, section 5): suspend
	to idle needs the FADT low-power S0 flag and an LPS0 device, suspend to
	RAM an _S3_ object. Read once; the ACPI module is loaded only for this
	and put again.
*/
static uint32
cpu_sleep_flags()
{
	static int32 sFlags = -1;
	int32 flags = atomic_get(&sFlags);
	if (flags >= 0)
		return (uint32)flags;

	flags = 0;
	acpi_module_info* acpi;
	if (get_module(B_ACPI_MODULE_NAME, (module_info**)&acpi) == B_OK) {
		// FADT: the flags are at byte 112; bit 21 is "low power S0 idle
		// capable" (ACPI 6.x, table 5.10)
		const uint32 kFADTFlagsOffset = 112;
		const uint32 kLowPowerS0Idle = 1u << 21;
		void* table = NULL;
		bool lowPowerS0 = false;
		if (acpi->get_table("FACP", 0, &table) == B_OK && table != NULL) {
			uint32 tableLength;
			memcpy(&tableLength, (uint8*)table + 4, sizeof(tableLength));
			if (tableLength >= kFADTFlagsOffset + sizeof(uint32)) {
				uint32 fadtFlags;
				memcpy(&fadtFlags, (uint8*)table + kFADTFlagsOffset,
					sizeof(fadtFlags));
				lowPowerS0 = (fadtFlags & kLowPowerS0Idle) != 0;
			}
		}

		char path[256];
		if (lowPowerS0
			&& acpi->get_device("PNP0D80", 0, path, sizeof(path)) == B_OK)
			flags |= R5_POWER_F_S0IX;

		acpi_handle handle;
		if (acpi->get_handle(NULL, "\\_S3_", &handle) == B_OK)
			flags |= R5_POWER_F_S3;

		put_module(B_ACPI_MODULE_NAME);
	}

	atomic_set(&sFlags, flags);
	return (uint32)flags;
}


status_t
_user_r5_set_power_profile(int32 profile)
{
	// Only the power service sets it (docs/POWER.md, section 4); programs
	// go through BPowerRoster and the service's admin port.
	if (geteuid() != 0)
		return B_PERMISSION_DENIED;

	return cpu_set_power_profile(profile);
}


status_t
_user_r5_get_power_info(r5_power_info* userInfo, size_t size)
{
	if (size < offsetof(r5_power_info, reserved))
		return B_BAD_VALUE;
	if (userInfo == NULL || !IS_USER_ADDRESS(userInfo))
		return B_BAD_ADDRESS;

	r5_power_info info = {};
	info.size = (uint32)min_c(size, sizeof(r5_power_info));

	int32 profile = atomic_get(&sPowerProfile);
	if (profile < 0) {
		// nothing set yet: the profile the scheduler mode stands for
		profile = _user_get_scheduler_mode() == SCHEDULER_MODE_POWER_SAVING
			? R5_POWER_PROFILE_SAVER : R5_POWER_PROFILE_PERFORMANCE;
	}
	info.profile = (uint32)profile;

	cpufreq_module_info_v2* module = cpufreq_v2_module();
	if (module != NULL) {
		info.method = module->cpufreq_method();
		if (info.method != R5_POWER_METHOD_NONE)
			info.flags |= R5_POWER_F_PROFILES;
	} else if (sCPUPerformanceModule != NULL)
		info.method = R5_POWER_METHOD_SCHEDULER;
	else
		info.method = R5_POWER_METHOD_NONE;

	info.flags |= cpu_sleep_flags();
		// R5_POWER_F_SLEEP_WORKS stays off until step 4

	return user_memcpy(userInfo, &info, info.size);
}
// SPDX-SnippetEnd


status_t
increase_cpu_performance(int delta)
{
	if (sCPUPerformanceModule != NULL)
		return sCPUPerformanceModule->cpufreq_increase_performance(delta);
	return B_NOT_SUPPORTED;
}


status_t
decrease_cpu_performance(int delta)
{
	if (sCPUPerformanceModule != NULL)
		return sCPUPerformanceModule->cpufreq_decrease_performance(delta);
	return B_NOT_SUPPORTED;
}


void
cpu_idle(void)
{
#if KDEBUG
	if (!are_interrupts_enabled())
		panic("cpu_idle() called with interrupts disabled.");
#endif

	if (sCPUIdleModule != NULL)
		sCPUIdleModule->cpuidle_idle();
	else
		arch_cpu_idle();

	// SPDX-SnippetBegin
	// SPDX-License-Identifier: MIT
	// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
	// MWAIT wake-up via invoke_scheduler does not fire an interrupt, so
	// reschedule here the same way x86_hardware_interrupt() does.
	cpu_status state = disable_interrupts();
	Thread* thread = thread_get_current_thread();
	if (thread->cpu->invoke_scheduler) {
		SpinLocker schedulerLocker(thread->scheduler_lock);
		scheduler_reschedule(B_THREAD_READY);
		schedulerLocker.Unlock();
	}
	restore_interrupts(state);
	// SPDX-SnippetEnd
}


void
cpu_wait(int32* variable, int32 test)
{
	if (sCPUIdleModule != NULL)
		sCPUIdleModule->cpuidle_wait(variable, test);
	else
		arch_cpu_pause();
}


// SPDX-SnippetBegin
// SPDX-License-Identifier: MIT
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
extern "C" bool
r5_cpu_idle_has_module(void)
{
	return sCPUIdleModule != NULL;
}
// SPDX-SnippetEnd


//	#pragma mark -


void
_user_clear_caches(void *address, size_t length, uint32 flags)
{
	clear_caches(address, length, flags);
}


bool
_user_cpu_enabled(int32 cpu)
{
	if (cpu < 0 || cpu >= smp_get_num_cpus())
		return false;

	return !gCPU[cpu].disabled;
}


status_t
_user_set_cpu_enabled(int32 cpu, bool enabled)
{
	int32 i, count;

	if (geteuid() != 0)
		return B_PERMISSION_DENIED;
	if (cpu < 0 || cpu >= smp_get_num_cpus())
		return B_BAD_VALUE;

	// We need to lock here to make sure that no one can disable
	// the last CPU

	InterruptsSpinLocker locker(sSetCpuLock);

	if (!enabled) {
		// check if this is the last CPU to be disabled
		for (i = 0, count = 0; i < smp_get_num_cpus(); i++) {
			if (!gCPU[i].disabled)
				count++;
		}

		if (count == 1)
			return B_NOT_ALLOWED;
	}

	bool oldState = gCPU[cpu].disabled;

	if (oldState != !enabled)
		scheduler_set_cpu_enabled(cpu, enabled);

	if (!enabled) {
		if (smp_get_current_cpu() == cpu) {
			locker.Unlock();
			thread_yield();
			locker.Lock();
		}

		// someone reenabled the CPU while we were rescheduling
		if (!gCPU[cpu].disabled)
			return B_OK;

		ASSERT(smp_get_current_cpu() != cpu);
		while (!thread_is_idle_thread(gCPU[cpu].running_thread)) {
			locker.Unlock();
			thread_yield();
			locker.Lock();

			if (!gCPU[cpu].disabled)
				return B_OK;
			ASSERT(smp_get_current_cpu() != cpu);
		}
	}

	return B_OK;
}

