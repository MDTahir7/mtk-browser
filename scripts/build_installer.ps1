# Builds, stages and packages the native CEF application. No network access or
# Electron tooling is used.
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$running = Get-CimInstance Win32_Process -Filter "Name='MTKBrowser.exe'"
if ($running) {
    throw 'Close all MTK Browser windows before building the installer.'
}

$icon = Join-Path $repoRoot 'native\resources\mtk_browser.ico'
if (-not (Test-Path $icon)) {
    & python (Join-Path $PSScriptRoot 'generate_icon.py')
    if ($LASTEXITCODE -ne 0) { throw 'Icon generation failed.' }
}

& cmake --build 'build/native-adblock' --config Release --target mtk_browser
if ($LASTEXITCODE -ne 0) { throw 'Native Release build failed.' }

& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'stage_release.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Release staging failed.' }

$isccCandidates = @(
    (Get-Command ISCC.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source),
    # Portable Inno Setup kept beside the build output, used when the tool is
    # not installed system-wide.
    (Join-Path $repoRoot 'build\tools\innosetup\ISCC.exe'),
    'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
    'C:\Program Files\Inno Setup 6\ISCC.exe'
) | Where-Object { $_ -and (Test-Path $_) }
$iscc = $isccCandidates | Select-Object -First 1
if (-not $iscc) { throw 'Inno Setup 6 compiler (ISCC.exe) was not found.' }

& $iscc (Join-Path $repoRoot 'installer\MTKBrowser.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }

$setup = Join-Path $repoRoot 'dist\installer\MTK-Browser-Setup-0.2.0-Preview.exe'
if (-not (Test-Path $setup)) { throw 'Expected installer output was not created.' }
$file = Get-Item $setup
$hash = Get-FileHash $setup -Algorithm SHA256
Write-Output "Installer: $($file.FullName)"
Write-Output "Size:      $([math]::Round($file.Length / 1MB, 1)) MB"
Write-Output "SHA-256:   $($hash.Hash)"
Write-Warning 'Unsigned preview: Windows SmartScreen may show an Unknown publisher warning.'