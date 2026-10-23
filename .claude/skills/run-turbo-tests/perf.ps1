<#
Performance regression run (Turbo 0.70, F1). Plays every patch of the perf corpus in -Exe with
the patchcompat probe in play mode plus INFINITE_FRAMEPROF, VSync and the frame limiter off
(INFINITE_PERF_UNCAPPED), -Runs times, and keeps the median of each metric:
   frameMs              wall time per frame (probe, loading frames skipped)
   cpu.<stage>          FRAMEPROF CPU ms per frame stage, cpu.total
   gpu.<stage>          FRAMEPROF GPU ms per frame stage, gpu.total
   render3d             Render 3D cook including its sources
   node.<i>.<type>.ms   PointZ node CPU ms per frame, .gpu its GPU ms
The FRAMEPROF block of the first 120 frames (patch loading, shader compiles) is dropped.

Corpus: perf-corpus.txt next to this file, one .inf per line (absolute paths, # comments). Each
patch is copied with its sibling "<name>_media" folder into build\perf\corpus once, so the
original is never opened by the app.

   powershell -ExecutionPolicy Bypass -File perf.ps1 [-Exe <exe>] [-RefExe <exe>] [-Runs 3] [-UpdateBaseline]

Two ways to compare:
 - -RefExe (preferred, the gate): the reference build and the candidate run alternately in the
   same session (ref, new, ref, new...), so clock and thermal drift hit both. Between sessions
   the same build moved ~20% (2026-10-22), more than the 10% threshold.
 - otherwise perf-baseline.json (-UpdateBaseline writes it): only a rough trend.
A gated metric regresses when it is more than -Threshold (10%) AND more than -FloorMs (0.5 ms)
above the reference. Gated: frameMs, cpu.total, gpu.total, render3d, and node GPU times whose
reference is at least -FloorMs; stages and node CPU times (too noisy) are reported only.
Results: build\perf\results.json, build\perf\report.txt.
Exit code: 0 ok, 1 regression / crash / hang / missing output.
#>
param(
   [string]$Exe,
   [string]$RefExe,
   [int]$Runs = 3,
   [int]$Frames = 380,
   [double]$Threshold = 0.10,
   [double]$FloorMs = 0.5,
   [int]$TimeoutSec = 180,
   [switch]$UpdateBaseline,
   # Explicit .inf files instead of the corpus (not copied: keep their media next to them).
   # (Not "-Patches": PowerShell names are case-insensitive and $patches is the list below.)
   [string[]]$PatchFiles
)
$ErrorActionPreference = 'Continue'
$here = $PSScriptRoot
$root = (Resolve-Path (Join-Path $here '..\..\..')).Path
if (-not $Exe) { $Exe = Join-Path $root 'build\windows-vs2022\Release\Infinite-Turbo.exe' }
$Exe = (Resolve-Path $Exe).Path
if ($RefExe) { $RefExe = (Resolve-Path $RefExe).Path }
$work = Join-Path $root 'build\perf'
$corpusDir = Join-Path $work 'corpus'
$runDir = Join-Path $work 'runs'
$baselinePath = Join-Path $here 'perf-baseline.json'
$reportPath = Join-Path $work 'report.txt'
New-Item -ItemType Directory -Force -Path $corpusDir, $runDir | Out-Null
Remove-Item -LiteralPath $reportPath -ErrorAction SilentlyContinue

function Say([string]$text) { Write-Host $text; Add-Content -LiteralPath $reportPath -Value $text -Encoding UTF8 }

function Get-Median([double[]]$values) {
   $s = $values | Sort-Object
   $n = $s.Count
   if ($n -eq 0) { return $null }
   if ($n % 2 -eq 1) { return [double]$s[[int](($n - 1) / 2)] }
   return ([double]$s[$n / 2 - 1] + [double]$s[$n / 2]) / 2.0
}

# "FRAMEPROF gpu: a=1.0 node bodies=2.0 total=3.0ms" -> @{ a = 1.0; node_bodies = 2.0; total = 3.0 }
# (stage names may hold one space: "node bodies", "ui draw").
function Read-Stages([string]$line) {
   $h = @{}
   $body = $line -replace '^FRAMEPROF( gpu:)?', ''
   foreach ($m in [regex]::Matches($body, '([A-Za-z+]+(?: [A-Za-z+]+)?)=([0-9.]+)')) {
      $h[($m.Groups[1].Value -replace ' ', '_')] = [double]$m.Groups[2].Value
   }
   return $h
}

# One play of one patch: hashtable metric -> value, or $null when the run broke.
$script:broken = @()
function Measure-Patch($exePath, $p, [int]$r, [string]$tag) {
   $json = Join-Path $runDir ("{0}.{1}.{2}.json" -f $p.Name, $tag, $r)
   $log = Join-Path $runDir ("{0}.{1}.{2}.log" -f $p.Name, $tag, $r)
   Remove-Item -LiteralPath $json -ErrorAction SilentlyContinue
   $psi = New-Object Diagnostics.ProcessStartInfo
   $psi.FileName = $exePath
   $psi.WorkingDirectory = Split-Path -Parent $exePath
   $psi.UseShellExecute = $false
   $psi.RedirectStandardOutput = $true
   $psi.RedirectStandardError = $true
   $psi.EnvironmentVariables['INFINITE_LOADPATCH'] = $p.Path
   $psi.EnvironmentVariables['INFINITE_PATCHCOMPAT'] = $json
   $psi.EnvironmentVariables['INFINITE_PATCHCOMPAT_PLAY'] = '1'
   $psi.EnvironmentVariables['INFINITE_PATCHCOMPAT_FRAME'] = "$Frames"
   $psi.EnvironmentVariables['INFINITE_EXITAFTER'] = "$($Frames + 6)"
   $psi.EnvironmentVariables['INFINITE_FRAMEPROF'] = '1'
   $psi.EnvironmentVariables['INFINITE_PERF_UNCAPPED'] = '1'
   $proc = [Diagnostics.Process]::Start($psi)
   $stdout = $proc.StandardOutput.ReadToEndAsync()
   $stderr = $proc.StandardError.ReadToEndAsync()
   $verdict = 'ok'
   if (-not $proc.WaitForExit($TimeoutSec * 1000)) { try { $proc.Kill() } catch {}; $verdict = 'hang' }
   elseif ($proc.ExitCode -ne 0) { $verdict = ('crash 0x{0:X8}' -f ([int64]$proc.ExitCode -band 0xffffffff)) }
   $text = $stdout.Result
   ($text + "`n--- stderr ---`n" + $stderr.Result) | Set-Content -LiteralPath $log -Encoding UTF8
   if ($verdict -eq 'ok' -and -not (Test-Path -LiteralPath $json)) { $verdict = 'nojson' }
   if ($verdict -ne 'ok') { $script:broken += "$($p.Name) $tag run $r ($verdict)"; Say "  [$verdict] $($p.Name) $tag run $r, see $log"; return $null }

   $one = @{}
   $lines = $text -split "`r?`n"
   $cpu = @($lines | Where-Object { $_ -match '^FRAMEPROF events' }) | Select-Object -Skip 1
   $gpu = @($lines | Where-Object { $_ -match '^FRAMEPROF gpu:' }) | Select-Object -Skip 1
   $r3d = @($lines | Where-Object { $_ -match '^FRAMEPROF render3d' }) | Select-Object -Skip 1
   foreach ($set in @(@{ L = $cpu; P = 'cpu.' }, @{ L = $gpu; P = 'gpu.' })) {
      $acc = @{}
      foreach ($l in $set.L) { $h = Read-Stages $l; foreach ($k in $h.Keys) { if (-not $acc[$k]) { $acc[$k] = @() }; $acc[$k] += $h[$k] } }
      foreach ($k in $acc.Keys) { $one[$set.P + $k] = ($acc[$k] | Measure-Object -Average).Average }
   }
   if ($r3d) { $one['render3d'] = (@($r3d | ForEach-Object { [double]([regex]::Match($_, '=([0-9.]+)ms').Groups[1].Value) }) | Measure-Object -Average).Average }
   $probe = Get-Content -LiteralPath $json -Raw | ConvertFrom-Json
   if ($probe.frameMs -ne $null) { $one['frameMs'] = [double]$probe.frameMs }
   foreach ($n in $probe.nodes) {
      if ($n.PSObject.Properties['ms']) {
         $key = 'node.{0}.{1}' -f $n.i, ($n.type -replace '[^A-Za-z0-9]', '')
         if ($n.ms -ne $null) { $one[$key + '.ms'] = [double]$n.ms }
         if ($n.gpu -ne $null) { $one[$key + '.gpu'] = [double]$n.gpu }
      }
   }
   return $one
}

function Add-Samples([hashtable]$samples, $one) {
   if ($one -eq $null) { return }
   foreach ($k in $one.Keys) { if (-not $samples[$k]) { $samples[$k] = @() }; $samples[$k] += $one[$k] }
}

function Get-Medians([hashtable]$samples) {
   $med = [ordered]@{}
   foreach ($k in ($samples.Keys | Sort-Object)) { $med[$k] = [math]::Round((Get-Median $samples[$k]), 3) }
   return $med
}

# ---------------------------------------------------------------- corpus
$listFile = Join-Path $here 'perf-corpus.txt'
$sources = Get-Content -LiteralPath $listFile | Where-Object { $_.Trim() -and -not $_.Trim().StartsWith('#') } | ForEach-Object { $_.Trim() }
$patches = @()
foreach ($src in $sources) {
   if (-not (Test-Path -LiteralPath $src)) { Write-Host "perf: corpus patch missing, skipped: $src"; continue }
   $name = [IO.Path]::GetFileNameWithoutExtension($src)
   $dest = Join-Path $corpusDir $name
   $destInf = Join-Path $dest ([IO.Path]::GetFileName($src))
   if (-not (Test-Path -LiteralPath $destInf) -or (Get-Item -LiteralPath $src).LastWriteTime -gt (Get-Item -LiteralPath $destInf).LastWriteTime) {
      New-Item -ItemType Directory -Force -Path $dest | Out-Null
      Copy-Item -LiteralPath $src -Destination $destInf -Force
      $media = Join-Path (Split-Path -Parent $src) ($name + '_media')
      if (Test-Path -LiteralPath $media) { Copy-Item -LiteralPath $media -Destination $dest -Recurse -Force }
   }
   $patches += [pscustomobject]@{ Name = $name; Path = $destInf }
}
if ($PatchFiles) {
   $patches = @($PatchFiles | ForEach-Object { $_ -split ',' } | Where-Object { $_ } | ForEach-Object {
      $full = (Resolve-Path -LiteralPath $_).Path
      [pscustomobject]@{ Name = [IO.Path]::GetFileNameWithoutExtension($full); Path = $full }
   })
}
if ($patches.Count -eq 0) { Write-Host 'perf: empty corpus'; exit 1 }

# The discrete GPU the app renders on: NVIDIA first (laptops also list the integrated GPU).
$gpus = @(Get-CimInstance Win32_VideoController | ForEach-Object { $_.Name })
$gpuName = @($gpus | Where-Object { $_ -match 'NVIDIA' }) + @($gpus | Where-Object { $_ -notmatch 'NVIDIA' }) | Select-Object -First 1
Say ("Infinite-Turbo perf  {0}  gpu={1}  runs={2}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm'), $gpuName, $Runs)
Say "  new: $Exe"
if ($RefExe) { Say "  ref: $RefExe (interleaved)" }

# ---------------------------------------------------------------- runs
$results = [ordered]@{}
$refResults = [ordered]@{}
foreach ($p in $patches) {
   $samples = @{}
   $refSamples = @{}
   for ($r = 1; $r -le $Runs; $r++) {
      if ($RefExe) { Add-Samples $refSamples (Measure-Patch $RefExe $p $r 'ref') }
      Add-Samples $samples (Measure-Patch $Exe $p $r 'new')
   }
   $results[$p.Name] = Get-Medians $samples
   if ($RefExe) { $refResults[$p.Name] = Get-Medians $refSamples }
}

$doc = [ordered]@{ date = (Get-Date -Format 's'); gpu = $gpuName; exe = $Exe; runs = $Runs; frames = $Frames; patches = $results }
if ($RefExe) { $doc['refExe'] = $RefExe; $doc['refPatches'] = $refResults }
$doc | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $work 'results.json') -Encoding UTF8

if ($UpdateBaseline) {
   $b = [ordered]@{ date = $doc.date; gpu = $gpuName; exe = $Exe; runs = $Runs; frames = $Frames; patches = $results }
   $b | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $baselinePath -Encoding UTF8
   Say "baseline written: $baselinePath"
}

# ---------------------------------------------------------------- compare
$regressions = @()
$gating = $true
$basePatches = $null
if ($RefExe) { $basePatches = $refResults }
elseif (-not $UpdateBaseline) {
   if (-not (Test-Path -LiteralPath $baselinePath)) { Say 'no perf-baseline.json yet: run with -UpdateBaseline or pass -RefExe'; $gating = $false }
   else {
      $base = Get-Content -LiteralPath $baselinePath -Raw | ConvertFrom-Json
      if ($base.gpu -ne $gpuName) { Say "baseline GPU '$($base.gpu)' differs from '$gpuName': reported, not gated"; $gating = $false }
      $basePatches = @{}
      foreach ($prop in $base.patches.PSObject.Properties) {
         $h = @{}
         foreach ($q in $prop.Value.PSObject.Properties) { $h[$q.Name] = $q.Value }
         $basePatches[$prop.Name] = $h
      }
      Say '  (vs perf-baseline.json: a trend only, sessions drift ~20%; gate with -RefExe)'
   }
}
if ($basePatches) {
   foreach ($pname in $results.Keys) {
      $bp = $basePatches[$pname]
      if (-not $bp) { Say "  [new]   $pname has no reference"; continue }
      Say "  $pname"
      foreach ($k in $results[$pname].Keys) {
         if (-not $bp.Contains($k)) { continue }
         $nv = [double]$results[$pname][$k]
         $bv = [double]$bp[$k]
         # Per-node CPU ms swing up to ~70% between identical runs (scheduling); only the
         # node GPU times are stable enough to gate.
         $gated = ($k -in @('frameMs', 'cpu.total', 'gpu.total', 'render3d')) -or ($k -like 'node.*.gpu' -and $bv -ge $FloorMs)
         if (-not $gated) { continue }
         $delta = $nv - $bv
         $pct = if ($bv -gt 0) { 100.0 * $delta / $bv } else { 0.0 }
         $bad = ($delta -gt $FloorMs) -and ($nv -gt $bv * (1.0 + $Threshold))
         $mark = if ($bad) { 'WORSE' } elseif ($delta -lt -$FloorMs -and $nv -lt $bv * (1.0 - $Threshold)) { 'better' } else { '' }
         Say ("    {0,-40} {1,8:N2} -> {2,8:N2} ms  {3,6:N1}%  {4}" -f $k, $bv, $nv, $pct, $mark)
         if ($bad) { $regressions += "$pname $k" }
      }
   }
}

Say '== Perf summary'
if ($script:broken) { Say ('broken runs: ' + ($script:broken -join ', ')) }
$measured = @($results.Values | Where-Object { $_.Count -gt 0 }).Count
if ($measured -eq 0) { Say 'nothing was measured'; exit 1 }
if ($regressions -and $gating) { Say ('regressions (> {0:P0} and > {1} ms): {2}' -f $Threshold, $FloorMs, ($regressions -join ', ')) }
if ($script:broken -or ($regressions -and $gating)) { exit 1 }
Say 'perf ok.'
exit 0
