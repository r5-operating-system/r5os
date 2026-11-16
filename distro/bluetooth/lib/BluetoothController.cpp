/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * BluetoothController and the state it reports (libr5bt.so). The scan
 * follows Haiku's InquiryPanel (src/preferences/bluetooth): a
 * DiscoveryListener on its own looper and DiscoveryAgent::StartInquiry().
 */


#include <R5Bluetooth.h>

#include <Catalog.h>
#include <MessageFilter.h>
#include <MessageRunner.h>
#include <LaunchRoster.h>
#include <Roster.h>

#include <bluetooth/bdaddrUtils.h>
#include <bluetooth/DeviceClass.h>
#include <bluetooth/DiscoveryAgent.h>
#include <bluetooth/DiscoveryListener.h>
#include <bluetooth/LocalDevice.h>
#include <bluetooth/RemoteDevice.h>
#include <bluetooth/HCI/btHCI_event.h>

#include <bluetoothserver_p.h>

#include <new>
#include <string.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "BluetoothController"


// The launch job of bluetooth_server (distro/bluetooth/launch/bluetooth).
static const char* kBluetoothJob = "x-vnd.Haiku-bluetooth_server";

static const uint32 kMsgUpdate = 'btUp';
static const uint32 kMsgScanTimeout = 'btTo';
static const uint32 kMsgListenerFound = 'btLf';
static const uint32 kMsgListenerDone = 'btLd';

static const bigtime_t kUpdateInterval = 3000000;
static const bigtime_t kStartTimeout = 10000000;
static const bigtime_t kServerTimeout = 2000000;

// Inquiry length in units of 1.28 s (Core 6.0, Vol 4, Part E, 7.1.1);
// libbluetooth's default, as Haiku's InquiryPanel uses.
static const uint8 kInquiryLength = BT_DEFAULT_INQUIRY_TIME;
// A scan that never reports its end (the server went away) ends here.
static const bigtime_t kScanTimeout = (bigtime_t)kInquiryLength * 1280000
	+ 5000000;


// #pragma mark - BluetoothRadioInfo, BluetoothDeviceInfo


BluetoothRadioInfo::BluetoothRadioInfo()
	:
	id(-1)
{
	memset(&address, 0, sizeof(address));
}


BluetoothDeviceInfo::BluetoothDeviceInfo()
	:
	deviceClass(0),
	rssi(HCI_RSSI_INVALID)
{
	memset(&address, 0, sizeof(address));
}


BString
BluetoothDeviceInfo::AddressString() const
{
	return bdaddrUtils::ToString(address);
}


BString
BluetoothDeviceInfo::Kind() const
{
	// Major and minor device classes: Bluetooth Assigned Numbers, section
	// 2.8 "Class of Device".
	uint32 major = (deviceClass >> 8) & 0x1f;
	uint32 minor = (deviceClass >> 2) & 0x3f;

	switch (major) {
		case 1:
			return B_TRANSLATE("Computer");
		case 2:
			return B_TRANSLATE("Phone");
		case 3:
			return B_TRANSLATE("Network access point");
		case 4:
			switch (minor) {
				case 1:
				case 2:
					return B_TRANSLATE("Headset");
				case 4:
					return B_TRANSLATE("Microphone");
				case 5:
					return B_TRANSLATE("Speaker");
				case 6:
					return B_TRANSLATE("Headphones");
				case 8:
					return B_TRANSLATE("Car audio");
			}
			return B_TRANSLATE("Audio device");
		case 5:
			switch (minor >> 4) {
				case 1:
					return B_TRANSLATE("Keyboard");
				case 2:
					return B_TRANSLATE("Mouse");
				case 3:
					return B_TRANSLATE("Keyboard and mouse");
			}
			switch (minor & 0x0f) {
				case 1:
				case 2:
					return B_TRANSLATE("Game controller");
				case 3:
					return B_TRANSLATE("Remote control");
			}
			return B_TRANSLATE("Input device");
		case 6:
			return B_TRANSLATE("Printer or camera");
		case 7:
			return B_TRANSLATE("Wearable");
		case 8:
			return B_TRANSLATE("Toy");
		case 9:
			return B_TRANSLATE("Health device");
	}
	return BString();
}


BString
BluetoothDeviceInfo::DisplayName() const
{
	if (!name.IsEmpty())
		return name;

	BString kind = Kind();
	if (kind.IsEmpty())
		return AddressString();
	return kind;
}


int32
BluetoothDeviceInfo::SignalLevel() const
{
	if (rssi == HCI_RSSI_INVALID)
		return -1;
	// R5's display choice: 10 dB per step, from -90 dBm (one bar) to
	// -60 dBm (four bars). docs/LIBR5.md, "Bluetooth".
	if (rssi >= -60)
		return 4;
	if (rssi >= -70)
		return 3;
	if (rssi >= -80)
		return 2;
	if (rssi >= -90)
		return 1;
	return 0;
}


// #pragma mark - BluetoothState


BluetoothState::BluetoothState()
	:
	service(BLUETOOTH_SERVICE_STARTING),
	radioEnabled(true),
	radio(-1),
	scanning(false),
	scanned(false)
{
}


status_t
BluetoothState::Archive(BMessage& archive) const
{
	status_t status = archive.AddInt32("service", service);
	if (status == B_OK)
		status = archive.AddBool("radio_enabled", radioEnabled);
	if (status == B_OK)
		status = archive.AddInt32("radio", radio);
	if (status == B_OK)
		status = archive.AddBool("scanning", scanning);
	if (status == B_OK)
		status = archive.AddBool("scanned", scanned);

	for (size_t i = 0; status == B_OK && i < radios.size(); i++) {
		const BluetoothRadioInfo& info = radios[i];
		status = archive.AddInt32("radio:id", info.id);
		if (status == B_OK)
			status = archive.AddString("radio:name", info.name);
		if (status == B_OK) {
			status = archive.AddData("radio:address", B_RAW_TYPE,
				&info.address, sizeof(info.address));
		}
	}

	for (size_t i = 0; status == B_OK && i < devices.size(); i++) {
		const BluetoothDeviceInfo& info = devices[i];
		status = archive.AddData("device:address", B_RAW_TYPE, &info.address,
			sizeof(info.address));
		if (status == B_OK)
			status = archive.AddString("device:name", info.name);
		if (status == B_OK)
			status = archive.AddUInt32("device:class", info.deviceClass);
		if (status == B_OK)
			status = archive.AddInt8("device:rssi", info.rssi);
	}
	return status;
}


static bool
find_address(const BMessage& archive, const char* name, int32 index,
	bdaddr_t& address)
{
	const void* data;
	ssize_t size;
	if (archive.FindData(name, B_RAW_TYPE, index, &data, &size) != B_OK
		|| size != (ssize_t)sizeof(bdaddr_t))
		return false;
	memcpy(&address, data, sizeof(bdaddr_t));
	return true;
}


status_t
BluetoothState::Unarchive(const BMessage& archive)
{
	service = archive.GetInt32("service", BLUETOOTH_SERVICE_STARTING);
	radioEnabled = archive.GetBool("radio_enabled", true);
	radio = archive.GetInt32("radio", -1);
	scanning = archive.GetBool("scanning", false);
	scanned = archive.GetBool("scanned", false);

	radios.clear();
	int32 id;
	for (int32 i = 0; archive.FindInt32("radio:id", i, &id) == B_OK; i++) {
		BluetoothRadioInfo info;
		info.id = id;
		archive.FindString("radio:name", i, &info.name);
		find_address(archive, "radio:address", i, info.address);
		radios.push_back(info);
	}

	devices.clear();
	bdaddr_t address;
	for (int32 i = 0; find_address(archive, "device:address", i, address);
			i++) {
		BluetoothDeviceInfo info;
		info.address = address;
		archive.FindString("device:name", i, &info.name);
		archive.FindUInt32("device:class", i, &info.deviceClass);
		archive.FindInt8("device:rssi", i, &info.rssi);
		devices.push_back(info);
	}
	return B_OK;
}


const BluetoothRadioInfo*
BluetoothState::CurrentRadio() const
{
	for (size_t i = 0; i < radios.size(); i++) {
		if (radios[i].id == radio)
			return &radios[i];
	}
	return NULL;
}


// #pragma mark - ScanListener


/*!	Runs on its own looper (DiscoveryListener starts it). It owns the
	RemoteDevice objects libbluetooth creates for each device found, which
	DiscoveryListener forgets without deleting when a new inquiry starts.
*/
class BluetoothController::ScanListener : public DiscoveryListener {
public:
	ScanListener(const BMessenger& controller)
		:
		DiscoveryListener(),
		fController(controller)
	{
		// bluetooth_server answers a refused or failed inquiry request
		// with a plain reply instead of the inquiry messages.
		if (Lock()) {
			AddCommonFilter(new ReplyFilter(fController));
			Unlock();
		}
	}

	virtual ~ScanListener()
	{
		_DeleteDevices();
	}

	virtual void DeviceDiscovered(RemoteDevice* device, DeviceClass deviceClass)
	{
		if (device == NULL)
			return;
		// Only kept for deleting: the filter below reports every answer,
		// also the repeats that DiscoveryListener does not pass on.
		(void)deviceClass;
		fDevices.push_back(device);
	}

	virtual void InquiryResponse(int type)
	{
		BMessage done(kMsgListenerDone);
		done.AddInt32("type", type);
		fController.SendMessage(&done);
	}

	virtual void InquiryStarted(status_t status)
	{
		// DiscoveryListener has just emptied its list; the devices of the
		// previous scan are ours alone now.
		_DeleteDevices();
		if (status != B_OK) {
			BMessage done(kMsgListenerDone);
			done.AddInt32("type", BT_INQUIRY_ERROR);
			fController.SendMessage(&done);
		}
	}

private:
	class ReplyFilter : public BMessageFilter {
	public:
		ReplyFilter(const BMessenger& controller)
			:
			BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE),
			fController(controller)
		{
		}

		virtual filter_result Filter(BMessage* message, BHandler** target)
		{
			(void)target;
			int32 status;
			if (message->what == B_NO_REPLY
				|| (message->what == 0
					&& message->FindInt32("status", &status) == B_OK
					&& status != B_OK)) {
				BMessage done(kMsgListenerDone);
				done.AddInt32("type", BT_INQUIRY_ERROR);
				fController.SendMessage(&done);
				return B_SKIP_MESSAGE;
			}
			if (message->what == BT_MSG_INQUIRY_DEVICE)
				_ReportAnswers(message);
			return B_DISPATCH_MESSAGE;
		}

		// Every inquiry answer, the first and the repeats. DiscoveryListener
		// only calls DeviceDiscovered() for the first one of an address and
		// keeps the newer signal strength of a repeat to itself, so a device
		// first heard far away kept one bar.
		void _ReportAnswers(BMessage* message)
		{
			uint8 count = 0;
			if (message->FindUInt8("count", &count) != B_OK)
				return;
			for (uint8 i = 0; i < count; i++) {
				const void* address;
				const void* deviceClass;
				ssize_t addressSize;
				ssize_t classSize;
				if (message->FindData("bdaddr", B_ANY_TYPE, i, &address,
						&addressSize) != B_OK
					|| addressSize != (ssize_t)sizeof(bdaddr_t)
					|| message->FindData("dev_class", B_ANY_TYPE, i,
						&deviceClass, &classSize) != B_OK
					|| classSize != 3)
					continue;
				const uint8* record = (const uint8*)deviceClass;
				int8 rssi = HCI_RSSI_INVALID;
				message->FindInt8("rssi", i, &rssi);
				BString name;
				message->FindString("friendly_name", i, &name);

				BMessage found(kMsgListenerFound);
				found.AddData("address", B_RAW_TYPE, address, addressSize);
				found.AddUInt32("class", record[0] | (record[1] << 8)
					| (record[2] << 16));
				found.AddString("name", name);
				found.AddInt8("rssi", rssi);
				fController.SendMessage(&found);
			}
		}

	private:
		BMessenger	fController;
	};

	void _DeleteDevices()
	{
		for (size_t i = 0; i < fDevices.size(); i++)
			delete fDevices[i];
		fDevices.clear();
	}

	BMessenger					fController;
	std::vector<RemoteDevice*>	fDevices;
};


// #pragma mark - BluetoothController


BluetoothController::BluetoothController(const BMessenger& target)
	:
	BLooper("bluetooth controller"),
	fTarget(target),
	fRunner(NULL),
	fScanTimeout(NULL),
	fListener(NULL),
	fLaunched(false),
	fLaunchTime(0)
{
}


BluetoothController::~BluetoothController()
{
	delete fRunner;
	delete fScanTimeout;

	if (fListener != NULL) {
		if (fState.scanning)
			_CancelScan();
		if (fListener->Lock())
			fListener->Quit();
	}
}


status_t
BluetoothController::Start()
{
	thread_id thread = Run();
	if (thread < 0)
		return thread;

	BMessage update(kMsgUpdate);
	fRunner = new BMessageRunner(BMessenger(this), &update, kUpdateInterval);
	PostMessage(kMsgBluetoothRefresh);
	return B_OK;
}


bool
BluetoothController::QuitRequested()
{
	return true;
}


void
BluetoothController::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgBluetoothRefresh:
			// Asked for: send the state even when it did not change.
			fLastPosted.clear();
			_Update();
			break;

		case kMsgUpdate:
			_Update();
			break;

		case kMsgBluetoothScan:
			_StartScan();
			break;

		case kMsgBluetoothCancelScan:
			_CancelScan();
			break;

		case kMsgBluetoothSetRadio:
		{
			bool on;
			if (message->FindBool("on", &on) == B_OK)
				_SetRadio(on);
			break;
		}

		case kMsgBluetoothPair:
			_Pair(message);
			break;

		case kMsgBluetoothForget:
		{
			const void* address;
			ssize_t size;
			BMessage result(kMsgBluetoothForgetResult);
			result.AddString("name", message->GetString("name", ""));
			bool forgotten = false;
			BMessenger server(BLUETOOTH_SIGNATURE);
			if (message->FindData("bdaddr", B_ANY_TYPE, &address, &size)
					== B_OK && size == (ssize_t)sizeof(bdaddr_t)
				&& server.IsValid()) {
				BMessage request(BT_MSG_R5_FORGET);
				request.AddData("bdaddr", B_ANY_TYPE, address, size);
				BMessage reply;
				if (server.SendMessage(&request, &reply, kServerTimeout,
						kServerTimeout) == B_OK)
					forgotten = reply.GetBool("forgotten", false);
			}
			result.AddBool("forgotten", forgotten);
			fTarget.SendMessage(&result, (BHandler*)NULL, kServerTimeout);
			break;
		}

		case BT_MSG_R5_PAIR_RESULT:
		{
			BMessage result(kMsgBluetoothPairResult);
			result.AddBool("paired", message->GetBool("paired", false));
			result.AddString("name", message->GetString("name", ""));
			fTarget.SendMessage(&result, (BHandler*)NULL, kServerTimeout);
			break;
		}

		case kMsgBluetoothSelectRadio:
		{
			int32 id;
			if (message->FindInt32("hci_id", &id) == B_OK
				&& _FindRadio(id) != NULL && id != fState.radio) {
				_CancelScan();
				_StopScan();
				fState.radio = id;
				fState.devices.clear();
				fState.scanned = false;
				_PostState();
			}
			break;
		}

		case kMsgListenerFound:
			_DeviceFound(message);
			break;

		case kMsgListenerDone:
		case kMsgScanTimeout:
			if (fState.scanning) {
				_StopScan();
				fState.scanned = true;
				_PostState();
			}
			break;

		default:
			BLooper::MessageReceived(message);
			break;
	}
}


void
BluetoothController::_Update()
{
	if (!be_roster->IsRunning(BLUETOOTH_SIGNATURE)) {
		if (fState.service == BLUETOOTH_SERVICE_RUNNING) {
			// It quit or crashed. It is not started again from here, so a
			// crash is not hidden behind a restart loop.
			fState.service = BLUETOOTH_SERVICE_STOPPED;
		} else if (!fLaunched) {
			fLaunched = true;
			fLaunchTime = system_time();
			// Through launch_daemon, which starts the system service as
			// uid 0 (distro/bluetooth/launch). BRoster::Launch() would start
			// a second server as the person signed in, without the rights
			// for its settings.
			status_t status = BLaunchRoster().Start(kBluetoothJob);
			fState.service = status == B_OK || status == B_ALREADY_RUNNING
				? BLUETOOTH_SERVICE_STARTING : BLUETOOTH_SERVICE_FAILED;
		} else if (fState.service == BLUETOOTH_SERVICE_STARTING
			&& system_time() - fLaunchTime > kStartTimeout) {
			fState.service = BLUETOOTH_SERVICE_FAILED;
		}

		// The LocalDevice objects talk to the server that is gone. They
		// cannot be deleted (private destructor), only forgotten.
		fRadios.clear();
		fState.radios.clear();
		fState.radio = -1;
		_StopScan();
		_PostState();
		return;
	}

	std::vector<hci_id> ids;
	bool enabled;
	if (!_ReadServer(ids, enabled)) {
		// Registered but not answering yet: it has just been started.
		if (fState.service != BLUETOOTH_SERVICE_RUNNING
			&& fLaunched && system_time() - fLaunchTime > kStartTimeout)
			fState.service = BLUETOOTH_SERVICE_FAILED;
		_PostState();
		return;
	}

	fState.service = BLUETOOTH_SERVICE_RUNNING;
	fState.radioEnabled = enabled;
	_ReadRadios(ids);
	if (!enabled && fState.scanning) {
		_CancelScan();
		_StopScan();
	}
	_PostState();
}


bool
BluetoothController::_ReadServer(std::vector<hci_id>& ids, bool& enabled)
{
	BMessenger server(BLUETOOTH_SIGNATURE);
	if (!server.IsValid())
		return false;

	// The switch and the radios by hci_id, in one request with a timeout.
	BMessage request(BT_MSG_R5_GET_RADIO);
	BMessage reply;
	if (server.SendMessage(&request, &reply, kServerTimeout, kServerTimeout)
			!= B_OK
		|| reply.FindBool("enabled", &enabled) != B_OK)
		return false;

	ids.clear();
	int32 id;
	for (int32 i = 0; reply.FindInt32("hci_id", i, &id) == B_OK; i++)
		ids.push_back(id);
	return true;
}


// Acquires exactly the radios it does not know yet and forgets the ones that
// went away. It used the plain acquire, which hands the radios out in turn:
// a known radio handed out again was a leaked LocalDevice, and a radio
// replaced within one poll (same count, new hci_id) was never seen.
void
BluetoothController::_ReadRadios(const std::vector<hci_id>& ids)
{
	bool changed = false;
	for (size_t i = 0; i < fRadios.size();) {
		bool present = false;
		for (size_t j = 0; j < ids.size() && !present; j++)
			present = ids[j] == fRadios[i].id;
		if (present) {
			i++;
			continue;
		}
		// Gone. Its objects talk to a radio that no longer exists; they
		// cannot be deleted (private destructor), only forgotten.
		if (fRadios[i].id == fState.radio) {
			_CancelScan();
			_StopScan();
		}
		fRadios.erase(fRadios.begin() + i);
		changed = true;
	}

	std::vector<BluetoothRadioInfo> radios;
	for (size_t i = 0; i < ids.size(); i++) {
		const BluetoothRadioInfo* known = NULL;
		for (size_t j = 0; j < fState.radios.size() && known == NULL; j++) {
			if (fState.radios[j].id == ids[i] && _FindRadio(ids[i]) != NULL)
				known = &fState.radios[j];
		}
		if (known != NULL) {
			radios.push_back(*known);
			continue;
		}

		LocalDevice* device = LocalDevice::GetLocalDevice(ids[i]);
		if (device == NULL)
			continue;
		Radio radio = { ids[i], device, NULL };
		fRadios.push_back(radio);
		changed = true;

		BluetoothRadioInfo info;
		info.id = ids[i];
		info.name = device->GetFriendlyName();
		// libbluetooth's error strings are "Unknown|<reason>".
		if (info.name.StartsWith("Unknown|"))
			info.name = "";
		info.address = device->GetBluetoothAddress();
		radios.push_back(info);
	}
	if (!changed && radios.size() == fState.radios.size())
		return;
	fState.radios = radios;

	if (fState.CurrentRadio() == NULL) {
		fState.radio = fState.radios.empty() ? -1 : fState.radios[0].id;
		fState.devices.clear();
		fState.scanned = false;
	}
}


BluetoothController::Radio*
BluetoothController::_FindRadio(hci_id id)
{
	for (size_t i = 0; i < fRadios.size(); i++) {
		if (fRadios[i].id == id)
			return &fRadios[i];
	}
	return NULL;
}


void
BluetoothController::_StartScan()
{
	Radio* radio = _FindRadio(fState.radio);
	if (fState.scanning || !fState.radioEnabled
		|| fState.service != BLUETOOTH_SERVICE_RUNNING || radio == NULL) {
		_PostState();
		return;
	}

	if (radio->agent == NULL)
		radio->agent = radio->device->GetDiscoveryAgent();
	if (fListener == NULL)
		fListener = new(std::nothrow) ScanListener(BMessenger(this));
	if (radio->agent == NULL || fListener == NULL) {
		_PostState();
		return;
	}

	if (radio->agent->StartInquiry(BT_GIAC, fListener,
			(bigtime_t)kInquiryLength) != B_OK) {
		_PostState();
		return;
	}

	fState.devices.clear();
	fState.scanning = true;
	delete fScanTimeout;
	BMessage timeout(kMsgScanTimeout);
	fScanTimeout = new BMessageRunner(BMessenger(this), &timeout,
		kScanTimeout, 1);
	_PostState();
}


void
BluetoothController::_CancelScan()
{
	if (!fState.scanning || fListener == NULL)
		return;
	Radio* radio = _FindRadio(fState.radio);
	if (radio != NULL && radio->agent != NULL)
		radio->agent->CancelInquiry(fListener);
}


void
BluetoothController::_StopScan()
{
	fState.scanning = false;
	delete fScanTimeout;
	fScanTimeout = NULL;
}


void
BluetoothController::_DeviceFound(BMessage* message)
{
	if (!fState.scanning)
		return;

	BluetoothDeviceInfo info;
	if (!find_address(*message, "address", 0, info.address))
		return;
	message->FindUInt32("class", &info.deviceClass);
	message->FindString("name", &info.name);
	message->FindInt8("rssi", &info.rssi);

	for (size_t i = 0; i < fState.devices.size(); i++) {
		if (bdaddrUtils::Compare(fState.devices[i].address, info.address)) {
			if (info.name.IsEmpty())
				info.name = fState.devices[i].name;
			if (info.rssi == HCI_RSSI_INVALID)
				info.rssi = fState.devices[i].rssi;
			fState.devices[i] = info;
			_PostState();
			return;
		}
	}
	fState.devices.push_back(info);
	_PostState();
}


void
BluetoothController::_Pair(BMessage* message)
{
	// Create Connection makes bluetooth_server authenticate, which pairs a
	// device without a key (Secure Simple Pairing). Haiku's
	// RemoteDevice::Connect() sends the same request. A scan would keep the
	// radio from paging, so it stops first.
	const bdaddr_t* address;
	ssize_t size;
	if (message->FindData("bdaddr", B_ANY_TYPE, (const void**)&address,
			&size) != B_OK || size != (ssize_t)sizeof(bdaddr_t)
		|| _FindRadio(fState.radio) == NULL || !fState.radioEnabled)
		return;

	if (fState.scanning) {
		_CancelScan();
		_StopScan();
		_PostState();
	}

	BMessenger server(BLUETOOTH_SIGNATURE);
	if (!server.IsValid())
		return;

	BMessage request(BT_REQ_CREATE_CONN);
	request.AddInt32("hci_id", fState.radio);
	request.AddData("bdaddr", B_ANY_TYPE, address, sizeof(bdaddr_t));
	request.AddString("name", message->GetString("name", ""));
	// The scan does not keep these: R2 and no clock offset, the values
	// BlueZ uses for a device it has no inquiry data for.
	request.AddUInt8("pscan_rep_mode", 0x02);
	request.AddUInt16("clock_offset", 0);
	request.AddUInt32("record", message->GetUInt32("class", 0));
	// Not waiting: pairing takes as long as the person needs.
	// The answer (BT_MSG_R5_PAIR_RESULT) comes back to this looper.
	server.SendMessage(&request, this, kServerTimeout);
}


void
BluetoothController::_SetRadio(bool on)
{
	status_t status = B_ERROR;
	BMessenger server(BLUETOOTH_SIGNATURE);
	if (server.IsValid()) {
		BMessage request(BT_MSG_R5_SET_RADIO);
		request.AddBool("enabled", on);
		BMessage reply;
		status = server.SendMessage(&request, &reply, kServerTimeout,
			kServerTimeout);
		if (status == B_OK) {
			if (reply.what == B_NO_REPLY)
				status = B_NOT_SUPPORTED;
			else
				status = reply.GetInt32("status", B_ERROR);
		}
		if (status == B_OK)
			fState.radioEnabled = reply.GetBool("enabled", on);
	}

	if (!fState.radioEnabled && fState.scanning) {
		_CancelScan();
		_StopScan();
	}

	BMessage result(kMsgBluetoothRadioResult);
	result.AddBool("on", on);
	result.AddInt32("status", status);
	fTarget.SendMessage(&result, (BHandler*)NULL, kServerTimeout);
	_PostState();
}


void
BluetoothController::_PostState()
{
	BMessage state(kMsgBluetoothStateChanged);
	if (fState.Archive(state) != B_OK)
		return;

	ssize_t size = state.FlattenedSize();
	std::vector<char> flat(size > 0 ? size : 0);
	if (size > 0 && state.Flatten(flat.data(), size) == B_OK) {
		if (flat == fLastPosted)
			return;
	} else
		flat.clear();

	if (fTarget.SendMessage(&state, (BHandler*)NULL, kServerTimeout) == B_OK)
		fLastPosted.swap(flat);
	else
		fLastPosted.clear();
}
