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
#ifndef _CARD_STRIP_ITEM_H
#define _CARD_STRIP_ITEM_H


#include <Archivable.h>
#include <String.h>


class BBitmap;


namespace BAppearanceKit {


class BCardStrip;


// One card of a BCardStrip: a picture, a title and one subtitle line. The
// picture is handed in and copied; the item does not load one. Not a view:
// the strip draws it.
class BCardStripItem : public BArchivable {
public:
								BCardStripItem(const char* title,
									const char* subtitle = NULL,
									const BBitmap* picture = NULL);
								BCardStripItem(BMessage* archive);
	virtual						~BCardStripItem();

	static	BArchivable*		Instantiate(BMessage* archive);
	virtual	status_t			Archive(BMessage* archive,
									bool deep = true) const;

			void				SetTitle(const char* title);
			const char*			Title() const;
			void				SetSubtitle(const char* subtitle);
			const char*			Subtitle() const;
			// Copied; NULL removes it. Best at BCardStrip::PictureSize().
			void				SetPicture(const BBitmap* picture);
			const BBitmap*		Picture() const;

private:
			void				_Changed();

	virtual	void				_ReservedCardStripItem1();
	virtual	void				_ReservedCardStripItem2();
	virtual	void				_ReservedCardStripItem3();
	virtual	void				_ReservedCardStripItem4();

								BCardStripItem(const BCardStripItem&);
			BCardStripItem&		operator=(const BCardStripItem&);

			BString				fTitle;
			BString				fSubtitle;
			BBitmap*			fPicture;
			BCardStrip*			fOwner;
			struct Data;
			Data*				fData;
			uint32				_reserved[4];

			friend class BCardStrip;
};


}	// namespace BAppearanceKit


#endif	// _CARD_STRIP_ITEM_H
