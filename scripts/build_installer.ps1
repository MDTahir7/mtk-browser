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

# Optional release signing. Certificate material stays in the Windows
# certificate store/CI secret provider; no private key is read from the repo.
$signingRequired = $env:MTK_REQUIRE_SIGNING -eq '1'
$certificateSha1 = $env:MTK_SIGN_CERT_SHA1
$timestampUrl = if ($env:MTK_SIGN_TIMESTAMP_URL) {
    $env:MTK_SIGN_TIMESTAMP_URL
} else {
    'http://timestamp.sectigo.com'
}
if ($env:MTK_SIGNTOOL -and -not (Test-Path $env:MTK_SIGNTOOL -PathType Leaf)) {
    throw "MTK_SIGNTOOL does not point to a file: $($env:MTK_SIGNTOOL)"
}
$signTool = if ($env:MTK_SIGNTOOL) {
    $env:MTK_SIGNTOOL
} else {
    Get-Command signtool.exe -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty Source -First 1
}

function Invoke-CodeSign([string]$Path) {
    if (-not $signTool -or -not $certificateSha1) {
        if ($signingRequired) {
            throw 'Signing is required, but MTK_SIGNTOOL or MTK_SIGN_CERT_SHA1 is missing.'
        }
        return $false
    }
    & $signTool sign /sha1 $certificateSha1 /fd SHA256 /tr $timestampUrl /td SHA256 $Path
    if ($LASTEXITCODE -ne 0) { throw "Code signing failed: $Path" }
    & $signTool verify /pa /all $Path
    if ($LASTEXITCODE -ne 0) { throw "Signature verification failed: $Path" }
    return $true
}

$stagedExe = Join-Path $repoRoot 'dist\MTK Browser\MTKBrowser.exe'
$stagedExeSigned = Invoke-CodeSign $stagedExe

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

$setup = Join-Path $repoRoot 'dist\installer\MTK-Browser-Setup-0.3.0-Preview.exe'
if (-not (Test-Path $setup)) { throw 'Expected installer output was not created.' }
$installerSigned = Invoke-CodeSign $setup
$file = Get-Item $setup
$hash = Get-FileHash $setup -Algorithm SHA256
Write-Output "Installer: $($file.FullName)"
Write-Output "Size:      $([math]::Round($file.Length / 1MB, 1)) MB"
Write-Output "SHA-256:   $($hash.Hash)"
if ($stagedExeSigned -and $installerSigned) {
    Write-Output 'Signing:    Authenticode signatures verified.'
} else {
    Write-Warning 'Unsigned preview: Windows SmartScreen may show an Unknown publisher warning.'
}