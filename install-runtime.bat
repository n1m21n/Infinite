@echo off
setlocal EnableExtensions EnableDelayedExpansion
title Infinite - dependencias de execucao
where winget.exe >nul 2>nul || (echo ERRO: winget nao encontrado. & exit /b 1)
winget list --id Gyan.FFmpeg -e >nul 2>nul || winget install --id Gyan.FFmpeg -e --source winget --accept-package-agreements --accept-source-agreements
if errorlevel 1 exit /b 1
set "MODEL_DIR=%LOCALAPPDATA%\Infinite\models"
set "MODEL_FILE=%MODEL_DIR%\u2net.onnx"
set "PERSON_MODEL_FILE=%MODEL_DIR%\u2net_human_seg.onnx"
if not exist "%MODEL_DIR%" mkdir "%MODEL_DIR%"
if not exist "%MODEL_FILE%" powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$ProgressPreference='SilentlyContinue'; Invoke-WebRequest -Uri 'https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2net.onnx' -OutFile '%MODEL_FILE%'"
if errorlevel 1 exit /b 1
for /f "tokens=*" %%H in ('powershell.exe -NoProfile -Command "(Get-FileHash -Algorithm MD5 -LiteralPath '%MODEL_FILE%').Hash.ToLowerInvariant()"') do set "MODEL_MD5=%%H"
if /I not "!MODEL_MD5!"=="60024c5c889badc19c04ad937298a77b" (echo ERRO: checksum invalido do U2Net. & exit /b 1)
if not exist "%PERSON_MODEL_FILE%" powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$ProgressPreference='SilentlyContinue'; Invoke-WebRequest -Uri 'https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2net_human_seg.onnx' -OutFile '%PERSON_MODEL_FILE%'"
if errorlevel 1 exit /b 1
for /f "tokens=*" %%H in ('powershell.exe -NoProfile -Command "(Get-FileHash -Algorithm MD5 -LiteralPath '%PERSON_MODEL_FILE%').Hash.ToLowerInvariant()"') do set "PERSON_MODEL_MD5=%%H"
if /I not "!PERSON_MODEL_MD5!"=="c09ddc2e0104f800e3e1bb4652583d1f" (echo ERRO: checksum invalido do U2Net Human Seg. & exit /b 1)
echo Dependencias de execucao instaladas.
echo O runtime Windows ML + DirectML ja acompanha o pacote ao lado de Infinite.exe.
