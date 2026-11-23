# LibR5

Shared libraries that R5 adds beside `libbe`. `libbe` stays the Haiku Interface Kit. A program written for Haiku links `libbe` and does not need these libraries. Its ABI does not change because these libraries exist.

Do not move a call from here into `libbe`. Do not add a function here only to rename a `libbe` call. A helper belongs here when R5 wrote it and more than one program uses it.

The C++ is MIT, so any application can link these libraries. Code added to them must be MIT-compatible. A program that does not link it is unchanged. The icon pictures have their own notices in `docs/ICONS.md`.

The R5Book (`docs/r5book/index.html`) shows each part with a picture, what it is for, its members, and an example, in the layout of the Be Book. A change to a public call here updates its R5Book page in the same change. This file stays the contract.

## Libraries

| Library | Header | Source | What it is |
|---|---|---|---|
| `libr5icons.so` | `<R5Icons.h>` | `distro/icons` | Icons and icon buttons, line glyphs (`r5_glyph_draw()`) and level glyphs for signal and battery (`r5_level_glyph_draw()`). `docs/ICONS.md` |
| `libr5ui.so` | `<R5UI.h>` | `distro/ui` | The screen picture, cards, the sidebar, the page heading, the administrator lock (`<AdminLock.h>`), the window effects (`<WindowEffects.h>`), the tilt level and axis bar (`<TiltLevel.h>`, `<AxisBar.h>`), and the card strip (`<CardStrip.h>`). It links `libr5icons.so` because a locked sidebar row draws `R5_ICON_LOCKED` |
| `libr5wifi.so` | `<R5WiFi.h>` | `distro/wifi/lib` | The Wi-Fi panel (`WiFiView`), the connect window (`WiFiConnectWindow`), the Wi-Fi state machine (`WiFiController`), the network list, and the Wi-Fi glyphs. From air/OS, MIT. Regular image only. It links `libr5icons.so`, which draws the signal bars, and `libr5ui.so` for the password field and the glass. See "Wi-Fi Kit" below |
| `libr5admin.so` | `<R5Admin.h>` | `distro/admin` | Admin requests: a request that changes the machine goes to a service's own port, where the kernel names the sender (uid, group, team). Login, bluetooth_server, `libr5ui.so`, `libr5bt.so`, Users and bluetooth_audio use it. Both images. See "Admin requests" below |
| `libr5bt.so` | `<R5Bluetooth.h>` | `distro/bluetooth/lib` | The Bluetooth panel (`BluetoothView`), its controller (`BluetoothController`) and state. R5's own, MIT. Regular image only, because it needs `libbluetooth.so`. It links `libr5icons.so` (the Bluetooth glyph and the signal bars) and `libr5ui.so` (`BCard`). See "Bluetooth Kit" below |
| `libr5game.so` | `<GameController.h>`, `<GameControllerView.h>`, `<GameControllerMotionView.h>` | `distro/game/lib` | Game controllers: `BGameController` (the standard layout, name, vendor and product ID, battery, button labels, plug and unplug messages, rumble, trigger effects and motion), `BGameControllerView` (the live picture) and `BGameControllerMotionView` (tilt and turn). R5's own, MIT. Both images; the mapping database only in the regular image. It links `libr5icons.so` (the PlayStation glyphs) and `libr5ui.so` (`BTiltLevel`, `BAxisBar`). See "Game Kit" below |
| `libr5power.so` | `<PowerRoster.h>` | `distro/power/lib` | The Power Kit: `BPowerRoster` (batteries, the power source, the processor profile, sleep, the machine's power settings) and its watch message `kMsgPowerChanged`. R5's own, MIT; the battery reading is PowerStatus's former `ACPIDriverInterface`, moved here. Both images. It links `libr5admin.so` for the settings request. See "Power Kit" below |

`scripts/apply-icons.py` and `scripts/apply-r5ui.py` copy the sources into the Haiku tree, copy the header to `headers/os/interface/`, and add the library to the regular image and the minimum image. On a running system the files are `/system/lib/libr5icons.so` and `/system/lib/libr5ui.so`. The soname is the file name, with no version suffix, the same way `libbe.so` is named.

```
Application Example :
	Example.cpp
	: be libr5ui.so [ TargetLibstdc++ ]
	;
```

`SubInclude` for `src preferences r5ui` has to come before the program that links it.

Adding a function keeps old programs working. Changing a function's arguments, or removing a function, breaks programs already built against that soname. Do that only with a written note in this file and in `CHANGELOG.md`. The same rule is in `AGENTS.md`.

## Floating margin and tray style

Added 22 Nov 2026 for the Deskbar styles (`docs/design/DESKBAR-STYLES.md`), at the end of `<R5UI.h>`:

| Call | What it does |
|---|---|
| `r5_floating_margin()` | The margin, in pixels, a floating part of the desktop keeps from the screen edges: 14 at the default font size of 12, scaled with `be_plain_font`. Deskbar's "Keep away from the screen edges" uses it, so the margin is one R5 value, not a constant in Deskbar |
| `r5_deskbar_tray_style()`, `r5_deskbar_tray_color()` | How R5's own tray items draw their symbol: `R5_TRAY_STYLE_CLASSIC` (0, their own icon) or `R5_TRAY_STYLE_SYMBOLS` (1, one-colour line symbols in the colour returned). Read in the item's `Draw()`; outside Deskbar always classic |
| `r5_set_deskbar_tray_style(style, color)` | Deskbar sets it in its own process, then sends `R5_DESKBAR_TRAY_STYLE_CHANGED` (`'r5ts'`, with int32 `"style"` and the colour `"color"`, so an item can keep its own copy) to every tray view. Not for other programs |
| `r5_deskbar_tray_look()`, `r5_tray_look_update(look, message)` | External review, 23 Nov 2026. `struct r5_tray_look` holds the style and the colour together. A tray item takes `r5_deskbar_tray_look()` in `AttachedToWindow()`, then `r5_tray_look_update()` with each `R5_DESKBAR_TRAY_STYLE_CHANGED` (true when the message changed its copy), and draws from its own copy. The values behind all these calls change under one lock. Added at the end; nothing before it changed |

The rule for tray symbols, with an example, is in `docs/ICONS.md` ("Tray symbols").

## Screen picture

`r5_screen_draw()` draws the frame Settings uses for a screen. Notifications, Desktop and Deskbar, and Display all call it. The caller draws anything that sits on the glass, and any label outside the frame.

```cpp
#include <R5UI.h>

void
ExampleView::Draw(BRect updateRect)
{
	(void)updateRect;
	rgb_color fill = ui_color(B_CONTROL_BACKGROUND_COLOR);
	BRect content = r5_screen_draw(this, Bounds(), fill);
	r5_screen_caption(this, content, "21 dpi",
		make_color(255, 255, 255));
	// Draw a bar, a card, or nothing else inside content.
}
```

`bounds` is the area the picture may use. The picture keeps a 4:3 shape and is centered in that area. `contentColor` is the fill inside the frame. The returned rectangle is that fill, in the view's coordinates.

`r5_screen_caption()` draws one line centered in that rectangle. A NULL or empty string draws nothing. Text that does not fit is skipped. It does not draw a label under the frame. The caller does that in the area below `bounds`.

Pass `NULL` for the view and either call returns without drawing.

## Card

`BAppearanceKit::BCard` is a view with a group layout inside it. The header is `<Card.h>`, installed next to `<R5UI.h>`. It is not Haiku's `BCardView`. Children are added through `GetLayout()`, the same way as a `BGroupView`.

`B_CARD_DOCUMENT` fills with `B_DOCUMENT_BACKGROUND_COLOR`. `B_CARD_PANEL` fills with `B_PANEL_BACKGROUND_COLOR` tinted by `B_DARKEN_1_TINT`. The border is one pixel, `B_PANEL_BACKGROUND_COLOR` tinted by `B_DARKEN_2_TINT`. The corner radius is a quarter of `be_plain_font->Size()` (3 px at 12 pt). The inner margin is `B_USE_BIG_SPACING`. Every color comes from `ui_color()`.

`Archive()` and `Instantiate()` keep the style. The archived class name is `BAppearanceKit::BCard`.

```cpp
#include <Card.h>
#include <LayoutBuilder.h>
#include <StringView.h>

BAppearanceKit::BCard* card = new BAppearanceKit::BCard("info",
	BAppearanceKit::B_CARD_PANEL);
BLayoutBuilder::Group<>((BGroupLayout*)card->GetLayout())
	.Add(new BStringView("title", "Processors"))
	.Add(new BStringView("value", "2"));
```

## Sidebar item

`BAppearanceKit::BSidebarItem` is one row in the sidebar. The header is `<SidebarItem.h>`.

A row with a label and an id is a Home row, a category, or a page. An empty label and an empty id is a separator: it cannot be selected, and it draws one line in `B_LIST_BACKGROUND_COLOR` tinted by `B_DARKEN_2_TINT`.

`Update()` sets the height from `u = be_plain_font->Size() / 13`. Home is `u*8` of top padding plus a row of `u*34`. A separator is `u*13` (`u*6`, a 1 px line, `u*6`). Any other row is `u*36` plus a `u*2` gap.

`DrawItem()` draws the icon only on a top-level row, with `B_OP_ALPHA` and `B_PIXEL_ALPHA` / `B_ALPHA_OVERLAY`. The caller draws any wrench before `SetIcon()`. A child has no icon. Its text starts where the category name starts. Text that does not fit ends with an ellipsis.

Home and a selected row are bold. A category that is not selected uses a SemiBold face of the same family when that face exists, and bold otherwise. A child label is plain. The selection is a rounded rectangle in `B_STATUS_BAR_COLOR`, inset `u*6` from the left and right of the list, with corner radius `u*4` and text in `B_LIST_BACKGROUND_COLOR`. `B_LIST_SELECTED_BACKGROUND_COLOR` is gray in the default set (`{190, 190, 190}`), so it does not match the mockup. `B_STATUS_BAR_COLOR` is the R5 blue in the default set (`{50, 150, 255}`). A dark theme that changes the status bar changes this row too. That is accepted for now: the row follows one theme color, and that color is the one that already looks right. The row under the pointer gets the same rounded rectangle in 28/255 of `B_STATUS_BAR_COLOR` mixed into the list background; a separator, a disabled row and the selected row get no hover. The list redraws in full when its width changes (`B_FULL_UPDATE_ON_RESIZE`), so a scroll bar that hides at start leaves no old edge behind. A child that is not selected uses the list text mixed so that 80 percent of the color is the text and 20 percent is the list background. Category icons are `u*24`. Home draws a white circle of `u*24` (`B_LIST_BACKGROUND_COLOR`) and the pebble at `u*20` inside it. The circle is filled with `B_OP_COPY`, then the bitmap is drawn with alpha again, because the pebble's wave is a hole and a copy-mode blit turns that hole black or blue.

A folded category draws its count on the right at `u*11`, in `mix_color()` of the list background and the list text with amount 140 (the stand-in for `#64748b`). An unfolded category draws `B_DOWN_ARROW` there instead. A child with `B_SIDEBAR_BADGE_LOCKED` draws `R5_ICON_LOCKED` at `ComposeIconSize(B_MINI_ICON)`, with the bitmap alpha multiplied by 60 percent. The line under Home is `tint_color()` of the list background with `B_DARKEN_1_TINT` (the stand-in for `#e2e2e2`), inset `u*4`.

`Archive()` and `Instantiate()` keep the label, id, keywords, count, badge, emphasis, and icon. The archived class name is `BAppearanceKit::BSidebarItem`.

```cpp
#include <SidebarItem.h>

BAppearanceKit::BSidebarItem* home = new BAppearanceKit::BSidebarItem(
	"Home", "home", pebble);
home->SetEmphasized(true);
```

## Sidebar

`BAppearanceKit::BSidebar` is an outline list of `BSidebarItem` rows. The header is `<Sidebar.h>`. Categories are top-level items. Pages are added with `AddUnder()`. The scroll bar stays outside: the caller wraps the sidebar in a `BScrollView`.

`SetSingleExpand(true)` is the default. Unfolding one category folds the others. `ExpandOnly(NULL)` folds every category. While a filter is active, more than one category may stay unfolded.

`Draw()` paints one round rectangle behind an unfolded category and its pages. The fill is `mix_color()` of `B_LIST_BACKGROUND_COLOR` and `B_CONTROL_HIGHLIGHT_COLOR` with amount 40. The corner radius is a quarter of `be_plain_font->Size()`. The rows themselves draw the icon, the text, and the selection. The outline list's expander triangle is not drawn. `SetFooter()` draws a panel at the bottom of the list when the text is not empty and the rows leave room for it. The panel is inset `u*4`, padded `u*10`, and filled with `mix_color()` of the list background and `B_CONTROL_HIGHLIGHT_COLOR` at amount 18 (the stand-in for `#f3f6fb`). Its border is the same mix at amount 48 (the stand-in for `#dbe3ef`). The word "Tip:" is bold in `B_DOCUMENT_TEXT_COLOR`. The rest of the footer is `u*11` in `mix_color()` of the list background and the list text at amount 150 (the stand-in for `#475569`). That same mix, at amount 160, is the muted text on a card and under a page title. Card and heading borders use `tint_color()` of `B_PANEL_BACKGROUND_COLOR` with `B_DARKEN_2_TINT` (the stand-in for `#c4c4c4` and for the dashed `#b9b9b9`).

`ItemForId()` finds a row by its id. `SelectId()` unfolds the parent when there is one, selects the row, and scrolls it into view. It returns false when the id is missing. While it changes what is unfolded it clears the selection message, then puts that message back, so an arrow key does not open a page.

`SetFilter()` hides children whose label and keywords do not contain the text, ignoring case. A category with no remaining match is hidden. An emphasized row and a separator stay. Every category that still has a match is unfolded. An empty filter shows every row again and unfolds the category that was open before the filter. Hidden rows are kept and put back. They are not deleted.

`MouseDown()` on a category selects it and calls `ExpandOnly()`. `MouseDown()` on a page or on Home leaves the click to the outline list. `KeyDown()` moves with Up and Down and skips a separator. Right unfolds the selected category. Left folds it. Enter posts the selection message.

`Archive()` and `Instantiate()` keep whether one category unfolds at a time. The archived class name is `BAppearanceKit::BSidebar`. The rows are archived with the outline list.

```cpp
#include <Sidebar.h>

BAppearanceKit::BSidebar* list = new BAppearanceKit::BSidebar("sidebar");
list->AddItem(home);
list->AddItem(system);
list->AddUnder(display, system);
list->SetFilter("display");
```

## Action card

`BAppearanceKit::BActionCard` is a document card that sends a message. The header is `<ActionCard.h>`. It is a `BCard` with style `B_CARD_DOCUMENT` and a `BInvoker`. It has no child views. It draws itself.

The icon sits at the top left at `ComposeIconSize(40)`. The caller draws any wrench before `SetIcon()`. The title uses `be_bold_font`. The summary uses `be_plain_font`, wraps to at most two lines, and the second line ends with an ellipsis. The gap and the inner margin are `be_control_look->DefaultItemSpacing()`.

A tag is a small round label. Its text is `be_plain_font` at 0.85 of that size, bold. The fill is `mix_color()` of `B_DOCUMENT_BACKGROUND_COLOR` and the tag color with amount 30. The text is the tag color tinted by `B_DARKEN_3_TINT`. The default tag color is `B_CONTROL_HIGHLIGHT_COLOR`. Tags wrap onto the next line.

The card follows the pointer, the pressed button, and the keyboard focus. Hover mixes 10/255 of `B_STATUS_BAR_COLOR` (the list's selection blue) into the document fill, draws the border in `B_STATUS_BAR_COLOR`, and adds a soft inner ring of 70/255 of that blue, as in the mockup. Pressed mixes 26/255 into the fill. Focus uses `B_KEYBOARD_NAVIGATION_COLOR` for the border. The corner radius is a quarter of `be_plain_font->Size()`.

A click on a tag sends the tag message with `int32` `"be:tag"` and string `"be:label"`. A click on the rest of the card calls `Invoke()`. Space and Enter do the same. The card is navigable. `AttachedToWindow()` aims the invoker at the window when no target was set.

`GetPreferredSize()` and `MinSize()` follow the text and the tags. A width of 0 is treated as 280. The caller may still set an explicit minimum, and Settings does that at 280 wide.

`Archive()` and `Instantiate()` keep the title, summary, tags, tag color, icon, invoke message (`"r5:invoke"`), and tag message. The archived class name is `BAppearanceKit::BActionCard`.

```cpp
#include <ActionCard.h>

BAppearanceKit::BActionCard* card = new BAppearanceKit::BActionCard(
	"system", new BMessage('open'));
card->SetTitle("System");
card->SetSummary("Display, sound, notifications, virtual memory, and this computer.");
card->SetIcon(icon);
card->AddTag("Display");
```

## Page header

`BAppearanceKit::BPageHeader` is the heading card at the top of a page. The header is `<PageHeader.h>`. It is a `BCard` with style `B_CARD_DOCUMENT`. It has no child views.

`SetEyebrow()` with NULL or an empty string hides that line and leaves no gap. The eyebrow uses `be_plain_font` at 0.9 of that size. When the font family has a style named SemiBold, that style is used. Otherwise the line stays plain. The color is the document text mixed so that 60 percent is the text and 40 percent is the document background.

The title uses `be_bold_font` at 8 points above that font's size. The subtitle uses `be_plain_font`, mixed so that 70 percent is the document text and 30 percent is the document background. Each line is cut with an ellipsis in the width left of the icon.

The icon is drawn in a square of 48 units (`be_plain_font` size / 13 * 48), vertically centered, 16 units from the right edge. Hand in a bitmap of exactly that size, as Settings does: a bitmap of another size is stretched to the square and loses its sharp edges (CPU Info did that until 22 Nov 2026). The caller draws any wrench before `SetIcon()`. The preferred height follows the lines and the icon. The maximum width is unlimited and the maximum height is the preferred height.

`Invalidate()` runs on `B_COLORS_UPDATED` and `B_FONTS_UPDATED`. `Archive()` and `Instantiate()` keep the three lines and the icon. The archived class name is `BAppearanceKit::BPageHeader`.

```cpp
#include <PageHeader.h>

BAppearanceKit::BPageHeader* heading = new BAppearanceKit::BPageHeader("heading");
heading->SetEyebrow("System");
heading->SetTitle("Notifications");
heading->SetSubtitle("Manage alerts and notifications");
heading->SetIcon(icon);
```

## Screen arrangement

`BAppearanceKit::BScreenArrangement` draws one or more screens in their places on the desk, the way Display shows them. The header is `<ScreenArrangement.h>`. It is a `BView` and a `BInvoker`. A click, Space, or Enter sends the message from the constructor with `int32 "be:screen"` set to that screen's id. The target is the window unless `SetTarget()` names another. The view does not change the system. The caller applies a change.

```cpp
namespace BAppearanceKit {

struct screen_tile {
	int32		id;			// the caller's id for this screen
	BRect		frame;		// in desktop coordinates
	const char*	name;		// copied by SetScreens()
	bool		main;		// the Deskbar is on this screen
	bool		enabled;	// false draws it dimmed
};

class BScreenArrangement : public BView, public BInvoker {
public:
	BScreenArrangement(const char* name, BMessage* selectionMessage);

	status_t	SetScreens(const screen_tile* tiles, int32 count);
	int32		CountScreens() const;
	bool		ScreenAt(int32 index, screen_tile* tile) const;
	void		SetSelected(int32 id);
	int32		Selected() const;			// -1 when none
	void		SetDraggable(bool draggable);
	bool		IsDraggable() const;
	void		SetModificationMessage(BMessage* message);	// adopted
	BMessage*	ModificationMessage() const;
	void		SetCaption(const char* caption);	// NULL or "" hides it
	const char*	Caption() const;
	void		SetEnabled(bool enabled);
	bool		IsEnabled() const;
};

}
```

`SetScreens()` copies the tiles and returns `B_BAD_VALUE` for a negative count or a NULL list with a count. Tiles are kept in id order. `ScreenAt()` fills the caller's tile. Its `name` stays valid until the next `SetScreens()`. Arrow keys move the selection to the nearest screen in that direction.

The picture is filled with the list background mixed toward `B_CONTROL_HIGHLIGHT_COLOR` by 18, and stroked with the same mix at 48. That is the Search tip blue (`#f3f6fb` and `#dbe3ef` on the light theme). Each screen is `r5_screen_draw()`, filled with `B_DESKTOP_COLOR`. A screen with `enabled` false is filled with the panel background tinted `B_DARKEN_1_TINT`. The main screen then draws the Deskbar strip the same way Desktop and Deskbar does, following the running Deskbar. A filled circle sits in the glass with the number, starting at 1 in id order. The selected circle uses `B_STATUS_BAR_COLOR`. The others use the control border. The name is one bold line under the frame, cut with an ellipsis. A selected screen has a ring in `B_STATUS_BAR_COLOR` just outside the frame. Keyboard focus strokes the view in `B_KEYBOARD_NAVIGATION_COLOR`. The caption is one line at the bottom left, in the document background mixed toward the document text by 140 (`#64748b` on the light theme).

One screen is centered and keeps its own shape. Its height is at most 60 percent of the view. It is not stretched across the width. More than one screen are scaled together so the gaps stay, and the group is centered. `SetEnabled(false)` ignores mouse and keys. Settings uses that when the administrator lock is closed. Dragging is off unless `SetDraggable(true)` and there are two or more screens. On release, a screen snaps so its edge meets the next one, with no gap and no overlap. While the mouse is down the screen follows it. The modification message, adopted by `SetModificationMessage()`, then goes to the target with `int32 "be:screen"` and `BPoint "be:origin"` (the new top left in desktop coordinates).

Sizes follow `u = be_plain_font->Size() / 13`. The circle is `u*22`, and `u*26` when selected. The ring is `u*3` thick and sits `u*2` outside the frame. The name is `u*10`. The caption is `u*11`, `u*12` in from the left and `u*8` up from the bottom. The minimum size is `u*220` by `u*100`. The preferred height is 140 at a 12 point font.

`Archive()` and `Instantiate()` keep the tiles, the selection, the caption, and whether it can be dragged. The archived class name is `BAppearanceKit::BScreenArrangement`.

```cpp
#include <ScreenArrangement.h>

BAppearanceKit::BScreenArrangement* screens
	= new BAppearanceKit::BScreenArrangement("screens",
		new BMessage('pick'));
BAppearanceKit::screen_tile tile;
tile.id = 0;
tile.frame.Set(0, 0, 1279, 799);
tile.name = "Screen";
tile.main = true;
tile.enabled = true;
screens->SetScreens(&tile, 1);
screens->SetDraggable(false);
screens->SetCaption(NULL);
```

## User card

`BAppearanceKit::BUserCard` is a compact account card with an avatar, display name, and role badge. The header is `<UserCard.h>`. It is a `BControl` that sends a message when clicked or activated with Space or Enter.

```cpp
#include <UserCard.h>

BAppearanceKit::BUserCard* card = new BAppearanceKit::BUserCard(
	"user", "user", "User", "Administrator", avatarBitmap,
	new BMessage('usel'));
```

## User list item

`BAppearanceKit::BUserListItem` is a two-line item for `BListView` adhering to `docs/STYLE.md`. The header is `<UserListItem.h>`. It draws a circular avatar, the display name in bold on the first line, and `@login · role` on the second line.

```cpp
#include <UserListItem.h>

listView->AddItem(new BAppearanceKit::BUserListItem(
	"user", "User", "Administrator", avatarBitmap));
```

## Password control

`BAppearanceKit::BPasswordControl` is a password field with an integrated show/hide eye toggle button. The header is `<PasswordControl.h>`. The eye is `R5_GLYPH_EYE` while the password is hidden (a click shows it) and `R5_GLYPH_EYE_OFF` while it is shown. `SetPlaceholder("Enter password")` sets the gray text shown while the field is empty. `SetInvalid(true)` draws the border in `B_FAILURE_COLOR`. It is not a virtual method. `IsInvalid()` reads that flag. The flag is not archived. The field text is Manrope one step larger than body text (14 on 12), and the blue focus ring is drawn inside the control's own bounds. `Archive()` and `Instantiate()` keep whether the password is shown and the placeholder. They do not keep the password. The archived class name is `BAppearanceKit::BPasswordControl`.

```cpp
#include <PasswordControl.h>

BAppearanceKit::BPasswordControl* password = new BAppearanceKit::BPasswordControl(
	"password", "Password", "", new BMessage('sign'));
```

## Icon view

`BAppearanceKit::BIconView` shows one bitmap at its own pixel size. The header is `<R5IconView.h>` (Haiku already has a private `IconView.h`). The view is exactly as large as the bitmap and never scales it, so a vector icon rendered at the size it is shown stays sharp. Render the bitmap at that size, for example with `r5_vector_icon_bitmap()`, and do not stretch it with `DrawBitmap(bitmap, rect)`: a stretch of even one pixel makes the edges jagged. `SetIcon()` keeps a copy. The background is the parent's color.

```cpp
#include <R5IconView.h>

BBitmap* pebble = r5_vector_icon_bitmap(data, size, 44);
BAppearanceKit::BIconView* logo = new BAppearanceKit::BIconView("logo", pebble);
delete pebble;
```

## Accent button

`BAppearanceKit::BAccentButton` is the main action of a window: a button filled with the selection blue (`B_STATUS_BAR_COLOR`) with a bold label, white on a dark accent and black on a light one. The header is `<AccentButton.h>`. It is a `BButton`, so the message, the target, `MakeDefault()` and Enter work as before. It is 38 pixels high at a 12 point system font, and the label is one step larger than body text. It is as wide as its label; for the full row width set an explicit maximum width of `B_SIZE_UNLIMITED`. Use one per window. The other buttons stay plain `BButton`s.

```cpp
#include <AccentButton.h>

BAppearanceKit::BAccentButton* signIn = new BAppearanceKit::BAccentButton(
	"signin", "Sign in", new BMessage('sign'));
signIn->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
```

## Tilt level

`BAppearanceKit::BTiltLevel` (`<TiltLevel.h>`, added 17 Nov 2026) is a spirit level seen from above: a circle with a cross and a small centre ring, and a bubble in the accent colour (`B_CONTROL_HIGHLIGHT_COLOR`). `SetValue(x, y)` moves the bubble, each -1 to 1 (left to right, top to bottom; NaN is 0), clamped, and kept inside the circle; `X()` and `Y()` read it back. It redraws only when the value changed. It shows a value; the user does not edit it. The lines mix `B_PANEL_TEXT_COLOR` into the parent's colour, so it follows dark mode. Its size follows the plain font: at least 4, preferred 7.5, at most 10 line heights square. `SetEnabled(false)` draws it gray, the bubble included, as a disabled control; `IsEnabled()` reads it back (true at first). `Archive()` keeps the value (`"r5:x"`, `"r5:y"`) and a disabled state (`"r5:enabled"`). First user: `BGameControllerMotionView` (the tilt of a game controller); a second is any program that shows a two-axis value around a centre (a balance, a stick's position).

```cpp
#include <TiltLevel.h>

BAppearanceKit::BTiltLevel* level = new BAppearanceKit::BTiltLevel("tilt");
level->SetValue(motion.acceleration[0], motion.acceleration[2]);
```

## Axis bar

`BAppearanceKit::BAxisBar` (`<AxisBar.h>`, added 17 Nov 2026) is a horizontal rounded bar for a value from -1 to 1 that fills from the middle in the accent colour: to the right for a positive value, to the left for a negative one, with a tick at 0. `SetValue()` (clamped, NaN is 0) and `Value()`. For a value that has a centre: a turn rate, a stick axis, a balance. It is not a control (that is `BSlider`) and not progress (that is `BStatusBar`, which fills from the left). One line height high, at least 5 and preferably 14 line heights wide, as wide as the layout gives it. Dark mode as `BTiltLevel`. `SetEnabled()` and `IsEnabled()` as `BTiltLevel`: gray, the fill included, when disabled. `Archive()` keeps `"r5:value"` and a disabled state (`"r5:enabled"`).

```cpp
#include <AxisBar.h>

BAppearanceKit::BAxisBar* pitch = new BAppearanceKit::BAxisBar("pitch");
pitch->SetValue(motion.rotation[0] / 360);
```

What was checked first (`AGENTS.md`, LibR5): libr5ui had no level or centred bar; `r5_level_glyph_draw()` in libr5icons draws signal and battery steps, not a position. In Haiku, `BSlider` and `BChannelSlider` are controls the user drags, `BStatusBar` fills from the left only, and nothing draws a two-axis position; `BGameControllerView` draws sticks as part of its picture, not as a view of its own. So both are new, in libr5ui, because the Game Kit uses them and other programs can.

## Card strip

`BAppearanceKit::BCardStrip` (`<CardStrip.h>`, added 17 Nov 2026) is a horizontal row of cards of which one is chosen, a `BView` and a `BInvoker`. Each card is a `BAppearanceKit::BCardStripItem` (`<CardStripItem.h>`): a picture, a bold title and one smaller subtitle line in the quiet text colour. The item copies the picture it is handed and does not load one; `BCardStrip::PictureSize()` is the size a card shows at the current font size, so render pictures at that size. A click or the left and right arrow keys (Home, End) choose a card; the chosen one is filled lightly with the accent colour and has a 2-pixel accent border (the keyboard navigation colour while the strip has the focus). Then the strip sends its message with `"index"` (int32) to its target (the window unless `SetTarget()` says otherwise), once per change. `Select()` chooses without a message; -1 chooses nothing. When the cards do not fit, the strip scrolls sideways: the mouse wheel, an arrow at an end that hides cards, and the keys, which keep the chosen card in view. Without cards it draws one dashed card with `SetEmptyText(title, subtitle)` and, after `SetEmptyAction(label, message)`, a button whose message goes to the strip's target. Colours come from `ui_color()` (document background, document text, `B_STATUS_BAR_COLOR`), so it follows dark mode; sizes follow the plain font. It is as high as one card plus a margin and as wide as the layout gives it, at least one card.

| Call | What it does |
|---|---|
| `BCardStrip(name, selectionMessage)` | The strip; it owns the message |
| `AddItem(item)`, `AddItem(item, index)`, `RemoveItem(index)`, `MakeEmpty()`, `CountItems()`, `ItemAt()`, `IndexOf()`, `InvalidateItem()` | The cards. The strip owns an added item and deletes it; an item belongs to one strip; the caller owns a removed item. The selection stays on the same card when one before it comes or goes |
| `Select(index)`, `CurrentSelection()`, `ScrollToSelection()` | The chosen card, without a message |
| `PictureSize()` | The picture size of a card, in pixels |
| `SetEmptyText(title, subtitle)`, `SetEmptyAction(label, message)` | The dashed card while the strip is empty; `NULL` as the label removes the button |
| `BCardStripItem(title, subtitle, picture)`, `SetTitle()`, `SetSubtitle()`, `SetPicture()`, `Title()`, `Subtitle()`, `Picture()` | One card; a change redraws it in its strip |

`Archive()` keeps the items (`"r5:item"`, with the picture when `deep`), the selection, the message and the empty card; the button of the empty card is made again from `"r5:empty_label"` and `"r5:empty_msg"`. Both classes have a private data pointer, four reserved virtual functions and `_reserved[4]`. First user: Settings, Game controllers (one card per controller). Possible second users: the Bluetooth devices, the sound outputs.

```cpp
#include <CardStrip.h>

BAppearanceKit::BCardStrip* strip = new BAppearanceKit::BCardStrip(
	"controllers", new BMessage(kMsgChosen));
strip->SetEmptyText("No controller",
	"Plug one in with USB, or pair one in Bluetooth.");
strip->SetEmptyAction("Open Bluetooth\xe2\x80\xa6", new BMessage(kMsgBluetooth));
BSize size = strip->PictureSize();
BBitmap* picture = BGameControllerView::PictureBitmap(vendor, product, family,
	size.width, size.height, ui_color(B_DOCUMENT_TEXT_COLOR));
strip->AddItem(new BAppearanceKit::BCardStripItem("DualSense Wireless Controller",
	"Bluetooth \xc2\xb7 80 %", picture));
delete picture;
```

What was checked first (`AGENTS.md`, LibR5): Haiku has no horizontal `BListView`; `BListView`, `BOutlineListView` and `BColumnListView` are vertical lists. In libr5ui, `BCard` and `BActionCard` are single cards, and a row of `BActionCard`s has no selection, no keys and no scrolling; `BSidebar` is a vertical list of categories. So the strip is new, in libr5ui, because the Game controllers page uses it and the Bluetooth and Sound pages can.

## Account picture

`r5_avatar_draw(view, circle, icon, highlighted)` in `<R5UI.h>` draws a round plate with a thin ring and the icon in the middle. `highlighted` gives the light accent plate (an administrator, as on Sign in); otherwise the plate is gray and the picture is drawn in gray. Without an icon it draws a plain person outline. `r5_avatar_icon_side(circleSide)` is the icon size that is drawn pixel for pixel; any other size is filtered. `BUserCard`, `BUserListItem` and the Sign in window use it.

```cpp
float side = r5_avatar_icon_side(44);
BBitmap* people = r5_mime_icon_bitmap("application/x-vnd.Be-PEPL", side);
r5_avatar_draw(this, BRect(0, 0, 43, 43), people, true);
```


## Administrator lock

The machine-settings lock. The header is `<AdminLock.h>`. A program runs as the signed-in person. A change that needs an administrator goes to Sign in (Login, uid 0), which checks the grant and makes the change. An administrator starts unlocked. One unlock is a grant for that program for `kAdminGrantSeconds` (5 minutes). The password goes to Login in one message and is not logged or kept.

Since November 2026 this is part of `libr5ui.so` and is MIT. It was GPL-3.0-or-later and compiled into each program; the rights holder relicensed it so any program can use it, Device Manager included. Settings, Users, Time, Screen, Network, Printers, Repositories, VirtualMemory and Device Manager link it. Login's side (`distro/users/AccountAdmin.cpp`, `distro/login/DriverAdmin.cpp`) is MIT too since 12 Nov 2026; a program talks to it only through messages.

| Call | What it does |
|---|---|
| `r5_admin_query(administrator, unlocked)` | Asks Login. `unlocked` is true for an administrator or while a grant runs. Both are false when Login cannot be asked |
| `r5_admin_change_allowed()` | `unlocked` from `r5_admin_query()` |
| `r5_admin_prompt_unlock(target)` | Opens the unlock window. On success sends `kMsgAdminLockChanged` (`'r5ac'`) to `target` |
| `r5_admin_unlock(name, password, error)` | The same without a window |
| `r5_admin_relock()` | Ends this program's grant |
| `r5_admin_set_clock(seconds)`, `r5_admin_set_timezone(offset, name)`, `r5_admin_set_clock_gmt(gmt)` | The clock and time zone (Time, Settings) |
| `r5_admin_set_driver_blocked(driver, blocked, error)` | Turns a packaged driver under `/boot/system/add-ons/kernel/` off (`blocked` true) or on for the next start, in `/boot/system/settings/packages`. Login refuses drivers the system needs to start (the list of Haiku's Devices `IsCriticalDriver()`) |
| `r5_driver_settings_writable()` | True when this program may write the driver settings directory itself. That is `/boot/home/config/settings/kernel/drivers`, the directory `load_driver_settings()` reads in the kernel (`kUserSettingsDirectory`), not the signed-in person's home |
| `r5_driver_settings_read(file, key, value)` | The text after `key` on its line in that file. `B_NAME_NOT_FOUND` without the line. Reads the whole file (up to 1 MB) |
| `r5_driver_settings_write(file, keys, keyCount, lines, replaceFile)` | Replaces the lines that start with one of `keys` by `lines` and keeps the rest; with `replaceFile` the file is only `lines`. Writes a temporary file and renames it. `file` is a name in that directory, no `/` |
| `r5_admin_set_driver_setting(file, keys, keyCount, lines, replaceFile, error)` | The same write through Login, with the grant. Login only accepts the Media settings: `play_buffer_frames` and `record_buffer_frames` (128, 256) in `hda.settings` and `usb_audio`, `channel_layout` (1 to 16, `auto`) in `usb_audio`, and `virtual_surround` (`on`, `off`) |

`BAppearanceKit::BAdminLockBar` is the lock row: a label and an Unlock button. Views in its window that are not exempt are disabled while the lock is closed; `SetExempt(view, true)` keeps one usable (the person's own account card in Users). `AdminLockBar` is a typedef for it, the name the preferences have always used. The class keeps `_reserved[4]` so it can get fields later.

```cpp
#include <AdminLock.h>

BLayoutBuilder::Group<>(this, B_HORIZONTAL)
	.Add(fRevertButton)
	.AddGlue()
	.Add(new BAppearanceKit::BAdminLockBar());
```

The message codes Login answers are in `distro/ui/AdminLockProtocol.h` (private) and `distro/users/AccountAdmin.h`, with the same values. A code that shipped does not change.

Since November 2026 every one of these calls reaches Login through `r5_admin_send()` ("Admin requests" below). Login's normal messenger only answers `kMsgAdminQuery`; an unlock, a grant and every change are only taken on its admin port, so the grant belongs to the team the kernel names.

## Admin requests

`libr5admin.so`, header `<R5Admin.h>`, source `distro/admin`, MIT, in both images.

**Look here first** for any request that a service carries out because of who sent it (an administrator, uid 0, a program with an unlock grant). Do not decide by `BMessage::ReturnAddress()` or `IsSourceRemote()`: the reply messenger is whatever the sender chose, also the messenger of Login or of another uid 0 program. Login and bluetooth_server decided that way until November 2026, so any program could act as an administrator. The kernel gives the uid, group and team of the writer of each port message (`get_port_message_info_etc()`), and nobody can change those.

| Call | What it does |
|---|---|
| `r5_admin_port_start(name, handler, cookie, &port)` | Service: creates the admin port and its reading thread. For every request it calls `handler(request, sender, reply, cookie)` on that thread, one at a time, and sends `reply` back to the sender's reply port, only when that port belongs to the sender's team. The handler locks what it touches |
| `r5_admin_sender` | `uid`, `gid`, `team` of the sender, from the kernel |
| `r5_admin_port_reply(message, port)` | Service, in `MessageReceived()`: answers `R5_ADMIN_PORT_REQUEST` (`'r5AP'`) with int32 `"port"`. False for any other message |
| `r5_admin_port_stop(port)` | Service: deletes the port and waits for its thread. Not while holding the lock the handler takes |
| `r5_admin_sender_allowed(sender)` | Service: true for uid 0, a member of group `admin`, or a team with Login's grant (`/boot/system/settings/r5-machine-unlock`). The rule Login and bluetooth_server's switch use. Added 22 Nov 2026, at the end, for the power service |
| `r5_admin_send(service, request, reply, timeout)` | Client: asks `service` for its admin port, checks that the port belongs to the service's team, sends the request (at most 64 KB) and waits for the reply. The sent copy is cleared (it may hold a password) |

Services: Login (accounts, unlock, relock, clock, time zone, driver blocking and driver settings; its normal messenger only answers the query) and bluetooth_server (`BT_MSG_R5_SET_RADIO`, administrator or grant; `BT_MSG_R5_AUDIO_*`, uid 0) and the power service (`kMsgPowerSetSettings`, administrator or grant, through `BPowerRoster::SetSettings()`). The R5Book page is `docs/r5book/admin-kit.html`.

## Window effects

The shadow and the frosted glass of windows, in the GPU mode (`docs/APP-SERVER-COMPOSITOR.md`). The header is `<WindowEffects.h>`, namespace `BAppearanceKit`, in `libr5ui.so`. Free functions, like `ui_color()` and `set_decorator()`: each one sends one message to app_server over the program's own `BPrivate::AppServerLink` (codes `AS_R5_GET_WINDOW_EFFECTS` to `AS_R5_SET_WINDOW_EFFECT_STRENGTH`, at the end of `ServerProtocol.h`). Nothing is in `libbe`. `B_TRANSPARENT_COLOR` keeps its Haiku meaning ("do not erase") and never becomes glass. In the CPU mode every call is accepted and stored, and nothing changes on screen. A program that does not link `libr5ui.so` looks as it did.

| Call | What it does |
|---|---|
| `window_effects()` | The user's effects: `B_WINDOW_SHADOW` (`0x1`), `B_WINDOW_FROSTED_GLASS` (`0x2`), `B_WINDOW_ANIMATIONS` (`0x4`: windows fade in and out and the workspace switch slides). All set when app_server cannot be asked |
| `set_window_effects(effects)` | Sets them for the whole desktop, at once. app_server keeps them in its `appearance` file; a file without the keys means all three on. `B_BAD_VALUE` for an unknown bit. Settings and the Appearance program use it; a program normally does not |
| `window_effect_strength()` | `B_WINDOW_EFFECTS_QUIET` (1), `B_WINDOW_EFFECTS_BALANCED` (2, the default) or `B_WINDOW_EFFECTS_GLASSY` (3). One step sets the shadow and the glass together |
| `set_window_effect_strength(strength)` | Sets the step. `B_BAD_VALUE` outside 1 to 3 |
| `set_window_glass(window, glass)` | The window's background is glass: its top view gets the background of a glass view (below). Call it after the `BWindow` constructor |
| `set_view_glass(view, glass)` | One view's background is glass. The view must be attached and its window locked; the call syncs the window first, so app_server knows the view. `B_BAD_VALUE` for a view of another team or one that is not attached |
| `gpu_draws_text()` | M12 (Q13 of `docs/APP-SERVER-GPU-DRAWING.md`): `true` while the GPU draws the letters (Skia, `gpu_draw` on and the GPU mode active). Skia's own subpixel filter has no "average weight", so the Antialiasing tab of Settings and Appearance hides that slider then. `false` while Painter draws the letters, and when app_server cannot be asked. Code `AS_R5_GPU_DRAWS_TEXT`. Added at the end; nothing before it changed |
| `set_window_shadow(window, kind)` | Deskbar styles (22 Nov 2026). The shadow of the program's own window, GPU mode only: `B_WINDOW_SHADOW_DEFAULT` (0, as the decorator gives it, the default), `B_WINDOW_SHADOW_FULL` (1, the full soft shadow of the effect step whatever the focus, also for a window without a decorator), `B_WINDOW_SHADOW_EDGE` (2, the same without the offset down, for a window against a screen edge). `B_BAD_VALUE` for another kind or a window of another team. Code `AS_R5_SET_WINDOW_SHADOW`. Added at the end; nothing before it changed |
| `window_effects_active()` | True while the compositor shows the effects (the GPU mode); false in the CPU mode (Safe mode, no GPU) and when app_server cannot be asked. Deskbar asks it to choose between glass and its opaque surface. Code `AS_R5_WINDOW_EFFECTS_ACTIVE`. Added at the end |
| `B_WINDOW_EFFECTS_CHANGED` (`'r5we'`) | External review, 23 Nov 2026. app_server sends this message to every program when the effects change (`set_window_effects()`, `set_window_effect_strength()`) or when the compositor turns off (from the GPU to the CPU mode), as Haiku sends `B_COLORS_UPDATED` for colours. Fields: bool `"active"` (as `window_effects_active()`) and uint32 `"effects"` (as `window_effects()`). It reaches the application's looper; Deskbar draws its style again on it. Added at the end |
| `set_window_edge_line(window, color, width)` | The line along the outer edge of the program's own window, carried round the arc of its round corners by the compositor (stepped in the CPU mode, where app_server paints the stepped rim of a window without a decorator), for a window without a decorator line such as Deskbar's borderless window; the program draws the straight sides. A decorator's own line comes first. Width 0 (the default) is none, at most 4. Code `AS_R5_SET_WINDOW_EDGE_LINE`. Added at the end |
| `set_window_corner_radius(window, radius)` | Rounds the four corners of the program's own window, radius in pixels (0 square, the default; at most 64). The frame stays the same rectangle; the corners leave what the window shows and gets clicks for, in both modes (blended in the GPU mode, stepped in the CPU mode), and content in a corner is clipped. A window whose decorator returns an outline (decorator ABI 2) uses the decorator's. A round window gets the full soft shadow of the effect step, and the decorator's edge line runs on round the corner. Scale with the font size (18 at 12 points). `B_BAD_VALUE` for a negative radius or a window of another team. Code `AS_R5_SET_WINDOW_CORNER_RADIUS`. Added at the end; nothing before it changed |

What glass means for the pixels: app_server fills the view's background with its view colour and alpha 0. What the program draws on it with `B_OP_ALPHA` and `B_ALPHA_COMPOSITE` keeps its alpha (Painter's composite modes are made for a transparent target); every other drawing mode writes alpha 255 and stays opaque, as on a normal view, with antialiased edges blended against the view colour. The compose shows the view colour with the blur behind it showing through by the step's "see-through" value, and composite pixels over the blur by their alpha. Child views with their own colour stay opaque. In a window that has a glass view, a bitmap with alpha 0 drawn with `B_OP_COPY` is see-through there too, because the compose uses the alpha of the whole content area of such a window.

```cpp
#include <WindowEffects.h>

MyWindow::MyWindow()
	: BWindow(BRect(100, 100, 500, 400), "Glass", B_TITLED_WINDOW, 0)
{
	BAppearanceKit::set_window_glass(this, true);
}

void
MyView::Draw(BRect)
{
	// smooth text over the glass
	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
	DrawString("Hello", BPoint(20, 40));
}
```

Link `libr5ui.so`. `r5-effects-test` (in `/system/bin`) is a small user of every call: `r5-effects-test shadow off`, `r5-effects-test strength glassy`, `r5-effects-test glass 30`.

## Themes

Added 23 Nov 2026 (`docs/design/THEMING.md`). The header is `<Themes.h>`, namespace `BAppearanceKit`, in `libr5ui.so`. A theme is a saved combination of parts that exist on their own (the decorator, the control look, the scroll bars, the Deskbar style, the mode with an accent, the window effects); applying one sets each part the way Appearance and Desktop and Deskbar do. There is no drawing engine. A `theme_info` struct with free functions, in the style of Haiku's `menu_info` and `scroll_bar_info`; the struct has reserved space at the end. Nothing is in `libbe`.

| Call | What it does |
|---|---|
| `get_theme(info)` | The theme in use. Every part is read from the system (another page or program may have changed it); `~/config/settings/r5/theme`, written by `set_theme()`, adds the theme's name and base, its accent (while the mode is the same) and the R5 Modern scroll bar style (while the controls are the same). Before any theme was applied, the name is the built-in theme the system matches, else "R5" |
| `set_theme(info, parts)` | Applies `parts` (`B_THEME_WINDOWS`, `B_THEME_CONTROLS`, `B_THEME_SCROLL_BARS`, `B_THEME_DESKBAR`, `B_THEME_COLORS`, `B_THEME_EFFECTS`, `B_THEME_ALL_PARTS`) at once and keeps `info` as the theme in use. The colours are set when the colours or the controls change. When the controls or the scroll bar style change, every running program takes the new look without a restart (below). `B_BAD_VALUE` for a bad name; otherwise the first error of a part (the other parts are still applied, and a part that failed is not kept as the one in use). An add-on name with a `/` is refused, so a theme file can only name add-ons in the add-ons folders |
| `count_themes()`, `get_theme_at(index, info)` | The built-in themes first (R5 Modern, R5, Classic (Haiku)), then the saved ones by name |
| `find_theme(name, info)` | A built-in or saved theme by name; `B_ENTRY_NOT_FOUND` |
| `is_built_in_theme(name)` | True for the three built-in names, in any case |
| `save_theme(info, parts)` | Writes `parts` of `info` to `~/config/settings/r5/themes/<info->name>`, replacing a saved theme of that name. `B_NOT_ALLOWED` for a built-in name; `B_BAD_VALUE` for an empty name, one with `/`, `"` or `\`, or no parts |
| `remove_theme(name)` | Deletes a saved theme; `B_NOT_ALLOWED` for a built-in one |
| `theme_matches(theme, base)` | True when `theme` shows the same as `base` for every part `base` holds; the Theme page shows "(changed)" when it is false |
| `get_theme_colors(info, colors)` | The `ui_color` set of a theme's mode, accent and controls, as `set_ui_colors()` takes it. Every colour a theme sets is in it, so one set replaces another completely; the desktop colour is not |
| `count_theme_accents()`, `theme_accent_at(index, mode)`, `theme_accent_name(index)` | The accent swatches of the Theme page (Blue, Teal, Purple, Pink, Orange, Graphite), a little lighter in dark mode |

`theme_info` fields: `name`, `base` (the theme it started from), `parts`, `decorator` (the add-on's file name, `""` for Haiku's built-in Classic (Haiku)), `control_look` (the add-on's file name, `""` for Haiku's controls), `modern_scroll_bars`, `double_arrows`, `deskbar_style` (`B_THEME_DESKBAR_R5`, `B_THEME_DESKBAR_CLASSIC`), `deskbar_layout` (`B_THEME_DESKBAR_VERTICAL`, `_COMPACT`, `_HORIZONTAL`), `deskbar_rounded`, `deskbar_floating`, `mode` (`B_THEME_LIGHT`, `B_THEME_DARK`), `accent` (alpha 0: the colour set's own), `effects`, `effect_strength`.

The file is driver settings text (`name`, `base`, `windows`, `controls`, `scroll_bars`, `arrows`, `deskbar_style`, `deskbar_layout`, `deskbar_rounded`, `deskbar_floating`, `mode`, `accent`, `shadow`, `glass`, `animations`, `strength`); a key that is not there is a part the theme does not hold.

Live switching: after it set the control look, `set_theme()` sends `B_COLORS_UPDATED` with an int64 `"r5:control_look"` to every program (`BRoster::Broadcast()`). R5's `BWindow` loads the control look app_server names when it sees a new value, and draws again. The previous look stays in memory until the program quits. A program does not have to do anything.

Dark mode for a program: use `ui_color()` (and `SetViewUIColor()`, `SetHighUIColor()`) and `be_control_look`, and avoid fixed colours; where a colour of its own is needed, decide light or dark with `ui_color(B_PANEL_BACKGROUND_COLOR).IsDark()`. The Settings pages and the controls use `ui_color`; Deskbar's Default (R5) style and the R5 decorators decide with `IsDark()` and then use the mockup's own light or dark values.

```cpp
#include <Themes.h>

using namespace BAppearanceKit;

theme_info theme;
if (find_theme("R5 Modern", &theme) == B_OK) {
	theme.mode = B_THEME_DARK;
	set_theme(&theme, theme.parts | B_THEME_COLORS);
}
```

Link `libr5ui.so`. `r5-effects-test theme list|get`, `theme <name> [light|dark]`, `theme mode light|dark` use these calls.

## Level glyphs

`libr5icons.so`, `<R5Icons.h>`. The full contract with the pictures is in `docs/ICONS.md` ("Level glyphs"); the R5Book page is `docs/r5book/icons-kit.html#r5_level_glyph`.

```
void r5_level_glyph_draw(BView* view, r5_level_glyph which, BRect frame,
	int32 level, rgb_color foreground, rgb_color background,
	r5_level_style style);
```

`which` is `R5_LEVEL_SIGNAL` (1, four rising bars) or `R5_LEVEL_BATTERY` (2, a battery with four segments). `level` is how many of the four steps are filled; values outside 0 to 4 are clamped. The empty steps are a faint tint of `foreground` over `background`. `style` is `R5_LEVEL_STYLE_NORMAL` (0), `R5_LEVEL_STYLE_DISABLED` (1, greyed) or `R5_LEVEL_STYLE_OFF` (2, outlines only). The view's drawing state is the same afterwards. `DrawWiFiSignalBars()` uses it. The line glyph for Bluetooth is `R5_GLYPH_BLUETOOTH` (7) with `r5_glyph_draw()`.

## Wi-Fi Kit

`libr5wifi.so`, headers in `headers/os/net/`: `<R5WiFi.h>` (`WiFiController`, `WiFiState`, `WiFiNetworkInfo`, `WiFiLinkDetails`, the message codes), `<R5WiFiView.h>` (`WiFiView`), `<R5WiFiListView.h>` (`NetworkListView`, `SignalView`), `<R5WiFiJoinOther.h>` (`JoinOtherWindow`), and the header-only `<R5WiFiGlyphs.h>`, `<R5WiFiIconPaths.h>` and `<R5WiFiNetworkList.h>`. The R5Book page is `docs/r5book/wifi-kit.html`.

The code is air/OS's WiFi preferences (MIT, jmgasper; `THIRD_PARTY_NOTICES.md`). R5 moved the window's content into `WiFiView` so that the WiFi program (`distro/wifi/app`) and the Wi-Fi page in Settings (`distro/settings/WiFiModule.cpp`) share one panel, renamed the headers with an `R5WiFi` prefix, and gave `WiFiController` and `WiFiView` `_reserved[8]`. `WiFiState` and the structures it holds are read with `Unarchive()`; fields are only added at the end. `WiFiLinkDetails` is embedded in `WiFiState` before `networks`, so a field added to `WiFiLinkDetails` moves `networks`. Programs read the state from the message (`Unarchive()`), not by copying the struct, and ignore message fields they do not know. A new `WiFiState` field goes after `networks`. A new `WiFiLinkDetails` field goes at the end of that struct, and the message name is new.

| Call | What it does |
|---|---|
| `WiFiView(bool settingsPage = false)` | The whole panel. It starts a `WiFiController` when attached to a window and stops it when detached. `settingsPage` drops the icon and insets and leaves the window's default button alone |
| `WiFiController(target, component, scanInterval)`, `Start()` | The state machine on its own thread. Stop it with `B_QUIT_REQUESTED` |
| `kMsgWiFiScan`, `kMsgWiFiJoin`, `kMsgWiFiLeave`, `kMsgWiFiForget`, `kMsgWiFiSetPower`, `kMsgWiFiSelectDevice`, `kMsgWiFiRefresh`, `kMsgWiFiSetScanInterval` | Requests to the controller. The fields are in the R5Book page |
| `kMsgWiFiStateChanged`, `kMsgWiFiJoinResult` | What the target receives |
| `DrawWiFiTrayIcon()`, `DrawWiFiSignalBars()`, `DrawWiFiLock()` | Header only. `DrawWiFiSignalBars()` calls `r5_level_glyph_draw()`, so a program that uses it links `libr5icons.so` |
| `WiFiLinkDetails` `standard`, `width`, `streams`, `rxKbps` | Added at the end. `standard` is `"Wi-Fi 4"`, `"Wi-Fi 5"` or `"Wi-Fi 6"`, or empty. `width` is MHz. `streams` is the spatial streams of the last transmit. `rxKbps` is the receive rate, or 0: the MT7921 driver does not report one yet. Older readers ignore the message fields `"standard"`, `"width"`, `"streams"` and `"rx kbps"`. Since 17 Nov 2026, `standard` is only what the link uses; a link without HT, VHT or HE leaves it empty |
| `WiFiLinkDetails` `routerStandard` | Added at the end (17 Nov 2026). What the access point offers, from its beacon: `"Wi-Fi 4"`, `"Wi-Fi 5"`, `"Wi-Fi 6"`, or empty. It can be newer than `standard`. The stack answers it (`IEEE80211_IOC_HAIKU_ROUTER`, `0x6007`), so it works with every FreeBSD-stack driver. `WiFiView` shows it only in the tool tip of the current network ("Router: Wi-Fi 6", plus "Your router supports Wi-Fi 6." when the link uses less), never in the status line. Message field `"router standard"` |
| `WiFiLinkLine(link)` | `"Wi-Fi 6 · 600 Mbit/s"`. Uses `rxKbps` when it is set, otherwise `txKbps`. Empty when both the standard and the rate are unknown |
| `r5_wifi_link_line(line)` | The same line from the associated wireless adapter. It does not start a `WiFiController`. `B_ERROR` when nothing is associated or nothing to show. The Deskbar network menu calls it, so that replicant links `libr5wifi.so` for this one line |
| `WiFiConnectWindow` | One connect window for the panel and the Deskbar. Title "Connect to Wi-Fi". The password field is `BAppearanceKit::BPasswordControl`. `BAppearanceKit::set_window_glass(window, true)` runs as soon as the `BWindow` exists. Connect automatically is on. The password is stored by `WiFiController` after the link is up, and only when that box is on |
| `r5_wifi_security_name(authentication)` | The translated security name used by the panel and the Deskbar |
| `r5_wifi_connect_subtitle(subtitle, authentication, bars, routerStandard)` | Security, a signal word from `bars` (0 to 4), and `routerStandard` when that pointer is set |
| `r5_wifi_connect_dialog(device, name, subtitle, authentication, bars)` | Blocks until the join succeeds (`B_OK`) or the window is closed (`B_CANCELED`). A second call returns `B_BUSY`. The Deskbar and the Wi-Fi program use it. It starts its own `WiFiController`. A timeout says the router was not reached. The red line and the red field border are used when the result carries bool `"key_failed"`. The supplicant sends that on a 4-way mismatch and on a rejected SAE confirm |
| `kMsgWiFiConnectClosed` | The connect window tells `WiFiView` it has closed, so the view drops its pointer |

`BNetworkRoster::AddPersistentNetwork(const wireless_network&, const char* password)` is an addition to Haiku's `libbnetapi.so` (air/OS), not to this library; the one-argument call stays. `scripts/apply-wireless.py` copies the library, the WiFi program and the headers, and adds `libr5wifi.so` and WiFi to the regular image.

## Bluetooth Kit

`libr5bt.so`, header `<R5Bluetooth.h>` in `headers/os/interface/` (beside `R5Icons.h` and `Card.h`). The plan is `docs/BLUETOOTH.md` (step 3a). The R5Book page is `docs/r5book/bluetooth-kit.html`.

The classes are top-level, like the Wi-Fi Kit, not in `namespace BAppearanceKit`. They sit on top of Haiku's `libbluetooth.so` and `bluetooth_server`, which Haiku programs keep using as they are. `BluetoothController` and `BluetoothView` had `_reserved[8]`; the controller now keeps the last paired-device answer there (`_reserved[6]` left) and the view its pairing status and My devices (`_reserved[3]` left). `BluetoothState` and the structures it holds are read with `Unarchive()`; fields and message names are only added at the end.

| Call | What it does |
|---|---|
| `BluetoothView(bool settingsPage = false)` | The whole panel: the box My devices (the paired devices, each with Connected or Not connected, and beside the list a `BCard` panel with the chosen device's kind, status, Classic or Low Energy, address, and Disconnect and Forget); the box Devices nearby (the scan, without the paired devices) with Pair and Scan, down to the last line; and that last line, without a box: the check box "Bluetooth on", the radio in use or a note in a fainter color, and a radio menu when there is more than one radio. While Bluetooth is off both lists are drawn faint. Each device row draws `r5_level_glyph_draw(R5_LEVEL_SIGNAL)` when the radio reported a signal strength. It starts a `BluetoothController` when attached to a window and stops it when detached. `settingsPage` drops the window insets |
| `BluetoothView::SetRadioSwitchAllowed(bool)` | The switch is system-wide. A program keeps it disabled while `r5_admin_change_allowed()` is false; the rest of the panel stays usable. On by default. The bold titles are named `"my devices title"`, `"devices title"` and `"device name"`, so a host can give them its own bold font |
| `BluetoothController(target)`, `Start()` | The controller on its own thread. It reads the switch and the radios by `hci_id` from `bluetooth_server` every 3 seconds, with a 2-second timeout, and acquires only radios it does not know yet (`LocalDevice::GetLocalDevice(hci_id)`). `bluetooth_server` is a system service (uid 0) that launch_daemon starts at boot and again after it stops. When it is not running the controller asks launch_daemon once to start its job (`BLaunchRoster::Start()`; `BRoster::Launch()` would start a second server as the person signed in) and reports `BLUETOOTH_SERVICE_STOPPED` while it is gone. Stop it with `B_QUIT_REQUESTED`: libbluetooth's own calls have no timeout, so a window must not `Lock()` it |
| `kMsgBluetoothScan`, `kMsgBluetoothCancelScan`, `kMsgBluetoothSetRadio` (`"on"`), `kMsgBluetoothSelectRadio` (`"hci_id"`), `kMsgBluetoothRefresh`, `kMsgBluetoothPair` (`"bdaddr"`, `"name"`, `"class"`; added in step 4: pairs with a device from the scan through Create Connection, like Haiku's `RemoteDevice::Connect()`, and stops a running scan first), `kMsgBluetoothForget` (`"bdaddr"`, `"name"`; forgets a paired device's keys and ends its link), `kMsgBluetoothDisconnect` (`"bdaddr"`; ends the link and keeps the keys), `kMsgBluetoothUseForSound` (`"bdaddr"`, `"use"`; writes bluetooth_audio's choice for this person and sets "Bluetooth audio" as the Media output, or clears the choice and sets the first other audio output) | Requests to the controller |
| `kMsgBluetoothStateChanged`, `kMsgBluetoothRadioResult` (`"on"`, `"status"`), `kMsgBluetoothPairResult` (`"paired"`, `"name"`; added in step 6: how a `kMsgBluetoothPair` ended, from `bluetooth_server`; `BluetoothView` shows it in its status line), `kMsgBluetoothForgetResult` (`"forgotten"`, `"name"`) | What the target receives |
| `BluetoothPairedDevice` (`info`, `lowEnergy`, `hid`, `connected`, `Kind()`; added at the end: `battery`, `IsAudio()`, `IsGameController()`), `BluetoothState::UnarchivePaired(const BMessage&, std::vector<BluetoothPairedDevice>&)`, `BluetoothState::UnarchiveSoundDevice(const BMessage&, bdaddr_t&)` | `battery` is percent, or -1: LE devices with the Battery Service report it while connected (read once at connect, then by notification), and Classic headsets over HFP while their HFP session runs (since step 7b). `UnarchiveSoundDevice()` gives the headset this person's sound goes to (`"sound:address"`). | My devices. The controller asks `bluetooth_server` every 3 seconds (`BT_MSG_R5_GET_PAIRED`: names and states, never keys) and adds the answer to `kMsgBluetoothStateChanged` as `"paired:*"` fields, so `BluetoothState` keeps its size. A dual-mode device paired both ways is one entry. `Kind()` is the class-of-device kind, or "Input device" for LE HID |
| `BluetoothDeviceInfo::Kind()`, `DisplayName()`, `AddressString()`, `SignalLevel()` | The kind from the class of device (Bluetooth Assigned Numbers, "Class of Device"); the name, else the kind, else the address; `00:11:22:33:44:55`; 0 to 4 bars, 10 dB per bar from -90 dBm (one bar) to -60 dBm (four bars), R5's display choice, or -1 without a signal strength |

A scan is an inquiry of 10 × 1.28 s, libbluetooth's default (`BT_DEFAULT_INQUIRY_TIME`), through `DiscoveryAgent::StartInquiry()` and a `DiscoveryListener`, as Haiku's Bluetooth preferences do. `LocalDevice` and `DiscoveryAgent` have private destructors in libbluetooth, so the controller acquires each radio once and keeps it; the `RemoteDevice` objects of a scan are deleted when the next scan starts or the controller stops. Every inquiry answer is reported, the repeats too (a message filter on the listener), so a device's signal bars follow its newest strength. Remote names are only what the device sent in the inquiry; the controller does not ask a device for its name.

The switch: HCI has no power-off command for a radio. Off means that `bluetooth_server` writes Write Scan Enable `0x00` (no inquiry scan, no page scan) to every radio, refuses inquiry, periodic inquiry, Create Connection and Remote Name Request, refuses a Write Scan Enable that is not `0x00`, refuses `BT_REQ_CREATE_CONN`, and rejects incoming connection requests (Reject Connection Request, reason `0x0D`). The choice is in `B_SYSTEM_SETTINGS_DIRECTORY/bluetooth/r5_radio` (`radio on` or `radio off`, mode 0600), written by `bluetooth_server` through a temporary file and a rename, so it survives a restart. The server is a system service (uid 0, `distro/bluetooth/launch/bluetooth`, regular image only), so `/boot/system/settings/bluetooth` is root's alone, mode 0700; the pairing keys are there too (`paired_devices`, 0600), one set for everyone who signs in. On allows scans and connections again and does not change the scan mode. The server messages are `BT_MSG_R5_GET_RADIO` (the switch and the radios by `hci_id`) and `BT_MSG_R5_SET_RADIO` in `bluetoothserver_p.h`. The server carries out `BT_MSG_R5_SET_RADIO` only for a sender that may change a machine setting: uid 0, a member of group `admin`, or a team with a grant in Login's `/boot/system/settings/r5-machine-unlock` (the rule of `sender_may_change()`); otherwise it answers `B_NOT_ALLOWED`.

`RemoteDevice::GetRSSI()` is an addition to Haiku's `libbluetooth.so` (R5, MIT snippet), not to this library. In the same change both `RemoteDevice` constructors set the signal strength to `HCI_RSSI_INVALID` and the name to incomplete; Haiku left them unset. `scripts/apply-bluetooth.py` copies the library and the header, and adds `libr5bt.so` to the regular image.

## Game Kit

`libr5game.so`, headers `<GameController.h>` and `<GameControllerView.h>` in `headers/os/device/` (beside `Joystick.h`). The plan and the license check of the mapping database are `docs/GAME-CONTROLLERS.md` (option B). The R5Book page is `docs/r5book/game-kit.html`. Both images have the library in `/system/lib`; the soname is `libr5game.so`. `scripts/apply-game.py` copies it.

The classes are top-level, like the Bluetooth Kit. `BJoystick` in `libdevice` is unchanged (apart from RenkuOS `244bf43292`, which frees the data of an earlier `Open()`), and a program that only uses `BJoystick` is unchanged. `BGameController` and `BGameControllerView` have a private data pointer, four reserved virtual functions and `_reserved[8]` and `_reserved[4]`; calls and enum values are only added at the end.

| Call | What it does |
|---|---|
| `CountDevices()`, `GetDeviceName(index, name, size)`, `RescanDevices()` | The controllers under `/dev/joystick` and its directories, by the name `Open()` takes (`usb/0`, `bluetooth/0`), sorted |
| `Open(deviceName)`, `Close()`, `IsOpen()`, `Update()` | As `BJoystick`: `Open()` asks the driver what the controller is (`R5_JOYSTICK_GET_INFO`), finds its mapping and opens it through `BJoystick` in enhanced mode; `Update()` reads it once, every frame |
| `IsButtonDown(game_controller_button)`, `ButtonValues()`, `AxisValue(game_controller_axis)`, `HasMapping()`, `HasButton()`, `HasAxis()` | The standard layout in SDL's order: `B_CONTROLLER_BUTTON_A` (0, the lower face button), `_B`, `_X`, `_Y`, `_BACK`, `_GUIDE`, `_START`, `_LEFT_STICK`, `_RIGHT_STICK`, `_LEFT_SHOULDER`, `_RIGHT_SHOULDER`, `_DPAD_UP`, `_DPAD_DOWN`, `_DPAD_LEFT`, `_DPAD_RIGHT`, `_MISC1`, `_PADDLE1` to `_PADDLE4`, `_TOUCHPAD` (20); `B_CONTROLLER_AXIS_LEFT_X` (0), `_LEFT_Y`, `_RIGHT_X`, `_RIGHT_Y`, `_LEFT_TRIGGER`, `_RIGHT_TRIGGER` (5). Sticks -32768 to 32767, left and up negative; triggers 0 to 32767. Without a mapping the buttons stay up and the axes at 0 |
| `CountRawAxes()`, `RawAxisValue()`, `CountRawButtons()`, `IsRawButtonDown()`, `CountRawHats()`, `RawHatValue()` | What the controller reports, in the driver's order. A hat is 0 (centered), 1 (up), 3 (right), 5 (down), 7 (left), the even numbers in between |
| `Name()`, `DeviceName()`, `VendorID()`, `ProductID()`, `Connection()`, `Family()`, `BatteryLevel()` | The product name (the driver's, else the database's, else "Game controller"); the IDs (0 when the driver does not know them); `B_GAME_CONTROLLER_CONNECTION_USB`, `_BLUETOOTH` or `_OTHER`; `B_GAME_CONTROLLER_FAMILY_UNKNOWN` (0, no mapping), `_XBOX`, `_PLAYSTATION`, `_NINTENDO`, `_SEGA_GENESIS` (4); the battery in percent, or -1 |
| `LabelStyle()`, `static LabelStyleFor(vendor, product)`, `static SetLabelStyle(vendor, product, labels)`, `static ResetLabelStyles()` | The face-button labels the person chose for a controller model: `B_GAME_CONTROLLER_LABELS_AUTOMATIC` (0, the family's own), `_XBOX` (1, A B X Y) or `_PLAYSTATION` (2, cross, circle, square, triangle). `LabelStyle()` never returns Automatic. Stored in `B_USER_SETTINGS_DIRECTORY/game_controllers` (a flattened `BMessage`, `"labels vvvv:pppp"` as int32). Only Settings calls the setters |
| `static StartWatching(BMessenger)`, `static StopWatching(BMessenger)` | `B_GAME_CONTROLLER_ADDED` (`'gcad'`) and `B_GAME_CONTROLLER_REMOVED` (`'gcrm'`) with the string `"device"`. One looper per team watches `/dev/joystick` and its directories with node monitoring and compares the device list after every change |
| `static LayoutFor(vendor, product)`, `static SetLayout(vendor, product, family)` | Added after step 8. The layout the person chose for a model: `B_GAME_CONTROLLER_FAMILY_UNKNOWN` is automatic (the database); `_XBOX`, `_PLAYSTATION`, `_NINTENDO` or `_SEGA_GENESIS` give that shape and a fixed mapping in the HID order of that family's controllers (below). `"layout vvvv:pppp"` (int32) in the same file. Only Settings calls the setter |
| `static GetSavedMapping(vendor, product, text, size)`, `static SetSavedMapping(vendor, product, text)`, `HasSavedMapping()` | Added after step 8. A setup made button by button (Settings, "Set up buttons…"): SDL's mapping keys, `"a:b0,b:b1,dpup:h0.1,leftx:a0,+righttrigger:a4,"`; buttons and hats as the raw calls number them, axes in the order of `RawAxisValue()`, `+`/`-` half an axis, `~` turned round. `NULL` or `""` removes it; text that does not parse is `B_BAD_VALUE`; none saved is `B_ENTRY_NOT_FOUND`. `"mapping vvvv:pppp"` (string) in the same file. `HasSavedMapping()`: the open controller uses one |
| `BGameControllerView(name)`, `SetDevice(deviceName)`, `Controller()`, `SetFamily(family)`, `Family()`, `SetLabels(labels)` | The picture of the controller's model, chosen by vendor and product ID (since 17 Nov 2026; `docs/GAME-CONTROLLERS.md`, section 7): the Gamepad Line Art Pack (CC0) and R5's own Genesis / M30 drawing, as filled outlines (even-odd, no stroked `BShape`), scaled to the view, in the document text color mixed toward the background, so dark mode works; below 250 pixels wide the line is drawn thicker. With a device it reads the controller every 16 ms while attached to a window and draws only when something changed; a pressed control fills with `B_STATUS_BAR_COLOR` under the line, a pressed stick fills its cap, a moved stick shows a ring in that color off centre, and analog triggers are small caps over the shoulders with "L2 60%" beside them (not for the M30 and the Joy-Cons). Without a device it draws the still picture of a family (DualSense, Xbox Series, Switch Pro, M30); `B_GAME_CONTROLLER_FAMILY_UNKNOWN` and a controller without a mapping get the picture built from what it reports, where `SetLabels()` chooses the face labels. The view color is its background: a host on a filled box sets it |
| `SetTriggerMark(game_controller_trigger, position)` | Added 17 Nov 2026, at the end: a tick across the L2 or R2 cap where a trigger effect starts, `position` 0 (released) to 1 (pulled all the way); below 0 removes it. Drawn only on a picture with analog trigger caps. Settings sets it from "Starts at" |
| `static PictureBitmap(vendorID, productID, family, width, height, lineColor)` | Added 17 Nov 2026, at the end: the still drawing of a model, chosen as `SetDevice()` chooses it (vendor and product ID, else the family's own picture), as a new `B_RGBA32` bitmap of `width` by `height` pixels (at most 4096 each): transparent background, the line in `lineColor`, kept in shape and centred, bolder below 250 pixels wide, the printed labels only where they are 6 points or larger. Without a model and with `B_GAME_CONTROLLER_FAMILY_UNKNOWN` a plain outline of a controller body. The caller owns the bitmap; `NULL` for a size below one pixel or when it could not be made. Needs a `BApplication`. For a card or a list row: Settings draws the cards of its `BCardStrip` and the gray example without a controller with it |

The mapping: the driver gives the bus, the vendor and product ID, the version, the product name, the battery and the HID usage of every axis (`R5_JOYSTICK_GET_INFO` in `<joystick_driver.h>`, an R5 snippet: `B_JOYSTICK_DRIVER_BASE + 82`, `r5_joystick_info` with its own `size` so fields can be added at the end). usb_hid fills it from the device descriptor and the product string, bluetooth_hid from what bluetooth_server read in the SDP Device ID record (only a USB-IF vendor ID, `VendorIDSource` 2) and the device's name, i2c_hid from its HID descriptor. `BGameController` reads the Windows entries of `gamecontrollerdb.txt` (GUID `03000000vvvv0000pppp000000000000`, any bus and version): DirectInput numbers buttons by HID usage, as hid_shared does, and axes by usage (X, Y, Z, Rx, Ry, Rz, sliders), so R5 sorts the driver's axes by usage first. A trigger on a whole axis runs from 0 at one end to 32767 at the other, as in SDL. R5's own entries would go first, in `r5_gamecontrollerdb.txt` beside the database (MIT); none ship yet. Both files are read from `game_controllers` under `B_SYSTEM_NONPACKAGED_DATA_DIRECTORY`, then `B_SYSTEM_DATA_DIRECTORY`.

Since the boot of 17 Nov 2026 with the 8BitDo M30 (`2dc8:0651`, over Bluetooth: the d-pad did nothing and a button lit the wrong key), `Open()` takes the standard layout from, in this order: a saved setup, a chosen layout, the database; and it fits a database line to what the HID descriptor says:
- A Windows line for the controller's own bus first (GUID bytes 0 and 1: `03` USB, `05` Bluetooth), else the first line of any bus. A controller can report other buttons over Bluetooth than over a cable; the database has no Windows Bluetooth line for the M30, so its USB line stays a guess.
- Buttons by HID usage: DirectInput's button n is the n-th Button usage the report has, while hid_shared numbers a button by its usage (usage 1 is button 0) and leaves gaps. `r5_joystick_info` has a new field at the end, `button_usages[4]` (bit n: usage n + 1 exists), filled by hid_shared; with an older driver nothing is renumbered.
- The d-pad: the controller's hat switch when it has one; without one, the X and Y axes when no stick reads them (Bluetooth pads without sticks report it either way); a d-pad on buttons stays. Sources the controller does not have (a button usage, an axis or a hat beyond its count) are dropped.
- The fixed layouts (HID usage - 1 for buttons; sticks by usage: X and Y left, Z and Rz right, or Rx and Ry when there is no Z; triggers on Rx and Ry when the right stick leaves them free, else on buttons): Xbox and Nintendo in the W3C standard gamepad order (A 0, B 1, X 2, Y 3, shoulders 4 and 5, triggers 6 and 7, back 8, start 9, stick presses 10 and 11, guide 12; the Switch Pro Controller numbers the same places); PlayStation as the DualShock 4 and DualSense (square 0, cross 1, circle 2, triangle 3, then the same, touchpad 13); Sega Genesis as the database's Windows lines number the M30 (A 0, B 1, X 3, Y 4, C 6, Z 8, L 9, R 7, select 10, start 11, home 2), no sticks. A starting point for a controller the database gets wrong, not a check of a real one.
- The saved setup is used as it was set up: nothing is fitted or moved.

`ResetLabelStyles()` removes only the label choices since the same file holds layouts and setups (it used to remove the file); its arguments and its meaning are unchanged.

The watcher is one looper per team in this library. The last `StopWatching()` quits it, and a static destructor of the library quits one still running when the library is unloaded or the team exits: a looper whose code is unloaded crashes `BApplication::_WindowAt()` when the application quits (Settings unloads its add-ons, and this library with them, before `~BApplication` walks its loopers).

The family: no mapping is Unknown; a chosen layout gives its own family; a name with "M30", "Genesis", "Mega Drive" or "Saturn" is Sega Genesis; vendor `0x054C` is PlayStation, `0x057E` Nintendo; every other mapped controller is Xbox (the standard layout). The Sega picture (R5's M30 drawing) lights C and Z for the right and left shoulder of the standard layout, and L and R for the triggers, where the database puts the 8BitDo M30's extra buttons; not checked with the controller.

`gamecontroller_test` (regular image, `/bin`) is the test without hardware: the parser and the descriptor-first rules, the installed database, a virtual controller on bluetooth_hid read through `BGameController`, two synthetic pads with the M30's IDs (one with a hat, one with the d-pad on X and Y), a saved setup and a chosen layout, the plug and unplug messages, and the drawing. `gamecontroller_test --hold <seconds>` keeps that virtual controller for a look at the Settings page.

### Feel and motion (added 17 Nov 2026)

Option C, phase 1 (`docs/GAME-CONTROLLERS.md`): rumble, trigger effects and motion for the DualSense, the DualSense Edge and the DualShock 4 over USB and Bluetooth, and rumble on any controller whose report descriptor has HID PID force feedback (USB HID PID 1.0, usage page `0x0F`). The calls are added at the end of `BGameController`, after `HasSavedMapping()`, with two enums and a struct before the class; nothing that shipped changed. A call the open controller cannot do returns `B_NOT_SUPPORTED`; before `Open()` every call returns `B_NO_INIT`. None of it is tested with a real controller.

| Call | What it does |
|---|---|
| `CanRumble()`, `SetRumble(strong, weak, duration)`, `StopRumble()` | `strong` is the heavy, low-frequency motor (the left one), `weak` the light, fast one (the right), each 0 to 1. After `duration` (µs) the library stops it from a thread of its own; 0 keeps it until the next call; a negative one is `B_BAD_VALUE`. A PID controller gets one effect: a sine (Set Periodic Report) when it offers one, else a constant force, with the larger of the two values as the magnitude and a period of 60 ms (strong) or 20 ms (weak). `Close()` stops it |
| `HasTriggerEffects()`, `SetTriggerEffect(game_controller_trigger, game_controller_trigger_effect, start, strength)` | DualSense and Edge only. `B_CONTROLLER_TRIGGER_LEFT` (0) or `_RIGHT` (1); `B_TRIGGER_EFFECT_OFF` (0), `_RESISTANCE` (1), `_CLICK` (2), `_VIBRATION` (3); `start` is where on the pull it begins and `strength` how hard, each 0 to 1. The controller's simple modes (`0x05`, `0x01`, `0x02`, `0x06`); `Close()` sets both triggers free |
| `HasMotion()`, `SetMotionEnabled(bool)`, `IsMotionEnabled()`, `GetMotion(game_controller_motion*)` | `game_controller_motion` has `timestamp` (`system_time()` when the report came), `acceleration[3]` in g and `rotation[3]` in degrees per second (x pitch, y yaw, z roll), and `_reserved[4]`. `Update()` reads the newest report; `GetMotion()` is `B_NOT_ALLOWED` while motion is off and `B_ENTRY_NOT_FOUND` until the first sample. Uncalibrated: 8192 counts per g and 16 per degree per second, SDL's values without the controller's calibration report; the axes as SDL documents its sensors (x right, y up, z toward the player), not checked with a controller |

Per model, all built in user space (`GameControllerReports.cpp`, from SDL's `SDL_hidapi_ps4.c` and `SDL_hidapi_ps5.c` as a reference for the layouts, facts only, zlib):
- DualSense: output report `0x02` (48 bytes) on USB, `0x31` (78 bytes, with `0x00`, `0x10` and a CRC-32 of `0xA2` and bytes 0 to 73 in bytes 74 to 77) on Bluetooth. The motors and both trigger effects are sent every time, with the improved rumble of firmware 2.24 and later (the firmware report `0x20` is read on USB; over Bluetooth and on the Edge it is assumed). Motion from input report `0x01` on USB and `0x31` on Bluetooth.
- DualShock 4: output report `0x05` (32 bytes) on USB, `0x11` (78 bytes, `0xC4`, CRC) on Bluetooth, with only the rumble flag, so the light bar keeps its colour. Motion from `0x01` on USB and `0x11` on Bluetooth.
- Over Bluetooth both controllers send only their short report `0x01` until their first output report, then only their full report. `SetMotionEnabled(true)` sends one; a rumble or trigger effect does too. bluetooth_hid then builds the short report from every full one, so `BJoystick` programs keep their buttons and sticks (`R5HIDReports.h` in hid_shared).
- HID PID: Device Control (Device Reset, Enable Actuators) and Device Gain at the first rumble, then Create New Effect (feature) and the effect block index from PID Block Load (feature) when the controller manages its own effects, else block 1; Set Effect, Set Periodic or Set Constant Force and Effect Operation Start Solo per rumble; Effect Operation Stop to stop. Feature reports are only carried over USB; a Bluetooth PID controller that needs them answers the error.

The driver side, three additive calls in `<joystick_driver.h>` (an R5 snippet), answered by hid_shared for usb_hid and bluetooth_hid (i2c_hid and hyperv/hid answer `B_NOT_SUPPORTED`). Each takes a structure whose pointer and lengths come from the caller; hid_shared copies them with `user_memcpy()` after `IS_USER_ADDRESS()` on both ends:
- `R5_JOYSTICK_SEND_REPORT` (`B_JOYSTICK_DRIVER_BASE + 83`, `r5_joystick_report`): an output or feature report the report descriptor declares with that type and ID, at most as long as declared (padded with zeros to that length), report ID first when the device uses IDs. usb_hid sends an output report on the interrupt OUT pipe when there is one (one at a time, one second at most), else as `SET_REPORT`, and a feature report as `SET_REPORT`; bluetooth_hid hands an output report to bluetooth_server (its queue now holds up to 512 bytes per report), a feature report is `B_NOT_SUPPORTED`.
- `R5_JOYSTICK_GET_REPORT` (`+ 84`): `R5_JOYSTICK_REPORT_INPUT` gives the last input report with that ID as it came, its first 128 bytes, with `timestamp` (four IDs are kept; usb_hid only sees reports while the joystick is open); `R5_JOYSTICK_REPORT_FEATURE` reads a declared feature report with `GET_REPORT` (usb_hid only).
- `R5_JOYSTICK_GET_DESCRIPTOR` (`+ 85`, `r5_joystick_descriptor`): the report descriptor as the device gave it (at most 4096 bytes), or `B_BUFFER_OVERFLOW` with the full length when it did not fit.

### Feel and motion, phase 2 (added 17 Nov 2026)

The calls above now also work for the Nintendo Switch Pro Controller (`057E:2009`, USB and Bluetooth), the left and right Joy-Con (`057E:2006`, `057E:2007`, Bluetooth), the Xbox Wireless Controllers over Bluetooth Classic and Low Energy (`045E:02E0`, `02FD`, `0B05`, `0B0C`, `0B13`, `0B20`, `0B21`, `0B22`), and Xbox One and Series controllers over USB (any device whose first interface is class `0xFF`, subclass `0x47`, protocol `0xD0`: the Gaming Input Protocol, GIP). One pair of calls is added at the end of `BGameController`, after `GetMotion()`; nothing that shipped changed.

| Call | What it does |
|---|---|
| `HasTriggerRumble()`, `SetTriggerRumble(left, right, duration)` | The small motors in the triggers of Microsoft's own Xbox controllers (over Bluetooth and USB; not on other makers' GIP pads), each 0 to 1. After `duration` (µs) the library stops them; 0 keeps them until the next call; a negative one is `B_BAD_VALUE`. `StopRumble()` and `Close()` stop them with the main motors. Not the DualSense's adaptive triggers (`SetTriggerEffect()`) |

What each model gets:

| Model | Rumble | Motion | Trigger rumble | Battery (`BatteryLevel()`, the Bluetooth item in Deskbar) |
|---|---|---|---|---|
| Switch Pro Controller | HD rumble, output `0x10`: the high band (frequency code `0x74`) for `weak`, the low band (`0x3D`) for `strong`, both sides alike, resent every 50 ms while it runs (the controller stops a rumble it hears nothing of) | Yes: subcommand `0x40`, then the newest of the three samples in report `0x30`, in the axes of the PlayStation controllers; scaled with the factory calibration read from SPI flash `0x6020` (24 bytes), else the nominal 4096 counts per g and 14.2842 per degree per second | No | Bluetooth: from every full report or subcommand reply (0 to 8, in steps of 12.5 %) |
| Joy-Con (left, right) | The same HD rumble | No: the sensors are only in the full report mode, which changes a Joy-Con's buttons | No | No (no full reports) |
| Xbox over Bluetooth | Output `0x03`: the motors enabled (`0x0F`), left trigger, right trigger, strong, weak, each 0 to 100, 2.55 s and repeated (the library stops it) | No | Yes (Microsoft) | Report `0x04`: the low two bits, as 10, 40, 70 or 100 % |
| Xbox over USB (GIP) | The GIP rumble packet `09 00 seq 09 00` with the same bytes | No | Yes (Microsoft) | No |

The driver side (R5 snippets in hid_shared's `R5HIDReports.h`, usb_hid's `HIDDevice.cpp` and `QuirkyDevices.cpp`, and bluetooth_hid's `HIDDevice.cpp`):
- **Switch Pro Controller over USB.** Its own report descriptor does not describe the reports it sends, so usb_hid uses an R5 one: the simple report `0x3F` the controller has over Bluetooth (16 buttons, a hat, four 16-bit axes), and the vendor reports `0x21`, `0x30`, `0x81` in and `0x01`, `0x10`, `0x80` out, 63 bytes each. When the device is added usb_hid sends the handshake `80 02`, high speed `80 03`, the handshake again and "USB only" `80 04`, then the subcommands rumble on (`0x48`), the full report mode (`0x03 0x30`) and the first player light (`0x30 0x01`), each after the answer to the one before (at most 100 ms each). Each full report becomes a simple report for the joystick; `R5_JOYSTICK_GET_REPORT` still gives the full one.
- **Switch controllers over Bluetooth.** When bluetooth_server gives their IDs (`R5_BTHID_SET_INFO`), bluetooth_hid sends rumble on, (Pro Controller only) the full report mode, and the first player light, one subcommand after each reply (report `0x21`), each sent again after 40 reports without a reply, three times. For the Pro Controller it builds the simple report `0x3F` from each full one when the controller's descriptor has that report in the expected size.
- **GIP.** usb_hid takes the interface (a quirk entry like Haiku's Xbox 360 one) with an R5 report descriptor: input `0x20` with A B X Y LB RB, both triggers as buttons past a third, View Menu LS RS Guide Share, a hat, X Y Z Rz for the sticks (Y down) and Rx Ry for the triggers (0 to 1023), in the order of libr5game's fixed Xbox layout; output `0x09` (12 bytes after the command) for the rumble. When the device is added, and again when it announces itself, usb_hid sends power on, the guide light, "security passed" and the motor set-up; it acknowledges every packet that asks for it and turns the input packet `0x20` and the guide button packet `0x07` into the R5 report. The Series controller's Share button is read too; Elite paddles are not.
- **`r5_joystick_info`** gets `flags` at the end (a caller with a smaller `size` does not see it): `R5_JOYSTICK_FLAG_STANDARD_LAYOUT` (`0x1`, the driver gives the controller in the order of libr5game's fixed layout of its family, so `BGameController` uses that layout, not the database) and `R5_JOYSTICK_FLAG_GIP` (`0x2`, rumble is output `0x09`). usb_hid sets both for a GIP controller.
- **bluetooth_server** reads the PnP ID (`0x2A50`) of a Low Energy device's Device Information service and hands vendor, product and version to bluetooth_hid, so an Xbox controller with Low Energy firmware has its IDs (before this, no Low Energy controller had any).

Limits: the Joy-Cons over USB (only in the charging grip, `057E:200E`) and as a pair, the Switch 2 controllers, the user calibration of the Switch IMU (`0x8026`), the Switch stick calibration, the Xbox 360 controllers' rumble, Elite paddles, the GIP chatpad, headset and other accessories, and the Xbox controllers' battery over USB are not done.

`BGameControllerMotionView` (`<GameControllerMotionView.h>`): the controller's motion as a `BTiltLevel` ("Tilt") and three `BAxisBar`s ("Pitch", "Roll", "Yaw" over "Turn"), from the Appearance Kit. `SetMotion(const game_controller_motion&)` shows a sample: the level from the acceleration's x and z, the bars from the rotation, full at `TurnRange()` degrees per second (360 unless `SetTurnRange()` changed it). `Reset()` puts everything back in the middle. `SetEnabled(false)` draws the level, the bars and their labels gray, as a disabled control (the Game controllers page does this for its example without a controller); `IsEnabled()` reads it back. The program reads the controller; the view only shows. A private data pointer, four reserved virtual functions and `_reserved[4]`. The library now links `libr5ui.so`.

`gamecontroller_feel_test` (regular image, `/bin`) is the test without hardware: a virtual DualSense on bluetooth_hid with report IDs and the full report `0x31` in and out, the three driver calls (and what they refuse, kernel addresses included), the buttons and motion from full reports only, the exact rumble and trigger reports with their CRC in bluetooth_hid's output queue, a timed rumble that stops, `Close()` freeing the triggers, the two widgets drawing, `PictureBitmap()`, and `BCardStrip` (items, selection by `Select()` and the keys, the selection message, the archive, the drawing with cards and empty). `scripts/test-game-feel.sh` runs the host unit test of the report builders, the CRC, the motion parser, the short report and the PID reports; `scripts/fuzz-bt.sh hid` fuzzes the descriptor parser and the report code.

## Power Kit

`libr5power.so`, header `<PowerRoster.h>` (`headers/os/app`), source `distro/power/lib`, MIT, in both images. The contract is `docs/POWER.md`; the R5Book page is `docs/r5book/power-kit.html`. Added 22 Nov 2026 (POWER.md step 2).

| Call | What it does |
|---|---|
| `BPowerRoster()` | Finds the batteries under `/dev/power` (ACPI `_BST`, `_BIX`/`_BIF`) |
| `CountBatteries()`, `GetBatteryInfo(index, power_battery_info*)` | The batteries, read again on every call: `state` (`B_POWER_BATTERY_DISCHARGING`, `_CHARGING`, `_CRITICAL`, `_NOT_CHARGING`), `percent`, `time_left` in seconds (to empty or to full), `health` (full charge in percent of the design capacity), `capacity`, `full_capacity`, `design_capacity`, `in_mah`. -1 is "not reported" |
| `IsOnAC()` | The ACPI AC adapter (`/dev/power/acpi_ac/0`); without one, true unless a battery discharges |
| `Profile()`, `CanChangeProfile()` | `B_POWER_PROFILE_SAVER`, `_BALANCED`, `_PERFORMANCE`, from the kernel (`_kern_r5_get_power_info()`); false when the processor offers no speed control |
| `CanSleep(power_sleep_state*)` | Whether sleep works (false before POWER.md step 4) and the state the firmware offers: `B_POWER_SLEEP_S0IX`, `_S3`, `_NONE` |
| `GetSettings(BMessage*)`, `SetSettings(const BMessage&)` | The machine's power settings from the power service. Setting is an admin request (`r5_admin_send()`); the service checks the sender and writes `B_SYSTEM_SETTINGS_DIRECTORY/power` itself. `B_NO_INIT` without the service (Safe mode). The field names are in the private `<PowerProtocol.h>` |
| `static StartWatching(BMessenger)`, `static StopWatching(BMessenger)` | `kMsgPowerChanged` (`'r5pw'`) with int32 `"change"`: `B_POWER_BATTERY_CHANGED`, `B_POWER_AC_CHANGED` (bool `"on ac"`), `B_POWER_PROFILE_CHANGED` (int32 `"profile"`), `B_POWER_SETTINGS_CHANGED`; `B_POWER_WILL_SLEEP` and `B_POWER_DID_WAKE` are reserved for step 4. One looper per team reads the batteries and AC every two seconds (PowerStatus's former thread) and passes on what the service sends; the last `StopWatching()` quits it, and so does unloading the library |

The class has a private data pointer and `_reserved[8]`; new calls go at the end. PowerStatus links the library and reads through the private `BPrivate::BatteryReader` (`<PowerBattery.h>`, `headers/private/power`); its Deskbar item loads the library with it. The Power page in Settings and the power service link it too.

The kernel side is private: `_kern_r5_set_power_profile()` (uid 0 only; the power service) and `_kern_r5_get_power_info()` in `<syscalls.h>`, with `<r5_power_profile.h>`, and version 2 of the cpufreq module interface (`cpufreq_module_info_v2` in `<cpufreq.h>`, a name ending in `/v2`). The existing `cpufreq_module_info` did not change.
