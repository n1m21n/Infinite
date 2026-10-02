@echo off
setlocal EnableExtensions
title Infinite-Turbo - MCP setup

rem Registra o Infinite-Turbo como servidor MCP no Claude Desktop.
rem Procura o executavel como o run-windows.bat (ao lado, dist, build).
set "EXE_NAME=Infinite-Turbo.exe"
set "INFINITE_EXE="
if exist "%~dp0%EXE_NAME%" set "INFINITE_EXE=%~dp0%EXE_NAME%"
if not defined INFINITE_EXE if exist "%~dp0build\windows-vs2022\Release\%EXE_NAME%" set "INFINITE_EXE=%~dp0build\windows-vs2022\Release\%EXE_NAME%"
if not defined INFINITE_EXE if exist "%~dp0dist\Infinite-Turbo-Windows-x64\%EXE_NAME%" set "INFINITE_EXE=%~dp0dist\Infinite-Turbo-Windows-x64\%EXE_NAME%"

if not defined INFINITE_EXE (
  echo ERRO: %EXE_NAME% nao encontrado. Rode build-windows.bat primeiro.
  pause
  exit /b 1
)

echo Registrando: %INFINITE_EXE%
start "" /wait "%INFINITE_EXE%" --mcp-install
exit /b %ERRORLEVEL%
