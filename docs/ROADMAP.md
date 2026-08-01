# MTK Browser Roadmap to Chrome/Brave Level

Status legend: `[ ]` not started, `[~]` in progress, `[x]` done and verified.

Rule: security fundamentals are never Pro-gated. Nothing is marked `[x]` until it
has been actually built and verified, and any limitation is written down.

## Tier 1 — Release blockers

- [ ] **T1.1 Chromium sandbox enabled**
  - CEF 150 requires `bootstrap.exe` + application DLL packaging instead of a
    single EXE. Today `main.cpp` sets `no_sandbox=true` and root `CMakeLists.txt`
    force-disables `USE_SANDBOX`.
  - Work: new app-DLL target, exported entry points, staging + installer updates,
    then re-verify renderer/GPU/utility processes.
  - Touches: `CMakeLists.txt`, `native/CMakeLists.txt`, `native/src/main.cpp`,
    `scripts/stage_release.ps1`, `installer/MTKBrowser.iss`.
- [ ] **T1.2 Code signing for release artifacts**
  - Signing hooks already exist in `scripts/build_installer.ps1`
    (`MTK_SIGN_CERT_SHA1`, `MTK_SIGNTOOL`, `MTK_REQUIRE_SIGNING`).
  - Remaining: obtain an OV/EV certificate, sign EXE + installer, verify with
    `signtool verify /pa /all`. External purchase required.
- [ ] **T1.3 Secure auto-update**
  - In progress in the working tree (`native/src/updater.*`,
    `native/src/update_public_key.h`, `scripts/publish_update.ps1`).
  - Must have: signed manifest, key rotation plan, atomic install, rollback,
    and no silent downgrade.
- [ ] **T1.4 Crash handling and recovery**
  - Detect unclean shutdown, offer tab restore, and capture a local crash record.
  - Session file already exists (`native/src/session_store.cpp`); needs a
    "was the last exit clean?" marker.
- [ ] **T1.5 Default browser + file/protocol associations**
  - Register `http`, `https`, `.htm`, `.html` ProgIDs and Windows
    `RegisteredApplications` capabilities; add an in-app "Make default" action.
  - Touches: `installer/MTKBrowser.iss`, small native helper.
- [ ] **T1.6 Multi-window support**
  - One `BrowserWindow` per top-level window, per-window tab list, window state
    persistence, and a profile-scoped singleton that focuses the right window.
- [~] **T1.7 Automated tests + CI**
  - Done: 41-case logic suite under `tests/`, run by `scripts/run_tests.ps1`,
    covering navigation/request policy, download rules and file-name hardening,
    and session-restore persistence. Verified passing locally.
  - Remaining: a CI workflow that builds Release and runs the suite on every
    push, plus coverage for CEF-dependent paths that need a live browser.

## Tier 2 — Daily-use parity

- [ ] **T2.1 Private/Incognito window** (separate in-memory CEF request context)
- [ ] **T2.2 Multiple profiles** with a profile picker
- [ ] **T2.3 Password manager + autofill** (OS-encrypted storage, never plaintext)
- [ ] **T2.4 Bookmarks bar, folders, and Chrome import**
- [ ] **T2.5 Full History page with search and date grouping**
- [ ] **T2.6 Tab UX**: drag-reorder, pin, mute, duplicate, middle-click close,
      `Ctrl+1..9`, `Ctrl+Shift+T`, overflow scrolling
- [ ] **T2.7 Printing**: print, print preview, Save as PDF
- [ ] **T2.8 Built-in PDF viewer** (PDFs currently download instead of opening)
- [ ] **T2.9 Spellcheck** with dictionary suggestions in editable fields
- [ ] **T2.10 Per-site settings**: permissions, cookies, remembered zoom
- [ ] **T2.11 Clear browsing data dialog** (history, cookies, cache, downloads)
- [ ] **T2.12 Media**: picture-in-picture, media keys, autoplay policy
- [ ] **T2.13 Downloads polish**: toolbar popup panel, drag-out, cross-restart resume
      (cross-restart resume needs an own resumable HTTP path; CEF callbacks only
      cover the live process)
- [ ] **T2.14 Full-screen mode (F11)**
- [ ] **T2.15 Accessibility**: UI Automation/screen reader, high contrast, focus order
- [ ] **T2.16 Localization**: complete Hindi + English UI strings

## Tier 3 — Brave-level differentiators

- [ ] **T3.1 Extension support** (largest single effort)
- [ ] **T3.2 Sync** across devices, end-to-end encrypted
- [ ] **T3.3 Per-site Shields panel** (in progress in the working tree) + custom filters UI
- [ ] **T3.4 Fingerprinting protection**
- [ ] **T3.5 HTTPS-only mode + DNS-over-HTTPS provider choice**
- [ ] **T3.6 Managed, updatable filter and category feed** (signed, with fail-safe
      fallback to the bundled list)
- [ ] **T3.7 Performance**: background tab freeze/discard, startup and memory work
- [ ] **T3.8 Other platforms**: macOS, Linux, Android

## Tier 4 — Product and operations

- [ ] **T4.1 Website, download page, privacy policy, support channel**
- [ ] **T4.2 Release channels (stable/beta) + automated release notes**
- [ ] **T4.3 Automated third-party license/notice generation**
- [ ] **T4.4 MTK Browser Pro feature plan** (security features stay free)

## Recommended order

1. T1.3 auto-update (already in progress)
2. T1.1 sandbox
3. T1.2 code signing
4. T1.6 multi-window + T2.6 tab UX
5. T2.1 private window, T2.8 PDF viewer, T2.7 printing

## Coordination note

Large parts of the UI live in `native/src/browser_window.cpp`. While that file has
uncommitted changes in the working tree, new work should prefer new files or
low-conflict areas (installer, scripts, tests) to avoid clobbering work in progress.
