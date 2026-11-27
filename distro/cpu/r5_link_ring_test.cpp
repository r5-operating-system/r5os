/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * r5_link_ring_test: checks of the shared-memory link between programs and
 * app_server (docs/design/APP-SERVER-RING.md, docs/KERNEL.md "One-team
 * areas").
 *
 * 1. Isolation: no other team's ring can be cloned or shared again; this
 *    program's own ring cannot be made cloneable, cloned, transferred or
 *    shared, and a fork() child does not get it. A fresh area can be shared
 *    into exactly one other team.
 * 2. Garbage: this program writes bad data into its own window rings (bad
 *    record sizes, positions far ahead, unknown record kinds, random link
 *    messages, unordered port batches, a flood of wakes, a marker without
 *    its batch). After each, app_server must still be running and answer a
 *    new window. A hang is a failure (watchdog, 10 s).
 *
 * Output lines start with "r5_link_ring_test:", on stdout and the serial
 * line. The exit status is the number of failures. Without a ring (the
 * Performance switch off, Safe mode) only the port checks run.
 */


#include <Application.h>
#include <OS.h>
#include <Window.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <syscalls.h>


static const char* kRingName = "r5 link ring";
static const int32 kLinkCode = '_PTL';
static const int32 kWakeCode = '_PTW';
static const uint32 kFallbackCode = 'r5RF';

// The layout of LinkRing.cpp (link_ring_shared).
static const size_t kToServerHead = 64;
static const size_t kToServerTail = 64 + 64;
static const size_t kToServerClosed = 64 + 132;
static const size_t kDataOffset = B_PAGE_SIZE;

static int32 sFailures = 0;
static team_id sAppServer = -1;
static sem_id sWatchdogSem = -1;


static void
report(bool ok, const char* format, ...)
{
	char line[256];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	printf("r5_link_ring_test: %s %s\n", ok ? "PASS" : "FAIL", line);
	debug_printf("r5_link_ring_test: %s %s\n", ok ? "PASS" : "FAIL", line);
	fflush(stdout);
	if (!ok)
		sFailures++;
}


static void
note(const char* format, ...)
{
	char line[256];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	printf("r5_link_ring_test: %s\n", line);
	debug_printf("r5_link_ring_test: %s\n", line);
	fflush(stdout);
}


static int32
watchdog(void* data)
{
	const char* what = (const char*)data;
	if (acquire_sem_etc(sWatchdogSem, 1, B_RELATIVE_TIMEOUT, 10000000)
			== B_TIMED_OUT) {
		report(false, "hang: %s did not answer within 10 s", what);
		debug_printf("r5_link_ring_test: done, failures %" B_PRId32 "\n",
			sFailures);
		_exit(100);
	}
	return 0;
}


static team_id
find_app_server()
{
	int32 cookie = 0;
	team_info info;
	while (get_next_team_info(&cookie, &info) == B_OK) {
		const char* name = strrchr(info.args, '/');
		name = name != NULL ? name + 1 : info.args;
		if (strncmp(name, "app_server", 10) == 0)
			return info.team;
	}
	return -1;
}


static int32
own_rings(area_id* areas, int32 max)
{
	int32 count = 0;
	ssize_t cookie = 0;
	area_info info;
	while (get_next_area_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {
		if (strcmp(info.name, kRingName) == 0 && count < max)
			areas[count++] = info.area;
	}
	return count;
}


static port_id
server_port(const char* title)
{
	int32 cookie = 0;
	port_info info;
	while (get_next_port_info(sAppServer, &cookie, &info) == B_OK) {
		if (strcmp(info.name, title) == 0)
			return info.port;
	}
	return -1;
}


/*!	A window with a round trip: Sync() under the watchdog. */
static bool
window_answers(BWindow* window, const char* what)
{
	thread_id dog = spawn_thread(watchdog, "watchdog", B_NORMAL_PRIORITY,
		(void*)what);
	resume_thread(dog);
	bool ok = false;
	if (window->Lock()) {
		window->Sync();
		ok = true;
		window->Unlock();
	}
	release_sem(sWatchdogSem);
	status_t result;
	wait_for_thread(dog, &result);
	return ok;
}


static bool
app_server_fine(const char* after)
{
	team_info info;
	if (get_team_info(sAppServer, &info) != B_OK) {
		report(false, "app_server is gone after %s", after);
		return false;
	}
	BWindow* window = new BWindow(BRect(40, 40, 160, 100), "ring_check",
		B_TITLED_WINDOW, B_NOT_RESIZABLE);
	window->Show();
	bool ok = window_answers(window, "a new window");
	if (window->Lock())
		window->Quit();
	report(ok, "app_server runs and answers a new window after %s", after);
	return ok;
}


// #pragma mark - isolation


static void
test_isolation(area_id ownRing)
{
	area_info own;
	get_area_info(ownRing, &own);

	// Every other team's rings, app_server's side included.
	int32 others = 0;
	int32 refused = 0;
	int32 teamCookie = 0;
	team_info team;
	while (get_next_team_info(&teamCookie, &team) == B_OK) {
		if (team.team == getpid())
			continue;
		ssize_t cookie = 0;
		area_info info;
		while (get_next_area_info(team.team, &cookie, &info) == B_OK) {
			if (strcmp(info.name, kRingName) != 0)
				continue;
			others++;
			void* address = NULL;
			area_id clone = clone_area("ring thief", &address, B_ANY_ADDRESS,
				B_READ_AREA | B_WRITE_AREA, info.area);
			area_id share = _kern_r5_share_area(info.area, getpid(),
				B_READ_AREA | B_WRITE_AREA);
			if (clone < 0 && share < 0)
				refused++;
			else {
				if (clone >= 0)
					delete_area(clone);
				if (share >= 0)
					delete_area(share);
			}
		}
	}
	report(others > 0 && refused == others,
		"other teams' rings: %" B_PRId32 " found, %" B_PRId32 " refused to "
		"clone and to share", others, refused);

	status_t status = set_area_protection(ownRing,
		B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);
	report(status != B_OK, "own ring cannot be made cloneable (%s)",
		strerror(status));

	status = resize_area(ownRing, B_PAGE_SIZE);
	report(status != B_OK, "own ring cannot be resized (%s)", strerror(status));

	void* address = NULL;
	area_id clone = clone_area("own clone", &address, B_ANY_ADDRESS,
		B_READ_AREA, ownRing);
	report(clone < 0, "own ring cannot be cloned in its own team (%s)",
		strerror(clone));
	if (clone >= 0)
		delete_area(clone);

	address = NULL;
	area_id moved = _kern_transfer_area(ownRing, &address, B_ANY_ADDRESS,
		sAppServer);
	report(moved < 0, "own ring cannot be transferred (%s)",
		strerror(moved));

	area_id again = _kern_r5_share_area(ownRing, sAppServer,
		B_READ_AREA | B_WRITE_AREA);
	report(again < 0, "own ring cannot be shared again (%s)",
		strerror(again));

	// fork(): the child must not have the ring.
	int pipeIn[2];
	int pipeOut[2];
	pipe(pipeIn);
	pipe(pipeOut);
	pid_t child = fork();
	if (child == 0) {
		area_id mapped = area_for(own.address);
		char answer = mapped < 0 ? 'y' : 'n';
		write(pipeOut[1], &answer, 1);
		// Wait so the parent can share an area into this team.
		char go;
		read(pipeIn[0], &go, 1);
		_exit(0);
	}
	char answer = 'n';
	read(pipeOut[0], &answer, 1);
	report(answer == 'y', "a fork() child does not get the ring");

	// A fresh area goes to exactly one other team.
	void* base = NULL;
	area_id fresh = create_area("ring share test", &base, B_ANY_ADDRESS,
		B_PAGE_SIZE, B_NO_LOCK, B_READ_AREA | B_WRITE_AREA);
	area_id first = _kern_r5_share_area(fresh, child,
		B_READ_AREA | B_WRITE_AREA);
	area_id second = _kern_r5_share_area(fresh, child, B_READ_AREA);
	void* cloneAddress = NULL;
	area_id selfClone = clone_area("fresh clone", &cloneAddress,
		B_ANY_ADDRESS, B_READ_AREA, fresh);
	area_info firstInfo;
	bool firstInChild = first >= 0 && get_area_info(first, &firstInfo) == B_OK
		&& firstInfo.team == child;
	report(firstInChild && second < 0 && selfClone < 0,
		"a new area is shared into one team (%s), not a second time (%s), "
		"and not cloned after it (%s)", strerror(first), strerror(second),
		strerror(selfClone));
	area_id fork2 = _kern_r5_share_area(fresh, child, B_READ_AREA);
	(void)fork2;

	char go = 'g';
	write(pipeIn[1], &go, 1);
	int childStatus;
	waitpid(child, &childStatus, 0);
	delete_area(fresh);
	close(pipeIn[0]);
	close(pipeIn[1]);
	close(pipeOut[0]);
	close(pipeOut[1]);
}


// #pragma mark - garbage


struct Ring {
	uint8*		base;
	uint32		size;
	uint32*		head;
	uint32*		tail;
	uint32*		closed;
};


static bool
map_ring(area_id area, Ring& ring)
{
	area_info info;
	if (get_area_info(area, &info) != B_OK)
		return false;
	ring.base = (uint8*)info.address;
	ring.size = ((uint32*)ring.base)[2];
	ring.head = (uint32*)(ring.base + kToServerHead);
	ring.tail = (uint32*)(ring.base + kToServerTail);
	ring.closed = (uint32*)(ring.base + kToServerClosed);
	return ring.size >= B_PAGE_SIZE && (ring.size & (ring.size - 1)) == 0;
}


static void
put(Ring& ring, uint32 position, const void* data, size_t size)
{
	for (size_t i = 0; i < size; i++) {
		ring.base[kDataOffset + ((position + i) & (ring.size - 1))]
			= ((const uint8*)data)[i];
	}
}


static void
publish(Ring& ring, uint32 head)
{
	__atomic_store_n(ring.head, head, __ATOMIC_RELEASE);
}


enum {
	kBadSize = 0,
	kFarHead,
	kBadKind,
	kRandomMessages,
	kUnorderedBatches,
	kWakeFlood,
	kMarkerWithoutBatch,
	kScenarioCount
};

static const char* kScenarioNames[] = {
	"a record size beyond the ring",
	"a write position far ahead",
	"an unknown record kind",
	"random link messages",
	"two port batches without their marker",
	"a flood of wake messages",
	"a marker without its port batch"
};


static void
run_scenario(int32 scenario)
{
	area_id before[64];
	int32 beforeCount = own_rings(before, 64);

	char title[32];
	snprintf(title, sizeof(title), "ring_%c", (char)('a' + scenario));
	BWindow* window = new BWindow(BRect(60 + 20 * scenario, 60, 220, 140),
		title, B_TITLED_WINDOW, B_NOT_RESIZABLE);
	window->Show();
	window_answers(window, "the window before");

	area_id after[64];
	int32 afterCount = own_rings(after, 64);
	area_id ringArea = -1;
	for (int32 i = 0; i < afterCount && ringArea < 0; i++) {
		bool known = false;
		for (int32 j = 0; j < beforeCount; j++)
			known |= after[i] == before[j];
		if (!known)
			ringArea = after[i];
	}
	port_id port = server_port(title);
	Ring ring;
	if (ringArea < 0 || port < 0 || !map_ring(ringArea, ring)) {
		report(false, "%s: no window ring or port found", kScenarioNames[scenario]);
		return;
	}

	uint32 head = __atomic_load_n(ring.head, __ATOMIC_ACQUIRE);
	srand(1234 + scenario);
	switch (scenario) {
		case kBadSize:
		{
			uint32 record[2] = { 0xfffffff0, 1 };
			put(ring, head, record, sizeof(record));
			publish(ring, head + 16);
			break;
		}
		case kFarHead:
			publish(ring, head + 0x40000000);
			break;
		case kBadKind:
		{
			uint32 record[4] = { 8, 0x1234567, 0, 0 };
			put(ring, head, record, sizeof(record));
			publish(ring, head + 16);
			break;
		}
		case kRandomMessages:
		{
			// Link messages with codes app_server does not know, and
			// headers whose sizes do not add up.
			uint8 payload[1024];
			for (size_t i = 0; i < sizeof(payload); i++)
				payload[i] = (uint8)rand();
			int32* words = (int32*)payload;
			words[0] = 64;
			words[1] = 0x7fff0000;
			words[2] = 0;
			words[16] = 4000;
			words[17] = 0x7fff0001;
			uint32 record[2] = { sizeof(payload), 1 };
			put(ring, head, record, sizeof(record));
			put(ring, head + 8, payload, sizeof(payload));
			publish(ring, head + 8 + sizeof(payload));
			break;
		}
		case kUnorderedBatches:
		{
			int32 batch[6] = { 12, (int32)kFallbackCode, 0, 12, 0x7fff0002, 0 };
			write_port(port, kLinkCode, batch, sizeof(batch));
			write_port(port, kLinkCode, batch, sizeof(batch));
			break;
		}
		case kWakeFlood:
			for (int32 i = 0; i < 300; i++)
				write_port_etc(port, kWakeCode, NULL, 0, B_RELATIVE_TIMEOUT, 0);
			break;
		case kMarkerWithoutBatch:
		{
			uint32 record[2] = { 0, 2 };
			put(ring, head, record, sizeof(record));
			publish(ring, head + 8);
			break;
		}
	}
	write_port_etc(port, kWakeCode, NULL, 0, B_RELATIVE_TIMEOUT, 0);
	snooze(200000);

	bool closed = __atomic_load_n(ring.closed, __ATOMIC_ACQUIRE) != 0;
	note("%s: app_server %s the ring", kScenarioNames[scenario],
		closed ? "closed" : "kept");

	if (scenario != kMarkerWithoutBatch) {
		// The window itself goes on, on its port if the ring was closed.
		bool ok = window_answers(window, kScenarioNames[scenario]);
		report(ok, "%s: the window still answers", kScenarioNames[scenario]);
		if (window->Lock())
			window->Quit();
	} else {
		// That window waits for a batch that never comes; only its own
		// link. It ends with this program.
		note("%s: that window is left waiting, as intended",
			kScenarioNames[scenario]);
	}
	app_server_fine(kScenarioNames[scenario]);
}


// #pragma mark -


class TestApp : public BApplication {
public:
	TestApp()
		:
		BApplication("application/x-vnd.R5OS-link-ring-test")
	{
	}

	virtual void ReadyToRun()
	{
		thread_id thread = spawn_thread(_Run, "tests", B_NORMAL_PRIORITY,
			this);
		resume_thread(thread);
	}

	static int32 _Run(void*)
	{
		sAppServer = find_app_server();
		sWatchdogSem = create_sem(0, "watchdog");
		if (sAppServer < 0) {
			report(false, "app_server not found");
			be_app->PostMessage(B_QUIT_REQUESTED);
			return 0;
		}

		area_id rings[64];
		int32 count = own_rings(rings, 64);
		if (count == 0) {
			note("no ring on the application link: ports only (switch off "
				"or Safe mode)");
			app_server_fine("start on ports");
		} else {
			note("%" B_PRId32 " ring(s) on this program's links", count);
			test_isolation(rings[0]);
			for (int32 i = 0; i < kScenarioCount; i++)
				run_scenario(i);
		}

		note("done, failures %" B_PRId32, sFailures);
		// The window of the last scenario waits for good; quitting the
		// application would wait for it too.
		_exit(sFailures);
		return 0;
	}
};


int
main()
{
	TestApp app;
	app.Run();
	// The window of the last scenario waits for good; do not wait for it.
	_exit(sFailures);
	return sFailures;
}
