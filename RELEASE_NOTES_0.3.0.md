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
- PDFs render inside the current MTK Browser tab through CEF's bundled viewer.
  Only the exact built-in viewer origin is allowed; its Download action still
  goes through MTK Browser's existing safe/standard download policy.
- YouTube Restricted Mode is enforced at the network layer with Google's
  documented `YouTube-Restrict: Strict` header on YouTube and InnerTube API
  hosts. Filtering decisions are made by YouTube, so this reduces but does not
  guarantee removal of all mature content.
- Empty ad-slot collapsing no longer walks into media players or widget roles,
  so the YouTube seek bar, chapters and player controls stay intact.
- Instagram discovery lockdown: the Explore browse grid and the people/location
  browsers are redirected to Instagram's own keyword search page, the Reels feed
  is blocked, and adult hashtags and search terms are refused. Search stays
  available — on Instagram web the Search button and Explore share one link, so
  it is redirected rather than hidden. Feed, profiles, stories and messages keep
  working.
- Instagram sponsored posts are removed from the feed. They arrive in the same
  GraphQL payload as organic posts and carry no "Sponsored" text in the DOM, so
  they are identified by Meta's ad click-through link, which is also blocked.
- The block page now leads with a rendered 3D shield animation — a swaying
  shield, a pulsing lock and expanding rings on a 3-second seamless loop. It is
  a 202 KB transparent VP9 asset that ships beside the executable and is inlined
  into the page, so the block page stays fully offline and self-contained. The
  animation is silent and decorative; if the asset is missing, or the reader has
  asked the platform to reduce motion, the static shield glyph is shown instead.

- Finished downloads now carry the Windows mark of the web (`Zone.Identifier`,
  internet zone, plus the source address). Windows, Defender/SmartScreen and
  Office therefore treat saved files as untrusted: executables warn before they
  run and Office documents open in Protected View. MTK Browser writes the
  marker; it does not run an antivirus scan itself.
- Optional **parent PIN**. Once set, Settings — including the download mode and
  the home page — cannot be opened without it. The PIN is stored only as a
  PBKDF2-HMAC-SHA256 verifier (200,000 iterations, 16-byte random salt) through
  Windows CNG, never as text, and repeated wrong attempts are slowed down.

- **Clear browsing data** in the overflow menu: browsing history, download
  history, cookies and site data, cached files, the saved session, and optionally
  bookmarks. Each item is reported individually after it runs, finished files on
  disk are kept, and the action is behind the parent PIN when one is set.
- **Omnibox autocomplete** from bookmarks and history. It appears from the second
  character, lists bookmarks first, is limited to eight rows, and every candidate
  is re-checked against the safety policy before it is offered or opened.

- Full screen with `F11` (`F11` or `Esc` to leave), also in the overflow menu.
  The chrome is hidden rather than rebuilt, so tabs, downloads and find state
  survive, and leaving restores the exact previous size and position.
- **Reopen closed tab** with `Ctrl+Shift+T` or from the overflow menu. Up to ten
  addresses are remembered per session; blocked and internal pages are never
  remembered, and each address is re-checked against the safety policy before it
  reopens.
- The tab strip now scrolls horizontally once tabs reach their minimum readable
  width. Use the mouse wheel over the strip; the active tab is always scrolled
  into view.

- **Signed update checks.** MTK Browser looks for a signed update manifest over
  HTTPS shortly after startup and every 12 hours, and on demand from the
  overflow menu. Trust comes from the signature, not from the host:
  - The manifest carries a detached ECDSA P-256 signature, verified with a
    public key compiled into the browser through Windows CNG. An unsigned or
    altered manifest is refused **before anything is downloaded**.
  - Only `https` is accepted, certificate errors are never bypassed, and an
    `https`-to-`http` redirect is rejected.
  - The downloaded installer must match the signed SHA-256 and declared size, or
    it is deleted.
  - A version that is not strictly newer is ignored, which blocks downgrades.
  - The user is always asked before an update is installed. Update checks can be
    switched off in Settings, which sits behind the parent PIN, and turning them
    off requires an acknowledgement.
  - Verified end to end against the real update endpoint: an up-to-date manifest
    reports no update, a newer signed manifest downloads and verifies the
    installer and then prompts, and a manifest tampered to advertise a newer
    version is refused with nothing downloaded.

## Security status
- MTK Shields, SafeSearch, local content policy, private-network denial,
  permission denial, filename sanitisation, and download-origin validation stay
  enabled in both download modes.
- **The Chromium sandbox is now enabled.** MTK Browser ships CEF 150's bootstrap
  packaging: `MTKBrowser.exe` is CEF's bootstrap and `MTKBrowser.dll` is the
  application. Verified on this build by reading process tokens: renderer
  processes run at **untrusted** integrity, the GPU process at low integrity,
  and no process carries `--no-sandbox`. The browser process stays at medium
  integrity because it is the sandbox broker, which is by design.
  - Icon, version details and the DPI/long-path manifest are stamped onto the
    bootstrap executable at build time, so Explorer, the taskbar and shortcuts
    still show MTK Browser.
  - The bootstrap requires the executable and the DLL to be either both unsigned
    or both signed with the same certificate. Staging fails the build if those
    states differ.
- A real-time managed category service is not bundled yet. It requires an
  operated update endpoint, signed feed format/key lifecycle, rollback and
  expiry policy. Local categorisation and optional managed DNS remain active.
- Builds are unsigned unless a release operator provides a certificate through
  `MTK_SIGN_CERT_SHA1` and `signtool.exe` (or sets `MTK_SIGNTOOL`). Set
  `MTK_REQUIRE_SIGNING=1` to make an unsigned release fail.

## Known limitations
- Instagram exposes no network-enforceable safe mode. MTK Browser can close the
  discovery surfaces and refuse adult terms, but it cannot classify the media
  inside an individual post, story or direct message. Meta's own Sensitive
  Content Control / Teen Account settings still have to be set on the account.
- Pause/Resume works only while the current MTK Browser process remains open.
  CEF does not expose cross-restart transfer resumption through this handler.
- URL session restore does not restore per-tab back/forward stacks or login
  session cookies.
- Download history persists status, byte counts, filename, path, URL and risk
  flags. Transient detail text is not stored; the interrupted-on-restart detail
  is reconstructed while loading.
- Native dialog/shortcut behaviour should receive a final manual desktop pass
  before public release.
