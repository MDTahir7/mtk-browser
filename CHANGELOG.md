# Changelog

All notable changes to MTK Browser are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [0.3.0-preview] - 2026-10-17

### Added
- **Native C++20 / Win32 Architecture**: Full migration from the legacy Electron prototype to a high-performance native Windows C++20 and Chromium Embedded Framework (CEF) foundation.
- **CEF 150 Windows Sandbox**: Full Chromium sandbox integration using CEF's bootstrap loader (`MTKBrowser.exe` broker and `MTKBrowser.dll` application). Renderers run at Untrusted integrity, GPU runs at Low integrity, with `--no-sandbox` strictly prevented.
- **Cryptographically Verified Updates**: Auto-update manifest verification powered by detached ECDSA P-256 signatures validated via Windows CNG against a hardcoded public key, plus SHA-256 payload integrity checks.
- **Parent PIN Protection**: Cryptographic PIN lock for browser settings, download mode selection, and data clearing using PBKDF2-HMAC-SHA256 (262,144/200,000 iterations via Windows CNG BCrypt) with progressive delay throttling.
- **Network-Level SafeSearch & Platform Lockdown**:
  - Google, Bing, DuckDuckGo SafeSearch query enforcement.
  - YouTube Strict Restricted Mode via `YouTube-Restrict: Strict` header injection.
  - Instagram discovery lockdown (Explore grid redirection, Reels blocking, sponsored post removal).
- **Windows Mark of the Web**: Finished downloads are automatically stamped with NTFS `Zone.Identifier` (Internet Zone 3) and referrer source addresses so Windows Defender and SmartScreen inspect untrusted files.
- **Comprehensive Download Management**:
  - Safe Download Mode (blocks executables, scripts, and double-extension masquerades) and Standard Download Mode.
  - Filename sanitization preventing path traversal (`..`), control characters, and reserved Windows DOS devices (`CON`, `PRN`, `AUX`, `NUL`, `COM1-9`, `LPT1-9`).
  - Active download popup panel with pause, resume, and cancel capabilities.
- **Password Manager & Contact Autofill**:
  - DPAPI-encrypted credential storage (`passwords.v1`) scoped to current Windows user and profile ID.
  - Account picker dialog for domains with multiple saved logins.
  - Explicit user-consent contact and address autofill store (`autofill_store`).
- **Native Browsable Pages (`mtk://`)**:
  - `mtk://history/`: Fast, offline history viewer with local date grouping, search, and atomic deletion.
  - `mtk://downloads/`: Categorized download history with search and disk status checks.
  - `mtk://settings/`: Native tabbed settings management.
  - `mtk://passkeys/`: Passkey diagnostics and WebAuthn status viewer.
- **Fingerprinting Protection**: Stripping of high-entropy User-Agent Client Hints (`Sec-CH-UA-Full-Version-List`, etc.) and seeded PRNG noise injection into Canvas, WebGL, and AudioBuffer readbacks.
- **Productivity & Window Controls**:
  - Vertical tabs collapsible left-rail UI (compact 48px rail and 240px expanded drawer).
  - Web App runner: Policy-approved web apps run in dedicated standalone windows with isolated profiles.
  - Reading list and offline HTML article archive.
  - Full-screen mode (`F11`), closed tab restore (`Ctrl+Shift+T`), and horizontal tab strip overflow scrolling.
- **Bilingual Localization (English + Hindi)**:
  - Runtime text substitution for all native menus, dialogs, tooltips, and accessible descriptions.
  - Fixed MSVC compilation code page with `/utf-8` to guarantee clean Devanagari text rendering without mojibake.
- **Automated Verification Suite**: Expanded offline C++ logic test suite to 269 test cases and 6 Node.js password form tests.

### Changed
- Legacy Electron prototype (`src/`) demoted to behavioral baseline and removed from release packaging.
- Staged release and Inno Setup installer scripts (`scripts/stage_release.ps1`, `scripts/build_installer.ps1`) updated to package the sandboxed bootstrap runtime.

### Security
- Navigation strictly refuses non-web schemes (`javascript:`, `file:`, custom protocols) and private/local network ranges (`127.0.0.0/8`, `10.0.0.0/8`, `192.168.0.0/16`, `::1`).
- Per-site Shields toggles are restricted so user exemptions can never disable core security rules or adult domain blocks.

---

## [0.2.0] - 2026-06-15

### Added
- Initial C++ Win32 prototype scaffolding under `native/`.
- Offline content filtering engine evaluating domain-level and keyword blocklists.
- Basic bookmark import from Google Chrome and Microsoft Edge.
- Basic session save and restore mechanism.

---

## [0.1.0] - 2026-04-01

### Added
- Initial proof-of-concept Electron prototype under `src/`.
- Basic SafeSearch query rewriting.
- Local domain blocklist evaluation for child-safe browsing experiments.
