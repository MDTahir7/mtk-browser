# MTK Browser 0.3.0 Preview

## New
- Optional **Continue where you left off** startup mode. Up to 20 validated
  `http`/`https` tabs and the active tab are saved atomically; blocked and
  internal pages are never restored.
- Curated context menu with Back, Forward, Reload, safe Open link in new tab,
  Copy link/text, Paste, Select all, and Save image through the download policy.
  Print, View Source, DevTools, renderer-defined commands, and external protocol
  actions remain unavailable.
- Persistent download history under `%LOCALAPPDATA%\MTK Browser\downloads.v1`.
- Live-session Pause/Resume/Cancel controls and an active-download toolbar badge.
- Safe and Standard download modes. Safe mode warns, but does not block, after
  2 GB. Existing files are never overwritten.
- Optional Authenticode signing hooks for release automation.

## Security status
- MTK Shields, SafeSearch, local content policy, private-network denial,
  permission denial, filename sanitisation, and download-origin validation stay
  enabled in both download modes.
- CEF Windows sandbox is **not yet enabled**. CEF 150 requires a bootstrap EXE
  plus application-DLL packaging refactor; this preview still uses
  `no_sandbox=true`.
- A real-time managed category service is not bundled yet. It requires an
  operated update endpoint, signed feed format/key lifecycle, rollback and
  expiry policy. Local categorisation and optional managed DNS remain active.
- Builds are unsigned unless a release operator provides a certificate through
  `MTK_SIGN_CERT_SHA1` and `signtool.exe` (or sets `MTK_SIGNTOOL`). Set
  `MTK_REQUIRE_SIGNING=1` to make an unsigned release fail.

## Known limitations
- Pause/Resume works only while the current MTK Browser process remains open.
  CEF does not expose cross-restart transfer resumption through this handler.
- URL session restore does not restore per-tab back/forward stacks or login
  session cookies.
- Download history persists status, byte counts, filename, path, URL and risk
  flags. Transient detail text is not stored; the interrupted-on-restart detail
  is reconstructed while loading.
- Native dialog/shortcut behaviour should receive a final manual desktop pass
  before public release.
