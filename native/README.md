# MTK Browser — Windows native foundation

This directory is the first native implementation track for MTK Browser. It uses a pinned CEF binary distribution with a native Win32 shell and a C++ policy boundary. The existing Electron prototype remains in `src/` for reference and behavior comparison; it is not the target architecture.

## Prerequisites

- Windows 10/11 x64
- Visual Studio 2022/2026 C++ desktop workload
- CMake 3.21+
- A reviewed CEF Windows binary distribution matching the selected version

CEF is intentionally not downloaded by this repository. Keep the selected distribution outside the source tree or in an approved vendor location, record its version/commit, and generate its notices/SBOM before release.

## Configure

From a Developer PowerShell:

```powershell
cmake -S . -B build/native-2026 -G "Visual Studio 18 2026" -A x64 -DCEF_ROOT="E:\MTK Browser\vendor\cef-sdk"
cmake --build build/native-2026 --config Release
```

The build stages the pinned CEF `Release` and `Resources` payload beside `MTKBrowser.exe`. Do not distribute a build until the CEF 150 `bootstrap.exe` application-DLL sandbox packaging is implemented and the exact CEF/Chromium notices and dependency inventory have been generated.

## Current scope

- Native Win32 dark window, custom toolbar and protected omnibox
- Real multi-tab browsing with separate CEF browser instances
- New-tab, tab switching, closing and popup-to-tab routing
- Back, forward, reload/stop, home and live navigation state
- Browser-focus shortcuts: `Ctrl+L`, `Ctrl+T`, `Ctrl+W`, `Ctrl+Tab`,
  `Alt+Left/Right`, `F5`, `Ctrl+R`, `Ctrl+F`, `F3`, `Ctrl+G`,
  `Ctrl++`, `Ctrl+-`, `Ctrl+0`, and `Ctrl+J`
- Native find-in-page bar with next/previous result count and escape-to-close
- Per-tab CEF zoom controls with `Ctrl++`, `Ctrl+-`, and `Ctrl+0` reset
- Search-or-address input resolution with safe Google fallback
- HTTPS upgrade and SafeSearch URL enforcement
- Unsafe/private-navigation blocking with an MTK block page
- Context-aware network ad/tracker filtering across scripts, frames, XHR,
  media, workers, pings, prefetches, fonts, images, and other resources
- Conservative cosmetic filtering for common Google, GPT, Taboola, Outbrain,
  and standard ad-slot containers
- DNT and GPC privacy preference headers
- Deny-by-default permissions, invalid certificates, HTTP auth,
  client certificates, and external protocols
- Curated context menu with safe navigation/edit actions and image saves routed
  through the download policy; DevTools, View Source and renderer-defined
  commands stay unavailable
- Isolated CEF data under `%LOCALAPPDATA%\MTK Browser\User Data` and MTK-owned preferences under `%LOCALAPPDATA%\MTK Browser\preferences.ini`
- Native Settings dialog with a saved home page, startup mode, download mode and website-language preference
- Optional atomic URL-only session restore (maximum 20 policy-approved tabs)
- Persistent History and Bookmarks library, with safe re-validation before saved links open
- Persistent download history plus current-process Pause/Resume, cancel, clear,
  reveal in Explorer, and an active-download toolbar badge
- **Safe downloads (default):** only allowlisted document, image, audio, video
  and archive types are saved, every download is confirmed first, and a saved
  file is never opened by the browser. Files over 2 GB are not blocked: the
  confirmation notes the size when it is known up front, and a download that
  passes 2 GB with an unknown size warns once and keeps running
- **Standard downloads:** mainstream browser behaviour, any file type saves
  straight to the download folder with no prompts or size warnings, files can
  be opened from the manager, and types that can execute code are confirmed
  before they are saved and again before they are opened
- Neither mode enforces a download size limit
- Download protections that apply in both modes: downloads only from addresses
  allowed by the safety policy, file names sanitised against path traversal and
  reserved device names, no overwrite of an existing file, and a fixed
  destination of `%USERPROFILE%\Downloads\MTK Browser`
- Switching from safe to standard downloads requires an explicit acknowledgement
- Automatic staging of the pinned CEF runtime and resources

The reviewed CEF 150 SDK compiles and links with MSVC. The native Release
executable has passed isolated responsive-window, session-restore,
download-history restart, and real small-download smoke tests. The active build
output is `build/native-adblock/native/Release/MTKBrowser.exe`; the staged
application is `dist/MTK Browser/MTKBrowser.exe`, and `npm run dist:win` creates
the native preview installer.

This remains a development build with `no_sandbox=true`. CEF 150 uses
`bootstrap.exe` plus an application DLL for the Windows sandbox rather than the
legacy `cef_sandbox.lib` flow. Production distribution is blocked until that
bootstrap packaging, actual Authenticode signing, exact notices/SBOM, and a
final clean-machine installer/manual desktop validation pass are complete.

Next milestones are CEF bootstrap sandbox packaging, an operated signed category
feed, administrator-locked policy, production code signing, and release
compliance artifacts.
