# MTK Browser performance benchmark

This harness measures a **cold launch**, time until a requested page's window
title appears, combined CEF-process working set, and CPU seconds observed at
that point. Each trial uses a fresh, valid MTK profile identifier, so it cannot
accidentally reuse a running browser, history, or cookies.

Run it from the repository root after building the sandboxed Release executable:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\benchmark_browser.ps1 `
  -BrowserPath build\native-professional\native\Release\MTKBrowser.exe `
  -Samples 5
```

The CSV is written to `build\benchmarks\mtk-browser-performance.csv`. A sample
is not silently treated as successful: `startup_timeout` and
`page_ready_timeout` are recorded separately. To measure the built-in home page
instead of an HTTPS page, use `-Url mtk://home/ -ReadyTitlePattern '*MTK Browser
Home*'`.

For a small account-free website compatibility smoke check, run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\compatibility_smoke.ps1 `
  -BrowserPath "C:\Users\<you>\AppData\Local\Programs\MTK Browser\MTKBrowser.exe"
```

It tests Example Domain, Google Search and Wikipedia in separate fresh profiles
and writes `build\benchmarks\compatibility-smoke.csv`. It is deliberately not a
substitute for the authorized account, payment, meeting and media tests in the
manual release matrix.

## Fair comparison protocol

Use the same machine, power mode, network, display scale, URL, and number of
samples for every browser. Close every other browser first. For Chrome or Brave,
use their own fresh profile command-line option and record the exact version;
do not pass MTK's `--profile` argument to another product.

This is deliberately an engineering baseline, not a battery-life claim. Page
readiness is based on the visible browser-window title, not a synthetic web
benchmark or a claim about first paint. Battery, scrolling smoothness, video,
and real account-page compatibility must be measured in a separate manual
matrix before any public performance comparison is published.
