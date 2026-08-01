"""Packs a rendered PNG sequence into the horizontal frame strip the toolbar plays.

The browser cannot decode video in its chrome, so an animated toolbar icon ships
as one PNG whose square frames sit side by side; `native/src/icon_strip.cpp`
blits a single frame per repaint.

Usage:
    python scripts/build_icon_strip.py <frame-dir> <output.png> [--size 96]

Blender and Pillow are build-time tools only. The packed strip is committed, so
an ordinary build needs neither.
"""

from __future__ import annotations

import argparse
import pathlib
import sys

from PIL import Image

# Kept in step with kMaxFrameSize and kMaxStripWidth in native/src/icon_strip.cpp.
MAX_FRAME_SIZE = 256
MAX_STRIP_WIDTH = 4096


def build(frame_dir: pathlib.Path, output: pathlib.Path, size: int) -> int:
    frames = sorted(frame_dir.glob("*.png"))
    if not frames:
        print(f"No PNG frames found in {frame_dir}", file=sys.stderr)
        return 1
    if size > MAX_FRAME_SIZE:
        print(f"Frame size {size} exceeds the loader limit of {MAX_FRAME_SIZE}",
              file=sys.stderr)
        return 1
    if size * len(frames) > MAX_STRIP_WIDTH:
        print(f"{len(frames)} frames at {size} px exceed the loader's "
              f"{MAX_STRIP_WIDTH} px strip limit", file=sys.stderr)
        return 1

    strip = Image.new("RGBA", (size * len(frames), size), (0, 0, 0, 0))
    for index, path in enumerate(frames):
        frame = Image.open(path).convert("RGBA")
        if frame.size != (size, size):
            frame = frame.resize((size, size), Image.LANCZOS)
        strip.paste(frame, (index * size, 0))

    output.parent.mkdir(parents=True, exist_ok=True)
    strip.save(output, format="PNG", optimize=True)
    print(f"Wrote {output} ({strip.width}x{strip.height}, {len(frames)} frames, "
          f"{output.stat().st_size / 1024:.1f} KB)")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("frame_dir", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--size", type=int, default=96,
                        help="edge length of one square frame in pixels")
    arguments = parser.parse_args()
    return build(arguments.frame_dir, arguments.output, arguments.size)


if __name__ == "__main__":
    raise SystemExit(main())
