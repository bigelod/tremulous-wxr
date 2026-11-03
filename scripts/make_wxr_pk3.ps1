<#
.SYNOPSIS
    Pack a deployed game folder's loose content into base/zzz-wxr.pk3.

.DESCRIPTION
    **This is the difference between a build that hosts a game and one that
    crashes.** Run it on any deployed folder, not just the deploy target.

    `sv_pure` defaults to 1 (code/server/sv_init.c), so the instant the game
    hosts a local server the client goes pure against its OWN server. A pure
    client can only read files that live inside a pk3 -- FS_FindVM says so
    outright, in code/qcommon/files.c:

        if(search->dir && !fs_numServerPaks)

    Every directory search path, base/ included, is skipped whole. Shipping
    our 1.2 content as loose files therefore fails in two ways at once:

      * base/vm/*.qvm goes unfindable, FS_FindVM returns -1, VM_Create
        returns NULL, and CL_InitUI raises
            Com_Error( ERR_FATAL, "VM_Create on UI failed" )
        That is the hard crash on menu -> Create Server -> level select. It
        happens on the SECOND UI VM creation, because hosting clears the hunk
        and rebuilds it -- which is why the menus work and only hosting dies.

      * The loose 1.2 ui/*.menu files go unfindable too, so our 1.2 ui.qvm
        falls back to the 1.1.0 menus inside data-1.1.0.pk3 and cannot parse
        them. Measured: 0 parse errors when not pure, 401 when pure. Same
        root cause, which is why fixing one fixes both.

    The pk3 is named to sort LAST on purpose. FS_AddGameDirectory adds pk3s
    in ascending order and prepends each to the search path, so the
    alphabetically last pk3 is searched FIRST and wins. The engine's own
    FS_Startup listing confirms the ordering -- map-uncreation first,
    data-1.1.0 last. "zzz-" guarantees we outrank the 1.1.0 data.

    Content is taken from the TARGET FOLDER's own base/ subdirectories, not
    from the repository, so the pk3 always contains exactly what is deployed
    beside it and the two cannot disagree.

    Loose copies are left in place. They cost nothing, they win while the
    client is not pure, and they keep asset iteration a file copy rather than
    a repack.

    This does NOT affect joining real servers: a pure server forces its own
    pk3s and QVMs onto us regardless of what we ship.

.PARAMETER Target
    The deployed game folder -- the one holding tremulous.exe and base/.

.EXAMPLE
    ./scripts/make_wxr_pk3.ps1 -Target Release/AlphaTest5
    ./scripts/make_wxr_pk3.ps1 -Target D:/VR/tremulous-wxr
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $Target,

    [string] $Pk3Name = 'zzz-wxr.pk3'
)

$ErrorActionPreference = 'Stop'

$baseDir = Join-Path $Target 'base'
if (-not (Test-Path $baseDir)) {
    throw "No base/ folder under $Target -- is that a deployed game folder?"
}

$pk3Path = Join-Path $baseDir $Pk3Name

Add-Type -AssemblyName System.IO.Compression.FileSystem

# Every directory under base/ is content: armour, configs, models, scripts,
# sound, ui, vm. Root-level loose FILES are deliberately excluded -- the dlls
# are not game content, and autoexec.cfg is exec'd by Com_ExecuteCfg before
# the client ever goes pure, so it must stay readable as a loose file.
$contentDirs = Get-ChildItem $baseDir -Directory | Sort-Object Name

if (-not $contentDirs) {
    throw "No content directories under $baseDir -- nothing to pack."
}

$vmDir = Join-Path $baseDir 'vm'
if (-not (Test-Path $vmDir)) {
    Write-Warning "No base/vm -- the pk3 will carry no QVMs, and hosting will still fail."
} else {
    $qvms = Get-ChildItem $vmDir -Filter *.qvm -ErrorAction SilentlyContinue
    if (-not $qvms) {
        Write-Warning "base/vm holds no .qvm files -- hosting will still fail."
    }
}

if (Test-Path $pk3Path) { Remove-Item $pk3Path -Force }

$entries = 0
$baseFull = (Resolve-Path $baseDir).Path.TrimEnd('\', '/')
$rootLen = $baseFull.Length + 1

# Entry names are written explicitly with forward slashes. CreateFromDirectory
# has used '\' in some .NET Framework versions, and a pk3 whose entry names
# contain backslashes is silently unreadable by the engine -- a failure that
# looks exactly like a missing file.
$zip = [System.IO.Compression.ZipFile]::Open($pk3Path, 'Create')
try {
    foreach ($dir in $contentDirs) {
        foreach ($file in Get-ChildItem $dir.FullName -Recurse -File) {
            $entry = $file.FullName.Substring($rootLen).Replace('\', '/')
            [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $zip, $file.FullName, $entry,
                [System.IO.Compression.CompressionLevel]::Optimal)
            $entries++
        }
    }
}
finally {
    $zip.Dispose()
}

$size = [math]::Round((Get-Item $pk3Path).Length / 1KB)
Write-Host ("Built {0} -- {1} entries, {2} KB" -f $pk3Path, $entries, $size) -ForegroundColor Green
Write-Host ('  packed: ' + (($contentDirs | ForEach-Object { $_.Name }) -join ', ')) -ForegroundColor DarkGray
Write-Host '  survives sv_pure 1, so hosting a local game now works.' -ForegroundColor DarkGray
