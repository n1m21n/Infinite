@echo off
setlocal EnableExtensions
title Infinite - Windows x64

set "INFINITE_EXE="
if exist "%~dp0Infinite.exe" set "INFINITE_EXE=%~dp0Infinite.exe"
if not defined INFINITE_EXE if exist "%~dp0dist\Infinite-Windows-x64\Infinite.exe" set "INFINITE_EXE=%~dp0dist\Infinite-Windows-x64\Infinite.exe"
if not defined INFINITE_EXE if exist "%~dp0build\windows-vs2022\Release\Infinite.exe" set "INFINITE_EXE=%~dp0build\windows-vs2022\Release\Infinite.exe"

if not defined INFINITE_EXE (
  echo ERRO: Infinite.exe nao encontrado.
  echo Execute build-windows.bat primeiro ou coloque este arquivo ao lado de Infinite.exe.
  pause
  exit /b 1
)

for %%I in ("%INFINITE_EXE%") do set "INFINITE_DIR=%%~dpI"
pushd "%INFINITE_DIR%"
echo Iniciando: %INFINITE_EXE%
echo.
"%INFINITE_EXE%" %*
set "INFINITE_EXIT=%ERRORLEVEL%"
popd

echo.
echo Infinite foi encerrado. Codigo de saida: %INFINITE_EXIT%
if not "%INFINITE_EXIT%"=="0" (
  echo Execute diagnose-windows.bat e envie o arquivo Infinite-diagnostic.log.
)
pause
exit /b %INFINITE_EXIT%
