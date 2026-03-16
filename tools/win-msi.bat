@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul 2>&1

:: =============================================================================
::  Flopster — Windows MSI Installer Builder
::  Builds a proper Windows Installer (.msi) using WiX Toolset.
::
::  Prefers WiX v4 (dotnet tool), falls back to WiX v3 (candle/light).
::  Optionally builds the plugin from source if artefacts are missing.
::
::  Usage:
::    win-msi.bat [--rebuild] [--no-build] [--arch <arch>] [--out <dir>] [--help]
::
::  Flags:
::    --rebuild        Force clean rebuild of plugin before packaging
::    --no-build       Skip build entirely; fail if artefacts are missing
::    --arch <arch>    Target architecture: arm64 | x64 | x86
::                     Default on ARM host : arm64
::                     Default on x64 host : x64
::    --out <dir>      Output directory for the .msi  (default: dist\)
::    --help, -h       Show this help and exit
::
::  Requirements:
::    cmake in PATH
::    Visual Studio 2022  OR  Ninja
::    WiX v4:  dotnet tool install --global wix
::      OR
::    WiX v3:  https://wixtoolset.org/releases/  (candle/light in PATH)
::
::  Output: <out-dir>\Flopster-1.24.msi
::
::  Product    : Flopster
::  Vendor     : Shiru & Resonaura
::  Version    : 1.24.0.0
::  UpgradeCode: {A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
:: =============================================================================

:: ── Identity & version ────────────────────────────────────────────────────────
set "PRODUCT_NAME=Flopster"
set "MANUFACTURER=Shiru and Resonaura"
for /f "delims=" %%V in ('powershell -NoProfile -Command "(Get-Content '%~dp0..\package.json' | ConvertFrom-Json).version"') do set "VERSION_SHORT=%%V"
set "VERSION=%VERSION_SHORT%.0.0"
set "UPGRADE_CODE={A1B2C3D4-E5F6-7890-ABCD-EF1234567890}"

:: ── Paths ─────────────────────────────────────────────────────────────────────
set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
for %%I in ("%SCRIPT_DIR%\..") do set "ROOT=%%~fI"

set "ASSETS=%ROOT%\assets"
set "SAMPLES=%ROOT%\samples"

:: ── Auto-detect host architecture ────────────────────────────────────────────
set "HOST_ARCH=x64"
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64"  set "HOST_ARCH=arm64"
if /i "%PROCESSOR_ARCHITEW6432%"=="ARM64"  set "HOST_ARCH=arm64"

:: Default target = host
set "TARGET_ARCH=%HOST_ARCH%"

:: ── Defaults ──────────────────────────────────────────────────────────────────
set "REBUILD=0"
set "NO_BUILD=0"
set "OUT_DIR=%ROOT%\dist"

:: ── Parse arguments ───────────────────────────────────────────────────────────
:parse_args
if "%~1"=="" goto :done_args

if /i "%~1"=="--help"     goto :show_help
if /i "%~1"=="-h"         goto :show_help

if /i "%~1"=="--rebuild" (
    set "REBUILD=1"
    shift & goto :parse_args
)
if /i "%~1"=="--no-build" (
    set "NO_BUILD=1"
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
if /i "%~1"=="--out" (
    if "%~2"=="" (
        echo  [ERROR] --out requires a directory argument.
        exit /b 1
    )
    set "OUT_DIR=%~2"
    shift & shift & goto :parse_args
)

echo  [WARN]  Unknown argument: %~1  (ignoring)
shift & goto :parse_args

:done_args

if "!REBUILD!"=="1" if "!NO_BUILD!"=="1" (
    echo  [ERROR] --rebuild and --no-build are mutually exclusive.
    exit /b 1
)

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

:: ── Derive per-arch build/artefact paths ─────────────────────────────────────
set "BUILD=%ROOT%\build-!TARGET_ARCH!"
set "ARTEFACTS=%BUILD%\Flopster_artefacts\Release"
set "VST3_SRC=%ARTEFACTS%\VST3\Flopster.vst3"
set "EXE_SRC=%ARTEFACTS%\Standalone\Flopster.exe"

:: WiX working directory — all generated files land here
set "WIX_WORK=%BUILD%\wix-work"
set "STAGE=%WIX_WORK%\stage"

:: MSI output filename includes arch
set "MSI_NAME=Flopster-%VERSION_SHORT%-!TARGET_ARCH!.msi"

:: ── Banner ────────────────────────────────────────────────────────────────────
echo(
echo  +=======================================================+
echo  ^|   Flopster MSI Builder  v%VERSION_SHORT%                       ^|
echo  ^|   by Shiru ^& Resonaura                               ^|
echo  +=======================================================+
echo(
echo  [INFO]  Target arch : !TARGET_ARCH!
echo  [INFO]  Host arch   : %HOST_ARCH%
echo  [INFO]  MSI name    : !MSI_NAME!
echo  [INFO]  Build dir   : !BUILD!
echo(

:: =============================================================================
:: STEP 1 — Check prerequisites
:: =============================================================================
echo [1/6] Checking prerequisites...
echo -----------------------------------------------

:: ── cmake ────────────────────────────────────────────────────────────────────
cmake --version >nul 2>&1
if errorlevel 1 (
    echo  [ERROR] cmake not found in PATH.
    echo          Download from: https://cmake.org/download/
    exit /b 1
)
for /f "tokens=3" %%V in ('cmake --version 2^>^&1 ^| findstr /i "cmake version"') do (
    echo  [OK]    cmake %%V
)

:: ── Detect build toolchain: VS 2022 preferred, Ninja fallback ────────────────
set "HAS_NINJA=0"
set "GENERATOR="
set "EXTRA_FLAGS="
set "USE_NINJA=0"

:: Map TARGET_ARCH → VS platform name
if /i "!TARGET_ARCH!"=="arm64" set "VS_PLATFORM=ARM64"
if /i "!TARGET_ARCH!"=="x64"   set "VS_PLATFORM=x64"
if /i "!TARGET_ARCH!"=="x86"   set "VS_PLATFORM=Win32"

:: Map TARGET_ARCH → vcvarsall argument (for Ninja cross-compile)
if /i "%HOST_ARCH%"=="arm64" (
    if /i "!TARGET_ARCH!"=="arm64" set "VCVARS_ARCH=arm64"
    if /i "!TARGET_ARCH!"=="x64"   set "VCVARS_ARCH=arm64_amd64"
    if /i "!TARGET_ARCH!"=="x86"   set "VCVARS_ARCH=arm64_x86"
) else (
    if /i "!TARGET_ARCH!"=="arm64" set "VCVARS_ARCH=amd64_arm64"
    if /i "!TARGET_ARCH!"=="x64"   set "VCVARS_ARCH=amd64"
    if /i "!TARGET_ARCH!"=="x86"   set "VCVARS_ARCH=amd64_x86"
)

ninja --version >nul 2>&1
if not errorlevel 1 (
    set "HAS_NINJA=1"
    for /f %%V in ('ninja --version 2^>^&1') do echo  [OK]    ninja %%V
) else (
    echo  [INFO]  ninja not found — will try Visual Studio generator.
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "!VSWHERE!" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

set "VS2022_PATH="
if exist "!VSWHERE!" (
    for /f "usebackq tokens=*" %%P in (
        `"!VSWHERE!" -latest -version "[17.0,18.0)" -property installationPath 2^>nul`
    ) do set "VS2022_PATH=%%P"
)

if defined VS2022_PATH (
    echo  [OK]    Visual Studio 2022 at: !VS2022_PATH!
    set "GENERATOR=Visual Studio 17 2022"
    set "EXTRA_FLAGS=-A !VS_PLATFORM!"
) else if "!HAS_NINJA!"=="1" (
    echo  [INFO]  VS 2022 not found — using Ninja.
    set "GENERATOR=Ninja"
    set "USE_NINJA=1"
    set "EXTRA_FLAGS=-DCMAKE_BUILD_TYPE=Release"
) else (
    echo  [ERROR] Neither Visual Studio 2022 nor Ninja found.
    echo          Install one of:
    echo            Visual Studio 2022: https://visualstudio.microsoft.com/
    echo            Ninja (winget):     winget install Ninja-build.Ninja
    exit /b 1
)
echo  [INFO]  Generator: !GENERATOR!

:: ── Detect WiX: v4 preferred, v3 fallback ────────────────────────────────────
set "WIX_VER=0"
set "WIX4_CMD="
set "WIX3_CANDLE="
set "WIX3_LIGHT="

:: WiX v4 via dotnet tool
wix --version >nul 2>&1
if not errorlevel 1 (
    for /f "tokens=*" %%V in ('wix --version 2^>^&1') do set "WIX4_VER=%%V"
    set "WIX4_CMD=wix"
    set "WIX_VER=4"
    echo  [OK]    WiX v4 found: !WIX4_VER!
    goto :wix_found
)

:: WiX v3 in PATH
candle.exe -? >nul 2>&1
if not errorlevel 1 (
    set "WIX3_CANDLE=candle.exe"
    set "WIX3_LIGHT=light.exe"
    set "WIX_VER=3"
    echo  [OK]    WiX v3 found in PATH.
    goto :wix_found
)

:: WiX v3 at common install locations
for %%D in (
    "%ProgramFiles(x86)%\WiX Toolset v3.14\bin"
    "%ProgramFiles(x86)%\WiX Toolset v3.11\bin"
    "%ProgramFiles(x86)%\WiX Toolset v3.10\bin"
    "%ProgramFiles%\WiX Toolset v3.14\bin"
    "%ProgramFiles%\WiX Toolset v3.11\bin"
) do (
    if exist "%%~D\candle.exe" (
        set "WIX3_CANDLE=%%~D\candle.exe"
        set "WIX3_LIGHT=%%~D\light.exe"
        set "WIX_VER=3"
        echo  [OK]    WiX v3 found at: %%~D
        goto :wix_found
    )
)

:: Neither found — print full instructions and bail
echo(
echo  [ERROR] WiX Toolset not found.  Please install one of the following:
echo(
echo    WiX v4 (recommended — requires .NET SDK):
echo      dotnet tool install --global wix
echo      .NET SDK: https://dotnet.microsoft.com/download
echo(
echo    WiX v3 (legacy):
echo      https://wixtoolset.org/releases/
echo      After install, add  candle.exe / light.exe  to your PATH,
echo      OR place them in:  %%ProgramFiles(x86)%%\WiX Toolset v3.14\bin\
echo(
exit /b 1

:wix_found
echo  [INFO]  Using WiX version : !WIX_VER!
echo  [INFO]  CMake generator   : !GENERATOR!
echo(

:: ── For Ninja: locate vcvarsall.bat and activate cross-compile environment ───
if "!USE_NINJA!"=="1" (
    set "VCVARS="
    for %%D in (
        "!VS2022_PATH!"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Professional"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Community"
        "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Enterprise"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Professional"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Community"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\BuildTools"
    ) do (
        if exist "%%~D\VC\Auxiliary\Build\vcvarsall.bat" (
            set "VCVARS=%%~D\VC\Auxiliary\Build\vcvarsall.bat"
            goto :msi_vcvars_found
        )
    )
    echo  [WARN]  vcvarsall.bat not found — Ninja will use PATH as-is.
    echo          Cross-compilation may not work correctly.
    goto :msi_vcvars_skip
)
goto :msi_vcvars_skip

:msi_vcvars_found
echo  [OK]    vcvarsall.bat: !VCVARS!
echo  [OK]    Activating MSVC environment for: !VCVARS_ARCH!
call "!VCVARS!" !VCVARS_ARCH!
if errorlevel 1 (
    echo  [ERROR] vcvarsall.bat failed for arch: !VCVARS_ARCH!
    exit /b 1
)
echo  [OK]    MSVC environment activated.

:msi_vcvars_skip
echo(

:: =============================================================================
:: STEP 2 — Build plugin (if needed)
:: =============================================================================
echo [2/6] Build...
echo -----------------------------------------------

if "!NO_BUILD!"=="1" (
    echo  [INFO]  --no-build: skipping build step.
    goto :after_build
)

set "NEED_BUILD=0"
if "!REBUILD!"=="1"           set "NEED_BUILD=1"
if not exist "%EXE_SRC%"      set "NEED_BUILD=1"
if not exist "%VST3_SRC%"     set "NEED_BUILD=1"

if "!NEED_BUILD!"=="0" (
    echo  [OK]    Release artefacts already present — skipping build.
    echo          Pass --rebuild to force recompilation.
    goto :after_build
)

if "!REBUILD!"=="1" (
    if exist "%BUILD%" (
        echo  [INFO]  --rebuild: removing old build directory...
        rmdir /s /q "%BUILD%"
        if errorlevel 1 (
            echo  [ERROR] Could not remove: %BUILD%
            exit /b 1
        )
        echo  [OK]    Old build removed.
    )
)

echo  [INFO]  CMake configure (arch: !TARGET_ARCH!)...
if "!USE_NINJA!"=="1" (
    cmake -S "%ROOT%" -B "%BUILD%" ^
        -G "!GENERATOR!" ^
        -DCMAKE_BUILD_TYPE=Release ^
        -DCMAKE_SYSTEM_PROCESSOR=!TARGET_ARCH! ^
        -DCMAKE_SYSTEM_NAME=Windows
) else (
    cmake -S "%ROOT%" -B "%BUILD%" ^
        -G "!GENERATOR!" ^
        !EXTRA_FLAGS! ^
        -DCMAKE_BUILD_TYPE=Release
)
if errorlevel 1 (
    echo  [ERROR] CMake configuration failed.
    exit /b 1
)

echo  [INFO]  CMake build (Release, !TARGET_ARCH!)...
cmake --build "%BUILD%" --config Release --parallel
if errorlevel 1 (
    echo  [ERROR] Build failed.
    exit /b 1
)
echo  [OK]    Build succeeded.

:after_build
echo(

:: =============================================================================
:: STEP 3 — Verify artefacts
:: =============================================================================
echo [3/6] Verifying artefacts...
echo -----------------------------------------------

if not exist "!VST3_SRC!" (
    echo  [ERROR] VST3 bundle not found:   !VST3_SRC!
    echo          Run without --no-build, or pass --rebuild.
    exit /b 1
)
echo  [OK]    VST3 bundle:  %VST3_SRC%

if not exist "!EXE_SRC!" (
    echo  [ERROR] Standalone not found:    !EXE_SRC!
    echo          Run without --no-build, or pass --rebuild.
    exit /b 1
)
echo  [OK]    Standalone:   %EXE_SRC%



if not exist "%SAMPLES%" (
    echo  [ERROR] Samples directory not found: %SAMPLES%
    exit /b 1
)
echo  [OK]    Samples directory found.

:: Optional banner image (app.png) — used for WiX UI banner/background
set "HAS_BANNER=0"
if exist "%ASSETS%\app.png" (
    set "HAS_BANNER=1"
    echo  [OK]    Banner image (app.png) found.
) else (
    echo  [WARN]  app.png not found — installer will use default WiX banner.
)
echo(

:: =============================================================================
:: STEP 4 — Stage files into install layout
:: =============================================================================
echo [4/6] Staging installation layout...
echo -----------------------------------------------
echo(
echo   Stage layout:
echo     stage\vst3\Flopster.vst3\   -^>  %%CommonProgramFiles%%\VST3\Flopster.vst3\
echo     stage\standalone\           -^>  %%ProgramFiles%%\Flopster\
echo(

:: Clean and recreate the WiX work directory
if exist "%WIX_WORK%" rmdir /s /q "%WIX_WORK%"
mkdir "%WIX_WORK%"
mkdir "%STAGE%"

:: ── VST3 bundle ──────────────────────────────────────────────────────────────
set "STAGE_VST3=%STAGE%\vst3\Flopster.vst3"
mkdir "%STAGE%\vst3"
mkdir "%STAGE_VST3%"

echo  [INFO]  Staging VST3 bundle...
xcopy /e /i /q /y "%VST3_SRC%" "%STAGE_VST3%\" >nul
if errorlevel 1 (
    echo  [ERROR] Failed to stage VST3 bundle.
    exit /b 1
)

:: Inject resources into VST3 bundle (Contents\Resources)
set "VST3_RES=%STAGE_VST3%\Contents\Resources"
if not exist "%VST3_RES%" mkdir "%VST3_RES%"
if exist "%ASSETS%\scanlines.png" copy /y "%ASSETS%\scanlines.png" "%VST3_RES%\" >nul
xcopy /e /i /q /y "%SAMPLES%" "%VST3_RES%\samples\" >nul
echo  [OK]    VST3 staged (with resources + samples).

:: ── Standalone application ───────────────────────────────────────────────────
set "STAGE_SA=%STAGE%\standalone"
mkdir "%STAGE_SA%"

echo  [INFO]  Staging Standalone application...
copy /y "%EXE_SRC%" "%STAGE_SA%\Flopster.exe" >nul
if errorlevel 1 (
    echo  [ERROR] Failed to stage Flopster.exe.
    exit /b 1
)
if exist "%ASSETS%\scanlines.png" copy /y "%ASSETS%\scanlines.png" "%STAGE_SA%\" >nul
xcopy /e /i /q /y "%SAMPLES%" "%STAGE_SA%\samples\" >nul
echo  [OK]    Standalone staged (with resources + samples).

:: ── Banner image ─────────────────────────────────────────────────────────────
if "!HAS_BANNER!"=="1" (
    copy /y "%ASSETS%\app.png" "%WIX_WORK%\banner.png" >nul
    echo  [OK]    Banner image staged.
)
echo(

:: =============================================================================
:: STEP 5 — Generate WiX source file (.wxs) via PowerShell
:: =============================================================================
echo [5/6] Generating WiX source file...
echo -----------------------------------------------

::
:: Strategy: write a self-contained PowerShell script to disk first, then
:: invoke it.  This avoids the cmd.exe escaping nightmare of trying to pass
:: a here-string with backticks, dollar signs, and angle brackets inline.
::
:: The PS1 script:
::   • Recursively walks the staged directories with Get-ChildItem -Recurse.
::   • Generates deterministic Component GUIDs seeded by the relative path
::     (MD5-based) so repeated builds produce stable GUIDs for the same files.
::   • Emits one <Component> per directory that contains files (WiX best-practice).
::   • Writes the complete .wxs with correct namespace for WiX v3 or v4.
::

set "WXS=%WIX_WORK%\Flopster.wxs"
set "PS1=%WIX_WORK%\gen_wxs.ps1"
set "LICENSE_RTF=%WIX_WORK%\License.rtf"

:: Convert backslashes to forward slashes for safe embedding in the PS1
set "STAGE_VST3_PS=%STAGE_VST3:\=/%"
set "STAGE_SA_PS=%STAGE_SA:\=/%"
set "WXS_PS=%WXS:\=/%"
set "WIX_WORK_PS=%WIX_WORK:\=/%"

:: ── Write gen_wxs.ps1 ─────────────────────────────────────────────────────────
:: Using (  ) redirection block + echo to write each line.
:: Blank lines use  echo(  — the only form guaranteed NOT to print "ECHO is on."
(
echo $ErrorActionPreference = 'Stop'
echo(
echo # ---------------------------------------------------------------------------
echo # Inputs injected by make_msi.bat
echo # ---------------------------------------------------------------------------
echo $WixVer      = '%WIX_VER%'
echo $WxsPath     = '%WXS_PS%'
echo $StageVst3   = '%STAGE_VST3_PS%'
echo $StageSa     = '%STAGE_SA_PS%'
echo $WixWorkDir  = '%WIX_WORK_PS%'
echo $Version     = '%VERSION%'
echo $UpgradeCode = '%UPGRADE_CODE%'
echo $HasBanner   = [bool]%HAS_BANNER%
echo(
echo # ---------------------------------------------------------------------------
echo # Get-DeterministicGuid : stable GUID seeded by a string (MD5-based)
echo # ---------------------------------------------------------------------------
echo function Get-DeterministicGuid([string]$seed) {
echo     $bytes = [System.Text.Encoding]::UTF8.GetBytes($seed)
echo     $hash  = [System.Security.Cryptography.MD5]::Create().ComputeHash($bytes)
echo     return [System.Guid]::new($hash).ToString('B').ToUpper()
echo }
echo(
echo # ---------------------------------------------------------------------------
echo # Build-DirXml : recursively emit WiX Directory/Component/File XML
echo #   Returns a List[string] of XML lines (no trailing newline per line).
echo #   One Component per directory that contains files (WiX best practice).
echo #   The first File in each Component is KeyPath="yes".
echo # ---------------------------------------------------------------------------
echo function Build-DirXml {
echo     param(
echo         [string]$PhysicalPath,
echo         [string]$DirId,
echo         [ref]$CompIds,
echo         [int]$Indent = 12
echo     )
echo     $pad   = ' ' * $Indent
echo     $lines = [System.Collections.Generic.List[string]]::new()
echo(
echo     $items = Get-ChildItem -LiteralPath $PhysicalPath
echo     $files = @($items ^| Where-Object { -not $_.PSIsContainer })
echo     $dirs  = @($items ^| Where-Object { $_.PSIsContainer })
echo(
echo     if ($files.Count -gt 0) {
echo         $compId   = ('comp_' + $DirId) -replace '[^a-zA-Z0-9_]', '_'
echo         $compGuid = Get-DeterministicGuid "comp:$DirId"
echo         $CompIds.Value.Add($compId) ^| Out-Null
echo(
echo         $lines.Add("$pad<Component Id=`"$compId`" Guid=`"$compGuid`">")
echo         $first = $true
echo         foreach ($f in $files) {
echo             $fileId  = ('file_' + $DirId + '_' + $f.Name) -replace '[^a-zA-Z0-9_]', '_'
echo             $keyPath = if ($first) { 'yes' } else { 'no' }
echo             $lines.Add("$pad    <File Id=`"$fileId`" Source=`"$($f.FullName)`" KeyPath=`"$keyPath`" />")
echo             $first = $false
echo         }
echo         $lines.Add("$pad</Component>")
echo     }
echo(
echo     foreach ($d in $dirs) {
echo         $childId = $DirId + '_' + ($d.Name -replace '[^a-zA-Z0-9_]', '_')
echo         $lines.Add("$pad<Directory Id=`"$childId`" Name=`"$($d.Name)`">")
echo         $childLines = Build-DirXml -PhysicalPath $d.FullName -DirId $childId -CompIds $CompIds -Indent ($Indent + 4)
echo         foreach ($l in $childLines) { $lines.Add($l) }
echo         $lines.Add("$pad</Directory>")
echo     }
echo(
echo     return ,$lines
echo }
echo(
echo # ---------------------------------------------------------------------------
echo # Walk VST3 bundle tree
echo # ---------------------------------------------------------------------------
echo $vst3CompIds = [System.Collections.Generic.List[string]]::new()
echo $vst3Lines   = Build-DirXml -PhysicalPath $StageVst3 -DirId 'dir_VST3Bundle' -CompIds ([ref]$vst3CompIds) -Indent 12
echo $vst3Xml     = $vst3Lines -join "`r`n"
echo(
echo # ---------------------------------------------------------------------------
echo # Walk Standalone directory tree
echo # ---------------------------------------------------------------------------
echo $saCompIds = [System.Collections.Generic.List[string]]::new()
echo $saLines   = Build-DirXml -PhysicalPath $StageSa -DirId 'dir_SARoot' -CompIds ([ref]$saCompIds) -Indent 12
echo $saXml     = $saLines -join "`r`n"
echo(
echo # ---------------------------------------------------------------------------
echo # Component refs for Feature elements
echo # ---------------------------------------------------------------------------
echo $pad16     = ' ' * 16
echo $vst3Refs  = ($vst3CompIds ^| ForEach-Object { "$pad16<ComponentRef Id=`"$_`" />" }) -join "`r`n"
echo $saRefs    = ($saCompIds   ^| ForEach-Object { "$pad16<ComponentRef Id=`"$_`" />" }) -join "`r`n"
echo(
echo $scGuid = Get-DeterministicGuid 'shortcut:Flopster.Desktop'
echo(
echo # ---------------------------------------------------------------------------
echo # WiX v3 vs v4 schema differences
echo # ---------------------------------------------------------------------------
echo # WiX v4:
echo #   - Namespace  : http://wixtoolset.org/schemas/v4/wxs
echo #   - No <Product> wrapper; top-level <Package> carries all attributes
echo #     including UpgradeCode.
echo #   - <MajorUpgrade> is a direct child of <Package>.
echo #   - UI extension ref: WixToolset.UI.wixext
echo # WiX v3:
echo #   - Namespace  : http://schemas.microsoft.com/wix/2006/wi
echo #   - <Product> wraps everything; <Package> is a self-closing child.
echo #   - UI extension ref: WixUIExtension  (loaded via -ext on light.exe)
echo # ---------------------------------------------------------------------------
echo(
echo if ($WixVer -eq '4') {
echo(
echo $wxsContent = @"
echo ^<?xml version="1.0" encoding="UTF-8"?^>
echo ^<!--
echo   Flopster.wxs - Auto-generated by make_msi.bat. Do not edit by hand.
echo   Product  : Flopster $Version
echo   Vendor   : Shiru and Resonaura
echo   Generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
echo --^>
echo ^<Wix xmlns="http://wixtoolset.org/schemas/v4/wxs"
echo      xmlns:ui="http://wixtoolset.org/schemas/v4/wxs/ui"^>
echo(
echo   ^<Package Name="Flopster"
echo            Language="1033"
echo            Version="$Version"
echo            Manufacturer="Shiru and Resonaura"
echo            InstallerVersion="500"
echo            UpgradeCode="$UpgradeCode"
echo            Scope="perMachine"
echo            Compressed="yes"^>
echo(
echo     ^<MajorUpgrade DowngradeErrorMessage="A newer version of Flopster is already installed." /^>
echo(
echo     ^<MediaTemplate EmbedCab="yes" /^>
echo(
echo     ^<Property Id="WIXUI_INSTALLDIR" Value="INSTALLDIR_SA" /^>
echo     ^<Property Id="ARPPRODUCTICON"   Value="FlopsterIcon" /^>
echo     ^<Property Id="ARPHELPLINK"      Value="https://github.com/resonaura/flopster" /^>
echo     ^<Property Id="ARPURLINFOABOUT"  Value="https://github.com/resonaura/flopster" /^>
echo(
echo     ^<Icon Id="FlopsterIcon" SourceFile="%WIX_WORK%\banner.png" /^>
echo(
echo     ^<StandardDirectory Id="CommonFilesFolder"^>
echo       ^<Directory Id="dir_VST3Root" Name="VST3"^>
echo         ^<Directory Id="dir_VST3Bundle" Name="Flopster.vst3"^>
echo $vst3Xml
echo         ^</Directory^>
echo       ^</Directory^>
echo     ^</StandardDirectory^>
echo(
echo     ^<StandardDirectory Id="ProgramFilesFolder"^>
echo       ^<Directory Id="INSTALLDIR_SA" Name="Flopster"^>
echo $saXml
echo       ^</Directory^>
echo     ^</StandardDirectory^>
echo(
echo     ^<StandardDirectory Id="DesktopFolder"^>
echo       ^<Component Id="comp_DesktopShortcut" Guid="$scGuid"^>
echo         ^<Shortcut Id="sc_Desktop_Flopster"
echo                   Name="Flopster"
echo                   Description="Flopster - floppy drive instrument by Shiru and Resonaura"
echo                   Target="[INSTALLDIR_SA]Flopster.exe"
echo                   WorkingDirectory="INSTALLDIR_SA"
echo                   Icon="FlopsterIcon"
echo                   IconIndex="0" /^>
echo         ^<RegistryValue Root="HKCU"
echo                        Key="Software\Shiru\Flopster"
echo                        Name="DesktopShortcut"
echo                        Type="integer"
echo                        Value="1"
echo                        KeyPath="yes" /^>
echo       ^</Component^>
echo     ^</StandardDirectory^>
echo(
echo     ^<Feature Id="FeatureAll"
echo              Title="Flopster"
echo              Description="Install Flopster audio plugin."
echo              Level="1"
echo              ConfigurableDirectory="INSTALLDIR_SA"
echo              Display="expand"
echo              Absent="disallow"^>
echo(
echo       ^<Feature Id="FeatureVST3"
echo                Title="VST3 Plugin"
echo                Description="Installs the VST3 plugin to %CommonProgramFiles%\VST3\ for use in any VST3-compatible DAW."
echo                Level="1"
echo                AllowAbsent="yes"^>
echo $vst3Refs
echo       ^</Feature^>
echo(
echo       ^<Feature Id="FeatureStandalone"
echo                Title="Standalone Application"
echo                Description="Installs the standalone Flopster application and a Desktop shortcut."
echo                Level="1"
echo                AllowAbsent="yes"^>
echo $saRefs
echo         ^<ComponentRef Id="comp_DesktopShortcut" /^>
echo       ^</Feature^>
echo(
echo     ^</Feature^>
echo(
echo     ^<ui:WixUI Id="WixUI_FeatureTree" /^>
echo     ^<WixVariable Id="WixUILicenseRtf" Value="License.rtf" /^>
echo(
echo   ^</Package^>
echo(
echo ^</Wix^>
echo "@
echo(
echo } else {
echo(
echo $wxsContent = @"
echo ^<?xml version="1.0" encoding="UTF-8"?^>
echo ^<!--
echo   Flopster.wxs - Auto-generated by make_msi.bat. Do not edit by hand.
echo   Product  : Flopster $Version
echo   Vendor   : Shiru and Resonaura
echo   Generated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
echo --^>
echo ^<Wix xmlns="http://schemas.microsoft.com/wix/2006/wi"^>
echo(
echo   ^<Product Id="*"
echo            Name="Flopster"
echo            Language="1033"
echo            Version="$Version"
echo            Manufacturer="Shiru and Resonaura"
echo            UpgradeCode="$UpgradeCode"^>
echo(
echo     ^<Package InstallerVersion="500"
echo             InstallScope="perMachine"
echo             Compressed="yes" /^>
echo(
echo     ^<MajorUpgrade DowngradeErrorMessage="A newer version of Flopster is already installed." /^>
echo(
echo     ^<Media Id="1" Cabinet="Flopster.cab" EmbedCab="yes" /^>
echo(
echo     ^<Property Id="WIXUI_INSTALLDIR" Value="INSTALLDIR_SA" /^>
echo     ^<Property Id="ARPPRODUCTICON"   Value="FlopsterIcon" /^>
echo     ^<Property Id="ARPHELPLINK"      Value="https://github.com/resonaura/flopster" /^>
echo     ^<Property Id="ARPURLINFOABOUT"  Value="https://github.com/resonaura/flopster" /^>
echo(
echo     ^<Icon Id="FlopsterIcon" SourceFile="%WIX_WORK%\banner.png" /^>
echo(
echo     ^<Directory Id="TARGETDIR" Name="SourceDir"^>
echo(
echo       ^<Directory Id="CommonFilesFolder"^>
echo         ^<Directory Id="dir_VST3Root" Name="VST3"^>
echo           ^<Directory Id="dir_VST3Bundle" Name="Flopster.vst3"^>
echo $vst3Xml
echo           ^</Directory^>
echo         ^</Directory^>
echo       ^</Directory^>
echo(
echo       ^<Directory Id="ProgramFilesFolder"^>
echo         ^<Directory Id="INSTALLDIR_SA" Name="Flopster"^>
echo $saXml
echo         ^</Directory^>
echo       ^</Directory^>
echo(
echo       ^<Directory Id="DesktopFolder" Name="Desktop"^>
echo         ^<Component Id="comp_DesktopShortcut" Guid="$scGuid"^>
echo           ^<Shortcut Id="sc_Desktop_Flopster"
echo                     Name="Flopster"
echo                     Description="Flopster - floppy drive instrument by Shiru and Resonaura"
echo                     Target="[INSTALLDIR_SA]Flopster.exe"
echo                     WorkingDirectory="INSTALLDIR_SA"
echo                     Icon="FlopsterIcon"
echo                     IconIndex="0" /^>
echo           ^<RegistryValue Root="HKCU"
echo                          Key="Software\Shiru\Flopster"
echo                          Name="DesktopShortcut"
echo                          Type="integer"
echo                          Value="1"
echo                          KeyPath="yes" /^>
echo         ^</Component^>
echo       ^</Directory^>
echo(
echo     ^</Directory^>
echo(
echo     ^<Feature Id="FeatureAll"
echo              Title="Flopster"
echo              Description="Install Flopster audio plugin."
echo              Level="1"
echo              ConfigurableDirectory="INSTALLDIR_SA"
echo              Display="expand"
echo              Absent="disallow"^>
echo(
echo       ^<Feature Id="FeatureVST3"
echo                Title="VST3 Plugin"
echo                Description="Installs the VST3 plugin to %CommonProgramFiles%\VST3\ for use in any VST3-compatible DAW."
echo                Level="1"
echo                AllowAbsent="yes"^>
echo $vst3Refs
echo       ^</Feature^>
echo(
echo       ^<Feature Id="FeatureStandalone"
echo                Title="Standalone Application"
echo                Description="Installs the standalone Flopster application and a Desktop shortcut."
echo                Level="1"
echo                AllowAbsent="yes"^>
echo $saRefs
echo         ^<ComponentRef Id="comp_DesktopShortcut" /^>
echo       ^</Feature^>
echo(
echo     ^</Feature^>
echo(
echo     ^<UIRef Id="WixUI_FeatureTree" /^>
echo     ^<WixVariable Id="WixUILicenseRtf" Value="License.rtf" /^>
echo(
echo   ^</Product^>
echo(
echo ^</Wix^>
echo "@
echo(
echo }
echo(
echo Set-Content -Path $WxsPath -Value $wxsContent -Encoding UTF8
echo Write-Host "  [OK]    Flopster.wxs written: $WxsPath"
) > "%PS1%"

:: ── Write a minimal License.rtf (WixUI_FeatureTree requires it) ──────────────
(
echo {\rtf1\ansi\deff0
echo {\fonttbl{\f0 Arial;}}
echo \f0\fs20
echo Flopster - Floppy Drive Instrument\line
echo Copyright (c) Shiru \& Resonaura. All rights reserved.\line
echo \line
echo Permission is hereby granted, free of charge, to any person obtaining\line
echo a copy of this software to use, copy, modify and distribute it.\line
echo \line
echo THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.\line
echo }
) > "%LICENSE_RTF%"

:: ── Run the PowerShell WXS generator ─────────────────────────────────────────
echo  [INFO]  Running WXS generator (PowerShell)...
powershell -NoProfile -ExecutionPolicy Bypass -File "%PS1%"
if errorlevel 1 (
    echo  [ERROR] PowerShell WXS generation failed. See output above.
    echo          The generated script is at: %PS1%
    exit /b 1
)

if not exist "%WXS%" (
    echo  [ERROR] WXS file was not created: %WXS%
    exit /b 1
)
echo  [OK]    WXS ready: %WXS%
echo(

:: =============================================================================
:: STEP 6 — Build MSI
:: =============================================================================
echo [6/6] Building MSI...
echo -----------------------------------------------

if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"
set "MSI_OUT=%OUT_DIR%\%MSI_NAME%"

if "!WIX_VER!"=="4" (
    call :build_msi_v4
) else (
    call :build_msi_v3
)
if errorlevel 1 (
    echo  [ERROR] MSI build failed.  See output above.
    echo          WiX work directory preserved for inspection: %WIX_WORK%
    exit /b 1
)

:: ── Final summary ─────────────────────────────────────────────────────────────
echo(
echo  +=======================================================+
echo  ^|   MSI built successfully!                            ^|
echo  +=======================================================+
echo(
echo   Output : %MSI_OUT%
echo(
echo   Install (interactive):
echo     Double-click %MSI_NAME%
echo     or:  msiexec /i "%MSI_OUT%"
echo(
echo   Install (silent, all features):
echo     msiexec /i "%MSI_OUT%" /qn ADDLOCAL=FeatureAll
echo(
echo   Uninstall:
echo     msiexec /x "%MSI_OUT%"
echo     or:  Control Panel -^> Programs -^> Uninstall Flopster
echo(

endlocal
exit /b 0


:: =============================================================================
:: :build_msi_v4 — build with WiX v4  (wix build)
:: =============================================================================
:build_msi_v4
echo  [INFO]  Building with WiX v4...

:: Ensure the UI extension is available
wix extension list 2>nul | findstr /i "WixToolset.UI" >nul
if errorlevel 1 (
    echo  [INFO]  Adding WiX UI extension...
    wix extension add WixToolset.UI.wixext
    if errorlevel 1 (
        echo  [WARN]  Could not add WixToolset.UI.wixext.
        echo          The installer UI may be minimal (no feature-selection dialog).
    )
)

wix build "%WXS%" ^
    -ext WixToolset.UI.wixext ^
    -b "%WIX_WORK%" ^
    -o "%MSI_OUT%"

if errorlevel 1 exit /b 1
echo  [OK]    WiX v4 build complete: %MSI_OUT%
exit /b 0


:: =============================================================================
:: :build_msi_v3 — build with WiX v3  (candle → light)
:: =============================================================================
:build_msi_v3
echo  [INFO]  Building with WiX v3 (candle + light)...

set "WXS_OBJ=%WIX_WORK%\Flopster.wixobj"

:: ── Candle (compile .wxs → .wixobj) ─────────────────────────────────────────
echo  [INFO]  candle.exe ...
"%WIX3_CANDLE%" ^
    -nologo ^
    -arch x64 ^
    -ext WixUIExtension ^
    -out "%WXS_OBJ%" ^
    "%WXS%"
if errorlevel 1 (
    echo  [ERROR] candle.exe failed.
    exit /b 1
)
echo  [OK]    Compiled: %WXS_OBJ%

:: ── Light (link .wixobj → .msi) ──────────────────────────────────────────────
echo  [INFO]  light.exe ...
"%WIX3_LIGHT%" ^
    -nologo ^
    -ext WixUIExtension ^
    -cultures:en-US ^
    -b "%WIX_WORK%" ^
    -out "%MSI_OUT%" ^
    "%WXS_OBJ%"
if errorlevel 1 (
    echo  [ERROR] light.exe failed.
    exit /b 1
)
echo  [OK]    Linked: %MSI_OUT%
exit /b 0


:: =============================================================================
:: :show_help
:: =============================================================================
:show_help
echo(
echo  Usage: make_msi.bat [--rebuild] [--no-build] [--out ^<dir^>] [--help]
echo(
echo  Builds a Windows MSI installer for Flopster using WiX Toolset.
echo(
echo  Flags:
echo    --rebuild          Force clean rebuild of plugin before packaging.
echo    --no-build         Skip CMake build; fail if artefacts are missing.
echo    --out ^<dir^>        Output directory for the .msi  (default: dist\)
echo    --help, -h         Show this message and exit.
echo(
echo  Prerequisites:
echo    cmake              https://cmake.org/download/
echo    Visual Studio 2022 https://visualstudio.microsoft.com/
echo      OR  Ninja        winget install Ninja-build.Ninja
echo    WiX v4 (preferred) dotnet tool install --global wix
echo      OR  WiX v3       https://wixtoolset.org/releases/
echo(
echo  MSI details:
echo    Product name  : Flopster
echo    Manufacturer  : Shiru and Resonaura
echo    Version       : 1.24.0.0
echo    Upgrade code  : {A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
echo    Features      : VST3 Plugin, Standalone Application (both on by default)
echo    Install scope : perMachine (requires Administrator)
echo    Desktop shortcut created for Standalone feature.
echo(
echo  Output: dist\Flopster-1.24.msi   (or --out ^<dir^>\Flopster-1.24.msi)
echo(
exit /b 0
