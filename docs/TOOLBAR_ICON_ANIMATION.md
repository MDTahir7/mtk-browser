# Toolbar Shields icon animation

The Shields button plays a short 3D sequence when the pointer rests on it: the
shield is drawn, two swords slide out from behind it and meet, and a tick
appears. This document records how that artwork is produced, because the shipped
file is a binary that no build step recreates.

## What ships

| Path | Role |
|---|---|
| `native/resources/mtk_shield_hover.png` | Rendered frame strip that ships (committed) |
| `scripts/build_icon_strip.py` | Packs a rendered PNG sequence into that strip |
| `native/src/icon_strip.{h,cpp}` | Decodes, scales and blits one frame per repaint |

The 3D source asset is **not** in this repository, and must never be added. See
the licensing section below.

## How it is delivered

The chrome is Win32, so it has neither a video decoder nor a hosted browser to
play a loop in. The animation is therefore a single PNG whose square frames sit
side by side: `mtk_shield_hover.png` is 2880x96, which is 30 frames of 96 px.

`native/src/icon_strip.cpp` decodes it once with the Windows Imaging Component,
scales the whole strip to the size the button needs with Fant resampling, caches
that scaled copy per size, and composites one frame with `AlphaBlend`. Frames are
premultiplied, so rounded edges stay clean over the gradient toolbar. Scaling per
size is what keeps the icon sharp at 100%, 125%, 150% and 200% display scaling.

Hover is observed on the button itself, because an owner-draw control repaints
only when Windows asks it to. `BrowserWindow::ShieldProc` arms `TME_LEAVE` on
every `WM_MOUSEMOVE` and drives a 33 ms timer, which is one frame at 30 fps.

Playback is **one pass that holds the last frame**, not a loop. The final frame is
the assembled state — swords crossed, tick shown — so holding it looks
deliberate. Looping would drag the swords back into the shield and read as a
glitch. Leaving the button resets to frame 0, the bare shield.

A missing, oversized or wrongly shaped strip is not an error: `Strip::Load` fails
and the button falls back to its flat glyph. `scripts/stage_release.ps1` still
fails the release build in that case, so a broken asset cannot ship silently.

## Sequence

At 30 fps, one second total:

| Frames | What happens |
|---|---|
| 1-8 | Bare shield. The swords are tucked behind it and scaled down, so they are hidden by the shield silhouette. |
| 9-16 | The swords slide out along their own diagonals, growing and straightening as they go. |
| 17-18 | Impact: a 6% overshoot on frame 17 and a slight recoil on frame 18. |
| 19-27 | The tick scales up from nothing, overshoots to 1.18 and settles. |
| 28-30 | Hold, which is also the resting pose while the pointer stays on the button. |

## Regenerating the strip

1. Open the source asset in Blender and repoint its textures to the downloaded
   texture folder; the shipped `.blend` uses relative paths that will not resolve.
2. Separate the mesh by loose parts and join them into four objects: the shield,
   the two swords, and the tick. Set each origin to its own bounds centre.
3. Set up the icon render: EEVEE Next, transparent film, `Standard` view
   transform, an orthographic camera fitted to all four parts, and three low
   suns (white key, blue fill, violet rim).
4. Tint to brand: desaturate the asset albedo, then multiply by `#7c5cff`, the
   accent already used for the badge and the block page shield. Tinting this way
   fixes the hue while keeping the sculpted light and shade of the texture.
5. Keyframe the sequence above and render 30 PNG frames at 96 px.
6. Pack them:

   ```
   python scripts/build_icon_strip.py build/shield_frames native/resources/mtk_shield_hover.png --size 96
   ```

7. Update the expected frame count in `tests/icon_strip_tests.cpp` if it changed,
   then run `scripts/run_tests.ps1`.

Blender and Pillow are build-time tools only. The packed strip is committed, so an
ordinary build needs neither.

## Licensing

The current artwork is derived from a Magnific (formerly Freepik) 3D asset
downloaded under the account holder's subscription. The vendor confirmed this
exact use: a PNG rendered from a downloaded 3D asset may be used as a toolbar
button icon in a desktop application UI, as a secondary element of the interface.

Three constraints follow, and they are recorded in `THIRD_PARTY_NOTICES.md`:

- The `.blend` and texture files must never be committed here, shipped in the
  installer, or handed to anyone. Only rendered output may be delivered.
- The artwork must not be used as a logo, trademark, or application icon. The
  application icon is a separate, product-owned file.
- It stays a secondary interface element, never a product's main visual.

Because those constraints block future branding use, a product-owned shield
should be modelled before this artwork appears anywhere near brand identity.

## Verified

- The shipped strip loads as 30 square frames, draws every frame, wraps
  out-of-range indices, and scales to 24, 30, 36 and 48 px, all covered by
  `tests/icon_strip_tests.cpp` against the real file.
- Frame measurements confirm the sequence: coverage is 35% while only the shield
  shows, rises to 51.8% once the swords are out, peaks at 54.6% on the impact
  frame, and the tick region brightens from 110 to 138 as the tick appears.
- With the animation running, the browser stayed responsive across forced
  repaints and settled when the pointer left.
- Not verified by eye in this environment: the browser window opens off-screen
  under automation, so on-screen appearance still needs a human check.
