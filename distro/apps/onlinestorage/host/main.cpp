/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#include "SelfTest.h"

#include <stdio.h>


int
main()
{
	int result = RunOnlineStorageTests();
	if (result != 0)
		fprintf(stderr, "host tests failed\n");
	return result;
}
