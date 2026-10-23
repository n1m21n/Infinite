<#
Turbo 0.71 (S0): BEFORE / AFTER captures for the Render 3D HQ work.

Plays each patch to frame -Frame (playing, uncapped, as perf.ps1 does) and saves
every node of type -Type as <OutDir>\<patch>_<nodeIndex>.png (upright), plus the
Render 3D frame time FRAMEPROF reports, into <OutDir>\captures.txt.

  capture.ps1 -PatchFiles examples\hq\raster\A_material_studio.inf,examples\hq\raster\F_points_1m.inf
  capture.ps1 -PatchFiles examples\hq\filament -Type "Render 3D HQ" -OutDir build\hq\after

Patches are read in place (they reference ..\hq_media\ next to their folder), never written.
#>
param(
   # Comma-separated (an array does not survive `powershell -File`); a folder takes its *.inf.
   [Parameter(Mandatory = $true)][string]$PatchFiles,
   [string]$Type = 'Render 3D',
   [string]$OutDir = 'build\hq\before',
   [int]$Frame = 150,
   [string]$Exe = ''
)

$root = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
if ($Exe -eq '') { $Exe = Join-Path $root 'build\windows-vs2022\Release\Infinite-Turbo.exe' }
if (-not (Test-Path -LiteralPath $Exe)) { Write-Host "capture: no exe at $Exe"; exit 1 }
$out = Join-Path $root $OutDir
New-Item -ItemType Directory -Force -Path $out | Out-Null
$report = Join-Path $out 'captures.txt'
"capture $(Get-Date -Format 'yyyy-MM-dd HH:mm')  type=$Type  frame=$Frame  exe=$Exe" | Set-Content -LiteralPath $report -Encoding utf8

$vars = @('INFINITE_LOADPATCH', 'INFINITE_PATCHCOMPAT', 'INFINITE_PATCHCOMPAT_PLAY', 'INFINITE_PATCHCOMPAT_FRAME',
          'INFINITE_EXITAFTER', 'INFINITE_DUMPTYPE', 'INFINITE_DUMPPNG', 'INFINITE_FRAMEPROF', 'INFINITE_PERF_UNCAPPED')
$list = @()
foreach ($item in ($PatchFiles -split ',')) {
   $item = $item.Trim()
   if ($item -eq '') { continue }
   if (Test-Path -LiteralPath $item -PathType Container) { $list += (Get-ChildItem -LiteralPath $item -Filter *.inf).FullName }
   else { $list += $item }
}
foreach ($pf in $list) {
   # Back-to-back runs: an instance still shutting down (audio device, scanner)
   # made the next one load its patch with no cables. Wait for it to be gone.
   $waited = 0
   while ((Get-Process -Name 'Infinite-Turbo' -ErrorAction SilentlyContinue) -and $waited -lt 60) {
      Start-Sleep -Milliseconds 500
      $waited++
   }
   Start-Sleep -Milliseconds 500
   $patch = (Resolve-Path -LiteralPath $pf).Path
   $name = [IO.Path]::GetFileNameWithoutExtension($patch)
   $log = Join-Path $out "$name.log"
   $env:INFINITE_LOADPATCH = $patch
   $env:INFINITE_PATCHCOMPAT = Join-Path $out "$name.json"
   $env:INFINITE_PATCHCOMPAT_PLAY = '1'
   $env:INFINITE_PATCHCOMPAT_FRAME = "$Frame"
   $env:INFINITE_EXITAFTER = "$($Frame + 10)"
   $env:INFINITE_DUMPTYPE = $Type
   $env:INFINITE_DUMPPNG = Join-Path $out $name
   $env:INFINITE_FRAMEPROF = '1'
   $env:INFINITE_PERF_UNCAPPED = '1'
   $p = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -NoNewWindow -PassThru `
        -RedirectStandardOutput $log -RedirectStandardError "$log.err"
   $null = $p.Handle
   $done = $p.WaitForExit(240000)
   if (-not $done) { try { $p.Kill() } catch {} }
   foreach ($v in $vars) { Remove-Item "Env:$v" -ErrorAction SilentlyContinue }
   $ms = (Select-String -LiteralPath $log -Pattern 'FRAMEPROF render3d\(incl\. its sources\)=([0-9.]+)ms' |
          Select-Object -Last 1 | ForEach-Object { $_.Matches[0].Groups[1].Value })
   $hqMs = (Select-String -LiteralPath $log -Pattern 'FRAMEPROF renderhq\(incl\. its sources\)=([0-9.]+)ms' |
            Select-Object -Last 1 | ForEach-Object { $_.Matches[0].Groups[1].Value })
   $frameMs = (Select-String -LiteralPath $log -Pattern 'total=([0-9.]+)ms' | Select-Object -Last 1 |
             ForEach-Object { $_.Matches[0].Groups[1].Value })
   $pngs = (Get-ChildItem -LiteralPath $out -Filter "$($name)_*.png" -ErrorAction SilentlyContinue | ForEach-Object { $_.Name }) -join ' '
   $line = '{0,-24} render3d {1,7} ms  hq {2,7} ms  frame {3,7} ms  {4}{5}' -f $name, $ms, $hqMs, $frameMs, $pngs, $(if ($done) { '' } else { '  TIMEOUT' })
   Write-Host $line
   Add-Content -LiteralPath $report -Value $line
}
Write-Host "capture: $report"
