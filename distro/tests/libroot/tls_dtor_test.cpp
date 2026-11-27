// SPDX-License-Identifier: MIT
// Copyright 2026 R5OS Contributors
//
// thread_local destructor after dlclose() (GitHub #3012, Haiku #18182).
//
// First a clean round: dlopen() the library, a thread creates the library's
// thread_local object and ends, then dlclose(). The destructor runs once and
// the library is unloaded.
// Then the rounds of the bug: dlopen(), a thread creates the object, the main
// thread dlclose()s the library, and only then the thread ends. The
// destructor must run exactly once, without a crash, so the library has to
// stay loaded (as with glibc, it then stays loaded for good). Before the fix
// the thread called into the unmapped library at its end.
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


// closeFirst: dlclose() while the thread still owns the object
static int
run_round(const char* path, int number, bool closeFirst)
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

	if (closeFirst)
		dlclose(handle);
	bool loadedAfterClose = library_loaded();

	release_sem(round.release);
	pthread_join(thread, NULL);
	int counter = __atomic_load_n(&round.counter, __ATOMIC_SEQ_CST);

	if (!closeFirst)
		dlclose(handle);
	bool loadedAtEnd = library_loaded();

	delete_sem(round.touched);
	delete_sem(round.release);

	bool ok = counter == 1 && loadedAfterClose
		&& (closeFirst ? loadedAtEnd : !loadedAtEnd);
	if (!ok) {
		printf("FAIL round %d (%s): destructor ran %d times, loaded after "
			"dlclose %d, loaded at the end %d\n", number,
			closeFirst ? "dlclose first" : "thread first", counter,
			loadedAfterClose, loadedAtEnd);
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

	if (run_round(path, 0, false) != 0)
		return 1;
	for (int i = 1; i <= rounds; i++) {
		if (run_round(path, i, true) != 0)
			return 1;
	}

	printf("PASS tls_dtor_test: clean round unloaded; %d rounds with "
		"dlclose() first kept the library and ran each destructor once\n",
		rounds);
	return 0;
}
