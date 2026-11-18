/*
 * Copyright 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Verify snooze() accuracy at 1 ms, 10 ms and 1 s against system_time().
 */


#include <stdio.h>
#include <OS.h>


static int
check_sleep(bigtime_t us, bigtime_t maxOverrunUs)
{
	bigtime_t t0 = system_time();
	snooze(us);
	bigtime_t dt = system_time() - t0;
	if (dt < us - 200 || dt > us + maxOverrunUs) {
		printf("r5_timer_test FAIL req=%lld got=%lld\n",
			(long long)us, (long long)dt);
		return 1;
	}
	printf("r5_timer_test req=%lld got=%lld\n", (long long)us, (long long)dt);
	return 0;
}


int
main(void)
{
	int rc = 0;
	rc |= check_sleep(1000, 50000);
	rc |= check_sleep(10000, 50000);
	rc |= check_sleep(1000000, 150000);
	if (rc == 0)
		printf("r5_timer_test PASS\n");
	return rc;
}
