#Requires -Version 5.0
<#
.SYNOPSIS
    Flopster Windows Build Script
    Builds VST3 + Standalone in Release (or Debug with --debug flag)

.PARAMETER Debug
    Build Debug configuration instead of Release

.PARAMETER Release
    Build Release configuration (default)

.PARAMETER Rebuild
    Remove build directory and rebuild from scratch

.PARAMETER Arch
    Target architecture: arm64, x64, x86
    Defaults to host architecture (x64 on x64, arm64 on ARM64)

.EXAMPLE
    .\win-build.ps1                      # Native arch, Release
    .\win-build.ps1 -Arch x64            # Cross-compile for x64
    .\win-build.ps1 -Arch x86 -Debug     # Cross-compile for x86 Debug
#>

param(
    [switch]$Debug,
    [switch]$Release,
    [switch]$Rebuild,
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
  Usage: .\win-build.ps1 [-Debug] [-Release] [-Rebuild] [-Arch <arch>] [-Help]

    -Debug              Build Debug configuration
    -Release            Build Release configuration (default)
    -Rebuild            Remove build directory and rebuild from scratch
    -Arch <arch>        Target architecture: arm64, x64, x86
                        Default on ARM host : arm64
                        Default on x64 host : x64
    -Help               Show this help

  Examples:
    .\win-build.ps1                     # native arch, Release
    .\win-build.ps1 -Arch x64           # cross-compile for x64
    .\win-build.ps1 -Arch x86           # cross-compile for x86
    .\win-build.ps1 -Arch arm64         # compile for ARM64

"@
    exit 0
}

function Write-Status {
    param(
        [string]$Message,
        [ValidateSet('ok', 'warn', 'error', 'build')]
        [string]$Type = 'build'
    )
    $prefix = "[$Type]".PadRight(10)
    Write-Host "  $prefix $Message"
}

function Get-HostArchitecture {
    if ($env:PROCESSOR_ARCHITECTURE -match 'ARM64') { 'arm64' } else { 'x64' }
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
        $installPath = & $vswhereExe -latest -property installationPath 2>$null
        $version = & $vswhereExe -latest -property installationVersion 2>$null

        if ($installPath -and $version) {
            $major = [int]($version -split '\.' | Select-Object -First 1)
            return @{
                Found            = $true
                InstallPath      = $installPath
                Version          = $version
                Major            = $major
                VSWhere          = $vswhereExe
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

    # For VS >= 18 (e.g. VS 2026): use VS-bundled Ninja
    if ($VSInfo.Found -and $VSInfo.Major -ge 18) {
        $bundledNinja = Join-Path $VSInfo.InstallPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
        if (Test-Path $bundledNinja) {
            $env:PATH = $(Split-Path $bundledNinja) + ';' + $env:PATH
            $result.Generator = 'Ninja'
            $result.UseNinja = $true
            return $result
        }
    }

    # Map major version → CMake generator name for older VS versions
    $genMap = @{
        17 = 'Visual Studio 17 2022'
        16 = 'Visual Studio 16 2019'
        15 = 'Visual Studio 15 2017'
    }

    if ($VSInfo.Found -and $genMap.ContainsKey($VSInfo.Major)) {
        $result.Generator = $genMap[$VSInfo.Major]
        $result.ArchFlag = "-A $vsPlatform"
        return $result
    }

    # Fall back to Ninja in PATH
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
        [string]$UseNinja
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
        @("${env:ProgramFiles}\Microsoft Visual Studio\18\Enterprise", "${env:ProgramFiles}\Microsoft Visual Studio\18\Professional", "${env:ProgramFiles}\Microsoft Visual Studio\18\Community", "${env:ProgramFiles}\Microsoft Visual Studio\18\BuildTools"),
        @("${env:ProgramFiles(x86)}\Microsoft Visual Studio\18\Enterprise", "${env:ProgramFiles(x86)}\Microsoft Visual Studio\18\Professional", "${env:ProgramFiles(x86)}\Microsoft Visual Studio\18\Community", "${env:ProgramFiles(x86)}\Microsoft Visual Studio\18\BuildTools"),
        @("${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise", "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional", "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community", "${env:ProgramFiles}\Microsoft Visual Studio\2022\BuildTools"),
        @("${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Enterprise", "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Professional", "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Community", "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools"),
        @("${env:ProgramFiles}\Microsoft Visual Studio\2019\Enterprise", "${env:ProgramFiles}\Microsoft Visual Studio\2019\Professional", "${env:ProgramFiles}\Microsoft Visual Studio\2019\Community", "${env:ProgramFiles}\Microsoft Visual Studio\2019\BuildTools"),
        @("${env:ProgramFiles(x86)}\Microsoft Visual Studio\2019\Enterprise", "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2019\Professional", "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2019\Community", "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2019\BuildTools")
    )

    foreach ($pathGroup in $searchPaths) {
        foreach ($basePath in $pathGroup) {
            $vcvarsPath = Join-Path $basePath 'VC\Auxiliary\Build\vcvarsall.bat'
            if (Test-Path $vcvarsPath) {
                return $vcvarsPath
            }
        }
    }

    return ''
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
$buildBase = $rootDir
$juceDir = Join-Path $rootDir 'JUCE'

# Determine build type
$buildType = if ($Debug) { 'Debug' } else { 'Release' }

# Get host architecture
$hostArch = Get-HostArchitecture

# Determine target architecture
$targetArch = if ($Arch) { $Arch } else { $hostArch }

# Validate target architecture
if ($targetArch -notmatch '^(arm64|x64|x86)$') {
    Write-Error "Invalid architecture: $targetArch. Valid values: arm64, x64, x86"
}

# Derive build directory
$buildDir = if ($buildType -eq 'Debug') {
    Join-Path $buildBase "build-debug-$targetArch"
} else {
    Join-Path $buildBase "build-$targetArch"
}

# Print banner
Write-Host @"

  ============================================
   Flopster Build Script
   by Shiru & Resonaura
  ============================================

"@

Write-Status "Build type  : $buildType" 'build'
Write-Status "Target arch : $targetArch" 'build'
Write-Status "Host arch   : $hostArch" 'build'
Write-Status "Build dir   : $buildDir" 'build'
Write-Host

# Check cmake
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Status "cmake not found in PATH." 'error'
    Write-Host "  Install from https://cmake.org/download/"
    Write-Host "  and make sure to check 'Add CMake to system PATH' during install."
    exit 1
}

$cmakeVersion = & cmake --version 2>&1 | Select-String 'cmake version'
Write-Status $cmakeVersion 'ok'

# Find Visual Studio and determine generator
Write-Host
$vsInfo = Find-VSInstallation

if ($vsInfo.Found) {
    Write-Status "Found Visual Studio $($vsInfo.Version)" 'ok'
} else {
    Write-Status "Visual Studio not found" 'warn'
}

$genInfo = Find-CMakeGenerator -TargetArch $targetArch -VSInfo $vsInfo -HostArch $hostArch

if (-not $genInfo) {
    Write-Status "No suitable build system found." 'error'
    Write-Host "  Please install one of:"
    Write-Host "    - Visual Studio 2019 or newer (recommended): https://visualstudio.microsoft.com/"
    Write-Host "    - Ninja: https://ninja-build.org/"
    Write-Host "    - Or: winget install Ninja-build.Ninja"
    exit 1
}

Write-Status "CMake generator: $($genInfo.Generator)" 'ok'

if ($genInfo.UseNinja) {
    Write-Status "Using VS-bundled Ninja" 'ok'

    $vcvars = Find-VCVars -VSInstallPath $vsInfo.InstallPath -UseNinja $true

    if ($vcvars) {
        Write-Status "vcvarsall.bat found: $vcvars" 'ok'
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
        Write-Status "vcvarsall.bat not found  -  Ninja will use PATH as-is." 'warn'
        Write-Host "  Cross-compilation may not work correctly."
        Write-Host "  Install Visual Studio 2022 Build Tools:"
        Write-Host "    https://aka.ms/vs/17/release/vs_buildtools.exe"
    }
}

# Check JUCE
Write-Host
if (-not (Test-Path (Join-Path $juceDir 'CMakeLists.txt'))) {
    Write-Status "JUCE not found at $juceDir" 'warn'
    Write-Status "Cloning JUCE 8.0.7..." 'build'

    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        Write-Status "git not found. Install Git from https://git-scm.com/" 'error'
        exit 1
    }

    & git clone --depth 1 --branch 8.0.7 https://github.com/juce-framework/JUCE.git $juceDir 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Status "Failed to clone JUCE" 'error'
        exit 1
    }
    Write-Status "JUCE cloned" 'ok'
} else {
    Write-Status "JUCE found at $juceDir" 'ok'
}

# Clean if rebuild
if ($Rebuild -and (Test-Path $buildDir)) {
    Write-Status "Removing existing build directory..." 'build'
    Remove-Item -Path $buildDir -Recurse -Force
    Write-Status "Removed $buildDir" 'ok'
}

# CMake configure
$needConfigure = -not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))

if ($needConfigure) {
    Write-Host
    Write-Status "Configuring with CMake..." 'build'
    Write-Host "          Generator : $($genInfo.Generator)"
    Write-Host "          Arch flag : $($genInfo.ArchFlag)"
    Write-Host

    if (-not (Test-Path $buildDir)) {
        New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
    }

    $cmakeArgs = @(
        "-S", $rootDir,
        "-B", $buildDir,
        "-G", $genInfo.Generator,
        "-DCMAKE_BUILD_TYPE=$buildType"
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
        Write-Host
        Write-Status "CMake configuration failed." 'error'
        exit 1
    }
    Write-Status "CMake configuration done" 'ok'
} else {
    Write-Status "CMake already configured -- skipping (use -Rebuild to reconfigure)" 'build'
}

# Build
Write-Host
Write-Status "Building Flopster ($buildType, $targetArch)..." 'build'
Write-Host

# Add build dir to PATH so juce_vst3_helper is found during post-build steps
$env:PATH = $buildDir + ';' + $env:PATH

& cmake --build $buildDir --config $buildType --parallel 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host
    Write-Status "Build failed." 'error'
    Write-Host "          Check output above for errors."
    exit 1
}

Write-Host

# Verify artifacts
Write-Status "Verifying artifacts..." 'build'

$artefactBase = Join-Path $buildDir "Flopster_artefacts\$buildType"
$allOk = $true

# VST3  -  the inner DLL name encodes the arch: arm64-win, x86_64-win, x86-win
$vst3ArchDirMap = @{
    'arm64' = 'arm64-win'
    'x64'   = 'x86_64-win'
    'x86'   = 'x86-win'
}
$vst3ArchDir = $vst3ArchDirMap[$targetArch]

$vst3Path = Join-Path $artefactBase "VST3\Flopster.vst3\Contents\$vst3ArchDir\Flopster.vst3"
if (Test-Path $vst3Path) {
    Write-Status "VST3\Flopster.vst3\Contents\$vst3ArchDir\Flopster.vst3" 'ok'
} elseif (Test-Path (Join-Path $artefactBase "VST3\Flopster.vst3")) {
    Write-Status "VST3\Flopster.vst3  (bundle present)" 'ok'
} else {
    Write-Status "VST3 artefact not found at:" 'warn'
    Write-Host "         $vst3Path"
    $allOk = $false
}

$standaloneExe = Join-Path $artefactBase "Standalone\Flopster.exe"
if (Test-Path $standaloneExe) {
    Write-Status "Standalone\Flopster.exe" 'ok'
} else {
    Write-Status "Standalone artefact not found at:" 'warn'
    Write-Host "         $standaloneExe"
    $allOk = $false
}

Write-Host

if (-not $allOk) {
    Write-Status "Some artefacts missing -- check build output above." 'warn'
} else {
    Write-Host "  ============================================"
    Write-Host "   Build complete!"
    Write-Host "  ============================================"
}

Write-Host
Write-Host "   Target arch : $targetArch"
Write-Host "   VST3        ->  $artefactBase\VST3\Flopster.vst3"
Write-Host "   Standalone  ->  $artefactBase\Standalone\Flopster.exe"
Write-Host
Write-Host "   Run win-install.ps1 -Arch $targetArch to install into the system."
Write-Host
