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
#ifndef _CARD_STRIP_H
#define _CARD_STRIP_H


#include <Invoker.h>
#include <List.h>
#include <View.h>

#include "CardStripItem.h"


class BButton;


namespace BAppearanceKit {


// A horizontal row of cards of which one is selected: a picture, a title
// and a subtitle per card (BCardStripItem). The mouse and the left and
// right arrow keys choose a card; the strip then sends its message with
// "index" (int32) to its target. When the cards do not fit, the strip
// scrolls sideways (mouse wheel, the arrows at its ends, the keys). With no
// card it shows one dashed card with a text and, if set, a button. Drawn
// with the UI colours, so it follows dark mode.
class BCardStrip : public BView, public BInvoker {
public:
								BCardStrip(const char* name,
									BMessage* selectionMessage = NULL);
								BCardStrip(BMessage* archive);
	virtual						~BCardStrip();

	static	BArchivable*		Instantiate(BMessage* archive);
	virtual	status_t			Archive(BMessage* archive,
									bool deep = true) const;

			// The strip owns an added item and deletes it.
			bool				AddItem(BCardStripItem* item);
			bool				AddItem(BCardStripItem* item, int32 index);
			// The caller owns a removed item.
			BCardStripItem*		RemoveItem(int32 index);
			void				MakeEmpty();
			int32				CountItems() const;
			BCardStripItem*		ItemAt(int32 index) const;
			int32				IndexOf(const BCardStripItem* item) const;
			void				InvalidateItem(int32 index);

			// Selects without sending the message; -1 selects nothing.
			void				Select(int32 index);
			int32				CurrentSelection() const;
			void				ScrollToSelection();

			// The picture size a card shows, in pixels, for the current
			// font size: render pictures at this size.
			BSize				PictureSize() const;

			// The dashed card shown while the strip is empty.
			void				SetEmptyText(const char* title,
									const char* subtitle);
			// A button on that card; its message goes to the strip's
			// target. NULL label removes it.
			void				SetEmptyAction(const char* label,
									BMessage* message);

	virtual	void				AttachedToWindow();
	virtual	void				Draw(BRect updateRect);
	virtual	void				FrameResized(float width, float height);
	virtual	void				KeyDown(const char* bytes, int32 numBytes);
	virtual	void				MakeFocus(bool focus = true);
	virtual	void				MessageReceived(BMessage* message);
	virtual	void				MouseDown(BPoint where);
	virtual	BSize				MinSize();
	virtual	BSize				MaxSize();
	virtual	BSize				PreferredSize();

private:
			struct Data;

			void				_Init();
			float				_Unit() const;
			float				_Padding() const;
			float				_CardWidth() const;
			float				_CardHeight() const;
			float				_Gap() const;
			float				_ContentWidth() const;
			float				_MaxScroll() const;
			BRect				_CardFrame(int32 index) const;
			BRect				_Inner() const;
			void				_ScrollTo(float offset);
			void				_SelectByUser(int32 index);
			void				_LayoutButton();
			void				_DrawCard(BCardStripItem* item, BRect frame,
									bool selected);
			void				_DrawEmpty(BRect frame);

	virtual	void				_ReservedCardStrip1();
	virtual	void				_ReservedCardStrip2();
	virtual	void				_ReservedCardStrip3();
	virtual	void				_ReservedCardStrip4();

								BCardStrip(const BCardStrip&);
			BCardStrip&			operator=(const BCardStrip&);

			BList				fItems;
			int32				fSelected;
			float				fScroll;
			Data*				fData;
			uint32				_reserved[4];
};


}	// namespace BAppearanceKit


#endif	// _CARD_STRIP_H
