/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * M11 step 4 test tool: sets the window effects from a shell, through the
 * same <WindowEffects.h> calls the Settings page uses, and opens a window
 * with or without glass for the measurement.
 *
 *   r5-effects-test get
 *   r5-effects-test shadow|frosted|animations on|off
 *   r5-effects-test strength quiet|balanced|glassy
 *   r5-effects-test workspace <index>   (step 4b: the slide)
 *   r5-effects-test glass|plain|full|tick|slide [seconds]
 *
 * glass and plain open the same window with and without set_window_glass(),
 * full a glass window over the whole screen, tick a window below them that
 * draws a moving bar every 16 ms, so there is damage under the glass.
 * slide is a plain window that moves itself across the others every 16 ms,
 * the way a drag moves it (Desktop::MoveWindowBy).
 *
 *   r5-effects-test round [radius] [seconds]
 *
 * round (the window outline, 22 Nov 2026) opens three bordered windows, the
 * kind Deskbar uses, each with set_window_corner_radius(): one whose colour
 * bars reach every edge and a bar that moves (damage in the corners), one
 * with a text view and both scroll bars meeting in the bottom-right corner,
 * and one with glass. The radius is in pixels; without one, 18 scaled with
 * the font size. Radius 0 opens the same windows square, for the compare.
 *
 *   r5-effects-test round-move [radius] [seconds]
 *
 * round-move (step 1.5, 22 Nov 2026) opens one round bordered window with
 * the colour bars that moves itself sideways every 16 ms and grows and
 * shrinks every second, so a move and a resize of a round window can be
 * watched (a bordered window has no tab to drag).
 *
 *   r5-effects-test decorator list|<name>
 *   r5-effects-test soft [seconds]
 *
 * decorator (Default (R5 Modern), 22 Nov 2026) lists the window decorators
 * in the order and with the names Appearance shows, or chooses one by its
 * name or file name, as Appearance does. soft opens the two windows of
 * board 4 "Soft" of the mockup at its places: an ordinary active window
 * with white content ("StyledEdit") and an inactive one that asks for
 * glass ("Sign in"), for the comparison with the mockup.
 */


#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

#include <Application.h>
#include <Button.h>
#include <InterfaceDefs.h>
#include <MessageRunner.h>
#include <Screen.h>
#include <ScrollView.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>
#include <View.h>
#include <Window.h>

#include <WindowEffects.h>

#include <DecorInfo.h>


using namespace BAppearanceKit;


static const uint32 kMsgTick = 'tick';
static const uint32 kMsgSlide = 'slid';


class SlideWindow : public BWindow {
public:
	SlideWindow(BRect frame)
		:
		BWindow(frame, "Slide", B_TITLED_WINDOW, B_QUIT_ON_WINDOW_CLOSE),
		fStep(6),
		fRunner(NULL)
	{
		BMessage slide(kMsgSlide);
		fRunner = new BMessageRunner(BMessenger(this), &slide, 16000);
	}

	virtual ~SlideWindow()
	{
		delete fRunner;
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what != kMsgSlide) {
			BWindow::MessageReceived(message);
			return;
		}
		BRect frame = Frame();
		if (frame.left + fStep < 40 || frame.right + fStep > 900)
			fStep = -fStep;
		MoveBy(fStep, 0);
	}

private:
	float			fStep;
	BMessageRunner*	fRunner;
};


class TextView : public BView {
public:
	TextView(BRect frame, bool composite, bool tick)
		:
		BView(frame, "text", B_FOLLOW_ALL, B_WILL_DRAW),
		fComposite(composite),
		fTick(tick),
		fPosition(0),
		fRunner(NULL)
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	}

	virtual ~TextView()
	{
		delete fRunner;
	}

	virtual void AttachedToWindow()
	{
		if (fTick) {
			BMessage tick(kMsgTick);
			fRunner = new BMessageRunner(BMessenger(this), &tick, 16000);
		}
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == kMsgTick) {
			fPosition = (fPosition + 4) % (int32)Bounds().Width();
			Invalidate();
			return;
		}
		BView::MessageReceived(message);
	}

	virtual void Draw(BRect)
	{
		// Text with alpha over glass: B_OP_ALPHA with B_ALPHA_COMPOSITE
		// writes correct alpha; B_OP_OVER would be opaque.
		if (fComposite) {
			SetDrawingMode(B_OP_ALPHA);
			SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
		}
		SetHighColor(0, 0, 0);
		BFont font;
		GetFont(&font);
		font.SetSize(24);
		SetFont(&font);
		DrawString(fTick ? "R5 tick" : "R5 glass", BPoint(20, 40));
		FillRect(BRect(20, 60, 120, 90));
		if (fTick) {
			SetHighColor(0, 90, 200);
			FillRect(BRect(fPosition, 100, fPosition + 40, Bounds().bottom));
		}
	}

private:
	bool			fComposite;
	bool			fTick;
	int32			fPosition;
	BMessageRunner*	fRunner;
};


/*!	Colour bars that reach every edge of the window, so the cut corners
	show, and a bar that moves through the bottom corners every 16 ms.
*/
class EdgeView : public BView {
public:
	EdgeView(BRect frame)
		:
		BView(frame, "edges", B_FOLLOW_ALL, B_WILL_DRAW),
		fPosition(0),
		fRunner(NULL)
	{
		SetViewColor(B_TRANSPARENT_COLOR);
	}

	virtual ~EdgeView()
	{
		delete fRunner;
	}

	virtual void AttachedToWindow()
	{
		BMessage tick(kMsgTick);
		fRunner = new BMessageRunner(BMessenger(this), &tick, 16000);
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what == kMsgTick) {
			fPosition = (fPosition + 3) % (int32)Bounds().Width();
			Invalidate();
			return;
		}
		BView::MessageReceived(message);
	}

	virtual void Draw(BRect)
	{
		BRect bounds = Bounds();
		const rgb_color colors[] = {
			{ 230, 60, 60, 255 }, { 240, 180, 40, 255 },
			{ 60, 170, 90, 255 }, { 40, 120, 230, 255 }
		};
		float band = ceilf(bounds.Height() / 4);
		for (int32 i = 0; i < 4; i++) {
			SetHighColor(colors[i]);
			// the last band reaches the last row (step 1.5: a row was left
			// unpainted, and the moving bar's trace stayed there)
			float bottom = i == 3 ? bounds.bottom
				: std::min(bounds.bottom, bounds.top + (i + 1) * band - 1);
			FillRect(BRect(bounds.left, bounds.top + i * band, bounds.right,
				bottom));
		}
		SetHighColor(20, 30, 50);
		FillRect(BRect(fPosition, bounds.bottom - 30, fPosition + 50,
			bounds.bottom));
		SetHighColor(255, 255, 255);
		BFont font;
		GetFont(&font);
		font.SetSize(18);
		SetFont(&font);
		DrawString("R5 round", BPoint(bounds.left + 40, bounds.top + 40));
	}

private:
	int32			fPosition;
	BMessageRunner*	fRunner;
};


/*!	A bordered window that moves itself sideways every 16 ms, the way a drag
	moves it, and grows and shrinks every second, the way a resize does.
*/
class MovingWindow : public BWindow {
public:
	MovingWindow(BRect frame)
		:
		BWindow(frame, "Round moving", B_BORDERED_WINDOW_LOOK,
			B_NORMAL_WINDOW_FEEL, B_QUIT_ON_WINDOW_CLOSE),
		fStep(4),
		fTicks(0),
		fRunner(NULL)
	{
		BMessage slide(kMsgSlide);
		fRunner = new BMessageRunner(BMessenger(this), &slide, 16000);
	}

	virtual ~MovingWindow()
	{
		delete fRunner;
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what != kMsgSlide) {
			BWindow::MessageReceived(message);
			return;
		}
		BRect frame = Frame();
		if (frame.left + fStep < 40 || frame.right + fStep > 900)
			fStep = -fStep;
		MoveBy(fStep, 0);
		// every second: 40 x 30 larger, the next second back
		if (++fTicks % 60 == 0) {
			float grow = (fTicks / 60) % 2 == 1 ? 1 : -1;
			ResizeBy(40 * grow, 30 * grow);
		}
	}

private:
	float			fStep;
	int32			fTicks;
	BMessageRunner*	fRunner;
};


class TestApp : public BApplication {
public:
	TestApp(int argc, char** argv)
		:
		BApplication("application/x-vnd.R5OS-effects-test"),
		fArgc(argc),
		fArgv(argv),
		fResult(0)
	{
	}

	virtual void ReadyToRun()
	{
		fResult = _Run();
		if (fWindowSeconds <= 0)
			PostMessage(B_QUIT_REQUESTED);
	}

	int Result() const { return fResult; }

private:
	int _Run()
	{
		fWindowSeconds = 0;
		if (fArgc < 2)
			return _Usage();
		const char* what = fArgv[1];
		const char* value = fArgc > 2 ? fArgv[2] : NULL;

		if (strcmp(what, "get") == 0) {
			uint32 effects = window_effects();
			const char* names[] = { "", "quiet", "balanced", "glassy" };
			printf("shadow %s frosted %s animations %s strength %s\n",
				(effects & B_WINDOW_SHADOW) != 0 ? "on" : "off",
				(effects & B_WINDOW_FROSTED_GLASS) != 0 ? "on" : "off",
				(effects & B_WINDOW_ANIMATIONS) != 0 ? "on" : "off",
				names[window_effect_strength()]);
			return 0;
		}
		if (strcmp(what, "workspace") == 0 && value != NULL) {
			int32 index = atoi(value);
			if (count_workspaces() <= index)
				set_workspace_count(index + 1);
			activate_workspace(index);
			return 0;
		}
		if (strcmp(what, "strength") == 0 && value != NULL) {
			enum window_effect_strength strength = B_WINDOW_EFFECTS_BALANCED;
			if (strcmp(value, "quiet") == 0)
				strength = B_WINDOW_EFFECTS_QUIET;
			else if (strcmp(value, "glassy") == 0)
				strength = B_WINDOW_EFFECTS_GLASSY;
			else if (strcmp(value, "balanced") != 0)
				return _Usage();
			return _Check(set_window_effect_strength(strength));
		}
		uint32 flag = 0;
		if (strcmp(what, "shadow") == 0)
			flag = B_WINDOW_SHADOW;
		else if (strcmp(what, "frosted") == 0)
			flag = B_WINDOW_FROSTED_GLASS;
		else if (strcmp(what, "animations") == 0)
			flag = B_WINDOW_ANIMATIONS;
		if (flag != 0 && value != NULL) {
			uint32 effects = window_effects() & ~flag;
			if (strcmp(value, "on") == 0)
				effects |= flag;
			else if (strcmp(value, "off") != 0)
				return _Usage();
			return _Check(set_window_effects(effects));
		}

		if (strcmp(what, "decorator") == 0 && value != NULL)
			return _Decorator(value);
		if (strcmp(what, "soft") == 0)
			return _Soft(value);
		if (strcmp(what, "round") == 0)
			return _Round(value, fArgc > 3 ? fArgv[3] : NULL);
		if (strcmp(what, "round-move") == 0)
			return _RoundMove(value, fArgc > 3 ? fArgv[3] : NULL);

		bool glass = strcmp(what, "glass") == 0;
		bool full = strcmp(what, "full") == 0;
		bool tick = strcmp(what, "tick") == 0;
		bool slide = strcmp(what, "slide") == 0;
		if (!glass && !full && !tick && !slide && strcmp(what, "plain") != 0)
			return _Usage();
		fWindowSeconds = value != NULL ? atoi(value) : 30;
		if (fWindowSeconds <= 0)
			fWindowSeconds = 30;
		BRect frame(120, 120, 520, 400);
		// the glass window lies over the tick window and the desktop
		if (glass)
			frame.Set(600, 140, 1000, 420);
		if (tick)
			frame.Set(260, 220, 760, 560);
		if (slide) {
			BWindow* window = new SlideWindow(BRect(60, 180, 300, 360));
			window->AddChild(new TextView(window->Bounds(), false, false));
			window->Show();
			SetPulseRate(fWindowSeconds * 1000000LL);
			return 0;
		}
		window_look look = B_TITLED_WINDOW_LOOK;
		if (full) {
			frame = BScreen().Frame();
			look = B_NO_BORDER_WINDOW_LOOK;
		}
		BWindow* window = new BWindow(frame, full ? "Full glass"
				: glass ? "Glass" : tick ? "Tick" : "Plain", look,
			B_NORMAL_WINDOW_FEEL,
			B_QUIT_ON_WINDOW_CLOSE | B_ASYNCHRONOUS_CONTROLS);
		TextView* view = new TextView(window->Bounds(), glass || full, tick);
		window->AddChild(view);
		window->Show();
		int result = 0;
		if (glass || full) {
			// The window's top view, and the view that fills it: a child
			// with its own colour stays opaque otherwise. M11 review: a
			// refused call is the exit code, not only a printed line.
			result = _Check(set_window_glass(window, true));
			if (window->Lock()) {
				if (_Check(set_view_glass(view, true)) != 0)
					result = 1;
				window->Unlock();
			}
		}
		SetPulseRate(fWindowSeconds * 1000000LL);
		return result;
	}

	int _Decorator(const char* name)
	{
		BPrivate::DecorInfoUtility utility;
		if (strcmp(name, "list") != 0) {
			BPrivate::DecorInfo* decor = utility.FindDecorator(name);
			if (decor == NULL) {
				fprintf(stderr, "r5-effects-test: no decorator %s\n", name);
				return 1;
			}
			int result = _Check(utility.SetDecorator(decor));
			printf("r5-effects-test: decorator %s\n",
				decor->Name().String());
			return result;
		}
		// the order of Appearance: the add-ons by name, then the built-in
		BPrivate::DecorInfo* current = utility.CurrentDecorator();
		int32 count = utility.CountDecorators();
		for (int32 pass = 0; pass < 2; pass++) {
			BObjectList<BPrivate::DecorInfo> list(20);
			for (int32 i = 0; i < count; i++) {
				BPrivate::DecorInfo* decor = utility.DecoratorAt(i);
				if (decor == NULL || decor->IsDefault() != (pass == 1))
					continue;
				int32 index = 0;
				while (index < list.CountItems()
					&& strcmp(list.ItemAt(index)->Name().String(),
						decor->Name().String()) <= 0)
					index++;
				list.AddItem(decor, index);
			}
			for (int32 i = 0; i < list.CountItems(); i++) {
				BPrivate::DecorInfo* decor = list.ItemAt(i);
				printf("decorator: %s%s (%s)\n", decor->Name().String(),
					current != NULL && current->Path() == decor->Path()
						? " [current]" : "", decor->Path().String());
			}
		}
		return 0;
	}

	int _Soft(const char* secondsArg)
	{
		fWindowSeconds = secondsArg != NULL ? atoi(secondsArg) : 120;
		if (fWindowSeconds <= 0)
			fWindowSeconds = 120;
		uint32 flags = B_QUIT_ON_WINDOW_CLOSE | B_ASYNCHRONOUS_CONTROLS;
		int result = 0;

		// Board 4: the outer edge of a window at (60, 58) and (700, 148),
		// 560 x (46 + 330 - 14) and 500 x (46 + 300 - 14); the content is
		// inside the 1 px line, under the 32 px of the tab.
		BWindow* glass = new BWindow(BRect(701, 181, 1198, 478), "Sign in",
			B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL, flags);
		BView* panel = new BView(glass->Bounds(), "panel", B_FOLLOW_ALL,
			B_WILL_DRAW);
		panel->SetViewColor(232, 238, 247);
		glass->AddChild(panel);
		BStringView* label = new BStringView(BRect(40, 26, 440, 44),
			"label", "Password");
		panel->AddChild(label);
		BTextControl* field = new BTextControl(BRect(40, 50, 457, 80),
			"password", NULL, "", NULL);
		field->TextView()->HideTyping(true);
		panel->AddChild(field);
		BButton* button = new BButton(BRect(40, 96, 457, 130), "signin",
			"Sign in", NULL);
		panel->AddChild(button);
		BStringView* note = new BStringView(BRect(40, 150, 457, 168), "note",
			"The glass body is only for a program that asks for it.");
		note->SetAlignment(B_ALIGN_CENTER);
		panel->AddChild(note);
		glass->Show();
		if (_Check(set_window_glass(glass, true)) != 0)
			result = 1;
		if (glass->Lock()) {
			if (_Check(set_view_glass(panel, true)) != 0)
				result = 1;
			glass->Unlock();
		}

		BWindow* plain = new BWindow(BRect(61, 91, 618, 418), "StyledEdit",
			B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL, flags);
		BRect textRect = plain->Bounds().InsetByCopy(20, 16);
		textRect.OffsetTo(20, 16);
		BTextView* text = new BTextView(plain->Bounds(), "text",
			textRect, B_FOLLOW_ALL, B_WILL_DRAW);
		text->SetViewColor(255, 255, 255);
		text->SetText("Same soul. Fresh pixels.\n\n"
			"The tab is a soft yellow card with large round corners. It sits"
			" behind the window, which overlaps its lower edge.\n\n"
			"Haiku's own close and zoom buttons: close on the left, zoom on"
			" the right of the tab. The window is resized by its edge.");
		text->MakeEditable(false);
		plain->AddChild(text);
		plain->Show();
		plain->Activate();

		printf("r5-effects-test: soft, %" B_PRId32 " s\n", fWindowSeconds);
		SetPulseRate(fWindowSeconds * 1000000LL);
		return result;
	}

	int _RoundMove(const char* radiusArg, const char* secondsArg)
	{
		float radius = roundf(18.0f * be_plain_font->Size() / 12.0f);
		if (radiusArg != NULL)
			radius = atof(radiusArg);
		fWindowSeconds = secondsArg != NULL ? atoi(secondsArg) : 60;
		if (fWindowSeconds <= 0)
			fWindowSeconds = 60;
		BWindow* window = new MovingWindow(BRect(80, 200, 400, 420));
		window->AddChild(new EdgeView(window->Bounds()));
		window->Show();
		int result = _Check(set_window_corner_radius(window, radius));
		printf("r5-effects-test: round-move, radius %g, %" B_PRId32 " s\n",
			radius, fWindowSeconds);
		SetPulseRate(fWindowSeconds * 1000000LL);
		return result;
	}

	int _Round(const char* radiusArg, const char* secondsArg)
	{
		float radius = roundf(18.0f * be_plain_font->Size() / 12.0f);
		if (radiusArg != NULL)
			radius = atof(radiusArg);
		fWindowSeconds = secondsArg != NULL ? atoi(secondsArg) : 60;
		if (fWindowSeconds <= 0)
			fWindowSeconds = 60;
		uint32 flags = B_QUIT_ON_WINDOW_CLOSE | B_ASYNCHRONOUS_CONTROLS;
		int result = 0;

		BWindow* edges = new BWindow(BRect(80, 100, 460, 380), "Round edges",
			B_BORDERED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL, flags);
		edges->AddChild(new EdgeView(edges->Bounds()));
		edges->Show();
		if (_Check(set_window_corner_radius(edges, radius)) != 0)
			result = 1;

		BWindow* text = new BWindow(BRect(520, 100, 900, 380), "Round text",
			B_BORDERED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL, flags);
		BRect inner = text->Bounds();
		inner.right -= B_V_SCROLL_BAR_WIDTH;
		inner.bottom -= B_H_SCROLL_BAR_HEIGHT;
		BRect textRect = inner.OffsetToCopy(B_ORIGIN);
		textRect.InsetBy(4, 4);
		BTextView* textView = new BTextView(inner, "text", textRect,
			B_FOLLOW_ALL, B_WILL_DRAW);
		BString lines;
		for (int32 i = 1; i <= 60; i++) {
			lines << "Line " << i << ": the corners of this window are"
				" round, and so is its content.\n";
		}
		textView->SetText(lines.String());
		textView->SetWordWrap(false);
		text->AddChild(new BScrollView("scroll", textView, B_FOLLOW_ALL, 0,
			true, true, B_NO_BORDER));
		text->Show();
		if (_Check(set_window_corner_radius(text, radius)) != 0)
			result = 1;

		BWindow* glass = new BWindow(BRect(300, 300, 680, 560), "Round glass",
			B_BORDERED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL, flags);
		TextView* glassView = new TextView(glass->Bounds(), true, false);
		glass->AddChild(glassView);
		glass->Show();
		if (_Check(set_window_glass(glass, true)) != 0)
			result = 1;
		if (glass->Lock()) {
			if (_Check(set_view_glass(glassView, true)) != 0)
				result = 1;
			glass->Unlock();
		}
		if (_Check(set_window_corner_radius(glass, radius)) != 0)
			result = 1;

		printf("r5-effects-test: round, radius %g, %" B_PRId32 " s\n", radius,
			fWindowSeconds);
		SetPulseRate(fWindowSeconds * 1000000LL);
		return result;
	}

	virtual void Pulse()
	{
		PostMessage(B_QUIT_REQUESTED);
	}

	int _Check(status_t status)
	{
		if (status != B_OK) {
			fprintf(stderr, "r5-effects-test: %s\n", strerror(status));
			return 1;
		}
		return 0;
	}

	int _Usage()
	{
		fprintf(stderr, "usage: r5-effects-test get\n"
			"       r5-effects-test shadow|frosted|animations on|off\n"
			"       r5-effects-test strength quiet|balanced|glassy\n"
			"       r5-effects-test workspace <index>\n"
			"       r5-effects-test glass|plain|full|tick|slide [seconds]\n"
			"       r5-effects-test round [radius] [seconds]\n"
			"       r5-effects-test round-move [radius] [seconds]\n"
			"       r5-effects-test decorator list|<name>\n"
			"       r5-effects-test soft [seconds]\n");
		return 2;
	}

	int		fArgc;
	char**	fArgv;
	int		fResult;
	int32	fWindowSeconds;
};


int
main(int argc, char** argv)
{
	TestApp app(argc, argv);
	app.Run();
	return app.Result();
}
