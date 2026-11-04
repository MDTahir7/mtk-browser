# Security Policy

MTK Browser is designed from the ground up as a privacy-first, family-safe Windows browser. Because security, content filtering, and user safety are foundational project objectives, we treat vulnerability reports with high priority and urgency.

---

## Supported Versions

Only the latest active development preview branch and the most recent release receive security updates.

| Version | Supported | Status |
| :--- | :---: | :--- |
| 0.3.x Preview | Yes | Current development baseline |
| < 0.3.0 | No | Legacy prototype (unsupported) |

---

## Security Architecture & Defense in Depth

MTK Browser enforces layered security across its architecture:

1. **Chromium Sandbox Isolation**:
   The native browser executable (`MTKBrowser.exe`) acts as the Chromium sandbox broker. Rendering and utility processes execute at **Untrusted** integrity, and the GPU process runs at **Low** integrity. No process is permitted to carry `--no-sandbox`.

2. **Network & Navigation Controls**:
   - Navigation to unsupported schemes (`javascript:`, `file:` via address bar, custom protocols) is strictly refused.
   - Private and local network addresses (`127.0.0.0/8`, `10.0.0.0/8`, `172.16.0.0/12`, `192.168.0.0/16`, `169.254.0.0/16`, `::1`, `fc00::/7`, `fe80::/10`) are blocked from web navigation.
   - Adult and known harmful domains are blocked by deterministic policy and local filter engines before network requests are dispatched.
   - Strict SafeSearch headers and query parameters are enforced on Google, Bing, DuckDuckGo, and YouTube.

3. **Download Safety & Filename Hardening**:
   - Directory traversal (`..`), reserved Windows device names (`CON`, `PRN`, `AUX`, `NUL`, `COM1-9`, `LPT1-9`), and control characters are stripped from downloaded file names.
   - Blocked origins and non-web schemes cannot initiate downloads.
   - Shipped Safe Download mode disallows executable scripts (`.exe`, `.bat`, `.cmd`, `.ps1`, `.vbs`, `.js`, `.scr`, `.msi`) and double-extension masquerades (`.pdf.exe`).
   - Downloaded files are stamped with the Windows Mark of the Web (`Zone.Identifier`, Internet Zone 3) so Windows Defender and SmartScreen inspect untrusted files.

4. **Credential & Preference Protection**:
   - Saved passwords and autofill profile data are encrypted with Windows Data Protection API (DPAPI) bound to the current Windows user and profile ID. Plaintext credentials never touch disk.
   - The Parent PIN is protected via PBKDF2-HMAC-SHA256 (200,000 iterations, 16-byte cryptographically random salt generated via Windows CNG BCrypt) with progressive delay throttling on incorrect attempts.

5. **Cryptographic Update Verification**:
   - Auto-update manifests are signed with detached ECDSA P-256 signatures verified via Windows Cryptography API: Next Generation (CNG) against a public key compiled into the executable.
   - Tampered, downgraded, or unsigned manifests and payloads are rejected before download or execution.

---

## Reporting a Security Vulnerability

**Please do not report security vulnerabilities through public GitHub issues, discussions, or pull requests.**

### Reporting Procedure

1. **Email the Maintainer**: Send an encrypted or plain-text advisory directly to `tomboy41@gmail.com` with the subject tag `[SECURITY] MTK Browser Vulnerability Report`.
2. **Include Technical Details**:
   - A clear description of the vulnerability and its potential impact.
   - Step-by-step reproduction instructions or a minimal Proof of Concept (PoC).
   - Component affected (e.g., `policy.cpp`, `download_manager.cpp`, `updater.cpp`, `password_store.cpp`).
   - The exact build commit hash, operating system version, and environment details.
3. **Acknowledgment & Triage**:
   - We aim to acknowledge receipt of security reports within **48 hours**.
   - An initial assessment and severity rating will be communicated within **5 business days**.
   - We ask for coordinated disclosure: please allow up to 30 days for a fix to be engineered and verified before public disclosure.

---

## Scope & Vulnerability Assessment

### In Scope
- Bypasses of content filtering, adult-domain blocks, or SafeSearch enforcement.
- Sandbox escapes or bypasses that execute arbitrary code with elevated privilege.
- Directory traversal or filename sanitization escapes allowing file writes outside the downloads folder.
- Local credential or DPAPI extraction bypasses across differing Windows user accounts or profiles.
- Weaknesses in the Parent PIN verification, salt handling, or rate limiting.
- Cryptographic flaws in ECDSA P-256 update manifest verification.
- Memory corruption (buffer overflow, use-after-free, double-free) in native C++ parsing modules.

### Out of Scope
- Attacks requiring physical access to an unlocked, already-compromised Windows machine with administrator privileges.
- Social engineering attacks against the end-user.
- Known upstream Chromium issues that have already been publicly disclosed and are awaiting an upstream CEF/Chromium release.
- Unavoidable limitations documented in `RELEASE_NOTES_0.3.0.md` (e.g., inability to inspect encrypted in-app private messages on third-party social media web interfaces).
