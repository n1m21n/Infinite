@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
title Infinite - compilacao Windows x64

call :refresh_path

if not defined VCPKG_ROOT set "VCPKG_ROOT=%LOCALAPPDATA%\InfiniteBuild\vcpkg"
if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
  echo ERRO: vcpkg nao encontrado. Execute install-dependencies.bat primeiro.
  exit /b 1
)
set "INFINITE_VCPKG_ROOT=%VCPKG_ROOT%"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo ERRO: Visual Studio Build Tools 2022 nao encontrado.
  exit /b 1
)
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT (
  echo ERRO: workload C++ x64 nao encontrado no Visual Studio.
  exit /b 1
)
call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1

rem VsDevCmd define VCPKG_ROOT para a copia interna do Visual Studio. Restaura
rem a instancia completa preparada por install-dependencies.bat.
set "VCPKG_ROOT=!INFINITE_VCPKG_ROOT!"
if not exist "!VCPKG_ROOT!\scripts\buildsystems\vcpkg.cmake" (
  echo ERRO: o vcpkg preparado pelo instalador nao foi localizado em "!VCPKG_ROOT!".
  exit /b 1
)
echo vcpkg localizado: !VCPKG_ROOT!

set "CMAKE_EXE="
for /f "delims=" %%I in ('where cmake.exe 2^>nul') do if not defined CMAKE_EXE set "CMAKE_EXE=%%I"
if not defined CMAKE_EXE if exist "%ProgramFiles%\CMake\bin\cmake.exe" set "CMAKE_EXE=%ProgramFiles%\CMake\bin\cmake.exe"
if not defined CMAKE_EXE if exist "%LOCALAPPDATA%\Programs\CMake\bin\cmake.exe" set "CMAKE_EXE=%LOCALAPPDATA%\Programs\CMake\bin\cmake.exe"
if not defined CMAKE_EXE if exist "!VSROOT!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=!VSROOT!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE_EXE (
  echo ERRO: cmake.exe nao encontrado. Execute install-dependencies.bat novamente.
  exit /b 1
)
echo CMake localizado: !CMAKE_EXE!

rem setx so aparece automaticamente em terminais novos. Releia a raiz do
rem Windows ML e tambem aceite o caminho padrao usado pelo instalador.
if not defined INFINITE_WINDOWSML_ROOT for /f "tokens=2,*" %%A in ('reg query "HKCU\Environment" /v INFINITE_WINDOWSML_ROOT 2^>nul ^| findstr /i "INFINITE_WINDOWSML_ROOT"') do set "INFINITE_WINDOWSML_ROOT=%%B"
if not defined INFINITE_WINDOWSML_ROOT set "INFINITE_WINDOWSML_ROOT=%LOCALAPPDATA%\InfiniteBuild\windowsml\Microsoft.Windows.AI.MachineLearning.2.2.12"
if not exist "!INFINITE_WINDOWSML_ROOT!\build\cmake" (
  echo ERRO: Windows ML nao foi localizado em "!INFINITE_WINDOWSML_ROOT!".
  echo Execute install-dependencies.bat novamente.
  exit /b 1
)
echo Windows ML + DirectML localizado: !INFINITE_WINDOWSML_ROOT!

rem Nao deixa um executavel de uma compilacao antiga parecer ser o resultado
rem de uma compilacao que falhou. O pacote sera recriado somente depois de o
rem MSBuild concluir com sucesso.
echo Invalidando executaveis anteriores...
if exist "%CD%\build\windows-vs2022\Release\Infinite.exe" del /q "%CD%\build\windows-vs2022\Release\Infinite.exe"
if exist "%CD%\dist\Infinite-Windows-x64\Infinite.exe" del /q "%CD%\dist\Infinite-Windows-x64\Infinite.exe"
if exist "%CD%\dist\Infinite-Windows-x64.zip" del /q "%CD%\dist\Infinite-Windows-x64.zip"
if exist "%CD%\build\windows-vs2022\Release\Infinite.exe" (
  echo ERRO: feche o Infinite.exe antes de compilar.
  exit /b 1
)

if exist ".git" (
  git submodule update --init --recursive
  if errorlevel 1 exit /b 1
)

echo Configurando o projeto...
"!CMAKE_EXE!" --preset windows-vs2022 --fresh -DCMAKE_TOOLCHAIN_FILE="!VCPKG_ROOT!\scripts\buildsystems\vcpkg.cmake"
if errorlevel 1 exit /b 1

echo Compilando Release...
"!CMAKE_EXE!" --build --preset windows-release
if errorlevel 1 exit /b 1

echo Gerando pacote redistribuivel...
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%CD%\scripts\package-windows.ps1"
if errorlevel 1 exit /b 1

echo.
echo Compilacao concluida: dist\Infinite-Windows-x64\Infinite.exe
echo Pacote ZIP: dist\Infinite-Windows-x64.zip
exit /b 0

:refresh_path
set "MACHINE_PATH="
set "USER_PATH="
for /f "usebackq delims=" %%P in (`powershell.exe -NoProfile -Command "[Environment]::GetEnvironmentVariable('Path','Machine')"`) do set "MACHINE_PATH=%%P"
for /f "usebackq delims=" %%P in (`powershell.exe -NoProfile -Command "[Environment]::GetEnvironmentVariable('Path','User')"`) do set "USER_PATH=%%P"
if defined MACHINE_PATH set "PATH=!MACHINE_PATH!;!PATH!"
if defined USER_PATH set "PATH=!USER_PATH!;!PATH!"
set "PATH=%ProgramFiles%\CMake\bin;%LOCALAPPDATA%\Programs\CMake\bin;%LOCALAPPDATA%\Microsoft\WinGet\Links;!PATH!"
exit /b 0
