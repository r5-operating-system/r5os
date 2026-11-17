/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * gamecontroller_feel_test: rumble, trigger effects and motion
 * (docs/GAME-CONTROLLERS.md, option C phase 1) without a real controller.
 * As uid 0, with the bluetooth_hid driver (regular image):
 *
 *  1. a virtual DualSense (054c:0ce6) with report IDs: the short report
 *     0x01 with buttons and axes, and the full report 0x31 in and out as
 *     vendor bytes, as the real one declares them;
 *  2. the driver calls on its joystick node: R5_JOYSTICK_GET_DESCRIPTOR
 *     gives the descriptor back, R5_JOYSTICK_GET_REPORT the last full
 *     report fed, R5_JOYSTICK_SEND_REPORT refuses a report the descriptor
 *     does not declare, one longer than declared, a feature report (not
 *     over Bluetooth yet) and kernel addresses (SMAP rule);
 *  3. only full reports are fed: BGameController must still see Cross and
 *     the left stick (the driver builds the short report from them), and
 *     the motion of the sensors in them;
 *  4. SetRumble() and SetTriggerEffect() must reach the driver's output
 *     queue as the exact 78-byte Bluetooth report with its CRC-32; a timed
 *     rumble must stop by itself; Close() frees the triggers;
 *  5. with app_server: BTiltLevel, BAxisBar and BGameControllerMotionView
 *     draw something.
 *
 * bluetooth_server also takes reports from the output queue, so this test
 * starts waiting before it sends, and sends again (at most four times).
 * Prints "gamecontroller_feel_test: ok" or the first failure.
 */


#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <Application.h>
#include <AxisBar.h>
#include <Bitmap.h>
#include <OS.h>
#include <String.h>
#include <TiltLevel.h>
#include <View.h>

#include <GameController.h>
#include <GameControllerMotionView.h>
#include <R5BluetoothHID.h>
#include <joystick_driver.h>

#include "GameControllerReports.h"


using namespace R5GameController;


static int
fail(const char* what)
{
	printf("gamecontroller_feel_test: FAIL %s (%s)\n", what,
		strerror(errno));
	return 1;
}


#define CHECK(condition, what) \
	do { if (!(condition)) return fail(what); } while (false)


// Report 0x01: X Y Z Rz, a hat, 14 buttons, 6 bits of padding, Rx Ry (the
// DualSense's short report). Report 0x31: 77 vendor bytes in, 77 out.
static const uint8 kDualSense[] = {
	0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
	0x85, 0x01,
	0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35,
	0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x04, 0x81, 0x02,
	0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x35, 0x00, 0x46, 0x3B, 0x01,
	0x65, 0x14, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42, 0x65, 0x00,
	0x05, 0x09, 0x19, 0x01, 0x29, 0x0E, 0x15, 0x00, 0x25, 0x01,
	0x75, 0x01, 0x95, 0x0E, 0x81, 0x02,
	0x75, 0x06, 0x95, 0x01, 0x81, 0x03,
	0x05, 0x01, 0x09, 0x33, 0x09, 0x34,
	0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
	0x06, 0x00, 0xFF,
	0x85, 0x31, 0x09, 0x31, 0x15, 0x00, 0x26, 0xFF, 0x00,
	0x75, 0x08, 0x95, 0x4D, 0x81, 0x02,
	0x09, 0x32, 0x95, 0x4D, 0x91, 0x02,
	0xC0
};

static const char* const kName = "DualSense feel test";


static int sControl = -1;
static int32 sPadID = -1;
static volatile bool sFeeding = true;
static uint8 sFull[78];


static void
put16(uint8* at, int16 value)
{
	at[0] = (uint8)value;
	at[1] = (uint8)((uint16)value >> 8);
}


// The full report: left stick up and left, Cross down, the hat centred,
// L2 half; gyroscope 16, -32, 1600 (1, -2, 100 degrees per second) and
// accelerometer 0, 8192, -4096 (0, 1, -0.5 g).
static void
make_full_report()
{
	memset(sFull, 0, sizeof(sFull));
	sFull[0] = 0x31;
	uint8* state = sFull + 2;
	state[0] = 0x10;
	state[1] = 0x10;
	state[2] = 0x80;
	state[3] = 0x80;
	state[4] = 0x80;
	state[7] = 0x08 | 0x20;
	put16(state + 15, 16);
	put16(state + 17, -32);
	put16(state + 19, 1600);
	put16(state + 21, 0);
	put16(state + 23, 8192);
	put16(state + 25, -4096);
}


static status_t
feed_thread(void*)
{
	while (sFeeding) {
		r5_bthid_input input = { sPadID, sizeof(sFull), sFull };
		ioctl(sControl, R5_BTHID_INPUT, &input, sizeof(input));
		snooze(20000);
	}
	return B_OK;
}


// Waits for an output report of the pad that check() accepts, skipping
// others, for up to timeout.
static bool
read_output(bool (*check)(const uint8*, uint32), bigtime_t timeout)
{
	bigtime_t end = system_time() + timeout;
	while (system_time() < end) {
		uint8 data[R5_BTHID_MAX_REPORT];
		r5_bthid_output output;
		memset(&output, 0, sizeof(output));
		// One long wait: the waiter that came first gets the report.
		output.timeout = end - system_time();
		if (output.timeout <= 0)
			break;
		output.data = data;
		output.capacity = sizeof(data);
		if (ioctl(sControl, R5_BTHID_GET_OUTPUT, &output, sizeof(output))
				!= 0)
			continue;
		if (output.id == sPadID && check(data, output.length))
			return true;
	}
	return false;
}


static bool
valid_report(const uint8* data, uint32 length)
{
	if (length != 78 || data[0] != 0x31 || data[2] != 0x10)
		return false;
	uint8 header = 0xa2;
	uint32 crc = crc32(crc32(0, &header, 1), data, 74);
	return data[74] == (uint8)crc && data[75] == (uint8)(crc >> 8)
		&& data[76] == (uint8)(crc >> 16) && data[77] == (uint8)(crc >> 24);
}


static bool
rumble_report(const uint8* data, uint32 length)
{
	// Both triggers and the motors valid, weak 128, strong 255.
	return valid_report(data, length) && data[3] == 0x0e && data[5] == 128
		&& data[6] == 255 && data[41] == 0x04;
}


static bool
stopped_report(const uint8* data, uint32 length)
{
	return valid_report(data, length) && data[5] == 0 && data[6] == 0;
}


static bool
trigger_report(const uint8* data, uint32 length)
{
	// The right trigger: resistance from 40 %, strength 70 %.
	return valid_report(data, length) && data[13] == 0x01
		&& data[14] == 102 && data[15] == 179 && data[24] == 0x05;
}


static bool
freed_report(const uint8* data, uint32 length)
{
	return valid_report(data, length) && data[13] == 0x05
		&& data[24] == 0x05;
}


static bool
any_report(const uint8* data, uint32 length)
{
	return valid_report(data, length);
}


static bool
find_pad(BString& device)
{
	for (int i = 0; i < 40; i++) {
		BGameController controller;
		for (int32 j = 0; j < controller.CountDevices(); j++) {
			char name[B_OS_NAME_LENGTH];
			if (controller.GetDeviceName(j, name, sizeof(name)) != B_OK
				|| strncmp(name, "bluetooth/", 10) != 0
				|| controller.Open(name) != B_OK)
				continue;
			if (strcmp(controller.Name(), kName) == 0) {
				device = name;
				return true;
			}
		}
		snooze(50000);
	}
	return false;
}


static int
test_driver(const char* device)
{
	BString path("/dev/joystick/");
	path << device;
	int fd = open(path.String(), O_RDONLY);
	CHECK(fd >= 0, "open the joystick node");

	// The descriptor, whole and too small.
	uint8 descriptor[R5_JOYSTICK_MAX_DESCRIPTOR];
	r5_joystick_descriptor request;
	memset(&request, 0, sizeof(request));
	request.length = sizeof(descriptor);
	request.data = descriptor;
	CHECK(ioctl(fd, R5_JOYSTICK_GET_DESCRIPTOR, &request, sizeof(request))
		== 0, "R5_JOYSTICK_GET_DESCRIPTOR");
	CHECK(request.length == sizeof(kDualSense)
		&& memcmp(descriptor, kDualSense, sizeof(kDualSense)) == 0,
		"descriptor not the one given");
	request.length = 4;
	CHECK(ioctl(fd, R5_JOYSTICK_GET_DESCRIPTOR, &request, sizeof(request))
		!= 0 && errno == B_BUFFER_OVERFLOW
		&& request.length == sizeof(kDualSense), "descriptor overflow");
	request.length = sizeof(descriptor);
	request.data = (void*)(addr_t)0xffffffff80000000ULL;
	CHECK(ioctl(fd, R5_JOYSTICK_GET_DESCRIPTOR, &request, sizeof(request))
		!= 0, "descriptor into a kernel address");

	// The last full report, as fed.
	uint8 report[R5_JOYSTICK_CACHED_REPORT];
	r5_joystick_report get;
	bool got = false;
	for (int i = 0; i < 50 && !got; i++) {
		memset(&get, 0, sizeof(get));
		get.type = R5_JOYSTICK_REPORT_INPUT;
		get.id = 0x31;
		get.length = sizeof(report);
		get.data = report;
		got = ioctl(fd, R5_JOYSTICK_GET_REPORT, &get, sizeof(get)) == 0;
		if (!got)
			snooze(20000);
	}
	CHECK(got && get.length == sizeof(sFull)
		&& memcmp(report, sFull, sizeof(sFull)) == 0 && get.timestamp > 0,
		"R5_JOYSTICK_GET_REPORT input 0x31");
	get.id = 0x07;
	CHECK(ioctl(fd, R5_JOYSTICK_GET_REPORT, &get, sizeof(get)) != 0,
		"an undeclared input report");
	get.id = 0x31;
	get.type = R5_JOYSTICK_REPORT_FEATURE;
	CHECK(ioctl(fd, R5_JOYSTICK_GET_REPORT, &get, sizeof(get)) != 0,
		"a feature report over Bluetooth");

	// What may not be sent.
	uint8 out[80];
	memset(out, 0, sizeof(out));
	out[0] = 0x31;
	r5_joystick_report send;
	memset(&send, 0, sizeof(send));
	send.type = R5_JOYSTICK_REPORT_OUTPUT;
	send.id = 0x31;
	send.length = 79;
	send.data = out;
	CHECK(ioctl(fd, R5_JOYSTICK_SEND_REPORT, &send, sizeof(send)) != 0,
		"a report longer than declared");
	send.length = 78;
	send.id = 0x02;
	CHECK(ioctl(fd, R5_JOYSTICK_SEND_REPORT, &send, sizeof(send)) != 0,
		"an undeclared output report");
	send.id = 0;
	CHECK(ioctl(fd, R5_JOYSTICK_SEND_REPORT, &send, sizeof(send)) != 0,
		"no report ID on a device with IDs");
	send.id = 0x31;
	send.type = R5_JOYSTICK_REPORT_FEATURE;
	CHECK(ioctl(fd, R5_JOYSTICK_SEND_REPORT, &send, sizeof(send)) != 0,
		"a feature report");
	send.type = R5_JOYSTICK_REPORT_OUTPUT;
	send.data = (void*)(addr_t)0xffffffff80000000ULL;
	CHECK(ioctl(fd, R5_JOYSTICK_SEND_REPORT, &send, sizeof(send)) != 0,
		"a report from a kernel address");
	CHECK(ioctl(fd, R5_JOYSTICK_SEND_REPORT,
		(void*)(addr_t)0xffffffff80000000ULL, sizeof(send)) != 0,
		"a request at a kernel address");
	CHECK(ioctl(fd, R5_JOYSTICK_SEND_REPORT, &send, 4) != 0,
		"a short request");

	close(fd);
	printf("gamecontroller_feel_test: driver calls ok\n");
	return 0;
}


struct capture {
	bool		(*check)(const uint8*, uint32);
	bigtime_t	timeout;
	bool		found;
};


static status_t
capture_thread(void* data)
{
	capture* what = (capture*)data;
	what->found = read_output(what->check, what->timeout);
	return B_OK;
}


/*!	bluetooth_server waits on the same queue, with a 500 ms timeout. A
	reader that starts waiting first, and is then left 700 ms so the server
	times out and waits again behind it, gets the next report.
*/
static bool
send_until(BGameController& controller, status_t (*send)(BGameController&),
	bool (*check)(const uint8*, uint32))
{
	for (int i = 0; i < 4; i++) {
		capture what = { check, 2500000, false };
		thread_id reader = spawn_thread(capture_thread, "capture",
			B_NORMAL_PRIORITY, &what);
		resume_thread(reader);
		snooze(700000);
		status_t sent = send(controller);
		status_t result;
		wait_for_thread(reader, &result);
		if (sent != B_OK)
			return false;
		if (what.found)
			return true;
	}
	return false;
}


static status_t
send_rumble(BGameController& c)
{
	return c.SetRumble(1.0f, 0.5f, 0);
}


static status_t
send_trigger(BGameController& c)
{
	return c.SetTriggerEffect(B_CONTROLLER_TRIGGER_RIGHT,
		B_TRIGGER_EFFECT_RESISTANCE, 0.4f, 0.7f);
}


static status_t
send_stop(BGameController& c)
{
	return c.StopRumble();
}


static status_t
send_timed(BGameController& c)
{
	return c.SetRumble(0.3f, 0.3f, 1500000);
}


static bool
timed_report(const uint8* data, uint32 length)
{
	return valid_report(data, length) && data[5] == 77 && data[6] == 77;
}


static int
test_controller(const char* device)
{
	BGameController controller;
	CHECK(controller.Open(device) == B_OK, "open the controller");
	CHECK(controller.CanRumble() && controller.HasTriggerEffects()
		&& controller.HasMotion(), "the DualSense can rumble, has trigger "
		"effects and motion");

	// The buttons and the stick from the full reports only.
	bool pressed = false;
	for (int i = 0; i < 50 && !pressed; i++) {
		snooze(40000);
		pressed = controller.Update() == B_OK
			&& controller.IsButtonDown(B_CONTROLLER_BUTTON_A)
			&& controller.AxisValue(B_CONTROLLER_AXIS_LEFT_X) < -16000;
	}
	CHECK(pressed, "Cross and the left stick from the full report");

	// Motion.
	game_controller_motion motion;
	CHECK(controller.GetMotion(&motion) == B_NOT_ALLOWED,
		"motion before it is switched on");
	CHECK(controller.SetMotionEnabled(true) == B_OK, "switch motion on");
	bool moved = false;
	for (int i = 0; i < 50 && !moved; i++) {
		snooze(40000);
		moved = controller.Update() == B_OK
			&& controller.GetMotion(&motion) == B_OK;
	}
	CHECK(moved, "no motion sample");
	printf("gamecontroller_feel_test: motion %.2f %.2f %.2f g, %.1f %.1f "
		"%.1f deg/s\n", motion.acceleration[0], motion.acceleration[1],
		motion.acceleration[2], motion.rotation[0], motion.rotation[1],
		motion.rotation[2]);
	CHECK(motion.rotation[0] == 1 && motion.rotation[1] == -2
		&& motion.rotation[2] == 100 && motion.acceleration[0] == 0
		&& motion.acceleration[1] == 1 && motion.acceleration[2] == -0.5f
		&& motion.timestamp > 0, "motion values");
	// Switching motion on over Bluetooth sent a report (to switch the
	// controller to its full reports); it may already be gone to
	// bluetooth_server, so it is only drained here.
	read_output(any_report, 300000);

	// Rumble, a trigger effect, a stop.
	CHECK(send_until(controller, send_rumble, rumble_report),
		"rumble report");
	CHECK(send_until(controller, send_trigger, trigger_report),
		"trigger effect report");
	CHECK(send_until(controller, send_stop, stopped_report), "stop report");
	CHECK(controller.SetRumble(0.5f, 0.5f, -1) == B_BAD_VALUE,
		"negative duration");
	CHECK(controller.SetTriggerEffect((game_controller_trigger)7,
		B_TRIGGER_EFFECT_OFF, 0, 0) == B_BAD_VALUE, "unknown trigger");

	// A timed rumble stops by itself: 1.5 s, so this reader is back in
	// front of bluetooth_server when the stop comes.
	bool stoppedItself = false;
	for (int i = 0; i < 4 && !stoppedItself; i++) {
		CHECK(send_until(controller, send_timed, timed_report),
			"timed rumble report");
		stoppedItself = read_output(stopped_report, 3000000);
	}
	CHECK(stoppedItself, "a timed rumble did not stop");

	// Close() frees the triggers.
	bool freed = false;
	for (int i = 0; i < 4 && !freed; i++) {
		capture what = { freed_report, 2500000, false };
		thread_id reader = spawn_thread(capture_thread, "capture",
			B_NORMAL_PRIORITY, &what);
		resume_thread(reader);
		snooze(700000);
		controller.Close();
		status_t result;
		wait_for_thread(reader, &result);
		freed = what.found;
		if (!freed) {
			CHECK(controller.Open(device) == B_OK, "open again");
			CHECK(send_trigger(controller) == B_OK, "trigger again");
		}
	}
	CHECK(freed, "Close() left a trigger effect");
	printf("gamecontroller_feel_test: rumble and trigger reports ok\n");
	return 0;
}


static int
test_other_controller()
{
	// A controller without a model protocol or PID: everything says no.
	BGameController controller;
	for (int32 i = 0; i < controller.CountDevices(); i++) {
		char name[B_OS_NAME_LENGTH];
		if (controller.GetDeviceName(i, name, sizeof(name)) != B_OK
			|| controller.Open(name) != B_OK)
			continue;
		if (controller.VendorID() == 0x054c)
			continue;
		CHECK(!controller.CanRumble() || controller.SetRumble(0, 0, 0)
			== B_OK, "rumble on another controller");
		if (!controller.HasTriggerEffects()) {
			CHECK(controller.SetTriggerEffect(B_CONTROLLER_TRIGGER_LEFT,
				B_TRIGGER_EFFECT_CLICK, 0, 1) == B_NOT_SUPPORTED,
				"trigger effect without support");
		}
		if (!controller.HasMotion()) {
			CHECK(controller.SetMotionEnabled(true) == B_NOT_SUPPORTED,
				"motion without support");
		}
	}
	return 0;
}


static int32
painted(BView* view, BRect bounds, rgb_color background)
{
	BBitmap bitmap(bounds, B_BITMAP_ACCEPTS_VIEWS, B_RGBA32);
	if (bitmap.InitCheck() != B_OK)
		return -1;
	view->ResizeTo(bounds.Width(), bounds.Height());
	bitmap.AddChild(view);
	if (!bitmap.Lock())
		return -1;
	view->SetLowColor(background);
	view->SetViewColor(background);
	view->FillRect(bounds, B_SOLID_LOW);
	view->Draw(bounds);
	view->Sync();
	int32 count = 0;
	const uint8* bits = (const uint8*)bitmap.Bits();
	int32 length = bitmap.BitsLength();
	for (int32 i = 0; i + 3 < length; i += 4) {
		if (bits[i] != background.blue || bits[i + 1] != background.green
			|| bits[i + 2] != background.red)
			count++;
	}
	bitmap.RemoveChild(view);
	bitmap.Unlock();
	return count;
}


static int
test_drawing()
{
	rgb_color white = make_color(255, 255, 255);
	BAppearanceKit::BTiltLevel* level
		= new BAppearanceKit::BTiltLevel("level");
	level->SetValue(0.5f, -0.25f);
	CHECK(level->X() == 0.5f && level->Y() == -0.25f, "level value");
	level->SetValue(4, -9);
	CHECK(level->X() == 1 && level->Y() == -1, "level clamps");
	int32 pixels = painted(level, BRect(0, 0, 99, 99), white);
	delete level;
	CHECK(pixels > 200, "BTiltLevel draws too little");

	BAppearanceKit::BAxisBar* bar = new BAppearanceKit::BAxisBar("bar");
	bar->SetValue(-0.5f);
	CHECK(bar->Value() == -0.5f, "bar value");
	int32 half = painted(bar, BRect(0, 0, 199, 13), white);
	bar->SetValue(0);
	int32 none = painted(bar, BRect(0, 0, 199, 13), white);
	delete bar;
	CHECK(half > none && none > 100, "BAxisBar fill");

	BGameControllerMotionView* view
		= new BGameControllerMotionView("motion");
	game_controller_motion motion;
	memset(&motion, 0, sizeof(motion));
	motion.acceleration[0] = 0.5f;
	motion.rotation[1] = 180;
	view->SetMotion(motion);
	view->Reset();
	view->SetTurnRange(250);
	CHECK(view->TurnRange() == 250, "turn range");
	BSize size = view->PreferredSize();
	printf("gamecontroller_feel_test: motion view %.0f x %.0f\n",
		size.width, size.height);
	CHECK(size.width > 100 && size.height > 40, "motion view size");
	delete view;

	printf("gamecontroller_feel_test: drawing ok\n");
	return 0;
}


static int
run()
{
	sControl = open("/dev/" R5_BTHID_CONTROL_NAME, O_RDWR);
	CHECK(sControl >= 0, "open the bluetooth_hid control node (uid 0?)");

	r5_bthid_create create;
	memset(&create, 0, sizeof(create));
	create.descriptor = kDualSense;
	create.descriptor_length = sizeof(kDualSense);
	CHECK(ioctl(sControl, R5_BTHID_CREATE, &create, sizeof(create)) == 0,
		"create the virtual DualSense");
	sPadID = create.id;
	r5_bthid_info info;
	memset(&info, 0, sizeof(info));
	info.id = sPadID;
	info.vendor_id = 0x054c;
	info.product_id = 0x0ce6;
	strlcpy(info.name, kName, sizeof(info.name));
	CHECK(ioctl(sControl, R5_BTHID_SET_INFO, &info, sizeof(info)) == 0,
		"set its IDs");

	make_full_report();
	thread_id feeder = spawn_thread(feed_thread, "feed", B_NORMAL_PRIORITY,
		NULL);
	resume_thread(feeder);

	BString device;
	int result = 1;
	if (!find_pad(device))
		fail("the virtual DualSense is not listed");
	else {
		printf("gamecontroller_feel_test: virtual DualSense at %s\n",
			device.String());
		result = test_driver(device.String());
		if (result == 0)
			result = test_controller(device.String());
		if (result == 0)
			result = test_other_controller();
	}

	sFeeding = false;
	status_t status;
	wait_for_thread(feeder, &status);
	ioctl(sControl, R5_BTHID_DESTROY, &sPadID, sizeof(sPadID));
	close(sControl);
	return result;
}


int
main()
{
	if (run() != 0)
		return 1;

	BApplication app("application/x-vnd.R5OS-gamecontroller-feel-test");
	if (test_drawing() != 0)
		return 1;

	printf("gamecontroller_feel_test: ok\n");
	return 0;
}
