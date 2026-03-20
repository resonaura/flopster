@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul 2>&1

:: =============================================================================
::  Flopster — Windows Build Script
::  Builds VST3 + Standalone in Release (or Debug with --debug flag)
::  by Shiru & Resonaura
::
::  Usage: win-build.bat [--debug] [--release] [--rebuild] [--arch <arch>]
::
::  --arch <arch>   Target architecture: arm64 | x64 | x86
::                  Default on ARM host : arm64
::                  Default on x64 host : x64
::
::  Multi-arch example (on Windows ARM):
::    win-build.bat --arch arm64
::    win-build.bat --arch x64
::    win-build.bat --arch x86
:: =============================================================================

set "SCRIPT_DIR=%~dp0"
set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
for %%I in ("%SCRIPT_DIR%\..") do set "ROOT=%%~fI"
set "BUILD_TYPE=Release"
set "REBUILD=0"
set "JUCE_DIR=%ROOT%\JUCE"

:: ── Auto-detect host architecture ────────────────────────────────────────────
set "HOST_ARCH=x64"
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64"  set "HOST_ARCH=arm64"
if /i "%PROCESSOR_ARCHITEW6432%"=="ARM64"  set "HOST_ARCH=arm64"

:: Default target = host
set "TARGET_ARCH=%HOST_ARCH%"

:: ── Parse arguments ───────────────────────────────────────────────────────────
:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="--debug"   ( set "BUILD_TYPE=Debug"   & shift & goto parse_args )
if /i "%~1"=="-d"        ( set "BUILD_TYPE=Debug"   & shift & goto parse_args )
if /i "%~1"=="--release" ( set "BUILD_TYPE=Release" & shift & goto parse_args )
if /i "%~1"=="-r"        ( set "BUILD_TYPE=Release" & shift & goto parse_args )
if /i "%~1"=="--rebuild" ( set "REBUILD=1"          & shift & goto parse_args )
if /i "%~1"=="--arch" (
    if "%~2"=="" (
        echo  [ERROR] --arch requires an argument: arm64, x64, x86
        exit /b 1
    )
    set "TARGET_ARCH=%~2"
    shift & shift & goto parse_args
)
if /i "%~1"=="--help" goto show_help
if /i "%~1"=="-h"     goto show_help
echo  [warn] Unknown flag: %~1 (ignored)
shift
goto parse_args

:show_help
echo.
echo  Usage: win-build.bat [--debug^|--release] [--rebuild] [--arch ^<arch^>] [--help]
echo.
echo    --debug          Build Debug configuration
echo    --release        Build Release configuration (default)
echo    --rebuild        Remove build directory and rebuild from scratch
echo    --arch ^<arch^>    Target architecture: arm64 ^| x64 ^| x86
echo                     Default on ARM host : arm64
echo                     Default on x64 host : x64
echo    --help           Show this help
echo.
echo  Examples:
echo    win-build.bat                    -- native arch, Release
echo    win-build.bat --arch x64         -- cross-compile for x64
echo    win-build.bat --arch x86         -- cross-compile for x86
echo    win-build.bat --arch arm64       -- compile for ARM64
echo.
exit /b 0

:args_done

:: ── Normalise / validate TARGET_ARCH ─────────────────────────────────────────
if /i "!TARGET_ARCH!"=="x86_64"  set "TARGET_ARCH=x64"
if /i "!TARGET_ARCH!"=="amd64"   set "TARGET_ARCH=x64"
if /i "!TARGET_ARCH!"=="win32"   set "TARGET_ARCH=x86"
if /i "!TARGET_ARCH!"=="i686"    set "TARGET_ARCH=x86"
if /i "!TARGET_ARCH!"=="i386"    set "TARGET_ARCH=x86"

if /i "!TARGET_ARCH!" neq "arm64" (
  if /i "!TARGET_ARCH!" neq "x64" (
    if /i "!TARGET_ARCH!" neq "x86" (
      echo  [ERROR] Unknown --arch value: !TARGET_ARCH!
      echo          Valid values: arm64, x64, x86
      exit /b 1
    )
  )
)

:: ── Derive per-arch build directory ──────────────────────────────────────────
if /i "%BUILD_TYPE%"=="Debug" (
    set "BUILD_DIR=%ROOT%\build-debug-!TARGET_ARCH!"
) else (
    set "BUILD_DIR=%ROOT%\build-!TARGET_ARCH!"
)

:: ── Banner ────────────────────────────────────────────────────────────────────
echo.
echo  ============================================
echo   Flopster Build Script
echo   by Shiru ^& Resonaura
echo  ============================================
echo.
echo  [build] Build type  : %BUILD_TYPE%
echo  [build] Target arch : !TARGET_ARCH!
echo  [build] Host arch   : %HOST_ARCH%
echo  [build] Build dir   : !BUILD_DIR!
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

:: ── Locate vswhere ────────────────────────────────────────────────────────────
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "!VSWHERE!" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

:: ── Resolve CMake generator + architecture flag ───────────────────────────────
::
::  VS generator:   use -G "Visual Studio 17 2022" -A <platform>
::    arm64  → -A ARM64
::    x64    → -A x64
::    x86    → -A Win32
::
::  Ninja generator (cross-compile from ARM host):
::    We activate the VS cross-compile environment via vcvarsall.bat before
::    invoking CMake so the right CL.EXE / LINK.EXE are on PATH.
::    Then pass -DCMAKE_GENERATOR_PLATFORM is not used with Ninja; instead
::    we set CMAKE_SYSTEM_PROCESSOR so JUCE picks the right arch.
::
set "GENERATOR="
set "CMAKE_ARCH_FLAG="
set "NINJA_VCVARS_ARCH="
set "USE_NINJA=0"

:: Map TARGET_ARCH → VS platform name
if /i "!TARGET_ARCH!"=="arm64" set "VS_PLATFORM=ARM64"
if /i "!TARGET_ARCH!"=="x64"   set "VS_PLATFORM=x64"
if /i "!TARGET_ARCH!"=="x86"   set "VS_PLATFORM=Win32"

:: Map TARGET_ARCH → vcvarsall argument (for Ninja cross-compile)
::   host_arch_target_arch  e.g. arm64_x64, amd64, amd64_arm64, amd64_x86
if /i "%HOST_ARCH%"=="arm64" (
    if /i "!TARGET_ARCH!"=="arm64" set "VCVARS_ARCH=arm64"
    if /i "!TARGET_ARCH!"=="x64"   set "VCVARS_ARCH=arm64_amd64"
    if /i "!TARGET_ARCH!"=="x86"   set "VCVARS_ARCH=arm64_x86"
) else (
    if /i "!TARGET_ARCH!"=="arm64" set "VCVARS_ARCH=amd64_arm64"
    if /i "!TARGET_ARCH!"=="x64"   set "VCVARS_ARCH=amd64"
    if /i "!TARGET_ARCH!"=="x86"   set "VCVARS_ARCH=amd64_x86"
)

:: ── Try Visual Studio 2026 ────────────────────────────────────────────────────
set "VS2026_PATH="
if exist "!VSWHERE!" (
    for /f "usebackq tokens=*" %%i in (
        `"!VSWHERE!" -latest -version "[18.0,19.0)" -requires Microsoft.Component.MSBuild -property installationPath 2^>nul`
    ) do set "VS2026_PATH=%%i"
)

if defined VS2026_PATH (
    if exist "!VS2026_PATH!\MSBuild\Current\Bin\MSBuild.exe" (
        set "GENERATOR=Visual Studio 18 2026"
        set "CMAKE_ARCH_FLAG=-A !VS_PLATFORM!"
        echo  [ok] Found Visual Studio 2026
        goto generator_done
    )
)

:: ── Try Visual Studio 2022 ────────────────────────────────────────────────────
set "VS_INSTALL_PATH="
if exist "!VSWHERE!" (
    for /f "usebackq tokens=*" %%i in (
        `"!VSWHERE!" -latest -version "[17.0,18.0)" -requires Microsoft.Component.MSBuild -property installationPath 2^>nul`
    ) do set "VS_INSTALL_PATH=%%i"
)

if defined VS_INSTALL_PATH (
    if exist "!VS_INSTALL_PATH!\MSBuild\Current\Bin\MSBuild.exe" (
        set "GENERATOR=Visual Studio 17 2022"
        set "CMAKE_ARCH_FLAG=-A !VS_PLATFORM!"
        echo  [ok] Found Visual Studio 2022
        goto generator_done
    )
)

:: ── Try Visual Studio 2019 ────────────────────────────────────────────────────
set "VS2019_PATH="
if exist "!VSWHERE!" (
    for /f "usebackq tokens=*" %%i in (
        `"!VSWHERE!" -version "[16.0,17.0)" -requires Microsoft.Component.MSBuild -property installationPath 2^>nul`
    ) do set "VS2019_PATH=%%i"
)

if defined VS2019_PATH (
    if exist "!VS2019_PATH!\MSBuild\Current\Bin\MSBuild.exe" (
        set "GENERATOR=Visual Studio 16 2019"
        set "CMAKE_ARCH_FLAG=-A !VS_PLATFORM!"
        echo  [ok] Found Visual Studio 2019
        goto generator_done
    )
)

:: ── Try Ninja ─────────────────────────────────────────────────────────────────
where ninja >nul 2>&1
if not errorlevel 1 (
    set "GENERATOR=Ninja"
    set "USE_NINJA=1"
    echo  [ok] Using Ninja build system
    goto generator_done
)

echo  [ERROR] No suitable build system found.
echo          Please install one of:
echo            - Visual Studio 2022 or newer (recommended): https://visualstudio.microsoft.com/
echo            - Ninja: https://ninja-build.org/
echo            - Or: winget install Ninja-build.Ninja
exit /b 1

:generator_done

:: ── For Ninja: locate vcvarsall.bat and set the cross-compile environment ─────
::
::  Strategy:
::   1. Use vswhere to find the real install path (handles any edition/arch).
::   2. Fall back to a hardcoded list covering all known editions on both
::      x64 and ARM64 hosts (VS installs to Program Files on ARM64, not x86).
::
set "VCVARS="
if "!USE_NINJA!"=="1" (

    :: -- 1. Try vswhere (most reliable) ----------------------------------------
    if exist "!VSWHERE!" (
        set "_VS_PATH="
        for /f "usebackq tokens=*" %%P in (
            `"!VSWHERE!" -latest -requires Microsoft.VisualCpp.Tools.HostX64.TargetARM64 -property installationPath 2^>nul`
        ) do set "_VS_PATH=%%P"

        :: Broader query if the specific component wasn't found
        if not defined _VS_PATH (
            for /f "usebackq tokens=*" %%P in (
                `"!VSWHERE!" -latest -requires Microsoft.Component.MSBuild -property installationPath 2^>nul`
            ) do set "_VS_PATH=%%P"
        )

        if defined _VS_PATH (
            if exist "!_VS_PATH!\VC\Auxiliary\Build\vcvarsall.bat" (
                set "VCVARS=!_VS_PATH!\VC\Auxiliary\Build\vcvarsall.bat"
                goto vcvars_found
            )
        )
    )

    :: -- 2. Hardcoded fallback list (VS 2026 + 2022 + 2019, all editions, x64 + ARM64 hosts)
    for %%D in (
        "%ProgramFiles%\Microsoft Visual Studio\2026\Enterprise"
        "%ProgramFiles%\Microsoft Visual Studio\2026\Professional"
        "%ProgramFiles%\Microsoft Visual Studio\2026\Community"
        "%ProgramFiles%\Microsoft Visual Studio\2026\BuildTools"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2026\Enterprise"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2026\Professional"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2026\Community"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2026\BuildTools"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Professional"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Community"
        "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Enterprise"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Professional"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools"
        "%ProgramFiles%\Microsoft Visual Studio\2019\Enterprise"
        "%ProgramFiles%\Microsoft Visual Studio\2019\Professional"
        "%ProgramFiles%\Microsoft Visual Studio\2019\Community"
        "%ProgramFiles%\Microsoft Visual Studio\2019\BuildTools"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Enterprise"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Professional"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools"
    ) do (
        if exist "%%~D\VC\Auxiliary\Build\vcvarsall.bat" (
            set "VCVARS=%%~D\VC\Auxiliary\Build\vcvarsall.bat"
            goto vcvars_found
        )
    )

    echo  [WARN] vcvarsall.bat not found — Ninja will use PATH as-is.
    echo         Cross-compilation may not work correctly.
    echo         Install Visual Studio 2022 Build Tools:
    echo           https://aka.ms/vs/17/release/vs_buildtools.exe
    echo         Select workload: "Desktop development with C++"
    goto vcvars_skip
)
goto vcvars_skip

:vcvars_found
echo  [ok] vcvarsall.bat found: !VCVARS!
echo  [ok] Activating MSVC environment for: !VCVARS_ARCH!
call "!VCVARS!" !VCVARS_ARCH!
if errorlevel 1 (
    echo  [ERROR] vcvarsall.bat failed for arch: !VCVARS_ARCH!
    exit /b 1
)
echo  [ok] MSVC environment activated.

:vcvars_skip

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
if "!REBUILD!"=="1" (
    if exist "!BUILD_DIR!" (
        echo  [build] Removing existing build directory...
        rmdir /s /q "!BUILD_DIR!"
        echo  [ok] Removed !BUILD_DIR!
    )
)

:: ── CMake configure ──────────────────────────────────────────────────────────
set "NEED_CONFIGURE=0"
if not exist "!BUILD_DIR!\CMakeCache.txt" set "NEED_CONFIGURE=1"

if "!NEED_CONFIGURE!"=="1" (
    echo.
    echo  [build] Configuring with CMake...
    echo          Generator : !GENERATOR!
    echo          Arch flag : !CMAKE_ARCH_FLAG!
    echo.

    if not exist "!BUILD_DIR!" mkdir "!BUILD_DIR!"

    if "!USE_NINJA!"=="1" (
        :: Ninja: pass arch via CMAKE_SYSTEM_PROCESSOR
        cmake -S "%ROOT%" -B "!BUILD_DIR!" ^
            -G "!GENERATOR!" ^
            -DCMAKE_BUILD_TYPE=!BUILD_TYPE! ^
            -DCMAKE_SYSTEM_PROCESSOR=!TARGET_ARCH! ^
            -DCMAKE_SYSTEM_NAME=Windows
    ) else (
        cmake -S "%ROOT%" -B "!BUILD_DIR!" ^
            -G "!GENERATOR!" ^
            !CMAKE_ARCH_FLAG! ^
            -DCMAKE_BUILD_TYPE=!BUILD_TYPE!
    )

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
echo  [build] Building Flopster (%BUILD_TYPE%, !TARGET_ARCH!)...
echo.

cmake --build "!BUILD_DIR!" --config !BUILD_TYPE! --parallel

if errorlevel 1 (
    echo.
    echo  [ERROR] Build failed.
    echo          Check output above for errors.
    exit /b 1
)

echo.

:: ── Verify artefacts ─────────────────────────────────────────────────────────
echo  [build] Verifying artefacts...

set "ARTEFACT_BASE=!BUILD_DIR!\Flopster_artefacts\!BUILD_TYPE!"
set "ALL_OK=1"

:: VST3 — the inner DLL name encodes the arch: arm64-win, x86_64-win, x86-win
if /i "!TARGET_ARCH!"=="arm64" set "VST3_ARCH_DIR=arm64-win"
if /i "!TARGET_ARCH!"=="x64"   set "VST3_ARCH_DIR=x86_64-win"
if /i "!TARGET_ARCH!"=="x86"   set "VST3_ARCH_DIR=x86-win"

if exist "!ARTEFACT_BASE!\VST3\Flopster.vst3\Contents\!VST3_ARCH_DIR!\Flopster.vst3" (
    echo  [ok] VST3\Flopster.vst3\Contents\!VST3_ARCH_DIR!\Flopster.vst3
) else if exist "!ARTEFACT_BASE!\VST3\Flopster.vst3" (
    echo  [ok] VST3\Flopster.vst3  (bundle present)
) else (
    echo  [warn] VST3 artefact not found at:
    echo         !ARTEFACT_BASE!\VST3\Flopster.vst3
    set "ALL_OK=0"
)

:: Standalone
if exist "!ARTEFACT_BASE!\Standalone\Flopster.exe" (
    echo  [ok] Standalone\Flopster.exe
) else (
    echo  [warn] Standalone artefact not found at:
    echo         !ARTEFACT_BASE!\Standalone\Flopster.exe
    set "ALL_OK=0"
)

echo.

if "!ALL_OK!"=="0" (
    echo  [warn] Some artefacts missing -- check build output above.
) else (
    echo  ============================================
    echo   Build complete!
    echo  ============================================
)

echo.
echo   Target arch : !TARGET_ARCH!
echo   VST3        -^>  !ARTEFACT_BASE!\VST3\Flopster.vst3
echo   Standalone  -^>  !ARTEFACT_BASE!\Standalone\Flopster.exe
echo.
echo   Run win-install.bat --arch !TARGET_ARCH! to install into the system.
echo.

endlocal
