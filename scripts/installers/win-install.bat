@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul 2>&1

:: =============================================================================
::  Flopster — Windows End-User Installer
::  No build tools required. Just run this script.
::  Handles first install and updates. Backs up previous version.
::  by Shiru & Resonaura
:: =============================================================================

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

set "VST3_SRC=%SCRIPT_DIR%\Flopster.vst3"
set "EXE_SRC=%SCRIPT_DIR%\Flopster.exe"
set "BACK_BMP=%SCRIPT_DIR%\back.bmp"
set "CHAR_BMP=%SCRIPT_DIR%\char.bmp"
set "SAMPLES_SRC=%SCRIPT_DIR%\samples"

set "VST3_DST1=%CommonProgramFiles%\VST3\Flopster.vst3"
set "VST3_DST2=%LOCALAPPDATA%\Programs\VstPlugins\Flopster.vst3"
set "APP_DIR=%LOCALAPPDATA%\Programs\Flopster"
set "APP_DST=%APP_DIR%\Flopster.exe"
set "SHORTCUT=%USERPROFILE%\Desktop\Flopster.lnk"

set "BACKUP_DIR=%TEMP%\Flopster-backup"

set "IS_UPDATE=0"
set "HAD_VST3_SYS=0"
set "HAD_VST3_USR=0"
set "HAD_EXE=0"

:: ── Banner ────────────────────────────────────────────────────────────────────
echo.
echo  +==============================================+
echo  ^|   Flopster Installer  v1.21                 ^|
echo  ^|   by Shiru ^& Resonaura                      ^|
echo  +==============================================+
echo.

:: ── Verify package contents ───────────────────────────────────────────────────
echo [1/5] Verifying package contents...
echo -----------------------------------------------

if not exist "%VST3_SRC%" (
    echo  [ERROR] Flopster.vst3 not found next to this script.
    echo          Expected: %VST3_SRC%
    goto :fail_norestore
)
echo  [OK]    Flopster.vst3 found.

if not exist "%EXE_SRC%" (
    echo  [ERROR] Flopster.exe not found next to this script.
    echo          Expected: %EXE_SRC%
    goto :fail_norestore
)
echo  [OK]    Flopster.exe found.

if not exist "%BACK_BMP%" (
    echo  [ERROR] back.bmp not found next to this script.
    goto :fail_norestore
)
echo  [OK]    back.bmp found.

if not exist "%CHAR_BMP%" (
    echo  [ERROR] char.bmp not found next to this script.
    goto :fail_norestore
)
echo  [OK]    char.bmp found.

if not exist "%SAMPLES_SRC%" (
    echo  [ERROR] samples\ directory not found next to this script.
    goto :fail_norestore
)
echo  [OK]    samples\ found.
echo.

:: ── Detect existing installation ─────────────────────────────────────────────
echo [2/5] Checking for existing installation...
echo -----------------------------------------------

if exist "%VST3_DST1%" ( set "IS_UPDATE=1" & set "HAD_VST3_SYS=1" & echo  [INFO]  Found existing VST3 ^(system^): %VST3_DST1% )
if exist "%VST3_DST2%" ( set "IS_UPDATE=1" & set "HAD_VST3_USR=1" & echo  [INFO]  Found existing VST3 ^(user^):   %VST3_DST2% )
if exist "%APP_DST%"   ( set "IS_UPDATE=1" & set "HAD_EXE=1"      & echo  [INFO]  Found existing Standalone:    %APP_DST% )

if "!IS_UPDATE!"=="1" (
    echo.
    echo  [INFO]  Existing installation detected — this will be an update.
    echo          A backup will be saved to: %BACKUP_DIR%
    echo          It will be restored automatically if anything goes wrong.
) else (
    echo  [INFO]  No existing installation found — fresh install.
)
echo.

:: ── Check for admin rights and elevate if needed ──────────────────────────────
net session >nul 2>&1
if errorlevel 1 (
    echo  [INFO]  Administrator rights required for system VST3 directory.
    echo          Requesting elevation via UAC...
    echo.
    powershell -Command "Start-Process -FilePath '%~f0' -Verb RunAs -Wait"
    exit /b 0
)
echo  [OK]    Running as Administrator.
echo.

:: ── Close running Flopster instance ──────────────────────────────────────────
echo [3/5] Closing any running Flopster instances...
echo -----------------------------------------------

tasklist /FI "IMAGENAME eq Flopster.exe" 2>nul | find /I "Flopster.exe" >nul
if not errorlevel 1 (
    echo  [INFO]  Flopster.exe is running — attempting to close it...
    taskkill /F /IM "Flopster.exe" >nul 2>&1
    if not errorlevel 1 (
        echo  [OK]    Flopster.exe closed.
        timeout /t 2 /nobreak >nul
    ) else (
        echo  [WARN]  Could not close Flopster.exe automatically.
        echo          Please close it manually, then press any key to continue.
        pause >nul
    )
) else (
    echo  [OK]    Flopster is not running.
)
echo.

:: ── Backup existing installation ─────────────────────────────────────────────
if "!IS_UPDATE!"=="1" (
    echo [4/5] Backing up existing installation...
    echo -----------------------------------------------

    if exist "%BACKUP_DIR%" rmdir /s /q "%BACKUP_DIR%"
    mkdir "%BACKUP_DIR%"

    if "!HAD_VST3_SYS!"=="1" (
        xcopy /e /i /q /y "%VST3_DST1%" "%BACKUP_DIR%\vst3_sys\" >nul
        echo  [OK]    Backed up VST3 ^(system^).
    )
    if "!HAD_VST3_USR!"=="1" (
        xcopy /e /i /q /y "%VST3_DST2%" "%BACKUP_DIR%\vst3_usr\" >nul
        echo  [OK]    Backed up VST3 ^(user^).
    )
    if "!HAD_EXE!"=="1" (
        if not exist "%BACKUP_DIR%\standalone" mkdir "%BACKUP_DIR%\standalone"
        copy /y "%APP_DST%" "%BACKUP_DIR%\standalone\Flopster.exe" >nul
        echo  [OK]    Backed up Standalone.
    )
    echo.
) else (
    echo [4/5] Backup — skipped ^(fresh install^).
    echo -----------------------------------------------
    echo.
)

:: ── Install ───────────────────────────────────────────────────────────────────
echo [5/5] Installing...
echo -----------------------------------------------

:: VST3 system-wide
call :install_vst3 "%VST3_DST1%" "system-wide"
if errorlevel 1 (
    echo  [WARN]  System-wide VST3 install failed ^(may need higher privileges^).
    echo          Continuing with user-local...
)

:: VST3 user-local
call :install_vst3 "%VST3_DST2%" "user-local"
if errorlevel 1 (
    echo  [ERROR] User-local VST3 install failed.
    goto :fail
)

:: Standalone
echo.
echo  [INFO]  Installing Standalone to: %APP_DST%

if not exist "%APP_DIR%" mkdir "%APP_DIR%"
if exist "%APP_DST%" del /q "%APP_DST%"

copy /y "%EXE_SRC%" "%APP_DST%" >nul
if errorlevel 1 (
    echo  [ERROR] Failed to copy Flopster.exe to: %APP_DST%
    goto :fail
)
echo  [OK]    Standalone installed: %APP_DST%

:: Copy resources into Standalone dir
copy /y "%BACK_BMP%" "%APP_DIR%\" >nul
copy /y "%CHAR_BMP%" "%APP_DIR%\" >nul
if exist "%APP_DIR%\samples" rmdir /s /q "%APP_DIR%\samples"
xcopy /e /i /q /y "%SAMPLES_SRC%" "%APP_DIR%\samples\" >nul
echo  [OK]    Resources copied into Standalone directory.

:: Desktop shortcut
powershell -NoProfile -Command ^
    "$ws = New-Object -ComObject WScript.Shell;" ^
    "$s = $ws.CreateShortcut('%SHORTCUT%');" ^
    "$s.TargetPath = '%APP_DST%';" ^
    "$s.WorkingDirectory = '%APP_DIR%';" ^
    "$s.Description = 'Flopster — floppy drive instrument';" ^
    "$s.Save()" >nul 2>&1
if not errorlevel 1 (
    echo  [OK]    Desktop shortcut updated: %SHORTCUT%
) else (
    echo  [WARN]  Could not create desktop shortcut ^(non-critical^).
)
echo.

:: ── Clean up backup ───────────────────────────────────────────────────────────
if exist "%BACKUP_DIR%" (
    rmdir /s /q "%BACKUP_DIR%"
    echo  [OK]    Backup cleaned up.
)
echo.

:: ── Summary ───────────────────────────────────────────────────────────────────
echo.
echo  +==============================================+
if "!IS_UPDATE!"=="1" (
    echo  ^|   Update complete!                          ^|
) else (
    echo  ^|   Installation complete!                    ^|
)
echo  +==============================================+
echo.
echo   VST3 ^(system^)   : %VST3_DST1%
echo   VST3 ^(user^)     : %VST3_DST2%
echo   Standalone      : %APP_DST%
echo   Desktop shortcut: %SHORTCUT%
echo.
echo   Next steps:
echo     - Restart your DAW and rescan plugins.
echo     - Ableton:    Options -^> Preferences -^> Plug-Ins -^> Rescan
echo     - FL Studio:  Options -^> Manage plugins -^> Find more plugins
echo     - Reaper:     Options -^> Preferences -^> Plug-ins -^> VST -^> Rescan
echo     - Standalone: double-click Flopster on your Desktop
echo.

pause
exit /b 0


:: =============================================================================
::  :install_vst3 "<destination>" "<label>"
:: =============================================================================
:install_vst3
setlocal
set "DST=%~1"
set "LBL=%~2"

echo  [INFO]  Installing VST3 ^(%LBL%^) to: %DST%

if exist "%DST%" (
    rmdir /s /q "%DST%"
    if errorlevel 1 (
        echo  [ERROR] Could not remove existing bundle at: %DST%
        echo          Make sure your DAW is closed and try again.
        endlocal & exit /b 1
    )
)

for %%P in ("%DST%") do set "DST_PARENT=%%~dpP"
if not exist "%DST_PARENT%" mkdir "%DST_PARENT%"

xcopy /e /i /q /y "%VST3_SRC%" "%DST%\" >nul
if errorlevel 1 (
    echo  [ERROR] Failed to copy VST3 bundle to: %DST%
    endlocal & exit /b 1
)

echo  [OK]    VST3 installed ^(%LBL%^): %DST%

:: Copy resources into VST3 bundle Resources\
set "RES=%DST%\Contents\Resources"
if not exist "%RES%" mkdir "%RES%"
copy /y "%BACK_BMP%" "%RES%\" >nul
copy /y "%CHAR_BMP%" "%RES%\" >nul
if exist "%RES%\samples" rmdir /s /q "%RES%\samples"
xcopy /e /i /q /y "%SAMPLES_SRC%" "%RES%\samples\" >nul
echo  [OK]    Resources copied into VST3 bundle ^(%LBL%^).

endlocal & exit /b 0


:: =============================================================================
::  :rollback — restore backup if it exists
:: =============================================================================
:rollback
echo.
echo  [WARN]  Rolling back to previous version...

if not exist "%BACKUP_DIR%" (
    echo  [WARN]  No backup found — cannot restore.
    goto :rollback_done
)

if exist "%BACKUP_DIR%\vst3_sys\" (
    if exist "%VST3_DST1%" rmdir /s /q "%VST3_DST1%"
    xcopy /e /i /q /y "%BACKUP_DIR%\vst3_sys\" "%VST3_DST1%\" >nul
    echo  [OK]    Restored VST3 ^(system^).
)
if exist "%BACKUP_DIR%\vst3_usr\" (
    if exist "%VST3_DST2%" rmdir /s /q "%VST3_DST2%"
    xcopy /e /i /q /y "%BACKUP_DIR%\vst3_usr\" "%VST3_DST2%\" >nul
    echo  [OK]    Restored VST3 ^(user^).
)
if exist "%BACKUP_DIR%\standalone\Flopster.exe" (
    copy /y "%BACKUP_DIR%\standalone\Flopster.exe" "%APP_DST%" >nul
    echo  [OK]    Restored Standalone.
)

rmdir /s /q "%BACKUP_DIR%"
echo  [OK]    Rollback complete. Previous version restored.

:rollback_done
exit /b 0


:: =============================================================================
:fail
call :rollback
:fail_norestore
echo.
echo  [ERROR] Installation failed. See messages above.
echo.
pause
exit /b 1
