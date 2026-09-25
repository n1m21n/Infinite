<#
Infinite-Turbo self-test driver (Windows).

Drives the built-in env-var self-test harness in src/main.cpp through the real
Infinite-Turbo.exe: real ImGui frames, real GL draws, real node graph. Each
check sets INFINITE_<NAME>=1 plus INFINITE_EXITAFTER=<frames>, the app prints
a verdict line ending in "OK", containing "FAIL" or ending in "BUG", and exits.

Results are written INSIDE the project folder so they can be read back later
(also from a Cowork session): build\test-results\
   summary.txt           one line per check + totals
   <NAME>.log            full stdout/stderr of each check
   screenshot.png        visual smoke test

Usage (from the repo root, or via test-windows.bat):
   powershell -ExecutionPolicy Bypass -File .claude\skills\run-turbo-tests\driver.ps1 [options]
     -SkipBuild            reuse the current build
     -Config Debug         test the Debug build (default Release)
     -Only A,B,C           run only these checks (names without INFINITE_)
     -Quick                smoke subset: UNDOTEST, PATCHTEST, ROUNDTRIPTEST, BYPASSTEST,
                           AUDIOTEARDOWNSWEEPTEST, DELETECRASHTEST
     -ShotOnly             build + screenshot only
     -TimeoutSec 180       per-check timeout (a hung check is killed and reported [HANG])
Exit code: 0 when everything passed, 1 otherwise.
#>
param(
   [switch]$SkipBuild,
   [ValidateSet('Release','Debug')][string]$Config = 'Release',
   [string[]]$Only,
   [switch]$Quick,
   [switch]$ShotOnly,
   [int]$TimeoutSec = 180
)

$ErrorActionPreference = 'Continue'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
Set-Location $root
$bin = Join-Path $root "build\windows-vs2022\$Config\Infinite-Turbo.exe"
$out = Join-Path $root 'build\test-results'
New-Item -ItemType Directory -Force -Path $out | Out-Null
Get-ChildItem -LiteralPath $out -File -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
$summary = Join-Path $out 'summary.txt'

function Say([string]$text) { Write-Host $text; Add-Content -LiteralPath $summary -Value $text -Encoding UTF8 }

# name:exitAfter - frame budgets inherited from the macOS driver (each verified
# to print its full verdict by that frame). macOS-only checks were dropped:
# PLUGINSCANTEST (Audio Unit instantiation).
$tests = @(
   'UNDOTEST:10','PATCHTEST:30','ROUNDTRIPTEST:35','GROUPTEST:30','COMMENTTEST:15',
   'HIDETEST:35','SELECTTEST:35','DISTRIBUTETEST:10','PHASE4TEST:10','MINIVIEWPORTTEST:12',
   'COLORTEST:13','MACROTEST:30','PALETTETEST:30','BYPASSTEST:30','GEOTEST:30',
   'MESHOPTEST:30','TEXT3DTEST:30','PATHOCEANTEST:35','SHADOWTEST:35','MATFRAMETEST:35',
   'MAPTEST:35','PADPATHTEST:35','BUGTEST:35','FIXTEST:35','3DTEST:35',
   'TRANSFORMSWEEPTEST:10','MAPPINGSWEEPTEST:10','REVISIONSWEEPTEST:10','ENVTEST:14',
   'PHASEATEST:35','PHASECTEST:35','PHASEDTEST:35','PHASEETEST:35','PHASEFTEST:35',
   'WRAPTEST:35','LIVETEST:35','PHASE1TEST:35','DELETECRASHTEST:8','AUDIOGRAPHTEST:8',
   'DRAGTEST:35','WTDRAGTEST:35','AUDIOPARAMSWEEPTEST:1','AUDIOTEARDOWNSWEEPTEST:10',
   'AUDIOPDCTEST:1','AUDIOLIFECYCLETEST:8','AUDIORECOVERYTEST:8',
   'SAMPLERDRAGTEST:600','MEDIADRAGTEST:600','PLUGINDRAGTEST:600'
)
if ($Quick) {
   $Only = @('UNDOTEST','PATCHTEST','ROUNDTRIPTEST','BYPASSTEST','AUDIOTEARDOWNSWEEPTEST','DELETECRASHTEST')
}
if ($Only) {
   # Through cmd.exe / -File, "-Only A,B" arrives as ONE string: split it.
   $wanted = $Only | ForEach-Object { $_ -split '[,;\s]+' } | Where-Object { $_ } |
      ForEach-Object { $_.ToUpperInvariant() -replace '^INFINITE_','' }
   $known = @{}
   foreach ($t in $tests) { $known[$t.Split(':')[0]] = $t }
   # Unknown names still run, with a generous 60-frame budget.
   $tests = $wanted | ForEach-Object { if ($known.ContainsKey($_)) { $known[$_] } else { "${_}:60" } }
}

Say ("Infinite-Turbo self-tests  {0:yyyy-MM-dd HH:mm}  config={1}" -f (Get-Date), $Config)

# ---------------------------------------------------------------- build
if (-not $SkipBuild) {
   Say '== Build'
   $buildArgs = @('nopack')
   if ($Config -eq 'Debug') { $buildArgs += 'debug' }
   & "$root\build-windows.bat" @buildArgs 2>&1 |
      Tee-Object -FilePath (Join-Path $out 'build.log') | Out-Host
   if ($LASTEXITCODE -ne 0) { Say 'BUILD FAILED - see build\test-results\build.log'; exit 1 }
}
if (-not (Test-Path -LiteralPath $bin)) { Say "binary not found: $bin"; exit 1 }

function Invoke-Check([string]$name, [hashtable]$vars, [string]$log) {
   foreach ($k in $vars.Keys) { Set-Item -Path "Env:$k" -Value $vars[$k] }
   $errLog = "$log.stderr"
   $p = Start-Process -FilePath $bin -WorkingDirectory (Split-Path -Parent $bin) -NoNewWindow -PassThru `
        -RedirectStandardOutput $log -RedirectStandardError $errLog
   $null = $p.Handle # cache the handle so ExitCode is available after exit
   $finished = $p.WaitForExit($TimeoutSec * 1000)
   foreach ($k in $vars.Keys) { Remove-Item -Path "Env:$k" -ErrorAction SilentlyContinue }
   if (-not $finished) { try { $p.Kill() } catch {} ; Start-Sleep -Milliseconds 300 }
   if (Test-Path -LiteralPath $errLog) {
      Get-Content -LiteralPath $errLog -ErrorAction SilentlyContinue | Add-Content -LiteralPath $log
      Remove-Item -LiteralPath $errLog -Force -ErrorAction SilentlyContinue
   }
   if (-not $finished) { return 'HANG' }
   return $p.ExitCode
}

# ---------------------------------------------------------------- screenshot
$fail = 0; $pass = 0; $failed = @()
Say '== Visual smoke (screenshot)'
$shot = Join-Path $out 'screenshot.png'
$rc = Invoke-Check 'SCREENSHOT' @{ IMAGERESYNTH_SCREENSHOT = $shot; INFINITE_SHOWCASE = '1' } (Join-Path $out 'SCREENSHOT.log')
if (Test-Path -LiteralPath $shot) { Say "  [pass]  SCREENSHOT  -> build\test-results\screenshot.png" ; $pass++ }
else { Say "  [FAIL]  SCREENSHOT (exit $rc)"; $fail++; $failed += 'SCREENSHOT' }
if ($ShotOnly) { exit ($(if ($fail) { 1 } else { 0 })) }

# ---------------------------------------------------------------- suite
Say ("== Self-test suite ({0} checks)" -f $tests.Count)
foreach ($spec in $tests) {
   $name, $frames = $spec.Split(':')
   $log = Join-Path $out "$name.log"
   $rc = Invoke-Check $name @{ ("INFINITE_$name") = '1'; INFINITE_EXITAFTER = $frames } $log
   $text = if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log -ErrorAction SilentlyContinue } else { @() }
   if ($rc -eq 'HANG') {
      Say "  [HANG]  $name - killed after ${TimeoutSec}s, see build\test-results\$name.log"
      $fail++; $failed += "$name (hang)"; continue
   }
   if ($rc -ne 0) {
      Say ("  [CRASH] {0} - exit code {1} (0x{2:X8}), see build\test-results\{0}.log" -f $name, $rc, ([int64]$rc -band 0xffffffff))
      $fail++; $failed += "$name (crash)"; continue
   }
   $bad = $text | Where-Object { $_ -match 'FAIL' -or $_ -match 'BUG$' }
   if ($bad) {
      Say "  [FAIL]  $name - see build\test-results\$name.log"
      $bad | Select-Object -First 12 | ForEach-Object { Say "          $_" }
      $fail++; $failed += $name
   } elseif (-not $text) {
      Say "  [EMPTY] $name - no output (frame budget too small or stdout lost)"
      $fail++; $failed += "$name (empty)"
   } else {
      $verdict = $text | Where-Object { $_ -match ' OK$' } | Select-Object -Last 1
      Say ("  [pass]  {0}  {1}" -f $name, $(if ($verdict) { "- $verdict" } else { '' }))
      $pass++
   }
}

Say '== Summary'
Say "passed: $pass   failed: $fail"
if ($fail -gt 0) {
   Say ("failing: " + ($failed -join ', '))
   Say 'Compare with .claude\skills\run-turbo-tests\known-baseline.md before treating a FAIL as a regression.'
   exit 1
}
Say 'all checks green.'
exit 0
