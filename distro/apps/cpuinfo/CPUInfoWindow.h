/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef CPU_INFO_WINDOW_H
#define CPU_INFO_WINDOW_H


#include "CpuReport.h"

#include <Window.h>


class CPUInfoWindow : public BWindow {
public:
							CPUInfoWindow();
	virtual					~CPUInfoWindow();

	virtual void			MessageReceived(BMessage* message);
	virtual bool			QuitRequested();

private:
			CpuReport		fReport;
};


#endif	// CPU_INFO_WINDOW_H
