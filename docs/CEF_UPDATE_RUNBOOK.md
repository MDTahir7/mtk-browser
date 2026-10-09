# CEF / Chromium security-update runbook

Use this procedure for every CEF update and immediately for a Chromium security
advisory affecting the pinned version. A successful compile alone is not an
update. Do not publish an installer until every required gate below has evidence.

## 1. Triage

1. Record the advisory/CVE, severity, affected Chromium range and the fixed
   Chromium version in the release issue.
2. If the installed version is affected by a critical or exploited issue, stop
   normal release work and target the first compatible fixed CEF stable build.
3. Confirm that the CEF Windows 64-bit minimal package is available from the
   official CEF build distribution. Treat mirrors and copied archives as
   untrusted.

## 2. Pin the exact SDK

1. Download the archive to a temporary directory; do not unpack over the old
   `vendor/cef-sdk` directory.
2. Check its published SHA-1 against the official CEF build index, then unpack
   it to a new directory.
3. Update `vendor/CEF_PIN.txt` with the CEF version, Chromium version, package,
   published checksum, source URL, selection date and channel.
4. Configure a fresh sandbox-on tree. Example:

   ```powershell
   cmake -S . -B build/native-cef-verify -DCEF_ROOT="E:/MTK Browser/vendor/cef-sdk" -A x64
   cmake --build build/native-cef-verify --config Release --target mtk_browser
   ```

   `MTK_ENABLE_CEF_SANDBOX` must remain `ON`. An unsandboxed debug build is not
   release evidence.

## 3. Required automated gates

Run these against the candidate source and keep their output with the release:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/run_tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build_installer.ps1
signtool verify /pa /all "dist/MTK Browser/MTKBrowser.exe"
signtool verify /pa /all "dist/MTK Browser/MTKBrowser.dll"
signtool verify /pa /all "dist/installer/MTK-Browser-Setup-<version>-Preview.exe"
```

For a public release, set `MTK_REQUIRE_SIGNING=1`; unsigned output is only a
preview and must not be represented as a trusted public installer.

## 4. Manual runtime gates

Use a throwaway profile. Check all of these on the built, staged payload:

- Browser launches as `MTKBrowser.exe` plus `MTKBrowser.dll`; no process has
  `--no-sandbox`.
- Renderer tokens are Untrusted or otherwise match the current CEF sandbox
  baseline; browser broker remains Medium by design.
- `https://` loads with a valid certificate; an invalid-certificate test page
  is refused and offers no bypass.
- `mtk://home/`, `mtk://history/` and the PDF viewer still work; arbitrary
  `mtk://` paths do not.
- Create tabs, bookmarks, a saved password, a download and a private window;
  restart after a normal exit and after a forced-close recovery exercise.
- Test login, upload and media on the approved compatibility matrix before
  promoting the update.

## 5. Update and rollback decision

1. Publish only a signed installer and a manifest created by
   `scripts/publish_update.ps1`.
2. Test the manifest/signature/download path from the previous signed build on
   a separate machine or clean VM. A tampered manifest and a mismatched SHA-256
   must be refused.
3. Keep the prior signed installer and its manifest available until the new
   release completes the compatibility window. If a severe regression appears,
   publish a newer signed recovery build; the updater deliberately refuses
   version downgrades.
4. Record the CEF pin, test evidence, signing certificate subject/thumbprint,
   manifest hash and rollback/recovery build in the release notes.
