@echo off
setlocal EnableExtensions
cd /d "%~dp0"
title Infinite-Turbo - testes automaticos

rem Uso: test-windows.bat [-SkipBuild] [-Quick] [-Only A,B] [-Config Debug] [-ShotOnly]
rem Resultados: build\test-results\summary.txt (+ um .log por teste e screenshot.png)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0.claude\skills\run-turbo-tests\driver.ps1" %*
set "RESULT=%ERRORLEVEL%"
echo.
if "%RESULT%"=="0" (echo Todos os testes passaram.) else (echo Houve falhas. Veja build\test-results\summary.txt)
exit /b %RESULT%
