<#
Runs a small, account-free browser compatibility smoke matrix. This is not a
replacement for the authorized-account matrix in the manual checklist: it only
proves that ordinary public TLS pages create a tab, render a document title,
and close cleanly in a fresh profile.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [ValidateNotNullOrEmpty()]
  [string]$BrowserPath,

  [ValidateRange(5, 120)]
  [int]$TimeoutSeconds = 45,

  [string]$OutputPath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$benchmark = Join-Path $PSScriptRoot 'benchmark_browser.ps1'
$BrowserPath = [IO.Path]::GetFullPath($BrowserPath)
if (-not (Test-Path -LiteralPath $BrowserPath -PathType Leaf)) {
  throw "Browser executable was not found: $BrowserPath"
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
  $OutputPath = Join-Path $repoRoot 'build\benchmarks\compatibility-smoke.csv'
}
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
$temporaryRoot = Join-Path ([IO.Path]::GetDirectoryName($OutputPath)) 'compatibility-runs'
New-Item -ItemType Directory -Force -Path $temporaryRoot | Out-Null

$checks = @(
  @{ name = 'Example Domain'; url = 'https://example.com/'; title = '*Example Domain*' },
  @{ name = 'Google Search'; url = 'https://www.google.com/'; title = '*Google*' },
  @{ name = 'Wikipedia'; url = 'https://www.wikipedia.org/'; title = '*Wikipedia*' }
)

$results = foreach ($check in $checks) {
  $temporary = Join-Path $temporaryRoot ("{0}.csv" -f ([guid]::NewGuid().ToString('N')))
  & $benchmark -BrowserPath $BrowserPath -Url $check.url `
    -ReadyTitlePattern $check.title -Samples 1 -TimeoutSeconds $TimeoutSeconds `
    -OutputPath $temporary | Out-Host
  # benchmark_browser.ps1 is invoked in this PowerShell process. It throws on
  # an execution failure; $LASTEXITCODE is intentionally unavailable here when
  # no native executable has been called directly.
  $row = Import-Csv -LiteralPath $temporary | Select-Object -First 1
  [pscustomobject]@{
    service = $check.name
    url = $check.url
    title_pattern = $check.title
    startup_ms = $row.startup_ms
    page_ready_ms = $row.page_ready_ms
    working_set_mib = $row.working_set_mib
    window_title = $row.window_title
    passed = ($row.startup_timeout -eq 'False' -and
              $row.page_ready_timeout -eq 'False')
  }
}

$results | Export-Csv -LiteralPath $OutputPath -NoTypeInformation -Encoding utf8
$results | Format-Table -AutoSize
$failed = @($results | Where-Object { -not $_.passed })
if ($failed.Count -ne 0) { exit 1 }
