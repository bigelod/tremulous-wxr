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

# Compare against the NEWEST build artifact, not against tremulous.exe alone.
#
# sdl_glimp.c and the renderer sources only feed renderer_opengl*.dll, so a
# renderer-only edit legitimately leaves the client exe untouched and an
# exe-only check cries wolf. A guard that fires when nothing is wrong gets
# ignored, and then it is not a guard. The question that actually matters is
# "did a build run since the last source edit?", which the newest artifact
# answers.
$artifacts = @(
    Get-ChildItem $StageSrc -Filter *.exe -ErrorAction SilentlyContinue
    Get-ChildItem $StageSrc -Filter *.dll -ErrorAction SilentlyContinue
    Get-ChildItem (Join-Path $StageSrc 'base') -Filter *.dll -ErrorAction SilentlyContinue
) | Sort-Object LastWriteTime -Descending

$newestArtifact = $artifacts | Select-Object -First 1
$binaryTime = $newestArtifact.LastWriteTime

if ($newestSource -and $newestSource.LastWriteTime -gt $binaryTime) {
    Write-Host ''
    Write-Host 'REFUSING TO STAGE: every binary is older than the newest source.' -ForegroundColor Red
    Write-Host "  newest binary   $binaryTime  $($newestArtifact.Name)"
    Write-Host "  newest source   $($newestSource.LastWriteTime)  $($newestSource.FullName)"
    Write-Host ''
    Write-Host 'Build again, or re-run without -SkipBuild.' -ForegroundColor Yellow
    exit 1
}

Write-Host "Build is newer than all sources (newest artifact $($newestArtifact.Name), $binaryTime)." -ForegroundColor Green

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

# Loose game assets and loose QVMs. While the client is NOT pure the base/
# directory sits at the head of the search path, so these win over the stale
# copies inside data-1.1.0.pk3, and editing one is visible on the next run
# without repacking anything. That is worth keeping for iteration.
#
# But they are only half the story -- see the pk3 built below.
Copy-Item (Join-Path $RepoRoot 'assets/*') -Destination "$Dest/base" -Recurse -Force
Write-Host 'Staged loose assets/ over base/.' -ForegroundColor DarkGray

# ------------------------------------------------------- the pk3 that matters
#
# Everything staged above is INVISIBLE to a pure client, and hosting a game
# makes the client pure against its own server. That one fact is what crashed
# AlphaTest5 with "VM_Create on UI failed" and broke its UI at the same time.
#
# The full explanation, and the packing itself, live in make_wxr_pk3.ps1 --
# one copy, so a folder staged by hand can be repaired with the same script:
#
#     ./scripts/make_wxr_pk3.ps1 -Target Release/AlphaTest5
#
# It is deliberately run AFTER the loose staging above, because it packs the
# target folder's own base/ subdirectories.

$pk3Name = 'zzz-wxr.pk3'

& (Join-Path $PSScriptRoot 'make_wxr_pk3.ps1') -Target $Dest -Pk3Name $pk3Name
if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) { throw 'Packing the pk3 failed.' }

$staged.Add((Join-Path "$Dest/base" $pk3Name))

# ------------------------------------------------------------------- autoexec
#
# Com_ExecuteCfg execs base/autoexec.cfg, and it was never staged -- the
# device log said "couldn't exec autoexec.cfg". Every default this file was
# supposed to carry therefore never applied on the headset, including
# AlphaTest4's bytecode-VM experiment (autogen.cfg still had vm_* "2", the
# JIT) and AlphaTest5's `logfile 2`, which is why no qconsole.log ever came
# back. A config that is written but not shipped is worse than no config:
# it makes a test look conducted when it was not.

$autoexec = Join-Path $RepoRoot 'scripts/autoexec_alphatest.cfg'
if (Test-Path $autoexec) {
    Copy-Item $autoexec -Destination "$Dest/base/autoexec.cfg" -Force
    $staged.Add((Join-Path "$Dest/base" 'autoexec.cfg'))
    Write-Host 'Staged scripts/autoexec_alphatest.cfg -> base/autoexec.cfg.' -ForegroundColor Green
} else {
    Write-Warning "No autoexec at $autoexec (device will print 'couldn't exec autoexec.cfg')"
}

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
Write-Host '  * vr_enable defaults to 0; base/autoexec.cfg now sets it to 1 on the device.'
Write-Host '  * A q3config.cfg/autogen.cfg already on the device overrides changed defaults.'
Write-Host "  * $pk3Name carries assets/ AND the QVMs. Hosting a game makes the client"
Write-Host '    pure, and a pure client can ONLY read files inside a pk3. If you add'
Write-Host '    content and it works flat but vanishes when you host, it is this.'
Write-Host '  * vr.log is written next to the executable; line 1 is the build stamp.'
