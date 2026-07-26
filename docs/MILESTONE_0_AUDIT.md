# Milestone 0: Environment and Feasibility Audit

**Date:** 2026-07-23. **Boundary:** no Chromium clone, download, build, or source modification was performed.

## Verified machine facts
| Area | Result |
|---|---|
| OS | Windows 11 Home Single Language, 64-bit, build 26200 |
| CPU | Intel Core i5-13450HX, 10 cores / 16 logical processors |
| RAM | 23.71 GiB physical; 27 GiB pagefile |
| Free storage | C: 56.48 GiB; E: 115.02 GiB; F: 98.08 GiB; no single volume is adequate |
| Git | 2.50.0; Git LFS 3.7.0 |
| Python | 3.10–3.14 installed; default 3.14.3 |
| depot_tools | Not installed / not on PATH |
| Windows C++ tooling | Visual Studio Build Tools 2026 18.4.2 and Windows SDK root detected |
| Java | Oracle JDK 24.0.2; `JAVA_HOME` unset |
| Android SDK | Detected at `%LOCALAPPDATA%\Android\Sdk`; API 34–36.1, build-tools 35–36.1, NDK 27–28 |
| Android host support | Windows is not the supported upstream environment for Chromium Android builds |

## Suitability
**Not suitable as currently configured.** CPU is acceptable and the Windows compiler tooling is promising for later Windows work. RAM is below the recommended 32 GiB practical baseline, and the largest free volume is 115 GiB—far below a safe full Chromium work area. Do not use the existing Electron prototype as the target product; it remains untouched.

## Capacity plan
Reserve one fast NTFS/Linux filesystem with **at least 350 GiB free for one target**; **500 GiB+** is recommended for source, `.git`, `gclient`/CIPD caches, generated files, symbols, multiple output directories and recovery headroom. Plan 32 GiB RAM minimum (64 GiB preferred for faster parallel builds). Android-first work needs a dedicated supported x86-64 Linux host/VM with the same capacity; do not assume WSL is supported without revision-specific confirmation.

## Missing / unverified prerequisites
`depot_tools`; a pinned Chromium revision; Linux Android build host; sufficient single-volume storage; explicit `JAVA_HOME`/revision-compatible JDK configuration; actual Windows SDK/debugger/component version validation after sync; remote-build/CI signing and Android keystore plan; complete licence/SBOM audit; Git repository initialization and access controls.

## Proposed repository tree
```text
chromium/                 # pinned, clean upstream worktree
vendor/                   # approved manifests, licences, signatures, SBOM inputs
patches/chromium/         # narrow rebaseable adapters
proprietary/
  browser_branding/  adblock/  safe_browsing_policy/  adult_domain_filter/
  search_enforcement/  content_classifier/  image_moderation/
  parental_controls/  policy_updates/  block_page_ui/  audit_events/
docs/adr/                 # decisions and release evidence
```

## Next action
Do not run a Chromium fetch yet. First provision a 1 TB+ supported Linux build host (or reclaim a single 500 GiB+ volume), install `depot_tools` only after approval, choose/pin a Chromium revision, then obtain legal approval to start Milestone 1.