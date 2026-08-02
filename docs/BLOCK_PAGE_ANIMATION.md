# Block page animation

The block page (`BlockPage()` in `native/src/browser_client.cpp`) opens with a
rendered 3D sequence: the shield is drawn, two swords slide out from behind it and
meet, then a tick appears and the pose is held. This document records where that
asset comes from and how to regenerate it, because the shipped file is a binary
that no build step recreates.

The toolbar Shields button plays the same sequence as a PNG frame strip. See
`docs/TOOLBAR_ICON_ANIMATION.md`; the two assets are rendered from one Blender
scene so the block page and the button stay visually consistent.

## What ships

| Path | Role |
|---|---|
| `native/resources/mtk_block_shield.webm` | Rendered VP9 loop with alpha that ships (committed, 25 KB) |
| `native/resources/mtk_shield_hover.png` | The same sequence as a frame strip for the toolbar |
| `assets/blender/mtk_block_shield.blend` | Earlier product-owned shield scene, retained so it can be restored |

`native/CMakeLists.txt` copies the `.webm` beside the executable, the same way it
stages `mtk_filters.txt`, and `scripts/stage_release.ps1` fails the build if the
file is missing or larger than 700 KB.

## How it is delivered

`BlockPage()` returns a single `data:text/html` URL. That URL cannot reference a
local file, so the browser process reads the asset once on the first blocked
navigation, checks the WebM magic number, base64-encodes it with Windows CNG
(`CryptBinaryToStringA`) and splices it into the page as
`data:video/webm;base64,...`. At 25 KB the resulting URL is far inside Chromium's
2 MB limit.

`mtk::policy::EvaluateRequest` is fail-closed for every non-`http(s)` scheme, so
`browser_client.cpp` allows exactly one extra case: a `RT_MEDIA` request whose URL
starts with `data:video/webm;base64,`. A `data:` URL carries its own bytes and
reaches no network, so this does not widen network access.

The video plays **once and holds its last frame**; it is deliberately not looped.
The sequence ends with the swords crossed and the tick shown, and that assembled
pose is the point. Looping would drag the swords back into the shield every second
on a page the reader is still looking at.

Two fallbacks keep the page safe: a missing, oversized or non-WebM asset falls
back to the static shield glyph, and `prefers-reduced-motion: reduce` hides the
video and shows the glyph instead.

## Sequence

At 30 fps, one second total:

| Frames | What happens |
|---|---|
| 1-8 | Bare shield. The swords are tucked behind it and scaled down, so the shield silhouette hides them. |
| 9-16 | The swords slide out along their own diagonals, growing and straightening. |
| 17-18 | Impact: a 6% overshoot, then a slight recoil. |
| 19-27 | The tick scales up from nothing, overshoots to 1.18 and settles. |
| 28-30 | Hold, which is the pose the page keeps showing. |

## Regenerating the asset

The scene setup, part separation and brand tint are described step by step in
`docs/TOOLBAR_ICON_ANIMATION.md`. Once that scene is prepared:

1. Set the render to 256x256 and render frames 1-30 as an RGBA PNG sequence.
2. Pack them with ffmpeg:

   ```
   ffmpeg -y -framerate 30 -i frames/f_%04d.png \
     -c:v libvpx-vp9 -pix_fmt yuva420p -auto-alt-ref 0 \
     -b:v 0 -crf 30 -row-mt 1 native/resources/mtk_block_shield.webm
   ```

   `yuva420p` keeps the alpha channel and `-auto-alt-ref 0` is required for alpha
   in libvpx. Verify the round trip before committing: decode a frame and confirm
   the alpha channel still spans 0-255. `ffprobe` reports the base stream as
   `yuv420p` even when alpha is present, because VP9 alpha travels as a separate
   block addition, so check a decoded frame rather than the stream metadata.

ffmpeg, Blender and Pillow are build-time tools only. The generated asset is
committed, so an ordinary build needs none of them.

## Why VP9 with alpha

An animated WebP of the same loop is several times larger because WebP stores
alpha losslessly per frame. VP9 with `yuva420p` reaches 25 KB at CRF 30 with no
visible banding on the dark card, which keeps the inlined data URL small.
`scripts/build_block_animation.py` remains available if a WebP variant is ever
needed for a surface where video is unsuitable.

## Licensing

The current sequence is rendered from a Magnific (formerly Freepik) 3D asset
downloaded under the account holder's subscription, and the same constraints apply
here as for the toolbar strip. They are recorded in `THIRD_PARTY_NOTICES.md`:

- The `.blend` and texture files must never be committed here, shipped in the
  installer, or handed to anyone. Only rendered output may be delivered.
- The artwork must not be used as a logo, trademark, or application icon.
- It stays a secondary interface element, never a product's main visual.

Note the trade-off this replacement makes: the block page previously used a
product-owned scene, `assets/blender/mtk_block_shield.blend`, which carried no
third-party restriction. That scene is deliberately kept in the repository, so the
block page can be moved back to fully owned artwork whenever branding work needs
it.

## Verified

- Block page in the development build: video present, `loop` off, intrinsic
  256x256, CSS 152x152, duration 1.0 s, no media error, static glyph hidden.
- Playback observed advancing on replay from 0.213 s to 0.572 s, then reported
  `ended` and held its final frame without restarting.
- Alpha round trip checked on a decoded frame: alpha spans 0-255 with roughly half
  the frame transparent, matching the previous asset.
