/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * BPowerRoster, the R5 Power Kit (docs/POWER.md, section 3).
 */


#include <PowerRoster.h>

#include <fcntl.h>
#include <new>
#include <string.h>
#include <unistd.h>
#include <vector>

#include <Autolock.h>
#include <Looper.h>
#include <Message.h>
#include <MessageRunner.h>
#include <Messenger.h>

#include <R5Admin.h>
#include <r5_power_profile.h>
#include <syscalls.h>

#include "PowerBattery.h"
#include "PowerProtocol.h"


using BPrivate::BatteryReader;
using BPrivate::power_status_battery_info;


static const char* kACDevice = "/dev/power/acpi_ac/0";
static const bigtime_t kServiceTimeout = 2000000;
static const bigtime_t kWatchInterval = 2000000;
	// how often the watcher reads the batteries and AC, as PowerStatus did
static const uint32 kMsgWatcherPoll = 'r5pp';


// #pragma mark - helpers


static bool
read_ac(bool* _onAC)
{
	int fd = open(kACDevice, O_RDONLY);
	if (fd < 0)
		return false;
	uint8 status;
	bool ok = read(fd, &status, 1) == 1;
	close(fd);
	if (ok)
		*_onAC = status != 0;
	return ok;
}


static bool
on_ac(BatteryReader& reader)
{
	bool onAC;
	if (read_ac(&onAC))
		return onAC;

	// No AC device: plugged in unless a battery discharges (as PowerStatus
	// decides)
	reader.Update();
	for (int32 i = 0; i < reader.CountBatteries(); i++) {
		power_status_battery_info info;
		if (reader.GetBatteryInfo(i, &info) == B_OK
			&& (info.state & BATTERY_DISCHARGING) != 0)
			return false;
	}
	return true;
}


static status_t
battery_info(BatteryReader& reader, int32 index, power_battery_info* info)
{
	if (info == NULL)
		return B_BAD_VALUE;

	power_status_battery_info status;
	acpi_extended_battery_info extended;
	status_t result = reader.GetBatteryInfo(index, &status);
	if (result == B_OK)
		result = reader.GetExtendedBatteryInfo(index, &extended);
	if (result != B_OK)
		return result;

	memset(info, 0, sizeof(*info));
	info->state = (uint8)status.state;
	info->capacity = status.capacity;
	info->full_capacity = status.full_capacity;
	// ACPI reports 0xffffffff for "unknown"
	info->design_capacity = (int32)extended.design_capacity > 0
		? (int32)extended.design_capacity : -1;
	info->in_mah = extended.power_unit == ACPI_BATTERY_UNIT_MA;

	info->percent = status.full_capacity > 0 && status.capacity >= 0
		? (int32)((int64)status.capacity * 100 / status.full_capacity) : -1;
	if (info->percent > 100)
		info->percent = 100;

	info->time_left = -1;
	if ((status.state & BATTERY_DISCHARGING) != 0)
		info->time_left = status.time_left;
	else if ((status.state & BATTERY_CHARGING) != 0
		&& status.current_rate > 0 && status.full_capacity > status.capacity) {
		info->time_left = (bigtime_t)3600
			* (status.full_capacity - status.capacity) / status.current_rate;
	}

	int32 lastFull = (int32)extended.last_full_charge;
	info->health = lastFull > 0 && info->design_capacity > 0
		? (int32)((int64)lastFull * 100 / info->design_capacity) : -1;
	return B_OK;
}


static bool
power_info(r5_power_info& info)
{
	memset(&info, 0, sizeof(info));
	info.size = sizeof(info);
	return _kern_r5_get_power_info(&info, sizeof(info)) == B_OK;
}


// #pragma mark - the watcher


/*!	One looper per team, started by the first StartWatching() and quit by
	the last StopWatching(), as BGameController does. It reads the batteries
	and AC every two seconds (the work PowerStatus's thread did) and sends
	kMsgPowerChanged when something changed; it watches the power service
	for the profile and the settings and passes those on. The batteries are
	read in the program itself, so this also works in Safe mode, where the
	service does not run.
*/
class PowerWatcher : public BLooper {
public:
	PowerWatcher()
		:
		BLooper("power watcher"),
		fRunner(NULL),
		fOnAC(true),
		fWatchingService(false)
	{
		fReader.Connect();
		fOnAC = on_ac(fReader);
		fBatteries = _Snapshot();
	}

	virtual ~PowerWatcher()
	{
		delete fRunner;
		if (fWatchingService) {
			BMessenger service(R5_POWER_SERVICE_SIGNATURE);
			BMessage request(kMsgPowerUnwatch);
			request.AddMessenger("target", BMessenger(this));
			service.SendMessage(&request);
		}
	}

	void Start()
	{
		BMessage poll(kMsgWatcherPoll);
		fRunner = new(std::nothrow) BMessageRunner(BMessenger(this), &poll,
			kWatchInterval);
		_WatchService();
	}

	bool HasTargets() const
	{
		return !fTargets.empty();
	}

	void Add(const BMessenger& target)
	{
		fTargets.push_back(target);
	}

	bool Remove(const BMessenger& target)
	{
		for (size_t i = 0; i < fTargets.size(); i++) {
			if (fTargets[i] == target) {
				fTargets.erase(fTargets.begin() + i);
				return true;
			}
		}
		return false;
	}

	virtual void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case kMsgWatcherPoll:
				_Poll();
				break;
			case kMsgPowerChanged:
				// from the service: the profile or the settings
				_Send(*message);
				break;
			default:
				BLooper::MessageReceived(message);
		}
	}

private:
	struct battery_snapshot {
		uint32	state;
		int32	percent;
		int32	minutes;
	};

	std::vector<battery_snapshot> _Snapshot()
	{
		std::vector<battery_snapshot> snapshot;
		fReader.Update();
		for (int32 i = 0; i < fReader.CountBatteries(); i++) {
			power_battery_info info;
			if (battery_info(fReader, i, &info) != B_OK)
				continue;
			battery_snapshot entry;
			entry.state = info.state;
			entry.percent = info.percent;
			entry.minutes = info.time_left < 0 ? -1
				: (int32)(info.time_left / 60);
			snapshot.push_back(entry);
		}
		return snapshot;
	}

	void _Poll()
	{
		if (!fWatchingService)
			_WatchService();

		std::vector<battery_snapshot> now = _Snapshot();
		bool changed = now.size() != fBatteries.size();
		for (size_t i = 0; !changed && i < now.size(); i++) {
			changed = now[i].state != fBatteries[i].state
				|| now[i].percent != fBatteries[i].percent
				|| now[i].minutes != fBatteries[i].minutes;
		}
		fBatteries = now;
		if (changed) {
			BMessage message(kMsgPowerChanged);
			message.AddInt32("change", B_POWER_BATTERY_CHANGED);
			_Send(message);
		}

		bool onAC = on_ac(fReader);
		if (onAC != fOnAC) {
			fOnAC = onAC;
			BMessage message(kMsgPowerChanged);
			message.AddInt32("change", B_POWER_AC_CHANGED);
			message.AddBool("on ac", onAC);
			_Send(message);
		}
	}

	void _WatchService()
	{
		BMessenger service(R5_POWER_SERVICE_SIGNATURE);
		if (!service.IsValid())
			return;
		BMessage request(kMsgPowerWatch);
		request.AddMessenger("target", BMessenger(this));
		fWatchingService = service.SendMessage(&request) == B_OK;
	}

	void _Send(const BMessage& message)
	{
		for (size_t i = 0; i < fTargets.size(); i++)
			fTargets[i].SendMessage(const_cast<BMessage*>(&message));
	}

	BatteryReader					fReader;
	BMessageRunner*					fRunner;
	std::vector<battery_snapshot>	fBatteries;
	bool							fOnAC;
	bool							fWatchingService;
	std::vector<BMessenger>			fTargets;
};


static BLocker sWatcherLock("power watcher lock");
static PowerWatcher* sWatcher = NULL;


// Called with sWatcherLock held.
static void
quit_watcher()
{
	if (sWatcher == NULL)
		return;
	PowerWatcher* watcher = sWatcher;
	sWatcher = NULL;
	if (watcher->Lock())
		watcher->Quit();
}


/*!	No looper of this library may outlive its code (Settings unloads its
	add-ons before ~BApplication walks the loopers).
*/
class PowerWatcherCleanup {
public:
	~PowerWatcherCleanup()
	{
		BAutolock locker(sWatcherLock);
		quit_watcher();
	}
};


static PowerWatcherCleanup sWatcherCleanup;


// #pragma mark - BPowerRoster


BPowerRoster::BPowerRoster()
	:
	fData(NULL)
{
	BatteryReader* reader = new(std::nothrow) BatteryReader;
	if (reader != NULL)
		reader->Connect();
	fData = reader;
}


BPowerRoster::~BPowerRoster()
{
	delete (BatteryReader*)fData;
}


int32
BPowerRoster::CountBatteries() const
{
	BatteryReader* reader = (BatteryReader*)fData;
	return reader != NULL ? reader->CountBatteries() : 0;
}


status_t
BPowerRoster::GetBatteryInfo(int32 index, power_battery_info* info) const
{
	BatteryReader* reader = (BatteryReader*)fData;
	if (reader == NULL)
		return B_NO_MEMORY;
	reader->Update();
	return battery_info(*reader, index, info);
}


bool
BPowerRoster::IsOnAC() const
{
	BatteryReader* reader = (BatteryReader*)fData;
	if (reader == NULL) {
		bool onAC;
		return read_ac(&onAC) ? onAC : true;
	}
	return on_ac(*reader);
}


power_profile
BPowerRoster::Profile() const
{
	r5_power_info info;
	if (!power_info(info) || info.profile > B_POWER_PROFILE_PERFORMANCE)
		return B_POWER_PROFILE_BALANCED;
	return (power_profile)info.profile;
}


bool
BPowerRoster::CanChangeProfile() const
{
	r5_power_info info;
	return power_info(info) && (info.flags & R5_POWER_F_PROFILES) != 0;
}


bool
BPowerRoster::CanSleep(power_sleep_state* state) const
{
	r5_power_info info;
	bool known = power_info(info);
	if (state != NULL) {
		if (known && (info.flags & R5_POWER_F_S0IX) != 0)
			*state = B_POWER_SLEEP_S0IX;
		else if (known && (info.flags & R5_POWER_F_S3) != 0)
			*state = B_POWER_SLEEP_S3;
		else
			*state = B_POWER_SLEEP_NONE;
	}
	return known && (info.flags & R5_POWER_F_SLEEP_WORKS) != 0;
}


status_t
BPowerRoster::GetSettings(BMessage* settings) const
{
	if (settings == NULL)
		return B_BAD_VALUE;
	BMessenger service(R5_POWER_SERVICE_SIGNATURE);
	if (!service.IsValid())
		return B_NO_INIT;
	BMessage request(kMsgPowerGetSettings);
	status_t status = service.SendMessage(&request, settings,
		kServiceTimeout, kServiceTimeout);
	if (status != B_OK)
		return status;
	return settings->what == kMsgPowerGetSettings ? B_OK : B_ERROR;
}


status_t
BPowerRoster::SetSettings(const BMessage& settings)
{
	BMessenger service(R5_POWER_SERVICE_SIGNATURE);
	if (!service.IsValid())
		return B_NO_INIT;
	BMessage request(settings);
	request.what = kMsgPowerSetSettings;
	BMessage reply;
	status_t status = r5_admin_send(service, &request, &reply,
		kServiceTimeout);
	if (status != B_OK)
		return status;
	return reply.GetInt32("status", B_ERROR);
}


/*static*/ status_t
BPowerRoster::StartWatching(BMessenger target)
{
	if (!target.IsValid())
		return B_BAD_VALUE;
	BAutolock locker(sWatcherLock);
	if (sWatcher == NULL) {
		sWatcher = new(std::nothrow) PowerWatcher();
		if (sWatcher == NULL)
			return B_NO_MEMORY;
		sWatcher->Run();
		if (sWatcher->Lock()) {
			sWatcher->Start();
			sWatcher->Unlock();
		}
	}
	if (!sWatcher->Lock())
		return B_ERROR;
	sWatcher->Add(target);
	sWatcher->Unlock();
	return B_OK;
}


/*static*/ status_t
BPowerRoster::StopWatching(BMessenger target)
{
	BAutolock locker(sWatcherLock);
	if (sWatcher == NULL || !sWatcher->Lock())
		return B_BAD_VALUE;
	bool removed = sWatcher->Remove(target);
	bool empty = !sWatcher->HasTargets();
	sWatcher->Unlock();
	// The last target gone: the looper goes too.
	if (empty)
		quit_watcher();
	return removed ? B_OK : B_BAD_VALUE;
}
