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
    (Join-Path $testDir 'icon_strip_tests.cpp'),
    (Join-Path $testDir 'import_tests.cpp'),
    (Join-Path $testDir 'profile_tests.cpp'),
    (Join-Path $testDir 'site_settings_tests.cpp'),
    (Join-Path $testDir 'strings_tests.cpp'),
    (Join-Path $testDir 'password_tests.cpp'),
    (Join-Path $testDir 'crash_tests.cpp'),
    (Join-Path $testDir 'user_filter_tests.cpp'),
    (Join-Path $testDir 'connection_tests.cpp'),
    (Join-Path $testDir 'history_page_tests.cpp'),
    (Join-Path $testDir 'home_page_tests.cpp'),
    (Join-Path $testDir 'bookmark_folder_tests.cpp'),
    (Join-Path $testDir 'fingerprint_tests.cpp'),
    (Join-Path $testDir 'persistence_tests.cpp'),
    (Join-Path $testDir 'translation_tests.cpp'),
    (Join-Path $testDir 'web_app_tests.cpp'),
    (Join-Path $testDir 'autofill_tests.cpp'),
    (Join-Path $testDir 'private_session_tests.cpp'),
    (Join-Path $testDir 'passkey_tests.cpp'),
    (Join-Path $testDir 'task_manager_tests.cpp'),
    (Join-Path $testDir 'downloads_page_tests.cpp'),
    (Join-Path $testDir 'settings_page_tests.cpp'),
    (Join-Path $sourceDir 'autofill_store.cpp'),
    (Join-Path $sourceDir 'tab_search.cpp'),
    (Join-Path $sourceDir 'task_manager_dialog.cpp'),
    (Join-Path $sourceDir 'translation.cpp'),
    (Join-Path $sourceDir 'web_app.cpp'),
    (Join-Path $sourceDir 'dialog_text.cpp'),
    (Join-Path $sourceDir 'fingerprint_script.cpp'),
    (Join-Path $sourceDir 'user_filters.cpp'),
    (Join-Path $sourceDir 'strings.cpp'),
    (Join-Path $sourceDir 'password_store.cpp'),
    (Join-Path $sourceDir 'private_session.cpp'),
    (Join-Path $sourceDir 'crash_guard.cpp'),
    (Join-Path $sourceDir 'policy.cpp'),
    # The built-in documents. Session restore and the home-page preference both
    # ask the home page whether a URL is the browser's own page, so it links
    # here too; the history page is covered by its own cases.
    (Join-Path $sourceDir 'home_page.cpp'),
    (Join-Path $sourceDir 'builtin_favicons.cpp'),
    (Join-Path $sourceDir 'favicon_store.cpp'),
    (Join-Path $sourceDir 'inline_asset.cpp'),
    (Join-Path $sourceDir 'history_page.cpp'),
    (Join-Path $sourceDir 'passkey_page.cpp'),
    (Join-Path $sourceDir 'downloads_page.cpp'),
    (Join-Path $sourceDir 'settings_page.cpp'),
    (Join-Path $sourceDir 'bookmark_import.cpp'),
    (Join-Path $sourceDir 'library_store.cpp'),
    (Join-Path $sourceDir 'profile.cpp'),
    (Join-Path $sourceDir 'site_settings.cpp'),
    (Join-Path $sourceDir 'filter_engine.cpp'),
    (Join-Path $sourceDir 'download_manager.cpp'),
    (Join-Path $sourceDir 'session_store.cpp'),
    (Join-Path $sourceDir 'preferences.cpp'),
    (Join-Path $sourceDir 'icon_strip.cpp'),
    (Join-Path $sourceDir 'ole_drag_drop.cpp'),
    (Join-Path $sourceDir 'tab_preview.cpp'),
    (Join-Path $sourceDir 'updater.cpp')
)
foreach ($source in $sources) {
    if (-not (Test-Path $source)) { throw "Missing source file: $source" }
}

$exe = Join-Path $outputDir 'mtk_tests.exe'

# The icon strip loader reads from the directory of the running executable, so the
# shipped asset is staged beside the test binary and the real file is exercised.
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_shield_hover.png') `
    (Join-Path $outputDir 'mtk_shield_hover.png') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_home_hover.png') `
    (Join-Path $outputDir 'mtk_home_hover.png') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_search_3d.webp') `
    (Join-Path $outputDir 'mtk_search_3d.webp') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_search_3d_still.webp') `
    (Join-Path $outputDir 'mtk_search_3d_still.webp') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_home_3d.webp') `
    (Join-Path $outputDir 'mtk_home_3d.webp') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_home_3d_still.webp') `
    (Join-Path $outputDir 'mtk_home_3d_still.webp') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_store_hover.png') `
    (Join-Path $outputDir 'mtk_store_hover.png') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_store_3d.webp') `
    (Join-Path $outputDir 'mtk_store_3d.webp') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_store_3d_still.webp') `
    (Join-Path $outputDir 'mtk_store_3d_still.webp') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_notes_hover.png') `
    (Join-Path $outputDir 'mtk_notes_hover.png') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_notes_3d.webp') `
    (Join-Path $outputDir 'mtk_notes_3d.webp') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_notes_3d_still.webp') `
    (Join-Path $outputDir 'mtk_notes_3d_still.webp') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_download_hover.png') `
    (Join-Path $outputDir 'mtk_download_hover.png') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_download_3d.webp') `
    (Join-Path $outputDir 'mtk_download_3d.webp') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_download_3d_still.webp') `
    (Join-Path $outputDir 'mtk_download_3d_still.webp') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_history_hover.png') `
    (Join-Path $outputDir 'mtk_history_hover.png') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_history_3d.webp') `
    (Join-Path $outputDir 'mtk_history_3d.webp') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_history_3d_still.webp') `
    (Join-Path $outputDir 'mtk_history_3d_still.webp') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_settings_hover.png') `
    (Join-Path $outputDir 'mtk_settings_hover.png') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_settings_3d.webp') `
    (Join-Path $outputDir 'mtk_settings_3d.webp') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_settings_3d_still.webp') `
    (Join-Path $outputDir 'mtk_settings_3d_still.webp') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_bookmarks_hover.png') `
    (Join-Path $outputDir 'mtk_bookmarks_hover.png') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_bookmarks_3d.webp') `
    (Join-Path $outputDir 'mtk_bookmarks_3d.webp') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_bookmarks_3d_still.webp') `
    (Join-Path $outputDir 'mtk_bookmarks_3d_still.webp') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_focus_strip.png') `
    (Join-Path $outputDir 'mtk_focus_strip.png') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_focus_3d.webp') `
    (Join-Path $outputDir 'mtk_focus_3d.webp') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_focus_3d_still.webp') `
    (Join-Path $outputDir 'mtk_focus_3d_still.webp') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_refresh_strip.png') `
    (Join-Path $outputDir 'mtk_refresh_strip.png') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_refresh_3d.webp') `
    (Join-Path $outputDir 'mtk_refresh_3d.webp') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_refresh_3d_still.webp') `
    (Join-Path $outputDir 'mtk_refresh_3d_still.webp') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_screenshot_hover.png') `
    (Join-Path $outputDir 'mtk_screenshot_hover.png') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_screenshot_strip.png') `
    (Join-Path $outputDir 'mtk_screenshot_strip.png') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_screenshot_3d.webp') `
    (Join-Path $outputDir 'mtk_screenshot_3d.webp') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_screenshot_3d_still.webp') `
    (Join-Path $outputDir 'mtk_screenshot_3d_still.webp') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_search_web_3d.webp') `
    (Join-Path $outputDir 'mtk_search_web_3d.webp') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_search_web_3d_still.webp') `
    (Join-Path $outputDir 'mtk_search_web_3d_still.webp') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_ask_ai_3d.webp') `
    (Join-Path $outputDir 'mtk_ask_ai_3d.webp') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_ask_ai_3d_still.webp') `
    (Join-Path $outputDir 'mtk_ask_ai_3d_still.webp') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_publishing_3d.webp') `
    (Join-Path $outputDir 'mtk_publishing_3d.webp') -Force
Copy-Item (Join-Path $repoRoot 'native\resources\mtk_publishing_3d_still.webp') `
    (Join-Path $outputDir 'mtk_publishing_3d_still.webp') -Force
$quoted = ($sources | ForEach-Object { '"' + $_ + '"' }) -join ' '
# /utf-8 matches the browser target: the sources are UTF-8 without a BOM, and
# without this MSVC would read them in the system ANSI code page.
$command = "cl /nologo /std:c++20 /utf-8 /EHsc /W3 /permissive- /DNOMINMAX /I`"$sourceDir`" " +
           "/I`"$testDir`" $quoted /Fo`"$outputDir\\`" /Fe`"$exe`" " +
           "shell32.lib ole32.lib bcrypt.lib crypt32.lib windowscodecs.lib " +
           "msimg32.lib gdi32.lib user32.lib comctl32.lib comdlg32.lib dwmapi.lib winhttp.lib"

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

# The password bridge is shipped JavaScript embedded in the native source. Run
# its DOM-fixture tests as part of the same release gate when Node.js is
# available; this catches autofill regressions without network access or a
# logged-in account.
$node = Get-Command node.exe -ErrorAction SilentlyContinue
if (-not $node) { throw 'Node.js is required to run password form tests.' }
& $node.Source --test (Join-Path $testDir 'password_form_tests.cjs')
if ($LASTEXITCODE -ne 0) {
    Write-Error "Password form tests failed with exit code $LASTEXITCODE."
    exit $LASTEXITCODE
}
Write-Output 'Password form tests passed.'
