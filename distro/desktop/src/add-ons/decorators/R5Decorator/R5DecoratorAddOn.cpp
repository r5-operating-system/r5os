/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 */


/*!	The add-on entry of the R5 window frame (Default (R5)). The class itself
	is in R5Decorator.cpp, which Default (R5 Modern) builds as well.
*/


#include "R5Decorator.h"

#include <new>
#include <syslog.h>


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
	return new (std::nothrow) R5Decorator(settings, rect, desktop);
}


//	#pragma mark - add-on entry


R5_DECORATOR_ABI_EXPORT


extern "C" DecorAddOn*
(instantiate_decor_addon)(image_id id, const char* name)
{
	syslog(LOG_INFO, "decorator: r5 loaded\n");
	return new (std::nothrow) R5DecorAddOn(id, name);
}
