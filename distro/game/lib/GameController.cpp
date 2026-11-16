/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * BGameController (docs/GAME-CONTROLLERS.md, option B): BJoystick for the
 * values, R5_JOYSTICK_GET_INFO for what the controller is, and the
 * mapping database for the standard layout.
 */


#include "GameController.h"

#include <errno.h>
#include <stddef.h>
#include <fcntl.h>
#include <new>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <Autolock.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Joystick.h>
#include <Looper.h>
#include <Message.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <String.h>

#include <joystick_driver.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "GameControllerMapping.h"


using namespace R5GameController;


static const char* const kJoystickDirectory = "/dev/joystick";
static const char* const kLabelsFile = "game_controllers";


// #pragma mark - the device list


static void
scan_directory(const char* path, const char* prefix,
	std::vector<std::string>& names, int depth)
{
	BDirectory directory(path);
	if (directory.InitCheck() != B_OK)
		return;
	BEntry entry;
	while (directory.GetNextEntry(&entry) == B_OK) {
		char name[B_FILE_NAME_LENGTH];
		if (entry.GetName(name) != B_OK)
			continue;
		BString child(prefix);
		if (child.Length() > 0)
			child << "/";
		child << name;
		if (entry.IsDirectory()) {
			if (depth < 3) {
				BString childPath(path);
				childPath << "/" << name;
				scan_directory(childPath.String(), child.String(), names,
					depth + 1);
			}
		} else
			names.push_back(child.String());
	}
}


static std::vector<std::string>
scan_devices()
{
	std::vector<std::string> names;
	scan_directory(kJoystickDirectory, "", names, 0);
	std::set<std::string> sorted(names.begin(), names.end());
	return std::vector<std::string>(sorted.begin(), sorted.end());
}


static BString
device_path(const char* deviceName)
{
	if (deviceName[0] == '/')
		return BString(deviceName);
	BString path(kJoystickDirectory);
	path << "/" << deviceName;
	return path;
}


// #pragma mark - the watcher


/*!	One looper per team, started by the first StartWatching(). It watches
	/dev/joystick and its directories, and after every change compares the
	device list with the one before, so a message is sent once per device.
*/
class GameControllerWatcher : public BLooper {
public:
	GameControllerWatcher()
		:
		BLooper("game controller watcher")
	{
		fDevices = scan_devices();
		_WatchDirectories();
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what != B_NODE_MONITOR) {
			BLooper::MessageReceived(message);
			return;
		}
		_WatchDirectories();
		std::vector<std::string> now = scan_devices();
		std::set<std::string> before(fDevices.begin(), fDevices.end());
		std::set<std::string> after(now.begin(), now.end());
		for (size_t i = 0; i < now.size(); i++) {
			if (before.find(now[i]) == before.end())
				_Send(B_GAME_CONTROLLER_ADDED, now[i].c_str());
		}
		for (size_t i = 0; i < fDevices.size(); i++) {
			if (after.find(fDevices[i]) == after.end())
				_Send(B_GAME_CONTROLLER_REMOVED, fDevices[i].c_str());
		}
		fDevices = now;
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

private:
	void _Watch(const char* path)
	{
		BEntry entry(path);
		node_ref node;
		if (entry.GetNodeRef(&node) != B_OK)
			return;
		for (size_t i = 0; i < fWatched.size(); i++) {
			if (fWatched[i] == node)
				return;
		}
		if (watch_node(&node, B_WATCH_DIRECTORY, this) == B_OK)
			fWatched.push_back(node);
	}

	void _WatchDirectories()
	{
		// /dev/joystick itself, and the bus directories in it (usb,
		// bluetooth), which appear with their first device.
		_Watch(kJoystickDirectory);
		BDirectory directory(kJoystickDirectory);
		BEntry entry;
		while (directory.GetNextEntry(&entry) == B_OK) {
			if (!entry.IsDirectory())
				continue;
			BPath path;
			if (entry.GetPath(&path) == B_OK)
				_Watch(path.Path());
		}
	}

	void _Send(uint32 what, const char* device)
	{
		BMessage message(what);
		message.AddString("device", device);
		for (size_t i = 0; i < fTargets.size(); i++)
			fTargets[i].SendMessage(&message);
	}

	std::vector<std::string>	fDevices;
	std::vector<node_ref>		fWatched;
	std::vector<BMessenger>		fTargets;
};


static BLocker sWatcherLock("game controller watcher lock");
static GameControllerWatcher* sWatcher = NULL;


// #pragma mark - the label choice


static status_t
labels_path(BPath& path)
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK)
		return status;
	return path.Append(kLabelsFile);
}


static void
read_labels(BMessage& labels)
{
	labels.MakeEmpty();
	BPath path;
	if (labels_path(path) != B_OK)
		return;
	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() == B_OK)
		labels.Unflatten(&file);
}


static status_t
write_labels(const BMessage& labels)
{
	BPath path;
	status_t status = labels_path(path);
	if (status != B_OK)
		return status;
	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status = file.InitCheck();
	if (status != B_OK)
		return status;
	return labels.Flatten(&file);
}


static BString
labels_key(uint16 vendor, uint16 product)
{
	BString key;
	key.SetToFormat("labels %04x:%04x", vendor, product);
	return key;
}


// #pragma mark - BGameController


struct BGameController::Private {
	BJoystick			joystick;
	bool				open;
	BString				deviceName;
	BString				name;
	int					infoFD;
	r5_joystick_info	info;
	bool				hasMapping;
	controller_mapping	mapping;
	game_controller_family family;
	int32				order[kMaxAxes];
	raw_state			raw;
	mapped_state		mapped;
	std::vector<std::string> devices;

	Private()
		:
		open(false),
		infoFD(-1),
		hasMapping(false),
		family(B_GAME_CONTROLLER_FAMILY_UNKNOWN)
	{
		memset(&info, 0, sizeof(info));
		info.battery = -1;
		memset(&mapping, 0, sizeof(mapping));
		memset(&raw, 0, sizeof(raw));
		memset(&mapped, 0, sizeof(mapped));
		for (int32 i = 0; i < kMaxAxes; i++)
			order[i] = i;
	}
};


BGameController::BGameController()
	:
	fPrivate(new(std::nothrow) Private)
{
	memset(_reserved, 0, sizeof(_reserved));
	if (fPrivate != NULL)
		fPrivate->devices = scan_devices();
}


BGameController::~BGameController()
{
	Close();
	delete fPrivate;
}


int32
BGameController::CountDevices()
{
	return fPrivate != NULL ? (int32)fPrivate->devices.size() : 0;
}


status_t
BGameController::GetDeviceName(int32 index, char* name, size_t bufferSize)
{
	if (fPrivate == NULL)
		return B_NO_MEMORY;
	if (name == NULL)
		return B_BAD_VALUE;
	if (index < 0 || index >= (int32)fPrivate->devices.size())
		return B_BAD_INDEX;
	if (fPrivate->devices[index].size() >= bufferSize)
		return B_NAME_TOO_LONG;
	strlcpy(name, fPrivate->devices[index].c_str(), bufferSize);
	return B_OK;
}


status_t
BGameController::RescanDevices()
{
	if (fPrivate == NULL)
		return B_NO_MEMORY;
	fPrivate->devices = scan_devices();
	return B_OK;
}


status_t
BGameController::Open(const char* deviceName)
{
	if (fPrivate == NULL)
		return B_NO_MEMORY;
	if (deviceName == NULL || deviceName[0] == '\0')
		return B_BAD_VALUE;
	Close();

	Private& d = *fPrivate;
	BString path = device_path(deviceName);

	// What the controller is: an extra descriptor, so BatteryLevel() can
	// ask again later. A driver without the R5 call leaves the IDs at 0.
	memset(&d.info, 0, sizeof(d.info));
	d.info.battery = -1;
	d.infoFD = open(path.String(), O_RDONLY | O_CLOEXEC);
	if (d.infoFD >= 0) {
		r5_joystick_info info;
		memset(&info, 0, sizeof(info));
		info.size = sizeof(info);
		if (ioctl(d.infoFD, R5_JOYSTICK_GET_INFO, &info, sizeof(info)) == 0)
			d.info = info;
		d.info.name[sizeof(d.info.name) - 1] = '\0';
	}

	if (d.joystick.Open(deviceName, true) < 0) {
		if (d.infoFD >= 0)
			close(d.infoFD);
		d.infoFD = -1;
		return B_ERROR;
	}

	d.raw.axisCount = std::min((int32)kMaxAxes, d.joystick.CountAxes());
	d.raw.hatCount = std::min((int32)kMaxHats, d.joystick.CountHats());
	d.raw.buttonCount = std::min((int32)kMaxButtons,
		d.joystick.CountButtons());
	if (d.raw.axisCount < 0)
		d.raw.axisCount = 0;
	if (d.raw.hatCount < 0)
		d.raw.hatCount = 0;
	if (d.raw.buttonCount < 0)
		d.raw.buttonCount = 0;

	// DirectInput's axis order: the axes sorted by HID usage.
	int32 usageCount = std::min((int32)d.info.axis_count,
		(int32)R5_JOYSTICK_MAX_AXIS_USAGES);
	if (usageCount == d.raw.axisCount)
		axis_order(d.info.axis_usage, d.raw.axisCount, d.order);
	else
		axis_order(NULL, d.raw.axisCount, d.order);

	d.hasMapping = d.info.vendor_id != 0
		&& find_system_mapping(d.info.vendor_id, d.info.product_id,
			d.mapping) == B_OK;
	if (!d.hasMapping)
		memset(&d.mapping, 0, sizeof(d.mapping));

	if (d.info.name[0] != '\0')
		d.name = d.info.name;
	else if (d.hasMapping && d.mapping.name[0] != '\0')
		d.name = d.mapping.name;
	else
		d.name = "Game controller";
	d.family = family_for(d.info.vendor_id, d.info.product_id,
		d.hasMapping ? d.mapping.name : d.name.String(), d.hasMapping);
	d.deviceName = deviceName;
	d.open = true;
	memset(&d.mapped, 0, sizeof(d.mapped));
	return B_OK;
}


void
BGameController::Close()
{
	if (fPrivate == NULL || !fPrivate->open)
		return;
	fPrivate->joystick.Close();
	if (fPrivate->infoFD >= 0)
		close(fPrivate->infoFD);
	fPrivate->infoFD = -1;
	fPrivate->open = false;
	fPrivate->hasMapping = false;
	memset(&fPrivate->raw, 0, sizeof(fPrivate->raw));
	memset(&fPrivate->mapped, 0, sizeof(fPrivate->mapped));
}


bool
BGameController::IsOpen() const
{
	return fPrivate != NULL && fPrivate->open;
}


status_t
BGameController::Update()
{
	if (!IsOpen())
		return B_NO_INIT;
	Private& d = *fPrivate;
	status_t status = d.joystick.Update();
	if (status != B_OK)
		return status;

	int16 axes[kMaxAxes];
	int32 axisCount = d.joystick.CountAxes();
	if (axisCount > 0 && axisCount <= kMaxAxes
		&& d.joystick.GetAxisValues(axes) == B_OK) {
		for (int32 i = 0; i < d.raw.axisCount; i++)
			d.raw.axes[i] = axes[d.order[i]];
	}
	int32 hatCount = d.joystick.CountHats();
	if (hatCount > 0 && hatCount <= kMaxHats)
		d.joystick.GetHatValues(d.raw.hats);
	int32 buttonCount = d.joystick.CountButtons();
	if (buttonCount > 0 && buttonCount <= kMaxButtons)
		d.joystick.GetButtonValues(d.raw.buttons);

	if (d.hasMapping)
		map_state(d.mapping, d.raw, d.mapped);
	return B_OK;
}


bool
BGameController::HasMapping() const
{
	return IsOpen() && fPrivate->hasMapping;
}


bool
BGameController::HasButton(game_controller_button button) const
{
	return HasMapping() && mapping_has_button(fPrivate->mapping, button);
}


bool
BGameController::HasAxis(game_controller_axis axis) const
{
	return HasMapping() && mapping_has_axis(fPrivate->mapping, axis);
}


bool
BGameController::IsButtonDown(game_controller_button button) const
{
	if (!HasMapping() || button < 0 || button >= B_CONTROLLER_BUTTON_COUNT)
		return false;
	return (fPrivate->mapped.buttons & ((uint32)1 << button)) != 0;
}


uint32
BGameController::ButtonValues() const
{
	return HasMapping() ? fPrivate->mapped.buttons : 0;
}


int16
BGameController::AxisValue(game_controller_axis axis) const
{
	if (!HasMapping() || axis < 0 || axis >= B_CONTROLLER_AXIS_COUNT)
		return 0;
	return fPrivate->mapped.axes[axis];
}


int32
BGameController::CountRawAxes() const
{
	return IsOpen() ? fPrivate->raw.axisCount : 0;
}


int16
BGameController::RawAxisValue(int32 index) const
{
	if (!IsOpen() || index < 0 || index >= fPrivate->raw.axisCount)
		return 0;
	// Raw means the driver's order, not DirectInput's.
	for (int32 i = 0; i < fPrivate->raw.axisCount; i++) {
		if (fPrivate->order[i] == index)
			return fPrivate->raw.axes[i];
	}
	return 0;
}


int32
BGameController::CountRawButtons() const
{
	return IsOpen() ? fPrivate->raw.buttonCount : 0;
}


bool
BGameController::IsRawButtonDown(int32 index) const
{
	if (!IsOpen() || index < 0 || index >= fPrivate->raw.buttonCount)
		return false;
	return fPrivate->raw.buttons[index];
}


int32
BGameController::CountRawHats() const
{
	return IsOpen() ? fPrivate->raw.hatCount : 0;
}


uint8
BGameController::RawHatValue(int32 index) const
{
	if (!IsOpen() || index < 0 || index >= fPrivate->raw.hatCount)
		return 0;
	return fPrivate->raw.hats[index];
}


const char*
BGameController::Name() const
{
	return IsOpen() ? fPrivate->name.String() : "";
}


const char*
BGameController::DeviceName() const
{
	return IsOpen() ? fPrivate->deviceName.String() : "";
}


uint16
BGameController::VendorID() const
{
	return IsOpen() ? fPrivate->info.vendor_id : 0;
}


uint16
BGameController::ProductID() const
{
	return IsOpen() ? fPrivate->info.product_id : 0;
}


game_controller_connection
BGameController::Connection() const
{
	if (!IsOpen())
		return B_GAME_CONTROLLER_CONNECTION_OTHER;
	switch (fPrivate->info.bus) {
		case R5_JOYSTICK_BUS_USB:
			return B_GAME_CONTROLLER_CONNECTION_USB;
		case R5_JOYSTICK_BUS_BLUETOOTH:
			return B_GAME_CONTROLLER_CONNECTION_BLUETOOTH;
	}
	// A driver without R5_JOYSTICK_GET_INFO: the directory says it.
	if (fPrivate->deviceName.StartsWith("usb/"))
		return B_GAME_CONTROLLER_CONNECTION_USB;
	if (fPrivate->deviceName.StartsWith("bluetooth/"))
		return B_GAME_CONTROLLER_CONNECTION_BLUETOOTH;
	return B_GAME_CONTROLLER_CONNECTION_OTHER;
}


game_controller_family
BGameController::Family() const
{
	return IsOpen() ? fPrivate->family : B_GAME_CONTROLLER_FAMILY_UNKNOWN;
}


int32
BGameController::BatteryLevel() const
{
	if (!IsOpen() || fPrivate->infoFD < 0)
		return -1;
	r5_joystick_info info;
	memset(&info, 0, sizeof(info));
	info.size = sizeof(info);
	if (ioctl(fPrivate->infoFD, R5_JOYSTICK_GET_INFO, &info,
			sizeof(info)) != 0
		|| info.size < offsetof(r5_joystick_info, axis_count))
		return -1;
	if (info.battery < 0 || info.battery > 100)
		return -1;
	return info.battery;
}


game_controller_labels
BGameController::LabelStyle() const
{
	if (IsOpen()) {
		game_controller_labels stored = LabelStyleFor(VendorID(), ProductID());
		if (stored != B_GAME_CONTROLLER_LABELS_AUTOMATIC)
			return stored;
		if (Family() == B_GAME_CONTROLLER_FAMILY_PLAYSTATION)
			return B_GAME_CONTROLLER_LABELS_PLAYSTATION;
	}
	return B_GAME_CONTROLLER_LABELS_XBOX;
}


/*static*/ game_controller_labels
BGameController::LabelStyleFor(uint16 vendorID, uint16 productID)
{
	if (vendorID == 0)
		return B_GAME_CONTROLLER_LABELS_AUTOMATIC;
	BMessage labels;
	read_labels(labels);
	int32 value;
	if (labels.FindInt32(labels_key(vendorID, productID).String(), &value)
			!= B_OK)
		return B_GAME_CONTROLLER_LABELS_AUTOMATIC;
	if (value == B_GAME_CONTROLLER_LABELS_XBOX)
		return B_GAME_CONTROLLER_LABELS_XBOX;
	if (value == B_GAME_CONTROLLER_LABELS_PLAYSTATION)
		return B_GAME_CONTROLLER_LABELS_PLAYSTATION;
	return B_GAME_CONTROLLER_LABELS_AUTOMATIC;
}


/*static*/ status_t
BGameController::SetLabelStyle(uint16 vendorID, uint16 productID,
	game_controller_labels labels)
{
	if (vendorID == 0)
		return B_BAD_VALUE;
	if (labels != B_GAME_CONTROLLER_LABELS_AUTOMATIC
		&& labels != B_GAME_CONTROLLER_LABELS_XBOX
		&& labels != B_GAME_CONTROLLER_LABELS_PLAYSTATION)
		return B_BAD_VALUE;
	BMessage stored;
	read_labels(stored);
	BString key = labels_key(vendorID, productID);
	stored.RemoveName(key.String());
	if (labels != B_GAME_CONTROLLER_LABELS_AUTOMATIC)
		stored.AddInt32(key.String(), labels);
	return write_labels(stored);
}


/*static*/ status_t
BGameController::ResetLabelStyles()
{
	BPath path;
	status_t status = labels_path(path);
	if (status != B_OK)
		return status;
	BEntry entry(path.Path());
	if (!entry.Exists())
		return B_OK;
	return entry.Remove();
}


/*static*/ status_t
BGameController::StartWatching(BMessenger target)
{
	if (!target.IsValid())
		return B_BAD_VALUE;
	BAutolock locker(sWatcherLock);
	if (sWatcher == NULL) {
		sWatcher = new(std::nothrow) GameControllerWatcher();
		if (sWatcher == NULL)
			return B_NO_MEMORY;
		sWatcher->Run();
	}
	if (!sWatcher->Lock())
		return B_ERROR;
	sWatcher->Add(target);
	sWatcher->Unlock();
	return B_OK;
}


/*static*/ status_t
BGameController::StopWatching(BMessenger target)
{
	BAutolock locker(sWatcherLock);
	if (sWatcher == NULL || !sWatcher->Lock())
		return B_BAD_VALUE;
	bool removed = sWatcher->Remove(target);
	sWatcher->Unlock();
	// The looper stays for the next StartWatching(); its node monitors cost
	// nothing while no directory changes.
	return removed ? B_OK : B_BAD_VALUE;
}


void BGameController::_ReservedGameController1() {}
void BGameController::_ReservedGameController2() {}
void BGameController::_ReservedGameController3() {}
void BGameController::_ReservedGameController4() {}
