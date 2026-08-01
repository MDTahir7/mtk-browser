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

$running = Get-CimInstance Win32_Process -Filter "Name='MTKBrowser.exe'" |
    Where-Object { $_.ExecutablePath -like "$target*" }
if ($running) {
    throw 'MTK Browser is running from the staged folder. Close it first.'
}

if (Test-Path $target) { Remove-Item -Recurse -Force $target }
New-Item -ItemType Directory -Path $target | Out-Null

Copy-Item -Path (Join-Path $source '*') -Destination $target -Recurse -Force

# Local debug/build artifacts must never ship inside the installer payload.
# The DLL build also produces an import library, which only linkers need.
Get-ChildItem -Path $target -Recurse -File -Include 'debug.log', '*.pdb', '*.ilk', '*.exp', '*.lib' |
    Remove-Item -Force

if (-not (Test-Path (Join-Path $target 'mtk_filters.txt'))) {
    throw 'MTK Shields filter list is missing from the build output.'
}

# The block page and the Shields panel inline these assets. A missing file only
# degrades to a static glyph, but shipping it silently would be a regression, so
# fail loudly here. The 700 KB ceiling mirrors kMaxInlineWebmBytes in
# native/src/inline_asset.h; a larger file is refused at runtime.
$inlineAnimations = @{
    'mtk_block_shield.webm' = 'Block page animation'
    'mtk_siren_alert.webm'  = 'Shields panel beacon'
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

# The toolbar shield plays a rendered frame strip. Its frames must stay square
# and within the loader's limits in native/src/icon_strip.cpp, or the button
# silently falls back to the flat glyph.
$iconStrip = Join-Path $target 'mtk_shield_hover.png'
if (-not (Test-Path $iconStrip)) {
    throw 'Shield hover strip (mtk_shield_hover.png) is missing from the build output.'
}
Add-Type -AssemblyName System.Drawing
$stripImage = [System.Drawing.Image]::FromFile($iconStrip)
try {
    $stripWidth = $stripImage.Width
    $stripHeight = $stripImage.Height
} finally {
    $stripImage.Dispose()
}
if ($stripHeight -le 0 -or $stripHeight -gt 256 -or $stripWidth -gt 4096 -or
    ($stripWidth % $stripHeight) -ne 0) {
    throw "Shield hover strip is ${stripWidth}x${stripHeight}; frames must be square, at most 256 px, and the strip at most 4096 px wide."
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

$exe = Join-Path $target 'MTKBrowser.exe'
Write-Output "Staged: $exe"
Write-Output ('Files:  ' + (Get-ChildItem $target -Recurse -File).Count)
