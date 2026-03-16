@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul 2>&1

:: =============================================================================
::  Flopster — Windows Installer
::  Builds (if needed), installs VST3 to Common/VST3 and LocalAppData,
::  copies standalone to Desktop, and bundles resources into each install.
:: =============================================================================

:: ── Paths ────────────────────────────────────────────────────────────────────
set "SCRIPT_DIR=%~dp0"
:: Strip trailing backslash
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

:: ROOT is one level up from the plugin/ directory
for %%I in ("%SCRIPT_DIR%\..") do set "ROOT=%%~fI"

set "BUILD=%SCRIPT_DIR%\build"
set "SRC=%SCRIPT_DIR%\src"
set "SAMPLES=%ROOT%\samples"

set "ARTEFACTS=%BUILD%\Flopster_artefacts\Release"
set "VST3_SRC=%ARTEFACTS%\VST3\Flopster.vst3"
set "EXE_SRC=%ARTEFACTS%\Standalone\Flopster.exe"

:: Install destinations
set "VST3_DST1=%CommonProgramFiles%\VST3\Flopster.vst3"
set "VST3_DST2=%LOCALAPPDATA%\Programs\VstPlugins\Flopster.vst3"
set "DESKTOP=%USERPROFILE%\Desktop"
set "EXE_DST=%DESKTOP%\Flopster.exe"

:: ── Parse arguments ───────────────────────────────────────────────────────────
set "REBUILD=0"
:parse_args
if "%~1"=="" goto :done_args
if /i "%~1"=="--rebuild" set "REBUILD=1"
if /i "%~1"=="-r"        set "REBUILD=1"
if /i "%~1"=="--help"    goto :show_help
if /i "%~1"=="-h"        goto :show_help
shift
goto :parse_args
:show_help
echo Usage: install.bat [--rebuild]
echo   --rebuild   Force a clean rebuild even if artefacts exist.
exit /b 0
:done_args

:: ── Banner ────────────────────────────────────────────────────────────────────
echo.
echo  +==============================================+
echo  ^|   Flopster Plugin Installer  v1.21          ^|
echo  ^|   by Shiru ^& Resonaura                      ^|
echo  +==============================================+
echo.

:: ── 1. Check prerequisites ────────────────────────────────────────────────────
echo [1/5] Checking prerequisites...
echo -----------------------------------------------

:: Check cmake
cmake --version >nul 2>&1
if errorlevel 1 (
    echo  [ERROR] cmake not found in PATH.
    echo          Install CMake from https://cmake.org/download/
    echo          and make sure it is added to your PATH.
    exit /b 1
)
for /f "tokens=3" %%V in ('cmake --version 2^>^&1 ^| findstr /i "cmake version"') do (
    echo  [OK]    cmake %%V
)

:: Check ninja (optional but preferred)
set "HAS_NINJA=0"
ninja --version >nul 2>&1
if not errorlevel 1 (
    set "HAS_NINJA=1"
    for /f %%V in ('ninja --version 2^>^&1') do echo  [OK]    ninja %%V
) else (
    echo  [WARN]  ninja not found — will try Visual Studio generator as fallback.
)

:: ── 2. Determine generator ────────────────────────────────────────────────────
::
:: Preference order:
::   1. Visual Studio 17 2022  (if installed)
::   2. Ninja                  (if installed)
::   3. Fail with a helpful message
::
set "GENERATOR="
set "EXTRA_FLAGS="

:: Try to detect VS 2022 via vswhere
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%P in (
        `"%VSWHERE%" -latest -version "[17.0,18.0)" -property installationPath 2^>nul`
    ) do set "VS2022_PATH=%%P"
)

if defined VS2022_PATH (
    echo  [OK]    Visual Studio 2022 found at: !VS2022_PATH!
    set "GENERATOR=Visual Studio 17 2022"
    set "EXTRA_FLAGS=-A x64"
) else if "!HAS_NINJA!"=="1" (
    echo  [INFO]  Visual Studio 2022 not found — using Ninja generator.
    set "GENERATOR=Ninja"
    set "EXTRA_FLAGS=-DCMAKE_BUILD_TYPE=Release"
) else (
    echo  [ERROR] Neither Visual Studio 2022 nor ninja were found.
    echo          Please install one of the following:
    echo            - Visual Studio 2022 (Community/Pro/Enterprise)
    echo                https://visualstudio.microsoft.com/
    echo            - ninja  (via winget: winget install Ninja-build.Ninja)
    exit /b 1
)

echo  [INFO]  Using generator: !GENERATOR!
echo.

:: ── 3. Build if needed ────────────────────────────────────────────────────────
echo [2/5] Build...
echo -----------------------------------------------

set "NEED_BUILD=0"
if "!REBUILD!"=="1" set "NEED_BUILD=1"
if not exist "%EXE_SRC%" set "NEED_BUILD=1"
if not exist "%VST3_SRC%\Contents\x86_64-win\Flopster.vst3" (
    :: Also check for the flat-file VST3 layout used by some JUCE versions
    if not exist "%VST3_SRC%\Flopster.vst3" (
        :: And the common case: the .vst3 IS the folder, binary inside
        if not exist "%VST3_SRC%\Contents\x86_64-win\Flopster.vst3" (
            set "NEED_BUILD=1"
        )
    )
)

:: Simplest reliable check: just look for the standalone exe
if not exist "%EXE_SRC%" set "NEED_BUILD=1"

if "!NEED_BUILD!"=="1" (
    if "!REBUILD!"=="1" (
        if exist "%BUILD%" (
            echo  [INFO]  --rebuild requested — removing old build directory...
            rmdir /s /q "%BUILD%"
            if errorlevel 1 (
                echo  [ERROR] Could not remove old build dir: %BUILD%
                exit /b 1
            )
            echo  [OK]    Old build directory removed.
        )
    )

    echo  [INFO]  Configuring CMake...
    cmake -S "%SCRIPT_DIR%" -B "%BUILD%" ^
        -G "!GENERATOR!" ^
        !EXTRA_FLAGS! ^
        -DCMAKE_BUILD_TYPE=Release

    if errorlevel 1 (
        echo  [ERROR] CMake configuration failed.
        exit /b 1
    )
    echo  [OK]    CMake configuration succeeded.

    echo  [INFO]  Building (Release)...
    if "!GENERATOR!"=="Ninja" (
        cmake --build "%BUILD%" --config Release
    ) else (
        cmake --build "%BUILD%" --config Release --parallel
    )

    if errorlevel 1 (
        echo  [ERROR] Build failed. Check the output above for errors.
        exit /b 1
    )
    echo  [OK]    Build succeeded.
) else (
    echo  [OK]    Release artefacts already present -- skipping build.
    echo          Pass --rebuild to force recompilation.
)

echo.

:: ── 4. Verify artefacts ───────────────────────────────────────────────────────
echo [3/5] Verifying artefacts...
echo -----------------------------------------------

if not exist "%VST3_SRC%" (
    echo  [ERROR] VST3 bundle not found: %VST3_SRC%
    echo          Try running with --rebuild.
    exit /b 1
)
echo  [OK]    VST3 bundle found.

if not exist "%EXE_SRC%" (
    echo  [ERROR] Standalone executable not found: %EXE_SRC%
    echo          Try running with --rebuild.
    exit /b 1
)
echo  [OK]    Standalone .exe found.

if not exist "%SRC%\back.bmp" (
    echo  [ERROR] Missing resource: %SRC%\back.bmp
    exit /b 1
)
if not exist "%SRC%\char.bmp" (
    echo  [ERROR] Missing resource: %SRC%\char.bmp
    exit /b 1
)
echo  [OK]    Bitmap resources found.

if not exist "%SAMPLES%" (
    echo  [ERROR] Samples directory not found: %SAMPLES%
    exit /b 1
)
echo  [OK]    Samples directory found.
echo.

:: ── 5. Install VST3 bundles ───────────────────────────────────────────────────
echo [4/5] Installing VST3...
echo -----------------------------------------------

:: Helper: install a single VST3 destination
::   call :install_vst3 "<destination-path>"
goto :after_install_vst3_fn

:install_vst3
setlocal
set "DST=%~1"

echo  [INFO]  Installing VST3 to: %DST%

:: Remove old installation
if exist "%DST%" (
    rmdir /s /q "%DST%"
    if errorlevel 1 (
        echo  [ERROR] Could not remove existing bundle at: %DST%
        echo          Make sure your DAW is closed and try again.
        endlocal
        exit /b 1
    )
)

:: Create parent directory
for %%P in ("%DST%") do set "DST_PARENT=%%~dpP"
if not exist "%DST_PARENT%" mkdir "%DST_PARENT%"

:: Copy the VST3 bundle
xcopy /e /i /q /y "%VST3_SRC%" "%DST%\" >nul
if errorlevel 1 (
    echo  [ERROR] Failed to copy VST3 bundle to: %DST%
    endlocal
    exit /b 1
)
echo  [OK]    Bundle copied.

:: Copy bitmaps into Resources\
set "RES=%DST%\Contents\Resources"
if not exist "%RES%" mkdir "%RES%"
copy /y "%SRC%\back.bmp" "%RES%\" >nul
copy /y "%SRC%\char.bmp" "%RES%\" >nul
echo  [OK]    Bitmaps copied to Resources\.

:: Copy samples into Resources\samples\
set "RES_SAMPLES=%RES%\samples"
if exist "%RES_SAMPLES%" rmdir /s /q "%RES_SAMPLES%"
xcopy /e /i /q /y "%SAMPLES%" "%RES_SAMPLES%\" >nul
if errorlevel 1 (
    echo  [WARN]  Could not copy samples to: %RES_SAMPLES%
) else (
    echo  [OK]    Samples copied to Resources\samples\.
)

echo  [OK]    VST3 installed: %DST%
endlocal
exit /b 0

:after_install_vst3_fn

:: Install to %CommonProgramFiles%\VST3\ (system-wide, may need admin)
call :install_vst3 "%VST3_DST1%"
if errorlevel 1 (
    echo  [WARN]  Failed to install to system VST3 directory.
    echo          You may need to run this script as Administrator.
    echo          Continuing with user-local installation...
)

echo.

:: Install to %LOCALAPPDATA%\Programs\VstPlugins\ (user-local, no admin needed)
call :install_vst3 "%VST3_DST2%"
if errorlevel 1 (
    echo  [ERROR] Failed to install to user-local VST3 directory.
    exit /b 1
)

echo.

:: ── 6. Install Standalone to Desktop ─────────────────────────────────────────
echo [5/5] Installing Standalone to Desktop...
echo -----------------------------------------------

if exist "%EXE_DST%" del /q "%EXE_DST%"
copy /y "%EXE_SRC%" "%EXE_DST%" >nul
if errorlevel 1 (
    echo  [ERROR] Could not copy Flopster.exe to Desktop.
    exit /b 1
)
echo  [OK]    Flopster.exe copied to: %EXE_DST%
echo.

:: ── Summary ───────────────────────────────────────────────────────────────────
echo.
echo  +==============================================+
echo  ^|   Installation complete!                    ^|
echo  +==============================================+
echo.
echo   VST3 (system)  : %VST3_DST1%
echo   VST3 (user)    : %VST3_DST2%
echo   Standalone     : %EXE_DST%
echo.
echo   Next steps:
echo     - Restart your DAW and rescan plugins.
echo     - In Ableton: Options -^> Preferences -^> Plug-Ins -^> Rescan.
echo     - In FL Studio: Options -^> Manage plugins -^> Find more plugins.
echo     - In Reaper: Options -^> Preferences -^> Plug-ins -^> VST -^> Rescan.
echo     - Run Flopster.exe on the Desktop to use the standalone version.
echo.

endlocal
exit /b 0
