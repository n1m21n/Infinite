$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
# Turbo 0.53: build-windows.bat says which build to pack (MIT or full) through
# INFINITE_BUILD_DIR / INFINITE_DIST_NAME / INFINITE_EDITION.
$buildRel = if ($env:INFINITE_BUILD_DIR) { $env:INFINITE_BUILD_DIR } else { 'build\windows-vs2022' }
$distName = if ($env:INFINITE_DIST_NAME) { $env:INFINITE_DIST_NAME } else { 'Infinite-Turbo-Windows-x64' }
$edition = if ($env:INFINITE_EDITION) { $env:INFINITE_EDITION } else { 'full (GPL modules)' }
$exe = Join-Path $root (Join-Path $buildRel 'Release\Infinite-Turbo.exe')
$scanner = Join-Path $root (Join-Path $buildRel 'Release\infinite-vst3-scanner.exe')
$distRoot = Join-Path $root 'dist'
$package = Join-Path $distRoot $distName
$zip = Join-Path $distRoot ($distName + '.zip')

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
Copy-Item -LiteralPath (Join-Path $root 'THIRD_PARTY_NOTICES.md') -Destination $package
if ($edition -ne 'MIT') {
    Copy-Item -LiteralPath (Join-Path $root 'src\gpl\LICENSE') -Destination (Join-Path $package 'LICENSE-GPL-3.0.txt')
}
Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination $package
Copy-Item -LiteralPath (Join-Path $root 'WINDOWS_BUILD.md') -Destination $package
Copy-Item -LiteralPath (Join-Path $root 'CHANGELOG-TURBO.md') -Destination $package
Copy-Item -LiteralPath (Join-Path $root 'PACKAGE_MANIFEST.txt') -Destination $package
Copy-Item -LiteralPath (Join-Path $root 'run-windows.bat') -Destination $package
Copy-Item -LiteralPath (Join-Path $root 'diagnose-windows.bat') -Destination $package
Copy-Item -LiteralPath (Join-Path $root 'setup-mcp.bat') -Destination $package
Copy-Item -LiteralPath (Join-Path $root 'install-runtime.bat') -Destination $package

$fontTarget = Join-Path $package 'assets\fonts'
New-Item -ItemType Directory -Path $fontTarget -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'assets\fonts\IBMPlexSans-Regular.ttf') -Destination $fontTarget
Copy-Item -LiteralPath (Join-Path $root 'assets\fonts\OFL.txt') -Destination $fontTarget
Copy-Item -LiteralPath (Join-Path $root 'assets\fonts\Inter-Regular.ttf') -Destination $fontTarget
Copy-Item -LiteralPath (Join-Path $root 'assets\fonts\OFL-Inter.txt') -Destination $fontTarget

# Turbo 0.46: the example patch the app opens with.
$exampleTarget = Join-Path $package 'assets\examples'
New-Item -ItemType Directory -Path $exampleTarget -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'assets\examples\superSynthMCP.inf') -Destination $exampleTarget
# Turbo 0.47: drum kit for the Drum Sequencer pattern library
Copy-Item -LiteralPath (Join-Path $root 'assets\drumkits') -Destination (Join-Path $package 'assets') -Recurse -Force
# Turbo 0.54: MIDI pattern packs (both builds); SLOOP kits only in the full build (GPL material).
Copy-Item -LiteralPath (Join-Path $root 'assets\note-patterns') -Destination (Join-Path $package 'assets') -Recurse -Force
if (Test-Path -LiteralPath (Join-Path $root 'assets\drum-patterns')) {
    Copy-Item -LiteralPath (Join-Path $root 'assets\drum-patterns') -Destination (Join-Path $package 'assets') -Recurse -Force
}
if ($edition -ne 'MIT') {
    Copy-Item -LiteralPath (Join-Path $root 'assets\drumkits-gpl') -Destination (Join-Path $package 'assets') -Recurse -Force
}

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

# Versao lida do CMakeLists.txt (project(... VERSION x.y.z)), para o pacote
# nunca ficar com um numero velho escrito a mao.
$version = 'desconhecida'
$cmakeText = Get-Content -LiteralPath (Join-Path $root 'CMakeLists.txt') -Raw
if ($cmakeText -match 'project\(InfiniteTurbo VERSION ([0-9.]+)') { $version = "$($Matches[1])-turbo" }

$commit = 'pacote sem metadados Git'
try { $commit = (git -C $root rev-parse HEAD).Trim() } catch {}
@"
Infinite-Turbo (for Windows) x64
Versao: $version
Origem: https://github.com/n1m21n/Infinite
Commit: $commit
Edicao: $edition
Configuracao: Release, VST3 ON, Spout ON, x64-windows-static
Remove Background: Windows ML + DirectML/DX12, com fallback OpenCV CPU
"@ | Set-Content -LiteralPath (Join-Path $package 'VERSAO.txt') -Encoding UTF8

if (Test-Path -LiteralPath $zip) {
    Remove-Item -LiteralPath $zip -Force
}
Compress-Archive -Path (Join-Path $package '*') -DestinationPath $zip -CompressionLevel Optimal
Write-Host "Pacote criado: $zip"
