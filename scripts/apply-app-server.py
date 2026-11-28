#!/usr/bin/env python3
"""Share one app_server desktop until StartSession. Copy-only."""
from __future__ import annotations

import pathlib
import shutil
import sys

HAIKU = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "/home/nickr/r5os/haiku")
ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "distro" / "app_server"


def copy(name: str, dest: pathlib.Path) -> None:
    shutil.copy2(SRC / name, dest)
    dest.touch()
    print(f"copied {name} -> {dest}")


def main() -> int:
    dest = HAIKU / "src" / "servers" / "app" / "AppServer.cpp"
    copy("AppServer.cpp", dest)
    config = HAIKU / "src" / "servers" / "app" / "ServerConfig.h"
    copy("ServerConfig.h", config)
    local = HAIKU / "src" / "servers" / "app" / "drawing" / "interface" / "local"
    copy("AccelerantHWInterface.cpp", local / "AccelerantHWInterface.cpp")
    # M11 step 3b: SoftwareCursorFrame for the compositor's scanout copy.
    copy("AccelerantHWInterface.h", local / "AccelerantHWInterface.h")
    copy("GPUCopy.cpp", local / "GPUCopy.cpp")
    copy("GPUCopy.h", local / "GPUCopy.h")
    copy("GPUCopyBands.h", local / "GPUCopyBands.h")
    copy("r5_span_avx.h", local / "r5_span_avx.h")
    copy("local-Jamfile", local / "Jamfile")
    for name in ("Compositor.cpp", "Compositor.h", "CompositorPrivate.h",
            "CompositorVulkan.cpp", "CompositorEffects.cpp",
            "CompositorAnimations.cpp", "CompositorShare.cpp",
            "WindowBuffer.cpp", "WindowBuffer.h",
            "WindowHWInterface.cpp", "WindowHWInterface.h"):
        copy(name, local / name)
    copy("compositor/compositor_shaders.h", local / "compositor_shaders.h")
    # M12 steps 2-5: Skia draws the windows (gpu_draw true). The Skia code
    # sits next to the compositor; app_server links the static Skia.
    for name in ("SkiaGpu.cpp", "SkiaGpu.h", "SkiaPainter.cpp",
            "SkiaPainter.h", "SkiaShaders.h", "SkiaText.cpp", "SkiaDrawingEngine.cpp",
            "SkiaDrawingEngine.h", "DrawCompare.cpp", "DrawCompare.h",
            "SkiaOffscreen.cpp"):
        copy("skia/" + name, local / name)
    app = HAIKU / "src" / "servers" / "app"
    copy("app_server-Jamfile", app / "Jamfile")
    # SkiaDrawingEngine needs to know which HWInterface it is bound to.
    copy("drawing/Jamfile", app / "drawing" / "Jamfile")
    # Display step 4 (docs/DISPLAY-DCN.md): more than one screen.
    copy("drawing/HWInterface.h", app / "drawing" / "HWInterface.h")
    copy("drawing/HWInterface.cpp", app / "drawing" / "HWInterface.cpp")
    copy("R5OutputHWInterface.h", local / "R5OutputHWInterface.h")
    copy("R5OutputHWInterface.cpp", local / "R5OutputHWInterface.cpp")
    copy("drawing/Painter/Jamfile", app / "drawing" / "Painter" / "Jamfile")
    copy("drawing/Painter/Painter.cpp", app / "drawing" / "Painter" / "Painter.cpp")
    copy("drawing/Painter/bitmap_painter/DrawBitmapBilinear.h",
        app / "drawing" / "Painter" / "bitmap_painter" / "DrawBitmapBilinear.h")
    copy("ServerBitmap.h", app / "ServerBitmap.h")
    copy("ServerBitmap.cpp", app / "ServerBitmap.cpp")
    copy("ServerFont.h", app / "ServerFont.h")
    copy("font/FontCache.cpp", app / "font" / "FontCache.cpp")
    copy("font/FontCacheEntry.cpp", app / "font" / "FontCacheEntry.cpp")
    copy("font/FontStyle.h", app / "font" / "FontStyle.h")
    copy("font/FontStyle.cpp", app / "font" / "FontStyle.cpp")
    copy("agg-Jamfile", HAIKU / "src" / "libs" / "agg" / "Jamfile")
    copy("drawing/DrawingEngine.h", app / "drawing" / "DrawingEngine.h")
    copy("compositor/compose_period.h", local / "compose_period.h")
    # blend_line32() keeps the destination's alpha (Haiku left it unset).
    copy("drawing/drawing_support.h", app / "drawing" / "drawing_support.h")
    # M12 step 4: alpha masks are drawn by Skia in the GPU mode. The
    # offscreen header sits in drawing/, which both libraries include.
    copy("drawing/AlphaMask.cpp", app / "drawing" / "AlphaMask.cpp")
    copy("skia/SkiaOffscreen.h", app / "drawing" / "SkiaOffscreen.h")
    return 0


if __name__ == "__main__":
    sys.exit(main())
