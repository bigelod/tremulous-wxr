<#
.SYNOPSIS
    Build Tremulous WXR x64 Release and stage it to the deploy folder.

.DESCRIPTION
    One command between a source edit and a folder the owner can copy to the
    headset. Three things it does that matter:

      * Pins CMake. The `cmake` on PATH may be an old one that cannot even
        parse this project (3.8 was found on the owner's box; the project
        needs 3.25+). We use the copy Visual Studio ships.

      * Refuses to stage a binary older than the newest source file. "Am I
        running the binary I just built?" is answered permanently, from the
        start, rather than after a day lost to it.

      * Prints a SHA-256 per staged file, so what landed on the headset can
        be confirmed against what was built.

.PARAMETER GameData
    Folder holding the Tremulous base/ pk3 set. Staged once; skipped on later
    runs since the pk3s do not change and they are ~100 MB.

.PARAMETER SkipBuild
    Stage whatever is already in build/Release without rebuilding.

.EXAMPLE
    ./scripts/deploy_wxr.ps1
    ./scripts/deploy_wxr.ps1 -SkipBuild
#>

[CmdletBinding()]
param(
    [string] $Dest     = 'D:/VR/tremulous-wxr',
    [string] $GameData = 'E:/tmp/Q3Mods/Tremulous/base',
    [switch] $SkipBuild
)

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $RepoRoot 'build'
$StageSrc = Join-Path $BuildDir 'Release'

# ---------------------------------------------------------------- find cmake

function Resolve-CMake {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -all -prerelease -property installationPath
        if ($vsPath) {
            $candidate = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
            if (Test-Path $candidate) { return $candidate }
        }
    }

    # Fall back to PATH, but only if it is new enough to be usable.
    $onPath = Get-Command cmake -ErrorAction SilentlyContinue
    if ($onPath) {
        $versionLine = & $onPath.Source --version | Select-Object -First 1
        if ($versionLine -match '(\d+)\.(\d+)') {
            $major = [int]$Matches[1]
            $minor = [int]$Matches[2]
            if (($major -gt 3) -or ($major -eq 3 -and $minor -ge 25)) {
                return $onPath.Source
            }
            Write-Warning "Ignoring cmake on PATH: $versionLine is below the required 3.25."
        }
    }

    throw 'No usable CMake found (need 3.25+). Install Visual Studio 2022 C++ tools or a modern CMake.'
}

# --------------------------------------------------------------------- build

if (-not $SkipBuild) {
    $cmake = Resolve-CMake
    Write-Host "CMake:  $cmake" -ForegroundColor DarkGray

    & $cmake -S $RepoRoot -B $BuildDir -G 'Visual Studio 17 2022' -A x64
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }

    & $cmake --build $BuildDir --config Release --parallel
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
}

$clientExe = Join-Path $StageSrc 'tremulous.exe'
if (-not (Test-Path $clientExe)) {
    throw "No client binary at $clientExe. Build first, or drop -SkipBuild."
}

# ----------------------------------------------------------- freshness guard

$newestSource = Get-ChildItem (Join-Path $RepoRoot 'code') -Recurse -Include *.c, *.h, *.cpp |
                Sort-Object LastWriteTime -Descending |
                Select-Object -First 1

$binaryTime = (Get-Item $clientExe).LastWriteTime

if ($newestSource -and $newestSource.LastWriteTime -gt $binaryTime) {
    Write-Host ''
    Write-Host 'REFUSING TO STAGE: the binary is older than the source.' -ForegroundColor Red
    Write-Host "  tremulous.exe   $binaryTime"
    Write-Host "  newest source   $($newestSource.LastWriteTime)  $($newestSource.FullName)"
    Write-Host ''
    Write-Host 'Build again, or re-run without -SkipBuild.' -ForegroundColor Yellow
    exit 1
}

Write-Host "Binary is newer than all sources (built $binaryTime)." -ForegroundColor Green

# --------------------------------------------------------------------- stage

New-Item -ItemType Directory -Force -Path $Dest           | Out-Null
New-Item -ItemType Directory -Force -Path "$Dest/base"    | Out-Null
New-Item -ItemType Directory -Force -Path "$Dest/base/vm" | Out-Null

$staged = [System.Collections.Generic.List[string]]::new()

function Stage-File([string] $Source, [string] $TargetDir) {
    Copy-Item $Source -Destination $TargetDir -Force
    $staged.Add((Join-Path $TargetDir (Split-Path $Source -Leaf)))
}

foreach ($name in 'tremulous.exe', 'tremded.exe', 'SDL2.dll',
                  'renderer_opengl1.dll', 'renderer_opengl2.dll') {
    $path = Join-Path $StageSrc $name
    if (Test-Path $path) { Stage-File $path $Dest }
    else { Write-Warning "missing from build output: $name" }
}

foreach ($name in 'cgame.dll', 'game.dll', 'ui.dll') {
    $path = Join-Path $StageSrc "base/$name"
    if (Test-Path $path) { Stage-File $path "$Dest/base" }
}

foreach ($qvm in Get-ChildItem (Join-Path $StageSrc 'base/vm') -Filter *.qvm -ErrorAction SilentlyContinue) {
    Stage-File $qvm.FullName "$Dest/base/vm"
}

# Loose game assets. These sit at the head of the engine's search path and so
# override the 1.1.0 pk3s -- which is the point: they are the 1.2 successor to
# data-gpp1.pk3, and they must win over the stale copies inside data-1.1.0.pk3.
Copy-Item (Join-Path $RepoRoot 'assets/*') -Destination "$Dest/base" -Recurse -Force
Write-Host 'Staged loose assets/ over base/.' -ForegroundColor DarkGray

# Game data. Big and unchanging, so only once.
if (Test-Path $GameData) {
    foreach ($pk3 in Get-ChildItem $GameData -Filter *.pk3) {
        # vms-1.1.0.pk3 holds 1.1.0's QVMs. Ours supersede it and keeping it
        # is a free variable in a debugging session.
        if ($pk3.Name -eq 'vms-1.1.0.pk3') { continue }

        $target = Join-Path "$Dest/base" $pk3.Name
        if (-not (Test-Path $target)) {
            Write-Host "  + $($pk3.Name)" -ForegroundColor DarkGray
            Copy-Item $pk3.FullName -Destination "$Dest/base" -Force
        }
    }
} else {
    Write-Warning "Game data folder not found: $GameData (pk3s not staged)"
}

# ------------------------------------------------------------------ manifest

Write-Host ''
Write-Host "Staged to $Dest" -ForegroundColor Cyan
Write-Host ''

foreach ($file in $staged) {
    $hash = (Get-FileHash $file -Algorithm SHA256).Hash.Substring(0, 16)
    $rel  = $file.Replace((Resolve-Path $Dest).Path, '').TrimStart('\', '/')
    Write-Host ("  {0}  {1}" -f $hash, $rel)
}

Write-Host ''
Write-Host 'Reminders:' -ForegroundColor Yellow
Write-Host '  * vr_enable defaults to 0. Set it to 1 to start the XrApi client.'
Write-Host '  * A q3config.cfg already on the test device overrides changed defaults.'
Write-Host '  * vr.log is written next to the executable; line 1 is the build stamp.'
