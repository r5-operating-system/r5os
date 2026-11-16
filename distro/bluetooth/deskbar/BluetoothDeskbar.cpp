/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * The Bluetooth item in Deskbar (docs/BLUETOOTH.md, step 3b). This small
 * add-on is what Deskbar loads; it talks to bluetooth_server only through
 * BMessenger and BMessage, never by loading the server into Deskbar. Its
 * requests are asynchronous: the answers come back to the view, so Deskbar
 * never waits on the server.
 *
 * The program of the same file (main()) runs once in each session (a user
 * launch job, after Deskbar) and adds the item.
 */


#include <Application.h>
#include <Archivable.h>
#include <Catalog.h>
#include <Deskbar.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <String.h>
#include <View.h>

#include <Bitmap.h>

#include <algorithm>
#include <image.h>
#include <math.h>
#include <string.h>
#include <syslog.h>

#include <AdminLock.h>
#include <R5Icons.h>

#include <bluetoothserver_p.h>

#include <R5Admin.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "BluetoothDeskbar"


static const char* kItemName = "bluetooth_deskbar";
// The item bluetooth_server added before step 3b (DeskbarReplicant.cpp).
static const char* kOldItemName = "BluetoothServerReplicant";
static const char* kAddOnSignature = "application/x-vnd.R5OS-bluetooth-deskbar";

static const uint32 kMsgPoll = 'btdP';
static const uint32 kMsgToggle = 'btdT';
static const uint32 kMsgOpen = 'btdO';

static const bigtime_t kPollInterval = 5000000;


// A connected device with its battery (step 7b: headsets over HFP; LE
// devices with the Battery Service): the level glyph, then the name and
// the percentage. Nothing happens on a click.
class BatteryItem : public BMenuItem {
public:
	BatteryItem(const char* name, int32 percent)
		:
		BMenuItem("", NULL),
		fPercent(percent)
	{
		BString label;
		label.SetToFormat(B_TRANSLATE("%s: battery %d%%"), name,
			(int)percent);
		SetLabel(label.String());
	}

protected:
	virtual void GetContentSize(float* _width, float* _height)
	{
		BMenuItem::GetContentSize(_width, _height);
		*_width += _GlyphWidth() + _Gap();
	}

	virtual void DrawContent()
	{
		BMenu* menu = Menu();
		BPoint where = ContentLocation();
		font_height height;
		menu->GetFontHeight(&height);
		float lineHeight = ceilf(height.ascent + height.descent);
		float glyphHeight = floorf(lineHeight * 0.6f);
		float top = where.y + floorf((lineHeight - glyphHeight) / 2);
		BRect glyph(where.x, top, where.x + _GlyphWidth() - 1,
			top + glyphHeight - 1);
		int32 level = (fPercent + 12) / 25;
		r5_level_glyph_draw(menu, R5_LEVEL_BATTERY, glyph, level,
			ui_color(B_MENU_ITEM_TEXT_COLOR),
			ui_color(B_MENU_BACKGROUND_COLOR), R5_LEVEL_STYLE_NORMAL);
		menu->MovePenTo(where.x + _GlyphWidth() + _Gap(),
			where.y + ceilf(height.ascent));
		menu->DrawString(Label());
	}

private:
	float _GlyphWidth()
	{
		font_height height;
		Menu()->GetFontHeight(&height);
		return ceilf((height.ascent + height.descent) * 1.1f);
	}

	float _Gap()
	{
		return ceilf(be_plain_font->Size() * 0.5f);
	}

	int32	fPercent;
};


// The switch on bluetooth_server's admin port (libr5admin.so), on its own
// thread so Deskbar does not wait. data: 1 for on.
static status_t
set_radio(void* data)
{
	BMessage request(BT_MSG_R5_SET_RADIO);
	request.AddBool("enabled", data != NULL);
	BMessage reply;
	r5_admin_send(BMessenger(BLUETOOTH_SIGNATURE), &request, &reply, 2000000);
	return B_OK;
}


static status_t
our_image(image_info& image)
{
	int32 cookie = 0;
	while (get_next_image_info(B_CURRENT_TEAM, &cookie, &image) == B_OK) {
		if ((char*)our_image >= (char*)image.text
			&& (char*)our_image <= (char*)image.text + image.text_size)
			return B_OK;
	}
	return B_ERROR;
}


class BluetoothReplicant : public BView {
public:
	BluetoothReplicant(BRect frame)
		:
		BView(frame, kItemName, B_FOLLOW_NONE,
			B_WILL_DRAW | B_TRANSPARENT_BACKGROUND)
	{
		_Init();
	}

	BluetoothReplicant(BMessage* archive)
		:
		BView(archive)
	{
		_Init();
	}

	virtual ~BluetoothReplicant()
	{
		delete fRunner;
		delete fGlyph;
	}

	static BArchivable* Instantiate(BMessage* archive);

	virtual status_t Archive(BMessage* archive, bool deep = true) const
	{
		status_t status = BView::Archive(archive, deep);
		if (status != B_OK)
			return status;
		// BView::Archive() writes class=BView first (AGENTS.md).
		archive->RemoveName("class");
		status = archive->AddString("class", "BluetoothReplicant");
		if (status == B_OK) {
			archive->RemoveName("add_on");
			status = archive->AddString("add_on", kAddOnSignature);
		}
		return status;
	}

	virtual void AttachedToWindow()
	{
		BView::AttachedToWindow();
		SetViewColor(B_TRANSPARENT_COLOR);
		BMessage poll(kMsgPoll);
		fRunner = new BMessageRunner(BMessenger(this), &poll, kPollInterval);
		_Poll();
	}

	virtual void DetachedFromWindow()
	{
		delete fRunner;
		fRunner = NULL;
		BView::DetachedFromWindow();
	}

	// Glass-ready: the item paints no background of its own, so whatever
	// Deskbar draws behind it shows through, today its colour and with the
	// glass of a later Deskbar the glass. The glyph is a bitmap with its
	// coverage in the alpha channel, drawn with B_ALPHA_COMPOSITE: on a
	// glass view that keeps its alpha (docs/LIBR5.md, "Window effects"); on
	// an ordinary view it is the same as an overlay.
	virtual void Draw(BRect updateRect)
	{
		(void)updateRect;
		rgb_color color = ui_color(B_PANEL_TEXT_COLOR);
		if (!fReached || !fEnabled || fRadios == 0) {
			// Faint: half way to the Deskbar background, as Deskbar's own
			// items do.
			color = mix_color(color, ui_color(B_PANEL_BACKGROUND_COLOR), 160);
		}

		BRect frame = Bounds().InsetByCopy(1, 1);
		float side = floorf(std::min(frame.Width(), frame.Height())) + 1;
		if (fGlyph == NULL || fGlyphColor != color || fGlyphSide != side) {
			delete fGlyph;
			fGlyph = r5_glyph_bitmap(R5_GLYPH_BLUETOOTH, side, color);
			fGlyphColor = color;
			fGlyphSide = side;
		}
		if (fGlyph == NULL)
			return;

		PushState();
		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
		BRect bounds = Bounds();
		DrawBitmap(fGlyph, BPoint(
			floorf(bounds.left + (bounds.Width() + 1 - side) / 2),
			floorf(bounds.top + (bounds.Height() + 1 - side) / 2)));
		PopState();
	}

	virtual void MessageReceived(BMessage* message)
	{
		switch (message->what) {
			case kMsgPoll:
				_Poll();
				break;

			case kMsgToggle:
			{
				// On the server's admin port, where it checks the sender the
				// kernel names (an administrator, or a grant). Its own
				// thread: Deskbar does not wait; the next poll shows the
				// new state.
				thread_id thread = spawn_thread(set_radio, "bluetooth switch",
					B_NORMAL_PRIORITY, (void*)(addr_t)(fEnabled ? 0 : 1));
				if (thread >= 0)
					resume_thread(thread);
				break;
			}

			case kMsgOpen:
				be_roster->Launch(BLUETOOTH_APP_SIGNATURE);
				break;

			case B_NO_REPLY:
				_SetState(false, fEnabled, fRadios);
				break;

			default:
				// bluetooth_server's answers have no code of their own;
				// only the radio answer has "enabled".
				if (message->what == 0 && !message->HasBool("enabled")) {
					fPaired = *message;
					break;
				}
				if (message->what == 0 && message->HasBool("enabled")) {
					int32 radios = 0;
					int32 id;
					while (message->FindInt32("hci_id", radios, &id) == B_OK)
						radios++;
					_SetState(true, message->GetBool("enabled", true), radios);
					break;
				}
				BView::MessageReceived(message);
				break;
		}
	}

	virtual void MouseDown(BPoint where)
	{
		BPopUpMenu* menu = new BPopUpMenu("bluetooth", false, false);
		menu->SetFont(be_plain_font);

		const char* status;
		if (!fReached)
			status = B_TRANSLATE("Bluetooth is not available");
		else if (fRadios == 0)
			status = B_TRANSLATE("No Bluetooth radio");
		else if (fEnabled)
			status = B_TRANSLATE("Bluetooth: On");
		else
			status = B_TRANSLATE("Bluetooth: Off");
		BMenuItem* statusItem = new BMenuItem(status, NULL);
		statusItem->SetEnabled(false);
		menu->AddItem(statusItem);

		// From the last BT_MSG_R5_GET_PAIRED answer (_Poll()).
		bool batteries = false;
		const char* name;
		for (int32 i = 0; fEnabled
				&& fPaired.FindString("name", i, &name) == B_OK; i++) {
			int32 battery = fPaired.GetInt32("battery", i, -1);
			if (!fPaired.GetBool("connected", i, false) || battery < 0)
				continue;
			if (!batteries)
				menu->AddSeparatorItem();
			batteries = true;
			menu->AddItem(new BatteryItem(name, battery));
		}
		if (batteries)
			menu->AddSeparatorItem();

		BMenuItem* toggle = new BMenuItem(fEnabled
			? B_TRANSLATE("Turn Bluetooth off")
			: B_TRANSLATE("Turn Bluetooth on"), new BMessage(kMsgToggle));
		// System-wide: only for an administrator or an open grant, the
		// same rule bluetooth_server applies.
		toggle->SetEnabled(fReached && r5_admin_change_allowed());
		menu->AddItem(toggle);

		menu->AddSeparatorItem();
		menu->AddItem(new BMenuItem(B_TRANSLATE("Bluetooth settings"
			B_UTF8_ELLIPSIS), new BMessage(kMsgOpen)));

		menu->SetTargetForItems(this);
		ConvertToScreen(&where);
		menu->SetAsyncAutoDestruct(true);
		menu->Go(where, true, true, true);
	}

private:
	void _Init()
	{
		fRunner = NULL;
		fReached = false;
		fEnabled = false;
		fRadios = 0;
		fLogged = false;
		fGlyph = NULL;
		fGlyphSide = 0;
		fGlyphColor = make_color(0, 0, 0);
		// An item from an archive gets the flag too.
		SetFlags(Flags() | B_TRANSPARENT_BACKGROUND);
	}

	void _Poll()
	{
		BMessenger server(BLUETOOTH_SIGNATURE);
		if (!server.IsValid()) {
			_SetState(false, fEnabled, fRadios);
			return;
		}
		BMessage request(BT_MSG_R5_GET_RADIO);
		// Asynchronous: the answer (or B_NO_REPLY) comes to MessageReceived.
		if (server.SendMessage(&request, this, 0) != B_OK)
			_SetState(false, fEnabled, fRadios);
		BMessage paired(BT_MSG_R5_GET_PAIRED);
		server.SendMessage(&paired, this, 0);
	}

	void _SetState(bool reached, bool enabled, int32 radios)
	{
		if (reached && !fLogged) {
			// The pass line of docs/BLUETOOTH.md, step 3b.
			syslog(LOG_INFO, "deskbar_bt_replicant: attached via messenger");
			fLogged = true;
		}
		if (reached == fReached && enabled == fEnabled && radios == fRadios)
			return;
		fReached = reached;
		fEnabled = enabled;
		fRadios = radios;
		Invalidate();
	}

	BMessageRunner*	fRunner;
	bool			fReached;
	bool			fEnabled;
	int32			fRadios;
	bool			fLogged;
	BBitmap*		fGlyph;
	rgb_color		fGlyphColor;
	float			fGlyphSide;
	BMessage		fPaired;
};


_EXPORT BArchivable*
BluetoothReplicant::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, "BluetoothReplicant"))
		return NULL;
	return new BluetoothReplicant(archive);
}


extern "C" _EXPORT BView*
instantiate_deskbar_item(float maxWidth, float maxHeight)
{
	(void)maxWidth;
	(void)&BluetoothReplicant::Instantiate;
	return new BluetoothReplicant(BRect(0, 0, maxHeight - 1, maxHeight - 1));
}


static status_t
install_item()
{
	BDeskbar deskbar;
	if (!deskbar.IsRunning())
		return B_ERROR;
	// Older builds let bluetooth_server add itself, so Deskbar loaded the
	// whole server. bluetooth_server runs before Deskbar now and cannot
	// take that item out of a session; this does.
	if (deskbar.HasItem(kOldItemName))
		deskbar.RemoveItem(kOldItemName);
	if (deskbar.HasItem(kItemName))
		return B_OK;

	BPath path;
	status_t status = find_directory(B_SYSTEM_SERVERS_DIRECTORY, &path);
	if (status == B_OK)
		status = path.Append("bluetooth_deskbar");
	BEntry entry;
	if (status == B_OK)
		status = entry.SetTo(path.Path());
	if (status != B_OK || !entry.Exists()) {
		image_info info;
		status = our_image(info);
		if (status == B_OK)
			status = entry.SetTo(info.name);
	}

	entry_ref ref;
	if (status == B_OK)
		status = entry.GetRef(&ref);
	if (status == B_OK)
		status = deskbar.AddItem(&ref);
	if (status != B_OK) {
		syslog(LOG_WARNING, "bluetooth_deskbar: cannot add the item: %s",
			strerror(status));
	}
	return status;
}


class InstallApplication : public BApplication {
public:
	InstallApplication()
		:
		BApplication(kAddOnSignature),
		fTries(0)
	{
	}

	virtual void ReadyToRun()
	{
		SetPulseRate(1000000);
		_TryInstall();
	}

	virtual void Pulse()
	{
		_TryInstall();
	}

private:
	void _TryInstall()
	{
		// Deskbar may still be starting; give it 30 seconds.
		if (install_item() == B_OK || ++fTries >= 30)
			Quit();
	}

	int32	fTries;
};


int
main()
{
	InstallApplication application;
	application.Run();
	return 0;
}
