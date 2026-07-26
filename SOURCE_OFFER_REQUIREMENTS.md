# Source-Offer Requirements and Distribution Controls

**Not legal advice.** This policy is a release-control baseline; counsel must validate it against the exact shipped bill of materials and jurisdictions.

## Baseline
Chromium's BSD-3-Clause top-level licence does not itself require a source offer for proprietary modifications. It does require preserving applicable licence, copyright and disclaimer notices. That fact does **not** remove obligations from third-party code selected by the pinned Chromium revision or product dependencies.

## Mandatory controls
| Licence case | Source/distribution control |
|---|---|
| BSD/MIT/Apache/ISC/ICU-style | Include required attribution, licence and NOTICE text; track patent/trademark terms. |
| MPL-2.0 | If distributed and modified, provide source for the affected MPL-covered files under MPL terms; do not blend product-private code into those files without counsel approval. |
| LGPL | Determine static/dynamic linkage and version-specific duties; provide notices and required corresponding source/relinkability before release. |
| GPL/AGPL | Exclude by default. If ever approved, provide complete corresponding source and all required installation information/offers for recipients as applicable. |
| Unknown/custom | Block release until licence and obligations are reviewed. |

## Product policy
- Proprietary modules remain under `proprietary/`; interfaces into Chromium use narrow adapters, build flags and patch files.
- No Brave Core, Brave Shields code, or other MPL-licensed implementation is copied into this repository.
- Record provenance (author, source URL, licence, commit, approval) for every imported file and filter list.
- The installer and in-product credits screen must ship the exact notices generated for that release.
- Keep source-offer artifacts, SBOM, notices, patch manifest and counsel approval for the product-support lifetime.

## Release blocker
A release is blocked if the exact component inventory, licence expression, notice package, source-offer analysis, or approval record is missing.