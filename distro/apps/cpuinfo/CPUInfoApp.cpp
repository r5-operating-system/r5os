/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */

#include "CPUInfoWindow.h"
#include "CpuReport.h"

#include <Application.h>

#include <stdio.h>
#include <string.h>


class CPUInfoApp : public BApplication {
public:
	CPUInfoApp()
		:
		BApplication("application/x-vnd.R5-CPUInfo")
	{
	}

	virtual void ReadyToRun()
	{
		CPUInfoWindow* window = new CPUInfoWindow();
		window->Show();
	}
};


int
main(int argc, char** argv)
{
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--cli") == 0) {
			CpuReport report;
			CollectCpuReport(report);
			BString text = FormatCpuReport(report);
			fputs(text.String(), stdout);
			return 0;
		}
	}

	CPUInfoApp app;
	app.Run();
	return 0;
}
