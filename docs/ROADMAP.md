# MTK Browser Roadmap to Chrome/Brave Level

Status legend: `[ ]` not started, `[~]` in progress, `[x]` done and verified.

Rule: security fundamentals are never Pro-gated. Nothing is marked `[x]` until it
has been actually built and verified, and any limitation is written down.

## Tier 1 — Release blockers

- [~] **T1.1 Chromium sandbox enabled**
  - Done: `MTK_ENABLE_CEF_SANDBOX` is `ON` by default and forces `USE_SANDBOX`,
    the app builds as `MTKBrowser.dll` loaded by CEF's renamed `bootstrap.exe`,
    `main.cpp` derives `no_sandbox` from the real `sandbox_info` handle, and
    staging refuses a payload missing the DLL or with mismatched signing state.
    The 0.3.0 payload ships both files (EXE 3.1 MB, DLL 1.0 MB).
  - Remaining: confirm at runtime that renderer, GPU and utility processes are
    actually running inside the sandbox (e.g. Process Explorer integrity level /
    mitigation policies), and record the result here.
  - Touches: `CMakeLists.txt`, `native/CMakeLists.txt`, `native/src/main.cpp`,
    `scripts/stage_release.ps1`, `installer/MTKBrowser.iss`.
- [ ] **T1.2 Code signing for release artifacts**
  - Signing hooks already exist in `scripts/build_installer.ps1`
    (`MTK_SIGN_CERT_SHA1`, `MTK_SIGNTOOL`, `MTK_REQUIRE_SIGNING`).
  - Remaining: obtain an OV/EV certificate, sign EXE + installer, verify with
    `signtool verify /pa /all`. External purchase required.
- [~] **T1.3 Secure auto-update**
  - Done: `native/src/updater.*` fetches a manifest plus a detached signature
    over HTTPS, verifies the signature against the pinned key in
    `update_public_key.h`, compares versions, downloads the installer and checks
    its SHA-256 before anything is executed. A failed or unsigned manifest is
    treated as hostile. `scripts/publish_update.ps1` signs a release manifest.
  - Remaining: key rotation plan, rollback path, and an end-to-end test of a
    real published update (needs T1.2 signing to be meaningful).
- [ ] **T1.4 Crash handling and recovery**
  - Detect unclean shutdown, offer tab restore, and capture a local crash record.
  - Session file already exists (`native/src/session_store.cpp`); needs a
    "was the last exit clean?" marker.
- [ ] **T1.5 Default browser + file/protocol associations**
  - Register `http`, `https`, `.htm`, `.html` ProgIDs and Windows
    `RegisteredApplications` capabilities; add an in-app "Make default" action.
  - Touches: `installer/MTKBrowser.iss`, small native helper.
- [~] **T1.6 Multi-window support**
  - Done: windows own themselves through `BrowserWindow::Open`, are tracked in a
    registry, and only the last one to close ends the message loop. `Ctrl+N` and
    a menu entry open another window, each with its own tab list and its own DPI
    so two windows on differently scaled displays both paint correctly. One
    window owns the session snapshot (the format holds a single tab list) and
    ownership moves to a remaining window when the owner closes; update checks
    are likewise scheduled once per process, not once per window.
  - Remaining: per-window size/position persistence, dragging a tab out into a
    new window, and a session format that can hold more than one window.
- [~] **T1.7 Automated tests + CI**
  - Done: 71-case logic suite under `tests/`, run by `scripts/run_tests.ps1`,
    covering navigation/request policy, download rules and file-name hardening,
    session-restore persistence, the toolbar icon strip loader, and the Chromium
    bookmarks importer (which parses a file written by another program, so it is
    tested against malformed, deeply nested and policy-refused input), profile id
    validation, and per-site settings including a test that "Shields off" cannot
    weaken a safety rule. Verified passing locally (71 passed, 0 failed).
  - Remaining: there is no `.github/workflows` yet, so nothing runs on push.
    Needs a CI workflow that builds Release and runs the suite, plus coverage
    for CEF-dependent paths that need a live browser.

## Tier 2 — Daily-use parity

`[~]` here means the code is written and the Release build plus the logic suite
pass, but the behaviour has not been checked on screen yet. UI work is not marked
`[x]` on a compile alone.

- [~] **T2.1 Private window**
  - Done: `Ctrl+Shift+N` and a menu entry open a window backed by its own
    `CefRequestContext` with no cache path, so its cookies, cache and storage are
    in memory only and are discarded with the window. Nothing it visits is
    written to history, it never owns the session snapshot, and its closed-tab
    list dies with it. The chrome is drawn in a deeper violet so the two kinds of
    window cannot be confused.
  - Remaining: on-screen confirmation, an explanation page on first use, and a
    decision on whether downloads started there should appear in the shared
    download history (today they do).
- [~] **T2.2 Multiple profiles**
  - Done: `native/src/profile.cpp` selects a profile from `--profile=<id>`, and
    every store built on `preferences::ApplicationDataDirectory()` follows it at
    once: preferences, bookmarks and history, download history, saved session and
    Chromium's cache. The default profile deliberately keeps the original
    directory, so an existing installation needs no migration. Update installers
    moved to `InstallationDataDirectory()` so they are not downloaded once per
    profile. The menu has a Profile submenu with a picker and "New profile...",
    creating one is behind the parent PIN, and each profile runs in its own
    process because the stores and the cache lock are process-wide. Ids are
    restricted to letters, digits, `-` and `_` since they build a path; that
    validation and the command-line parsing are covered by tests.
  - Remaining: profile avatars/display names separate from the id, deleting a
    profile from the UI, and a first-run picker.
- [ ] **T2.3 Password manager + autofill** (OS-encrypted storage, never
      plaintext). Not started; this is the largest Tier 2 item and needs a
      threat model written down before any code.
- [~] **T2.4 Bookmarks bar, folders, and Chrome import**
  - Done: bookmarks bar under the toolbar (painted, DPI-scaled, `Ctrl+Shift+B`,
    remembered in preferences), `Ctrl+D` to save a page, and an importer for
    Chrome, Edge and Brave (`native/src/bookmark_import.cpp`). Imported
    bookmarks pass through `EvaluateNavigation`, so an import cannot introduce an
    address the browser refuses to open, and the refused count is reported.
  - Remaining: folders. `library_store` has a flat list, so folders need a
    format change, drag-and-drop, and a bar overflow menu.
- [~] **T2.5 History with search and date grouping**
  - Done: live search box, and history grouped by Today / Yesterday / long date
    using the new per-entry timestamp. Group rows are not selectable.
  - Remaining: this is still a dialog, not a browsable page, and there is no
    "remove all from this day".
- [~] **T2.6 Tab UX**: drag-reorder, pin, mute, duplicate, middle-click close,
      right-click tab menu, `Ctrl+1..8`/`Ctrl+9`, `Ctrl+M`, `Ctrl+Shift+T`,
      overflow scrolling. Pinned tabs are the leading run of the strip, get a
      narrow slot and no close button. Not yet checked on screen; pinned state is
      not written to the session file.
- [~] **T2.7 Printing**: `Ctrl+P`, a menu entry and a page context-menu entry all
      open Chromium's print preview, which is also where Save as PDF lives.
- [~] **T2.8 Built-in PDF viewer**: the request policy already allows exactly the
      bundled PDF viewer extension origin (`IsTrustedPdfViewerUrl`), so inline
      viewing should work. Needs a real PDF opened in the app to confirm it
      renders instead of downloading.
- [~] **T2.9 Spellcheck**: local Hunspell spellcheck enabled with en-US, and
      suggestions plus "Add to dictionary" in the context menu. Google's
      server-backed enhanced spellcheck is explicitly turned off, because it
      uploads typed text. Chromium has no Hunspell dictionary for Hindi.
- [~] **T2.10 Per-site settings**
  - Done: `native/src/site_settings.cpp` stores one rule per origin (scheme and
    host) in `sites.v1`, holding camera, microphone, location and notification
    decisions, ad/tracker filtering, cookies, and a remembered zoom level.
    `Alt+S` opens the dialog for the site in the active tab, behind the parent
    PIN because granting a camera is a safety decision. Zoom follows Chromium's
    own ladder of steps and is reapplied on every navigation to that site.
    Cookie blocking is enforced through CEF's cookie access filter, so it covers
    both sending and storing. Clear browsing data can reset all of it.
  - Permissions still default to refused and no prompt is raised from the
    permission callback: a page asks at a moment of its choosing, and answering
    with a modal dialog there would interrupt the user and re-enter CEF. Access
    is granted the other way round, from the site settings dialog.
  - The per-site Shields switch drops ad and tracker rules only. Safety rules
    (scheme, private network, unsafe host, adult search term, NSFW social
    surface) stay in force for every site and are not adjustable, which is
    covered by a test.
  - Remaining: a list of all sites with stored rules, per-site "clear this
    site's data", and reaching the dialog from the Shields panel.
- [x] **T2.11 Clear browsing data dialog** (history, downloads, cookies, cache,
      saved session, bookmarks)
- [~] **T2.12 Media**: autoplay now requires a user gesture, and picture-in-picture,
      the media session service and hardware media keys are enabled through
      `OnBeforeCommandLineProcessing`. Needs a real page to confirm.
- [~] **T2.13 Downloads polish**
  - Done: the toolbar button now opens a popup panel
    (`native/src/downloads_panel.cpp`) showing the five most recent downloads
    with a progress bar and inline actions: pause, resume, cancel for live
    downloads, and open, show in folder, copy for finished ones. It dismisses on
    focus loss, on Esc and on a second press of the button, and the full window
    stays on `Alt+J`. The panel holds no CEF headers; pause, resume and cancel
    are handed in as callbacks.
  - "Copy" puts the file on the clipboard as a shell file drop, which Explorer
    pastes as the file. That stands in for dragging the file out: a drag loop
    needs an OLE-initialised thread, which the browser UI thread does not
    guarantee, and a drag that silently fails would be worse than none.
  - Remaining: real drag-out, and cross-restart resume, which needs an own
    resumable HTTP path because CEF's callbacks only cover the live process.
- [x] **T2.14 Full-screen mode (F11)** — chrome hidden, not destroyed; Esc exits
- [~] **T2.15 Accessibility**: toolbar buttons now carry real accessible names
      instead of the drawn glyph ("Back" rather than "←"), and the renderer
      accessibility tree is forced on so page content reaches assistive
      technology. Remaining: verification with an actual screen reader, high
      contrast handling, and focus order through the tab strip and bookmarks bar.
- [ ] **T2.16 Localization**: complete Hindi + English UI strings. Not started;
      every string is currently inline in the source, so this needs a string
      table first.

## Tier 3 — Brave-level differentiators

- [ ] **T3.1 Extension support** (largest single effort)
- [ ] **T3.2 Sync** across devices, end-to-end encrypted
- [~] **T3.3 Shields panel** — the toolbar panel exists (`native/src/shields_panel.cpp`,
      dismissal and DPI issues fixed) and per-site ad/tracker filtering now
      persists per origin through T2.10. Remaining: reaching that switch from the
      panel itself, per-site blocked counts, and a custom filters UI.
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

## Keyboard shortcuts are deliberately not Chrome's

Every browser action is on the Alt layer, so nothing collides with the Ctrl keys
web pages and Windows already own. The reference is `Alt+/` in the product and
`docs/KEYBOARD_SHORTCUTS.md` in the repository. The cost is recorded there too:
anyone arriving from Chrome or Brave will press `Ctrl+T` and get nothing.

## Recommended order

1. T1.1 verify the sandbox at runtime (code is in, proof is missing)
2. T1.7 CI workflow (cheap, and it protects everything after it)
3. T1.4 crash recovery, then T1.5 default browser
4. T1.6 multi-window + T2.6 tab UX
5. T2.1 private window, T2.8 PDF viewer, T2.7 printing
6. T1.2 code signing when a certificate is purchased, which also unblocks the
   remaining T1.3 auto-update work

## Coordination note

Large parts of the UI live in `native/src/browser_window.cpp`, and more than one
session works in this repository. Check `git status` before starting: while that
file has uncommitted changes, prefer new files or low-conflict areas (installer,
scripts, tests) to avoid clobbering work in progress.
