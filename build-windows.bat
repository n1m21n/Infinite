@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
title Infinite-Turbo - compilacao Windows x64

rem Uso: build-windows.bat [debug] [fresh] [pack]
rem   debug   compila Debug em vez de Release
rem   fresh   reconfigura do zero (apaga o cache do CMake; use depois de trocar
rem           dependencias ou quando o CMake reclamar de cache antigo)
rem   pack    alem de compilar, gera dist\Infinite-Turbo-Windows-x64 e o ZIP
rem           (por padrao so compila: o exe de teste fica em build\...\Release)
set "CONFIG=Release"
set "BUILD_PRESET=windows-release"
set "FRESH="
set "PACK="
for %%A in (%*) do (
  if /I "%%~A"=="debug" (set "CONFIG=Debug" & set "BUILD_PRESET=windows-debug")
  if /I "%%~A"=="fresh" set "FRESH=--fresh"
  if /I "%%~A"=="pack" set "PACK=1"
  if /I "%%~A"=="nopack" set "PACK="
)
set "BUILD_DIR=%CD%\build\windows-vs2022"
set "EXE_NAME=Infinite-Turbo.exe"
set "DIST_NAME=Infinite-Turbo-Windows-x64"

call :refresh_path

if not defined VCPKG_ROOT set "VCPKG_ROOT=%LOCALAPPDATA%\InfiniteBuild\vcpkg"
if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
  echo ERRO: vcpkg nao encontrado. Execute install-dependencies.bat primeiro.
  exit /b 1
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo ERRO: Visual Studio Build Tools 2022 nao encontrado. Execute install-dependencies.bat.
  exit /b 1
)
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT (
  echo ERRO: workload C++ x64 nao encontrado no Visual Studio.
  exit /b 1
)
rem Nao chama VsDevCmd: o gerador "Visual Studio 17 2022" do CMake localiza o
rem MSVC sozinho, e o VsDevCmd enchia o PATH ate estourar o limite do cmd.
echo Visual Studio: !VSROOT!
echo vcpkg: !VCPKG_ROOT!

set "CMAKE_EXE="
for /f "delims=" %%I in ('where cmake.exe 2^>nul') do if not defined CMAKE_EXE set "CMAKE_EXE=%%I"
if not defined CMAKE_EXE if exist "%ProgramFiles%\CMake\bin\cmake.exe" set "CMAKE_EXE=%ProgramFiles%\CMake\bin\cmake.exe"
if not defined CMAKE_EXE if exist "%LOCALAPPDATA%\Programs\CMake\bin\cmake.exe" set "CMAKE_EXE=%LOCALAPPDATA%\Programs\CMake\bin\cmake.exe"
if not defined CMAKE_EXE if exist "!VSROOT!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=!VSROOT!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE_EXE (
  echo ERRO: cmake.exe nao encontrado. Execute install-dependencies.bat novamente.
  exit /b 1
)
echo CMake: !CMAKE_EXE!

rem setx so aparece em terminais novos: rele a raiz do Windows ML do registro
rem e aceita o caminho padrao usado pelo instalador.
if not defined INFINITE_WINDOWSML_ROOT for /f "tokens=2,*" %%A in ('reg query "HKCU\Environment" /v INFINITE_WINDOWSML_ROOT 2^>nul ^| findstr /i "INFINITE_WINDOWSML_ROOT"') do set "INFINITE_WINDOWSML_ROOT=%%B"
if not defined INFINITE_WINDOWSML_ROOT set "INFINITE_WINDOWSML_ROOT=%LOCALAPPDATA%\InfiniteBuild\windowsml\Microsoft.Windows.AI.MachineLearning.2.2.12"
if not exist "!INFINITE_WINDOWSML_ROOT!\build\cmake" (
  echo ERRO: Windows ML nao foi localizado em "!INFINITE_WINDOWSML_ROOT!".
  echo Execute install-dependencies.bat novamente.
  exit /b 1
)
echo Windows ML + DirectML: !INFINITE_WINDOWSML_ROOT!

rem Um executavel antigo nao pode parecer resultado de uma compilacao que falhou.
tasklist /FI "IMAGENAME eq %EXE_NAME%" 2>nul | find /I "%EXE_NAME%" >nul
if not errorlevel 1 (
  echo ERRO: feche o %EXE_NAME% antes de compilar.
  exit /b 1
)
if exist "%BUILD_DIR%\%CONFIG%\%EXE_NAME%" del /q "%BUILD_DIR%\%CONFIG%\%EXE_NAME%"

rem Configura so quando necessario: primeira vez, "fresh", ou cache ausente.
set "NEED_CONFIGURE="
if defined FRESH set "NEED_CONFIGURE=1"
if not exist "%BUILD_DIR%\CMakeCache.txt" set "NEED_CONFIGURE=1"
if defined NEED_CONFIGURE (
  echo Configurando o projeto...
  "!CMAKE_EXE!" --preset windows-vs2022 !FRESH! -DCMAKE_TOOLCHAIN_FILE="!VCPKG_ROOT!\scripts\buildsystems\vcpkg.cmake"
  if errorlevel 1 exit /b 1
)

echo Compilando %CONFIG% (todos os nucleos)...
set "START_TIME=%TIME%"
"!CMAKE_EXE!" --build --preset %BUILD_PRESET% -- /m /nologo /v:minimal
if errorlevel 1 (
  echo.
  echo ERRO: a compilacao falhou. Copie as linhas "error" acima para corrigir.
  exit /b 1
)
echo Inicio: %START_TIME%  Fim: %TIME%

if not defined PACK (
  echo.
  echo Compilacao concluida: build\windows-vs2022\%CONFIG%\%EXE_NAME%
  exit /b 0
)
if /I not "%CONFIG%"=="Release" (
  echo.
  echo Compilacao Debug concluida: build\windows-vs2022\Debug\%EXE_NAME%
  exit /b 0
)

echo Gerando pacote redistribuivel...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%CD%\scripts\package-windows.ps1"
if errorlevel 1 exit /b 1

echo.
echo Compilacao concluida: dist\%DIST_NAME%\%EXE_NAME%
echo Pacote ZIP: dist\%DIST_NAME%.zip
exit /b 0

:refresh_path
rem Rebuilds PATH from the registry (Machine + User) plus the tool folders,
rem without appending the current PATH and without duplicates. Appending made
rem PATH grow past cmd's 8191-character line limit ("The input line is too
rem long") on machines with many tools installed.
set "NEW_PATH="
for /f "usebackq delims=" %%P in (`powershell.exe -NoProfile -Command "$l=New-Object System.Collections.Generic.List[string]; $all=[Environment]::GetEnvironmentVariable('Path','Machine')+';'+[Environment]::GetEnvironmentVariable('Path','User')+';'+$env:SystemRoot+'\System32;'+$env:SystemRoot+';'+$env:SystemRoot+'\System32\WindowsPowerShell\v1.0;'+$env:ProgramFiles+'\Git\cmd;'+$env:LOCALAPPDATA+'\Programs\Git\cmd;'+$env:ProgramFiles+'\CMake\bin;'+$env:LOCALAPPDATA+'\Programs\CMake\bin;'+$env:LOCALAPPDATA+'\Microsoft\WinGet\Links'; foreach($s in $all.Split(';')){ $t=[Environment]::ExpandEnvironmentVariables($s.Trim()).TrimEnd('\'); if($t -and -not $l.Contains($t) -and (Test-Path -LiteralPath $t)){ $l.Add($t) } }; $l -join ';'"`) do set "NEW_PATH=%%P"
if defined NEW_PATH set "PATH=!NEW_PATH!"
set "NEW_PATH="
exit /b 0
