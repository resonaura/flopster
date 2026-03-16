@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul 2>&1

:: =============================================================================
::  Flopster — Windows Quick Run
::  Rebuilds (if needed), installs Standalone, then launches it immediately.
::  Useful for rapid test cycles without opening a DAW.
::
::  Usage: tools\win-run.bat [--rebuild] [--arch <arch>] [--full] [--help]
::
::  --full   Also install VST3 (default: standalone only)
::
::  by Shiru & Resonaura
:: =============================================================================

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
for %%I in ("%SCRIPT_DIR%\..") do set "ROOT=%%~fI"

set "REBUILD=0"
set "TARGET_ARCH="
set "FULL=0"

:: ── Parse arguments ───────────────────────────────────────────────────────────
:parse_args
if "%~1"=="" goto :done_args

if /i "%~1"=="--help"    goto :show_help
if /i "%~1"=="-h"        goto :show_help

if /i "%~1"=="--rebuild" (
    set "REBUILD=1"
    shift & goto :parse_args
)
if /i "%~1"=="-r" (
    set "REBUILD=1"
    shift & goto :parse_args
)
if /i "%~1"=="--full" (
    set "FULL=1"
    shift & goto :parse_args
)
if /i "%~1"=="-f" (
    set "FULL=1"
    shift & goto :parse_args
)
if /i "%~1"=="--arch" (
    if "%~2"=="" (
        echo  [ERROR] --arch requires an argument: arm64, x64, x86
        exit /b 1
    )
    set "TARGET_ARCH=%~2"
    shift & shift & goto :parse_args
)

echo  [WARN]  Unknown argument: %~1  (ignoring)
shift & goto :parse_args

:done_args

:: ── Banner ────────────────────────────────────────────────────────────────────
echo(
echo  +=======================================================+
if "!FULL!"=="1" (
    echo  ^|   Flopster — Rebuild ^& Run  ^(full^)                 ^|
) else (
    echo  ^|   Flopster — Rebuild ^& Run                           ^|
)
echo  ^|   by Shiru ^& Resonaura                               ^|
echo  +=======================================================+
echo(

:: ── Step 1: Build ─────────────────────────────────────────────────────────────
echo  [1/3] Building...
echo -----------------------------------------------

set "BUILD_ARGS="
if "!REBUILD!"=="1" set "BUILD_ARGS=--rebuild"
if defined TARGET_ARCH (
    set "BUILD_ARGS=!BUILD_ARGS! --arch !TARGET_ARCH!"
)

call "%SCRIPT_DIR%\win-build.bat" !BUILD_ARGS!
if errorlevel 1 (
    echo  [ERROR] Build failed.
    exit /b 1
)
echo(

:: ── Step 2: Install ──────────────────────────────────────────────────────────
if "!FULL!"=="1" (
    echo  [2/3] Installing VST3 + Standalone...
) else (
    echo  [2/3] Installing Standalone...
)
echo -----------------------------------------------

if "!FULL!"=="1" (
    set "INSTALL_ARGS=--only vst3,standalone"
) else (
    set "INSTALL_ARGS=--only standalone"
)
if defined TARGET_ARCH (
    set "INSTALL_ARGS=!INSTALL_ARGS! --arch !TARGET_ARCH!"
)

call "%SCRIPT_DIR%\win-install.bat" !INSTALL_ARGS!
if errorlevel 1 (
    echo  [ERROR] Install failed.
    exit /b 1
)
echo(

:: ── Step 3: Resolve arch for artefact path ────────────────────────────────────
if not defined TARGET_ARCH (
    set "HOST_ARCH=x64"
    if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64"  set "HOST_ARCH=arm64"
    if /i "%PROCESSOR_ARCHITEW6432%"=="ARM64"  set "HOST_ARCH=arm64"
    set "TARGET_ARCH=!HOST_ARCH!"
)

set "EXE=%ROOT%\build-!TARGET_ARCH!\Flopster_artefacts\Release\Standalone\Flopster.exe"

:: ── Step 4: Launch ────────────────────────────────────────────────────────────
echo  [3/3] Launching Standalone (%TARGET_ARCH%)...
echo -----------------------------------------------

if not exist "!EXE!" (
    echo  [ERROR] Flopster.exe not found: !EXE!
    echo          The install step may have failed.
    exit /b 1
)

:: Kill any existing instance so we always get a fresh launch
tasklist /FI "IMAGENAME eq Flopster.exe" 2>nul | find /I "Flopster.exe" >nul
if not errorlevel 1 (
    echo  [INFO]  Killing existing Flopster instance...
    taskkill /F /IM "Flopster.exe" >nul 2>&1
    timeout /t 1 /nobreak >nul
)

echo  [OK]    Launching: !EXE!
echo(
start "" "!EXE!"

echo(
echo  +=======================================================+
echo  ^|   ✅  Flopster launched!                             ^|
echo  +=======================================================+
echo(

endlocal
exit /b 0

:: =============================================================================
:show_help
echo(
echo  Usage: win-run.bat [--rebuild] [--arch ^<arch^>] [--help]
echo(
echo    --rebuild        Force a clean rebuild before installing and running
echo    --full           Also install VST3 (default: standalone only)
echo    --arch ^<arch^>    Target architecture to run: arm64 ^| x64 ^| x86
echo                     Default: native host arch
echo    --help           Show this help
echo(
echo  Examples:
echo    win-run.bat                  -- build + install standalone + run
echo    win-run.bat --full           -- build + install VST3 + standalone + run
echo    win-run.bat --rebuild        -- clean build + install + run
echo    win-run.bat --arch x64       -- run x64 build specifically
echo(
exit /b 0
