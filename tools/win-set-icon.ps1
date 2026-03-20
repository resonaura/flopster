#Requires -Version 5.0
<#
.SYNOPSIS
    Embeds a .ico file into a Windows EXE by replacing its RT_GROUP_ICON / RT_ICON
    resources via the Win32 UpdateResource API.

    This is used as a CMake POST_BUILD step for Flopster_Standalone to replace the
    low-quality icon produced by juceaide (PNG -> ICO conversion) with our pre-built
    app.ico that contains all required resolutions.

.PARAMETER ExePath
    Path to the target EXE to update.

.PARAMETER IcoPath
    Path to the .ico file to embed.
#>
param(
    [Parameter(Mandatory)][string]$ExePath,
    [Parameter(Mandatory)][string]$IcoPath
)

$ErrorActionPreference = 'Stop'

# ── Win32 UpdateResource API ──────────────────────────────────────────────────
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class ResUpdater {
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    public static extern IntPtr BeginUpdateResource(string lpFileName, bool bDeleteExistingResources);

    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool UpdateResource(IntPtr hUpdate, IntPtr lpType, IntPtr lpName,
                                             ushort wLanguage, byte[] lpData, uint cb);

    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool EndUpdateResource(IntPtr hUpdate, bool fDiscard);

    public static readonly IntPtr RT_ICON       = (IntPtr)3;
    public static readonly IntPtr RT_GROUP_ICON = (IntPtr)14;
}
'@

# ── Parse ICO file ────────────────────────────────────────────────────────────
# ICO layout:
#   ICONDIR        6 bytes  (reserved, type=1, count)
#   ICONDIRENTRY  16 bytes  x count  (w, h, cc, reserved, planes, bitCount, bytesInRes, imageOffset)
#   <image data>

$icoData = [System.IO.File]::ReadAllBytes($IcoPath)
$count   = [BitConverter]::ToUInt16($icoData, 4)

$entries = @()
$baseId  = 100   # Start RT_ICON resource IDs here to avoid collision with JUCE's IDs (1..N)

for ($i = 0; $i -lt $count; $i++) {
    $o      = 6 + $i * 16
    $w      = $icoData[$o]
    $h      = $icoData[$o + 1]
    $cc     = $icoData[$o + 2]
    $planes = [BitConverter]::ToUInt16($icoData, $o + 4)
    $bits   = [BitConverter]::ToUInt16($icoData, $o + 6)
    $bytes  = [BitConverter]::ToUInt32($icoData, $o + 8)
    $imgOff = [BitConverter]::ToUInt32($icoData, $o + 12)

    $imgData = New-Object byte[] $bytes
    [Array]::Copy($icoData, [int]$imgOff, $imgData, 0, [int]$bytes)

    $entries += [pscustomobject]@{
        W = $w; H = $h; CC = $cc
        Planes = $planes; Bits = $bits
        Data = $imgData
        Id   = $baseId + $i
    }
}

# ── Build GRPICONDIR (RT_GROUP_ICON payload) ──────────────────────────────────
# GRPICONDIR     6 bytes  (reserved, type=1, count)
# GRPICONDIRENTRY 14 bytes x count  (w, h, cc, reserved, planes, bitCount, bytesInRes, nId)
$grp = New-Object byte[] (6 + $count * 14)
$grp[2] = 1   # type = icon
[BitConverter]::GetBytes([uint16]$count).CopyTo($grp, 4)

for ($i = 0; $i -lt $count; $i++) {
    $e = $entries[$i]
    $o = 6 + $i * 14
    $grp[$o]     = $e.W
    $grp[$o + 1] = $e.H
    $grp[$o + 2] = $e.CC
    $grp[$o + 3] = 0  # reserved
    [BitConverter]::GetBytes([uint16]$e.Planes).CopyTo($grp, $o + 4)
    [BitConverter]::GetBytes([uint16]$e.Bits).CopyTo($grp, $o + 6)
    [BitConverter]::GetBytes([uint32]$e.Data.Length).CopyTo($grp, $o + 8)
    [BitConverter]::GetBytes([uint16]$e.Id).CopyTo($grp, $o + 12)
}

# ── Write resources into EXE ──────────────────────────────────────────────────
$handle = [ResUpdater]::BeginUpdateResource($ExePath, $false)
if ($handle -eq [IntPtr]::Zero) {
    $err = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
    throw "BeginUpdateResource failed (error $err): $ExePath"
}

# Write individual icon images as RT_ICON entries (IDs 100, 101, ...)
foreach ($e in $entries) {
    [ResUpdater]::UpdateResource($handle, [ResUpdater]::RT_ICON,
        [IntPtr]$e.Id, 0x0409, $e.Data, [uint32]$e.Data.Length) | Out-Null
}

# Write the icon group as RT_GROUP_ICON ID 1 — this replaces JUCE's group entry
[ResUpdater]::UpdateResource($handle, [ResUpdater]::RT_GROUP_ICON,
    [IntPtr]1, 0x0409, $grp, [uint32]$grp.Length) | Out-Null

if (-not [ResUpdater]::EndUpdateResource($handle, $false)) {
    $err = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
    throw "EndUpdateResource failed (error $err)"
}

Write-Host "  [OK]    Icon embedded into $([System.IO.Path]::GetFileName($ExePath)) ($count image(s))"
