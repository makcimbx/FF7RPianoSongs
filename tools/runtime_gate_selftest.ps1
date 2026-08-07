param(
    [string]$RuntimeGateScript = (Join-Path $PSScriptRoot "runtime_gate.ps1")
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Assert-True([bool]$Condition, [string]$Message) {
    if (!$Condition) {
        throw "runtime_gate_selftest: $Message"
    }
}

function Get-TestHash([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-TestTextHash([string]$Text) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash(
            [Text.Encoding]::UTF8.GetBytes($Text)))).Replace("-", "").ToLowerInvariant()
    }
    finally { $sha.Dispose() }
}

function Set-U32([byte[]]$Bytes, [int]$Offset, [uint32]$Value) {
    [BitConverter]::GetBytes($Value).CopyTo($Bytes, $Offset)
}

function New-FakePe([string]$Path) {
    [byte[]]$bytes = New-Object byte[] 1024
    $bytes[0] = 0x4d
    $bytes[1] = 0x5a
    Set-U32 $bytes 0x3c 0x80
    $bytes[0x80] = 0x50
    $bytes[0x81] = 0x45
    Set-U32 $bytes 0x88 0x6a16ced2
    $optional = 0x80 + 24
    Set-U32 $bytes ($optional + 56) 0x099d9000
    Set-U32 $bytes ($optional + 64) 0x0769ea6e
    [System.IO.File]::WriteAllBytes($Path, $bytes)
}

function New-Fixture([string]$Root) {
    $repo = Join-Path $Root "repo"
    $game = Join-Path $Root "game"
    $state = Join-Path $Root "state"
    $win64 = Join-Path $game "End/Binaries/Win64"
    $packageWin64 = Join-Path $repo "package/End/Binaries/Win64"
    foreach ($directory in @(
        $win64,
        $packageWin64,
        (Join-Path $repo "build/bin/Release"),
        (Join-Path $repo "dist"),
        (Join-Path $repo "build"),
        (Join-Path $repo "src/game"),
        (Join-Path $repo "tools"),
        (Join-Path $win64 "Music/test-song/.cache"),
        (Join-Path $win64 "Music/other-song/.cache"),
        (Join-Path $win64 "Scores")
    )) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }

    $oldAsi = Join-Path $win64 "FF7RPianoSongs.asi"
    $newAsi = Join-Path $packageWin64 "FF7RPianoSongs.asi"
    [System.IO.File]::WriteAllBytes($oldAsi, [Text.Encoding]::UTF8.GetBytes("old-installed-asi"))
    [System.IO.File]::WriteAllBytes($newAsi, [Text.Encoding]::UTF8.GetBytes("reviewed-release-asi"))
    Copy-Item -LiteralPath $newAsi -Destination (Join-Path $repo "build/bin/Release/FF7RPianoSongs.dll")
    Copy-Item -LiteralPath $newAsi -Destination (Join-Path $repo "dist/FF7RPianoSongs.asi")

    $ini = Join-Path $win64 "FF7RPianoSongs.ini"
    $song = Join-Path $win64 "Music/test-song/song.json"
    $sidecar = Join-Path $win64 "Music/test-song/.cache/song.mabf.bin"
    Set-Content -LiteralPath $ini -Value "ExtendedCharts=1" -Encoding utf8 -NoNewline
    Set-Content -LiteralPath $song -Value '{"id":"test-song"}' -Encoding utf8 -NoNewline
    [System.IO.File]::WriteAllBytes($sidecar, [Text.Encoding]::UTF8.GetBytes("sidecar-sentinel"))
    Set-Content -LiteralPath (Join-Path $win64 "FF7RPianoSongs.log") -Value "historical-log" -Encoding utf8 -NoNewline

    $additionalSentinels = [ordered]@{
        "Music/test-song/chart.json" = '{"notes":[1,2,3]}'
        "Music/test-song/.cache/chart.bin" = "primary-chart-cache"
        "Music/other-song/song.json" = '{"id":"other-song"}'
        "Music/other-song/source.mid" = "other-midi-source"
        "Music/other-song/.cache/song.mabf.bin" = "other-sidecar-cache"
        "Music/other-song/.cache/chart.bin" = "other-chart-cache"
        "Scores/piano-scores.json" = '{"test-song":12345}'
        "FF7RPianoSongs.custom_piano_scores.ini" = "[Scores]`ntest-song=12345"
    }
    foreach ($sentinel in $additionalSentinels.GetEnumerator()) {
        Set-Content -LiteralPath (Join-Path $win64 $sentinel.Key) `
            -Value $sentinel.Value -Encoding utf8 -NoNewline
    }
    $sentinelRecords = @($additionalSentinels.Keys | ForEach-Object {
        $path = Join-Path $win64 $_
        [pscustomobject]@{ path = $path; sha256 = Get-TestHash $path }
    })

    $newHash = Get-TestHash $newAsi

    $exe = Join-Path $win64 "ff7rebirth_.exe"
    New-FakePe $exe
    $exeBytes = [System.IO.File]::ReadAllBytes($exe)
    $catalog = [ordered]@{
        schema_version = 3
        default_build = "fixture-build"
        builds = @(
            [ordered]@{
                id = "fixture-build"
                pe_timestamp = "0x6a16ced2"
                size_of_image = "0x099d9000"
                pe_checksum = "0x0769ea6e"
                file_size = $exeBytes.Length
                sha256 = Get-TestHash $exe
            },
            [ordered]@{
                id = "fixture-other-build"
                pe_timestamp = "0x68fd6fde"
                size_of_image = "0x09800000"
                pe_checksum = "0x01020304"
                file_size = $exeBytes.Length + 4096
                sha256 = "0" * 64
            }
        )
        addresses = @()
    }
    $catalog | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (
        Join-Path $repo "src/game/rva_catalog.json") -Encoding utf8

    New-Item -ItemType Directory -Force -Path (Join-Path $repo "cmake"), (Join-Path $repo "build/generated") | Out-Null
    foreach ($inputContent in ([ordered]@{
        "CMakeLists.txt" = "fixture-cmake"
        "build.ps1" = "fixture-build"
        "release.json" = "{}"
        "cmake/release_identity.generated.h.in" = "fixture-template"
        "tools/generate_rva_catalog.py" = "fixture-generator"
        "build/CMakeCache.txt" = "fixture-cache"
        "build/generated/release_identity.generated.h" = "fixture-generated-release"
    }).GetEnumerator()) {
        Set-Content -LiteralPath (Join-Path $repo $inputContent.Key) `
            -Value $inputContent.Value -Encoding utf8 -NoNewline
    }
    $inputPaths = @(
        "CMakeLists.txt",
        "build.ps1",
        "cmake/release_identity.generated.h.in",
        "release.json",
        "src/game/rva_catalog.json",
        "tools/generate_rva_catalog.py") | Sort-Object
    $inputRecords = @($inputPaths | ForEach-Object {
        [ordered]@{ path = $_; sha256 = Get-TestHash (Join-Path $repo $_) }
    })
    $inputIdentity = ($inputRecords | ForEach-Object { "$($_.path)`t$($_.sha256)`n" }) -join ""
    $compiler = (Get-Command pwsh -ErrorAction Stop).Source
    $cmake = (Get-Command cmake -ErrorAction Stop).Source
    $provenance = [ordered]@{
        schema = "ff7rpianosongs.build-provenance.v3"
        configuration = "Release"
        dll = [ordered]@{ path = "bin/Release/FF7RPianoSongs.dll"; sha256 = $newHash }
        toolchain = [ordered]@{
            visualStudioInstallationPath = $Root
            compilerPath = $compiler
            compilerVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($compiler).FileVersion
            compilerSha256 = Get-TestHash $compiler
            cmakePath = $cmake
            cmakeVersion = (& $cmake --version | Select-Object -First 1)
            cmakeSha256 = Get-TestHash $cmake
            generator = "fixture"
            generatorPlatform = "fixture"
            generatorToolset = "fixture"
        }
        cmake = [ordered]@{
            cachePath = "CMakeCache.txt"
            cacheSha256 = Get-TestHash (Join-Path $repo "build/CMakeCache.txt")
        }
        releaseIdentity = [ordered]@{
            authorityPath = "release.json"
            authoritySha256 = Get-TestHash (Join-Path $repo "release.json")
            catalogId = "fixture-build"
            generatedHeaderPath = "generated/release_identity.generated.h"
            generatedHeaderSha256 = Get-TestHash (Join-Path $repo "build/generated/release_identity.generated.h")
        }
        productionInputs = $inputRecords
        productionInputSetSha256 = Get-TestTextHash $inputIdentity
    }
    $provenance | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (
        Join-Path $repo "build/bin/Release/FF7RPianoSongs.provenance.json") -Encoding utf8

    return [pscustomobject]@{
        repo = $repo
        game = $game
        state = $state
        win64 = $win64
        oldAsi = $oldAsi
        newAsi = $newAsi
        developmentAsi = Join-Path $repo "dist/FF7RPianoSongs.asi"
        oldHash = Get-TestHash $oldAsi
        newHash = $newHash
        ini = $ini
        iniHash = Get-TestHash $ini
        song = $song
        songHash = Get-TestHash $song
        sidecar = $sidecar
        sidecarHash = Get-TestHash $sidecar
        log = Join-Path $win64 "FF7RPianoSongs.log"
        logHash = Get-TestHash (Join-Path $win64 "FF7RPianoSongs.log")
        sentinelRecords = $sentinelRecords
    }
}

function Invoke-Gate([hashtable]$Arguments, [switch]$ExpectFailure) {
    $failed = $false
    $result = $null
    try {
        if (!$Arguments.ContainsKey("TestNonce")) {
            $Arguments.TestNonce = $script:TestNonce
        }
        $result = & $RuntimeGateScript @Arguments -Confirm:$false
    }
    catch {
        $failed = $true
        if (!$ExpectFailure) {
            throw
        }
    }
    if ($ExpectFailure -and !$failed) {
        throw "runtime_gate_selftest: expected action to fail"
    }
    if (!$ExpectFailure) {
        return @($result)[-1]
    }
}

function Invoke-GateHardExit([hashtable]$Arguments, [string]$Stage) {
    if (!$Arguments.ContainsKey("TestNonce")) {
        $Arguments.TestNonce = $script:TestNonce
    }
    $start = [System.Diagnostics.ProcessStartInfo]::new()
    $start.FileName = (Get-Command pwsh -ErrorAction Stop).Source
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.ArgumentList.Add("-NoProfile")
    $start.ArgumentList.Add("-File")
    $start.ArgumentList.Add($RuntimeGateScript)
    foreach ($entry in $Arguments.GetEnumerator()) {
        $start.ArgumentList.Add("-$($entry.Key)")
        $start.ArgumentList.Add([string]$entry.Value)
    }
    $start.Environment["FF7RP_RUNTIME_GATE_TEST_HARD_EXIT"] = $Stage
    $process = [System.Diagnostics.Process]::Start($start)
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    if ($process.ExitCode -ne 197) {
        throw "Hard-exit seam '$Stage' returned $($process.ExitCode). stdout=$stdout stderr=$stderr"
    }
}

function Get-OnlyManifest([string]$StateRoot) {
    $manifests = @(Get-ChildItem -LiteralPath $StateRoot -Recurse -Filter "session.json" -File)
    Assert-True ($manifests.Count -eq 1) "expected exactly one session manifest under $StateRoot"
    return $manifests[0].FullName
}

function Assert-RollbackOutcome([object]$Fixture, [string]$ManifestPath) {
    Assert-True ((Get-TestHash $Fixture.oldAsi) -eq $Fixture.oldHash) "rollback did not restore the exact prior ASI"
    Assert-True ((Get-TestHash $Fixture.log) -eq $Fixture.logHash) "rollback did not restore the exact archived log"
    $manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
    foreach ($path in @(
        [string]$manifest.paths.stagingAsi,
        [string]$manifest.paths.previousAsi,
        [string]$manifest.paths.failedAsi,
        "$($manifest.paths.targetAsi).$($manifest.sessionId).restore",
        "$($manifest.paths.log).$($manifest.sessionId).restore",
        "$($manifest.paths.log).$($manifest.sessionId).failed")) {
        Assert-True (!(Test-Path -LiteralPath $path)) "rollback left transaction file $path"
    }
    Assert-Sentinels $Fixture
}

function Prepare([object]$Fixture, [string]$SongId = "test-song") {
    return Invoke-Gate @{
        Action = "Prepare"
        GameRoot = $Fixture.game
        RepositoryRoot = $Fixture.repo
        PackageRoot = (Join-Path $Fixture.repo "package")
        StateDirectory = $Fixture.state
        SongId = $SongId
    }
}

function Prepare-Development([object]$Fixture, [string]$SongId = "test-song") {
    return Invoke-Gate @{
        Action = "Prepare"
        Mode = "Development"
        GameRoot = $Fixture.game
        RepositoryRoot = $Fixture.repo
        StateDirectory = $Fixture.state
        SongId = $SongId
    }
}

function Prepare-PositionalPackaged([object]$Fixture, [string]$SongId = "test-song") {
    return & $RuntimeGateScript `
        "Prepare" `
        $Fixture.game `
        (Join-Path $Fixture.repo "package") `
        $Fixture.repo `
        $Fixture.state `
        "" `
        $SongId `
        "loader-valid-sidecar" `
        $script:TestNonce `
        "Keep"
}

function Assert-Sentinels([object]$Fixture) {
    Assert-True ((Get-TestHash $Fixture.ini) -eq $Fixture.iniHash) "INI changed"
    Assert-True ((Get-TestHash $Fixture.song) -eq $Fixture.songHash) "song input changed"
    Assert-True ((Get-TestHash $Fixture.sidecar) -eq $Fixture.sidecarHash) "cache sidecar changed"
    foreach ($sentinel in $Fixture.sentinelRecords) {
        Assert-True ((Test-Path -LiteralPath $sentinel.path -PathType Leaf) -and
            (Get-TestHash $sentinel.path) -eq $sentinel.sha256) "preservation sentinel changed: $($sentinel.path)"
    }
}

$temp = Join-Path ([System.IO.Path]::GetTempPath()) ("ff7rp-runtime-gate-selftest-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $temp -Force | Out-Null
$script:TestNonce = [guid]::NewGuid().ToString("N")
$env:FF7RP_RUNTIME_GATE_TEST_NONCE = $script:TestNonce
$env:FF7RP_RUNTIME_GATE_TEST_ROOT = $temp
$env:FF7RP_RUNTIME_GATE_TEST_OWNER_ROOT = Join-Path $temp "owners"
$env:FF7RP_RUNTIME_GATE_TEST_PROCESS = "NotRunning"
$env:FF7RP_RUNTIME_GATE_TEST_FAILURE = ""
$env:FF7RP_RUNTIME_GATE_TEST_HARD_EXIT = ""

try {
    # Update, evidence collection, overlapping-session refusal, and rollback.
    $fixture = New-Fixture (Join-Path $temp "update")
    $manifestPath = Prepare $fixture
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    Assert-True ($manifest.state -eq "AwaitingUserRun") "prepare state is not AwaitingUserRun"
    Assert-True ($manifest.mode -eq "Packaged") "default Prepare did not retain packaged compatibility"
    Assert-True ($manifest.artifact.mode -eq "Packaged") "default artifact mode is not packaged"
    Assert-True ($manifest.artifact.sourcePath -eq $fixture.newAsi) "default Prepare did not select the packaged ASI"
    Assert-True ((Get-TestHash $fixture.oldAsi) -eq $fixture.newHash) "reviewed ASI was not installed"
    Assert-True ((Get-TestHash $manifest.paths.previousAsi) -eq $fixture.oldHash) "previous ASI was not retained"
    Assert-True ((Get-TestHash $manifest.priorAsi.backupPath) -eq $fixture.oldHash) "session backup is invalid"
    Assert-True (!(Test-Path -LiteralPath $fixture.log)) "prepare left the stale live log in place"
    Assert-True ((Get-Content -LiteralPath (Join-Path (Split-Path -Parent $manifestPath) "log-before.log") -Raw) -eq "historical-log") `
        "prepare did not preserve the prior log"
    Invoke-Gate @{
        Action = "Prepare"; GameRoot = $fixture.game; RepositoryRoot = $fixture.repo
        PackageRoot = (Join-Path $fixture.repo "package"); StateDirectory = (Join-Path $temp "alternate-state")
    } -ExpectFailure
    Copy-Item -LiteralPath (Join-Path (Split-Path -Parent $manifestPath) "log-before.log") -Destination $fixture.log
    Invoke-Gate @{ Action = "Collect"; Session = $manifestPath } -ExpectFailure
    Remove-Item -LiteralPath $fixture.log -Force
    $logLines = @(
        '2026-01-01 [info] [startup] product=FF7RPianoSongs version=test',
        '2026-01-01 [info] [song] status=registry_ready count=1',
        '2026-01-01 [info] [audio_sead] status=prepared_playsetup_route',
        '2026-01-01 [info] [hooks] status=installed_release_hooks',
        '2026-01-01 [info] [startup] status=initialized mode=core_skeleton'
    )
    $logLines += @(1..40 | ForEach-Object {
        '2026-01-01 [info] [runtime] boundary=' + $_ + ' payload=' + ('é' * 5000)
    })
    $logLines += @(
        ('2026-01-01 [info] [runtime] path="C:\Users\private\' + ('x' * 5000) + '"'),
        '2026-01-01 [info] [audio_sead] status=native_mabf_capture_open_failed path=C:/Users/Private User/AppData/Local/FF7RPianoSongs/capture.mabf.bin result=failed',
        '2026-01-01 [info] [audio_sead] status=native_mabf_capture_open_failed path="D:\Private Folder\capture.mabf.bin", diagnostic follows',
        '2026-01-01 [info] [runtime] source_sound=0x1 class=0x2 package=0x3 bgm=0x4 route_sound=0x5 sidecar=0x6 manager=0x7 route_controller=0x8 allocation=0x9 mabf=0xa owner=0xb ptr=0xc',
        '2026-01-01 [info] [audio_sead_sidecar] id="test-song" status=ready mabf_size=0x1234 ptr=0x1234567812345678'
    )
    $logLines | Set-Content -LiteralPath $fixture.log -Encoding utf8
    Invoke-Gate @{ Action = "Collect"; Session = $manifestPath } | Out-Null
    $evidence = Get-Content -LiteralPath (Join-Path (Split-Path -Parent $manifestPath) "evidence.json") -Raw | ConvertFrom-Json
    Assert-True ($evidence.assessment -eq "Passed") "valid-sidecar evidence did not pass"
    Assert-True ($evidence.startupOrdering -eq "Legacy" -and $evidence.legacyStartupMarkers) `
        "legacy startup ordering was not identified"
    $excerpt = Get-Content -LiteralPath $evidence.excerptPath -Raw
    Assert-True ($excerpt -match 'ptr=<redacted>') "pointer was not redacted"
    Assert-True ($excerpt -match 'owner=<redacted>') "owner address was not redacted"
    foreach ($pointerField in @(
        "source_sound", "class", "package", "bgm", "route_sound", "sidecar", "manager",
        "route_controller", "allocation", "mabf")) {
        Assert-True ($excerpt -match "\b$pointerField=<redacted>") "$pointerField pointer was not redacted"
        Assert-True ($excerpt -notmatch "\b$pointerField=0x") "$pointerField pointer value leaked"
    }
    Assert-True ($excerpt -match 'mabf_size=0x1234') "safe scalar hex field was unexpectedly redacted"
    Assert-True ($excerpt -notmatch 'C:\\Users\\private') "personal path was not redacted"
    Assert-True ($excerpt -notmatch 'C:/Users/Private User') "unquoted generic-string path was not redacted"
    Assert-True ($excerpt -match 'path="<redacted-path>" result=failed') "production unquoted path did not exercise complete redaction"
    Assert-True ($excerpt -match 'path="<redacted-path>", diagnostic follows') "quoted path followed by free text was not redacted"
    $excerptBytes = [System.IO.File]::ReadAllBytes([string]$evidence.excerptPath)
    Assert-True ($excerptBytes.Length -le 65536) "excerpt exceeds the exact documented byte cap"
    Assert-True ($excerptBytes.Length -gt 0 -and $excerptBytes[-1] -eq 10) "excerpt lacks its deterministic LF terminator"
    Assert-True ([Array]::IndexOf($excerptBytes, [byte]13) -lt 0) "excerpt contains non-deterministic CRLF newlines"
    Assert-True (!($excerptBytes.Length -ge 3 -and $excerptBytes[0] -eq 0xEF -and
        $excerptBytes[1] -eq 0xBB -and $excerptBytes[2] -eq 0xBF)) "excerpt contains a UTF-8 BOM"
    $strictUtf8 = [System.Text.UTF8Encoding]::new($false, $true)
    $null = $strictUtf8.GetString($excerptBytes)
    Invoke-Gate @{ Action = "Collect"; Session = $manifestPath } -ExpectFailure
    Invoke-Gate @{ Action = "Finalize"; Session = $manifestPath; Disposition = "Rollback" } | Out-Null
    Assert-RollbackOutcome $fixture $manifestPath

    # Progressive startup installs empty-capable hooks before repository settlement.
    $fixture = New-Fixture (Join-Path $temp "progressive-evidence")
    $manifestPath = Prepare $fixture
    @(
        '[startup] product=FF7RPianoSongs version=test',
        '[audio_sead] status=prepared_playsetup_route',
        '[hooks] status=installed_release_hooks',
        '[startup] status=initialized mode=core_skeleton',
        '[audio_sead_sidecar] id="test-song" status=ready mabf_size=0x1',
        '[song] status=repository_settled valid_count=1') |
        Set-Content -LiteralPath $fixture.log -Encoding utf8
    Invoke-Gate @{ Action = "Collect"; Session = $manifestPath } | Out-Null
    $evidence = Get-Content -LiteralPath (Join-Path (Split-Path -Parent $manifestPath) "evidence.json") -Raw | ConvertFrom-Json
    Assert-True ($evidence.assessment -eq "Passed" -and
        $evidence.startupOrdering -eq "Progressive" -and
        $evidence.progressiveStartupMarkers -and $evidence.repositorySettled) `
        "progressive startup ordering was not accepted"
    Invoke-Gate @{ Action = "Finalize"; Session = $manifestPath; Disposition = "Rollback" } | Out-Null
    Assert-RollbackOutcome $fixture $manifestPath

    foreach ($case in @(
        [pscustomobject]@{ Name="progressive-missing-sidecar"; Lines=@(
            '[startup] product=FF7RPianoSongs version=test','[audio_sead] status=prepared_playsetup_route',
            '[hooks] status=installed_release_hooks','[startup] status=initialized mode=core_skeleton',
            '[song] status=repository_settled valid_count=1') },
        [pscustomobject]@{ Name="progressive-reordered"; Lines=@(
            '[startup] product=FF7RPianoSongs version=test','[audio_sead] status=prepared_playsetup_route',
            '[song] status=repository_settled valid_count=1','[hooks] status=installed_release_hooks',
            '[startup] status=initialized mode=core_skeleton',
            '[audio_sead_sidecar] id="test-song" status=ready mabf_size=0x1') },
        [pscustomobject]@{ Name="progressive-failed"; Lines=@(
            '[startup] product=FF7RPianoSongs version=test','[audio_sead] status=prepared_playsetup_route',
            '[hooks] status=installed_release_hooks','[startup] status=initialized mode=core_skeleton',
            '[audio_sead_sidecar] id="test-song" status=ready mabf_size=0x1',
            '[song] status=repository_failed valid_count=1') },
        [pscustomobject]@{ Name="progressive-missing-version"; Lines=@(
            '[audio_sead] status=prepared_playsetup_route','[hooks] status=installed_release_hooks',
            '[startup] status=initialized mode=core_skeleton',
            '[audio_sead_sidecar] id="test-song" status=ready mabf_size=0x1',
            '[song] status=repository_settled valid_count=1') }
    )) {
        $fixture = New-Fixture (Join-Path $temp $case.Name)
        $manifestPath = Prepare $fixture
        $case.Lines | Set-Content -LiteralPath $fixture.log -Encoding utf8
        Invoke-Gate @{ Action = "Collect"; Session = $manifestPath } | Out-Null
        $evidence = Get-Content -LiteralPath (Join-Path (Split-Path -Parent $manifestPath) "evidence.json") -Raw | ConvertFrom-Json
        Assert-True ($evidence.assessment -ne "Passed") "$($case.Name) evidence unexpectedly passed"
        Invoke-Gate @{ Action = "Finalize"; Session = $manifestPath; Disposition = "Rollback" } | Out-Null
        Assert-RollbackOutcome $fixture $manifestPath
    }

    # Existing positional callers retain the pre-Mode Packaged parameter order.
    $fixture = New-Fixture (Join-Path $temp "packaged-positional-compatibility")
    $manifestPath = Prepare-PositionalPackaged $fixture
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    Assert-True ($manifest.mode -eq "Packaged") "positional Prepare did not retain packaged compatibility"
    Invoke-Gate @{ Action = "Finalize"; Session = $manifestPath; Disposition = "Rollback" } | Out-Null
    Assert-RollbackOutcome $fixture $manifestPath

    # Previous v1 manifests omitted mode/source metadata and remain readable and recoverable.
    $fixture = New-Fixture (Join-Path $temp "legacy-manifest-compatibility")
    $manifestPath = Prepare $fixture
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $manifest.PSObject.Properties.Remove("mode")
    $manifest.artifact.PSObject.Properties.Remove("mode")
    $manifest.artifact.PSObject.Properties.Remove("sourcePath")
    $manifest | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $manifestPath -Encoding utf8
    $status = Invoke-Gate @{ Action = "Status"; Session = $manifestPath } | ConvertFrom-Json
    Assert-True ($status.mode -eq "Packaged") "legacy manifest did not default to packaged mode"
    Invoke-Gate @{ Action = "Finalize"; Session = $manifestPath; Disposition = "Rollback" } | Out-Null
    Assert-RollbackOutcome $fixture $manifestPath

    # Development mode validates exact Release provenance and dist identity without reading package/.
    $fixture = New-Fixture (Join-Path $temp "development")
    Remove-Item -LiteralPath (Join-Path $fixture.repo "package") -Recurse -Force
    $manifestPath = Prepare-Development $fixture
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    Assert-True ($manifest.mode -eq "Development") "development mode was not recorded"
    Assert-True ($manifest.artifact.mode -eq "Development") "development artifact mode was not recorded"
    Assert-True ([string]::IsNullOrEmpty([string]$manifest.artifact.packageAsi)) "development mode retained a package artifact"
    Assert-True ($manifest.artifact.sourcePath -eq $fixture.developmentAsi) "development mode did not select dist ASI"
    Assert-True ((Get-TestHash $fixture.oldAsi) -eq $fixture.newHash) "development ASI was not installed"
    $status = Invoke-Gate @{ Action = "Status"; Session = $manifestPath } | ConvertFrom-Json
    Assert-True ($status.mode -eq "Development") "status did not report development mode"
    Invoke-Gate @{ Action = "Status"; Session = $manifestPath; Mode = "Development" } -ExpectFailure
    Invoke-Gate @{ Action = "Finalize"; Session = $manifestPath; Disposition = "Rollback" } | Out-Null
    Assert-RollbackOutcome $fixture $manifestPath

    # Default packaged mode remains fail-closed when package/ is missing.
    $fixture = New-Fixture (Join-Path $temp "packaged-required")
    Remove-Item -LiteralPath (Join-Path $fixture.repo "package") -Recurse -Force
    Invoke-Gate @{
        Action = "Prepare"; GameRoot = $fixture.game; RepositoryRoot = $fixture.repo
        StateDirectory = $fixture.state
    } -ExpectFailure
    Assert-True ((Get-TestHash $fixture.oldAsi) -eq $fixture.oldHash) "missing package rejection mutated ASI"
    Assert-True ((Get-TestHash $fixture.log) -eq $fixture.logHash) "missing package rejection mutated log"

    # Development mode rejects stale production inputs and mismatched dist artifacts before mutation.
    $fixture = New-Fixture (Join-Path $temp "development-stale-provenance")
    Add-Content -LiteralPath (Join-Path $fixture.repo "CMakeLists.txt") -Value "drift" -NoNewline
    Invoke-Gate @{
        Action = "Prepare"; Mode = "Development"; GameRoot = $fixture.game
        RepositoryRoot = $fixture.repo; StateDirectory = $fixture.state
    } -ExpectFailure
    Assert-True ((Get-TestHash $fixture.oldAsi) -eq $fixture.oldHash) "stale development provenance mutated ASI"
    Assert-True ((Get-TestHash $fixture.log) -eq $fixture.logHash) "stale development provenance mutated log"

    $fixture = New-Fixture (Join-Path $temp "development-dist-mismatch")
    Add-Content -LiteralPath $fixture.developmentAsi -Value "tamper" -NoNewline
    Invoke-Gate @{
        Action = "Prepare"; Mode = "Development"; GameRoot = $fixture.game
        RepositoryRoot = $fixture.repo; StateDirectory = $fixture.state
    } -ExpectFailure
    Assert-True ((Get-TestHash $fixture.oldAsi) -eq $fixture.oldHash) "development artifact rejection mutated ASI"
    Assert-True ((Get-TestHash $fixture.log) -eq $fixture.logHash) "development artifact rejection mutated log"

    # The artifact declares which game build it was compiled for, and the gate verifies exactly
    # that build instead of accepting any catalogued executable that happens to match.
    foreach ($buildCase in @(
        @{ Name = "wrong-game-build"; CatalogId = "fixture-other-build" },
        @{ Name = "undeclared-game-build"; CatalogId = "fixture-unknown-build" })) {
        $fixture = New-Fixture (Join-Path $temp $buildCase.Name)
        $provenancePath = Join-Path $fixture.repo "build/bin/Release/FF7RPianoSongs.provenance.json"
        $record = Get-Content -LiteralPath $provenancePath -Raw | ConvertFrom-Json
        $record.releaseIdentity.catalogId = $buildCase.CatalogId
        $record | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $provenancePath -Encoding utf8
        Invoke-Gate @{
            Action = "Prepare"; GameRoot = $fixture.game; RepositoryRoot = $fixture.repo
            PackageRoot = (Join-Path $fixture.repo "package"); StateDirectory = $fixture.state
        } -ExpectFailure
        Assert-True ((Get-TestHash $fixture.oldAsi) -eq $fixture.oldHash) `
            "$($buildCase.Name) rejection mutated the ASI"
        Assert-True ((Get-TestHash $fixture.log) -eq $fixture.logHash) `
            "$($buildCase.Name) rejection mutated the log"
    }

    # The fixed receipt is durable before the first ASI/log mutation. A hard exit at that exact
    # boundary blocks every other state root and leaves the session recoverable.
    $fixture = New-Fixture (Join-Path $temp "ownership-boundary")
    Invoke-GateHardExit @{
        Action = "Prepare"; GameRoot = $fixture.game; RepositoryRoot = $fixture.repo
        PackageRoot = (Join-Path $fixture.repo "package"); StateDirectory = $fixture.state
    } "AfterOwnershipReceipt"
    $manifestPath = Get-OnlyManifest $fixture.state
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    Assert-True (Test-Path -LiteralPath $manifest.paths.ownership -PathType Leaf) "ownership receipt was not durable at the hard-exit boundary"
    Assert-True ((Get-TestHash $fixture.oldAsi) -eq $fixture.oldHash) "ownership-boundary exit mutated the ASI"
    Assert-True ((Get-TestHash $fixture.log) -eq $fixture.logHash) "ownership-boundary exit mutated the log"
    Invoke-Gate @{
        Action = "Prepare"; GameRoot = $fixture.game; RepositoryRoot = $fixture.repo
        PackageRoot = (Join-Path $fixture.repo "package"); StateDirectory = (Join-Path $temp "ownership-boundary-alternate-state")
    } -ExpectFailure
    Invoke-Gate @{ Action = "Recover"; Session = $manifestPath } | Out-Null
    Assert-RollbackOutcome $fixture $manifestPath
    Assert-True (!(Test-Path -LiteralPath $manifest.paths.ownership)) "recovery left the ownership receipt"

    # Hard exits immediately after rollback replacement and immediately before archived-log
    # restoration are both idempotently recoverable.
    foreach ($stage in @("AfterRollbackReplacement", "BeforePriorLogRestore")) {
        $fixture = New-Fixture (Join-Path $temp ("hard-rollback-" + $stage))
        $manifestPath = Prepare $fixture
        Set-Content -LiteralPath $fixture.log -Value "current-session-log-$stage" -Encoding utf8 -NoNewline
        $currentLogHash = Get-TestHash $fixture.log
        Invoke-GateHardExit @{
            Action = "Finalize"; Session = $manifestPath; Disposition = "Rollback"
        } $stage
        $interrupted = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        Assert-True ((Get-TestHash $fixture.oldAsi) -eq $fixture.oldHash) "$stage did not complete exact ASI replacement before exit"
        Assert-True ((Get-TestHash $fixture.log) -eq $currentLogHash) "$stage restored the archived log before its declared boundary"
        if ($stage -eq "AfterRollbackReplacement") {
            Assert-True (Test-Path -LiteralPath $interrupted.paths.failedAsi -PathType Leaf) "replacement seam did not leave the expected recoverable .failed sibling"
        }
        Invoke-Gate @{ Action = "Recover"; Session = $manifestPath } | Out-Null
        Assert-RollbackOutcome $fixture $manifestPath
        Assert-True (!(Test-Path -LiteralPath $interrupted.paths.ownership)) "$stage recovery left the ownership receipt"
    }

    # Recover on an already-terminal rollback verifies and repairs transaction cleanup instead of
    # returning blindly.
    $fixture = New-Fixture (Join-Path $temp "terminal-recover")
    $manifestPath = Prepare $fixture
    Invoke-Gate @{ Action = "Finalize"; Session = $manifestPath; Disposition = "Rollback" } | Out-Null
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    Copy-Item -LiteralPath $fixture.newAsi -Destination $manifest.paths.stagingAsi
    Copy-Item -LiteralPath $fixture.newAsi -Destination $manifest.paths.failedAsi
    Copy-Item -LiteralPath $fixture.oldAsi -Destination $manifest.paths.previousAsi
    Remove-Item -LiteralPath $fixture.log -Force
    Invoke-Gate @{ Action = "Recover"; Session = $manifestPath } | Out-Null
    Assert-RollbackOutcome $fixture $manifestPath

    # Clean install and uninstall.
    $fixture = New-Fixture (Join-Path $temp "clean")
    Remove-Item -LiteralPath $fixture.oldAsi -Force
    $manifestPath = Prepare $fixture ""
    Assert-True ((Get-TestHash $fixture.oldAsi) -eq $fixture.newHash) "clean install failed"
    Invoke-Gate @{ Action = "Finalize"; Session = $manifestPath; Disposition = "Uninstall" } | Out-Null
    Assert-True (!(Test-Path -LiteralPath $fixture.oldAsi)) "uninstall left the installed ASI"
    Assert-Sentinels $fixture

    # Running and process-query failures fail before mutation.
    $fixture = New-Fixture (Join-Path $temp "process")
    $env:FF7RP_RUNTIME_GATE_TEST_PROCESS = "Running"
    Invoke-Gate @{
        Action = "Prepare"; GameRoot = $fixture.game; RepositoryRoot = $fixture.repo
        PackageRoot = (Join-Path $fixture.repo "package"); StateDirectory = $fixture.state
    } -ExpectFailure
    Invoke-Gate @{
        Action = "Prepare"; Mode = "Development"; GameRoot = $fixture.game
        RepositoryRoot = $fixture.repo; StateDirectory = $fixture.state
    } -ExpectFailure
    Assert-True ((Get-TestHash $fixture.oldAsi) -eq $fixture.oldHash) "running-process rejection mutated ASI"
    $env:FF7RP_RUNTIME_GATE_TEST_PROCESS = "QueryFailure"
    Invoke-Gate @{
        Action = "Prepare"; GameRoot = $fixture.game; RepositoryRoot = $fixture.repo
        PackageRoot = (Join-Path $fixture.repo "package"); StateDirectory = $fixture.state
    } -ExpectFailure
    Invoke-Gate @{
        Action = "Prepare"; Mode = "Development"; GameRoot = $fixture.game
        RepositoryRoot = $fixture.repo; StateDirectory = $fixture.state
    } -ExpectFailure
    Assert-True ((Get-TestHash $fixture.oldAsi) -eq $fixture.oldHash) "query failure mutated ASI"
    $env:FF7RP_RUNTIME_GATE_TEST_PROCESS = "NotRunning"

    # Process overrides are ignored unless both the nonce and fixture-root containment authorize them.
    $fixture = New-Fixture (Join-Path $temp "process-authorization")
    $env:FF7RP_RUNTIME_GATE_TEST_PROCESS = "QueryFailure"
    try {
        & $RuntimeGateScript -Action Prepare -GameRoot $fixture.game -RepositoryRoot $fixture.repo `
            -PackageRoot (Join-Path $fixture.repo "package") -StateDirectory $fixture.state `
            -TestNonce "wrong-$script:TestNonce" -WhatIf -Confirm:$false | Out-Null
    }
    catch {
        Assert-True ($_.Exception.Message -ne "Unable to establish process state; failing closed.") `
            "wrong nonce activated the synthetic process-query response"
    }
    Assert-True ((Get-TestHash $fixture.oldAsi) -eq $fixture.oldHash) "wrong-nonce process override changed the ASI"
    $authorizedRoot = $env:FF7RP_RUNTIME_GATE_TEST_ROOT
    try {
        $env:FF7RP_RUNTIME_GATE_TEST_ROOT = Join-Path $temp "unrelated-authorized-root"
        try {
            & $RuntimeGateScript -Action Prepare -GameRoot $fixture.game -RepositoryRoot $fixture.repo `
                -PackageRoot (Join-Path $fixture.repo "package") -StateDirectory $fixture.state `
                -TestNonce $script:TestNonce -WhatIf -Confirm:$false | Out-Null
        }
        catch {
            Assert-True ($_.Exception.Message -ne "Unable to establish process state; failing closed.") `
                "out-of-root context activated the synthetic process-query response"
        }
    }
    finally {
        $env:FF7RP_RUNTIME_GATE_TEST_ROOT = $authorizedRoot
    }
    Assert-True ((Get-TestHash $fixture.oldAsi) -eq $fixture.oldHash) "out-of-root process override changed the ASI"
    $env:FF7RP_RUNTIME_GATE_TEST_PROCESS = "NotRunning"

    # Package identity mismatch fails before mutation.
    $fixture = New-Fixture (Join-Path $temp "identity")
    Add-Content -LiteralPath $fixture.newAsi -Value "tamper" -NoNewline
    Invoke-Gate @{
        Action = "Prepare"; GameRoot = $fixture.game; RepositoryRoot = $fixture.repo
        PackageRoot = (Join-Path $fixture.repo "package"); StateDirectory = $fixture.state
    } -ExpectFailure
    Assert-True ((Get-TestHash $fixture.oldAsi) -eq $fixture.oldHash) "identity rejection mutated ASI"

    # Failures around publication restore the exact previous artifact.
    foreach ($stage in @("AfterPublish")) {
        $fixture = New-Fixture (Join-Path $temp $stage)
        $env:FF7RP_RUNTIME_GATE_TEST_FAILURE = $stage
        Invoke-Gate @{
            Action = "Prepare"; GameRoot = $fixture.game; RepositoryRoot = $fixture.repo
            PackageRoot = (Join-Path $fixture.repo "package"); StateDirectory = $fixture.state
        } -ExpectFailure
        $env:FF7RP_RUNTIME_GATE_TEST_FAILURE = ""
        Assert-True ((Get-TestHash $fixture.oldAsi) -eq $fixture.oldHash) "$stage did not restore prior ASI"
        Assert-Sentinels $fixture
    }
    $fixture = New-Fixture (Join-Path $temp "AfterPublish-Development")
    $env:FF7RP_RUNTIME_GATE_TEST_FAILURE = "AfterPublish"
    Invoke-Gate @{
        Action = "Prepare"; Mode = "Development"; GameRoot = $fixture.game
        RepositoryRoot = $fixture.repo; StateDirectory = $fixture.state
    } -ExpectFailure
    $env:FF7RP_RUNTIME_GATE_TEST_FAILURE = ""
    Assert-True ((Get-TestHash $fixture.oldAsi) -eq $fixture.oldHash) "development AfterPublish did not restore prior ASI"
    Assert-Sentinels $fixture

    # Unknown installed content blocks rollback; Recover quarantines it and restores the prior ASI.
    $fixture = New-Fixture (Join-Path $temp "recovery")
    $manifestPath = Prepare $fixture
    [System.IO.File]::WriteAllBytes($fixture.oldAsi, [Text.Encoding]::UTF8.GetBytes("unknown-third-party-change"))
    Invoke-Gate @{ Action = "Finalize"; Session = $manifestPath; Disposition = "Rollback" } -ExpectFailure
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    Assert-True ($manifest.state -eq "RecoveryRequired") "unknown target did not require recovery"
    Invoke-Gate @{ Action = "Recover"; Session = $manifestPath } | Out-Null
    Assert-True ((Get-TestHash $fixture.oldAsi) -eq $fixture.oldHash) "recovery did not restore prior ASI"
    $quarantine = @(Get-ChildItem -LiteralPath (Split-Path -Parent $manifestPath) -Filter "unknown-installed-*.asi")
    Assert-True ($quarantine.Count -eq 1) "unknown installed ASI was not quarantined"

    # Interrupted Keep can recover from the durable session backup after previous-file cleanup.
    $fixture = New-Fixture (Join-Path $temp "keep-interruption")
    $manifestPath = Prepare $fixture
    @(
        '[startup] version=test',
        '[song] status=registry_ready count=1',
        '[audio_sead] status=prepared_playsetup_route',
        '[hooks] status=installed_release_hooks',
        '[startup] status=initialized mode=core_skeleton',
        '[audio_sead_sidecar] id="test-song" status=ready mabf_size=0x1') |
        Set-Content -LiteralPath $fixture.log -Encoding utf8
    Invoke-Gate @{ Action = "Collect"; Session = $manifestPath } | Out-Null
    $env:FF7RP_RUNTIME_GATE_TEST_FAILURE = "AfterPreviousDelete"
    Invoke-Gate @{ Action = "Finalize"; Session = $manifestPath; Disposition = "Keep" } -ExpectFailure
    $env:FF7RP_RUNTIME_GATE_TEST_FAILURE = ""
    Invoke-Gate @{ Action = "Recover"; Session = $manifestPath } | Out-Null
    Assert-True ((Get-TestHash $fixture.oldAsi) -eq $fixture.oldHash) "Keep interruption did not restore session backup"

    # Keep requires collected evidence, while rollback remains available before collection.
    $fixture = New-Fixture (Join-Path $temp "keep-without-evidence")
    $manifestPath = Prepare $fixture
    Invoke-Gate @{ Action = "Finalize"; Session = $manifestPath; Disposition = "Keep" } -ExpectFailure
    Invoke-Gate @{ Action = "Finalize"; Session = $manifestPath; Disposition = "Rollback" } | Out-Null

    # Development Keep uses the same immutable collection and acceptance path.
    $fixture = New-Fixture (Join-Path $temp "development-keep")
    $manifestPath = Prepare-Development $fixture
    @(
        '[startup] version=test',
        '[song] status=registry_ready count=1',
        '[audio_sead] status=prepared_playsetup_route',
        '[hooks] status=installed_release_hooks',
        '[startup] status=initialized mode=core_skeleton',
        '[audio_sead_sidecar] id="test-song" status=ready mabf_size=0x1') |
        Set-Content -LiteralPath $fixture.log -Encoding utf8
    Invoke-Gate @{ Action = "Collect"; Session = $manifestPath } | Out-Null
    Invoke-Gate @{ Action = "Finalize"; Session = $manifestPath; Disposition = "Keep" } | Out-Null
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    Assert-True ($manifest.state -eq "Accepted") "development Keep did not reach Accepted"
    Assert-True ((Get-TestHash $fixture.oldAsi) -eq $fixture.newHash) "development Keep changed the accepted ASI"
    Assert-Sentinels $fixture

    # WhatIf creates neither state nor ownership, and unsafe state placement is rejected.
    $fixture = New-Fixture (Join-Path $temp "whatif")
    Remove-Item -LiteralPath $fixture.state -Recurse -Force -ErrorAction SilentlyContinue
    & $RuntimeGateScript -Action Prepare -GameRoot $fixture.game -RepositoryRoot $fixture.repo `
        -PackageRoot (Join-Path $fixture.repo "package") -StateDirectory $fixture.state `
        -TestNonce $script:TestNonce -WhatIf -Confirm:$false | Out-Null
    Assert-True (!(Test-Path -LiteralPath $fixture.state)) "WhatIf created persistent state"
    Invoke-Gate @{
        Action = "Prepare"; GameRoot = $fixture.game; RepositoryRoot = $fixture.repo
        PackageRoot = (Join-Path $fixture.repo "package"); StateDirectory = (Join-Path $fixture.game "state")
    } -ExpectFailure

    # An abandoned OS-owned mutex is reacquired after its owning process exits.
    $fixture = New-Fixture (Join-Path $temp "abandoned-mutex")
    & pwsh -NoProfile -Command `
        '$m=[Threading.Mutex]::new($false,"Global\FF7RPianoSongs.RuntimeGate.v1");$null=$m.WaitOne();[Environment]::Exit(0)'
    Assert-True ($LASTEXITCODE -eq 0) "abandoned-mutex fixture failed"
    $manifestPath = Prepare $fixture
    Invoke-Gate @{ Action = "Finalize"; Session = $manifestPath; Disposition = "Rollback" } | Out-Null
}
finally {
    $env:FF7RP_RUNTIME_GATE_TEST_NONCE = $null
    $env:FF7RP_RUNTIME_GATE_TEST_ROOT = $null
    $env:FF7RP_RUNTIME_GATE_TEST_OWNER_ROOT = $null
    $env:FF7RP_RUNTIME_GATE_TEST_PROCESS = $null
    $env:FF7RP_RUNTIME_GATE_TEST_FAILURE = $null
    $env:FF7RP_RUNTIME_GATE_TEST_HARD_EXIT = $null
    if (Test-Path -LiteralPath $temp) {
        Remove-Item -LiteralPath $temp -Recurse -Force
    }
}

Write-Output "runtime_gate_selftest: ok"
