# Appsolutely: R5's app store, design choices

Owner decisions, 23 Nov 2026. On the roadmap since 23 Nov 2026 (`docs/ROADMAP.md`, Near term item 10, and the Named products row). Nothing is built.

## Decided

| Topic | Choice |
|---|---|
| Name | **Appsolutely** (owner; earlier candidates Cargo, Loadout, Packd, Shelf, Harbor, Bazaar, Ichiba; not "R5 Store"). No trademark check done yet |
| What it is | A real store, not a package tool: a front page with a featured banner, editor-style rows, categories, app pages with screenshots and ratings, one "Get" button (mockup in chat, 23 Nov 2026, R5 Modern look) |
| Base | Haiku's HaikuDepot copied into R5's tree (`distro/`), keeping its engine and replacing its face. HaikuDepot is MIT (273 headers checked); its original headers stay, R5 parts get SPDX-Snippet MIT tags |
| Engine kept | `model`, `packagemanagement`, `server`, `process`, `util` (about 130 files): installing and removing, the data from Haiku's server (`depot.haiku-os.org`: descriptions, screenshots, ratings, categories, prominence), background work |
| Face replaced | `ui` (37 files) becomes the Appsolutely store |
| Content | HaikuPorts apps and R5's own apps (from the R5 package repository). No web apps |
| Front page | Automatic: from the server's data (popular, best rated, prominence), no hand-picked list. R5's own apps have their own row, titled **"Enhanced for R5 Operating System"** (owner, 23 Nov 2026), with a small R5 badge on each app |
| Payment | Free apps only for now, but the design leaves room for a price and buying later |
| HaikuDepot | Removed from R5 once Appsolutely works |

## Icon (chosen: G2, owner, 23 Nov 2026)

`docs/design/appsolutely/appsolutely-icon.svg` (sheet `docs/design/appsolutely/icon-variants.png`, middle). The R5 wave pebble large, with HaikuDepot's own box icon (HVIF from `haiku/src/apps/haikudepot/HaikuDepot.rdef`) as a badge at the lower right, the same pattern as the Settings icon (pebble plus the preferences wrench). Under the R5 icon rule (own shadow dropped, one thin outline `#1b1b1b`, one cast shadow), rendered with the icon-rule trial scripts (`generated/icons-r5/trial`, outside git); checked at 128, 64 and 32 px on light and dark. Not drawn in Icon-O-Matic as HVIF yet. Licence: the box is MIT (Haiku); the pebble is R5 artwork, GPL-3.0-or-later, so the combined icon follows that until the owner decides on the artwork licence.

Rejected: F (pebble rising out of the box, looked like a loose ring), G (box badge too small at 32 px), H (pebble sticker unreadable at 32 px), and two versions without the yellow cube (the cardboard went with it).

## Facts and open points

- HaikuDepot already has a "featured" list driven by the server's prominence value (`haiku/src/apps/haikudepot/HaikuDepotConstants.h:124`, `packagemodel/PackageClassificationInfo.cpp`).
- Haiku's server only knows Haiku and HaikuPorts packages. R5's own apps need their description, screenshots and category from the R5 package repository; ratings for them have no source yet.
- How cleanly the engine is separated from `ui` is not checked in the code yet; only the folder layout was read.
- The automatic front page needs a rule for "popular" and "best rated" from the server's data; which fields exist is to be checked.
- Removing HaikuDepot touches the Welcome page (`distro/welcome/welcome_en.html` tells users to use HaikuDepot) and any menu or link that opens it.
- Updates: R5's own system updates stay on the Software update page (`docs/design/INSTALL-UPDATE.md`). Where app updates appear (Appsolutely's "Updates" in the mockup) must match that design.
- Ratings and accounts on Haiku's server have their own usage conditions (`ui/UserUsageConditionsWindow.*`); check what that means for R5 users before relying on them.
- "Room for paid apps later": a price field and a buy flow are not designed yet; a payment provider, taxes and support are a separate decision.
