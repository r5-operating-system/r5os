# M12 brief: the GPU draws the windows, and the CPU mode moves to Safe mode (11 Nov 2026, updated 15 Nov 2026)

For Cursor or Claude. It does not override `AGENTS.md` or `docs/GPU-ARCHITECTURE.md`. Read those first, plus `generated/gpu-m11/BRIEF.md` (the two modes, the window buffers, the fallback, the lessons), `docs/APP-SERVER-COMPOSITOR.md` (the compositor as built in M11), `generated/gpu-m12/SAFE-MODE.md` (step 6), and `docs/GPU-ARCHITECTURE.md` "Release mechanism" (the model for the Skia recipe). Where this brief and a design document disagree, stop and ask; do not pick one.

**Rules for the agent (read before anything else):**
1. **Read `AGENTS.md` in full first**, then this brief. Your first reply names the `AGENTS.md` rules that apply to your step (at most five lines) and what you will and will not do. No file is changed before that reply.
2. **Only the step the prompt names.** No other M12 step, no item from the roadmap, M13 or another brief, no clean-up or refactor on the side. Something you notice goes under "Seen, not done" in your report.
3. **Stop and ask** when the brief, a design document, `AGENTS.md` or the source disagree, when a step needs something outside its files, or when a test fails twice. Do not choose on your own and do not work around it.
4. **Ask before every `git push`, PR, stick write, merge into `main` or change outside the repository** (`AGENTS.md`, development loop step 8). Never merge, never force-push, never push to `main`. PRs as `AGENTS.md` says (Cursor `ManagePullRequest`, otherwise the compare URL; `gh pr create` only when the user asks).
5. **Do not change** `AGENTS.md`, the fixed GFX9 contracts in `docs/GRAPHICS.md`, `distro/BUILD` or What's new, unless the prompt says so. Where a step needs an `AGENTS.md` change (step 6), write the proposed text in the report; the user makes the change.
6. **Only one agent jams at a time.** Ask before you jam in the shared WSL tree.
7. **Never claim a test that did not run.** Report Veranderd / Getest / Onbewezen / Documentatie, with the pass lines quoted, and stop. The next step starts only with a new prompt from the user.

**Status (15 Nov 2026):**
- M11 steps 0–5 are merged (last: #4261, the direct-window pause and the forced failure). The M11 stick (step 6) is being prepared (`generated/gpu-m11/PROMPT-STEP6-STICK.md`).
- **Start after the M11 stick has passed.** Steps 0–5 need no GPU (Lavapipe in QEMU or VirtualBox). Step 0 (paper) may be written before the M11 stick passes. Nothing of M12 goes on a stick before M11 has passed.
- Performance work is **M13**, after M12 (user, 15 Nov 2026). M12 measures; it does not tune.

**M12 is done when, on the 5625U:**
- in the GPU mode, the GPU draws everything: every drawing operation app_server offers, text, bitmaps and offscreen `BBitmap` views, through Skia on Vulkan (RADV);
- the drawing-compare set passes for every operation (GPU against Painter, within the tolerance from step 0);
- the GPU mode looks approximately the same as the CPU mode: the same layout, colours, fonts and decorator;
- drawing time and CPU time are measured for both modes and recorded;
- without a supported GPU (VirtualBox), the same Skia path runs on Lavapipe, and its draw time is measured and recorded;
- **Safe mode (step 6):** when RADV is lost and its restart fails, the desktop goes on on Lavapipe without a freeze; when Lavapipe fails too, the next boot opens the boot menu with Safe mode selected; the CPU mode (Painter) runs only in Safe mode and there draws exactly as before.

---

## What exists today (after M11)

- **The two modes from M11.** GPU mode, on RADV or on Lavapipe when there is no GPU: each window has its own buffer (made through Vulkan), and the GPU composes, with the shadow, frosted glass, the three strengths and the animations. But Painter still draws each window's content into that buffer on the CPU. CPU mode: Haiku's app_server, the automatic fallback (decision 11 of M11) and the direct-window pause (decision 9 of M11).
- **Painter (AGG)** behind the `DrawingEngine` interface draws lines, rectangles, round rectangles, ellipses, arcs, polygons, Bézier curves, `BShape`, five gradient types, bitmaps (scaled, filtered), the `B_OP_*` drawing modes, clipping regions, `BAffineTransform`, `BAlphaMask`, and text (FreeType through `ServerFont`).
- **app_server is a Vulkan client** of RADV and of Lavapipe. Vulkan windows of programs are shared zero-copy on RADV (M11 step 3b).
- **The switch to the CPU mode** ran for the first time in M11 step 5 and found a crash in the runtime loader (a library with thread-local storage unloaded, then a new thread). Fixed in #4261. Every turn-off path in M12 is tested on purpose, not assumed.
- **Skia is not in the tree.** air/OS builds Skia on Haiku for its browser (licence of their recipe unclear: read only).

---

## Decisions (fixed for this brief)

1. **All or nothing.** In the GPU mode the GPU draws everything, and Painter draws nothing; in the CPU mode Painter draws everything. No mixing per window or per operation, and no "this one on the CPU". So the GPU drawing only goes on when every operation passes the compare set. Until then it is behind a test switch (`gpu_draw true`). After the stick passes, it becomes part of the GPU mode by default. This replaces "only if measurements show it is faster" in `docs/GPU-ARCHITECTURE.md` "2D in app_server" step 3. Step 0 writes that edit.
2. **Skia, with its Vulkan backend.** BSD-3-Clause; it combines with GPL-3.0 and MIT.
   - Before it is added, step 0 reports, as `AGENTS.md` asks: project, SPDX, copyright, the exact version, which of its bundled libraries are built, integration, source-offer duties, and combination questions. Keep the build minimal: FreeType and the image codecs R5 already ships; no ICU unless step 0 shows it is needed.
   - Step 0 also chooses Ganesh or Graphite, with the reason.
   - Pinned to one version, built from **R5's own recipe in this repository**, with a version contract and update steps in `docs/GPU-ARCHITECTURE.md`, as for Mesa.
3. **Skia stays hidden.** It is linked only by app_server (statically or as a private library: step 0 decides). No Skia header in a public header, nothing in `libbe`, nothing in the `libr5*` libraries. Applications see only the Haiku API. That is the BeOS way: Skia is an engine under the hood, like Mesa.
4. **`SkiaDrawingEngine`** behind the same `DrawingEngine` interface, drawing into the M11 window buffers through Vulkan, on the same device and queue as the compositor. New R5 files are **MIT** with an SPDX header (`AGENTS.md`, Licensing).
   - Step 0 lists **every** `DrawingEngine` call and Painter feature with its Skia mapping. Where Skia has no direct equivalent (for example `B_OP_INVERT`, `B_OP_SELECT`, `B_OP_ERASE`, the diamond gradient), the mapping is a Skia runtime effect or blender.
   - A call without a mapping blocks the GPU drawing; it is never quietly drawn on the CPU.
5. **Text.** Skia's glyph cache, on FreeType, with the same font files. Hinting, antialiasing and subpixel rendering come from the Antialiasing tab of Appearance. Text metrics (`StringWidth`, escapements, line heights) stay `ServerFont`'s, so layout is identical in both modes. Only the rasterisation of the letters can differ slightly.
6. **"Approximately the same".** Step 0 sets a tolerance per class of operation, and the compare set holds every operation to it:
   - solid fills and copies: exact;
   - anti-aliased edges and text: within a stated maximum per pixel and a stated share of pixels.
   The decorator, colours, sizes and layout are the same in both modes by construction. M11's `compose compare` keeps running with effects off.
7. **Offscreen `BBitmap` views** (`B_BITMAP_ACCEPTS_VIEWS`) are drawn by the GPU too, into a buffer the CPU can read. app_server finishes the GPU work for that bitmap at `BView::Sync()`, and before any read of its pixels by app_server itself (`ReadBitmap`, drag bitmaps, screenshots). Haiku programs must already call `Sync()` before they read the bits, so a correct program sees no change. A program that reads without `Sync()` already races in Haiku. Step 0 names test programs that do this.
8. **Fallback in steps 1–5** is M11's: on GPU loss, a failed submit or a fence timeout, the whole desktop switches to the CPU mode for the rest of the session, every window is redrawn by Painter, and one line is logged: `app_server: gpu draw off: <reason>`. Step 6 replaces this switch with the ladder of decision 10.
9. **Measurement.** Both modes over the same scenario, and the GPU mode on RADV and on Lavapipe (5625U and VirtualBox): median draw time per frame, CPU time. Lavapipe being slower than Painter is recorded, not a reason to change the path. The GPU mode stays the default even when it is slower on one operation. The result lists the operations that were slower, so M13 can improve them. It is not a reason to draw them on the CPU (point 1). Step 1 already measures one typical frame on Lavapipe, so a large gap is known before steps 2–5 are built.
10. **Safe mode (step 6, `generated/gpu-m12/SAFE-MODE.md`).** The desktop is always composed; the CPU mode runs only in Safe mode. The ladder:
    1. normal start on RADV when the GPU is available (`R5_GPU_CAP_AVAILABLE`), otherwise Lavapipe;
    2. during a session: RADV is lost and the one restart after a reset fails (M11 decision 14) → the compositor and the drawing go on **on Lavapipe**, without a freeze, every window redrawn once; one line `app_server: gpu lost, going on with lavapipe: <reason>`;
    3. Lavapipe fails (at start or during a session) → app_server tries once more; if that fails, the desktop start counts as failed, one line `app_server: desktop start failed: <reason>`;
    4. after a failed desktop start, the next boot shows the boot menu by itself with **Safe mode** selected and one line why ("The desktop could not start last time"). How app_server tells the boot loader: step 0 designs it (a marker on the boot volume, removed after a good Sign in; additive, MIT);
    5. **Safe mode** = Haiku's safe mode, the fail-safe graphics driver and the CPU mode: no GPU driver, no compositor, no effects, a clear "Safe mode" mark on the desktop. The older system state (Haiku's package states) is offered in the boot menu.
    - **Scope:** AMD64. ARM64 and RISC-V keep the automatic switch to the CPU mode (decision 8) until Lavapipe is proven there; the brief and `docs/GPU-ARCHITECTURE.md` say so.
    - **The direct-window pause** (M11 decision 9) stays the one use of the CPU path outside Safe mode for `BWindowScreen`, and for `BDirectWindow` on RADV. On Lavapipe a `BDirectWindow` gets a buffer of its own (user, 15 Nov 2026, after step 0, Q10): step 6b.
    - The test switches `gpu_draw_fail` and `gpu_compositor_fail` now test the ladder (RADV → Lavapipe), and a new `gpu_lavapipe_fail true` tests the failed start.
    - The `AGENTS.md` sentence that the software path "always stays installed and working" stays true: Painter stays in the image and works in Safe mode. The user updates `AGENTS.md`; step 6 proposes the text.

**References (read, do not copy):** air/OS Summit's Skia build on Haiku (licence unclear). Skia's own documentation and examples (BSD-3, may be used with the notice). For Safe mode: the table in `generated/gpu-m12/SAFE-MODE.md`.

---

## Steps

| Step | What | Where | Pass line |
|---|---|---|---|
| 0 | **Design on paper**, `docs/APP-SERVER-GPU-DRAWING.md`: the Skia component report (decision 2); Ganesh or Graphite; static or private library; the full mapping table of `DrawingEngine` calls and Painter features; the tolerance per class; text; offscreen bitmaps and `Sync()`; the compare set; the measurement; the Safe mode design of decision 10 (the marker, the boot menu, the switch to Lavapipe in a session, what Safe mode turns off); the edits to `docs/GPU-ARCHITECTURE.md` and `THIRD_PARTY_NOTICES.md`. The user reviews it, and approves Skia, before code | – | – |
| 1 | **The Skia recipe** for x86_64 in the repository, the version contract, and a host test that draws one typical frame on Lavapipe, with its time. arm64 and riscv64: state that they were not built | WSL build, QEMU or VirtualBox | `skia: <version> ok lavapipe frame <n> us` |
| 2 | **The compare program** `r5-draw-compare`: every operation from the mapping table, drawn by Painter and by `SkiaDrawingEngine`, compared with the tolerance. Prints one line per operation | QEMU or VirtualBox with Lavapipe | `draw-compare <op> ok` per operation; summary `draw-compare <n>/<n> ok` |
| 3 | **`SkiaDrawingEngine` part 1:** fills, rectangles, lines, round rectangles, ellipses, arcs, bitmaps (scaled, filtered), clipping regions, all `B_OP_*` modes | the same, `gpu_draw true` | those operations `ok` in the compare set |
| 4 | **Part 2:** polygons, Bézier curves, `BShape`, the five gradient types, `BAffineTransform`, `BAlphaMask`, and **text** | the same | the compare set complete: `draw-compare <n>/<n> ok` |
| 5 | **Offscreen bitmaps and `Sync()`**, the fallback of decision 8 (`gpu_draw_fail true`), and the measurement | the same; the test programs from step 0; the M11 scenario for 60 s | `app_server: gpu draw off: forced failure` exactly once; offscreen test `ok`; `app_server: draw median <n> us cpu <n> ms per 10 s` recorded in `CHANGELOG.md` |
| 6 | **Safe mode** (decision 10): RADV → Lavapipe in a session, the failed-start marker, the boot menu with Safe mode selected, Safe mode with the CPU mode and its mark, the test switches | QEMU (Lavapipe; RADV loss simulated by `gpu_compositor_fail true`), then the stick | with `gpu_compositor_fail true`: `gpu lost, going on with lavapipe: forced failure` once, the desktop keeps working, no `gpu compositor off`; with `gpu_lavapipe_fail true`: `desktop start failed`, the next boot opens the menu with Safe mode selected, Safe mode shows the mark and draws with Painter; the boot after a good Sign in starts normally |
| 6b | **A buffer of its own for `BDirectWindow` on Lavapipe** (Q10 of step 0): laid out like the screen, a cloneable area imported with `VK_EXT_external_memory_host`, read by the compose every refresh while active (`docs/APP-SERVER-GPU-DRAWING.md`, "Decided after step 0"). RADV and `BWindowScreen` keep the pause | QEMU, Lavapipe, a `BDirectWindow` program | no `compositor paused: direct window`; its pixels in the composed frame; `app_server: direct window buffer <w>x<h>` once |
| 7 | **The stick** (one, after the M11 stick has passed) | 5625U | below |

PRs, from `origin/main`, not stacked: step 0 alone (documentation). Step 1 alone (the recipe). Steps 2–5 depend on each other: **one PR**, one commit per step. Step 6 alone (it touches the boot loader and the fallback, not the drawing). Step 6b alone (it touches the compositor and the direct-window path).

---

## Stick (step 7)

**Before:** steps 0–6 merged; the pre-stick check on paper (every step, every setting, every pass and fail line, where each line is made in the code) and `scripts/check-pre-stick.py` with the M12 lines; the image fully built before anything copies it (`r5-os-anyboot.iso`); the settings file planted for boot 1; the stick tools (`check`, `boot-next`, `check-user` in `/boot/system/bin`) extended with the M12 lines and checked to be in the image; a short card per boot with named commands. Written with `scripts/write-stick.py`, read back with `scripts/read-stick-logs.py`. No periodic sync on the stick.

**Boot 1:** `gpu_compositor true`, `gpu_draw true`, `gpu_debug true`.
1. Sign in. The desktop must come up as usual, with the R5 decorator.
2. Scenario, 60 s: the M11 scenario plus 20 s of WebPositive on a long page, and the Appearance page with every tab opened once.
3. Run `r5-draw-compare` on RADV.
4. Open Terminal and type `check`. It prints PASS or FAIL per line:
   - the M11 lines;
   - `app_server: gpu draw on <device> skia <version>`;
   - `draw-compare <n>/<n> ok`;
   - `app_server: draw median <n> us cpu <n> ms per 10 s` at least three times;
   - no `gpu draw off`, no `gpu lost`.

**Boot 2:** Safe mode chosen by hand in the boot menu. The same scenario in the CPU mode, for the comparison: `app_server: draw cpu median …` lines, the Safe mode mark. Photos of the same windows in both modes.

**Boot 3:** boot 1 plus `gpu_compositor_lavapipe true`. The same scenario on Lavapipe.

**Boot 4:** boot 1 plus `gpu_compositor_fail true`. The same scenario.
- Pass: `gpu lost, going on with lavapipe: forced failure` exactly once, the desktop keeps working on Lavapipe through the whole scenario, no `gpu compositor off`.

**Boot 5:** boot 1 plus `gpu_lavapipe_fail true` and `gpu_compositor_lavapipe true`.
- Pass: `desktop start failed`; the next boot (boot 6) opens the boot menu by itself with Safe mode selected and the line why; Safe mode starts with the mark. Then `boot-next` back to no settings file; boot 7 starts normally, without the menu.

**Result in `CHANGELOG.md`:** the medians and CPU times of RADV, Lavapipe and the CPU mode side by side, plus the VirtualBox numbers from step 5, the operations that were slower on the GPU (input for M13), and the photos compared in words.

**Failed:** a frozen desktop, any `draw-compare <op> FAIL`, a missing M11 or M12 line, wrong text layout, more than one `gpu draw off` or `gpu lost` line in a boot, a `gpu compositor off` in boot 4, Safe mode not offered after boot 5, or the menu still shown in boot 7.

**After a pass:** the M12 row in `docs/GPU-ARCHITECTURE.md`, `CHANGELOG.md` with the measurements, What's new (one build bump), and the defaults (`gpu_draw` on in the GPU mode, the ladder on AMD64) as their own small PR, so the opt-in stick and the default change are not one change. The user updates `AGENTS.md` with the Safe mode text.

---

## Lessons (apply to every step)

The list in `generated/gpu-m11/BRIEF.md` ("Lessons from M6–M8"), plus:
- A new third-party component is reported to the user and approved before it enters the build.
- Skia's version changes only through the recipe and its update steps, never by a drive-by bump.
- Never "this one operation on the CPU". A gap blocks the GPU drawing and is reported.
- From M11: a fallback that never ran is not a fallback. Every turn-off and switch path is forced once in QEMU with its test switch before the stick.
- From M11: reviews by Claude, Gemini and Grok each found other things; every finding is fixed in the same PR, also the small ones.

---

## Rounds

Counted in sessions, not weeks; the bottleneck is testing.

| Round | What | PRs | Test |
|---|---|---|---|
| 0 (may run before the M11 stick passes) | Step 0 | one, documentation | – |
| 1 | Step 1, after the user approved Skia | one | WSL build, QEMU |
| 2 | Steps 2–5 | one, a commit per step | QEMU or VirtualBox, one boot per step |
| 3 | Step 6 | one | QEMU, the boots of its pass line |
| 3b | Step 6b | one | QEMU, a `BDirectWindow` program on Lavapipe |
| 4 | The stick (step 7) | results in `CHANGELOG.md` | 5625U, seven boots |

---

## Not in M12

- **Performance tuning (M13).** M12 measures and lists what is slow; making it faster is M13: frame pacing and vsync, input latency, the slow operations from the M12 list, app_server locking, start times. `generated/gpu-m13/` when the user asks for that brief.
- Changes to the Haiku drawing API or `libbe`.
- Skia for applications, or a Skia-based kit. A later Graphics Kit decision, not this one.
- GPU-rendered letter outlines (Slug-style), colour fonts beyond what FreeType already does.
- DCN, vsync, scanout planes, more than one screen.
- Skia's own CPU raster backend as another software path. Only if the Lavapipe numbers ask for it, and only after a user decision.
- A buffer of its own for `BWindowScreen`, and for `BDirectWindow` on RADV (decision 10; the user said yes only for `BDirectWindow` on Lavapipe, step 6b).
- Safe mode on ARM64 and RISC-V (decision 10, scope).

**PR titles:** "Area - what changes for the user", in plain English. Example: "Graphics - The GPU draws your windows".
