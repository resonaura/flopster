#Requires -Version 5.0
<#
.SYNOPSIS
    Flopster Windows Installer
    Builds (if needed), installs VST3 to Common/VST3 and LocalAppData,
    copies standalone to Desktop, and bundles resources into each install.

.PARAMETER Rebuild
    Force a clean rebuild even if artefacts exist

.PARAMETER Only
    Install only the specified format(s). Comma-separated list of: vst3, standalone

.PARAMETER Arch
    Target architecture to install: arm64, x64, x86
    Defaults to host architecture

.EXAMPLE
    .\win-install.ps1                              # install all (native arch)
    .\win-install.ps1 -Arch x64                    # install x64 build
    .\win-install.ps1 -Only vst3                   # reinstall VST3 only
    .\win-install.ps1 -Rebuild -Only vst3          # force rebuild, then install VST3
#>

param(
    [switch]$Rebuild,
    [string]$Only = '',
    [ValidateSet('arm64', 'x64', 'x86')]
    [string]$Arch = '',
    [switch]$Help
)

$ErrorActionPreference = 'Stop'

# ──────────────────────────────────────────────────────────────────────────────
# Helper Functions
# ──────────────────────────────────────────────────────────────────────────────

function Show-Help {
    Write-Host @"
  Usage: .\win-install.ps1 [-Rebuild] [-Only <formats>] [-Arch <arch>] [-Help]

  Options:
    -Rebuild, -r        Force a clean rebuild even if artefacts exist.
    -Only <formats>     Install only the specified format(s).
                        Comma-separated list of: vst3, standalone
    -Arch <arch>        Target architecture: arm64, x64, x86
                        Default on ARM host : arm64
                        Default on x64 host : x64

  Examples:
    .\win-install.ps1                              -- install all (native arch)
    .\win-install.ps1 -Arch x64                    -- install x64 build
    .\win-install.ps1 -Arch arm64                  -- install ARM64 build
    .\win-install.ps1 -Arch x86                    -- install x86 build
    .\win-install.ps1 -Only vst3                   -- reinstall VST3 only
    .\win-install.ps1 -Only standalone             -- reinstall Standalone only
    .\win-install.ps1 -Only vst3,standalone        -- reinstall both
    .\win-install.ps1 -Rebuild -Only vst3          -- force rebuild then install VST3

"@
    exit 0
}

function Write-Status {
    param(
        [string]$Message,
        [ValidateSet('ok', 'warn', 'error', 'info')]
        [string]$Type = 'info'
    )
    $prefix = "[$Type]".PadRight(9)
    Write-Host "  $prefix $Message"
}

function Get-HostArchitecture {
    $env:PROCESSOR_ARCHITECTURE -match 'ARM64' ? 'arm64' : 'x64'
}

function Parse-OnlyFormats {
    param(
        [string]$OnlyString
    )

    $formats = @{
        vst3       = 0
        standalone = 0
    }

    if ([string]::IsNullOrWhiteSpace($OnlyString)) {
        return @{ vst3 = 1; standalone = 1 }
    }

    $tokens = $OnlyString -split ',' | ForEach-Object { $_.Trim().ToLower() }

    foreach ($token in $tokens) {
        if ($token -eq 'vst3') {
            $formats.vst3 = 1
        } elseif ($token -eq 'standalone') {
            $formats.standalone = 1
        } else {
            Write-Host "  [ERROR] Unknown format '$token'. Valid values: vst3, standalone"
            exit 1
        }
    }

    return $formats
}

function Find-VSInstallation {
    $vswhereExe = @(
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe"
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1

    if (-not $vswhereExe) {
        return @{ Found = $false }
    }

    try {
        $installPath = & $vswhereExe -latest -version "[17.0,18.0)" -requires Microsoft.Component.MSBuild -property installationPath 2>$null

        if ($installPath) {
            return @{
                Found           = $true
                InstallPath     = $installPath
                VSWhere         = $vswhereExe
            }
        }
    } catch {
        # vswhere might not be available
    }

    return @{ Found = $false }
}

function Find-CMakeGenerator {
    param(
        [string]$TargetArch,
        [hashtable]$VSInfo,
        [string]$HostArch
    )

    $result = @{
        Generator      = ''
        ArchFlag       = ''
        UseNinja       = $false
        VCVarsArch     = ''
        VCVars         = ''
    }

    # Map TARGET_ARCH → VS platform name
    $vsPlatformMap = @{
        'arm64' = 'ARM64'
        'x64'   = 'x64'
        'x86'   = 'Win32'
    }
    $vsPlatform = $vsPlatformMap[$TargetArch]

    # Map TARGET_ARCH → vcvarsall argument (for Ninja cross-compile)
    if ($HostArch -eq 'arm64') {
        $vcvarsMap = @{
            'arm64' = 'arm64'
            'x64'   = 'arm64_amd64'
            'x86'   = 'arm64_x86'
        }
    } else {
        $vcvarsMap = @{
            'arm64' = 'amd64_arm64'
            'x64'   = 'amd64'
            'x86'   = 'amd64_x86'
        }
    }
    $result.VCVarsArch = $vcvarsMap[$TargetArch]

    # Check for Visual Studio 2022
    if ($VSInfo.Found) {
        $msbuildPath = Join-Path $VSInfo.InstallPath 'MSBuild\Current\Bin\MSBuild.exe'
        if (Test-Path $msbuildPath) {
            $result.Generator = 'Visual Studio 17 2022'
            $result.ArchFlag = "-A $vsPlatform"
            return $result
        }
    }

    # Fall back to Ninja
    if (Get-Command ninja -ErrorAction SilentlyContinue) {
        $result.Generator = 'Ninja'
        $result.UseNinja = $true
        return $result
    }

    return $null
}

function Find-VCVars {
    param(
        [string]$VSInstallPath,
        [bool]$UseNinja
    )

    if (-not $UseNinja) {
        return ''
    }

    if ($VSInstallPath) {
        $vcvarsPath = Join-Path $VSInstallPath 'VC\Auxiliary\Build\vcvarsall.bat'
        if (Test-Path $vcvarsPath) {
            return $vcvarsPath
        }
    }

    # Hardcoded fallback list
    $searchPaths = @(
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\BuildTools",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Enterprise",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Professional",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Community",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools"
    )

    foreach ($basePath in $searchPaths) {
        $vcvarsPath = Join-Path $basePath 'VC\Auxiliary\Build\vcvarsall.bat'
        if (Test-Path $vcvarsPath) {
            return $vcvarsPath
        }
    }

    return ''
}

function Install-VST3 {
    param(
        [string]$SourcePath,
        [string]$DestinationPath,
        [string]$AssetsPath,
        [string]$SamplesPath
    )

    Write-Status "Installing VST3 to: $DestinationPath" 'info'

    # Remove existing bundle if present
    if (Test-Path $DestinationPath) {
        try {
            Remove-Item -Path $DestinationPath -Recurse -Force
        } catch {
            Write-Status "Could not remove existing bundle at: $DestinationPath" 'error'
            Write-Host "          Make sure your DAW is closed and try again."
            return $false
        }
    }

    # Create parent directory
    $parent = Split-Path -Parent $DestinationPath
    if (-not (Test-Path $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }

    # Copy VST3 bundle
    try {
        Copy-Item -Path $SourcePath -Destination $DestinationPath -Recurse -Force
    } catch {
        Write-Status "Failed to copy VST3 bundle to: $DestinationPath" 'error'
        return $false
    }
    Write-Status "Bundle copied." 'ok'

    # Copy resources
    $resPath = Join-Path $DestinationPath 'Contents\Resources'
    if (-not (Test-Path $resPath)) {
        New-Item -ItemType Directory -Path $resPath -Force | Out-Null
    }

    $scanlinesPath = Join-Path $AssetsPath 'scanlines.png'
    if (Test-Path $scanlinesPath) {
        Copy-Item -Path $scanlinesPath -Destination $resPath -Force | Out-Null
    }

    # Copy samples
    $resSamplesPath = Join-Path $resPath 'samples'
    if (Test-Path $resSamplesPath) {
        Remove-Item -Path $resSamplesPath -Recurse -Force | Out-Null
    }

    if (Test-Path $SamplesPath) {
        try {
            Copy-Item -Path $SamplesPath -Destination $resSamplesPath -Recurse -Force
            Write-Status "Samples copied to Resources\samples\." 'ok'
        } catch {
            Write-Status "Could not copy samples to: $resSamplesPath" 'warn'
        }
    }

    Write-Status "VST3 installed: $DestinationPath" 'ok'
    return $true
}

# ──────────────────────────────────────────────────────────────────────────────
# Main Script
# ──────────────────────────────────────────────────────────────────────────────

if ($Help) {
    Show-Help
}

# Setup paths
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$rootDir = Split-Path -Parent $scriptDir
$assetsDir = Join-Path $rootDir 'assets'
$samplesDir = Join-Path $rootDir 'samples'

# Get host architecture
$hostArch = Get-HostArchitecture

# Determine target architecture
$targetArch = $Arch ? $Arch : $hostArch

# Validate target architecture
if ($targetArch -notmatch '^(arm64|x64|x86)$') {
    Write-Error "Invalid architecture: $targetArch. Valid values: arm64, x64, x86"
}

# Parse install formats
$formats = Parse-OnlyFormats -OnlyString $Only

# Install destinations
$vst3Dst1 = Join-Path $env:CommonProgramFiles 'VST3\Flopster.vst3'
$vst3Dst2 = Join-Path $env:LOCALAPPDATA 'Programs\VstPlugins\Flopster.vst3'
$desktopPath = Join-Path $env:USERPROFILE 'Desktop'
$exeDst = Join-Path $desktopPath 'Flopster.exe'

# Build directories
$buildDir = Join-Path $rootDir "build-$targetArch"
$artefactsDir = Join-Path $buildDir 'Flopster_artefacts\Release'
$vst3Src = Join-Path $artefactsDir 'VST3\Flopster.vst3'
$exeSrc = Join-Path $artefactsDir 'Standalone\Flopster.exe'

# Validate that at least one format was selected
if ($formats.vst3 -eq 0 -and $formats.standalone -eq 0) {
    Write-Host "  [ERROR] Nothing to install. Check your -Only arguments."
    Write-Host "          Valid values: vst3, standalone"
    exit 1
}

# Print banner
Write-Host @"

  +============================================+
  |   Flopster Plugin Installer                |
  |   by Shiru & Resonaura                     |
  +============================================+

"@

$targets = @()
if ($formats.vst3 -eq 1) { $targets += "VST3" }
if ($formats.standalone -eq 1) { $targets += "Standalone" }

Write-Status "Target arch : $targetArch" 'info'
Write-Status "Host arch   : $hostArch" 'info'
Write-Status "Formats     : $($targets -join ', ')" 'info'
Write-Status "Build dir   : $buildDir" 'info'
Write-Host

# ── 1. Check prerequisites ────────────────────────────────────────────────────
Write-Host "[1/5] Checking prerequisites..."
Write-Host "-----------------------------------------------"
Write-Host

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Status "cmake not found in PATH." 'error'
    Write-Host "          Install CMake from https://cmake.org/download/"
    Write-Host "          and make sure it is added to your PATH."
    exit 1
}

$cmakeVersion = & cmake --version 2>&1 | Select-String 'cmake version'
Write-Status "cmake $($cmakeVersion -replace '.*version ', '')" 'ok'

$hasNinja = $false
if (Get-Command ninja -ErrorAction SilentlyContinue) {
    $hasNinja = $true
    $ninjaVersion = & ninja --version 2>&1
    Write-Status "ninja $ninjaVersion" 'ok'
} else {
    Write-Status "ninja not found — will try Visual Studio generator as fallback." 'warn'
}

# ── 2. Determine generator + arch flags ──────────────────────────────────────
Write-Host
Write-Status "Finding build system..." 'info'
Write-Host

$vsInfo = Find-VSInstallation

if ($vsInfo.Found) {
    Write-Status "Visual Studio 2022 found" 'ok'
} else {
    Write-Status "Visual Studio 2022 not found" 'warn'
}

$genInfo = Find-CMakeGenerator -TargetArch $targetArch -VSInfo $vsInfo -HostArch $hostArch

if (-not $genInfo) {
    Write-Status "Neither Visual Studio 2022 nor ninja were found." 'error'
    Write-Host "          Please install one of the following:"
    Write-Host "            - Visual Studio 2022 (Community/Pro/Enterprise)"
    Write-Host "                https://visualstudio.microsoft.com/"
    Write-Host "            - ninja  (via winget: winget install Ninja-build.Ninja)"
    exit 1
}

Write-Status "Using generator : $($genInfo.Generator)" 'info'
Write-Status "Target arch     : $targetArch" 'info'
Write-Host

# ── Activate MSVC cross-compile environment for Ninja ────────────────────────
if ($genInfo.UseNinja) {
    $vcvars = Find-VCVars -VSInstallPath $vsInfo.InstallPath -UseNinja $true

    if ($vcvars) {
        Write-Status "Activating MSVC environment for: $($genInfo.VCVarsArch)" 'ok'

        # Execute vcvarsall.bat and capture environment
        $vcvarsOutput = & cmd /c """$vcvars"" $($genInfo.VCVarsArch) && set" 2>&1
        if ($LASTEXITCODE -ne 0) {
            Write-Status "vcvarsall.bat failed for arch: $($genInfo.VCVarsArch)" 'error'
            exit 1
        }

        # Parse and set environment variables
        $vcvarsOutput | ForEach-Object {
            if ($_ -match '^([^=]+)=(.*)$') {
                [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2])
            }
        }

        Write-Status "MSVC environment activated." 'ok'
    } else {
        Write-Status "vcvarsall.bat not found — Ninja will use PATH as-is." 'warn'
    }
}

# ── 3. Build if needed ────────────────────────────────────────────────────────
Write-Host
Write-Host "[2/5] Build..."
Write-Host "-----------------------------------------------"
Write-Host

$needBuild = $false
if ($Rebuild) { $needBuild = $true }
if (-not (Test-Path $exeSrc)) { $needBuild = $true }
if (-not (Test-Path $vst3Src)) { $needBuild = $true }

if ($needBuild) {
    if ($Rebuild -and (Test-Path $buildDir)) {
        Write-Status "Removing old build directory..." 'info'
        try {
            Remove-Item -Path $buildDir -Recurse -Force
        } catch {
            Write-Status "Could not remove old build dir: $buildDir" 'error'
            exit 1
        }
        Write-Status "Old build directory removed." 'ok'
    }

    Write-Status "Configuring CMake..." 'info'

    if (-not (Test-Path $buildDir)) {
        New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
    }

    $cmakeArgs = @(
        "-S", $rootDir,
        "-B", $buildDir,
        "-G", $genInfo.Generator,
        "-DCMAKE_BUILD_TYPE=Release"
    )

    if ($genInfo.UseNinja) {
        $cmakeArgs += @(
            "-DCMAKE_SYSTEM_PROCESSOR=$targetArch",
            "-DCMAKE_SYSTEM_NAME=Windows"
        )
    } else {
        $cmakeArgs += $genInfo.ArchFlag -split ' ' | Where-Object { $_ }
    }

    & cmake @cmakeArgs 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Status "CMake configuration failed." 'error'
        exit 1
    }
    Write-Status "CMake configuration succeeded." 'ok'

    Write-Status "Building (Release, $targetArch)..." 'info'
    & cmake --build $buildDir --config Release --parallel 2>&1

    if ($LASTEXITCODE -ne 0) {
        Write-Status "Build failed. Check the output above for errors." 'error'
        exit 1
    }
    Write-Status "Build succeeded." 'ok'
} else {
    Write-Status "Release artefacts already present -- skipping build." 'ok'
    Write-Host "          Pass -Rebuild to force recompilation."
}

Write-Host

# ── 4. Verify artefacts ───────────────────────────────────────────────────────
Write-Host "[3/5] Verifying artefacts..."
Write-Host "-----------------------------------------------"
Write-Host

if ($formats.vst3 -eq 1) {
    if (-not (Test-Path $vst3Src)) {
        Write-Status "VST3 bundle not found: $vst3Src" 'error'
        Write-Host "          Try running with -Rebuild or -Arch $targetArch."
        exit 1
    }
    Write-Status "VST3 bundle found." 'ok'
}

if ($formats.standalone -eq 1) {
    if (-not (Test-Path $exeSrc)) {
        Write-Status "Standalone executable not found: $exeSrc" 'error'
        Write-Host "          Try running with -Rebuild or -Arch $targetArch."
        exit 1
    }
    Write-Status "Standalone .exe found." 'ok'
}

if (-not (Test-Path $samplesDir)) {
    Write-Status "Samples directory not found: $samplesDir" 'error'
    exit 1
}
Write-Status "Samples directory found." 'ok'
Write-Host

# ── 5. Install VST3 bundles ───────────────────────────────────────────────────
if ($formats.vst3 -eq 1) {
    Write-Host "[4/5] Installing VST3..."
    Write-Host "-----------------------------------------------"
    Write-Host

    $result1 = Install-VST3 -SourcePath $vst3Src -DestinationPath $vst3Dst1 -AssetsPath $assetsDir -SamplesPath $samplesDir
    if (-not $result1) {
        Write-Status "Failed to install to system VST3 directory." 'warn'
        Write-Host "          You may need to run this script as Administrator."
        Write-Host "          Continuing with user-local installation..."
    }

    Write-Host

    $result2 = Install-VST3 -SourcePath $vst3Src -DestinationPath $vst3Dst2 -AssetsPath $assetsDir -SamplesPath $samplesDir
    if (-not $result2) {
        Write-Status "Failed to install to user-local VST3 directory." 'error'
        exit 1
    }
} else {
    Write-Host "[4/5] Skipping VST3 (not in -Only list)"
    Write-Host "-----------------------------------------------"
}

Write-Host

# ── 6. Install Standalone to Desktop ─────────────────────────────────────────
if ($formats.standalone -eq 1) {
    Write-Host "[5/5] Installing Standalone to Desktop..."
    Write-Host "-----------------------------------------------"
    Write-Host

    if (Test-Path $exeDst) {
        Remove-Item -Path $exeDst -Force
    }

    try {
        Copy-Item -Path $exeSrc -Destination $exeDst -Force
    } catch {
        Write-Status "Could not copy Flopster.exe to Desktop." 'error'
        exit 1
    }
    Write-Status "Flopster.exe copied to: $exeDst" 'ok'
} else {
    Write-Host "[5/5] Skipping Standalone (not in -Only list)"
    Write-Host "-----------------------------------------------"
}

Write-Host

# ── Summary ───────────────────────────────────────────────────────────────────
Write-Host
Write-Host "  +============================================+"
Write-Host "  |   Installation complete!                  |"
Write-Host "  +============================================+"
Write-Host

if ($formats.vst3 -eq 1) {
    Write-Host "   VST3 (system)  : $vst3Dst1"
    Write-Host "   VST3 (user)    : $vst3Dst2"
}
if ($formats.standalone -eq 1) {
    Write-Host "   Standalone     : $exeDst"
}

Write-Host
Write-Host "  Next steps:"
Write-Host "     - Restart your DAW and rescan plugins."
Write-Host "     - In Ableton: Options -> Preferences -> Plug-Ins -> Rescan."
Write-Host "     - In FL Studio: Options -> Manage plugins -> Find more plugins."
Write-Host "     - In Reaper: Options -> Preferences -> Plug-ins -> VST -> Rescan."

if ($formats.standalone -eq 1) {
    Write-Host "     - Run Flopster.exe on the Desktop to use the standalone version."
}

Write-Host
Write-Host "   Architecture installed: $targetArch"
Write-Host
