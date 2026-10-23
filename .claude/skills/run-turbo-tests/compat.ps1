<#
Patch compatibility gate (Turbo 0.70, C3). Stages the corpus listed in compat-corpus.txt into
build/compat/corpus (each patch with its "<name>_media" sibling folder, numbered so equal names
do not collide), runs patchcompat.ps1 with -Exe, and compares with the reference results.

   powershell -ExecutionPolicy Bypass -File compat.ps1 [-Exe <exe>] [-RecordRef]

-RecordRef writes the reference results (build/compat/ref) with -Exe: do it once with the
reference build (v0.62.0 for turbo/next) before changing the code. Without it, results go to
build/compat/new and compare-patchcompat.py --gate writes build/compat/report.md.
Exit code: 0 ok, 1 when a patch went from ok to crash / hang / nojson.
#>
param(
   [string]$Exe,
   [switch]$RecordRef,
   [int]$TimeoutSec = 60
)
$ErrorActionPreference = 'Continue'
$here = $PSScriptRoot
$root = (Resolve-Path (Join-Path $here '../../..')).Path
if (-not $Exe) { $Exe = Join-Path $root 'build/windows-vs2022/Release/Infinite-Turbo.exe' }
$Exe = (Resolve-Path $Exe).Path
$work = Join-Path $root 'build/compat'
$corpus = Join-Path $work 'corpus'
$ref = Join-Path $work 'ref'
$out = if ($RecordRef) { $ref } else { Join-Path $work 'new' }

# Stage the corpus fresh every run (the list or the patches may have changed).
if (Test-Path -LiteralPath $corpus) { Remove-Item -LiteralPath $corpus -Recurse -Force }
New-Item -ItemType Directory -Force -Path $corpus | Out-Null
$sources = Get-Content -LiteralPath (Join-Path $here 'compat-corpus.txt') |
   Where-Object { $_.Trim() -and -not $_.Trim().StartsWith('#') } | ForEach-Object { $_.Trim() }
$files = @()
foreach ($s in $sources) {
   $path = if ([IO.Path]::IsPathRooted($s)) { $s } else { Join-Path $root $s }
   if (-not (Test-Path -LiteralPath $path)) { Write-Host "compat: missing, skipped: $s"; continue }
   if ((Get-Item -LiteralPath $path).PSIsContainer) {
      $files += Get-ChildItem -LiteralPath $path -Filter *.inf -Recurse
   } else { $files += Get-Item -LiteralPath $path }
}
$n = 0
foreach ($f in ($files | Sort-Object FullName -Unique)) {
   $n++
   $name = [IO.Path]::GetFileNameWithoutExtension($f.Name)
   $dest = Join-Path $corpus ('{0:D3}_{1}' -f $n, ($name -replace '[^A-Za-z0-9_+-]', '_'))
   New-Item -ItemType Directory -Force -Path $dest | Out-Null
   # Numbered file name: patchcompat names its outputs after the patch, and the corpus has
   # equal names in different folders. Media paths stay valid (relative to the folder).
   Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $dest ((Split-Path -Leaf $dest) + '.inf'))
   $media = Join-Path $f.DirectoryName ($name + '_media')
   if (Test-Path -LiteralPath $media) { Copy-Item -LiteralPath $media -Destination $dest -Recurse }
}
Write-Host "compat: $n patches staged in $corpus"

if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Recurse -Force }
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'patchcompat.ps1') -Exe $Exe -Corpus $corpus -Out $out -TimeoutSec $TimeoutSec
if ($RecordRef) { Write-Host "compat: reference recorded in $ref"; exit 0 }
if (-not (Test-Path -LiteralPath (Join-Path $ref 'status.csv'))) { Write-Host 'compat: no reference yet, run with -RecordRef on the reference build'; exit 1 }
& python -I (Join-Path $here 'compare-patchcompat.py') $ref $out (Join-Path $work 'report.md') --gate
exit $LASTEXITCODE
