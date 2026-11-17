/*
 * SPDX-License-Identifier: MIT
 * Copyright 2026 R5OS Contributors
 *
 * Host test of the voice path (docs/BLUETOOTH.md, step 7c), run by
 * scripts/test-voice.sh with AddressSanitizer and UBSan: the checks of
 * distro/bluetooth/tests/bt_voice_test/voice_checks.h, which bt_voice_test
 * also runs in QEMU. Prints "voice_test: ok" when everything passed.
 */


#include <stdio.h>
#include <stdlib.h>

#include "msbc.h"
#include "h2sco.h"
#include "VoiceLink.h"


static int sFailures = 0;


static void
check(bool condition, const char* what)
{
	if (!condition) {
		printf("voice_test: FAIL %s\n", what);
		sFailures++;
	}
}


#include "voice_checks.h"


int
main()
{
	voice_checks::run_all();
	if (sFailures != 0) {
		printf("voice_test: %d failed\n", sFailures);
		return 1;
	}
	printf("voice_test: ok\n");
	return 0;
}
