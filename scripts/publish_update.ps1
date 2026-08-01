# Builds and signs the MTK Browser update manifest.
#
# Produces two small files that get published next to the installer:
#   update.json      the manifest the browser reads
#   update.json.sig  a detached ECDSA P-256 signature over the exact bytes above
#
# The browser verifies the signature with the public key compiled into it, so a
# tampered manifest or a manifest from anyone without the private key is refused.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\publish_update.ps1 `
#       -Version 0.4.0 -Installer dist\installer\MTK-Browser-Setup-0.4.0-Preview.exe `
#       -DownloadUrl https://github.com/MDTahir7/mtk-browser-releases/releases/download/v0.4.0/MTK-Browser-Setup-0.4.0-Preview.exe

param(
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$Installer,
    [Parameter(Mandatory = $true)][string]$DownloadUrl,
    [string]$Notes = '',
    [string]$OutputDir = ''
)
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $OutputDir) { $OutputDir = Join-Path $repoRoot 'dist\update' }
$privatePath = Join-Path $env:USERPROFILE '.mtk-signing\update-private.blob'

if (-not (Test-Path $privatePath)) {
    throw "Signing key not found at $privatePath. Run scripts\generate_update_key.ps1 first."
}
if (-not (Test-Path $Installer)) {
    throw "Installer not found: $Installer"
}
if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    throw "Version must look like 0.4.0, got '$Version'."
}
# Only https is accepted by the browser, so a mistake is caught here instead of
# silently shipping an unusable manifest.
if ($DownloadUrl -notmatch '^https://') {
    throw 'DownloadUrl must be an https address.'
}

$installerItem = Get-Item $Installer
$hash = (Get-FileHash $Installer -Algorithm SHA256).Hash.ToLower()

# Compact, fixed-order JSON: the signature covers these exact bytes.
$manifest = @{
    schema      = 1
    version     = $Version
    url         = $DownloadUrl
    sha256      = $hash
    size        = $installerItem.Length
    notes       = $Notes
    published   = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
} | ConvertTo-Json -Compress

if (-not (Test-Path $OutputDir)) { New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null }
$manifestPath = Join-Path $OutputDir 'update.json'
$signaturePath = Join-Path $OutputDir 'update.json.sig'

# Write without a BOM: the signature is over raw UTF-8 bytes.
$manifestBytes = [System.Text.Encoding]::UTF8.GetBytes($manifest)
[System.IO.File]::WriteAllBytes($manifestPath, $manifestBytes)

Add-Type -AssemblyName System.Core
$blob = [System.IO.File]::ReadAllBytes($privatePath)
$key = [System.Security.Cryptography.CngKey]::Import(
    $blob, [System.Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob)
$ecdsa = New-Object System.Security.Cryptography.ECDsaCng($key)
$ecdsa.HashAlgorithm = [System.Security.Cryptography.CngAlgorithm]::Sha256
# ECDsaCng emits the raw r||s form that BCryptVerifySignature expects.
$signature = $ecdsa.SignData($manifestBytes)
$ecdsa.Dispose()
$key.Dispose()

[System.IO.File]::WriteAllText($signaturePath,
    [System.Convert]::ToBase64String($signature), (New-Object System.Text.ASCIIEncoding))

Write-Output "Manifest:  $manifestPath"
Write-Output "Signature: $signaturePath ($($signature.Length) bytes raw)"
Write-Output "Version:   $Version"
Write-Output "SHA-256:   $hash"
Write-Output "Size:      $([math]::Round($installerItem.Length / 1MB, 1)) MB"
Write-Output ''
Write-Output 'Publish both files at the update URL, and upload the installer to the release.'
