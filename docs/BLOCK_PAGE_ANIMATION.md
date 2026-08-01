# Block page animation

The block page (`BlockPage()` in `native/src/browser_client.cpp`) opens with a
looping 3D shield. This document records where that asset comes from and how to
regenerate it, because the shipped file is a binary that no build step recreates.

## What ships

| Path | Role |
|---|---|
| `assets/blender/mtk_block_shield.blend` | 3D source scene (committed) |
| `native/resources/mtk_block_shield.webm` | Generated asset that ships (committed) |
| `scripts/build_block_animation.py` | Optional packer for an animated-WebP variant |

`native/CMakeLists.txt` copies the `.webm` beside the executable, the same way
it stages `mtk_filters.txt`, and `scripts/stage_release.ps1` fails the build if
the file is missing or larger than 700 KB.

## How it is delivered

`BlockPage()` returns a single `data:text/html` URL. That URL cannot reference a
local file, so the browser process reads the asset once on the first blocked
navigation, checks the WebM magic number, base64-encodes it with Windows CNG
(`CryptBinaryToStringA`) and splices it into the page as
`data:video/webm;base64,...`. The result is a ~277 KB URL, well inside
Chromium's 2 MB limit.

`mtk::policy::EvaluateRequest` is fail-closed for every non-`http(s)` scheme, so
`browser_client.cpp` allows exactly one extra case: a `RT_MEDIA` request whose
URL starts with `data:video/webm;base64,`. A `data:` URL carries its own bytes
and reaches no network, so this does not widen network access.

Two fallbacks keep the page safe: a missing, oversized or non-WebM asset falls
back to the static shield glyph, and `prefers-reduced-motion: reduce` hides the
video and shows the glyph instead.

## Scene layout

Camera looks down +Y from `(0, -4.25, -0.04)`, 45 mm lens, square 320x320 frame,
`Standard` view transform (AgX lifts dark values and would wash out the panel),
`film_transparent` on, EEVEE Next.

- `Shield` — heraldic outline built in bmesh from an arched top edge, straight
  flanks and mirrored bezier curves into the point; solidified and bevelled.
  Violet metal, slight emission, and the parent of everything else.
- `ShieldPanel` — the same outline at 0.855 scale, sitting *in front* of the
  shield so the metal reads as a rim around a near-black inner face.
- `LockBody` / `LockShackle` / `LockKeyhole` — 0.42 x 0.34 x 0.16 bevelled body,
  the upper half of a torus for the shackle, and an emissive keyhole disc that
  pulses twice per loop.
- `PulseRing1..3` — thin tori that expand from 0.42 to 1.62 scale while fading,
  phase-offset by a third of the loop.
- Lights are deliberately low powered. The panel's dielectric specular sheen,
  not its albedo, is what washes it out, so bright area lights cannot be
  compensated for by darkening the base colour.

Every animated channel carries a `CYCLES` F-curve modifier with keys one period
apart (frames 1 and 91 for a 90-frame loop), which is what makes the loop
seamless.

## Regenerating the asset

1. Open `assets/blender/mtk_block_shield.blend`.
2. Render the animation as an RGBA PNG sequence (frames 1-90, 320x320).
3. Pack it with ffmpeg. The crop removes the empty margin so the encoder does
   not spend bits on transparent pixels:

   ```
   ffmpeg -y -framerate 30 -i seq/f_%04d.png \
     -vf "crop=248:248:35:35,scale=256:256:flags=lanczos" \
     -c:v libvpx-vp9 -pix_fmt yuva420p -auto-alt-ref 0 \
     -b:v 0 -crf 30 -row-mt 1 native/resources/mtk_block_shield.webm
   ```

   `yuva420p` keeps the alpha channel and `-auto-alt-ref 0` is required for
   alpha in libvpx. Verify the round trip before committing: decode a frame and
   confirm the alpha channel still spans 0-255.

ffmpeg, Blender and Pillow are build-time tools only. The generated asset is
committed, so an ordinary build needs none of them.

## Why VP9 with alpha

An animated WebP of the same loop is 660-800 KB because WebP stores alpha
losslessly per frame. VP9 with `yuva420p` reaches 202 KB at CRF 30 with no
visible banding on the dark panel, which keeps the inlined data URL small.
`scripts/build_block_animation.py` remains available if a WebP variant is ever
needed for a surface where video is unsuitable.

## Verified

- Block page in a sandboxed build: video present, intrinsic 256x256, CSS 152x152,
  `readyState` 4, `loop` and `muted` true, no media error, static glyph hidden.
- Playback measured over ~1.6 s advanced 1.600 s with a clean wrap from 2.854 s
  to 0.251 s, so the loop runs at real time and repeats without a gap.
- Live page capture reviewed at full size: no square edge around the animation
  and no colour seam against the card background.
