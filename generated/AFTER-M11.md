# Still to do: the list for after M11 (15 Nov 2026)

M11 is done. Since build 10064 the GPU mode is on by default (user, 15 Nov 2026), before boot 4 passed; `gpu_compositor false` and Safe mode keep the CPU mode. Stick 50 passed boots 1–3 on the 5625U; boot 4 (a hang while composed) failed on a self-test bug, fixed in `6829434c46` and not booted again. Results: `CHANGELOG.md`, 15 Nov, "M11 done". Logs: `generated/boot-5625u-50/m11/`.

## Order

1. **The next stick that is made anyway** (the first M12 stick, or a short one if the default should come sooner) runs M11 boot 4 again with `6829434c46`. Pass: one hang, `gpu reset mode2 ok`, `gpu selftest after reset ok`, `gpu compositor restarted after reset`, no `gpu compositor off`, the pointer moving the whole time.
2. **The default** (M11 decision 1): done in build 10064 on the user's word, before items 1 and 3. Those two are now checks of the default, not conditions for it.
3. **The second boot-1 run on stick 50:** `window 2 zero-copy off: import` on RADV, and the scenario lines not found after `check wait`. Read the syslog in `generated/boot-5625u-50/m11/boot1-2/` before the next stick; find why the import failed.

## Stick tools

4. `check` expects `animations off` and `animations on`, but the compositor prints no such line. Either log the Animations switch (one line, as for Shadow and Frosted glass) or drop the two checks. Logging is the better choice: it matches the other two switches.
5. `animation workspace` was not seen: no workspace switch reached the log in boot 1. Check on the next stick that the card asks for one, and that the slide logs it.

## Found during M11, not fixed

6. The eight review items left with a reason in `generated/gpu-m11/REVIEW-STACK-4.md`. Check each reason again before M12 step 3, where the drawing code is touched.
7. VulkanTeapot on the copy path shows half-drawn frames (white bands) now and then, in the CPU mode as well as composed (M11 step 5, not from M11).
8. `draw outside buffer: Deskbar` once in a `BWindowScreen` boot: `BWindowScreen` sets its mode before it takes the screen lock. The safety check caught it.

## For M13 (performance), measured on stick 50

9. On Lavapipe app_server used 3.5–3.7 s of CPU per 10 s with VulkanTeapot running, against 0.3–0.9 s on RADV. The desktop held 59.5 fps, but a slower machine will feel it.
10. Zero-copy against the copy path on RADV: the same frame rate (held at the refresh rate), less app_server CPU with zero-copy (326–728 ms against 688–903 ms per 10 s). A longer, steadier measurement belongs to M13.

## M12

11. Step 0 is done (`docs/APP-SERVER-GPU-DRAWING.md`, #4266 and #4267), with step 6b (a buffer of its own for `BDirectWindow` on Lavapipe) and the look and feel redesign as follow-up. Next: step 1 (the Skia recipe), after the user approves Skia.

## Carried over from AFTER-M8

12. Items 9–13 of `generated/AFTER-M8.md` (write-combining boot, shutdown long press, the fault interrupt in the syslog, `scripts/test-r5-gpu`, the VirtualBox copy guard). Item 8 (GPU reset) was done in M10. Check each before planning.
