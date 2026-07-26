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