param([string] $GameRoot)

$ErrorActionPreference = 'Stop'
$gameFolder = 'steamapps/common/FINAL FANTASY VII REBIRTH'
$requiredFiles = @(foreach ($group in 3, 8) {
    "End/Content/Paks/pakchunk$group-WindowsNoEditor.utoc"
    "End/Content/Paks/pakchunk$group-WindowsNoEditor.ucas"
})

function Find-MissingGameFile([string] $Root) {
    foreach ($file in $requiredFiles) {
        if (![IO.File]::Exists([IO.Path]::Combine($Root, $file))) { return $file }
    }
}

function Find-SteamGame {
    $steam = (Get-ItemProperty -Path 'HKCU:\Software\Valve\Steam' -Name SteamPath -ErrorAction SilentlyContinue).SteamPath
    if (!$steam) { return }
    $libraries = @($steam)
    # Steam keeps stale library drives. Do not resolve them through PowerShell's drive provider.
    $vdf = [IO.Path]::Combine($steam, 'steamapps/libraryfolders.vdf')
    if ([IO.File]::Exists($vdf)) {
        try {
            foreach ($entry in [regex]::Matches([IO.File]::ReadAllText($vdf), '(?m)^\s*"path"\s*"([^"]+)"')) {
                $libraries += $entry.Groups[1].Value.Replace('\\', '\')
            }
        } catch {
            Write-Warning 'Could not read the Steam library list. You can enter the game folder manually.'
        }
    }
    foreach ($library in ($libraries | Select-Object -Unique)) {
        if (![IO.Directory]::Exists($library)) { continue }
        $candidate = [IO.Path]::GetFullPath([IO.Path]::Combine($library, $gameFolder))
        if (!(Find-MissingGameFile $candidate)) { $candidate }
    }
}

# The stock sidecar inputs used by src/pipeline/native_audio_cache.cpp.
$chunks = @(
    @{ File = 'Music.zen.bin';      Group = 8; Id = '80bd4180ba7f81e100000002' }
    @{ File = 'BGMList.zen.bin';    Group = 3; Id = '7a8041d31d691a3d00000002' }
    @{ File = 'ScoreInfo.zen.bin';  Group = 3; Id = '773d97220ebde22600000002' }
    @{ File = 'PianoScore.zen.bin'; Group = 3; Id = 'ed313fea776e3add00000002' }
)

$id = [guid]::NewGuid().ToString('N')
$work = Join-Path ([IO.Path]::GetTempPath()) "ff7rp-carriers-$id"
try {
    if (![Environment]::Is64BitOperatingSystem) { throw 'This tool needs 64-bit Windows 10 or 11.' }
    if (!$GameRoot) {
        $games = @(Find-SteamGame | Sort-Object -Unique)
        if ($games.Count -eq 1) {
            $GameRoot = $games[0]
        } elseif ($games.Count -gt 1) {
            Write-Host "Multiple game copies found. Use the one that produced the log:`n$($games -join "`n")"
        } else {
            Write-Host 'No complete game installation found in the Steam libraries.'
        }
    }
    if (!$GameRoot) { $GameRoot = Read-Host 'Paste the game folder (the one containing End)' }
    if (!$GameRoot) { throw 'No game folder supplied.' }
    $GameRoot = $GameRoot.Trim().Trim('"')
    if (![IO.Directory]::Exists($GameRoot)) { throw "Game folder not found or inaccessible: $GameRoot" }
    $GameRoot = [IO.Path]::GetFullPath($GameRoot)
    $missing = Find-MissingGameFile $GameRoot
    if ($missing) { throw "Missing or inaccessible game file: $missing (in $GameRoot)" }
    $paks = [IO.Path]::Combine($GameRoot, 'End/Content/Paks')
    Write-Host "Reading from: $GameRoot"

    $output = Join-Path $work 'chunks'
    [void][IO.Directory]::CreateDirectory($output)
    foreach ($name in 'retoc.exe', 'oo2core_9_win64.dll', 'vcruntime140.dll') {
        $toolFile = Join-Path $PSScriptRoot $name
        if (![IO.File]::Exists($toolFile)) { throw "Missing $name. Extract the entire tool ZIP before running Collect.cmd." }
        Copy-Item -LiteralPath $toolFile -Destination $work
    }
    $retoc = Join-Path $work 'retoc.exe'
    foreach ($chunk in $chunks) {
        $toc = Join-Path $paks "pakchunk$($chunk.Group)-WindowsNoEditor.utoc"
        $target = Join-Path $output $chunk.File
        Write-Host "Extracting $($chunk.File)..."
        & $retoc get $toc $chunk.Id $target
        if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $target -PathType Leaf) -or
            (Get-Item -LiteralPath $target).Length -eq 0) {
            throw "Could not extract $($chunk.File) (retoc exit code: $LASTEXITCODE)."
        }
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $stagedZip = Join-Path $work 'carriers.zip'
    [IO.Compression.ZipFile]::CreateFromDirectory($output, $stagedZip)
    $archive = Join-Path $PSScriptRoot "FF7RPianoSongs-carriers-$($id.Substring(0,8)).zip"
    Move-Item -LiteralPath $stagedZip -Destination $archive
    Write-Host "Done. Send this file: $archive"
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
} finally {
    if (Test-Path -LiteralPath $work) {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }
}
