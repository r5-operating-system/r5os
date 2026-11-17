/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * BGameControllerView: a picture of a game controller, drawn in code
 * (vector, scaled to the view, in the system colors, so dark mode works),
 * in which the parts light up while they are pressed. libr5game.so,
 * docs/LIBR5.md "Game Kit".
 */
#ifndef _GAME_CONTROLLER_VIEW_H
#define _GAME_CONTROLLER_VIEW_H


#include <View.h>

#include <GameController.h>


class BMessageRunner;


class BGameControllerView : public BView {
public:
								BGameControllerView(const char* name);
	virtual						~BGameControllerView();

	// Opens the controller ("usb/0") and shows it live while the view is
	// in a window; NULL closes it. The view reads it about 60 times a
	// second and draws only when something changed.
			status_t			SetDevice(const char* deviceName);
			BGameController*	Controller();
									// the open controller, or NULL

	// Without a device: the still shape of a family. UNKNOWN is the
	// picture a controller without a mapping gets.
			void				SetFamily(game_controller_family family);
			game_controller_family Family() const;

	// The labels on the face buttons. AUTOMATIC: the controller's own
	// choice (BGameController::LabelStyle()).
			void				SetLabels(game_controller_labels labels);

	virtual	void				AttachedToWindow();
	virtual	void				DetachedFromWindow();
	virtual	void				Draw(BRect updateRect);
	virtual	void				FrameResized(float width, float height);
	virtual	void				MessageReceived(BMessage* message);
	virtual	BSize				MinSize();
	virtual	BSize				PreferredSize();

	// Added 17 Nov 2026 (docs/LIBR5.md, "Game Kit"): a tick on a trigger
	// cap where a trigger effect starts, 0 (released) to 1 (pulled all the
	// way); below 0 removes it. Only drawn on a controller with analog
	// triggers.
			void				SetTriggerMark(game_controller_trigger trigger,
									float position);

private:
			struct Private;

			void				_StartPolling();
			void				_StopPolling();
			bool				_Poll();

	virtual	void				_ReservedGameControllerView1();
	virtual	void				_ReservedGameControllerView2();
	virtual	void				_ReservedGameControllerView3();
	virtual	void				_ReservedGameControllerView4();

								BGameControllerView(
									const BGameControllerView&);
			BGameControllerView& operator=(const BGameControllerView&);

			Private*			fPrivate;
			uint32				_reserved[4];
};


#endif	// _GAME_CONTROLLER_VIEW_H
