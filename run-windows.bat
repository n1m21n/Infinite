@echo off
setlocal EnableExtensions
title Infinite-Turbo (for Windows)

rem Uso: run-windows.bat [argumentos do app]
rem Procura o executavel ao lado deste arquivo, depois no pacote dist e no build.
set "EXE_NAME=Infinite-Turbo.exe"
set "INFINITE_EXE="
if exist "%~dp0%EXE_NAME%" set "INFINITE_EXE=%~dp0%EXE_NAME%"
if not defined INFINITE_EXE if exist "%~dp0dist\Infinite-Turbo-Windows-x64\%EXE_NAME%" set "INFINITE_EXE=%~dp0dist\Infinite-Turbo-Windows-x64\%EXE_NAME%"
if not defined INFINITE_EXE if exist "%~dp0build\windows-vs2022\Release\%EXE_NAME%" set "INFINITE_EXE=%~dp0build\windows-vs2022\Release\%EXE_NAME%"
if not defined INFINITE_EXE if exist "%~dp0build\windows-vs2022\Debug\%EXE_NAME%" set "INFINITE_EXE=%~dp0build\windows-vs2022\Debug\%EXE_NAME%"

if not defined INFINITE_EXE (
  echo ERRO: %EXE_NAME% nao encontrado.
  echo Execute build-windows.bat primeiro ou coloque este arquivo ao lado de %EXE_NAME%.
  pause
  exit /b 1
)

for %%I in ("%INFINITE_EXE%") do set "INFINITE_DIR=%%~dpI"
pushd "%INFINITE_DIR%"
echo Iniciando: %INFINITE_EXE%
echo.
rem Prioridade acima do normal: render e audio em tempo real sem sufocar o sistema.
start "" /wait /abovenormal "%INFINITE_EXE%" %*
set "INFINITE_EXIT=%ERRORLEVEL%"
popd

echo.
echo Infinite-Turbo foi encerrado. Codigo de saida: %INFINITE_EXIT%
if not "%INFINITE_EXIT%"=="0" (
  echo Execute diagnose-windows.bat e envie o arquivo Infinite-Turbo-diagnostic.log.
  pause
)
exit /b %INFINITE_EXIT%
