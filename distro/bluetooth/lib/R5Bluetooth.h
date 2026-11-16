/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * The R5 Bluetooth Kit, libr5bt.so (docs/LIBR5.md, "Bluetooth";
 * docs/BLUETOOTH.md, step 3a). It sits on top of Haiku's libbluetooth and
 * bluetooth_server and is shared by the Bluetooth page of Settings and,
 * later, the Bluetooth program and its Deskbar item.
 *
 * A function, field or message that shipped does not change. Add new ones
 * at the end.
 */
#ifndef R5_BLUETOOTH_H
#define R5_BLUETOOTH_H


#include <GroupView.h>
#include <Looper.h>
#include <Messenger.h>
#include <String.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/HCI/btHCI.h>

#include <vector>


class BBox;
class BButton;
class BCheckBox;
class BListView;
class BMenuField;
class BMessageRunner;
class BPopUpMenu;
class BStringView;

namespace Bluetooth {
	class DiscoveryAgent;
	class LocalDevice;
}


// Requests understood by BluetoothController
enum {
	kMsgBluetoothRefresh		= 'btRf',
		// Read the current state and report it
	kMsgBluetoothScan			= 'btSc',
		// Look for devices nearby (about 13 seconds)
	kMsgBluetoothCancelScan		= 'btCs',
	kMsgBluetoothSetRadio		= 'btPw',
		// bool "on". System-wide: callers check the administrator lock
	kMsgBluetoothSelectRadio	= 'btSr',
		// int32 "hci_id"
	kMsgBluetoothPair			= 'btPa',
		// data "bdaddr" (bdaddr_t), string "name", uint32 "class": pair
		// with a device found by a scan (step 4). The result shows in the
		// device's own connection and pairing events.
	kMsgBluetoothForget			= 'btFg'
		// data "bdaddr", string "name": forget a paired device (its keys)
		// and end its link; answered with kMsgBluetoothForgetResult
};

// Notifications sent to the controller's target
enum {
	kMsgBluetoothStateChanged	= 'btST',
		// A flattened BluetoothState
	kMsgBluetoothRadioResult	= 'btRR',
		// bool "on", int32 "status"
	kMsgBluetoothPairResult		= 'btPR',
		// bool "paired", string "name": the end of a kMsgBluetoothPair
	kMsgBluetoothForgetResult	= 'btFR'
		// bool "forgotten", string "name"
};


enum bluetooth_service_state {
	BLUETOOTH_SERVICE_STARTING = 0,
		// bluetooth_server was asked to start and does not answer yet
	BLUETOOTH_SERVICE_RUNNING = 1,
	BLUETOOTH_SERVICE_FAILED = 2,
		// It could not be started
	BLUETOOTH_SERVICE_STOPPED = 3
		// It was running and is gone (launch_daemon starts it again)
};


struct BluetoothRadioInfo {
								BluetoothRadioInfo();

			hci_id				id;
			BString				name;
			bdaddr_t			address;
};


struct BluetoothDeviceInfo {
								BluetoothDeviceInfo();

			// "00:11:22:33:44:55"
			BString				AddressString() const;
			// What the device is, from its class of device ("Phone",
			// "Headphones", ...), localized. Empty when not known.
			BString				Kind() const;
			// Its name, or the kind and address when it sent no name.
			BString				DisplayName() const;
			// The four steps of r5_level_glyph_draw(R5_LEVEL_SIGNAL), or -1
			// when the radio reported no signal strength.
			int32				SignalLevel() const;

			bdaddr_t			address;
			BString				name;
			uint32				deviceClass;
				// The 24-bit class of device
			int8				rssi;
				// dBm; HCI_RSSI_INVALID (127) when not reported
};


struct BluetoothState {
								BluetoothState();

			status_t			Archive(BMessage& archive) const;
			status_t			Unarchive(const BMessage& archive);

			const BluetoothRadioInfo* CurrentRadio() const;

			int32				service;
				// bluetooth_service_state
			bool				radioEnabled;
				// The radio switch of bluetooth_server
			hci_id				radio;
				// The chosen radio, or -1
			std::vector<BluetoothRadioInfo> radios;
			bool				scanning;
			bool				scanned;
				// A scan finished since the controller started
			std::vector<BluetoothDeviceInfo> devices;
				// Found by the last scan, in the order they answered
};


/*!	Does all bluetooth_server and libbluetooth work on its own thread, so a
	window never waits on the server or a radio. bluetooth_server is a system
	service; when it is not running, the controller asks launch_daemon once
	to start it. Every request it sends itself has a
	timeout; libbluetooth's own calls (radio name, scan) have none and only
	ever run on this thread.
*/
class BluetoothController : public BLooper {
public:
								BluetoothController(const BMessenger& target);
	virtual						~BluetoothController();

			status_t			Start();
	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			class ScanListener;
			struct Radio {
				hci_id					id;
				Bluetooth::LocalDevice*	device;
				Bluetooth::DiscoveryAgent* agent;
			};

			void				_Update();
			bool				_ReadServer(std::vector<hci_id>& ids,
									bool& enabled);
			void				_ReadRadios(const std::vector<hci_id>& ids);
			Radio*				_FindRadio(hci_id id);
			void				_StartScan();
			void				_CancelScan();
			void				_StopScan();
			void				_DeviceFound(BMessage* message);
			void				_SetRadio(bool on);
			void				_Pair(BMessage* message);
			void				_PostState();

			BMessenger			fTarget;
			BMessageRunner*		fRunner;
			BMessageRunner*		fScanTimeout;
			ScanListener*		fListener;
			bool				fLaunched;
			bigtime_t			fLaunchTime;
			BluetoothState		fState;
			std::vector<Radio>	fRadios;
				// The radios bluetooth_server reported. LocalDevice and
				// DiscoveryAgent have private destructors in libbluetooth,
				// so each is acquired once and kept for the controller's
				// life.
			std::vector<char>	fLastPosted;
				// The last state sent, flattened: an unchanged state is not
				// sent again

			uint32				_reserved[8];
};


/*!	The whole Bluetooth panel: the radio switch, the radio, and the devices
	nearby. The Bluetooth page of Settings shows it as its page.
*/
class BluetoothView : public BGroupView {
public:
								BluetoothView(bool settingsPage = false);
	virtual						~BluetoothView();

	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();
	virtual	void				MessageReceived(BMessage* message);

			// The radio switch is system-wide. A program keeps it disabled
			// while the administrator lock is closed; the rest of the panel
			// stays usable. On by default.
			void				SetRadioSwitchAllowed(bool allowed);

private:
			void				_StopController();
			void				_SetState(const BluetoothState& state);
			void				_UpdateRadio();
			void				_UpdateDevices();
			void				_UpdateControls();
			void				_SendToController(BMessage* message);

private:
			bool				fSettingsPage;
			bool				fRadioSwitchAllowed;
			bool				fHasState;
			bool				fRadioFailed;
			BluetoothController* fController;
			BMessenger			fControllerMessenger;
			BluetoothState		fState;

			BView*				fSummaryIcon;
			BStringView*		fSummaryTitle;
			BStringView*		fSummaryDetail;
			BCheckBox*			fRadioSwitch;
			BMenuField*			fRadioField;
			BPopUpMenu*			fRadioMenu;
			BStringView*		fRadioNote;
			BListView*			fDeviceList;
			BStringView*		fScanStatus;
			BButton*			fScanButton;

			// The pairing status line (taken from the reserved room; the
			// size of the class stays the same).
			BString*			fPairStatus;
			int32				fPairing;
			// room for later private state (libr5bt.so)
			uint32				_reserved[5];
};


#endif	// R5_BLUETOOTH_H
