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
  `Alt+Left/Right`, `F5`, and `Ctrl+R`
- Search-or-address input resolution with safe Google fallback
- HTTPS upgrade and SafeSearch URL enforcement
- Unsafe/private-navigation blocking with an MTK block page
- Context-aware network ad/tracker filtering across scripts, frames, XHR,
  media, workers, pings, prefetches, fonts, images, and other resources
- Conservative cosmetic filtering for common Google, GPT, Taboola, Outbrain,
  and standard ad-slot containers
- DNT and GPC privacy preference headers
- Deny-by-default permissions, downloads, invalid certificates, HTTP auth,
  client certificates, external protocols, and context menus
- Isolated installation data under `%LOCALAPPDATA%\MTK Browser\User Data`
- Automatic staging of the pinned CEF runtime and resources

The reviewed CEF 150 SDK now compiles and links with MSVC, and the native
Release executable has passed a basic responsive-window startup smoke test.
The active build output is
`build/native-adblock/native/Release/MTKBrowser.exe`; the staged application is
`dist/MTK Browser/MTKBrowser.exe`, and `npm run dist:win` creates the native
preview installer.

This remains a development build with `no_sandbox=true`. CEF 150 uses
`bootstrap.exe` plus an application DLL for the Windows sandbox rather than the
legacy `cef_sandbox.lib` flow. Production distribution is blocked until that
bootstrap packaging, signing, exact notices/SBOM, and installer validation are
complete.

Next milestones are richer native settings/shields panels, history/profile
storage policy, signed admin-policy updates, CEF bootstrap sandbox packaging,
installer/signing, and release compliance artifacts.
