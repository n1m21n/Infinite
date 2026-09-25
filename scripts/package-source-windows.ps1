$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$distRoot = Join-Path $root 'dist'
$stage = Join-Path $distRoot 'Infinite-Turbo-Windows-Source'
$zip = Join-Path $distRoot 'Infinite-Turbo-Windows-Source.zip'

if (Test-Path -LiteralPath $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}
if (Test-Path -LiteralPath $zip) {
    Remove-Item -LiteralPath $zip -Force
}
New-Item -ItemType Directory -Path $stage -Force | Out-Null

$rootFiles = @(
    '.gitignore',
    'ARCHITECTURE.md',
    'CHANGELOG-TURBO.md',
    'CMakeLists.txt',
    'CMakePresets.json',
    'LICENSE',
    'PACKAGE_MANIFEST.txt',
    'README.md',
    'WINDOWS_BUILD.md',
    'build-windows.bat',
    'diagnose-windows.bat',
    'install-dependencies.bat',
    'install-runtime.bat',
    'run-windows.bat',
    'test-windows.bat',
    'vcpkg.json'
)
foreach ($relative in $rootFiles) {
    $source = Join-Path $root $relative
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Arquivo obrigatorio ausente: $relative"
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $stage $relative)
}

foreach ($relative in @('src', 'cmake', 'scripts')) {
    Copy-Item -LiteralPath (Join-Path $root $relative) -Destination $stage -Recurse
}

$workflowDir = Join-Path $stage '.github\workflows'
New-Item -ItemType Directory -Path $workflowDir -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root '.github\workflows\windows-build.yml') -Destination $workflowDir

$assetDir = Join-Path $stage 'assets'
New-Item -ItemType Directory -Path $assetDir -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'assets\Infinite.ico') -Destination $assetDir
Copy-Item -LiteralPath (Join-Path $root 'assets\fonts') -Destination $assetDir -Recurse
Copy-Item -LiteralPath (Join-Path $root 'assets\examples') -Destination $assetDir -Recurse

$docsDir = Join-Path $stage 'docs'
New-Item -ItemType Directory -Path $docsDir -Force | Out-Null
foreach ($relative in @('CODE_STANDARDS.md', 'screenshot.png')) {
    $source = Join-Path $root (Join-Path 'docs' $relative)
    if (Test-Path -LiteralPath $source) {
        Copy-Item -LiteralPath $source -Destination $docsDir
    }
}

$externalDir = Join-Path $stage 'external'
New-Item -ItemType Directory -Path $externalDir -Force | Out-Null
foreach ($name in @('imgui', 'imgui-node-editor', 'json', 'shine', 'stb')) {
    Copy-Item -LiteralPath (Join-Path $root (Join-Path 'external' $name)) -Destination $externalDir -Recurse
}

@"
This source archive is prepared for a Windows GitHub delivery.

Intentionally omitted because they are not consumed by the Windows build:
- build, dist and vcpkg_installed output
- external/vst3sdk (Windows VST3 hosting is supplied by JUCE)
- historical PATCH-R8 through PATCH-R31 files and apply-r22.bat
- promotional PDFs, website output, crash reports and large demo videos
- editor-specific .claude state

The future compiled release must be attached separately as:
dist\Infinite-Turbo-Windows-x64.zip
"@ | Set-Content -LiteralPath (Join-Path $stage 'GITHUB_DELIVERY.md') -Encoding UTF8

Compress-Archive -LiteralPath $stage -DestinationPath $zip -CompressionLevel Optimal
Write-Host "Source package created: $zip"
