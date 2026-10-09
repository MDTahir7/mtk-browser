[CmdletBinding()]
param(
  # Use the freshly built Release executable, not an installer staging folder.
  [Parameter(Mandatory = $true)]
  [ValidateNotNullOrEmpty()]
  [string]$BrowserPath,

  # An ordinary HTTPS page keeps the page-ready metric comparable between runs.
  [ValidatePattern('^(https?://|mtk://)')]
  [string]$Url = 'https://example.com/',

  [ValidateRange(1, 30)]
  [int]$Samples = 5,

  [ValidateRange(5, 120)]
  [int]$TimeoutSeconds = 45,

  # Startup is measured when a browser window is visible. A more specific page
  # title is needed before this script can responsibly call a page "ready".
  [string]$ReadyTitlePattern = '*Example Domain*',

  [string]$OutputPath = '',

  # Lets CI and reviewers validate arguments and paths without starting a GUI.
  [switch]$DryRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$BrowserPath = [IO.Path]::GetFullPath($BrowserPath)
if (-not (Test-Path -LiteralPath $BrowserPath -PathType Leaf)) {
  throw "Browser executable was not found: $BrowserPath"
}
if ([IO.Path]::GetExtension($BrowserPath) -ine '.exe') {
  throw "BrowserPath must point to an .exe file: $BrowserPath"
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
  $OutputPath = Join-Path $PSScriptRoot '..\build\benchmarks\mtk-browser-performance.csv'
}
$OutputPath = [IO.Path]::GetFullPath($OutputPath)

if ($DryRun) {
  [pscustomobject]@{
    BrowserPath = $BrowserPath
    Url = $Url
    Samples = $Samples
    TimeoutSeconds = $TimeoutSeconds
    ReadyTitlePattern = $ReadyTitlePattern
    OutputPath = $OutputPath
  } | Format-List
  return
}

$browserName = [IO.Path]::GetFileNameWithoutExtension($BrowserPath)
$outputDirectory = Split-Path -Parent $OutputPath
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

function Get-RunProcesses([datetime]$StartedAt) {
  # CEF starts renderer, GPU and utility children after the browser process. The
  # process name and start time avoid counting an already-open MTK Browser.
  @(Get-Process -Name $browserName -ErrorAction SilentlyContinue | Where-Object {
    try { $_.StartTime -ge $StartedAt.AddSeconds(-1) } catch { $false }
  })
}

function Get-Median([double[]]$Values) {
  $ordered = @($Values | Sort-Object)
  if ($ordered.Count -eq 0) { return $null }
  $middle = [int][math]::Floor($ordered.Count / 2)
  if (($ordered.Count % 2) -eq 1) { return $ordered[$middle] }
  return [math]::Round(($ordered[$middle - 1] + $ordered[$middle]) / 2, 2)
}

if (-not ([System.Management.Automation.PSTypeName]'MtkBenchmarkHelper').Type) {
  Add-Type -TypeDefinition @"
  using System;
  using System.Text;
  using System.Runtime.InteropServices;

  public class MtkBenchmarkHelper {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc enumProc, IntPtr lParam);
    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
    [DllImport("user32.dll", CharSet = CharSet.Auto)]
    public static extern int GetWindowText(IntPtr hWnd, StringBuilder text, int count);
    [DllImport("user32.dll", CharSet = CharSet.Auto)]
    public static extern int GetClassName(IntPtr hWnd, StringBuilder text, int count);
    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")]
    public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);

    public static IntPtr FindBrowserWindow(uint pid, out string title) {
      IntPtr found = IntPtr.Zero;
      string foundTitle = "";
      EnumWindows((hWnd, lParam) => {
        uint windowPid;
        GetWindowThreadProcessId(hWnd, out windowPid);
        if (windowPid == pid && IsWindowVisible(hWnd)) {
          StringBuilder cls = new StringBuilder(64);
          GetClassName(hWnd, cls, 64);
          if (cls.ToString() == "MTKBrowserWindow") {
            StringBuilder t = new StringBuilder(512);
            GetWindowText(hWnd, t, 512);
            found = hWnd;
            foundTitle = t.ToString();
            return false;
          }
        }
        return true;
      }, IntPtr.Zero);
      title = foundTitle;
      return found;
    }
  }
"@
}

function Wait-ForMainWindow([datetime]$StartedAt, [Diagnostics.Stopwatch]$Timer,
                            [int]$Timeout) {
  $deadline = (Get-Date).AddSeconds($Timeout)
  do {
    $processes = Get-RunProcesses $StartedAt
    foreach ($proc in $processes) {
      $title = ""
      $hwnd = [MtkBenchmarkHelper]::FindBrowserWindow($proc.Id, [ref]$title)
      if ($hwnd -ne [IntPtr]::Zero -and -not [string]::IsNullOrWhiteSpace($title)) {
        return [pscustomobject]@{
          Process = $proc
          Handle = $hwnd
          Title = $title
        }
      }
      if ($proc.MainWindowHandle -ne 0 -and -not [string]::IsNullOrWhiteSpace($proc.MainWindowTitle)) {
        return [pscustomobject]@{
          Process = $proc
          Handle = [IntPtr]$proc.MainWindowHandle
          Title = $proc.MainWindowTitle
        }
      }
    }
    Start-Sleep -Milliseconds 100
  } while ((Get-Date) -lt $deadline)
  return $null
}

function Wait-ForPageTitle([datetime]$StartedAt, [int]$Timeout, [string]$Pattern) {
  if ([string]::IsNullOrWhiteSpace($Pattern)) { return $null }
  $deadline = (Get-Date).AddSeconds($Timeout)
  do {
    $processes = Get-RunProcesses $StartedAt
    foreach ($proc in $processes) {
      $title = ""
      $hwnd = [MtkBenchmarkHelper]::FindBrowserWindow($proc.Id, [ref]$title)
      if ($hwnd -ne [IntPtr]::Zero -and $title -like $Pattern) {
        return [pscustomobject]@{
          Process = $proc
          Handle = $hwnd
          Title = $title
        }
      }
      if ($proc.MainWindowHandle -ne 0 -and $proc.MainWindowTitle -like $Pattern) {
        return [pscustomobject]@{
          Process = $proc
          Handle = [IntPtr]$proc.MainWindowHandle
          Title = $proc.MainWindowTitle
        }
      }
    }
    Start-Sleep -Milliseconds 100
  } while ((Get-Date) -lt $deadline)
  return $null
}

function Wait-ForRunExit([datetime]$StartedAt, [int]$Timeout) {
  $deadline = (Get-Date).AddSeconds($Timeout)
  do {
    if (@(Get-RunProcesses $StartedAt).Count -eq 0) { return $true }
    Start-Sleep -Milliseconds 100
  } while ((Get-Date) -lt $deadline)
  return $false
}

$runs = @()
for ($sample = 1; $sample -le $Samples; ++$sample) {
  # A CEF singleton can briefly outlive its top-level window. Never let the
  # next sample attach to that process, or it would measure a warm redirect.
  if (-not (Wait-ForRunExit (Get-Date).AddMinutes(-10) 15)) {
    throw "Previous MTK Browser processes did not exit before sample $sample."
  }
  # A distinct profile prevents a warm browser process, prior cookies, history
  # or a running-instance redirect from contaminating a cold-start measurement.
  # Profile ids are deliberately capped at 32 ASCII characters by the browser's
  # path-safety policy. Keep this one below that limit: "perf-" plus 26 hex
  # characters is unique enough for a benchmark while still being accepted.
  $profile = "perf-$(([guid]::NewGuid().ToString('N')).Substring(0, 26))"
  $startedAt = Get-Date
  $timer = [Diagnostics.Stopwatch]::StartNew()
  $root = Start-Process -FilePath $BrowserPath -ArgumentList @("--profile=$profile", $Url) -PassThru
  $window = Wait-ForMainWindow $startedAt $timer $TimeoutSeconds
  $startupMs = if ($null -eq $window) { $null } else { [math]::Round($timer.Elapsed.TotalMilliseconds, 0) }
  $ready = Wait-ForPageTitle $startedAt $TimeoutSeconds $ReadyTitlePattern
  $pageReadyMs = if ($null -eq $ready) { $null } else { [math]::Round($timer.Elapsed.TotalMilliseconds, 0) }
  # PowerShell unwraps a one-item pipeline result into a scalar. Keep this an
  # array so `.Count` and Measure-Object stay valid for both one and many CEF
  # processes.
  $processes = @(Get-RunProcesses $startedAt)
  $workingSetMiB = [math]::Round((($processes | Measure-Object -Property WorkingSet64 -Sum).Sum / 1MB), 1)
  $cpuSeconds = [math]::Round((($processes | Measure-Object -Property CPU -Sum).Sum), 2)
  # Prefer the window object that actually satisfied the page-ready condition.
  # The startup handle can still say "New Tab" even after the page title has
  # arrived, which would make an otherwise successful CSV row misleading.
  $title = if ($null -ne $ready) { $ready.Title } `
           elseif ($null -eq $window) { '' } else { $window.Title }

  $runs += [pscustomobject]@{
    sample = $sample
    started_utc = $startedAt.ToUniversalTime().ToString('o')
    url = $Url
    startup_ms = $startupMs
    page_ready_ms = $pageReadyMs
    working_set_mib = $workingSetMiB
    cpu_seconds_at_sample = $cpuSeconds
    cef_processes = $processes.Count
    window_title = $title
    startup_timeout = ($null -eq $window)
    page_ready_timeout = ($null -eq $ready)
  }

  # Close only the top-level window that this run created. We intentionally do
  # not force-kill a browser: an unresponsive close is itself a result worth
  # recording and force-killing could corrupt the isolated profile.
  if ($null -ne $window) {
    if ($window.Handle) {
      [MtkBenchmarkHelper]::PostMessage($window.Handle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
    }
    if (-not (Wait-ForRunExit $startedAt $TimeoutSeconds)) {
      Write-Warning "Sample $sample window closed, but one or more CEF processes remained."
    }
  } elseif (-not $root.HasExited) {
    Write-Warning "Sample $sample did not create a window; process $($root.Id) was left untouched."
  }
}

$runs | Export-Csv -LiteralPath $OutputPath -NoTypeInformation -Encoding utf8
$startupValues = @($runs | Where-Object { $null -ne $_.startup_ms } |
  ForEach-Object { [double]$_.startup_ms })
$pageReadyValues = @($runs | Where-Object { $null -ne $_.page_ready_ms } |
  ForEach-Object { [double]$_.page_ready_ms })
$workingSetValues = @($runs | ForEach-Object { [double]$_.working_set_mib })
$cpuValues = @($runs | ForEach-Object { [double]$_.cpu_seconds_at_sample })
$summary = [pscustomobject]@{
  samples = $runs.Count
  startup_successes = @($runs | Where-Object { -not $_.startup_timeout }).Count
  page_ready_successes = @($runs | Where-Object { -not $_.page_ready_timeout }).Count
  median_startup_ms = Get-Median -Values $startupValues
  median_page_ready_ms = Get-Median -Values $pageReadyValues
  median_working_set_mib = Get-Median -Values $workingSetValues
  median_cpu_seconds_at_sample = Get-Median -Values $cpuValues
  output = $OutputPath
}
$summary | Format-List
