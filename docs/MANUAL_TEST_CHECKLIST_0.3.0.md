# MTK Browser 0.3.0 Preview — Manual Test Checklist

Automated and isolated-profile testing already covered: build, session save/restore
filtering, download-history persistence across restart, a real completed download,
window close with pending restored tabs, and second-launch profile protection.

This checklist covers what automation could **not** verify: native Win32 dialogs,
tab-strip mouse behaviour, and real-world browsing quality.

## Setup

- Build under test: `dist/installer/MTK-Browser-Setup-0.3.0-Preview.exe` (120.0 MB, unsigned)
- Install per-user; expect a SmartScreen "unknown publisher" prompt. That is expected for this preview.
- Data locations to inspect: `%LOCALAPPDATA%\MTK Browser\` (`preferences.ini`, `session.v1`, `downloads.v1`, `library.v1`, `User Data\`)
- Downloads land in `%USERPROFILE%\Downloads\MTK Browser`
- Record: Windows version, GPU, and whether any step needed a retry.

## 1. Install and first run

| # | Step | Expected | Result |
|---|---|---|---|
| 1.1 | Run installer, accept desktop shortcut | Installs to `%LOCALAPPDATA%\Programs\MTK Browser`, no admin prompt | |
| 1.2 | Launch from Start Menu | Dark window opens, one tab, home page loads | |
| 1.3 | Right-click `MTKBrowser.exe` → Properties → Details | ProductName "MTK Browser", version "0.3.0 Preview" | |
| 1.4 | Start Menu folder | "Preview notice" and "Uninstall MTK Browser" entries exist | |
| 1.5 | Launch a second time while running | Existing window comes to front, no second window, no error | |

## 2. Tabs and window lifecycle (highest priority — not automated)

| # | Step | Expected | Result |
|---|---|---|---|
| 2.1 | `Ctrl+T` a few times, switch with `Ctrl+Tab` / `Shift+Ctrl+Tab` | Correct tab activates, page content matches the selected tab | |
| 2.2 | Click a tab's `x` glyph on a non-last tab | That tab closes, neighbour becomes active, no flicker of the wrong page | |
| 2.3 | **Close a non-last tab, then immediately close the window** | App exits cleanly within a few seconds, no hang, no crash dialog | |
| 2.4 | Open a new tab and click its `x` before the page renders | Tab disappears, app stays responsive | |
| 2.5 | `Ctrl+W` on the only remaining tab | Whole window closes | |
| 2.6 | After each exit, check Task Manager | No leftover `MTKBrowser.exe` processes | |
| 2.7 | After each exit, check Event Viewer → Application | No `MTKBrowser.exe` APPCRASH entries | |

## 3. Session restore

| # | Step | Expected | Result |
|---|---|---|---|
| 3.1 | Settings → On startup → "Continue where you left off", Save | Setting persists in `preferences.ini` (`startup_mode=continue`) | |
| 3.2 | Open 3 sites, make tab 2 active, close window, relaunch | Same 3 tabs restored, tab 2 active | |
| 3.3 | Open 25 tabs, close, relaunch | At most 20 tabs restored, no error | |
| 3.4 | Visit a blocked page so the MTK block page shows, then close and relaunch | Blocked/internal page is **not** restored | |
| 3.5 | Switch back to "Open the home page", Save, close, relaunch | Only the home page opens; `session.v1` is gone | |
| 3.6 | With continue mode on, kill the process from Task Manager, relaunch | Tabs from the autosaved snapshot come back | |

## 4. Settings

| # | Step | Expected | Result |
|---|---|---|---|
| 4.1 | Tab through the Settings dialog | Logical tab order, all labels fully readable, nothing clipped | |
| 4.2 | Set a valid home page, Save, click Home | Navigates to that page; survives restart | |
| 4.3 | Enter a blocked/adult URL as home page | Rejected with a "Home page blocked" message, not saved | |
| 4.4 | Enable “On exit, clear browsing history, finished download history and saved tabs”; visit a harmless site, complete a small download, then close all MTK windows normally and relaunch | History, finished download row and saved tabs are gone; bookmarks, passwords, site settings, cookies/cache and the downloaded file remain | |
| 4.5 | In Site settings, allow camera and/or microphone for an authorized disposable test site; start then stop device capture | `CAM`, `MIC`, or `CAM+MIC` appears only while capture is live and disappears when capture stops; a stored Allow without capture shows no badge | |
| 4.6 | Choose “Allowed until MTK Browser closes” for an authorized test site's camera or microphone, verify one request, fully close/relaunch MTK Browser and request again | Access works only during that run; after restart it is refused until explicitly granted again | |
| 4.4 | Change website language, Save | Notice says restart needed; after restart pages honour it | |
| 4.5 | Cancel after editing fields | Nothing is saved | |

## 4A. PDF viewing

| # | Step | Expected | Result |
|---|---|---|---|
| 4A.1 | Open `https://www.rd.usda.gov/sites/default/files/pdf-sample_0.pdf` | PDF renders inside the current MTK Browser tab; Edge/default browser does not open and no automatic download starts | |
| 4A.2 | Use the PDF viewer's download button | Existing safe/standard download policy is used | |
| 4A.3 | Reload, zoom, change pages, then use Back | Viewer remains responsive and Back returns to the previous page | |
| 4A.4 | Close the PDF tab while it is loading | Tab closes cleanly with no leftover MTK Browser process after app exit | |

## 4C. Advanced tab controls and diagnostics

| # | Step | Expected | Result |
|---|---|---|---|
| 4C.1 | Open three ordinary pages, assign two to the same tab group | Group name is visible in the tab strip and Tab search results | |
| 4C.2 | Open Tab groups, choose the group, then restart MTK Browser | Group members are arranged together and the group label survives restart | |
| 4C.3 | Use Tab groups → Ungroup tabs | Tabs remain open and only their group labels are removed | |
| 4C.4 | Browser menu → Browser diagnostics | Dialog shows local CEF version, window/tab count, DPI, private state and memory-saver state; no URL, password or browsing-history data is shown | |
| 4C.5 | Browser menu → Passkey capability check | Internal page reports WebAuthn and platform-authenticator availability without creating or transmitting a credential | |
| 4C.6 | On a permitted HTTPS page, choose Open in separate app window | A separate window opens with an isolated profile and a Start-menu shortcut is created; the original tab stays open | |
| 4C.7 | Hover a normal tab, then move over its close button | Tooltip shows title, group and full URL; it disappears over the close button and when the pointer leaves the tab strip | |
| 4C.8 | Open a second normal MTK window, then use Tab Search in the first window | Tabs from both normal windows are listed; choosing the second-window tab foregrounds that window and selects the tab | |
| 4C.9 | Open a private window and use Tab Search | Only tabs from that private window are listed; normal-window tabs never appear | |
| 4C.10 | Open two tabs, Browser menu → Split view, then select and close each pane | Both live pages resize side-by-side; selecting the right pane promotes it without reloading; closing/reordering/pinning returns safely to one pane | |
| 4C.11 | Enable Windows Hello for saved passwords in Settings, then open Saved passwords | Windows checks availability before saving; its secure prompt appears before the password list, and cancellation keeps the list closed | |

## 5. Downloads — safe mode (default)

| # | Step | Expected | Result |
|---|---|---|---|
| 5.1 | Download an image or other allowed document | Confirmation appears first, then file saves to `Downloads\MTK Browser` | |
| 5.2 | Decline the confirmation | Nothing is saved | |
| 5.3 | Try to download a `.exe` / `.msi` / `.bat` | Blocked with a clear reason | |
| 5.4 | Try a double-extension file such as `invoice.pdf.exe` | Blocked | |
| 5.5 | Download the same filename twice | Second file gets a new name, first is not overwritten | |
| 5.6 | Select a completed row → "Open file" | Explains safe mode never opens files | |
| 5.7 | Select a completed row → "Show in folder" | Explorer opens with the file selected | |
| 5.8 | Start a large (>2 GB) download | Warns but does **not** block; transfer keeps running | |
| 5.9 | Right-click a finished download in Explorer → Properties | A Security notice with an "Unblock" checkbox is shown (mark of the web applied) | |

## 4B. Parent PIN

| # | Step | Expected | Result |
|---|---|---|---|
| 4B.1 | Settings → "Parent PIN...", set a PIN | Needs at least 4 characters; both entries must match | |
| 4B.2 | Close Settings, reopen it | The PIN is requested before Settings opens | |
| 4B.3 | Enter a wrong PIN | Refused; Settings stays closed and the prompt stays open | |
| 4B.4 | Cancel the PIN prompt | Settings does not open | |
| 4B.5 | Restart the browser and open Settings | The same PIN is still required and still works | |
| 4B.6 | Settings → "Parent PIN..." → change PIN | The current PIN is required first | |
| 4B.7 | Settings → "Parent PIN..." → Remove PIN | Current PIN required, then a confirmation; afterwards Settings opens without a PIN | |
| 4B.8 | Inspect `preferences.ini` | Only `pin_salt` / `pin_hash` hex appear; the PIN itself is never stored | |

## 6. Downloads — standard mode

| # | Step | Expected | Result |
|---|---|---|---|
| 6.1 | Settings → Downloads → Standard, Save | Requires an explicit acknowledgement | |
| 6.2 | Download a document | Saves with no prompt and no size warning | |
| 6.3 | Download an executable type | Warns before saving | |
| 6.4 | "Open file" on that executable row | Warns again before opening | |
| 6.5 | Verify persistence | Mode survives restart (`download_mode=standard`) | |

## 7. Downloads dialog controls (not automated)

| # | Step | Expected | Result |
|---|---|---|---|
| 7.1 | `Ctrl+J` and overflow menu → Downloads | Same dialog opens both ways | |
| 7.2 | Start a slow download, watch the toolbar `↓` button | Badge shows the active count and clears when finished | |
| 7.3 | Select an in-progress row | Pause and Cancel are enabled | |
| 7.4 | Click Pause | Row shows paused, button label becomes "Resume" | |
| 7.5 | Click Resume | Transfer continues, label returns to "Pause" | |
| 7.6 | Click Cancel on an active row | Transfer stops, row shows canceled | |
| 7.7 | Select a completed row | Pause and Cancel are disabled | |
| 7.8 | Pause a download, close the app, relaunch, open Downloads | Row appears as interrupted; Pause/Resume stays disabled for it | |
| 7.9 | Click Cancel on a finished/interrupted row | Row is removed from history | |
| 7.10 | Clear finished | Only finished rows are removed, active ones remain | |
| 7.11 | Open folder | Opens `Downloads\MTK Browser` | |

## 8. Context menu

| # | Step | Expected | Result |
|---|---|---|---|
| 8.1 | Right-click empty page area | Back, Forward, Reload only (enabled per state) | |
| 8.2 | Right-click a link | "Open link in new tab" and "Copy link address" appear | |
| 8.3 | Use "Open link in new tab" | Opens in a new MTK tab | |
| 8.4 | Use "Copy link address", paste into the omnibox | Correct URL pasted | |
| 8.5 | Right-click a link to a blocked site | "Open link in new tab" is greyed out | |
| 8.6 | Right-click an image → "Save image" | Goes through the normal download policy | |
| 8.7 | Right-click inside a text field | Copy / Paste / Select all appear when applicable | |
| 8.8 | Right-click ordinary page content → Inspect element | Associated CEF DevTools opens at the clicked element | |
| 8.9 | Press F12 on an ordinary page | Associated CEF DevTools opens; Console and Network panels are usable | |

## 9. Safety and shields

| # | Step | Expected | Result |
|---|---|---|---|
| 9.1 | Search an adult term on Google, Bing, DuckDuckGo | Blocked or SafeSearch enforced | |
| 9.2 | Open `https://www.youtube.com/check_content_restrictions` | "HTTP header restrictions are on — Strict" is reported | |
| 9.2a | Search mature terms inside YouTube (in-page search box, not the omnibox) | Restricted Mode filters results; YouTube shows its Restricted Mode notice | |
| 9.2b | Play a normal video, drag the seek bar, use chapters and full screen | Seek bar is visible and draggable; only ad markers stay hidden | |
| 9.2c | Instagram: click the sidebar Search icon, type a normal term | Search icon is visible and works; results load on the keyword search page, no browse grid appears | |
| 9.2d | Instagram: search a vulgar term, open `/reels/`, open an adult hashtag | Each is refused with the MTK block page | |
| 9.2e | Instagram: home feed, a profile, stories, messages, notifications | All work normally | |
| 9.2f | Instagram: scroll the feed for a few minutes | Sponsored posts do not appear; organic posts are untouched | |
| 9.3 | Visit a known adult/piracy domain | MTK block page appears | |
| 9.4 | Visit `http://` site | Upgraded to HTTPS where possible | |
| 9.5 | Try `http://192.168.x.x` or `localhost` | Denied | |
| 9.6 | Visit an ad-heavy news site | Ads/trackers blocked, Shields count rises, layout has no big empty ad gaps | |
| 9.7 | Trigger camera / mic / location / notification prompts | All denied, no prompt shown to the user | |
| 9.8 | Click a `mailto:` or custom protocol link | Not launched externally | |
| 9.9 | Visit a site with an invalid certificate | Refused, no click-through option | |

## 9A. Block page animation

| # | Step | Expected | Result |
|---|---|---|---|
| 9A.1 | Trigger any block (for example `http://192.168.1.1/`) | Shield animation plays above "Navigation blocked": shield sways, keyhole pulses, rings expand outward | |
| 9A.2 | Leave the block page open for a minute | Loop repeats smoothly with no visible jump, no square edge around the animation | |
| 9A.3 | Trigger several blocks in a row, in several tabs | Animation appears every time and starts immediately; no flicker or blank box | |
| 9A.4 | Turn on Windows Settings > Accessibility > Visual effects > Animation effects **off**, then trigger a block | Static shield glyph is shown instead of the animation | |
| 9A.5 | Rename `mtk_block_shield.webm` in the install folder, restart, trigger a block | Page still renders correctly with the static shield glyph | |
| 9A.6 | Confirm the animation is silent | No audio at any volume; it is muted and decorative | |

## 10. Everyday browsing quality

| # | Step | Expected | Result |
|---|---|---|---|
| 10.1 | Type a search phrase in the omnibox | Resolves to a safe search | |
| 10.2 | Back / Forward / Reload / Stop and Home buttons | All behave correctly; progress indicator appears while loading | |
| 10.3 | `Ctrl+F`, type, `F3` / `Ctrl+G`, `Esc` | Match count updates, next/previous works, Esc closes | |
| 10.4 | `Ctrl++`, `Ctrl+-`, `Ctrl+0` | Zoom in/out/reset applies per tab | |
| 10.5 | Add bookmark, reopen from Bookmarks | Re-validated and opens; persists after restart | |
| 10.6 | Open History, open an entry, remove one, clear all | Behaves as labelled; persists correctly | |
| 10.10 | Type 2+ characters of a visited or bookmarked site in the omnibox | A short list appears under the toolbar; bookmarks (★) are listed before history (🕘) | |
| 10.11 | Use Down/Up then Enter, then repeat with a mouse click | The highlighted entry opens; Esc and clicking elsewhere close the list | |
| 10.12 | Type only 1 character, then clear the omnibox | No list is shown | |
| 10.13 | Overflow menu → Clear browsing data | Eight choices; nothing selected is refused; the summary lists exactly what was cleared or requested | |
| 10.14 | Clear cookies only, then revisit a site you were signed in to | You are signed out; history and bookmarks are untouched | |
| 10.15 | With a parent PIN set, open Clear browsing data | The PIN is required first | |
| 10.16 | Set MTK Browser as default in Windows Default apps, fully close it, then open a harmless HTTPS link from another application | MTK opens the requested URL as the first tab, not only the home page | |
| 10.17 | With MTK Browser already open, open a second harmless HTTPS link from another application | Existing MTK window is focused and a new tab opens the requested URL; no second same-profile process remains | |
| 10.18 | On a disposable HTTPS storage-test page, create a cookie, localStorage row, IndexedDB row and Cache Storage entry; select Cookies and site data, close/reopen the page | The site is signed out and each disposable storage bucket is absent. Record any surviving bucket as a release blocker. | |
| 10.7 | Resize and maximise the window | Toolbar and page area lay out correctly, no gaps or overlap | |
| 10.10 | `F11`, then `F11` or `Esc` again | Page fills the screen with no chrome; leaving restores the exact previous size, position and tabs | |
| 10.11 | Overflow menu → Full screen / Exit full screen | Same behaviour as F11; the label reflects the current state | |
| 10.12 | Close a tab, then `Ctrl+Shift+T` (or menu → Reopen closed tab) | The closed tab comes back with its address; menu item is greyed out when nothing was closed | |
| 10.13 | Close a blocked page's tab, then `Ctrl+Shift+T` | The blocked page is **not** restored | |
| 10.14 | Open ~15 tabs, then scroll the mouse wheel over the tab strip | The strip scrolls horizontally; tabs keep a readable width instead of shrinking | |
| 10.15 | With many tabs open, switch tabs with `Ctrl+Tab` | The selected tab always scrolls into view | |
| 10.8 | Play a video (YouTube or similar) | Video and audio work | |
| 10.9 | Log into a normal site | Login works within the session | |

## 10A. Release compatibility matrix

Run this matrix on a clean, non-private profile for each Release candidate. Use
throwaway accounts only; never record passwords, messages, payment details or
personal browsing data in the Result column. Record browser version, CEF build,
Windows version, test date, outcome (`Pass`, `Fail`, `Blocked`), and a short
non-sensitive symptom or issue link for every row. A `Blocked` row is not a
pass: it needs an owner and retest date before release promotion.

| # | Service / flow | Test | Expected | Result |
|---|---|---|---|---|
| 10A.1 | Google Account | Sign in, open account page, sign out | Login, redirects and cookies work; sign-out removes session | |
| 10A.2 | Gmail | Open inbox, read a test email, compose then discard | Mail UI loads; editor, attachments picker and discard flow work | |
| 10A.3 | YouTube | Search, play a normal video, seek, captions, full screen, PiP if offered | Video/audio controls stay usable; no player breakage | |
| 10A.4 | WhatsApp Web | Link a disposable test account, send/receive a test message, sign out | QR/linking, messages and attachment picker work | |
| 10A.5 | Google Meet / Jitsi | Join a private test room without admitting external participants | Camera/mic permission state is clear; expected deny/default behavior is explained and no crash occurs | |
| 10A.6 | File upload | Upload a harmless small text/image file to a test site, then cancel/remove it | Native picker opens; selected file reaches the site only after user chooses it | |
| 10A.7 | Office web editor | Open a disposable document and edit a short non-sensitive line | Editing, autosave indicator and keyboard input work | |
| 10A.8 | Payment sandbox | Use an official sandbox/test checkout only; never a live card or bank login | Redirects, 3DS/test UI and cancel path work; no live transaction | |
| 10A.9 | Banking | Public landing page only unless a bank-approved test environment exists | TLS, navigation and rendering work; do not enter credentials in production banking | |
| 10A.10 | PDF | Open a public PDF, search, rotate if available, Print → Save as PDF then cancel | PDF viewer is in-tab; no crash or unexpected external app | |
| 10A.11 | Streaming | Public trailer/sample from a supported service | Playback, seek and full screen work; document DRM/codec limitation exactly | |
| 10A.12 | Internal pages | Visit `mtk://home/`, history and a deliberately blocked URL | CSP and navigation boundaries hold; no external content or script injection | |
| 10A.13 | External URL launch | With MTK Browser selected for `https`, open an ordinary link from a separate Windows app or Run dialog | The target opens as the first tab; unsafe schemes and local paths do not open | |

### Compatibility release gate

- Security-sensitive rows (10A.1, 10A.5, 10A.8, 10A.9 and 10A.12) must be
  `Pass` or have a written, user-visible limitation approved for the channel.
- Public-service checks may be repeated by engineering. Account, payment and
  private-call checks require a designated tester with authorized test accounts.
- Preserve only non-sensitive screenshots/log excerpts in the release issue;
  do not attach QR codes, inbox content, account identifiers or payment data.

## 11. Uninstall

| # | Step | Expected | Result |
|---|---|---|---|
| 11.1 | Uninstall from Settings → Apps | Removes program folder and shortcuts | |
| 11.2 | Check `%LOCALAPPDATA%\MTK Browser` | User data behaviour matches expectations (document what remains) | |
| 11.3 | Reinstall | Installs cleanly over the previous state | |

## 11A. Updates

| # | Step | Expected | Result |
|---|---|---|---|
| 11A.1 | Overflow menu → "Check for updates..." | Reports "MTK Browser is up to date" when no newer version is published | |
| 11A.2 | Disconnect the network, then check again | Reports a failed check; no crash, nothing downloaded | |
| 11A.3 | Settings → uncheck automatic updates, Save | Asks for confirmation first; `preferences.ini` shows `auto_update=off` | |
| 11A.4 | With a parent PIN set, try to reach that setting | Settings, and therefore the update switch, need the PIN | |
| 11A.5 | Publish a newer signed manifest, then check | Installer downloads, then asks before installing; declining leaves the browser running | |
| 11A.6 | Accept the update | Browser closes and the installer starts | |
| 11A.7 | Check `%LOCALAPPDATA%\MTK Browser\Updates` after a restart | Old downloaded installers are cleaned up | |

## 12. Sandbox

| # | Step | Expected | Result |
|---|---|---|---|
| 12.1 | Check the install folder | Both `MTKBrowser.exe` and `MTKBrowser.dll` are present | |
| 12.2 | `MTKBrowser.exe` → Properties → Details | ProductName "MTK Browser", version "0.3.0 Preview", MTK icon shown | |
| 12.3 | With the browser open, run Task Manager → Details → add the "Integrity" column | Renderer processes show **Untrusted**, GPU shows **Low**, only the main process is Medium | |
| 12.4 | Task Manager → any child process → command line | No `--no-sandbox` anywhere | |
| 12.5 | Browse normally: pages, video, PDF in a tab, a download, Settings, history | Everything still works with the sandbox on | |
| 12.6 | Delete `MTKBrowser.dll` and launch | The app refuses to start instead of running unsandboxed (expected; reinstall to fix) | |

## Known gaps — do not treat as test failures

- No managed real-time category feed; filtering is local plus optional managed DNS.
- Build is unsigned, so SmartScreen warnings are expected.
- Pause/Resume works only within the running process; CEF cannot resume transfers across a restart.
- Session restore keeps URLs only, not per-tab history stacks or login cookies.

## Sign-off

- Tester:
- Date:
- Build SHA-256: `32637E3698C33BA9B18DED978B52B3979DAD44753BFDB565CE68C6BC750D6D9D`
- Blocking issues found:
- Verdict: pass / pass with notes / fail
