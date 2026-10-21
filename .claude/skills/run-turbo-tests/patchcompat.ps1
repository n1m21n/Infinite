<#
Patch compatibility run (Turbo 0.60). Opens every .inf under -Corpus in -Exe with the
INFINITE_PATCHCOMPAT probe (src/app/selftest/PatchCompat.inl) and writes, per patch,
<Out>\<name>.json (node summaries, unknown types, unknown param keys) plus
<Out>\status.csv (patch, exit code, verdict: ok / crash / hang / nojson).

   powershell -ExecutionPolicy Bypass -File patchcompat.ps1 -Exe <Infinite-Turbo.exe> -Corpus <dir> -Out <dir>

Compare two runs (a reference build and a candidate) with compare-patchcompat.py.
#>
param(
   [Parameter(Mandatory = $true)][string]$Exe,
   [Parameter(Mandatory = $true)][string]$Corpus,
   [Parameter(Mandatory = $true)][string]$Out,
   [int]$Frames = 20,
   [int]$TimeoutSec = 60
)
$ErrorActionPreference = 'Continue'
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$status = Join-Path $Out 'status.csv'
'patch,exit,verdict' | Set-Content -LiteralPath $status -Encoding UTF8
$patches = Get-ChildItem -LiteralPath $Corpus -Filter *.inf -Recurse | Sort-Object FullName
$i = 0
foreach ($p in $patches) {
   $i++
   $name = [IO.Path]::GetFileNameWithoutExtension($p.Name)
   $json = Join-Path $Out "$name.json"
   $log = Join-Path $Out "$name.log"
   if (Test-Path -LiteralPath $json) { Remove-Item -LiteralPath $json -Force }
   $psi = New-Object Diagnostics.ProcessStartInfo
   $psi.FileName = $Exe
   $psi.WorkingDirectory = Split-Path -Parent $Exe
   $psi.UseShellExecute = $false
   $psi.RedirectStandardOutput = $true
   $psi.RedirectStandardError = $true
   $psi.EnvironmentVariables['INFINITE_LOADPATCH'] = $p.FullName
   $psi.EnvironmentVariables['INFINITE_PATCHCOMPAT'] = $json
   $psi.EnvironmentVariables['INFINITE_PATCHCOMPAT_FRAME'] = "$Frames"
   $psi.EnvironmentVariables['INFINITE_EXITAFTER'] = "$($Frames + 6)"
   $proc = [Diagnostics.Process]::Start($psi)
   $stdout = $proc.StandardOutput.ReadToEndAsync()
   $stderr = $proc.StandardError.ReadToEndAsync()
   $verdict = 'ok'
   if (-not $proc.WaitForExit($TimeoutSec * 1000)) { try { $proc.Kill() } catch {}; $verdict = 'hang' }
   $code = if ($verdict -eq 'hang') { 'n/a' } else { $proc.ExitCode }
   ($stdout.Result + "`n--- stderr ---`n" + $stderr.Result) | Set-Content -LiteralPath $log -Encoding UTF8
   if ($verdict -eq 'ok' -and $code -ne 0) { $verdict = 'crash' }
   if ($verdict -eq 'ok' -and -not (Test-Path -LiteralPath $json)) { $verdict = 'nojson' }
   "$name,$code,$verdict" | Add-Content -LiteralPath $status -Encoding UTF8
   Write-Host ("[{0}/{1}] {2} {3}" -f $i, $patches.Count, $verdict, $name)
}
