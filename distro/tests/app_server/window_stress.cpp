// SPDX-License-Identifier: MIT
// Copyright 2026 R5OS Contributors
//
// Opens and closes many windows quickly, to look for app_server crashes
// (GitHub #3933 double free with many windows, #3409 Desktop::SetFocusWindow).
//
// Several threads each create windows, show them, move the focus between
// them, hide and show them, and quit them again, some right after Show() and
// some after a short draw. Runs for the given number
// of seconds and prints how many windows it opened. app_server crashing shows
// as this program losing its connection (it then stops with a message).
//
// Build (scripts/build-stability-tests.sh):
//   x86_64-unknown-haiku-g++ -o window_stress window_stress.cpp -lbe
// Run: ./window_stress [seconds] [threads]

#include <Application.h>
#include <OS.h>
#include <View.h>
#include <Window.h>

#include <stdio.h>
#include <stdlib.h>


static int32 sOpened = 0;
static bigtime_t sEnd = 0;


class StressView : public BView {
public:
	StressView(BRect frame, rgb_color color)
		:
		BView(frame, "stress", B_FOLLOW_ALL, B_WILL_DRAW),
		fColor(color)
	{
	}

	void Draw(BRect)
	{
		SetHighColor(fColor);
		FillRect(Bounds());
		SetHighColor(0, 0, 0);
		StrokeLine(Bounds().LeftTop(), Bounds().RightBottom());
	}

private:
	rgb_color	fColor;
};


static BWindow*
new_window(uint32 seed)
{
	float x = 20 + (seed * 37) % 900;
	float y = 40 + (seed * 53) % 600;
	BRect frame(x, y, x + 120 + seed % 200, y + 80 + seed % 150);
	window_look look = (seed % 5 == 0) ? B_FLOATING_WINDOW_LOOK
		: B_TITLED_WINDOW_LOOK;
	window_feel feel = (seed % 7 == 0) ? B_FLOATING_APP_WINDOW_FEEL
		: B_NORMAL_WINDOW_FEEL;
	BWindow* window = new BWindow(frame, "stress", look, feel,
		B_ASYNCHRONOUS_CONTROLS);
	rgb_color color = { (uint8)(seed * 13), (uint8)(seed * 29),
		(uint8)(seed * 71), 255 };
	window->AddChild(new StressView(window->Bounds(), color));
	return window;
}


static status_t
stress_thread(void* data)
{
	uint32 seed = (uint32)(addr_t)data * 1000 + 1;
	BWindow* windows[8] = {};

	while (system_time() < sEnd) {
		int slot = seed % 8;
		if (windows[slot] != NULL) {
			if (windows[slot]->Lock())
				windows[slot]->Quit();
			windows[slot] = NULL;
		}

		BWindow* window = new_window(seed);
		window->Show();
		atomic_add(&sOpened, 1);

		switch (seed % 4) {
			case 0:
				// close at once, while app_server still sets it up
				if (window->Lock())
					window->Quit();
				break;
			case 1:
				// take the focus, then close
				if (window->Lock()) {
					window->Activate(true);
					window->Unlock();
				}
				if (window->Lock())
					window->Quit();
				break;
			case 2:
				// keep it a while; move focus to another kept window
				windows[slot] = window;
				if (BWindow* other = windows[(slot + 3) % 8]) {
					if (other->Lock()) {
						other->Activate(true);
						other->Unlock();
					}
				}
				break;
			default:
				// keep it, hide and show it again
				windows[slot] = window;
				if (window->Lock()) {
					window->Hide();
					window->Show();
					window->Activate(true);
					window->Unlock();
				}
				break;
		}

		seed = seed * 1103515245 + 12345;
		if (seed % 16 == 0)
			snooze(2000);
	}

	for (int i = 0; i < 8; i++) {
		if (windows[i] != NULL && windows[i]->Lock())
			windows[i]->Quit();
	}
	return B_OK;
}


class StressApp : public BApplication {
public:
	StressApp(int seconds, int threads)
		:
		BApplication("application/x-vnd.R5-window-stress"),
		fSeconds(seconds),
		fThreads(threads)
	{
	}

	void ReadyToRun()
	{
		resume_thread(spawn_thread(&_Run, "stress control", B_NORMAL_PRIORITY,
			this));
	}

private:
	static status_t _Run(void* data)
	{
		StressApp* app = (StressApp*)data;
		bigtime_t start = system_time();
		sEnd = start + app->fSeconds * 1000000LL;

		thread_id threads[64];
		int count = app->fThreads < 64 ? app->fThreads : 64;
		for (int i = 0; i < count; i++) {
			threads[i] = spawn_thread(stress_thread, "stress", B_NORMAL_PRIORITY,
				(void*)(addr_t)(i + 1));
			resume_thread(threads[i]);
		}
		while (system_time() < sEnd) {
			snooze(10000000);
			printf("window_stress: %" B_PRId32 " windows after %.0f s\n",
				atomic_get(&sOpened), (system_time() - start) / 1000000.0);
			fflush(stdout);
		}
		for (int i = 0; i < count; i++) {
			status_t result;
			wait_for_thread(threads[i], &result);
		}

		printf("PASS window_stress: %" B_PRId32 " windows in %.1f s with %d "
			"threads\n", atomic_get(&sOpened),
			(system_time() - start) / 1000000.0, count);
		fflush(stdout);
		be_app->PostMessage(B_QUIT_REQUESTED);
		return B_OK;
	}

	int		fSeconds;
	int		fThreads;
};


int
main(int argc, char** argv)
{
	int seconds = argc > 1 ? atoi(argv[1]) : 300;
	int threads = argc > 2 ? atoi(argv[2]) : 4;
	StressApp app(seconds, threads);
	app.Run();
	return 0;
}
