# Extension support feasibility

MTK Browser currently ships the **CEF 150 minimal** Windows distribution. Its
`include/` tree has no extension-management API header or `LoadExtension` /
`GetExtensions` request-context surface. The only permitted extension origin
in the browser code is Chromium's bundled PDF viewer:

`chrome-extension://mhjfbmdgcfjbbpaeojofohoefgiehjai/`

That narrow allowance must stay narrow. Treating arbitrary `chrome-extension:`
URLs as trusted would let unreviewed code gain browser-level privileges.

## Required foundation before implementation

1. Replace the minimal CEF package with an upstream distribution that exposes
   the documented extension-management API and has a compatible Chromium
   extension runtime.
2. Build a throwaway-profile proof that installs a harmless unpacked test
   extension, records its exact ID and verifies it cannot bypass MTK navigation,
   download, parental-control or Shields policies.
3. Only then add user-facing install, per-extension permissions, enable/disable,
   update/remove and family-mode restriction flows. Extensions must be disabled
   by default for child profiles until that policy layer is verified.
4. Keep extension packages and update metadata signed; do not load arbitrary
   CRX/ZIP files or a web-store URL as a substitute for signature validation.

Until step 1 is completed, MTK Browser intentionally offers **no third-party
extension installation**. This is a capability constraint of the selected CEF
foundation, not a missing menu item.
