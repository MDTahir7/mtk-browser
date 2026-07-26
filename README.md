# MTK Browser

A privacy-first, family-safe browser project.

The repository now contains two tracks:

- `native/`: the target Windows implementation using CEF, C++ and a native Win32 shell.
- `src/`: the legacy Electron prototype retained only for behavior comparison; it is not the target architecture.

## Included protections

- Normal images and media are allowed on permitted websites
- Adult-content terms and known unsafe domains are blocked
- Google, Bing, DuckDuckGo, and YouTube strict modes are enforced
- Common ad and tracker requests are blocked
- Popups, downloads, notifications, camera, microphone, location, USB, Bluetooth, and external protocols are denied
- Private/local network addresses are denied
- Browsing uses an in-memory session that is cleared on exit
- Chromium sandbox, context isolation, and HTTPS upgrades are enabled

## Current development tracks

The reviewed Windows build currently uses `build/native-adblock` and stages its
complete CEF runtime beside the native executable:

```powershell
cmake --build build/native-adblock --config Release --target mtk_browser
powershell -ExecutionPolicy Bypass -File scripts/stage_release.ps1
```

Build the per-user Windows installer (Desktop/Start Menu shortcuts and
uninstaller included) with:

```bat
npm run dist:win
```

The installer is generated at
`dist/installer/MTK-Browser-Setup-0.2.0-Preview.exe`. The `src/` Electron
prototype remains available only for behavior comparison via `npm run
start:legacy`; `npm start` now launches the native application.

## Important limitation

No browser can guarantee detection of every illegal or newly published page using a static local list. For deployment to children, schools, or organizations, combine this app with a managed DNS filtering service, operating-system parental controls, administrator-locked settings, and a continuously updated categorization service.