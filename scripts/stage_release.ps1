# Stages the built native MTK Browser (CEF runtime included) into
# dist\MTK Browser so there is one stable folder to launch and share.
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\stage_release.ps1

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$source   = Join-Path $repoRoot 'build\native-sandbox\native\Release'
$target   = Join-Path $repoRoot 'dist\MTK Browser'

if (-not (Test-Path (Join-Path $source 'MTKBrowser.exe'))) {
    throw "Build output not found. Run: cmake --build build/native-sandbox --config Release --target mtk_browser"
}

# Sandboxed packaging: MTKBrowser.exe is CEF's bootstrap and it loads
# MTKBrowser.dll. Shipping one without the other produces an app that cannot
# start, so both are required here.
if (-not (Test-Path (Join-Path $source 'MTKBrowser.dll'))) {
    throw 'MTKBrowser.dll is missing. The sandboxed build produces the application as a DLL.'
}

$targetPrefix = [System.IO.Path]::GetFullPath($target).TrimEnd('\', '/') +
    [System.IO.Path]::DirectorySeparatorChar
$running = Get-CimInstance Win32_Process -Filter "Name='MTKBrowser.exe'" |
    Where-Object {
        $_.ExecutablePath -and
        [System.IO.Path]::GetFullPath($_.ExecutablePath).StartsWith(
            $targetPrefix, [System.StringComparison]::OrdinalIgnoreCase)
    }
if ($running) {
    throw 'MTK Browser is running from the staged folder. Close it first.'
}

if (Test-Path $target) { Remove-Item -Recurse -Force $target }
New-Item -ItemType Directory -Path $target | Out-Null

Copy-Item -Path (Join-Path $source '*') -Destination $target -Recurse -Force

# Copy-Item does not preserve the LPAC read grant on the build output. CEF's
# sandboxed child processes need this grant to read the staged runtime, so
# reproduce the grant recursively after copying the files.
& icacls $target /grant '*S-1-15-2-2:(OI)(CI)(RX)' /T /C | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw 'Failed to grant the CEF sandbox read access to staged runtime files.'
}

# Local debug/build artifacts must never ship inside the installer payload.
# The DLL build also produces an import library, which only linkers need.
Get-ChildItem -Path $target -Recurse -File -Include 'debug.log', '*.pdb', '*.ilk', '*.exp', '*.lib', '*.map' |
    Remove-Item -Force

if (-not (Test-Path (Join-Path $target 'mtk_filters.txt'))) {
    throw 'MTK Shields filter list is missing from the build output.'
}

# The block page and the Shields panel inline these assets. A missing file only
# degrades to a static glyph, but shipping it silently would be a regression, so
# fail loudly here. The 700 KB ceiling mirrors kMaxInlineWebmBytes in
# native/src/inline_asset.h; a larger file is refused at runtime.
$inlineAnimations = @{
    'mtk_block_shield.webm'        = 'Block page animation'
    'mtk_siren_alert.webm'         = 'Shields panel beacon'
    'mtk_search_3d.webp'           = '3D Search icon animation'
    'mtk_search_3d_still.webp'     = '3D Search icon still'
    'mtk_home_3d.webp'             = '3D Home icon animation'
    'mtk_home_3d_still.webp'       = '3D Home icon still'
    'mtk_store_3d.webp'            = '3D Store icon animation'
    'mtk_store_3d_still.webp'      = '3D Store icon still'
    'mtk_notes_3d.webp'            = '3D Notes icon animation'
    'mtk_notes_3d_still.webp'      = '3D Notes icon still'
    'mtk_download_3d.webp'         = '3D Download icon animation'
    'mtk_download_3d_still.webp'   = '3D Download icon still'
    'mtk_history_3d.webp'          = '3D History icon animation'
    'mtk_history_3d_still.webp'    = '3D History icon still'
    'mtk_settings_3d.webp'         = '3D Settings icon animation'
    'mtk_settings_3d_still.webp'   = '3D Settings icon still'
    'mtk_bookmarks_3d.webp'        = '3D Bookmarks icon animation'
    'mtk_bookmarks_3d_still.webp'  = '3D Bookmarks icon still'
    'mtk_screenshot_3d.webp'       = '3D Screenshot icon animation'
    'mtk_screenshot_3d_still.webp' = '3D Screenshot icon still'
    'mtk_search_web_3d.webp'       = '3D Search Web icon animation'
    'mtk_search_web_3d_still.webp' = '3D Search Web icon still'
    'mtk_ask_ai_3d.webp'           = '3D Ask AI icon animation'
    'mtk_ask_ai_3d_still.webp'     = '3D Ask AI icon still'
    'mtk_publishing_3d.webp'       = '3D MTK Publishing emblem animation'
    'mtk_publishing_3d_still.webp' = '3D MTK Publishing emblem still'
}
foreach ($assetName in $inlineAnimations.Keys) {
    $label = $inlineAnimations[$assetName]
    $animation = Join-Path $target $assetName
    if (-not (Test-Path $animation)) {
        throw "$label ($assetName) is missing from the build output."
    }
    $animationBytes = (Get-Item $animation).Length
    if ($animationBytes -le 0 -or $animationBytes -gt 700KB) {
        throw "$label is $animationBytes bytes; the browser only inlines assets up to 700 KB."
    }
}

# The toolbar shield, home, store, notes, download, history, and settings buttons play rendered frame strips. Their frames
# must stay square and within the loader's limits in native/src/icon_strip.cpp,
# or the button silently falls back to the flat glyph.
Add-Type -AssemblyName System.Drawing
foreach ($stripName in @('mtk_shield_hover.png', 'mtk_home_hover.png', 'mtk_refresh_strip.png', 'mtk_store_hover.png', 'mtk_notes_hover.png', 'mtk_download_hover.png', 'mtk_history_hover.png', 'mtk_settings_hover.png', 'mtk_focus_strip.png', 'mtk_screenshot_hover.png', 'mtk_screenshot_strip.png')) {
    $iconStrip = Join-Path $target $stripName
    if (-not (Test-Path $iconStrip)) {
        throw "Icon hover strip ($stripName) is missing from the build output."
    }
    $stripImage = [System.Drawing.Image]::FromFile($iconStrip)
    try {
        $stripWidth = $stripImage.Width
        $stripHeight = $stripImage.Height
    } finally {
        $stripImage.Dispose()
    }
    if ($stripHeight -le 0 -or $stripHeight -gt 256 -or $stripWidth -gt 4096 -or
        ($stripWidth % $stripHeight) -ne 0) {
        throw "Hover strip ($stripName) is ${stripWidth}x${stripHeight}; frames must be square, at most 256 px, and the strip at most 4096 px wide."
    }
}

# CEF's bootstrap refuses to run under its original name, so an unrenamed copy
# would only ever be a confusing dead file in the install directory.
Get-ChildItem -Path $target -File -Include 'bootstrap.exe', 'bootstrapc.exe' |
    Remove-Item -Force

# The bootstrap requires the EXE and DLL to be either both unsigned or both
# signed with the same certificate, so a half-signed payload is rejected here
# rather than at the user's first launch.
$exeSignature = (Get-AuthenticodeSignature (Join-Path $target 'MTKBrowser.exe')).Status
$dllSignature = (Get-AuthenticodeSignature (Join-Path $target 'MTKBrowser.dll')).Status
if ($exeSignature -ne $dllSignature) {
    throw "MTKBrowser.exe ($exeSignature) and MTKBrowser.dll ($dllSignature) signing states differ."
}

$legal = Join-Path $target 'Legal'
New-Item -ItemType Directory -Path $legal | Out-Null
Copy-Item (Join-Path $repoRoot 'native\PREVIEW_NOTICE.txt') $target
Copy-Item (Join-Path $repoRoot 'LICENSES.md') $legal
Copy-Item (Join-Path $repoRoot 'THIRD_PARTY_NOTICES.md') $legal
Copy-Item (Join-Path $repoRoot 'UPSTREAM.md') $legal
Copy-Item (Join-Path $repoRoot 'SOURCE_OFFER_REQUIREMENTS.md') $legal
Copy-Item (Join-Path $repoRoot 'RELEASE_NOTES_0.3.0.md') $target
Copy-Item (Join-Path $repoRoot 'vendor\cef-sdk\LICENSE.txt') (Join-Path $legal 'CEF_LICENSE.txt')
Copy-Item (Join-Path $repoRoot 'vendor\cef-sdk\CREDITS.html') (Join-Path $legal 'CEF_CREDITS.html')
if (Test-Path (Join-Path $repoRoot 'native\resources\mtk_browser_logo.png')) {
    Copy-Item (Join-Path $repoRoot 'native\resources\mtk_browser_logo.png') $target
}
if (Test-Path (Join-Path $repoRoot 'native\resources\mtk_browser.ico')) {
    Copy-Item (Join-Path $repoRoot 'native\resources\mtk_browser.ico') $target
}

$exe = Join-Path $target 'MTKBrowser.exe'
Write-Output "Staged: $exe"
Write-Output ('Files:  ' + (Get-ChildItem $target -Recurse -File).Count)
