@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul 2>&1

:: =============================================================================
::  Flopster — Windows Manual Uninstaller
::  Removes all traces of a Flopster installation placed by install.bat,
::  win-install.bat, or make_msi.bat (manual/zip installs only).
::
::  For MSI-installed copies, prefer: msiexec /x Flopster-1.24.msi
::  This script is the fallback for zip-based / dev installs.
::
::  What is removed:
::    - %CommonProgramFiles%\VST3\Flopster.vst3          (system VST3)
::    - %LOCALAPPDATA%\Programs\VstPlugins\Flopster.vst3 (user VST3)
::    - %ProgramFiles%\Flopster\                         (MSI standalone dir)
::    - %LOCALAPPDATA%\Programs\Flopster\                (zip standalone dir)
::    - Desktop shortcut  (Flopster.lnk)
::
::  Usage: win-uninstall.bat [--yes] [--help]
::
::  Flags:
::    --yes, -y    Skip confirmation prompt (non-interactive / CI use)
::    --help, -h   Show this message and exit
::
::  by Shiru & Resonaura
:: =============================================================================

:: ── Paths ─────────────────────────────────────────────────────────────────────
set "VST3_SYS=%CommonProgramFiles%\VST3\Flopster.vst3"
set "VST3_USR=%LOCALAPPDATA%\Programs\VstPlugins\Flopster.vst3"
set "APP_MSI=%ProgramFiles%\Flopster"
set "APP_ZIP=%LOCALAPPDATA%\Programs\Flopster"
set "SHORTCUT=%USERPROFILE%\Desktop\Flopster.lnk"

:: ── Defaults ──────────────────────────────────────────────────────────────────
set "SKIP_CONFIRM=0"

:: ── Parse arguments ───────────────────────────────────────────────────────────
:parse_args
if "%~1"=="" goto :done_args

if /i "%~1"=="--help" goto :show_help
if /i "%~1"=="-h"     goto :show_help

if /i "%~1"=="--yes" (
    set "SKIP_CONFIRM=1"
    shift & goto :parse_args
)
if /i "%~1"=="-y" (
    set "SKIP_CONFIRM=1"
    shift & goto :parse_args
)

echo  [WARN]  Unknown argument: %~1  (ignoring)
shift & goto :parse_args

:done_args

:: ── Banner ────────────────────────────────────────────────────────────────────
echo.
echo  +==============================================+
echo  ^|   Flopster Uninstaller                      ^|
echo  ^|   by Shiru ^& Resonaura                      ^|
echo  +==============================================+
echo.

:: ── Scan what is actually installed ──────────────────────────────────────────
echo  Scanning for installed components...
echo  -----------------------------------------------
echo.

set "FOUND_ANYTHING=0"

call :check_exists "VST3 (system)"      "%VST3_SYS%"    VST3_SYS_FOUND
call :check_exists "VST3 (user-local)"  "%VST3_USR%"    VST3_USR_FOUND
call :check_exists "Standalone (MSI)"   "%APP_MSI%"     APP_MSI_FOUND
call :check_exists "Standalone (zip)"   "%APP_ZIP%"     APP_ZIP_FOUND
call :check_exists "Desktop shortcut"   "%SHORTCUT%"    SHORTCUT_FOUND

if "!FOUND_ANYTHING!"=="0" (
    echo  [INFO]  Nothing to uninstall — no Flopster components detected.
    echo.
    goto :end_clean
)

echo.

:: ── Confirmation ─────────────────────────────────────────────────────────────
if "!SKIP_CONFIRM!"=="0" (
    echo  The items listed above will be permanently removed.
    echo  This cannot be undone.
    echo.
    set /p "CONFIRM=  Proceed with uninstallation? [y/N]  "
    if /i not "!CONFIRM!"=="y" (
        echo.
        echo  [INFO]  Uninstallation cancelled.
        echo.
        goto :end_clean
    )
    echo.
)

:: ── Elevation check ───────────────────────────────────────────────────────────
:: System VST3 and ProgramFiles require admin rights.
if "!VST3_SYS_FOUND!"=="1" goto :need_admin
if "!APP_MSI_FOUND!"=="1"  goto :need_admin
goto :skip_elevation

:need_admin
net session >nul 2>&1
if errorlevel 1 (
    echo  [INFO]  Administrator rights required to remove system-wide files.
    echo          Requesting elevation via UAC...
    echo.
    powershell -Command "Start-Process -FilePath '%~f0' -ArgumentList '--yes' -Verb RunAs -Wait"
    exit /b 0
)
echo  [OK]    Running as Administrator.
echo.

:skip_elevation

:: ── Close any running Flopster processes ─────────────────────────────────────
echo  Closing any running Flopster instances...
echo  -----------------------------------------------

tasklist /FI "IMAGENAME eq Flopster.exe" 2>nul | find /I "Flopster.exe" >nul
if not errorlevel 1 (
    echo  [INFO]  Flopster.exe is running — terminating...
    taskkill /F /IM "Flopster.exe" >nul 2>&1
    if not errorlevel 1 (
        echo  [OK]    Flopster.exe terminated.
        timeout /t 2 /nobreak >nul
    ) else (
        echo  [WARN]  Could not terminate Flopster.exe automatically.
        echo          Please close it manually, then press any key to continue.
        pause >nul
    )
) else (
    echo  [OK]    Flopster is not running.
)
echo.

:: ── Remove components ─────────────────────────────────────────────────────────
echo  Removing components...
echo  -----------------------------------------------
echo.

set "ERRORS=0"

:: VST3 system-wide
if "!VST3_SYS_FOUND!"=="1" (
    call :remove_dir "%VST3_SYS%" "VST3 (system)"
)

:: VST3 user-local
if "!VST3_USR_FOUND!"=="1" (
    call :remove_dir "%VST3_USR%" "VST3 (user-local)"
)

:: Standalone MSI install dir
if "!APP_MSI_FOUND!"=="1" (
    call :remove_dir "%APP_MSI%" "Standalone (MSI)"
)

:: Standalone zip install dir
if "!APP_ZIP_FOUND!"=="1" (
    call :remove_dir "%APP_ZIP%" "Standalone (zip)"
)

:: Desktop shortcut
if "!SHORTCUT_FOUND!"=="1" (
    echo  [INFO]  Removing Desktop shortcut: %SHORTCUT%
    del /q "%SHORTCUT%" >nul 2>&1
    if errorlevel 1 (
        echo  [ERROR] Could not remove shortcut: %SHORTCUT%
        set /a ERRORS+=1
    ) else (
        echo  [OK]    Desktop shortcut removed.
    )
    echo.
)

:: ── Registry cleanup (best-effort) ───────────────────────────────────────────
:: Remove the HKCU key written by the shortcut component in the MSI/installer.
reg query "HKCU\Software\Shiru\Flopster" >nul 2>&1
if not errorlevel 1 (
    echo  [INFO]  Removing HKCU\Software\Shiru\Flopster registry key...
    reg delete "HKCU\Software\Shiru\Flopster" /f >nul 2>&1
    if not errorlevel 1 (
        echo  [OK]    Registry key removed.
    ) else (
        echo  [WARN]  Could not remove registry key (non-critical).
    )
    :: Remove parent key if now empty
    reg query "HKCU\Software\Shiru" >nul 2>&1
    if not errorlevel 1 (
        for /f %%K in ('reg query "HKCU\Software\Shiru" 2^>nul ^| find /c "HKEY"') do (
            if "%%K"=="1" (
                reg delete "HKCU\Software\Shiru" /f >nul 2>&1
            )
        )
    )
    echo.
)

:: ── Summary ───────────────────────────────────────────────────────────────────
echo  -----------------------------------------------
if "!ERRORS!"=="0" (
    echo.
    echo  +==============================================+
    echo  ^|   Flopster uninstalled successfully!        ^|
    echo  +==============================================+
    echo.
    echo   All Flopster files have been removed from this machine.
    echo.
    echo   If your DAW still shows Flopster in its plugin list:
    echo     - Ableton:    Options -^> Preferences -^> Plug-Ins -^> Rescan
    echo     - FL Studio:  Options -^> Manage plugins -^> Find more plugins -^> Rescan
    echo     - Reaper:     Options -^> Preferences -^> Plug-ins -^> VST -^> Rescan
    echo     - Other DAWs: trigger a plugin rescan / restart the DAW
    echo.
) else (
    echo.
    echo  [WARN]  Uninstall completed with !ERRORS! error(s).
    echo          Some files may not have been removed — see messages above.
    echo          You may need to delete them manually or re-run as Administrator.
    echo.
)

:end_clean
endlocal
exit /b 0


:: =============================================================================
:: Subroutines
:: =============================================================================

:: ── :check_exists "<label>" "<path>" <flag-var> ───────────────────────────────
:: Sets <flag-var>=1 and prints a found/not-found line.
:check_exists
setlocal
set "_LABEL=%~1"
set "_PATH=%~2"
set "_VAR=%~3"

if exist "!_PATH!" (
    echo   [FOUND]  !_LABEL!
    echo            !_PATH!
    echo.
    endlocal & set "%_VAR%=1" & set "FOUND_ANYTHING=1"
) else (
    echo   [-----]  !_LABEL! ^(not installed^)
    endlocal & set "%_VAR%=0"
)
exit /b 0


:: ── :remove_dir "<path>" "<label>" ───────────────────────────────────────────
:: Removes a directory tree, reporting success or failure.
:remove_dir
setlocal
set "_PATH=%~1"
set "_LABEL=%~2"

echo  [INFO]  Removing %_LABEL%: %_PATH%

rmdir /s /q "%_PATH%" >nul 2>&1
if errorlevel 1 (
    :: Second attempt after a short pause (locked files race condition)
    timeout /t 1 /nobreak >nul
    rmdir /s /q "%_PATH%" >nul 2>&1
)

if exist "%_PATH%" (
    echo  [ERROR] Could not fully remove: %_PATH%
    echo          The directory or some files may still be in use.
    echo          Close all DAWs and try again, or delete manually:
    echo            rmdir /s /q "%_PATH%"
    endlocal
    set /a ERRORS+=1
) else (
    echo  [OK]    Removed: %_PATH%
)
echo.
endlocal
exit /b 0


:: ── :show_help ────────────────────────────────────────────────────────────────
:show_help
echo.
echo  Usage: win-uninstall.bat [--yes] [--help]
echo.
echo  Removes all traces of a Flopster installation.
echo.
echo  Locations checked and removed:
echo    %%CommonProgramFiles%%\VST3\Flopster.vst3
echo    %%LOCALAPPDATA%%\Programs\VstPlugins\Flopster.vst3
echo    %%ProgramFiles%%\Flopster\
echo    %%LOCALAPPDATA%%\Programs\Flopster\
echo    %%USERPROFILE%%\Desktop\Flopster.lnk
echo.
echo  Flags:
echo    --yes, -y    Skip confirmation prompt
echo    --help, -h   Show this message and exit
echo.
echo  Note: For MSI-installed copies, the preferred method is:
echo    msiexec /x Flopster-1.24.msi
echo  This script handles zip-based / dev installs.
echo.
exit /b 0
