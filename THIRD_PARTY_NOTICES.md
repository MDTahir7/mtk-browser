# Third-Party Notices — Milestone 0

## Release status
No Chromium checkout, patch set, or new third-party dependency was downloaded or added in Milestone 0. Therefore this file is **not** the final notice bundle and cannot list all components of a future Chromium binary.

The current workspace contains a legacy Electron prototype. Its bundled Electron/Chromium notices are separate legacy artifacts and are not a substitute for notices for the planned direct-Chromium product.

## Required pre-release notice process
1. Pin the exact Chromium commit and record its `DEPS` graph.
2. Produce an SBOM covering Chromium, `third_party`, platform packages, codecs, fonts, build-produced static code, proprietary approved dependencies, and Android/Windows artifacts.
3. Run the selected Chromium revision's licence/credits tooling and preserve its output, including a user-accessible Open Source Licences/Credits page.
4. Copy every required licence, copyright and NOTICE text verbatim into the installer, application resources, and distribution archive as the applicable licence requires.
5. Record component name, version/commit, source URL, licence expression, linkage/distribution form, notice location, and legal-review status.
6. Regenerate this document for every release; do not reuse a notice list from another Chromium revision.

## Preliminary licence-risk register
| Class | Exact component set | Status | Required action |
|---|---|---|---|
| Chromium top-level BSD-3-Clause | Upstream engine | Preliminary verified | Retain licence and copyright notices. |
| Permissive third parties | Unknown until pinned `DEPS` audit | Unverified | Inventory and retain attribution/NOTICE files. |
| GPL/LGPL/MPL/other copyleft | Unknown until pinned `DEPS` audit | Unverified | Identify, determine linkage, obtain legal approval, and satisfy or exclude. |
| Proprietary modules | Product-owned source | Planned | Keep separate; do not copy Brave Shields or other MPL code. |

No dependency may be introduced that could impose proprietary-source disclosure without documented legal approval.

## Licensed artwork in the shipped application

| Component | Source | Form that ships | Licence constraints |
|---|---|---|---|
| Toolbar Shields button animation (`native/resources/mtk_shield_hover.png`) | Magnific (formerly Freepik) 3D asset `security-shield-icon-009`, downloaded under the account holder's subscription | Rendered PNG frame strip only | Usable as a secondary element of the application interface. Confirmed with the vendor for this exact use: a PNG rendered from a downloaded 3D asset may be used as a toolbar button icon in a desktop application UI. |
| Block page shield loop (`native/resources/mtk_block_shield.webm`), Shields panel beacon (`native/resources/mtk_siren_alert.webm`) | Product-owned Blender scenes in `assets/blender/` | Rendered WebM | Product-owned; no third-party restriction. |

Rules that must not be broken for the Magnific-derived artwork:

1. **No source redistribution.** The `.blend` and texture files must never be committed to this repository, placed in the installer payload, or delivered to anyone. Only rendered output ships. The current pipeline satisfies this: the source asset stays outside the repository and only the PNG strip is committed.
2. **No logo or trademark use.** This artwork must not become the application icon, brand mark, store listing icon, or any registered trademark. The application icon (`native/resources/mtk_browser.ico`) is deliberately a separate, product-owned asset.
3. **Secondary element only.** The artwork stays as interface decoration for the Shields button. It must not become the main visual of a product, a sticker or filter that users manipulate, or an item offered for sale on its own.
4. **Copyright stays with the original author.** No ownership may be claimed over the asset or renders derived from it.
5. **Attribution depends on the plan.** Free-tier accounts must credit Magnific in the application credits or store description; Premium and higher do not. Re-check this before publishing if the subscription tier changes.

Because rules 1 and 2 constrain future branding work, a product-owned replacement should be rendered before this artwork is used anywhere near brand identity.
