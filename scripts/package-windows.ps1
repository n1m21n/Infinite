$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'build\windows-vs2022\Release\Infinite-Turbo.exe'
$scanner = Join-Path $root 'build\windows-vs2022\Release\infinite-vst3-scanner.exe'
$distRoot = Join-Path $root 'dist'
$package = Join-Path $distRoot 'Infinite-Turbo-Windows-x64'
$zip = Join-Path $distRoot 'Infinite-Turbo-Windows-x64.zip'

if (-not (Test-Path -LiteralPath $exe)) {
    throw "Executavel nao encontrado: $exe"
}
if (-not (Test-Path -LiteralPath $scanner)) {
    throw "Scanner VST3 nao encontrado: $scanner"
}

if (Test-Path -LiteralPath $package) {
    Remove-Item -LiteralPath $package -Recurse -Force
}
New-Item -ItemType Directory -Path $package -Force | Out-Null

Copy-Item -LiteralPath $exe -Destination $package
Copy-Item -LiteralPath $scanner -Destination $package
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination $package
Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination $package
Copy-Item -LiteralPath (Join-Path $root 'WINDOWS_BUILD.md') -Destination $package
Copy-Item -LiteralPath (Join-Path $root 'CHANGELOG-TURBO.md') -Destination $package
Copy-Item -LiteralPath (Join-Path $root 'PACKAGE_MANIFEST.txt') -Destination $package
Copy-Item -LiteralPath (Join-Path $root 'run-windows.bat') -Destination $package
Copy-Item -LiteralPath (Join-Path $root 'diagnose-windows.bat') -Destination $package
Copy-Item -LiteralPath (Join-Path $root 'install-runtime.bat') -Destination $package

$fontTarget = Join-Path $package 'assets\fonts'
New-Item -ItemType Directory -Path $fontTarget -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'assets\fonts\IBMPlexSans-Regular.ttf') -Destination $fontTarget
Copy-Item -LiteralPath (Join-Path $root 'assets\fonts\OFL.txt') -Destination $fontTarget

# Windows ML is self-contained: keep ONNX Runtime and DirectML next to the EXE.
Get-ChildItem -LiteralPath (Split-Path -Parent $exe) -Filter '*.dll' -File |
    Copy-Item -Destination $package
foreach ($runtimeName in @('onnxruntime.dll', 'DirectML.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $package $runtimeName))) {
        throw "Runtime Windows ML ausente do build: $runtimeName"
    }
}

$modelSources = @(
    (Join-Path $env:LOCALAPPDATA 'Infinite\models\u2net.onnx'),
    (Join-Path $env:LOCALAPPDATA 'Infinite\models\u2net_human_seg.onnx')
)
foreach ($modelSource in $modelSources) {
  if (Test-Path -LiteralPath $modelSource) {
    $modelTarget = Join-Path $package 'models'
    New-Item -ItemType Directory -Path $modelTarget -Force | Out-Null
    Copy-Item -LiteralPath $modelSource -Destination $modelTarget
  }
}

$ffmpeg = (Get-Command ffmpeg.exe -ErrorAction SilentlyContinue).Source
if ($ffmpeg) {
    Copy-Item -LiteralPath $ffmpeg -Destination $package
}

$commit = 'pacote sem metadados Git'
try { $commit = (git -C $root rev-parse HEAD).Trim() } catch {}
@"
Infinite-Turbo (for Windows) x64
Versao: 0.32.0-turbo (base R31A)
Origem: https://github.com/n1m21n/Infinite
Commit-base: $commit
Configuracao: Release, VST3 ON, Spout ON, x64-windows-static
Remove Background: Windows ML + DirectML/DX12, com fallback OpenCV CPU
"@ | Set-Content -LiteralPath (Join-Path $package 'VERSAO.txt') -Encoding UTF8

if (Test-Path -LiteralPath $zip) {
    Remove-Item -LiteralPath $zip -Force
}
Compress-Archive -Path (Join-Path $package '*') -DestinationPath $zip -CompressionLevel Optimal
Write-Host "Pacote criado: $zip"
