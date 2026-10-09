# MTK Browser 0.3.0 Preview

MTK Browser is a privacy-first, family-safe browser project. The shipped Windows track is the native C++20, Win32, and CEF implementation under `native/`. The `src/` Electron prototype remains only for behavior comparison and is not included in the native installer.

## Included protections
- Normal images and media are allowed on policy-permitted requests and sites.
- Adult-content terms and known unsafe domains are blocked.
- Google, Bing, DuckDuckGo, and YouTube strict modes are enforced.
- Common ad and tracker requests are blocked.
- Popups, notifications, camera, microphone, location, USB, Bluetooth, and external protocols are denied.
- Safe and Standard download modes share origin checks and filename hardening.
- Private/local network addresses are denied.
- CEF browser data is isolated under `%LOCALAPPDATA%\MTK Browser\User Data`; optional URL-only session restore is stored separately.
- HTTPS upgrades are enabled.

## Native build and installer
The release Windows build uses CEF's sandboxed bootstrap packaging. Configure a
fresh build directory with sandboxing left on (the default), then stage its
complete CEF runtime beside the native executable:

```powershell
cmake -S . -B build/native-sandbox -DCEF_ROOT="E:/MTK Browser/vendor/cef-sdk" -A x64
cmake --build build/native-sandbox --config Release --target mtk_browser
powershell -ExecutionPolicy Bypass -File scripts/stage_release.ps1
```

Build the per-user Windows installer, including Desktop/Start Menu shortcuts and an uninstaller, with:

```bat
npm run dist:win
```

The installer is generated at `dist/installer/MTK-Browser-Setup-0.3.0-Preview.exe`. `npm start` launches the native application; the Electron prototype is available only through `npm run start:legacy`.

## Release verification

Run the C++ safety/persistence suite plus the offline password-form fixtures:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/run_tests.ps1
```

Measure clean installed startup without reusing a singleton process:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/benchmark_browser.ps1 `
  -BrowserPath "$env:LOCALAPPDATA\Programs\MTK Browser\MTKBrowser.exe" `
  -Url mtk://home/ -Samples 3 -ReadyTitlePattern '*MTK Browser*'
```

The benchmark uses throwaway profiles, waits for all CEF children to exit
between samples, and writes CSV evidence under `build/benchmarks/`.

## Preview security status
The native release configuration enables CEF's Windows sandbox. The staged app
contains both `MTKBrowser.exe` (CEF bootstrap) and `MTKBrowser.dll` (the MTK
application); neither file alone is a valid payload. Builds are still unsigned
unless an operator supplies and verifies an Authenticode certificate through the
release signing environment described in `RELEASE_NOTES_0.3.0.md`.

No browser can guarantee detection of every illegal or newly published page using a static local list. For deployment to children, schools, or organizations, combine MTK Browser with managed DNS filtering, operating-system parental controls, administrator-locked settings, and a continuously updated categorization service.
