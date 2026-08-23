# Builds a Release Infinite.exe and stages a distributable folder with the
# runtime DLLs it needs (none beyond the OS inbox ones, in practice - the
# build links everything statically except system libraries).
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File package.ps1            # build + stage
#   powershell -ExecutionPolicy Bypass -File package.ps1 -Launch    # ...and run
param(
    [switch]$Launch
)

$ErrorActionPreference = "Stop"

$Root  = Split-Path -Parent $MyInvocation.MyCommand.Path
$Build = Join-Path $Root "build-dist"
$Stage = Join-Path $Root "dist\Infinite"
$Exe   = Join-Path $Build "Release\Infinite.exe"
$ScannerExe = Join-Path $Build "Release\infinite-vst3-scanner.exe"

function Find-VsDevShell {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found - install Visual Studio 2022/2026 with the C++ workload"
    }
    $vsPath = & $vswhere -latest -property installationPath
    if (-not $vsPath) { throw "no Visual Studio installation found" }
    $shell = Join-Path $vsPath "Common7\Tools\Launch-VsDevShell.ps1"
    if (-not (Test-Path $shell)) { throw "Launch-VsDevShell.ps1 not found under $vsPath" }
    return $shell
}

Write-Host "==> configuring"
# The Visual Studio generator needs the MSVC environment (cl, link, rc) on
# PATH, which the dev shell provides; CMake would otherwise fall back to
# whatever toolchain it can find.
& (Find-VsDevShell) -Arch amd64 -SkipAutomaticLocation | Out-Null

# CMake refuses to reconfigure an existing build directory with a different
# generator, so a tree configured elsewhere would fail here. Wiping is safe:
# this directory is only ever a build artifact.
if (Test-Path (Join-Path $Build "CMakeCache.txt")) {
    $gen = Select-String -Path (Join-Path $Build "CMakeCache.txt") `
        -Pattern "CMAKE_GENERATOR:INTERNAL=(.+)" | Select-Object -First 1
    if (-not $gen -or $gen.Matches[0].Groups[1].Value -ne "Visual Studio 17 2022") {
        Write-Host "    generator changed, reconfiguring from scratch"
        Remove-Item -Recurse -Force $Build
    }
}

cmake -S $Root -B $Build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { throw "configure failed" }
# Accept 2026 too when 2022 is not installed.
if (-not (Test-Path $Build)) { throw "configure produced no build directory" }

Write-Host "==> building"
cmake --build $Build --config Release
if ($LASTEXITCODE -ne 0) { throw "build failed" }

Write-Host "==> staging $Stage"
if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
New-Item -ItemType Directory -Force (Split-Path -Parent $Stage) | Out-Null
New-Item -ItemType Directory -Force $Stage | Out-Null
Copy-Item $Exe $Stage
# The out-of-process VST3 scanner (src/scanner_main_win.cpp) must sit next to
# Infinite.exe - Platform::ScannerExecutablePath() looks for it there.
if (Test-Path $ScannerExe) { Copy-Item $ScannerExe $Stage }

Write-Host "==> done: $Stage\Infinite.exe"
if ($Launch) {
    Start-Process (Join-Path $Stage "Infinite.exe")
}
