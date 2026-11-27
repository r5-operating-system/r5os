// SPDX-License-Identifier: MIT
// Copyright 2026 R5OS Contributors
//
// Library half of tls_dtor_test (GitHub #3012): a thread_local object whose
// destructor lives in this library. The test unloads the library while a
// thread still owns such an object.

#include <stdio.h>


namespace {

struct Tracked {
	Tracked()
		:
		counter(NULL)
	{
	}

	~Tracked()
	{
		if (counter != NULL)
			__atomic_add_fetch(counter, 1, __ATOMIC_SEQ_CST);
		snprintf(text, sizeof(text), "destroyed");
	}

	int*	counter;
	char	text[64];
};


thread_local Tracked sTracked;

}	// namespace


extern "C" void
tls_dtor_touch(int* counter)
{
	sTracked.counter = counter;
	snprintf(sTracked.text, sizeof(sTracked.text), "alive");
}
