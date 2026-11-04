# Agent Guidelines & Maintainer Runbook (AGENTS.md)

This document provides technical instructions, architectural invariants, and operational workflows for AI coding agents and human maintainers working on **MTK Browser**.

---

## 1. Project Overview & Architecture

MTK Browser is a privacy-first, family-safe Windows browser built with **C++20**, **Win32**, and **Chromium Embedded Framework (CEF 150)**.

### Process & Packaging Model
- **Sandboxed Bootstrap Architecture**: The release browser utilizes CEF 150's Windows sandbox model.
  - `MTKBrowser.exe`: CEF bootstrap executable acting as the sandbox broker process (runs at Medium integrity).
  - `MTKBrowser.dll`: The core MTK Browser application library exporting `RunWinMain`.
  - Renderer and utility child processes execute under **Untrusted** integrity; GPU process executes under **Low** integrity.
  - Neither the EXE nor the DLL alone is a valid payload; both must be deployed together with identical digital signature states.
- **Legacy Electron Prototype**: The `src/` directory holds an early Electron prototype. It is retained solely for legacy behavior reference and must **not** be included in native build packaging or expanded.

---

## 2. Directory Layout

```
├── .github/              # CI workflows, issue templates, PR template
├── assets/               # Project logos, icons, Blender scenes
├── docs/                 # Architectural Decision Records (ADRs) and design specs
├── installer/            # Inno Setup script (MTKBrowser.iss)
├── native/
│   ├── resources/        # Icons, UI hover strips, 3D WebP/WebM loops, filter rules
│   └── src/              # C++20 Win32 browser source code
├── scripts/              # PowerShell and Python build/test automation scripts
├── tests/                # Native C++ logic tests and Node.js form fixture tests
├── vendor/               # CEF SDK pin configuration (CEF_PIN.txt) and notices
├── CMakeLists.txt        # Root CMake build configuration
├── LICENSE               # MIT License
├── package.json          # Node.js toolchain and test scripts
├── README.md             # Project presentation and build guide
├── RELEASE_NOTES_0.3.0.md# 0.3.0 Preview feature release notes
├── SECURITY.md           # Responsible security disclosure policy
└── THIRD_PARTY_NOTICES.md# Attribution, third-party licenses, and artwork constraints
```

---

## 3. Core Architectural Modules (`native/src/`)

| Module | Files | Responsibility |
|---|---|---|
| **Policy Engine** | `policy.cpp`, `policy.h` | SafeSearch rewriting, navigation URL validation, adult domain blocking, private network address denial (`127.0.0.0/8`, `10.0.0.0/8`, `192.168.0.0/16`, `::1`), request header injection (`YouTube-Restrict: Strict`). |
| **Filter Engine** | `filter_engine.cpp`, `filter_engine.h` | In-memory EasyList-compatible domain and URL filtering, thread-safe request matching, hot-reloading from `mtk_filters.txt`. |
| **Download Manager** | `download_manager.cpp`, `download_manager.h` | Filename hardening, path traversal prevention, Windows Mark of the Web (`Zone.Identifier`) application, Safe Download mode enforcement. |
| **Password Store** | `password_store.cpp`, `password_store.h` | DPAPI-encrypted credential persistence (`passwords.v1`) with entropy bound to Windows user SID and profile ID. |
| **Browser Window** | `browser_window*.cpp`, `browser_window.h` | Native Win32 chrome, tab bar, vertical tabs left-rail, omnibox autocomplete, custom titlebar, GDI/DirectWrite rendering. |
| **Updater** | `updater.cpp`, `updater.h` | Detached ECDSA P-256 signature verification via Windows CNG (`bcrypt.dll`), SHA-256 installer hash validation, anti-downgrade controls. |
| **Autofill Store** | `autofill_store.cpp`, `autofill_store.h` | Consent-gated contact and address profile storage encrypted with DPAPI. |
| **Session Store** | `session_store.cpp`, `session_store.h` | Multi-window tab state persistence and crash recovery snapshot management. |

---

## 4. Verification & Testing Commands

Before proposing or merging any changes, execute the following verification commands:

### A. Run Logic Test Suite (MSVC C++20 + Node.js)
```powershell
powershell -ExecutionPolicy Bypass -File scripts/run_tests.ps1
```
- Compiles all C++ unit tests under `tests/` with `/std:c++20 /utf-8 /EHsc /W3 /permissive-`.
- Runs `mtk_tests.exe` (260+ automated test cases).
- Runs DOM-fixture password form tests (`tests/password_form_tests.cjs`).
- **Pass requirement**: 0 failed cases.

### B. Validate JavaScript & Node.js Syntax
```powershell
npm run check
```
- Validates syntax of `src/main.js`, `src/preload.js`, `src/security.js`, and `src/ui/app.js`.

### C. Validate Python Build & Utility Scripts
```powershell
python -m py_compile scripts\*.py *.py
```
- Ensures all automation scripts compile cleanly.

### D. Verify Resource Assets
```powershell
Add-Type -AssemblyName System.Drawing
foreach ($name in 'mtk_block_shield.webm', 'mtk_siren_alert.webm') {
  $file = Get-Item "native/resources/$name"
  if ($file.Length -le 0 -or $file.Length -gt 700KB) { throw "$name exceeds 700 KB inline limit." }
}
```

---

## 5. Non-Negotiable Security Invariants

When implementing fixes or enhancements, agents and maintainers must strictly enforce the following rules:

1. **Never Weaken Content Filtering**: Do not bypass or weaken domain blocklists, keyword categorization, or SafeSearch enforcement for convenience or feature requests.
2. **Never Disable the Sandbox**: Never introduce `--no-sandbox` or disable LPAC ACL configuration in CMake or runtime commands.
3. **Fail-Closed Input Parsing**: URL parsing, domain extraction, and parameter sanitation must reject ambiguous or malformed structures.
4. **No Plaintext Credential Storage**: Passwords, tokens, and PIN hashes must never be written to disk in unencrypted form. Always use Windows DPAPI (`CryptProtectData`) or PBKDF2-HMAC-SHA256 via CNG.
5. **No Direct Real-Profile Modification in Tests**: Unit tests must use `profile::SetRootForTesting` to operate in an isolated throwaway directory. Tests must never modify `%LOCALAPPDATA%\MTK Browser\`.
6. **UTF-8 Source Encoding**: All C++ files must be saved as UTF-8 without BOM. Do not introduce raw ANSI characters; use standard Unicode escape sequences or clean UTF-8 with `/utf-8` compiler flags.

---

## 6. Guidelines for AI Agents Handling Common Workflows

### Pull Request Reviews
- Verify that changes include corresponding unit tests in `tests/`.
- Check that all modified C++ code follows RAII for Win32 handles (`HANDLE`, `HKEY`, `HDC`, `HBITMAP`, `BCRYPT_KEY_HANDLE`).
- Check that no third-party raw 3D assets (`.blend`), proprietary fonts, or unreviewed binaries are committed.
- Verify that `THIRD_PARTY_NOTICES.md` is updated if new external dependencies or assets are introduced.

### Security Reviews & Triage
- Triage issues labeled `bug` or `security` against the threat model in `SECURITY.md`.
- Ensure directory traversal safeguards remain active in all download paths (`download_manager.cpp`).
- Validate that all IPC messages between renderer and browser processes validate origin and boundaries.

### Release Notes & Documentation Updates
- Record user-facing changes in `CHANGELOG.md` following Keep a Changelog conventions.
- Do not fabricate release files, performance metrics, adoption statistics, or user testimonials.
