# M12: missing Deskbar leaf with Skia drawing on Lavapipe (findings, 16 Nov 2026)

Branch `claude/m12-buffer` (draft PR #4316). QEMU, 12 cores, settings `gpu_draw true`, `gpu_debug true`, `gpu_selftest true`, `gpu_compositor_lavapipe true`, Sign in by `m12-boot-keys.sh`, a screenshot every 10 s from 130 s to 220 s. Each image shows the Deskbar corner of those screenshots (two rows of frames per boot). Not tested on the 5625U, not tested on RADV.

## Result

The leaf is lost by Skia's destination read, not by the window buffers, the texture or the copies on a resize.

The leaf (`BarMenuTitle`, 28x22 bitmap, `B_OP_ALPHA`, `B_PIXEL_ALPHA`, `B_ALPHA_OVERLAY`, no scale) goes through the R5 blender (`SkiaPainter::_SpecialPaint`, special 5 = Painter's `Bgr32Alpha`, drawn by `_DirectDraw`). That blender reads the destination; Graphite on Vulkan does that as an input attachment with a barrier (`VulkanCaps::getDstReadStrategy()` returns `kReadFromInput`). On Lavapipe, when a run holds many draws, that read does not reliably see the drawing before it: the leaf comes out partly, not at all, or with other pixels.

All 8 call sites of `_SpecialPaint` / `_BlenderPaint` use this read (bitmaps, text, fills), so the same can hit labels and icons; it fits the missing labels of the 5625U boot 3. Whether RADV is affected is not known.

## Measurements

| Run | Condition | Leaf | Image |
|---|---|---|---|
| b4 (earlier session) | buffers start transparent | missing | `b4-transparent-leaf-missing.png` |
| b5 (earlier session) | buffers start opaque black | present | `b5-opaque-black.png` |
| l1, l2, l4-l8 | transparent, no delay | present | `l6-l8-leaf-present.png` |
| l3 | transparent, no delay | missing (top black, later gray without leaf) | `l3-transparent-leaf-missing.png` |
| p1 | Painter (`gpu_draw false`) | present | `p1-painter.png` |
| d1, d2 | 300 ms delay in every Skia run (test build) | missing 2/2, other pixels in the leaf slot | `d1-d2-delay-leaf-lost.png` |
| f1 | delay + resize waits for the run in flight | missing | `f1-delay-wait-before-resize-leaf-lost.png` |
| r2 | delay + GPU readback of the leaf texture | missing; texture correct (85 blue, 255 opaque pixels) | - |
| r3 | delay + leaf draw through R5's copy path (`_Composite`) | present in every frame and readback | `r3-delay-copy-path-leaf-present.png` |

- Without the delay the leaf is lost in 1 of 8 boots; with the delay in 3 of 3 (d1, d2, f1). The buffer clearing (b4 against b5) was one boot per condition and is not the cause.
- Deskbar has no glass (`opaque 1` in the trace); the compositor's glass shader is not involved.
- The leaf texture is right on the GPU every time (r2); readbacks of different windows in one run never share memory (r1); the copy of the old image on a resize carries the background (r1, r2).
- Waiting for the run in flight before a resize does not help (f1).
- The tray icons are all there in every boot, Painter too; only their order over two rows differs (`tray-order-p1-l5-l6-l7-l8.png`, final frames of p1, l5, l6, l7, l8). Not a drawing loss.

## Open

Which fix: the copy path for every destination-reading draw on Lavapipe only (RADV unchanged until measured), everywhere, only for bitmaps, or Skia's `kTextureCopy` on Lavapipe. The cost of the copy path on Lavapipe is not measured. The 300 ms delay, the traces and the copy-path experiment are not in this commit.
