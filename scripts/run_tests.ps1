# Builds and runs the MTK Browser logic tests.
#
# These cover the safety-critical modules that need no CEF instance: navigation
# and request policy, download rules and file-name hardening, and session
# restore. They compile with the same MSVC toolchain as the browser and add no
# third-party test dependency.
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\run_tests.ps1

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$sourceDir = Join-Path $repoRoot 'native\src'
$testDir = Join-Path $repoRoot 'tests'
$outputDir = Join-Path $repoRoot 'build\tests'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'vswhere.exe was not found; install Visual Studio Build Tools.' }
$installPath = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installPath) { throw 'No MSVC x64 toolchain was found.' }
$vcvars = Join-Path $installPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat was not found at $vcvars." }

New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

# Test cases plus the browser sources under test. Only CEF-free modules are
# listed; anything needing a live browser is covered by manual checks instead.
$sources = @(
    (Join-Path $testDir 'test_support.cpp'),
    (Join-Path $testDir 'policy_tests.cpp'),
    (Join-Path $testDir 'download_tests.cpp'),
    (Join-Path $testDir 'session_tests.cpp'),
    (Join-Path $sourceDir 'policy.cpp'),
    (Join-Path $sourceDir 'filter_engine.cpp'),
    (Join-Path $sourceDir 'download_manager.cpp'),
    (Join-Path $sourceDir 'session_store.cpp'),
    (Join-Path $sourceDir 'preferences.cpp')
)
foreach ($source in $sources) {
    if (-not (Test-Path $source)) { throw "Missing source file: $source" }
}

$exe = Join-Path $outputDir 'mtk_tests.exe'
$quoted = ($sources | ForEach-Object { '"' + $_ + '"' }) -join ' '
$command = "cl /nologo /std:c++20 /EHsc /W3 /permissive- /DNOMINMAX /I`"$sourceDir`" " +
           "/I`"$testDir`" $quoted /Fo`"$outputDir\\`" /Fe`"$exe`" shell32.lib ole32.lib bcrypt.lib"

$batch = Join-Path $outputDir 'run_tests.cmd'
@(
    '@echo off',
    "call `"$vcvars`" >nul",
    'if errorlevel 1 exit /b 1',
    $command,
    'if errorlevel 1 exit /b 1',
    "`"$exe`"",
    'exit /b %errorlevel%'
) | Set-Content -Path $batch -Encoding ASCII

& cmd.exe /c "`"$batch`""
$exitCode = $LASTEXITCODE
if ($exitCode -ne 0) {
    Write-Error "Logic tests failed with exit code $exitCode."
    exit $exitCode
}
Write-Output 'Logic tests passed.'
