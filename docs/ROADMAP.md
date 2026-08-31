# MTK Browser Roadmap to Chrome/Brave Level

Status legend: `[ ]` not started, `[~]` in progress, `[x]` done and verified.

Rule: security fundamentals are never Pro-gated. Nothing is marked `[x]` until it
has been actually built and verified, and any limitation is written down.

## Tier 1 — Release blockers

- [x] **T1.1 Chromium sandbox enabled**
  - Done: `MTK_ENABLE_CEF_SANDBOX` is `ON` by default and forces `USE_SANDBOX`,
    the app builds as `MTKBrowser.dll` loaded by CEF's renamed `bootstrap.exe`,
    `main.cpp` derives `no_sandbox` from the real `sandbox_info` handle, and
    staging refuses a payload missing the DLL or with mismatched signing state.
    The 0.3.0 payload ships both files (EXE 3.1 MB, DLL 1.0 MB).
  - Verified at runtime on 2 Aug 2026 by launching the Release build in a
    throwaway profile (`--profile=sbxcheck`) and reading each child process's
    token integrity level:

    | Process | Integrity |
    |---|---|
    | browser | Medium |
    | renderer (x2) | Untrusted |
    | GPU | Low |
    | utility, `storage.mojom.StorageService` | Untrusted |
    | utility, `network.mojom.NetworkService` | Medium |

    No process carried `--no-sandbox`. The network service running at Medium is
    Chromium's own design on Windows, not a gap in this packaging.
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
- [x] **T1.4 Crash handling and recovery**
  - `native/src/crash_guard.cpp` writes a marker in the profile directory at
    startup and deletes it on a normal exit, so a marker found at startup means
    the previous run never reached its shutdown. Unclean exits are appended to a
    bounded local `crashes.v1`; nothing is sent anywhere.
  - The session snapshot is now kept while running in both startup modes, because
    that is the only way tabs can be offered back after a crash. A clean exit in
    "open home page" mode deletes it, so choosing the home page still leaves
    nothing behind. Private windows never contribute to it.
  - Verified end to end on 2 Aug 2026 in a throwaway profile: the marker and
    snapshot existed while running, both survived a forced kill, the next start
    wrote `2026-08-02 09:31:53 unclean exit (run started 2026-08-02 09:27:26)`
    and put the "Restore tabs" dialog on screen, and a later clean close removed
    the marker again.
- [~] **T1.5 Default browser + file/protocol associations**
  - Done: `native/src/default_browser.cpp` writes the `MTKBrowserHTML` ProgID,
    `Capabilities` with `http`/`https` URL associations and the HTML file
    associations, and the `RegisteredApplications` entry, all under HKCU so no
    administrator rights are needed. Registration runs on every start, which also
    repairs a stale entry, and the installer removes those keys on uninstall so
    Windows stops offering a browser that is gone. The menu shows either "Make
    MTK Browser the default..." or that it already is, read from the live
    `https` association rather than from our own keys.
  - Windows 10 and 11 do not let a program make itself the default, so the action
    registers and then opens the Default apps page and says so plainly instead of
    pretending to have done it.
  - Remaining: on-screen confirmation that MTK Browser appears in that list and
    that a link from another program opens in it.
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
  - Done: 103-case logic suite under `tests/`, run by `scripts/run_tests.ps1`,
    covering navigation/request policy, download rules and file-name hardening,
    session-restore persistence, the toolbar icon strip loader, and the Chromium
    bookmarks importer (which parses a file written by another program, so it is
    tested against malformed, deeply nested and policy-refused input), profile id
    validation, and per-site settings including a test that "Shields off" cannot
    weaken a safety rule, UI string encoding, and the password store including a
    real DPAPI round trip and the https/exact-origin rules, and crash-history
    trimming, the user's own filter rules, and HTTPS-only mode. Verified passing
    locally (103 passed, 0 failed).
  - Done: `.github/workflows/tests.yml` runs the suite on every push to `main`,
    on pull requests and on demand. It uses a plain `windows-2022` runner because
    none of these tests need CEF, and it also checks the committed animation and
    icon-strip assets against the same limits the release staging script
    enforces, so a bad asset fails on push rather than at packaging time.
  - Fixed while adding the bookmark-folder cases: the suite had been reading and
    writing the **real** profile. Several cases redirected `LOCALAPPDATA`, but the
    profile root comes from a Windows known folder rather than that variable, so
    the redirection did nothing and cases that write, clear or delete were acting
    on real session, password, site-settings, crash-history and user-filter
    files. `profile::SetRootForTesting` now exists for this, `RunAll` points the
    whole suite at a throwaway directory before the first case and removes it
    afterwards, and it refuses to run at all if that directory cannot be created.
    `WritingToSuiteProfile()` lets a case assert the redirection is in effect
    before it touches a file. Verified by hashing every file in the real profile
    directory around a full run: nothing changed.
  - Remaining: a Release build in CI would have to download and cache the ~1 GB
    CEF binary distribution, so that is deliberately left out; the local build is
    what gates a commit today. Coverage for CEF-dependent paths still needs a
    live browser.

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
- [~] **T2.3 Password manager + autofill**
  - The threat model was written first and is in `docs/PASSWORD_MANAGER.md`,
    including what this does **not** protect against. Any change to the rules
    changes that document in the same commit.
  - Done: `native/src/password_store.cpp` keeps one credential per origin and
    user name in `passwords.v1`, with the password held only as a user-scoped
    DPAPI blob whose extra entropy includes the profile id, so another Windows
    account, another machine or another profile cannot decrypt it.
    `CRYPTPROTECT_UI_FORBIDDEN` keeps decryption from ever raising a prompt.
  - Capture and fill run over CEF's message router
    (`native/src/password_bridge.cpp`). The browser side derives the origin from
    the frame's own URL rather than from anything the page sent, so a page can
    only ask about itself, and every field is bounded. Saving always asks first.
    Filling happens only over https, only on an exact origin match, and not at
    all when several accounts are saved for one site, because that would be a
    guess.
  - `Alt+K` opens the saved list behind the parent PIN, with show, copy one,
    remove and remove all. Filling is not PIN-gated, since gating the everyday
    case would make the feature pointless. Clear browsing data can delete the
    lot, with its own confirmation.
  - Remaining: an account picker for sites with several saved sign-ins, an
    "update the saved password" flow when a changed password is submitted, and
    on-screen verification of capture and fill on real sign-in forms.
- [~] **T2.4 Bookmarks bar, folders, and Chrome import**
  - Done: bookmarks bar under the toolbar (painted, DPI-scaled, `Ctrl+Shift+B`,
    remembered in preferences), `Ctrl+D` to save a page, and an importer for
    Chrome, Edge and Brave (`native/src/bookmark_import.cpp`). Imported
    bookmarks pass through `EvaluateNavigation`, so an import cannot introduce an
    address the browser refuses to open, and the refused count is reported.
  - Done: folders, as one shallow level rather than a tree. `library.v1` gained
    `F` folder records and an optional folder field on `B` records, both trailing
    and optional, so a file written before folders loads unchanged and everything
    in it stays at the top level. Names are trimmed, capped at 64 characters,
    refuse control characters, and are unique without regard to case; there is a
    cap of 30 folders.
  - Folders lead the bookmarks bar and open a popup of their contents. The bar's
    right-click menu creates, renames and deletes folders, moves a bookmark
    between folders or back to the top level, and removes a bookmark. Deleting a
    folder that holds bookmarks asks whether to keep them or delete them with it.
    Renaming updates the folder and its bookmarks together, so the two cannot
    disagree, and a bookmark that names a folder with no record gets that folder
    registered rather than being moved silently.
  - 11 cases in `tests/bookmark_folder_tests.cpp` cover name validation, the
    uniqueness and count caps, moving between folders, saving into a folder that
    does not exist, renaming, both deletion answers, a save/reload round trip
    including an empty folder and a non-Latin name, loading a file from before
    folders, an orphaned folder reference, malformed records, and that clearing
    bookmarks clears folders too.
  - Remaining: drag-and-drop, a bar overflow menu for when the buttons no longer
    fit, nesting beyond one level, choosing the folder while saving with `Ctrl+D`,
    and on-screen confirmation of the bar and its menus.
- [~] **T2.5 History as a browsable page, with search and date grouping**
  - Done: history is now a browser-owned document at `mtk://history/`
    (`native/src/history_page.*`), served by the same `mtk` scheme as the home
    page. `Alt+H`, the overflow menu and the home page all open it, so the list
    dialog is no longer the history surface. Entries are grouped by local
    calendar day as Today / Yesterday / long date, each group shows its count and
    a "Remove this day" action, each row can be removed individually, and there
    is "Remove everything". Every delete is confirmed in a browser dialog, not on
    the page.
  - The page carries no script of its own, so search is an ordinary GET form back
    to the document and the greeting-style dynamic parts are rendered in C++. Its
    CSP allows `form-action 'self'` only; the home page keeps its narrower Google
    form action. Addresses the safety policy would now refuse are listed as a
    count instead of being offered as links, and a private window renders the
    empty state rather than the on-disk profile's history.
  - Row deletion carries the row index **and** the stored timestamp, and the
    browser only removes the row when both still agree, so a list that changed
    between render and click cannot delete a different page.
  - 10 cases in `tests/history_page_tests.cpp` cover URL and query acceptance
    (lookalike hosts, extra parameters, fragments, control characters, malformed
    escapes, length), the action allowlist, local-day grouping across midnight,
    search, HTML escaping of stored titles and addresses, the private-window empty
    state, and the absence of script or remote references. Suite is now 113
    cases, 0 failed.
  - The library dialog is now bookmarks-only: its history branch, the day-group
    helpers it needed and the flag that selected between the two are gone, so
    there is one history surface rather than two that could disagree.
  - Remaining: on-screen confirmation in the running browser, and a keyboard
    focus styling pass on the page.
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
- [~] **T2.16 Localization (Hindi + English)**
  - Done: `native/src/strings.cpp` holds one table where each entry carries both
    translations on the same line, so a string cannot be added to one language
    and forgotten in the other; the enum and both arrays are generated from it
    and checked with `static_assert`. The UI language follows the language choice
    already in Settings rather than adding a second question about the same
    thing. Translated so far: the browser menu, the tab context menu, the
    downloads popup, and the toolbar and find-bar accessible names.
  - Found while doing this: the sources are UTF-8 without a byte order mark and
    the build had no `/utf-8`, so MSVC was reading them in code page 1252 and
    every non-ASCII literal was already mojibake (an em dash became `â€”`). The
    flag is now set for the browser and for the test build, and a test asserts
    the Hindi strings really are Devanagari, since this class of bug compiles
    cleanly either way.
  - Done: the resource dialogs are translated by runtime text assignment rather
    than by keeping one dialog template per language, which would duplicate every
    layout. `native/src/dialog_text.*` replaces a dialog's caption and the text of
    a listed set of controls from the same table, so a dialog label cannot drift
    away from the menu string next to it. The templates keep their English text,
    which is what a resource editor shows and what appears if the pass is ever
    skipped. Static labels that were left at `-1` in the script now carry real
    ids, because a label without an id cannot be addressed at run time.
  - Every resource dialog now goes through this route: Bookmarks, Downloads,
    Clear browsing data, the parent-PIN prompt and setup, the shared text prompt,
    Settings, Site settings, Saved passwords and Your filter rules. The combo-box
    items built in code are translated too: startup mode, download mode, the
    per-site permission choices and the encrypted-DNS list. Language names in the
    language picker stay in their own language, which is how someone finds the one
    they can read, and DNS operator names stay as they are while the description
    beside each is translated.
  - `DohLabel` in the preference layer is no longer what the Settings list shows;
    the window maps a provider to a string id instead, so display text lives with
    the other display text rather than in the store.
  - About 85 new strings. The suite already checked that every entry has both
    languages and that the English side stays ASCII, which caught a real mistake
    here: em dashes in English dialog captions. The Devanagari spot-check list now
    covers the dialog strings, so mojibake in them fails a run rather than
    shipping.
  - Remaining: the block page, the shortcut page, the Shields panel and the
    message boxes. Hindi layout also needs an on-screen pass, because Devanagari
    is taller than Latin and the templates use fixed label heights, so a longer
    translation may clip.

## Tier 3 — Brave-level differentiators

- [ ] **T3.1 Extension support** (largest single effort)
- [ ] **T3.2 Sync** across devices, end-to-end encrypted
- [~] **T3.3 Per-site Shields panel + custom filters**
  - Done: the panel now names the current site, shows what was blocked while that
    page was showing, says when that site's own ad/tracker switch is off, and
    counts the user's own rules. Per-site blocked counts are kept in a small
    locked map in `browser_client.cpp` (a display counter, not part of any
    decision).
  - Done: `native/src/user_filters.cpp` holds rules a person writes themselves,
    edited with `Alt+Y` behind the parent PIN. Deliberately smaller syntax than
    Adblock Plus, because a typed rule has to be predictable: a line blocks any
    address containing it, `@@` allows and wins, `#` is a note. They are kept
    apart from the bundled engine, which is immutable after startup so it can be
    read from CEF's IO thread without locking; these can change while running, so
    they sit behind a shared lock instead of being merged into it.
  - The panel stays presentation-only. The per-site switch lives in the site
    settings dialog (`Alt+S`) and the panel says where it is, rather than adding
    a script bridge to a surface that was deliberately built without one.
  - A custom **allow** rule cannot open anything the safety policy refuses: the
    policy is consulted first and independently, which a test pins down.
  - Remaining: per-site rule counts in the panel, and a rule syntax check while
    typing rather than only on save.
- [ ] **T3.4 Fingerprinting protection**
- [~] **T3.5 HTTPS-only mode + DNS-over-HTTPS provider choice**
  - Done: HTTPS-only refuses plain-HTTP **subresources** and is on by default,
    with a Settings toggle that asks for confirmation before it is turned off.
    Main-frame addresses are left alone on purpose, because the navigation path
    already upgrades them to https; refusing them here would turn that upgrade
    into a block page. A test pins that distinction down.
  - Done: encrypted DNS with a provider list in Settings. The default is
    Cloudflare for Families, which refuses malware and adult domains at the DNS
    layer as well, and "Use the Windows DNS setting" switches it off. The mode is
    `automatic`, not `secure`, so an unreachable resolver degrades instead of
    looking like a broken internet connection.
  - Remaining: on-screen confirmation that lookups really are encrypted (e.g.
    against a resolver test page), and a per-site HTTP exception for the rare page
    that has no https at all.
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

1. **On-screen verification of everything marked `[~]`.** Tier 1 and Tier 2 are
   built; most of it has never been looked at on a screen. That is now the
   cheapest way to find real problems, and it needs a person.
2. T1.2 code signing, once a certificate is purchased. It also unblocks the
   remaining T1.3 auto-update work (rollback and a real published update).
3. The "remaining" notes inside the Tier 2 items: bookmark folders, an account
   picker for sites with several saved sign-ins, Hindi for the resource dialogs,
   real drag-out for downloads.
4. Tier 3, starting with the Shields panel work that T2.10 already made possible.

## Coordination note

Large parts of the UI live in `native/src/browser_window.cpp`, and more than one
session works in this repository. Check `git status` before starting: while that
file has uncommitted changes, prefer new files or low-conflict areas (installer,
scripts, tests) to avoid clobbering work in progress.
