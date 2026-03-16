@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul 2>&1

:: =============================================================================
::  Flopster — Windows Build Script
::  Builds VST3 + Standalone in Release (or Debug with --debug flag)
::  by Shiru & Resonaura
:: =============================================================================

set "SCRIPT_DIR=%~dp0"
set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
set "ROOT=%SCRIPT_DIR%\.."
set "BUILD_RELEASE=%SCRIPT_DIR%\build"
set "BUILD_DEBUG=%SCRIPT_DIR%\build-debug"
set "BUILD_TYPE=Release"
set "BUILD_DIR=%BUILD_RELEASE%"
set "REBUILD=0"
set "JUCE_DIR=%SCRIPT_DIR%\JUCE"

:: ── Parse arguments ───────────────────────────────────────────────────────────
:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="--debug"   ( set "BUILD_TYPE=Debug"   & set "BUILD_DIR=%BUILD_DEBUG%"   & shift & goto parse_args )
if /i "%~1"=="-d"        ( set "BUILD_TYPE=Debug"   & set "BUILD_DIR=%BUILD_DEBUG%"   & shift & goto parse_args )
if /i "%~1"=="--release" ( set "BUILD_TYPE=Release" & set "BUILD_DIR=%BUILD_RELEASE%" & shift & goto parse_args )
if /i "%~1"=="-r"        ( set "BUILD_TYPE=Release" & set "BUILD_DIR=%BUILD_RELEASE%" & shift & goto parse_args )
if /i "%~1"=="--rebuild" ( set "REBUILD=1" & shift & goto parse_args )
if /i "%~1"=="--help"    goto show_help
if /i "%~1"=="-h"        goto show_help
echo [warn] Unknown flag: %~1 (ignored)
shift
goto parse_args

:show_help
echo.
echo  Usage: build.bat [--debug] [--release] [--rebuild] [--help]
echo.
echo    --debug     Build Debug configuration
echo    --release   Build Release configuration (default)
echo    --rebuild   Remove build directory and rebuild from scratch
echo    --help      Show this help
echo.
exit /b 0

:args_done

:: ── Banner ────────────────────────────────────────────────────────────────────
echo.
echo  ============================================
echo   Flopster Build Script
echo   by Shiru ^& Resonaura
echo  ============================================
echo.
echo  [build] Build type : %BUILD_TYPE%
echo  [build] Build dir  : %BUILD_DIR%
echo.

:: ── Check cmake ───────────────────────────────────────────────────────────────
where cmake >nul 2>&1
if errorlevel 1 (
    echo  [ERROR] cmake not found in PATH.
    echo          Install from https://cmake.org/download/
    echo          and make sure to check "Add CMake to system PATH" during install.
    exit /b 1
)
for /f "tokens=*" %%v in ('cmake --version 2^>nul ^| findstr /r "cmake version"') do (
    echo  [ok] %%v
)

:: ── Check/find build system (Ninja or VS) ─────────────────────────────────────
set "GENERATOR="
set "EXTRA_ARGS="

:: Try to find Visual Studio 2022
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.Component.MSBuild -property installationPath 2^>nul`) do (
        if exist "%%i\MSBuild\Current\Bin\MSBuild.exe" (
            set "GENERATOR=Visual Studio 17 2022"
            set "EXTRA_ARGS=-A x64"
            echo  [ok] Found Visual Studio 2022
            goto generator_done
        )
    )
    :: Try VS 2019 as fallback
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -version "[16.0,17.0)" -requires Microsoft.Component.MSBuild -property installationPath 2^>nul`) do (
        if exist "%%i\MSBuild\Current\Bin\MSBuild.exe" (
            set "GENERATOR=Visual Studio 16 2019"
            set "EXTRA_ARGS=-A x64"
            echo  [ok] Found Visual Studio 2019
            goto generator_done
        )
    )
)

:: Try Ninja as fallback
where ninja >nul 2>&1
if not errorlevel 1 (
    set "GENERATOR=Ninja"
    set "EXTRA_ARGS="
    echo  [ok] Using Ninja build system
    goto generator_done
)

echo  [ERROR] No suitable build system found.
echo          Please install one of:
echo            - Visual Studio 2022 (recommended): https://visualstudio.microsoft.com/
echo            - Ninja: https://ninja-build.org/
echo            - Or: winget install Ninja-build.Ninja
exit /b 1

:generator_done

:: ── Check JUCE ────────────────────────────────────────────────────────────────
if not exist "%JUCE_DIR%\CMakeLists.txt" (
    echo  [warn] JUCE not found at %JUCE_DIR%
    echo  [build] Cloning JUCE 8.0.7 ...
    where git >nul 2>&1
    if errorlevel 1 (
        echo  [ERROR] git not found. Install Git from https://git-scm.com/
        exit /b 1
    )
    git clone --depth 1 --branch 8.0.7 https://github.com/juce-framework/JUCE.git "%JUCE_DIR%"
    if errorlevel 1 (
        echo  [ERROR] Failed to clone JUCE
        exit /b 1
    )
    echo  [ok] JUCE cloned
) else (
    echo  [ok] JUCE found at %JUCE_DIR%
)

:: ── Clean if rebuild ─────────────────────────────────────────────────────────
if "%REBUILD%"=="1" (
    if exist "%BUILD_DIR%" (
        echo  [build] Removing existing build directory...
        rmdir /s /q "%BUILD_DIR%"
        echo  [ok] Removed %BUILD_DIR%
    )
)

:: ── CMake configure ──────────────────────────────────────────────────────────
set "NEED_CONFIGURE=0"
if not exist "%BUILD_DIR%\CMakeCache.txt" set "NEED_CONFIGURE=1"

if "%NEED_CONFIGURE%"=="1" (
    echo.
    echo  [build] Configuring with CMake...
    echo          Generator: %GENERATOR%
    echo.

    if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

    cmake -S "%SCRIPT_DIR%" -B "%BUILD_DIR%" ^
        -G "%GENERATOR%" %EXTRA_ARGS% ^
        -DCMAKE_BUILD_TYPE=%BUILD_TYPE%

    if errorlevel 1 (
        echo.
        echo  [ERROR] CMake configuration failed.
        exit /b 1
    )
    echo  [ok] CMake configuration done
) else (
    echo  [build] CMake already configured -- skipping (use --rebuild to reconfigure)
)

echo.

:: ── Build ────────────────────────────────────────────────────────────────────
echo  [build] Building Flopster (%BUILD_TYPE%)...
echo.

cmake --build "%BUILD_DIR%" --config %BUILD_TYPE% --parallel

if errorlevel 1 (
    echo.
    echo  [ERROR] Build failed.
    echo          Check output above for errors.
    exit /b 1
)

echo.

:: ── Verify artefacts ─────────────────────────────────────────────────────────
echo  [build] Verifying artefacts...

set "ARTEFACT_BASE=%BUILD_DIR%\Flopster_artefacts\%BUILD_TYPE%"
set "ALL_OK=1"

:: VST3
if exist "%ARTEFACT_BASE%\VST3\Flopster.vst3\Contents\x86_64-win\Flopster.vst3" (
    echo  [ok] VST3\Flopster.vst3
) else if exist "%ARTEFACT_BASE%\VST3\Flopster.vst3" (
    echo  [ok] VST3\Flopster.vst3
) else (
    echo  [warn] VST3 artefact not found
    set "ALL_OK=0"
)

:: Standalone
if exist "%ARTEFACT_BASE%\Standalone\Flopster.exe" (
    echo  [ok] Standalone\Flopster.exe
) else (
    echo  [warn] Standalone artefact not found
    set "ALL_OK=0"
)

echo.

if "%ALL_OK%"=="0" (
    echo  [warn] Some artefacts missing -- check build output above.
) else (
    echo  ============================================
    echo   Build complete!
    echo  ============================================
)

echo.
echo   VST3       -^>  %ARTEFACT_BASE%\VST3\Flopster.vst3
echo   Standalone -^>  %ARTEFACT_BASE%\Standalone\Flopster.exe
echo.
echo   Run install.bat to install into the system.
echo.

endlocal
