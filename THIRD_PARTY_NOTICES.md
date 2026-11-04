# Third-Party Notices & Asset Audit

## Release Status (0.3.0 Preview)

MTK Browser 0.3.0 Preview uses the Chromium Embedded Framework (CEF 150.0.14 / Chromium 150.0.7871.129) Windows 64-bit distribution pinned in `vendor/CEF_PIN.txt`. Upstream Chromium and CEF are distributed under the BSD 3-Clause license.

The repository includes a legacy Electron prototype under `src/` maintained solely for historical comparison. Its dependencies are isolated in `package.json` and are not packaged into the native Windows installer.

---

## Required Pre-Release Notice Process

1. Pin the exact Chromium/CEF commit and record its binary distribution SHA-1 (see `vendor/CEF_PIN.txt`).
2. Maintain an SBOM covering CEF runtime binaries, third-party libraries, codecs, and platform integrations.
3. Preserve upstream CEF licenses and credits (`vendor/cef-sdk/LICENSE.txt` and `vendor/cef-sdk/CREDITS.html`), staging them into the application distribution under `Legal/`.
4. Copy every required license, copyright, and notice text verbatim into the installer and application distribution.
5. Record component name, version/commit, source URL, license expression, linkage/distribution form, notice location, and legal-review status.
6. Regenerate this document for every major release; do not reuse notice lists across major Chromium updates.

---

## Component License Register

| Component | Source / Project | License | Status | Distribution & Linkage |
|---|---|---|---|---|
| **Chromium Core** | Google / The Chromium Authors | BSD-3-Clause | Verified | Dynamic linkage via CEF runtime binaries |
| **Chromium Embedded Framework (CEF)** | Marshall A. Greenblatt / CEF Authors | BSD-3-Clause | Verified | Static `libcef_dll_wrapper` + dynamic `libcef.dll` |
| **Windows Imaging Component (WIC) & DirectX/DWM** | Microsoft Corporation | Windows SDK / OS Component | System Component | Dynamically linked Windows system libraries |
| **Windows Cryptography API (CNG)** | Microsoft Corporation | Windows SDK / OS Component | System Component | Dynamically linked Windows system libraries (`bcrypt.lib`, `crypt32.lib`) |
| **Windows App Runtime / Windows Hello** | Microsoft Corporation | Windows SDK / OS Component | System Component | Dynamically linked Windows system library (`windowsapp.lib`) |

---

## Licensed & Third-Party Artwork Audit

### Shipped Media & 3D Artwork

| Component | Source | Form that Ships | License Status & Constraints |
|---|---|---|---|
| **Toolbar Shields button animation** (`native/resources/mtk_shield_hover.png`) | Magnific (formerly Freepik) 3D asset `security-shield-icon-009`, downloaded under account subscription | Rendered PNG frame strip only | Usable as a secondary element of the application interface. Confirmed with vendor: PNG rendered from a downloaded 3D asset may be used as a toolbar button icon in a desktop UI. **Requires human legal review before commercial redistribution.** |
| **Block page shield sequence** (`native/resources/mtk_block_shield.webm`) | Same Magnific asset `security-shield-icon-009`, rendered from same Blender scene | Rendered VP9 WebM only | Same constraints as toolbar strip. The earlier product-owned scene `assets/blender/mtk_block_shield.blend` is retained so this surface can be returned to fully owned artwork. |
| **Shields panel beacon** (`native/resources/mtk_siren_alert.webm`) | Product-owned Blender scene in `assets/blender/mtk_siren_alert.blend` | Rendered WebM | Product-owned; no third-party restrictions. |
| **3D Toolbar & Navigation Icons** (`native/resources/mtk_*_3d.webp`, `mtk_*_3d_still.webp`, `mtk_*_hover.png`) | Derived from commercial 3D asset packs (e.g. `search-icon-002`, `home`, `store`, `notes`, `download`, `history`, `settings`, `bookmarks`, `focus`, `refresh`, `screenshot`, `ask_ai`, `publishing`) | Rendered WebP animations, still WebP glyphs, and PNG hover strips | Rendered raster representations only. **Flagged for legal review**: Raw 3D files (`.blend`) and textures must remain outside the repository; open-source licensing does NOT convey sublicensing rights to underlying 3D meshes. |
| **Application Icons & Brand Logo** (`assets/logo/*`, `native/resources/mtk_browser.ico`, `native/resources/mtk_browser_logo.png`) | Project-owned artwork designed specifically for MTK Browser | ICO, PNG, SVG, WebP | Proprietary brand assets of MTK Browser. Excluded from general MIT re-licensing without attribution. |

---

## Mandatory Constraints for Third-Party Derived Artwork

The following rules are binding on the repository and release pipeline:

1. **No Source Redistribution**: The `.blend` source scenes, 3D geometry meshes, and raw texture files must **never** be committed to this repository, placed in installer payloads, or distributed publicly. Only rendered 2D raster output (PNG/WebP/WebM) ships.
2. **No Logo or Trademark Use**: Third-party stock artwork must never be used as the application icon, primary brand logo, registered trademark, or store listing icon. The official application icon (`native/resources/mtk_browser.ico`) is a separate, project-owned design.
3. **Secondary Element Only**: Stock-derived artwork is restricted to secondary interface decoration (e.g., hover states on toolbar buttons). It must not be marketed as a standalone asset, sold separately, or incorporated into user-customizable asset packs.
4. **Original Author Copyright**: Copyright in underlying 3D models remains with the original stock creators and platforms. No proprietary copyright claim is asserted over third-party meshes.
5. **Attribution & Subscription Verification**: Maintainers must verify subscription status upon release. If free-tier terms apply, appropriate attribution must be surfaced in application credits or release notes.
6. **Roadmap to 100% Owned Media**: To eliminate third-party licensing dependencies entirely, all stock-derived 3D toolbar icons are scheduled for replacement with fully original, in-house Blender models in future milestones.
