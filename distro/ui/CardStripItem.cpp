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


#include "CardStripItem.h"

#include <Bitmap.h>
#include <Message.h>

#include <new>

#include "CardStrip.h"


namespace BAppearanceKit {


struct BCardStripItem::Data {
};


static const char* kCardStripItemClassName = "BAppearanceKit::BCardStripItem";


static BBitmap*
copy_picture(const BBitmap* picture)
{
	if (picture == NULL || !picture->IsValid())
		return NULL;
	BBitmap* copy = new(std::nothrow) BBitmap(picture);
	if (copy != NULL && !copy->IsValid()) {
		delete copy;
		copy = NULL;
	}
	return copy;
}


BCardStripItem::BCardStripItem(const char* title, const char* subtitle,
	const BBitmap* picture)
	:
	fTitle(title),
	fSubtitle(subtitle),
	fPicture(copy_picture(picture)),
	fOwner(NULL),
	fData(NULL)
{
	for (int32 index = 0; index < 4; index++)
		_reserved[index] = 0;
}


BCardStripItem::BCardStripItem(BMessage* archive)
	:
	BArchivable(archive),
	fPicture(NULL),
	fOwner(NULL),
	fData(NULL)
{
	for (int32 index = 0; index < 4; index++)
		_reserved[index] = 0;
	if (archive == NULL)
		return;
	const char* text;
	if (archive->FindString("r5:title", &text) == B_OK)
		fTitle = text;
	if (archive->FindString("r5:subtitle", &text) == B_OK)
		fSubtitle = text;
	BMessage picture;
	if (archive->FindMessage("r5:picture", &picture) == B_OK) {
		BBitmap* bitmap = new(std::nothrow) BBitmap(&picture);
		if (bitmap != NULL && bitmap->IsValid())
			fPicture = bitmap;
		else
			delete bitmap;
	}
}


BCardStripItem::~BCardStripItem()
{
	delete fPicture;
	delete fData;
}


BArchivable*
BCardStripItem::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, kCardStripItemClassName))
		return NULL;
	return new(std::nothrow) BCardStripItem(archive);
}


status_t
BCardStripItem::Archive(BMessage* archive, bool deep) const
{
	status_t status = BArchivable::Archive(archive, deep);
	if (status != B_OK)
		return status;
	if (archive->ReplaceString("class", kCardStripItemClassName) != B_OK)
		status = archive->AddString("class", kCardStripItemClassName);
	if (status == B_OK)
		status = archive->AddString("r5:title", fTitle);
	if (status == B_OK)
		status = archive->AddString("r5:subtitle", fSubtitle);
	if (status == B_OK && deep && fPicture != NULL) {
		BMessage picture;
		status = fPicture->Archive(&picture);
		if (status == B_OK)
			status = archive->AddMessage("r5:picture", &picture);
	}
	return status;
}


void
BCardStripItem::SetTitle(const char* title)
{
	if (fTitle == title)
		return;
	fTitle = title;
	_Changed();
}


const char*
BCardStripItem::Title() const
{
	return fTitle.String();
}


void
BCardStripItem::SetSubtitle(const char* subtitle)
{
	if (fSubtitle == subtitle)
		return;
	fSubtitle = subtitle;
	_Changed();
}


const char*
BCardStripItem::Subtitle() const
{
	return fSubtitle.String();
}


void
BCardStripItem::SetPicture(const BBitmap* picture)
{
	delete fPicture;
	fPicture = copy_picture(picture);
	_Changed();
}


const BBitmap*
BCardStripItem::Picture() const
{
	return fPicture;
}


void
BCardStripItem::_Changed()
{
	if (fOwner != NULL)
		fOwner->InvalidateItem(fOwner->IndexOf(this));
}


void BCardStripItem::_ReservedCardStripItem1() {}
void BCardStripItem::_ReservedCardStripItem2() {}
void BCardStripItem::_ReservedCardStripItem3() {}
void BCardStripItem::_ReservedCardStripItem4() {}


}	// namespace BAppearanceKit
