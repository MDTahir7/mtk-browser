## Description

<!-- Describe the changes proposed in this pull request and the rationale behind them. -->

## Type of Change

- [ ] Bug fix (non-breaking change fixing an issue)
- [ ] New feature (non-breaking change adding functionality)
- [ ] Security hardening or policy enforcement
- [ ] Documentation update
- [ ] Refactoring or build/test infrastructure improvement

## Security & Architecture Verification

- [ ] **No Safety Policy Weakening**: This change does not weaken, bypass, or disable content filtering, SafeSearch enforcement, download safeguards, or Chromium sandbox isolation.
- [ ] **Fail-Closed**: Any new URL, input, or file parsing logic fails safely on malformed or unexpected data.
- [ ] **No Secret or Binary Leaks**: No API keys, credentials, proprietary 3D source files (`.blend`), or unreviewed binary packages are included.
- [ ] **Licensing**: All new code is compatible with the MIT License; any external assets or notices comply with `THIRD_PARTY_NOTICES.md`.
- [ ] **UTF-8 Encoding**: All modified or new C++ source files are encoded in UTF-8 without BOM.

## Testing & Validation

- [ ] Executed `powershell -ExecutionPolicy Bypass -File scripts/run_tests.ps1` (All C++ logic tests passed).
- [ ] Executed `node --test tests/password_form_tests.cjs` (Password autofill tests passed).
- [ ] Executed `npm run check` (Syntax verification passed).
- [ ] Added new automated test cases under `tests/` covering the changed behavior.
- [ ] Performed manual UI verification on Windows (if modifying browser chrome, dialogs, or rendering).
