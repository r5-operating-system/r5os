// SPDX-License-Identifier: MIT
// Copyright 2026 R5OS Contributors
//
// thread_local destructor after dlclose() (GitHub #3012, Haiku #18182).
//
// Each round: dlopen() the library, a thread creates the library's
// thread_local object, the main thread dlclose()s the library, and only then
// the thread ends. The destructor must run exactly once, without a crash, and
// the library must be gone after the thread ended. Before the fix the thread
// called into the unmapped library at its end.
//
// Build (scripts/build-stability-tests.sh):
//   x86_64-unknown-haiku-g++ -shared -fPIC -o libtlsdtor.so tls_dtor_lib.cpp
//   x86_64-unknown-haiku-g++ -o tls_dtor_test tls_dtor_test.cpp
// Run next to libtlsdtor.so: ./tls_dtor_test [rounds]

#include <OS.h>
#include <image.h>

#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef void (*touch_func)(int* counter);


struct Round {
	touch_func	touch;
	int			counter;
	sem_id		touched;
	sem_id		release;
};


static bool
library_loaded()
{
	image_info info;
	int32 cookie = 0;
	while (get_next_image_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {
		if (strstr(info.name, "libtlsdtor.so") != NULL)
			return true;
	}
	return false;
}


static void*
worker(void* data)
{
	Round* round = (Round*)data;
	round->touch(&round->counter);
	release_sem(round->touched);
	acquire_sem(round->release);
	return NULL;
		// the thread_local destructor runs after this
}


static int
run_round(const char* path, int number)
{
	Round round;
	round.counter = 0;
	round.touched = create_sem(0, "touched");
	round.release = create_sem(0, "release");

	void* handle = dlopen(path, RTLD_NOW);
	if (handle == NULL) {
		printf("FAIL round %d: dlopen: %s\n", number, dlerror());
		return 1;
	}
	round.touch = (touch_func)dlsym(handle, "tls_dtor_touch");
	if (round.touch == NULL) {
		printf("FAIL round %d: dlsym\n", number);
		return 1;
	}

	pthread_t thread;
	pthread_create(&thread, NULL, worker, &round);
	acquire_sem(round.touched);

	dlclose(handle);
	bool loadedWhilePending = library_loaded();

	release_sem(round.release);
	pthread_join(thread, NULL);

	bool loadedAfter = library_loaded();
	int counter = __atomic_load_n(&round.counter, __ATOMIC_SEQ_CST);

	delete_sem(round.touched);
	delete_sem(round.release);

	if (counter != 1 || !loadedWhilePending || loadedAfter) {
		printf("FAIL round %d: destructor ran %d times, loaded while pending "
			"%d, loaded after %d\n", number, counter, loadedWhilePending,
			loadedAfter);
		return 1;
	}
	return 0;
}


int
main(int argc, char** argv)
{
	int rounds = argc > 1 ? atoi(argv[1]) : 100;

	char path[B_PATH_NAME_LENGTH];
	strlcpy(path, argv[0], sizeof(path));
	char* slash = strrchr(path, '/');
	if (slash != NULL)
		slash[1] = '\0';
	else
		path[0] = '\0';
	strlcat(path, "libtlsdtor.so", sizeof(path));

	for (int i = 0; i < rounds; i++) {
		if (run_round(path, i) != 0)
			return 1;
	}

	printf("PASS tls_dtor_test: %d rounds, each destructor ran once after "
		"dlclose()\n", rounds);
	return 0;
}
