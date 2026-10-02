@echo off
setlocal EnableExtensions DisableDelayedExpansion
cd /d "%~dp0"
title Infinite-Turbo - release

rem Uso: release-windows.bat [nobuild]
rem   1. compila do zero e empacota (build-windows.bat fresh pack)
rem   2. copia o ZIP para dist\release\Infinite-Turbo-<versao>-Windows-x64.zip
rem   3. extrai as notas da versao do CHANGELOG-TURBO.md
rem   4. cria a tag git v<versao> (local; o push e seu)
rem   5. abre a pagina de nova release do GitHub e a pasta com os arquivos
rem   nobuild  pula o passo 1 (usa o dist\ que ja existe)

set "VER="
for /f "tokens=3" %%v in ('findstr /b /c:"project(InfiniteTurbo VERSION" CMakeLists.txt') do set "VER=%%v"
if not defined VER (
  echo ERRO: versao nao encontrada no CMakeLists.txt.
  exit /b 1
)
echo Infinite-Turbo %VER%
echo.

if /I not "%~1"=="nobuild" (
  call build-windows.bat fresh pack
  if errorlevel 1 (
    echo ERRO: a compilacao falhou. Nada foi publicado.
    exit /b 1
  )
)

if not exist "dist\Infinite-Turbo-Windows-x64.zip" (
  echo ERRO: dist\Infinite-Turbo-Windows-x64.zip nao existe. Rode sem "nobuild".
  exit /b 1
)
if not exist "dist\release" mkdir "dist\release"
set "ZIP=dist\release\Infinite-Turbo-%VER%-Windows-x64.zip"
copy /y "dist\Infinite-Turbo-Windows-x64.zip" "%ZIP%" >nul
if errorlevel 1 (
  echo ERRO: nao consegui copiar o ZIP.
  exit /b 1
)

rem Notas: da primeira linha "## " do changelog ate a seguinte (exclusive).
set "NOTES=dist\release\release-notes-%VER%.md"
set "START="
set "END="
for /f "tokens=1 delims=:" %%n in ('findstr /n /b /c:"## " CHANGELOG-TURBO.md') do call :mark %%n
if not defined START (
  echo AVISO: nenhuma secao "## " no CHANGELOG-TURBO.md; notas vazias.
  type nul > "%NOTES%"
) else (
  if not defined END set "END=99999999"
  call :writenotes
)

rem Tag local (anotada). Ja existindo, so avisa.
git rev-parse -q --verify "refs/tags/v%VER%" >nul 2>&1
if errorlevel 1 (
  git tag -a "v%VER%" -m "Infinite-Turbo %VER%"
  if errorlevel 1 (echo AVISO: nao consegui criar a tag v%VER%.) else (echo Tag criada: v%VER%)
) else (
  echo Tag v%VER% ja existe.
)

echo.
echo ===============================================================
echo  Pronto. Para publicar:
echo    1. git push origin turbo/windows-only
echo    2. git push origin v%VER%
echo    3. Na pagina que vai abrir: titulo "Infinite-Turbo %VER%",
echo       cole %NOTES%
echo       e anexe %ZIP%
echo ===============================================================
start "" "https://github.com/ricardopalmieri/Infinite/releases/new?tag=v%VER%&title=Infinite-Turbo%%20%VER%"
start "" explorer.exe "%CD%\dist\release"
exit /b 0

:mark
if not defined START (set "START=%1" & exit /b 0)
if not defined END set "END=%1"
exit /b 0

:writenotes
rem START/END are expanded once here, before the loop runs; the line text
rem stays in %%b and is never re-parsed (quotes, ">" and "&" are safe).
type nul > "%NOTES%"
for /f "skip=%START% tokens=1* delims=:" %%a in ('findstr /n "^" CHANGELOG-TURBO.md') do (
  if %%a LSS %END% (echo(%%b)>> "%NOTES%"
)
exit /b 0
