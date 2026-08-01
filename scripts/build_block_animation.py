"""Packs the rendered block-page shield loop into an animated WebP.

The 3D source lives in Blender (see docs/BLOCK_PAGE_ANIMATION.md); this script
only turns the rendered RGBA PNG sequence into the single asset that ships in
native/resources. The generated file is committed, so a normal build needs
neither Blender nor Pillow.

Usage:
    python scripts/build_block_animation.py <frame-dir> <output.webp> [--fps 30]

Requires Pillow (build-time only, never shipped).
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:  # pragma: no cover - build tool only
    sys.exit("Pillow is required: python -m pip install pillow")

# Alpha below this counts as empty when measuring the used area of the canvas.
ALPHA_FLOOR = 6
# Breathing room kept around the content so the glow is not clipped.
PADDING = 6


def load_frames(frame_dir: Path) -> list[Image.Image]:
    paths = sorted(frame_dir.glob("*.png"))
    if not paths:
        sys.exit(f"no PNG frames found in {frame_dir}")
    return [Image.open(p).convert("RGBA") for p in paths]


def union_box(frames: list[Image.Image]) -> tuple[int, int, int, int]:
    """Smallest box containing visible pixels of every frame, kept square."""
    left = top = 1 << 30
    right = bottom = 0
    for frame in frames:
        alpha = frame.getchannel("A").point(lambda v: 255 if v >= ALPHA_FLOOR else 0)
        box = alpha.getbbox()
        if not box:
            continue
        left, top = min(left, box[0]), min(top, box[1])
        right, bottom = max(right, box[2]), max(bottom, box[3])
    if right <= left:
        sys.exit("every rendered frame was empty")

    width, height = frames[0].size
    left = max(0, left - PADDING)
    top = max(0, top - PADDING)
    right = min(width, right + PADDING)
    bottom = min(height, bottom + PADDING)

    # Square the box around its own centre so the asset keeps a 1:1 aspect.
    side = max(right - left, bottom - top)
    cx, cy = (left + right) // 2, (top + bottom) // 2
    left = max(0, min(width - side, cx - side // 2))
    top = max(0, min(height - side, cy - side // 2))
    return left, top, left + side, top + side


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("frame_dir", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--fps", type=int, default=30)
    parser.add_argument("--size", type=int, default=256,
                        help="edge length of the packed square asset")
    parser.add_argument("--quality", type=int, default=72)
    args = parser.parse_args()

    frames = load_frames(args.frame_dir)
    box = union_box(frames)
    packed = [f.crop(box).resize((args.size, args.size), Image.LANCZOS)
              for f in frames]

    args.output.parent.mkdir(parents=True, exist_ok=True)
    packed[0].save(
        args.output,
        format="WEBP",
        save_all=True,
        append_images=packed[1:],
        duration=round(1000 / args.fps),
        loop=0,
        quality=args.quality,
        method=6,
        allow_mixed=True,
    )
    size_kb = args.output.stat().st_size / 1024
    print(f"frames={len(packed)} crop={box} size={args.size}px "
          f"quality={args.quality} -> {args.output.name} {size_kb:.1f} KB")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
