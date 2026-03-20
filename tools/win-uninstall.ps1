#Requires -Version 5.0
<#
.SYNOPSIS
    Flopster — Windows Manual Uninstaller
    Removes all traces of a Flopster installation.

    For MSI-installed copies, prefer: msiexec /x Flopster-<ver>.msi
    This script is the fallback for zip-based / dev installs.

.PARAMETER Yes
    Skip confirmation prompt (non-interactive / CI use)

.EXAMPLE
    .\win-uninstall.ps1          # interactive, asks for confirmation
    .\win-uninstall.ps1 -Yes     # skip confirmation
#>

param(
    [switch]$Yes,
    [switch]$Help
)

$ErrorActionPreference = 'Stop'

if ($Help) {
    Write-Host @"
  Usage: .\win-uninstall.ps1 [-Yes] [-Help]

  Removes all traces of a Flopster installation.

  Locations checked and removed:
    %CommonProgramFiles%\VST3\Flopster.vst3
    %LOCALAPPDATA%\Programs\VstPlugins\Flopster.vst3
    %ProgramFiles%\Flopster\
    %LOCALAPPDATA%\Programs\Flopster\
    %USERPROFILE%\Desktop\Flopster.lnk

  Flags:
    -Yes, -y    Skip confirmation prompt
    -Help, -h   Show this message and exit

  Note: For MSI-installed copies, the preferred method is:
    msiexec /x Flopster-<ver>.msi
  This script handles zip-based / dev installs.

"@
    exit 0
}

# ── Paths ─────────────────────────────────────────────────────────────────────
$vst3Sys  = Join-Path $env:CommonProgramFiles 'VST3\Flopster.vst3'
$vst3Usr  = Join-Path $env:LOCALAPPDATA 'Programs\VstPlugins\Flopster.vst3'
$appMsi   = Join-Path $env:ProgramFiles 'Flopster'
$appZip   = Join-Path $env:LOCALAPPDATA 'Programs\Flopster'
$shortcut = Join-Path $env:USERPROFILE 'Desktop\Flopster.lnk'

# ── Admin check / self-elevation ──────────────────────────────────────────────
function Test-Admin {
    $identity  = [System.Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [System.Security.Principal.WindowsPrincipal]$identity
    return $principal.IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Request-Elevation {
    $scriptPath = $MyInvocation.ScriptName
    $argList    = @('-ExecutionPolicy', 'Bypass', '-File', "`"$scriptPath`"", '-Yes')
    Start-Process powershell.exe -ArgumentList $argList -Verb RunAs -Wait
    exit 0
}

# ── Banner ────────────────────────────────────────────────────────────────────
Write-Host
Write-Host "  +============================================+"
Write-Host "  |   Flopster Uninstaller                     |"
Write-Host "  |   by Shiru & Resonaura                     |"
Write-Host "  +============================================+"
Write-Host

# ── Scan what is actually installed ──────────────────────────────────────────
Write-Host "  Scanning for installed components..."
Write-Host "  -----------------------------------------------"
Write-Host

$found = @{}

function Test-Component {
    param([string]$Label, [string]$Path, [string]$Key)
    if (Test-Path $Path) {
        Write-Host "   [FOUND]  $Label"
        Write-Host "            $Path"
        Write-Host
        $script:found[$Key] = $true
    } else {
        Write-Host "   [-----]  $Label (not installed)"
    }
}

Test-Component 'VST3 (system)'     $vst3Sys  'vst3_sys'
Test-Component 'VST3 (user-local)' $vst3Usr  'vst3_usr'
Test-Component 'Standalone (MSI)'  $appMsi   'app_msi'
Test-Component 'Standalone (zip)'  $appZip   'app_zip'
Test-Component 'Desktop shortcut'  $shortcut 'shortcut'

if ($found.Count -eq 0) {
    Write-Host "  [INFO]  Nothing to uninstall — no Flopster components detected."
    Write-Host
    exit 0
}

Write-Host

# ── Confirmation ─────────────────────────────────────────────────────────────
if (-not $Yes) {
    Write-Host "  The items listed above will be permanently removed."
    Write-Host "  This cannot be undone."
    Write-Host
    $confirm = Read-Host "  Proceed with uninstallation? [y/N]"
    if ($confirm -notmatch '^[yY]$') {
        Write-Host
        Write-Host "  [INFO]  Uninstallation cancelled."
        Write-Host
        exit 0
    }
    Write-Host
}

# ── Elevation check ───────────────────────────────────────────────────────────
$needAdmin = $found['vst3_sys'] -or $found['app_msi']

if ($needAdmin -and -not (Test-Admin)) {
    Write-Host "  [INFO]  Administrator rights required to remove system-wide files."
    Write-Host "          Requesting elevation via UAC..."
    Write-Host
    Request-Elevation
}

if ($needAdmin) {
    Write-Host "  [OK]    Running as Administrator."
    Write-Host
}

# ── Close any running Flopster processes ─────────────────────────────────────
Write-Host "  Closing any running Flopster instances..."
Write-Host "  -----------------------------------------------"

$running = Get-Process -Name Flopster -ErrorAction SilentlyContinue
if ($running) {
    Write-Host "  [INFO]  Flopster.exe is running — terminating..."
    try {
        $running | Stop-Process -Force
        Start-Sleep -Seconds 2
        Write-Host "  [OK]    Flopster.exe terminated."
    } catch {
        Write-Host "  [WARN]  Could not terminate Flopster.exe automatically."
        Write-Host "          Please close it manually, then press Enter to continue."
        Read-Host
    }
} else {
    Write-Host "  [OK]    Flopster is not running."
}
Write-Host

# ── Remove components ─────────────────────────────────────────────────────────
Write-Host "  Removing components..."
Write-Host "  -----------------------------------------------"
Write-Host

$errors = 0

function Remove-Dir {
    param([string]$Path, [string]$Label)
    Write-Host "  [INFO]  Removing ${Label}: $Path"

    try {
        Remove-Item -Path $Path -Recurse -Force -ErrorAction Stop
    } catch {
        Start-Sleep -Seconds 1
        try {
            Remove-Item -Path $Path -Recurse -Force -ErrorAction Stop
        } catch {}
    }

    if (Test-Path $Path) {
        Write-Host "  [ERROR] Could not fully remove: $Path"
        Write-Host "          The directory or some files may still be in use."
        Write-Host "          Close all DAWs and try again, or delete manually."
        $script:errors++
    } else {
        Write-Host "  [OK]    Removed: $Path"
    }
    Write-Host
}

if ($found['vst3_sys']) { Remove-Dir $vst3Sys 'VST3 (system)' }
if ($found['vst3_usr']) { Remove-Dir $vst3Usr 'VST3 (user-local)' }
if ($found['app_msi'])  { Remove-Dir $appMsi  'Standalone (MSI)' }
if ($found['app_zip'])  { Remove-Dir $appZip  'Standalone (zip)' }

if ($found['shortcut']) {
    Write-Host "  [INFO]  Removing Desktop shortcut: $shortcut"
    try {
        Remove-Item -Path $shortcut -Force
        Write-Host "  [OK]    Desktop shortcut removed."
    } catch {
        Write-Host "  [ERROR] Could not remove shortcut: $shortcut"
        $errors++
    }
    Write-Host
}

# ── Registry cleanup (best-effort) ───────────────────────────────────────────
$regKey = 'HKCU:\Software\Shiru\Flopster'
if (Test-Path $regKey) {
    Write-Host "  [INFO]  Removing registry key: $regKey"
    try {
        Remove-Item -Path $regKey -Force
        Write-Host "  [OK]    Registry key removed."
    } catch {
        Write-Host "  [WARN]  Could not remove registry key (non-critical)."
    }

    # Remove parent key if empty
    $parentKey = 'HKCU:\Software\Shiru'
    if (Test-Path $parentKey) {
        $children = Get-ChildItem -Path $parentKey -ErrorAction SilentlyContinue
        if (-not $children) {
            Remove-Item -Path $parentKey -Force -ErrorAction SilentlyContinue
        }
    }
    Write-Host
}

# ── Summary ───────────────────────────────────────────────────────────────────
Write-Host "  -----------------------------------------------"
if ($errors -eq 0) {
    Write-Host
    Write-Host "  +============================================+"
    Write-Host "  |   Flopster uninstalled successfully!       |"
    Write-Host "  +============================================+"
    Write-Host
    Write-Host "   All Flopster files have been removed from this machine."
    Write-Host
    Write-Host "   If your DAW still shows Flopster in its plugin list:"
    Write-Host "     - Ableton:    Options -> Preferences -> Plug-Ins -> Rescan"
    Write-Host "     - FL Studio:  Options -> Manage plugins -> Find more plugins -> Rescan"
    Write-Host "     - Reaper:     Options -> Preferences -> Plug-ins -> VST -> Rescan"
    Write-Host "     - Other DAWs: trigger a plugin rescan / restart the DAW"
    Write-Host
} else {
    Write-Host
    Write-Host "  [WARN]  Uninstall completed with $errors error(s)."
    Write-Host "          Some files may not have been removed — see messages above."
    Write-Host "          You may need to delete them manually or re-run as Administrator."
    Write-Host
}
