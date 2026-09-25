@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
title Infinite-Turbo - dependencias de execucao

rem Para quem recebeu o pacote pronto (ZIP): instala FFmpeg (gravacao com audio)
rem e os modelos de remocao de fundo. Nao precisa de Visual Studio.
where winget.exe >nul 2>nul || (echo ERRO: winget nao encontrado. Instale o App Installer pela Microsoft Store. & pause & exit /b 1)

echo [1/2] FFmpeg...
winget list --id Gyan.FFmpeg -e >nul 2>nul || winget install --id Gyan.FFmpeg -e --source winget --accept-package-agreements --accept-source-agreements
if errorlevel 1 (echo ERRO: falha ao instalar o FFmpeg. & pause & exit /b 1)

echo [2/2] Modelos U2Net...
set "MODEL_DIR=%LOCALAPPDATA%\Infinite\models"
if not exist "%MODEL_DIR%" mkdir "%MODEL_DIR%"
rem O pacote pode trazer os modelos em .\models: copia sem baixar de novo.
if exist "%~dp0models\u2net.onnx" if not exist "%MODEL_DIR%\u2net.onnx" copy /y "%~dp0models\u2net.onnx" "%MODEL_DIR%\" >nul
if exist "%~dp0models\u2net_human_seg.onnx" if not exist "%MODEL_DIR%\u2net_human_seg.onnx" copy /y "%~dp0models\u2net_human_seg.onnx" "%MODEL_DIR%\" >nul
call :model u2net.onnx 60024c5c889badc19c04ad937298a77b
if errorlevel 1 (pause & exit /b 1)
call :model u2net_human_seg.onnx c09ddc2e0104f800e3e1bb4652583d1f
if errorlevel 1 (pause & exit /b 1)

echo.
echo Dependencias de execucao instaladas.
echo O runtime Windows ML + DirectML ja acompanha o pacote ao lado de Infinite-Turbo.exe.
pause
exit /b 0

:model
set "MODEL_FILE=%MODEL_DIR%\%~1"
if not exist "%MODEL_FILE%" (
  echo Baixando %~1...
  powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$ProgressPreference='SilentlyContinue'; Invoke-WebRequest -Uri 'https://github.com/danielgatis/rembg/releases/download/v0.0.0/%~1' -OutFile '%MODEL_FILE%'"
  if errorlevel 1 (echo ERRO: falha ao baixar %~1. & exit /b 1)
)
set "MD5="
for /f "tokens=*" %%H in ('powershell.exe -NoProfile -Command "(Get-FileHash -Algorithm MD5 -LiteralPath '%MODEL_FILE%').Hash.ToLowerInvariant()"') do set "MD5=%%H"
if /I not "!MD5!"=="%~2" (
  echo ERRO: checksum invalido de %~1. Apague "%MODEL_FILE%" e execute novamente.
  exit /b 1
)
echo OK: %~1
exit /b 0
