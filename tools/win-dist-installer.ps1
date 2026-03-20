#Requires -Version 5.0
<#
.SYNOPSIS
    Flopster  -  Windows End-User Installer
    No build tools required. Just run this script.
    Handles first install and updates. Backs up previous version.
    by Shiru & Resonaura

    Run this script from the distribution folder that contains:
      Flopster.vst3\, Flopster.exe, samples\
#>

param()

$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# ── Paths ─────────────────────────────────────────────────────────────────────
$vst3Src    = Join-Path $scriptDir 'Flopster.vst3'
$exeSrc     = Join-Path $scriptDir 'Flopster.exe'
$samplesSrc = Join-Path $scriptDir 'samples'

$vst3Dst1 = Join-Path $env:CommonProgramFiles 'VST3\Flopster.vst3'
$vst3Dst2 = Join-Path $env:LOCALAPPDATA 'Programs\VstPlugins\Flopster.vst3'
$appDir   = Join-Path $env:LOCALAPPDATA 'Programs\Flopster'
$appDst   = Join-Path $appDir 'Flopster.exe'
$shortcut = Join-Path $env:USERPROFILE 'Desktop\Flopster.lnk'
$backupDir = Join-Path $env:TEMP 'Flopster-backup'

# ── Admin check / self-elevation ──────────────────────────────────────────────
function Test-Admin {
    $identity  = [System.Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [System.Security.Principal.WindowsPrincipal]$identity
    return $principal.IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)
}

# ── Banner ────────────────────────────────────────────────────────────────────
Write-Host
Write-Host "  +============================================+"
Write-Host "  |   Flopster Installer                       |"
Write-Host "  |   by Shiru & Resonaura                     |"
Write-Host "  +============================================+"
Write-Host

# ── Step 1: Verify package contents ──────────────────────────────────────────
Write-Host "[1/5] Verifying package contents..."
Write-Host "-----------------------------------------------"
Write-Host

if (-not (Test-Path $vst3Src)) {
    Write-Host "  [ERROR] Flopster.vst3 not found next to this script."
    Write-Host "          Expected: $vst3Src"
    Write-Host
    Write-Host "  Press Enter to exit..."
    Read-Host
    exit 1
}
Write-Host "  [OK]    Flopster.vst3 found."

if (-not (Test-Path $exeSrc)) {
    Write-Host "  [ERROR] Flopster.exe not found next to this script."
    Write-Host "          Expected: $exeSrc"
    Write-Host
    Write-Host "  Press Enter to exit..."
    Read-Host
    exit 1
}
Write-Host "  [OK]    Flopster.exe found."

if (-not (Test-Path $samplesSrc)) {
    Write-Host "  [ERROR] samples\ directory not found next to this script."
    Write-Host
    Write-Host "  Press Enter to exit..."
    Read-Host
    exit 1
}
Write-Host "  [OK]    samples\ found."
Write-Host

# ── Step 2: Detect existing installation ─────────────────────────────────────
Write-Host "[2/5] Checking for existing installation..."
Write-Host "-----------------------------------------------"
Write-Host

$isUpdate    = $false
$hadVst3Sys  = Test-Path $vst3Dst1
$hadVst3Usr  = Test-Path $vst3Dst2
$hadExe      = Test-Path $appDst

if ($hadVst3Sys) { $isUpdate = $true; Write-Host "  [INFO]  Found existing VST3 (system): $vst3Dst1" }
if ($hadVst3Usr) { $isUpdate = $true; Write-Host "  [INFO]  Found existing VST3 (user):   $vst3Dst2" }
if ($hadExe)     { $isUpdate = $true; Write-Host "  [INFO]  Found existing Standalone:    $appDst" }

if ($isUpdate) {
    Write-Host
    Write-Host "  [INFO]  Existing installation detected  -  this will be an update."
    Write-Host "          A backup will be saved to: $backupDir"
    Write-Host "          It will be restored automatically if anything goes wrong."
} else {
    Write-Host "  [INFO]  No existing installation found  -  fresh install."
}
Write-Host

# ── Check for admin rights and elevate if needed ──────────────────────────────
if (-not (Test-Admin)) {
    Write-Host "  [INFO]  Administrator rights required for system VST3 directory."
    Write-Host "          Requesting elevation via UAC..."
    Write-Host
    $scriptPath = $MyInvocation.MyCommand.Path
    Start-Process powershell.exe -ArgumentList @(
        '-ExecutionPolicy', 'Bypass',
        '-File', "`"$scriptPath`""
    ) -Verb RunAs -Wait
    exit 0
}
Write-Host "  [OK]    Running as Administrator."
Write-Host

# ── Step 3: Close running Flopster instance ───────────────────────────────────
Write-Host "[3/5] Closing any running Flopster instances..."
Write-Host "-----------------------------------------------"
Write-Host

$running = Get-Process -Name Flopster -ErrorAction SilentlyContinue
if ($running) {
    Write-Host "  [INFO]  Flopster.exe is running  -  attempting to close it..."
    try {
        $running | Stop-Process -Force
        Start-Sleep -Seconds 2
        Write-Host "  [OK]    Flopster.exe closed."
    } catch {
        Write-Host "  [WARN]  Could not close Flopster.exe automatically."
        Write-Host "          Please close it manually, then press Enter to continue."
        Read-Host
    }
} else {
    Write-Host "  [OK]    Flopster is not running."
}
Write-Host

# ── Step 4: Backup existing installation ─────────────────────────────────────
if ($isUpdate) {
    Write-Host "[4/5] Backing up existing installation..."
    Write-Host "-----------------------------------------------"
    Write-Host

    if (Test-Path $backupDir) {
        Remove-Item -Path $backupDir -Recurse -Force
    }
    New-Item -ItemType Directory -Path $backupDir -Force | Out-Null

    if ($hadVst3Sys) {
        Copy-Item -Path $vst3Dst1 -Destination (Join-Path $backupDir 'vst3_sys') -Recurse -Force
        Write-Host "  [OK]    Backed up VST3 (system)."
    }
    if ($hadVst3Usr) {
        Copy-Item -Path $vst3Dst2 -Destination (Join-Path $backupDir 'vst3_usr') -Recurse -Force
        Write-Host "  [OK]    Backed up VST3 (user)."
    }
    if ($hadExe) {
        $bkStandalone = Join-Path $backupDir 'standalone'
        New-Item -ItemType Directory -Path $bkStandalone -Force | Out-Null
        Copy-Item -Path $appDst -Destination (Join-Path $bkStandalone 'Flopster.exe') -Force
        Write-Host "  [OK]    Backed up Standalone."
    }
    Write-Host
} else {
    Write-Host "[4/5] Backup  -  skipped (fresh install)."
    Write-Host "-----------------------------------------------"
    Write-Host
}

# ── Helper: install one VST3 bundle ──────────────────────────────────────────
function Install-VST3Bundle {
    param([string]$Destination, [string]$Label)

    Write-Host "  [INFO]  Installing VST3 ($Label) to: $Destination"

    if (Test-Path $Destination) {
        try {
            Remove-Item -Path $Destination -Recurse -Force
        } catch {
            Write-Host "  [ERROR] Could not remove existing bundle at: $Destination"
            Write-Host "          Make sure your DAW is closed and try again."
            return $false
        }
    }

    $parent = Split-Path -Parent $Destination
    if (-not (Test-Path $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }

    try {
        Copy-Item -Path $script:vst3Src -Destination $Destination -Recurse -Force
    } catch {
        Write-Host "  [ERROR] Failed to copy VST3 bundle to: $Destination"
        return $false
    }
    Write-Host "  [OK]    VST3 installed ($Label): $Destination"

    # Copy resources
    $resDir = Join-Path $Destination 'Contents\Resources'
    if (-not (Test-Path $resDir)) {
        New-Item -ItemType Directory -Path $resDir -Force | Out-Null
    }

    $scanlinesPath = Join-Path $script:scriptDir 'scanlines.png'
    if (Test-Path $scanlinesPath) {
        Copy-Item -Path $scanlinesPath -Destination $resDir -Force | Out-Null
    }

    $resSamples = Join-Path $resDir 'samples'
    if (Test-Path $resSamples) {
        Remove-Item -Path $resSamples -Recurse -Force | Out-Null
    }
    Copy-Item -Path $script:samplesSrc -Destination $resSamples -Recurse -Force
    Write-Host "  [OK]    Resources copied into VST3 bundle ($Label)."

    return $true
}

# ── Helper: rollback ──────────────────────────────────────────────────────────
function Invoke-Rollback {
    Write-Host
    Write-Host "  [WARN]  Rolling back to previous version..."

    if (-not (Test-Path $backupDir)) {
        Write-Host "  [WARN]  No backup found  -  cannot restore."
        return
    }

    $bkVst3Sys = Join-Path $backupDir 'vst3_sys'
    $bkVst3Usr = Join-Path $backupDir 'vst3_usr'
    $bkExe     = Join-Path $backupDir 'standalone\Flopster.exe'

    if (Test-Path $bkVst3Sys) {
        if (Test-Path $vst3Dst1) { Remove-Item -Path $vst3Dst1 -Recurse -Force }
        Copy-Item -Path $bkVst3Sys -Destination $vst3Dst1 -Recurse -Force
        Write-Host "  [OK]    Restored VST3 (system)."
    }
    if (Test-Path $bkVst3Usr) {
        if (Test-Path $vst3Dst2) { Remove-Item -Path $vst3Dst2 -Recurse -Force }
        Copy-Item -Path $bkVst3Usr -Destination $vst3Dst2 -Recurse -Force
        Write-Host "  [OK]    Restored VST3 (user)."
    }
    if (Test-Path $bkExe) {
        Copy-Item -Path $bkExe -Destination $appDst -Force
        Write-Host "  [OK]    Restored Standalone."
    }

    Remove-Item -Path $backupDir -Recurse -Force
    Write-Host "  [OK]    Rollback complete. Previous version restored."
}

# ── Step 5: Install ───────────────────────────────────────────────────────────
Write-Host "[5/5] Installing..."
Write-Host "-----------------------------------------------"
Write-Host

# VST3 system-wide
$sysOk = Install-VST3Bundle -Destination $vst3Dst1 -Label 'system-wide'
if (-not $sysOk) {
    Write-Host "  [WARN]  System-wide VST3 install failed (may need higher privileges)."
    Write-Host "          Continuing with user-local..."
}

# VST3 user-local
$usrOk = Install-VST3Bundle -Destination $vst3Dst2 -Label 'user-local'
if (-not $usrOk) {
    Write-Host "  [ERROR] User-local VST3 install failed."
    Invoke-Rollback
    Write-Host
    Write-Host "  [ERROR] Installation failed. See messages above."
    Write-Host
    Write-Host "  Press Enter to exit..."
    Read-Host
    exit 1
}

# Standalone
Write-Host
Write-Host "  [INFO]  Installing Standalone to: $appDst"

if (-not (Test-Path $appDir)) {
    New-Item -ItemType Directory -Path $appDir -Force | Out-Null
}
if (Test-Path $appDst) {
    Remove-Item -Path $appDst -Force
}

try {
    Copy-Item -Path $exeSrc -Destination $appDst -Force
} catch {
    Write-Host "  [ERROR] Failed to copy Flopster.exe to: $appDst"
    Invoke-Rollback
    Write-Host
    Write-Host "  Press Enter to exit..."
    Read-Host
    exit 1
}
Write-Host "  [OK]    Standalone installed: $appDst"

# Copy resources into Standalone dir
$scanlinesPath = Join-Path $scriptDir 'scanlines.png'
if (Test-Path $scanlinesPath) {
    Copy-Item -Path $scanlinesPath -Destination $appDir -Force | Out-Null
}

$appSamples = Join-Path $appDir 'samples'
if (Test-Path $appSamples) {
    Remove-Item -Path $appSamples -Recurse -Force | Out-Null
}
Copy-Item -Path $samplesSrc -Destination $appSamples -Recurse -Force
Write-Host "  [OK]    Resources copied into Standalone directory."

# Desktop shortcut
try {
    $ws = New-Object -ComObject WScript.Shell
    $s  = $ws.CreateShortcut($shortcut)
    $s.TargetPath       = $appDst
    $s.WorkingDirectory = $appDir
    $s.Description      = 'Flopster  -  floppy drive instrument'
    $s.Save()
    Write-Host "  [OK]    Desktop shortcut updated: $shortcut"
} catch {
    Write-Host "  [WARN]  Could not create desktop shortcut (non-critical)."
}
Write-Host

# Clean up backup
if (Test-Path $backupDir) {
    Remove-Item -Path $backupDir -Recurse -Force
    Write-Host "  [OK]    Backup cleaned up."
}
Write-Host

# ── Summary ───────────────────────────────────────────────────────────────────
Write-Host
Write-Host "  +============================================+"
if ($isUpdate) {
    Write-Host "  |   Update complete!                         |"
} else {
    Write-Host "  |   Installation complete!                   |"
}
Write-Host "  +============================================+"
Write-Host
Write-Host "   VST3 (system)    : $vst3Dst1"
Write-Host "   VST3 (user)      : $vst3Dst2"
Write-Host "   Standalone       : $appDst"
Write-Host "   Desktop shortcut : $shortcut"
Write-Host
Write-Host "   Next steps:"
Write-Host "     - Restart your DAW and rescan plugins."
Write-Host "     - Ableton:    Options -> Preferences -> Plug-Ins -> Rescan"
Write-Host "     - FL Studio:  Options -> Manage plugins -> Find more plugins"
Write-Host "     - Reaper:     Options -> Preferences -> Plug-ins -> VST -> Rescan"
Write-Host "     - Standalone: double-click Flopster on your Desktop"
Write-Host

Write-Host "  Press Enter to exit..."
Read-Host
