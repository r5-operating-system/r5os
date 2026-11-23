/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */
#ifndef _R5_MODERN_STYLE_H
#define _R5_MODERN_STYLE_H


#include <ControlLook.h>


namespace BAppearanceKit {


/*!	True while the control look is R5 Modern, or another look that asks for
	the R5 style: it returns more than Haiku's 1 for B_MENU_BACKGROUND
	(docs/LIBR5.md, "Following the controls"). Haiku's and R5's controls
	return 1. Inside libr5ui only; not installed.
*/
static inline bool
r5ui_modern_controls()
{
	if (be_control_look == NULL)
		return false;
	float left = 0, top = 0, right = 0, bottom = 0;
	be_control_look->GetBackgroundInsets(BControlLook::B_MENU_BAR_BACKGROUND, 0,
		left, top, right, bottom);
	return left > 6;
}


}	// namespace BAppearanceKit


#endif	// _R5_MODERN_STYLE_H
