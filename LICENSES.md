# Licence Audit — Milestone 0 (Preliminary)

**Status:** no Chromium source, `DEPS`, or pinned revision is present. This is a risk assessment, not a complete component inventory or legal advice. A qualified open-source licensing lawyer must approve each release bill of materials.

## Chromium main licence
The upstream top-level Chromium licence is BSD-3-Clause. It permits proprietary distribution, including modified binaries, provided its conditions and copyright/disclaimer notices are retained. Confirm the exact `chromium/LICENSE` text and commit hash before any release: [upstream licence](https://github.com/chromium/chromium/blob/main/LICENSE).

## Distribution policy
| Licence family | Default product policy | Distribution action |
|---|---|---|
| BSD/MIT/Apache-2.0/ISC/ICU | Allowed after review | Keep required copyright, licence, NOTICE and patent text. |
| MPL-2.0 | Requires explicit legal approval | Publish source for modified MPL-covered files when distributed; keep it out of proprietary modules by default. |
| LGPL | Requires explicit legal approval | Meet applicable notice, corresponding-source and relinking/replacement obligations; architecture must be reviewed. |
| GPL/AGPL | Prohibited by default | Do not ship or link it until counsel approves a specific compliant distribution plan. |
| Fonts/codecs/patent licences | Requires explicit legal approval | Verify trademark, patent-pool, codec and redistribution terms separately. |

## Confirmed / unverified state
- **Confirmed for the proposed upstream foundation:** Chromium main licence is BSD-3-Clause, subject to commit-level verification.
- **Not yet verified:** exact third-party libraries, licenses, build flags, codecs, fonts, and platform binaries. No claim is made that the final product has no GPL/LGPL/MPL material.
- **Existing Electron prototype:** out of scope for the target product and must not be treated as a Chromium distribution inventory.

## Release gate
Pin a Chromium commit and `DEPS` revision; generate a complete SBOM and notices from that checkout; identify every copyleft component and linkage mode; obtain legal sign-off; archive the signed inventory with the release. Chromium tracks third-party code to support licence compliance and attribution: [upstream guidance](https://chromium.googlesource.com/chromium/src/+/1b1f5fcd8d83eb29bd4531eeff59b178cf2a0eb8/docs/adding_to_third_party.md).

Content was rephrased for compliance with licensing restrictions.