/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * The Bluetooth page: the BluetoothView from libr5bt.so (docs/BLUETOOTH.md,
 * step 3a). Only the radio switch is system-wide, so the page does not
 * require an administrator as a whole (RequiresAdmin() stays false): the
 * switch stays gray while the lock is closed, and looking for devices works
 * for everyone (docs/SETTINGS.md, "Bluetooth"). A change takes effect at
 * once; there is nothing to put back and no factory setup, so Revert and
 * Defaults stay disabled.
 */


#include "BluetoothModule.h"

#include "SettingsFont.h"
#include "SettingsManager.h"
#include "SettingsMessages.h"
#include "SettingsModule.h"

#include <GroupView.h>
#include <LayoutBuilder.h>

#include <R5Bluetooth.h>


/*!	Holds the BluetoothView. The window sends kMsgRefreshPage here when the
	lock opens or closes (SettingsWindow::_ApplyPageLock()).
*/
class BluetoothPage : public BGroupView {
public:
	BluetoothPage(SettingsContext* context)
		:
		BGroupView("bluetooth page", B_VERTICAL, 0),
		fContext(context),
		fView(new BluetoothView(true))
	{
		BLayoutBuilder::Group<>(this)
			.Add(fView);

		// The box titles and the summary title in the Settings bold face;
		// r5_settings_apply_plain() keeps that face.
		static const char* const kBold[] = {
			"summary title", "radio switch", "devices title"
		};
		for (size_t i = 0; i < sizeof(kBold) / sizeof(kBold[0]); i++) {
			BView* view = fView->FindView(kBold[i]);
			if (view != NULL)
				view->SetFont(r5_settings_bold_font());
		}
		_SyncLock();
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == kMsgRefreshPage) {
			_SyncLock();
			return;
		}
		BGroupView::MessageReceived(message);
	}

private:
	void _SyncLock()
	{
		fView->SetRadioSwitchAllowed(fContext != NULL
			&& fContext->AdminChangeAllowed());
	}

	SettingsContext*	fContext;
	BluetoothView*		fView;
};


class BluetoothModule : public SettingsModule {
public:
	virtual	const char*			Identifier() const { return "bluetooth"; }
	virtual	const char*			Name() const { return "Bluetooth"; }
	virtual	const char*			Category() const { return "devices"; }
	virtual	const char*			Description() const
									{ return "Find Bluetooth devices nearby"; }
	virtual	const char*			Keywords() const
									{ return "bluetooth wireless radio "
										"headphones mouse keyboard"; }
	virtual	bool				CanSave() const { return false; }

	virtual	BView*				CreateView(SettingsContext* context,
									BRect frame)
								{
									(void)frame;
									return new BluetoothPage(context);
								}
	virtual	status_t			LoadSettings(SettingsContext* context)
									{ (void)context; return B_OK; }
	virtual	status_t			SaveSettings(SettingsContext* context)
									{ (void)context; return B_OK; }
	virtual	status_t			RevertSettings(SettingsContext* context)
									{ (void)context; return B_OK; }
};


void
register_bluetooth_page(SettingsManager& manager)
{
	manager.Add(new BluetoothModule());
}
