# MTK Browser professional implementation backlog

Updated: 2026-09-07. Scope: the user's complete 47-item feature list.

`Queued` means not yet delivered to the stated acceptance criteria, not necessarily
that no supporting code exists. `Partial` means useful code already exists but
implementation or verification remains. `Verified` requires build, relevant tests,
and a real UI check for user-facing behavior. No feature is complete on compilation
alone. External dependencies do not stop independent local work.

| # | Feature / acceptance criteria | Status / next dependency |
|---|---|---|
| 1 | Chromium/CEF update procedure; critical-patch response and compatibility checks | Partial; repeatable security-update runbook exists, next upstream update must execute it |
| 2 | Signed EXE, DLL and installer with matching verified publisher | External: signing identity/certificate |
| 3 | Signed auto-update, key rotation, recovery, stable/beta channels | Partial; real distribution/signing needed |
| 4 | Sandbox, isolation, TLS and internal-page runtime verification | Partial; fresh sandboxed Release smoke test created six CEF processes with 0 `--no-sandbox` switches; TLS/internal-page checks remain |
| 5 | Crash recovery, all-window restore, atomic stores and recovery tests | Partial; V3 session snapshot records all normal windows, tabs, pins and bounds; on-screen recovery test remains |
| 6 | Website compatibility matrix: login, mail, calls, uploads, media, payments | Partial; release compatibility matrix and promotion gate documented; authorized test-account execution remains |
| 7 | Automated logic/UI/build/install/upgrade/uninstall release checks | Partial; logic CI exists |
| 8 | Address suggestions, selectable search engine, search shortcuts/privacy | Partial; local suggestions and Google/Bing/DuckDuckGo selection with enforced safe-search, plus search engine omnibox shortcuts (@g, :g, @b, :b, @d, :d, @ddg, @w, :w) implemented and verified |
| 9 | Optional familiar shortcuts and customization | Partial; Settings now offers an opt-in familiar Ctrl shortcut layer while retaining the Alt default; per-action remapping remains |
| 10 | Multi-window geometry, tab transfer, all-window session restore | Partial; all-window snapshot/restore and normal-tab drag-out plumbing exist; runtime test remains |
| 11 | Tab search, groups, vertical tabs, previews, closed-tab list | Partial; tab search, recently-closed list, persistent group labels, and group management exist. Vertical tabs collapsible left-rail UI (expanded 240px and collapsed 48px icon-only rail with toggle chevron, centered icons, tooltips and preference persistence) is implemented and verified. Tab previews and live on-screen check remain |
| 12 | Bookmark edit/save folder selection, nesting, drag/drop, import/export | Verified local; bookmark folders, Netscape HTML export with recursive nested folder DL/p hierarchy, folder import, atomic persistence, drag-and-drop bar reordering, and Move earlier/later context actions delivered and verified |
| 13 | Password update, generator, account picker, optional Windows Hello | Partial; update, account picker, local cryptographic generator and opt-in Windows Hello unlock for the saved-password list are present; on-screen secure-prompt validation remains |
| 14 | Passkey/security-key compatibility and consent-based form autofill | Partial; internal capability page checks Chromium WebAuthn/platform authenticators. Consent-based address and contact autofill store with user/profile-scoped DPAPI encryption, explicit user consent gating, and atomic persistence delivered and verified. Real-site security-key remains |
| 15 | Downloads retry/resume across restart, search, warnings | Partial; local search, policy-checked retry, restart resumption metadata / Range header logic delivered, plus dedicated internal page mtk://downloads/ with category filtering (Documents, Images, Archives, Media, Other), clean file naming, and inline actions |
| 16 | Profiles: names/avatars/delete/first run; private-mode explanation | Partial; user-friendly display names, first private-window explanation and Recycle Bin deletion exist; avatars and first-run picker remain |
| 17 | Permission indicators, temporary grants and site management | Partial; per-site controls, memory-only temporary grants and live CAM/MIC tab indicator implemented; on-screen capture validation remains |
| 18 | Consistent typography, spacing, icons, menus and dialogs | Partial; visual audit needed |
| 19 | Light/dark/system themes and multi-DPI layouts | Partial; native chrome offers light, dark and Windows-following themes; on-screen DPI/layout validation remains |
| 20 | Searchable settings and safe reset/troubleshooting | Partial; dedicated internal page mtk://settings/ with live search filter, grouped preferences (Appearance, Search, Shields, System), diagnostics display, and safe defaults reset |
| 21 | Customizable, fast new-tab page | Verified local; rich 3D interactive home dashboard with animated WebP button suite, live real-time clock & greeting, weather widget via wttr.in, inspirational daily quotes, interactive quick notes scratchpad with local storage, and download cards with file-type badges |
| 22 | Memory/battery saver, sleeping/discarded tabs and exceptions | Partial; inactive unpinned tabs receive CEF hidden/scheduling signal, true tab discard and sleep/wake context menu actions implemented with hollow ring indicators and pinned tab exceptions. Battery profiling remains |
| 23 | Per-tab/process resource task manager | Verified local; native Win32 task manager dialog lists Browser, GPU, Tab, and Utility tasks with CPU%, memory formatting, column sorting, and English/Hindi localization |
| 24 | Repeatable startup/page/RAM/CPU/battery benchmarks | Partial; a cold-start harness records window startup, title-based page readiness, combined CEF working set and CPU for isolated profiles. Battery/scroller and Chrome/Brave comparison runs remain |
| 25 | Keyboard/screen-reader/high-contrast accessibility; full Hindi layouts | Partial; system High Contrast palette is implemented, screen-reader and Hindi layout checks remain |
| 26 | Signed updateable tracker/malicious/content filter feeds | Queued; feed source/operator needed |
| 27 | Privacy-conscious phishing/malware/download reputation | Queued; provider/access terms needed |
| 28 | Complete per-site deletion: cookies, storage, workers, caches | Partial; whole-browser cleanup deletes cookies, requests Chromium clearing of local storage/IndexedDB/Cache Storage/service workers for known origins, and uses CEF's HTTP-cache API. Unknown legacy origins and runtime verification remain |
| 29 | Fingerprint protection validation and per-site exceptions | Partial; canvas/audio spoofing and navigator hardwareConcurrency (4) / deviceMemory (8) farbling implemented and verified; per-site exceptions remain |
| 30 | Clear protection panel with reason and controls | Partial |
| 31 | Child profiles, age policies, schedules, parent overrides | Partial; parent PIN exists |
| 32 | Block reasons, false-positive reports and parent approval | Partial |
| 33 | Data-on-exit and opt-in diagnostics/privacy controls | Partial; optional clean-exit removal of browsing history, finished-download rows and saved tabs (ExecuteClearActivityOnExit); diagnostics and broader storage clearing remain |
| 34 | Extension install/permissions/update/remove and family restrictions | External foundation dependency; bundled CEF 150 minimal SDK has no extension-management API. See `docs/CEF_EXTENSION_FEASIBILITY.md`; do not weaken the PDF-only extension-origin allowlist |
| 35 | Encrypted sync with conflict resolution, recovery and device revocation | Queued; service design/hosting needed |
| 36 | Split view and workspaces | Verified local; split view allows two tabs to share side-by-side native window; workspaces management UI (create, switch, tab assignment, arrange tabs, clearing) with durable V3 session persistence implemented and verified |
| 37 | Reader mode, reading list and saved articles | Partial; local reader mode simplifies public article pages without sending content anywhere. Durable reading list saves, opens, and removes article links, and features offline article snapshot archiving with atomic persistence and cleanup |
| 38 | Translation with language and privacy controls | Partial; page menu can explicitly open Hindi or English translation in a separate Google Translate tab after a per-action disclosure. Offline/private provider choice and runtime validation remain |
| 39 | PDF/printing: render/search/forms/rotation/Save as PDF | Partial; native runtime check needed |
| 40 | Installable web apps with separate windows | Partial; policy-approved HTTPS web apps run in dedicated windows with slim standalone chrome (hidden tab and bookmark bars, Back and Reload controls, origin badge pill, clean window titles), isolated profile storage, Start-menu shortcuts, and list/remove app management |
| 41 | Media, video calls, streaming, PiP and media keys | Partial; codecs/DRM feasibility and service testing |
| 42 | DevTools, inspect element, console and network tools | Partial; CEF DevTools open with F12 or Inspect element; on-screen console/network validation remains |
| 43 | Website/download/release notes/support/bug reporting | Partial; production destination needed |
| 44 | Accurate preview notices, privacy policy, dependency notices/support policy | Partial |
| 45 | Windows installation, default-browser and file associations | Partial; real installation checks needed |
| 46 | Android/macOS/Linux after Windows quality gate | Queued; platform toolchains/devices required |
| 47 | Optional AI with consent and data controls | Deferred until core quality; provider/scope needed |

## Execution evidence

- Prior turn: 145 logic cases passed before its final parser change; native-adblock
  Release compiled. That cached build has sandbox OFF and is not release evidence.
- Current batch: corrected placement coordinate handling and fullscreen/minimized
  state; password update; bookmark save/edit; live custom-filter validation;
  selectable safe-search engine; private-window explanation; and a safe HTML
  bookmark export, Downloads history search and policy-checked retry. Sandboxed
  Release build succeeded and 167 logic cases passed.
  UI runtime checks remain a separate gate.
- Runtime security check, 7 Sep 2026: fresh sandboxed Release launch created a
  browser, two renderers, GPU and utility processes; 0 of 6 inspected command
  lines contained `--no-sandbox`; all throwaway processes were stopped.
- Compatibility/runtime smoke, 7 Sep 2026: a fresh temporary profile launched
  the current Release executable to a real `MTK Browser Home — MTK Browser`
  window. Seven related CEF processes were observed, none used `--no-sandbox`,
  and a normal window-close signal ended the main process. The isolated profile
  was removed afterwards. This proves startup/internal-home handling only; it
  does not replace account, media, payment or accessibility matrix checks.
- On-screen public-web check, 7 Sep 2026: the Release browser rendered Google
  over HTTPS, including its live document content and browser controls. This
  is evidence for ordinary public navigation only; no login, upload, call or
  payment test was performed.
- Installed-release check, 7 Sep 2026: the freshly packaged 0.3.0 Preview
  installer replaced the existing per-user install at
  `%LOCALAPPDATA%\\Programs\\MTK Browser`, completed successfully, and launched
  the installed executable. Its `mtk://home/` document and an independent
  `https://example.com/` navigation both rendered with the HTTPS lock and live
  page content. The current 167-case logic suite also passed with 0 failures.
- Reading-list logic check, 7 Sep 2026: the local library store keeps reading
  list records separate from bookmarks, writes them atomically, rolls memory
  back if a write fails, and survives reload. The 170-case logic suite passed
  with 0 failures, and the native-professional Release executable compiled.
- Performance-harness smoke, 7 Sep 2026: one isolated `mtk://home/` Release
  run recorded visible-window startup at 861 ms, title-ready at 1,014 ms and
  combined CEF working set at 378.2 MiB. This is a one-sample baseline only,
  not a Chrome/Brave or battery comparison.
- Latest installed-preview smoke, 7 Sep 2026: the rebuilt unsigned 0.3.0
  Preview installer (SHA-256
  `5029F5DE7339D5944DE7F662157C3730F952E4B3BD26D6783AD3268223DD16F8`)
  replaced the per-user install. The installed executable showed the
  `MTK Browser Home — MTK Browser` window and started seven related CEF
  processes; a normal close signal was accepted. This is startup evidence,
  not a full feature or compatibility sign-off.
- Translation implementation check, 7 Sep 2026: URL construction rejects
  non-web pages and unsupported targets while percent-encoding query, fragment
  and UTF-8 bytes. The updated logic suite passed 171 cases with 0 failures
  and the native-professional Release target compiled. A rebuilt unsigned
  Preview installer (SHA-256
  `967F564A5B4D55A1A8E6AC3FA564E996770037709B3BB6BC67C1224977CC659F`)
  was then installed successfully; on-screen translation-flow validation
  remains.
- Web-app launch logic check, 16 Sep 2026: a policy-approved HTTPS site gets a
  stable opaque per-origin profile id; the URL is quoted as one Windows
  command-line argument before the separate process is launched. The 173-case
  logic suite passed with 0 failures and the native-professional Release target
  compiled. This is not
  PWA installation: there is no shortcut, standalone chrome or remove UI yet.
- Installed Preview update, 16 Sep 2026: the unsigned
  `MTK-Browser-Setup-0.3.0-Preview.exe` package (SHA-256
  `8261E9C19089D3D5809C23C15F02BFD2483EC015843E199F7A699AA3048AF4B8`)
  completed a silent per-user replacement. The installed
  `MTKBrowser.exe` is present at `%LOCALAPPDATA%\\Programs\\MTK Browser`
  (SHA-256 `17A8EFFA5AAF4C082F570568CDA7A9BFDD07ECD68F55EF356A9B3DDE2DE59ED2`).
  This verifies package/install replacement only; the new confirmation and
  separate-window interaction still need an on-screen check.
- Start-menu web-app shortcut check, 16 Sep 2026: each confirmed HTTPS app
  window now refreshes an opaque-id `.lnk` shortcut under the user's Start-menu
  Programs folder, carrying the same isolated profile argument. The 174-case
  logic suite passed with 0 failures and the native-professional Release target
  compiled. On-screen shortcut creation and cleanup still need validation.
- Latest Preview install, 16 Sep 2026: after the previous browser instance was
  closed, `MTK-Browser-Setup-0.3.0-Preview.exe` completed a silent per-user
  replacement and updated `%LOCALAPPDATA%\\Programs\\MTK Browser\\MTKBrowser.exe`.
  The package SHA-256 is
  `4FB751934829C93F36E7251B107C4930E12FD0556B72C598E9E472D926065D4D`.
  Installation and executable timestamp were verified; the executable was
  briefly locked by the launched browser during post-install hashing.
- Tab-group, PDF and diagnostics implementation check, 16 Sep 2026: grouped
  tabs now persist through session save/restore, can be arranged or ungrouped
  from the tab-groups menu, and appear in tab search labels. Offline PDF export
  uses temporary output plus atomic replacement, while Browser diagnostics shows
  local build/runtime state without URLs or credentials. The full 176-case logic
  suite passed with 0 failures and the native-professional Release target
  compiled. On-screen validation of these new menu actions remains pending.
- Password-form regression gate, 16 Sep 2026: the release test runner now also
  executes the six offline Node.js DOM-fixture tests for the shipped password
  bridge (save, change-password and autofill safety cases); all 6 passed.
- Tab-group visual polish, 16 Sep 2026: grouped tabs now show a stable,
  deterministic accent stripe derived from the group name; high-contrast mode
  keeps using the Windows system palette. Professional Release rebuilt and the
  176-case C++ plus 6-case password-form suites passed.
- Tab-hover detail, 16 Sep 2026: hovering a tab now shows a native tooltip with
  its title, group (when present) and full URL, while close-button hover hides
  the tooltip to avoid accidental ambiguity. The feature creates no renderer
  or network work; the Professional Release build passed.
- Latest tooltip Preview install, 16 Sep 2026: the regenerated 120 MB unsigned
  installer completed a silent per-user replacement (exit code 0). Package
  SHA-256 is `A97FCD25937FBB1F32D8B2866884AFEE66A8DA722FED3AB4F76473336D10174B`;
  installed executable timestamp updated to 20:00 local time.
- Familiar-shortcut Preview install, 16 Sep 2026: the installer regenerated
  with `Ctrl+Shift+A` Tab Search support completed with exit code 0. Package
  SHA-256 is `1365EDA38755FB30A5203B07B64796C4B40DBD58E90280910076161BF12CA5CA`;
  the installed executable timestamp updated to 20:12 local time.
- Multi-window Tab Search Preview install, 16 Sep 2026: the regenerated 120 MB
  installer completed with exit code 0. Package SHA-256 is
  `079844E0B1349F9485ADA4C907332E34CF42324D1A6577754C144974E1D48FCF`;
  installed executable timestamp updated to 20:20 local time.
- Post-install smoke rerun, 16 Sep 2026: the latest installed build reached a
  visible window and page-ready title successfully (1/1). Startup was 878 ms,
  page-ready 904 ms, working set 291.3 MiB and CPU 1.14 s; the normal close
  path completed.
- Multi-window regression rerun, 16 Sep 2026: after installing the cross-window
  Tab Search build, the complete local gate remained green at 176 C++ cases and
  6 password-form cases, with 0 failures.
- Tab Search lifetime hardening, 16 Sep 2026: selection now stores a window
  handle and re-resolves it through the live window registry after the modal
  closes, so a window closed during search cannot become a dangling pointer.
  Professional Release compilation passed; installer regeneration is pending.
- Hardened Tab Search Preview install, 16 Sep 2026: regenerated 120 MB unsigned
  installer completed with exit code 0. Package SHA-256 is
  `114846308BAA6FC4234263F48C043BBDCF7854EBE7983AE495FFBE5A8D614082`;
  installed executable timestamp updated to 20:30 local time.
- Final installed smoke, 16 Sep 2026: hardened build reached the MTK home page
  successfully (1/1) with 655 ms startup, 687 ms page-ready, 282.9 MiB working
  set and 1.09 s CPU; normal shutdown completed.
- Post-install regression rerun, 16 Sep 2026: the complete local gate passed
  again after packaging and installation: 176 C++ cases plus 6 offline
  password-form cases, all with 0 failures.
- Installed smoke benchmark, 16 Sep 2026: one isolated installed-executable run
  reached a visible MTK Browser window in 639 ms, reported page readiness in
  681 ms, and measured 235.7 MiB combined working set. The window closed through
  the normal close path; this remains a one-sample baseline, not a Chrome/Brave
  comparison.
- Benchmark harness hardening, 16 Sep 2026: fixed PowerShell's one-item
  pipeline unwrapping and added a graceful CEF-process drain between samples,
  preventing warm singleton redirects from being reported as cold starts. A
  clean three-sample installed run reached 100% window/page readiness with
  median startup 294 ms, page readiness 308 ms and 208.9 MiB working set.
- Benchmark reporting polish, 16 Sep 2026: the summary now reports median CPU
  seconds alongside startup, page-ready and memory values; dry-run argument
  validation passed for the installed executable.
- Multi-window Tab Search, 16 Sep 2026: normal-window Tab Search now indexes
  tabs from every normal MTK window and foregrounds the selected window; private
  mode remains limited to the current private window. Professional Release
  compilation passed; on-screen multi-window interaction remains to be checked.
- Tab Search copy polish, 16 Sep 2026: the dialog hint now explicitly says it
  searches open windows (with matching Hindi copy), reducing ambiguity when
  multiple browser windows are present. The updated Professional Release binary
  compiled successfully; installer packaging and on-screen wording check remain
  pending.
- Windows Hello password-manager unlock, 16 Sep 2026: Settings now exposes an
  opt-in Windows Hello gate for opening the saved-password list. Availability is
  checked before the preference can be enabled; Windows owns the verification
  prompt and the browser receives only success/failure, never a PIN, password
  or biometric value. Preference persistence is covered by the local suite.
- Split view, 16 Sep 2026: Browser menu now lets two existing tabs share a
  side-by-side native window. Closing, reordering, detaching, pinning or adding
  a tab safely exits or adjusts the split, avoiding a stale second-pane target.
  Professional Release compilation passed; visual interaction testing remains.
- Latest installer, 16 Sep 2026: the Windows Hello and split-view build was
  packaged successfully as `MTK-Browser-Setup-0.3.0-Preview.exe` (120 MB,
  SHA-256 `3AD69E2038FE77E9BA5A3A24522AFC1814C6016C7D446232E2CF04C69714E1C6`),
  then installed to the existing per-user MTK Browser location. The installed
  executable hash matches the staged release executable exactly. It remains an
  unsigned Preview until a verified publisher certificate is supplied.
- Installed smoke, 16 Sep 2026: the latest installed executable reached a
  visible MTK Browser window in 647 ms with six related CEF processes and then
  shut down normally. An `example.com` title did not become ready within the
  45-second test window, so this is startup evidence only, not a website
  compatibility pass.
- Installed local-home validation, 16 Sep 2026: the current installed build
  reached its real `MTK Browser Home` document successfully (1/1), with 339 ms
  visible-window startup and 482 ms page readiness; it closed through the
  normal browser path. This validates the native internal-home launch flow,
  not interactive split-view or Windows Hello UI.
- Sandboxed external-URL launch repair, 16 Sep 2026: a CEF bootstrap was
  forwarding a positional web address only through its documented argument
  list, leaving the app DLL on Home. The browser now safely recovers approved
  `http(s)` arguments from that list while keeping paths and switches out of
  tabs. A fresh Professional Release profile opened `https://example.com/`
  successfully: 841 ms visible-window startup and 1,253 ms page-ready. The
  full local gate then passed: 177 C++ cases and 6 offline password-form cases
  with 0 failures.
- Latest installed URL-launch build, 16 Sep 2026: installer regeneration
  completed as the unsigned 120 MB Preview package (SHA-256
  `A84D43B92AE8FE0D9F9C56DAE1B61F8AD9C152EE3AEAE15E6D4D13384D3BD888`).
  It silently replaced the existing per-user install; the installed EXE hash
  exactly matches the staged EXE. An isolated installed run opened
  `https://example.com/` successfully (429 ms visible-window startup; 899 ms
  page-ready; 344.3 MiB combined CEF working set; normal shutdown). This is a
  one-page smoke test, not the full account/website compatibility matrix.
- Public compatibility smoke, 16 Sep 2026: `scripts/compatibility_smoke.ps1`
  now runs isolated account-free checks and records one clear CSV row per site.
  The current installed Preview passed all three checks: Example Domain (697 ms
  page-ready), Google Search (650 ms), and Wikipedia (889 ms). This proves only
  basic public HTTPS navigation/rendering; the authorized Gmail, WhatsApp,
  meeting, payment, banking, upload, PDF, streaming and document-editor rows
  remain release-matrix work.
- Memory Saver, Download Resume & Public Matrix Run, 20 Sep 2026:
  Integrated true tab discard (`native/src/memory_saver.h`) with context-menu
  sleep/wake actions and hollow-ring tab strip indicator. Added download range/resumption
  metadata across restarts (`native/src/download_manager.h`, `download_manager.cpp`).
  Restored missing account-picker strings in `native/src/strings.h` and registered
  `memory_saver.h` and Windows Runtime libraries in `native/CMakeLists.txt`.
  All 181 C++ logic unit tests and 6 password-form DOM tests passed (187 total cases, 0 failures).
  Both `native-professional` and `native-sandbox` compiled cleanly with MSVC.
  Packaged unsigned installer `MTK-Browser-Setup-0.3.0-Preview.exe` (120 MB,
  SHA-256 `4736A48B72388C63CA3924F077042B7E3CE9417FE19AAB60B9748B7163AE4A1A`).
  Hardened `benchmark_browser.ps1` with a dedicated Win32 window resolver to avoid
  spurious overlay windows. The staged Release build passed the full public compatibility
  smoke matrix: Example Domain (179 ms startup, 322 ms page-ready), Google Search
  (261 ms startup, 852 ms page-ready), and Wikipedia (257 ms startup, 833 ms page-ready).
- Local Milestone Expansion, 20 Sep 2026:
  Completed five self-contained local features from the backlog:
  1. Feature #11 (Vertical tabs collapsible left-rail UI): 240px expanded vs 48px collapsed
     icon-only rail with chevron toggle button, centered icons, tooltips, and preference persistence.
  2. Feature #12 (Nested bookmark folders & HTML export): Netscape bookmark HTML export
     with recursive `<DL><p>` folder hierarchy and segment splitting.
  3. Feature #14 (Consent-based address/contact autofill store): DPAPI-encrypted address
     profiles with explicit user consent enforcement, CRUD operations, and atomic persistence.
  4. Feature #37 (Offline reading list article archiving): local HTML article snapshot
     storage, atomic retrieval, and automatic lifecycle cleanup.
  5. Feature #40 (Web App standalone window chrome & management): dedicated standalone
     window chrome (hidden tab/bookmark bars, Back + Reload buttons, origin badge pill, clean
     title) and list/remove installed web app management.
  Test suite expanded to 189 C++ logic tests + 6 Node.js password DOM tests (195 total cases,
  0 failures). Release targets compiled cleanly with MSVC. Release staged to `dist/MTK Browser`.
  Packaged unsigned Preview installer `MTK-Browser-Setup-0.3.0-Preview.exe` (120 MB, SHA-256
  `CA48C22848989D4F63074B16E93B67A9F562878FD1192B36794F85B66BE2B382`). Sandboxed browser
  benchmark run verified startup in 750 ms, page ready in 907 ms, and 294.5 MiB working set.
- Bug fixes & test expansion, 20 Sep 2026:
  Fixed `kMaxReadingListEntries` undeclared identifier (reading list entry cap at 200) and
  `SaveBookmarkAndFlush` not updating the folder of an existing bookmark (now updates both
  title and folder atomically, and rejects non-existent non-empty folder names early).
  Added `MoveBookmarkAndMoveFolderReorderAndPersist` test covering bookmark and folder
  reorder operations with boundary checks and persistence round-trips. Test suite expanded
  to 191 C++ logic tests + 6 Node.js password DOM tests (197 total cases, 0 failures).
- Local Milestone & Self-Contained Completion, 21 Sep 2026:
  Delivered remaining self-contained local features across the backlog:
  1. Feature #8: Omnibox search shortcuts (@g, :g, @b, :b, @d, :d, @ddg, @w, :w) mapped to
     direct search engines (Google, Bing, DuckDuckGo, Wikipedia) with safe-search enforcement.
  2. Feature #12: Bookmark drag & drop reordering on the bookmarks bar with dynamic mouse
     drag tracking, visual border highlight, and "Move earlier" / "Move later" context menu actions.
  3. Feature #29: Fingerprint protection extended to navigator hardwareConcurrency (4) and
     deviceMemory (8) farbling alongside canvas and web audio protection.
  4. Feature #33: Clear activity on exit implementation (`ExecuteClearActivityOnExit`) purging
     session snapshots, browsing history, and finished downloads on browser clean exit.
  5. Feature #36: Workspaces management UI (switching, tab assignment, arrange tabs, clearing)
     with visual accent tabs, tooltip indicators, and durable V3 tab-to-workspace session persistence.
  6. Preference Serialization: Restored full INI persistence and safe default reset logic.
  Expanded test suite to 193 C++ logic tests + 6 Node.js password DOM tests (199 total cases,
  0 failures). Release build (`native-sandbox`) compiled cleanly. Release staged (249 files).
  Packaged unsigned Preview installer `MTK-Browser-Setup-0.3.0-Preview.exe` (120 MB, SHA-256
  `2155119F0D922ACCCEE673696FA788E0F751D1B3C426C9E61FB18A031EBF5B25`).
- Internal Web Pages, 3D Dashboard & System Monitor Milestone, 8 Oct 2026:
  Expanded native browser surfaces and testing gates:
  1. Feature #15 (Downloads): Dedicated internal web page `mtk://downloads/` with category
     tabs (All, Documents, Images, Archives, Media, Other), live search, clean file naming,
     progress bars, and inline pause/resume/cancel/retry/open/show actions.
  2. Feature #20 (Searchable settings): Dedicated internal web page `mtk://settings/` with
     live query filtering, categorized preferences (Appearance, Search, Shields, System),
     diagnostics display, and safe default reset.
  3. Feature #21 (Customizable 3D Home Dashboard): Upgraded `mtk://home/` with a complete suite
     of animated 3D WebP button graphics (Search, Home, Store, Notes, Download, History,
     Settings, Bookmarks, Focus, Screenshot), real-time clock and greeting, live weather
     widget via wttr.in, daily inspirational quotes with shuffle, interactive quick notes
     scratchpad with localStorage persistence, and download cards with file-type badges.
  4. Feature #14 & #4 (Passkeys & Internal Capability): Dedicated internal web page
     `mtk://passkeys/` verifying WebAuthn/platform authenticators with English and Hindi copy.
  5. Feature #23 (Per-tab/process Task Manager): Native Win32 task manager dialog
     (`native/src/task_manager_dialog.cpp`) monitoring Browser, GPU, Tab, and Utility tasks
     with CPU%, memory formatting, column sorting, and localization.
  6. Feature #2.1 (Private Session Lifecycle Isolation): Multi-window private session tracker
     (`native/src/private_session.cpp`) guaranteeing strict isolation, zero disk leakage for
     private downloads and history, and session cleanup.
  7. Feature #11 (Bespoke Screenshot Tools Panel): Completely redesigned `screenshot_panel.cpp`
     from scratch, replacing the generic Chrome-plugin clone with MTK Browser's signature
     dark luxury obsidian aesthetic (`RGB(16, 20, 35)`), integrated camera header, distinct
     neon-accented cards (Electric Violet for Visible Screen, Cyberpunk Cyan for Area Snip,
     Sunset Coral for Entire Webpage), custom high-precision GDI vector glyphs, status footer,
     and full English/Hindi localization (`strings::IsHindi()`).
  Test suite verified at 260 C++ logic tests + 6 Node.js password DOM tests (266 total cases,
  0 failures). Sandboxed Release target compiled cleanly. Release staged (285 files).
  Packaged unsigned Preview installer `MTK-Browser-Setup-0.3.0-Preview.exe` (124.5 MB, SHA-256
  `B02FA77D769ECB795B6D6C4889B59A07E3E8D744A179FC65C2197BCCBBE79DCB`).
  Public compatibility smoke matrix verified: Example Domain (557 ms startup, 710 ms page-ready),
  Google Search (265 ms startup, 978 ms page-ready), and Wikipedia (286 ms startup, 1167 ms page-ready).

## Gates

### Latest local implementation batch

- Added a direct offline PDF export using CEF PrintToPDF, a user-selected path,
  temporary output and replacement only after successful completion. The
  reading list still stores links; it does not manage these exported PDFs.
- Tab search now includes group labels. Added local diagnostics for build SDK,
  window/tab counts, DPI, private-window state and memory-saver setting.
- Professional Release compilation passed. On-screen automation opened the
  older installed Preview rather than the requested build; its menu lacked the
  new commands. PDF export and diagnostics runtime validation remain pending.

- Preserve user profile data; all automated storage tests use a throwaway profile.
- Do not purchase services, publish, upload browsing data or create accounts merely
  to unblock a feature. Record the exact external dependency and continue locally.
- Keep ROADMAP.md limitations accurate; do not mark this entire backlog complete
  based on one delivered subfeature.
