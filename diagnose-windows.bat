@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
title Infinite-Turbo - diagnostico de inicializacao

set "LOG=%~dp0Infinite-Turbo-diagnostic.log"
set "INFINITE_EXE="
if exist "%~dp0Infinite-Turbo.exe" set "INFINITE_EXE=%~dp0Infinite-Turbo.exe"
if not defined INFINITE_EXE if exist "%~dp0dist\Infinite-Turbo-Windows-x64\Infinite-Turbo.exe" set "INFINITE_EXE=%~dp0dist\Infinite-Turbo-Windows-x64\Infinite-Turbo.exe"
if not defined INFINITE_EXE if exist "%~dp0build\windows-vs2022\Release\Infinite-Turbo.exe" set "INFINITE_EXE=%~dp0build\windows-vs2022\Release\Infinite-Turbo.exe"
if not defined INFINITE_EXE if exist "%~dp0build\windows-vs2022\Debug\Infinite-Turbo.exe" set "INFINITE_EXE=%~dp0build\windows-vs2022\Debug\Infinite-Turbo.exe"

>"%LOG%" echo Infinite-Turbo (for Windows) - diagnostico de inicializacao
>>"%LOG%" echo Data: %DATE% %TIME%
>>"%LOG%" echo Pasta: %CD%
>>"%LOG%" echo.

if not defined INFINITE_EXE (
  >>"%LOG%" echo ERRO: Infinite-Turbo.exe nao encontrado.
  type "%LOG%"
  pause
  exit /b 1
)

for %%I in ("%INFINITE_EXE%") do (
  set "INFINITE_DIR=%%~dpI"
  >>"%LOG%" echo Executavel: %%~fI
  >>"%LOG%" echo Tamanho: %%~zI bytes
)

>>"%LOG%" echo.
>>"%LOG%" echo ===== WINDOWS =====
ver >>"%LOG%" 2>&1
powershell.exe -NoProfile -Command "Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,OSArchitecture | Format-List" >>"%LOG%" 2>&1

>>"%LOG%" echo.
>>"%LOG%" echo ===== VIDEO =====
powershell.exe -NoProfile -Command "Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,Status | Format-List" >>"%LOG%" 2>&1

>>"%LOG%" echo.
>>"%LOG%" echo ===== ARQUIVO E ASSINATURA =====
certutil -hashfile "%INFINITE_EXE%" SHA256 >>"%LOG%" 2>&1
powershell.exe -NoProfile -Command "Get-AuthenticodeSignature -LiteralPath $env:INFINITE_EXE | Select-Object Status,StatusMessage | Format-List" >>"%LOG%" 2>&1

>>"%LOG%" echo.
>>"%LOG%" echo ===== DEPENDENCIAS PE =====
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "!VSWHERE!" (
  for /f "usebackq tokens=*" %%V in (`"!VSWHERE!" -latest -products * -property installationPath`) do set "VSROOT=%%V"
)
if defined VSROOT if exist "!VSROOT!\Common7\Tools\VsDevCmd.bat" (
  call "!VSROOT!\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul 2>&1
  where dumpbin.exe >>"%LOG%" 2>&1
  dumpbin.exe /dependents "%INFINITE_EXE%" >>"%LOG%" 2>&1
) else (
  >>"%LOG%" echo dumpbin indisponivel.
)

>>"%LOG%" echo.
>>"%LOG%" echo ===== WINDOWS ML / DIRECTML =====
for %%D in (onnxruntime.dll DirectML.dll) do (
  if exist "!INFINITE_DIR!%%D" (
    for %%I in ("!INFINITE_DIR!%%D") do >>"%LOG%" echo OK: %%D - %%~zI bytes
  ) else (
    >>"%LOG%" echo AUSENTE: %%D
  )
)

>>"%LOG%" echo.
>>"%LOG%" echo ===== EXECUCAO =====
echo Executando Infinite-Turbo.exe. Aguarde a abertura ou o encerramento...
set "STARTUP_LOG_EXE=!INFINITE_DIR!Infinite-startup.log"
set "STARTUP_LOG_LOCAL=%LOCALAPPDATA%\Infinite\Infinite-startup.log"
if exist "!STARTUP_LOG_EXE!" del /q "!STARTUP_LOG_EXE!"
if exist "!STARTUP_LOG_LOCAL!" del /q "!STARTUP_LOG_LOCAL!"
for %%I in ("%INFINITE_EXE%") do pushd "%%~dpI"
"%INFINITE_EXE%" >>"%LOG%" 2>&1
set "APP_EXIT=!ERRORLEVEL!"
popd
>>"%LOG%" echo Codigo de saida decimal: !APP_EXIT!
powershell.exe -NoProfile -Command "[Convert]::ToString(([int64]$env:APP_EXIT -band 0xffffffff),16).PadLeft(8,'0').ToUpperInvariant()" >"%TEMP%\infinite-exit-hex.txt" 2>nul
set /p APP_EXIT_HEX=<"%TEMP%\infinite-exit-hex.txt"
>>"%LOG%" echo Codigo de saida hexadecimal: 0x!APP_EXIT_HEX!

>>"%LOG%" echo.
>>"%LOG%" echo ===== RASTREAMENTO INTERNO =====
if exist "!STARTUP_LOG_EXE!" (
  type "!STARTUP_LOG_EXE!" >>"%LOG%" 2>&1
) else if exist "!STARTUP_LOG_LOCAL!" (
  type "!STARTUP_LOG_LOCAL!" >>"%LOG%" 2>&1
) else (
  >>"%LOG%" echo Infinite-startup.log nao foi criado.
)

>>"%LOG%" echo.
>>"%LOG%" echo ===== VST3 =====
set "VST_SCANNER=!INFINITE_DIR!infinite-vst3-scanner.exe"
if exist "!VST_SCANNER!" (
  >>"%LOG%" echo Scanner auxiliar: !VST_SCANNER!
  certutil -hashfile "!VST_SCANNER!" SHA256 >>"%LOG%" 2>&1
) else (
  >>"%LOG%" echo ERRO: infinite-vst3-scanner.exe nao encontrado ao lado do Infinite-Turbo.exe.
)
if exist "%LOCALAPPDATA%\Infinite\Infinite-vst3.log" (
  type "%LOCALAPPDATA%\Infinite\Infinite-vst3.log" >>"%LOG%" 2>&1
) else (
  >>"%LOG%" echo Infinite-vst3.log ainda nao foi criado.
)
for %%B in ("%LOCALAPPDATA%\Infinite\PluginVST3Blocklist-v*.txt") do (
  >>"%LOG%" echo.
  >>"%LOG%" echo Blocklist VST3 atual: %%~nxB
  type "%%~fB" >>"%LOG%" 2>&1
)

>>"%LOG%" echo.
>>"%LOG%" echo ===== LOG DO APLICATIVO =====
if exist "%LOCALAPPDATA%\Infinite\Infinite.log" (
  type "%LOCALAPPDATA%\Infinite\Infinite.log" >>"%LOG%" 2>&1
) else (
  >>"%LOG%" echo Infinite.log ainda nao foi criado ou o log foi desativado.
)

>>"%LOG%" echo.
>>"%LOG%" echo ===== EVENTOS DE ERRO RECENTES =====
powershell.exe -NoProfile -Command "$start=(Get-Date).AddMinutes(-10); Get-WinEvent -FilterHashtable @{LogName='Application'; StartTime=$start; Level=2} -ErrorAction SilentlyContinue | Where-Object { $_.Message -match 'Infinite-Turbo.exe' } | Select-Object -First 8 TimeCreated,ProviderName,Id,Message | Format-List" >>"%LOG%" 2>&1

echo.
echo Diagnostico concluido.
echo Arquivo: %LOG%
echo Codigo de saida: !APP_EXIT! ^(0x!APP_EXIT_HEX!^)
echo.
type "%LOG%"
pause
exit /b !APP_EXIT!
