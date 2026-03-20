#Requires -Version 5.0
<#
.SYNOPSIS
    Flopster — Windows Quick Run
    Rebuilds (if needed), installs Standalone, then launches it immediately.
    Useful for rapid test cycles without opening a DAW.

.PARAMETER Rebuild
    Force a clean rebuild before installing and running

.PARAMETER Arch
    Target architecture to run: arm64, x64, x86 (default: native host arch)

.PARAMETER Full
    Also install VST3 (default: standalone only)

.EXAMPLE
    .\win-run.ps1                    # build + install standalone + run
    .\win-run.ps1 -Full              # build + install VST3 + standalone + run
    .\win-run.ps1 -Rebuild           # clean build + install + run
    .\win-run.ps1 -Arch x64          # run x64 build specifically
#>

param(
    [switch]$Rebuild,
    [ValidateSet('arm64', 'x64', 'x86')]
    [string]$Arch = '',
    [switch]$Full,
    [switch]$Help
)

$ErrorActionPreference = 'Stop'

if ($Help) {
    Write-Host @"
  Usage: .\win-run.ps1 [-Rebuild] [-Arch <arch>] [-Full] [-Help]

    -Rebuild         Force a clean rebuild before installing and running
    -Full            Also install VST3 (default: standalone only)
    -Arch <arch>     Target architecture to run: arm64, x64, x86
                     Default: native host arch
    -Help            Show this help

  Examples:
    .\win-run.ps1                    -- build + install standalone + run
    .\win-run.ps1 -Full              -- build + install VST3 + standalone + run
    .\win-run.ps1 -Rebuild           -- clean build + install + run
    .\win-run.ps1 -Arch x64          -- run x64 build specifically

"@
    exit 0
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$rootDir = Split-Path -Parent $scriptDir

# Determine target architecture
if (-not $Arch) {
    $Arch = ($env:PROCESSOR_ARCHITECTURE -match 'ARM64') ? 'arm64' : 'x64'
}

# Banner
Write-Host
Write-Host "  +=======================================================+"
if ($Full) {
    Write-Host "  |   Flopster — Rebuild & Run  (full)                  |"
} else {
    Write-Host "  |   Flopster — Rebuild & Run                           |"
}
Write-Host "  |   by Shiru & Resonaura                               |"
Write-Host "  +=======================================================+"
Write-Host

# ── Step 1: Build ─────────────────────────────────────────────────────────────
Write-Host "  [1/3] Building..."
Write-Host "-----------------------------------------------"

$buildArgs = @('-Arch', $Arch)
if ($Rebuild) { $buildArgs += '-Rebuild' }

& "$scriptDir\win-build.ps1" @buildArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "  [ERROR] Build failed."
    exit 1
}

Write-Host

# ── Step 2: Install ──────────────────────────────────────────────────────────
if ($Full) {
    Write-Host "  [2/3] Installing VST3 + Standalone..."
} else {
    Write-Host "  [2/3] Installing Standalone..."
}
Write-Host "-----------------------------------------------"

$onlyValue = $Full ? 'vst3,standalone' : 'standalone'
$installArgs = @('-Only', $onlyValue, '-Arch', $Arch)

& "$scriptDir\win-install.ps1" @installArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "  [ERROR] Install failed."
    exit 1
}

Write-Host

# ── Step 3: Launch ────────────────────────────────────────────────────────────
Write-Host "  [3/3] Launching Standalone ($Arch)..."
Write-Host "-----------------------------------------------"

$exePath = Join-Path $rootDir "build-$Arch\Flopster_artefacts\Release\Standalone\Flopster.exe"

if (-not (Test-Path $exePath)) {
    Write-Host "  [ERROR] Flopster.exe not found: $exePath"
    Write-Host "          The install step may have failed."
    exit 1
}

# Kill any existing instance so we always get a fresh launch
$existing = Get-Process -Name Flopster -ErrorAction SilentlyContinue
if ($existing) {
    Write-Host "  [INFO]  Killing existing Flopster instance..."
    $existing | Stop-Process -Force
    Start-Sleep -Seconds 1
}

Write-Host "  [OK]    Launching: $exePath"
Write-Host

Start-Process -FilePath $exePath

Write-Host
Write-Host "  +=======================================================+"
Write-Host "  |   Flopster launched!                                 |"
Write-Host "  +=======================================================+"
Write-Host
