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
#ifndef _R5_AXIS_BAR_H
#define _R5_AXIS_BAR_H


#include <View.h>


namespace BAppearanceKit {


// A horizontal bar for a value from -1 to 1 that fills from the middle:
// to the right for a positive value, to the left for a negative one, with
// a tick at 0. For a stick axis, a turn rate or a balance. Shows a value,
// the user does not edit it (that is BSlider). Drawn with the UI colours,
// so it follows dark mode.
class BAxisBar : public BView {
public:
								BAxisBar(const char* name);
								BAxisBar(BMessage* archive);
	virtual						~BAxisBar();

	static	BArchivable*		Instantiate(BMessage* archive);
	virtual	status_t			Archive(BMessage* archive,
									bool deep = true) const;

			// Clamped to -1 to 1; redraws only when it changed.
			void				SetValue(float value);
			float				Value() const;

	virtual	void				AttachedToWindow();
	virtual	void				Draw(BRect updateRect);
	virtual	void				FrameResized(float width, float height);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();

private:
			void				_Init();

	virtual	void				_ReservedAxisBar1();
	virtual	void				_ReservedAxisBar2();
	virtual	void				_ReservedAxisBar3();
	virtual	void				_ReservedAxisBar4();

								BAxisBar(const BAxisBar&);
			BAxisBar&			operator=(const BAxisBar&);

			float				fValue;
			uint32				_reserved[4];
};


}	// namespace BAppearanceKit


#endif	// _R5_AXIS_BAR_H
