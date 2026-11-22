# Brief: Default (R5 Modern), the round R5 decorator

Owner decision, 22 Nov 2026. Design: Design canvas "R5 window decorators – mockup", board 4 "Soft"
(https://claude.ai/artifact/GYVvNRWPUHqCnpKiwAZ1dD#artboard-Soft). Board 5 (button studies) was
declined: the buttons stay as on board 4.

## Before anything

1. Read `AGENTS.md` in full. Then reply with the rules that bind this task (licensing and
   SPDX snippets, native source in `distro/`, no new calls in `libbe`, build with
   `scripts/build-locked.sh` from your own worktree, ask before push/PR/jam, no stick).
2. Read `docs/APP-SERVER-COMPOSITOR.md` (decorator hooks, ABI version, CPU and GPU mode,
   input margin), `distro/desktop/src/add-ons/decorators/R5Decorator/`,
   `distro/desktop/src/servers/app/WindowEffectLevels.h`, R5Book `appearance-effects.html`.
3. Work in your own worktree from `origin/main`. Do step 0 and stop.

## What the user gets

A third choice in Appearance, Look and feel, next to **Default (R5)** and **Classic (Haiku)**.
Its name in the list is **"Default (R5 Modern)"** (owner, 22 Nov 2026). Default (R5) and
Classic stay exactly as they are.

## The look (numbers at the default font size; scale like Haiku, times font size / 12)

- **Tab:** a soft yellow card, top corners round, radius **18 px**. Visible height 32 px; it
  continues 14 px under the window, which overlaps it (the window's top-left corner is drawn
  over the tab). Minimum width 250 px, 9 to 10 px padding, 10 px gap. Bold title in the tab
  font. Colour from `B_WINDOW_TAB_COLOR` (top about 10% lighter, a soft vertical gradient);
  inactive from `B_WINDOW_INACTIVE_TAB_COLOR`.
- **Buttons:** Haiku's own shapes. Close on the **left** (a 15 px square), zoom on the
  **right** (16 px: a 9 px square behind a 12 px square). Finish "glass", **square** corners:
  fill white at 55% alpha, a 1 px line of the tab's dark yellow at about 35%, a 1 px white
  highlight on top. Inactive: grey line, white at 60%. In the CPU mode: the same colours,
  blended against the tab once, opaque.
- **Window:** all four corners round, radius 18 px. A 1 px light edge (white about 75% in the
  GPU mode; a plain grey-blue line, about `#9fb0c8`, in the CPU mode). No drawn resize knob:
  the R5 input margin as in Default (R5).
- **GPU mode:** the shadow and frosted glass of the existing steps (Quiet, Balanced, Glassy in
  `WindowEffectLevels.h`); Balanced must look like board 4 (wide soft shadow, slightly blue).
  Do not add a second table of numbers. Glass: the tab (via `GetGlassRegion`). The window
  body is glass only for a program that asks (`set_window_glass`), as today.
- **Without GPU** (Lavapipe without effects, and the CPU mode in Safe mode): no shadow, no
  glass, solid tab. The round corners stay.

## Steps

**Step 0 (report only, no code).** Answer, with file and line:
- How a decorator can give the compositor a non-rectangular outline today, and what is
  missing. Reference to read, not to copy blindly: RenkuOS/Source commits `a6fb86368`
  (`WindowShape`: opaque region, edge region, corner coverage tables, shadow profile),
  `15f03bcf8` (`RoundedDecorator`) and `8d0aee830` (`DecorStyle.h`), author Ian
  (idumych), MIT, signed off. R5 already has a decorator ABI version; say what of
  `DecorStyle.h` would still add something.
- What a radius of 18 px cuts from programs: StyledEdit, Terminal, Tracker windows,
  `B_DOCUMENT_WINDOW_LOOK` (scroll bars meeting in the bottom-right corner), a
  `BDirectWindow`, a full-screen `BWindowScreen`. Propose how content meets the round
  corner (clip it with coverage, or keep the corner of the content square inside the round
  frame, or another way) with the trade-off of each. The owner chooses.
- The CPU mode: whether the footprint can leave the corner pixels out (a stepped region,
  no anti-aliasing) so the desktop and windows behind draw there, and what that costs in
  clipping.
- Which code goes where: a new add-on next to `R5Decorator` that shares its code (my
  expectation), or an option inside `R5Decorator`. Default (R5) must not change.
- The plan for steps 1 to 3, with the tests.
Stop and wait for the owner.

**Step 1.** The outline hook, added at the end of the R5 decorator interface (ABI version
bump and `R5DecoratorAbiTest` still refused), and its use in the GPU compose: corner coverage
as alpha, the shadow following the round outline, the glass clipped to it.

**Step 2.** The CPU mode: the stepped corner region, so Safe mode shows the same shape.

**Step 3.** The decorator itself, its name in the Appearance list (the list only; the
Settings window layout in `AGENTS.md` stays as it is), and hit testing on the round corners
and the tab's corners.

Each step: its own commit, `CHANGELOG.md` entry (problem, change, limits, how tested),
`docs/APP-SERVER-COMPOSITOR.md` updated for the hook, `docs/LIBR5.md`/R5Book only if a public
call changes. No What's new and no `distro/BUILD` bump until the owner has seen it.

## Tests

- Build only with `scripts/build-locked.sh` from your worktree. Ask before the jam.
- One short QEMU boot on your own image copy (Lavapipe), one screendump of an active and an
  inactive window with the new decorator, and one in Safe mode (CPU). Not
  `scripts/run-qemu-r5.sh` while another session may run QEMU.
- Check: Default (R5) and Classic pixel-identical to before (the compare mode in the
  compositor doc); a decorator add-on without the new ABI version is refused, not crashed.
- No stick and no 5625U claim: the owner decides when this goes on a stick.

## Out of scope

Animations, wobbly or see-through windows, new button shapes, the Settings window layout,
Deskbar. Do not take RenkuOS's CPU compositor. Do not push or open a PR without asking.

Report at the end: **Veranderd / Getest / Onbewezen / Documentatie**.
