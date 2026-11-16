/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * The Bluetooth program (docs/BLUETOOTH.md, step 3b): the BluetoothView of
 * libr5bt.so in a window, the same panel as the Bluetooth page of Settings.
 * It replaces Haiku's Bluetooth preferences in the regular image and keeps
 * their signature, so the icon and the Deskbar item's menu still find it.
 *
 * Only the Bluetooth switch is system-wide. A person who is not an
 * administrator gets the lock button: an administrator name and password
 * open it for the usual grant (AdminLock.h), and bluetooth_server checks
 * the same grant.
 */


#include <Application.h>
#include <Button.h>
#include <Catalog.h>
#include <LayoutBuilder.h>
#include <MessageRunner.h>
#include <Window.h>

#include <AdminLock.h>
#include <R5Bluetooth.h>
#include <R5Icons.h>

#include <bluetoothserver_p.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Bluetooth"


static const uint32 kMsgLock = 'btLk';
static const uint32 kMsgLockRefresh = 'btLr';

// Login's grant runs out on its own; look again this often.
static const bigtime_t kLockRefreshInterval = 15000000;


class BluetoothWindow : public BWindow {
public:
	BluetoothWindow()
		:
		BWindow(BRect(0, 0, 520, 480), B_TRANSLATE_SYSTEM_NAME("Bluetooth"),
			B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS
				| B_QUIT_ON_WINDOW_CLOSE),
		fView(new BluetoothView(false)),
		fLock(new BButton("lock", "", new BMessage(kMsgLock))),
		fRunner(NULL),
		fAdministrator(false),
		fUnlocked(false)
	{
		BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
			.Add(fView)
			.AddGroup(B_HORIZONTAL)
				.SetInsets(B_USE_WINDOW_SPACING, 0, B_USE_WINDOW_SPACING,
					B_USE_WINDOW_SPACING)
				.Add(fLock)
				.AddGlue()
			.End();

		r5_button_set_lock(fLock, true, fLock->PreferredSize().Height());
		_RefreshLock();

		BMessage refresh(kMsgLockRefresh);
		fRunner = new BMessageRunner(BMessenger(this), &refresh,
			kLockRefreshInterval);
		// The size the panel asks for, not the first guess (that left an
		// empty band under it).
		ResizeToPreferred();
		CenterOnScreen();
	}

	virtual ~BluetoothWindow()
	{
		delete fRunner;
	}

	virtual void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case kMsgLock:
				if (fUnlocked)
					r5_admin_relock();
				else
					r5_admin_prompt_unlock(BMessenger(this));
				_RefreshLock();
				break;

			case kMsgAdminLockChanged:
			case kMsgLockRefresh:
				_RefreshLock();
				break;

			default:
				BWindow::MessageReceived(message);
				break;
		}
	}

private:
	void _RefreshLock()
	{
		bool administrator = false;
		bool unlocked = false;
		r5_admin_query(administrator, unlocked);
		fAdministrator = administrator;
		fUnlocked = administrator || unlocked;

		fView->SetRadioSwitchAllowed(fUnlocked);
		// An administrator starts unlocked and needs no lock (as in
		// Settings).
		if (fAdministrator) {
			if (!fLock->IsHidden(fLock))
				fLock->Hide();
		} else {
			if (fLock->IsHidden(fLock))
				fLock->Show();
			r5_button_set_lock(fLock, !fUnlocked,
				fLock->Frame().IsValid() ? fLock->Frame().Height()
					: fLock->PreferredSize().Height());
		}
	}

	BluetoothView*		fView;
	BButton*			fLock;
	BMessageRunner*		fRunner;
	bool				fAdministrator;
	bool				fUnlocked;
};


class BluetoothApplication : public BApplication {
public:
	BluetoothApplication()
		:
		BApplication(BLUETOOTH_APP_SIGNATURE)
	{
	}

	virtual void ReadyToRun()
	{
		(new BluetoothWindow())->Show();
	}
};


int
main()
{
	BluetoothApplication application;
	application.Run();
	return 0;
}
