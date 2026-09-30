@echo off
rem Gera dist\Infinite-Turbo-Windows-x64.zip a partir do build Release atual.
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%CD%\scripts\package-windows.ps1"
if errorlevel 1 (echo Falha ao gerar o pacote.& exit /b 1)
echo.
echo Pacote pronto em dist\Infinite-Turbo-Windows-x64.zip
