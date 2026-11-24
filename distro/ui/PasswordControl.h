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
#ifndef _PASSWORD_CONTROL_H
#define _PASSWORD_CONTROL_H


#include <Control.h>
#include <View.h>


class BTextView;


namespace BAppearanceKit {


class BPasswordControl : public BControl {
public:
								BPasswordControl(const char* name,
									const char* label,
									const char* text = NULL,
									BMessage* message = NULL);
								BPasswordControl(BMessage* archive);
	virtual						~BPasswordControl();

	static	BArchivable*		Instantiate(BMessage* archive);
	virtual	status_t			Archive(BMessage* archive,
									bool deep = true) const;

			const char*			Text() const;
			void				SetText(const char* text);
			BTextView*			TextView() const;

			bool				IsPasswordRevealed() const;
			void				SetPasswordRevealed(bool revealed);

			// Gray text shown while the field is empty, such as
			// "Enter password".
			void				SetPlaceholder(const char* text);
			const char*			Placeholder() const;

			// A red border, used only when the router refused the password.
			void				SetInvalid(bool invalid);
			bool				IsInvalid() const;

	virtual	void				AttachedToWindow();
	virtual	void				FrameResized(float newWidth, float newHeight);
	virtual	void				Draw(BRect updateRect);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MakeFocus(bool focus = true);
	virtual	void				SetEnabled(bool enabled);
	virtual	void				WindowActivated(bool active);

	virtual	void				GetPreferredSize(float* width, float* height);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();

	virtual	void				MessageReceived(BMessage* message);

private:
			void				_LayoutChildren();
			BRect				_EyeButtonRect() const;
			void				_DrawEyeButton();

	virtual	void				_ReservedPasswordControl1();
	virtual	void				_ReservedPasswordControl2();
	virtual	void				_ReservedPasswordControl3();
	virtual	void				_ReservedPasswordControl4();

								BPasswordControl(const BPasswordControl&);
			BPasswordControl&	operator=(const BPasswordControl&);

			struct Data;
			Data*				fData;
			uint32				_reserved[4];
};


// Accounts step 5 (24 Nov 2026): the strength hint under a new password, as
// in "Add account" and on the first-start screen of Sign in. Four parts, lit
// from the left as far as the strength goes. StrengthOf() gives 0 (empty)
// to 4 (strong); HintFor() the line shown under the bar.
class BPasswordStrengthBar : public BView {
public:
								BPasswordStrengthBar(const char* name);
	virtual						~BPasswordStrengthBar();

			void				SetStrength(int32 strength);
			int32				Strength() const;

	static	int32				StrengthOf(const char* password);
	static	const char*			HintFor(int32 strength);

	virtual	void				Draw(BRect updateRect);

private:
	virtual	void				_ReservedPasswordStrengthBar1();
	virtual	void				_ReservedPasswordStrengthBar2();

								BPasswordStrengthBar(
									const BPasswordStrengthBar&);
			BPasswordStrengthBar& operator=(const BPasswordStrengthBar&);

			int32				fStrength;
			uint32				_reserved[3];
};


}	// namespace BAppearanceKit


#endif	// _PASSWORD_CONTROL_H
