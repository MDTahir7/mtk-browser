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
The active Windows build uses `build/native-adblock` and stages its complete CEF runtime beside the native executable:

```powershell
cmake --build build/native-adblock --config Release --target mtk_browser
powershell -ExecutionPolicy Bypass -File scripts/stage_release.ps1
```

Build the per-user Windows installer, including Desktop/Start Menu shortcuts and an uninstaller, with:

```bat
npm run dist:win
```

The installer is generated at `dist/installer/MTK-Browser-Setup-0.3.0-Preview.exe`. `npm start` launches the native application; the Electron prototype is available only through `npm run start:legacy`.

## Preview security status
The current native preview uses `no_sandbox=true`. CEF 150 Windows sandbox support requires a bootstrap executable plus application-DLL packaging refactor and remains a release blocker. Builds are unsigned unless an operator supplies and verifies an Authenticode certificate through the release signing environment described in `RELEASE_NOTES_0.3.0.md`.

No browser can guarantee detection of every illegal or newly published page using a static local list. For deployment to children, schools, or organizations, combine MTK Browser with managed DNS filtering, operating-system parental controls, administrator-locked settings, and a continuously updated categorization service.
