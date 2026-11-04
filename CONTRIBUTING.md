# Contributing to MTK Browser

Thank you for your interest in contributing to MTK Browser! We are building a privacy-first, family-safe Windows web browser using native C++20, Win32 APIs, and the Chromium Embedded Framework (CEF).

Because safety and security are foundational to this project, we maintain rigorous engineering, testing, and licensing standards.

---

## Code of Conduct

All contributors, maintainers, and community members are expected to uphold our [Code of Conduct](CODE_OF_CONDUCT.md). Please treat everyone with respect and constructive collaboration.

---

## Principles for Contribution

1. **Security & Family Safety First**: No feature, performance enhancement, or convenience shortcut may weaken content filtering, SafeSearch enforcement, download safeguards, or sandbox isolation.
2. **Deterministic & Fail-Closed**: All URL parsers, domain categorizers, and file sanitizers must fail safely (rejecting or blocking ambiguous or malformed input).
3. **Native Architecture**: The target product is the native C++20 / Win32 / CEF codebase under `native/`. The legacy Electron files in `src/` exist only as a historical reference and should not be expanded.
4. **Verified with Tests**: Any modification to URL routing, download handling, policy evaluation, session restore, password encryption, or UI strings must include corresponding automated test cases.
5. **Clean Licensing**: Never introduce code, libraries, 3D assets, fonts, or filter lists with incompatible or unclear licenses (especially GPL/AGPL copyleft dependencies or unverified commercial assets).

---

## Development Environment & Prerequisites

To develop and test MTK Browser on Windows:

| Dependency | Minimum Version | Notes |
| :--- | :--- | :--- |
| **OS** | Windows 10 (Build 17763+) or 11 | x64 architecture required |
| **Compiler** | Visual Studio 2022 (MSVC v143) | "Desktop development with C++" workload with Windows SDK |
| **Build System** | CMake 3.21+ | Required for full native builds |
| **Node.js** | Node.js 20+ | Used to run password form autofill tests and script tooling |
| **Python** | Python 3.10+ | Used for resource scripts and asset validation |
| **CEF SDK** | Version pinned in `vendor/CEF_PIN.txt` | Required only for full browser builds, not for logic tests |
| **Inno Setup** | Inno Setup 6.x | Optional; required only for packaging installer executables |

---

## Running the Automated Test Suite

The fast logic test suite covers safety-critical modules (navigation policy, download rules, filename hardening, session persistence, password store, site settings, and string translations). **It does not require the CEF binary distribution and runs on any standard Windows machine with MSVC installed.**

Run the full logic test suite from PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/run_tests.ps1
```

This compiles and runs:
- The 260+ C++ native test cases via `build/tests/mtk_tests.exe`.
- The Node.js DOM-fixture password form capture and autofill tests via `tests/password_form_tests.cjs`.

To run Node.js syntax and form tests independently:

```powershell
npm run check
node --test tests/password_form_tests.cjs
```

---

## Building the Full Native Browser (with CEF)

When working on browser chrome, renderer bindings, or UI integration:

1. **Download and Extract CEF**:
   Download the exact CEF Windows 64-bit minimal binary distribution matching the version in `vendor/CEF_PIN.txt`. Extract it into `vendor/cef-sdk/`.

2. **Configure with CMake**:
   ```powershell
   cmake -S . -B build/native-sandbox -DCEF_ROOT="vendor/cef-sdk" -A x64
   ```

3. **Compile the Native Executable and DLL**:
   ```powershell
   cmake --build build/native-sandbox --config Release --target mtk_browser
   ```

4. **Stage the Runtime**:
   Stage the runtime dependencies (CEF binaries, filter rules, rendered animation assets) into `dist/MTK Browser`:
   ```powershell
   powershell -ExecutionPolicy Bypass -File scripts/stage_release.ps1
   ```

5. **Launch the Staged Application**:
   ```powershell
   & "dist\MTK Browser\MTKBrowser.exe"
   ```

---

## Coding Standards

### C++20 & Win32 Guidelines
- Use modern C++20 idioms: RAII for all system handles, `std::string_view`, `std::wstring_view`, and `std::span` where appropriate.
- Prefer Windows Unicode APIs (`CreateWindowExW`, `GetModuleFileNameW`, etc.).
- Source files are encoded in **UTF-8 without BOM**. All build targets must compile with `/utf-8` to prevent mojibake in multilingual UI strings (such as Hindi).
- Never access real user profile data in test suites: always use `profile::SetRootForTesting` to point file operations to an isolated temporary directory.
- Avoid raw heap allocations (`new`/`delete`); use `std::make_unique` or standard containers.

### Third-Party Assets & Media
- Never commit raw stock 3D models (`.blend`), commercial textures, or proprietary font binaries to the repository.
- Any rendered icon, hover strip, or animation must satisfy the license constraints in `THIRD_PARTY_NOTICES.md`.

---

## Pull Request Process

1. **Create an Issue**: Before submitting major architectural or UI changes, open a GitHub issue to discuss the proposal with maintainers.
2. **Branch Naming**: Use descriptive branch names:
   - `fix/issue-description`
   - `feat/feature-description`
   - `docs/documentation-update`
3. **Run All Verification**: Ensure `scripts/run_tests.ps1` and `npm run check` succeed with 0 failures before opening a pull request.
4. **Document Changes**: Update `CHANGELOG.md` and relevant design documentation under `docs/` if modifying user-facing features or security behavior.
5. **Review**: Maintainers will review code for memory safety, fail-safe correctness, performance impact, and adherence to project goals.
