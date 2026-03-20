#Requires -Version 5.0
<#
.SYNOPSIS
    Flopster — Windows MSI Installer Builder
    Builds a proper Windows Installer (.msi) using WiX Toolset.
    Prefers WiX v4 (dotnet tool), falls back to WiX v3 (candle/light).
    Optionally builds the plugin from source if artefacts are missing.

.PARAMETER Rebuild
    Force clean rebuild of plugin before packaging

.PARAMETER NoBuild
    Skip build entirely; fail if artefacts are missing

.PARAMETER Arch
    Target architecture: arm64, x64, x86 (default: native host arch)

.PARAMETER Out
    Output directory for the .msi (default: dist\)

.EXAMPLE
    .\win-msi.ps1                    # build + package, native arch
    .\win-msi.ps1 -Arch x64          # package x64
    .\win-msi.ps1 -Rebuild           # force rebuild then package
    .\win-msi.ps1 -NoBuild -Arch x86 # package existing x86 artefacts
#>

param(
    [switch]$Rebuild,
    [switch]$NoBuild,
    [ValidateSet('arm64', 'x64', 'x86')]
    [string]$Arch = '',
    [string]$Out  = '',
    [switch]$Help
)

$ErrorActionPreference = 'Stop'

if ($Help) {
    Write-Host @"
  Usage: .\win-msi.ps1 [-Rebuild] [-NoBuild] [-Arch <arch>] [-Out <dir>] [-Help]

  Flags:
    -Rebuild          Force clean rebuild of plugin before packaging.
    -NoBuild          Skip CMake build; fail if artefacts are missing.
    -Arch <arch>      Target architecture: arm64, x64, x86
    -Out <dir>        Output directory for the .msi  (default: dist\)
    -Help             Show this message and exit.

  Prerequisites:
    cmake              https://cmake.org/download/
    Visual Studio 2022 https://visualstudio.microsoft.com/
      OR  Ninja        winget install Ninja-build.Ninja
    WiX v4 (preferred) dotnet tool install --global wix
      OR  WiX v3       https://wixtoolset.org/releases/

"@
    exit 0
}

if ($Rebuild -and $NoBuild) {
    Write-Host "  [ERROR] -Rebuild and -NoBuild are mutually exclusive."
    exit 1
}

# ── Paths ─────────────────────────────────────────────────────────────────────
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$rootDir   = Split-Path -Parent $scriptDir

# ── Read version from package.json ───────────────────────────────────────────
$packageJson   = Join-Path $rootDir 'package.json'
$versionShort  = (Get-Content $packageJson -Raw | ConvertFrom-Json).version
$version       = "$versionShort.0.0"
$upgradeCode   = '{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}'

# ── Paths ─────────────────────────────────────────────────────────────────────
$assetsDir   = Join-Path $rootDir 'assets'
$samplesDir  = Join-Path $rootDir 'samples'

if (-not $Arch) {
    $Arch = ($env:PROCESSOR_ARCHITECTURE -match 'ARM64') ? 'arm64' : 'x64'
}

$hostArch = ($env:PROCESSOR_ARCHITECTURE -match 'ARM64') ? 'arm64' : 'x64'

$buildDir    = Join-Path $rootDir "build-$Arch"
$artefactDir = Join-Path $buildDir 'Flopster_artefacts\Release'
$vst3Src     = Join-Path $artefactDir 'VST3\Flopster.vst3'
$exeSrc      = Join-Path $artefactDir 'Standalone\Flopster.exe'
$wixWork     = Join-Path $buildDir 'wix-work'
$stage       = Join-Path $wixWork 'stage'
$msiName     = "Flopster-$versionShort-$Arch.msi"
$outDir      = $Out ? $Out : (Join-Path $rootDir 'dist')

# ── Banner ────────────────────────────────────────────────────────────────────
Write-Host
Write-Host "  +=======================================================+"
Write-Host "  |   Flopster MSI Builder  v$versionShort                         |"
Write-Host "  |   by Shiru & Resonaura                               |"
Write-Host "  +=======================================================+"
Write-Host
Write-Host "  [INFO]  Target arch : $Arch"
Write-Host "  [INFO]  Host arch   : $hostArch"
Write-Host "  [INFO]  Version     : $versionShort"
Write-Host "  [INFO]  MSI name    : $msiName"
Write-Host "  [INFO]  Build dir   : $buildDir"
Write-Host

# ── STEP 1 — Check prerequisites ─────────────────────────────────────────────
Write-Host "[1/6] Checking prerequisites..."
Write-Host "-----------------------------------------------"
Write-Host

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Host "  [ERROR] cmake not found in PATH."
    Write-Host "          Download from: https://cmake.org/download/"
    exit 1
}
$cmakeVer = & cmake --version 2>&1 | Select-String 'cmake version'
Write-Host "  [OK]    $cmakeVer"

# ── Detect WiX: v4 preferred, v3 fallback ────────────────────────────────────
$wixVer     = 0
$wix4Cmd    = ''
$wix3Candle = ''
$wix3Light  = ''

if (Get-Command wix -ErrorAction SilentlyContinue) {
    $wix4Ver    = & wix --version 2>&1
    $wix4Cmd    = 'wix'
    $wixVer     = 4
    Write-Host "  [OK]    WiX v4 found: $wix4Ver"
} elseif (Get-Command candle.exe -ErrorAction SilentlyContinue) {
    $wix3Candle = 'candle.exe'
    $wix3Light  = 'light.exe'
    $wixVer     = 3
    Write-Host "  [OK]    WiX v3 found in PATH."
} else {
    $wix3Paths = @(
        "${env:ProgramFiles(x86)}\WiX Toolset v3.14\bin",
        "${env:ProgramFiles(x86)}\WiX Toolset v3.11\bin",
        "${env:ProgramFiles(x86)}\WiX Toolset v3.10\bin",
        "${env:ProgramFiles}\WiX Toolset v3.14\bin",
        "${env:ProgramFiles}\WiX Toolset v3.11\bin"
    )
    foreach ($p in $wix3Paths) {
        if (Test-Path (Join-Path $p 'candle.exe')) {
            $wix3Candle = Join-Path $p 'candle.exe'
            $wix3Light  = Join-Path $p 'light.exe'
            $wixVer     = 3
            Write-Host "  [OK]    WiX v3 found at: $p"
            break
        }
    }
}

if ($wixVer -eq 0) {
    Write-Host
    Write-Host "  [ERROR] WiX Toolset not found. Please install one of the following:"
    Write-Host
    Write-Host "    WiX v4 (recommended — requires .NET SDK):"
    Write-Host "      dotnet tool install --global wix"
    Write-Host "      .NET SDK: https://dotnet.microsoft.com/download"
    Write-Host
    Write-Host "    WiX v3 (legacy):"
    Write-Host "      https://wixtoolset.org/releases/"
    Write-Host
    exit 1
}

Write-Host "  [INFO]  Using WiX version : $wixVer"
Write-Host

# ── STEP 2 — Build plugin (if needed) ────────────────────────────────────────
Write-Host "[2/6] Build..."
Write-Host "-----------------------------------------------"
Write-Host

if ($NoBuild) {
    Write-Host "  [INFO]  -NoBuild: skipping build step."
} else {
    $needBuild = $Rebuild -or (-not (Test-Path $exeSrc)) -or (-not (Test-Path $vst3Src))

    if (-not $needBuild) {
        Write-Host "  [OK]    Release artefacts already present — skipping build."
        Write-Host "          Pass -Rebuild to force recompilation."
    } else {
        $buildArgs = @('-Arch', $Arch)
        if ($Rebuild) { $buildArgs += '-Rebuild' }

        & "$scriptDir\win-build.ps1" @buildArgs
        if ($LASTEXITCODE -ne 0) {
            Write-Host "  [ERROR] Build failed."
            exit 1
        }
        Write-Host "  [OK]    Build succeeded."
    }
}

Write-Host

# ── STEP 3 — Verify artefacts ─────────────────────────────────────────────────
Write-Host "[3/6] Verifying artefacts..."
Write-Host "-----------------------------------------------"
Write-Host

if (-not (Test-Path $vst3Src)) {
    Write-Host "  [ERROR] VST3 bundle not found: $vst3Src"
    Write-Host "          Run without -NoBuild, or pass -Rebuild."
    exit 1
}
Write-Host "  [OK]    VST3 bundle:  $vst3Src"

if (-not (Test-Path $exeSrc)) {
    Write-Host "  [ERROR] Standalone not found: $exeSrc"
    Write-Host "          Run without -NoBuild, or pass -Rebuild."
    exit 1
}
Write-Host "  [OK]    Standalone:   $exeSrc"

if (-not (Test-Path $samplesDir)) {
    Write-Host "  [ERROR] Samples directory not found: $samplesDir"
    exit 1
}
Write-Host "  [OK]    Samples directory found."

$hasBanner = Test-Path (Join-Path $assetsDir 'app.png')
if ($hasBanner) {
    Write-Host "  [OK]    Banner image (app.png) found."
} else {
    Write-Host "  [WARN]  app.png not found — installer will use default WiX banner."
}

Write-Host

# ── STEP 4 — Stage files ──────────────────────────────────────────────────────
Write-Host "[4/6] Staging installation layout..."
Write-Host "-----------------------------------------------"
Write-Host
Write-Host "   Stage layout:"
Write-Host "     stage\vst3\Flopster.vst3\   ->  %CommonProgramFiles%\VST3\Flopster.vst3\"
Write-Host "     stage\standalone\           ->  %ProgramFiles%\Flopster\"
Write-Host

if (Test-Path $wixWork) { Remove-Item -Path $wixWork -Recurse -Force }
New-Item -ItemType Directory -Path $stage -Force | Out-Null

# Stage VST3 bundle
$stageVst3 = Join-Path $stage 'vst3\Flopster.vst3'
New-Item -ItemType Directory -Path (Split-Path $stageVst3) -Force | Out-Null
Write-Host "  [INFO]  Staging VST3 bundle..."
Copy-Item -Path $vst3Src -Destination $stageVst3 -Recurse -Force

$vst3Res = Join-Path $stageVst3 'Contents\Resources'
if (-not (Test-Path $vst3Res)) { New-Item -ItemType Directory -Path $vst3Res -Force | Out-Null }
$scanlinesPath = Join-Path $assetsDir 'scanlines.png'
if (Test-Path $scanlinesPath) { Copy-Item -Path $scanlinesPath -Destination $vst3Res -Force | Out-Null }
Copy-Item -Path $samplesDir -Destination (Join-Path $vst3Res 'samples') -Recurse -Force
Write-Host "  [OK]    VST3 staged (with resources + samples)."

# Stage Standalone
$stageSa = Join-Path $stage 'standalone'
New-Item -ItemType Directory -Path $stageSa -Force | Out-Null
Write-Host "  [INFO]  Staging Standalone application..."
Copy-Item -Path $exeSrc -Destination (Join-Path $stageSa 'Flopster.exe') -Force
if (Test-Path $scanlinesPath) { Copy-Item -Path $scanlinesPath -Destination $stageSa -Force | Out-Null }
Copy-Item -Path $samplesDir -Destination (Join-Path $stageSa 'samples') -Recurse -Force
Write-Host "  [OK]    Standalone staged (with resources + samples)."

# Convert PNG → ICO
$hasIco = $false
$icoPath = Join-Path $wixWork 'Flopster.ico'
if ($hasBanner) {
    $appPng = Join-Path $assetsDir 'app.png'
    Copy-Item -Path $appPng -Destination (Join-Path $wixWork 'banner.png') -Force | Out-Null
    Write-Host "  [OK]    Banner image staged."
    try {
        Add-Type -AssemblyName System.Drawing
        $bmp = [System.Drawing.Bitmap]::new($appPng)
        $ico = [System.Drawing.Icon]::FromHandle($bmp.GetHicon())
        $fs  = [System.IO.File]::OpenWrite($icoPath)
        $ico.Save($fs)
        $fs.Close(); $ico.Dispose(); $bmp.Dispose()
        $hasIco = $true
        Write-Host "  [OK]    Icon converted: Flopster.ico"
    } catch {
        Write-Host "  [WARN]  PNG to ICO conversion failed — installer will have no icon."
    }
}

Write-Host

# ── STEP 5 — Generate WXS file ───────────────────────────────────────────────
Write-Host "[5/6] Generating WiX source file..."
Write-Host "-----------------------------------------------"
Write-Host

# ── Helper: stable GUID seeded by a string (MD5-based) ───────────────────────
function Get-DeterministicGuid([string]$seed) {
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($seed)
    $hash  = [System.Security.Cryptography.MD5]::Create().ComputeHash($bytes)
    return [System.Guid]::new($hash).ToString('B').ToUpper()
}

# ── Helper: recursively walk a directory and emit WiX XML ────────────────────
function Build-DirXml {
    param(
        [string]$PhysicalPath,
        [string]$DirId,
        [System.Collections.Generic.List[string]]$CompIds,
        [int]$Indent = 12
    )
    $pad   = ' ' * $Indent
    $lines = [System.Collections.Generic.List[string]]::new()

    $items = Get-ChildItem -LiteralPath $PhysicalPath
    $files = @($items | Where-Object { -not $_.PSIsContainer })
    $dirs  = @($items | Where-Object { $_.PSIsContainer })

    if ($files.Count -gt 0) {
        $compId   = ('comp_' + $DirId) -replace '[^a-zA-Z0-9_]', '_'
        $compGuid = Get-DeterministicGuid "comp:$DirId"
        $CompIds.Add($compId) | Out-Null

        $lines.Add("$pad<Component Id=`"$compId`" Guid=`"$compGuid`">")
        $first = $true
        foreach ($f in $files) {
            $fileId  = ('file_' + $DirId + '_' + $f.Name) -replace '[^a-zA-Z0-9_]', '_'
            $keyPath = if ($first) { 'yes' } else { 'no' }
            $lines.Add("$pad    <File Id=`"$fileId`" Source=`"$($f.FullName)`" KeyPath=`"$keyPath`" />")
            $first = $false
        }
        $lines.Add("$pad</Component>")
    }

    foreach ($d in $dirs) {
        $childId    = $DirId + '_' + ($d.Name -replace '[^a-zA-Z0-9_]', '_')
        $lines.Add("$pad<Directory Id=`"$childId`" Name=`"$($d.Name)`">")
        Build-DirXml -PhysicalPath $d.FullName -DirId $childId -CompIds $CompIds -Indent ($Indent + 4) |
            ForEach-Object { $lines.Add($_) }
        $lines.Add("$pad</Directory>")
    }

    return $lines
}

# Walk VST3 bundle
$vst3CompIds = [System.Collections.Generic.List[string]]::new()
$vst3Xml     = (Build-DirXml -PhysicalPath $stageVst3 -DirId 'dir_VST3Bundle' -CompIds $vst3CompIds -Indent 12) -join "`r`n"

# Walk Standalone directory
$saCompIds = [System.Collections.Generic.List[string]]::new()
$saXml     = (Build-DirXml -PhysicalPath $stageSa -DirId 'dir_SARoot' -CompIds $saCompIds -Indent 12) -join "`r`n"

$pad16    = ' ' * 16
$vst3Refs = ($vst3CompIds | ForEach-Object { "$pad16<ComponentRef Id=`"$_`" />" }) -join "`r`n"
$saRefs   = ($saCompIds   | ForEach-Object { "$pad16<ComponentRef Id=`"$_`" />" }) -join "`r`n"

$scGuid   = Get-DeterministicGuid 'shortcut:Flopster.Desktop'
$iconProp = if ($hasIco) { "    <Property Id=`"ARPPRODUCTICON`" Value=`"FlopsterIcon`" />`r`n    <Icon Id=`"FlopsterIcon`" SourceFile=`"$icoPath`" />" } else { '' }
$iconAttr = if ($hasIco) { "`r`n$((' ' * 18))Icon=`"FlopsterIcon`" IconIndex=`"0`"" } else { '' }
$generated = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'

$wxsPath    = Join-Path $wixWork 'Flopster.wxs'
$licRtfPath = Join-Path $wixWork 'License.rtf'

# Write License.rtf (WixUI_FeatureTree requires it)
$licRtf = @'
{\rtf1\ansi\deff0
{\fonttbl{\f0 Arial;}}
\f0\fs20
Flopster - Floppy Drive Instrument\line
Copyright (c) Shiru \& Resonaura. All rights reserved.\line
\line
Permission is hereby granted, free of charge, to any person obtaining\line
a copy of this software to use, copy, modify and distribute it.\line
\line
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.\line
}
'@
Set-Content -Path $licRtfPath -Value $licRtf -Encoding ASCII

# Generate WXS content
if ($wixVer -eq 4) {
    $wxsContent = @"
<?xml version="1.0" encoding="UTF-8"?>
<!--
  Flopster.wxs - Auto-generated by win-msi.ps1. Do not edit by hand.
  Product  : Flopster $versionShort
  Vendor   : Shiru and Resonaura
  Generated: $generated
-->
<Wix xmlns="http://wixtoolset.org/schemas/v4/wxs"
     xmlns:ui="http://wixtoolset.org/schemas/v4/wxs/ui">

  <Package Name="Flopster"
           Language="1033"
           Version="$version"
           Manufacturer="Shiru and Resonaura"
           InstallerVersion="500"
           UpgradeCode="$upgradeCode"
           Scope="perMachine"
           Compressed="yes">

    <MajorUpgrade DowngradeErrorMessage="A newer version of Flopster is already installed." />

    <MediaTemplate EmbedCab="yes" />

    <Property Id="WIXUI_INSTALLDIR" Value="INSTALLDIR_SA" />
    <Property Id="ARPHELPLINK"      Value="https://github.com/resonaura/flopster" />
    <Property Id="ARPURLINFOABOUT"  Value="https://github.com/resonaura/flopster" />
$iconProp

    <StandardDirectory Id="CommonFilesFolder">
      <Directory Id="dir_VST3Root" Name="VST3">
        <Directory Id="dir_VST3Bundle" Name="Flopster.vst3">
$vst3Xml
        </Directory>
      </Directory>
    </StandardDirectory>

    <StandardDirectory Id="ProgramFilesFolder">
      <Directory Id="INSTALLDIR_SA" Name="Flopster">
$saXml
      </Directory>
    </StandardDirectory>

    <StandardDirectory Id="DesktopFolder">
      <Component Id="comp_DesktopShortcut" Guid="$scGuid">
        <Shortcut Id="sc_Desktop_Flopster"
                  Name="Flopster"
                  Description="Flopster - floppy drive instrument by Shiru and Resonaura"
                  Target="[INSTALLDIR_SA]Flopster.exe"
                  WorkingDirectory="INSTALLDIR_SA"$iconAttr />
        <RegistryValue Root="HKCU"
                       Key="Software\Shiru\Flopster"
                       Name="DesktopShortcut"
                       Type="integer"
                       Value="1"
                       KeyPath="yes" />
      </Component>
    </StandardDirectory>

    <Feature Id="FeatureAll"
             Title="Flopster"
             Description="Install Flopster audio plugin."
             Level="1"
             ConfigurableDirectory="INSTALLDIR_SA"
             Display="expand"
             Absent="disallow">

      <Feature Id="FeatureVST3"
               Title="VST3 Plugin"
               Description="Installs the VST3 plugin to %CommonProgramFiles%\VST3\ for use in any VST3-compatible DAW."
               Level="1"
               AllowAbsent="yes">
$vst3Refs
      </Feature>

      <Feature Id="FeatureStandalone"
               Title="Standalone Application"
               Description="Installs the standalone Flopster application and a Desktop shortcut."
               Level="1"
               AllowAbsent="yes">
$saRefs
        <ComponentRef Id="comp_DesktopShortcut" />
      </Feature>

    </Feature>

    <ui:WixUI Id="WixUI_FeatureTree" />
    <WixVariable Id="WixUILicenseRtf" Value="License.rtf" />

  </Package>

</Wix>
"@
} else {
    $wxsContent = @"
<?xml version="1.0" encoding="UTF-8"?>
<!--
  Flopster.wxs - Auto-generated by win-msi.ps1. Do not edit by hand.
  Product  : Flopster $versionShort
  Vendor   : Shiru and Resonaura
  Generated: $generated
-->
<Wix xmlns="http://schemas.microsoft.com/wix/2006/wi">

  <Product Id="*"
           Name="Flopster"
           Language="1033"
           Version="$version"
           Manufacturer="Shiru and Resonaura"
           UpgradeCode="$upgradeCode">

    <Package InstallerVersion="500"
             InstallScope="perMachine"
             Compressed="yes" />

    <MajorUpgrade DowngradeErrorMessage="A newer version of Flopster is already installed." />

    <Media Id="1" Cabinet="Flopster.cab" EmbedCab="yes" />

    <Property Id="WIXUI_INSTALLDIR" Value="INSTALLDIR_SA" />
    <Property Id="ARPHELPLINK"      Value="https://github.com/resonaura/flopster" />
    <Property Id="ARPURLINFOABOUT"  Value="https://github.com/resonaura/flopster" />
$iconProp

    <Directory Id="TARGETDIR" Name="SourceDir">

      <Directory Id="CommonFilesFolder">
        <Directory Id="dir_VST3Root" Name="VST3">
          <Directory Id="dir_VST3Bundle" Name="Flopster.vst3">
$vst3Xml
          </Directory>
        </Directory>
      </Directory>

      <Directory Id="ProgramFilesFolder">
        <Directory Id="INSTALLDIR_SA" Name="Flopster">
$saXml
        </Directory>
      </Directory>

      <Directory Id="DesktopFolder" Name="Desktop">
        <Component Id="comp_DesktopShortcut" Guid="$scGuid">
          <Shortcut Id="sc_Desktop_Flopster"
                    Name="Flopster"
                    Description="Flopster - floppy drive instrument by Shiru and Resonaura"
                    Target="[INSTALLDIR_SA]Flopster.exe"
                    WorkingDirectory="INSTALLDIR_SA"$iconAttr />
          <RegistryValue Root="HKCU"
                         Key="Software\Shiru\Flopster"
                         Name="DesktopShortcut"
                         Type="integer"
                         Value="1"
                         KeyPath="yes" />
        </Component>
      </Directory>

    </Directory>

    <Feature Id="FeatureAll"
             Title="Flopster"
             Description="Install Flopster audio plugin."
             Level="1"
             ConfigurableDirectory="INSTALLDIR_SA"
             Display="expand"
             Absent="disallow">

      <Feature Id="FeatureVST3"
               Title="VST3 Plugin"
               Description="Installs the VST3 plugin to %CommonProgramFiles%\VST3\ for use in any VST3-compatible DAW."
               Level="1"
               AllowAbsent="yes">
$vst3Refs
      </Feature>

      <Feature Id="FeatureStandalone"
               Title="Standalone Application"
               Description="Installs the standalone Flopster application and a Desktop shortcut."
               Level="1"
               AllowAbsent="yes">
$saRefs
        <ComponentRef Id="comp_DesktopShortcut" />
      </Feature>

    </Feature>

    <UIRef Id="WixUI_FeatureTree" />
    <WixVariable Id="WixUILicenseRtf" Value="License.rtf" />

  </Product>

</Wix>
"@
}

Set-Content -Path $wxsPath -Value $wxsContent -Encoding UTF8
Write-Host "  [OK]    Flopster.wxs written: $wxsPath"
Write-Host

# ── STEP 6 — Build MSI ───────────────────────────────────────────────────────
Write-Host "[6/6] Building MSI..."
Write-Host "-----------------------------------------------"
Write-Host

if (-not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}
$msiOut = Join-Path $outDir $msiName

if ($wixVer -eq 4) {
    # Ensure UI extension is available
    $extList = & wix extension list 2>$null
    if ($extList -notmatch 'WixToolset.UI') {
        Write-Host "  [INFO]  Adding WiX UI extension..."
        & wix extension add WixToolset.UI.wixext
        if ($LASTEXITCODE -ne 0) {
            Write-Host "  [WARN]  Could not add WixToolset.UI.wixext — UI may be minimal."
        }
    }

    & wix build $wxsPath -ext WixToolset.UI.wixext -b $wixWork -o $msiOut
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  [ERROR] WiX v4 build failed. WiX work dir preserved: $wixWork"
        exit 1
    }
    Write-Host "  [OK]    WiX v4 build complete: $msiOut"

} else {
    $wixObj    = Join-Path $wixWork 'Flopster.wixobj'
    $candleArch = @{ 'x86' = 'x86'; 'arm64' = 'arm64' }[$Arch] ?? 'x64'

    Write-Host "  [INFO]  candle.exe ..."
    & $wix3Candle -nologo -arch $candleArch -ext WixUIExtension -out $wixObj $wxsPath
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  [ERROR] candle.exe failed."
        exit 1
    }
    Write-Host "  [OK]    Compiled: $wixObj"

    Write-Host "  [INFO]  light.exe ..."
    & $wix3Light -nologo -ext WixUIExtension -cultures:en-US -b $wixWork -out $msiOut $wixObj
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  [ERROR] light.exe failed."
        exit 1
    }
    Write-Host "  [OK]    Linked: $msiOut"
}

# ── Final summary ─────────────────────────────────────────────────────────────
Write-Host
Write-Host "  +=======================================================+"
Write-Host "  |   MSI built successfully!                            |"
Write-Host "  +=======================================================+"
Write-Host
Write-Host "   Output : $msiOut"
Write-Host
Write-Host "   Install (interactive):"
Write-Host "     msiexec /i `"$msiOut`""
Write-Host
Write-Host "   Install (silent, all features):"
Write-Host "     msiexec /i `"$msiOut`" /qn ADDLOCAL=FeatureAll"
Write-Host
Write-Host "   Uninstall:"
Write-Host "     msiexec /x `"$msiOut`""
Write-Host "     or:  Control Panel -> Programs -> Uninstall Flopster"
Write-Host
