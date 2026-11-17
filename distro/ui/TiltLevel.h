/*
 * SPDX-FileCopyrightText: 2026 R5OS Contributors
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef _R5_TILT_LEVEL_H
#define _R5_TILT_LEVEL_H


#include <View.h>


namespace BAppearanceKit {


// A spirit level seen from above: a bubble in a circle, with a cross and a
// small centre ring. x and y run from -1 to 1 (left to right, top to
// bottom); 0, 0 is the centre. Shows a value, the user does not edit it.
// Drawn with the UI colours, so it follows dark mode.
class BTiltLevel : public BView {
public:
								BTiltLevel(const char* name);
								BTiltLevel(BMessage* archive);
	virtual						~BTiltLevel();

	static	BArchivable*		Instantiate(BMessage* archive);
	virtual	status_t			Archive(BMessage* archive,
									bool deep = true) const;

			// Clamped to -1 to 1; redraws only when it moved.
			void				SetValue(float x, float y);
			float				X() const;
			float				Y() const;

	virtual	void				AttachedToWindow();
	virtual	void				Draw(BRect updateRect);
	virtual	void				FrameResized(float width, float height);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();

			// False draws it gray, as a disabled control; true at first.
			void				SetEnabled(bool enabled);
			bool				IsEnabled() const;

private:
			void				_Init();

	virtual	void				_ReservedTiltLevel1();
	virtual	void				_ReservedTiltLevel2();
	virtual	void				_ReservedTiltLevel3();
	virtual	void				_ReservedTiltLevel4();

								BTiltLevel(const BTiltLevel&);
			BTiltLevel&			operator=(const BTiltLevel&);

			float				fX;
			float				fY;
			bool				fEnabled;
			uint32				_reserved[4];
};


}	// namespace BAppearanceKit


#endif	// _R5_TILT_LEVEL_H
