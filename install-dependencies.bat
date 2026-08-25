@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
title Infinite - instalador de dependencias

rem O instalador do Visual Studio recusa --passive sem um token UAC elevado.
rem O argumento interno evita um segundo relancamento e permite validar o UAC.
if /I "%~1"=="--elevated" goto :verify_elevation

fltmc.exe >nul 2>nul
if not errorlevel 1 goto :dependencies

echo Este instalador precisa de permissao de administrador.
echo Solicitando elevacao pelo UAC...
set "INFINITE_INSTALLER=%~f0"
set "INFINITE_INSTALLER_DIR=%~dp0"

rem Eleva o cmd.exe, que entao chama este BAT. Isso funciona de modo mais
rem confiavel do que tentar iniciar um arquivo BAT diretamente com RunAs.
rem A saida permanece visivel ao vivo na janela elevada. No final, a janela
rem aguarda uma tecla para que mensagens de sucesso ou erro possam ser lidas.
setlocal DisableDelayedExpansion
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$q = [char]34; $command = 'call ' + $q + $env:INFINITE_INSTALLER + $q + ' --elevated & set ' + $q + 'INFINITE_RESULT=!errorlevel!' + $q + ' & echo. & echo Pressione uma tecla para fechar esta janela. & pause >nul & exit /b !INFINITE_RESULT!'; try { $p = Start-Process -FilePath $env:ComSpec -ArgumentList @('/d','/v:on','/c',$command) -WorkingDirectory $env:INFINITE_INSTALLER_DIR -Verb RunAs -Wait -PassThru -ErrorAction Stop; exit $p.ExitCode } catch { Write-Error $_; exit 1 }"
set "ELEVATION_RESULT=%errorlevel%"
endlocal & set "ELEVATION_RESULT=%ELEVATION_RESULT%"

if not "!ELEVATION_RESULT!"=="0" (
  echo.
  echo ERRO: a instalacao elevada terminou com o codigo !ELEVATION_RESULT!.
  echo O erro detalhado foi exibido na janela elevada.
  echo Se necessario, abra o Terminal como administrador e execute este arquivo novamente.
) else (
  echo Instalacao elevada concluida com sucesso.
)
exit /b !ELEVATION_RESULT!

:verify_elevation
fltmc.exe >nul 2>nul
if errorlevel 1 (
  echo ERRO: o processo relancado nao recebeu permissao de administrador.
  exit /b 740
)

:dependencies
echo Permissao de administrador confirmada.

echo [1/7] Verificando o Windows Package Manager...
where winget.exe >nul 2>nul
if errorlevel 1 (
  echo ERRO: winget nao foi encontrado. Instale o App Installer pela Microsoft Store.
  exit /b 1
)

echo [2/7] Instalando Git, CMake, FFmpeg e Visual Studio Build Tools...
call :winget Git.Git
if errorlevel 1 exit /b 1
call :winget Kitware.CMake
if errorlevel 1 exit /b 1
call :winget Gyan.FFmpeg
if errorlevel 1 exit /b 1

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSSETUP=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\setup.exe"
set "VSROOT="
set "VSANY="
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
  for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSANY=%%I"
)

if defined VSROOT (
  echo Workload C++ do Visual Studio ja instalado.
) else (
  if not defined VSANY (
    winget install --id Microsoft.VisualStudio.2022.BuildTools -e --source winget --accept-package-agreements --accept-source-agreements --override "--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
    if errorlevel 1 (
      echo ERRO: falha ao instalar o Visual Studio Build Tools.
      exit /b 1
    )
    if exist "%VSWHERE%" (
      for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
      for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSANY=%%I"
    )
  )

  if not defined VSROOT (
    if not defined VSANY (
      echo ERRO: nenhuma instalacao do Visual Studio 2022 foi localizada.
      exit /b 1
    )
    if not exist "%VSSETUP%" (
      echo ERRO: Visual Studio Installer nao encontrado em "%VSSETUP%".
      exit /b 1
    )

    echo Adicionando o workload C++ a "!VSANY!"...
    "%VSSETUP%" modify --installPath "!VSANY!" --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended
    set "VSSETUP_RESULT=!errorlevel!"
    if "!VSSETUP_RESULT!"=="3010" (
      echo O Visual Studio solicitou uma reinicializacao. Reinicie o Windows e execute este arquivo novamente.
      exit /b 3010
    )
    if not "!VSSETUP_RESULT!"=="0" (
      echo ERRO: falha ao adicionar o workload C++ ao Visual Studio. Codigo !VSSETUP_RESULT!.
      exit /b !VSSETUP_RESULT!
    )

    set "VSROOT="
    for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
    if not defined VSROOT (
      echo ERRO: o Visual Studio terminou sem confirmar as ferramentas C++ x64.
      exit /b 1
    )
  )
)

rem O winget atualiza o PATH persistente, mas nao o processo BAT que ja esta
rem aberto. Recarrega os valores e cobre instalacoes por usuario e do VS.
call :refresh_path

set "GIT_EXE="
for /f "delims=" %%I in ('where git.exe 2^>nul') do if not defined GIT_EXE set "GIT_EXE=%%I"
if not defined GIT_EXE if exist "%ProgramFiles%\Git\cmd\git.exe" set "GIT_EXE=%ProgramFiles%\Git\cmd\git.exe"
if not defined GIT_EXE if exist "%LOCALAPPDATA%\Programs\Git\cmd\git.exe" set "GIT_EXE=%LOCALAPPDATA%\Programs\Git\cmd\git.exe"
if not defined GIT_EXE (
  echo ERRO: Git nao encontrado apos a instalacao.
  exit /b 1
)
for %%I in ("!GIT_EXE!") do set "PATH=%%~dpI;!PATH!"
echo Git localizado: !GIT_EXE!

set "CMAKE_EXE="
for /f "delims=" %%I in ('where cmake.exe 2^>nul') do if not defined CMAKE_EXE set "CMAKE_EXE=%%I"
if not defined CMAKE_EXE if exist "%ProgramFiles%\CMake\bin\cmake.exe" set "CMAKE_EXE=%ProgramFiles%\CMake\bin\cmake.exe"
if not defined CMAKE_EXE if exist "%LOCALAPPDATA%\Programs\CMake\bin\cmake.exe" set "CMAKE_EXE=%LOCALAPPDATA%\Programs\CMake\bin\cmake.exe"
if not defined CMAKE_EXE if defined VSROOT if exist "!VSROOT!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=!VSROOT!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE_EXE (
  echo ERRO: CMake consta no winget, mas cmake.exe nao foi localizado.
  echo Feche este terminal, abra um novo e execute where cmake.exe.
  exit /b 1
)
for %%I in ("!CMAKE_EXE!") do set "PATH=%%~dpI;!PATH!"
echo CMake localizado: !CMAKE_EXE!

echo [3/7] Preparando o vcpkg...
if not defined VCPKG_ROOT set "VCPKG_ROOT=%LOCALAPPDATA%\InfiniteBuild\vcpkg"
if not exist "%VCPKG_ROOT%\.git" (
  git clone https://github.com/microsoft/vcpkg.git "%VCPKG_ROOT%"
  if errorlevel 1 exit /b 1
) else (
  git -C "%VCPKG_ROOT%" pull --ff-only
  if errorlevel 1 exit /b 1
)
call "%VCPKG_ROOT%\bootstrap-vcpkg.bat" -disableMetrics
if errorlevel 1 exit /b 1
setx VCPKG_ROOT "%VCPKG_ROOT%" >nul

echo [4/7] Inicializando os submodulos do projeto...
if exist ".git" (
  git submodule update --init --recursive
  if errorlevel 1 exit /b 1
) else (
  echo Pacote ZIP detectado: submodulos Git nao sao necessarios para o build Windows.
)

echo [5/7] Compilando e instalando bibliotecas C++...
"%VCPKG_ROOT%\vcpkg.exe" install --triplet x64-windows-static --x-manifest-root="%CD%"
if errorlevel 1 exit /b 1

echo [6/7] Instalando Windows ML e DirectML para GPU DX12...
set "WINDOWSML_VERSION=2.2.12"
set "WINDOWSML_BASE=%LOCALAPPDATA%\InfiniteBuild\windowsml"
set "WINDOWSML_ROOT=%WINDOWSML_BASE%\Microsoft.Windows.AI.MachineLearning.%WINDOWSML_VERSION%"
set "WINDOWSML_ARCHIVE=%WINDOWSML_BASE%\Microsoft.Windows.AI.MachineLearning.%WINDOWSML_VERSION%.zip"
if not exist "%WINDOWSML_BASE%" mkdir "%WINDOWSML_BASE%"
if not exist "%WINDOWSML_ROOT%\build\cmake" (
  echo Baixando Microsoft.Windows.AI.MachineLearning %WINDOWSML_VERSION%...
  powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$ProgressPreference='SilentlyContinue'; Invoke-WebRequest -Uri 'https://api.nuget.org/v3-flatcontainer/microsoft.windows.ai.machinelearning/%WINDOWSML_VERSION%/microsoft.windows.ai.machinelearning.%WINDOWSML_VERSION%.nupkg' -OutFile '%WINDOWSML_ARCHIVE%'"
  if errorlevel 1 (
    echo ERRO: falha ao baixar o pacote oficial Windows ML.
    exit /b 1
  )
  if exist "%WINDOWSML_ROOT%" rmdir /s /q "%WINDOWSML_ROOT%"
  powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Expand-Archive -LiteralPath '%WINDOWSML_ARCHIVE%' -DestinationPath '%WINDOWSML_ROOT%' -Force"
  if errorlevel 1 (
    echo ERRO: falha ao extrair o pacote Windows ML.
    exit /b 1
  )
)
if not exist "%WINDOWSML_ROOT%\build\cmake" (
  echo ERRO: o pacote Windows ML nao contem a integracao CMake esperada.
  exit /b 1
)
set "DIRECTML_DLL="
for /r "%WINDOWSML_ROOT%" %%I in (DirectML.dll) do if not defined DIRECTML_DLL set "DIRECTML_DLL=%%I"
if not defined DIRECTML_DLL (
  echo ERRO: DirectML.dll nao foi localizado no pacote Windows ML.
  exit /b 1
)
setx INFINITE_WINDOWSML_ROOT "%WINDOWSML_ROOT%" >nul
echo Windows ML localizado: %WINDOWSML_ROOT%
echo DirectML localizado: !DIRECTML_DLL!

echo [7/7] Instalando o modelo de remocao de fundo U2Net...
set "MODEL_DIR=%LOCALAPPDATA%\Infinite\models"
set "MODEL_FILE=%MODEL_DIR%\u2net.onnx"
set "PERSON_MODEL_FILE=%MODEL_DIR%\u2net_human_seg.onnx"
if not exist "%MODEL_DIR%" mkdir "%MODEL_DIR%"
if not exist "%MODEL_FILE%" (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$ProgressPreference='SilentlyContinue'; Invoke-WebRequest -Uri 'https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2net.onnx' -OutFile '%MODEL_FILE%'"
  if errorlevel 1 exit /b 1
)
for /f "tokens=*" %%H in ('powershell.exe -NoProfile -Command "(Get-FileHash -Algorithm MD5 -LiteralPath '%MODEL_FILE%').Hash.ToLowerInvariant()"') do set "MODEL_MD5=%%H"
if /I not "!MODEL_MD5!"=="60024c5c889badc19c04ad937298a77b" (
  echo ERRO: checksum invalido do modelo U2Net. Apague "%MODEL_FILE%" e execute novamente.
  exit /b 1
)
if not exist "%PERSON_MODEL_FILE%" (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$ProgressPreference='SilentlyContinue'; Invoke-WebRequest -Uri 'https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2net_human_seg.onnx' -OutFile '%PERSON_MODEL_FILE%'"
  if errorlevel 1 exit /b 1
)
for /f "tokens=*" %%H in ('powershell.exe -NoProfile -Command "(Get-FileHash -Algorithm MD5 -LiteralPath '%PERSON_MODEL_FILE%').Hash.ToLowerInvariant()"') do set "PERSON_MODEL_MD5=%%H"
if /I not "!PERSON_MODEL_MD5!"=="c09ddc2e0104f800e3e1bb4652583d1f" (
  echo ERRO: checksum invalido do modelo U2Net Human Seg. Apague "%PERSON_MODEL_FILE%" e execute novamente.
  exit /b 1
)

echo.
echo Dependencias instaladas. Execute build-windows.bat para compilar.
echo Remove Background usara Windows ML + DirectML em qualquer GPU DX12 compativel.
exit /b 0

:winget
winget list --id %1 -e >nul 2>nul
if not errorlevel 1 exit /b 0
winget install --id %1 -e --source winget --accept-package-agreements --accept-source-agreements
if errorlevel 1 (
  echo ERRO: falha ao instalar %1.
  exit /b 1
)
exit /b 0

:refresh_path
set "MACHINE_PATH="
set "USER_PATH="
for /f "usebackq delims=" %%P in (`powershell.exe -NoProfile -Command "[Environment]::GetEnvironmentVariable('Path','Machine')"`) do set "MACHINE_PATH=%%P"
for /f "usebackq delims=" %%P in (`powershell.exe -NoProfile -Command "[Environment]::GetEnvironmentVariable('Path','User')"`) do set "USER_PATH=%%P"
if defined MACHINE_PATH set "PATH=!MACHINE_PATH!;!PATH!"
if defined USER_PATH set "PATH=!USER_PATH!;!PATH!"
set "PATH=%ProgramFiles%\Git\cmd;%LOCALAPPDATA%\Programs\Git\cmd;%ProgramFiles%\CMake\bin;%LOCALAPPDATA%\Programs\CMake\bin;%LOCALAPPDATA%\Microsoft\WinGet\Links;!PATH!"
exit /b 0
