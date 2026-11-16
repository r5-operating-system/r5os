# M11 compositor in VirtualBox (16 Nov 2026)

VM `R5OS-comp` (VirtualBox 7.2, VBoxVGA, 2 CPUs), always a fresh VDI from an
own image copy made under the build lock. Lavapipe in every run
(`gpu compositor on lavapipe: no render node`). Size changes were sent with
`VBoxManage controlvm R5OS-comp setvideomodehint`; the VirtualBox window then
sends its own hints (one pixel smaller each step). Syslogs were read from the
VDI after power-off.

## Order of the runs

1. `diag1-*`, `syslog-diag1`: first logging build. Fresh start is normal at
   1024x768; after a hint to 1437x649 or 1440x650 the picture is sheared
   (`diag1-hint1437.png`, `diag1-hint1440.png`). The DIAG lines show that
   app_server's back, front and frame pitches always match the new width.
2. `d2-*`, `run2/`: frame, back and front dumps after each frame change
   (`run2/diag-*.png`). 1440x650 directly is normal (`d2-c1440.png`); 1437x649
   is broken (`d2-d1437.png`) while the guest front buffer dump is right.
3. `d3-*`: a later full redraw (workspace switch, or the next mode change)
   shows the right picture (`d3-bcd.png`).
4. `series/`, `series-grid.png`, `run3/`: an attempted fix that repeated the
   whole picture after a mode change. Worse. Patch:
   `diag2-and-refresh-attempt.patch` (not committed to the source).
5. `off-run/`, `off-run-grid.png`: the same series with `gpu_compositor false`.
   All normal; VirtualBox follows each hint within about 20 ms.
6. `on-run4/`: compositor on, timed logs. `SetMode guard after 23423 ms`,
   then 15500, 15474, 29177 ms: `SetMode` waited for the compose slot. The
   numbers are the "refresh period" from pixel clock 60 (1437 x 649 x 1000 /
   60 us = 15.5 s; 1920 x 922 = 29.5 s), slept with the slot held.
7. `fix-run/`, `fix-run-grid.png`: the fix (compose_period.h, the wait moved
   out of the compose slot), default settings. Fresh start and sign-in
   (`fix-run/b-signed.png`) and all eight series screenshots are normal; the
   syslog has `compose period unknown: pixel clock 60 kHz, total 1023 x 767`
   and `frame follows the screen ..., period 0 us` for every change.
8. `qemu/`, `qemu-grid.png`: QEMU (KVM, std VGA, Lavapipe), a planted job
   switched to 1280x800, 800x600 and 1024x768. Normal, each `screenmode`
   returned in 0 s, periods 13334 and 16666 us (`qemu/qemu-mode.txt`).

`diag.patch` is the logging used in runs 1 to 3.
