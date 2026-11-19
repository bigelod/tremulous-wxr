<#
.SYNOPSIS
    Build once and stage every standard target.

.DESCRIPTION
    Three folders, one build:

      E:\tmp\Q3Mods\trem-wxr        the folder copied to the Quest
      D:\VR\tremulous-wxr           the PC test install
      Release\DeployReady           a clean full build, ready to zip or copy

    DeployReady is deleted and rebuilt on every run, so it never accumulates
    screenshots, saved configs or a stale pk3. It carries the executable, the
    renderer DLLs, the QVMs, the game data pk3s, zzz-wxr.pk3, autoexec.cfg,
    ReadMe.txt, GPL and COPYING.txt -- everything needed to run without the
    repository.

.EXAMPLE
    ./scripts/deploy_all.ps1
    ./scripts/deploy_all.ps1 -SkipBuild
#>

[CmdletBinding()]
param(
    [switch] $SkipBuild
)

$ErrorActionPreference = 'Stop'

$QuestDest  = 'E:/tmp/Q3Mods/trem-wxr'
$PcDest     = 'D:/VR/tremulous-wxr'
$RepoRoot   = Split-Path -Parent $PSScriptRoot
$ReadyDest  = Join-Path $RepoRoot 'Release/DeployReady'

function Assert-Staged([string] $Dest) {
    if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        throw "Staging failed for $Dest"
    }
}

# Build on the first call, stage without rebuilding on the rest.
if ($SkipBuild) {
    & (Join-Path $PSScriptRoot 'deploy_wxr.ps1') -Dest $QuestDest -SkipBuild
} else {
    & (Join-Path $PSScriptRoot 'deploy_wxr.ps1') -Dest $QuestDest
}
Assert-Staged $QuestDest

& (Join-Path $PSScriptRoot 'deploy_wxr.ps1') -Dest $PcDest -SkipBuild
Assert-Staged $PcDest

# A clean folder, not an accumulating one. The deploy script packs zzz-wxr.pk3
# from the target's own base/, and a used folder's base/ holds screenshots and
# downloaded content that have no business in a release.
if (Test-Path $ReadyDest) {
    Remove-Item $ReadyDest -Recurse -Force
}

& (Join-Path $PSScriptRoot 'deploy_wxr.ps1') -Dest $ReadyDest -SkipBuild
Assert-Staged $ReadyDest

Write-Host ''
Write-Host "DeployReady is a clean full build: $ReadyDest" -ForegroundColor Cyan
Write-Host 'Zip that folder, or copy it to a headset as-is.' -ForegroundColor Cyan
