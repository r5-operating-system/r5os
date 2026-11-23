/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


/*!	The add-on entry of the R5 window frame (Default (R5)): the square
	counterpart to R5 Modern (0 px corners and square tab in the R5 Modern
	style).
*/


#define R5_SQUARE_DECORATOR 1
#include "../R5ModernDecorator/R5ModernDecorator.cpp"


//	#pragma mark - R5DecorAddOn


R5DecorAddOn::R5DecorAddOn(image_id id, const char* name)
	:
	DecorAddOn(id, name)
{
}


Decorator*
R5DecorAddOn::_AllocateDecorator(DesktopSettings& settings, BRect rect,
	Desktop* desktop)
{
	return new (std::nothrow) R5ModernDecorator(settings, rect, desktop, true);
}


//	#pragma mark - add-on entry


R5_DECORATOR_ABI_EXPORT


extern "C" DecorAddOn*
(instantiate_decor_addon)(image_id id, const char* name)
{
	syslog(LOG_INFO, "decorator: r5 loaded\n");
	return new (std::nothrow) R5DecorAddOn(id, name);
}
