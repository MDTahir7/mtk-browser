# Stages the built native MTK Browser (CEF runtime included) into
# dist\MTK Browser so there is one stable folder to launch and share.
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\stage_release.ps1

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$source   = Join-Path $repoRoot 'build\native-adblock\native\Release'
$target   = Join-Path $repoRoot 'dist\MTK Browser'

if (-not (Test-Path (Join-Path $source 'MTKBrowser.exe'))) {
    throw "Build output not found. Run: cmake --build build/native-adblock --config Release --target mtk_browser"
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
Get-ChildItem -Path $target -Recurse -File -Include 'debug.log', '*.pdb', '*.ilk', '*.exp' |
    Remove-Item -Force

if (-not (Test-Path (Join-Path $target 'mtk_filters.txt'))) {
    throw 'MTK Shields filter list is missing from the build output.'
}

$legal = Join-Path $target 'Legal'
New-Item -ItemType Directory -Path $legal | Out-Null
Copy-Item (Join-Path $repoRoot 'native\PREVIEW_NOTICE.txt') $target
Copy-Item (Join-Path $repoRoot 'LICENSES.md') $legal
Copy-Item (Join-Path $repoRoot 'THIRD_PARTY_NOTICES.md') $legal
Copy-Item (Join-Path $repoRoot 'UPSTREAM.md') $legal
Copy-Item (Join-Path $repoRoot 'SOURCE_OFFER_REQUIREMENTS.md') $legal
Copy-Item (Join-Path $repoRoot 'vendor\cef-sdk\LICENSE.txt') (Join-Path $legal 'CEF_LICENSE.txt')
Copy-Item (Join-Path $repoRoot 'vendor\cef-sdk\CREDITS.html') (Join-Path $legal 'CEF_CREDITS.html')

$exe = Join-Path $target 'MTKBrowser.exe'
Write-Output "Staged: $exe"
Write-Output ('Files:  ' + (Get-ChildItem $target -Recurse -File).Count)
