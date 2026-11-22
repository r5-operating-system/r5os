/*
 * Copyright 2005-2014, Haiku, Inc.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		Rene Gollent
 *		Nathan Whitehorn
 */

/*
 * R5OS modifications in this file are MIT, like the original work. They are
 * wrapped in SPDX-Snippet markers. The copyright and license notice above still
 * cover the original work. The original is air/OS's copy (commit
 * a4ddc48124a9) of Haiku's src/servers/power.
 */


#include "power_button_monitor.h"

#include "power_policy.h"

#include <string.h>

#include <Directory.h>
#include <Messenger.h>
#include <Roster.h>
#include <String.h>

#include <RosterPrivate.h>


static const char* kBasePath = "/dev/power/button";


PowerButtonMonitor::PowerButtonMonitor(PowerPolicy* policy)
	:
	fFDs(),
	fPolicy(policy)
{
	BDirectory dir;
	if (dir.SetTo(kBasePath) != B_OK)
		return;

	entry_ref ref;
	while (dir.GetNextRef(&ref) == B_OK) {
		if (strncmp(ref.name, "power", 5) == 0) {
			BString path;
			path.SetToFormat("%s/%s", kBasePath, ref.name);
			int fd = open(path.String(), O_RDONLY);
			if (fd > 0)
				fFDs.insert(fd);
		}
	}
}


PowerButtonMonitor::~PowerButtonMonitor()
{
	for (std::set<int>::iterator it = fFDs.begin(); it != fFDs.end(); ++it)
		close(*it);
}


void
PowerButtonMonitor::HandleEvent(int fd)
{
	uint8 button_pressed;
	if (read(fd, &button_pressed, 1) != 1)
		return;

// SPDX-SnippetBegin
// SPDX-SnippetCopyrightText: 2026 R5OS Contributors
// SPDX-License-Identifier: MIT
	// What the press does is the machine's setting ("When the power button
	// is pressed", docs/POWER.md, section 2). air/OS's check for the press
	// that woke the machine comes back with sleep (step 4).
	if (button_pressed)
		fPolicy->PowerButtonPressed();
// SPDX-SnippetEnd
}
